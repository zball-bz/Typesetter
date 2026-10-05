# T4-style-settings

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

Every place-dependent decision about presentation or typography becomes a typed property. Each property is declared once as a row of the shared schema and resolved by one emission-time cascade. The engine's built-in presentation is the bottom layer of that cascade: a stylesheet written in the same vocabulary users write.

Each row fixes:
- its path;
- its value domain;
- its granularity: Run, Extent, Block or Doc;
- whether it inherits;
- its effect class.

One delta type, a patch optionally guarded by a node-local selector, reaches the cascade through four carriers: the schedule stack, a node's own `style` attribute, host rules and the engine's default stylesheet. Document-wide values travel on the declaration channel and never on the stack.

For every node the cascade computes a full CascadeState (Styling for runs, NodeProps for blocks and extents, and the rule environment) plus a rule-free scope projection. Its computed-value semantics are CSS-like:
- one winning op per property;
- em resolved against the parent;
- flags folded in order.

Two entry points cover every node made after instantiation. Cascade.make creates nodes for the resolver, sidecars, token folding, fragments and math text. Cascade.lift re-homes lifted content. User and built-in content are therefore styled by the same rules.

Downstream layers read only these records and their projections:
- FaceKey, the v2 §6 measurement tuple, which is the only source of metric CSS;
- the generated CSS: contract classes for the typeset DOM, and compiled rules for the flow page.
They never read Config scalars, kind switches, class names or colour strings.

The effect class turns v2 §7 into a checked rule:
- only MEASURE rows enter FaceKey;
- PAINT rows map only to allow-listed non-geometric CSS;
- RENDER rows map only to generated contract classes.

## Diagnosis

Styling, configuration and presentation share no vocabulary, so each feature invented its own channel.

(1) `Styling` (engine/src/model/model.h:29-40) is hand-written. Its u64 bits mix four unrelated things:
- author emphasis (BOLD/EM/U/O/S);
- emit's internal script class (CJK);
- kind markers (CODE, LINK);
- a feature hook (SUP).
Its paint values are unvalidated CSS strings, which causes two leaks:
- semantics travel through paint: comment detection compares a colour string (emit.cc:618-622);
- paint reaches geometry: a value like `red;letter-spacing:5px` lands in style="".
Each property is spelled out at about 10 sites in two languages, and the derived values disagree: fontPx ignores sizePx (emit.cc:251 vs measure.h:66).

(2) Presentation is code, and it runs after every user mechanism:
- emit OR-composes heading bold and size, the code scale and the link bit (emit.cc:52-63, 471-551);
- the resolver writes `Styling{CLS_BOLD,1.0f}` literals (resolve.cc:177, 452) and rescales notes by ×0.85 (:352-357).
Deltas only OR (model.cc:17), so no surface can undo a default.

(3) Four stages create nodes after instantiation, each with its own style fold:
- the resolver (resolve.cc:79-90);
- sidecar extraction (doc.h:98-131);
- inline fragments (fragment.cc:32-48);
- token folding inside the pull loop (tokens.cc:41-51, doc.h:236-242).

(4) There are two scoping worlds. Styling has an emission-time fold, but about 34 typographic knobs are global Config fields (config.h:23-71) that emit, layout and render read directly. The only bridge is a four-key list in `__region` (executor.mjs:131-137), and it hijacks handler arguments.

(5) No one owns the measure/render contract:
- describeStyle maps bits to fonts (measure.h:60-71);
- the shell CSS maps classes to fonts with the opposite precedence (shell.mjs:26-29);
- metrics are keyed by StyleId (measure.h:21), not by the v2 §6 tuple.

(6) Configuration is not a document:
- per-knob C exports are re-enumerated in the worker and the shell;
- fixture settings are encoded in filenames (tests.cc:416-426);
- defaults drift between copies;
- locale is a two-branch switch (config.h:89-102).

The docs already specify most of the missing pieces: dynClasses (document-model.md:80), set-rule sugar over the stack (v2 §12 l.252), block-boundary stack snapshots (document-model.md:87) and a JSON config (document-model.md:310-327). None of them was built.

## Abstractions

### PropRegistry (the props section of the shared schema)

**owner_layer**

Cross-cutting.
- The rows live in engine/src/schema/props.def, a section of T2's shared schema source.
- One generator (T2's tools/gen-schema.mjs) emits engine/src/style/props.gen.h, runtime/src/shared/props.gen.mjs, contract.gen.css, docs/props.md and VOCAB_HASH.
- T4 owns the property rows and their Gran/Inherits/Effects/Css/Dump columns.
- T2 owns the file format, kinds, attributes and codecs.

**purpose**

The single closed vocabulary of engine-understood properties, each declared once.

Every value kind gets a total validator, generated twice:
- in C++, which is authoritative and runs in the fuzzable reader and in instantiate;
- in JS, for early diagnostics.

Settable element attributes do not form a second vocabulary. A kind-attribute row can alias a property path (codeblock `snapKerning` = `codeblock.snapKerning`), so each knob has one name, one domain and one validator.

**definition**

```
Row format:
PROP(Id, path, Gran, Kind, Domain, Inherits, Effects, Initial, Css, Dump)

Granularity:
- Run: character properties. They inherit down to text leaves and become run styles (Styling).
- Extent: non-inheriting. They apply to one node's inline extent and are read at its boundaries (NodeProps).
- Block: properties of block nodes and containers (NodeProps). How they are consumed is defined in Cascade.
- Doc: one value per document (DocProps). Only host config or document declarations can set them.

Run rows (all inherit):
- text.font: FontList, MEASURE. Explicit family override.
- text.fontRole: Ident ∈ declared fontRoles, MEASURE, initial body.
- text.weight: 100..900, MEASURE, initial 400. The `bold` sugar means 700.
- text.italic: Bool, MEASURE.
- text.size: FontSize{absPx f32 (0 = unset), mul f32}, MEASURE.
  - '0.7em' multiplies the parent's mul.
  - '22px' sets absPx and resets mul to 1 (CSS 'absolute replaces').
  - `sizePx` remains an alias.
- text.caps: normal|small, SHAPE. Synthetic small caps by T5; FaceKey.caps is reserved.
- text.lang: LangTag, RESOLVE|SHAPE|PAINT, initial = doc.lang.
- text.color: Color, PAINT, 'color:%s'.
- text.decoration: FlagSet{under,over,strike}, PAINT, 'text-decoration-line:%s'.
- text.emphasisMark: auto|dot|sesame|none, PAINT. Painted on italic CJK runs; `auto` comes from the LocalePack.
- text.baseline: Shift super|sub|<em>, PAINT, additive. The computed px is the parent value plus shift × the node's own emPx.
  - A single keyword step renders as class tsr-sup / tsr-sub.
  - Anything else renders as 'position:relative;top:%gpx'.
- text.space: collapse|preserve, SHAPE|RENDER. `preserve` keeps literal spaces with no break opportunities and renders class tsr-pre.
- text.punct: auto|full|book|none, SHAPE. T5 owns the cross-run pair rule.
- code.hang: indent|content, LAYOUT.

Extent row:
- text.attach: none|prev|next|both, BREAK. No break opportunity before the extent's first item, or after its last.

Block rows:
- par.indent: Length, Y, SHAPE. First-line indent; still 0 for a unit carrying a marker, as today.
- par.align: justify|start|center|end, Y, BREAK|LAYOUT.
- par.hyphenate: auto|true|false, Y, SHAPE.
- block.gap: Fraction(of doc.parGap) | Length, Y, LAYOUT, initial 1/1.
- block.spaceBefore and block.spaceAfter: Glue{natural,stretch,shrink}, N, LAYOUT, initial 0.
- block.indent: Length, N, LAYOUT. The container inset.
- block.keepWithNext: Bool, N, PAGINATE.
- list.marker: T3 numbering pattern, Y, RESOLVE, initial '•' / '{n}.'.
- codeblock.snapKerning: Bool, Y, SHAPE|RENDER.
- codeblock.fontFeatures: FeatureList, Y, RENDER.
- codeblock.sidecarFrac: 0.1..0.9, Y, LAYOUT.
- codeblock.contIndent: Int ch, Y, LAYOUT.
- box.* rows (padding, rule, background) arrive with T6's container layouter and use the same carriers.

Doc rows:
- doc.lang: LangTag, initial zh-CN.
- doc.title: Str, metadata.
- doc.baseSize: Px 4..96, initial 18.
- doc.parGap: Length, 1.2em.
- doc.leading: 0.5..4, initial 1.5.
- doc.cjkJustify: 0..4, initial 0.6.
- doc.cjkGlue: em, initial 0.1.
- fontRoles: Map<role,{latin,cjk}>.
- break.{hyphenPenalty, urlPenalty, urlMinLen, mathRelAfter, mathRelBefore, mathBinAfter}.
- cost.{exponent, shrinkThreshold, shrinkCoeff}.
- codeblock.{minCols, snapTolerance, snapMaxQ}.
- table.{cellPad, rowPad}.
A Doc row is promoted to Block only when a fixture or corpus case needs scoped values; the KP cache is shared across documents (doc.h:242-246).

Units:
- Run lengths in em resolve against the node's own computed size, as in CSS.
- Block and Doc lengths in em resolve against doc.baseSize (rem-like). Every Config length does this today: emit.cc:478, 514 and 541; layout.cc:13-14.
- Each length is rounded to su where it is consumed.

Numeric representation (I1):
- Size multipliers are float32, multiplied parent-first, exactly like today's compose and rescale.
- emPx and FaceKey.sizePx = (double)(absPx, or doc.baseSize if unset) × (double)mul.
- A parsed decimal is rounded to float32 once.
- -0 becomes +0; NaN and Inf are rejected.

Value kinds:
- Color: #hex{3,4,6,8}, numeric rgb()/hsl(), a named colour, currentColor, or var(--[A-Za-z0-9_-]+).
- FontList: quoted strings or identifier sequences, with `;{}()` rejected. Canonicalisation is the identity on input that is already canonical.
- LangTag: well-formed BCP-47, in canonical case.
- Ident: [A-Za-z_][A-Za-z0-9_-]*.
- FeatureList: quoted 4-character tags, each with an optional integer.
- Number/Length: finite and range-checked.
An invalid value raises `style-value` (with its span) and drops that one property.

Effects:
- Contract-bearing, enforced by the generator:
  - MEASURE rows have no Css and must be FaceKey inputs.
  - PAINT templates must be in the non-geometric allow-list {color, text-decoration-*, text-emphasis-*, background-color, position:relative+top px}.
  - RENDER rows map only to generated contract classes.
- Advisory only (documentation plus a 'row has a consumer' check; not exported as an ABI): RESOLVE, SHAPE, BREAK, LAYOUT, PAGINATE.
- The Css of Block rows (text-indent, text-align, margin-block, padding-inline-start) is used only by the rule→CSS compiler for the flow page, never by the typeset renderer.

Generated outputs:
- struct Styling: one canonical field per Run row, plus a ClassSetId.
- struct NodeProps: Extent and Block rows, plus a block ClassSetId.
- struct DocProps.
- == and Hash over the same canonical bits.
- applyOp.
- Per-dump token writers:
  - the tree dump keeps its order LATIN CJK EM BOLD CODE LINK U O S and adds SUP;
  - the block dump keeps BOLD EM CODE LINK;
  - CODE prints for fontRole mono, LINK prints from linkUrl, and SUP prints when baseline < 0;
  - styled-node arguments print with the surface names font=, lang=, color=, sizePx=.
- JSON section codecs.
- JS PROPS, SHORTHANDS and parsePatch.
- contract.gen.css.
- render/style_css.h runAttrs.
- VOCAB_HASH over every schema row.
```

**surface**

The same keys are accepted everywhere: `#style({...})`, `$.style.push({...})`, `$.set(sel, {...})`, the `style:` attribute on regions, fences and constructors, and host JSON.

Top-level keys:
- bold, italic, weight, font, fontRole;
- size ('0.7em' | 22 | '22px'), with sizePx kept as an alias;
- caps, color, lang;
- underline, overline, strike, emphasisMark;
- baseline ('super' | 'sub' | '0.3em');
- space, punct, attach, class.

Namespaced keys:
- par:{indent, align, hyphenate}
- block:{gap, spaceBefore, spaceAfter, indent, keepWithNext}
- code:{hang}
- codeblock:{…}
- list:{marker}

Example: `#style({size:'0.8em', baseline:'super', attach:'prev'})[†]`

**replaces**

- engine/src/model/model.h:8-40 (frozen CLS bits and fixed Styling fields)
- engine/src/model/model.h:58-70 (hand-written Hash, inconsistent with float ==)
- engine/src/model/model.cc:14-34 (applyPatch switch)
- engine/src/model/model.cc:109-145 (styleStr, which omits SUP)
- engine/src/emit/emit.cc:1083-1100 (block dump style tokens)
- runtime/src/worker/executor.mjs:6-11 (styleBits), :225-229 and :278-280 (three key whitelists)
- engine/src/render/typeset_html.cc:33-72 (runClasses/styleInto)
- engine/src/render/semantic_html.cc:62-111 (second style mapping)
- engine/src/api/config.h:23-71 (knob fields), :76-80 (constexprs), :82-84 (headingSizeMul)
- engine/src/ops/ops.def:74-75 (ARGK lang overloaded for the code language and the BCP-47 tag)

### ScopeDelta, Rules and the declaration channel

**owner_layer**

L2→L3.
- engine/src/style/delta.{h,cc}.
- The JS surface is in T2's generated stdlib (runtime/src/worker/executor.mjs).
- The DECLARE entry belongs to T2's declarations registry.

**purpose**

One data type for every scoped change to style or settings: a property patch, optionally guarded by a node-local selector.
- An unconditional delta styles its scope.
- A conditional delta is a rule, applied to matching nodes within its scope.

Built-in presentation is data in the same format. Document-wide values are declarations, not stack entries.

**definition**

```
Data types:
- struct PatchOp { PropId prop; enum Op { Set, Initial, AddFlags, ClearFlags } op; Value v; }
- struct Patch { SmallVec<PatchOp,4> ops; }
  - It is sorted by PropId after a duplicate check.
  - Two ops on one PropId in one patch (for example bold + weight) raise `style-conflict`, and the first in wire order is kept. C++ and JS therefore agree without relying on sort stability.
- struct Selector { Kind kind=any; StrRef role=0; StrRef cls=0; SmallVec<pair<AttrKey,ArgVal>,2> where; StrRef textLang=0; u8 depth=0 /*0 = any*/; }
  - kind, role, cls and where match the node's own attributes as present at instantiation. Layering rule: selectors never see L4 decisions.
  - textLang matches, by BCP-47 prefix, the node's language as given by inheritance and its own delta. Rules never feed selectors, so there are no cycles.
  - depth is the number of ancestors of the same kind, plus one.
  - There are no combinators.
- struct Rule { Selector sel; Patch patch; }
  - A conditional patch may not contain `class` or Doc rows; either raises `style-scope`.
- DeltaId and RuleId are interned.
- RuleEnv { RuleEnvId parent; RuleId rule; u32 seq; } is an interned persistent list.
  - Env 0 holds the engine defaults (engine/src/style/defaults.json).
  - Env 1 holds the host `rules`.

Carriers:
(a) Schedule stack: STYLE_PUSH <id of a childless `styled` node>.
- An unconditional delta folds once into the parent state of every later EMIT root.
- A node with `match.*` is a rule; it extends the env of later EMITs.
- STYLE_POP_TO pops both kinds alike.
- The bits varint, the patch-count byte and the 16-patch cap are gone.
- A non-inheriting row in an unconditional push raises `style-scope`.
(b) A node's own delta: the universal `style` attribute (T2).
- `styled` stays as the pure carrier kind for `#style(...)[…]`, strong/emph and `style.where(sel, patch, …kids)`.
- A `style.where` rule extends the env of its own subtree.
(c) The region/fence `style:` meta-arg, which the default pipeline strips from the handler's arguments.
- If the handler returns one node, the patch merges into that node's own delta.
- Otherwise the result is wrapped in a `styled` node.
(d) Host config `rules: [[selector, patch], …]` → env 1.
(e) Engine defaults → env 0:
  - [{kind:'heading'}, {weight:700, par:{align:'start', hyphenate:false}, block:{keepWithNext:true}}]
  - [{kind:'heading', level:1}, {size:'1.6em'}], with 1.35 and 1.15 for levels 2 and 3
  - [{kind:'code'}, {fontRole:'mono', size:'0.85em', space:'preserve'}]
  - [{kind:'codeblock'}, {fontRole:'mono', size:'0.85em', space:'preserve'}]
  - [{kind:'error'}, {fontRole:'mono'}]
  - [{role:'fn-marker'}, {baseline:'super', size:'0.7em', attach:'prev'}]
  - [{role:'note-body'}, {size:'0.85em'}]
  - [{role:'caption-label'}, {weight:700}]
  - [{role:'term-name'}, {weight:700}]
  - [{class:'tok-comment'}, {italic:true, code:{hang:'content'}}]
  - [{role:'caption'}, {par:{align:'center', hyphenate:false, indent:0}}]
  - [{kind:'list'}, {block:{indent:'1.5em', gap:'1/3'}}]
  - [{kind:'quote'}, {block:{indent:'1em'}}]
(f) Declarations, through T2's declaration channel: a DECLARE schedule entry that references a childless `decl` node.
- `$.doc(patch)` sets Doc rows.
- `$.locale(tag, pack)` adds pack data.
- Declarations are not stack entries: `$.style.height` does not count them and they cannot be popped.
- Declarations that feed the root cascade (doc.*, fontRoles) must come before the first EMIT. Otherwise they raise `config-scope` (error) and are ignored.
- Every other declaration applies to the whole document, wherever it appears.

Precedence at a node, lowest first:
1. Inherited computed values. For an EMIT root these are Cascade.root() ∘ the unconditional stack deltas at that EMIT.
2. The rules of the node's env, in sequence order: env 0 < env 1 < stack `$.set` rules in push order < `style.where` rules from outer to inner.
3. The node's own delta: settable element arguments first, then the `style:` meta-arg. The reader folds both into one DeltaId.

Computed-value semantics, per node and per property:
- Exactly one Set/Initial op wins: the last one in the order above.
- Relative values (em sizes and em shifts) resolve against the parent's computed value. A user rule `{kind:'heading', level:1} → 2em` therefore gives 2em, never 1.6 × 2.
- Size resolves before the node's other em-relative rows, so baseline uses the node's own computed size.
- FlagSet rows fold AddFlags/ClearFlags in order over the parent's value. Underline from one rule plus strike from another gives both.
- A Block row on a node whose T2 level is inline raises `prop-scope` and is ignored.

Execution containment (I7), using T2's `__block`:
- Content blocks: the value of a top-level block, plus splice and handler evaluation.
  - `$.style.*` and `$.set` raise `style-in-value` and do nothing.
  - Values carry their scoping structurally, with `style` and `style.where`.
- Statement blocks: `#{…}` and `#let`.
  - Style operations are allowed and persist after a normal exit (patch.tsm:1-6).
  - On a throw, the stack is popped to the statement's entry height.
  - T2 must not wrap statements in a function scope that hides `const`/`let`.
- The JS popTo clamps and raises `style-underflow`.
- A numeric push raises `style-raw-bits` (error) and is ignored.
- Deferred emissions (finishBibliographies, executor.mjs:242-266) record the stack snapshot (the delta node ids) when they are requested. After the program they replay it: popTo(0), push each entry, EMIT, popTo(0).
```

**surface**

- `style(patch, ...kids)`
- `style.where({kind:'para'}, {par:{indent:0}}, ...kids)`
- `sup(...kids)` and `sub(...kids)`, prelude sugar for `style({baseline:'super'|'sub', size:'0.7em'}, ...)`
- `#{ $.style.push({font:'"LXGW WenKai"', lang:'zh-TW'}) }` … `#{ $.style.popTo(h) }`
- `#{ $.set('heading', {weight:400}) }`
- `$.set({kind:'heading', level:2}, {size:'1.2em'})`
- `$.set({role:'theorem'}, {italic:true})`
- `$.set({class:'warn'}, {color:'#b00'})`
- `$.set({kind:'para', textLang:'zh'}, {par:{indent:'2em'}})`
- `$.set({kind:'list', depth:2}, {list:{marker:'◦'}})`
- `#!aside(style:{font:'…', lang:'zh-TW'}) … #aside!`
- `#{ $.doc({lang:'en', title:'…'}) }`

**replaces**

- engine/src/ops/ops.cc:158-170 (STYLE_PUSH codec: bits varint, npatch byte ≤16)
- runtime/src/shared/opbuf.mjs:78-90 (second patch writer)
- runtime/src/worker/executor.mjs:131-137 (four-key region style lift)
- runtime/src/worker/executor.mjs:274-284 (raw-number push, unclamped popTo)
- runtime/src/worker/executor.mjs:242-266 (bibliography bound to the end-of-program stack)
- engine/src/model/model.cc:60-92 (schedule refold)
- engine/src/emit/emit.cc:47-105 (CLS_LINK/CLS_CODE/codeScale/CLS_SUP attach/error presentation)
- engine/src/emit/emit.cc:471-505, 514, 541, 551 (figDepth caption mode, heading bold, size and ragged, list/quote indent, code style)
- engine/src/resolve/resolve.cc:173-177, 342-357, 371-386, 450-452 (Styling literals, ×0.7, ×0.85 rescale)
- engine/src/api/config.h:82-84 (headingSizeMul)
- engine/src/render/typeset_html.cc:704 (keep-with-next keyed on Kind::heading at top level)

### Cascade (one fold, one factory, one lift)

**owner_layer**

L3. engine/src/model/cascade.{h,cc}.

Used by instantiate and by every node fabricator that runs later: the resolver, sidecar extraction, fragments, token folding, math text and T2's level normalisation.

**purpose**

Computes, at emission time, each node's full CascadeState (run record, node record and rule environment) and its rule-free scope projection.

It is the only way to create a styled node. Fabricated and lifted content therefore gets the same treatment as authored content.

**definition**

```
State:
- struct CascadeState { StyleId style; PropsId props; RuleEnvId env; StyleId scope; }
- ContentNode stores it: +12 bytes over today's StyleId.
- `scope` folds only unconditional deltas: the stack at EMIT plus own deltas. It equals today's instantiate-time style, and it feeds the flow-page serializer and lifting.

API:
- CascadeState root(): registry initial ∘ host config ∘ pre-EMIT declarations, with env 1.
- CascadeState emitParent(const Stack&): root ∘ the unconditional stack deltas, with the env extended by the stack's rules.
- CascadeState enter(const CascadeState& parent, const NodeView& n): memoised.
- ContentNode* make(const CascadeState& parent, Kind, Attrs{role, class, where-attrs}, DeltaId own=0): the mandatory factory for every node created after instantiation.
- ContentNode* lift(Span<ContentNode*> moved, const ContentNode& site, Attrs wrapper, const CascadeState& destParent): re-homes flow content (T3 flows; footnotes today).
- CascadeState endState(): the stack state after the last schedule op.

What enter() does:
1. Inheriting rows start from the parent's values; the others start from initial.
2. It applies the matching rules of n's env, then n's own delta, with ScopeDelta's computed-value semantics. Kind-indexed rule buckets are merged by sequence number.
3. Classes:
  - if n is inline, Styling.classes = parent.classes ∪ n.class;
  - if n is block-level, NodeProps.classes = n.class.
4. scope = parent.scope ∘ n's own delta.
5. It interns the records. n's env is the parent env, extended by n's `style.where` rule.

The memo key is exact, with a full equality check:
(parent CascadeState, env, kind, role, own class set, values of the attributes referenced by env's where-clauses, textLang input, depth when referenced, ownDeltaId)
Settable element arguments are already inside ownDeltaId.

Fabricators and their anchor states:
- Resolver:
  - caption label: make(caption para, text, role 'caption-label');
  - term name: role 'term-name';
  - footnote marker: make(state of the note's parent, ref, role 'fn-marker'), with its display text made via make(marker, text);
  - TOC, glossary and notes lists: make(state of the collect node's parent);
  - notes appended without #notes(): endState().
- extractSidecars: the sidecar group is made under the codeblock's parent state (the prose context), not under the code body (doc.h:98, 131).
- Fragment conv: leaves carry own deltas such as {weight:700} or {italic:true}. Its bit fold (fragment.cc:32-48) is deleted.
- foldTokens: make(codeblock state, text, class tok-<tag>) for each token.
- Math text: faceOf(state, Script::MathText) instead of a fabricated Styling (math.cc:582-583).

lift(moved, site, wrapper, destParent). The principle: run properties travel with the content, block properties belong to the destination.
- The wrapper is an inline `styled` node, for example role 'note-body'. It is entered with the parent {style: site.scope, scope: site.scope, props: destParent.props, env: site.env}.
- The moved nodes are re-entered under the wrapper. They keep their own deltas, and rules are re-evaluated in site.env.
- Rules that matched site ancestors (a heading, a theorem) are not carried.
- The notes para and its list marker are made under destParent.

This reproduces today's rescale bit for bit (resolve.cc:352-357, 377-383):
- note text = site scope ×0.85f;
- inline code = 0.85f×0.85f;
- list markers stay at base size.
I verified this against a probe of a footnote in a heading under a red stack: today the body is [x0.85 color=red] and not bold.

How Block rows are consumed. T6 owns the layouter; emit applies these rules until T6 lands.
- Unit inset = the sum, over the unit and its ancestors, of su(block.indent), each rounded separately. This equals today's indent + suRoundPx(listIndentEm×base).
- Gap above a unit B that follows unit A = su(block.gap of their nearest common ancestor).
  - A Fraction is floor(su(doc.parGap) × n / d). So 1/3 gives 1229/3 = 409su at 16px, exactly today's paraGap/3.
  - Add the spaceAfter of containers closed after A and the spaceBefore of containers opened before B. T6 decides collapsing.
  - The nearest-common-container rule reproduces tightAbove (emit.cc:535-537) for nested lists and for quotes inside lists.
- A container's keepWithNext applies to its last unit.
- par.* rows are read from the unit's own node; text.* rows from the leaves.
```

**surface**

None directly. The observable semantics are CSS-like:
- inherited values < matching rules < the node's own style;
- per property, the nearest delta wins;
- em is relative to the parent, and px replaces.

**replaces**

- engine/src/model/model.cc:36-54 (Inst::copy style fold)
- engine/src/emit/emit.cc:26-41 (compose, ICtx addBits/mul)
- engine/src/emit/emit.cc:514, 535-537, 541 (inset and tightAbove computation)
- engine/src/layout/layout.cc:28 and engine/src/api/doc.h:282 (duplicated paraGap/3)
- engine/src/resolve/resolve.cc:79-90 (mkNode style=0), :296-335 (TOC and glossary at style 0), :352-357 and :371-386 (rescale)
- engine/src/api/doc.h:98-131 (sidecar parsed in the body-text style)
- engine/src/inline/fragment.cc:32-48 (third bit fold)
- engine/src/code/tokens.cc:41-51 (token style copied, `tag == 3` italic)
- engine/src/math/math.cc:582-583 (fabricated Styling{sizeMul})

### ClassChannel

**owner_layer**

L3 attribute and records. Placement in the DOM belongs to T7. The `class` attribute is universal, from T2's schema.

**purpose**

Implements document-model §3 dynClasses as an open, metric-neutral channel. Classes serve as selector targets and as theme hooks.

The engine never keys behaviour on a class name. Parts the engine fabricates carry roles (T3); classes are for users, themes and token categories.

**definition**

```
- `class`: a string or string[], each a validated CSS identifier, allowed on any node.
- Inline nodes: Styling.classes (a ClassSetId) is the union down the inline subtree. It renders on typeset runs as `tsr-c-<name>`.
- Block nodes: NodeProps.classes holds the node's own classes only, not inherited. T7 places them on the unit or box element rather than on N runs (v2 §8 DOM weight).
- Rendered class order is by string value, never by intern id, so token arrival order in provideTokens cannot change bytes.
- `class` is not allowed in conditional patches: rules set properties, never membership.
- foldTokens sets class `tok-<tag>`, with names from T9's tokens.def. Colours come from theme CSS, e.g. `.tsr-c-tok-keyword{color:var(--tsr-tok-keyword)}`.
- FlowUnit::CodeRun.isComment becomes `Hang hang`, read from code.hang.
```

**surface**

- `#style({class:'warn'})[…]`
- `#!aside(class:'note')`
- `style({class:'tok-comment'}, …)` in fence handlers
- `$.set({class:'warn'}, {weight:700})`
- page CSS: `.tsr-c-warn{color:var(--warn)}`

**replaces**

- engine/src/code/tokens.cc:41-51 (token class as a colour string; `tag == 3` italic)
- engine/src/emit/emit.cc:618-624 (colour-string compare → isComment)
- engine/src/emit/emit.h:79, engine/src/layout/layout.cc:180 (isComment)
- engine/src/model/model.h:14 (CLS_LINK: dumped, never rendered, but splits metric entries)
- test/fixtures/code/hang.tsm:3 (forged comment colour)

### Faces (measurement tuple, font roles, single em accessor)

**owner_layer**

L5/L6 boundary. engine/src/measure/face.{h,cc}; MetricStore in measure.h.

**purpose**

Makes the v2 §6 tuple the only key for measurement, and the only source of metric-bearing CSS. Every em-derived quantity gets one accessor, and font selection by role and script is resolved in one place with an explicit order.

**definition**

```
Types:
- enum class Script : u8 { Latin, Cjk, MathText }. Classification belongs to T5.
- struct FaceKey { StrRef family /*resolved list*/; double sizePx; u16 weight; u8 italic; u8 caps=0; }, with features and lang reserved.
- class FaceTable { FaceId idOf(const FaceKey&); const FaceKey& get(FaceId) const; }

Accessors:
- FaceId faceOf(StyleId, Script), memoised per pair.
- double emPx(StyleId) = (double)(absPx > 0 ? absPx : doc.baseSize) × (double)mul. This is today's describeStyle formula (measure.h:66-67), now used everywhere.

Family resolution:
- text.font if set. Otherwise:
- Latin: fontRoles[role].latin → fontRoles.body.latin.
- Cjk: fontRoles[role].cjk → fontRoles.body.cjk → fontRoles[role].latin. CJK-class glyphs never fall into a Latin face (config.h:24-29). There is no mono special case: hosts with CJK-capable mono fonts declare fontRoles.mono.cjk.
- MathText: body.latin. The math font itself belongs to T8.
- For Script::Cjk, italic maps to an upright face (v2 §14 l.272); text.emphasisMark paints the emphasis.

Measurement:
- MetricStore::key(StrRef s, FaceId f) = (u64)s<<32 | f.
- vmet is stored per FaceId.
- LinebreakBlock and CodeRun gain `FaceId face`.
- resolveWidths and tsr_measure_requests key on FaceId. The JSON shape is unchanged; the ids are opaque.
- Document-declared families are requested through T9's resource protocol (font faces) before measurement.

Rendering. Metric CSS comes only from the FaceKey:
- weight and italic classes;
- one face class per run, keeping the legacy names: body×cjk = tsr-cjk, mono×latin = tsr-code, mono×cjk = `.tsr-code.tsr-cjk`, an explicit compound rule;
- non-built-in roles get inline `font-family:var(--tsr-font-<role>[-cjk])`;
- inline font-size when it differs from the root;
- an explicit family only when text.font is set.

`.tsr-doc` carries the variables for every declared role, fully resolved engine-side, plus doc.baseSize and doc.lang.

`lang` is not in FaceKey. The residual 'locl' and generic-fallback exposure that document-model.md:82 records remains, and is documented as such.
```

**surface**

Declared in host JSON or with `$.doc({fontRoles:{…}})`:
`fontRoles:{body:{latin:'"Crimson Text", Georgia, serif', cjk:'"Noto Serif CJK SC", …'}, mono:{latin:'"Sarasa Mono SC"', cjk:'"Sarasa Mono SC"'}, kai:{latin:'"Crimson Text"', cjk:'"LXGW WenKai"'}}`

Then `#style({fontRole:'kai'})[…]`.

**replaces**

- engine/src/measure/measure.h:54-71 (describeStyle: bits→family, CODE beats CJK while CSS paints the reverse)
- engine/src/measure/measure.h:19-41 (keyed by StyleId, <<24)
- engine/src/emit/emit.cc:251, 274-275, 289, 478, 958 (em from cfg.baseSizePx×sizeMul, ignoring sizePx)
- engine/src/math/math.h:44 (docBasePx)
- engine/src/api/wasm_api.cc:98-125 (requests grouped by StyleId)
- runtime/src/main/shell.mjs:8-12, 26-29, 43, 349-351 (TSR_CJK_FONT copy; .tsr-code monospace ≠ cfg.monoFont; .tsr-cjk.tsr-i measured italic but painted upright)

### CssEmitters (contract CSS and rule compiler)

**owner_layer**

Cross-cutting. Generator output plus engine/src/render/style_css.{h,cc}. T7 delivers the results into pages.

**purpose**

Two kinds of CSS from one vocabulary:
(1) the typeset DOM's contract, which carries only measured facts;
(2) the flow, semantic and static page stylesheet, compiled from the same rules, so no host has to re-implement presentation.

**definition**

```
- contract.gen.css (a build artifact):
  - the nowrap and robustness rules;
  - face classes, including the explicit `.tsr-code.tsr-cjk`;
  - tsr-pre, tsr-sup and tsr-sub;
  - the consumers of the root variables.
- runAttrs(HtmlOut&, FaceId, StyleId, RootCtx) is the one run-attribute writer for both the typeset and flow serializers. It emits:
  - face classes;
  - `tsr-c-*` classes;
  - validated paint declarations;
  - `lang`, only where it differs from the root.
- rulesToCss(env) produces the flow-page stylesheet:
  - Each rule becomes `:where(<sel>){…}`. Specificity is 0, so declaration order decides, as in the cascade.
  - Selectors use T7's hook vocabulary: kind → element/class, role → data-role, class → .tsr-c-x, where-attributes → data attributes, textLang → :lang().
  - Run rows map to their CSS; em sizes stay em.
  - Block rows use their Css column, with em → calc(var(--tsr-base) * k).
  - Env 0, env 1 and document-level `$.set` rules compile into the page stylesheet.
  - A mid-document scoped env compiles under `[data-tsr-env=<64-bit content hash of its rule chain>]`. The flow serializer sets that attribute on EMIT roots in the env; it is content-addressed and therefore byte-stable.
- The flow serializer renders each leaf's `scope` projection inline. Everything the rules contribute arrives through the compiled stylesheet.
- A selector key with no faithful CSS form raises `rule-no-css`.
```

**surface**

- `renderTsm(src, {config})` returns {html, css}.
- The static exporter writes css next to the page.
- The blog drops its hand-copied heading, figcaption, blockquote and 中文-indent CSS (zball-io/src/_includes/base.njk:34-36, 48-56).

**replaces**

- runtime/src/main/shell.mjs:14-60 (hand-written metric-bearing CSS mixed with feature paint)
- engine/src/render/semantic_html.cc:58-111 (second style→CSS writer)
- zball-io/src/_includes/base.njk:34-36, 48-56 (static-page presentation copied by hand)

### ConfigCodec (sectioned settings document)

**owner_layer**

Cross-cutting/API. engine/src/api/settings.{h,cc}, generated from the shared schema.
- T4 owns the codec mechanism (layering, diagnostics, profiles) and the style sections.
- Sections owned by other themes register through the same schema.
- T9 owns transport through the worker and shell.

**purpose**

Implements document-model §11: one typed JSON settings document instead of per-knob setters.
- Every section is validated by its schema.
- Layering follows one rule.
- Host-only sections are unreachable from documents.
- Native drivers and goldens use the same codec through named profiles.

**definition**

```
- API: `int tsr_set_config(WasmDoc*, const char* json)` returns the diagnostic count.

Sections and owners:
- T4: doc, fontRoles, text, par, block, code, codeblock, break, cost, table, rules, locales.
- T3: roles, counters, and the terms inside locale packs.
- T6: page, box defaults.
- T7: copy, anchors.
- T9: host {width, epsilonSu, maxRounds, fontDeadlineMs}, fonts (@font-face descriptors), resources.

The policy is shared by all sections:
- An unknown key raises `config-unknown` (warning).
- A key at the wrong granularity raises `config-scope`.
- Layers: registry initial < profile < host JSON < document declarations. Host sections are unreachable from documents.
- Numbers are parsed with strtod under the C locale.
- An optional `$vocab` key carries the caller's VOCAB_HASH. A mismatch raises `vocab-skew` (error), which the worker surfaces.

Generated alongside: C++ accessors that replace Config, JS defaults and .d.ts, and the docs table.

tsrc gains:
- `--settings=f.json`
- `--set path=value`
- `--profile=golden`

Golden profile: engine/test/profiles/golden.json = {host:{width:300}, doc:{baseSize:16, lang:'zh-CN'}} (tests.cc:415-416, wasm_api.cc:62). An optional `<fixture>.settings.json` replaces the 'indent', 'punct-full', 'punct-none' and 'paged' filename conventions; for 'paged', page is {height:240}.

The shell and worker forward an opaque `config`. The shell's named options become sugar that maps onto dotted keys. Until M4, fontFamily also keeps painting the container.

Invalidation rule for T9: a change to any cascaded row (Run/Extent/Block/Doc) means re-ingest from instantiate. Only host-section keys enter at a later stage (width → break).
```

**surface**

- `engine.typeset(src, el, {config:{doc:{lang:'en'}, par:{indent:'2em'}, fontRoles:{mono:{latin:'"Sarasa Mono SC"'}}, rules:[[{kind:'para', textLang:'zh'}, {par:{indent:'2em'}}]]}})`
- `renderTsm(src, {config})`
- `tsrc --stage=layout --profile=golden --set par.indent=2em f.tsm`

**replaces**

- engine/src/api/wasm_api.cc:42-76 (tsr_config with positional sentinels, plus 6 per-knob exports)
- runtime/src/worker/worker.mjs:157-193 (17-field destructure)
- runtime/src/main/shell.mjs:331-337, 358-362, 393-397 (re-enumeration; 'Georgia, serif' vs config.h:23)
- runtime/src/node/render.mjs:24-28 (lang only)
- engine/src/api/native_cli.cc:38-65 (--width/--indent/--punct)
- engine/test/tests.cc:414-426, 462-465 (configuration by filename substring)
- tools/export-static.mjs:49, 56, 64 (hard-coded fonts and <html lang='zh-CN'>)

### LocalePack (language as a configuration dimension)

**owner_layer**

L4 support, cross-cutting. engine/src/locale/ plus engine/locale/*.json, compiled into gen/.

**purpose**

Language-keyed data packs with a deterministic fallback, selected by each node's effective `text.lang`. They replace the two-branch host switch. The document can declare its own language.

Packs carry data (terms, text tailoring and `auto` resolutions), never presentation. Language-dependent presentation is an ordinary rule with a `textLang` selector.

**definition**

```
- struct LocalePack { std::string tag; Terms terms /*schema owned by T3*/; TextTailoring text /*owned by T5*/; Patch autos /*values for `auto` rows: par.hyphenate, text.punct, text.emphasisMark*/; };
- const LocalePack& packFor(StrRef lang):
  - first apply a vendored likely-subtags subset: zh-TW/HK/MO → zh-Hant; zh-CN/SG → zh-Hans; ja → ja-Jpan;
  - then walk CLDR-style parent locales: zh-Hant → und, NOT zh. So zh-TW → zh-Hant → und, zh-CN → zh-Hans → zh → und, and ja → und.
- The `und` pack is neutral English (Figure / Table / Eq. / ': ').
- The zh pack reproduces 图/表/式/：.
- doc.lang is resolved in this order: registry initial 'zh-CN' (today's default, wasm_api.cc:62, shell.mjs:333) < host config < the document's `$.doc({lang})`. It becomes the root text.lang, and renderers emit `lang` on the root.
- emphasisMark `auto`: zh-Hans → filled dot, under; ja and zh-Hant → sesame, over.
- `$.locale(tag, partialPack)` (a declaration) and the config section `locales` extend packs.
```

**surface**

- `#{ $.doc({lang:'ja'}) }`
- `#{ $.locale('ja', {terms:{figure:'図 ', table:'表 ', equation:'式 '}}) }`
- `#!abstract(style:{lang:'en'}) … #abstract!` switches terms, hyphenation, punctuation rules and emphasis marks inside the scope.

**replaces**

- engine/src/api/config.h:66-71, 89-102 (applyLang: ja → 图, zh-Hant → 图)
- engine/src/api/wasm_api.cc:62-66 (tsr_set_lang)
- runtime/src/node/render.mjs:24-28
- runtime/src/main/shell.mjs:44 (text-emphasis hard-coded to 'under right')
- zball-io/eleventy.config.js:29-32 (front-matter regex for lang), :68 (paraIndentEm chosen per language by the host)

## Subsumption (finding → mechanism)

- **subsumed** by *PropRegistry*: `markup-language/style-surfaces`, `codegen-ops-model/fixed-styling-fields`, `render-runtime/missed:2`, `real-world-evidence/missed:3`
  One generated patch vocabulary serves every surface: #style, $.style.push, $.set, the `style:` attribute and host JSON.

  Validation:
  - C++ is authoritative, at read and instantiate; JS gives early diagnostics.
  - Values are canonical, and duplicate keys raise style-conflict.
  - VOCAB_HASH detects JS/WASM skew.

  What goes away:
  - raw class bits (numeric push → style-raw-bits);
  - CSS-string smuggling;
  - the three JS key lists.

  What becomes reachable from markup: relative size, sup/sub (baseline), caps, attach and classes.

  The CJK classification bit leaves Styling. Weight, italic and decoration become nearest-wins/flag-fold rows. The ArgK/Kind range checks in the reader belong to T2.
- **subsumed** by *ScopeDelta, Rules and the declaration channel*: `codegen-ops-model/two-style-encodings-and-stack`, `codegen-ops-model/region-meta-args-hijack`, `markup-language/missed:3`
  There is one delta codec: STYLE_PUSH references a childless `styled` node, and any node may carry an own `style`. The schedule stack stays the primitive, as v2 §12 commits to.

  The region/fence `style:` meta-arg replaces the four-key lift. It merges into the returned node's own delta and wraps only otherwise, so handlers receive clean args.

  The retroactive inline push is fixed by the statement/content block split (style-in-value), which uses T2's `__block`. Statement-level pushes keep working (patch.tsm). Document declarations ride DECLARE, never the stack.
- **subsumed** by *ScopeDelta (engine default stylesheet) + Cascade.make/lift + CssEmitters.rulesToCss*: `codegen-ops-model/kind-default-styles-in-emit`, `codegen-ops-model/resolver-fabricated-styles`, `emitter/kind-presentation-in-emit`, `real-world-evidence/presentation-constants`
  Kind defaults become env-0 rules in defaults.json: heading weight, size, align and keep; code mono, 0.85em and preserve; caption; list and quote insets and gap; note, marker and term roles.

  Every post-instantiation fabricator goes through Cascade.make with a defined anchor state, and notes go through Cascade.lift. Inherited colour and size are therefore kept, which fixes the verified figure-prefix case, and TOC and notes lists keep the list rule.

  Overrides work because each property has one winning op.

  The static-page CSS duplication is removed because rulesToCss compiles the same rules, including the Block rows' Css, into the flow-page stylesheet.

  Parts owned elsewhere:
  - per-depth and CJK counter styles: the list.marker domain is owned by T3, and the depth selector by T4;
  - generated strings (' — ', '[n] ', '⚠ ') are T3 templates and terms.
- **subsumed** by *PropRegistry (text.baseline inherited and additive, text.size em, text.attach, text.caps) + default rule {role:'fn-marker'}*: `codegen-ops-model/cls-sup-feature-bit`, `real-world-evidence/sup-attach-private`
  CLS_SUP splits into three public rows:
  - `text.baseline:'super'`: inherits and adds, so sup-in-sup works. It is paint-only and renders as class tsr-sup for a single step.
  - a measured `size:'0.7em'`.
  - `text.attach:'prev'`, an Extent row with BREAK effect, which emit reads in place of the CLS_SUP test (emit.cc:86-90). T5 later realises it as Penalty items.

  Nothing keys on a class name or on a paint property, and no invisible U+2060 enters content or copy.

  `text.caps:small` is registered and implemented by T5 as synthetic small caps, which stay measurable with canvas.

  The public inline `raw()` constructor is owned by T2 and T5 (the InlineObject protocol).
- **subsumed** by *ClassChannel + code.hang + Cascade.make in foldTokens*: `codegen-ops-model/token-class-as-color`, `emitter/comment-by-css-color`, `render-runtime/token-theme-sniffing`, `api-measure-code/token-class-as-color-string`, `break-layout-pages/comment-role-by-color`
  foldTokens creates each token leaf with Cascade.make(codeblock state, text, class tok-<tag>), so rules see built-in tokens exactly as they see a user fence handler's runs.

  The default rule {class:'tok-comment'} → {italic, code.hang:'content'} replaces `tag==3` and the colour compare. The fold's special case is deleted in M6; until then it sets the properties directly.

  Layout reads CodeRun.hang. User handlers get parity through the class or by setting code.hang directly.

  T9 owns tokens.def. Colours move to theme CSS, delivered by T7 (M8).
- **subsumed** by *ClassChannel*: `render-runtime/no-class-channel`
  Implements dynClasses:
  - classes from inline nodes go on runs;
  - block classes go on NodeProps, and T7 places them on unit and box elements;
  - rendered order is by string value.

  Per-StyleId generated classes are rejected because they would break per-paragraph byte identity (editor-design.md:53-54).
- **subsumed** by *CssEmitters + Faces*: `render-runtime/css-contract-monolith`, `render-runtime/duplicate-serializer-primitives`
  contract.gen.css carries only measured facts and contract rules: face classes with an explicit mono×cjk rule, tsr-pre, tsr-sup and tsr-sub, root variables and nowrap.

  The engine emits root variables from DocProps.

  One runAttrs writer serves both serializers.

  T7 owns HTML escaping, anchor naming, the feature-CSS modules and theme.css.
- **subsumed** by *Faces*: `api-measure-code/font-role-split`, `emitter/missed:5`, `api-measure-code/missed:3`
  faceOf resolves role × script, using the order role.cjk → body.cjk → role.latin for CJK.

  MetricStore, vmets and requests key on FaceId, so colour, link, decoration and token variants share one entry.

  User roles get root variables and inline var() paint.

  Families the document declares are loaded through T9's resource protocol.

  The math font is not a T4 role; T8 owns MathFont.
- **subsumed** by *PropRegistry (granularity per evidence) + carriers + Cascade (NodeProps)*: `emitter/global-typography-config`, `api-measure-code/global-feature-knobs-no-cascade`
  Granularity follows documented per-scope demand.

  Block rows:
  - par.indent, par.align, par.hyphenate;
  - codeblock.snapKerning, fontFeatures, sidecarFrac, contIndent (verbatim §1/§3/§5).

  Run row: text.punct. T5 owns the pair rule.

  Doc rows: cjkJustify, cjkGlue, leading, break.*, cost.*, grid rails and table pads. The KP cache is shared across documents, so these are promoted only on demand.

  Values are read from the unit's own node, never from its leading run.

  Settable element attributes alias rows, so ```` ```cpp(snapKerning: true) ```` is the own delta of that block. The per-language feature map becomes a {kind:'codeblock', lang} rule.

  There is no implicit lifting of keys by name.
- **subsumed** by *ConfigCodec*: `render-runtime/config-plumbing`, `api-measure-code/config-plumbing-per-knob`, `api-measure-code/native-driver-config-divergence`
  A sectioned JSON ABI replaces the per-knob exports. The keys that are not properties, such as page.size, copy.ref, idPrefix and roles.*, live in the sections of their owning themes, under the same codec and diagnostics.

  Defaults are generated once.

  The golden profile plus per-fixture settings files make `tsrc --profile=golden` reproduce the goldens.

  T9 owns driveToCompletion and the native token provider.
- **subsumed** by *PropRegistry (policy rows) + named safety rails*: `emitter/scattered-magic-constants`, `api-measure-code/magic-policy-constants`
  User-visible policy becomes rows: heading sizes as rules, the tight gap as Fraction 1/3, minCols, snap tolerance and max q, table pads, and the marker raise as `baseline` in px.

  Safety rails (hl 10000, nargs ≤64, `<64su` guards) become named constexprs in one header.

  Owned elsewhere:
  - T5: the TextRules constants (punct half, CJK boundary, kern cutoff, hyphen minima);
  - T9: driver and resource policy (round cap, timeouts, cache budgets) in its host section.
- **subsumed** by *LocalePack + declaration channel*: `real-world-evidence/locale-terms-switch`
  This theme owns:
  - the pack container and the deterministic fallback (likely subtags plus parent locales);
  - `$.doc({lang})` as a declaration;
  - the effective language per scope;
  - pack extension by the host and the user.

  T3 owns the schema of the `terms` section and how it is consumed.

  `und` becomes neutral English, and the golden profile states zh-CN explicitly.
- **owned-by-other-theme** by *T5-text-shaping*: `markup-language/quote-context-heuristic`
  The width class is f(run lang, with the neighbour heuristic as fallback), a T5 TextRules tailoring.

  T4 supplies the precondition: every run carries an effective text.lang, because doc.lang is folded into the root Styling. Authors override it locally with `#style({lang:'en'})[“OK”]`.
- **owned-by-other-theme** by *T6-layout-pagination*: `break-layout-pages/vertical-spacing-constants`
  T6 owns the mechanism: VGlue with stretch, shrink and collapse, and positions taken from LayoutResult.

  T4 owns the values and their su-exact definition:
  - block.gap (Fraction of doc.parGap; 1/3 = 409su at 16px, identical to paraGap/3);
  - block.spaceBefore/After as Glue;
  - block.indent summed per ancestor;
  - keepWithNext on a container's last unit.

  The nearest-common-container rule supplies the descendant context that node-local selectors lack.
- **bug-fix-only** by *Migration M0–M4*: `codegen-ops-model/css-injection-style-values`, `codegen-ops-model/sizepx-ignored-in-emit`, `emitter/sizepx-em-mismatch`, `codegen-ops-model/metric-key-fragmentation`, `codegen-ops-model/tree-dump-omits-sup`, `codegen-ops-model/popto-stack-divergence`
  Verified bugs, fixed on the way to the abstractions:
  - CSS injection: validators, in M2.
  - sizePx ignored in emit (both items): emPx, in M1.
  - Key collision: <<32, in M0.
  - Metric fragmentation: FaceId, in M4.
  - SUP missing from the dumps: the generated dumper, in M2.
  - popTo divergence: the clamp, in M0.

## User extension examples

### Theorem environment presentation: italic body, own spacing, no first-line indent, themeable

**Today**

`#!theorem … #theorem!` becomes group{role:'theorem'} and is flattened into its children's units, with no geometry (emit.cc:816-823).
- Italics need `#style({italic:true})` around every paragraph.
- Spacing and indent are global Config values (config.h:33, 38).
- The typeset DOM has no hook for theme CSS.

**After**

`#{ $.set({role:'theorem'}, {italic:true, par:{indent:0}, block:{spaceBefore:'0.8em', spaceAfter:'0.8em', keepWithNext:false}}) }`
`#!theorem(class:'thm')`

How each part takes effect:
- italic and par.indent inherit into the paragraphs;
- spaceBefore/After apply at the container's first and last unit;
- `tsr-c-thm` lands on the box element (T7).

It is the same mechanism as the built-in `{role:'caption'}` rule. Numbering comes from T3's RoleSpec.

### Custom footnote-like mark (sidenote ①, dagger) that looks and breaks like the built-in marker

**Today**

Superscript is reachable only through the raw-bit back door `$.style.push(1<<19)`. That push is block-scoped and retroactive, has no relative size and no no-break rule (emit.cc:89 tests CLS_SUP).

**After**

`#let mark = (s) => style({baseline:'super', size:'0.7em', attach:'prev', class:'sidemark'}, s)`

These are the same three properties the engine's `{role:'fn-marker'}` rule sets. No invisible characters are added to the content or to copy.

### Fence handler returning pre-highlighted code that keeps comment-aware hanging

**Today**

The handler must forge `style({color:'var(--tsr-tok-comment)'}, …)` (test/fixtures/code/hang.tsm:3). Any other colour silently disables hanging (emit.cc:618-622).

**After**

Write `style({class:'tok-comment'}, …)`. That is the class the built-in highlighter's folded tokens carry, through Cascade.make, so the same default rule applies italic and `code.hang:'content'`. Alternatively, set `{code:{hang:'content'}}` directly. Colour comes from theme CSS.

### English abstract inside a Chinese document, and a Chinese site that wants 2em indent only for Chinese paragraphs

**Today**

Indent, hyphenation, punctuation compression and supplements are host globals (config.h:36-71, 89-102). The blog picks the indent per language in host JS (zball-io/eleventy.config.js:68) and copies its CSS by hand (base.njk:34-36).

**After**

Two pieces:
- `#!abstract(style:{lang:'en', par:{hyphenate:true}}) … #abstract!`
- one site-wide host rule: `rules:[[{kind:'para', textLang:'zh'}, {par:{indent:'2em'}}]]`

The effective lang selects T5's text rules and T3's terms through packFor. rulesToCss emits the same indent for the static page.

### Per-block and per-language code typography

**Today**

Snap-kerning and font features are host-only globals (wasm_api.cc:67-76, Config.codeFontFeaturesByLang).

**After**

Two pieces:
- ```` ```cpp(snapKerning: true) ````: a settable attribute that aliases codeblock.snapKerning, so it becomes that block's own delta;
- `#{ $.set({kind:'codeblock', lang:'haskell'}, {codeblock:{fontFeatures:"'calt' 1"}}) }`

### Adding a locale (ja, zh-Hant, de)

**Today**

You edit the C++ applyLang (config.h:89-102). ja and zh-Hant currently get 图 instead of 図 or 圖.

**After**

`#{ $.doc({lang:'ja'}); $.locale('ja', {terms:{figure:'図 ', table:'表 '}}) }`, or ship engine/locale/ja.json.

zh-TW resolves to zh-Hant through likely subtags, and zh-Hant never falls back to Simplified zh.

### Restyling built-in kinds (non-bold headings, larger h1, keep-with-next for a user block)

**Today**

Impossible. emit OR-composes bold and size after every user mechanism (emit.cc:499-502), and keep-with-next is `Kind::heading` at top level only (typeset_html.cc:704).

**After**

- `$.set('heading', {weight:400})`
- `$.set({kind:'heading', level:1}, {size:'2em'})`: one winning op, relative to the parent, so 2em, not 3.2em
- `$.set({role:'callout'}, {block:{keepWithNext:true}})`: applied to the callout's last unit

### A per-script font pairing (Kai for CJK, Crimson for Latin) applied to a passage

**Today**

`#style({font:'"LXGW WenKai", serif'})` replaces the family for both scripts (test/fixtures/style/patch.tsm). monoFont is unreachable from JS.

**After**

Declare `fontRoles:{kai:{latin:'"Crimson Text"', cjk:'"LXGW WenKai"'}}` in host config or `$.doc`, then write `#style({fontRole:'kai'})[…]`.

The family is measured through FaceKey, loaded through T9's resource protocol and painted via `var(--tsr-font-kai-cjk)`, which is emitted on the root.

### Depth-dependent list bullets and small caps

**Today**

Markers are literally '•' and 'N.' (emit.cc:517). Converters drop \textsc (tools/convert/tex2tsm.mjs:176).

**After**

- `$.set({kind:'list', depth:2}, {list:{marker:'◦'}})`
- `#style({caps:'small'})[NASA]`: synthetic small caps, measured like any other run

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  Determinism:
  - Size multipliers stay float32, multiplied parent-first.
  - emPx and FaceKey.sizePx use today's describeStyle formula, so the 1-su ceil boundaries the critic found (structure 'Notes' 4098su, notes/basic 2178su) stay put.
  - The tight gap is a Fraction applied with integer division to the rounded doc gap: 409su, as today.
  - Insets are rounded per container, as today.
  - Values are canonicalised: -0 → +0, NaN is rejected, and FontList canonicalisation is the identity on fixture strings.
  - Duplicate PropIds in a patch are rejected, so sort stability is irrelevant.
  - Interning follows document order, and rules match in sequence order, never in hash order.
  - Classes render sorted by string.
  - JSON numbers are parsed with strtod under the C locale.
  - The mock measurer reads only FaceKey.sizePx (mock.h:24-33).

  Golden churn per step:
  - M1: style/patch only.
  - M2: SUP in the 3 notes tree dumps.
  - M4: the root line of the 48 html goldens.
  - M5: all 48 .ops; tree dumps of the patch, figure and notes fixtures; the hang fixture's ast/js/tree.
  - M6: about 22 tree dumps (effective heading and code styles, the note-body wrapper); semantic only if T7's role map is absent.
  - M8: token html (6 files) and semantic (2 files); tsr-pre on code runs (11 html).
- **I2 measurement–render robustness contract (v2 §7)**
  - Only MEASURE rows enter FaceKey, and renderers derive metric CSS only from FaceKey. That includes the CJK italic → upright mapping and an explicit mono×cjk rule; today CODE beats CJK in measurement while CSS paints the reverse.
  - PAINT rows map only to allow-listed non-geometric CSS, and RENDER rows only to contract classes. The generator enforces both.
  - text.space is SHAPE|RENDER, so 'collapse' changes measurement as well as paint.
  - letter-spacing and word-spacing are never registered.
  - The baseline shift is relative positioning, and the nowrap rules stay in the generated contract.
  - Root variables ship together with FaceTable (M4), so a configured font is never measured in one face and painted in another.
  - Residual exposure, documented rather than claimed closed: `lang` can switch 'locl' glyphs and generic fallback (document-model.md:82); FaceKey.lang is reserved.
  - The §16 dev audit compares each run's computed font with its FaceKey.
- **I3 ops contract and OPS_VERSION discipline**
  - One delta codec.
  - The C++ reader validates property names and values with total validators, so it stays a fuzz target.
  - There is one OPS_VERSION bump (M5), co-scheduled with T2's name-keyed bump. It also adds DECLARE and the VOCAB_HASH header field.
  - From then on, vocabulary growth changes VOCAB_HASH rather than OPS_VERSION. A mismatch, whether in the ops header or in the config `$vocab`, raises a `vocab-skew` error. Skew is therefore still detected, and it is distinct from an invalid value.
- **I4 execution declares, resolver decides**
  - `$.set`, `$.style`, `$.doc` and `$.locale` are write-only. `$.style.height` reports the script's own stack depth only.
  - The resolver stays a pure pass. It calls Cascade.make and Cascade.lift, which are pure functions over the tables.
  - It writes no presentation: no Styling literals, no ×0.7, no ×0.85.
- **I5 dual-target rule**
  - The registry, codec, cascade, faces, locale tables and rulesToCss are engine-core C++ with no browser assumptions.
  - CSS is either a build artifact or engine-serialised text.
  - Native tests use the same JSON profile and the mock measurer.
- **I6 emission-time style binding with the DAG/schedule encoding**
  Explicitly amended, not claimed as preserved.

  v2 §12 l.246 ('TeX grouping, not Typst set/show rules') and document-model §3's 'bits OR / nearest-wins' are rewritten in M6:
  - The stack remains the primitive for scope: which content is affected.
  - Selectors add which nodes within that scope.
  - The precedence table is inherited < env rules < own delta.
  - Unconditional stack deltas apply once at the EMIT root, as inherited values. Consequently `$.style.push({weight:400})` no longer un-bolds headings; `$.set('heading', …)` does that.

  Binding stays at emission time:
  - Rules are stack entries snapshotted at each EMIT.
  - `style.where` travels inside values.
  - Instantiate copies per emission.
  - Fabricated nodes take their anchor's state; lifted content keeps its site scope and env.
  - Deferred emissions replay the stack snapshot taken at their call site.

  New golden fixtures pin the documented precedence cases.
- **I7 block-granular containment**
  This is the implementation of v2 §12 l.251, amended:
  - Content blocks cannot touch the stack (style-in-value), so they need nothing restored.
  - Statement blocks keep their pushes on a normal exit, and pop to the entry height on a throw.
  - An invalid value drops one property with a diagnostic; the block is never lost.
- **I8 resumable pull loop; atomic per-paragraph upgrade**
  - No new pull state is added. NEED_MEASURE batches key on FaceId, and there are fewer of them.
  - Settings are applied before compile.
  - No StyleId, FaceId, PropsId or env id appears in the serialised typeset HTML. Class names are content, and root variables come from settings.
  - chunkParas, which today requires the exact `<div class="tsr-doc">` (shell.mjs:251-253), is changed in M4 to parse the root open tag. patchIn already full-swaps only when the head differs.
  - An e2e test checks that a one-paragraph edit still patches.
- **I9 hot path and editor fast path (~6 ms at 87K)**
  - Styling and NodeProps are generated fixed-layout structs.
  - The enter memo uses an exact key, and rule buckets are indexed by kind.
  - Fabrication through make() hits the memo, for example once per (codeblock state, token class).
  - Patch validation is memoised per string ref.
  - FaceId dedup removes per-colour, per-link and per-token measurement duplicates.
  - ContentNode grows by 12 bytes.
  - Gate: M6 must not regress tools/bench-edit.mjs by more than 5%.

## Interfaces

- **T2-constructor-ir** (consumes)
  T4 needs from T2:
  - One shared schema source and generator. T2 owns the format, kinds, attributes and codecs; T4 owns the props section and its columns. A kind-attribute row may alias a property path (the settable flag).
  - Universal `style`, `class` and `role` attributes on every kind, including text.
  - A declaration channel: a DECLARE entry plus a `decl` node, used by $.doc, $.locale and T3's $.role/counters.
  - VOCAB_HASH in the ops header, in the same bump as the name-keyed codec (M5).
  - `__block` with two classes:
    - statement blocks: style ops allowed; restore on throw only; no scope wrapper that hides `const`/`let`;
    - content blocks: a value-phase flag that makes style ops a diagnostic.
  - The default `__region`/`__fence` pipeline strips `style:` and merges it into a single returned node's own delta (wrapping otherwise).
  - Fragment lowering produces ordinary nodes with own deltas through Cascade.make.
  - Level normalisation creates its wrappers through Cascade.make and treats `styled` as transparent.
  - toContent maps `undefined` to nothing.
- **T2-constructor-ir** (provides)
  - props.gen.mjs: PROPS, SHORTHANDS, parsePatch.
  - The ctors style, style.where, sup and sub.
  - The surfaces $.style.{push, popTo, height}, $.set, $.doc and $.locale.
  - STYLE_PUSH(delta node) semantics.
  - The style-in-value, style-scope, style-conflict and style-raw-bits diagnostics.
- **T3-semantics** (provides)
  - Cascade.make(anchorState, …) for every resolver-built node. T3's builders adopt it:
    - captions, terms and markers are made under their parent;
    - TOC, glossary, notes and bibliography lists under the collect node's parent;
    - appended notes under endState().
  - Cascade.lift for flows (footnotes, future inserts).
  - The effective text.lang per node.
  - packFor(lang) with the `terms` section.
  - Default rules keyed on the roles caption, caption-label, fn-marker, note-body and term-name.
- **T3-semantics** (consumes)
  - Slot roles, such as 'caption' on figure paragraphs, are assigned at L2 by the stdlib constructors from the RoleSpec schema. This applies to built-in and user handlers alike, so the L3 cascade sees them; selectors never see roles assigned by the resolver.
  - Fabricated nodes carry roles instead of Styling.
  - The numbering-pattern format is the domain of list.marker.
  - Generated strings come from T3 templates and terms.
- **T5-text-shaping** (provides)
  - faceOf(StyleId, Script) and emPx(StyleId).
  - The effective text.lang on every run.
  - Rows for T5 to consume: text.punct (Run; T5 defines the cross-run pair rule), text.space, text.caps (synthetic small caps), text.attach (an Extent row that T5 realises as INF Penalty items at the extent's edges), par.indent and par.hyphenate (block level; `auto` resolved by the block node's lang), doc.cjkGlue and doc.cjkJustify.
  - The LocalePack `text` tailoring section.
  - T5 may add rows to the registry, for example text.autospace.
- **T5-text-shaping** (consumes)
  - Script classification per atom; it is no longer a style bit.
  - The TextRules constants that leave config.h:76-77.
  - Run-level tailoring reads the run's lang; block-level `auto` values read the block node's lang.
- **T6-layout-pagination** (provides)
  - NodeProps per node, with the consumption contract:
    - insets summed per ancestor, each rounded separately;
    - gap from the nearest common container (Fraction of doc.parGap, integer division);
    - spaceBefore and spaceAfter as Glue at container edges;
    - keepWithNext on a container's last unit.
  - The box.* rows, set through rules and the single `style:` meta-arg. There is no separate `box:` arg and no Config role table.
  - par.align/indent/hyphenate, codeblock.*, doc.leading, break.* and cost.* (BreakParams).
  - CodeRun.hang.
  T6 adds to breakKey every row its breaker reads.
- **T6-layout-pagination** (consumes)
  - The container layouter, VGlue/collapse, LineEnds and the page builder realise these values.
  - Until they land, emit applies the same su arithmetic.
  - The page builder reads block.keepWithNext instead of Kind::heading.
- **T7-render-runtime** (provides)
  - runAttrs.
  - contract.gen.css.
  - Root variables, base size and lang.
  - rulesToCss(env) for the flow, semantic and static page.
  - The `scope` projection for flow leaves.
  - Inline classes for runs and block classes on NodeProps, rendered in string order.
  - The selector-to-hook vocabulary that rulesToCss targets.
- **T7-render-runtime** (consumes)
  - HtmlWriter and AnchorNamer.
  - A role/class → element map for the flow page. It must include fn-marker → <sup> and caption-label/term-name → <strong>; otherwise M6 changes up to 10 semantic goldens.
  - Placement of block classes and roles on unit/box elements.
  - data attributes for the selector keys.
  - chunkParas parsing the root open tag (M4).
  - theme.css with the token, link and marker palette, shipped before M8 removes inline token colours.
  - Popups keyed on ref metadata; the tsr-sup class is kept meanwhile.
- **T8-math** (provides)
  faceOf(style, Script::MathText) and emPx for text leaves inside formulas, replacing math.cc:582-583. The math font is not a T4 font role: T8's MathFont owns it.
- **T9-host-protocol** (provides)
  - ConfigCodec: the shared policy, profiles, VOCAB_HASH handshake and the style sections.
  - The coarse invalidation rule: a change to any cascaded row means re-ingest; host keys declare their own entry stage.
  - FaceKey as the typed textWidth/vmet resource key, with features and lang reserved.
  - The families resolved by faceOf, to be fetched as font resources.
- **T9-host-protocol** (consumes)
  - The host, fonts (@font-face descriptors) and resources sections.
  - Worker and shell transport of the opaque config.
  - driveToCompletion for tsrc and the tests.
  - tokens.def, which supplies the tok-* names.
  - Font-face resource requests (NEED_RESOURCES) for document-declared families.
- **T1-surface-frontend** (provides)
  Generated patch keys and selector forms, for the grammar and completions. Any attribute sugar T1 adds (for example `{.warn lang=en}`) lowers to the `style`/`class` attributes.

## Migration

### M0 hygiene  → plan P0-08

- Canonicalise Styling floats (they stay float32): -0 → +0; NaN is rejected at applyPatch with `style-value`.
- Hash uses the same canonical bits as ==.
- MetricStore key becomes (u64)str<<32 | style.
- The JS popTo clamps and raises `style-underflow`.
- `$.style.push(number)` gets a deprecation diagnostic.

**Golden impact:** none

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/popto-stack-divergence`, `codegen-ops-model/metric-key-fragmentation`

### M1 single em accessor  → plan P0-08

Add emPx(StyleId) = (sizePx > 0 ? sizePx : base) × (double)sizeMul, which is describeStyle's formula. Use it for:
- emit fontPx (emit.cc:251);
- the punct half and CJK glue (:274-275);
- boundary glue (:289);
- indent (:478);
- inline and display math (:120, :759);
- resolveWidths (:958);
- MathTextCtx.

**Golden impact:** Only test/golden/style/patch.{blocks,breaks,layout,html}: the 22px run gets its correct glue and punct widths. All other fixtures have sizePx = 0.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/sizepx-ignored-in-emit`, `emitter/sizepx-em-mismatch`

### M2 schema rows over the existing wire  → plan P1-02

Add props.def to T2's schema generator. Generate from it:
- Styling, ==, Hash and applyPatch;
- the dumps, each keeping its own token order, with SUP added to the tree dump;
- the JS key tables;
- C++ validators plus early JS diagnostics;
- render/style_css.h runAttrs, shared by both serializers.

The semantic writer keeps its current property subset until M6. It nests sup>strong and uses fmtPx.

**Golden impact:** - Tree dumps: SUP appears on markers in notes/{basic,cjk-glue,explicit}.
- The content-args EM+BOLD / BOLD EM orders are unchanged (per-dump order).
- Semantic changes only where SUP and BOLD co-occur on a leaf; no fixture has that.
- html is unchanged.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/css-injection-style-values`, `render-runtime/missed:2`, `codegen-ops-model/tree-dump-omits-sup`, `render-runtime/duplicate-serializer-primitives`, `codegen-ops-model/fixed-styling-fields`

### M3 settings document ABI  → plan P1-03

- Add ConfigCodec (sectioned) and `tsr_set_config`. The old exports become wrappers.
- The worker and shell forward an opaque config. The named options become sugar, and fontFamily also keeps painting the container, as today.
- `fontRoles` is rejected (`config-scope`) until M4.
- tsrc gains --settings, --set and --profile.
- Golden profile: {host:{width:300}, doc:{baseSize:16, lang:'zh-CN'}}. Per-fixture settings files replace the filename substrings, including paged (page height 240).
- render.mjs and export-static take the config.

**Golden impact:** none

**OPS bump (as designed):** False

**Fixes:** `render-runtime/config-plumbing`, `api-measure-code/config-plumbing-per-knob`, `api-measure-code/native-driver-config-divergence`

### M4 faces and the root render contract  → plan P1-04

- Add FaceTable and faceOf. MetricStore, vmets, resolveWidths and the requests key on FaceId.
- Family resolution order with an explicit mono×cjk rule.
- `fontRoles` (body, mono and user roles) replaces bodyFont/cjkFont/monoFont. Roles are still selected by the CODE bit until M5.
- `.tsr-doc` emits role variables, base size and lang, together with the generated face classes and user-role var() paint. Measurement and paint therefore never diverge.
- shell.mjs chunkParas parses the root open tag, with an e2e patchIn test.
- MathTextCtx uses faceOf.

**Golden impact:** - The root line of all 48 html goldens changes.
- Run classes are unchanged (legacy names), and the mock widths depend only on size.
- Request JSON keeps its shape.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/missed:3`, `codegen-ops-model/metric-key-fragmentation`, `api-measure-code/font-role-split`, `emitter/missed:5`

### M5 one wire change (with T2's name-keyed codec)  → plan P2-08

Styling.bits retires:
- weight, italic and decoration become rows;
- size becomes {absPx, mul} f32;
- fontRole replaces CLS_CODE;
- CJK moves to T5 classification passed to faceOf;
- CLS_LINK is dropped;
- CLS_SUP becomes baseline:'super' (inherited, additive) + size 0.7em + text.attach:'prev' + role fn-marker, and emit reads attach (emit.cc:89).

Wire:
- STYLE_PUSH(delta node);
- universal style/class/role attributes;
- region `style:` merges into the node's own delta;
- DECLARE and VOCAB_HASH;
- raw-number push removed;
- fragment conv emits own deltas.

The figure ctor tags caption paragraphs with role 'caption' (L2); figDepth stays until M6.

foldTokens sets class tok-<tag>, keeps the inline colour and, for comments, sets italic and code.hang directly until M6. emit reads CodeRun.hang.

The hang fixture becomes `style({code:{hang:'content'}, color:'var(--tsr-tok-comment)'}, …)`.

**Golden impact:** - All 48 .ops are re-recorded once.
- Tree dumps:
  - style/patch: the aside's `styled` wrapper disappears and the group shows its own style;
  - 6 figure fixtures: caption paragraphs show role 'caption';
  - 3 notes fixtures: markers show role fn-marker.
- code/hang: its ast, js, ops and tree change; its layout and html do not.
- Unchanged: html (tsr-sup class kept for a single super step; token colour kept; classes not rendered yet), blocks and breaks (attach reproduces the CLS_SUP rule), and styled-argument dumps (surface spellings).

**OPS bump (as designed):** True

**Fixes:** `real-world-evidence/missed:3`, `codegen-ops-model/two-style-encodings-and-stack`, `codegen-ops-model/region-meta-args-hijack`, `codegen-ops-model/cls-sup-feature-bit`, `real-world-evidence/sup-attach-private`, `api-measure-code/token-class-as-color-string`, `emitter/comment-by-css-color`, `break-layout-pages/comment-role-by-color`

### M6 cascade, rules, default stylesheet, presentation NodeProps, containment  → plan P3-01

- Add Selector, RuleEnv, `$.set` and `style.where`.
- defaults.json becomes env 0.
- CascadeState is stored on nodes.
- Add NodeProps with par.align/hyphenate/indent, block.gap/indent/keepWithNext and list.marker.
- emit stops composing presentation (emit.cc:47-105, 471-541, 551, 624). It reads rows and computes the gap from the nearest common container instead of tightAbove; layout.cc:28 and doc.h:282 read the unit's stored gap.
- Every fabricator uses Cascade.make: the resolver, sidecars under the codeblock's parent, foldTokens (its special case deleted) and math text.
- Notes use Cascade.lift, with an inline role 'note-body' wrapper.
- Containment ships with T2's `__block`, and bibliography emission replays the stack snapshot.
- The flow serializer renders the `scope` projection, and rulesToCss produces the flow stylesheet.
- v2 §12 and document-model §3 are amended.

**Golden impact:** - About 22 tree goldens show effective styles on heading and code leaves, e.g. `[BOLD x1.60]`, `[CODE x0.85]`.
- The 3 notes trees gain the note-body wrapper.
- Semantic: none if T7's role map includes fn-marker → sup and caption-label/term-name → strong; otherwise up to 10 files (6 figure, 3 notes, refs).
- Unchanged: blocks, breaks, layout and html. Float32 parent-first products equal today's compose for every fixture chain; the gap Fraction gives 409su; insets are rounded per container; sidecars keep body size (code/sidecar is a regression guard).
- New fixtures: style/precedence (region style vs $.set, push of a relative size, h1 override), notes-in-heading, nested sup, a styled ref, and a role group with keepWithNext.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/kind-default-styles-in-emit`, `codegen-ops-model/resolver-fabricated-styles`, `markup-language/style-surfaces`, `markup-language/missed:3`, `emitter/kind-presentation-in-emit`, `real-world-evidence/presentation-constants`, `codegen-ops-model/token-class-as-color`, `break-layout-pages/vertical-spacing-constants`

### M7 knobs become scoped properties  → plan P3-02

The remaining Config knobs become rows of the granularity the evidence supports:
- Block: codeblock.snapKerning/fontFeatures/sidecarFrac/contIndent, and par.indent with a doc default;
- Run: text.punct;
- Doc: break.*, cost.*, grid rails, table pads, leading, cjkJustify, cjkGlue.

emit, layout and render read NodeProps or DocProps instead of cfg. Settable fence and region arguments alias rows. The per-language feature map becomes a rule. Safety rails become named constexprs.

**Golden impact:** None. Defaults reproduce config.h, and the *indent* fixtures use their settings files.

**OPS bump (as designed):** False

**Fixes:** `emitter/global-typography-config`, `api-measure-code/global-feature-knobs-no-cascade`, `api-measure-code/magic-policy-constants`, `emitter/scattered-magic-constants`

### M8 classes rendered and the theme split  → plan P3-18

- Run classes `tsr-c-*` are rendered.
- Token colours move from inline var() to `.tsr-c-tok-*` rules in theme.css, which T7 ships first.
- `tsr-pre` comes from text.space.
- TSR_CSS is reduced to contract.gen.css plus T7's modules.
- The dev audit is added.

**Golden impact:** - Token runs lose the inline colour and gain `tsr-c-tok-*`: 6 html and 2 semantic files (code/{wrap,tsm-hl,hang,json-hl,runs}, pages/paged-doc).
- Code runs gain tsr-pre: 11 html files.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/css-contract-monolith`, `render-runtime/no-class-channel`, `render-runtime/token-theme-sniffing`

### M9 locale packs and document language  → plan P3-30

- LocalePack, with likely subtags and parent locales.
- `$.doc` and `$.locale` through DECLARE.
- `auto` rows (par.hyphenate, text.punct, text.emphasisMark) resolve through packs.
- T3 switches supplements to pack terms, and T5 switches its tailoring.
- The exporter's `<html lang>` and renderTsm read doc.lang back from the compile result.

**Golden impact:** None. The golden profile sets zh-CN, and the zh pack reproduces 图/表/式/：.

**OPS bump (as designed):** False

**Fixes:** `real-world-evidence/locale-terms-switch`, `markup-language/quote-context-heuristic`

## Not generalized (kept special)

- **User-declared engine-consumed properties (new metric, shape or layout rows defined from a document)** — A metric row needs support in the measurement backend, and §7 forbids painting what was not measured.

Users get classes, rules over existing rows, font roles and theme CSS. A new engine property is one registry row plus consumer code, and only VOCAB_HASH changes.
- **Selector combinators (descendant, child, sibling) and CSS specificity** — The stack and `style.where` already scope rules. Two cheap context keys, textLang and depth, cover the evidenced needs.

List-internal spacing uses the nearest-common-container rule instead of a descendant selector. Node-local selectors keep the memo exact.
- **`show` rules (structural content transforms)** — They belong to T2's constructor rebinding and handler registry. Style rules only set properties.
- **letter-spacing, word-spacing and per-run line-height** — These are the engine's justification and layout channels (document-model.md:82; v2 §8).
- **Script classification and CJK italic as author knobs** — Classification belongs to T5. 'No italic for CJK' (v2 §14) is encoded in faceOf; only the emphasis mark is a property.
- **font-feature-settings in prose, and OpenType true small caps** — Canvas measureText cannot apply arbitrary features. Features are therefore allowed only in codeblock.fontFeatures, where the ch grid defines widths. Small caps are synthetic.

FaceKey reserves features and caps for a future DOM measurement backend.
- **Rules that add or remove classes** — Classes are node attributes and selectors read them. Letting rules change membership would create rule → class → rule loops.
- **Block granularity for break, cost, leading, cjkJustify and the grid rails** — There is no per-scope evidence for them, and the KP cache is shared across documents (doc.h:242-246). They stay Doc rows until a fixture or corpus case needs scoping.
- **Per-StyleId generated CSS classes** — StyleIds renumber on every edit, which would break patchIn's byte identity.
- **Ops and reader safety rails (hl range 10000, nargs ≤64, children ≤2^20, `<64su` guards)** — They are robustness bounds, not typographic policy, so they are named constexprs rather than settings.
- **Registered run paint beyond color, decoration, baseline and emphasis (background, shadow, border)** — YAGNI. Classes plus theme CSS cover them without enlarging Styling's interning key.

## Risks

- M5 is the large step: the ops bump, bit retirement and all 48 .ops re-recorded. Mitigations:
  - land it in the same bump as T2's name-keyed codec;
  - keep dump tokens and styled-argument spellings stable;
  - keep the tsr-sup class and the token colour until later steps.
- Semantic change: an absolute size now replaces the inherited size instead of composing with heading multipliers. `= Big #style({sizePx:20})[x]` goes from 32px to 20px. No fixture is affected, but documents that rely on composition change. This needs a documented note.
- Precedence change: rules that match a node beat values inherited from an outer `style:` or a stack push (CSS-like), so `$.style.push({weight:400})` no longer affects headings. This must be documented, with fixtures.
- Instantiation cost with many rules or many fabricated nodes: the memo must hit on the 87K editor path (bench gate at 5%).
- Theme dependence: from M8, hosts that do not inject theme.css lose token and link colours. This affects VS Code, zball-io and the exporter. Keep inline colour behind a flag for one release.
- Lifting semantics: run properties come from the site's scope and rules matched by site ancestors are dropped. This is today's behaviour, but a user may expect a note in a role-ruled theorem to be italic in the notes list.
- Flow-page fidelity: rulesToCss approximates (em → calc, depth selectors, env hashes). The semantic page is not measured, so this is acceptable, but drift between it and the typeset view is possible.
- Granularity confusion: two node records (Styling vs NodeProps) and Doc rows. Mitigations: generated typed accessors plus the `prop-scope`, `style-scope` and `config-scope` diagnostics.

## Open questions (decided in PLAN.md §3)

- Should `#{ $.set(...) }` in nested content positions (list items or region bodies, once T1 enables nested statements) lower to a `style.where` around the following siblings, as Typst's set does, instead of raising style-in-value? This needs T1 and T2.
- Confirm CSS 'absolute replaces' semantics for text.size, which overrides document-model §3's 'sizePx is a base and sizeMul composes on top'.
- Should author `text.color` be restricted to theme tokens (`var(--…)`) and classes, so that themes can always override it?
- Should a T3 FlowSpec be able to choose 'destination context' (LaTeX \normalfont) instead of the site scope for lifted content?
- Does the host need a 'force' layer above document declarations, for example site-wide font roles that documents cannot override?
- Should FaceKey carry lang and features once a DOM measurement backend (v2 §6 backend 2) can honour them?
- Should `list.marker` take T3's pattern string, or a structured counter-style record?
- Should the registry initial doc.lang stay 'zh-CN' (today's behaviour), or become 'und' (neutral)? This is a product decision; goldens are pinned by the profile either way.
- Should the flow page reflect mid-document scoped `$.set` rules through env-hash CSS, or document them as typeset-only?

## Changelog (critique responses)

- C1.1 [blocker] Cascade semantics for relative values, flags and rebase: ACCEPTED, and the cascade is restructured.
  - Per property, one winning Set/Initial op (env rules in sequence order, then the own delta). This gives 2em, not 3.2em.
  - em and baseline shifts resolve against the parent's computed value.
  - FlagSet rows fold AddFlags/ClearFlags in order.
  - rebase is replaced by Cascade.lift. Run properties come from the site's rule-free `scope` (= today's instantiate style), block properties from the destination. The note-body size comes from an inline wrapper with role note-body entered through the rules.
  - I verified it reproduces resolve.cc rescale: a probe of a footnote in a heading under a red push gives body [x0.85 color=red], not bold, and markers at base.
- C1.2 text.baseline Inherits=N: ACCEPTED. The row now inherits and is additive (computed px = parent + shift × own emPx), so sup() and the fn-marker raise reach text leaves. A nested-sup and styled-ref fixture is added in M6.
- C1.3 M5 removes CLS_SUP without an attach replacement: ACCEPTED. Added the explicit Extent row `text.attach` (BREAK). The fn-marker rule sets it, and emit reads it in M5 exactly where emit.cc:89 tests CLS_SUP. Nothing keys on paint or on a class, and there are no U+2060 characters or blocks churn.
- C1.4 M6 removes block presentation before BlockProps exist: ACCEPTED. NodeProps for the presentation rows (par.align/hyphenate/indent, block.gap/indent/keepWithNext, list.marker) move into M6. M7 keeps only the conversion of Config knobs into rows.
- C1.5 The codeblock rule leaks mono/0.85em into sidecars: ACCEPTED. I confirmed doc.h:98 and :131 parse sidecar notes in the body-text style. Sidecars now go through Cascade.make under the codeblock's parent (prose) state, which equals today's [base]. code/sidecar is listed as an M6 regression guard. I did not add a 'reset' op, because choosing the anchor state is enough.
- C1.6 Nodes created after instantiation bypass the cascade: ACCEPTED. Cascade.make is mandatory for every fabricator: resolver mkNode/mkText, extractSidecars, fragment conv (its bit fold deleted), foldTokens, math text and T2 normalisation. Anchor states are defined, including endState() for notes appended without an anchor. CascadeState is stored on nodes, so foldTokens can enter.
- C1.7 Container block properties, paraGap/3 and the em reference: ACCEPTED.
  - Defined consumption: insets summed per ancestor and rounded per container; the gap from the nearest common container; spaceBefore/After at container edges; keepWithNext on the last unit.
  - block.gap takes a Fraction of doc.parGap with integer division, so 1/3 = 409su. I confirmed 1229/3 = 409 against suRoundPx(0.4em) = 410.
  - Block lengths in em = doc.baseSize, as every Config length is today.
  - The nearest-common-container rule reproduces tightAbove without descendant selectors.
- C1.8 I1 double vs float32 multipliers: ACCEPTED. I verified the 4096.00006 → 4098su case for 1.6f. Multipliers stay float32 and are multiplied parent-first; emPx and FaceKey use describeStyle's exact formula. In the fixture chains, parent-first order coincides with today's compose and rescale, because at most two factors differ from 1.
- C1.9 Containment contradicts statement-level pushes: ACCEPTED. Two block classes:
  - content blocks: style ops are a diagnostic and a no-op;
  - statement blocks: persist on a normal exit, restore on a throw.
  v2 §12 l.251 is amended. patch.tsm goldens are unchanged and serve as the acceptance test.
- C1.10 `$.doc` as a poppable stack push: ACCEPTED. Document declarations move to T2's declaration channel (DECLARE + decl node). They are not counted in height and cannot be popped. Root-feeding keys must precede the first EMIT.
- C1.11 I6 presented as sugar: ACCEPTED. The invariant now says v2 §12 l.246 and document-model §3 are amended, with the precedence table and what the stack still means. Precedence fixtures are added.
- C1.12 I3 vocabulary growth escapes OPS_VERSION: ACCEPTED. VOCAB_HASH goes into the ops header (M5 bump, co-owned with T2) and into the config `$vocab`. A mismatch is a `vocab-skew` error, distinct from style-value.
- C1.13 Root attributes break chunkParas: ACCEPTED. I confirmed the exact startsWith at shell.mjs:252. The root attributes now ship in M4, and the same step changes chunkParas to parse the root open tag, with an e2e patch test.
- C1.14 Fonts measured but not painted during M3–M7: ACCEPTED. Root variables and face classes ship with FaceTable in M4. `fontRoles` is rejected by the codec until then, and the shell's fontFamily keeps painting the container.
- C1.15 Memo key: ACCEPTED. The key is exact with full equality and includes the where-attribute values, the textLang input and depth. Settable arguments are folded into ownDeltaId by the reader. Kind buckets are merged by sequence number.
- C1.16 text.space as RENDER only: ACCEPTED. It is now SHAPE|RENDER and consumed by T5, so collapse changes measurement too. The generator check covers RENDER templates.
- C1.17 text.lang overclaimed under I2: PARTIALLY ACCEPTED. The residual 'locl' and fallback exposure is now documented (document-model.md:82) and FaceKey.lang is reserved. I did not add lang to FaceKey: canvas cannot measure it, so it would split entries without changing widths. lang is emitted on .tsr-doc from doc.lang.
- C1.18 Effects mask lacks a CASCADE stage: PARTIALLY ACCEPTED. I did not add a CASCADE bit. Instead the invalidation rule for T9 is coarse: a change to any cascaded row means re-ingest from instantiate, and only host keys declare a later entry stage. The finer effects are no longer exported as an ABI.
- C1.19 Unstable sort with duplicate PropIds: ACCEPTED. Duplicates raise `style-conflict` and the first in wire order is kept, so sort stability no longer matters on either side.
- C1.20 Class render order by intern id: ACCEPTED. Classes render sorted by string value.
- C1.21 Dump order churn in M2: ACCEPTED. The generator keeps per-dump token orders: the tree dump keeps EM+BOLD, the block dump BOLD EM. content-args is unchanged.
- C1.22 Semantic goldens in M6: ACCEPTED. The flow serializer renders the rule-free `scope` projection, which equals today's leaf style, and rules arrive through rulesToCss. The only remaining churn is roles that replaced fabricated literals; T7's role map avoids it. M6's golden_impact lists the churn.
- C1.23 Token and hang transition in M5: ACCEPTED. foldTokens sets italic and code.hang directly until M6 deletes the special case. The hang fixture edit and its ast/js/ops/tree churn are listed in M5, and its colour is kept so layout and html do not move.
- C1.24 Figure role tagging is not ops-neutral: ACCEPTED. Tagging moves into the M5 re-record. Slot roles are execution-declared at L2 by stdlib constructors from the RoleSpec schema; this is the layering rule.
- C1.25 Locale fallback and the und pack: ACCEPTED. Added a vendored likely-subtags subset and CLDR-style parent locales (zh-Hant → und). und is neutral English, and the golden profile sets zh-CN explicitly.
- C1.26 Family resolution order: ACCEPTED. CJK uses role.cjk → body.cjk → role.latin, and there is no hidden mono exception.
- C1.27 `class` in rule patches: ACCEPTED. Conditional patches reject `class` (style-scope).
- C1.28 Bibliography binds to the end-of-program state: ACCEPTED. Deferred emissions replay the stack snapshot taken at the call site (executor.mjs:242-266).
- C2.1 [blocker] Non-inheriting properties have no carrier on this data shape: ACCEPTED.
  - baseline inherits.
  - Block rows on containers have a defined consumption contract (inset, gap, edges, keep), which T6 implements; emit applies the same arithmetic until then.
  - A region `style:` merges into the returned node's own delta instead of wrapping.
  - The theorem and callout examples now work, and fixtures are added.
  - I kept `styled` as an ordinary node with an own delta whose rows inherit, rather than a special 'transparent' mode, because inheritance gives the same result.
- C2.2 rebase changes meaning once defaults move into the cascade: ACCEPTED. lift keeps the emission-time stack context and the content's own deltas (via scope and the site env) and drops rules that matched site ancestors, as the critic proposed. The note-body size comes from enter() on the wrapper. Goldens are added for a footnote in a heading and in a role-ruled group.
- C2.3 Containment contradiction: ACCEPTED (same resolution as C1.9). T2 is also required not to wrap statements in a scope that hides `const`/`let`.
- C2.4 Semantic renderer leakage in M6: ACCEPTED, through the scope projection plus rulesToCss rather than 'effective minus env 0'. This also keeps user `$.set` rules visible on the flow page, via CSS.
- C2.5 foldTokens as a post-cascade fabricator: ACCEPTED. Tokens use Cascade.make with the codeblock's stored state, so built-in and user tokens are on equal footing. The comment special case is deleted in M6.
- C2.6 Encoding of $.doc and $.locale: ACCEPTED. One declaration channel, owned by T2 and shared with T3's $.role/counters. It is versioned through VOCAB_HASH, validated by the schema and kept outside the stack. `title` is a Doc metadata row that hosts read back.
- C2.7 Selectors at L3 vs caption roles at L4; three caption mechanisms: ACCEPTED. One mechanism: slot roles are assigned at L2 by the stdlib constructors from RoleSpec, and the {role:'caption'} rule carries the presentation. The style.where figure example is removed. Layering rule: selectors see only attributes present at instantiation.
- C2.8 Two schema registries: ACCEPTED. props.def becomes a section of T2's shared schema with one generator. Settable kind attributes alias property rows, so each knob has one name and one validator.
- C2.9 User font roles and document-declared fonts: ACCEPTED.
  - Non-built-in roles paint via inline var(--tsr-font-<role>[-cjk]), with root variables for every declared role.
  - Document-declared families are requested through T9's resource protocol.
  - fonts.math is removed (T8's MathFont).
  - The role map is renamed `fontRoles`, so it no longer collides with the host `fonts` face list.
- C2.10 No-break attach pushed into U+2060: ACCEPTED (same row as C1.3). T5 realises text.attach as Penalty items.
- C2.11 ConfigCodec generated only from props.def: ACCEPTED. The codec is sectioned, with each section owned by a theme (T3 roles/counters, T6 page, T7 copy/anchors, T9 host/resources/fonts). The policy and profiles are shared. host.maxRounds and fontDeadlineMs move to T9's section. I partially rejected 'T9 owns the codec shell': the layering is part of the cascade, so T4 keeps the mechanism and T9 the transport.
- C2.12 I1 churn claims: ACCEPTED (Fraction gap, float32, FontList canonicalisation as the identity on canonical input). The remaining churn is listed per step.
- C2.13 I3 skew: ACCEPTED (same resolution as C1.12).
- C2.14 Effects mask breadth and errors: ACCEPTED.
  - Only MEASURE, PAINT and RENDER are enforced; the others are advisory and not exported.
  - RESOLVE is added (text.lang, list.marker).
  - text.space becomes SHAPE|RENDER.
  - The `auto` domains are fixed: text.punct and par.hyphenate both include auto.
- C2.15 Over-cascading policy knobs: ACCEPTED. break.*, cost.*, the grid rails, table pads, leading, cjkJustify and cjkGlue are Doc rows. Block granularity is limited to the evidenced knobs (indent, align, hyphenate, snapKerning, fontFeatures, sidecarFrac, contIndent).
- C2.16 Precedence of stack pushes: ACCEPTED. Unconditional stack deltas apply once, at the EMIT root, as inherited values; conditional stack entries are rules. A fixture with a relative size under a push is added.
- C2.17 Classes stamped on every run: ACCEPTED. Inline classes go on runs; block classes go in NodeProps, and T7 places them on unit and box elements.
- C2.18 Selectors cannot express language or depth: ACCEPTED. Added `textLang` (BCP-47 prefix, from inheritance and own delta, never from rules) and `depth` (same-kind ancestors). This answers open question 4: packs carry data, and language-dependent presentation is a rule.
- C2.19 The static page still needs hand-written CSS: ACCEPTED. Added a Css column to the Block rows and the rulesToCss compiler (`:where()` selectors, specificity 0, declaration order). T7 delivers the output.
- C2.20 LocalePack defaults and CJK emphasis: PARTIALLY ACCEPTED.
  - Accepted: the und pack is neutral English, and the golden profile sets zh-CN.
  - Accepted: a `text.emphasisMark` row (PAINT) whose auto value comes from the pack.
  - Rejected: changing the registry initial doc.lang to 'und'. Today's documented default is zh (wasm_api.cc:62, shell.mjs:333), and flipping it silently changes every host that passes no lang. It is listed as an open product question.
- Missing items from the critics:
  - small caps: registered as text.caps, synthetic via T5;
  - raw(): attributed to T2/T5;
  - per-depth markers: depth selector plus T3 patterns;
  - Glue domain for T6 VGlue: block.spaceBefore/After are Glue;
  - tag==3 removal: realised in M6.
- Overlaps accepted:
  - T6 BoxModel: T4 owns the box.* rows and the single `style:` meta-arg; there is no `box:` arg and no Config role table.
  - T7: owns DOM hook placement and the role→element map for the flow page.
  - T9: owns the font-face requests.
  - T8: owns the math font.
