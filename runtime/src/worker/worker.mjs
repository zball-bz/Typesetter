// Engine host — module worker (architecture §4.1). Owns WASM, execution,
// and canvas measurement; the main thread only injects HTML.
import createTypesetter from '../../../engine/build-wasm/typesetter.js';
import { execute } from './executor.mjs';
import { CanvasMeasurer } from './canvas_measure.mjs';

let modPromise = null;
const getMod = () => (modPromise ??= createTypesetter());

async function typeset({ id, source, widthPx, baseSizePx, lineHeight, fontFamily, paraIndentEm }) {
  const M = await getMod();
  const doc = M._tsr_doc_new();
  try {
    M._tsr_config(doc, widthPx ?? 300, baseSizePx ?? 18, lineHeight ?? 1.5, paraIndentEm ?? 0);
    if (fontFamily) {
      const f = M.stringToNewUTF8(fontFamily);
      M._tsr_set_font(doc, f);
      M._free(f);
    }
    const srcPtr = M.stringToNewUTF8(source);
    M._tsr_compile(doc, srcPtr);
    M._free(srcPtr);

    const js = M.UTF8ToString(M._tsr_get_js(doc));
    const ops = await execute(js);
    const opsPtr = M._malloc(ops.length);
    M.HEAPU8.set(ops, opsPtr);
    const ok = M._tsr_ingest(doc, opsPtr, ops.length) === 0;
    M._free(opsPtr);
    if (!ok) throw new Error('ops ingest failed: ' + M.UTF8ToString(M._tsr_diags(doc)));

    const measurer = new CanvasMeasurer();
    for (let round = 0; round < 64; round++) {
      if (M._tsr_typeset(doc) === 0) break;
      const req = JSON.parse(M.UTF8ToString(M._tsr_measure_requests(doc)));
      for (const st of req.styles) {
        measurer.setStyle(st);
        if (st.needVmet) {
          const { ascent, descent } = measurer.vmet();
          M._tsr_provide_vmet(doc, st.id, ascent, descent);
        }
        for (const w of st.words) {
          const p = M.stringToNewUTF8(w);
          M._tsr_provide_word(doc, p, st.id, measurer.width(w));
          M._free(p);
        }
      }
    }
    const html = M.UTF8ToString(M._tsr_render(doc));
    const diags = M.UTF8ToString(M._tsr_diags(doc));
    const heightPx = M._tsr_doc_height_px(doc);
    postMessage({ type: 'result', id, html, diags, heightPx });
  } catch (e) {
    postMessage({ type: 'error', id, message: String(e?.stack || e) });
  } finally {
    M._tsr_doc_free(doc);
  }
}

onmessage = (ev) => {
  if (ev.data?.type === 'typeset') typeset(ev.data);
};
