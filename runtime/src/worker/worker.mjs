// Engine host — module worker (architecture §4.1). Owns WASM, execution,
// and canvas measurement; the main thread only injects HTML. Progressive
// upgrade (v2 §9): semantic flow HTML is posted right after ingest (no
// measurement needed — the resolver already ran), the typeset result
// follows once the pull loop converges. Docs persist for relayout until
// disposed.
import createTypesetter from '../../../engine/build-wasm/typesetter.js';
import { execute } from './executor.mjs';
import { CanvasMeasurer } from './canvas_measure.mjs';
import { checkAbi, compiledOf, fragmentsOf } from '../shared/abi.mjs';
import { ResourceHost } from '../shared/resources/host.mjs';
import { canvasProviders } from '../shared/resources/providers/canvas.mjs';
import { tokenProvider } from '../shared/resources/providers/tokens.mjs';
import { hyphProvider } from '../shared/resources/providers/hyph.mjs';
import { imageProvider } from '../shared/resources/providers/images.mjs';
import { htmlBoxProvider } from '../shared/resources/providers/html-boxes.mjs';
import { decodeRequest, encodeAnswer } from '../shared/rescodec.mjs';
import { POLICY } from '../shared/settings.gen.mjs';

// host policy (schema "policy"): createEngine({policy}) overrides it
const policy = { ...POLICY };

let modPromise = null;
const getMod = () => (modPromise ??= createTypesetter().then((M) => { checkAbi(M); return M; }));

// Editing sessions (editor-design.md §2) re-typeset the whole document per
// keystroke. Answers persist across documents in the engine's Session (plan
// P1-21; content-keyed widths, vertical metrics, code tokens and the KP
// memo): a new document asks only for what no earlier one was answered, so
// the measurer keeps no cache of its own (a round's rows are already unique).
// (plan P3-21; design T9 A2) one resource host per worker: the providers
// (canvas widths and metrics, code tokens, hyphenation patterns — plan
// P4-06 —, image boxes), one LRU cache of URL-keyed answers; each document
// a job with its own locator (the page's base) and manifest
const host = new ResourceHost({ policy });
{
  const canvas = canvasProviders(new CanvasMeasurer());
  host.register('textWidth', canvas.textWidth).register('fontVmet', canvas.fontVmet)
    .register('codeTokens', tokenProvider)
    .register('hyphPatterns', hyphProvider())
    .register('boxInfo', imageProvider({ get timeoutMs() { return policy.imageTimeoutMs; } }))
    .register('boxInfo', htmlBoxProvider({ get timeoutMs() { return policy.imageTimeoutMs; } }));
}
const jobOf = (baseUrl) => host.job({ bases: { doc: baseUrl ?? self.location.href } });
let session = 0;
function sessionOf(M) {
  if (!session) {
    const p = M.stringToNewUTF8(JSON.stringify({ budgetBytes: policy.sessionBudgetBytes }));
    session = M._tsr2_session_new(p);
    M._free(p);
  }
  return session;
}

// the faces this worker has loaded, as the engine keys widths by them
// (host.loadedFaces): a font landing changes the metric key, so nothing
// measured against its fallback is reused
function loadedFaces() {
  const out = [];
  for (const [key, st] of fontState) {
    if (!st.loaded) continue;
    const [family, weight, style, src] = key.split('|');
    let h = 2166136261;
    for (let i = 0; i < src.length; i++) h = Math.imul(h ^ src.charCodeAt(i), 16777619) >>> 0;
    out.push(`${family}|${weight}|${style}|${h.toString(16)}`);
  }
  return out.join('\n');
}

// W (pages-design.md §1): fonts are DECLARED, not discovered — the worker
// loads them into its own FontFaceSet before measuring, so metrics are
// right on the first pass and no settle re-typeset can exist. A font that
// misses the 4s deadline measures as its fallback until it lands; landing
// changes later documents' metric keys (host.loadedFaces). A face counts as
// loaded only on success; a failed one is retried after policy.fontRetryMs.
const fontState = new Map(); // key → {loaded} | {loading: Promise} | {failedAt}
function loadFont(key, f) {
  const st = { loading: null };
  fontState.set(key, st);  // first: a synchronous failure below replaces it
  st.loading = (async () => {
    try {
      const ff = new FontFace(f.family, `url(${JSON.stringify(String(f.src))})`, {
        weight: f.weight ?? 'normal', style: f.style ?? 'normal',
      });
      await ff.load();
      self.fonts.add(ff);
      fontState.set(key, { loaded: true });  // later documents key their widths by it
    } catch (e) {
      fontState.set(key, { failedAt: performance.now() });
      console.warn(`tsr: font failed to load: ${f.family}`, e);
    }
  })();
  return st.loading;
}
async function loadFonts(fonts) {
  const wait = [];
  for (const f of fonts ?? []) {
    if (!f.family || !f.src) continue;
    const key = `${f.family}|${f.weight ?? 'normal'}|${f.style ?? 'normal'}|${f.src}`;
    const st = fontState.get(key);
    if (st?.loaded) continue;
    if (st?.loading) wait.push(st.loading);
    else if (!st || performance.now() - st.failedAt > policy.fontRetryMs) wait.push(loadFont(key, f));
  }
  if (!wait.length) return;
  await Promise.race([Promise.allSettled(wait),
                      new Promise((r) => setTimeout(r, policy.fontDeadlineMs))]);
}

// Main-thread capabilities (plan P3-06; design T9 "capability"): work only
// the main thread can do, asked by name over one RPC with per-request ids
// (cap? → cap). createEngine({capabilities}) supplies them; 'imageDims' is
// built in: cross-origin images without CORS headers cannot be read in a
// worker (opaque responses), but an <img> there still yields their size.
const capCalls = new Map(); // rid → { resolve, reject }
let nextRid = 1;
function askCapability(name, args, timeoutMs) {
  return new Promise((resolve, reject) => {
    const rid = nextRid++;
    capCalls.set(rid, { resolve, reject });
    postMessage({ type: 'cap?', rid, name, args });
    setTimeout(() => {
      if (capCalls.delete(rid)) reject(new Error(`capability ${name}: timed out`));
    }, timeoutMs);
  });
}
// one request batch, copied out of wasm memory ([u32 length][TSRQ …])
function takeRequests(M, doc, kinds = 0) {
  const p = M._tsr2_requests(doc, kinds);
  const len = new DataView(M.HEAPU8.buffer).getUint32(p, true);
  return decodeRequest(M.HEAPU8.slice(p + 4, p + 4 + len));
}
function provideAnswer(M, doc, ans) {
  const bytes = encodeAnswer(ans);
  const p = M._malloc(bytes.length);
  M.HEAPU8.set(bytes, p);
  M._tsr2_provide(doc, p, bytes.length);
  M._free(p);
}
// timings (bench-edit.mjs): engineMs = wasm typeset passes (emit + KP +
// layout), the rest is the provider side of the pull loop
// `stale()` is the job's generation check: polled after every await, a
// superseded job stops (returns false) instead of finishing stale work.
// `scope`: the document's id — where the main thread measures its host
// boxes (plan P3-28)
async function measureLoop(M, doc, { tm = {}, baseUrl, job = jobOf(baseUrl), stale = () => false, scope } = {}) {
  const mark = (k, t0) => { tm[k] = (tm[k] ?? 0) + performance.now() - t0; };
  for (let round = 0; round < policy.maxRounds; round++) {
    if (stale()) return false;
    tm.rounds = round + 1;
    let t0 = performance.now();
    const done = M._tsr_typeset(doc) === 0;
    mark('engineMs', t0);
    if (done) return true;
    // the resource pull (plan P1-19): one binary batch out, one answer in
    t0 = performance.now();
    const req = takeRequests(M, doc);
    mark('requestMs', t0);
    // not done, and nothing to ask: the engine has stalled (never loop)
    if (!Object.values(req.kinds).some((rows) => rows.length))
      throw new Error('typeset stalled: the engine asked for nothing');
    // (plan P3-21) the batch to the resource host: each kind's rows to its
    // provider (image boxes in parallel, a capability for what the worker
    // cannot read)
    t0 = performance.now();
    const ans = await job.answer(req, { stale, tm, capability: askCapability, scope });
    if (!ans || stale()) return false;
    mark('providersMs', t0);
    t0 = performance.now();
    provideAnswer(M, doc, ans);
    mark('requestMs', t0);
  }
  throw new Error('typeset did not converge');
}

// the RenderResult frame (plan P3-05): every block's key, the bodies of the
// blocks the shell does not hold (`held`: its keys, 16 bytes each), the
// anchors and a generation; a single answer transfers its buffer
function postResult(M, doc, ids, tm, held) {
  const t0 = performance.now();
  const keys = held instanceof Uint8Array ? held : new Uint8Array(0);
  const kp = keys.length ? M._malloc(keys.length) : 0;
  if (kp) M.HEAPU8.set(keys, kp);
  const p = M._tsr2_render_result(doc, kp, keys.length / 16);
  if (kp) M._free(kp);
  // the head and table as bytes, the HTML decoded here (off the main thread)
  const dv = new DataView(M.HEAPU8.buffer);
  const len = dv.getUint32(p, true);
  const hl = dv.getUint32(p + 8, true);
  const tableEnd = 12 + hl + 56 * dv.getUint32(p + 12 + hl, true);  // (frame-relative: "TSRR", hl, head, n, table)
  const frame = M.HEAPU8.slice(p + 4, p + 4 + tableEnd).buffer;
  const html = M.UTF8ToString(p + 4 + tableEnd, len - tableEnd);
  if (tm) tm.renderMs = performance.now() - t0;
  const diags = M.UTF8ToString(M._tsr_diags(doc));
  const diagnostics = diagnosticsOf(M, doc);  // (plan P3-37) the same rows, as data
  const heightPx = M._tsr_doc_height_px(doc);
  const lang = docLangOf(M, doc);
  ids.forEach((id, k) => {
    const f = k === ids.length - 1 ? frame : frame.slice(0);
    postMessage({ type: 'result', id, frame: f, html, diags, diagnostics, heightPx, timings: tm, lang }, [f]);
  });
}
// (plan P3-31; design T9 A7) a document's declared inputs (inputs.def:
// labels, the other documents' manifests as one JSON array), before Ingest
function setInputs(M, doc, inputs) {
  for (const [name, value] of Object.entries(inputs ?? {})) {
    if (typeof value !== 'string') continue;
    const n = M.stringToNewUTF8(name);
    const bytes = new TextEncoder().encode(value);
    const p = M._malloc(bytes.length || 1);
    M.HEAPU8.set(bytes, p);
    M._tsr2_set_input(doc, n, p, bytes.length);
    M._free(p);
    M._free(n);
  }
}

// the host's inputs and the document's own ($.labels.import): one labels array
function mergeInputs(given, imported) {
  const out = { ...(given ?? {}) };
  if (imported?.labels?.length) {
    const inner = (out.labels ?? '').trim().replace(/^\[|\]$/g, '').trim();
    out.labels = `[${[inner, ...imported.labels].filter(Boolean).join(',')}]`;
  }
  return out;
}

// (plan P3-30, D-T06) the document's language as the engine decided it — its
// own, the host's or detected (doc.lang: auto): docinfo
// (plan P3-37; design T9 M9) a product through tsr2_get: u32 length + bytes
function productOf(M, doc, name) {
  const n = M.stringToNewUTF8(name);
  try {
    const p = M._tsr2_get(doc, n, 0);
    const len = new DataView(M.HEAPU8.buffer).getUint32(p, true);
    return new TextDecoder().decode(M.HEAPU8.subarray(p + 4, p + 4 + len));
  } finally {
    M._free(n);
  }
}
// the diagnostics as data: [{sev, code, span, message, origin, pid?}]
const diagnosticsOf = (M, doc) => JSON.parse(productOf(M, doc, 'diagnostics') || '[]');

function docLangOf(M, doc) {
  const p = M.stringToNewUTF8('docinfo');
  const out = M.UTF8ToString(M._tsr2_product(doc, p));
  M._free(p);
  try { return JSON.parse(out).lang ?? ''; } catch { return ''; }
}
const postError = (ids, message) => {
  for (const id of ids) postMessage({ type: 'error', id, message });
};

// Per-document mailbox (plan P0-11, defect #25). Messages for one docKey run
// strictly in order, so an older update can no longer install its document
// over (and free) a newer one, and paginate's width round trip cannot
// interleave with a relayout. A newer update (or relayout) supersedes the
// running one of its kind: the running job polls its generation after every
// await and stops; queued jobs of the same kind coalesce. Superseded
// requests are answered with the newer job's result.
const sessions = new Map(); // docKey → { queue, running, gen, doc }
const MERGES = new Set(['update', 'relayout']);

function enqueue(key, job) {
  let s = sessions.get(key);
  if (!s) sessions.set(key, (s = { key, queue: [], running: null, gen: 0, doc: undefined }));
  if (s.running && (job.kind === 'dispose' ||
                    (MERGES.has(job.kind) && s.running.kind === job.kind))) s.gen++;
  const last = s.queue[s.queue.length - 1];
  if (last && MERGES.has(job.kind) && last.kind === job.kind) {
    job.ids = [...last.ids, ...job.ids];
    s.queue[s.queue.length - 1] = job;
  } else {
    s.queue.push(job);
  }
  if (!s.running) pump(key, s);
}

async function pump(key, s) {
  await providersReady;
  while (s.queue.length) {
    const job = s.queue.shift();
    s.running = job;
    const gen = s.gen;
    const stale = () => s.gen !== gen;
    let done = true;
    try {
      done = await RUN[job.kind](s, job, stale);
    } catch (e) {
      postError(job.ids, String(e?.stack || e));
    }
    s.running = null;
    if (done === false) {  // superseded: the next job of its kind answers too
      const next = s.queue.find((j) => j.kind === job.kind);
      if (next) next.ids = [...job.ids, ...next.ids];
      else postError(job.ids, `${job.kind}: superseded`);
    }
  }
  if (s.disposed) sessions.delete(key);
}

// One turn of the worker's event loop (plan P0-11's mailbox, kept by P2-02):
// a newer message for the document can arrive and supersede the job before
// its expensive stages. Executing used to import a module on every update —
// a turn for free; a document without user code now imports nothing, so the
// job takes its turn explicitly (a MessagePort hop, not a clamped timer).
const turn = new MessageChannel();
const turnWaiters = [];
turn.port1.onmessage = () => turnWaiters.shift()?.();
const yieldTurn = () => new Promise((resolve) => {
  turnWaiters.push(resolve);
  turn.port2.postMessage(0);
});

// config + compile + execute + ingest + measure a fresh doc; it replaces the
// session's doc only on success, so a failing edit keeps the last good
// document alive for relayout/paginate
async function runTypeset(s, { ids, msg }, stale) {
  // one settings document (plan P1-03): what to typeset; fontFaces: which
  // declared webfaces to load before measuring
  const { source, settings, progressive, fontFaces, baseUrl, inputs } = msg;
  const tm = {};
  const mark = (k, t0) => { tm[k] = performance.now() - t0; };
  const M = await getMod();
  await loadFonts(fontFaces);
  if (stale()) return false;
  const doc = M._tsr_doc_new();
  M._tsr2_doc_attach(doc, sessionOf(M));
  try {
    const host = { ...(settings?.host ?? {}), loadedFaces: loadedFaces() };
    const cfg = M.stringToNewUTF8(JSON.stringify({ ...(settings ?? {}), host }));
    M._tsr2_set_config(doc, cfg);
    M._free(cfg);
    let t0 = performance.now();
    const srcPtr = M.stringToNewUTF8(source);
    M._tsr_compile(doc, srcPtr);
    M._free(srcPtr);
    mark('compileMs', t0);

    t0 = performance.now();
    const job = jobOf(baseUrl);  // (plan P3-21) its loads and its needs: one locator, one manifest
    const imported = {};  // (plan P3-31) the inputs the document asked for ($.labels.import)
    const ops = await execute(compiledOf(M, doc), { host: job, parse: fragmentsOf(M), inputs: imported,
      // (plan P3-31; D-I08) dev mode: a second execution with a fresh job writes the same ops
      check: policy.checkExecution ? () => jobOf(baseUrl) : null });
    mark('executeMs', t0);
    await yieldTurn();  // (counted in the edit's total, not in executeMs)
    if (stale()) { M._tsr_doc_free(doc); return false; }
    setInputs(M, doc, mergeInputs(inputs, imported));  // (plan P3-31) its declared inputs, before Ingest
    t0 = performance.now();
    const opsPtr = M._malloc(ops.length);
    M.HEAPU8.set(ops, opsPtr);
    const ok = M._tsr_ingest(doc, opsPtr, ops.length) === 0;
    M._free(opsPtr);
    mark('ingestMs', t0);
    if (!ok) throw new Error('ops ingest failed: ' + M.UTF8ToString(M._tsr_diags(doc)));

    if (progressive !== false) {
      const html = M.UTF8ToString(M._tsr_render_semantic(doc));
      const lang = docLangOf(M, doc);  // (plan P3-30) the page's language from the first paint on
      for (const id of ids) postMessage({ type: 'semantic', id, html, lang });
    }

    if (!(await measureLoop(M, doc, { tm, job, stale, scope: s.key }))) {
      M._tsr_doc_free(doc);
      return false;
    }
    if (s.doc !== undefined) M._tsr_doc_free(s.doc);
    s.doc = doc;
    postResult(M, doc, ids, tm, msg.held);
    return true;
  } catch (e) {
    M._tsr_doc_free(doc);
    throw e;
  }
}

// a fork of the live doc with a settings patch (plan P1-03): the live doc's
// products are never mutated; metric/token/image answers carry over, so the
// pull loop exits fast. Null when the patch would need re-execution.
function forkDoc(M, doc, patch) {
  const p = M.stringToNewUTF8(JSON.stringify(patch));
  const f = M._tsr2_doc_fork(doc, p);
  M._free(p);
  return f || undefined;
}

// P1 (pages-design.md §2): sheets at the page measure from a fork — the
// live document stays as it is; `idPrefix` (plan P3-06): the sheets' own,
// so they never share an id with the live view beside them
async function runPaginate(s, { ids, msg }) {
  const { pageWidthPx, pageHeightPx, baseUrl, idPrefix } = msg;
  const M = await getMod();
  if (s.doc === undefined) return postError(ids, 'paginate: doc disposed');
  const doc = forkDoc(M, s.doc, { host: { width: pageWidthPx }, page: { height: pageHeightPx },
                                  ...(idPrefix ? { render: { idPrefix } } : {}) });
  if (doc === undefined) return postError(ids, 'paginate: cannot fork the document');
  try {
    await measureLoop(M, doc, { baseUrl, scope: s.key });
    const html = M.UTF8ToString(M._tsr_render_pages(doc, pageHeightPx));
    const diags = M.UTF8ToString(M._tsr_diags(doc));
    const diagnostics = diagnosticsOf(M, doc);
    for (const id of ids) postMessage({ type: 'result', id, html, diags, diagnostics, heightPx: 0 });
  } finally {
    M._tsr_doc_free(doc);
  }
}

// a width change re-enters Layout in place (plan P1-16: emit reads no
// width, so host.width affects only Layout and Paint): the settings patch
// applies to the live doc, which breaks and lays out again — no fork, no
// re-emit. A patch that would need more (never for host.width) rebuilds
// from the retained ops as before; a superseded relayout leaves the live
// doc at a width the next one overrides.
async function runRelayout(s, { ids, msg }, stale) {
  const M = await getMod();
  if (s.doc === undefined) return postError(ids, 'relayout: doc disposed');
  const patch = { host: { width: msg.widthPx } };
  const p = M.stringToNewUTF8(JSON.stringify(patch));
  const rc = M._tsr2_set_config(s.doc, p);
  M._free(p);
  if (rc === 0) {
    const tm = {};
    if (!(await measureLoop(M, s.doc, { tm, baseUrl: msg.baseUrl, stale, scope: s.key }))) return false;
    postResult(M, s.doc, ids, tm, msg.held);
    return;
  }
  const doc = forkDoc(M, s.doc, patch);
  if (doc === undefined) return postError(ids, 'relayout: cannot fork the document');
  let ok = false;
  try {
    ok = await measureLoop(M, doc, { baseUrl: msg.baseUrl, stale, scope: s.key });
    if (!ok) return false;
    M._tsr_doc_free(s.doc);
    s.doc = doc;
    postResult(M, doc, ids, undefined, msg.held);
  } finally {
    if (!ok) M._tsr_doc_free(doc);
  }
}

// (plan P3-06; design T7 ops.fragment) a preview of what a label names: the
// live doc's semantic HTML of it, stamped with the generation of its last
// result (the shell shows only content that matches its view)
async function runFragment(s, { ids, msg }) {
  const M = await getMod();
  if (s.doc === undefined) return postError(ids, 'fragment: doc disposed');
  const p = M.stringToNewUTF8(String(msg.label ?? ''));
  const r = JSON.parse(M.UTF8ToString(M._tsr2_render_fragment(s.doc, p)));
  M._free(p);
  for (const id of ids) postMessage({ type: 'result', id, html: r.html, generation: r.generation });
}

// the live doc's result again (plan P3-05): the shell found a key it did not
// hold (a stale frame) and asks holding nothing
async function runRender(s, { ids, msg }) {
  const M = await getMod();
  if (s.doc === undefined) return postError(ids, 'render: doc disposed');
  postResult(M, s.doc, ids, undefined, msg.held);
}

async function runDispose(s) {
  s.disposed = true;
  for (const j of s.queue.splice(0)) postError(j.ids, `${j.kind}: doc disposed`);
  if (s.doc === undefined) return;
  const M = await getMod();
  M._tsr_doc_free(s.doc);
  s.doc = undefined;
}

const RUN = { update: runTypeset, paginate: runPaginate, relayout: runRelayout,
              render: runRender, fragment: runFragment, dispose: runDispose };

// (plan P3-21) the host's providers: imported here, registered before the
// next job runs (jobs wait for them)
let providersReady = Promise.resolve();
onmessage = (ev) => {
  const m = ev.data;
  if (m?.type === 'policy') {  // createEngine({policy}): host policy overrides
    for (const [k, v] of Object.entries(m.policy ?? {})) if (k in policy) policy[k] = v;
    return;
  }
  if (m?.type === 'providers') {  // createEngine({providers}): resource providers by module
    providersReady = providersReady.then(() => Promise.all(m.providers.map(async ({ kind, module }) => {
      try {
        const mod = await import(/* a host's provider module */ module);
        host.register(kind, mod.default ?? mod);
      } catch (e) {
        console.warn(`tsr: provider ${kind} (${module}) failed to load`, e);
      }
    })));
    return;
  }
  if (m?.type === 'cap') {  // a capability's answer (askCapability)
    const c = capCalls.get(m.rid);
    if (c) {
      capCalls.delete(m.rid);
      if (m.error !== undefined) c.reject(new Error(m.error));
      else c.resolve(m.value);
    }
    return;
  }
  // 'typeset' opens a session under its own id; 'update' re-typesets an
  // editing session's doc under its docId
  if (m?.type === 'typeset') enqueue(m.id, { kind: 'update', ids: [m.id], msg: m });
  else if (m?.type === 'update') enqueue(m.docId, { kind: 'update', ids: [m.id], msg: m });
  else if (m?.type === 'paginate') enqueue(m.docId, { kind: 'paginate', ids: [m.id], msg: m });
  else if (m?.type === 'relayout') enqueue(m.docId, { kind: 'relayout', ids: [m.id], msg: m });
  else if (m?.type === 'render') enqueue(m.docId, { kind: 'render', ids: [m.id], msg: m });
  else if (m?.type === 'fragment') enqueue(m.docId, { kind: 'fragment', ids: [m.id], msg: m });
  else if (m?.type === 'dispose') enqueue(m.docId, { kind: 'dispose', ids: [], msg: m });
};
