# Publication path: webfonts, paged print, static export

Status: **implemented** (W + P1 + P2; §5 records as-built deltas). Covers
the M5 webfont deferral and milestone M8 (architecture.md §7).

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
    (paragraphs of up to three lines never split);
  - **keep-with-next**: a heading frame sticks to the next frame (as
    built: a frame whose block says `keepWithNext` never ends a sheet; the
    "≥2 lines of the next frame" refinement is not implemented);
  - **atomic**: display math, rules, raw units, figures (image+caption),
    and each code logical line (its wrapped rows + zipped sidecar rows
    share rowTop — cut only between logical lines);
  - a cut that cannot satisfy the rules falls back to the greedy cut; an
    atom taller than a sheet is set alone — never an infinite loop.

As built (plan P3-12; design T6 PageBuilder): the layouters declare a
page-break **tier** before every fragment — Normal; keep-together;
widows/orphans (2/2: inside a paragraph, and between a code block's
logical lines); keep-with-next (after a block whose rules say so: headings);
Structural (inside a wrapped code line, a table row — the rule under it
included — or a float box); Forced (a page break). Fragments joined by
Structural tiers are a box; `paginate()` (layout/paginate.cc) reads no
block kinds. When the next box does not fit, the cut is the latest legal
one; failing that the keeps relax in D-Y04's order — keep-together, then
widows/orphans, then keep-with-next — and a relaxed cut is taken only if
it moves what it keeps together onto the next sheet (else the next keep
relaxes); each relaxation is a `keep-violated` warning, the last resort
the greedy cut. An atom taller than a sheet is set alone and overflows it
visibly (`page-overflow`, the sheet not clipped). Mechanisms whose
producers come later: page floats lift to the top of their sheet if they
fit there, else of the next; footnote inserts go to the bottom of their
reference's sheet, an em below its flow, lowering its goal; a table's
header rows repeat atop its continuation sheets (without ids). Tables cut
between rows. The page's geometry is the document's PageSpec:
`page.width` × `page.height` of content inside `page.margin` (defaults:
A4 at 96 dpi, 64px margins); print options override it.
- Output: `<div class="tsr-sheet">` per page, fixed height, containing the
  page's line boxes re-based to the page top. `data-pid` is NOT emitted
  (print markup never participates in progressive swap); source spans are.
  As built (plan P3-07): a sheet wraps each block's bands in
  `<div class="tsr-band" data-b="{pid}">` (one per block per sheet) — copy's
  block identity on paged output, so paragraph breaks survive and a block
  cut across sheets joins its lines with their own separators.

### Shell

`engine.print({pageWidthPx, pageHeightPx, marginPx})` — as originally
designed (a hidden print iframe; `set_width` there and back). As built:
`handle.print()` is the print behaviour (plan P3-06,
`runtime/src/main/behaviors/print.mjs`): the worker paginates a **fork** of
the document at the page measure (`tsr2_doc_fork` with a `host.width` /
`page.height` patch and a derived `render.idPrefix`), so the live document
is never touched; the shell injects the sheets under a print root in the
PARENT document (`[data-tsr-print="root"]`, shown alone by `@media print`)
— the iframe printed blank pages in some browsers (focus and removal
races) — with Gecko's fractional-px margin clamp.

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

As built (plan P3-21; T9 M7; D-I09):
- The exporter renders with `renderTsm(source, {settings, baseDir, rootDir})`.
  Both directories are the post's folder, so the post reads only what lies
  beside or below it.
- `<html lang>` is the language the engine typeset with (`docinfo.lang`), and
  `<title>` is `--title`, else the first heading's text (`docinfo.title`),
  else the file name.
- The manifest's resources (images, bibliographies, `$.load` files) that are
  relative, or files below the post's folder, are copied beside
  `index.html` at the same relative path, so the page's references hold.
- The hydration assets are the module graph of `shell.mjs`
  (`tools/lib/module-graph.mjs`: static imports and exports, literal dynamic
  imports, `new URL('…', import.meta.url)`), plus the wasm, the math font and
  the highlighter assets. A module that the page never loads (Node code,
  tests) is no longer copied. A non-literal dynamic import is a warning
  unless annotated: a document's hole module and a host's provider module
  are loaded by URL.
- `tools/check-export.mjs` (gate G6) checks the export of `test/export/` and
  renderTsm's resources: `$.load` within the root, a denial above it, both in
  the manifest, and a caller's provider.

## 4. Milestones

- **W** fonts option end-to-end + e2e with a real woff2 fixture.
- **P1** pagination post-pass + print shell + pdf smoke test.
- **P2** exporter + node smoke test (export a corpus file, assert semantic
  HTML + zero diags + hydration script present).

## 5. As-built deltas

- **Semantic leaf styles** (unlocked by P2, benefits everything): the
  semantic serializer now renders inline text from the LEAF's effective
  Styling (instantiation already folds styled deltas onto leaves) and
  treats `Kind::styled` as transparent. Token colors, the resolver's bold
  图 n： prefixes, and block-scope style patches all reach the no-JS page —
  previously they were silently dropped. `renderSemantic` gained the
  StyleTable parameter. The token palette CSS now targets `.tsr-flow` too.
- The exporter answers NEED_TOKENS **before** the semantic render, so the
  static page carries highlight spans; the native golden runner keeps the
  browser's progressive order (semantic first), so `*.semantic.txt` goldens
  stay un-highlighted by design.
- `tokens.mjs` is environment-adaptive: under Node it hands web-tree-sitter
  filesystem paths and reads `.scm` via fs (web-tree-sitter resolves
  strings through fs there, not fetch). No global fetch polyfill.
- (Until P3-12) Table units paginate atomically (whole table, not rule-to-rule rows) —
  simpler, and blog tables are small; oversized atoms overflow their sheet
  (clipped) exactly like KP's Overfull rescue (plan P0-12): one overlong run per line, never a collapsed paragraph.
- (Plan P3-13) Footnote inserts have a producer: a flow placed
  `deferred`. Its entries' fragments are `kPagedInsert` with `insertAt` (their
  marker's source position), matched to the flow box whose line span holds
  it. The flow's rule is the separator (`kPagedInsert | kPagedHeader`): it is
  written above each sheet's inserts (a repeat band) and replaces the
  footnote skip, the rule's own band giving the space. An insert is not split
  across sheets. If its marker's line and the note cannot share a sheet, the
  line moves to the next sheet with it (the fit counts the inserts a box
  references), as in TeX. The layout dump marks the roles (`insert@n`,
  `insert-sep`). Fixture: `pages/paged-inserts`.
- (Plan P3-15) Movable boxes have producers, the page floats (`place.float`
  top / bottom / page). The flow closes over the room a movable box left:
  its hole, from its top to the next box's, is removed before the boxes
  after it are placed. A top float goes to the top of its sheet if it fits
  there, else to the next sheet's top. A bottom float goes to the foot,
  above the inserts, if it fits, else to the next sheet's foot. A page
  float waits for a sheet of floats emitted right after its sheet: it is
  stacked from the top, and one taller than a sheet overflows it.
- A float box separated from its wrapped text by a sheet cut keeps the
  narrowed lines (cosmetic under-fill beside no float) — accepted; floats
  near page boundaries are an authoring concern in print.
- Hydration embeds the source in a `text/plain` script tag and calls the
  ordinary `createEngine()` with `progressive: false` (the static page IS
  the first paint); assets ship under `out/assets/` mirroring the repo
  layout so the runtime's relative imports hold. `--no-hydrate` gives the
  pure no-JS artifact.
