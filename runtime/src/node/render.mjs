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

let modPromise = null;
let session = 0;
function getMod() {
  modPromise ??= import('../../../engine/build-wasm/typesetter.js')
    .then((m) => m.default())
    .then((M) => { checkAbi(M); return M; });
  return modPromise;
}

// → { html, diagnostics, ok, manifest, settings, css, docinfo } (plan P3-21;
// design T9 A2; `diags` is diagnostics' older name). ok=false on ingest
// failure or error-severity diagnostics. css: the page's stylesheet from the
// rules (plan P3-01). manifest: everything the document references — its
// images (the engine's references product), its execution loads (the
// resource host's log: #bibliography, $.load) and the fonts the host
// declared (opts.fonts) — [{ url, role, source, status, requester }].
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
export async function renderTsm(source, opts = {}) {
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
    // (plan P3-31) its declared inputs (opts.inputs: {labels: '[manifest, …]'}), before Ingest
    for (const [name, value] of Object.entries(inputs)) {
      if (typeof value !== 'string') continue;
      const n = M.stringToNewUTF8(name);
      const bytes = new TextEncoder().encode(value);
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
      manifest.push({ url: r.src, role: r.role, source: 'doc', status: r.allowed ? 'referenced' : 'denied', requester: 'image' });
    }
    manifest.push(...job.manifest());
    for (const f of opts.fonts ?? [])
      if (f?.src) manifest.push({ url: String(f.src), role: 'font', source: 'host', status: 'declared', requester: 'host' });
    const diagnostics = M.UTF8ToString(M._tsr_diags(doc));
    return { html, css, diagnostics, diags: diagnostics, ok: !/^error /m.test(diagnostics), manifest,
             settings: JSON.parse(product('settings')), docinfo: JSON.parse(product('docinfo')),
             labels: product('labels') };  // (plan P3-31) its labels product (a project's manifest)
  } finally {
    M._tsr_doc_free(doc);
  }
}
