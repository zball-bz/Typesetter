# Publication path: webfonts, paged print, static export

Status: **design**. Covers the M5 webfont deferral and milestone M8
(architecture.md §7). Ordered W → P1 → P2.

## 1. W: worker-scope webfonts (kills the settle race by construction)

Problem (M5 deferral): the worker measures with *its own* FontFaceSet;
document-loaded webfonts are invisible to it, and even on the main thread a
late `document.fonts` settle would invalidate metrics.

Design: fonts are **declared to the engine**, not discovered.

```
engine.typeset(src, el, { fonts: [{family, src /*url*/, weight?, style?}] })
```

- Shell injects a matching `@font-face` per entry (paint side) — idempotent
  per family+weight+style.
- Worker, before the measure loop: `new FontFace(family, url(src),
  descriptors)` → `load()` → `self.fonts.add()`, all entries in parallel,
  bounded by a 4 s race — a font that misses the deadline measures as its
  fallback (the semantic page is already up; correctness of the *contract*
  is preserved because paint uses the same fallback until the file lands).
- Metrics are therefore right on the FIRST pass. No re-typeset machinery,
  no `document.fonts.ready` listener, no estimate states. The M5 "settle
  re-typeset" deferral is closed by making settling impossible to observe.

Failure of a font URL logs a worker-side console warning; the typeset result
is still delivered.

## 2. P1: paged layout and print-to-PDF

PDF = the browser's print engine driving our paged layout. No PDF writer:
font embedding/subsetting for free, and the print dialog is the UI.

### Pagination

New engine entry `tsr_render_pages(doc, pageHeightPx)` — a *post-pass* over
the finished `LayoutResult` (no re-break, no new layout mode):

- Flatten every ParaFrame's lines to absolute y. Cut greedily at the last
  fitting line boundary, then back the cut up until all keep-rules hold:
  - **widow/orphan**: ≥2 lines of a paragraph on each side of a cut
    (1-2-line paragraphs are atomic);
  - **keep-with-next**: a heading frame sticks to ≥2 lines of the next
    frame;
  - **atomic**: display math, rules, raw units, figures (image+caption),
    table rows (rule-to-rule), and each code logical line (its wrapped rows
    + zipped sidecar rows share rowTop — cut only between logical lines);
  - a cut that cannot satisfy the rules (oversized atom) falls back to the
    greedy cut — never an infinite loop, matching KP's hard-cut fallback.
- Output: `<div class="tsr-sheet">` per page, fixed height, containing the
  page's line boxes re-based to the page top. `data-pid` is NOT emitted
  (print markup never participates in progressive swap); source spans are.

### Shell

`engine.print({pageWidthPx, pageHeightPx, marginPx})`:

1. worker: `set_width(pageWidthPx)` → typeset → `render_pages` → HTML;
2. shell: hidden print iframe — `@page { size: A4; margin: 0 }`, body
   margin = `marginPx`, `.tsr-sheet { page-break-after: always; }`, same
   TSR_CSS + fonts; `iframe.contentWindow.print()`;
3. worker: `set_width(screenWidth)` → typeset (metrics persist, fast) so
   the live document is untouched.

Default geometry: A4 at 96 dpi — 794×1123 css px, 64 px margins → content
666×995. All numbers are options.

e2e: Chromium `page.pdf()` + pdftoppm smoke (page count, no overflow), plus
DOM assertions on sheet heights and keep-rules with a crafted document.

## 3. P2: static export

Build-time Node tool — no browser, no canvas, and therefore *no typeset
pass*: the exported artifact is the **semantic page** (resolver-complete:
numbers, refs, TOC, glossary, syntax-highlight token spans all present),
plus optional hydration that upgrades to the typeset rendering client-side.

`node tools/export-static.mjs post.tsm -o out/ [--no-hydrate] [--title T]`

- Runs compile → execute → ingest in Node against the wasm build (the
  corpus runner already proves this path); writes `out/index.html` with the
  semantic HTML inlined, TSR_CSS inlined, diags to stderr (nonzero exit on
  errors).
- Tokens: the native token provider path is browser-only; the exporter runs
  the same `tokenize()` module under Node (web-tree-sitter works in Node) so
  highlighted code is static too. Images: intrinsic dims unknown in Node —
  exported semantic `<img>` needs none (the browser flows it); hydration
  re-measures live.
- `--hydrate` (default): copies the runtime + wasm + hl assets + fonts into
  `out/assets/` and appends a module script that calls `createEngine()` on
  the article container with the source embedded — progressive upgrade then
  works exactly like the playground. This is the seam a static blog
  framework will later call directly.

## 4. Milestones

- **W** fonts option end-to-end + e2e with a real woff2 fixture.
- **P1** pagination post-pass + print shell + pdf smoke test.
- **P2** exporter + node smoke test (export a corpus file, assert semantic
  HTML + zero diags + hydration script present).
