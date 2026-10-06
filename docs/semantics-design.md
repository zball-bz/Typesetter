# Semantics: element registry, Index and the staged resolver (design; as built from plans P1-10, P2-07)

What a node *is* — a heading, a figure, an equation, a footnote, a glossary
term, a bibliography entry — and everything that follows from it (numbers,
labels, references, captions, collected lists, lifted notes) is data: one
registry row per class, interpreted by generic code. Design source:
`docs/remediation/design/T3-semantics.md` (S1); later steps add document
declarations (P2-07), structured references (P2-09), parts/sites (P3-03),
identities (P3-04) and new collections (P3-13).

## 1. The registry (`engine/src/elements/registry.{h,cc}`)

`engine/data/elements.json` holds the built-in rows, in the form a document
declares its own (§6). It is embedded at configure time
(`gen/semantic_data.gen.h`) and parsed once (`Registry::builtin()`); a
registry builds from any document of that form (`Registry::fromJson`), and
each document resolves with the registry its declarations make (§6,
`Doc::registry`). The native test `unitRegistry` renames the figure row —
in a replacement of elements.json, and declared by a document
(`semantics/parity-declared`) — and checks the document comes out the same:
only the Index names the class.

- **counters**: flat or by level (`level-arg`, `depth`, a skipped level
  counting 0 — `1.0.1` — or 1), or keyed (one step per distinct key: the
  citation ordinals); (P2-07) `within: {counter, depth, sep}` — numbered
  within another counter: its first `depth` components, in its current
  pattern, prefix this one's and its steps at a level ≤ `depth` restart
  this one — a `pattern` (NumberingPattern, below) and `start` values.
- **systems** (P2-07): `{symbols, mode}` (`numeric`, `alphabetic`,
  `cyclic`, `fixed`), used in a pattern as `{name}`.
- **classes**: `select` (selectors: a node kind, argument predicates,
  `inside` a class; the most specific wins, the latest on a tie),
  `counter` + `numbering` (`always`, `labelled`, `never`), `supplement`
  (a term key, `{term}`, `{text}`, or texts by language `{en, zh, …}`: the
  pack, then its language, then en, then the first), `labels` (`user`,
  `none`, or `{arg}`), `title` (`text`, `{arg}`, or `{ext}`: the instance's
  EXT data — region options such as `title:` arrive as EXT), `outline`,
  `alias` (generated labels: prefix + number | key),
  `sites` (templates attached when numbered: `prepend` at `first-para`, an
  `arg`, or a `replace` of the node), `ref` (the reference form), `flow`
  (a lifted item: its flow, placement, marker template and marker alias),
  `table` (instances are rows of a keyed table; `row-key`: the row key is
  that argument, not a label — `bibentry`'s `key`), `forms` (named
  reference forms beyond the built-in `number`, `title`/`name`,
  `supplement` and `full`), and (as built in P2-05)
  the presentation traits `box` (`"figure"`: the box tree's figure —
  captions, floats) and `html` (`"figure"`: `<figure>`/`<figcaption>` on the
  semantic page); no stage reads a role string to decide them. Defaults
  (P2-07): a class without `select` selects `{role: <its name>}` on any
  kind (selectors never inherit through `like`), and a numbered class
  without `ref` reads as `[supplement, number]`.
- **collectors**: a query (`classes: outline` nested by depth, a `table`
  with `cited` — `cited` or `cited-then-all`, which a collect node's
  `cited` overrides — or a `flow`), the context entries take
  (`collector`, `instance`, `row`), and `wrap` / `entry` / `empty`
  templates; a keyed table's collector declares its ordinals and anchors
  (`rows: {counter, anchor}`; its rows are the instances of the classes
  naming the table) and how references to its keys read (`cite`).
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
| `{slot, or}` | a value of the site (number, supplement, title, label, alias, marker-alias, anchor, ordinal, body-text) or content (body, inline-body, block-body); `or`: the slot used when this one is empty |
| `{node, args, kids, style}` | a node: inline kinds take the site's style, block kinds style 0 unless `style: "site"`; an argument is a literal (string, number, boolean), `{slot}` or `{anchor}` (`#tsr-` + the slot); an empty slot sets no argument |
| `{styled: {bits, size}, kids}` | a delta over everything inside, inserted content included |
| `{when, kids, else}` | kids if the slot is set |
| `{each, sep, kids}` | the collector's items, or a group reference's keys |
| `{paras: {anchor, scale, tail}}` | a flow item's body as paragraphs: the anchor on the first, the tail at the end of the last |

Adjacent strings, terms and text slots make one text node (`图 1：`,
`§1.2`, `[1] `). A generated `ref` node is resolved like any other.

A document writes templates as content (P2-07): `slot(name, {or})`,
`slot('term:<key>')` (a locale word), `when(of, …kids)`, `each(of, {sep})`,
`strong(…)` / `em(…)` (a delta of their bits), strings and any other node
(its literal arguments; labels and EXT data dropped). The kinds `slot`,
`when` and `each` belong in templates: elsewhere they are an error node
(`template-only`). A template holds nothing counted, collected or flowing —
an `event`, `entry`, `collect` or `note` in one refuses the declaration
(`decl-invalid`) — and at most 512 nodes.

**NumberingPattern** (`semantic/numbering.h`), the engine's one parsed
mini-language (Typst-compatible): counting symbols `1 a A i I ① 一 *` and
`{system}`; text before the first symbol is a prefix, between symbols a
separator, after the last a suffix; a number with more components than
symbols repeats the last symbol and separator (`1.1` [2, 10] → 2.10, `A.1`
[1, 3] → A.3, `(1a)` [1, 2] → (1b)). The empty pattern is `1.1`.

## 3. Locale terms (`semantic/terms.{h,cc}`)

Words come from locale packs (`engine/data/locale`: en, zh-Hans, zh-Hant,
ja) chosen by the document language (`doc.lang`, the host default) through
exact → script (zh-TW/HK/MO → zh-Hant, zh/zh-CN/zh-SG → zh-Hans) → language
→ root (en). ja gives 図/表/式, zh-Hant 圖/表/式; every other language keeps
the English words. The `terms.*` settings override single words.

## 4. The phases (`resolve/resolve.cc`, `semantic/`)

1. **PHASE 0** (`declare.cc`, §6) — the registry, before instantiate.
2. **LOCATE** (`index.cc`) — one read-only pre-order walk: each classed node
   becomes an instance (number, level, title, anchor); every label
   registers uniformly — a user label of a minted shape is refused
   (`label-reserved`), a second declaration too (`label-duplicate`), both
   dropped from their node; aliases are minted (`h-1.2`, `fn-3`, `fnref-3`);
   keyed rows and flow items are recorded; a counter event applies where
   it stands (§7). An instance snapshots its number in its counter's
   pattern at that moment, and its supplement (a counter event's, else its
   class's).
3. **BIND** — citation ordinals in document order, a note's body at its
   marker.
4. **MATERIALIZE** (`materialize.cc`) — the output tree, without changing
   the input (D-S13): unchanged subtrees are shared, changed nodes are new.
   References get their form (or the cite group of a citeable table;
   `ref(target, {form, supplement})` picks a form and replaces the
   supplement word), classed nodes their sites and anchors, collectors
   their content, flow items their markers, `field{name, of}` the slot of
   the enclosing instance (or of `of`'s); events and entries leave nothing
   where they stand, and a paragraph of only those and empty text vanishes
   with them (the vacuous paragraph); a flow no collector placed goes where
   its class says (notes: the document end).

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

## 6. Declarations (plan P2-07; `semantic/declare.{h,cc}`)

Three layers patch the rows of elements.json's form, named by section and
name, **field by field** (the last write wins; a new name appends a row):

1. the built-in rows;
2. the host's `semantics.elements` / `.counters` / `.collectors` /
   `.systems` settings (objects in the same form; `affects: Ingest`):
   `{"semantics": {"elements": {"figure": {"supplement": {"text": "Fig. "}}}}}`;
3. the document's declarations — DECL `element`, `counter`, `collector`,
   `counter-system` (hoisted: the document's last declaration of a name
   wins whole, wherever it is; the earlier ones are superseded,
   `decl-redeclared`). A declaration's row is its EXT `row` (canonical JSON
   from the JS stdlib, a template written `{"$t": k}` for its k-th DECL
   template, converted from its raw nodes), else its EXT data as the row's
   scalar fields (`$.declare('counter', 'c', {pattern: 'i'})`).

`like: <own name>` in a patch means the previous layer's row, which a patch
is anyway. A row the loader refuses is dropped — `decl-invalid` at the
declaration (`semantics-invalid` for a host row) — and the others stand;
an unknown field is ignored with `decl-field`. An instance before its
class's declaration works (hoisting) and says so (`decl-after-use`, info).
Registries are cached by their inputs (8 entries): a document executed
again with the same declarations reuses its registry.

The JS stdlib (`runtime/src/shared/stdlib.mjs`) is where the sugar ends:

```js
#{
  const head = [strong(slot('supplement'), slot('number')), when('title', ' (', slot('title'), ')'), strong('.'), ' ']
  $.element('theorem', {
    counter: {name: 'thm', within: 'heading', depth: 1},     // declares counter thm
    supplement: {en: 'Theorem ', zh: '定理 '},                 // a string is literal text
    title: {arg: 'title'},                                    // not an attribute: EXT data
    sites: [{where: 'prepend:first-para', template: head}],
  })
  $.element('lemma', {like: 'theorem', supplement: {en: 'Lemma ', zh: '引理 '}})
  $.counter('equation', {within: 'heading', depth: 1})
  $.counter.system('greek', {symbols: ['α', 'β', 'γ'], mode: 'alphabetic'})
}
#!theorem(label: "thm-ua", title: "Univalence") … #theorem!     → **Theorem 2.10** (Univalence)**.** …
@thm-ua → Theorem 2.10 · #ref("thm-ua", {form: "title"}) → Univalence
```

- `$.element(name, spec)` — `select`, `like`, `counter` (a name, or
  `{name, within, depth, sep, numbering, start, …}` declaring it),
  `numbering` (`'always'`, `'labelled'`, `false`, or a pattern — its
  counter's), `supplement`, `title`, `labels`, `outline`, `sites`
  (`where: 'prepend:first-para'` sugar), `ref`, `forms`, `alias`, `flow`,
  `table`, `rowKey`, `box`, `html`. A numbered element without a counter
  counts with its own; given a counter it is numbered `always`. Returns a
  constructor: `theorem({label, title}, …kids)` builds the instance
  (options other than label and the style keys become EXT data; inline
  kids a paragraph) — the same group the `#!theorem` region builds.
- `$.counter(name, spec)` — `within`, `depth`/`sep` (of within),
  `numbering` (the pattern), `start`, `gap`, `levels` (by level),
  `levelArg`, `keyed`. `$.counter.system(name, {symbols, mode})`.
- `$.collector(name, spec)` — `query` (or `table`, `flow`; `select`
  lists arrive with P3-13), `context`, `wrap`, `entry`, `empty`, `rows`,
  `cite`. Returns a nullary constructor of `collect{what: name, cited?}`.
- `$.labels.import(src)` — P3-31; until then it says so (`labels-import`).

## 7. Events (plan P2-07)

`counterUpdate(name, {set, step, add, numbering, supplement})`
(= `$.counter.update`) returns an `event` node — a content value, placed
where it is spliced and applied there by LOCATE in pre-order: `set` (an
integer or a list: the components), `step` (a level), `add` (to the last
component), `numbering` (the pattern from here on), `supplement` (the word
the counter's instances read from here on). An event is level-neutral and
renders nothing (MATERIALIZE drops it; its paragraph vanishes when that
leaves it empty). One no EMIT reaches — a statement `#{ counterUpdate(…) }`
whose value is discarded — is `event-unplaced` at the construct that made
it; an unknown counter is `event-counter`.

```
#let appendix = () => counterUpdate('heading', {set: [0], numbering: 'A.1', supplement: {term: 'appendix'}})
#appendix()
= Proofs <proofs>          → A Proofs (with a heading site); @proofs → Appendix A; an equation inside → (A.1)
```
