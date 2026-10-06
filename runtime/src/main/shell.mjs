// Public API (main thread). Thin by design: worker does everything except
// DOM injection and clipboard (architecture §4.2). Progressive upgrade
// (v2 §9): semantic flow HTML paints first; the typeset result swaps in
// keyed by data-pid, reporting old/new rects — scroll anchoring is the
// caller's responsibility (the engine provides the information).
//
// The core (plan P3-06; design T7 "Shell core + Behavior registry"): one
// session per container — one worker and one WASM instance serve many
// documents —, the transport, the measure/render contract, the commit path
// and the copy contract (core: replaceable, never absent). Every other DOM
// feature is a Behavior ({ name, css?, install(ctx) → uninstall }) that
// reads only declared data (the RenderResult's anchors, the DOM contract,
// settings) and calls only typed operations; hosts drop, replace or add
// behaviours on equal footing with the built-ins (refPreview, print,
// devAudit). Main-thread work the worker needs is a named capability.
import { commit, createSession, decodeResult, elementsAt, heldKeys, offsetAt, sessionHtml, StaleKeys } from './commit.mjs';
import { MATH_FONT } from '../shared/mathfont.gen.mjs';
import { installCopy } from './copy.mjs';
import { settingsFromOptions, settingOf } from '../shared/settings.gen.mjs';
import { refPreview } from './behaviors/ref-preview.mjs';
import { print } from './behaviors/print.mjs';
import { devAudit } from './behaviors/audit.mjs';

export { refPreview, print, devAudit };
// the behaviours a session gets unless the host says otherwise (devAudit is
// opt-in)
export const defaultBehaviors = () => [refPreview(), print()];

// Default CJK stack — mirrors the engine default (config.h cjkFont). CJK-class
// runs must resolve in ONE font: U+2014/…/fullwidth puncts exist in Latin
// faces and would otherwise split a line across two vertical metrics.
export const TSR_CJK_FONT =
  '"Noto Serif CJK SC", "Source Han Serif SC", "Songti SC", SimSun, serif';

// The serializer's CSS contract (document-model §9.1) — the nowrap rule IS
// the DPR robustness contract (v2 §7 rule 1); never remove it. Only what
// the engine's DOM needs to render as measured: a behaviour brings its own
// CSS (plan P3-06), injected once with it.
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
.tsr-doc a { color: var(--tsr-link, #1a5276); text-decoration: underline; text-underline-offset: 2px; }
.tsr-sp { display: inline-block; }
.tsr-sqL { margin-left: -0.5em; }   /* punct half squeezed at line start / pair */
.tsr-sqR { margin-right: -0.5em; }  /* punct half squeezed at line end / pair */
.tsr-cjk.tsr-i { font-style: normal; text-emphasis: filled dot; text-emphasis-position: under right; }
/* footnote markers (notes-design.md §1): size is measured (sizeMul); the
   raise is paint-only so line geometry is untouched */
.tsr-sup, .tsr-doc a.tsr-sup { position: relative; top: -0.45em; text-decoration: none; }
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


// a behaviour's CSS, injected once (for every session and engine)
const behaviorCss = new Set();
function ensureBehaviorCss(b) {
  if (!b.css || behaviorCss.has(b.name)) return;
  behaviorCss.add(b.name);
  const style = document.createElement('style');
  style.dataset.tsrBehavior = b.name;
  style.textContent = b.css;
  document.head.appendChild(style);
}

// Main-thread capabilities (plan P3-06; design T9 capability): the worker
// asks one by name (cap? → cap). imageDims: an image the worker could not
// read (cross-origin, no CORS) still yields its size through an <img>
// (figure-design.md §2); 0×0 = failure.
export const defaultCapabilities = () => ({ imageDims: imageDimsByElement });
function imageDimsByElement({ src }) {
  return new Promise((resolve) => {
    const img = new Image();
    img.onload = () => resolve({ w: img.naturalWidth, h: img.naturalHeight });
    img.onerror = () => resolve({ w: 0, h: 0 });
    img.src = src;
  });
}

// The measure/render contract: an element shows the engine's DOM with
// exactly the family, size and language the engine measured with (the live
// container, a print root) — not styling sugar.
function applyContract(el, settings) {
  el.style.fontFamily = settingOf(settings, 'fonts.body');
  el.style.fontSize = `${settingOf(settings, 'doc.baseSize')}px`;
  el.style.setProperty('--tsr-cjk-font', settingOf(settings, 'fonts.cjk'));
  // language tag drives OpenType 'locl' punctuation forms (multi-locale CJK
  // fonts pick 简中/繁中/日 glyph variants by it)
  const lang = settingOf(settings, 'doc.lang');
  if (lang) el.setAttribute('lang', lang);
}

// createEngine({ policy, behaviors = defaultBehaviors(), copy = installCopy,
// capabilities }) — copy(container) → uninstall is the core copy contract
// (a host may replace it, never remove it); capabilities add to (or
// replace) defaultCapabilities() by name.
export function createEngine(opts = {}) {
  const workerUrl = new URL('../worker/worker.mjs', import.meta.url);
  const worker = new Worker(workerUrl, { type: 'module' });
  // host policy (schema "policy": round cap, font deadline, caches, …)
  if (opts.policy) worker.postMessage({ type: 'policy', policy: opts.policy });
  const behaviors = opts.behaviors ?? defaultBehaviors();
  const copy = opts.copy ?? installCopy;
  const capabilities = { ...defaultCapabilities(), ...(opts.capabilities ?? {}) };
  let nextId = 1;
  const pending = new Map(); // id → {resolve, reject, onSemantic}
  const sessions = new Map(); // container → session
  const answerCapability = ({ rid, name, args }) => {
    const reply = (m) => worker.postMessage({ type: 'cap', rid, ...m });
    const fn = capabilities[name];
    if (typeof fn !== 'function') { reply({ error: `no capability '${name}'` }); return; }
    Promise.resolve().then(() => fn(args))
      .then((value) => reply({ value }), (e) => reply({ error: String(e?.message ?? e) }));
  };
  worker.onmessage = (ev) => {
    const { id, type } = ev.data;
    if (type === 'cap?') { answerCapability(ev.data); return; }
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
  const upgradeRecords = (view, oldRects) => view.blocks.map((b) => {
    const o = oldRects.get(String(b.pid));
    const n = b.el.getBoundingClientRect();
    return { pid: b.pid, old: o ? { top: o.top, height: o.height } : null, new: { top: n.top, height: n.height } };
  });
  // a result committed into a session (a stale one — it names a key the
  // session dropped — is asked for again holding nothing); the session's
  // behaviours hear of every commit that changed the view
  const commitTo = async (s, res) => {
    let c;
    try {
      c = commit(s.view, decodeResult(res.frame, res.html));
    } catch (e) {
      if (!(e instanceof StaleKeys)) throw e;
      const again = await request({ type: 'render', id: nextId++, docId: s.docId, held: new Uint8Array(0) });
      c = commit(s.view, decodeResult(again.frame, again.html));
    }
    if (!c.ignored) for (const cb of [...s.onCommit]) cb(c.ranges);
    return c;
  };

  // the anchors of the committed view (RenderResult head: [label, pid,
  // class, preview]), by label — built when first asked after a commit
  const anchorsOf = (s) => {
    const head = s.view.head;
    if (s.anchorsHead !== head) {
      s.anchorsHead = head;
      s.idPrefix = head?.idPrefix ?? settingOf(s.settings, 'render.idPrefix');
      s.anchors = new Map((head?.anchors ?? []).map(([label, pid, cls, preview]) =>
        [label, { id: s.idPrefix + label, label, pid, cls, preview: preview ?? '' }]));
    }
    return s.anchors;
  };
  // the anchor a link in the view leads to (its href is "#" + id), or null
  const refAt = (s, el) => {
    const a = el?.closest?.('a[href]');
    if (!a) return null;
    const anchors = anchorsOf(s);
    const href = a.getAttribute('href') ?? '';
    const at = '#' + s.idPrefix;
    return href.startsWith(at) ? anchors.get(href.slice(at.length)) ?? null : null;
  };
  // typed operations (design T7 BehaviorCtx.ops)
  // fragment: a preview of what a label names — { html, generation } when
  // it belongs to the committed view, else null (never rejects)
  const fragmentOf = async (s, label) => {
    if (s.disposed) return null;
    try {
      const r = await request({ type: 'fragment', id: nextId++, docId: s.docId, label });
      return r.html && r.generation === s.view.generation ? { html: r.html, generation: r.generation } : null;
    } catch {
      return null;
    }
  };
  // paginate: sheets at a page measure from a fork (the live document stays
  // as it is); idPrefix: the sheets' own
  const paginateOf = async (s, { pageWidthPx = 666, pageHeightPx = settingOf(s.settings, 'page.height'),
                                 idPrefix } = {}) => {
    const r = await request({ type: 'paginate', id: nextId++, docId: s.docId, pageWidthPx, pageHeightPx,
                              idPrefix, baseUrl: document.baseURI });
    return { html: r.html, diags: r.diags };
  };

  // a session's overlay: inside the container, outside the commit root (a
  // whole-view swap keeps it: data-tsr-shell), positioned at its origin
  const overlayOf = (s) => {
    if (!s.overlay) {
      const o = document.createElement('div');
      o.dataset.tsrShell = 'overlay';
      o.style.cssText = 'position:absolute;left:0;top:0;width:0;height:0;overflow:visible;z-index:20';
      if (getComputedStyle(s.container).position === 'static') s.container.style.position = 'relative';
      s.container.appendChild(o);
      s.overlay = o;
    }
    return s.overlay;
  };

  // Behaviours are installed per session, after its first commit. A
  // behaviour that throws — installing, or in a handler it registered
  // through ctx — is disabled (uninstalled) and the rest go on.
  const installBehaviors = (s) => {
    for (const b of behaviors) {
      ensureBehaviorCss(b);
      const offs = [];
      let uninstall = null, disabled = false;
      const teardown = () => {
        for (const off of offs.splice(0)) off();
        try { uninstall?.(); } catch (e) { console.warn(`tsr: behavior ${b.name}: uninstall failed`, e); }
        uninstall = null;
      };
      const disable = (e) => {
        if (disabled) return;
        disabled = true;
        console.warn(`tsr: behavior ${b.name} failed and is disabled`, e);
        teardown();
      };
      const guard = (fn) => (...a) => {
        if (disabled) return undefined;
        try {
          const r = fn(...a);
          if (r && typeof r.catch === 'function') r.catch(disable);
          return r;
        } catch (e) {
          disable(e);
          return undefined;
        }
      };
      const ctx = {
        container: s.container,
        root: () => s.view.root,
        get overlay() { return overlayOf(s); },
        settings: s.settings,
        setting: (path) => settingOf(s.settings, path),
        applyContract: (el) => applyContract(el, s.settings),
        onCommit(cb) {
          const g = guard(cb);
          s.onCommit.add(g);
          const off = () => s.onCommit.delete(g);
          offs.push(off);
          return off;
        },
        listen(target, type, fn, o) {
          const g = guard(fn);
          target.addEventListener(type, g, o);
          const off = () => target.removeEventListener(type, g, o);
          offs.push(off);
          return off;
        },
        anchors: {
          byLabel: (label) => anchorsOf(s).get(label) ?? null,
          byId: (id) => {
            const anchors = anchorsOf(s);
            return id?.startsWith(s.idPrefix) ? anchors.get(id.slice(s.idPrefix.length)) ?? null : null;
          },
        },
        refAt: (el) => refAt(s, el),
        ops: {
          fragment: (label) => fragmentOf(s, label),
          paginate: (spec) => paginateOf(s, spec),
          offsetAt: (node) => offsetAt(s.view, node),
          elementsAt: (byte) => elementsAt(s.view, byte),
        },
        // a method the handle forwards (handle.print → print's)
        expose(name, fn) {
          s.exposed[name] = fn;
          offs.push(() => { if (s.exposed[name] === fn) delete s.exposed[name]; });
        },
      };
      try {
        uninstall = b.install(ctx) ?? null;
      } catch (e) {
        disable(e);
        continue;
      }
      s.uninstalls.push(teardown);
    }
  };

  const disposeSession = (s) => {
    if (s.disposed) return;
    s.disposed = true;
    for (const un of s.uninstalls.splice(0).reverse()) un();
    s.uninstallCopy?.();
    s.overlay?.remove();
    s.onCommit.clear();
    if (sessions.get(s.container) === s) sessions.delete(s.container);
    worker.postMessage({ type: 'dispose', docId: s.docId });
  };
  const superseded = (what) => new Error(`${what}: superseded (the session was disposed)`);

  return {
    async typeset(source, container, opts = {}) {
      const { progressive = true, fonts, onSemantic, onUpgrade } = opts;
      ensureCss();
      ensureFontFaces(fonts);
      // one document per container: a new typeset replaces its session
      const prev = sessions.get(container);
      if (prev) disposeSession(prev);
      const id = nextId++;
      // one settings document (plan P1-03; docs/settings-table.md): the
      // legacy named options (widthPx, fontFamily, lang, …) are sugar for
      // their rows, and opts.settings wins over them
      const base = settingsFromOptions(opts);
      const s = {
        docId: id, container, settings: base, view: createSession(container), disposed: false,
        onCommit: new Set(), uninstalls: [], exposed: {}, overlay: null, uninstallCopy: null,
        anchorsHead: undefined, anchors: new Map(), idPrefix: settingOf(base, 'render.idPrefix'),
      };
      sessions.set(container, s);
      // the session measure: relayout() moves it so later update()s follow
      let width = opts.widthPx ?? base.host?.width ?? container.getBoundingClientRect().width;
      const settingsAt = (w) => ({ ...base, host: { ...(base.host ?? {}), width: w } });
      applyContract(container, base);
      let semanticHtml = null;
      const res = await request(
        { type: 'typeset', id, source, settings: settingsAt(width), progressive,
          fontFaces: fonts, baseUrl: document.baseURI },
        (html) => {
          if (s.disposed) return;
          semanticHtml = html;
          if (progressive) {
            container.innerHTML = html; // first paint: browser flows it
            onSemantic?.(html);
          }
        },
      );
      await settleFonts(fonts);  // paint with the faces the engine measured
      if (s.disposed) throw superseded('typeset');
      const before = rectsOf(container);
      await commitTo(s, res);
      const upgrades = upgradeRecords(s.view, before);
      onUpgrade?.(upgrades);
      s.uninstallCopy = copy(container) ?? null;
      installBehaviors(s);
      const view = s.view;
      const handle = {
        // the document as one HTML string (the legacy shape), built on demand
        get html() { return sessionHtml(view); },
        diags: res.diags,
        heightPx: res.heightPx,
        timings: res.timings,
        semanticHtml,
        upgrades,
        // (plan P3-05) a DOM position's source byte, and the elements that
        // show a source byte (the VS Code preview's jump and reveal)
        offsetAt: (node) => offsetAt(view, node),
        elementsAt: (byte) => elementsAt(view, byte),
        // the commit session (the blocks held, their keys and elements):
        // read-only, for hosts' diagnostics and tests
        session: view,
        // Editing session (editor-design.md §2): re-typeset new source under
        // the SAME doc handle. The worker's persistent caches make this the
        // low-latency path; a failing edit keeps the last good doc alive.
        // DOM damage is patched per-paragraph; full swap is the fallback.
        async update(newSource) {
          if (s.disposed) throw superseded('update');
          const rid = nextId++;
          const r = await request({ type: 'update', id: rid, docId: id,
            source: newSource, settings: settingsAt(width), progressive: false,
            fontFaces: fonts, baseUrl: document.baseURI, held: heldKeys(view) });
          if (s.disposed) throw superseded('update');
          // rects only for a listener (an edit's commit reads no layout)
          const before = onUpgrade ? rectsOf(container) : null;
          const c = await commitTo(s, r);
          let ups = [];
          if (onUpgrade) {
            const changed = new Set(c.ranges.flatMap((g) => g.newPids));
            ups = upgradeRecords(view, before).filter((u) => c.rebuilt || changed.has(u.pid));
            onUpgrade(ups);
          }
          Object.assign(handle, { diags: r.diags, heightPx: r.heightPx, timings: r.timings });
          return { get html() { return sessionHtml(view); }, diags: r.diags, heightPx: r.heightPx,
                   timings: r.timings, upgrades: ups, patched: !c.rebuilt, ranges: c.ranges, kept: c.kept };
        },
        // width-only re-typeset: metrics persist in the worker-held doc
        async relayout(newWidthPx) {
          if (s.disposed) throw superseded('relayout');
          const rid = nextId++;
          // the session measure moves now: an update() issued before this
          // resolves is queued behind it in the worker and must follow it
          width = newWidthPx;
          const r = await request({ type: 'relayout', id: rid, docId: id, widthPx: newWidthPx,
                                    held: heldKeys(view) });
          if (s.disposed) throw superseded('relayout');
          // (rects only for a listener, as on the edit path: plan P3-05)
          const before = onUpgrade ? rectsOf(container) : null;
          const c = await commitTo(s, r);
          const ups = onUpgrade ? upgradeRecords(view, before) : [];
          onUpgrade?.(ups);
          Object.assign(handle, { diags: r.diags, heightPx: r.heightPx, timings: r.timings });
          return { get html() { return sessionHtml(view); }, diags: r.diags, heightPx: r.heightPx,
                   upgrades: ups, timings: r.timings, ranges: c.ranges, kept: c.kept };
        },
        // P1 (pages-design.md §2): sheets at the page measure, from a fork
        // (the live document stays as it is)
        paginate: (spec) => paginateOf(s, spec),
        // print-to-PDF: the print behaviour's (behaviors/print.mjs)
        print(printOpts) {
          if (!s.exposed.print) return Promise.reject(new Error('print: no print behavior installed'));
          return s.exposed.print(printOpts);
        },
        // this document only: its behaviours, copy and overlay go, the worker
        // frees it; the container keeps the last view
        dispose: () => disposeSession(s),
      };
      return handle;
    },
    // the handle-less view of a container's session (null: none)
    sessionOf(container) {
      return sessions.get(container)?.view ?? null;
    },
    dispose() {
      for (const s of [...sessions.values()]) disposeSession(s);
      worker.terminate();
    },
  };
}
