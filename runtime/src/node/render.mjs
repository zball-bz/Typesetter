// Node-side .tsm → semantic HTML (pages-design.md §3): the build-time half
// of the engine, shared by tools/export-static.mjs and site generators
// (eleventy-plugin-style integrations). One wasm module per process; doc
// handles are transient. No browser, no canvas, no typeset pass — the
// output is the resolver-complete semantic page with static token spans.
import { execute } from '../worker/executor.mjs';
import { ResourceHost } from '../shared/resources/host.mjs';
import { tokenProvider } from '../shared/resources/providers/tokens.mjs';
import { checkAbi, compiledOf, fragmentsOf } from '../shared/abi.mjs';
import { decodeRequest, encodeAnswer } from '../shared/rescodec.mjs';
import { RES_KINDS } from '../shared/resources.gen.mjs';
import { settingsFromOptions } from '../shared/settings.gen.mjs';
import { moduleGraph } from './module-graph.mjs';

let modPromise = null;
let session = 0;
function getMod() {
  modPromise ??= import('../../../engine/build-wasm/typesetter.js')
    .then((m) => m.default())
    .then((M) => { checkAbi(M); return M; });
  return modPromise;
}

// (plan P3-35) the engine's front end, for tools: a document's AST JSON
// (tsr_parse_json; spans are UTF-8 byte offsets) and its syntax tokens;
// opts.settings: the settings document (its source.* rows: front matter)
async function frontEnd(fn, source, settings) {
  const M = await getMod();
  const p = M.stringToNewUTF8(String(source));
  const s = M.stringToNewUTF8(settings ? JSON.stringify(settings) : '');
  try {
    return JSON.parse(M.UTF8ToString(M[fn](p, s)));
  } finally {
    M._free(p);
    M._free(s);
  }
}
export const parseTsm = (source, { settings } = {}) => frontEnd('_tsr_parse_json', source, settings);
export const syntaxTokens = (source, { settings } = {}) => frontEnd('_tsr_syntax_tokens', source, settings);

// → { html, diagnostics, ok, manifest, settings, css, docinfo } (plan P3-21;
// design T9 A2; `diags` is diagnostics' older name). ok=false on ingest
// failure or error-severity diagnostics. css: the page's stylesheet from the
// rules (plan P3-01). manifest: everything the document references — its
// images (the engine's references product), its execution loads (the
// resource host's log: #bibliography, $.load) and the fonts the host
// declared (opts.fonts) — [{ url, ref, role, source, status, requester }]
// (ref: the reference as the document wrote it; url: where it resolved).
// settings: the effective settings document. docinfo: { lang, title }.
// labels (plan P3-31): its labels product — the manifest a project's other
// documents read (opts.inputs: {labels: '[manifest, …]'} is the reverse).
// opts.settings: the settings document (docs/settings-table.md; opts.lang is
// sugar for doc.lang). opts.baseDir / opts.rootDir: where document resources
// resolve — relative paths against baseDir, /site-root paths against rootDir
// (defaults: process.cwd()). opts.host: a ResourceHost of the caller's
// (its providers and cache), else one per process; opts.providers:
// [{ kind, provider }] registered for this render.
let defaultHost = null;
// (plan P3-36; design T7 S14) the bundle: besides the fields above, result —
// the semantic RenderResult ({head: {lang, title, idPrefix, profile}, html,
// anchors: [{id, label, cls}]}), resources (the manifest), styles ({contract,
// theme, rules}: the page's CSS, the engine's) and profile. opts.profile:
// 'page' (default) or 'feed' — formulas as their source (render.math:
// source, unless the settings say otherwise). exportStatic (./export.mjs)
// turns a bundle into a page.
let pageCss = null;
const pageStyles = () => (pageCss ??= import('../main/shell.mjs').then((m) => ({ contract: m.TSR_CSS, theme: m.THEME_CSS })));

export async function renderTsm(source, opts = {}) {
  const profile = opts.profile === 'feed' ? 'feed' : 'page';
  if (profile === 'feed' && opts.settings?.render?.math === undefined)
    opts = { ...opts, settings: { ...(opts.settings ?? {}), render: { ...(opts.settings?.render ?? {}), math: 'source' } } };
  const M = await getMod();
  const doc = M._tsr_doc_new();
  // one Session per process (plan P1-21): code tokens answered once are reused
  M._tsr2_doc_attach(doc, (session ||= M._tsr2_session_new(0)));  // (null: the default budget)
  try {
    // one settings document (plan P1-03); opts.lang stays as sugar for doc.lang
    const settings = settingsFromOptions(opts);
    const cfg = M.stringToNewUTF8(JSON.stringify(settings));
    M._tsr2_set_config(doc, cfg);
    M._free(cfg);
    const srcPtr = M.stringToNewUTF8(String(source));
    M._tsr_compile(doc, srcPtr);
    M._free(srcPtr);
    const { resolve } = await import('node:path');
    const rootDir = resolve(opts.rootDir ?? process.cwd());
    let host = opts.host ?? (defaultHost ??= new ResourceHost().register('codeTokens', tokenProvider));
    if (opts.providers?.length) {  // (plan P3-21) [{ kind, provider }]: this render's own
      const own = new ResourceHost({ cache: host.cache }).register('codeTokens', tokenProvider);
      for (const { kind, provider } of opts.providers) own.register(kind, provider);
      host = own;
    }
    const jobOf = () => host.job({ bases: { doc: resolve(opts.baseDir ?? rootDir) }, root: rootDir });
    const job = jobOf();
    const imported = {};  // (plan P3-31) the inputs the document asked for ($.labels.import)
    const ops = await execute(compiledOf(M, doc), { host: job, parse: fragmentsOf(M), inputs: imported,
      // (plan P3-31; D-I08) dev mode (policy.checkExecution): a second execution writes the same ops
      check: opts.policy?.checkExecution ? jobOf : null });
    const inputs = { ...(opts.inputs ?? {}) };
    if (imported.labels?.length) {
      const inner = (inputs.labels ?? '').trim().replace(/^\[|\]$/g, '').trim();
      inputs.labels = `[${[inner, ...imported.labels].filter(Boolean).join(',')}]`;
    }
    // (plan P5-01; D-M06) the declared math fonts' metrics (role 'math',
    // `metrics`: a .tsmf, relative to rootDir): the input mathFonts
    if (!inputs.mathFonts) {
      const { readFileSync } = await import('node:fs');
      const blobs = [];
      for (const f of opts.fonts ?? [])
        if (f?.role === 'math' && f.metrics) {
          try {
            blobs.push(readFileSync(resolve(rootDir, String(f.metrics))));
          } catch { /* left out: the engine says what math.fonts names that it lacks */ }
        }
      if (blobs.length) inputs.mathFonts = new Uint8Array(Buffer.concat(blobs));
    }
    // (plan P3-31) its declared inputs (opts.inputs: {labels: '[manifest, …]'}), before Ingest
    for (const [name, value] of Object.entries(inputs)) {
      if (typeof value !== 'string' && !(value instanceof Uint8Array)) continue;
      const n = M.stringToNewUTF8(name);
      const bytes = typeof value === 'string' ? new TextEncoder().encode(value) : value;
      const p = M._malloc(bytes.length || 1);
      M.HEAPU8.set(bytes, p);
      M._tsr2_set_input(doc, n, p, bytes.length);
      M._free(p);
      M._free(n);
    }
    const opsPtr = M._malloc(ops.length);
    M.HEAPU8.set(ops, opsPtr);
    const ingested = M._tsr_ingest(doc, opsPtr, ops.length) === 0;
    M._free(opsPtr);
    if (!ingested) {
      const diagnostics = M.UTF8ToString(M._tsr_diags(doc));
      return { html: '', css: '', diagnostics, diags: diagnostics, ok: false, manifest: job.manifest(), settings: {}, docinfo: {} };
    }
    // answer NEED_TOKENS before the semantic render: foldTokens rewrites the
    // tree, so the static page carries the highlight spans
    // (the resource pull, plan P1-19: only the code tokens are asked for)
    const p = M._tsr2_requests(doc, 1 << RES_KINDS.codeTokens.id);
    const len = new DataView(M.HEAPU8.buffer).getUint32(p, true);
    const req = decodeRequest(M.HEAPU8.slice(p + 4, p + 4 + len));
    const rows = req.kinds.codeTokens ?? [];
    if (rows.length) {
      const ans = await job.answer({ batch: req.batch, mks: req.mks, kinds: { codeTokens: rows } });
      const bytes = encodeAnswer(ans);
      const ap = M._malloc(bytes.length);
      M.HEAPU8.set(bytes, ap);
      M._tsr2_provide(doc, ap, bytes.length);
      M._free(ap);
    }
    const html = M.UTF8ToString(M._tsr_render_semantic(doc));
    const css = M.UTF8ToString(M._tsr2_render_css(doc));
    const product = (name) => {
      const p = M.stringToNewUTF8(name);
      const out = M.UTF8ToString(M._tsr2_product(doc, p));
      M._free(p);
      return out;
    };
    // the manifest: the images the engine knows of, the loads, the fonts
    const manifest = [];
    for (const line of product('references').split('\n').filter(Boolean)) {
      const r = JSON.parse(line);
      manifest.push({ url: r.src, ref: r.src, role: r.role, source: 'doc', status: r.allowed ? 'referenced' : 'denied', requester: 'image' });
    }
    const loads = job.manifest();
    manifest.push(...loads);
    // a #use module's own imports (requester 'import'): the JavaScript
    // engine loads them, not the host, so the module graph names them — a
    // page that publishes the module publishes them beside it
    const listed = new Set(manifest.map((m) => m.url));
    for (const m of loads) {
      if (m.role !== 'module' || m.status !== 'ok' || /^[a-z][a-z0-9+.-]*:/i.test(m.url)) continue;
      for (const file of (await moduleGraph(m.url)).files) {
        if (listed.has(file)) continue;
        listed.add(file);
        manifest.push({ url: file, role: 'module', source: m.source, status: 'ok', requester: 'import' });
      }
    }
    for (const f of opts.fonts ?? []) {
      if (f?.src) manifest.push({ url: String(f.src), role: 'font', source: 'host', status: 'declared', requester: 'host' });
      if (f?.metrics) manifest.push({ url: String(f.metrics), role: 'font-metrics', source: 'host', status: 'declared', requester: 'host' });
    }
    const diagnostics = M.UTF8ToString(M._tsr_diags(doc));
    const resolved = JSON.parse(product('settings'));
    const docinfo = JSON.parse(product('docinfo'));
    const labels = product('labels');  // (plan P3-31) its labels product (a project's manifest)
    // (plan P3-36) the anchors the page carries: its labels, as ids
    const idPrefix = resolved.render?.idPrefix ?? 'tsr-';
    let anchors = [];
    try {
      anchors = (JSON.parse(labels).labels ?? []).map((l) => ({ id: idPrefix + l.anchor, label: l.label, cls: l.class }));
    } catch { /* no labels product */ }
    const { contract, theme } = await pageStyles();
    return { html, css, diagnostics, diags: diagnostics, ok: !/^error /m.test(diagnostics), manifest,
             settings: resolved, docinfo, labels, profile,
             result: { head: { lang: docinfo.lang ?? '', title: docinfo.title ?? '', idPrefix, profile }, html, anchors },
             resources: manifest, styles: { contract, theme, rules: css },
             // (plan P5-01) the host's math faces, for a page that paints formulas
             fonts: (opts.fonts ?? []).filter((f) => f?.role === 'math' && f.family && f.src)
               .map((f) => ({ family: String(f.family), src: String(f.src), role: 'math' })) };
  } finally {
    M._tsr_doc_free(doc);
  }
}
