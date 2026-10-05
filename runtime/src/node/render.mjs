// Node-side .tsm → semantic HTML (pages-design.md §3): the build-time half
// of the engine, shared by tools/export-static.mjs and site generators
// (eleventy-plugin-style integrations). One wasm module per process; doc
// handles are transient. No browser, no canvas, no typeset pass — the
// output is the resolver-complete semantic page with static token spans.
import { execute } from '../worker/executor.mjs';
import { tokenize } from '../worker/tokens.mjs';
import { checkAbi } from '../shared/abi.mjs';
import { decodeRequest, encodeAnswer } from '../shared/rescodec.mjs';
import { RES_KINDS } from '../shared/resources.gen.mjs';
import { settingsFromOptions } from '../shared/settings.gen.mjs';

let modPromise = null;
function getMod() {
  modPromise ??= import('../../../engine/build-wasm/typesetter.js')
    .then((m) => m.default())
    .then((M) => { checkAbi(M); return M; });
  return modPromise;
}

// → { html, diags, ok }; ok=false on ingest failure or error-severity diags
// opts.settings: the settings document (docs/settings-table.md; opts.lang is
// sugar for doc.lang). opts.baseDir / opts.rootDir: where document resources (#bibliography
// src) resolve — relative paths against baseDir, /site-root paths against
// rootDir (defaults: process.cwd())
export async function renderTsm(source, opts = {}) {
  const M = await getMod();
  const doc = M._tsr_doc_new();
  try {
    // one settings document (plan P1-03); opts.lang stays as sugar for doc.lang
    const settings = settingsFromOptions(opts);
    const cfg = M.stringToNewUTF8(JSON.stringify(settings));
    M._tsr2_set_config(doc, cfg);
    M._free(cfg);
    const srcPtr = M.stringToNewUTF8(String(source));
    M._tsr_compile(doc, srcPtr);
    M._free(srcPtr);
    const js = M.UTF8ToString(M._tsr_get_js(doc));
    const ops = await execute(js, { baseDir: opts.baseDir, rootDir: opts.rootDir });
    const opsPtr = M._malloc(ops.length);
    M.HEAPU8.set(ops, opsPtr);
    const ingested = M._tsr_ingest(doc, opsPtr, ops.length) === 0;
    M._free(opsPtr);
    if (!ingested)
      return { html: '', diags: M.UTF8ToString(M._tsr_diags(doc)), ok: false };
    // answer NEED_TOKENS before the semantic render: foldTokens rewrites the
    // tree, so the static page carries the highlight spans
    // (the resource pull, plan P1-19: only the code tokens are asked for)
    const p = M._tsr2_requests(doc, 1 << RES_KINDS.codeTokens.id);
    const len = new DataView(M.HEAPU8.buffer).getUint32(p, true);
    const req = decodeRequest(M.HEAPU8.slice(p + 4, p + 4 + len));
    const rows = req.kinds.codeTokens ?? [];
    if (rows.length) {
      const codeTokens = [];
      for (const t of rows) codeTokens.push({ resId: t.resId, runs: await tokenize(t.lang, t.text) });
      const bytes = encodeAnswer({ batch: req.batch, kinds: { codeTokens } });
      const ap = M._malloc(bytes.length);
      M.HEAPU8.set(bytes, ap);
      M._tsr2_provide(doc, ap, bytes.length);
      M._free(ap);
    }
    const html = M.UTF8ToString(M._tsr_render_semantic(doc));
    const diags = M.UTF8ToString(M._tsr_diags(doc));
    return { html, diags, ok: !/^error /m.test(diags) };
  } finally {
    M._tsr_doc_free(doc);
  }
}
