# Render: paint and the typeset backends

Status: as built in plan P1-18 (design T7 S3,
`docs/remediation/design/T7-render-runtime.md`). Later steps: P3-05
(RenderResult and commit), P3-07 (spans on every line), P3-16 (the gutter as
a placed run), P3-36 (PresentationMap). The semantic backend stays a tree walk
(it runs before measurement) and is not described here.

## 1. Layers

```
Layout (fragments, layout/layout.h) ──► Paint (paint/paint.cc) ──► DisplayList
DisplayList ──► writeBlock / writeNodes (render/typeset_html.cc) ──► HTML
PageResult (layout/paginate.cc) ──┘ (paged: which nodes go on which sheet)
```

**Paint** is the only typeset-side code that reads the Config (the run
classes' base size, the code font features, the paragraph gap, the measure
for the equation number) and the leaves' payloads. **The writers** read only
DisplayList fields, the style table (`model/style.h`) and the interner: no
Config, no FlowUnit, no content tree, no state carried between nodes.

## 2. The DisplayList (`engine/src/paint/displaylist.h`)

- `DLRoot` — the root render contract (plan P1-04): language, the four font
  roles, the base size.
- `DLBlock` — one per frame: `pid`, the source base of its relative spans,
  its height, the gap after it, its nodes and runs.
- `DLNode` — one per fragment, in fragment order: kind, geometry (su; the
  writer rebases y), the attributes it carries (id and second id, span,
  join, ragged, cell, overfull, code highlight and continuation join), the
  values it prints (word spacing, a code row's font features and line
  height, a display formula's row height, its top offset and the number's
  right offset), the gutter marker, a Replaced box's payload, and its runs.
- `DLRun` — one DOM run, typed by what it paints: `Words` (a Latin run: the
  boxes of an item range, glue as spaces), `Chars` (a letter-spaced CJK run),
  `Glyph` (a punctuation glyph with its squeeze class, a pinned dash with its
  enforced width, an error object's text), `Hyphen`, `Spacer` (an indent or
  a boundary), `Math` / `Image` / `Raw` (inline objects), `CodeText` /
  `CodeCont` (a code row's segments and continuation indent). A run carries
  its style, link, inline anchor, synthetic flag, source start, data-syn and
  its own spacing values, all copied from layout's line values.

Runs open where the run instance changes (plan P1-12): style, link,
synthetic kind, a punctuation glyph, a pinned box or an object — the writer
never re-derives a boundary.

`tsrc --stage=dl` prints the DisplayList (a debug product; fixtures may
request its golden with `"products": ["dl"]`).

## 3. Writers (`engine/src/render/typeset_html.h`)

- `writeRoot` — the `.tsr-doc` (or `.tsr-doc.tsr-paged`) root.
- `writeBlock` — a flowing block: its `.tsr-para` container (`data-pid`,
  `data-s0`, height, the gap after it) and its nodes with spans relative to
  `data-s0`, so an edit leaves untouched blocks byte-identical (the editing
  loop's patch).
- `writeNodes` — a paged band: nodes `[lo, hi)` rebased into the sheet, spans
  absolute.

Ids come only from the nodes (each anchored block's first fragment), so the
flowing and paged outputs carry the same ids: the paged writer's old shared
`lastAnchored` state, which dropped every anchored unit whose index repeated
on a sheet (findings `render-runtime/missed:1`, `break-layout-pages/missed:0`),
is gone; `pages/paged-eq-ids` guards it.

The HTML is byte-identical to the pre-P1-18 serializer for every fixture
except where anchors used to be dropped.
