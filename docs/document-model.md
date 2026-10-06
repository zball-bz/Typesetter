# Document Model Specification

Status: **draft for review**. Normative for `engine/src/model|ops|resolve|render`, for `runtime/src/shared/ops.ts` and the executor, and for the dump formats golden tests consume. Companions: [design-decisions-v2.md](design-decisions-v2.md) (§ refs), [architecture.md](architecture.md), [testing.md](testing.md).

---

## 0. Products and lifetimes

A document handle owns, in order of production:

| product | lifetime | rebuilt by |
|---|---|---|
| SourceText | handle | recompile |
| AST, JsProgram | transient (until ingest) | recompile |
| **ContentTree** (post-resolve) + label/term/bib tables | handle | recompile |
| **BlockStreams** (per paragraph) + MetricStore | handle | recompile; entries invalidated by dppx change |
| **LayoutResult** | until next typeset/relayout | `tsr_typeset` / `tsr_relayout` |
| Diagnostics | handle; each entry carries its origin pass (compile / ingest / provide / emit / render) | a pass that re-runs (emit after late metrics or a width change, render) replaces its own slice (plan P0-11) |

Persistence of the middle products is what makes the pull-loop resumable and `relayout` cheap (architecture §2.4). All of it lives in the document arena.

## 1. Identity and anchoring

- **NodeId**: `u32` arena index. Document-scoped, stable for the handle lifetime, **not** stable across recompiles — continuity across edits is by source offsets, never by id.
- **pid** (paragraph id): the NodeId of each direct child of `doc`. The unit of upgrade swaps and of `tsr_render_typeset(range)`. DOM: `data-pid`.
- **Spans**: byte offsets `[start, end)` into the UTF-8 source. Every node carries one. Synthetic content takes the span of its generating construct: splice-produced nodes get the splice span; fence-handler nodes get the fence body span unless the handler passed a narrower offset (two-tier fidelity, v2 §4.1); resolver-produced content (ref text, collector expansions) gets the span of the `REF`/`COLLECT` site. As built (plan P2-04): the splice/region/fence rule is the SPAN/AT occurrence rule of §4.3, and instantiation's containment gives a span-less or out-of-parent node its parent's span.

## 2. Content tree

Node = `{ kind: u16, span, style: StyleId, args, children }`. `style` is resolved at instantiation (§4) and interned. `args` is a small key→value map; value types: `str | f64 | bool | NodeId | null`.

### 2.1 Kind set

> The authoritative, generated kind table (levels, body models, attributes and their value domains) is [`docs/schema-table.md`](schema-table.md), generated from `engine/schema/schema.json` (remediation P0-06). Where the prose below disagrees, the schema wins.

| kind | level | args | children | from |
|---|---|---|---|---|
| `doc` | block | — | blocks | M1 |
| `para` | block | — | inline | M1 |
| `heading` | block | `level`, `label?` | inline | M2 |
| `list` | block | `ordered`, `start?` | `item*` | M2 |
| `item` | block | — | blocks; in a description list its term, a `seq` in slot `term` (P3-34) | M2 |
| `terms` | block | — (plan P3-34; D-L08: `/ term: description`) | `item*` (N6), each with its term | P3-34 |
| `quote` | block | — | blocks | M2 |
| `codeblock` | block | `lang`, `wrap?`, `lineNo?`, `hl?` (ops v3) | plain text child OR one `seq` per line of styled runs (CH1) | M2/CH |
| `rule` | block | — | — | M2 |
| `group` | block | `role?`, `label?` | blocks | M2 |
| `table` | block | `cols`, `align?`, `label?` | `trow*` | M6 |
| `trow` | block | — | `tcell*` | M6 |
| `tcell` | block | — | blocks (schema since plan P2-16; mixed in the normal form: all-inline content is the cell's line, a block makes it blocks — emit flattens a cell to one stream until T6) | M6 |
| `term` | block | `name`, `label?` | blocks (description) | M4 |
| `collect` | block | `what` (a collector's name), `cited?` (P2-07) | — (expanded by resolver) | M4 |
| `mathblock` | block | `src`, `label?`, `name?` (resolver: "(n)") | — (MathBox at emit) | M7 ✓ |
| `error` | both | `message`, `code` | best-effort content | M1 |
| `comment` | both | `body` | — | M2 |
| `text` | inline | `str` (P2-10: U+000A in an inline-model text is a soft break — a space, or nothing between two joining characters, resolved after the normal form with the paragraph's context across node edges, plan P4-02; code/verbatim bodies keep newlines) | — | M1 |
| `styled` | inline | `delta` (§3) | inline | M1 |
| `link` | inline | `url` | inline | M2 |
| `code` | inline | `str` | — | M2 |
| `ref` | inline | `target`, `form?`, `supplement?` (P2-07; +resolved fields) | child refs (a group, `@[a, b]`) and an `extra` seq (slot `extra`: the bracket of `@x[…]`) — P2-09 | M4 |
| `mathinline` | inline | `src` | — (MathBox segments at emit) | M7 ✓ |
| `math` | — (wire only) | `display?`, `label?`; kids: `mathsrc` fragments and holes (plan P2-15) | instantiated as `mathblock` (display) or `mathinline`, kids kept; its source assembled at emit (math-design §10.2) | P2-15 |
| `mathsrc` | trivia | `src` (one line of a formula, as written; its own span) | only under a formula | P2-15 |
| `equations` | block | — ; kids: display formulas only (N6) | a multi-row display (D-S11; plan P2-16): its rows stand in its place until T6's layouter and T3's per-row numbering (P3-29) | P2-16 |
| `raw` | inline | `html`, `w?`, `h?` | — | M6 |
| `fill` | inline | — (`#fill`, nullary; plan P2-16) | fil glue: takes its line's slack (a site's ∎ at the measure's end, P3-03) | P2-16 |
| `hardbreak` | inline | `\` at the end of a line; the `linebreak` constructor (plan P3-33). The engine sets it as a forced line break (plan P1-13) | — | — |
| `field` | inline | `name`, `of?` | — (the enclosing instance's slot, or `of`'s; P2-05/P2-07) | P2-05 |
| `event` | trivia | `counter`, `set?`, `step?`, `add?`, `numbering?`, `supplement?` | — (a counter event, applied in place and dropped; P2-07) | P2-07 |
| `entry` | trivia | `key?` (+ `role`) | inline (a row of its class's table, dropped in place; P2-07) | P2-07 |
| `slot` / `when` / `each` | inline / transparent | `name`, `or?` / `of` / `of`, `sep?` | template content (P2-07; elsewhere an error node) | P2-07 |

Notes:
- **Labelable kinds** (accept `label`): `heading`, `group`, `table`, `term`, `mathblock`. Labels are args, not nodes.
- **Declaration epoch** (plan P2-15): instantiation stamps every node with `declEpoch`, the positional declarations (math.symbol / op / fn, rule) whose flow index is at most its EMIT's; a moved or cloned node keeps it, so what a formula's names mean is fixed where it was emitted.
- **Figure is a declared class, not a kind**: `group{role:"figure", label}` with a caption paragraph is selected by the figure row of the element registry (docs/semantics-design.md; plan P1-10) — keeps the engine kind set minimal, and a document declares classes the same way (P2-07: `$.element(name, spec)` writes a registry row; a `#!name` region builds `group{role: name}` with its options as EXT data, which the row's default selector `{role: name}` picks up).
- **Positional semantics are nodes** (P2-07): a counter event (`counterUpdate`) and a table row (`entry`, e.g. a bibliography entry) are level-neutral nodes that render nothing where they stand; template-only kinds (`slot`, `when`, `each`) are content of declarations.
- `val(x)` is not a kind: primitives splice as `text`; content values splice as themselves.
- **User constructors compose engine kinds.** There is no user-defined kind; custom constructs are built from `group`/`styled`/`raw` plus the rest. This is what keeps layout closed under the kind table.

## 3. Styles

As built (plan P2-08): the class bits below retired from the style. A style
is the run properties of docs/style-design.md — weight, italic, decoration,
font role, baseline, size, family, language, color — plus the script T5's
classifier gives a run; the dumps keep the bits' spellings (`BOLD`, `EM`,
`CODE`, `CJK`, `U`/`O`/`S`, `SUP`). The original table:

**Base class bits** (u64; carried from v1, bit indices frozen):

```
0 LATIN   4 BOUNDARY    8 PUNCT_OPEN   12 SPACE
1 CJK     5 HAS_STYLE   9 PUNCT_CLOSE  13 LINK
2 EM      6 CODE       10 INDENT       14 CJK_SPACE
3 BOLD    7 CJK_EMPH   11 HYPHEN       15 PUNCT_SPACE
16 UNDER  17 OVER      18 STRIKE   (CH1: text-decoration, metric-neutral)
```

- `TextStyling = { classBits: u64, dynClasses: sorted u32[], inline: InlineStyle? }`, interned by hash → `StyleId`. Blocks and runs store StyleIds, never copies.
- `InlineStyle` props (initial set): `fontFamily, fontSizePx, fontWeight, italic, color, letterSpacingPx`. As built (plan P1-02): the run properties are rows of the `props` section of `engine/schema/schema.json`; `Styling`, its equality/hash/patch/dump code, the typeset serializer's declarations and the executor's key tables are generated from them (docs/style-design.md), and every textual value is checked against a generated domain DFA at decode. Adding a property is a row plus its consumer, not an ops version bump.
- **Implemented subset (ops v2)**: `fontFamily` (CSS family list; wins over the class-based body/cjk/mono split and the `.tsr-cjk` var by inline-style specificity), `sizePx` (absolute base; `sizeMul` still composes on top), `color`, plus `lang` (BCP-47 → per-run `lang` attribute, drives OpenType 'locl' forms — the promised set extension). `fontWeight`/`italic` ride the class bits; `letterSpacingPx` is deliberately NOT exposed — letter-spacing is the engine's CJK justification channel and a user value would fight it. Caveat: canvas measurement has no lang concept, so a `lang` that changes advances via 'locl' is invisible to measurement — defined-width dashes/ellipses are immune, fullwidth punctuation is 1em in every locale, so the practical exposure is nil (recorded).
- Authoring surface: inline `#style({font, lang, color, sizePx, bold, italic})[…]`; block scope `#{ $.style.push({…}) } … #{ $.style.popTo(h) }`; region scope `#!aside(style: {font: '…', lang: "zh-TW"})` — as built (plan P2-08) the header's `style:` is the region node's own style change (it used to sniff `font`/`lang`/`color`/`sizePx` and wrap the region in a `styled` node); a style key at the top of a default region's header is a `ctor-arg` warning with the fix.
- As built (plan P2-08): every style change is a **delta node** (a childless `styled` node): a `styled` node's own attributes, the universal `style` attribute (a node's own change), and the schedule's `STYLE_PUSH <delta>`. Effective style = the stack at the EMIT ∘ the deltas on the path down, each node's own after its parent's. `size` follows CSS (D-T01): px replaces, em/% multiply. A footnote marker is `ref{role: fn-marker, attach: prev}` (as built in P3-01 its super baseline and 0.7em are the `fn-marker` default rule); `attach` (universal: prev/next/both) keeps an inline extent on the line of its neighbour.
- **The cascade** (as built, plan P3-01; `model/cascade.{h,cc}`, design T4 Cascade). One fold computes, at emission, a node's computed **run style** (`style`), its **block properties** (`props`, a `NodePropsTable` id: `par.indent/align/hyphenate`, `block.gap/indent/keepWithNext`, `list.marker` — inheriting rows start from the parent's, the others from their initial value) and the **rules in force for its children** (`env`, a persistent list: env 0 the defaults, env 1 the host's `style.rules`, then each `$.set` and `style.where` extends it). Order per property, last wins: base rules, the document's rules outer to inner, the node's own delta, forced host rules. Each node also keeps its **scope**: the rule-free projection (the stack at its EMIT and the own deltas on its path) — what the semantic page writes and what lifting carries. Nodes made after instantiation go through the same fold: normalize's wrappers and the resolver's generated nodes take the rules at their place (`settleMade`; the resolver's inline nodes at creation: `Cascade::make`, their roles selecting rules — `caption-label`, `term-name`, `fn-marker`, `note-body`), code tokens are text of class `tok-<tag>` under the code body, and a footnote's body is lifted to the notes list under an inline `styled{role: note-body}` (`Cascade::reenter`, D-S09): its own deltas travel, the rules of its site fold in again, so a footnote in a heading is not bold. The box tree reads block properties (block lengths in the block's own em), emit reads computed styles; neither composes presentation.
- **StyleDelta** (used by both `styled` nodes and the schedule stack): `{ addBits: u64, addDyn: u32[], patch: InlineStyle? }`.
- **Resolution** (at instantiation, §4): effective style of a node = fold of (schedule stack at its `EMIT`) ∘ (path of `styled` deltas from the emitted root down to the node). Bits OR; dyn sets union; inline patches nearest-wins per prop.
- **Binding time is emission-time** (v2 §12): a DAG value emitted twice under different stacks instantiates into two differently-styled subtrees. The instantiation walk therefore *copies* per emission; the DAG is a sharing optimization of the op stream, not of the content tree.
- **Parse nesting** (fuzz finding at the P2 end; `kMaxNesting`, linepass.h). The front end nests containers (quotes, list items, regions), content bodies (content arguments, notes, keyword bodies, link text, reference supplements, content literals) and inline pairs at most 128 deep along any path. A deeper quote or list marker is text, a deeper region opener an error block, a deeper body one `error{nest-limit}` node in its place (its text unread), a deeper pair marker text — each with a `nest-limit` diagnostic. Every recursive walk of the AST stays shallow, and codegen's program stays inside its reader's bound.
- **InstLimits** (remediation P0-07). Because copies are per occurrence, a few bytes of ops can describe an exponential tree. The copy runs on an explicit stack with two bounds: at most max(262144, 64 × raw nodes) nodes, and nesting at most 256 deep. A cut-off subtree becomes `error{inst-limit}` with one diagnostic.
- **Normal form** (P0-07, P2-11, `model/normalize.cc`). As built in P2-11 a node's kids sit at a position its body model gives (Blocks, Inline, Opaque; a `position` body takes its own; a note's body is mixed), effective levels resolve Transparent kinds through their kids and Adaptive kinds (group, term, error, raw, image — raw and image became adaptive in P2-11) fit either, and six rules apply: N1 (gone in P3-17, D-I02: display math splits a paragraph like any block, its label kept); N2 the unwrap below; N3 the block-seq splice below and, at a Blocks position, the empty-paragraph removal; N4 at a Blocks position a run of inline-level kids is one paragraph; N5 (P3-17, D-I02) a paragraph at a Blocks position holding a block splits around it into `para, block, para{cont}`. The `cont` paragraph (an engine-set attribute) is the same paragraph continued: its style, rules and properties are the original's, it has no first-line indent, and no space stands before it (in layout's su; on the semantic page `margin-top:0; text-indent:0`). A block in any other Inline position (a heading, a link, an inline group) becomes `error{block-in-inline}` around it; N6 a list holds items, a table rows, a row cells, an `equations` block display formulas (P2-16) — anything else is wrapped in `error{content-model}`. The original three rules:
  - a paragraph made only of empty text vanishes;
  - a paragraph whose only child is a block- or adaptive-level node is replaced by that node (for example a `#toc` splice alone on a line);
  - (P1-08) a `seq` holding a block — the value of a content body with several blocks, `#callout[⏎- a⏎⏎text⏎]` — takes its place among its siblings, and a paragraph that is only such a `seq` is those blocks. A block `seq` inside inline content (`#strong[⏎p1⏎⏎p2⏎]`) is not split yet (T2's level normalization).
- Block boundaries snapshot the schedule stack height; error recovery pops to it (remediation P0-05/P2-02: a normal block exit does not — a `#{ $.style.push(…) }` statement styles the blocks after it; `docs/lowering-design.md` §4).

## 4. Ops (normative binary contract)

### 4.1 JS-side values are shadow nodes

A content value in user/constructor JS is a **shadow node** `{ kind, args, children: shadow[], span, opId }` — a lightweight JS mirror created by each constructor as it writes the op. Why: constructors must be able to **traverse and regroup** content (the table constructor walking region children and splitting cells at tree level, v2 §4.1) — bare opaque ids cannot support that, and querying WASM mid-execution would be chatty. Regrouping emits new `MAKE_NODE` ops that reference the *existing* child `opId`s — the DAG shares; nothing is re-encoded. `m`-tag fragments (as built, plan P2-13): `tsr2_fragments` returns a LowerProgram — one block per text, its holes as out-of-band descriptors — which the executor runs on the same interpreter against the same constructors, so a fragment's values are shadow nodes like any other (docs/lowering-design.md §5.1); nothing is rebased.

### 4.2 Buffer layout

```
header   magic "TSOP", version u8 (=2), nStrings varint, stringBytes varint, nOps varint
strings  UTF-8 blob + varint end-offsets
ops      op stream (all ints varint/LEB128 unless noted)
```

### 4.3 Opcodes

```
0x01 MAKE_TEXT   strRef                                  → id
0x02 MAKE_NODE   kind nargs (argKey argVal)* nchildren id*  → id
0x03 EMIT        id
0x04 STYLE_PUSH  id   (ops 11, plan P2-08: a delta node — a childless styled
                       node; it was `bits npatch (argKey argVal)*` in v2–10)
0x05 STYLE_POP_TO height
0x06 SPAN        id start end       (post-hoc span attach; codegen wraps
                                     constructor calls in __at(node, s, e))
0x07 DIAG        sev(u8) codeRef msgRef start end   (since 7, plan P2-01:
                                     an execution diagnostic — 0 info,
                                     1 warning, 2 error; a stable code)
0x08 AT          id start end                       → id   (since 8, plan
                                     P2-04: an occurrence alias — the value
                                     `id` spliced again, instantiated with
                                     this span at its root)
0x09 RAWMAP      id n (cooked raw)*                 (since 8, plan P2-04:
                                     a text's cooked→raw map, raw relative
                                     to its span start)
0x0A DECL        type s e nargs (argKey argVal)* ntempl id*   (since 9,
                                     plan P2-05: a typed declaration at its
                                     flow position)
```

As built (plan P2-05; design T2 S9): **universal attributes**. Every kind but
`doc` and `text` accepts, after its own attributes, `label` (an anchor — any
block a labelled node opens carries it), `role` (the element class's name),
`slot` (the part of its parent it fills — the values a kind gives a meaning
are schema.json `slots`, plan P2-16: a code block's `margin`, a reference's
`extra`, every block's `tag`; such a child is a part, exempt from its
parent's body model in the normal form, its kids at the slot's model;
consumers read the generated `SlotId`), `syn` (the kind of generated text it
is), `copy` (`text` | `omit` | `replace:<text>`), `class` (style tokens, for
T4) and **EXT** data: `argKey=ext nameStrRef argVal`, a scalar under a name
matching `[a-z][a-z0-9-]{0,31}`, opaque to emit and layout (`extAttr(node,
name)`). The universal rows, the `field` kind (an inline placeholder: until
T3's elements fill it, it reads as unresolved, `field-unresolved`) and DECL
are since 9, so only buffers that use them advertise it. Attributes the
resolver sets (`ref.url`, `mathblock.name`) are flagged *resolved* and
dropped from input with `ops-arg`. **Declarations** (`$.declare(type, name,
data, ...templates)`; types in schema.json `decls`): a DECL carries its type,
span, flow position (the EMITs before it), name, EXT data and template nodes;
the reader collects them after decoding (D-I07); instantiation makes them
style-neutral `Decl`s on the content tree. A hoisted type's (element, counter,
collector, counter-system, doc, locale, fontRoles) last declaration of a name
wins wherever it is (`decl-redeclared` info); positional types (rule,
math.*) apply from their position. The tree dump lists them after the tree.
As built (plan P2-07; since 10): `element`, `counter`, `collector` and
`counter-system` declarations are rows of the element registry — EXT `row`
holds the row's canonical JSON (templates `{"$t": k}` → the DECL's k-th
template), read in PHASE 0 before instantiation (docs/semantics-design.md
§6); the kinds `event`, `entry`, `slot`, `when` and `each`, `ref.form`,
`ref.supplement` and `collect.cited` are since 10, and `collect.what`
widens to any collector name.

**Cooked→raw maps** (plan P2-04; design T1 TextRaw; the prerequisite of
per-atom source spans, P4-03). A text's cooked string differs from its raw
source where the parser decoded an escape (`\*`), collapsed a run of blanks
or joined two lines (container prefixes and indentation removed). The parser
records breakpoints (cooked offset, raw offset) with identity between them;
the LowerProgram's TEXT carries them, the executor writes RAWMAP, and the
instantiated text node keeps them (`ContentNode::rawmap`) — unless it is not
at its own source (an occurrence alias, a contained span). A text without a
map is positionally its own slice (a join cooked to a space keeps offsets).
The tree dump prints `raw=c:r,…`; a unit check walks every fixture's text
and verifies each cooked byte against its mapped source byte.

As built (plan P2-04; design T2 S7): **occurrence spans**. Every construct
the interpreter runs — a markup call, a splice, a fence, a region — gives its
result the construct's span: a node made during the construct gets SPAN
(unless it already has one), a value made earlier and spliced here gets an
AT alias, so a value spliced twice has two occurrences with their own spans
and is never re-spanned. Instantiation applies **span containment**: a node
whose span is empty or outside its parent's takes the parent's, so content
built by code, region interiors and spliced values stay inside the paragraph
that holds them (editing elsewhere never moves a paragraph's source range).
A default fence's code text carries the body's span.

As built (plan P2-01; design T2 S4): **node values** are frozen and branded
with a module-private symbol (`runtime/src/shared/opbuf.mjs`): node-ness is
the brand, never a duck-typed field, and a value cannot change after its op
was written (spans live in the buffer's side table). **One content
protocol**, `toContent` (`runtime/src/worker/executor.mjs`), serves splices,
constructor children, handler returns and m`…`: a node is itself;
string/number/bigint are text; `null`, `undefined`, `false` are nothing (a
splice of `undefined`/`null` says so: `splice-undefined`); arrays and
iterables flatten; an object with `[Symbol.for('tsm.content')]()` converts
itself; a function marked `[Symbol.for('tsm.nullary')]` (`toc`, `notes`,
`glossary`, `rule`) is called, any other is an error node
(`splice-function`); anything else is its `String()` with `splice-object`.
`m`…`` keeps interpolated content. **One diagnostic channel**: every error
the executor builds (`script-error`, `script-syntax`, `region-error`,
`fence-error` — at `ctx.error`'s offset into the body — `bib-load`) and
every execution warning is a DIAG op; the engine no longer scans error
nodes. Codegen's `__height(i)` names the running unit, so a diagnostic
about a splice points at its block. `$.style.push` takes a patch object
only.

- Ids are implicit: each MAKE op takes the next sequence number. Post-order by construction (JS evaluation order), so every referenced child id < the referencing op's id.
- `argVal` is tag-prefixed: `0=null, 1=bool, 2=f64 (8 bytes LE), 3=strRef, 4=nodeId`.
- `EMIT`/`STYLE_*` form the schedule and are only valid at top level (codegen wraps top-level content in `EMIT`; `$.style` writes stack ops).
- **Validation** (the reader is a fuzz target — it consumes JS-produced input and must reject, never crash): magic/version; string refs and node ids in range; child id < own id; unknown kind → `error` node + diagnostic, not a crash; stack height underflow → diagnostic + clamp.

## 5. Resolver

As built (plans P1-10, P2-07): the staged resolver of docs/semantics-design.md
— PHASE 0 (the registry: built-in rows < host `semantics.*` settings < the
document's declarations), LOCATE (instances, labels, rows, counter events),
BIND (citation ordinals), MATERIALIZE (references and their forms, sites,
fields, collectors; events, entries and vacuous paragraphs dropped). The
original plan follows.

Pure function of (ContentTree, Config). Document-order walk:

- **Counters**: section numbers from the derived heading tree; counter classes `figure | equation | footnote | <user>` with reset rules from config (`none | section`). Counter values are snapshotted into the label table at each label site.
- **Label table**: `label → { nodeId, kind, counters, textExcerpt }`. Duplicate label → diagnostic `label-duplicate`, first wins.
- **REF resolution**: `form = number | name | full` (default per target kind); output replaces the ref node's rendered content and sets `targetAnchor`. Unresolved → literal `??` content + `ref-unresolved` diagnostic.
- **Collectors**: `collect{what: toc|glossary|lof|bibliography}` expand into engine-kind subtrees (nested lists of links) from the tables. Citation order for bibliography = first-citation order.
- Runs before any rendering, so fallback HTML already carries final numbers (v2 §11.1).

Implementation notes (M4, normative for the dumps):
- The resolver **mutates the tree in place** between instantiation and emission; `--stage=tree` shows the post-resolve tree.
- Every heading gets an anchor: user label, or auto label `h-<number>` (also the fallback when a user label is a duplicate). Labeled units render `id="tsr-<label>"` on their first line; resolved refs render as `<a href="#tsr-<label>" data-syn="ref">`. As built (plan P3-04, design T3 S5 / T7 S9): identity is decoupled from its DOM spelling — MATERIALIZE writes no `url` for internal targets; a resolved reference or an internal link carries its target label (`ContentNode::anchorTo`, SemInfo.targetAnchor; a generated link also its `target` attribute), runs carry a `LinkTarget` (a label or an external URL), and only the serializers spell ids and hrefs, through `AnchorNamer` (prefix: the setting `render.idPrefix`, default `tsr-` — as built in P3-06, a render sets it for its duration; a preview fragment suppresses ids). `#link({target: "x"})[…]` links to a label (`ref-unresolved` when none). Labelled rules and raw blocks take anchors on both pages (S9b).
- `ref` keeps its kind; the resolver fills kids (display text) and a `url` arg. Display text per target kind uses the config supplements (`supHeading` "§", `supTable` "表 ", `supFigure` "图 ").
- `term` rewrites to `group{role:"term", label:name}` — bold name para (inline description joined with " — "), block description children follow.
- A paragraph whose only child is a `term`/`collect` splice is that construct at block level (unwrapped before dispatch).


## 6. Block stream

### 6.1 Units: `su` (subpixel unit) = 1/64 CSS px, `i32`

All engine-internal widths/positions are integer `su`. Why fixed-point: golden determinism (no float-summation-order variance across platforms/compilers) and a direct match to layout-engine precision. Measurement f64 px values are quantized on ingestion:

- word/advance widths: `ceil(px * 64)` **plus** `config.epsilon.perWordSu` (default 1) — this *is* the §7 ε policy: systematic overestimate, lines may only come out short;
- container widths: `floor(px * 64)`;
- vertical metrics: `round`.

Widths are measured per string with two exceptions. ——/…… **pair blocks are
never measured**: their width is the App C-normative 2em, and the typeset
serializer pins the pair span to `display:inline-block;width:2em` — canvas
and DOM disagree on cluster shaping / font selection for consecutive U+2014,
so measurement there cannot predict rendering. CJK-class runs measure (and
render, via `--tsr-cjk-font`) with `fonts.cjk`, keeping ambiguous glyphs
(dashes, ellipses, curly quotes) out of the Latin face.

**Quantized widths feed the breaker only.** Justification arithmetic (line slack, per-gap Δ) uses the **raw f64 px** measurements, which the metric store retains alongside the quantized value — otherwise ε and ceil would leak into the right edge as a systematic ~0.3px shortfall. Overflow safety comes from quantization; edge precision comes from raw math.

Document-height accumulation uses i64.

### 6.2 Block struct (formalizing the PoC)

```
LinebreakBlock {
  width, breakWidth, spaceWidth : su
  breakPenalty                  : f32   (INF allowed)
  stretchWeight                 : f32   (0 = rigid; Latin space 1.0; CJK glue = k)
  style                         : StyleId
  flags                         : isSpace | isHyphen | isCJK | isBoundary | isIndent
  content                       : text strRef | inlineBox nodeId
  span                          : source span
}
```

`stretchWeight` carries the v2 §8 k-rule into the breaker: line stretchability = Σ weights; the renderer distributes `Δword` per unit weight, so cost model and rendering agree by construction. Emission rules per Appendix C of v2.

**Breaking semantics (as built, plan P0-12; rules at the top of `break/break.cc`).** The breaker reads the TeX item projection of the blocks (`break/items.h`: Box / Glue / Penalty{Normal, Forbidden, Forced} / Disc{pre}): `BREAK_INF` is Forbidden — never a candidate; glue at a break and at a line start (paragraph start included) is discarded, so the optimizer measures exactly the range layout renders; a hyphen point adds its glyph only when broken; the paragraph end is a Forced break whose line has fil stretch and normal shrink. Line cost is `min(mapped(x)^exponent, 1e4)` (an integer power by multiplication), Overfull (x < −shrinkThreshold) is a class, penalties are i32 thousandths; ties go to lower demerits, then fewer lines, then the later parent. The search (plan P1-14) is TeX's active list: a node leaves it as soon as its line becomes Overfull, a Forced break deactivates every earlier node, line counts are kept apart only while the parshape prefix makes widths depend on them — no window, no line-count pruning, no retry ladder; `BreakParams` adds optional tolerance and emergency-stretch passes (off by default). When no path exists, the final pass rescues: the best active node breaks at the first legal break after the run, the line is Overfull — set at the shrink limit by layout, marked `data-overfull` in HTML, reported as `overfull-line`. A breakpoint is the index of the next line's first block after discard. Native and WASM builds produce identical breaks (`tools/wasm-goldens.mjs`).

### 6.3 Table cells (M6 v1)

Each `tcell` flattens to its own miniature block stream (`TableCell`), broken
by the same KP breaker at the cell content width. v1 geometry: **equal
columns** (`colW = measure / cols`), horizontal cell padding 0.4em, vertical
row padding 0.3em, full-width rules above/between/below rows. Cells are
**ragged** (never justified); the `align` string ('l'/'c'/'r' per column)
shifts whole lines at layout time. Cell lines carry `data-cell="1"`, no
`data-join`, and reference the cell's block stream via `cellIdx` in the
layout result; a label inside a cell (an inline term) anchors the cell's
first line, and the cell's breaks appear in the breaks dump (`cell=`). Region provenance is materialized at codegen: each source
line of a region paragraph is a row, segmented at top-level unescaped `|`
(code spans and splices are opaque; `\|` escapes; `||` is an empty cell;
leading/trailing empties of |-framed lines drop). Non-tabular regions rejoin
the segmentation (" | ") into ordinary paragraphs and become
`group{role:<name>}`. Fences compile to a runtime dispatcher: unknown tags
fall back to plain code blocks; handlers (registered `#{ $.fence(tag, fn) }`
before use) may be async, get `{args, offset, m, error, raw}`, and a thrown
handler becomes a renderable error node. `raw` nodes are block units with
handler-declared height (default one leading). Deferred: `m.parse` WASM
re-entry, the indented cell-continuation rule, `#use` (as built since: P2-13
`m.parse`; P3-31 `#use`, host-protocol-design §5b).

As built (plan P3-28; design T6 S14, T9 M11): `raw(html, {measure: 'host'})`
(and a handler's `ctx.raw(html, opts)`, which takes the raw constructor's
options) is a box whose height the host measures at the box's width. A block
box is measured at layout's available width: Layout files a boxInfo need
(kind `svg` for an `<svg>` root, else `html`; the markup; the width) and its
run is provisional — never painted — until the answer arrives; a relayout at
another width asks again for that width only, a fork at the same width asks
for nothing. Widths never depend on answers, so one round settles every box;
Layout asks at most twice (`box-unsettled` after that), and a box the host
cannot measure keeps its declared height (`box-measure`). An svg whose
attributes size it (a px `height`, or a `viewBox` scaled to its px or %
`width`, else the box's) is answered by the engine without a round trip. An
inline box needs a declared `w` (`raw-measure` otherwise): it is measured at
that width when its block is emitted, and sits on its measured baseline
(`vertical-align` below the line's baseline by its depth). Raw content, block
or inline, is laid out in one context: the document's font, wrapping, normal
line-height — the context the browser's `measureHtml` capability measures in
(a hidden probe in the document's typeset root). Images and raw boxes share
one size record, `IntrinsicSize {w, h, minW, scale, source: Declared |
Provided | Host | Placeholder}`. The sheets' own layout (a `media` paged
render) uses the answers it has and asks for none.

As built (plan P3-10; design T6 TableSpec, S9): a table is a box-tree
container of **Cell blocks**, one per grid position (row-major; a short row
padded with empty cells), each a **flow root**: its content is laid out by
the ordinary layouters at the column's content width — a cell of inline
content (or of inline content beside a block, as the resolver's inline
term) is one paragraph, a cell of blocks lays out its blocks (paragraphs
with their gaps, lists with markers, code blocks, formulas). Tracks are the
v1 TableSpec (`cols` equal Fr(1) columns, halign from `align`, padding
inside the cell, the 64su floor); a cell's paragraphs take their column's
halign as a LineEnds preset; the row takes its tallest cell; the document's
floats stay outside a cell. The table is one box of the vertical list and
one atomic group on paged sheets. Cell lines carry `data-track="cell"`
(P3-07), the layout dump prints `cell=<grid index>`, and the cells'
paragraphs are units of their own (their breaks dump as `unit=`).

### 6.4 Measurement states

MetricStore entries per (strRef × StyleId): `exact | pending(estimate) | invalid`. Bundled-font entries are born `exact` (precompiled metrics). A paragraph is `estimated` if any of its blocks is pending; upgrades re-run break+layout+render for exactly those paragraphs when measurements arrive.

*As built (plans P1-19, P1-20; pages-design §1 W; plan P3-37 amendment):* there are no estimate states. A MetricStore entry is present (raw px, quantized by Measure) or missing; a missing one is a need of the pull, answered or failed — a failed width is its em bound (design T9 A1), never an estimate. Fonts settle before the first Measure, so a typeset is exact or not yet done; the semantic page is the first paint. A block waits only for its own needs (per-block deferral, plan P1-20) and the document resumes at stage granularity (`stages.def`).

## 7. Measurement buffers

As built (plan P1-19; docs/host-protocol-design.md §4a): measurement is two
rows of the resource pull — `textWidth` (metric key, text → px) and
`fontVmet` (metric key → ascent, descent px) — in one binary batch with the
code tokens and image sizes. Only missing entries are requested (the JS
cache sits above; this is the engine-side dedup). The store keeps raw px;
quantization per §6.1 is the Measure stage's. The batch is
`tsr2_requests` / `tsr2_provide` (architecture §2.5); the JSON
`tsr_measure_requests` / `tsr_provide_word` / `tsr_provide_vmet` remain as
shims (checked at plan P3-37).

## 8. Layout result

As built (plan P1-18; docs/layout-design.md): layout walks the box tree
(`engine/src/boxtree/`) with one layouter per `LayouterId` and produces, per
top-level block, a frame of **fragments** in paint order and its vertical
list.

```
LayoutResult {
  docHeight : i64 su
  paras: [{                                  // one per top-level block (pid)
    pid, rect: {x,y,w,h: su},
    lines: [Fragment {                       // engine/src/layout/layout.h
      kind                 : Line | Rule | CodeRow | Raw | Math | Image
      y, left, width, height : su            // y = the top (a rule's too)
      unit, cell           : the leaf, and its other track (a cell, a caption row, a sidecar row)
      items                : [i, j)          // into the stream's HList (blocks [i, j) for dumps)
      wordDeltaPx, cjkDeltaPx : f64          // raw-px spacing (what paint copies)
      wordDeltaSu, cjkDeltaSu : i32          // rounded, for dumps/goldens
      endsWithHyphen, ragged, noGlue, overfull : bool
      join                 : space | none    // whether the break consumed a space (copy rule §9.3)
      anchor, anchor2      : the ids it carries (exactly one fragment per anchored block)
      marker               : a list marker / line number in the gutter
      srcSpan              : [s, e)
    }],
    vlist: [{unit, gap, clear, y, h, outOfFlow}]   // tsrc --stage=vlist
  }]
  breaks: [{pid, unit, cell, breakpoints, cost}]  // tsrc --stage=breaks
}
```

Leading: line advance `= max(baseLeading, ascent + descent)` where `baseLeading = round(lineHeight × fontSizePx × 64)` from the paragraph style and ascent/descent are the line's max run metrics — mixed CJK/Latin prose stays on the uniform grid (both fit under baseLeading); only oversized inline boxes (math, dropcap-adjacent) grow a line. The baseline of line i is communicated as `Fragment::baseline` (plan P1-18). As pinned in P3-19 (T7 S11) a text line's baseline is `top_i + ascent_i`, its tallest run's ascent. The render contract gives every run its face's content height as line-height: the container's per-role factors `(ascent + descent)/em` on `.tsr-r`, `.tsr-cjk`, `.tsr-code`, and an inline value for a user family. The line itself has line-height 0, so its line box is its runs' content areas, whatever the host's line-height. A code row (`tsr-row`) centres its style's extents in the row; its strut is the code face at the code size. A display formula centres its box.

The upgrade payload (v2 §9) is `paras[]` plus per-paragraph HTML.

## 9. Serializer contracts (normative — tests assert this DOM)

Class prefix `tsr-`. Both serializers escape all text (`& < > " '`); the only unescaped path is `raw.html` (trusted, handler-declared).

Both serializers build start tags through `render/html_writer.h` (plan P0-10): attribute names come from the explicit allowlist `kHtmlAttrs` (checked at compile time — an unlisted name does not build), every element has at most ONE `style` attribute (declarations merge into it), ids are spelled by `AnchorNamer` (`render.idPrefix`, default `tsr-`, + the escaped label), and a repeated attribute is a serializer defect — debug builds assert, release keeps the first value and reports `render-attr`.

### 9.1 Typeset HTML

As built (plan P1-18; docs/render-design.md): paint (`engine/src/paint/`)
turns each frame's fragments into a DisplayList block — runs formed at run
instance boundaries, typed inline payloads, every px value the HTML prints —
and the typeset writer (`render/typeset_html.cc`) is a stateless walk of it:
it reads no Config, no emit unit and no content tree. Ids are the fragments'
anchors (each anchored block's first fragment; an enclosing label sharing it
is an empty `<span id data-syn="anchor">` first inside it), so the flowing
and the paged output carry the same ids.

```html
<div class="tsr-doc" lang="zh-CN" style="--tsr-font-body:…;--tsr-font-cjk:…;--tsr-font-mono:…;--tsr-font-mono-cjk:…;font-size:18px">  <!-- P1-04: the measured fonts; text-rendering:geometricPrecision -->
  <div class="tsr-para" data-pid="7" style="position:relative;height:{h}px">
    <div class="tsr-line" data-s="120" data-e="181" data-join="space"
         style="top:{y}px;left:{x}px;width:{w}px;word-spacing:{d}px">
      <span class="tsr-r" data-s="120">real text </span>
      <span class="tsr-r tsr-cjk" data-s="131" style="letter-spacing:{c}px">中文串</span>
      <span class="tsr-r" data-syn="hyphen">-</span>
    </div>
  </div>
</div>
```

- CSS contract: `.tsr-line { position:absolute; white-space:nowrap; contain:layout style; }` — the v2 §7 rules are *serializer output*, not page-author responsibility. (No `paint` containment: list markers and line numbers render in the gutter, outside the line box, and paint containment would clip them.)
- Runs carry `data-s` when they map 1:1 to a source slice. As built (plan P4-03; design T5 per-item spans): every item has its own source span — a text's cluster through the node's cooked→raw map when the text is its own source (`ContentNode::srcExact`), else the node's span (made text: a reference's `[1]`, a caption's `图 1：`, a heading's number); a hyphen, a boundary glue and a space the parser inserted are points. A run's `data-s` is its first item that has a source (relative to the block's `data-s0`; a run of none — a hyphen, an inserted space — carries none); a line's `data-s` is its first such item and its `data-e` the furthest end, so a line's range is its own, not its paragraph's. `tools/check-spans.mjs` (gate G1) checks every html golden: a content run's `data-s` is its first character's byte (or its escape's backslash, or the markup made text comes from), runs are in source order on their line, and a stream's lines are in source order. As built (plan P3-07, D-R01): what copy takes of a run is decided at emit and encoded here — a text run (authored text, and class-body generated text: references, citations, caption prefixes, shown numbers) carries no `data-syn`; an omitted run carries `data-syn="<kind>"` (`hyphen`, `marker` (a gutter marker or line number), `cont`, `indent`, `boundary`, `fill`, `eqno`, `image`, `raw`, `fn-marker`, `backlink`, `error`, or a node's own `syn`); a replaced one also `data-copy="<text>"` and `data-copy-group` (taken once per group in a block); `data-syn="math"` keeps `data-src` as its replacement. Nodes say it with the universal `copy` (`text` | `omit` | `replace:<text>`) and `syn` attributes; the built-in templates mark the footnote marker and the notes' backlink.
- Lines (plan P3-07): `data-join` is the line's separator — `space`, `none` (a break inside a stream), `tab` (after a table cell), `row` (after a row's last cell), `para` (after a unit the semantic page sets apart: a paragraph, heading, code block, table or formula not in a tight list item); absent: a line boundary (a forced break, a code line's end, a block's last line). `data-ragged="1"` marks every line its stream does not justify (centred, ragged, cell-aligned lines, code rows); `data-track` names a second track's line — `cell`, `sidecar` (a code block's notes) or `caption` (a float's caption rows) — and replaces `data-cell`. Code rows carry `data-s`/`data-e`: their slice of an exact source line, else the line (or body) as a whole. An empty table cell is an empty line carrying its separator. Paged sheets wrap each block's bands in `<div class="tsr-band" data-b="{pid}">`, one per block per sheet.
- (plan P4-04) With the host setting `render.runWidths`, every run carries `data-w`: its width as the engine set it — its items' raw advances, the line's word-spacing on its spaces, its letter-spacing and margin, a punctuation glyph's own blank when it stands, a pinned width. `test/e2e/punct.spec.mjs` (the punctuation matrix) compares the rendered width with it at every device pixel ratio.
- A line holding a run wider than the measure carries `data-overfull="1"` (plan P0-12): it is set at the shrink limit and overflows on purpose; the audit skips its right edge and the paragraph's overflow check.
- Run boundary (as built, plan P4-01: the HList's run instances, shaping-design §5): style, link, the generated-reference kind, the copy policy and the realize class — a resolved ref's text (`[1]`, `??`) is never merged with the authored prose or punctuation beside it; an inline anchor (a footnote marker's id) opens its run and is written once. A line-final hyphen opens inside its word's link (`<a … data-syn="hyphen">-</a>`). A Rigid run (inline code) whose text has a word separator carries `word-spacing:0` on a justified line: its spaces are measured as written.
- `comment` nodes are not rendered here; error text is a run of `data-syn="error"` (the semantic page: `<span|div class="tsr-err" title="{message}" data-syn="error">`).

### 9.2 Semantic HTML

Element mapping (plan P3-23; the **presentation map**, `engine/data/elements.json` `html`): a node shows as its row's element. A node's row is its class's (or its `like:` base's), else its role's, else its kind's. The built-in rows:
- kinds: `para→p`, `heading→h{level}`, `list→ul|ol` (the list projection), `terms→dl` (plan P3-34: each item's term part in `dt`, its other blocks in `dd`, a sole paragraph inline), `item→li`, `quote→blockquote`, `codeblock→pre>code` (the codeblock projection), `rule→hr`, `mathblock` (the math projection), `table→table/tr/td` (the table projection), `group→div[data-role]`;
- classes: `figure→figure` (its caption part in `figcaption`), a defined `term→dl>dt+dd` (the term projection: the name in `dt`, the description and the blocks after it in `dd`);
- collectors: `toc`, `lof`, `lot`, `index→nav`; `glossary`, `notes`, `bibliography→section`, each with its DPUB-ARIA role. A collector's output stands in a group of its role (its name), so a document's collector shows as `div[data-role=<name>]`;
- generated roles, inline: `fn-marker→sup`, `caption-label`, `term-name→strong`. A role's element is not written where the run's own style says it already.

`link/ref→a`, `code→code`, `image→img` and `raw` (passthrough) stay code.

A row gives:
- `element` (from the allowlists of `elements/presentation.h`: flow elements for blocks, phrasing ones for `inline` rows);
- `slots` (a part's element; every part of a slot in one element, where its first part stands — one paragraph holds its text, several their paragraphs);
- `aria` (an allowlisted role);
- `projection` (list, table, codeblock, math, term);
- `typeset: {dataRole, frame}`;
- `like` (another row's fields first).

The host patches rows with `semantics.html`, a document with `$.element(name, {html: {…}})`. A row naming anything outside the allowlists refuses the registry (D-R09).

`data-role` (D-R02): a group whose row hooks it (`group`, `figure`, the collectors) carries its role as `data-role`, on the semantic page and on the typeset page's `.tsr-para` (and paged `.tsr-band`). A class whose row declares `typeset.frame` gets a frame box on the typeset page, `.tsr-frame[data-role]`, which the theme draws; no built-in class declares one (D-Y11).

A code block's sidecar notes (its margin part) follow their line inside the code, behind the fence's declared marker: `<span class="tsr-margin" data-syn="sidecar">/// …</span>`. Copy omits them, so a code block copies its code (D-R03).

Runs: the semantic page writes a run's weight (`strong`, and `font-weight` for a weight but 400 and 700) and its size (`font-size`: an absolute size times its multiplier, else the multiplier in `em`) as the typeset page paints them. As built (plan P3-07, D-R06): generated text the typeset view omits or replaces says so here too — an `<a>` of a footnote marker or a backlink carries `data-syn`, error text `data-syn="error"`, a node with `copy`/`syn` attributes its `data-syn`/`data-copy` (on its element, or a `<span>`) — so one copy contract serves both phases. Paragraph-level elements carry the same `data-pid` (the upgrade swap keys on it) and `data-s/e`. No positioning, no spacing styles — the browser flows it (v2 §9).

As built (plan P3-01): runs are written in their **scope** (the rule-free style, §3); what the rules add is the page's stylesheet, `rulesToCss` (`render/rules_css.{h,cc}`; the `css` product, `tsr2_render_css`, `renderTsm(…).css`, inlined by `tools/export-static.mjs`). Every rule is `:where(…)` (specificity 0, so order decides as in the cascade); a forced host rule is `!important`; a kind maps to the element above; a document env that begins mid-document is compiled under `[data-tsr-env="<hash of its rule chain>"]`, which the page sets on the top-level blocks in that env and on a `style.where` wrapper (`div` around blocks, `span` inline). Role and class selectors wait for the page's hooks (P3-18 `tsr-c-*`, P3-23 `data-role`); the generated roles read through their inline presentation rows (above). A selector with no CSS form (depth, other attributes) is left out with `rule-no-css`.

Implementation notes (M5): the semantic serializer runs on the post-resolve
tree with no measurements — the worker posts it immediately after ingest and
the shell paints it before the pull loop starts. Upgrade = one keyed
`data-pid` swap; the shell reports `{pid, old:{top,height}, new:{top,height}}`
per paragraph to the caller (`onUpgrade`), scroll anchoring stays caller-side
(v2 §9). `relayout(widthPx)` re-breaks/lays out the worker-held doc with
persisted metrics. The `data-join` attribute reflects whether the break
consumed a REAL source space: CJK inter-character breaks and synthetic glue
(boundary, punct halves, indents) render `data-join="none"`, so the §9.3
rebuild reproduces source text exactly. It is decided by the break, not by
alignment (plan P1-17): a wrapped heading, caption or error block joins
like a paragraph. As built (plan P3-07): every line of every stream carries
layout's separator (`Sep`) — a table cell's and a sidecar row's lines join
by their breaks too, a cell ends with `tab`/`row`, a unit with its
`sepAfter`. Every stream's lines come from one `materializeLines` (layout),
so paragraphs, cells, float captions and sidecar rows share the hyphen,
height, span and spacing rules — a tight ragged line shrinks to fit, as the
breaker assumed. Deferred: `pending(estimate)` states
and webfont-settle re-typesets, `size-adjust` fallback descriptors.

### 9.3 Copy (normative for `runtime/main/copy.mjs`)

Copy produces **content text**, not markup source. As built (plan P3-07; design T7 "ContentText projection + CopyPolicy"): the engine decides what copy takes of every run and line (§9.1) and `copy.mjs` concatenates:

- Ownership: the core copy handler owns the clipboard iff the range intersects at least one `.tsr-line` (even if the result is empty); otherwise native copy applies — except on the semantic page, where a range holding `data-syn`/`data-copy` elements is copied as the browser would copy it without the omitted ones (D-R06).
- Runs, in DOM order: a run without `data-syn` contributes its selected text (partial at the selection endpoints); `data-copy` (a formula's source too, plan P3-26) contributes its replacement once per `data-copy-group` in a block; any other `data-syn` run nothing.
- Lines, in order: a line without items (a blank code row, an empty cell) contributes `""` and its separator; a line whose items are all omitted contributes nothing, not even its separator; otherwise its text, then its separator.
- Separator: `data-join` — `space` `" "`, `none` `""`, `tab` `"\t"`, `row` `"\n"`, `para` `"\n\n"`; absent `"\n"` — except between two lines of one replaced node (the same `data-copy-group` ends one and starts the next): nothing. A change of block identity — a `.tsr-para`, or on paged sheets a `.tsr-band`'s `data-b` (a block cut across sheets keeps one) — is `"\n\n"`.
- Sidecars (D-R03): across code rows only the code is copied; a selection entirely inside the sidecar track (`data-track="sidecar"`) copies the notes, without their marker.

`handle.contentText(range)` (and a behaviour's `ctx.ops.contentText`) is the same projection; `ctx.ops.contentBlocks(range)` gives it one block at a time (plan P3-27), the blocks it joins with `"\n\n"`. Source offsets (`data-s`) are for anchoring and diagnostics, never for copy.

Text layer (plan P3-27; setting `a11y.textLayer`, off by default): the `textLayer` behaviour marks the commit root `aria-hidden="true"` and keeps a visually hidden `<div class="tsr-sr">` in the session overlay holding one `<p>` per block of `contentBlocks` over the root, rebuilt on every commit — assistive technology reads content text, not lines.

### 9.4 Math runs (M7)

- A formula renders as one `<span class="tsr-math" data-syn="math"
  data-copy="$…$" data-copy-group="…">` inline box (width/height/vertical-align from the MathBox;
  display formulas are a `Math` fragment: a centred row with an explicit
  height, the formula on the row's baseline as layout set it. Its number
  (plan P3-26) is a line of the leaf's tag track, measured and placed by
  layout: on the formula's baseline at the measure's end, or below it at the
  end when the two would come closer than an em; `data-track="tag"`, its
  runs `data-syn="eqno"`, never parted from the formula by a page cut).
  Inside: absolutely positioned `.tsr-mg` glyph runs (baseline pinned by
  line-height == hhea height) and `.tsr-mr` rule boxes. Glyphs are painted
  BY CODEPOINT in the bundled font — the artifact's gating check guarantees
  every variant/assembly glyph is cmap-reachable.
- Copy (§9.3; plan P3-26): a formula is a replaced run like any other —
  every part of a formula split at break points carries its source as
  `data-copy` and one `data-copy-group` (its source start, high bit set),
  so copy takes the source once. Audits treat a math span as one
  engine-defined line fragment.
- Label (plan P3-27, `a11y.mathLabel`, on by default): every formula part
  carries `role="math"` and `aria-label` = its source as written (D-R04).
- Rows (plan P3-29, D-S11): a formula of several rows (a line-final `\`)
  is a fragment per row, a jot apart, its number on the last; its first
  row carries the label, its later rows `aria-hidden="true"`. An
  `equations` block (display lines one after the other) is a box-tree
  container of its formulas, a jot apart, their columns shared at `&`.
  A display formula's `data-copy-group` is its own source position.
- Semantic page (§9.2; plan P3-27): `render.math: "boxes"` (the default)
  writes the same span — glyph runs, rules, `data-copy`, label — laid out
  at the page's base size, in flow (no absolute position); display inside
  `<p class="tsr-mathblock">`, which the theme centres. `"source"` writes
  `<code class="tsr-mathsrc">$src$</code>` inline, `$ src $` for display.
  `export-static` ships the math font (`@font-face` + `assets/`) when a
  formula is present. MathML stays rejected (v2 §13). (Plan P3-29) A
  formula of several rows is one `span.tsr-math.tsr-mathrows` holding its
  rows; an equations block is `div.tsr-equations` of its formulas' `p`s.

## 10. Diagnostics

As built (plan P0-11): `DiagSink` stamps every diagnostic with the pass that reported it, and `begin(origin)` drops that pass's earlier slice, so a re-run never duplicates its warnings; Emit runs per top-level block (plan P1-20) and `beginPid` replaces one block's slice when it is emitted again (a deferred display formula), the slice kept in block order. A diagnostic about a generated node without a span (a figure's image) points at its innermost spanned block.

As built (plan P3-37; design T9 M9): the products are `diags` — the text dump for humans and goldens, `<sev> <code> @[s,e) <message>` — and `diagnostics`, the same rows as JSON, `[{sev, code, span: [s, e], message, origin, pid?}]` (`tsr2_get(doc, "diagnostics")`; the worker posts it with every result, the shell's handle exposes it as `diagnostics`, and the VS Code extension reads it instead of parsing the text). The design's sketch was `{ severity: error|warning|info, code, span, message, related?: span[] }` via `tsr_diagnostics`; `related` spans are not produced. The initial code table:

```
parse-block          error    unparsable block (recovered at block boundary)
parse-inline         error    inline parse failure (block became error node)
splice-js            error    splice lexing failure (unbalanced, regex literal, …)
region-unclosed      error    #!name without matching closer
region-mismatch      error    closer name does not match innermost open region
script-error         error    document program threw (per top-level block)
script-syntax        error    a block's JavaScript does not compile
region-error         error    a region handler threw
fence-error          error    fence handler threw / ctx.error(msg, offset)
bib-load             error    a bibliography could not be loaded (warning: an entry failed to format)
splice-undefined     warning  #x where x is undefined or null: nothing rendered
splice-function      error    a function spliced as content (call it; #toc-like nullary ctors excepted)
splice-object        info     a plain object spliced as its String()
ops-invalid          error    op buffer failed validation
ref-unresolved       warning  @id with no label
label-duplicate      warning  same label declared twice (first wins)
style-underflow      warning  STYLE_POP_TO above current height (clamped)
style-in-value       warning  a $.style.push inside nested content ends with its statement (P2-12; $.set scopes, P3-01)
style-rule           warning  a malformed rule in the defaults or style.rules (dropped)
rule-no-css          warning  a rule the semantic page's stylesheet cannot express (left out, P3-01)
nest-limit           error    syntax nested deeper than 128 levels (cut, P2 end)
measure-fallback     info     glyphs measured via fallback font
```

## 11. Config (`tsr_doc_new` JSON)

As built (plan P1-03): one settings document, `tsr2_set_config(doc, json)`, whose rows are generated from the schema (`docs/settings-table.md`; paths such as `host.width`, `doc.lang`, `fonts.body`, `code.snapKerning`, `cost.exponent`, and since plan P3-35 `source.frontMatter`, the front end's host option); see `docs/host-protocol-design.md`. `tsr_doc_new` takes no JSON (plan P3-37 amendment); the document's own declarations (`$.doc`, `$.locale`, `$.set`, `$.element`) come after the host's rows. The sketch below is the original plan of the document's shape.

```json
{
  "fonts":   { "body": "...", "cjk": "...", "mono": "...", "monoCjk": "..." },
  // (no fonts.math key exists: the math font is the bundled Euler-Math,
  // metrics precompiled — docs/math-design.md §3)
  // fonts.cjk is live (M6+): CJK-class runs measure AND render with this
  // stack (renderer: .tsr-cjk { font-family: var(--tsr-cjk-font) }) so
  // U+2014/…/fullwidth puncts never resolve into the Latin face,
  "baseSizePx": 18, "lineHeight": 1.5,
  "cjkJustifyK": 0.6, "punctCompress": "book",
  "epsilon": { "perWordSu": 1 },
  "supplements": { "heading": "§", "figure": "图 ", "equation": "式 " },
  "counters": { "figure": { "resetAt": "none" } },
  "breaker": { "exponent": 3, "hyphenPenalty": 0.7, "shrinkThreshold": 0.37, "shrinkCoeff": 0.6, "cap": 1e4 }   // exponent: integer 1..4
}
```

Unknown keys → diagnostic, not error (forward compat).

As built (plan P2-07): the `semantics` section — `semantics.elements`,
`.counters`, `.collectors`, `.systems` (dom `json`: an object in
elements.json's form, kept as its JSON text) — patches the built-in
registry rows field by field before the document's own declarations
(docs/semantics-design.md §6); `affects: Ingest`. The `supplements` and
`counters` keys sketched above are these rows now.

As built (plan P3-02): every stage reads its settings through a generated
view (`settings_views.gen.h`: `IngestSettings` … `PaintSettings`) holding only the
rows whose `affects` names it, so a read of an undeclared row does not
compile and a settings patch reruns every stage that reads the row. (Plan
P3-32, design T9 M12: `Config` itself is defined only in `settings.gen.h`,
which no stage source sees — the architecture lint's `config-closure` rule —
so a stage cannot read past its view. `emit.h` asserts that neither Emit's
nor Measure's view has `host.width`: emit products are width-independent by
construction.) The
knobs with evidence of scoped use are properties, with the settings as
their document defaults through the default rules: `codeblock.snapKerning`,
`codeblock.sidecarFrac`, `codeblock.contIndent` (block rows; the code block
arguments `snapKerning`, `sidecarFrac`, `contIndent`, `features` alias them),
`text.features` (a run row: code and code blocks take `code.fontFeatures`,
code blocks of a language `code.fontFeaturesByLang` — host rules — and
measurement keys faces by it) and `text.punct` (a run row; unset: the
document's `cjk.punctCompress`). Policy literals are Doc rows
(`code.minCols`, `code.snapTolerance`, `code.snapMaxQ`, `table.cellPad`,
`table.rowPad`); safety rails are named constexprs (`support/rails.h`).

## 12. Dump formats (golden-test contract, byte-exact)

`--stage=mathbox` (M7): per-formula box trees in document order —
`display pid=N` / `inline pid=N "src"` headers, then an indented tree of
`hbox/glyph/rule/glue` lines with atom class, `w/asc/desc` in su, `@(dx,dy)`
child offsets (dy positive = up) and per-glyph `px` sizes. Only fixtures
containing math carry this golden.

- **Tree dump** — one node per line, children indented two spaces:

  ```
  para @[0,42)
    text @[0,10) str="均值是 "
    styled @[10,18) delta=+EM
      text @[11,17) str="就地"
  ```

  Strings JSON-escaped; args in fixed key order; StyleId printed as resolved `classBits+dyn+inline` (ids are not stable).
- **Ops disassembly** — `%7 = MAKE_NODE em children=[%6]`, `EMIT %8`, one op per line.
- **Block stream dump** — one block per line: `word "The" w=512su pen=INF style=LATIN @[0,3)`.
- **Layout dump** — per line: `L3 y=288su left=0 w=19200su Δw=27su Δc=16su join=space blocks=[14,22)`.
- HTML dumps are the serializer output itself, prettified by a stable formatter (one element per line).

Every stage type implements `dump()` once; `tsrc --stage=…` and the golden tests share it verbatim.
