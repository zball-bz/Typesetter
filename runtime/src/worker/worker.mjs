// Engine host — module worker (architecture §4.1). Owns WASM, execution,
// and canvas measurement; the main thread only injects HTML. Progressive
// upgrade (v2 §9): semantic flow HTML is posted right after ingest (no
// measurement needed — the resolver already ran), the typeset result
// follows once the pull loop converges. Docs persist for relayout until
// disposed.
import createTypesetter from '../../../engine/build-wasm/typesetter.js';
import { execute } from './executor.mjs';
import { CanvasMeasurer } from './canvas_measure.mjs';

let modPromise = null;
const getMod = () => (modPromise ??= createTypesetter());
const docs = new Map(); // docId → wasm doc handle (kept alive for relayout)

async function measureLoop(M, doc) {
  const measurer = new CanvasMeasurer();
  for (let round = 0; round < 64; round++) {
    if (M._tsr_typeset(doc) === 0) return;
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
  throw new Error('typeset did not converge');
}

function postResult(M, doc, id) {
  postMessage({
    type: 'result',
    id,
    html: M.UTF8ToString(M._tsr_render(doc)),
    diags: M.UTF8ToString(M._tsr_diags(doc)),
    heightPx: M._tsr_doc_height_px(doc),
  });
}

async function typeset({ id, source, widthPx, baseSizePx, lineHeight, fontFamily,
                         paraIndentEm, punctCompress, progressive }) {
  const M = await getMod();
  const doc = M._tsr_doc_new();
  try {
    M._tsr_config(doc, widthPx ?? 300, baseSizePx ?? 18, lineHeight ?? 1.5, paraIndentEm ?? 0);
    const pcMap = { full: 0, book: 1, none: 2 };
    if (punctCompress in pcMap) M._tsr_set_punct_compress(doc, pcMap[punctCompress]);
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

    if (progressive !== false)
      postMessage({ type: 'semantic', id, html: M.UTF8ToString(M._tsr_render_semantic(doc)) });

    await measureLoop(M, doc);
    docs.set(id, doc);
    postResult(M, doc, id);
  } catch (e) {
    M._tsr_doc_free(doc);
    postMessage({ type: 'error', id, message: String(e?.stack || e) });
  }
}

async function relayout({ id, docId, widthPx }) {
  const M = await getMod();
  const doc = docs.get(docId);
  if (doc === undefined)
    return postMessage({ type: 'error', id, message: 'relayout: doc disposed' });
  try {
    M._tsr_set_width(doc, widthPx);
    await measureLoop(M, doc); // width-only: metrics persist, loop exits fast
    postResult(M, doc, id);
  } catch (e) {
    postMessage({ type: 'error', id, message: String(e?.stack || e) });
  }
}

function dispose({ docId }) {
  const doc = docs.get(docId);
  if (doc === undefined) return;
  docs.delete(docId);
  getMod().then((M) => M._tsr_doc_free(doc));
}

onmessage = (ev) => {
  const m = ev.data;
  if (m?.type === 'typeset') typeset(m);
  else if (m?.type === 'relayout') relayout(m);
  else if (m?.type === 'dispose') dispose(m);
};
