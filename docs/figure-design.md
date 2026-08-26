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

- `#!figure(src: "x.png", alt: "…", label: "fig-x")` region — the default
  builder (executor, next to `tableBuild`) makes
  `group{role:"figure", label}` with an `image` node first and the region's
  paragraphs after it as the caption. `#!figure` keeps working with no `src`
  (a figure whose body is a table/code/math block — the caption is still
  numbered).
- `#image("x.png", {scale: 0.5})` splice for inline/handler use (a bare
  image block without figure numbering).

Resolver: `group{role:"figure"}` already increments `figNo` and feeds
`@label` → `图 n`. New: the scan pass prepends **`图 n：`** (bold, via
`cfg.supFigure` + `：`) to the figure's first paragraph child, mirroring how
`mathblock` gets `ArgK::name = "(n)"`. Figures without a caption paragraph
get no prefix (the number still exists for refs).

## 2. NEED_IMAGES: the third pull resource

Exactly symmetric to NEED_MEASURE and NEED_TOKENS (code-design.md §2): the
engine wants *intrinsic CSS dimensions*, never pixels.

- `Doc::imageReqs` scanned after resolve: every `image` node whose intrinsic
  dims are unknown (no author `w`+`h`).
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

Emit: new `FlowUnit::K::Image` with `imgSrc, imgAlt, imgW, imgH (Su),
floatSide (0/1/2)`. The group walk already flattens the figure group; the
caption keeps the figure's label anchor on its first line via the existing
`pendingAnchor` path.

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

A `FloatTracker` runs inside `Doc::typeset()`'s unit walk (the only place
that already visits units in reading order with breakpoints in hand):

- A float image unit registers `{side, occlW = imgW + gap, heightSu}`,
  where `heightSu` covers image + its caption (see below) + one paraGap of
  clearance. `gap` = 1 em.
- Each following **Text** unit gets `narrowK = ceil(remaining / baseLeading)`
  and `narrow = lineWidth − occlW`; after breaking, `remaining` decreases by
  `lines × baseLeading` plus the inter-unit gap.
- Every **non-text** unit (code, table, math, rule, another figure) *clears*:
  the tracker charges the leftover height so layout starts it below the
  float. Simple, predictable; magazine-style code wrap is out of scope.
- The tracker's decisions are *stored on the units* (`u.narrowK, u.narrow,
  u.narrowLeft` for text; `u.floatClearSu` advance for clearing units;
  placement on the image unit) — layout replays them verbatim instead of
  re-deriving, so break and layout cannot disagree.

Approximation, documented: occlusion is counted in `baseLeading` lines; a
taller line (inline display-ish math) under-clears by the excess. Identical
to TeX's `\parshape`-in-lines behaviour; acceptable for a blog.

Float figure caption: broken to the float's width and rendered inside the
float box (below the image), reusing `TableCell` — `u.cells[0]` broken to
`imgW`, exactly the sidecar pattern. The float box's total height =
image + caption rows.

Layout: the float image unit contributes **zero advance** (out of flow); it
renders at the measure's left or right edge at the current y. Text lines of
narrowed units get `left += occlW` when the float is on the left.

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
  (the group{role:figure} case), so the no-JS page is real HTML.

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
- **F2** float wrap: LineWidths prefix, FloatTracker, float caption cells,
  clearing rules, tests.

## 8. As-built deltas

- **Display sizing is scale-only** (§3 simplified): `w`/`h` args are purely
  the intrinsic dims; `scale` is the one display control (else natural size
  capped at the measure). An absolute-px display width was dropped — `scale`
  covers the blog cases and keeps one source of truth.
- Captions keep the body size (no 0.92 shrink — per-leaf style composition
  wasn't worth it); they are ragged + centred + unhyphenated, and skip 首行
  缩进.
- The KP breaker needed **zero changes** for parshape: the DP always carried
  the line index and called `widths.at(e.line)` — only the `LineWidths`
  struct grew the prefix form.
- The float tracker lives in `Doc::typeset()`'s unit walk and mirrors
  layout's exact gap accounting (tightAbove paraGap/3, inter-block paraGap);
  decisions replay via `u.narrow/narrowK/narrowLeft/floatClearSu`. Float
  registration charges image + caption rows + one paraGap of clearance;
  narrowing engages only when `occl < measure − 1px`.
- Float caption rows advance at `baseLeading` flat (no vmet/math extents) —
  formulas in float captions may sit tight; block-figure captions are
  ordinary text units and unaffected. Non-para kids of a *float* figure are
  dropped (block figures flow them normally).
- A float pushing past the last unit extends `docHeightSu` via a watermark
  in layout (the box must not clip at the document edge).
- The layout replay keeps the historical justification formula for full
  lines (`cfg.widthPx − indent`, unfloored) and uses `suToPx(narrow)` only
  for narrowed lines — zero golden churn on non-figure fixtures.
