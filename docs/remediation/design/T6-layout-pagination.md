# T6-layout-pagination

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

Layout should be a pure function: a width-independent box tree goes in, and positioned fragments on one vertical list (VList) come out. Every block feature is data on that tree, not a branch inside some stage.

How the tree is built:
- A dedicated box-tree builder runs after resolve.
- It chooses each block's layouter from the content model, never from a trait.
- It attaches typed traits that T4's cascade resolved into a trait table kept outside StyleId.
- It records every size that depends on the container as a SizeSpec.

What the five layouters (Paragraph, Stack, Replaced, Grid, Table) share:
- one su-only breaker with explicit TeX semantics: discard rules, discretionaries, a Forced paragraph end, per-alignment line-end glue, and a final-pass rescue;
- one line materializer;
- one detached-layout entry for floats, inline blocks, cells and footnote inserts;
- one side-tagged exclusion model, queried over conservative line bands;
- one VList whose vertical penalties are tiered rather than weighted.

Pagination is an optional stage over the VList. It relaxes keeps in a declared order instead of overflowing. Paint receives only frame-relative fragments with class hooks.

Corrections to the provisional layering:
1. Box-tree construction is an L6-entry stage (engine/src/boxtree/), not part of emit. L5 produces only item lists.
2. Block traits are resolved at L3 into a trait table, never into Styling.
3. break/ is an L6 service called by the Paragraph layouter, not a stage between L5 and L6.
4. paginate/ is an L6.5 stage that the screen path skips.

## Diagnosis

Seven root causes produce the ad-hoc items.

1. **No item semantics.** LinebreakBlock fuses box width, glue capacity (spaceWidth), paint weight (stretchWeight) and penalty under BF_* flags (emit.h:10-51). So each layer re-derives what is discarded at a break, what a hyphen adds and how slack is shared:
   - the breaker counts the trailing glue (break.cc:63-70);
   - layout trims it and re-derives the CJK gaps (layout.cc:498-522);
   - render prints '-' and re-derives the gaps again (typeset_html.cc:485, 531-567).
   The claim that cost model and renderer 'agree by construction' (document-model.md:174) is therefore false.
2. **Magnitude sentinels instead of tags.** INF (double), BREAK_INF (float) and the 1e17 retry threshold share one scale (break.cc:9, emit.h:53, break.cc:183). Two consequences:
   - the cost of the paragraph's last line depends on the last block's penalty (break.cc:73, 106-118);
   - an infeasible paragraph collapses into one line (break.cc:122).
3. **Breaking runs before layout.** Doc::typeset breaks every unit before any geometry exists (doc.h:258-361):
   - floats are predicted in baseLeading lines (doc.h:356);
   - table and sidecar widths are computed in api/;
   - decisions are replayed through five mutable FlowUnit fields (emit.h:102-106).
4. **No block protocol.** FlowUnit is a union of every kind's fields (emit.h:63-116). It is dispatched by kind again in doc.h, in layout, through LineBox.special, in renderLineBox and in renderPages. Nested streams are throwaway FlowUnits, and the line loop is copied four times (layout.cc:49-83, 312-358, 423-485, 491-602).
5. **Emit reads the measure** (emit.cc:591, 721-739), which breaks 'relayout = re-break only' (architecture.md:98).
6. **No vertical model:**
   - gaps are constants plus an inherited tightAbove flag (layout.cc:28, 606; emit.cc:535-537);
   - keeps and atomicity are re-derived from kinds inside the HTML serializer (typeset_html.cc:681-737);
   - alignment is five flags (layout.cc:569-594).
7. **Semantics travel by proxy:**
   - the role strings 'figure' and 'sidecar-lines' (emit.cc:781, 587);
   - Kind::heading, read in the paginator through a ContentNode pointer (typeset_html.cc:705);
   - a CSS colour string for comments (emit.cc:618).
   With no trait record for layout to read, user regions get no geometry at all.

Together these mean that every block feature edits emit, api, break, layout and both serializers.

## Abstractions

### LineBreaker (ItemView + LineEnds + BreakParams + measureLine)

**owner_layer**

L6 service: engine/src/break/{items.h,break.h,break.cc}.
- Called only by the Paragraph layouter and by intrinsic sizing; never by api/.
- Input is the su projection of T5's item list; ItemKind is T5's enum.
- Until S16, the input comes from an adapter over LinebreakBlock.

**purpose**

One breaking semantics shared by the optimizer, the materializer and paint. It defines:
- what is discarded at a break;
- what a discretionary adds on each side;
- what is forced or forbidden;
- how line-end glue enters both cost and paint for each alignment;
- what happens when nothing fits.

The breaker reads only su values, so it is a pure, exactly cacheable function. Raw px enter only at materialization.

**definition**

```
// break/items.h: the breaker's projection of T5 items. Every field is integral and there is no padding, so
// the bytes are the exact cache-key domain:
//   static_assert(std::has_unique_object_representations_v<BItem>); sizeof(BItem) == 24
enum class PenTag : u8 { Normal, Forbidden, Forced };
struct BItem { ItemKind k /*Box|Glue|Kern|Penalty|Disc (T5)*/; u8 order /*0 finite, 1 fil*/; PenTag tag; u8 reserved0;
               Su w, stretch, shrink; i32 pen /*thousandths*/; u32 disc; };
struct BDisc { Su pre, post, noBreak; };                 // each quantized ceil+eps (document-model 6.1)
struct ItemView { std::span<const BItem> it; std::span<const BDisc> disc; };            // su only
struct RawView  { std::span<const double> rawPx, rawStretch; std::span<const u8> attrs /*SourceSpace|Synthetic*/; }; // paint side only
struct EndGlue { Su w, stretch; u8 order; };
struct LineEnds { EndGlue start, end;          // a line ended by an optional break
                  EndGlue lastStart, lastEnd;  // a line ended by a Forced break (paragraph end, hardbreak)
                  bool rigidInterior; };       // interior glue keeps its shrink but contributes no stretch
// presets selected by T4's 'align':
//   justify {0, 0, 0, 0+fil}
//   left    {0, 0+2em, 0, 0+fil, rigid}
//   center  {0+1em, 0+1em, 0+fil, 0+fil, rigid}
//   right   {0+2em, 0, 0+fil, 0, rigid}
struct CostModel { u8 exponent = 3 /*integer 1..4*/; double shrinkThreshold = 0.37, shrinkCoeff = 0.6, cap = 1e4; };
struct BreakParams { CostModel cost; double tolerance = kNoTolerance; Su emergencyStretch = 0; LineEnds ends; };
struct LineFit { double ratio /*cost side, from su*/; u8 order; Su natural, overfull; u8 cls /*Ok|Loose|Overfull*/; };
struct BreakResult { std::vector<u32> breaks; std::vector<LineFit> fit; double demerits; u8 pass; };
struct LineMeasure { Su natural; Su stretch[2]; Su shrink; };
LineMeasure measureLine(const ItemView&, u32 from, u32 breakItem, const LineEnds&);
BreakResult breakParagraph(const ItemView&, const ParShape&, const BreakParams&);   // pure; cached by the caller

Rules
1. Legal breaks (TeX):
   - a Glue preceded by a non-discardable item (Box, Disc, InlineObject);
   - a Penalty whose tag is not Forbidden;
   - a Disc.
   A Forced penalty must break. The paragraph end is an implicit Forced break, whatever the last item's penalty.
2. Discard:
   - a line that ends at a Glue excludes that glue;
   - a line that ends at a Penalty keeps every item before it;
   - after any break, Glue, Kern and Penalty items are dropped up to the first Box or Disc.
   measureLine is the single definition of a line's natural width, its stretch per order and its shrink. The DP's i64 su prefix sums evaluate the same function in O(1), and debug builds assert equality on the chosen lines.
3. Disc. Inside a line it contributes noBreak. At a break, pre ends the line and post starts the next one. No layer prints a literal '-'.
4. Line ends. A line ended by an optional break adds ends.start and ends.end. A line ended by a Forced break adds lastStart and lastEnd. A hardbreak is therefore just a Forced penalty with no glue item, and it is correct under every preset.
5. Cost (PoC shape, document-model 11):
   - slack >= 0: the highest stretch order present absorbs it (fil present gives x = 0); otherwise x = slack / sum of finite stretch;
   - slack < 0: x = slack / sum of shrink;
   - Overfull if x < -shrinkThreshold;
   - per-line cost = min(cost(x), cap), with the integer power computed by multiplication;
   - demerits = sum of costs + sum of pen/1000, dimensionless;
   - the class is explicit: Loose if cost > tolerance;
   - no magnitude sentinel exists.
6. Passes:
   - (1) within tolerance, skipped when there is no tolerance (the default, matching today);
   - (2) with emergencyStretch added to every line, skipped when it is 0 (the default);
   - final pass, with TeX's rescue: if at legal break b every active node's line is Overfull and no feasible line ends at b, the best active node by the total order gets an artificial break at b. No demerits are added, cls = Overfull, and an 'overfull-line' diagnostic is emitted. Each overfull line therefore holds exactly one unbreakable run, and layout sets its spacing at the shrink limit.
7. Search:
   - the active list is a vector in position order;
   - in every pass, a node is deactivated as soon as its line becomes Overfull, and a Forced break deactivates every earlier node;
   - this pruning is exact while line width grows with the break position, which holds whenever each item's shrink is at most its width; a Disc with a wide pre may make it inexact, as in TeX;
   - there is no window and no retry ladder.
8. Ties use a total order: demerits, then line count, then the LATER parent. Breaks that yield identical lines after discard therefore resolve to the latest position, so dump indices match layout's trimmed ranges.
9. Cache contract:
   - key = a 128-bit hash over the raw bytes of it/disc, plus the ParShape slots and the BreakParams, i.e. exactly what the DP reads;
   - value = the su-only BreakResult, validated on a hit by the item count;
   - lifetime and eviction are T9's product-cache policy.
10. Numerics: tsr_core compiles with -ffp-contract=off. Prefix sums are i64 su, converted to px once per line.
```

**surface**

No new markup. The knobs are inherited block properties in T4's single registry:
  align: 'justify' | 'left' | 'center' | 'right'   (selects a LineEnds preset)
  breaker: {tolerance, emergencyStretch, exponent (integer 1..4), shrinkThreshold, shrinkCoeff}

The defaults reproduce today: no tolerance and no emergency stretch. TeX-like values are opt-in:
  #!aside(align: "left", breaker: {tolerance: 2, emergencyStretch: "1.5em"})
  ...
  #aside!

hardbreak (ops.def:38, dropped today) lowers (T1/T5) to a Forced penalty.

**replaces**

- engine/src/break/break.cc:9 (double INF) and engine/src/emit/emit.h:53 (float BREAK_INF) sentinels
- engine/src/break/break.cc:11-19 unbounded costFn with 0.001px floor and std::pow
- engine/src/break/break.cc:46-55 and :88-96 the ±5 cursor window and ±1 line-count pruning
- engine/src/break/break.cc:57-58 unordered_map dp entries (iteration order decides ties)
- engine/src/break/break.cc:63-70 trailing break glue counted in width and stretch
- engine/src/break/break.cc:73, 106-118 paragraph end priced by the last block's own penalty
- engine/src/break/break.cc:122 whole-paragraph {{n},INF} collapse
- engine/src/break/break.cc:149-194 XOR-packed unverified key, 1e17 retry ladder, clear() at 16384
- engine/src/break/break.h:16-21 LineWidths prefix
- engine/src/layout/layout.cc:498-499, 511-512, 540-550 trim / junction-kern / hyphen / join re-derivation
- engine/src/layout/layout.cc:513-522 CJK 'next is CJK' stretch rule (copies at typeset_html.cc:531-538, 557-567; removed in S16)
- engine/src/layout/layout.cc:569-594 isLast/noGlue/unbounded negative d and the caption centring
- engine/src/render/typeset_html.cc:481-490 literal '-' hyphen glyph
- engine/src/emit/emit.cc:130 BF_BOUND reused for math break glue (item production moves to T5/T8)

### ParShape + ExclusionMap (conservative line bands)

**owner_layer**

L6 layout/shape.{h,cc}. Each flow root owns one ExclusionMap; flow roots are the document, table cells, detached boxes and framed Stacks. The resulting ParShape is consumed by break/.

**purpose**

One paragraph-shape vector and one geometric exclusion model. Together they express floats on either side, stacked floats of any widths, floats starting at any y, hanging indents and marker push, with no per-scenario fields. They guarantee that no line overlaps a float, in a single pass with no re-break.

**definition**

```
struct LineSlot { Su left, width; };                 // relative to the container content box
struct ParShape { std::vector<LineSlot> lines; LineSlot rest;
  LineSlot at(u32 i) const { return i < lines.size() ? lines[i] : rest; } };
struct ParShapeSpec { Su hangIndent = 0; i32 hangAfter = 1; Su firstLineLeft = 0 /*MarkerSpec Push*/; };
struct Interval { Su x0, x1; };
enum class Side : u8 { Start, End };
struct Exclusion { i64 y0, y1; Su edge /*Start: right edge; End: left edge*/; Side side; Su gap; u32 ownerFrag; };
class ExclusionMap {                                 // flow-root coordinates, insertion order
  std::vector<Exclusion> v;
public:
  void add(const Exclusion&);
  Interval available(i64 y0, i64 y1, Interval box) const;  // [max(box.x0, Start edge+gap), min(box.x1, End edge-gap)] over rects meeting [y0,y1)
  i64 clearY(i64 y, Interval box, Su clearGap) const;      // first y' >= y with no rect meeting box horizontally: rect.y1 + clearGap
  i64 activeBottom(i64 y) const;                           // lowest y1 among rects active at y
};
struct LineBand { Su minAdv /*baseLeading*/, maxAdv /*max(baseLeading, maxAsc+maxDesc over the paragraph's items)*/; };
ParShape buildShape(const ExclusionMap&, Interval box, i64 yTop, LineBand, const ParShapeSpec&, Su minWrapWidth);

Semantics
- Conservative bands. Slot i is narrowed by every exclusion that meets [yTop + i*minAdv, yTop + (i+1)*maxAdv).
  Proof of no overlap: every realized advance lies in [minAdv, maxAdv], so top_i lies in [yTop + i*minAdv, yTop + i*maxAdv] and bottom_i <= yTop + (i+1)*maxAdv. Line i therefore lies inside its band, so it can never overlap an exclusion it was not shaped for.
  This takes one pass with no re-break, and it is exact (maxAdv = minAdv) for paragraphs without tall inline items.
- Side tags. Floats push from their own side, so each line gets exactly one interval, as CSS shortens line boxes instead of splitting them. A float inside an indented list is a Start rect whose edge already includes the indent, so outer text starts after it.
- Coordinates. Rects live in flow-root su. VList fragments are frame-relative, and the conversion happens at that boundary.
- If available < minWrapWidth, the paragraph starts at clearY. It never overprints and never leaves occlusion pending.
- Flow roots: the document, cells, floats, inline blocks, inserts and framed Stacks. Their floats never leak out, and the root grows to contain them.
- The breaker reads at(i).width and layout reads at(i).left. The slot width is the only definition of the measure.
- There are as many explicit slots as bands meet an exclusion or a hang; rest applies after them.
```

**surface**

Inherited block property (T4):
  par: {hang: '2em', hangAfter: 1}
Document setting:
  layout.minWrapWidth: '8em'
Exclusions come from the place trait, e.g. #!figure(place: {float: "left"}).

Example:
  #!refs(par: {hang: "2em"})
  ...
  #refs!

**replaces**

- engine/src/break/break.h:14-21 LineWidths{constant,narrow,narrowK}
- engine/src/emit/emit.h:102-106 narrow/narrowK/narrowLeft/floatShiftSu/floatClearSu replay fields
- engine/src/api/doc.h:263-361 F2 float tracker (same-side stack, opposite-side clear, non-text clear, widest-of-stack occlusion at 312)
- engine/src/api/doc.h:282-283 gap mirror of layout.cc:28/606
- engine/src/api/doc.h:344-351 1px narrowing threshold and per-unit relative occlusion
- engine/src/api/doc.h:355-357 occlusion consumed in baseLeading lines (figure-design.md:115-117 approximation)
- engine/src/layout/layout.cc:29, 37-38, 46, 558-563 float replay and indent-relative geometry
- engine/src/layout/layout.cc:570-572 two definitions of the measure
- engine/src/layout/layout.cc:609 floatBottomAbs watermark
- engine/src/emit/emit.cc:477-480 first-line indent as the only paragraph-shape feature (the indent stays a T5 item, as in TeX)

### Box tree: LayoutBlock + BlockTraits + SizeSpec, built by BoxTreeBuilder

**owner_layer**

The schema lives in engine/src/boxtree/block.h. layout/, break/, paginate/ and render/ include it, and a lint forbids them to include model.h.

The builder is engine/src/boxtree/build.cc, an L6-entry stage. Its inputs are:
- the resolved tree (L4);
- the TraitTable (L3, T4);
- the ItemLists by NodeId (L5, T5);
- intrinsic sizes (T9 resources).

Its output is width-independent and cached across relayouts.

**purpose**

The single contract between emit and layout, replacing the FlowUnit union. Structure, layouter choice, traits and size intents are fixed once per document state. Nothing in the tree depends on the measure, and nothing downstream compares role strings or kinds.

**definition**

```
enum class LayouterId : u8 { Paragraph, Stack, Replaced, Grid, Table };   // chosen by content model, never by a trait
// one size vocabulary for block widths, table tracks, float and inline-block widths
struct SizeSpec { enum K : u8 { Auto, Fixed, Percent /*of the container box, suRoundPx*/, Fr, MinContent, MaxContent, FitContent, FitBody } k;
                  Su fixed; u16 permille, fr; Su min, max; };
struct IntrinsicSize { Su w, h, baseline, minW, maxW; u16 aspectW, aspectH; u8 source /*Declared|Provided|Host|Placeholder*/; };
struct VLen { i32 v; u8 unit /*Su | Pg: floor(docParaGap*num/den)*/; u8 num, den; };
struct BoxModel { Su pad[4], border[4], indentStart, indentEnd; PaintClassId frame; bool framed /*=> flow root*/; };
struct MarkerSpec { ItemListRef content; Su gutter, sep; u8 overflow /*Hang|Push|OwnLine*/; PaintClassId cls; };
struct Place { enum M : u8 { Flow, Start, End, Top, Bottom, Page } mode; SizeSpec width /*default FitBody*/; Su gap; };
struct BlockTraits {   // compiled, interned (BlockTraitsId); never part of Styling/StyleId
  // inherited: bound at instantiation from the EMIT site's ancestors (v2 12 binding time)
  LineEndsPreset align; bool hyphenate; BreakParamsId breaker; ParShapeSpec par; VLen gap; u8 widows, orphans;
  // non-inherited: bound to the declaring node; kind default < role default < rule < explicit arg
  VLen before, after; BoxModel box; bool keepTogether, keepWithNext; u8 breakBefore, breakAfter /*Auto|Page*/;
  Place place; u8 beside /*Clear|Shrink*/; u8 media /*All|Screen|Paged*/; MarkerSpec marker; PaintClassId cls; };
struct ParagraphData { ItemListRef items; std::vector<u32> inlineBlocks; };      // InlineBlock objects, laid out detached
struct ReplacedRow   { Su h, depth, baseline; ItemListRef tag; PenTier breakAfter; };
struct ReplacedData  { PainterId painter; u32 payload; IntrinsicSize size; SizeSpec width; std::vector<ReplacedRow> rows /*1 unless T8 supplies more*/; };
struct GridData      { std::vector<GridLine> lines; GridParams params; };
struct StackData     { std::vector<LayoutBlock> kids; u64 slots /*T3 typed slot marks: caption, lead, ...*/; };
struct TableData     { TableSpec spec; std::vector<CellData> cells; };
struct LayoutBlock { NodeId node; Span span; StrRef anchor; LayouterId layouter; BlockTraitsId traits;
                     std::variant<ParagraphData, ReplacedData, GridData, StackData, TableData> data; };
struct TopBlock { u32 pid; LayoutBlock root; };          // unchanged per-root-child swap unit

Rules
- Layouter choice by content model:
  - inline content -> Paragraph;
  - verbatim content (codeblock, T2's verbatim ctor, T1's verbatim body mode) -> Grid, with a check that the font role is mono;
  - table/trow/tcell -> Table;
  - image, raw, rule, placeholder and display math -> Replaced;
  - block children -> Stack;
  - a codeblock with a sidecar slot -> Table (see TableSpec).
  A mismatch gives a diagnostic and the nearest valid layouter.
- Vertical algebra, replacing paraGap and tightAbove:
  - gap(prev, next) = max(parent.gap, prev.after*, next.before*). Here X.before* = max(X.before, firstChild(X).before*) when X is not framed; after* is defined the same way.
  - gap is inherited. The root default is 1pg (= paraGap). The list role default is 1/3 pg with integer division: 409su at 16px, 460su at 18px.
  - Descendants inherit the list gap unless they reset it. This reproduces 'everything in a list after its first unit packs tighter' (emit.cc:535-537): the gap before a list's first child is owned by the list's parent.
  - Floats are skipped in the sibling chain: zero advance and no gap after them.
- emit never reads cfg.widthPx. Size intents are SizeSpecs:
  - images: Percent(scale), or FitContent over IntrinsicSize capped at the available width;
  - placeholder: Percent(1000 permille) with aspect 3:1;
  - floats default to FitBody, i.e. shrink-to-fit over the children not in a caption slot, which reproduces today's image-width floats.
- Media. A media=Screen block, such as T3's end-of-document notes collector when notes become page inserts, is kept by flowScreen and dropped by paginate. Footnote bodies reach paginate as Inserts. Each anchor therefore occurs once per output target.
- Dumps. '--stage=blocks' keeps today's flat leaf format, printing the cumulative indent from the ancestors' BoxModel. A new '--stage=blocktree' prints containers and traits.
```

**surface**

The traits are one cross-kind attribute group in T2's per-kind schema, with reserved names. T4 registers each one's type, value domain and inherited flag.
  box: {padding: '0.5em 1em', border: {start: '2px'}, background: '--tsr-aside-bg', indent: '1.5em'}
  space: {before: '1em', after: '1em'}      gap: '1/3pg' | '0'
  keep: 'together' | 'with-next'           break: {before: 'page'}
  place: {float: 'left'|'right'|'top'|'bottom'|'page', width: '40%' | 'fit', gap: '1em'}      beside: 'clear' | 'shrink'
  media: 'screen' | 'paged'

Declared aliases, validated, each concept with one canonical spelling:
- figure/image float and side -> place.float (executor.mjs:216);
- table align: 'lcr' -> cols[i].align.

Replaced boxes:
  #image("a.png", {scale: 0.4})
  #raw(html, {h: 120})
  #raw(html, {measure: "host", minWidth: "10em"})

**replaces**

- engine/src/emit/emit.h:63-116 FlowUnit union (code/raw/image/math/table/float fields, cells overloading)
- engine/src/emit/emit.cc:591 sidebarW = sidebarFrac*(cfg.widthPx-indent)
- engine/src/emit/emit.cc:721-739 image clamp/scale/placeholder against cfg.widthPx
- engine/src/emit/emit.cc:471-476, 498, 709, 750, 767 per-kind ragged/centered/noHyphen decisions
- engine/src/emit/emit.cc:514-517, 541 list/quote indent keys and hard-coded markers
- engine/src/emit/emit.cc:535-537 tightAbove on every unit after the first in a list
- engine/src/layout/layout.cc:28, 604-606 paraGap and paraGap/3 arithmetic
- engine/src/emit/emit.cc:19, 781-783, 587-589 figDepth and role-string dispatch
- engine/src/render/typeset_html.cc:705 tb.node->kind == Kind::heading (ContentNode reached from the paginator)
- engine/src/api/config.h:64-65, 79-80 listIndentEm/quoteIndentEm and constexpr table padding

### BlockLayouter registry + layoutDetached + materializeLines

**owner_layer**

L6 layout/{layouter.h, paragraph.cc, stack.cc, replaced.cc, grid.cc, table.cc, lines.cc, detached.cc}

**purpose**

One recursive geometric protocol shared by built-in and user constructs:
- each LayoutBlock is laid out by its layouter into a content box at a cursor;
- every off-flow or inline-level block (float, inline block, cell, footnote insert) goes through one detached-layout entry;
- every paragraph-like stream produces lines through one function.

**definition**

```
struct Placement { Interval box; i64 y; };                          // flow-root coordinates
struct IntrinsicWidths { Su minContent, maxContent; };
struct LayoutCtx { const MetricStore& m; const TraitTable& t; BreakCache& bc; DiagSink& d; ExclusionMap& excl; NeedSink& needs; };
struct BlockLayouter {
  virtual IntrinsicWidths intrinsic(const LayoutBlock&, LayoutCtx&) const = 0;  // never consults host answers
  virtual void layout(const LayoutBlock&, Placement, LayoutCtx&, VList& out) const = 0; };
const BlockLayouter& layouterFor(LayouterId);                     // static table
struct DetachedBox { Su w, h, baseline; VList content; };
DetachedBox layoutDetached(const LayoutBlock&, SizeSpec width, Su avail, LayoutCtx&);   // new flow root
struct LinePolicy { LineEnds ends; u8 join /*FromBreak|Never*/; MarkerSpec marker; StrRef anchor; Su baseLeading; PaintClassId cls; };
void materializeLines(const ItemView&, const RawView&, const BreakResult&, const ParShape&, const LinePolicy&,
                      Placement, LayoutCtx&, VList& out);

Built-ins
- Paragraph:
  - steps: lay out the InlineBlocks through layoutDetached (width from their SizeSpec against ParShape.rest; they become Box items with asc/desc); then buildShape (bands), breakParagraph and materializeLines;
  - advance = max(baseLeading, maxAsc+maxDesc); Fragment.ascent = halfLeading + maxAsc;
  - join = space iff a SourceSpace glue lies in the discarded run at the break; a Disc, synthetic glue or no glue gives none; a Forced end gives newline; cells use JoinPolicy::Never (document-model 6.3);
  - endsWithHyphen is true exactly when the break is at a Disc with a non-empty pre;
  - the paint ratio is recomputed from raw px, (slotPx - naturalRawPx)/rawStretch at the realized order, while alignment offsets use measureLine's su natural (the I2 upper bound); Overfull lines are set at the shrink limit;
  - between lines it emits a VPenalty: Normal, or Avoid2 where a cut would leave fewer than orphans/widows lines;
  - the marker is a Line fragment at x = contentX - gutter. When markerW + sep > gutter, MarkerSpec.overflow decides: Hang protrudes; Push adds the excess to ParShapeSpec.firstLineLeft; OwnLine sets the marker on its own line.
- Stack:
  - content box = box minus padding, border and indent; kids follow the vertical algebra;
  - framed implies a flow root, with a Frame fragment over the kids;
  - keeps become VPenalties; a kid with place != Flow goes to placeFloat;
  - beside=Clear sets y = clearY(y, full content box, parent.gap), reproducing doc.h:316-321 and its paraGap clearance;
  - beside=Shrink lays out in available(y, activeBottom(y), box). That interval does not depend on the block's own height, so there is no fixpoint.
- Replaced:
  - width = resolve(SizeSpec, box); height comes from IntrinsicSize (aspect) or the declared value; x comes from the align preset;
  - one VList Box per row; each row tag is a Line fragment on the row baseline, dropped below the row when body.w + gap + tag.w > avail;
  - overflow gives a 'display-overflow' diagnostic;
  - source=Host files NEED_BOX{payload, widthSu}. The host answers only {h, baseline}, and layout uses a provisional height until then.
- Grid:
  - a pure wrapGridLine(GridLine, GridParams, availCols) -> rows;
  - GridParams = {font style, ch/cjk probes, snap tolerance, minCols = 8, cont: Fixed n|Hanging n|CommentAware, breakAfter char-class table}. The class table defaults to ' \t , ; ) } ] >', i.e. today's isBreakable, plus CJK kinsoku;
  - rows become GridRow fragments; Structural penalties sit inside a logical line, and Avoid2 widows/orphans between logical lines.
- placeFloat:
  - lays the block out with layoutDetached at Place.width (default FitBody);
  - Start/End: adds Exclusion{y, y+h+gap} and emits a VList Box{Float, h = 0, extent = h};
  - Top/Bottom/Page: emits a Box{Movable, h}, kept in place on screen.
- Host termination. Requested widths depend only on container widths, and container widths never depend on host answers: intrinsic() uses the declared minW/maxW, and Shrink uses activeBottom. So one layout pass discovers every NEED_BOX and one round answers them all. A defensive cap of 2 rounds degrades to a placeholder plus 'host-box-unresolved'. Provisional layouts are never painted, and answers are quantized ceil+eps.
```

**surface**

Users compose layouters through content and traits; they do not write them.
  #!aside(box: {padding: "0.5em", border: {start: "3px"}})
  ...
  #aside!

Side-by-side subfigures are InlineBlocks:
  #!figure(label: "fig-ab")
  #subfigure(width: "48%")[...]
  #subfigure(width: "48%")[...]
  Caption.
  #figure!

Fence and region handlers return ordinary group/table/raw/verbatim nodes (T2 public ctors) carrying the same traits.

**replaces**

- engine/src/layout/layout.cc:32-602 per-kind if-chain (float image, rule, raw, code, image, math, table, text)
- engine/src/layout/layout.cc:49-83, 312-358, 423-485, 491-602 four copied line loops (join only for captions, no hyphen flag on sidecar rows, flat baseLeading for float captions)
- engine/src/layout/layout.cc:112-363 the private grid breaker inline in layoutDoc (isBreakable 168-171, 8-column floors 145/163, snap 157-158)
- engine/src/layout/layout.cc:365-400 duplicated centre-on-measure code for image and display math
- engine/src/layout/layout.cc:93-95 rule y stored at its midline
- engine/src/api/doc.h:288-343 per-kind break dispatch in api/
- engine/src/emit/emit.cc:592-598, 670-674, 802-811 sub-flows built in a throwaway FlowUnit (anchors and ICtx lost)
- engine/src/emit/emit.cc:802-803 non-paragraph kids of a float figure dropped
- engine/src/render/typeset_html.cc:226-249 renderer recomputes display-math height and eqno position from cfg

### TableSpec + resolveTracks

**owner_layer**

L6 layout/table.{h,cc}. The spec is built by BoxTreeBuilder from table/trow/tcell args (T2 schema) and from the codeblock+sidecar slot.

**purpose**

One grid layouter with integer track resolution over the shared SizeSpec vocabulary. It supports:
- rules, spans, header rows and flow-root cells;
- an explicit overflow policy.

The verbatim three-box model (code | sidecar with equal-height rows) becomes an ordinary two-track table instead of a code-only special case.

**definition**

```
struct ColSpec { SizeSpec width; LineEndsPreset align; };
struct RuleLine { u8 axis /*Row|Col*/; u32 index, from, to; Su weight; PaintClassId cls; };
struct RuleSpec { enum P : u8 { Grid, Booktabs, None, Custom } preset; std::vector<RuleLine> lines; };
struct CellPlacement { u32 row, col, rowspan = 1, colspan = 1; std::optional<LineEndsPreset> halign; u8 valign /*Top|Middle|Bottom|Baseline*/; bool header; };
struct CellData { CellPlacement at; LayoutBlock content; };        // laid out with layoutDetached: a flow root
struct TableSpec { std::vector<ColSpec> cols; Su colGap, padX, padY; RuleSpec rules; u32 headerRows; };
struct TrackResult { std::vector<Su> x, w; Su overflow; };
TrackResult resolveTracks(std::span<const ColSpec>, std::span<const IntrinsicWidths> perCol, Su avail, Su gap);

Semantics
- Track resolution, all integer:
  1. Fixed and Percent tracks first; Percent = suRoundPx(permille x availPx).
  2. MinContent, MaxContent and Auto from the cells' intrinsic widths (CSS auto table layout). min-content = the widest run between legal breaks; max-content = the unbroken natural width.
  3. Fr tracks share the remaining free space as floor(free x fr / sum of fr), each clamped to its min (default 64su). The remainder stays as trailing space.
  4. If the sum of min-content exceeds avail, tracks sit at min-content and overflow. This gives a 'table-overflow' diagnostic, and the overflow extent is put on the table's Frame fragment; T7 chooses scroll or visible.
- v1 reproduces exactly: cols: n becomes n x {Fr(1), align from 'lcr'} with colGap 0 and padX inside the cell; cell width = track - 2*padX with the 64su floor (layout.cc:404-408).
- A codeblock with a sidecar slot (a T2 typed slot) lowers in the builder to:
  - cols [{Fr(1), min 64su} Grid, {Percent(sidebarFrac)} Paragraph];
  - colGap = 1 code em, rules None, one row per logical line;
  - line numbers as gutter Marker fragments.
  This reproduces code/sidecar's 7680su sidecar, 10650su code track and sidecar x=11520su (emit.cc:591, layout.cc:125-127), and snap-kerning takes its column budget from the code track.
- Rows are VList Boxes, with Structural penalties inside rows and rowspan groups and Normal penalties between rows.
- Header rows are Repeatable. A repeated copy is a fragment copy flagged RepeatCopy, with anchorId and source spans cleared.
- Cells are flow roots laid out at track width by their own layouters, so they may hold lists, code, display math and nested tables.
- valign offsets the cell's VList. halign uses a LineEnds preset, with su-natural offsets.
```

**surface**

  #!table(cols: [{width: "auto", align: "l"}, {width: "1fr"}, {width: "6em", align: "r"}], rules: "booktabs", header: 1, label: "tbl-x")
  | Name | Value | Unit |
  ...
  #table!

  #cell(colspan: 2, align: "c")[...]

`cols: 3, align: "lcr"` remains valid through the declared aliases and keeps v1 geometry.

**replaces**

- engine/src/layout/layout.cc:401-490 equal-column table with full-width rules
- engine/src/api/doc.h:322-334 duplicated colW/padding/64su floor for cell breaking
- engine/src/api/config.h:79-80 constexpr kTableCellPadEm/kTableRowPadEm
- engine/src/emit/emit.cc:647-684 tAligns string, cell truncation (669), inline-only cells (672)
- runtime/src/worker/executor.mjs:88 silent cell truncation
- engine/src/layout/layout.cc:120-128, 309-360 sidecar partition and equal-height row zip
- engine/src/api/doc.h:335-342 sidecar cell breaking in api/
- engine/src/render/typeset_html.cc:681-684 whole-table atomic pagination

### VList + Fragment (layout output contract)

**owner_layer**

L6 output: layout/vlist.h. It is consumed by paginate/ (L6.5) and projected by T7 into its display list (L7).

**purpose**

The single vertical output of layout:
- frame-relative, typed fragments with class hooks (no semantic kinds, no magic codes);
- fixed vertical glue;
- tiered penalties;
- float extents and movable page floats;
- inserts and frame brackets.

The screen and paged paths read the same structure, and anchors, baselines and copy joins are data rather than serializer state.

**definition**

```
enum class FragKind : u8 { Line, GridRow, Replaced, Frame };
struct PaintFit { double ratio /*raw px, materialized*/; u8 order; Su overfull; };
struct Fragment { FragKind kind; PainterId painter /*Replaced: Rule|Image|Placeholder|Raw|MathRow|Host*/; u32 block;
  Su x, y, w, h, ascent;    // relative to the pid frame; baseline = y + ascent (ascent includes half-leading)
  u32 a, b;                 // item range | grid byte slice | payload,row
  PaintFit fit; u8 join; StrRef anchorId /*exactly one fragment per anchored block*/;
  PaintClassId cls /*T4 class channel: list-marker, line-number, eq-tag, role classes*/; u16 flags /*Continuation|Overfull|RepeatCopy*/; };
enum class PenTier : u8 { Normal, Avoid1 /*keep-with-next, keep-together*/, Avoid2 /*widows/orphans*/, Structural, Forced };
struct VItem { enum K : u8 { Box, Glue, Penalty, Insert, FrameOpen, FrameClose } k;
  u32 pid; i64 h; u32 fragLo, fragHi; u8 boxFlags /*Float|Movable|MediaScreen*/; i64 extent /*Float: occupied height*/;
  Su glue /*fixed*/; PenTier pen; u8 flow /*insert flow id from T3*/; u32 ref /*insert block | frame fragment*/; };
struct VList { std::vector<VItem> items; std::vector<Fragment> frags; };
struct ParaFrame2 { u32 pid; i64 y; Su h, gapAfter; u32 fragLo, fragHi; };
struct ScreenFrames { std::vector<ParaFrame2> paras; i64 docHeight; };
ScreenFrames flowScreen(const VList&);   // one infinite page: Movable boxes in place, Inserts ignored, MediaScreen kept

Rules
- Boxes are line-granular: text lines, grid rows, table rows and replaced rows.
- Every gap between boxes is an explicit Penalty (Normal or a higher tier) or a fixed Glue. A page break is legal at a Penalty below Structural, or at a Glue that follows a Box. Structural and Avoid tiers are crossed only by relaxation.
- Fragments are frame-relative, so an edit above a paragraph leaves its fragments byte-identical (patchIn, editor-design 3). Each pid's fragments are contiguous.
- A wrap float is Box{Float, h=0, extent}. A page float is Box{Movable, h}.
- Paint reads only fragments (position, size, baseline, PaintFit, join, anchor, class), never Config, kinds or roles.
- Until T7 consumes fragments natively, the legacy dumps keep today's format: Rule fragments print y + h/2 (layout.cc:95), and an adapter maps FragKind and painter onto today's renderer branches.
```

**surface**

No direct markup. Traits map onto it:
- keep -> Avoid1
- widows/orphans -> Avoid2
- break: {before: 'page'} and #pagebreak() -> Forced
- space/gap -> fixed Glue

Dump: tsrc --stage=vlist.

**replaces**

- engine/src/layout/layout.h:7-32 LineBox union with code-only fields and special 0-5 (layout.h:20 documents only 0-4)
- engine/src/layout/layout.cc:621-651 and engine/src/emit/emit.cc:1019-1050 per-kind dump branches
- engine/src/render/typeset_html.cc:183-297 per-special render branches
- engine/src/render/typeset_html.cc:179, 211-216, 265-270, 310-318, 612, 752 stateful lastAnchored id bookkeeping
- engine/src/render/typeset_html.cc:625-628 margin-bottom recomputed from cfg (unrounded px vs su)
- runtime/src/main/shell.mjs:30 .tsr-marker right:100% gutter trick
- runtime/src/main/shell.mjs:65 .tsr-eqno CSS-centred, unmeasured tag

### PageBuilder (paginate stage) + PageSpec

**owner_layer**

New L6.5 stage, engine/src/paginate/, between layout and paint. PageSpec comes from T4 settings, which host print options override (T9). T7 serializes PageResult and does not paginate.

**purpose**

Pagination as a deterministic, linear page builder over the VList:
- keeps are declared as tiered penalties by layouters and traits, never by kind tests;
- they are relaxed in a declared order rather than overflowing;
- float extents are respected;
- page floats and footnote inserts are placed;
- page geometry is document data.

**definition**

```
struct PageSpec { Su w, h; Su margin[4]; Su gutterBleed; u32 firstNumber = 1; Su footnoteSkip; u16 footnoteMaxPermille; };
struct PageSlice { u32 vlo, vhi; i64 yShift; };
struct FrameSlice { u32 frag; Su y0, y1; bool openTop, openBottom; };
struct Page { u32 number; std::vector<PageSlice> body; std::vector<FrameSlice> frames;
              std::vector<PageSlice> inserts; std::vector<u32> repeats /*RepeatCopy header rows*/; i64 overflow; };
struct PageResult { std::vector<Page> pages; std::vector<std::pair<StrRef, u32>> anchorPage; };
PageResult paginate(const VList&, const PageSpec&, LayoutCtx& /*inserts on demand*/, DiagSink&);

Algorithm (linear, deterministic, lexicographic; no badness weights)
1. Accumulate the page.
   - content bottom = max(flow y, bottom of every Float begun on the page) + insert heights + footnoteSkip once;
   - glue and penalties at a page top are discarded;
   - MediaScreen boxes are skipped.
2. When the next Box would pass the goal, choose the latest legal break in (pageStart, here] that crosses no Avoid1, Avoid2 or Structural penalty. If there is none, relax in order:
   - (a) allow Avoid1 (keep-with-next, keep-together);
   - (b) also allow Avoid2 (widows/orphans);
   - (c) also allow Structural boundaries.
   Each relaxation emits 'keep-violated'. The final fallback is today's greedy cut (typeset_html.cc:736-737). A single Box taller than the page is set alone with Page.overflow > 0 and a 'page-overflow' diagnostic; it is visible, never silently clipped.
3. A Forced penalty ends the page.
4. Floats. A break is taken only where every Float begun on the page ends within the goal. Otherwise the cut moves before the Float's Box, which is today's atomic float band (typeset_html.cc:682, 734). Narrowed lines continuing past a cut stay narrowed (accepted in pages-design.md:124-126).
5. Movable boxes are lifted to the top of the current page if they fit there, else queued for the next page top. Following slices get a yShift.
6. Insert(footnote): the block is laid out on demand (layoutDetached at body width) and lowers the goal. If it does not fit, it splits at its line Boxes and the rest carries over.
7. Repeatable header rows are re-emitted as RepeatCopy at the top of continuation pages.
8. FrameOpen and FrameClose become FrameSlices with open edges.
9. anchorPage records the page of every anchor fragment.

Penalty sources
- Paragraph: Normal between lines; Avoid2 where orphans/widows (default 2/2) would be violated. This reproduces today's atomic 3-line paragraphs.
- keepWithNext (default for headings at any depth, and for a block image before its caption): Avoid1 after the block. Combined with the next paragraph's orphans this gives pages-design 2's 'sticks to >= 2 lines'.
- keepTogether: Avoid1 inside the block.
- Grid: Structural inside a logical line; Avoid2 widows/orphans counted over logical lines.
- Table: Structural inside rows and rowspan groups; Normal between rows.
```

**surface**

Settings (T4 / JSON ABI). Precedence: host print options override the document PageSpec, which overrides the default.
  page: {size: [666, 995] | 'A4', margin: '64px', footnotes: {skip: '1em', max: '50%'}}

Traits:
  keep: 'together'
  break: {before: 'page'}
  widows: 3, orphans: 2   (inherited block properties; one place, no PageSpec copy)

Page references through T3's ref form:
  #ref("sec-x", form: "page")

**replaces**

- engine/src/render/typeset_html.cc:642-765 renderPages band cutter inside the HTML serializer
- engine/src/render/typeset_html.cc:681-705 kind tests (Table/float Image atomic, Kind::heading on the TopBlock node only, block image stickAfter)
- engine/src/render/typeset_html.cc:673 rule-midline correction
- engine/src/render/typeset_html.cc:722-729 literal widow/orphan 2 for Text units only
- engine/src/render/typeset_html.cc:737, 749 oversize-atom fallback plus overflow:hidden silent clipping
- runtime/src/main/shell.mjs:423, 448-452 A4 and page-size constants
- docs/notes-design.md §1 plan to extend renderPages with footnote cost

## Subsumption (finding → mechanism)

- **subsumed** by *LineBreaker (ItemView + LineEnds + BreakParams + measureLine)*: `break-layout-pages/glue-semantics-split`, `break-layout-pages/hyphen-url-not-discretionary`
  Every consumer-side re-derivation is removed:
  - break.cc:63-70 counted trailing glue;
  - layout.cc:498-522 trims plus the CJK 'next is CJK' rule;
  - layout.cc:511-512 and 540-541 hyphen and kern handling;
  - typeset_html.cc:485 literal '-' and 531-567 compensation.

  The replacements are explicit discard rules, Disc{pre, post, noBreak}, one measureLine, and LineEnds that enter both cost and paint. The breaker is su-only. Paint realizes slack from raw px over the same items, so line membership agrees by construction, and the remaining epsilon is the intended v2 §7 margin.

  Item production belongs to T5:
  - uniform s_ref word glue, with k*s_ref CJK glue only where a gap is realized;
  - Discs for hyphenation, URLs (over the flattened paragraph) and SHY;
  - noBreak quantized ceil+eps.

  Until S16, the adapter maps BF_* blocks to TeX items: CJK char = Box, Penalty(Forbidden), Glue(0+sw), Penalty(pen), which preserves today's attached CJK stretch.
- **owned-by-other-theme** by *T5-text-shaping*: `break-layout-pages/break-policy-config-knobs`
  Break-opportunity classification belongs to T5's TextRules:
  - class-pair penalties and kinsoku (with a Forbidden penalty before any glue);
  - URL, note-marker and inline-code classes;
  - per-lang hyphenation.
  This covers config.h:39-50 and emit.cc:89-90.

  T6 provides:
  - the PenTag contract: hardbreak (ops.def:38) becomes a Forced penalty that uses LineEnds.last*;
  - BreakParams as inherited T4 properties, with defaults equal to today;
  - a hyphenate trait that must not suppress URL or emergency opportunities. T5 splits the noHyphen conflation at emit.cc:208.
- **subsumed** by *LineBreaker (ItemView + LineEnds + BreakParams + measureLine)*: `break-layout-pages/kp-window-heuristics`, `break-layout-pages/missed:3`
  An active list with Overfull deactivation in every pass, plus TeX's final-pass rescue, replaces the ±5 window, the ±1 line-count pruning and the {10,20,50,inf} ladder (break.cc:46-55, 88-96, 183-190).

  Explicit classes, a cost capped at 1e4, i32 penalties and PenTag replace the shared double scale of INF / BREAK_INF / 1e17.

  Measured with the critic's port (exact on 125/128 non-float Text units):
  - an exhaustive search and the deactivating search give identical breaks on all 116 simulated units;
  - no golden paragraph needs the rescue;
  - on the two-URL counterexample, the rescue gives one overfull run per line ([1,3,5]).
- **bug-fix-only** by *LineBreaker (ItemView + LineEnds + BreakParams + measureLine)*: `break-layout-pages/missed:5`
  Per-position entries live in a vector with the total order (demerits, lines, later parent), replacing unordered_map iteration (break.cc:57-58, 79-94, 109-118).

  Also:
  - a lint bans unordered iteration in result-affecting code;
  - -ffp-contract=off is set;
  - a CI job runs a WASM (libc++) golden subset.
- **bug-fix-only** by *ParShape + ExclusionMap (conservative line bands)*: `break-layout-pages/measure-definition-split`
  Slack comes from LineSlot.width on every line, which deletes the narrowed/non-narrowed branch at layout.cc:570-572.

  This is golden-neutral at the fixture width: suFloorPx(300) = 19200 exactly. Elsewhere it is at most 1/64 px tighter, the safe direction under v2 §7.
- **subsumed** by *ParShape + ExclusionMap (conservative line bands)*: `break-layout-pages/parshape-prefix-form`, `real-world-evidence/parshape-prefix`
  A {left, width} slot vector replaces LineWidths{constant, narrow, narrowK} plus narrowLeft/floatShiftSu/floatClearSu.

  Producers:
  - side-tagged exclusions: left and right floats at once, stacks of different widths, floats starting mid-paragraph;
  - ParShapeSpec hang/hangAfter: bibliography, notes, description lists;
  - MarkerSpec Push for labels wider than the gutter.

  Conservative bands keep v2 §10's line-index form but remove its overprint failure without a fixpoint. Drop caps are deferred (not_generalized).

  The cache hashes the slot vector.
- **subsumed** by *BlockLayouter registry + layoutDetached + materializeLines*: `break-layout-pages/float-tracker-replay`, `api-measure-code/doc-typeset-hosts-layout-logic`
  Breaking moves into the Paragraph layouter, which queries the live ExclusionMap at the real cursor. This deletes:
  - doc.h:258-361 (the per-kind loop, the tracker, table and sidecar widths);
  - the duplicated gap rule (doc.h:282-283 vs layout.cc:28, 606);
  - the five replay fields.

  Doc::typeset only sequences the stages.

  Today's baseLeading counting over-clears (figure-design.md:115-117 says 'under-clears'; S3 fixes the doc). Bands over real advances keep it safe and exact for ordinary paragraphs.
- **subsumed** by *BlockLayouter registry + layoutDetached + materializeLines*: `break-layout-pages/float-model-closed`, `real-world-evidence/figure-model-single-image`
  Place applies to any block or container. placeFloat uses layoutDetached, so tables, code, asides and whole figures float, and no kid is dropped (emit.cc:802-803). Its width defaults to FitBody over non-caption slots, which reproduces image-width floats.

  Floats on the page:
  - page placements (top/bottom/page) are Movable boxes, kept in place on screen;
  - non-paragraph blocks beside a float default to beside=Clear on the full content box (figure-design §4), with Shrink as opt-in.

  Related items:
  - side-by-side subfigures and galleries are InlineBlocks laid out by the same layoutDetached before breaking;
  - subfigure numbering and caption slots belong to T3;
  - srcset and video are a T9 resource plus a T7 painter;
  - margin figures are deferred (see not_generalized).
- **owned-by-other-theme** by *T3-semantics*: `emitter/figure-role-string-dispatch`
  T3's role registry owns counters, labels, caption and lead slots, and the semantic element, which today are string compares at resolve.cc:166, semantic_html.cc:286 and emit.cc:781.

  T6 deletes the emit/layout half:
  - figDepth (emit.cc:19, 471-476, 816-818) becomes caption-slot traits (align: center, hyphenate: false) from T4's default stylesheet;
  - float packing (emit.cc:785-814) becomes Place on the container, built by the box-tree builder from typed slot marks.

  Layout never sees 'figure'.
- **subsumed** by *Box tree: LayoutBlock + BlockTraits + SizeSpec, built by BoxTreeBuilder*: `break-layout-pages/unit-kind-switch`, `emitter/flowunit-kind-switch`
  FlowUnit::K becomes a LayouterId chosen by content model plus a typed payload variant. LineBox.special 0-5 becomes FragKind plus a painter id, which T7 dispatches through a painter table.

  This removes the re-dispatch in doc.h:288-343, layout.cc:32-602, typeset_html.cc:183-431 and 681-709, layout.cc:621-651 and emit.cc:1019-1050.

  Layering: the tree is built after resolve by boxtree/, not by emit. It references NodeId and Span instead of ContentNode*, and a lint bans model.h from layout/, break/, paginate/ and render/typeset_html.cc.

  Users do not register C++ layouters (I4). Equal footing means the same traits and content model for built-ins and user regions.
- **subsumed** by *BlockLayouter registry + layoutDetached + materializeLines*: `break-layout-pages/nested-stream-copies`, `emitter/missed:3`, `break-layout-pages/missed:2`
  Cells, float boxes, captions, sidecars and notes are flow roots whose paragraphs are ordinary ItemLists. Anchors are kept, where today they are lost in FlowUnit tmp (emit.cc:592-598, 670-674, 802-811), and dumpBreaks iterates every stream.

  One materializeLines replaces the four loops. Its fixes:
  - endsWithHyphen comes from the Disc;
  - heights include vmet and math;
  - join scans the whole discarded run at the break, as layout.cc:545-550 does today, not only the break item.

  The critic measured that a break-item rule loses the source space in figure/pull-diag's CJK/Latin caption. The later-parent tie keeps the break index at the glue there.

  Cells keep 'no data-join' (document-model §6.3) as JoinPolicy::Never.
- **subsumed** by *TableSpec + resolveTracks*: `break-layout-pages/table-closed`, `real-world-evidence/table-model-v1`, `break-layout-pages/code-sidecar-three-box`
  ColSpec{SizeSpec, align}, rule presets, spans, header rows, valign, flow-root cells and an explicit overflow policy cover the HoTT and Typst cases.

  v1 is n x Fr(1) with floor rounding (byte-identical), so Sizer::Equal and rowSync are deleted.

  The sidecar is [{Fr(1), min 64su} Grid, {Percent(f)} Paragraph] with a 1 code em gap, which reproduces 7680/10650/x=11520su. The tree stays a codeblock, so the semantic serializer keeps <pre>.

  Ownership:
  - the marker split moves out of api/doc.h:88-150 into a T2 typed sidecar slot, keeping whole-body tokenization;
  - captions come from T3 slots;
  - authoring args ride T2's open schema (S12).
- **subsumed** by *LineBreaker (ItemView + LineEnds + BreakParams + measureLine)*: `break-layout-pages/alignment-flags`
  LineEnds presets with an interior pair and a last pair replace ragged, centered, isLast, noGlue and tAligns (emit.h:67-69; layout.cc:371-373, 388-390, 460-466, 569-594). Ragged and centred lines use finite end stretch with rigid interior glue, so the breaker optimizes the rag with the same glue paint realizes.

  This avoids the degenerate fil-centring the critic measured. With the later-parent tie, all 6 centred caption units keep today's breaks at end stretch 1-3em.

  Alignment offsets use the su natural width (the I2 bound), unifying layout.cc:461-465 and 591. Display math and images are Replaced blocks with the center preset, and row tags are placed by layout.
- **subsumed** by *Box tree: LayoutBlock + BlockTraits + SizeSpec, built by BoxTreeBuilder*: `break-layout-pages/vertical-spacing-constants`, `break-layout-pages/group-role-dispatch`
  The vertical algebra has an inherited container gap plus before/after, and the Stack layouter reads BoxModel and Keeps, so user regions get exactly what list, quote and figure get. The aside at left=0 (region/figure.layout.txt:8) becomes a framed flow root with a Frame fragment carrying its class.

  The defaults reproduce today exactly, which tsrc confirms on a nested list at 18px: 460su = 1382/3. The defaults are:
  - root gap 1pg;
  - list gap 1/3 pg with integer division, inherited;
  - list indent 1.5em, quote indent 1.0em;
  - markers '•' and 'n.'.

  The headingSizeMul table (emit.cc:500-501) and CLS_BOLD are inline style, owned by T4's default stylesheet. User role defaults arrive in S12.
- **subsumed** by *BlockLayouter registry + layoutDetached + materializeLines*: `break-layout-pages/code-wrap-in-layout`, `api-measure-code/grid-is-codeblock-only`
  Grid becomes a pure wrapGridLine in layout/grid.cc. Its policy is GridParams data:
  - the char-class break table, defaulting to isBreakable;
  - minCols;
  - continuation policy;
  - snap tolerance.

  It is selected by content model, i.e. any verbatim content: codeblock, T2's verbatim ctor, or T1's verbatim body mode, with a mono font-role check. It is not selected by a 'layout' trait, because region bodies lose line structure, verified with tsrc --stage=tree.

  Token-role break classes are dropped (verbatim-design §6 'not opened'). The code-design.md §4 wording is fixed instead.

  The greedy algorithm is deliberately kept (code-design §4, verbatim-design §6).
- **owned-by-other-theme** by *T4-style-settings*: `break-layout-pages/comment-role-by-color`
  T4 provides a non-metric run role attribute, kept off Styling because MetricStore keys on StyleId (measure.h:21). GridParams.cont = CommentAware tests run.role == comment.

  This deletes the colour compare at emit.cc:618-624 and the fixture hack at test/fixtures/code/hang.tsm:3.
- **subsumed** by *PageBuilder (paginate stage) + PageSpec*: `break-layout-pages/paginator-in-serializer`, `render-runtime/paged-keep-rules-by-kind`
  pages-design §2's post-pass with no re-break is preserved, and so is its fallback. Keep tiers are relaxed in order (Avoid1, then Avoid2, then Structural) before the greedy cut of typeset_html.cc:736-737, so a keep never turns into overflow.

  Nothing tests Kind::heading or FlowUnit::K. Float extents keep floats from straddling sheets.

  The stage adds:
  - inserts;
  - page floats;
  - row-splittable tables with repeated headers;
  - PageSpec instead of shell literals.

  T7 serializes PageResult and does not paginate.
- **subsumed** by *VList + Fragment (layout output contract)*: `break-layout-pages/missed:0`
  Layout sets Fragment.anchorId on exactly one fragment per anchored block. Paginate slices without copying, so ids are emitted statelessly. Repeated header copies are RepeatCopy fragments with anchorId and spans cleared, and media-filtered collectors give one anchor per target.

  The lastAnchored parameter (typeset_html.cc:179, 612, 752) disappears. S0's checker asserts that each id is unique per output.
- **owned-by-other-theme** by *T7-render-runtime*: `break-layout-pages/missed:1`
  This is a serializer defect: two style attributes on snap-kerning code spans (typeset_html.cc:412-422). T7's single style writer fixes it.

  T6 provides the snap deltas as GridRow fragment data, plus a tsrc --snap golden in S0.
- **subsumed** by *BlockLayouter registry + layoutDetached + materializeLines*: `break-layout-pages/missed:4`
  Replaced blocks carry a uniform IntrinsicSize. With measure: 'host', the Replaced layouter files NEED_BOX{payload, widthSu} through T9's resource protocol, and the host answers only {h, baseline}.

  Widths never depend on answers, because intrinsic sizing uses the declared minW/maxW. One round therefore suffices, with a cap of 2 plus a placeholder diagnostic.

  Provisional layouts are not painted, and native mocks answer deterministically. This replaces the declared fixed height (emit.cc:697, layout.cc:106) that is clipped by .tsr-raw overflow:hidden (shell.mjs:38).
- **subsumed** by *Box tree: LayoutBlock + BlockTraits + SizeSpec, built by BoxTreeBuilder*: `emitter/measure-dependent-geometry-in-emit`
  Emit records intent: image scale, intrinsic dims and sidebarFrac become SizeSpec and IntrinsicSize. Layout resolves them against the real container: the measure, a track, a float box or an inline block.

  Relayout skips emit, which fixes staleness without the duplicate diagnostics of re-emitting. The image-src safety diagnostic moves to the ingest scan (scanImageReqs already checks safeImageSrc), coordinated with T2 and T9.
- **subsumed** by *BlockLayouter registry + layoutDetached + materializeLines*: `math/display-math-unit`
  A display formula is a Replaced block with painter MathRow and rows supplied by T8: one row today, many for aligned displays. Each row is a VList Box, and the penalties between rows are Normal, or Structural for an unbreakable formula.

  Each row's tag is a measured Line fragment on the row baseline, moved below the row on collision. A row wider than the measure gives 'display-overflow'. Vertical centring moves from the renderer (typeset_html.cc:226-249) into layout, and block images share the layouter (layout.cc:365-381).
- **subsumed** by *VList + Fragment (layout output contract)*: `render-runtime/marker-gutter`
  List enumerators and code line numbers become Line fragments at x = contentX - gutter, which may be negative. They carry a PaintClassId from T4's class channel (list-marker, line-number) instead of piggybacking on codeStyle (emit.cc:552).

  MarkerSpec.overflow (Hang, Push, OwnLine) handles labels wider than the gutter, such as '(viii)' or description terms. The gutter stays out of the code measure (verbatim-design §5), and PageSpec.gutterBleed covers sheets. T7 serializes an explicit left instead of right:100% (shell.mjs:30).

## User extension examples

### Theorem / callout environment: framed, numbered with a run-in label, kept together in print

**Today**

#!theorem(label: "thm-1") ... #theorem! becomes group{role:'theorem'} (executor.mjs:130).
- emit flattens it with no geometry (emit.cc:776-821), so typeset mode shows no frame (region/figure.layout.txt:8: the aside sits at left=0).
- Only built-in kinds get keep treatment in print (typeset_html.cc:681-705).
- The resolver counts and labels only 'figure' (resolve.cc:166).

**After**

Declare the role once:
  #{ $.role('theorem', {counter: 'theorem', lead: 'Theorem #n.', box: {padding: '0.5em 0.8em', border: {start: '2px'}}, keep: 'together', space: {before: '1em', after: '1em'}}) }

- Counter, label and the run-in lead come from T3's role registry. T3 merges the lead into the first paragraph's inline content, so layout sees one paragraph.
- box, keep and space are non-inherited traits in the trait table.
- The Stack layouter makes the theorem a framed flow root with a Frame fragment of class 'theorem' and Avoid1 penalties inside.
- A theorem taller than a page splits with 'keep-violated' instead of overflowing.
- Available after S12.

### Float a table (or a code listing) with its caption beside the text

**Today**

Only an image with side left/right floats (emit.cc:740-741).
- Every non-text unit clears an active float (doc.h:316-321).
- A float figure silently drops non-paragraph kids (emit.cc:802-803).
- A wide float neither narrows nor clears the text (doc.h:345).

**After**

  #!figure(place: {float: "right", width: "45%"}, label: "tbl-a")
  #table(...)
  Caption.
  #figure!

- layoutDetached lays out the whole container at 45% and registers an End exclusion.
- Following paragraphs get conservative-band ParShapes; a table below gets beside=Clear on its full box.
- If the remaining column is < minWrapWidth, the text clears.
- In print, the float never straddles a sheet (float extent).
- place: {float: 'top'} makes it a Movable box.
- Available after S13.

### Hanging-indent bibliography or notes entries

**Today**

There is no mechanism. real-world-report.md:51-62 lists 'hanging-indent unit' and piecewise widths as engine gaps. Notes get their hanging look only from list indentation.

**After**

  #{ $.set('bibentry', {par: {hang: '2em', hangAfter: 1}}) }
or a region arg:
  #!refs(par: {hang: "2em"})

par is an inherited trait, so it gives ParShape{lines: [{0, W}], rest: {2em, W-2em}}. The breaker and the cache are unchanged, and the same works next to a float because both producers write the same slots. Available after S12.

### Table with content-fitted columns, booktabs rules, a repeating header, spans and block cells

**Today**

#!table(cols: 3, align: "lcr") is the whole model:
- equal columns, a full grid of rules, inline-only cells (emit.cc:672), extra cells dropped (executor.mjs:88);
- converters drop specs, spans and captions (tex2tsm.mjs:190-198), and wiki2tsm deletes wikitables;
- in print, a table is one atom, clipped when it overflows a sheet.

**After**

  #!table(cols: [{width: "auto", align: "l"}, {width: "1fr"}, {width: "6em", align: "r"}], rules: "booktabs", header: 1, label: "tbl-x")
plus
  #cell(colspan: 2)[...]

- Auto tracks use each cell's min- and max-content; a too-wide table overflows with 'table-overflow' instead of collapsing.
- Cells are flow roots holding lists, code or math.
- In print, rows split across pages with the header re-emitted as RepeatCopy (no duplicate ids).
- Available after S12.

### A user block whose height depends on width (plot, embed) produced by a fence handler

**Today**

raw(html, {height}) advances by the declared height (emit.cc:697, layout.cc:106). It is clipped by .tsr-raw{overflow:hidden} (shell.mjs:38) and never re-measured on relayout. Built-ins get private pull channels (NEED_IMAGES, doc.h:40-48).

**After**

  #{ $.fence('plot', (a) => raw(svg, {measure: 'host', minWidth: '12em'})) }

- This gives a Replaced block with IntrinsicSize{source: Host}.
- Layout files NEED_BOX{payload, widthSu} through T9's resource protocol; the host answers {h, baseline}. There is one round, because widths never depend on answers.
- Native tests answer from a mock.
- Relayout re-requests only at changed widths.
- Available after S14.

### Poetry with hanging turnovers (not code)

**Today**

A #!verse region loses its line structure: tsrc --stage=tree shows one para whose lines are joined by " ". Only codeblock reaches the grid, which forces CLS_CODE and the mono font (emit.cc:551).

**After**

  #!verse(body: "lines", gap: "0", par: {hang: "2em"})
  The woods are lovely, dark and deep,
  But I have promises to keep,
  #verse!

- T1's 'lines' body mode makes each source line a paragraph, giving a Stack with gap 0.
- Turnover lines hang via par, laid out by the proportional Paragraph layouter.
- The Grid layouter stays reserved for verbatim content, with its mono metric contract.
- Line numbers would be a T3 counter in the marker slot, like list enumerators (open question).

### Side-by-side subfigures with their own captions

**Today**

A figure is one image plus a caption (emit.cc:776-821). There is no inline-level container, so the only workaround is a table.

**After**

  #!figure(label: "fig-ab")
  #subfigure(width: "48%", label: "fig-a")[#image("a.png") First.]
  #subfigure(width: "48%", label: "fig-b")[#image("b.png") Second.]
  Both panels.
  #figure!

- T2's level normalization makes each subfigure an InlineBlock object.
- The Paragraph layouter lays them out with layoutDetached before breaking, so they wrap like words when the measure is narrow.
- (a)/(b) numbering comes from T3.
- Available after S13.

### Print control on user content: keep a lemma with the next block, force a page break, change widows/orphans

**Today**

Keep-with-next exists only for TopBlock headings and block images (typeset_html.cc:702-705). Widows and orphans are the literal 2 for Text units only (typeset_html.cc:727-729). There is no page-break construct.

**After**

  #!lemma(keep: "with-next")
  #pagebreak()
  #{ $.set('doc', {widows: 3, orphans: 2}) }

Each becomes a tiered VList penalty that the PageBuilder honours uniformly for built-in and user blocks, relaxing in a declared order when a page cannot satisfy them. Available after S11/S12.

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  Arithmetic:
  - All geometry is integer su: slots, exclusions, tracks, VList.
  - The breaker reads only su values and i32 penalties, so cache hits cannot depend on raw px or on history.
  - Costs are doubles over su inputs with integer powers by multiplication, and ties use the total order (demerits, lines, later parent).
  - Deactivation is a deterministic pruning rule.
  - No unordered container is iterated in result-affecting code.
  - tsr_core builds with -ffp-contract=off, and prefix sums are i64 su.
  - Page building is lexicographic over integers; track distribution is floor-based with a fixed remainder rule.

  Validation:
  - A WASM (libc++) CI subset runs alongside the native (libstdc++) goldens.

  Golden churn is listed per migration step and was measured where possible. The S1 numbers come from the critic's port, which reproduces 125/128 of today's non-float Text units exactly; that port is checked in at S0 so reviewers can rerun it.
- **I2 Measurement-render robustness contract (design-decisions-v2 §7)**
  Line rendering:
  - Each Line/GridRow fragment is one absolutely positioned nowrap line.
  - Item widths and Disc branches are ceil+eps; container slots are floored.
  - Alignment offsets use measureLine's su natural, which is the over-estimate, so a centred or right-aligned line's right edge never passes its slot.
  - Justification realizes raw slack over raw stretch, so the rendered line fills exactly the slot.

  Overfull and overlap:
  - Overfull content is confined by TeX's rescue to one run per line, with spacing at the shrink limit, visible overflow and 'overfull-line'. It never collapses a paragraph or spreads unbounded negative spacing (break.cc:122, layout.cc:577-581). This is §7 rule 5's containment at line granularity.
  - Conservative bands make float overprint impossible (proof in ParShape semantics).

  Vertical and repair:
  - Oversized page atoms overflow visibly with 'page-overflow'.
  - No repair pass is added.
  - Baselines are published as y + ascent, where ascent includes the half-leading; document-model §8:227 is amended.
- **I3 Ops contract and OPS_VERSION discipline (document-model §4)**
  Steps that touch only layout, break and paginate never touch ops.def: S0-S11 and S13-S16.

  All new authoring keys land in S12 as one cross-kind trait group plus table, raw and pagebreak args:
  - They depend on T2's open typed attribute schema, which needs no bump per key.
  - If that schema has not landed, ONE batched bump reserves every key for S12-S15, with gen-ops-ts and a full fixture re-record per CLAUDE.md.

  Validation:
  - The OpReader stays the fuzz target.
  - Trait values are validated against T4 value domains at instantiation; an invalid value gives a diagnostic plus the default, never a crash.
  - Legacy spellings (figure side/float, table align 'lcr') are declared aliases, so existing .tsm keeps working.
- **I4 Execution declares, resolver decides (v2 §11.1)**
  Layout and paginate run strictly after resolve and never call scripts.
  - Traits and role defaults are declared data. Role defaults are global and independent of declaration order, applied after execution.
  - Host-measured boxes return geometry to layout only; scripts never see measured values.
  - Page-number references are fixed-width slots filled from PageResult.anchorPage after pagination: zero extra layout iterations, within v2 §11.1's 'no Typst fixpoint'.
- **I5 Dual-target rule (architecture §2.1)**
  boxtree/, break/, layout/ and paginate/ are engine core with no host calls.
  - Host-measured boxes and image intrinsics go through T9's single NEED_RESOURCES seam, with native mocks.
  - PageSpec is document data that host print options may override; it replaces the shell constants at shell.mjs:448-452.
  - tsrc gains --stage=blocktree|vlist|paged and --snap/--page-height, so every new product is native-testable.
- **I6 Emission-time style binding with DAG/schedule encoding (v2 §12)**
  Traits live in a TraitTable keyed by NodeId, never in Styling, so StyleId and the MetricStore key (measure.h:21) are unchanged and no extra measurement is triggered.
  - Inherited traits (align, hyphenate, breaker, par, gap, widows, orphans) are bound at instantiation from the EMIT site's ancestors. T4 maintains a trait frame parallel to the style stack under the same DAG/schedule encoding, so a stored value emitted twice gets per-site values, as §12 intends.
  - Non-inherited traits bind to the declaring node with the precedence kind default < role default < rule < explicit arg. They never fold onto descendants.
- **I7 Block-granular containment (v2 §11)**
  Each LayoutBlock is laid out independently, and a failure degrades only that block, with a diagnostic spanning it:
  - invalid TableSpec -> Stack fallback;
  - unresolved host box -> placeholder;
  - display overflow -> diagnostic;
  - overfull run -> that line only;
  - layouter/content mismatch -> nearest valid layouter.

  Flow roots (cells, detached boxes, framed stacks, the document) scope exclusions, so a float inside a cell or frame cannot leak out. Containers never cross a root child, so the per-pid swap unit stays atomic.
- **I8 Resumable pull loop; atomic per-paragraph swaps (architecture §2.4, v2 §9)**
  Relayout:
  - The box tree is width-independent, so setWidth invalidates only layout, paginate and paint. This makes 'relayout = re-break only' true without re-emitting.
  - The paged/screen choice is a layout-time media filter, so it never invalidates resolve.

  Pull loop:
  - Layout gains one resumable NEED state for host boxes. It is a single round by construction (widths never depend on answers), and provisional layouts are not painted.

  Swaps:
  - Fragments are frame-relative and each pid's fragments are contiguous, so per-paragraph frame grouping and the byte-identity patchIn relies on (editor-design §3) are unchanged.
- **I9 Performance: hot path parse/codegen/execute/emit; editor fast path**
  Emit gets cheaper: no width reads and no throwaway sub-flow FlowUnits. The box tree is cached across relayouts, and compile, execute and ingest are untouched.

  The content-hash break cache is kept (exact key, validated, T9 lifetime). Paragraphs move with their floats, so only paragraphs whose bands meet a changed exclusion re-break.

  Removing the ±5 window raises the DP's active set from 11 candidates to about one line's worth of breakpoints. S2 is therefore gated on tools/bench-edit.mjs and tools/corpus-run.mjs (pbr-zh, HoTT); if the gate fails, a bounded-active mode that evicts by the total order is added.

  Layout stays linear, with O(active exclusions) band queries. Paginate is skipped on screen, and relayout skips emit.

## Interfaces

- **T5-text-shaping** (consumes)
  Per paragraph-like node, an ItemList keyed by NodeId. T5 owns ItemKind and the item attributes; T6's BItem/BDisc is only the packed su projection that serves as the cache key.

  Contents:
  - Box, Glue, Kern, Penalty, Disc and InlineObject (with asc/desc);
  - an InlineBlock member whose w/asc/desc layout fills before breaking;
  - explicit glue stretch, shrink and order (shrink <= width);
  - Penalty tagged Normal/Forbidden/Forced, with values in thousandths;
  - Disc{pre, post, noBreak}, each ceil+eps;
  - paint-side rawPx, rawStretch and the SourceSpace/Synthetic attributes.

  Guarantees T5 gives:
  - kinsoku pairs carry a Forbidden penalty before any glue;
  - CJK glue exists only where a gap is realized (k*s_ref);
  - hyphenation and URL Discs are built over the flattened paragraph.

  Until S16, T6's adapter maps LinebreakBlock to TeX items (CJK char = Box, Penalty(Forbidden), Glue(0+sw), Penalty(pen)); one joint note fixes the mapping for punct-sp, boundary and math glue.
- **T5-text-shaping** (provides)
  T6 provides:
  - the semantics of measureLine(): discard, Disc branches, Forced breaks using LineEnds.last*;
  - the join rule, which scans the whole discarded run;
  - per line, the cost-side LineFit and the materialized PaintFit{ratio, order, overfull}.

  Each glue is realized as ratio x its raw stretch. T5 and T7 map the glue class to a paint channel (word-spacing, letter-spacing, margin). layout/ never inspects BF_* flags, script classes or codepoints after S16.
- **T4-style-settings** (consumes)
  One property registry, with no second settings cascade for block knobs. Each property has a name, type, value domain, an inherited flag and defaults by kind and role. For block traits it is resolved at instantiation into a TraitTable outside Styling/StyleId.

  Inherited properties:
  - align, hyphenate, breaker.*, par.*, gap, widows, orphans.

  Non-inherited properties:
  - space.before/after, box.*, keep, break, place, beside, media, marker.

  Precedence: kind default < role default < rule < explicit arg. Role defaults are global and order-independent.

  Document settings carry only page (PageSpec), layout.minWrapWidth and grid defaults.

  The default stylesheet must reproduce today exactly:
  - paraGap 1.2em as 1pg;
  - list gap 1/3 pg with integer division;
  - list 1.5em, quote 1.0em;
  - table pads 0.4em/0.3em;
  - heading and caption align;
  - breaker defaults: no tolerance, no emergency stretch.

  Also required:
  - PaintClassId from the class channel for frames, markers, tags and rules;
  - a non-metric run role (token 'comment') kept out of the measurement key.
- **T3-semantics** (consumes)
  T3 provides typed slot marks on the resolved tree; layout never reads role strings:
  - caption (for FitBody and caption-slot defaults);
  - lead/run-in, which T3 merges into the first paragraph's inline content;
  - insert flow per note body (flow id plus marker anchor), and whether paged output uses inserts. If it does, the end-of-document collector carries media=screen.
  - counters and labels for markers and subfigures;
  - a screen fallback for form:'page' references with the same slot width as the paged rendering.
- **T3-semantics** (provides)
  PageResult.anchorPage (label -> page number, paged path only) for page-number references, rendered into fixed-width slots that T3 requests and T5 sizes. There is no feedback into resolution.
- **T2-constructor-ir** (consumes)
  T2 should provide:
  - an open typed attribute schema with one cross-kind block-trait group (reserved names: box, space, gap, keep, break, place, beside, par, align, hyphenate, breaker, media), so traits travel without a bump per key;
  - table cols [{width, align}], rules, header, and tcell colspan/rowspan/align/valign;
  - raw measure/minWidth;
  - declared aliases (float/side -> place.float; table align string -> cols);
  - public ctors group, table, cell, raw, verbatim, subfigure and pagebreak;
  - level normalization (block content in cells and floats; block nodes in inline position become InlineBlock);
  - the sidecar marker split as a typed slot outside api/doc.h:88-150, keeping whole-body tokenization;
  - coordination on moving the image-src diagnostic to the ingest scan.
- **T1-surface-frontend** (consumes)
  Region body modes:
  - markup (today);
  - lines: each source line becomes a paragraph, or ends with a Forced break, for verse and addresses;
  - verbatim: raw lines, which reach the Grid layouter.

  Also a hardbreak syntax that lowers to a Forced penalty.
- **T7-render-runtime** (provides)
  T6 owns every position, including eqno/tag, marker x, display offsets, frame extents and pagination. T6 provides LayoutResult v2:
  - per-pid frames {pid, y, h, gapAfter};
  - frame-relative Fragments {FragKind, painter, x, y, w, h, ascent, PaintFit, join, anchorId (exactly one per anchored block), PaintClassId, flags Continuation/Overfull/RepeatCopy};
  - PageResult {pages: body slices with yShift, frame slices with open edges, insert slices, repeats, overflow}.

  T7's display list is a pure projection of these plus T5 runs. T7 owns HtmlWriter, class mapping and copy attributes; RepeatCopy fragments carry no id and no data-s.

  T7 must not read Config geometry, kinds or roles, and must not paginate. Until T7 consumes fragments natively, T6 ships an adapter onto today's branches that prints rules at y + h/2.
- **T8-math** (consumes)
  A display formula arrives as Replaced{painter MathRow, payload, rows: [{h, depth, baseline, tag ItemList?, breakAfter}]}: one row today, many for aligned environments. Inline formulas arrive as InlineObjects with break penalties from the T5/T8 class tables.

  T6 places rows with the center preset and display spacing. It places per-row tags with the collision rule, paginates between rows, reports 'display-overflow', and never breaks inside a row.
- **T9-host-protocol** (consumes)
  T9 should provide:
  - one resource protocol carrying IntrinsicSize for images, and NEED_BOX{payload, widthSu} -> {h, baseline} for host boxes;
  - a resumable layout-stage NEED state with native mocks;
  - a product graph where width invalidates box-tree-free stages (layout, paginate, paint), while 'paged' and PageSpec invalidate paginate and paint only;
  - break-cache lifetime and eviction as an engine product cache, under T6's key/value contract;
  - a settings JSON ABI carrying BreakParams and PageSpec, with host print options overriding document PageSpec.

  layout files needs through NeedSink and never blocks.

## Migration

### S0 Guard rails (no behaviour change)  → plan P0-01

Add test infrastructure:
- tsrc --snap and --page-height flags.
- New fixtures:
  - snap-kerning with a sidecar;
  - a left float inside a list;
  - a labelled display equation after a labelled heading on one 240px sheet;
  - a wrapped heading and caption (copy join);
  - a 3-line paragraph at a sheet boundary;
  - nested list/quote/code at 16px and 18px (vertical algebra);
  - a heading followed by a 200px figure on 240px sheets (keep fallback);
  - a float 100px above a sheet bottom;
  - two overlong URLs in one paragraph (rescue).
- A golden invariant checker: no element repeats an attribute, every internal href resolves, and each tsr- id occurs once per output.
- A WASM (libc++) CI subset.
- The Python break port used for the churn predictions, checked in (tools/kp-port.py).

**Golden impact:** New golden files only; all 48 existing fixtures stay byte-identical. The new fixtures record today's buggy output (duplicated style attribute, collapsed URL paragraph), so later steps show their fixes as diffs.

**OPS bump (as designed):** False

### S1 Breaker semantics bundle (land atomically; the BREAK_INF fix alone turns rescued headings into overfull lines)  → plan P0-12

Add break/items.h and an adapter:
- space -> Glue (SourceSpace unless BOUND/PUNCT_SP/INDENT);
- CJK char -> Box, Penalty(Forbidden), Glue(0+sw), Penalty(pen);
- hyphen -> Disc{pre=bw};
- BREAK_INF -> Forbidden;
- penalties become i32 thousandths.

Breaker changes:
- TeX discard;
- the paragraph end becomes a Forced break with last-line fil and last-line shrink;
- explicit classes with a 1e4 cap;
- the final-pass rescue plus an 'overfull-line' diagnostic;
- dp in a vector with the (demerits, lines, later parent) order;
- x^3 computed by multiplication.

Layout sets Overfull lines at the shrink limit.

**Golden impact:** Measured with the critic's port. It reproduces 125/128 non-float Text units exactly; the 3 misses are math units.
- Breakpoints change in 12 paragraphs across 11 fixtures: cite/basic, cite/unknown-diag, cjk/punct, cjk/softwrap, code/json-hl, doc/refs (2 paragraphs), doc/refs-diag, inline/emph, inline/quotes, splice/ascii-cut, style/kern-boundary. Their breaks, layout and html goldens are re-recorded.
- The printed cost changes in 44 of 116 units: breaks.txt of 25 fixtures.
- Neutral: all 17 cell streams (table cells, float captions, sidecar), and figure/pull-diag (thanks to the later-parent tie).
- i32 penalties and the cap change no breakpoint, and no golden needs the rescue.
- Not simulated, reviewed at landing: math/* (6), figure/float, figure/stack, and region/table-tiny's text unit.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/break-inf-float-vs-double`, `break-layout-pages/trailing-glue-in-break-cost`, `emitter/kp-counts-discardable-glue`, `break-layout-pages/overfull-collapses-paragraph`, `emitter/negative-wordspacing-overfull`, `break-layout-pages/missed:3`, `break-layout-pages/missed:5`

### S2 Knuth-Plass proper, validated cache, FP hygiene (perf-gated)  → plan P1-14

Search and cache:
- an active list with Overfull deactivation in every pass;
- no window, no ±1 pruning, no retry ladder;
- BreakParams with today's defaults;
- a 128-bit key over exactly the DP inputs, validated on a hit.

Numerics: -ffp-contract=off on tsr_core, and i64 su prefix sums.

Gate: tools/bench-edit.mjs and tools/corpus-run.mjs (pbr-zh, HoTT) must stay within the I9 budget; otherwise add a bounded-active mode that evicts by the total order.

**Golden impact:** Neutral. In the port, the exhaustive search and the deactivating search give the S1 breaks on all 116 units.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/kp-window-heuristics`, `break-layout-pages/break-cache-robustness`

### S3 Breaking moves into layout (behaviour-preserving; before S4 so the image formula never needs a third copy in api/)  → plan P1-15

A paragraph path in layout calls the breaker. The float tracker becomes an ExclusionMap in layout, which first reproduces today's prefix ParShape through one shared gapBefore().

Deletions:
- Doc::typeset's per-kind loop;
- the table colW and sidecar breaking;
- the five replay fields.

dumpBreaks reads the BreakResults that layout records. Fix figure-design.md:115-117 ('under-clears' -> 'over-clears').

**Golden impact:** Neutral: all stages byte-identical.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/api-hosts-layout-policy`, `api-measure-code/doc-typeset-hosts-layout-logic`, `break-layout-pages/float-tracker-replay`, `break-layout-pages/doc-drift`

### S4 Width-independent emit (SizeSpec / IntrinsicSize)  → plan P1-16

emit records SizeSpec and IntrinsicSize instead of resolved geometry: image scale and intrinsic dims, the placeholder's aspect, and the sidecar's Percent(sidebarFrac). Layout resolves them with the identical formulas and roundings (emit.cc:721-739, 591).

The image-src diagnostic moves to the ingest scan (with T2/T9). Doc::setWidth invalidates layout only.

**Golden impact:** layout and html are byte-identical. blocks.txt of the 5 image fixtures (figure/block, figure/float, figure/pull-diag, figure/stack, pages/paged-doc) is re-recorded, because the dump prints the spec.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/emit-reads-measure-stale-on-relayout`, `emitter/stale-emit-on-relayout`, `api-measure-code/relayout-stale-emit`, `emitter/measure-dependent-geometry-in-emit`

### S5 ParShape vector, side-tagged exclusions, conservative bands, defined Clear  → plan P3-08

Float geometry:
- slots come from flow-root exclusion rects queried over [yTop + i*minAdv, yTop + (i+1)*maxAdv);
- floats have zero advance and no gap after them;
- left and right floats coexist, and stacks may have any widths;
- below minWrapWidth the text clears;
- beside=Clear uses the full content box plus parent.gap clearance;
- slack comes from LineSlot.width everywhere;
- framed stacks and cells are flow roots.

**Golden impact:** figure/float and figure/stack churn (breaks, layout, html): the double paraGap goes away, and the narrowing follows real band extents. Every other fixture is neutral, because 19200su = suFloorPx(300) and no other fixture has floats.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/parshape-prefix-form`, `real-world-evidence/parshape-prefix`, `break-layout-pages/float-adds-paragraph-gap`, `break-layout-pages/float-indent-geometry`, `break-layout-pages/wide-float-overprints-text`, `break-layout-pages/measure-definition-split`

### S6 One materializeLines; sub-flows as flow roots  → plan P1-17

Cells, float captions and sidecar rows become ItemLists with anchors, and dumpBreaks covers every stream. One materializeLines replaces the four loops:
- join scans the discarded run;
- endsWithHyphen comes from the Disc;
- heights include vmet and math;
- cells use JoinPolicy::Never;
- the paint ratio comes from raw px.

Float captions keep left alignment (open question).

**Golden impact:** Wrapped centred caption lines go from join=last to space/none and gain data-join: layout and html of figure/block (2 lines), figure/pull-diag (2) and region/figure (1).

breaks.txt gains cell-stream records in region/table, region/table-tiny, figure/float, figure/stack and code/sidecar.

pages/paged-doc's caption is a single line, so it is neutral. Everything else is neutral.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/nested-stream-copies`, `emitter/missed:3`, `break-layout-pages/missed:2`

### S7 Box tree builder, layouter registry and fragments (structural)  → plan P1-18

Build the box tree:
- boxtree/ builds LayoutBlocks after resolve, and emit keeps only ItemLists;
- the layouter is chosen by content model; nodes are NodeId + Span; a TraitTable holds built-in defaults reproducing today (1pg root gap, 1/3 pg list gap with integer division, indents 1.5em/1.0em, markers);
- the Paragraph, Stack, Replaced, Grid and Table layouters are in place (rule at its top y; display math as one-row Replaced);
- Fragments are frame-relative with PaintClassId, and markers and tags are Line fragments.

Guard the boundary and keep today's output:
- a lint bans model.h from layout/, break/, paginate/ and render/typeset_html;
- a T7 adapter maps fragments onto today's branches.

Fix the layout.h:20 and emit.h:67 comments.

**Golden impact:** All existing goldens are byte-identical:
- the legacy dumps print rules at y + h/2;
- S0's nested fixture proves the vertical algebra.

New blocktree and vlist goldens are added.

HTML changes only when T7 consumes fragments natively:
- the explicit tag position in math/eqref, the only tsr-eqno fixture;
- the explicit marker x in the 9 .tsr-marker fixtures (code/runs, code/sidecar, code/wrap, doc/refs, doc/structure, notes/basic, notes/cjk-glue, notes/explicit, region/figure).

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/unit-kind-switch`, `emitter/flowunit-kind-switch`, `math/display-math-unit`, `break-layout-pages/vertical-spacing-constants`, `break-layout-pages/group-role-dispatch`, `render-runtime/marker-gutter`, `break-layout-pages/baseline-not-communicated`, `emitter/figure-role-string-dispatch`

### S8 LineEnds replace the alignment flags  → plan P3-09

Remove ragged, centered, isLast, noGlue and tAligns. The align presets get interior and last pairs, with finite 2em end stretch and rigid interior glue for left/center/right. The breaker optimizes with the same end glue, and alignment offsets use the su natural width.

**Golden impact:** Breaks are neutral. The 6 centred caption units were measured with the finite-end model at 1-3em total end stretch and the later-parent tie, and no heading wraps at 300px.

Centred and right-aligned lines shift by a few su (layout + html):
- the caption lines of figure/block, figure/pull-diag, region/figure and pages/paged-doc;
- the 'c'/'r' cells of region/table.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/alignment-flags`

### S9 Table layouter with the existing args  → plan P3-10

Tables become a TableSpec: n x {Fr(1), align} tracks with floor rounding, the Grid rule preset, and padding from settings. Cells are flow roots, so block content the executor already appends to cells is laid out instead of flattened. Rows are VList Boxes.

**Golden impact:** region/table and region/table-tiny stay byte-identical (same colW floor, padX inside the cell, 64su floor, rules).

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/table-closed`

### S10 Grid layouter; codeblock+sidecar lowered to a two-track table  → plan P3-11

The grid becomes wrapGridLine in layout/grid.cc, with GridParams as data:
- the char-class table, defaulting to isBreakable;
- minCols = 8;
- continuation policy;
- CommentAware via T4's run role.

A codeblock with a sidecar lowers to [{Fr(1), min 64su}, {Percent(f)}] with gap = 1 code em, and the snap branch takes its column budget from the code track.

Fix the drift in grid.h:2 / verbatim-design.md:25-26 ('FIRST convergent') and in code-design.md §4 (break classes).

**Golden impact:** All 6 code fixtures are byte-identical; code/sidecar reproduces 7680/10650/x=11520su. The S0 snap+sidecar fixture changes, as intended.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/code-wrap-in-layout`, `api-measure-code/grid-is-codeblock-only`, `break-layout-pages/code-sidecar-three-box`, `break-layout-pages/snap-kerning-ignores-sidecar`, `api-measure-code/snap-ignores-sidecar-partition`

### S11 VList and the paginate stage  → plan P3-12

Layouters emit tiered penalties:
- paragraph orphans/widows;
- keep-with-next (headings at any depth, image before caption);
- Grid Structural/Avoid2;
- table rows.

paginate() then provides:
- ordered relaxation;
- float extents and Movable boxes;
- Inserts, once T3 marks note bodies;
- RepeatCopy headers;
- visible overflow.

renderPages becomes T7's serializer of PageResult, and the shell takes PageSpec instead of A4 literals.

Fix pages-design.md:45, 49, 52 and 123: 3-line atomicity, 'table rows', the KP hard-cut wording.

**Golden impact:** pages/paged-doc.paged.txt is re-recorded: sheet 3 no longer opens with a lone '}', because Avoid2 now applies over grid logical lines (a deliberate change).

The S0 paged fixtures change where they recorded clipped oversize atoms; the keep-fallback fixture is unchanged, since relaxation reproduces typeset_html.cc:736-737.

Screen goldens are untouched.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/paginator-in-serializer`, `render-runtime/paged-keep-rules-by-kind`, `break-layout-pages/paged-atoms-clipped`, `break-layout-pages/missed:0`, `break-layout-pages/doc-drift`

### S12 Authoring surface (depends on T2's open attribute schema and T4's registry)  → plan P3-14

Register the cross-kind trait group with value domains and inherited flags: box, space, gap, keep, break, place, beside, par, align, hyphenate, breaker, media.

Also land:
- $.role/$.set as global, order-independent role defaults;
- table cols [{width, align}], rules, header, and cell colspan/rowspan/align/valign, with the Auto track resolution, spans, rule presets and header repetition;
- raw measure/minWidth;
- the #pagebreak() ctor;
- declared aliases;
- validation diagnostics;
- executor tableBuild stops truncating cells and reports extra cells.

**Golden impact:** No textual golden changes, because the defaults equal today.

If T2's open schema has not landed, this is ONE batched OPS_VERSION bump that reserves every key for S12-S15, with gen-ops-ts and a full .ops re-record.

New fixtures: theorem, refs hang, lemma keep, pagebreak, a booktabs/spans/auto table, and a wide table overflow.

**OPS bump (as designed):** True

**Fixes:** `real-world-evidence/table-model-v1`, `break-layout-pages/table-closed`, `break-layout-pages/group-role-dispatch`

### S13 Generalized placement and detached layout  → plan P3-15

layoutDetached is shared by floats, cells and InlineBlocks:
- place applies to any block or container, with FitBody as the default width;
- page floats become Movable boxes;
- InlineBlock enables subfigures, with T5's InlineObject member and T2's level normalization;
- beside=Shrink is opt-in.

**Golden impact:** figure/float and figure/stack are neutral relative to S5, because FitBody equals the image width. New fixtures cover floated tables, opposite-side pairs, page floats and subfigures.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/float-model-closed`, `real-world-evidence/figure-model-single-image`

### S14 Host-measured replaced boxes  → plan P3-28

NEED_BOX{payload, widthSu} -> {h, baseline} goes through T9's resource protocol and its layout-stage NEED state. There is one round, capped at 2 with a placeholder diagnostic, and provisional layouts are not painted. Images move onto the same IntrinsicSize path, and the native mock answers deterministically.

**Golden impact:** Existing fixtures are neutral (no host boxes); one new fixture is added.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/missed:4`

### S15 Display rows (with T8)  → plan P3-29

ReplacedData.rows comes from T8 for aligned displays. Each row is a VList Box with its own tag and collision rule, and the penalties between rows are Normal, or Structural for unbreakable formulas.

**Golden impact:** Neutral, since current displays are single-row. T8 adds the new fixtures.

**OPS bump (as designed):** False

**Fixes:** `math/display-math-unit`

### S16 Breaker input switches to T5's ItemList (with T5)  → plan P4-08

Delete the LinebreakBlock adapter. The breaker reads T5's items, with uniform s_ref and k*s_ref glue only where a gap is realized, and paint realizes glue from PaintFit.ratio x raw stretch. layout/ and render lose every BF_* test.

**Golden impact:** Substantial, and shared with T5's own churn. On mixed CJK/Latin lines the breaker's stretch accounting moves to T5's k-rule and the last CJK char's attached stretch disappears, so cjk/* goldens, mixed-script breaks and html word/letter-spacing change. It must be reviewed with the CJK owner.

**OPS bump (as designed):** False

**Fixes:** `emitter/kp-ignores-stretch-weight`, `break-layout-pages/glue-semantics-split`, `break-layout-pages/hyphen-url-not-discretionary`

## Not generalized (kept special)

- **Grid (verbatim) wrapping stays a greedy column algorithm, separate from Knuth-Plass, reachable only from verbatim content** — code-design §4 says 'no Knuth, by decision', and verbatim-design §6 says 'two code paths stay'. The grid is a monospace metric contract (code-design.md:76), so proportional verse belongs to the Paragraph layouter. The continuation indent is clamped against the live column count (layout.cc:216-217). What is unified is the grid's location, its policy as data, its fragments and its use as a table track.
- **No user-written layout algorithms (JS or WASM plugins)** — I4: scripts never see measured values, and a JS layouter would need a synchronous call from the engine into the worker. Equal footing comes from the shared content model and traits, plus host-measured Replaced boxes for geometry the engine cannot compute.
- **Paragraph shapes stay line-indexed (v2 §10), with conservative bands rather than exact per-line positions** — Exact positions need a fixpoint between breaking and layout. Bands give a proven no-overlap guarantee in one pass. Their cost is extra narrowing for paragraphs that contain tall inline items and sit beside a float.
- **Exclusions are side-tagged rectangles giving one interval per line, scoped to flow roots** — This covers left and right floats and stacks. shape-outside, text on both sides of a centred float, and floats escaping a cell or frame would need multi-interval lines and cross-root invalidation for no current use case.
- **Margin placement (margin figures, sidenotes)** — Neither the screen measure nor PageSpec has a margin column today. The extension path is a Place mode Margin{Start, End} placed at the anchor line's y, with its own ExclusionMap per margin column for stacking, and an inline fallback when the column width is 0. It is deferred until a container declares such a column.
- **The screen is one infinite page** — v2 §10 and progressive per-paragraph swap. Page floats render in place on screen, footnotes stay T3's collector (media=screen), and Inserts and Movable lifting take effect only on the paged path.
- **No resolve<->paginate fixed point; page references fill fixed-width slots** — v2 §11.1 ('no Typst fixpoint'; the resolver is a single pass). Slots give exact page numbers with zero extra layout iterations.
- **Vertical glue is fixed and the page builder is lexicographic (no underfull badness, no vertical stretch)** — This reproduces today's greedy last fit with documented keep relaxation. A badness-weighted or Plass-style global page DP stays an open question until a feature needs it.
- **Spacing collapses only between siblings, passing through unframed containers; no collapse through empty blocks and no negative spacing** — This reproduces today's single-gap model, lets a heading's before survive an unframed region boundary, and avoids CSS's parent/child corner cases with frames and pagination.
- **The gutter (line numbers, list markers) stays outside the measure, as Line fragments with negative x plus PageSpec.gutterBleed** — verbatim-design §5 makes the gutter the 'degenerate column' that does not shrink the code measure. The bug was CSS placement and clipping, not the concept. MarkerSpec.overflow covers labels wider than the gutter.
- **A table row never splits internally; rowspan groups are atomic except as the last relaxation** — Splitting inside a row needs per-cell continuation state. Splitting between rows, with repeated headers, removes the real clipping problem.
- **T6 never line-breaks inside a display row** — Rows come from T8. Math line-breaking within a row is T8's scope, and T6 reports 'display-overflow'.
- **Cut until a fixture needs them: fitness classes, looseness, bounded-active mode, flagged-Disc demerits, drop caps, running-head marks, multiple insert classes** — Each has no current consumer or setting. The bounded-active mode returns only if the S2 perf gate fails. Drop caps need a measured glyph from T5 plus a ParShapeSpec field. Marks need a page-head area in PageSpec. The insert flow id is kept because T3's flows are generic, but only footnotes are registered.
- **Multi-column and n x m flows** — Recorded non-goal (v2 §10).

## Risks

- Golden churn concentrates in:
  - S1: breakpoints in 12 paragraphs across 11 fixtures, cost values in 25 fixtures' breaks.txt, 9 fixtures not simulated;
  - S5: 2 float fixtures;
  - S6: 3 caption fixtures (join) plus cell-stream breaks records;
  - S8: su shifts in 5 fixtures;
  - S11: the paged golden;
  - S16: CJK and mixed script, shared with T5.
  Each step must land as its own commit with a scripted golden-diff review, using the checked-in port.
- Discard-at-break exposes line-end punctuation compression to the optimizer, so CJK line choices change in cjk/punct and cjk/softwrap. A CJK owner must sign off.
- Removing the ±5 window raises the DP's active set to about one line of breakpoints, which can be 45 or more for CJK at 700px. S2 is gated on bench-edit and corpus-run, with a bounded-active fallback.
- Deactivation is exact only while line width grows with the break position. A Disc whose pre is wider than the following content can make the pruning miss the optimum, as in TeX. The case is rare and deterministic, but it must be documented.
- Conservative bands over-narrow lines in paragraphs that contain tall inline items and sit beside a float: safe, but visibly looser.
- S7 splits emit into ItemList production and a box-tree builder. It is the largest refactor and touches every block kind. The lint and the byte-identical legacy dumps are its safety net.
- The trait surface depends on T2's open schema and T4's registry with inherited flags. If either slips, S12 needs one batched OPS bump, and the user examples wait.
- Ordered keep relaxation differs from today only in fallback cases (an earlier cut that satisfies widows/orphans may now win over the greedy cut). It is diagnosed with 'keep-violated'.
- Host-measured boxes add a layout-time NEED round, and first paint waits for answers in embed-heavy documents. Rounds are bounded at 1 by construction (2 defensive).
- Residual floating-point differences between libstdc++ (goldens) and libc++ (production) remain possible in the DP's double arithmetic. Integer powers, total-order ties and -ffp-contract=off reduce them, but only the WASM CI subset proves it.
- Until T7 consumes fragments, the adapter keeps the renderer's cfg geometry reads (typeset_html.cc:226-249) and the CSS-centred eqno.

## Open questions (decided in PLAN.md §3)

- Ragged end stretch: 2em for left/right and 1em per side for center are golden-neutral at 1-3em. Should these be the defaults, or should owners tune them per role (heading vs caption)?
- Default for non-paragraph blocks beside a float: keep beside=Clear (figure-design §4), or Shrink into available(y, activeBottom)?
- Page builder: keep it lexicographic and greedy (proposed, reproduces today), or adopt a Plass-style global DP over page breaks with underfull badness?
- Keep relaxation order: Avoid1 (keep-with-next/together) before Avoid2 (widows/orphans), as proposed, or the reverse?
- Float captions are left-aligned today (layout.cc:63) while block captions are centred. Should one caption-slot default unify them (churns figure/float and figure/stack)?
- Page-reference slot width: a fixed digit count from settings, or derived from an upper bound on page count?
- List gap: keep 1/3 pg with integer division (409su at 16px, neutral), or move to round(0.4em) (410su, 1su churn in list fixtures)?
- Should T7 emit inter-paragraph margins from su (removing the fr.y vs DOM drift) at the cost of one changed value in every multi-paragraph html golden?
- Line numbers for paragraph-layouter content (verse, legal text): a T3 counter in a per-child marker slot (proposed), or a per-line marker on Forced segments?
- Wide tables on screen: should T7 render the reported overflow as a horizontal scroll container or as visible overflow?
- Where should the sidecar copy contract (verbatim-design §5, OPEN) land now that sidecars are table cells: '/// ' plus content text, or plain cell text with JoinPolicy::Never?

## Changelog (critique responses)

- A1 (soundness, major) Keeps as hard Forbidden penalties lose today's fallback. ACCEPTED. Vertical penalties are now tiered (Normal, Avoid1, Avoid2, Structural, Forced) and relaxed in order before the greedy cut. This reproduces typeset_html.cc:736-737 and docs/pages-design.md:51-52, adds a 'keep-violated' diagnostic, and an S0 fixture covers heading + 200px figure on 240px sheets.
- A2 (soundness, major) Zero-height wrap floats let a float straddle a sheet. ACCEPTED. VItem gains Float{extent}, and a break is taken only when every float begun on the page ends within the goal; otherwise the cut moves before the float. This is today's atomic band (typeset_html.cc:682, 734). Narrowed lines past a cut stay narrowed, as pages-design.md:124-126 accepts. Covered by an S0 fixture.
- A3 (soundness, major) The 2-pass refinement can overprint stacked floats. ACCEPTED and restructured. The refinement is deleted and replaced by conservative bands [yTop + i*minAdv, yTop + (i+1)*maxAdv) with a no-overlap proof, in a single pass that is exact for ordinary paragraphs. not_generalized now records the over-narrowing cost.
- A4 (soundness, major) The cache key excludes rawPx, yet LineFit depended on it. ACCEPTED. The breaker is su-only (ItemView has no raw px; RawView is paint-side). BreakResult holds a cost-side LineFit, and materializeLines recomputes the paint ratio from raw px exactly as layout.cc:570-584 does, so html word-spacing stays neutral and cache hits are history-independent.
- A5 (soundness, major) The cap+overfull final pass merges overflowing tokens. ACCEPTED. It is replaced by TeX's rescue: an artificial break at the first overflow, when every active node is overfull, with no added demerits. The port gives [1,3,5] on the two-URL case, i.e. one run per line, and the case is an S0 fixture.
- A6 (soundness, major) The S1 churn list was wrong and the paragraph-end semantics were imprecise. ACCEPTED. The end is now a Forced break independent of the last item's penalty, with last-line fil and shrink (TeX). Re-measured with the critic's port, including the cell streams:
  - breakpoints change in 12 paragraphs across 11 fixtures (doc/refs-diag and splice/ascii-cut added; notes/cjk-glue and cjk/indent removed);
  - figure/pull-diag is neutral thanks to the later-parent tie;
  - all 17 cell streams are neutral;
  - the port is checked in at S0.
- A7 (soundness, major) Fil centring plus an earlier-break tie gives degenerate captions. ACCEPTED and restructured. LineEnds now has interior and last pairs: ragged and centred presets use finite end stretch with rigid interior glue, and only the last pair uses fil. The tie rule is now 'later parent'. Measured: all 6 centred caption units keep today's breaks at end stretch 1-3em. The S8 claim is corrected to alignment shifts only.
- A8 (soundness, major) Join from the break item drops copy spaces in mixed CJK/Latin text. ACCEPTED. join = space iff a SourceSpace glue lies in the discarded run (layout.cc:545-550 semantics), and the later-parent tie keeps break indices at the glue (figure/pull-diag [19,27]).
- A9 (soundness, major) avoid=Clear by minContent never clears thin blocks. ACCEPTED. Clear now requires the full content box to be free, plus parent.gap clearance, reproducing doc.h:310, 316-321 and the pid6 rule in figure/float. The trait was also renamed 'beside', because it belongs to the flowing block, not to the float.
- A10 (soundness, major) rowSync fraction tracks cannot reproduce the sidecar widths. ACCEPTED. The lowering is [{Fr(1), min 64su}, {Percent(f) of the container box}] with a 1 code em gap, which reproduces 7680/10650/x=11520su (verified against emit.cc:591, layout.cc:125-127, code/sidecar.layout.txt). rowSync is deleted, since every table row is already equal-height.
- A11 (soundness, major) No step delivers the user trait surface or covers its I3 cost. ACCEPTED. S12 lands the whole surface (traits, table args, raw measure, pagebreak, aliases) with a hard dependency on T2's open schema; failing that, ONE batched bump reserves every key for S12-S15. Each example now names its step.
- A12 (soundness, major) NEED_BOX has no termination argument. ACCEPTED. The host answers only {h, baseline}. Intrinsic sizing uses the declared minW/maxW, and Shrink uses activeBottom(y), independent of the block's own height. Requested widths therefore never depend on answers: one round, a defensive cap of 2, provisional layouts not painted, answers quantized ceil+eps.
- A13 (soundness, minor) The static_assert cannot compile with a float field. ACCEPTED. Penalties are i32 thousandths and BItem is all-integral with an explicit reserved byte, so the assert holds (24 bytes). The port shows exact thousandths change no golden breakpoint.
- A14 (soundness, minor) BreakParams surface defaults contradicted 'defaults = today'. ACCEPTED. The defaults are no tolerance and emergencyStretch 0; the TeX-like values appear only as an opt-in example. The port measured 4 extra changes under the old surface defaults.
- A15 (soundness, minor) The rule moved to top y but S7 claimed neutrality. ACCEPTED. Fragments store the top y, and the legacy dumps and the T7 adapter print y + h/2 until T7 lands, so the 7 rule fixtures stay byte-identical in S7.
- A16 (soundness, minor) Golden fixture lists were wrong. ACCEPTED:
  - the only eqno fixture is math/eqref;
  - paged-doc's caption is one line;
  - S6 now lists the join=last -> space/none layout changes and the breaks.txt cell-stream records;
  - S12 keeps the '}' claim, because Grid now emits Avoid2 widows/orphans over logical lines.
- A17 (soundness, minor) FloatRef on screen and repeated header ids. ACCEPTED. Page floats are Box{Movable}, kept in place on screen and lifted with a yShift on pages. Repeated headers are RepeatCopy fragments with anchorId and spans cleared, checked by S0's id-uniqueness checker.
- A18 (soundness, minor) Fragment coordinates and baseline definition. ACCEPTED. Fragments are frame-relative (patchIn byte identity), and exclusions are flow-root coordinates converted at the boundary. One baseline definition remains: y + ascent, where ascent includes the half-leading. document-model §8:227 is amended.
- A19 (soundness, minor) FMA contraction. ACCEPTED. -ffp-contract=off is added to tsr_core (engine/CMakeLists.txt:27 has none today), and prefix sums are i64 su.
- A20 (soundness, minor) No deactivation in pass 3, unordered maxActive, looseness state. ACCEPTED. Deactivation now applies in every pass and the rescue keeps one node. maxActive, looseness and fitness are cut (B20); a bounded mode returns only if the S2 perf gate fails, and then evicts by the total order.
- A21 (soundness, minor) Page-break legality between lines, and unrealizable vertical stretch. ACCEPTED. Explicit Normal/Avoid2 penalties sit between lines, vertical glue is fixed, and the stretch/shrink fields are removed.
- A22 (soundness, minor) S3 before S4 forced a third copy of the image formula. ACCEPTED. The steps are reordered: breaking moves into layout (S3) before width-independent emit (S4).
- A23 (soundness, minor) Traits could enter Styling/StyleId, and role-default binding was unstated. ACCEPTED, together with B1: a TraitTable outside Styling, global and order-independent role defaults, and the stated precedence.
- A24 (soundness, minor) Centring precision, single-interval choice, frame scoping. ACCEPTED. Offsets use the su natural width. Exclusions are side-tagged, giving one interval per line by construction. Framed Stacks are flow roots that contain their floats.
- A-overlaps ACCEPTED. T6 owns paginate and PageResult, and T7 serializes it (RepeatCopy without id or data-s; the adapter prints rules at y + h/2). T5 owns the item kinds and kinsoku-before-glue, with a joint S1 adapter note. T4 owns the breaker defaults (equal to today) and the trait table outside StyleId. T3 owns the page-ref screen fallback and the media decision for collectors. T9 owns the layout-stage NEED state. T2 coordinates the diagnostic move.
- B1 (generality, blocker) Trait binding through the style fold, and two channels for align/breaker. ACCEPTED. There is one T4 registry with an inherited flag per property:
  - inherited properties (align, hyphenate, breaker, par, gap, widows, orphans) bind from the EMIT site's ancestors;
  - non-inherited properties bind to the declaring node: kind default < role default < rule < explicit arg;
  - values live in a TraitTable, never in Styling;
  - document settings keep only PageSpec, minWrapWidth and grid defaults;
  - widows/orphans live in one place.
- B2 (generality, major) A free 'layout' trait is meaningless, and the verse example cannot be expressed. ACCEPTED. The layouter is chosen by content model with mismatch diagnostics, and 'layout' is removed. I verified the tree with tsrc: the verse lines are joined into one para. The verse example is rewritten on T1's 'lines' body mode, proportional Paragraph and par.hang, and Grid is reachable only from verbatim content with a mono check.
- B3 (generality, major) Dimensionless demerits, pass-3 cost, S2 performance, integer exponent. ACCEPTED. There is TeX rescue, no length added to demerits, deactivation in every pass, the exponent domain is integers 1..4, and S2 is gated on bench-edit and corpus-run.
- B4 (generality, major) Zero-height float. ACCEPTED (same fix as A2).
- B5 (generality, major) Refinement trades safety for overprint. ACCEPTED (same fix as A3: conservative bands).
- B6 (generality, major) The vertical algebra was unspecified and tightAbove is inherited. ACCEPTED. The gap is container-owned and inherited (list default 1/3 pg, integer), with before/after collapsing between siblings and passing through unframed containers. Verified with tsrc on a nested list at 18px (+460su = 1382/3) and added as an S0 fixture before neutrality is claimed.
- B7 (generality, major) Trait keys collide with per-kind args. ACCEPTED. There is one cross-kind group with reserved names. Table column alignment moves into cols[{width, align}] with 'lcr' as a declared alias. float and side alias to place.float (executor.mjs:216). raw keeps h, and codeblock keeps its Bool wrap (the verse example no longer uses wrap).
- B8 (generality, major) emit built the L6 schema, and the ContentNode pointer leaks into layout and paint. ACCEPTED. boxtree/ is a post-resolve builder (L6 entry) and emit produces only ItemLists. Blocks carry NodeId + Span, and a lint bans model.h from layout/, break/, paginate/ and render/typeset_html.
- B9 (generality, major) Display math as one atomic box. ACCEPTED. ReplacedData has rows {h, depth, baseline, tag, breakAfter} supplied by T8. Each row is a VList Box with a tag and collision rule, and pagination happens between rows (S15).
- B10 (generality, major) No inline-level container. ACCEPTED. An InlineBlock InlineObject is laid out through the same layoutDetached that floats, cells and inserts use, before breaking and at a SizeSpec against ParShape.rest. The subfigure example and step S13 are added.
- B11 (generality, major) Run-in lead and wide markers. PARTIALLY ACCEPTED. A layout-level merge of item lists would make L6 reach into L5, so the lead slot is T3's model-level merge into the first paragraph's inline content, and T6 sees an ordinary paragraph. T6 adds MarkerSpec.overflow {Hang, Push, OwnLine}, with Push realized through ParShapeSpec.firstLineLeft.
- B12 (generality, major) Inserts vs the end-of-document collector. ACCEPTED. A media trait (screen | paged | all) is filtered at layout time (flowScreen and paginate), so anchors occur once per target, resolve is never re-run, and 'paged' invalidates only paginate and paint (T9). T3 decides per flow whether paged output uses inserts.
- B13 (generality, major) No trait-surface step, and three separate bumps. ACCEPTED (same as A11): one S12 surface step behind T2's open schema, or one batched bump; S13-S15 need no bump.
- B14 (generality, minor) Hardbreak as parfill glue mis-centres lines. ACCEPTED. A hardbreak is a Forced penalty, and any Forced break applies LineEnds.lastStart/lastEnd, so no glue item is inserted.
- B15 (generality, minor) Repeated header rows duplicate ids. ACCEPTED (same fix as A17).
- B16 (generality, minor) The page cost model ignores underfill, and keep-together overflows. ACCEPTED. Vertical penalties are tiers rather than weights, vertical glue is fixed, and keep-together is Avoid1 with the ordered split fallback. A badness-based page DP is listed as an open question.
- B17 (generality, minor) Ad-hoc residue. MOSTLY ACCEPTED:
  - FromReplaced becomes FitBody over non-caption slots;
  - rowSync and Sizer are deleted (v1 = n x Fr(1), floor-per-track);
  - one SizeSpec serves tracks, block widths and float/inline-block widths;
  - EqTag and Marker become Line fragments with PaintClassId.
  Not accepted: merging the Replaced painters. They stay distinct painter ids because their payloads differ (raw HTML, image src, MathBox, rule); dispatch is on payload type, not semantics.
- B18 (generality, minor) S10 fraction base unstated. ACCEPTED: Percent of the container box plus an Fr(1) remainder, measured byte-identical.
- B19 (generality, minor) Grid break classes from token roles contradict verbatim-design §6. ACCEPTED. The char-class table stays as GridParams data (default = isBreakable), and the code-design.md §4 wording is fixed instead. The run role is still used for CommentAware continuation, which is today's behaviour (emit.cc:618).
- B20 (generality, minor) Speculative fields. MOSTLY ACCEPTED. Cut: Marks, BItem.flagged, looseness, fitness, maxActive, dropLines/dropWidth, Spacing stretch/shrink. Kept: the u8 insert flow id (T3 flows are generic, the cost is zero, only footnotes are registered) and ParShapeSpec.hang (the bibliography example and S12 fixture).
- B21 (generality, minor) Host boxes and intrinsic(). ACCEPTED (same fix as A12): declared minW/maxW, no painting of provisional layouts, one round.
- B22 (generality, minor) roleTag reaches paint. ACCEPTED. It is replaced by PaintClassId from T4's class channel, so T7 maps classes without interpreting roles.
- B23 (generality, minor) Margin placement and wide tables. PARTIALLY ACCEPTED. Wide tables get a defined overflow: min-content tracks, 'table-overflow' diagnostic, and the overflow extent on the Frame fragment. Margin placement goes to not_generalized with a concrete extension path, because neither the screen measure nor PageSpec has a margin column to place into.
- B24 (generality, minor) Evidence accuracy. ACCEPTED: the eqno fixture is math/eqref, and the aside citation is region/figure.layout.txt:8 (verified).
- B-missing ACCEPTED. Margin figures get a disposition (not_generalized). grid-is-codeblock-only now names T1's verbatim body mode and T2's verbatim ctor. headingSizeMul goes to T4's default stylesheet. User role defaults are delivered in S12.
- B-overlaps ACCEPTED:
  - T7: T6 owns every position and pagination, and the display list is a pure projection.
  - T4: one registry; widows/orphans only as inherited traits.
  - T2: a cross-kind trait group and a typed sidecar slot.
  - T3: slot semantics vs T4 presentation defaults.
  - T5: ItemKind is T5's; BItem is only the packed su projection used as the cache key.
  - T8: rows.
  - T9: NEED_BOX semantics, cache lifetime and eviction, the paged invalidation bit, and host print options over document PageSpec.
  - T1: body modes.
