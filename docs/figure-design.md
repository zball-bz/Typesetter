# Figures: images, captions, float wrap

Status: **implemented** (F1 + F2; §8 records as-built deltas). Companion to
[document-model.md](document-model.md) and
[architecture.md](architecture.md) §7 (milestone F). Supersedes nothing; the
resolver's `group{role:"figure"}` numbering hooks (figNo, `supFigure`) were
laid in M4 and finally get content here.

## 1. Model

One new leaf kind:

```
KIND(image, 26)      args: src (URL), alt, w (display px), h (display px),
                           scale (fraction of measure), side ("left"/"right")
```

`w`/`h` double as *author-declared intrinsic dims*: when both are present the
engine never asks the host for the image (deterministic native tests, static
export). Otherwise intrinsic dims arrive through the pull loop (§2).

Ops contract: `OPS_VERSION 5` — KIND image=26, ARGK scale=28, alt=29,
side=30. As always: regen `ops.gen.mjs`, re-record every fixture.

Authoring surface:

- `#!figure(src: "x.png", alt: "…", label: "fig-x")` region — the `figure`
  constructor (a Body constructor in runtime/src/shared/stdlib.mjs, plan
  P2-03; `docs/ctor-design.md`) makes
  `group{role:"figure", label}` with an `image` node first and the region's
  paragraphs after it as the caption. `#!figure` keeps working with no `src`
  (a figure whose body is a table/code/math block — the caption is still
  numbered).
- `#image("x.png", {scale: 0.5})` splice for inline/handler use (a bare
  image block without figure numbering). Inside a paragraph it is an
  inline object (plan P1-13, docs/shaping-design.md §6): a box of its
  declared or intrinsic size on the baseline (`scale` applies to the block
  form only), a dashed 1em placeholder when unsized or unsafe — it used to
  vanish.

Resolver: `group{role:"figure"}` already increments `figNo` and feeds
`@label` → `图 n`. New: the scan pass prepends **`图 n：`** (bold, via
`cfg.supFigure` + `：`) to the figure's first paragraph child, mirroring how
`mathblock` gets `ArgK::name = "(n)"`. Figures without a caption paragraph
get no prefix (the number still exists for refs).

As built (plan P3-03): the constructor marks the caption paragraphs as the
figure's caption part (`slot: "caption"`, besides `role: "caption"`), and
the figure row's site attaches at that part (`at: "caption"`). A figure
whose body is one table numbers as a table (表 1: row `table-figure`,
D-S01; `kind:` overrides: `#!figure(kind: "figure")`), a figure inside a
figure is a subfigure (图 1(a), D-S02), and a table inside a figure is
unnumbered with its label naming the figure (`figure-table`, refers-to
enclosing) — docs/semantics-design.md §1.

## 2. NEED_IMAGES: the third pull resource

Exactly symmetric to NEED_MEASURE and NEED_TOKENS (code-design.md §2): the
engine wants *intrinsic CSS dimensions*, never pixels.

- As built (plan P1-19): the `boxInfo` row of the resource table
  (docs/host-protocol-design.md §4a), one need per distinct src, scanned
  after resolve for every `image` node without both author dims. The answer
  stays in the table: emit fills only the dims the author left out.
  (Plan P3-32: emit reads no answer. An inline image's box is settled in
  Measure, a block or floated image's size spec in Layout — host-protocol-design
  §1 "Image sizes after Emit"; the failed one's placeholder is layout's
  `Fragment::placeholder`.)
- `typeset()` returns `NeedMeasure` while any request is unanswered.
- `tsr_measure_requests` JSON gains `"images": [{id, src}]`;
  `tsr_provide_image(doc, id, wPx, hPx)` answers one.
- Worker: `fetch(src)` → `createImageBitmap` → `{width, height}`, cached per
  src for the worker's lifetime. **Every request must be answered**: failure
  provides `0×0`, the engine emits a `image-load` warning and renders a
  placeholder (dashed box at measure×measure/3 carrying the alt text) so the
  document never stalls and the failure is visible.

DPR note: bitmap pixels are taken as CSS px (1x convention). High-DPI assets
declare `scale:` or `w:` — same contract as HTML without `srcset`.

## 3. Sizing and block layout

Display width, first match wins:

1. `w` arg (px, clamped to the measure),
2. `scale` × measure,
3. min(intrinsic width, measure).

Height follows the aspect ratio (declared `h` only participates when paired
with `w`, and then defines the ratio together with it).

Block figure (no `float`): the image unit lays out like display math —
centred on the measure, advance = display height. The caption paragraphs are
ordinary Text units marked `ragged` + a new `centered` flag: layout shifts
each line right by slack/2 and never justifies. Caption styling: body size ×
0.92, no paragraph indent.

As built (plan P1-18; docs/layout-design.md): the box tree makes the image a
`Replaced` leaf (painter `Image`, `floatSide` 0/1/2 from its side) and emit
gives it an `ImageData` payload (`src, alt, ImageSize`: the intrinsic px, the
scale, placeholder). Since plan P1-16 emit reads no width: layout resolves
the display box at the measure (`resolveImageSize`, the rule above) and an
unsafe scheme is reported once, by the image-request scan. The figure group
is a stack block carrying the label; its first fragment (the image) carries
the id. Its paragraphs are captions because the role table says so (`figure`
→ captions), not by a role-string compare.

## 4. Float wrap (环绕)

TeX `parshape` semantics, greedy and explicit — not CSS floats re-derived.

`LineWidths` (break.h) grows a prefix form:

```
struct LineWidths {
  Su constant;
  Su narrow = 0;     // width for the first `narrowK` lines (0 = none)
  u32 narrowK = 0;
  Su at(u32 i) const { return (i < narrowK && narrow) ? narrow : constant; }
};
```

The float exclusions live in layout (plan P1-15: `ExclusionMap` in
`layout.cc`, at layout's own cursor — it used to be a `FloatTracker` in
`Doc::typeset()` whose decisions layout replayed from five unit fields):

- A float image unit registers `{side, occlW = imgW + gap, heightSu}`,
  where `heightSu` covers image + its caption (see below) + one paraGap of
  clearance. `gap` = 1 em.
- Each following **Text** unit gets `narrowK = ceil(remaining / baseLeading)`
  and `narrow = lineWidth − occlW`; after breaking, `remaining` decreases by
  `lines × baseLeading` plus the inter-unit gap.
- Every **non-text** unit (code, table, math, rule, another figure) *clears*:
  the tracker charges the leftover height so layout starts it below the
  float. Simple, predictable; magazine-style code wrap is out of scope.
- Layout breaks each paragraph with the widths the exclusions give at
  that point and lays its lines out in the same pass, so breaking and
  layout cannot disagree; no decision is stored on the units.

Approximation, documented: occlusion is counted in `baseLeading` lines; a
taller line (inline display-ish math) over-clears by the excess — the
narrowed lines reach below the float. Identical to TeX's
`\parshape`-in-lines behaviour; ParShape's conservative bands over real
line heights replace it (design T6).

Float figure caption: broken to the float's width and rendered inside the
float box (below the image), reusing `TableCell` — `u.cells[0]` broken to
`imgW`, exactly the sidecar pattern. The float box's total height =
image + caption rows.

Layout: the float image unit contributes **zero advance** (out of flow); it
renders at the measure's left or right edge at the current y. Text lines of
narrowed units get `left += occlW` when the float is on the left.

As built (plan P3-08; design T6 "ParShape + ExclusionMap") — this replaces
the prefix model above:

- A paragraph's shape is a `ParShape` (break.h): a slot `{left, width}` per
  explicit line and a `rest` — TeX's `\parshape`. The breaker reads the
  widths, layout the offsets; the slot width is the one definition of the
  measure (justification and centring slack come from it).
- Floats are side-tagged boxes in document coordinates (y from the top, x
  from the measure's start; a float in a list stands at its indent and
  pushes outer text from there). Line i of a paragraph starting at yTop is
  narrowed by every float meeting its **conservative band**
  `[yTop + i·minAdv, yTop + (i+1)·maxAdv)` — minAdv = baseLeading, maxAdv
  = the stream's tallest possible line — so no line overlaps a float
  whatever advances the lines realize (unitFloatsNeverOverlap checks it),
  in one pass. A float leaves 1em beside the text; a list item's text
  beside a start float keeps its marker's room.
- A float takes **zero advance and no gap after it** (the next block stands
  at the float's top). A float of the same side it would overlap stacks
  below it (a paragraph gap apart); one of the other side coexists unless
  the column between them would be narrower than `layout.minWrapWidth`
  (8em), then it goes below.
- A paragraph whose column beside the floats is narrower than
  `layout.minWrapWidth` clears them (never a sliver, never text over a
  float); a non-paragraph block clears every float its whole box meets
  (D-Y02), standing its gap below them.

## 5. Rendering, copy, semantics, safety

- Typeset serializer: `<img class="tsr-img" src alt draggable="false">`
  absolutely positioned; caption lines are ordinary text lines. Placeholder:
  `<div class="tsr-imgph">alt</div>`.
- `src` sanitizing: relative, `http(s):` and `data:image/*` only; anything
  else (notably `javascript:`) renders the placeholder + a warning. Attrs
  are HTML-escaped like all others.
- Copy (§9.3): image blocks are synthetic — skipped, like `data-syn="ref"`;
  the caption copies as text including the 图 n： prefix.
- Semantic serializer: `<figure><img src alt><figcaption>…</figcaption></figure>`
  (the group{role:figure} case), so the no-JS page is real HTML. As built
  in P3-03 the figcaption holds the caption part (a figure-box element with
  none: its paragraphs).

## 6. Testing

- Native goldens (`figures.tsm`): declared-dims figures — blocks/layout/html
  stages; a float fixture whose breaks golden pins the parshape widths
  (narrow rows visibly shorter). Pull path: unit test drives
  `provideImage` by hand (native tests answer 512×384 for any src).
- e2e: data-URI PNGs (deterministic, offline); assert centred img rect,
  wrapped lines' right edges beside a right float, copy skipping the image,
  placeholder on a bad src. Graphical check via screenshot as usual.

## 7. Milestones

- **F1** block figures: ops v5, image kind, NEED_IMAGES loop, sizing,
  centred layout + captions + numbering/refs, placeholder path, both
  serializers, copy, native+e2e.
- **F2** float wrap: LineWidths prefix, float exclusions (ExclusionMap in layout since P1-15), float caption cells,
  clearing rules, tests.

## 8. As-built deltas

- **Display sizing is scale-only** (§3 simplified): `w`/`h` args are purely
  the intrinsic dims; `scale` is the one display control (else natural size
  capped at the measure). When the author declares only one of them, the
  pulled dims fill the other by aspect ratio and the declared one stays
  (plan P0-11, defect #24; P1-19 separates author and intrinsic dims). An absolute-px display width was dropped — `scale`
  covers the blog cases and keeps one source of truth.
- Captions keep the body size (no 0.92 shrink — per-leaf style composition
  wasn't worth it); they are ragged + centred + unhyphenated, and skip 首行
  缩进. (As built in P3-09, D-Y05: justified and hyphenated, one line
  centred — `par.singleLine: center` — for block and float captions alike;
  float caption rows read their caption paragraphs' own properties.)
- The KP breaker needed **zero changes** for parshape: the DP always carried
  the line index and called `widths.at(e.line)` — only the `LineWidths`
  struct grew the prefix form.
- The float exclusions live at layout's cursor (plan P1-15; they used to
  run in `Doc::typeset()` and replay through `u.narrow/narrowK/narrowLeft/
  floatClearSu`) and advance with the stacks' gaps (plan P1-18: the gap of
  the deepest stack holding both blocks — a list's paraGap/3, else paraGap). Float registration charges image +
  caption rows + one paraGap of clearance; narrowing engages only when
  `occl < measure − 1px`.
- Float caption rows advance at `baseLeading` flat (no vmet/math extents) —
  formulas in float captions may sit tight; block-figure captions are
  ordinary text units and unaffected. Non-para kids of a *float* figure are
  dropped (block figures flow them normally).
- A float pushing past the last unit extends `docHeightSu` via a watermark
  in layout (the box must not clip at the document edge).
- The layout replay keeps the historical justification formula for full
  lines (`cfg.widthPx − indent`, unfloored) and uses `suToPx(narrow)` only
  for narrowed lines — zero golden churn on non-figure fixtures. (Retired
  in P3-08: every line's slack is its slot's width.)

- (plan P2-08) The figure constructor tags its paragraphs `role: caption`
  (the layout still finds captions by figure depth until P3-01 reads the
  role).

### Placement for any block (plan P3-15; design T6 S13)

- `place: {float: 'left' | 'right', width, gap}` floats any block or
  container: a figure whose body is a table, an aside, a code block. Its
  width is `place.width`: a length, or a percent of the room. Unset, it is
  FitBody, the max-content of its non-caption content, at most the room.
  The block is laid out detached (`layoutDetached`: a flow root at that
  width; no float leaks in or out), then placed beside the floats already
  there by the image floats' rule (ExclusionMap::place). The text beside it
  narrows by its exclusion, whose gap is `place.gap` (unset: an em).
- An image with `side` (its `place.float` alias) keeps its own float leaf
  (image plus caption rows), whose geometry FitBody reproduces. Its output
  is unchanged.
- `place: {float: 'inline', width}` makes a block an inline block.
  Consecutive inline siblings are laid out detached at their widths and set
  side by side, a gap apart. A line is full when the next box does not fit.
  Lines are centred, or follow the container's `par.align`; boxes are
  top-aligned, with the stack gap between lines. This is the way to put
  subfigures side by side; nested figures still stack unless they say so.
- `place: {float: 'top' | 'bottom' | 'page'}` makes a page float. It stays
  in place on screen. On paged sheets it is one movable box, and its frame
  moves with it:
  - `top` goes to the top of its sheet, or of the next one;
  - `bottom` goes to the foot, above the footnote inserts;
  - `page` goes to a sheet of floats after the sheet where it stood.

  The flow closes over the room it left.

