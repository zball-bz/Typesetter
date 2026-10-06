// Engine host — module worker (architecture §4.1). Owns WASM, execution,
// and canvas measurement; the main thread only injects HTML. Progressive
// upgrade (v2 §9): semantic flow HTML is posted right after ingest (no
// measurement needed — the resolver already ran), the typeset result
// follows once the pull loop converges. Docs persist for relayout until
// disposed.
import createTypesetter from '../../../engine/build-wasm/typesetter.js';
import { execute } from './executor.mjs';
import { CanvasMeasurer } from './canvas_measure.mjs';
import { tokenize } from './tokens.mjs';
import { sniffImageSize } from './image_sniff.mjs';
import { checkAbi, compiledOf, fragmentsOf } from '../shared/abi.mjs';
import { decodeRequest, encodeAnswer } from '../shared/rescodec.mjs';
import { POLICY } from '../shared/settings.gen.mjs';

// host policy (schema "policy"): createEngine({policy}) overrides it
const policy = { ...POLICY };

let modPromise = null;
const getMod = () => (modPromise ??= createTypesetter().then((M) => { checkAbi(M); return M; }));

// NEED_IMAGES (figure-design.md §2): intrinsic CSS dims only, cached per
// absolute URL for the worker's lifetime (in-flight lookups shared);
// 0×0 = failure (engine placeholder + diag)
const imageDims = new Map(); // url → Promise<{w, h}>

// Editing sessions (editor-design.md §2) re-typeset the whole document per
// keystroke. Answers persist across documents in the engine's Session (plan
// P1-21; content-keyed widths, vertical metrics, code tokens and the KP
// memo): a new document asks only for what no earlier one was answered, so
// the measurer keeps no cache of its own (a round's rows are already unique).
const measurer = new CanvasMeasurer();
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

// cross-origin images without CORS headers cannot be read in a worker
// (opaque responses); the main thread can still learn their intrinsic
// size through an <img> element, so the worker asks it as a fallback.
// One request id per question (two docs may ask for one src at once).
const mainDims = new Map(); // rid → resolve
let nextRid = 1;
function askMainForDims(src) {
  return new Promise((resolve) => {
    const rid = nextRid++;
    mainDims.set(rid, resolve);
    postMessage({ type: 'image-dims?', rid, src });
    setTimeout(() => { if (mainDims.delete(rid)) resolve({ w: 0, h: 0 }); }, policy.imageTimeoutMs);
  });
}
// the header carries the size: read a prefix of the body, decode only
// formats the sniffer does not know (SVG, AVIF)
async function fetchImageSize(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  const chunks = [];
  let have = 0;
  if (res.body) {
    const reader = res.body.getReader();
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      have += value.length;
      const head = chunks.length === 1 ? value : concat(chunks, have);
      const r = sniffImageSize(head);
      if (r && !r.more) {
        reader.cancel().catch(() => {});
        if (r.w > 0 && r.h > 0) return r;
        break;
      }
      if (!r) break;  // unknown format: decode below
    }
    for (;;) {  // the rest of the body, for the decoder
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      have += value.length;
    }
  }
  const blob = res.body ? new Blob(chunks, { type: res.headers.get('content-type') ?? '' })
                        : await res.blob();
  const bm = await createImageBitmap(blob);
  const dims = { w: bm.width, h: bm.height };
  bm.close();
  return dims;
}
function concat(chunks, n) {
  const out = new Uint8Array(n);
  let at = 0;
  for (const c of chunks) { out.set(c, at); at += c.length; }
  return out;
}
function imageSize(src, baseUrl) {
  let url;
  try { url = new URL(src, baseUrl ?? self.location.href).href; } catch { url = src; }
  let p = imageDims.get(url);
  if (!p) {
    p = fetchImageSize(url).catch(async (e) => {
      const dims = await askMainForDims(url);
      if (!(dims.w > 0)) console.warn(`tsr: image failed to load: ${url}`, e);
      return dims;
    });
    imageDims.set(url, p);
  }
  return p;
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
// a metric key → the canvas font (phase 1: the stack, size, weight, style)
const styleOf = (mk) => ({ family: mk.stack, sizePx: mk.sizePx, weight: mk.weight, italic: mk.italic });

// timings (bench-edit.mjs): engineMs = wasm typeset passes (emit + KP +
// layout), the rest is the provider side of the pull loop
// `stale()` is the job's generation check: polled after every await, a
// superseded job stops (returns false) instead of finishing stale work.
async function measureLoop(M, doc, { tm = {}, baseUrl, stale = () => false } = {}) {
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
    const ans = { batch: req.batch, kinds: {} };
    t0 = performance.now();
    const ims = req.kinds.boxInfo ?? [];
    const dims = await Promise.all(ims.map((im) => imageSize(im.ref, baseUrl)));
    if (stale()) return false;
    // 0×0 = a failed load: the engine's placeholder + image-load warning
    ans.kinds.boxInfo = ims.map((im, k) => ({ resId: im.resId, w: dims[k].w, h: dims[k].h, baseline: dims[k].h }));
    mark('imagesMs', t0);
    t0 = performance.now();
    ans.kinds.codeTokens = [];
    for (const t of req.kinds.codeTokens ?? []) {
      const runs = await tokenize(t.lang, t.text);
      if (stale()) return false;
      ans.kinds.codeTokens.push({ resId: t.resId, runs });
    }
    mark('tokensMs', t0);
    t0 = performance.now();
    ans.kinds.fontVmet = (req.kinds.fontVmet ?? []).map((r) => {
      measurer.setStyle(styleOf(req.mks[r.mk]));
      const { ascent, descent } = measurer.vmet();
      return { resId: r.resId, asc: ascent, desc: descent };
    });
    const words = req.kinds.textWidth ?? [];
    const seen = new Map();  // per round: keys that share a canvas font
    ans.kinds.textWidth = words.map((r) => {
      measurer.setStyle(styleOf(req.mks[r.mk]));
      const k = measurer.fontKey + '\0' + r.text;
      let px = seen.get(k);
      if (px === undefined) seen.set(k, (px = measurer.width(r.text)));
      return { resId: r.resId, px };
    });
    tm.words = (tm.words ?? 0) + words.length;
    mark('wordsMs', t0);
    t0 = performance.now();
    provideAnswer(M, doc, ans);
    mark('requestMs', t0);
  }
  throw new Error('typeset did not converge');
}

function postResult(M, doc, ids, tm) {
  const t0 = performance.now();
  const html = M.UTF8ToString(M._tsr_render(doc));
  if (tm) tm.renderMs = performance.now() - t0;
  const diags = M.UTF8ToString(M._tsr_diags(doc));
  const heightPx = M._tsr_doc_height_px(doc);
  for (const id of ids) postMessage({ type: 'result', id, html, diags, heightPx, timings: tm });
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
  if (!s) sessions.set(key, (s = { queue: [], running: null, gen: 0, doc: undefined }));
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
  const { source, settings, progressive, fontFaces, baseUrl } = msg;
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
    const ops = await execute(compiledOf(M, doc), { baseUrl, parse: fragmentsOf(M) });
    mark('executeMs', t0);
    await yieldTurn();  // (counted in the edit's total, not in executeMs)
    if (stale()) { M._tsr_doc_free(doc); return false; }
    t0 = performance.now();
    const opsPtr = M._malloc(ops.length);
    M.HEAPU8.set(ops, opsPtr);
    const ok = M._tsr_ingest(doc, opsPtr, ops.length) === 0;
    M._free(opsPtr);
    mark('ingestMs', t0);
    if (!ok) throw new Error('ops ingest failed: ' + M.UTF8ToString(M._tsr_diags(doc)));

    if (progressive !== false) {
      const html = M.UTF8ToString(M._tsr_render_semantic(doc));
      for (const id of ids) postMessage({ type: 'semantic', id, html });
    }

    if (!(await measureLoop(M, doc, { tm, baseUrl, stale }))) {
      M._tsr_doc_free(doc);
      return false;
    }
    if (s.doc !== undefined) M._tsr_doc_free(s.doc);
    s.doc = doc;
    postResult(M, doc, ids, tm);
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
// live document stays as it is
async function runPaginate(s, { ids, msg }) {
  const { pageWidthPx, pageHeightPx, baseUrl } = msg;
  const M = await getMod();
  if (s.doc === undefined) return postError(ids, 'paginate: doc disposed');
  const doc = forkDoc(M, s.doc, { host: { width: pageWidthPx }, page: { height: pageHeightPx } });
  if (doc === undefined) return postError(ids, 'paginate: cannot fork the document');
  try {
    await measureLoop(M, doc, { baseUrl });
    const html = M.UTF8ToString(M._tsr_render_pages(doc, pageHeightPx));
    const diags = M.UTF8ToString(M._tsr_diags(doc));
    for (const id of ids) postMessage({ type: 'result', id, html, diags, heightPx: 0 });
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
    if (!(await measureLoop(M, s.doc, { tm, baseUrl: msg.baseUrl, stale }))) return false;
    postResult(M, s.doc, ids, tm);
    return;
  }
  const doc = forkDoc(M, s.doc, patch);
  if (doc === undefined) return postError(ids, 'relayout: cannot fork the document');
  let ok = false;
  try {
    ok = await measureLoop(M, doc, { baseUrl: msg.baseUrl, stale });
    if (!ok) return false;
    M._tsr_doc_free(s.doc);
    s.doc = doc;
    postResult(M, doc, ids);
  } finally {
    if (!ok) M._tsr_doc_free(doc);
  }
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
              dispose: runDispose };

onmessage = (ev) => {
  const m = ev.data;
  if (m?.type === 'policy') {  // createEngine({policy}): host policy overrides
    for (const [k, v] of Object.entries(m.policy ?? {})) if (k in policy) policy[k] = v;
    return;
  }
  if (m?.type === 'image-dims') {
    const r = mainDims.get(m.rid);
    if (r) { mainDims.delete(m.rid); r({ w: m.w, h: m.h }); }
    return;
  }
  // 'typeset' opens a session under its own id; 'update' re-typesets an
  // editing session's doc under its docId
  if (m?.type === 'typeset') enqueue(m.id, { kind: 'update', ids: [m.id], msg: m });
  else if (m?.type === 'update') enqueue(m.docId, { kind: 'update', ids: [m.id], msg: m });
  else if (m?.type === 'paginate') enqueue(m.docId, { kind: 'paginate', ids: [m.id], msg: m });
  else if (m?.type === 'relayout') enqueue(m.docId, { kind: 'relayout', ids: [m.id], msg: m });
  else if (m?.type === 'dispose') enqueue(m.docId, { kind: 'dispose', ids: [], msg: m });
};
