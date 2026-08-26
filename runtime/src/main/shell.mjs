// Public API (main thread). Thin by design: worker does everything except
// DOM injection and clipboard (architecture §4.2). Progressive upgrade
// (v2 §9): semantic flow HTML paints first; the typeset result swaps in
// keyed by data-pid, reporting old/new rects — scroll anchoring is the
// caller's responsibility (the engine provides the information).
import { installCopy } from './copy.mjs';

// Default CJK stack — mirrors the engine default (config.h cjkFont). CJK-class
// runs must resolve in ONE font: U+2014/…/fullwidth puncts exist in Latin
// faces and would otherwise split a line across two vertical metrics.
export const TSR_CJK_FONT =
  '"Noto Serif CJK SC", "Source Han Serif SC", "Songti SC", SimSun, serif';

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
.tsr-code { font-family: monospace; white-space: pre; }
.tsr-cjk { font-family: var(--tsr-cjk-font, inherit); }
.tsr-marker { position: absolute; right: 100%; padding-right: 0.55em;
              user-select: none; -webkit-user-select: none; }
.tsr-doc [data-syn="cont"] { user-select: none; -webkit-user-select: none; }
.tsr-rule { position: absolute; height: 0; border-top: 1px solid currentColor; opacity: 0.35; }
.tsr-raw { position: absolute; overflow: hidden; }
.tsr-doc a { color: #1a5276; text-decoration: underline; text-underline-offset: 2px; }
.tsr-sp { display: inline-block; }
.tsr-sqL { margin-left: -0.5em; }   /* punct half squeezed at line start / pair */
.tsr-sqR { margin-right: -0.5em; }  /* punct half squeezed at line end / pair */
.tsr-cjk.tsr-i { font-style: normal; text-emphasis: filled dot; text-emphasis-position: under right; }
/* math (math-design.md §8): one inline box per formula, absolutely
   positioned glyph runs in the bundled font; rules are painted boxes */
.tsr-math { position: relative; display: inline-block; }
.tsr-math .tsr-mg { position: absolute; white-space: pre;
                    font-family: 'Euler Math', 'STIX Two Math', serif; }
.tsr-math .tsr-mr { position: absolute; background: currentColor; }
.tsr-eqno { position: absolute; top: 50%; transform: translateY(-50%); }
.tsr-hlline { background: var(--tsr-hl-line, rgba(250, 200, 60, 0.16)); }
.tsr-marker.tsr-code { color: var(--tsr-tok-comment, #8d897f);
                       font-size: 0.85em; padding-top: 0.15em; }
/* code token theme (code-design.md §5): engine emits var(--tsr-tok-<tag>);
   theming lives entirely here */
.tsr-doc { --tsr-tok-keyword: #7c4dbe; --tsr-tok-string: #2e7d32;
  --tsr-tok-number: #b45309; --tsr-tok-comment: #8d897f;
  --tsr-tok-function: #1d4ed8; --tsr-tok-type: #0f766e;
  --tsr-tok-constant: #b91c1c; --tsr-tok-variable: inherit;
  --tsr-tok-operator: #6b6b6b; --tsr-tok-punctuation: #7a7a72;
  --tsr-tok-property: #92400e; --tsr-tok-attribute: #92400e;
  --tsr-tok-label: #7c4dbe; --tsr-tok-embedded: inherit; }
@media (prefers-color-scheme: dark) {
  .tsr-doc { --tsr-tok-keyword: #b794f6; --tsr-tok-string: #7bc98b;
    --tsr-tok-number: #e5a45b; --tsr-tok-comment: #8f8b81;
    --tsr-tok-function: #7fb3f5; --tsr-tok-type: #5ecfbf;
    --tsr-tok-constant: #ef8a8a; --tsr-tok-operator: #9a9a92;
    --tsr-tok-punctuation: #8b8b83; --tsr-tok-property: #dfb27a;
    --tsr-tok-attribute: #dfb27a; --tsr-tok-label: #b794f6; } }
`;

// Bundled math font: metrics are precompiled (engine/gen/euler_math.h), so
// layout never waits on this file — only paint does (font-display: block).
export const TSR_MATH_FONT_URL =
  new URL('../../../fonts/euler-math.woff2', import.meta.url).href;

let cssInjected = false;
function ensureCss() {
  if (cssInjected) return;
  const style = document.createElement('style');
  style.dataset.tsr = '1';
  style.textContent = TSR_CSS +
    `\n@font-face { font-family: 'Euler Math'; src: url('${TSR_MATH_FONT_URL}')` +
    ` format('woff2'); font-display: block; }`;
  document.head.appendChild(style);
  cssInjected = true;
}

export function createEngine(opts = {}) {
  const workerUrl = new URL('../worker/worker.mjs', import.meta.url);
  const worker = new Worker(workerUrl, { type: 'module' });
  let nextId = 1;
  let liveDocId = null;
  let uninstallCopy = null;
  const pending = new Map(); // id → {resolve, reject, onSemantic}
  worker.onmessage = (ev) => {
    const { id, type } = ev.data;
    const p = pending.get(id);
    if (!p) return;
    if (type === 'semantic') {
      p.onSemantic?.(ev.data.html);
      return; // the result for this id is still coming
    }
    pending.delete(id);
    if (type === 'error') p.reject(new Error(ev.data.message));
    else p.resolve(ev.data);
  };
  const request = (msg, onSemantic) =>
    new Promise((resolve, reject) => {
      pending.set(msg.id, { resolve, reject, onSemantic });
      worker.postMessage(msg);
    });

  // Atomic per-pid swap with upgrade records (old/new paragraph rects).
  const swapIn = (container, html) => {
    const oldRects = new Map();
    for (const el of container.querySelectorAll('[data-pid]'))
      oldRects.set(el.dataset.pid, el.getBoundingClientRect());
    container.innerHTML = html;
    const upgrades = [];
    for (const el of container.querySelectorAll('.tsr-para[data-pid]')) {
      const o = oldRects.get(el.dataset.pid);
      const n = el.getBoundingClientRect();
      upgrades.push({
        pid: +el.dataset.pid,
        old: o ? { top: o.top, height: o.height } : null,
        new: { top: n.top, height: n.height },
      });
    }
    return upgrades;
  };

  return {
    async typeset(source, container, {
      widthPx, baseSizePx = 18, lineHeight, fontFamily = 'Georgia, serif',
      cjkFontFamily = TSR_CJK_FONT, lang = 'zh-CN',
      paraIndentEm, punctCompress = 'book', progressive = true,
      codeFontFeatures, codeFontFeaturesByLang, verbatimSnapKerning,
      onSemantic, onUpgrade,
    } = {}) {
      ensureCss();
      if (liveDocId !== null) {
        worker.postMessage({ type: 'dispose', docId: liveDocId });
        liveDocId = null;
      }
      const id = nextId++;
      const width = widthPx ?? container.getBoundingClientRect().width;
      // The container must render with exactly the family/size the engine
      // measured — this is the measure/render contract, not styling sugar.
      container.style.fontFamily = fontFamily;
      container.style.fontSize = `${baseSizePx}px`;
      container.style.setProperty('--tsr-cjk-font', cjkFontFamily);
      // language tag drives OpenType 'locl' punctuation forms (multi-locale
      // CJK fonts pick 简中/繁中/日 glyph variants by it)
      if (lang) container.setAttribute('lang', lang);
      let semanticHtml = null;
      const res = await request(
        { type: 'typeset', id, source, widthPx: width, baseSizePx, lineHeight,
          fontFamily, cjkFontFamily, paraIndentEm, punctCompress, progressive,
          codeFontFeatures, codeFontFeaturesByLang, verbatimSnapKerning },
        (html) => {
          semanticHtml = html;
          if (progressive) {
            container.innerHTML = html; // first paint: browser flows it
            onSemantic?.(html);
          }
        },
      );
      const upgrades = swapIn(container, res.html);
      onUpgrade?.(upgrades);
      liveDocId = id;
      uninstallCopy?.();
      uninstallCopy = installCopy(container);
      const handle = {
        html: res.html,
        diags: res.diags,
        heightPx: res.heightPx,
        semanticHtml,
        upgrades,
        // width-only re-typeset: metrics persist in the worker-held doc
        async relayout(newWidthPx) {
          const rid = nextId++;
          const r = await request({ type: 'relayout', id: rid, docId: id, widthPx: newWidthPx });
          const ups = swapIn(container, r.html);
          onUpgrade?.(ups);
          return { html: r.html, diags: r.diags, heightPx: r.heightPx, upgrades: ups };
        },
      };
      return handle;
    },
    dispose() {
      uninstallCopy?.();
      worker.terminate();
    },
  };
}
