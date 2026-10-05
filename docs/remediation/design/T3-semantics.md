# T3-semantics

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

Make the resolver the interpreter of one element registry instead of a set of per-feature passes. An element class is a registry row named by a `role`. T3 owns the row's semantic columns: counter and policy, number frame, supplement term, title source, sites, ref item/group forms, flow or table membership, and alias rule. T4, T6 and T7 own the `style`, `layout` and `html` sections of the same row; all are declared through one `$.element` record and inherited through `like`. Built-in rows are written in the same data form a document uses. Definitions are hoisted, position-free records. The few operations that must be positional (an appendix switching a counter's pattern, \setcounter) are level-neutral `event` content values placed where they are spliced. Membership is computed once per occurrence, at instantiation, from typed selectors, and stored on the node. The resolver then runs three phases. LOCATE is read-only and produces pattern-tagged snapshots, labels, aliases built from raw components, rows, flow items and ref sites. BIND works in document order with keyed first-use counters. MATERIALIZE is staged: it resolves all ref, flow and row content once, attaches sites as synthetic nodes, and then expands collectors from titles frozen before attachment. The resolver computes only numbers, identities and memberships. Everything the reader sees comes from style-neutral templates, composed by a fixed left fold at a declared span/style source. Downstream stages read node SemInfo and their own registry sections, never role strings or L4 tables. A theorem family, Part/Chapter/§ numbering, an appendix, per-chapter footnotes with multi-marker named notes, a proof ending in ∎, an index or a cross-chapter reference is therefore a declaration, not an engine change.

## Diagnosis

The resolver has no model of what an element means. Each semantic service is a code path selected by Kind, by the role string "figure" or by the `what` string (resolve.cc:130-231, 284-291, 431-440). The same strings are tested again in emit (emit.cc:776-818), in the semantic serializer (semantic_html.cc:284-311) and in the shell (shell.mjs:146, 175). Five root causes follow.

(1) No registry. Counters are struct fields (resolve.cc:61-66). Supplements are Config fields behind a two-way language test (config.h:66-102). List markers have a second formatter (emit.cc:517). Every feature adds a field, a switch case and a literal, and each neighbouring layer invents its own role table.

(2) No declaration channel and no notion of position. Execution declares only LABEL/REF/COLLECT, the op vocabulary is closed, and `tsr_doc_new()` parses no config (wasm_api.cc:39). A side-effect EMIT lands before its enclosing block (model.cc:94-96; probe t3crit/emitorder.tsm), so even a positional workaround lands in the wrong place. The documented JS-counter fallback (design-decisions-v2.md:238) cannot reach the label table.

(3) Locating and materializing are interleaved. scan writes presentation (resolve.cc:171-180, 193, 208), rewrite assigns citation ordinals (234-241), and buildNotes resolves bodies on demand (372). Correctness depends on the executor emitting the bibliography last (executor.mjs:241-266). The results are misnumbered citations in footnotes, aliasing on a repeated #notes(), and TOC excerpts taken from unresolved content.

(4) Presentation lives inside the resolver. It appears as literals and absolute styles (resolve.cc:177, 212, 259-267, 331, 345, 378, 423, 452) and as single-consumer string args: ArgK::name="(n)" is read only by emit.cc:756. Inherited styles are lost, and the no-JS page has no equation numbers.

(5) Identity is conflated with presentation and scope. Labels double as URLs (resolve.cc:113, 263, 292). Synthetic labels share the user namespace without reservation (149, 211-212). Every table dies with a local Resolver (523), so there is no editor API, no manifest and no cross-document reference.

Built-ins and user constructs are therefore not on equal footing.

## Abstractions

### Element registry: one row per class (semantic columns here; style/layout/html sections owned by T4/T6/T7)

**owner_layer**

Cross-cutting registry in engine/src/elements/registry.{h,cc}. Built-in rows live in engine/data/elements.json and compile to gen/elements_defaults.h.

Ownership: T3 owns identity, membership and the semantic columns, which are interpreted at L4 in engine/src/semantic/. T4, T6 and T7 own their sections and validate them.

Membership is computed at L3 instantiate and stored on the node, so T4's style rules and L4 read the same answer. This refines the provisional layering: the registry is an explicit product, built before instantiate and passed to instantiate, resolve, emit and the serializers.

**purpose**

Answer 'what is this node' once, in one row shared by every layer. No stage compares role strings, and a user class is declared exactly like a built-in.

**definition**

```
using ClassId=u16; using CounterId=u16; using TemplateId=u32;
struct Pred { StrRef arg; ArgTag tag; double num; StrRef str; };      // typed: level=1 (Num), kind='table' (Str), ordered=true (Bool)
struct Selector { Kind node=Kind::any; SmallVec<Pred,2> preds; ClassId inside=kNone; };   // inside = class of the nearest classed ancestor
// Membership: the most specific matching selector, where specificity = (node set) + |preds| + (inside set).
// Ties go to the latest declaration, with diag selector-ambiguous.
// The default selector of class `n` is {role:n} on any kind. `select` is never inherited through `like`.
// `role` carries the class on every kind (T2 schema); `class` stays T4's style-tag channel.
enum class Policy:u8 { Always, Labelled, Never };                    // node arg numbered:false forces Never
struct NumberingSpec { CounterId counter; Policy policy; TemplateId frame; TermRef supplement; };  // frame e.g. ['(', slot number, ')']
struct TitleSource { enum:u8 { Arg, Part, InlineKids, Key } src; StrRef name; };
struct Site { TemplateUse tmpl; enum:u8 { Prepend, Append, Replace, Tag } where;
              enum:u8 { Self, FirstPara, LastPara, Part } at; StrRef part; };
// Tag = T6's generic margin tag on any block unit (equation numbers, numbered listings).
struct AliasRule { StrRef prefix; enum:u8 { Comps, Ordinal, Key } body; };
struct FlowSpec { StrRef flow; Placement placement; u8 depth; TemplateUse marker, entry; };  // inline (note) or block (region)
struct TableSpec { StrRef table; bool citeable, multi; };
struct ElementClass {
  StrRef name; std::vector<Selector> select; ClassId like=kNone;     // every other field inherits through like
  std::optional<NumberingSpec> numbering;
  enum:u8 { Self, Enclosing } refersTo=Self;   // Enclosing: a label here resolves to the nearest classed ancestor (Typst: table in figure)
  std::vector<TitleSource> title; std::vector<Site> sites;
  small_map<StrRef,TemplateId> refForms; StrRef defaultForm; TemplateId refItem, refGroup; bool compressRanges=false;
  std::optional<FlowSpec> flow; std::optional<TableSpec> table;
  AliasRule alias, siteAlias; bool outline=false;
  TraitSection style, layout, html;            // opaque to T3; owned and validated by T4 / T6 / T7
};
struct SemInfo { ClassId cls; u32 inst; StrRef number, anchor, targetAnchor; ClassId targetCls; StrRef flow, part; bool synthetic; };
// SemInfo is a non-arg node field. instantiate writes cls; MATERIALIZE writes the rest. Tree dumps do not print it.
// Layers: built-in < host config < document, overlaid PER FIELD (last write wins).
// `like:x` inside a redefinition of x resolves against the previous layer.
// Phase 0 checks that the like and within graphs are acyclic (diag decl-cycle; the offending edge is dropped).
```

**surface**

#{ const theorem = $.element('theorem', {
     counter: {name:'thm', within:'heading', depth:1}, numbering:'1.1',
     supplement: {en:'Theorem ', zh:'定理 '}, title: [{arg:'title'}],
     sites: [{where:'prepend', at:'first-para',
              template:[strong(slot('supplement'), slot('number')), when({of:'title'}, ' (', slot('title'), ')'), strong('.'), ' ']}],
     html: {element:'section'}, layout: {keepWithNext:true} })      // html → T7 section, layout → T6 section
   $.element('lemma', {like:'theorem', supplement:{en:'Lemma ', zh:'引理 '}}) }
#!theorem(label: "thm-ua", title: "Univalence") … #theorem!
The region fallback builds group{role:'theorem'}, which matches the default selector.

Typed selectors replace numberAs and absorb:
$.element('table-figure', {select:[{node:'group', role:'figure', kind:'table'}], like:'figure', counter:'table', supplement:{term:'table'}})
$.element('figure-table', {select:[{node:'table', inside:'figure'}], like:'table', numbering:null, refersTo:'enclosing'})

Per-level headings:
$.element('part', {select:[{node:'heading', level:1}], counter:'part', supplement:{term:'part'}, outline:true})

Host JSON is a field patch: {"semantics":{"elements":{"figure":{"supplement":{"en":"Fig. "}}}}}.
Per node: heading(1, {numbered:false}), note({role:'endnote'}), entry({role:'index', key}).

**replaces**

- engine/src/resolve/resolve.cc:130-231 (per-Kind scan switch)
- engine/src/resolve/resolve.cc:165-181 (role=="figure" numbering and caption)
- engine/src/resolve/resolve.cc:284-291 (display switch on Entry.kind)
- engine/src/resolve/resolve.cc:37-41 (Entry{Kind} as a stand-in for the class)
- engine/src/emit/emit.cc:776-818 and engine/src/render/semantic_html.cc:284-311 (role string tests; T6/T7 read their own sections through SemInfo.cls)
- runtime/src/worker/executor.mjs:116-130 (figure-ness by region name; the builder only builds)
- docs/document-model.md:64 ('figure is a convention' becomes a declared row)
- Parallel registries proposed in the other themes (T2 RoleInfo/$.role, T7 RoleTable/ROLE_DECL, T6 role→BoxModel Config table, T8 $.numbering) collapse into sections of this row

### Semantic declarations: hoisted definition records and positional event nodes

**owner_layer**

Transport is T2's single declaration channel (versioned, domain-tagged header records). T3 owns the 'semantics' record vocabulary, its validation, and the meaning of the `event` kind. Phase 0 lives in engine/src/semantic/schema.cc and runs before instantiate.

**purpose**

Give a document the same declaration power as the built-ins, with no definition depending on where it is written. The few operations that really are positional stay positional, per occurrence.

**definition**

```
(a) DEFINITIONS: what ∈ element | counter | collector | counter-system.
  - Position-free header records with name-keyed typed scalar fields; nested maps are nested records.
  - Templates are node-valued fields (raw node ids, child-id < record), instantiated STYLE-NEUTRAL outside the style schedule.
  - Canonical records are structured: {counter:{name:'thm', within:'heading', depth:1}}, {where:'prepend', at:'first-para'},
    {placement:'section-end', depth:1}.
  - String sugar such as 'heading@1' or 'prepend:first-para' is parsed only by the JS stdlib (T2).
    The engine parses exactly one mini-language: NumberingPattern.
  - Limits: record depth ≤ 6, ≤ 256 fields per record, ≤ 512 nodes per template. A violation gives decl-invalid; the record
    is dropped and the buffer is still accepted. An unknown `what` or field gives a diag and is ignored.
  - Field-level overlay (see the registry). If an instance precedes its class's definition, diag decl-after-use (info).
  - The one binding rule: semantics are hoisted and document-global; the constructor that $.element returns is an ordinary
    JS value bound where it is written. #!name regions do not need it.

(b) POSITIONAL EVENTS: KIND event, level-neutral like comment and non-rendering.
  - Args: {counter, set:[…] | step:n | add:n, numbering, supplement}.
  - Built by the stdlib ctor counterUpdate(name, ops) (alias $.counter.update) and RETURNED as a content value.
  - Placed where it is spliced: #appendix() inside a paragraph, a region body, or a #let value emitted twice (= two events).
  - Instantiated per occurrence. LOCATE applies it in pre-order; MATERIALIZE B2 deletes it.
  - A paragraph whose children are only events and empty text is vacuous. T2's level normalization hoists its events to block
    level and drops the paragraph; until then, B2's vacuous-paragraph rule does it (the generalized emptyPara).
    pids are assigned after B2, so they are unchanged.
  - An event that no EMIT root reaches gets diag event-unplaced (Phase 0 scans RawOps). This catches the statement form
    #{ $.counter.update(…) }, whose value is discarded.
```

**surface**

#let appendix = () => counterUpdate('heading', {set:[0], numbering:'A.1', supplement:{term:'appendix'}})
#appendix()
#{ $.element('theorem', {...}); $.counter('thm', {within:'heading', depth:1}); $.collector('theorems', {...}) }

The host config 'semantics' section produces the same definition records (T4/T9 settings ABI).

**replaces**

- docs/design-decisions-v2.md:238 (computed numbering in user JS; superseded, see I4)
- engine/src/ops/ops.def:7-73 (no way to declare a class, counter, form or collector)
- engine/src/api/wasm_api.cc:39 (tsr_doc_new parses no config; the semantics section arrives as records)
- engine/src/resolve/resolve.cc:469-485 (emptyPara hack, generalized into the vacuous-paragraph rule)

### Counters + NumberingPattern

**owner_layer**

L4 Semantics, engine/src/semantic/numbering.{h,cc}

**purpose**

One counter automaton (by-level or flat, within/scope reset, gap policy, keyed stepping, positional events) and one pure formatter. It replaces four ints, a section stack, the citation ordinal map and the list-marker formatter.

**definition**

```
struct CounterDef { StrRef name; enum:u8 { Flat, ByLevel } shape=Flat; StrRef levelArg;   // ByLevel: depth from a node arg ('level')
  CounterId within=kNone; u8 withinDepth=1; StrRef withinSep=".";
  ClassId scope=kNone;                       // restart at each instance of this class (enum items restart per list)
  enum:u8 { Zero, One } gap=Zero; std::vector<i32> start;
  bool keyed=false;                          // steps once per distinct key (citation ordinals)
  PatternId pattern; };
struct CounterState { std::vector<i32> v; PatternId pattern; TermRef supplementOverride; };
struct Comp { CounterId c; i32 v; PatternId p; };
struct Snapshot { SmallVec<Comp,4> comps; TermRef supplement; };
// A Snapshot holds the whole within-chain, each component tagged with the pattern in effect at LOCATE time.

step(c, depth) for ByLevel (Flat counters use depth 1):
  if v.size() < depth { pad positions v.size()..depth-2 with (gap==Zero ? 0 : 1); push 0 } else v.resize(depth);
  v[depth-1]++;
  then reset every counter d with d.within==c && d.withinDepth>=depth.
- Zero reproduces today exactly: levels [1,3] give 1, 1.0.1, and [2,1] gives 0.1, 1 (resolve.cc:138-145).
- One fills a gap with an implicit parent, so numbers stay unique: [1,3,2] gives 1, 1.1.1, 1.2, and [2,1] gives 1.1, 2.

format(snapshot):
- the parents are formatted with each parent's own pattern at snapshot time and joined by withinSep;
- then the own components follow, in the counter's pattern;
- the class frame template wraps the result.
There is one pattern source: the counter's current, positionally updatable pattern. A class only frames it.
Events apply set/step/add, pattern and supplement changes at their position in LOCATE.

NumberingPattern, the engine's only parsed mini-language:
- Typst-compatible: '1.1', 'A.1', 'I', '①', '(1a)';
- plus '{name}' for a declared system: counter-system record {symbols:[…], mode: numeric|alphabetic|cyclic|additive|fixed};
- the built-in symbols 1 a A i I ① 一 * are predefined systems; ① overflows to arabic after 50;
- formatting is pure and integer-only.

Keyed counters replace the special citation ordinal: counter 'cite' {keyed:true}. Giving it within or scope restarts it,
which gives per-chapter bibliographies (biblatex refsection).

Ordered lists:
- built-in classes olist {select:[{node:'list', ordered:true}]} and enum-item {select:[{node:'item', inside:'olist'}]},
  with counter 'enum' {scope:'olist'};
- the list's `start` arg sets the start, and its `numbering` arg sets the scoped pattern (default '1.');
- the formatted marker goes to SemInfo.number, which T6 renders instead of emit.cc:517. List items become referenceable.
```

**surface**

$.counter('heading', {gap:'one'})                              // 1.1.1 instead of 1.0.1
$.counter('equation', {within:'heading', depth:1})              // (A.3) after an appendix, (2.3) before
$.counter('chapter', {}); $.counter('section', {within:'chapter'})   // chapters run on across parts
$.counter.system('stems', {symbols:['甲','乙','丙','丁','戊','己','庚','辛','壬','癸'], mode:'cyclic'})
#appendix()     (an event value, see Declarations)
heading(1, {numbered:false})[Introduction]
list({ordered:true, numbering:'(a)'}, item({label:'step-b'}, …))   → @step-b gives '(b)'

Host JSON: "semantics":{"counters":{"figure":{"within":"heading","depth":1}}}. This makes document-model §11 'counters' real.

**replaces**

- engine/src/resolve/resolve.cc:61-62 (secc, tableNo, figNo, eqNo)
- engine/src/resolve/resolve.cc:66 (noteNo)
- engine/src/resolve/resolve.cc:133-145 (1..6 clamp, zero-fill, to_string, '.' join)
- engine/src/resolve/resolve.cc:184-193 (labelled-only eqNo and '(' n ')')
- engine/src/resolve/resolve.cc:234-241 (citeOrdinal in rewrite order)
- engine/src/resolve/resolve.cc:359 and :373 (note number recomputed as i+1; the list marker IS the number)
- engine/src/emit/emit.cc:516-517 (second number formatter for ordered lists)
- docs/document-model.md:124 and :322 (counter classes / resetAt, specified but missing)
- docs/notes-design.md:34-36, 72-74 (footnote reset, noteMarks)

### Templates + Materializer

**owner_layer**

L4 Semantics, engine/src/semantic/materialize.{h,cc}. Style composition calls T4's exported pure fold. Built-in templates are rows in engine/data/elements.json, in the user data form.

**purpose**

All synthesized presentation is declared, style-neutral content with typed slots, instantiated at a declared span and style source. This covers ref items and groups, sites (caption prefix, theorem head, proof end mark, equation tag, footnote marker, term line), collector heads, entries and wrappers. The resolver keeps no literals other than diagnostics and never writes an absolute Styling.

**definition**

```
A template is a content subtree, instantiated style-neutral.

Template-only kinds (outside a template they become error nodes):
- slot{name, or?, pattern?}. Names: number | supplement | title | body | label | key | ordinal | extra | occurrence-mark |
  counter:<name> | field:<name> | term:<key>. `or` names a fallback slot used when this one is empty:
  slot('extra', {or:'supplement'}).
- when{of:<slot>}[kids]: rendered only if that slot is non-empty.
- each{of:'items'|'occurrences', sep}[kids]: once per item of a group ref or collector, or once per marker of a flow item.

Reused kinds:
- link{to:'target'|'item'|'site'|'occurrence'} and ref{to:…} materialize as today's link (url) and resolved ref
  (target, label, url) shapes, with args in today's order.
- A template ref/link with a free `target` gets diag template-ref.
- styled{…} inside a template is a delta carrier. It is flattened, and its delta applies once to every node of its subtree,
  slot-inserted content included. So strong(slot('supplement'), slot('number')) gives today's bare text[BOLD] '图 1：', and the
  notes entry gives today's per-node x0.85 on the body (resolve.cc:352-357, 377-383).

struct TemplateUse { TemplateId t; enum:u8 { UseSite, Instance, Row, Collector } spanFrom, styleFrom; };
Defaults:
- ref forms: UseSite;
- sites: the attach point;
- flow entries: Instance (notes/explicit.tree.txt: item @[37,44) is the note's span);
- row entries: Row (bib para span and style, resolve.cc:420-423);
- collector head and wrap: Collector.

style(node) = T4::fold(sourceStyle, sourceRuleScope, roleTags, deltas outer→inner).
This is a fixed left fold that reproduces today's float products bit for bit (StyleTable keys on exact floats,
model.h:46-73). It never produces an absolute Styling.

Rules:
(1) Adjacent generated text siblings with equal style merge, so '§' + '1' stays one node. Inserted titles and bodies never merge.
(2) Templates instantiated in MATERIALIZE may not contain counted, flow, labelled, collect, event or free-target ref nodes
    (diag template-content; stripped). MATERIALIZE therefore never creates instances or ordinals.
(3) A collector `head` is STATIC: it is instantiated in LOCATE at the collector's position and located like source content,
    so a 'References' heading can be counted and outlined.
(4) A citeable ref always renders through refGroup, as a group of one when needed. '[' n ']' and '[' n ', ' n ']' come
    from one template.
(5) name/full forms that recurse through titles are cut at depth 4 (diag ref-cycle).

cloneTitle(inst):
- copies the title captured in LOCATE, after B1;
- skips synthetic nodes and drops flows, anchors, collect, event and entry nodes;
- turns refs into their resolved text.
Inserted clones are never walked again.
```

**surface**

Template constructors (generated by T2): slot(name, {or, pattern}), when({of}, …kids), each({of, sep}, …kids). Inside templates, link({to}, …) and ref({to}, …) are also allowed.

Used in:
- $.element(name, {sites, ref:{item, group, <form>: …}, flow:{marker, entry}});
- $.collector(name, {head, wrap, entry, empty}).

Per reference: ref(target, {form:'name'|'number'|'full'|'marker'|<user>, supplement}). The sugar @x[…] fills `extra` (T1).

The built-in footnote marker, in the same form: ref({to:'item', anchor:'site'}, styled({script:'super', size:0.7}, slot('number'))).

**replaces**

- engine/src/resolve/resolve.cc:171-180 (absolute-bold caption prefix)
- engine/src/resolve/resolve.cc:193 and engine/src/emit/emit.cc:756 (equation tag as a string arg read by one consumer)
- engine/src/resolve/resolve.cc:212 ('↩' smuggled as a label excerpt)
- engine/src/resolve/resolve.cc:259-267 and :417-423 ('[', ', ', ']', '[n] ')
- engine/src/resolve/resolve.cc:279 ('??')
- engine/src/resolve/resolve.cc:317 (number + ' ' + text)
- engine/src/resolve/resolve.cc:331 and :457 (' — ')
- engine/src/resolve/resolve.cc:342-357 and :370-391 (CLS_SUP x0.7 marker; in-place rescale x0.85)
- engine/src/resolve/resolve.cc:444-467 (buildTerm presentation)

### Index + phased resolver (PHASE 0 → LOCATE → BIND → MATERIALIZE B1/B2/B3), including identity

**owner_layer**

L4 Semantics, engine/src/semantic/index.{h,cc}. resolve.cc becomes the phase driver. The Index is persisted on Doc with spans, not node pointers (document-model §0).

**purpose**

Compute facts before producing output, so that neither tables nor content depend on document position. Register every labelled node uniformly. Give identities that never encode URLs or DOM spelling. Hand downstream stages node data instead of L4 tables.

**definition**

```
PHASE 0 (before instantiate): build the registry from built-in rows, host config and definition records. Validate limits, slot names and cycles. Derive the reserved alias shapes.

INSTANTIATE (L3, with T2 and T4): copy() sets SemInfo.cls = classOf(node, nearest classed ancestor) per occurrence and captures T4's rule scope.

A LOCATE: one read-only pre-order walk. It enters flow bodies where they occur and visits row bodies as data. At each node it:
- applies event nodes;
- if the class is numbered under its policy, steps the counter and stores a Snapshot;
- captures the title source, as a pointer into the still-unmutated tree;
- registers a user label: LabelChar check (T1); a label with a reserved alias shape gives label-reserved; a duplicate gives
  label-duplicate with a related span, and the first wins;
- assigns the alias:
  - prefix + raw components joined by '.', OR the per-class document ordinal (never resets), OR the key;
  - an unnumbered instance gets prefix + '~' + ordinal;
  - a collision after set or reset gets a '~k' suffix;
- records Rows (table, key, occurrences), FlowItems and RefSites. A group ref and its child refs are one RefSite.
After the walk, flow items that no placement and no collector covers are marked unplaced (diag flow-unplaced).

A' BIND: RefSites outside rows and unplaced flows, in document order, so a note body binds at its marker.
- The whole target is tried as a label first (this keeps @[排版]).
- Otherwise each child ref is resolved in turn: label, then citeable row (whose keyed counter steps), else unresolved for that
  child only.
- Then come the rows each table selects (cited, cited-then-all, all). Their refs bind only to labels and to already-selected rows.
- A cite from a row to an unselected row renders its key, with diag row-crossref.
- A counted or flow node inside a row gets diag row-content and is rendered inert.
It is one pass.

B1 RESOLVE CONTENT: every RefSite form, every placed flow body and every selected row body is materialized exactly once, in place. name/full forms use memoized on-demand recursion with the depth budget.

B2 ATTACH: site templates are instantiated at self, first-para, last-para or a part, and marked synthetic. Each flow node then follows its placement:
- collector, end or section-end: the node is replaced by its marker site, and the body is detached into the FlowItem;
- Deferred: the marker and the materialized body stay in place, for T6.
Entry rows take their site or are removed. Event nodes and vacuous paragraphs are removed.

B3 COLLECT: collectors expand in place from the B1/B2 results through cloneTitle. Because cloneTitle ignores synthetic site output, a TOC reads the same before or after its headings. Each flow item is placed once, by the first covering collector in document order; leftovers follow the class placement. The first renderer of a row owns its anchor.

OUTPUT: SemInfo on nodes {cls, inst, number, anchor, targetAnchor, targetCls, flow, part, synthetic}. L6 and L7 read only SemInfo and their own registry sections.

INDEX: instances, labels, rows and flows, with spans, formatted numbers and anchors. It holds no node pointers and is invalid for node lookup after MATERIALIZE. It feeds tsr_labels, the manifests and --stage=index.
```

**surface**

No new markup. Labels use T1's universal `label` option and `<id>` rule.

Editor/host: tsr_labels(doc) returns JSON [{label, class, number, span, anchor}], for completion and go-to-label.

tsrc --stage=index dumps the Index. A post-ingest diags stage is added.

**replaces**

- engine/src/resolve/resolve.cc:43-76 (feature fields in Resolver)
- engine/src/resolve/resolve.cc:119-128 (addLabel, called from five branches)
- engine/src/resolve/resolve.cc:146-155 (TOC text captured during scan; heading-only duplicate fallback)
- engine/src/resolve/resolve.cc:205-215 (synthetic fn-/fnref- entries, the ↩ excerpt)
- engine/src/resolve/resolve.cc:244-258 and :274-276 (comma split, all-or-nothing, silent shadowing)
- engine/src/resolve/resolve.cc:372 (on-demand rewrite of note bodies inside buildNotes)
- engine/src/resolve/resolve.cc:476-515 (rewrite dispatch)
- engine/src/resolve/resolve.cc:520-530 (positional aggregate init at :523; implicit append at :526-529)
- engine/src/resolve/resolve.cc:113, :263, :292 ("#tsr-" URLs built in the model)
- engine/src/api/doc.h:70-75 (tables discarded after ingest)

### Collections: queries over classes, keyed tables and flows

**owner_layer**

L4 Semantics, engine/src/semantic/collect.{h,cc}

**purpose**

One collector abstraction for toc, lof/lot, glossary, notes, bibliography, index, list of theorems and per-chapter endnotes. Keyed tables generalize term, bibliography, index and imported-label rows. Flows generalize footnote lifting, for inline or block items with any number of markers; each item is placed exactly once or handed to the page builder.

**definition**

```
struct Query {
  enum:u8 { Classes, Table, Flow } src; std::vector<ClassId> classes; StrRef table, flow;
  i8 maxDepth=-1; bool outlineOnly=false, labelledOnly=false;
  enum:u8 { Any, Cited, CitedThenAll } cited=Any;                    // replaces form:'all'
  enum:u8 { Doc, Enclosing } scope=Doc; ClassId scopeClass; u8 scopeDepth;
  enum:u8 { Document, FirstUse, Key, SortKey } order=Document; enum:u8 { Flat, ByDepth } nest=Flat; bool groupByKey=false; };
struct CollectorSpec { StrRef name; Query q; TemplateUse head /*static*/, wrap /*contains each{of:'items'}*/, entry, empty; };
struct Row { StrRef table, key, sortKey, source; const ContentNode *title, *body; u32 order;
             std::vector<u32> occurrences; Fields fields; };
// Rows come from entry{role, key, sortKey?, source?, title?}[body] nodes anywhere (order-independent data).
// The key is normalized text; the title stays content.
// A multi table keeps several rows per key (index entries; literate fragment definitions '+≡').
struct FlowItem { StrRef flow; u32 inst, scopeInst; std::vector<u32> occurrences; bool placed; };
// occurrences = the defining site + every ref{form:'marker'} to its label (named notes). each{of:'occurrences'} renders '↩ a b'.
enum class Placement:u8 { CollectorOnly, End, SectionEnd /*depth*/, Deferred /*page|margin*/ };
// Deferred: the resolver numbers and materializes the item but leaves marker and body in place, tagged SemInfo.flow,
// for T6's page builder to place as an insert. When the settings say flow (unpaged) output, Deferred falls back to End.

Built-in presets (data):
- toc{Classes[outline], ByDepth}; lof{Classes[figure]}; lot{Classes[table]};
- glossary{Table glossary}; notes{Flow notes};
- bibliography{Table bib, Cited, FirstUse}; index{Table index, groupByKey, SortKey}.

Today's shapes are reproduced. For example, notes =
  group{role:'notes'}[rule, list{ordered}[each items:
    item[para{anchor:item}[styled{size:0.85}[slot body, ' ', ref{to:'site'}[term:backref]]]]]].
Entry markers come from the item's snapshot (SemInfo.number on the item), never from list position (resolver/missed:4).
```

**surface**

#toc()  toc({maxDepth:2})  #lof()  #glossary()  #notes()  #index()
#bibliography("refs.json", {all:true})   // returns its collector IN PLACE; the executor emits entry{role:'bibentry'} rows
#{ const theorems = $.collector('theorems', {select:['theorem','lemma'],
     entry:[link({to:'target'}, slot('supplement'), slot('number')), ' ', slot('title')]}) }
#theorems()
entry({role:'index', key:'monad', sortKey:'monad'})[monad]
note({role:'endnote'})[…]
#note(label: "src")[…] with ^[src] markers (T1 surface)

**replaces**

- engine/src/resolve/resolve.cc:296-322 (buildToc and its minLevel=7 sentinel)
- engine/src/resolve/resolve.cc:324-336 (buildGlossary over private gloss strings)
- engine/src/resolve/resolve.cc:362-394 (buildNotes, notesPlaced, shared-pointer aliasing)
- engine/src/resolve/resolve.cc:400-429 (buildBibliography, form:'all')
- engine/src/resolve/resolve.cc:431-440 (the `what` switch and collect-unknown)
- engine/src/resolve/resolve.cc:197-203 (bib scan of any kid with a `name`)
- runtime/src/worker/executor.mjs:176-189 (one ctor per collector; the text('') placeholder)
- runtime/src/worker/executor.mjs:241-266 (bibliography emitted at document end as group{role:'bibentry'})
- tools/convert/wiki2tsm.mjs:46-59 (named refs duplicate their bodies)
- engine/src/ops/ops.def:41 (note kind as a one-feature lift; it stays as the inline flow-item kind, and its class decides the flow)

### LocaleTerms consumer (TermRef) over T4's LocalePack

**owner_layer**

T4 owns LocalePack: the term tables, the BCP-47 fallback chain, $.terms, $.doc, and termsLang in the settings cascade. T3 owns the semantic term keys and calls terms.get(key, termsLang(source node)) from engine/src/semantic/materialize.cc.

**purpose**

Localized words (supplements, separators, the back-reference glyph, section titles) become data chosen by a declared terms language. This replaces the zh/ja-vs-English switch.

**definition**

```
using TermRef = variant<TermKey, InlineTerms /*{en:'Theorem ', zh:'定理 '}*/>;

termsLang(node) = the T4 setting termsLang in the doc/region/block cascade ?? the document lang ($.doc) ?? the host default (tsr_set_lang; 'zh').
It is deliberately NOT Styling.lang. That is the per-run 'locl' glyph tag (model.h:33), and a zh-TW glyph scope inside a zh-Hans document must keep 图.

Fallback chain: exact → script (zh-TW/HK/MO → zh-Hant; zh, zh-CN, zh-SG → zh-Hans) → lang → root.
Root = en, which keeps today's behaviour for every non-CJK language (config.h:96-100).

Keys T3 needs: figure, table, equation, section, part, chapter, appendix, footnote, theorem, lemma, proof, caption-sep, ref-sep, range-sep, unresolved, backref, term-sep, notes-title, references-title.

Built-in tables:
- zh-Hans: today's strings, byte-identical;
- zh-Hant: 圖;
- ja: 図;
- en: today's applyLang English.
```

**surface**

$.terms('de', {figure:'Abbildung ', table:'Tabelle ', 'caption-sep':': '})   // T4
$.doc({lang:'de'})                                                      // T4
#!aside(settings: {termsLang: 'en'}) … #aside!                          // T4 cascade
supplement:{term:'theorem'} or supplement:{en:'Theorem ', zh:'定理 '}

**replaces**

- engine/src/api/config.h:66-71 (four supplement fields plus capSep)
- engine/src/api/config.h:89-102 (applyLang two-way switch: ja and zh-Hant get 图)
- engine/src/api/wasm_api.cc:63-65 (tsr_set_lang as the only language source)
- engine/src/resolve/resolve.cc:212, :279, :331, :457 (↩, ??, — literals)
- zball-io/eleventy.config.js:29-32 (front-matter regex to obtain lang)

### LabelManifest: cross-document labels as imported rows

**owner_layer**

L4 Semantics, engine/src/semantic/manifest.{h,cc}. Loading goes through T9's resource protocol; #use is T2's.

**purpose**

Let multi-file books and blog series resolve references across documents. No single document's resolve becomes multi-pass, and no second label path exists beside citeable rows.

**definition**

```
EXPORT (tsr_label_manifest): Index → deterministic, versioned JSON, sorted by label:
{'v':1,'doc':'ch2','labels':[{'label':'sec-ua','class':'section','comps':[[2,'1'],[10,'1']],'title':'Univalence','anchor':'sec-ua'}],'counters':{'chapter':[2]}}
Each component carries its pattern id, so the importer can format it.

IMPORT: $.labels.import(src) loads through T9's resource protocol. The executor emits entry{role:'external', key:label, doc, href, class, comps, title} rows into table 'external' {citeable:true}. These rows are recorded in .ops, so native goldens need no file I/O.
- There is no separate External label path. Lookup is label first, with ref-shadowed when a local label hides a row, exactly as for citations.
- If the importing document has the class, its own ref templates and terms format the components, so supplements follow the importer's language. Otherwise the row falls back to its exported title and the pre-formatted number.
- Links go to href + T7's AnchorNamer(anchor).

PROJECT MODE (tools/export-static, renderTsm):
- compile every document, export manifests, then recompile with the imports;
- repeat until every manifest is byte-stable, at most 3 rounds (diag project-unstable); titles that cite other chapters settle in round 2;
- chapter offsets come from counter events (set:[n]).
```

**surface**

#{ $.labels.import('ch1.labels.json') }
renderTsm(src, {project:{manifests:[…], chapter:3}})
tsr_label_manifest(doc)

**replaces**

- engine/src/resolve/resolve.cc:50 and :523 (label table local to a single document's resolve)
- tools/convert/pbr2tsm.mjs:496 (internal links dropped)
- tools/convert/tex2tsm.mjs:183 (only the first \cref key kept)
- examples/real-world/hott-introduction.tsm (57 cross-chapter refs render '??'; disclosed in zball-io/src/docs/example-hott.tsm:221)

## Subsumption (finding → mechanism)

- **subsumed** by *Element registry (+ Counters)*: `markup-language/numbered-env-hardcoding`, `codegen-ops-model/role-string-dispatch`, `resolver/figure-role-string`, `real-world-evidence/role-figure-hardwired`, `resolver/missed:3`
  Membership:
  - `role` carries the class on every kind, and selectors are typed conjunctions.
  - Built-in ctors write no new arg, so tree goldens do not churn.
  - The `class` ARGK is dropped, which leaves `class` to T4's style tags.

  numberAs and absorb are deleted. Both are selector rows plus `like`:
  - table-figure {group, role:figure, kind:'table'} numbers with the table counter;
  - figure-table {table, inside:figure} is unnumbered with refersTo:'enclosing', so @tab-in gives '表 1' for the figure (missed:3).

  One row replaces five proposed registries:
  - the T7/T6/T4 traits are sections of the same row;
  - the Category enum is deleted, and T7's html section maps elements;
  - T2's $.role and T8's $.numbering fold into $.element/$.counter.

  Labelability is universal. The sidecar-lines role is T2's codeblock attribute. T2 provides the public group/figure/table ctors.
- **subsumed** by *Counters + NumberingPattern (counter half); Collections (collector half of collector-switch-and-fixed-counters)*: `resolver/fixed-counter-set`, `codegen-ops-model/collector-switch-and-fixed-counters`, `resolver/numbering-format-hardcoded`, `real-world-evidence/counters-fixed-fields`, `math/equation-numbering`
  Counters:
  - One pattern source: the counter's current, positionally updatable pattern.
  - Snapshots tag every within-chain component with its pattern, so after an appendix, equations print (A.3) and refs, TOC and headings agree.
  - gap zero|one is defined structurally and tested.
  - Keyed counters replace citation ordinals and allow refsection-style restarts.
  - List items are a scoped counter, so emit's second formatter goes.

  Equations:
  - Equations keep the 'labelled' policy.
  - The frame '(…)' is data.
  - The tag is a Site{where:Tag}.

  This supersedes design-decisions-v2.md:238, argued under I4.
- **subsumed** by *Index + phased resolver (identity: universal registration, aliases from raw components or ordinals, shape-based reservation, anchors instead of URLs)*: `markup-language/universal-labels`, `markup-language/label-namespace-collision`, `resolver/label-registration-per-kind`, `resolver/anchor-namespace`, `real-world-evidence/missed:1`
  Every labelled node registers.

  Aliases are built from raw integer components (h-1.1) or per-class document ordinals (fn-12), never from formatted numbers. Reservation is therefore shape-based and independent of patterns:
  - prefix + ~?digits(.digits)*(~digits)? for the h-, fn- and fnref- prefixes;
  - the whole bib- prefix.
  h-index stays a legal user label.

  A reset or set cannot collide: the '~k' suffix handles that. Unnumbered instances still get anchors (h-~3), so TOC links survive numbered:false.

  Accepted residue, by the v2 §11.1 design: term keys and citeable row keys are author-chosen names in the one namespace, guarded by label-duplicate and ref-shadowed.

  Surface syntax is T1's. DOM spelling is T7's AnchorNamer.
- **subsumed** by *Templates (refItem/refGroup/forms; slot extra with `or`) + BIND over RefSites + Collections (bib table, keyed counter)*: `markup-language/reference-forms-closed`, `resolver/ref-display-switch`, `real-world-evidence/ref-cite-format-in-cpp`, `resolver/citation-path`
  Grouping:
  - `,` stays the target separator.
  - T1 lowers @[a, b] to a parent ref with child refs, and the parent plus its children form one RefSite.
  - BIND tries the whole target as a label first, then each child, so mixed label/citation groups work.

  Forms and extra:
  - A single citeable ref renders as a group of one, so '[' n ']' and '(Knuth 1981, p. 33)' come from one group template.
  - The class Extra enum is replaced by two slots: slot('extra', {or:'supplement'}) gives Typst behaviour, and slot('extra') inside a when gives a Pandoc locator.
  - `form` is honoured.

  Author-year text is precomputed into row fields. There is no post-resolve JS.
- **subsumed** by *Collections (queries + keyed tables + flows) + Templates + staged MATERIALIZE*: `markup-language/collectors-closed`, `resolver/collector-what-dispatch`, `codegen-ops-model/note-kind-and-lift`, `resolver/footnote-pipeline`, `codegen-ops-model/bibliography-placeholder-and-end-emission`, `resolver/term-rewrite`, `resolver/missed:4`
  Bibliography and staging:
  - #bibliography returns its collector in place. The executor emits order-independent entry rows as trailing roots, and BIND completes ordinals before B3.
  - notesPlaced, the end-of-document contract and emptyPara go away.
  - Because B1 materializes every flow and row body once, a #notes() placed before its notes sees resolved bodies.

  Flows:
  - Each item is placed exactly once.
  - An item has an occurrences list, which gives multi-marker named notes.
  - An item may be a block.
  - Deferred placement hands items to T6's page builder instead of having it disassemble collector output.

  Numbering and terms:
  - Entry markers come from snapshots (missed:4).
  - term becomes entry{role:'term'} with a site template.
- **subsumed** by *Templates + Materializer (delta carriers, span/style sources, fixed left fold) + staged MATERIALIZE (frozen titles)*: `codegen-ops-model/resolver-fabricated-styles`, `resolver/site-display-injection`, `resolver/presentation-constants`, `resolver/excerpt-strings`, `resolver/missed:1`, `resolver/argk-overloading`
  Sites attach only in B2, so LOCATE is read-only, and B3's cloneTitle excludes synthetic site output.

  The equation tag becomes content, through Site{where:Tag}, which both serializers walk. S0 already prints the compat string on the semantic page.

  argk-overloading:
  - `name` stops being a resolver output, and form:'all' becomes cited:'cited-then-all'.
  - Template operations are distinct kinds (slot/when/each), not slot{name:'if'|'link'|'items'}.
  - The child-role arg is `part`, not a second meaning of `slot`.
  - Per-kind arg namespaces are T2's schema.
- **subsumed** by *LocaleTerms consumer (TermRef supplements, termsLang) over T4's LocalePack*: `resolver/supplement-config`, `api-measure-code/supplements-and-lang`, `real-world-evidence/locale-terms-switch`
  Ownership:
  - Supplements become TermRefs on classes, or per-counter overrides from events (an appendix switches to 'Appendix').
  - T4 owns the tables, the fallback chain, $.terms, $.doc and termsLang.
  - T3 owns only the key set.

  Behaviour:
  - Root is en.
  - termsLang is decoupled from the glyph-level Styling.lang.
  - Per-level heading words come from per-level heading classes (typed level selectors), not from a supplement list per depth.
- **subsumed** by *LabelManifest*: `real-world-evidence/no-cross-document-labels`
  Imports become citeable rows recorded in .ops, so per-document resolve stays single-pass and goldens stay deterministic. Export carries raw components with pattern ids, which the importer formats with its own terms. Project mode iterates a bounded number of times (≤3) until the manifests are stable.
- **owned-by-other-theme** by *T2-constructor-ir*: `resolver/rewrite-normalizations`, `resolver/missed:0`
  Assumed from T2:
  - a KindInfo.level table, and level normalization in instantiate that treats `event` as level-neutral like comment;
  - that normalization hoists a vacuous paragraph's events to block level;
  - public group/table/figure/collect/entry ctors.

  T3's part:
  - emptyPara is replaced by B2's vacuous-paragraph rule until T2 lands;
  - the para-unwrap (resolve.cc:486-492) stays in the driver until then.
- **owned-by-other-theme** by *T4-style-settings (with T5 for the no-break part)*: `codegen-ops-model/cls-sup-feature-bit`, `real-world-evidence/sup-attach-private`, `real-world-evidence/presentation-constants`
  Assumed from T4:
  - a paint-only `script` property and a relative `size` in StyleDelta;
  - a `style` section in element rows;
  - role-keyed rules;
  - CSS generated from one table.

  T3's part: built-in templates carry deltas as data (marker {script:super, size:0.7}, caption label {weight:bold}). Until T4's properties exist, S1's built-in rows encode the same delta with today's CLS_SUP/CLS_BOLD bits and sizeMul. Heading sizes, list indents and table padding are T4/T6.
- **owned-by-other-theme** by *T5-text-shaping*: `emitter/sup-bit-attach-rule`
  Assumed from T5: a generic bind (no-break before/after) attribute on any inline, and a public `fill` item. The marker template sets bind:'prev' as data, which removes emit.cc:89-90 (CLS_SUP → BREAK_INF). The proof end mark uses `fill`.
- **owned-by-other-theme** by *T6-layout-pagination*: `emitter/figure-role-string-dispatch`, `break-layout-pages/group-role-dispatch`, `emitter/anchor-opt-in-per-kind`, `real-world-evidence/figure-model-single-image`
  Assumed from T6:
  - caption layout on direct part:'caption' children instead of figDepth (emit.cc:471-476);
  - float placement on any block unit;
  - BoxModel and keep traits as the `layout` section of element rows;
  - anchors as item attributes in every sub-flow;
  - a generic margin tag;
  - SemInfo.number markers on items;
  - a page builder for Deferred flow items.

  T3 provides: part structure, subfigure numbering (class subfigure, counter within figure, pattern '(a)'), table-figure/figure-table rows, and SemInfo. The float rule keys on the declared caption part, not on the presence of an image.
- **owned-by-other-theme** by *T7-render-runtime*: `render-runtime/semantic-role-switch`, `render-runtime/anchor-namespace`, `render-runtime/shell-note-popups`, `real-world-evidence/notes-popups-dom-scraping`, `resolver/missed:2`
  Assumed from T7:
  - the `html` section of element rows, which replaces T3's former Category enum;
  - AnchorNamer (default prefix 'tsr-', stable per document);
  - a Behavior registry keyed on data-flow / data-ref-class;
  - an anchor-closure check in the golden runner.

  T3 provides:
  - SemInfo anchor/targetAnchor/targetCls/flow on nodes, so nothing parses 'fn-' or '.tsr-sup' (shell.mjs:146, 175);
  - flow-item occurrences for previews of any reference kind.
- **owned-by-other-theme** by *T1-surface-frontend (list form surface), with T6 (hanging/run-in layout) and T7 (dl/dt/dd)*: `real-world-evidence/description-list-missing`
  term stays reference-shaped (design-decisions-v2.md:240). A description list is a list shape with an item term part.

  T3's part: term presentation becomes a site template. A description item with role 'dterm' also defines a glossary row (table glossary, attach none), so glossary membership is orthogonal to list layout.
- **owned-by-other-theme** by *T1-surface-frontend*: `markup-language/footnote-sugar-oneoff`
  The ad-hoc parts of this item are T1's:
  - the private ^[ scanner, which ignores escapes and code spans;
  - the spacing relocation;
  - the sigil family.

  T3 keeps only the meaning: the footnote class (counter, marks pattern, flow, placement), ^[label] lowered to ref{form:'marker'}, and the named-note join through universal labels and occurrences. T3 no longer lists this item as fixed by its own migration.
- **owned-by-other-theme** by *T2-constructor-ir (figure builder marks caption parts) with T7-render-runtime (serializer maps parts to figcaption)*: `real-world-evidence/missed:5`
  The guesswork is in the builder and the serializer. T3 provides only the part contract: part:'caption', site attach at part caption, and TitleSource Part. It no longer claims this item as subsumed.
- **bug-fix-only** by *Migration S0 (with T9 for stage dumps)*: `resolver/missed:5`
  Three observability fixes:
  - the generated style-bit name table keeps today's spellings and order (model.cc:118-126), appends SUP, and is used only by the tree dump;
  - a post-ingest diags stage;
  - --stage=index.

## User extension examples

### HoTT-style theorems and lemmas sharing one counter, numbered per chapter, referenceable in several forms

**Today**

`#!theorem(label: "thm-ua")` becomes group{role:'theorem'} (executor.mjs:130), which is never registered (resolve.cc:165-170). `@thm-ua` renders '??' while semantic_html still emits id="tsr-thm-ua". The only workaround routes through the figure builder, which gives '图 1：', centred captions (emit.cc:471-476) and <figure>. `#ref("thm-ua", "name")` silently drops its second argument (executor.mjs:173).

**After**

Declare the theorem and lemma classes as in the registry surface. Then
`#!theorem(label:"thm-ua", title:"Univalence") … #theorem!`
renders '**Theorem 2.10** (Univalence)**.** …' in the region's first paragraph. References:
- `@thm-ua` gives 'Theorem 2.10';
- `@thm-ua[Thm.]` gives 'Thm. 2.10', through slot('extra', {or:'supplement'});
- `#ref("thm-ua", {form:"name"})` gives 'Univalence'.
A following #!lemma continues the shared counter as Lemma 2.11. Box and HTML element come from the same row's layout/html sections. No engine change.

### Part / Chapter / § numbering as in the HoTT book (chapters numbered continuously across parts)

**Today**

There is one hierarchical heading counter (resolve.cc:133-145). With parts at level 1, chapters become 1.1, 1.2, …, and every ref prints '§'. Selectors cannot test the numeric `level`. The HoTT introduction refers to @part-foundations, @cha-homotopy and @sec-… (hott-introduction.tsm:179-195).

**After**

#{ $.element('part', {select:[{node:'heading', level:1}], counter:'part', numbering:'I', supplement:{term:'part'}, outline:true})
   $.element('chapter', {select:[{node:'heading', level:2}], counter:'chapter', supplement:{term:'chapter'}, outline:true})
   $.element('section', {select:[{node:'heading', level:3}], counter:{name:'section', within:'chapter'}, supplement:{en:'§'}, outline:true}) }
Refs give 'Part I', 'Chapter 8' and '§2.10'. Chapters keep counting across parts because the chapter counter is not within part. The TOC nests by heading depth.

### Appendix lettering that propagates to equations and refs; unnumbered chapters; visible heading numbers; no '1.0.1'

**Today**

Headings are always decimal and zero-filled (resolve.cc:138-145). Numbers never appear at the heading itself. There is no \appendix or \chapter*. A JS counter cannot reach refs.

**After**

#let appendix = () => counterUpdate('heading', {set:[0], numbering:'A.1', supplement:{term:'appendix'}})
$.counter('equation', {within:'heading', depth:1}); $.element('equation', {numbering:{frame:['(', slot('number'), ')']}})
$.element('heading', {sites:[{where:'prepend', at:'self', template:[slot('number'), ' ']}]})
$.counter('heading', {gap:'one'})
`#appendix()` at the appendix.

Results:
- appendix headings show 'A', equations inside show '(A.3)', and refs read 'Appendix A' and '式 (A.3)';
- `heading(1, {numbered:false})` keeps its h-~n anchor;
- the TOC, refs and headings agree, because all three format the same pattern-tagged snapshot;
- a TOC placed after the headings does not repeat the attached number, because cloneTitle skips synthetic nodes.

### A list of theorems, and the documented list of figures

**Today**

Any `what` other than toc/glossary/notes/bibliography gives collect-unknown and an empty group (resolve.cc:431-440). lof is promised at document-model.md:127 and missing.

**After**

`#lof()` is a preset. A user collector:
$.collector('theorems', {select:['theorem','lemma'], head:[heading(2, {numbered:false}, slot('term:theorems-title'))], entry:[link({to:'target'}, slot('supplement'), slot('number')), ' ', slot('title')], wrap:[list({ordered:false}, each({of:'items'}))]})
`#theorems()` expands in place with cloned titles. Its static head is a real, outlined heading.

### A proof environment ending in a flush-right ∎

**Today**

Impossible. There is no append position, no end-of-last-paragraph site and no fill item.

**After**

$.element('proof', {sites:[
  {where:'prepend', at:'first-para', template:[em(slot('term:proof')), '. ']},
  {where:'append', at:'last-para', template:[fill(), '∎']}]})
`fill` is T5's public hfil item. `#!proof … #proof!` then works with no engine change.

### Per-chapter circled footnotes, Wikipedia-style named notes with several markers, a user endnote flow, and page-bottom notes in paged output

**Today**

One `note` kind; noteNo never resets; marks are std::to_string (resolve.cc:205-215, 362-394). wiki2tsm duplicates a named ref's body for every reuse (wiki2tsm.mjs:46-59). A second #notes() aliases the bodies at x0.72.

**After**

$.counter('footnote', {within:'chapter'}); $.element('footnote', {numbering:'①'})

Anchors and named notes:
- Aliases stay fn-<ordinal>, so `@fn-12` remains unique across chapters.
- `#note(label:"src")[…]` with markers `^[src]` (T1 lowers them to ref{form:'marker'}) builds one item with three occurrences. Its entry renders '↩ a b c' through each({of:'occurrences'}).

An endnote flow:
$.element('endnote', {select:[{node:'note', role:'endnote'}], numbering:'i', flow:{flow:'endnotes', placement:'collector-only'}}); $.collector('endnotes', {flow:'endnotes', scope:'enclosing', scopeClass:'chapter'})
Placed once per chapter.

Paged output: the footnote class sets placement:'deferred', and T6's page builder places the items as sheet-bottom inserts. Numbers stay document-determined (I2).

### German declared by the document; correct Japanese and Traditional Chinese supplements

**Today**

tsr_set_lang('de') falls into applyLang's else-branch, giving 'Figure '. ja and zh-TW both get 图 (config.h:89-102). The blog regex-parses front matter to find the language (eleventy.config.js:29-32).

**After**

#{ $.terms('de', {figure:'Abbildung ', table:'Tabelle ', equation:'Gl. ', 'caption-sep':': '}); $.doc({lang:'de'}) }   // T4's LocalePack
ja gives 図 and zh-Hant gives 圖. An English sidebar sets settings:{termsLang:'en'}, while a zh-TW glyph scope inside a zh-Hans document keeps 图.

### Author-year citations with locators, and mixed label/citation groups

**Today**

'[n]', ', ' and '[n] ' are C++ literals (resolve.cc:259-267, 423). `@[sec-a, kp81]` and `@[kp81, nosuch]` render '??' (resolve.cc:256-258). There are no locators.

**After**

$.bib.fields = (e) => ({citeText: e.author[0].family + ' ' + e.issued['date-parts'][0][0]})
$.element('bibentry', {ref:{item:[link({to:'target'}, slot('field:citeText')), when({of:'extra'}, ', ', slot('extra'))], group:['(', each({of:'items', sep:'; '}), ')']}})
Results:
- `@kp81[p. 33]` gives '(Knuth 1981, p. 33)', a group of one;
- `@[kp81, liang83]` gives '(Knuth 1981; Liang 1983)';
- `@[sec-a, kp81]` renders each child through its own class;
- an unknown key degrades only its own item, with a diagnostic naming that key.

### A table caption: a figure whose body is a table (HoTT <tab-pov>), and references to list items

**Today**

Wrapping a #!table in #!figure numbers the object twice, as 图 1 and 表 1 (resolve.cc:158-170). Ordered-list numbers exist only in emit (emit.cc:517), so items cannot be referenced.

**After**

With the table-figure and figure-table rows from the registry surface, `#!figure(kind:"table", label:"tab-pov")` shows '表 1：Comparing…'. Both `@tab-pov` and a label on the inner table give '表 1' (refersTo enclosing).

`list({ordered:true, numbering:'(a)'}, …, item({label:'step-b'}, …))` lets `@step-b` give '(b)'. The marker T6 draws is the same SemInfo.number.

### A back-of-book index with CJK collation, and literate-programming fragment cross-links (pbr)

**Today**

Only `term` enters a table (resolve.cc:216-226). Term names are flattened by shadowText (executor.mjs:67-71). pbr's 73 <<fragment>> uses are highlight tokens only (tokens.mjs:78-107).

**After**

$.element('index', {table:{name:'index', multi:true}})
#let idx = (k, ...t) => entry({role:'index', key:k, sortKey: pinyin(k)}, ...t)
`#index()` groups the rows by key, with back-links to every occurrence. Collation comes from the JS sortKey.

The same multi-row table holds fragment definitions ('+≡'). With T9's opt-in literate overlay emitting entry and ref nodes inside codeblock lines, a 'defined in / used in' collector needs no engine change.

### Cross-chapter references in a multi-file book

**Today**

The HoTT example renders 57 cross-chapter refs (18 targets) as '??'. pbr2tsm drops every internal link (pbr2tsm.mjs:496). `#use` compiles to a ReferenceError.

**After**

Project build: compile every chapter and export labels.json from each. In ch3, `#{ $.labels.import('ch2.labels.json') }`, or the project config, makes `@sec-ua` render '§2.10' in ch3's own language and link to ch2.html#tsr-sec-ua. counterUpdate('chapter', {set:[2]}) from the project config numbers the chapter. Each document's resolve stays single-pass.

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  Every phase is a deterministic function of (instantiated tree, registry):
  - walks are pre-order in document order;
  - hash maps are used only for lookup, never iterated for output;
  - collector order comes from vectors, or from a stable_sort on byte-wise keys with a document-order tie-break;
  - formatting is integer-only;
  - the style fold is a fixed left fold that reproduces today's float products;
  - manifests are sorted;
  - imports, rows and events are recorded in .ops.

  Golden churn is confined to the steps that call it out:
  - S0: notes markers [SUPx0.70]; notes semantic <li> ids; eqref semantic numbers;
  - S2: .ops/js re-record; cite tree bibliography group/rule spans;
  - S3: no extra re-record if it ships in the S2 wave;
  - S4: figure part="caption", eqref tag child, TOC/glossary clones;
  - S5: url args disappear.
  S1, the whole refactor, is byte-identical. That is its acceptance gate.
- **I2 Measurement–render robustness contract (v2 §7)**
  No number depends on layout: per-page note numbering is explicitly not generalized. Deferred flows are numbered by the resolver; T6 only positions them.

  Marker presentation uses T4's paint-only `script` (affectsMetrics=false) and the measured size, as CLS_SUP does today (shell.mjs:46). Break control uses T5's bind.

  The equation tag becomes content. T6/T8 must measure it before S4 drops the compat string. S0's semantic-page span is plain flow HTML and touches no measured path.
- **I3 Ops contract: the reader is a fuzz target; OPS_VERSION discipline**
  Wire format:
  - There is no T3-specific ArgK list. All T3 args are T2's name-keyed typed attributes.
  - Definitions are T2 header records.
  - The kinds event, entry, slot, when and each, and `role` on every kind, ship inside T2's single OPS bump. T3 adds no bump of its own.

  Validation:
  - Record depth, field and template-size limits apply.
  - An unknown what/field gives a diag and is dropped; the buffer is still accepted.
  - Template node ids obey child-id < record.
  - Template-only kinds outside templates become error nodes.
  - The engine parses only NumberingPattern; all other sugar is parsed in JS.

  The fuzz corpus gains record, template and event cases. Exact-version skew detection is unchanged.
- **I4 Execution declares, resolver decides; resolver is a pure single pass**
  Declarations are write-only. $.element, $.counter and $.collector return constructors or nothing, never numbers. counterUpdate returns an opaque content value that READS nothing; it is just positioned. Templates are content, not callbacks. No JS runs after execution.

  The resolver is a pure function of (tree, registry, config): Phase 0 → LOCATE → BIND → B1 → B2 → B3. It has no fixpoint:
  - MATERIALIZE templates cannot create instances or ordinals;
  - rows bind only to already-selected rows;
  - the like/within graphs are acyclic;
  - name forms are cut at depth 4.
  Project-level manifest iteration is bounded and lives outside any single resolve.

  Change argued: design-decisions-v2.md:238 ('computed numbering is done in user JS with user counters') should become 'numbering is declared and computed by the resolver'. JS-computed numbers can never enter the label table, and they count constructions, not occurrences (model.cc:38-55). document-model.md:124 already promises <user> counter classes.
- **I5 Dual-target rule**
  Everything lives in engine/src/elements and engine/src/semantic, with data compiled into gen/. Nothing assumes a browser. Bibliography data and manifests come through T9's providers and arrive as entry rows recorded in .ops. tsrc and tests.cc run every resolver golden from recorded .ops.
- **I6 Emission-time style binding (v2 §12) with the DAG/schedule encoding**
  Membership and T4's rule scope are captured at instantiate, per occurrence. A #let value emitted twice is two instances, each with its own class context. The duplicate label is diagnosed, and the second copy keeps only its alias (probe t3c/dag.tsm today numbers 'e1' twice).

  Templates bind at materialization, the analogue of emission time. Each one uses the rule scope and style of its declared source node, through T4::fold(sourceStyle, ruleScope, roleTags, delta), so set/show-like rules declared in a region apply to generated markers and heads. Definition templates are instantiated style-neutral, outside the schedule. The resolver never writes an absolute Styling, which fixes resolve.cc:177 and :452.
- **I7 Block-granular containment of errors**
  The resolver never aborts, and every failure stays local:
  - a bad record is dropped (decl-invalid, decl-cycle, selector-ambiguous);
  - a bad slot renders empty;
  - unresolved targets degrade per child;
  - row-content and row-crossref nodes render inert;
  - event-unplaced and flow-unplaced are diagnostics only;
  - an unknown collector, or a template-only kind outside a template, becomes an error node in place.
  Containing script execution itself is T2's job.
- **I8 Resumable pull loop; progressive upgrade with atomic per-paragraph swaps**
  The resolver still runs once in ingest, before the semantic first paint, so the fallback HTML carries final numbers, now including equation numbers (S0).

  Events, entry rows and vacuous paragraphs are removed in B2, before pid assignment (emit.cc:900-903), so pids are unchanged. Deferred flow items stay inside their paragraph (marker plus insert body), so swaps stay per-paragraph.
- **I9 Performance: hot path parse/codegen/execute/emit; editor fast path**
  Cost is O(nodes + refs·|template| + Σ|collector|).
  - Membership is computed in the existing instantiate walk: an O(1) lookup by (kind, role), then a check of at most a few typed predicates.
  - BIND is one linear pass over RefSites, replacing work rewrite already does.
  - B1/B2/B3 together touch each node once, plus the clones.
  - Templates are a few arena nodes, counted against T2's DAG copy budget.

  Acceptance gate for S1: bench:edit shows at most +0.5 ms at 87K, against the '<6 ms combined' compile/execute/ingest budget (editor-design.md:22). The Index is rebuilt per keystroke along with the Doc.

## Interfaces

- **T1-surface-frontend** (consumes)
  Labels: universal label syntax (one LabelChar class with no whitespace; a `label` option on every block ctor; trailing <id> only on unambiguous forms).

  Lowerings:
  - @[a, b] → ref{target:'a, b'}[ref{target:a}, ref{target:b}];
  - @x[…] → node-valued `extra`;
  - ^[…] → note;
  - ^[label] for an existing named note → ref{target:label, form:'marker'}.

  Also required: nested statements in region bodies, which today compile to text("") (probe t3c/reg.tsm). Definitions can then sit inside regions; positional events need only splices.

  T1 owns the ^[ scanner and spacing, and the description-list shapes. The @[…] lowering change ships in the S2 re-record wave.
- **T1-surface-frontend** (provides)
  The reserved alias shapes (h-/fn-/fnref- + digit body, and the bib- prefix), for definition-site diagnostics in editors. tsr_labels, for label completion.
- **T2-constructor-ir** (consumes)
  (1) One versioned declaration channel: hoisted, domain-tagged header records with name-keyed typed fields, nested records and node-valued template fields. Template nodes are instantiated style-neutral outside the schedule; today model.cc:46-50 copies Node-tagged ids raw.
  (2) Name-keyed typed attributes for every T3 arg.
  (3) The kinds event (level-neutral, non-rendering), entry, slot, when and each, plus `role` on every kind, in T2's single OPS bump.
  (4) Level normalization that treats event like comment and hoists a vacuous paragraph's events to block level.
  (5) Generated public ctors (group, table, figure, collect, entry, slot, when, each, counterUpdate) with a uniform options-object convention. Today heading(level, label, …kids) and note(…kids) cannot take options (executor.mjs:171, 180).
  (6) $.element returns a ctor in the built-in namespace, and T2's $.role is dropped in favour of it.
  (7) The DAG copy budget also covers per-use template instantiation.
  (8) The region fallback passes role/label/title/kind through; the figure builder marks part:'caption'; splice-built nodes get spans.
- **T2-constructor-ir** (provides)
  The 'semantics' record vocabulary (element, counter, collector, counter-system), the event args, and their validation rules (limits, unknown fields, cycles), plus decl/template/event fuzz-corpus cases.
- **T4-style-settings** (consumes)
  Styles:
  - T4::fold(sourceStyle, ruleScope, roleTags, deltas): an exported pure left fold with exact float semantics and a subtree-propagation mode for delta carriers;
  - a RuleScopeId captured per node at instantiate;
  - the paint-only `script` and relative `size` properties in StyleDelta (set/clear bits).

  Locale: LocalePack with its terms section, fallback chain (root en), $.terms/$.doc, and termsLang in the doc/region/block settings cascade.

  Transport: the settings JSON ABI carries the 'semantics' section into the same records. T4 owns and validates the `style` section of element rows.
- **T4-style-settings** (provides)
  SemInfo.cls at instantiate, from a registry built before instantiate, so style rules can match by element role as well as by T4's own `class` tags (theorem bodies, caption labels, markers). Also the semantic term-key vocabulary.
- **T5-text-shaping** (consumes)
  A generic bind (no-break before/after) attribute on any inline node, set by marker templates, so emit stops reading CLS_SUP (emit.cc:89-90). A public `fill` (hfil glue) item usable in any content, templates included (proof end marks).
- **T6-layout-pagination** (consumes)
  - A generic margin `tag` on any block unit, measured (equations with T8, numbered listings).
  - Item markers rendered from SemInfo.number (ordered lists, notes, collector entries) instead of emit.cc:517.
  - Caption layout keyed on part:'caption' children.
  - A page builder that places Deferred flow items as inserts.
  - Anchors as item attributes in every sub-flow.
  - T6 owns the `layout` section of element rows.
- **T6-layout-pagination** (provides)
  - SemInfo on nodes (cls, number, flow, part, synthetic).
  - Deferred flow items, with marker and materialized body in place.
  - Heading number and title on SemInfo, for running heads.
  - Numbering decisions (table-figure, figure-table, subfigure) that have no layout effect.
- **T7-render-runtime** (provides)
  - SemInfo anchor (user label or alias), targetAnchor, targetCls and flow on nodes, plus the synthetic flag for copy policy. There is no `url` for internal refs from S5 on, and NodeIds are never used.
  - Registry rows, whose `html` section T7 owns.
  - tsr_labels JSON.
  - Flow-item occurrences, for previews of any reference kind.
- **T7-render-runtime** (consumes)
  - AnchorNamer {prefix (default 'tsr-', stable per document), id(name), href(name), escaping for key aliases}, used by both serializers and the shell.
  - The `html` section of element rows (which replaces any Category enum).
  - A Behavior registry keyed on data-flow / data-ref-class.
  - An anchor-closure check in the golden runner from S0: every internal href resolves.
- **T8-math** (provides)
  The equation class: counter 'equation' with policy labelled, an optional within, frame '(' ')', and Site{where:Tag}. The tag is materialized as content in mathblock's `tag`, alongside the compat ArgK::name string until T8/T6 measure it. Math labels register universally. T8's proposed $.numbering is replaced by $.counter and $.element.
- **T9-host-protocol** (consumes)
  - The typed resource protocol (NEED_RESOURCES) with its uniform failure policy, for bibliography data and label manifests.
  - Stage dumps: post-ingest diags and index.
  - Settings ABI transport of the 'semantics' section.
  - The opt-in literate overlay emitting entry/ref nodes inside codeblock lines.
- **T9-host-protocol** (provides)
  - tsr_labels(doc) for editor go-to-label and completion.
  - tsr_label_manifest(doc), as versioned, sorted JSON with raw components.
  - The Index persisted on the Doc handle (document-model §0).
  - The bounded project iteration procedure for export-static and renderTsm.

## Migration

### S0 Correctness fixes on the current resolver  → plan P0-09

(a) scan records ref sites. After scan, citation ordinals are assigned in document order, with note bodies counted at their markers.
(b) Grouped citations resolve per key. An unknown key gives '??' in its own slot plus ref-unresolved naming the key. ref-shadowed fires when a key is both a label and a bib id.
(c) label-duplicate applies to every kind: the losing node drops its label, and headings keep their h- alias.
(d) Every labelled node registers. A classless target displays its title or label text, with ref-unnumbered.
(e) Reserved alias SHAPES: h-<digits(.digits)*>, fn-<digits>, fnref-<digits>, and bib-*. A matching user label gets label-reserved and is dropped; h-index stays legal.
(f) One compose helper (emit.cc:26 moved to model/) builds the caption prefix, term name, mkLink and notes scaffolding without absolute styles.
(g) A repeated #notes() places nothing (flow-already-placed). Bibliography rows are cloned per collector, with anchors only on the first.
(h) Designated initializers for Resolver.
(i) The semantic tight-item path emits the paragraph id on <li>.
(j) A generated style-bit name table keeps today's spellings and order, appends SUP, and is used by the tree dump only.
(k) semantic_html prints the compat equation tag (ArgK::name) as <span class="tsr-eqno">.
(l) The golden runner gains an anchor-closure check on html/semantic goldens. Today only notes/*.semantic.txt dangle (tsr-fn-n), and (i) fixes them.
(m) A post-ingest diags stage. Probes p1/p2/p4/p7/p8, TOC-before/after-headings and notes-before-items become fixtures.

**Golden impact:** tree: in notes/basic, notes/cjk-glue and notes/explicit the marker style changes from [basex0.70] to [SUPx0.70] (from j).
semantic: notes/*.semantic.txt gain id="tsr-fn-n" on <li>, an intended fix of dangling ids. math/eqref.semantic.txt shows (1) and (2) (intended; resolver/missed:1).
New diags and probe goldens are added. Everything else is byte-identical.

Checked across test/fixtures: none has a citation in a note, a duplicate non-heading label, a styled figure or term, a labelled aside, a reserved-shape user label, a repeated collector, or a figure wrapping a table.

**OPS bump (as designed):** False

**Fixes:** `resolver/cite-ordinal-pass-order`, `resolver/grouped-cite-all-or-nothing`, `real-world-evidence/grouped-cite-all-or-nothing`, `resolver/duplicate-label-dom-ids`, `resolver/reserved-label-collision`, `real-world-evidence/missed:1`, `real-world-evidence/labels-on-unsupported-nodes-silent`, `resolver/absolute-style-loss`, `resolver/collector-aliasing`, `resolver/fragile-aggregate-init`, `resolver/semantic-tight-item-anchor`, `render-runtime/semantic-footnote-ids-dangle`, `resolver/missed:1`, `resolver/missed:5`, `resolver/untested-diagnostics`

### S1 Registry, Index and staged resolver; built-ins as data rows (no ops change)  → plan P1-10

Add engine/src/elements (registry) and engine/src/semantic (numbering, index, collect, materialize), plus engine/data/elements.json. Built-in rows are written in the user data form: heading, table, figure, equation, footnote, term, bibentry, and the toc/glossary/notes/bibliography presets.

Membership is computed in instantiate (SemInfo.cls, a non-arg field). Term tables use T4's LocalePack format; if T4 has not landed, T3 ships zh-Hans/zh-Hant/ja/en with root en under engine/data/locale/. applyLang is deleted, and tsr_set_lang becomes the host default document language.

Byte identity comes from the template rules: delta carriers, span/style sources, group-of-one, the fixed left fold, and generated-text merge. Built-in deltas use today's bits and sizeMul until T4's properties exist. toc/glossary keep a transitional 'excerpt' title mode. The resolver still writes `url` on refs and the equation `name` string.

The Index is persisted and `tsrc --stage=index` is added. All per-feature code in resolve.cc is deleted.

**Golden impact:** None: every tree/blocks/breaks/layout/html/semantic golden is byte-identical, which is the acceptance criterion. Index goldens are new.

Behaviour changes with no fixture coverage: ja gives 図/表/式 and zh-Hant gives 圖/表/式. Non-CJK languages are unchanged (root en). style/patch.tsm's zh-TW glyph scopes contain no refs and are unaffected either way.

**OPS bump (as designed):** False

**Fixes:** `resolver/fixed-counter-set`, `resolver/label-registration-per-kind`, `resolver/ref-display-switch`, `resolver/collector-what-dispatch`, `resolver/footnote-pipeline`, `resolver/presentation-constants`, `resolver/supplement-config`, `resolver/numbering-format-hardcoded`, `resolver/term-rewrite`, `real-world-evidence/locale-terms-switch`, `resolver/invariant-declares-decides`, `resolver/spec-drift`

### S2 Declaration channel, events and public semantic constructors (inside T2's single OPS bump)  → plan P2-07

Wire format:
- Definitions become T2 header records in the 'semantics' domain.
- New kinds event, entry, slot, when and each; `role` on every kind; T3 args as T2 name-keyed attributes.
- The fuzz corpus gains record, template and event cases.

Stdlib:
- $.element, $.counter, $.counter.system, $.collector;
- counterUpdate (= $.counter.update);
- slot, when, each;
- ref(target, {form, supplement}), collect(spec), entry();
- $.labels.import, stubbed until S7.

Bibliography:
- #bibliography returns its collector in place.
- The executor emits entry{role:'bibentry', key} rows as trailing roots with empty spans, after the program and order-independent.
- emptyPara becomes the vacuous-paragraph rule, and form:'all' becomes cited:'cited-then-all'.

The host 'semantics' section produces the same records. Docs amended: document-model §2.1/§4/§5/§11, design-decisions-v2.md:238, notes-design 'As built'.

**Golden impact:** All test/fixtures/*.ops are re-recorded (version byte, T2 wave), and *.js.txt changes through the generated prelude (T2).

cite/basic and cite/unknown-diag tree: the bibliography group and its rule move from @[0,0) to the #bibliography call-site span (intended; resolver/resolver-spans). The `doc @[0,0)` line stays, because rows are trailing empty-span roots (model.cc:103-105). If T2 stops deriving the root span from the last EMIT, that churn is T2's.

blocks/layout/html/semantic are unchanged; the bibliography stays the last top block, at pid 2. If T2's splice __at spans land in the same wave, collector and term expansions in doc/refs and notes/explicit also take call-site spans; call that out at review.

**OPS bump (as designed):** True

**Fixes:** `markup-language/numbered-env-hardcoding`, `resolver/extensibility-matrix`, `codegen-ops-model/bibliography-placeholder-and-end-emission`, `real-world-evidence/role-figure-hardwired`, `real-world-evidence/counters-fixed-fields`, `math/equation-numbering`, `codegen-ops-model/collector-switch-and-fixed-counters`, `resolver/argk-overloading`, `resolver/rewrite-normalizations`, `resolver/resolver-spans`

### S3 Structured references (ships in the S2 re-record wave)  → plan P2-09

T1 lowers @[a, b] to a parent ref with child refs, and @x[…] to `extra`. The children belong to the parent RefSite.

BIND resolves the whole target first, then each child. refGroup renders both groups and single citations (a group of one). `form` is read, and ranges can be compressed.

If S3 slips past the S2 wave, BIND keeps a comma split for childless ref targets (identical rendering) until the fixtures are re-recorded.

**Golden impact:** tree/html: none; cite/basic renders identically through refGroup.

.ops: every fixture using @[…] must be re-recorded (`record --check`, tools/record-fixtures.mjs:26-33), at least cite/basic.ops. Inside the S2 wave this costs no extra round. The js goldens of those fixtures change.

**OPS bump (as designed):** False

**Fixes:** `markup-language/reference-forms-closed`, `real-world-evidence/ref-cite-format-in-cpp`, `resolver/citation-path`

### S4 Parts, sites, frozen-title clones, counter-backed markers  → plan P3-03

- The figure builder marks caption paragraphs part:'caption' (T2), and the figure site attaches at that part.
- Add the table-figure, figure-table (refersTo enclosing) and subfigure rows.
- The equation tag becomes a node-valued `tag` through Site{where:Tag}; T6/T8 measure it, and both serializers print it.
- toc/lof/glossary use cloneTitle instead of excerpts.
- olist/enum-item counter-backed markers go into SemInfo.number, which T6 renders instead of emit.cc:517. Notes entries take markers from snapshots.
- Heading sites become available, and append/last-para sites follow T5's fill.
- Add a dterm class.

**Golden impact:** tree:
- region/figure, figure/* and pages/paged-doc gain part="caption" on caption paragraphs;
- math/eqref: name="(1)" becomes a tag child;
- doc/refs: TOC/glossary link text splits into number text plus cloned title nodes.

semantic: <figcaption> wraps only part children.

blocks/layout/html: list markers are the same strings. eqref is unchanged only if T6 places the measured tag at today's position; review it.

**OPS bump (as designed):** False

**Fixes:** `markup-language/structured-content-flattened`, `resolver/excerpt-strings`, `resolver/site-display-injection`, `resolver/missed:3`, `resolver/missed:4`, `real-world-evidence/heading-numbers-invisible`, `resolver/figure-role-string`, `codegen-ops-model/resolver-fabricated-styles`

### S5 Identity decoupled from DOM spelling (with T7)  → plan P3-04

MATERIALIZE writes SemInfo anchor/targetAnchor/targetCls/flow and stops writing `url`. T7's AnchorNamer spells ids and hrefs, and data-ref-class/data-flow come from SemInfo. The shell footnote popup becomes a refPreview behavior that no longer matches 'tsr-fn-' or '.tsr-sup'. The e2e popup tests are re-pointed.

**Golden impact:** tree: url="#tsr-x" args disappear from doc/*, notes/*, cite/*, math/eqref, region/figure, figure/* and pages/paged-doc. The change is mechanical and should be reviewed as one diff.

html/semantic stay byte-identical with the default 'tsr-' prefix, unless T7 adds data-ref-* attributes in the same step (T7 flags that churn).

**OPS bump (as designed):** False

**Fixes:** `resolver/anchor-namespace`, `render-runtime/anchor-namespace`, `render-runtime/shell-note-popups`, `real-world-evidence/notes-popups-dom-scraping`, `resolver/missed:2`

### S6 New collections, flows and counters  → plan P3-13

- Index and notation tables (multi, groupByKey, JS sortKey).
- lof/lot presets, user collectors, and static collector heads.
- Scoped flows (section-end, scope enclosing) and per-chapter resets.
- Keyed citation counters with scope (refsection).
- Named multi-marker notes (occurrences) and block flows.
- Deferred placement, together with T6's page builder.
- Counter systems, gap:'one', and per-level heading classes.

**Golden impact:** None; new fixtures only. They include a HoTT-shaped part/chapter/section file, appendix + equation + ref + TOC, gap cases [1,3], [1,2,2,1,2] and [1,3,2], and named notes with three markers.

**OPS bump (as designed):** False

**Fixes:** `markup-language/collectors-closed`

### S7 Cross-document labels and project numbering (with T9; #use with T2)  → plan P3-31

- Manifest export (tsr_label_manifest).
- $.labels.import loads through the resource protocol and emits entry{role:'external'} rows, recorded in .ops.
- The importer formats imported components with its own class templates and terms.
- Project mode iterates until the manifests are stable (≤3 rounds); chapter offsets come from counter events.

**Golden impact:** None; new multi-document fixtures, with manifests recorded into .ops.

**OPS bump (as designed):** False

**Fixes:** `real-world-evidence/no-cross-document-labels`

## Not generalized (kept special)

- **The rendering kind set stays closed; there are no user-defined kinds** — document-model.md:66 keeps layout closed under the kind table. The new kinds (event, entry, slot, when, each) are semantic or template-only. B2/B3 remove or lower them, so emit and layout never see them. Classes are a semantic layer over render kinds.
- **No JS callbacks during or after resolve (no Typst show-closures, no post-resolve JS formatter)** — Either would need a fixpoint or a second JS↔WASM crossing, and it would break native post-ops goldens, which tsrc runs without JS. Declarative templates over located facts are sufficient, because the web target has no layout-dependent references (design-decisions-v2.md:237).
- **Per-page note numbering and page-number references** — I2: numbers must not depend on layout. Deferred flows let T6 place notes on pages, but their numbers stay document-determined. A page form stays reserved for a bounded export iteration (design-decisions-v2.md:237).
- **Citation style logic (CSL, et-al rules, author-year disambiguation) and cross-referencing between bibliography rows** — It stays in userland JS, as v2 §11.1 decides; the engine offers row fields, grouping and range compression. A row may cite only rows already selected (row-crossref otherwise), because a crossref closure would make BIND iterative.
- **NumberingPattern is a small string syntax** — It is the one accepted engine-parsed mini-language: Typst-compatible and pure, extended only by '{name}' references to declared symbol systems. Selectors, placements and sites are structured records; their string sugar is parsed in JS only.
- **Collation** — The engine has no ICU, and CJK indexes need pinyin or stroke order. Sort keys come from JS; the engine compares bytes, with a document-order tie-break.
- **One shared label namespace, with shape-reserved aliases rather than true namespaces** — `@fn-n` is documented (notes-design.md:38), and published anchors such as tsr-h-1.1 must not churn. Aliases built from raw integers make reservation decidable. Term keys and bib keys remain author-chosen names in the same namespace by v2 §11.1's design, guarded by diagnostics.
- **Definitions are document-global (hoisted, field-level last-wins); only events are positional** — LOCATE needs every class before it starts, and hoisting is strictly more permissive than LaTeX's preamble rule. Positional semantics are needed only for counter state (appendix, setcounter, supplement switch), which events cover. Scoped class overrides would duplicate T4's cascade without a use case.
- **Template control flow is limited to when(slot), each(items|occurrences) and slot fallbacks** — This covers every built-in and every corpus need (optional titles, grouped citations, multi-marker backrefs, locators). Loops and arithmetic would turn templates into a programming language and invite fixpoints.
- **Automatic figure-kind detection (Typst figure(kind: auto) from the body)** — An explicit `kind:` arg and selector rows cover it without content inspection. Auto-detection would need a 'contains' predicate, and no fixture or corpus case requires one yet (open question).
- **The ^[…] sugar and its binding to `note`** — `note` is already a real constructor (v2 §4 l.107). Scanning and rebinding through a sigil table are T1's. What a note means is entirely class data here.
- **Cross-document resolution is project-level and bounded-iterative, not live or incremental** — Each document's resolve stays single-pass and its goldens deterministic. Live cross-document invalidation belongs to T9's product graph, if it is ever needed.
- **The equation default policy stays 'labelled only'** — It is a documented as-built choice (math-design.md:342). It is now data that a document can change; the default stays.

## Risks

- S1's byte identity depends on template fidelity: delta carriers reproducing [BOLD], [SUPx0.70] and per-node x0.85; span/style sources reproducing note and row spans; group-of-one cites; and the exact float fold order. Mitigation: per-fixture tree diffs gate S1, and the 'excerpt' title mode is kept until S4.
- The editor fast path ('<6 ms combined', editor-design.md:22) pays for membership in instantiate, BIND and three MATERIALIZE stages on every keystroke. Mitigation: interned ids, a (kind, role) membership index, arena templates, and a bench:edit gate at +0.5 ms.
- Shape-reserved aliases may reject existing user labels such as h-1 or bib-x. Scan examples/real-world and zball-io before shipping S0(e).
- Events are content values. An author who writes `#{ $.counter.update(…) }` as a statement gets only an event-unplaced diagnostic, not the update. Mitigation: the diagnostic's message names the splice form, and the docs lead with `#counterUpdate(…)`.
- Registry rows shared by T3/T4/T6/T7 need a joint schema and one owner per section. If T6 or T7 keep their own tables, the five-registry problem returns. This needs agreement at the joint S2 review.
- Moving membership to instantiate makes T2's instantiate depend on the registry product (a Phase 0 before instantiate). If T2 keeps a single-pass reader, header records must precede nodes in the buffer.
- Deferred placement depends on T6's page builder. Until it exists, Deferred falls back to End, which is visible only in paged output.
- The single OPS bump must coincide with T2's wave, or the fixtures are re-recorded twice. S2 and S3 cannot land before T2's header records and node-valued attributes.
- S4's equation tag as content depends on T6/T8 measuring it. Until then the compat `name` string stays (and S0's semantic span reads it).
- Anchor ownership for rows rendered by several collectors is new semantics: the second rendering has no anchor.
- gap:'one' turns a leading level-2 heading into 1.1 and the first level-1 heading into 2. This is structurally consistent but may surprise authors; it is opt-in.

## Open questions (decided in PLAN.md §3)

- Transport detail (T2): do header records precede all nodes in the ops buffer, so the reader can build the registry before instantiate in one pass, or does Phase 0 make a separate pre-scan?
- Default for #!figure wrapping a table: keep 图 numbering with the inner table unnumbered (figure-table only), or default to table-figure numbering (表) when the body is a single table? Proposed: figure-table only, plus an explicit kind:.
- Should nested figures default to subfigure numbering ('图 1(a)')? That changes today's sibling numbering, which no fixture covers.
- Clone styling for TOC/lof entries: keep the title's source styles (emission-time scope), or restyle at the collector source?
- Should the fnref-n alias stay user-addressable, or only fn-n? Named-note occurrences add fnref-n.k.
- When a document declares a heading numbering pattern, should headings show their numbers by default? The blog default stays unnumbered.
- Escaping of key-bodied aliases (term names with spaces, bib keys): percent-encode or slugify? This is T7's AnchorNamer and affects published permalinks.
- Where does project configuration live (chapter numbers, manifest list): a renderTsm/export-static option, or a project .json shared with T9's static-export manifest?
- Does T4's termsLang cascade accept a region arg shorthand (e.g. #!aside(lang:'en', terms:true)), or only settings:{termsLang}?

## Changelog (critique responses)

- Soundness critic #1 (BLOCKER, decl transport contradictory; side-effect EMIT lands before the enclosing block): ACCEPTED. Re-verified with probe t3crit/emitorder.tsm ('DECL mid-para' is emitted before the paragraph holding fn-1) and model.cc:94-96. The transport is now split. Definitions are position-free header records. Positional changes are `event` content values returned by counterUpdate and placed where spliced; LOCATE applies them and B2 deletes them, together with vacuous paragraphs. event-unplaced catches a discarded value. The appendix example now returns content.
- Soundness #2 (gap:'skip' formula makes the first heading 2): ACCEPTED. Renamed to gap:'one' with a corrected formula (pad intermediates, push 0, increment), checked by simulation: [1,3] → 1, 1.1.1; [1,3,2] → 1, 1.1.1, 1.2; Zero still reproduces 1.0.1 and 0.1. Uniqueness is stated, and index goldens for [1,3], [1,2,2,1,2] and [1,3,2] are added in S6. The example text '1.1 instead of 1.0.1' is corrected to 1.1.1.
- Soundness #3 (aliases from displayed numbers collide under reset/set, unnumbered headings lose anchors, pattern-dependent reservation): ACCEPTED. Aliases are built from raw integer components (keeping today's h-<dotted>) or per-class document ordinals (fn-<n>, never resets). Collisions get a deterministic '~k' suffix, and unnumbered instances get prefix~ordinal. Reservation is shape-based on fixed digit bodies, so it no longer depends on user patterns and h-index stays legal. First-wins now applies only to user labels.
- Soundness #4 (snapshot lacks pattern epoch; parent components formatted with the child pattern): ACCEPTED. A Snapshot stores each within-chain component with its pattern id, and parents are formatted with their own pattern, so an appendix equation prints (A.3). An index golden for appendix + equation + ref + TOC is added.
- Soundness #5 (one in-place MATERIALIZE walk makes output depend on collector position): ACCEPTED. MATERIALIZE is now B1 (resolve every ref, flow and row body once), B2 (attach sites marked synthetic; detach flows) and B3 (collect from frozen titles; cloneTitle skips synthetic nodes; clones never re-walked). Re-checked against resolve.cc:146-147 (scan captured the TOC text before mutation) and :372 (on-demand note rewrite).
- Soundness #6 (S1 byte identity needs undefined template capabilities): ACCEPTED all four rules: styled template nodes are delta carriers applied per node, including slot content; TemplateUse declares span and style sources (Instance for notes per notes/explicit.tree.txt, Row for bib per resolve.cc:420-423); a citeable ref always renders through refGroup; the fold is a fixed left fold with today's float order. Built-in templates are published in the user data form.
- Soundness #7 (pre-reserved ArgK list incomplete; nested maps unencodable; `slot` overloads Kind and ArgK): ACCEPTED. The T3 ArgK list is dropped; T3 depends on T2's name-keyed typed attributes and header records with nested records (ops.cc:67-90 has only scalar or Node args). Limits and fuzz cases are added. The child-role arg is renamed `part`.
- Soundness #8 (termination: within/like cycles, refs in templates, collect nodes in clones): ACCEPTED. Phase 0 checks the like and within graphs for cycles (decl-cycle). Templates may hold only to:-anchored refs and links (template-ref otherwise). cloneTitle drops collect, event and entry nodes, and clones are never re-walked.
- Soundness #9 (rows and unplaced flows counted in LOCATE/BIND): ACCEPTED. BIND skips unplaced flow items (marked after LOCATE, diag flow-unplaced). Selected rows bind afterwards and only to labels and already-selected rows (row-crossref otherwise). Counted or flow nodes inside rows are inert (row-content). BIND stays one pass; crossref closure is listed in not_generalized.
- Soundness #10 (whole-record last-wins vs partial host patches; selector ties; like and select): ACCEPTED. Overlay is per field across built-in < host < document. `like:x` in a redefinition of x refers to the previous layer. `select` is not inherited. Ties go to the latest declaration with selector-ambiguous.
- Soundness #11 (S3 golden impact omits the .ops re-record; RefSite of child refs): ACCEPTED. S3 ships in the S2 re-record wave (verified tools/record-fixtures.mjs:26-33 --check). If it slips, BIND keeps the comma split for childless targets. Child refs belong to the parent RefSite.
- Soundness #12 (root span depends on how rows are emitted): ACCEPTED after re-checking model.cc:103-105 and cite/basic.tree.txt:1. Rows are trailing empty-span roots, so `doc @[0,0)` stays. The bibliography group/rule now take the call-site span, and that churn is called out in S2; it touches only the tree dump, since semantic prints no data-s for the group.
- Soundness #13 (no extra/locator slot; author-year example underivable): ACCEPTED. Added slot('extra'), the `or` fallback and the group-of-one rule. The example is re-derived as item [link citeText, when(extra) ', ' extra] and group '(' each '; ' ')'.
- Soundness #14 (absorb makes inner table labels classless): ACCEPTED. absorb is deleted; the figure-table row has refersTo:'enclosing', so @inner gives the figure's number, as in Typst.
- Soundness #15 (Styling.lang couples glyph locale and supplements; root undefined): ACCEPTED. Root = en (config.h:96-100). termsLang is a separate T4 cascade setting defaulting to the document lang, never Styling.lang. Verified that style/patch.tsm has no refs, so S1 stays byte-identical either way.
- Soundness #16 (manifest supplements in the exporter's language; stale titles): ACCEPTED. The export carries raw components with pattern ids, and the importer formats them with its own templates and terms. Titles that need other chapters converge by bounded project iteration (≤3 rounds, diag project-unstable) instead of symbolic titles, which keeps the importer simple.
- Soundness #17 (generated style-bit names could churn style/* goldens): ACCEPTED. S0(j) keeps today's spellings and order (model.cc:118-126), appends SUP, and is used only by the tree dump; the blocks dump list (emit.cc:1082-1088) is untouched.
- Soundness missing item: real-world-evidence/missed:5 is RE-DISPOSED as owned-by-other-theme (T2 builder marks parts, T7 serializer reads them); T3 provides only the part contract.
- Soundness missing item: markup-language/footnote-sugar-oneoff is RE-DISPOSED as owned-by-other-theme T1 (scanner, spacing, sigils). T3 keeps the meaning, and S6 no longer lists it as fixed.
- Soundness missing item: heading-numbers-invisible was not credible with title mutation: ACCEPTED. B3's cloneTitle skips synthetic site nodes, so heading sites and TOC coexist. A TOC-after-headings probe is added in S0(m).
- Soundness overlap T2 (transport, keys, node-valued attrs copied raw at model.cc:46-50, copy budget, calling convention): ACCEPTED. All are listed as T2 consumes items, and the joint S2 wave is required.
- Soundness overlap T4 (doc record owner; fold semantics; schema as an explicit product): ACCEPTED. T4 owns $.doc, $.terms and termsLang. The fold is specified as a left fold with exact float semantics plus subtree propagation. The registry is an explicit product passed to instantiate and resolve.
- Soundness overlap T6 (flow placement decided twice): ACCEPTED via Placement::Deferred. The resolver numbers and materializes; T6 places. Resolver placement applies only to flow output (Deferred falls back to End).
- Soundness overlap T7 (alias scheme vs permalink stability; closure check): ACCEPTED. The ordinal/raw-component aliases match today's h-<dotted> and fn-<n> under the default config. An anchor-closure check enters the golden runner in S0(l); verified that today only notes/*.semantic.txt dangle (tsr-fn-n), which S0(i) fixes.
- Soundness overlap T1 (@[a, b] lowering forces a .ops re-record): ACCEPTED. It is scheduled in the S2 wave, with a legacy split as fallback.
- Soundness critic: wrong citation example-hott.tsm:215: ACCEPTED. Corrected to zball-io/src/docs/example-hott.tsm:221.
- Generality critic #1 (BLOCKER, transport; decl as a block kind would split paragraphs; region-body statements dropped): ACCEPTED. Same split as Soundness #1. `event` is level-neutral like comment, never block. Re-verified with probe t3c/reg.tsm that statements inside region bodies compile to text(""); this is now a stated T1 dependency for definitions inside regions, while events need only splices.
- Generality #2 (BLOCKER, one-walk MATERIALIZE): ACCEPTED. Same staging as Soundness #5. Title pointers are captured in LOCATE, generated nodes are marked synthetic, and B1 recursion is memoized with a depth budget. Probes are added: TOC before and after headings with refs and footnotes, heading site + TOC, and #notes() before its notes.
- Generality #3 (five parallel registries; closed Category enum privileges built-ins): ACCEPTED. There is one row per class keyed by role name. T3 owns the semantic columns; T4/T6/T7 own style/layout/html sections in the same row, declared through $.element and inherited via like. The Category enum is deleted. T2's $.role and T8's $.numbering fold in, and T2 keeps only transport and ctor generation.
- Generality #4 (too many membership channels; `class` collides with T4): ACCEPTED. `role` carries the class on every kind, and the T3 `class` ARGK is dropped. Selectors are typed conjunctions (Str/Num/Bool) plus `inside`, with specificity and tie rules. numberAs and absorb are deleted in favour of selector rows plus like. like cycles are detected.
- Generality #5 (HoTT Part/Chapter/§ inexpressible; supplement-per-depth claim unsupported): ACCEPTED. Typed Num predicates allow per-level heading classes, flat counters with explicit within chains number chapters across parts, and the unsupported 'supplement list per depth' claim is withdrawn. Verified hott-introduction.tsm:179-195 refers to parts and chapters, which are external in that file, so the S6 fixture is HoTT-shaped and self-contained.
- Generality #6 (two pattern sources; appendix supplement not positional): ACCEPTED. The counter's current pattern is the only source, and a class contributes only a frame. Events can override the supplement (term 'appendix'); the override is captured in the snapshot.
- Generality #7 (closed Attach enum; no append; Tag is mathblock-only): ACCEPTED. A class now has a list of Sites {where: prepend|append|replace|tag, at: self|first-para|last-para|part}. Tag means T6's generic margin tag on any block unit. A `fill` item is requested from T5. A proof/∎ example is added; numbered listings use the same Tag site.
- Generality #8 (flows: single site, inline only, T6 disassembling collector output): ACCEPTED. FlowItem has occurrences, built from ref{form:'marker'} markers and rendered by each{of:'occurrences'}. Flow membership is a class trait on any kind, so block flows work. Placement::Deferred hands items to T6. Per-page numbering is added to not_generalized, citing I2.
- Generality #9 (L6/L7 reaching into Index pointers; classOf computed twice): ACCEPTED, partly in form. Membership is computed once at instantiate and stored on the node. MATERIALIZE writes everything downstream needs onto the node as well, but as a non-arg SemInfo field rather than args, which avoids dump churn and ops-vocabulary growth with the same layering effect. The Index keeps spans, not pointers, and is declared invalid for node lookup after MATERIALIZE.
- Generality #10 (rule scope missing at MATERIALIZE; per-node delta into inserted bodies): ACCEPTED. T4 captures a RuleScopeId per node at instantiate, and fold takes (sourceStyle, ruleScope, roleTags, delta). Delta carriers apply relatively to each inserted node's own style, as today's rescale does (resolve.cc:352-357). A fixture with a note inside a styled region and a scoped marker rule is added to S6.
- Generality #11 (ArgK list incomplete; engine-parsed string grammars): ACCEPTED. This relies on T2's name-keyed attributes, which the canonical records use. Canonical records are structured ({counter:{name, within, depth}}, {where, at}, {placement, depth}), string sugar is parsed only in the JS stdlib, and the engine parses only NumberingPattern. The decl reader is fuzzed.
- Generality #12 (manifest duplicates citeable rows; supplements in the source language; .ops recording forces re-execution): PARTIALLY ACCEPTED. Imports become entry{role:'external'} rows in a citeable table, the External path and decl what:'external-label' are deleted, and the export carries raw components rendered by the importer. REJECTED: moving imports out of .ops. Project mode re-runs the whole pipeline only at export time, and recording keeps native goldens self-contained (I1/I5).
- Generality #13 (slot kind overloading; Extra enum): ACCEPTED. The template kinds are now distinct (slot, when, each). Links reuse the link/ref kinds with to:. The Extra enum is replaced by slot('extra') and its `or` fallback.
- Generality #14 (ordered-list items: second formatter, not referenceable): ACCEPTED. olist/enum-item built-in classes with a counter scoped per list instance replace emit.cc:516-517 (verified). Markers go to SemInfo.number for T6, list items become referenceable, and missed:4's double source disappears.
- Generality #15 (template rule (4) forbids collector headings): ACCEPTED. A collector `head` is a static template instantiated in LOCATE, so it can be counted and outlined. Rule (2) still binds MATERIALIZE-time templates.
- Generality #16 (refs in uncited rows assign ordinals): ACCEPTED. Merged with Soundness #9. resolve.cc:418-426 confirms that today only rendered entries are rewritten; selected rows now bind after the main BIND.
- Generality #17 (rows and refs inside verbatim; pbr literate fragments): PARTIALLY ACCEPTED. The contract is stated: entry, ref and anchor nodes are legal inside codeblock lines, LOCATE and BIND walk them, and multi tables hold several rows per key. A user example is added. Production of those nodes is T9's opt-in overlay, and their in-grid layout is T6's; T3 does not build the overlay.
- Generality #18 (closed symbol set; citation ordinals unscoped): ACCEPTED. The counter-system record adds declared systems (symbols, mode), referenced as '{name}' in patterns. Citation ordinals are a keyed counter, so within/scope gives refsection.
- Generality #19 (hoisted definitions vs positional JS ctors): ACCEPTED. One documented rule: semantics are hoisted and document-global, the returned ctor is an ordinary JS value, and #!regions need no ctor. decl-after-use is an info diagnostic. Codegen hoisting stays optional for T2.
- Generality #20 (no-JS equation numbers deferred to S4): ACCEPTED. S0(k) prints the compat ArgK::name tag in semantic_html (verified semantic_html.cc:259-265 prints only the source). S4 swaps it for content.
- Generality missing item: footnote-sugar-oneoff re-disposed to T1 (see the soundness entry).
- Generality missing item: the label-namespace residue (term aliases and bib keys unreserved): ACCEPTED as a documented residue. They are author-chosen names by v2 §11.1's design, guarded by label-duplicate and ref-shadowed. Stated in the subsumption notes and in not_generalized.
- Generality missing item: argk-overloading reintroduced by slot{name:'if'|'link'|'items'} and the Extra enum: ACCEPTED. Fixed by the distinct template kinds, `part`, and the removal of Extra.
- Generality overlap (five registries) and overlap (declaration transport): ACCEPTED. One row with layered sections. T2 owns one domain-tagged declaration channel; positional events are T3 content nodes.
- Generality overlap T4 (`class` naming): ACCEPTED. `role` is T3's carrier and `class` is T4's; T4 rules may select on both.
- Generality overlap T4 (locale ownership): ACCEPTED. T4 owns LocalePack, $.terms, $.doc and termsLang. 'terms' and 'doc' are removed from T3's record vocabulary, and T3 calls terms.get(key, termsLang(source)).
- Generality overlap T6 (list markers, inserts, running heads): ACCEPTED. T3 owns all numbering, including list items. Deferred flows provide inserts, and SemInfo carries the heading number and title for marks.
- Generality overlap T7 (Index::refOf from L7): ACCEPTED. T7 reads SemInfo on nodes only.
- Generality overlap T5 (fill and bind in templates): ACCEPTED. Both are listed as T5 consumes items, and templates use them as data.
