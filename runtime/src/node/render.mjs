// Node-side .tsm → semantic HTML (pages-design.md §3): the build-time half
// of the engine, shared by tools/export-static.mjs and site generators
// (eleventy-plugin-style integrations). One wasm module per process; doc
// handles are transient. No browser, no canvas, no typeset pass — the
// output is the resolver-complete semantic page with static token spans.
import { execute } from '../worker/executor.mjs';
import { tokenize } from '../worker/tokens.mjs';

let modPromise = null;
function getMod() {
  modPromise ??= import('../../../engine/build-wasm/typesetter.js')
    .then((m) => m.default());
  return modPromise;
}

// → { html, diags, ok }; ok=false on ingest failure or error-severity diags
export async function renderTsm(source) {
  const M = await getMod();
  const doc = M._tsr_doc_new();
  try {
    const srcPtr = M.stringToNewUTF8(String(source));
    M._tsr_compile(doc, srcPtr);
    M._free(srcPtr);
    const js = M.UTF8ToString(M._tsr_get_js(doc));
    const ops = await execute(js);
    const opsPtr = M._malloc(ops.length);
    M.HEAPU8.set(ops, opsPtr);
    const ingested = M._tsr_ingest(doc, opsPtr, ops.length) === 0;
    M._free(opsPtr);
    if (!ingested)
      return { html: '', diags: M.UTF8ToString(M._tsr_diags(doc)), ok: false };
    // answer NEED_TOKENS before the semantic render: foldTokens rewrites the
    // tree, so the static page carries the highlight spans
    const req = JSON.parse(M.UTF8ToString(M._tsr_measure_requests(doc)));
    for (const t of req.tokens ?? []) {
      const tri = await tokenize(t.lang, t.text);
      const ptr = M._malloc(Math.max(4, tri.length * 4));
      M.HEAPU32.set(tri, ptr >> 2);
      M._tsr_provide_tokens(doc, t.id, ptr, tri.length / 3);
      M._free(ptr);
    }
    const html = M.UTF8ToString(M._tsr_render_semantic(doc));
    const diags = M.UTF8ToString(M._tsr_diags(doc));
    return { html, diags, ok: !/^error /m.test(diags) };
  } finally {
    M._tsr_doc_free(doc);
  }
}
