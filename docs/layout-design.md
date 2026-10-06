# Layout: the box tree, layouters and fragments

Status: as built in plan P1-18 (design T6 S7, `docs/remediation/design/T6-layout-pagination.md`).
Later steps extend it: P3-01 compiles the TraitTable from NodeProps, P3-08
replaces the prefix ParShape with bands, P3-09 replaces the alignment flags
with LineEnds, P3-10 grows the Table layouter, P3-12 moves pagination onto
fragments with page specs.

## 1. The pipeline around layout

```
resolve → BoxTree (boxtree/build.cc) → Emit (the leaves' content) → Measure
        → Layout (layout/layout.cc: layouters → fragments) → Paginate (layout/paginate.cc)
        → Paint (paint/) → the typeset / paged writer (render/typeset_html.cc)
```

`BoxTree` is a stage of its own (`engine/src/api/stages.def`, rerun Once):
it reads only the resolved tree's structure and a few settings rows that
declare it in `affects` (`list.indent`, `quote.indent`, `par.indent`,
`code.scale`, `doc.baseSize`), so token and image answers — which re-run
Emit — never rebuild it.

## 2. The box tree (`engine/src/boxtree/block.h`)

One `TopTree` per top-level block (the editing loop's swap unit, `pid`):
`LayoutBlock`s in pre-order, `blocks[0]` the top's root stack, a block's
subtree `[i, end)`. A block holds

- `layouter` — chosen by **content model**, never by a trait or a role:
  inline content → `Paragraph`; verbatim → `Grid` (a code block, its sidecar
  rows a second track); table rows → `Table`; an image, raw markup, a rule or
  a display formula → `Replaced` with a `painter` (`Rule`, `Image`, `Raw`,
  `MathRow`); block children → `Stack`;
- `traits` — a row of the TraitTable (`build.cc`, handwritten until P3-01):
  the gap between a stack's children in paragraph gaps (`num/den`, integer
  division; `0/0` inherits), the line alignment (justify, ragged, centred),
  hyphenation, keep-with-next;
- `x` (the start edge: its ancestors' indents) and `pad` (what a list or a
  quote adds for its children);
- `anchor` (its label), `marker` / `markerStyle` (a list item's marker, on
  its first leaf when that is a paragraph, heading or code block);
- `floatSide` (an image with a side, F2);
- a leaf's `unit`: its index in the top's emitted units.

**Roles are data.** A group's box trait comes from its element class (a
`figure`-class group: its paragraphs are captions and an image with a side
floats with its caption rows; plan P2-05); a code block's sidecar track is
its margin slot, `group{slot: margin}` (plan P2-13). No layer below
compares a role or kind spelling (lint `role-string-compare` covers
boxtree/, layout/, paint/, render/).

**Anchors** (finding `emitter/anchor-opt-in-per-kind`): an anchored block's
label rides the first leaf of its subtree whatever its kind (a code block, a
rule and raw markup included; a marker-only leaf carries none) as the leaf's
`carry`; when a deeper block's own label already holds that leaf, the
enclosing label rides along as `carry2` (painted as an empty id span). Layout
puts them on the leaf's first fragment (a table's first cell line, not its
rule), so every anchored block has exactly one anchored fragment and no
backend keeps anchor state.

`tsrc --stage=blocktree` prints the tree (layouter, painter, traits, edges,
anchors, markers, spans).

## 3. Emit's part

Emit (`emit/emit.cc`) shapes each leaf from its `LeafSource` (the node, a
caption or marker-only role, the paragraph indent decision, a float's
caption rows, a code block's sidecar group): its inline stream (`Flow`: the
HList and its legacy lowering), its other tracks (`cells`: table cells, a
float's caption rows, sidecar rows) and a typed payload (`LeafData`:
`RuleData`, `GridData`, `TableData`, `RawData`, `ImageData`, `MathData`).
Emit makes no structural decision: no markers, indents, anchors or role
dispatch.

## 4. Layouters (`engine/src/layout/layout.cc`)

`DocLayout` keeps the cursor, the ExclusionMap and the KP parameters; the
registry `kLayouters[LayouterId]` dispatches every block:

- **Stack** lays its children one below the other with its effective gap
  between them. Between two consecutive leaves exactly one gap applies: the
  one of the deepest stack holding both — a list's `1/3` makes everything
  inside a list after its first block pack tighter, the root's `1/1` is the
  paragraph gap. Tops are separated by the root gap.
- **Paragraph** breaks its stream with the cached KP at the widths the
  ExclusionMap gives (the prefix beside a float), consumes the exclusion by
  its lines and materializes them (`materializeLines`, one function for every
  stream) with the alignment of its traits.
- **Replaced** places one box: a rule (its band is one leading), raw markup
  (its declared height), a block image (centred, its display box resolved
  against the measure), a display formula (centred, advance
  `max(asc + desc, leading)`); an image with a side is a float box — out of
  flow, its caption rows broken to its width beneath it, the exclusion added.
- **Grid** sets a code block on its ch grid (verbatim-design.md), the
  sidecar rows zipped beside each logical line at equal height.
- **Table** (plans P3-10, P3-14) is a grid of flow roots. Its `TableSpec`
  has:
  - tracks: v1 `cols` are equal fr columns; `tracks` may be fr, fixed,
    percent, or content-fitted (`auto`, `min`, `max`);
  - a rule preset: `grid` (v1), `booktabs` or `none`;
  - header rows;
  - every cell's place, from the HTML table model: each cell goes at the
    first free position of its row and covers its `colspan`×`rowspan`. A
    position no cell covers holds an empty cell. A row longer than the
    columns widens the table, and the executor reports it (`table-cells`).

  Track resolution (`tracks()`) proceeds in this order:
  1. Fixed and percent columns come first.
  2. Auto columns take their max-content if it fits. Otherwise they sit
     between their min-content and max-content in proportion.
  3. Fr columns share the rest above their min-content.
  4. Intrinsic widths come from the cells' leaves. Min-content is the
     widest run no legal break divides; max-content is the widest unbroken
     line. A spanning cell's excess is shared by its columns.
  5. A table that is still wider than the measure overflows it. This gives
     a `table-overflow` warning (D-Y09). On screen its block scrolls
     sideways; a paged sheet shows it.

  Each cell is laid out at its columns' width and halign (its own `align`,
  else its column's). A row takes its tallest single-row cell, and a cell
  spanning rows stretches the last of them. `valign` places a cell within
  its rows.

  Pagination: page cuts fall between rows only, never inside a rowspan
  group. The header rows, with the rules around them, are `kPagedHeader`:
  they keep with the first row and repeat atop a continuation sheet, at
  their original room.

Every in-flow box that is not a paragraph clears the float beside it,
unless its `beside: shrink` lays it out in the room its top line leaves
(plan P3-14).

**Placement (plan P3-15).** `block()` sends a side float (`place.float`
left/right, not an image float) to `sideFloat`, which lays it out with
`layoutDetached(i, w)`. That call runs the ordinary layouters with the
block as a flow root at x 0 and width w (no vertical-list entries), then
takes its fragments out of the frame. The float is placed as an atom (its
frames go with it), its exclusion added with its own gap, its vertical-list
entry out of flow. A page float (`top`/`bottom`/`page`) is laid out in
place, and its fragments become one movable box (`kPagedMovable`, plus
`kPagedBottom` or `kPagedPage`). A stack sets consecutive inline blocks
(`place.float: inline`) as lines of detached boxes (`inlineRun`).
`placedWidth` resolves `place.width`; when it is unset, `intrinsic()` gives
the content's max-content (captions left out), and a table's intrinsic
widths are its columns' (`tableIntrinsic`).

**Block traits (plan P3-14).** `block()` wraps every layouter with the
trait group (style-design.md §4):
- the medium filter;
- a pending Forced break (`break.before`, an empty `#pagebreak()` block,
  `break.after`) set on the first content fragment;
- the frame, for a framed block: a `Frame` fragment (`boxBlock`,
  `kPagedFrame`) pushed first, the content box inset by padding and
  border, the frame's height set after its content;
- the shrink narrowing;
- `keep: together`, which raises the tiers inside the block to
  KeepTogether;
- a container's `keep: with-next`.

The stack's gap between two children is max(gap, after*, before*), the
CSS-like collapsing of `space.*`, and the tops use the same rule. A
paragraph's `par.hang` indents its ParShape slots after `hangAfter`. Its
`breaker.*` traits set its BreakParams, and they are part of the memo key.
Paged frames are not cut. Each sheet draws the frames of the blocks its flow
meets, clipped. A box's `extTop`/`extBot` carry the padding of the frames
it opens and closes, so a sheet starts at its frame. Paged output gets its
own layout pass only when some block is for one medium (`media`).

## 5. Fragments

`Fragment` (`layout.h`) is one materialized line or box in paint order:
`kind` (`Line`, `Rule`, `CodeRow`, `Raw`, `Math`, `Image`, `Frame` — what it is
geometrically, replacing the old `special` codes), geometry (`y` is the top,
a rule's too; dumps and paint place a rule at its band's middle), the leaf
and track it belongs to, the item range of its stream, the justification
values, the join, the alignment flag, the gutter marker and the anchors.

`tsrc --stage=layout` prints the fragments (unchanged format since P1-15);
`tsrc --stage=vlist` prints each top's vertical list: the gap before each
leaf, any float clearance, its y and advance, out-of-flow floats.

## 6. Pagination (`engine/src/layout/paginate.cc`)

A post-pass over the finished layout: fragments group into atomic bands (a
table or a float box whole; a code block's logical line with its sidecar
rows; otherwise one fragment), bands are cut greedily into sheets with
keep-rules (a keep-with-next leaf's last band sticks to what follows;
paragraph widows and orphans of 2 lines), a violated cut backs up, an
impossible one overflows. `PageResult` lists each sheet's origin and bands;
the paged writer rebases each band's painted nodes into its sheet.

## 7. Boundaries

Layout, break, paint and the typeset writer never see `model/model.h`, not
even through another header (lint `layout-includes-model` follows includes
transitively). The style table lives in `model/style.h` for exactly this
reason; `FlowUnit` and `TopBlock` refer to the content tree only by forward
declaration.
