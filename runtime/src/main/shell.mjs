// Public API (main thread). Thin by design: worker does everything except
// DOM injection (architecture §4.2).

// The serializer's CSS contract (document-model §9.1) — the nowrap rule IS
// the DPR robustness contract (v2 §7 rule 1); never remove it.
export const TSR_CSS = `
.tsr-doc { position: relative; text-rendering: geometricPrecision;
           /* the engine owns CJK punctuation compression (App C); Chromium's
              built-in trimming (text-spacing-trim: normal) would compress
              adjacent fullwidth punctuation a second time — and canvas
              measureText does not see it. Disable. */
           text-spacing-trim: space-all; }
.tsr-line { position: absolute; white-space: nowrap; contain: layout style; }
/* no 'paint' containment: list markers render in the gutter (right:100%),
   outside the line box — paint containment would clip them */
.tsr-b { font-weight: 700; }
.tsr-i { font-style: italic; }
.tsr-code { font-family: monospace; }
.tsr-marker { position: absolute; right: 100%; padding-right: 0.55em; }
.tsr-rule { position: absolute; height: 0; border-top: 1px solid currentColor; opacity: 0.35; }
.tsr-doc a { color: #1a5276; text-decoration: underline; text-underline-offset: 2px; }
.tsr-sp { display: inline-block; }
.tsr-sqL { margin-left: -0.5em; }   /* punct half squeezed at line start / pair */
.tsr-sqR { margin-right: -0.5em; }  /* punct half squeezed at line end / pair */
.tsr-cjk.tsr-i { font-style: normal; text-emphasis: filled dot; text-emphasis-position: under right; }
`;

let cssInjected = false;
function ensureCss() {
  if (cssInjected) return;
  const style = document.createElement('style');
  style.dataset.tsr = '1';
  style.textContent = TSR_CSS;
  document.head.appendChild(style);
  cssInjected = true;
}

export function createEngine(opts = {}) {
  const workerUrl = new URL('../worker/worker.mjs', import.meta.url);
  const worker = new Worker(workerUrl, { type: 'module' });
  let nextId = 1;
  const pending = new Map();
  worker.onmessage = (ev) => {
    const { id } = ev.data;
    const p = pending.get(id);
    if (!p) return;
    pending.delete(id);
    if (ev.data.type === 'error') p.reject(new Error(ev.data.message));
    else p.resolve(ev.data);
  };
  return {
    async typeset(source, container, { widthPx, baseSizePx = 18, lineHeight, fontFamily = 'Georgia, serif', paraIndentEm } = {}) {
      ensureCss();
      const id = nextId++;
      const width = widthPx ?? container.getBoundingClientRect().width;
      const res = await new Promise((resolve, reject) => {
        pending.set(id, { resolve, reject });
        worker.postMessage({ type: 'typeset', id, source, widthPx: width, baseSizePx, lineHeight, fontFamily, paraIndentEm });
      });
      // The container must render with exactly the family/size the engine
      // measured — this is the measure/render contract, not styling sugar.
      container.style.fontFamily = fontFamily;
      container.style.fontSize = `${baseSizePx}px`;
      container.innerHTML = res.html;
      return { html: res.html, diags: res.diags, heightPx: res.heightPx };
    },
    dispose() {
      worker.terminate();
    },
  };
}
