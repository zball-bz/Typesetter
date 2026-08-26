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

let modPromise = null;
const getMod = () => (modPromise ??= createTypesetter());
const docs = new Map(); // docId → wasm doc handle (kept alive for relayout)

// NEED_IMAGES (figure-design.md §2): intrinsic CSS dims only, cached per
// src for the worker's lifetime; 0×0 = failure (engine placeholder + diag)
const imageDims = new Map();

// W (pages-design.md §1): fonts are DECLARED, not discovered — the worker
// loads them into its own FontFaceSet before measuring, so metrics are
// right on the first pass and no settle re-typeset can exist. A font that
// misses the 4s deadline measures as its fallback.
const loadedFonts = new Set();
async function loadFonts(fonts) {
  const want = (fonts ?? []).filter((f) => {
    const key = `${f.family}|${f.weight ?? 'normal'}|${f.style ?? 'normal'}`;
    if (loadedFonts.has(key) || !f.family || !f.src) return false;
    loadedFonts.add(key);
    return true;
  });
  if (!want.length) return;
  await Promise.race([
    Promise.allSettled(want.map(async (f) => {
      try {
        const ff = new FontFace(f.family, `url(${JSON.stringify(String(f.src))})`, {
          weight: f.weight ?? 'normal', style: f.style ?? 'normal',
        });
        await ff.load();
        self.fonts.add(ff);
      } catch (e) {
        console.warn(`tsr: font failed to load: ${f.family}`, e);
      }
    })),
    new Promise((r) => setTimeout(r, 4000)),
  ]);
}
async function imageSize(src) {
  if (imageDims.has(src)) return imageDims.get(src);
  let dims = { w: 0, h: 0 };
  try {
    const bm = await createImageBitmap(await (await fetch(src)).blob());
    dims = { w: bm.width, h: bm.height };
    bm.close();
  } catch (e) {
    console.warn(`tsr: image failed to load: ${src}`, e);
  }
  imageDims.set(src, dims);
  return dims;
}

async function measureLoop(M, doc) {
  const measurer = new CanvasMeasurer();
  for (let round = 0; round < 64; round++) {
    if (M._tsr_typeset(doc) === 0) return;
    const req = JSON.parse(M.UTF8ToString(M._tsr_measure_requests(doc)));
    for (const im of req.images ?? []) {
      const d = await imageSize(im.src);
      M._tsr_provide_image(doc, im.id, d.w, d.h);
    }
    for (const t of req.tokens ?? []) {
      const tri = await tokenize(t.lang, t.text);
      const ptr = M._malloc(Math.max(4, tri.length * 4));
      M.HEAPU32.set(tri, ptr >> 2);
      M._tsr_provide_tokens(doc, t.id, ptr, tri.length / 3);
      M._free(ptr);
    }
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
                         cjkFontFamily, paraIndentEm, punctCompress, progressive,
                         codeFontFeatures, codeFontFeaturesByLang,
                         verbatimSnapKerning, fonts }) {
  const M = await getMod();
  await loadFonts(fonts);
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
    if (cjkFontFamily) {
      const f = M.stringToNewUTF8(cjkFontFamily);
      M._tsr_set_cjk_font(doc, f);
      M._free(f);
    }
    const setFeat = (lang, feat) => {
      const l = M.stringToNewUTF8(lang), f = M.stringToNewUTF8(feat);
      M._tsr_set_code_features(doc, l, f);
      M._free(l); M._free(f);
    };
    if (verbatimSnapKerning) M._tsr_set_snap_kerning(doc, 1);
    if (codeFontFeatures) setFeat('', codeFontFeatures);
    for (const [l, f] of Object.entries(codeFontFeaturesByLang ?? {})) setFeat(l, f);
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
