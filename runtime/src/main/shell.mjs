// Public API (main thread). Thin by design: worker does everything except
// DOM injection and clipboard (architecture §4.2). Progressive upgrade
// (v2 §9): semantic flow HTML paints first; the typeset result swaps in
// keyed by data-pid, reporting old/new rects — scroll anchoring is the
// caller's responsibility (the engine provides the information).
import { commit, createSession, decodeResult, elementsAt, heldKeys, offsetAt, sessionHtml, StaleKeys } from './commit.mjs';
import { MATH_FONT } from '../shared/mathfont.gen.mjs';
import { installCopy } from './copy.mjs';
import { settingsFromOptions, settingOf } from '../shared/settings.gen.mjs';

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
.tsr-doc { font-family: var(--tsr-font-body); }
.tsr-code { font-family: var(--tsr-font-mono, monospace); white-space: pre; }
.tsr-cjk { font-family: var(--tsr-font-cjk, var(--tsr-cjk-font, inherit)); }
.tsr-code.tsr-cjk { font-family: var(--tsr-font-mono-cjk, var(--tsr-cjk-font, inherit)); }
.tsr-marker { position: absolute; right: 100%; padding-right: 0.55em;
              user-select: none; -webkit-user-select: none; }
.tsr-doc [data-syn="cont"] { user-select: none; -webkit-user-select: none; }
.tsr-rule { position: absolute; height: 0; border-top: 1px solid currentColor; opacity: 0.35; }
.tsr-img { position: absolute; }
.tsr-imgph { position: absolute; border: 1px dashed currentColor; opacity: 0.5;
             display: flex; align-items: center; justify-content: center;
             font-size: 0.85em; box-sizing: border-box; }
.tsr-raw { position: absolute; overflow: hidden; }
/* inline objects (plan P1-13): boxes on the baseline, of the engine's size */
.tsr-iimg { vertical-align: baseline; }
.tsr-iimgph { display: inline-block; border: 1px dashed currentColor; opacity: 0.5; box-sizing: border-box; }
.tsr-iraw { display: inline-block; overflow: hidden; vertical-align: baseline; }
.tsr-doc a { color: #1a5276; text-decoration: underline; text-underline-offset: 2px; }
.tsr-sp { display: inline-block; }
.tsr-sqL { margin-left: -0.5em; }   /* punct half squeezed at line start / pair */
.tsr-sqR { margin-right: -0.5em; }  /* punct half squeezed at line end / pair */
.tsr-cjk.tsr-i { font-style: normal; text-emphasis: filled dot; text-emphasis-position: under right; }
/* footnote markers (notes-design.md §1): size is measured (sizeMul); the
   raise is paint-only so line geometry is untouched */
.tsr-sup, .tsr-doc a.tsr-sup { position: relative; top: -0.45em; text-decoration: none; }
/* hover/focus popup with the note body (shell installNotePopups) */
.tsr-notepop { position: absolute; z-index: 20; max-width: 28em; max-height: 45vh;
  overflow: auto; padding: 0.5em 0.7em; font-size: 0.85em; line-height: 1.45;
  font-family: var(--tsr-pop-font, inherit); background: var(--tsr-pop-bg, #fffdf7);
  color: var(--tsr-pop-fg, #1c1c1a); border: 1px solid rgba(0,0,0,0.18);
  border-radius: 4px; box-shadow: 0 4px 14px rgba(0,0,0,0.12); white-space: normal; }
@media (prefers-color-scheme: dark) {
  .tsr-notepop { background: var(--tsr-pop-bg, #2a2a28); color: var(--tsr-pop-fg, #e6e4dc);
    border-color: rgba(255,255,255,0.18); } }
/* math (math-design.md §8): one inline box per formula, absolutely
   positioned glyph runs in the bundled font; rules are painted boxes */
.tsr-math { position: relative; display: inline-block; }
.tsr-math .tsr-mg { position: absolute; white-space: pre;
                    font-family: ${JSON.stringify(MATH_FONT.family)}; font-kerning: none; }
.tsr-math .tsr-mr { position: absolute; background: currentColor; }
/* names / operators / "text" in formulas: upright, in the body font (the
   engine measured them there) — Euler stays for variables and symbols */
.tsr-math .tsr-mg.tsr-mt { font-family: inherit; font-style: normal; }
.tsr-eqno { position: absolute; top: 50%; transform: translateY(-50%); }
.tsr-hlline { background: var(--tsr-hl-line, rgba(250, 200, 60, 0.16)); }
.tsr-marker.tsr-code { color: var(--tsr-tok-comment, #8d897f);
                       font-size: 0.85em; padding-top: 0.15em; }
/* code token theme (code-design.md §5): engine emits var(--tsr-tok-<tag>);
   theming lives entirely here */
.tsr-doc, .tsr-flow { --tsr-tok-keyword: #7c4dbe; --tsr-tok-string: #2e7d32;
  --tsr-tok-number: #b45309; --tsr-tok-comment: #8d897f;
  --tsr-tok-function: #1d4ed8; --tsr-tok-type: #0f766e;
  --tsr-tok-constant: #b91c1c; --tsr-tok-variable: inherit;
  --tsr-tok-operator: #6b6b6b; --tsr-tok-punctuation: #7a7a72;
  --tsr-tok-property: #92400e; --tsr-tok-attribute: #92400e;
  --tsr-tok-label: #7c4dbe; --tsr-tok-embedded: inherit; }
@media (prefers-color-scheme: dark) {
  .tsr-doc, .tsr-flow { --tsr-tok-keyword: #b794f6; --tsr-tok-string: #7bc98b;
    --tsr-tok-number: #e5a45b; --tsr-tok-comment: #8f8b81;
    --tsr-tok-function: #7fb3f5; --tsr-tok-type: #5ecfbf;
    --tsr-tok-constant: #ef8a8a; --tsr-tok-operator: #9a9a92;
    --tsr-tok-punctuation: #8b8b83; --tsr-tok-property: #dfb27a;
    --tsr-tok-attribute: #dfb27a; --tsr-tok-label: #b794f6; } }
`;

// The bundled math font (plan P1-23; the manifest mathfont.gen.mjs, written
// by tools/mathc.py with the metrics artifact): a declared webfont of role
// 'math', installed like every other declared face. Metrics are
// precompiled, so layout never waits on the file — only paint does
// (font-display: block). No fallback family: a missing glyph is the
// engine's math-coverage diagnostic, never another font's ink.
export const TSR_MATH_FONT_URL = new URL(`../../../${MATH_FONT.file}`, import.meta.url).href;
const MATH_FONT_FACE = { family: MATH_FONT.family, src: TSR_MATH_FONT_URL, role: MATH_FONT.role };

// Declared webfonts (pages-design.md §1): one @font-face per entry on the
// paint side; the worker loads the same files into its own FontFaceSet so
// measurement never sees a fallback the paint doesn't.
const injectedFonts = new Set();
function ensureFontFaces(fonts) {
  for (const f of fonts ?? []) {
    const key = `${f.family}|${f.weight ?? 'normal'}|${f.style ?? 'normal'}`;
    if (!f.family || !f.src || injectedFonts.has(key)) continue;
    injectedFonts.add(key);
    const style = document.createElement('style');
    style.dataset.tsrFont = f.family;
    style.textContent =
      `@font-face { font-family: ${JSON.stringify(f.family)};` +
      ` src: url(${JSON.stringify(String(f.src))});` +
      ` font-weight: ${f.weight ?? 'normal'}; font-style: ${f.style ?? 'normal'};` +
      ` font-display: block; }`;
    document.head.appendChild(style);
  }
}

// Bound wait so the first typeset paint uses the same faces the engine
// measured with (glyph ink only — geometry is engine-owned either way).
function settleFonts(fonts) {
  const loads = (fonts ?? []).map((f) =>
    document.fonts.load(`${f.style ?? 'normal'} ${f.weight ?? 'normal'} 16px ${JSON.stringify(f.family)}`)
      .catch(() => {}));
  if (!loads.length) return Promise.resolve();
  return Promise.race([Promise.allSettled(loads),
                       new Promise((r) => setTimeout(r, 4000))]);
}

// Footnote popups: hovering (or focusing) a marker shows the note body
// next to it. Delegated on the container so DOM patches never lose it;
// the body text is read from the note's paragraph (minus its ↩ link).
function installNotePopups(container) {
  let pop = null, current = null;
  const hide = () => { pop?.remove(); pop = null; current = null; };
  const bodyOf = (marker) => {
    const id = marker.getAttribute('href')?.slice(1);
    const el = id && container.querySelector(`[id="${CSS.escape(id)}"]`);
    if (!el) return null;
    // typeset DOM: the id sits on the note's FIRST line box and the notes
    // list is ONE .tsr-para — take this line and the following sibling
    // lines up to the next item's first line (it carries a list marker /
    // the next anchor). Semantic page: the element itself.
    const lines = [];
    if (el.classList.contains('tsr-line')) {
      for (let n = el; n; n = n.nextElementSibling) {
        if (n !== el && (n.id || n.querySelector('.tsr-marker'))) break;
        lines.push(n);
      }
    } else lines.push(el);
    const text = lines.map((n) => {
      const clone = n.cloneNode(true);
      for (const a of clone.querySelectorAll('a[href^="#tsr-fnref-"]')) a.remove();
      for (const m of clone.querySelectorAll('.tsr-marker')) m.remove();
      return clone.textContent;
    }).join(' ');
    return text.replace(/\s+/g, ' ').trim();
  };
  const show = (marker) => {
    if (current === marker) return;
    hide();
    const text = bodyOf(marker);
    if (!text) return;
    pop = document.createElement('div');
    pop.className = 'tsr-notepop';
    pop.textContent = text;
    // host = the positioned .tsr-doc (the container itself may not be a
    // containing block); offsets are relative to the host's box
    const host = container.querySelector('.tsr-doc') ?? container;
    if (host === container && getComputedStyle(host).position === 'static')
      host.style.position = 'relative';
    host.appendChild(pop);
    const hr = host.getBoundingClientRect();
    const mr = marker.getBoundingClientRect();
    const w = Math.min(pop.offsetWidth, hr.width);
    let left = mr.left - hr.left;
    if (left + w > hr.width) left = Math.max(0, hr.width - w);
    pop.style.left = `${left}px`;
    pop.style.top = `${mr.bottom - hr.top + 6}px`;
    current = marker;
  };
  const markerAt = (t) => t?.closest?.('a.tsr-sup[href^="#tsr-fn-"]');
  const inPop = (t) => !!t?.closest?.('.tsr-notepop');
  const onOver = (e) => { const m = markerAt(e.target); if (m) show(m); else if (!inPop(e.target)) hide(); };
  // leaving the marker keeps the popup while the pointer moves INTO it
  // (long notes scroll); leaving both hides
  const onOut = (e) => {
    if ((markerAt(e.target) || inPop(e.target)) && !markerAt(e.relatedTarget) && !inPop(e.relatedTarget)) hide();
  };
  const onFocus = (e) => { const m = markerAt(e.target); if (m) show(m); };
  container.addEventListener('mouseover', onOver);
  container.addEventListener('mouseout', onOut);
  container.addEventListener('focusin', onFocus);
  container.addEventListener('focusout', hide);
  window.addEventListener('scroll', hide, { passive: true });
  return () => {
    hide();
    container.removeEventListener('mouseover', onOver);
    container.removeEventListener('mouseout', onOut);
    container.removeEventListener('focusin', onFocus);
    container.removeEventListener('focusout', hide);
    window.removeEventListener('scroll', hide);
  };
}

let cssInjected = false;
function ensureCss() {
  if (cssInjected) return;
  const style = document.createElement('style');
  style.dataset.tsr = '1';
  style.textContent = TSR_CSS;
  document.head.appendChild(style);
  cssInjected = true;
  ensureFontFaces([MATH_FONT_FACE]);
}

export function createEngine(opts = {}) {
  const workerUrl = new URL('../worker/worker.mjs', import.meta.url);
  const worker = new Worker(workerUrl, { type: 'module' });
  // host policy (schema "policy": round cap, font deadline, caches, …)
  if (opts.policy) worker.postMessage({ type: 'policy', policy: opts.policy });
  let nextId = 1;
  let liveDocId = null;
  let uninstallCopy = null;
  const pending = new Map(); // id → {resolve, reject, onSemantic}
  // NEED_IMAGES fallback (figure-design.md §2): the worker could not read
  // the image (cross-origin, no CORS); an <img> here still yields its size
  const answerDims = (rid, src) => {
    const img = new Image();
    const reply = (w, h) => worker.postMessage({ type: 'image-dims', rid, w, h });
    img.onload = () => reply(img.naturalWidth, img.naturalHeight);
    img.onerror = () => reply(0, 0);
    img.src = src;
  };
  worker.onmessage = (ev) => {
    const { id, type } = ev.data;
    if (type === 'image-dims?') { answerDims(ev.data.rid, ev.data.src); return; }
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

  // The commit path (plan P3-05; ./commit.mjs): a result names every block
  // by key and carries only the bodies this session lacks; commit() keeps
  // the blocks it holds and replaces the differing middle. Upgrade records
  // (old/new paragraph rects by pid) are read for a typeset and a relayout.
  const rectsOf = (root) => {
    const m = new Map();
    for (const el of root.querySelectorAll('[data-pid]')) m.set(el.dataset.pid, el.getBoundingClientRect());
    return m;
  };
  const upgradeRecords = (session, oldRects) => session.blocks.map((b) => {
    const o = oldRects.get(String(b.pid));
    const n = b.el.getBoundingClientRect();
    return { pid: b.pid, old: o ? { top: o.top, height: o.height } : null, new: { top: n.top, height: n.height } };
  });
  // a result committed; a stale one (it names a key the session dropped)
  // is asked for again holding nothing
  const commitResult = async (session, res, docId) => {
    try {
      return commit(session, decodeResult(res.frame, res.html));
    } catch (e) {
      if (!(e instanceof StaleKeys)) throw e;
      const again = await request({ type: 'render', id: nextId++, docId, held: new Uint8Array(0) });
      return commit(session, decodeResult(again.frame, again.html));
    }
  };

  return {
    async typeset(source, container, opts = {}) {
      const { progressive = true, fonts, onSemantic, onUpgrade } = opts;
      ensureCss();
      ensureFontFaces(fonts);
      if (liveDocId !== null) {
        worker.postMessage({ type: 'dispose', docId: liveDocId });
        liveDocId = null;
      }
      const id = nextId++;
      // one settings document (plan P1-03; docs/settings-table.md): the
      // legacy named options (widthPx, fontFamily, lang, …) are sugar for
      // their rows, and opts.settings wins over them
      const base = settingsFromOptions(opts);
      // the session measure: relayout() moves it so later update()s follow
      let width = opts.widthPx ?? base.host?.width ?? container.getBoundingClientRect().width;
      const settingsAt = (w) => ({ ...base, host: { ...(base.host ?? {}), width: w } });
      const fontFamily = settingOf(base, 'fonts.body');
      const cjkFontFamily = settingOf(base, 'fonts.cjk');
      const baseSizePx = settingOf(base, 'doc.baseSize');
      const lang = settingOf(base, 'doc.lang');
      // The container must render with exactly the family/size the engine
      // measured — this is the measure/render contract, not styling sugar.
      container.style.fontFamily = fontFamily;
      container.style.fontSize = `${baseSizePx}px`;
      container.style.setProperty('--tsr-cjk-font', cjkFontFamily);
      // footnote popups are plain text: Latin stack first, CJK stack after
      container.style.setProperty('--tsr-pop-font', `${fontFamily}, ${cjkFontFamily}`);
      // language tag drives OpenType 'locl' punctuation forms (multi-locale
      // CJK fonts pick 简中/繁中/日 glyph variants by it)
      if (lang) container.setAttribute('lang', lang);
      let semanticHtml = null;
      const res = await request(
        { type: 'typeset', id, source, settings: settingsAt(width), progressive,
          fontFaces: fonts, baseUrl: document.baseURI },
        (html) => {
          semanticHtml = html;
          if (progressive) {
            container.innerHTML = html; // first paint: browser flows it
            onSemantic?.(html);
          }
        },
      );
      await settleFonts(fonts);  // paint with the faces the engine measured
      const session = createSession(container);
      const before = rectsOf(container);
      await commitResult(session, res, id);
      const upgrades = upgradeRecords(session, before);
      onUpgrade?.(upgrades);
      liveDocId = id;
      uninstallCopy?.();
      const uc = installCopy(container);
      const un = installNotePopups(container);
      uninstallCopy = () => { uc?.(); un(); };
      const handle = {
        // the document as one HTML string (the legacy shape), built on demand
        get html() { return sessionHtml(session); },
        diags: res.diags,
        heightPx: res.heightPx,
        timings: res.timings,
        semanticHtml,
        upgrades,
        // (plan P3-05) a DOM position's source byte, and the elements that
        // show a source byte (the VS Code preview's jump and reveal)
        offsetAt: (node) => offsetAt(session, node),
        elementsAt: (byte) => elementsAt(session, byte),
        // the commit session (the blocks held, their keys and elements):
        // read-only, for hosts' diagnostics and tests
        session,
        // Editing session (editor-design.md §2): re-typeset new source under
        // the SAME doc handle. The worker's persistent caches make this the
        // low-latency path; a failing edit keeps the last good doc alive.
        // DOM damage is patched per-paragraph; full swap is the fallback.
        async update(newSource) {
          const rid = nextId++;
          const r = await request({ type: 'update', id: rid, docId: id,
            source: newSource, settings: settingsAt(width), progressive: false,
            fontFaces: fonts, baseUrl: document.baseURI, held: heldKeys(session) });
          // rects only for a listener (an edit's commit reads no layout)
          const before = onUpgrade ? rectsOf(container) : null;
          const c = await commitResult(session, r, id);
          let ups = [];
          if (onUpgrade) {
            const changed = new Set(c.ranges.flatMap((g) => g.newPids));
            ups = upgradeRecords(session, before).filter((u) => c.rebuilt || changed.has(u.pid));
            onUpgrade(ups);
          }
          Object.assign(handle, { diags: r.diags, heightPx: r.heightPx, timings: r.timings });
          return { get html() { return sessionHtml(session); }, diags: r.diags, heightPx: r.heightPx,
                   timings: r.timings, upgrades: ups, patched: !c.rebuilt, ranges: c.ranges, kept: c.kept };
        },
        // width-only re-typeset: metrics persist in the worker-held doc
        async relayout(newWidthPx) {
          const rid = nextId++;
          // the session measure moves now: an update() issued before this
          // resolves is queued behind it in the worker and must follow it
          width = newWidthPx;
          const r = await request({ type: 'relayout', id: rid, docId: id, widthPx: newWidthPx,
                                    held: heldKeys(session) });
          // (rects only for a listener, as on the edit path: plan P3-05)
          const before = onUpgrade ? rectsOf(container) : null;
          const c = await commitResult(session, r, id);
          const ups = onUpgrade ? upgradeRecords(session, before) : [];
          onUpgrade?.(ups);
          Object.assign(handle, { diags: r.diags, heightPx: r.heightPx, timings: r.timings });
          return { get html() { return sessionHtml(session); }, diags: r.diags, heightPx: r.heightPx,
                   upgrades: ups, timings: r.timings, ranges: c.ranges, kept: c.kept };
        },
        // P1 (pages-design.md §2): sheets at the page measure; the live
        // document is restored to its screen width before this resolves
        async paginate({ pageWidthPx = 666, pageHeightPx = 995 } = {}) {
          const rid = nextId++;
          const r = await request({ type: 'paginate', id: rid, docId: id,
            pageWidthPx, pageHeightPx });
          return { html: r.html, diags: r.diags };
        },
        // print-to-PDF = the browser's print engine over our paged layout.
        // The sheets are injected into the PARENT document under a print
        // root; @media print hides everything else. (A hidden-iframe
        // approach printed blank/blanked pages in some browsers — focus and
        // removal races. The parent already has every font loaded.)
        async print({ pageWidthPx = 666, pageHeightPx = 995, marginPx = 64 } = {}) {
          const { html } = await handle.paginate({ pageWidthPx, pageHeightPx });
          document.getElementById('tsr-print-root')?.remove();
          document.getElementById('tsr-print-style')?.remove();
          const style = document.createElement('style');
          style.id = 'tsr-print-style';
          // Gecko sizes A4 at FRACTIONAL css px (793.70 × 1122.52) and
          // fragments with zero overflow tolerance: a 995px sheet inside a
          // 994.52px page content box splits into content + clipped-blank
          // page — every page doubles. (Chromium tolerates the sub-pixel
          // overflow, which is why it hid there.) Clamp the margins so the
          // content box clears the sheets with ≥3px slack on both axes,
          // and never force a break after the LAST sheet — Gecko honors
          // that literally too, as a trailing blank page.
          const A4W = 793.7, A4H = 1122.5;
          const mx = Math.max(0, Math.min(marginPx, Math.floor((A4W - pageWidthPx - 3) / 2)));
          const my = Math.max(0, Math.min(marginPx, Math.floor((A4H - pageHeightPx - 3) / 2)));
          style.textContent =
            `@page { size: A4; margin: ${my}px ${mx}px }` +
            `#tsr-print-root { display: none; }` +
            `@media print {` +
            ` body { margin: 0 !important; }` +
            ` body > :not(#tsr-print-root) { display: none !important; }` +
            ` #tsr-print-root { display: block !important; }` +
            ` .tsr-sheet { break-after: page; page-break-after: always; }` +
            ` .tsr-sheet:last-child { break-after: auto; page-break-after: auto; }` +
            `}`;
          document.head.appendChild(style);
          const root = document.createElement('div');
          root.id = 'tsr-print-root';
          root.style.fontFamily = fontFamily;
          root.style.fontSize = `${baseSizePx}px`;
          root.style.setProperty('--tsr-cjk-font', cjkFontFamily);
          if (lang) root.setAttribute('lang', lang);
          root.innerHTML = html;
          document.body.appendChild(root);
          try { await document.fonts.ready; } catch { /* print what settled */ }
          const done = new Promise((r) =>
            window.addEventListener('afterprint', r, { once: true }));
          window.print();
          await Promise.race([done, new Promise((r) => setTimeout(r, 120000))]);
          root.remove();
          style.remove();
          return { html };
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
