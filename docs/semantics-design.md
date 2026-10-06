# Semantics: element registry, Index and the staged resolver (design; as built from plan P1-10)

What a node *is* — a heading, a figure, an equation, a footnote, a glossary
term, a bibliography entry — and everything that follows from it (numbers,
labels, references, captions, collected lists, lifted notes) is data: one
registry row per class, interpreted by generic code. Design source:
`docs/remediation/design/T3-semantics.md` (S1); later steps add document
declarations (P2-07), structured references (P2-09), parts/sites (P3-03),
identities (P3-04) and new collections (P3-13).

## 1. The registry (`engine/src/elements/registry.{h,cc}`)

`engine/data/elements.json` holds the built-in rows, in the form a document
will declare its own (P2-07). It is embedded at configure time
(`gen/semantic_data.gen.h`) and parsed once (`Registry::builtin()`); a
registry builds from any document of that form (`Registry::fromJson`), and a
document can carry another (`Doc::registry`). The native test
`unitRegistry` renames the figure row and checks the document comes out the
same — only the Index names the class.

- **counters**: flat or by level (`level-arg`, `depth`, a skipped level
  counting 0 — `1.0.1` — or 1), or keyed (one step per distinct key: the
  citation ordinals).
- **classes**: `select` (selectors: a node kind, argument predicates,
  `inside` a class; the most specific wins, the latest on a tie),
  `counter` + `numbering` (`always`, `labelled`, `never`), `supplement`
  (a term), `labels` (`user`, `none`, or `{arg}`), `title` (`text` or
  `{arg}`), `outline`, `alias` (generated labels: prefix + number | key),
  `sites` (templates attached when numbered: `prepend` at `first-para`, an
  `arg`, or a `replace` of the node), `ref` (the reference form), `flow`
  (a lifted item: its flow, placement, marker template and marker alias),
  `table` (instances are rows of a keyed table), and (as built in P2-05)
  the presentation traits `box` (`"figure"`: the box tree's figure —
  captions, floats) and `html` (`"figure"`: `<figure>`/`<figcaption>` on the
  semantic page); no stage reads a role string to decide them.
- **collectors**: a query (`classes: outline` nested by depth, a `table`
  with `cited` order and an `all-arg`, or a `flow`), the context entries
  take (`collector`, `instance`, `row`), and `wrap` / `entry` / `empty`
  templates; a collector may declare the table its own children form
  (`rows`) and how references to its keys read (`cite`).
- **unresolved** / **unnumbered**: the reference forms for a missing label
  and a label on an unnumbered node.

Membership is decided once, at instantiate (`ContentNode::cls`, with the
class of the nearest classed ancestor for `inside`). No stage compares role
strings to learn what a node is.

## 2. Templates

A template is generated, style-neutral content (JSON arrays):

| item | meaning |
|---|---|
| `"text"` | literal text |
| `{term}` | a locale word |
| `{slot}` | a value of the site (number, supplement, title, label, alias, marker-alias, anchor, ordinal, body-text) or content (body, inline-body, block-body) |
| `{node, args, kids, style}` | a node: inline kinds take the site's style, block kinds style 0 unless `style: "site"`; an argument is a literal, `{slot}` or `{anchor}` (`#tsr-` + the slot); an empty slot sets no argument |
| `{styled: {bits, size}, kids}` | a delta over everything inside, inserted content included |
| `{when, kids, else}` | kids if the slot is set |
| `{each, sep, kids}` | the collector's items, or a group reference's keys |
| `{paras: {anchor, scale, tail}}` | a flow item's body as paragraphs: the anchor on the first, the tail at the end of the last |

Adjacent strings, terms and text slots make one text node (`图 1：`,
`§1.2`, `[1] `). A generated `ref` node is resolved like any other.

## 3. Locale terms (`semantic/terms.{h,cc}`)

Words come from locale packs (`engine/data/locale`: en, zh-Hans, zh-Hant,
ja) chosen by the document language (`doc.lang`, the host default) through
exact → script (zh-TW/HK/MO → zh-Hant, zh/zh-CN/zh-SG → zh-Hans) → language
→ root (en). ja gives 図/表/式, zh-Hant 圖/表/式; every other language keeps
the English words. The `terms.*` settings override single words.

## 4. The phases (`resolve/resolve.cc`, `semantic/`)

1. **PHASE 0** — the registry, before instantiate.
2. **LOCATE** (`index.cc`) — one read-only pre-order walk: each classed node
   becomes an instance (number, level, title, anchor); every label
   registers uniformly — a user label of a minted shape is refused
   (`label-reserved`), a second declaration too (`label-duplicate`), both
   dropped from their node; aliases are minted (`h-1.2`, `fn-3`, `fnref-3`);
   keyed rows and flow items are recorded.
3. **BIND** — citation ordinals in document order, a note's body at its
   marker.
4. **MATERIALIZE** (`materialize.cc`) — the output tree, without changing
   the input (D-S13): unchanged subtrees are shared, changed nodes are new.
   References get their form (or the cite group of a citeable table),
   classed nodes their sites and anchors, collectors their content, flow
   items their markers; a flow no collector placed goes where its class
   says (notes: the document end).

Interim exceptions of D-S13: the resolver still writes a reference's `url`
(P3-04 moves identities to the renderer) and an equation's tag as its `name`
argument (P3-26). Resolve keeps its *Once* rerun class until sidecar
extraction leaves it.

## 5. The Index

`Doc::index`: instances, labels (registration order), rows (with ordinals)
and flows, with spans — no node pointers after resolve. `tsrc
--stage=index` prints it (a golden for every fixture):

```
instance heading 1 level=1 label=sec-a title="Labels" @[0,8)
label side-1 -> (unnumbered) @[18,73)
row bib kp81 ordinal=1 @[0,0)
flow notes: fn-1 fn-2
```
