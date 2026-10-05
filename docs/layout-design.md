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

**Roles are data.** `group{role}` is looked up in the role table (`figure`:
its paragraphs are captions and an image with a side floats with its caption
rows; `sidecar-lines`: a code block's sidecar track) by interned name; no
layer below compares a role or kind spelling (lint `role-string-compare`
covers boxtree/, layout/, paint/, render/).

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
- **Table** breaks each cell to its column's content width and rules the
  rows (three-line style).

Every in-flow box that is not a paragraph clears the float beside it.

## 5. Fragments

`Fragment` (`layout.h`) is one materialized line or box in paint order:
`kind` (`Line`, `Rule`, `CodeRow`, `Raw`, `Math`, `Image` — what it is
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
