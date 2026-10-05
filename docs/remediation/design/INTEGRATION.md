# Cross-theme integration

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Layer map

### L0 Source & syntax (owner T1)

**Owns**

- engine/src/syntax/syntax.def: lexical classes (SpliceHead, IdStart/IdJoin, LabelChar, Escapable), delimiter rows with a CLOSED set of body modes (Verbatim, Js, CallChain, Pair, LinkText, Content, IdList, Ident), block rules with interruption policy, keyword forms, one SUGAR row per sugar (slot + typed payload); generated syntax.gen.json + docs/syntax-table.md
- SurfaceLexer: SpanCursor (no scan leaves its leaf; joins are structural; CRLF-normalised SourceText), AtomTape lexed once per leaf, one island-aware bracket counter for content bodies, inline stack holding only Pair and weak '[' frames, shared primitives (splice head, bare id, label, ArgList)
- BlockAutomaton: Prefix/Column/Explicit containers, line ownership granted only by body mode, tentative carries with explicit structural bounds, block-granular recovery (region-unclosed, by-name resync)
- CallAST (Call/Splice/Stmt/Error/Hole/SoftBreak) with zero-width provenance side records: exact spans, label spans, Sep/Join cuts, per-line columns, cooked-to-raw maps
- parseContent: one re-entrant entry for the document, content bodies (Blocks mode + sole-paragraph unwrap), fragments with out-of-band holes, tools
- FrontEndExports: tokens, outline, AST JSON, printer/escapeTsm, conformance tests that pin tree-sitter/TextMate approximations
- Syntactic diagnostics only: label-orphan, label-like-text, label-conflict, region-unclosed, header-positional

**Consumes:** UTF-8 source and FrontEndOptions {frontMatter} taken from the settings document. Nothing from execution, resolution or measurement.

**Produces:** CallAST + provenance side records + Error nodes; tokens/outline/astjson products; LexState snapshots and RevertedWindows for incremental re-lex.

**Must not:** Decide block/inline placement, display promotion or list merging (L3 normalize; today codegen.cc:95-117); decide CJK line joins or note spacing (L5; today inline.cc:48-59 and inline.cc:384-397); split table cells at '|' (L2 table ctor over provenance cuts; today inline.cc:601-627); register labels or resolve refs (L4); evaluate or edit JS text (it only delimits it); scan past its leaf (today contiguous() at inline.cc:68-72 returns true past the last span); know constructor names beyond SUGAR rows; read any setting except FrontEndOptions.

### L1 Lowering (mechanics T2, semantics contract T1)

**Owns**

- sugar.def: SugarId -> ctor name + argument map, resolved at compile time into bound CALL ops
- AST -> LowerProgram (BLOCK/CALL/TEXT/SOFTBREAK/HOLE/NAME/REGION/FENCE/STMT/IF/FOR/ERROR) + hole module that contains only user code and hoisted #let names
- T1's LoweringContract semantics: one statement-scope rule (document or keyword-form body; content opens no scope), named-only ArgList = opts object, #!name(H) ... #name! == #name(H)[...], structural content-arg insertion, Error -> error op
- Fragment programs (m``, m.parse, ctx.m.parse, sidecar notes) in the same program format; tsrc --stage=lower|js

**Consumes:** CallAST with ParaProv cuts and StmtSide binds; the generated ctor manifest (param specs) for compile-time binding of sugar.

**Produces:** LowerProgram bytes + hole JS module exporting its ABI hash; a block table for per-block frames; fragment programs.

**Must not:** Print static markup structure as JS (only user code becomes JS); place generated identifiers in user scope (today the 31-name destructured prologue at codegen.cc:209-212 makes #let list fatal); apply placement peepholes (L3 owns levels); edit argument text (structural insertion only); special-case any constructor name. If LowerProgram fails the I9 bench gate, the fallback prints JS from the same CallTree, still hygienic and framed, with a lower.mjs-vs-codegen conformance gate.

### L2 Execution (owner T2; host loads via T9)

**Owns**

- One interpreter (runtime/src/shared/lower.mjs) for documents, fragments and sidecars
- Registry with namespaces ctor/fence/format: one bound calling convention ctor(opts, ...content), trampolines, next-delegation, sealed base ctors (text, seq, error, node); regions are ctors with a Body param
- Frames at every block depth: block-granular error containment (I7), style-height rules, span stamping
- Node values (branded, immutable, owned by no buffer), toContent/plain, generated stdlib (ctors.gen.mjs, tsm:std)
- Declaration emitters writing DECL records ($.element, $.counter, $.collector, $.math.*, $.locale, $.doc) and schedule entries ($.style.push/popTo, $.set)
- Ops writer (version byte = max since of vocabulary used); DIAG records for executor warnings; ctx.load/$.load/#use through the host ResourceHost

**Consumes:** LowerProgram + hole module; the pre-execution settings view (ctx.settings); host loads (data, modules, bibliography JSON) through the locator.

**Produces:** One ops buffer per execution: content DAG (MAKE), schedule (EMIT, STYLE_PUSH/POP_TO), DECL records with flow anchors, event nodes, SPAN/AT occurrence spans, DIAG warnings.

**Must not:** Read resolved values, numbers, metrics or resource answers (I4); build nodes outside generated ctors; let a throw escape its frame (today codegen.cc:214-229 emits bare __emit calls, so one throw kills the document); sniff handler args for presentation (today executor.mjs:131-137); dispatch on built-in names (today executor.mjs:127-128 'table'/'figure'); keep an ambient current buffer; assume a browser beyond the worker host API (I5).

### L3 Model: decode, Phase 0, instantiate, normalize, cascade, membership (owners T2 reader/instantiate/normalize, T4 cascade, T3 membership)

**Owns**

- Schema-driven ops reader/validator: total, fuzzed, window MIN_COMPAT..OPS_VERSION (replaces the exact check at engine/src/ops/ops.cc:100)
- Phase 0: element registry built from built-in rows + host settings + DECL records (RawOps is fully decoded before instantiate, so DECLs need no wire ordering)
- Instantiate on an explicit stack with InstLimits (today a raw recursive copy, engine/src/model/model.cc:36-54), per-occurrence spans with containment, declEpoch stamping for positional DECL namespaces
- normalize(): level classes Block/Inline/Adaptive/Transparent/Trivia, body and slot models, block-in-inline policy; also called on every subtree L4 synthesizes
- Unified Selector matcher; membership SemInfo.cls per occurrence
- Cascade: CascadeState {Styling run record, NodeProps (= T6 BlockTraits view), RuleEnv, scope}; Cascade.make/lift as the ONLY post-instantiation node factory

**Consumes:** Ops buffer; settings document layers; built-in data rows (elements.json, defaults.json).

**Produces:** Normal-form ContentTree with CascadeState, SemInfo.cls, declEpoch and real spans per node; Decl table; Ingest-origin diagnostics.

**Must not:** Number, resolve or build presentation (L4); let selectors observe L4 decisions; read metrics or the measure; create nodes except through Cascade.make; put block traits into StyleId (keeps MetricStore keys stable); OR-compose kind presentation (today emit.cc:52-63, 471-551 and resolve.cc:177, 452).

### L4 Semantics (owner T3)

**Owns**

- Semantic columns of element rows: counter + policy, number frame, supplement TermRef, title source, sites, ref forms, flow/table membership, alias rule
- Counters + NumberingPattern (one engine-parsed mini-language), positional counter events
- Index: labels, shape-reserved aliases from raw components, AnchorIds (registry winners only), rows, flow items, ref sites
- Phased resolver PHASE0 -> LOCATE (read-only) -> BIND (document order) -> MATERIALIZE B1/B2/B3
- Templates (slot/when/each) + Materializer with declared span/style sources; collections (class queries, keyed tables, flows with End/SectionEnd/Deferred placement)
- Term-key vocabulary (tables are T4 LocalePack); label manifest semantics; tsr_labels

**Consumes:** L3 tree + registry + declared inputs (label manifests) + LocalePack terms + settings (termsLang, counter starts).

**Produces:** Resolved tree with SemInfo {cls, number, anchor, targetAnchor, targetCls, flow, slot, synthetic}; style-neutral materialized content created via Cascade.make and normalized; Index and labels products.

**Must not:** Write absolute Styling, CSS, URLs or DOM id spellings (today resolve.cc:113, 263, 292 and resolve.cc:177, 452); depend on walk order (LOCATE is read-only); run JS or iterate to a fixpoint; read layout/page data (page refs only through fixed-width slots); compare role strings (today resolve.cc:166).

### L5 Shaping (owner T5; T8 math is an ObjectKind client)

**Owns**

- TextRules: one CC per grapheme cluster from pinned UCD, per-script Section columns (break pairs, blanks, autospace, font role, kern eligibility), locale-pack text sections, hyphenation dictionaries, EmergencyTable
- Shaper: closed flatten table generated from schema inline kinds, cluster atoms across node edges, one boundary function (soft breaks, attach edges, Unicode controls, ambiguous width)
- HList: Box/Glue/Penalty/Disc with glue class, attrs and ColdRec spans (no stand-alone Kern)
- Run instances: face, link, syn, copy, RealizeClass, anchor point
- InlineObject registry (math, image, raw, hbox) with two-phase expand/resolve and declared edge classes
- resolveWidths -> one MeasureRequest; T8 MathDict/MathRow/MathFont/lazy finalize as the math ObjectKind

**Consumes:** Resolved tree + CascadeState projections (faceOf, emPx, TextProps, text.lang) + RES answers (textWidth, fontVmet, hyphenPatterns).

**Produces:** Per paragraph-like unit: a width-independent HList, run table and object extents; needs; hlist dumps.

**Must not:** Read the measure, page or container width; decide block structure; emit DOM or CSS; leave script decisions for later layers (paint never calls TextRules); create content-tree nodes; key anything on StyleId across documents; use the fused BF_* flags (emit.h:10-22) once migrated.

### L6 Layout (owner T6)

**Owns**

- BoxTreeBuilder (L6 entry): width-independent LayoutBlocks, layouter chosen by content model, BlockTraits view of NodeProps, SizeSpec/IntrinsicSize
- LineBreaker service: BItem su projection, tagged penalties (Normal/Forbidden/Forced), LineEnds presets, BreakParams, measureLine, TeX discard, final-pass rescue, total-order ties, BreakMemo key
- BlockLayouter registry (Paragraph, Stack, Replaced, Grid, Table) + layoutDetached + one materializeLines (joins/Sep)
- ParShape + ExclusionMap (side-tagged, conservative line bands); TableSpec/resolveTracks; vertical algebra
- VList + Fragments: frame-relative su geometry, baseline, PaintFit, Sep, track, gutter placement, PaintClassId, exactly one anchored fragment per anchored block; margin tag placement

**Consumes:** HLists + run tables (L5); NodeProps/BlockTraits (L3); SemInfo numbers and slots (L4: markers, tags, captions); intrinsic sizes and NEED_BOX answers (RES); its settings view (viewport.width, layout.*, break.*).

**Produces:** VList + Fragments + per-pid frames; break dumps.

**Must not:** Include model.h or read ContentNode, roles or kinds (today emit.cc:781 'figure', typeset_html.cc:705 Kind::heading); inspect script classes, codepoints or glue classes; produce HTML/CSS; read Config geometry outside its view; let results depend on hash-map iteration order (today break.cc:57-58).

### L6.5 Paginate (owner T6)

**Owns**

- PageBuilder over the VList: tiered keeps relaxed Avoid1 -> Avoid2 -> Structural before the greedy cut, float extents, movable page floats, inserts from T3 Deferred flows, repeated table headers
- PageSpec (page.* settings; host print options override the document)

**Consumes:** VList, PageSpec, insert flows.

**Produces:** PageResult (body/frame/insert slices, repeats, overflow) and anchorPage for page-reference slots.

**Must not:** Re-break lines or re-lay out blocks (pages-design §2 post-pass); test kinds (today typeset_html.cc:681-737 re-derives keeps inside the serializer); feed back into resolution (no page fixpoint).

### L7 Paint (owner T7)

**Owns**

- DisplayList: runs at run-instance boundaries, typed payloads, a closed primitive set (Run, Spacer, InlineBox{Glyphs, ImageRef, RawHtml}; Rule, Image, Raw, Glyphs, Frame)
- HtmlWriter (allowlisted elements/attributes, one style attribute, one escaper, trustedRaw) + AnchorNamer (render.idPrefix)
- PresentationMap = the html section of element rows (semantic element, slot elements, ARIA, projections, typeset hooks, ref preview, generated-text copy)
- SynKind vocabulary, CopyPolicy, ContentText; Sep DOM encoding
- Backends: typeset and paged (DisplayList walks), semantic (pre-measure tree walk), fragments; RenderResult frames (generation, value keys, unheld blocks, anchors table)

**Consumes:** VList/PageResult (L6), run tables and spans (L5), resolved tree (semantic backend only), element html sections, T4's generated run-attribute mapping and contract CSS, settings render.*/copy.*/a11y.*.

**Produces:** Per-block HTML with keys, anchors table, container contract, contentText, preview fragments.

**Must not:** Decide positions, gaps, keeps, eqno/marker placement, run boundaries or breaks (today typeset_html.cc:226-249, 531-567); read Config, FlowUnit or ContentNode in typeset/paged backends; call TextRules or classify codepoints; keep cross-block state (today lastAnchored); mint ids outside AnchorNamer; write CSS values not validated by T4 (today typeset_html.cc:416-421 writes two style attributes).

### H Host runtime (owners T9 driver/resources, T7 shell)

**Owns**

- drive(doc, target) loop shared by worker, Node export, tsrc, golden runner and fuzzers
- ResourceHost/ResourceLocator/ProviderSet/LruCache with one timeout and failure policy and one references manifest
- Session: content-keyed answers, in-engine answerers, memo slots (BreakMemo)
- Worker mailbox with per-doc serialization and generations; fork-on-rebuild
- Shell core (sessions, single commit path, core copy) + Behavior registry (refPreview, print, audit); static export bundle; project driver

**Consumes:** Products via tsr2_get, needs via tsr2_requests, the settings JSON, host-registered providers/capabilities/behaviors.

**Produces:** Answers, settings patches, declared inputs, DOM commits, export bundles.

**Must not:** Scrape DOM structure or label spellings (today shell.mjs:146, 175); re-break or measure outside requests; pass functions across the worker boundary; mutate a live doc for a different target (today worker.mjs:228-246 calls tsr_set_width on the live doc); hold engine pointers or StyleIds across documents.

### X1 Shared schema and generated vocabularies (owner T2; sections contributed)

**Owns**

- schema.json + schema.lock.json (immutable ids and since values): kinds with level/body/slot models, per-kind AttrSpecs with domains, ctor ParamSpecs incl. aliasOf property paths, DECL namespaces with binding mode
- Sections: T4 property/settings rows (Gran, Effects, Precedence, Affects, Css), T3 element/counter/collector records, T8 math rows, T7 render/copy settings rows, T6 trait property rows
- tools/gen-schema.mjs outputs committed with CI freshness checks

**Consumes:** Rows contributed by section owners.

**Produces:** C++ reader/accessors/KindInfo/normalizer tables, JS ctors/binder/opbuf, dumps, docs, settings accessors and per-stage views, schemaHash for the ABI handshake.

**Must not:** Allow hand-maintained mirrors of any row; reuse an id; change a row's meaning without a MIN_COMPAT bump; carry JS lowering templates (those are L1).

### X2 Settings and style cascade (owner T4; transport and Affects domain T9)

**Owns**

- Property registry rows (Run/Extent/Block/Doc granularity; MEASURE/SHAPE/PAINT/RENDER effects; inherits flag)
- Settings registry rows with Precedence HostOnly/HostDefault/HostForce/DocOnly and Affects subset of stages.def
- ScopeDelta (patch + optional selector) through four carriers: schedule stack, node own style, host rules, engine defaults
- Faces (FaceKey, faceOf, emPx), ClassChannel, LocalePack container, generated contract CSS and rulesToCss

**Consumes:** Schema generator; stages.def for Affects.

**Produces:** CascadeState per node, DocProps, FaceIds, MetricKey inputs, generated CSS, validated settings.

**Must not:** Let PAINT rows reach geometry or MetricKey; register letter-spacing/word-spacing as author properties; key behaviour on class names; let rules add or remove classes.

### X3 Element registry (owner T3; sections T4 style, T6 layout, T7 html)

**Owns**

- One row per element class: name, like, select (membership selector), semantic columns, slot list, owner sections
- engine/data/elements.json built-in rows in the user data form
- Declaration through $.element (hoisted DECL) or settings elements.<name>

**Consumes:** DECL records, host settings, built-in rows.

**Produces:** ClassIds and per-section compiled tables consumed by L3-L7.

**Must not:** Let any consumer read a role string; let a section owner keep a private parallel table (risk noted by T3).

### X4 Pipeline, resources and diagnostics (owner T9)

**Owns**

- stages.def with rerun classes (target after T6 S3: Compile, Execute, Ingest, Resolve, Shape(Emit), Measure, BoxTree, Layout, Paginate, Paint), products.def, tsr2_ C ABI, one tsr2_abi handshake
- resources.def with the admission rule (post-Ingest, engine-computed content keys) and inputs.def (pre-Ingest declared blobs: labels, mathFont)
- diagnostics.def codes with origins (Stage/pid, Settings, Input, Resource, Execute)

**Consumes:** Stage implementations, settings views.

**Produces:** Typed needs/answers, fork/rebuild decisions, sliced diagnostics JSON.

**Must not:** Add a per-feature pull channel, export or worker branch; key answers on presentation (StyleId) or ambient state; add invalidation APIs (complete keys instead).

## Shared mechanisms

### Shared schema + additive vocabulary versioning (owner T2-constructor-ir)

Used by: T1-surface-frontend, T3-semantics, T4-style-settings, T5-text-shaping, T6-layout-pagination, T7-render-runtime, T8-math, T9-host-protocol

One schema.json/schema.lock pair holds kinds (level, body/slot models, inline fallback), per-kind AttrSpecs with value domains and write order, ctor ParamSpecs (aliasOf property paths allowed), DECL namespaces with binding mode, and the props/settings rows contributed by T4/T6/T7/T8. Every row carries an immutable `since`; a buffer's version byte = max(since of vocabulary it uses); the reader accepts MIN_COMPAT..OPS_VERSION; a meaning change raises MIN_COMPAT. Generated outputs (C++ reader/accessors, JS ctors/binder/opbuf, dumps, docs, settings views) are committed and CI-checked. Replaces the flat ARGK namespace (ops.def:43-75) and the exact version check (ops.cc:100).

### SyntaxTable (syntax.def) and front-end exports (owner T1-surface-frontend)

Used by: T2-constructor-ir, T8-math, T9-host-protocol, T7-render-runtime

Lexical/structural facts only (classes, delimiter rows with closed body modes, block rules, keyword forms, SUGAR rows with slot + payload). T2's sugar.def keys on SugarId; T8 contributes the math island row (escape policy raw, holes parameter); T9 runs the exported tokenizer as the in-engine 'tsm' codeTokens answerer; tree-sitter/TextMate/VS Code/converters read syntax.gen.json and are conformance-tested with an allow-list. syntaxVersion is part of the ABI handshake.

### One lowering + constructor Registry + frames (owner T2-constructor-ir)

Used by: T1-surface-frontend, T3-semantics, T4-style-settings, T6-layout-pagination, T7-render-runtime, T8-math, T9-host-protocol

Markup lowers once (AST -> LowerProgram) and runs on one interpreter for documents, m``, ctx.m.parse and sidecars. Every ctor (built-in or user) has the bound convention ctor(opts, ...content); a region is a ctor with a Body param, so #!name(H)...#name! == #name(H)[...]; fences and formatters are registry entries; overrides delegate with next and apply from their registration point to sugar, explicit calls and fragments alike (no separate $.sugar namespace). Frames wrap every block and hook call: containment to an error node + diagnostic, style-height restore, span stamping. Semantics are pinned by T1's LoweringContract fixtures.

### DECL channel with declared binding modes + content events (owner T2-constructor-ir)

Used by: T3-semantics, T4-style-settings, T5-text-shaping, T8-math, T9-host-protocol

One DECL op {ns, name, typed payload, node-valued templates instantiated style-neutral, flow anchor = EMITs before it + span}. Each namespace declares its binding in the schema: hoisted (element, counter, collector, counter-system, doc, locale incl. text tailoring, fontRoles: last-wins, 'decl-after-use' info) or positional (math symbol/op/fn: visible to nodes whose declEpoch >= anchor; epoch stamped at instantiate and preserved when L4 moves or clones nodes). Phase 0 reads RawOps.decls after decode, so no wire ordering constraint. Reading-order state changes (appendix counter switch, setcounter) are `event` content nodes, not DECLs. `$.set` rules are schedule entries, not DECLs.

### Element registry (element classes with owner sections) (owner T3-semantics)

Used by: T2-constructor-ir, T4-style-settings, T6-layout-pagination, T7-render-runtime, T8-math, T9-host-protocol

A class is a row named by the universal `role` attr: like-inheritance, membership selector, slot list, semantic columns (T3), style section (T4 rules compiled into the default layer), layout section (T6 traits: keep, box, place...), html section (T7 PresentationMap). Declared by $.element (hoisted DECL, returns a ctor via defineCtor) or settings elements.<name>; built-ins are rows in engine/data/elements.json in the same form. Consumers read ClassId + their section only. Replaces role-string tests at resolve.cc:166, emit.cc:781, semantic_html.cc:286, shell.mjs:175, and supersedes T7's render.classes key and T6's $.role.

### Unified Selector (owner T4-style-settings)

Used by: T3-semantics, T6-layout-pagination, T7-render-runtime

Selector {kind, role, cls, where[], textLang, depth, inside} evaluated at L3, node-local plus cheap top-down context, no combinators. Membership selectors (T3) may use only kind/where/inside; style rules may use all keys; rules never set role or class; membership never reads style-derived keys, so there are no cycles. rulesToCss maps keys to T7 DOM hooks (data-role, .tsr-c-x, data attributes, :lang()).

### Property and settings registry + emission-time Cascade (owner T4-style-settings)

Used by: T2-constructor-ir, T3-semantics, T5-text-shaping, T6-layout-pagination, T7-render-runtime, T8-math, T9-host-protocol

Rows declare granularity (Run/Extent/Block/Doc), effects (MEASURE/SHAPE/PAINT/RENDER), inheritance, domain, Css mapping, settings Precedence and Affects. One ScopeDelta type travels through the schedule stack, node own `style`, host rules and engine defaults. CascadeState {Styling, NodeProps, RuleEnv, scope} is computed per node at instantiate; Cascade.make/lift is the only factory after instantiation (resolver, sidecars, fragments, token folding, math text). Precedence lowest->highest: engine kind defaults < built-in element style/layout sections < host rules < document element sections < document $.set rules in scope < node own style/declared aliases. T6's BlockTraits is the compiled Block-granularity view of NodeProps, not a second table.

### Settings document ABI, per-stage views and fixture profiles (owner T9-host-protocol)

Used by: T4-style-settings, T1-surface-frontend, T3-semantics, T6-layout-pagination, T7-render-runtime, T8-math

tsr2_set_config(json) -> {diagCount, rebuild: none|REBUILD|REEXECUTE}; sections registered by owning themes through the T4 codec (unknown key = warning, wrong scope = config-scope); generated per-stage settings views make undeclared reads a compile error; X.fixture.json {profile, settings, inputs} replaces filename conventions (engine/test/tests.cc:417-424); --profile=golden reproduces every golden.

### LocalePack (owner T4-style-settings)

Used by: T3-semantics, T5-text-shaping, T8-math

Language-keyed data packs with likely-subtags + CLDR parent chain (zh-TW -> zh-Hant -> und; never zh-Hant -> zh), selected by effective text.lang (terms use termsLang, which defaults to text.lang). Sections: terms (keys owned by T3), text tailoring and hyphenation (T5), autos for `auto` property values. One extension surface: $.locale(tag, {base, terms, text, autos}) as a hoisted DECL, plus the `locales` settings section. Packs carry data, never presentation. Replaces applyLang (config.h:89-102).

### Instantiation normal form (owner T2-constructor-ir)

Used by: T3-semantics, T4-style-settings, T5-text-shaping, T6-layout-pagination, T8-math

normalize(subtree, position) is pure and generated from KindInfo (levels, body/slot models, opaque models); it runs in instantiate and must be called on every subtree L4 synthesizes (debug assert 'emit-unnormal'). T5's closed flatten table and T6's layouter choice are generated from the post-normalization kind set; block-in-inline splits into para/block/para{cont} once T6/T7 render cont.

### Universal attributes, slots and provenance (owner T2-constructor-ir)

Used by: T1-surface-frontend, T3-semantics, T5-text-shaping, T6-layout-pagination, T7-render-runtime, T8-math

Every kind accepts label, role, slot, class, style, syn, copy and EXT (scalar user data). `slot` is the single part marker (caption, title, body, tag, term, marker, margin); per-kind SlotSpecs (codeblock.margin, any block's tag) and per-class slot lists live in schema/element rows. Spans are per occurrence (AT), contained in the parent; T1 cooked-to-raw maps travel onto text nodes for T5 per-item spans and T7 data-s. A node without a real span must carry a syn kind.

### TextRules / CC classifier (owner T5-text-shaping)

Used by: T6-layout-pagination, T8-math, T1-surface-frontend, T7-render-runtime

cpInfo() is the only codepoint classifier (replaces five: support.h:158-190, inline.cc:50, emit.cc:396/841, layout.cc:239-250, typeset_html.cc:402). Consumers outside shape/ are forbidden after the shaping wave; until then the interim join predicate lives once in support/ (T1) and the grid uses the shaper's Grid mode (T6); T8 maps atom classes to edge CCs; the mock measurer keeps a frozen literal copy.

### HList items, InlineObject protocol and run instances (owner T5-text-shaping)

Used by: T6-layout-pagination, T7-render-runtime, T8-math, T9-host-protocol

ItemKind {Box, Glue, Penalty, Disc} with TeX legality (no stand-alone Kern), glue class + attrs, ColdRec spans. InlineObject kinds expand structure metric-free and resolve extents either at Measure (math, image, raw; needs through the single NEED_RESOURCES state with per-pid waiters) or at Layout (width-dependent hbox/InlineBlock via T6 layoutDetached). Edge CCs decide pair rules at object edges. A run instance (face, link, syn, copy, RealizeClass, anchor) is the only run boundary for KernCtx, DOM runs and copy. KP never runs on a pid with pending objects.

### LineBreaker semantics + BreakMemo key (owner T6-layout-pagination)

Used by: T5-text-shaping, T8-math, T9-host-protocol

measureLine defines natural width, discard after breaks, Disc pre/post/nobr, Forced breaks (hardbreak, paragraph end) with LineEnds.last*, rescue instead of one-line collapse (break.cc:122). BItem is a padding-free su projection used as the exact cache key; T9 stores BreakMemo in a Session memo slot and compares key bytes on hit.

### Layout output contract (VList + Fragment + PageResult) (owner T6-layout-pagination)

Used by: T7-render-runtime, T3-semantics, T9-host-protocol

Frame-relative Fragments {kind, painter, x, y, w, h, ascent/baseline, item range, PaintFit, Sep + sepText, track, gutter Placement, PaintClassId, slot, anchorId, flags} in DOM order; exactly one anchored fragment per anchored block; PageResult slices without copying. T7's LaidOut* fields are folded into Fragment (no second record). Realised gap px are computed in paint from PaintFit.ratio x item raw stretch with T5's (glue class x RealizeClass) carrier table, so layout stays ignorant of glue classes.

### Label and anchor identity (AnchorId) (owner T3-semantics)

Used by: T5-text-shaping, T6-layout-pagination, T7-render-runtime, T9-host-protocol, T1-surface-frontend

Only registry winners get AnchorIds; aliases are built from raw counter components or class ordinals with shape-reserved prefixes (h-, fn-, fnref-, bib-, eq-) generated from AliasRule rows; LabelChar comes from T1's syntax table. T5 marks anchor points on runs, T6 puts one anchorId per anchored block, T7's AnchorNamer is the only speller (prefix + escaping, byte-identical 'tsr-' default). Refs store target AnchorIds, never '#tsr-' URLs.

### SynKind vocabulary, CopyPolicy and ContentText (owner T7-render-runtime)

Used by: T2-constructor-ir, T3-semantics, T5-text-shaping, T6-layout-pagination, T8-math

Two orthogonal attrs: syn (open kind name; absent = content) and optional copy override (text|omit|replace(s)); default copy mode per syn kind from copy.policy, overlaid by the class's html section. T3 tags generated text, T8 tags math as replace(source), users use $.synthetic. T6 produces Sep from materializeLines; T7 owns its DOM encoding (data-syn, data-copy, data-join) and the native contentText projection.

### HtmlWriter + generated run-attribute mapping + contract CSS (owner T7-render-runtime)

Used by: T4-style-settings, T5-text-shaping, T8-math, T3-semantics

T7's HtmlWriter is the only place elements, attributes, styles and ids are assembled (allowlists, one style attribute, one escaper, trustedRaw). T4 supplies the pure generated mapping runDecls(FaceId, StyleId) -> (contract classes, validated paint declarations, lang-if-different), contract.gen.css and rulesToCss; no writer-side sanitizer because values are validated at ingest.

### Typed resource protocol and declared inputs (owner T9-host-protocol)

Used by: T4-style-settings, T5-text-shaping, T6-layout-pagination, T8-math, T3-semantics, T1-surface-frontend, T2-constructor-ir

RES rows (textWidth, fontVmet, fontFace, codeTokens, boxInfo, hyphenPatterns) admitted only for post-Ingest data with engine-computed content keys; complete MetricKey = FaceKey (T4) + sizePx + lang + features + dppx + faceDigest; failures degrade the consuming quantity locally with a diagnostic; one NEED_RESOURCES state with per-pid deferral. Pre-Ingest data are declared inputs (inputs.def: labels manifests, mathFont .tsmf), validated by fuzzed decoders and never shown to scripts. Execute-time loads (ctx.load, #use, bibliography JSON) share the locator, cache, failure policy and manifest but not the engine pull.

### Stage model, products, ABI handshake and diagnostics (owner T9-host-protocol)

Used by: T1-surface-frontend, T2-constructor-ir, T3-semantics, T4-style-settings, T5-text-shaping, T6-layout-pagination, T7-render-runtime, T8-math

stages.def with rerun classes drives Affects, products, diagnostic origins and fork-on-rebuild; edited in lockstep with T6 (Break folds into Layout after T6 S3; BoxTree and Paginate are stages). One tsr2_abi() at module load returns {opsWindow, schemaHash, programAbi, resVersion, renderVersion, syntaxVersion}; ops buffers carry only the version byte. Diagnostics carry origin + pid + source; executor warnings arrive as DIAG records in the ops buffer (origin Execute), so they are golden-visible.

### Content-keyed Session cache (owner T9-host-protocol)

Used by: T6-layout-pagination, T7-render-runtime, T4-style-settings, T5-text-shaping, T8-math

Answers and memos keyed by complete content bytes (MetricKey, BreakInput, block-HTML value hash, MathFont content hash); hits are copied into the Doc; no StyleId/StrRef/NodeId crosses documents; no invalidation calls; fresh Session per golden fixture and a warm-vs-fresh differential test.

## Conflicts resolved

### T1-surface-frontend / T2-constructor-ir / T5-text-shaping

**Issue:** Soft-break representation: T1 adds a `softbreak` ops kind (S13); T2 keeps a JS-only Node serialized as today's join text until T5; T5 wants U+000A inside cooked text.

**Resolution:** One meaning, two representations by layer. L0/L1/L2 keep a structural SoftBreak (AST node, SOFTBREAK program op, JS-only Node) because body.rows() and provenance need it. On the wire it is U+000A inside inline-model text: no new kind, MAKE_TEXT stays dominant and adjacent text stays coalesced (I9). The schema documents that a newline in an inline-model text leaf is a soft break (code/verbatim body models unaffected). It lands in the P2 MIN_COMPAT wave, with emit resolving it through the cjkish predicate extracted unchanged from inline.cc:48-59 into support/ (blocks/html byte-identical); inline.cc:48-59 is deleted then. P4 (T5 step 5) swaps the predicate for TextRules joinsWithoutSpace.

### T2-constructor-ir / T3-semantics / T4-style-settings / T5-text-shaping / T8-math

**Issue:** Declaration binding: T3 wants hoisted position-free records (and asks whether they must precede nodes on the wire); T8 wants schedule-ordered decls with declEpoch; T4 doc-wide values on a DECLARE channel; T5 document-global last-wins tailorings; T2 offers one DECL op with flow anchors.

**Resolution:** One DECL op (T2 codec) whose namespace declares its binding mode in the schema: hoisted for element/counter/collector/counter-system/doc/locale (incl. text tailoring)/fontRoles; positional for math.* (epoch stamped at instantiate copy, carried through resolver moves and clones). No wire ordering requirement: the reader decodes the whole buffer into RawOps before model.cc instantiates, so Phase 0 builds the registry from RawOps.decls. Counter state changes stay `event` content nodes; `$.set` stays a schedule entry.

### T3-semantics / T7-render-runtime / T4-style-settings

**Issue:** Where per-class presentation is declared: T7's PresentationMap through the settings key render.classes.<cls> (with $.element render: as sugar) vs T3's html section of element rows.

**Resolution:** One record per class: the element row with sections semantic (T3), style (T4), layout (T6), html (T7). It is declared by $.element (hoisted DECL) or by the settings section elements.<name>; both decode to the same record and the settings codec delegates section validation to each owner. render.classes is dropped. presentation.def becomes the html columns of engine/data/elements.json. T7 still owns the html schema and its allowlists.

### T6-layout-pagination / T4-style-settings / T3-semantics

**Issue:** Role defaults: T6 S12 wants $.role/$.set as global, order-independent role defaults (kind default < role default < rule < explicit arg); T4's $.set is a positional rule on the schedule stack; T3 has element rows.

**Resolution:** There is no $.role. Order-independent role defaults are the element row's style/layout sections (hoisted), which gives T6 what it asked for. $.set keeps positional Typst-like semantics on the schedule stack (I6 as amended by T4 M6). Precedence, lowest first: engine kind defaults (defaults.json, env 0), built-in element sections, host rules (env 1), document element sections, document $.set rules in scope, node own style or declared aliases.

### T6-layout-pagination / T4-style-settings / T2-constructor-ir

**Issue:** Trait surface: T6 wants top-level args (box:, place:, keep:, space:...) as a cross-kind attribute group with its own wire keys; T4 allows only the style: meta-arg; T2 removed the region meta-arg sniffing (executor.mjs:131-137).

**Resolution:** One vocabulary: T4 property rows (box.*, space.*, keep, break, place, beside, par.*, align, hyphenate, breaker.*, media), validated once. The canonical surface is style:{...} on any ctor, region or fence. A built-in ctor's ParamSpec may declare aliasOf rows, so #!figure(place: {...}) is accepted, bound and stripped by the binder and never sniffed. User ctors get style: handling plus any aliases they declare. Traits travel on the wire as the node's own style delta (T4 M5), so T6 S12 needs no batched OPS bump.

### T3-semantics / T4-style-settings

**Issue:** Two selector languages: T3 membership selectors (kind, preds, inside) and T4 rule selectors (kind, role, cls, where, textLang, depth).

**Resolution:** Use one Selector struct and matcher at L3, owned by T4: {kind, role, cls, where[], textLang, depth, inside}. Membership may use only kind, where and inside. Rules may use every key but never set role or class. Membership specificity follows T3; rules are ordered by sequence (CSS :where semantics). Neither side can create a cycle.

### T2-constructor-ir / T3-semantics / T4-style-settings / T6-layout-pagination / T7-render-runtime

**Issue:** Part naming: T3 part:'caption', T2 universal slot, T4 'slot roles', T6 'typed slot marks', T7 SlotId.

**Resolution:** Use the single universal attr `slot`. Slot lists are declared per kind (T2 SlotSpecs) and per element class (T3 rows). Built-in ctors set slot from their ParamSpecs at L2, for example the figure caption, so L3 selectors and T6 caption layout can see it.

### T3-semantics / T4-style-settings / T5-text-shaping / T1-surface-frontend

**Issue:** No-break attachment of note markers is spelled four ways: T3 `bind` attr, T4 `text.attach` Extent property, T5 `attach` edge attribute, T1 attach-left.

**Resolution:** One T4 property row `attach` in {none, prev, next, both} at Extent granularity, so it does not inherit and applies to the first/last atom of the node's extent. T5 owns its meaning: an INF penalty plus Blank displacement. It is set by the footnote-marker class default, by templates, by any style:, or by user ctors. emit.cc:89-90 (CLS_SUP -> BREAK_INF) is deleted at T5 step 10 and the parser's space relocation (inline.cc:384-397) at T1 S14, both in the P4 wave.

### T3-semantics / T6-layout-pagination / T7-render-runtime / T8-math

**Issue:** Equation tags and multi-row displays: T3 puts the tag in mathblock's `tag`; T8 wraps rows as equation{label}(math, tag) inside group{role:'equations'}; T6 uses Replaced{rows[{..., tag}]} plus a generic margin tag; T7 maps a tag slot.

**Resolution:** `tag` is a universal block slot (T2 SlotSpec, allowed on math, codeblock and any block), filled by T3's Site{where:Tag}. Math kids stay fragments and holes, which meets T8's requirement without a wrapper node. Multi-row displays become a typed schema kind `equations` (Block; body = display-math rows) instead of group{role:'equations'}, so there is no role-string dispatch. Each row is a math{display} node that T3 numbers and labels generically. T6 lays the kind out as one Replaced block with rows: shared alignment columns, a margin tag per row with the collision rule, breaks between rows. The compat ArgK::name string goes away at T3 S4, once tags are measured.

### T5-text-shaping / T6-layout-pagination

**Issue:** T6's BItem lists a Kern item kind; T5 rules out a stand-alone Kern.

**Resolution:** T5 owns ItemKind = {Box, Glue, Penalty, Disc}. Junction kerns live inside Glue/Disc widths (KernCtx), and no producer needs TeX's discardable kern. The Kern enum value is reserved and T6's discard rule is reworded without it.

### T5-text-shaping / T8-math / T9-host-protocol / T6-layout-pagination

**Issue:** Inline objects and pending resolution: T5 has a two-phase ObjectKind with Deferred; T8 an InlineObject with `pending` and a private finalize hook in resolveWidths; T9 per-pid deferral with waiters; T6 InlineBlocks sized by layoutDetached before breaking.

**Resolution:** T5's ObjectKind is the only protocol. T8's lbStart/lbEnd map to firstCC/lastCC through a T8 table, and `pending` becomes resolve() returning needs. Metric-resolved objects (math, image, raw) resolve at Measure through T9's single wait state with per-pid waiters, with no private hook. Width-dependent objects (hbox/InlineBlock with Percent width, subfigures) declare resolveAt: Layout and are sized by T6's Paragraph layouter before breaking.

### T6-layout-pagination / T7-render-runtime

**Issue:** Two layout output records: T7 consumes LaidOutBlock/Line/Box (seeded by T7 S3) and wants GapReal values from T6; T6 defines VList + Fragment.

**Resolution:** T6's VList/Fragment is the only L6 output. It absorbs the fields T7 needs: Just via the LineEnds preset, Track, Sep and sepText, LineMarks, gutter Placement, LineStyle and baseline. T7 S3's adapter emits Fragments. Gaps are not stored by layout: paint computes px = PaintFit.ratio x the item's raw stretch, with the carrier taken from T5's table.

### T5-text-shaping / T7-render-runtime

**Issue:** CJK punctuation blanks: T5 step 7 replaces tsr-sqL/R with explicit px margins; T7 wants to keep the contract class to avoid churn.

**Resolution:** T5 wins. The class encodes a 0.5em constant evaluated in a CSS em that differs from emit's em (shell.mjs:41-42 vs emit.cc:251). It cannot express per-scope punct modes or pack tailoring, and it breaks v2 §7's explicit-spacing rule. Explicit px lands at T5 step 7 inside the P4 wave, where CJK html churns anyway. Until then the class is generated from the T5 table by T4's contract generator.

### T5-text-shaping / T7-render-runtime / T2-constructor-ir

**Issue:** Copy attribute shape: T5 has one attr copy = Content|Synthetic(kind)|Replace(text); T7 has syn (open kind) plus copy (text|omit|replace).

**Resolution:** Adopt T7's two orthogonal attrs: syn names the kind and copy optionally overrides the per-kind default from copy.policy. T5's run key is (face, link, syn, copy, RealizeClass, anchor). T2 owns the rows (additive since values) and T7 owns the vocabulary and policy.

### T4-style-settings / T9-host-protocol

**Issue:** Two settings ABIs and fixture formats: T4's ConfigCodec/tsr_set_config/<fixture>.settings.json, with an open 'force layer' question; T9's tsr2_set_config with REBUILD/REEXECUTE, a precedence lattice including HostForce, and X.fixture.json.

**Resolution:** One landing in P1. T4 owns rows, codec, validation and generated accessors. T9 owns the ABI (tsr2_set_config -> {diagCount, rebuild}), the Affects domain (stages.def), per-stage views, profiles and the fixture format X.fixture.json {profile, settings, inputs}, which absorbs T4's .settings.json. Precedence is T9's lattice, which answers T4's force-layer question for settings rows.

### T2-constructor-ir / T4-style-settings / T7-render-runtime / T9-host-protocol

**Issue:** Skew detection is proposed three times: T2's per-buffer since versioning plus an RT_ABI hash, T4's VOCAB_HASH in every ops header and in settings $vocab, and T7/T9's single module-load handshake.

**Resolution:** The ops header keeps only the version byte (vocabulary needed; reader window MIN_COMPAT..OPS_VERSION; amend architecture.md:127). One tsr2_abi() handshake returns opsWindow, schemaHash (= T4 VOCAB_HASH, because props and kinds share one schema), programAbi (= T2 RT_ABI), resVersion, renderVersion and syntaxVersion. The worker, Node and the hole module's abi export check it. Settings may carry $schema, and a mismatch raises a 'vocab-skew' warning. No hash goes into each buffer.

### T3-semantics / T9-host-protocol

**Issue:** Cross-document labels: T3 has $.labels.import emit entry{role:'external'} rows into .ops and iterates up to 3 project rounds; T9 makes manifests declared inputs given to the engine before Ingest, invisible to scripts, with a two-pass driver over start-independent values.

**Resolution:** Take T9's transport: it keeps I4 (no script reads another document's resolved values), the decoder is fuzzed, and fixtures declare inputs so goldens need no I/O. Take T3's semantics: table 'external' rows, label-first lookup with ref-shadowed, and the importer formats raw components with its own templates and terms. The driver runs Pass A, then Pass B, plus one extra pass only when a manifest title is flagged unstable (it contains external refs), capped at 3 with 'project-unstable'.

### T8-math / T9-host-protocol

**Issue:** Math font transport: T8 wants a mathFont{hash} RES that gates emit; T9's admission rule makes anything known before Ingest a setting or input.

**Resolution:** A declared math font is known before Ingest, so it is a declared input (inputs.def mathFont, .tsmf validated like ops) loaded by the host before Ingest. MathFontRegistry caches by content hash process-wide. No pre-emit wait state is added, and Euler stays embedded.

### T3-semantics / T4-style-settings / T5-text-shaping

**Issue:** Language of generated terms: T3 wants termsLang = setting ?? document lang (so a zh-TW glyph scope keeps 图), with an exact -> script -> lang -> root chain; T4 selects packs by effective text.lang with CLDR parents (zh-Hant -> und).

**Resolution:** Use T4's fallback chain; T3's lang step would map zh-Hant to zh-Hans strings. The selection key is termsLang, a T4 Block row that inherits and defaults to the effective text.lang, so `lang:` on a scope switches terms, hyphenation and punctuation together (one language per scope). A scope that only wants glyph variants sets termsLang or a font role explicitly. Golden-neutral: the style/patch zh-TW scopes contain no refs, and the golden profile is zh-CN.

### T4-style-settings / T5-text-shaping

**Issue:** Two locale extension surfaces: T4's $.locale(tag, partialPack) and T5's $.text.tailor(tag, {...}).

**Resolution:** One surface: $.locale(tag, {base, terms, text:{classes, defined, hyphenate}, autos}) as a hoisted DECL plus the `locales` settings section. T5 owns the text section schema and the TextRulesId digest; T3 owns the term keys.

### T4-style-settings / T7-render-runtime

**Issue:** Two run-attribute writers: T4's CssEmitters.runAttrs vs T7's HtmlWriter.style().

**Resolution:** T7 owns the writer. T4 owns the generated pure mapping runDecls(FaceId, StyleId), contract.gen.css and rulesToCss. No CSS string is produced outside that mapping.

### T1-surface-frontend / T2-constructor-ir

**Issue:** Lowering mechanics: T1 assumes documents print a CallTree as JS with an __s namespace, $.sugar.<slot> rebinding and a 'let-redeclared' error; T2 compiles to a LowerProgram plus a user-code hole module on one interpreter, with ctor trampolines, and makes repeated #let legal.

**Resolution:** T2's LowerProgram is the target, amending v2 §2 (design-decisions-v2.md:50), and is gated on the I9 bench at T2 S5. T1's printed JS from the same CallTree is the fallback; the P0 interim containment (reserved namespace plus per-block try) is its first step. T1's LoweringContract fixtures define the semantics and run against both. Rebinding goes through ctor overrides, so $.ctor('note', next => ...) retargets ^[...] and #note[...] together; there is no $.sugar. Repeated #let is allowed and lowers to reassignment of the hoisted binding (Typst habit); docs note that closures see the latest value.

### T1-surface-frontend / T2-constructor-ir / T5-text-shaping / T6-layout-pagination

**Issue:** Hard-break timing: T1 S10 maps backslash+EOL to a linebreak, while emit silently drops hardbreak (emit.cc:170-172) and T2 refuses to expose a ctor that renders nothing.

**Resolution:** Land in this order: T6 S1 (Forced penalty semantics), then T5 step 3 (the closed flatten table maps hardbreak to a Forced penalty through fuseLegacy), then T1 S10's escape together with T2's public linebreak ctor in one commit. Until then backslash+EOL keeps today's behaviour.

### T2-constructor-ir / T8-math / T6-layout-pagination / T7-render-runtime

**Issue:** Mid-paragraph display math: T2 keeps mathblock INLINE_FALLBACK (degrade to inline); T8 decides to split like Typst; T2 S8b is gated on para{cont}.

**Resolution:** Adopt the general rule: a block in inline position at a Blocks position splits into para, block, para{cont}, math included. mathblock's INLINE_FALLBACK row is removed in P3 once T6/T7 render para{cont} (no first-line indent, no paragraph gap). There are 0 corpus cases.

### T9-host-protocol / T6-layout-pagination

**Issue:** T9's stages.def has a Break stage before Layout; T6 moves breaking into layout (floats need live exclusions) and adds BoxTree and Paginate stages.

**Resolution:** stages.def is edited in lockstep with T6. After T6 S3 the order is Compile, Execute, Ingest (decode + Phase 0 + instantiate + cascade), Resolve, Shape(Emit), Measure, BoxTree, Layout (breaking inside), Paginate, Paint. After T6 S4, viewport.width affects Layout only (T9 M12).

### T9-host-protocol / T2-constructor-ir

**Issue:** Sidecar extraction: T9 places the pass in Ingest; T2 moves it into the default fence in JS (codeblock.margin slot) and deletes Doc::extractSidecars (doc.h:88-150).

**Resolution:** Follow T2: sidecars are content produced by a constructor, and Ingest does no content rewriting.

### T9-host-protocol / T2-constructor-ir

**Issue:** Executor warnings: T9 merges host executor diagnostics in drive(); T2 adds a DIAG op to the ops buffer.

**Resolution:** Use the DIAG op (origin Execute, spanned). It is the executor's only channel to the engine and makes warnings visible in recorded .ops goldens. drive() merges nothing on the side.

### T5-text-shaping / T6-layout-pagination

**Issue:** Stretch model: App C (design-decisions-v2.md:369) fixes CJK breaker capacity at 0.1em; v2 §8 (design-decisions-v2.md:196) and document-model.md:174 promise weights n_latin + k*n_cjk that agree with the renderer by construction.

**Resolution:** v2 §8 is the architectural rule and App C is amended. It is adopted at the single T5 step 11 / T6 S16 churn step (capacity = sum of weights x juSu), with T6 recalibrating shrinkThreshold and the CJK owner signing off. Until then the HList stores both quantities exactly (ColdRec.capSu).

### T6-layout-pagination / T7-render-runtime / T3-semantics / T4-style-settings

**Issue:** List markers: T6 MarkerSpec Push/OwnLine needs marker widths; T7 says markers are never measured and right-anchored; T3 puts numbers in SemInfo; T4 has an open question on the list.marker domain.

**Resolution:** T3 counters number the items. The pattern is T4's list.marker row, whose domain is T3's NumberingPattern (answers T4's open question). By default a marker is unmeasured paint-only text right-anchored at the gutter (golden-neutral). Push/OwnLine are opt-in and switch the marker to a measured ItemList.

### T4-style-settings / T5-text-shaping / T9-host-protocol

**Issue:** MetricKey contents: T4's FaceKey reserves lang and features; T5 and T9 include them.

**Resolution:** FaceKey (T4) is face identity: resolved family stack, weight, italic, caps. MetricKey (T9 encoding) = FaceKey + sizePx + lang + features + dppx + faceDigest. lang is included because locl variants are chosen by lang. FaceId interning still merges colour/link/token variants, fixing the StyleId-keyed fragmentation at measure.h:21.

### T6-layout-pagination / T7-render-runtime

**Issue:** T6 leaves open whether paint should emit inter-paragraph margins from su.

**Resolution:** Yes: layout owns every position (I2, no runtime repair). This is T7 S10, in its own commit: 33 html goldens change from 19.2px to 19.203px.

### T1-surface-frontend / T2-constructor-ir / T9-host-protocol

**Issue:** Fragment ABI: T1 returns a JSON CallTree, T2 a LowerProgram, T9 names tsr2_parse_fragment.

**Resolution:** One export, tsr2_parse_fragment(_many), returning LowerProgram bytes. AST JSON remains a tooling product (tsr2_get 'astjson', tsrc --stage=astjson).

### T2-constructor-ir / T3-semantics

**Issue:** T2's lazy region Body never runs an unread interior, but T3 needs statements in region bodies for definitions.

**Resolution:** Built-in ctors and the default region always force their bodies. A user handler that never reads its body skips that body's declarations, and the interpreter reports 'body-unread' (info) when the skipped interior contains statements.

### T4-style-settings / T6-layout-pagination / T3-semantics

**Issue:** Three default tables: T4 defaults.json (env 0), T6 TraitTable built-in defaults, T3 built-in element rows.

**Resolution:** Kind defaults live in defaults.json and class defaults in the built-in element rows' style/layout sections. TraitTable is only the compiled NodeProps view, so there is no third table. All of them must reproduce today exactly: 1pg root gap, 1/3pg list gap with integer division, 1.5em/1.0em indents, heading weight/size, table pads.

## Principles

- P1 Built-ins are pre-loaded rows. Every built-in kind default, element class, counter, collector, presentation, math family, locale pack, language manifest and settings default is a row in a data file, written in the same declaration form a document or host uses. C++ holds only closed primitives. Check: a CI lint rejects role/class/element/sugar/math-family name literals in engine/src outside gen/, data/ and diagnostics (allow-list); every registry has an equal-footing golden in which a document re-declares a built-in row under a new name and gets identical tree/layout/html apart from the name.
- P2 Dispatch on generated ids, never on spellings. No layer compares role strings, colour strings, label prefixes or kind names (today resolve.cc:166, emit.cc:618-622, emit.cc:781, semantic_html.cc:286, shell.mjs:175). Check: -Werror=switch-enum on all engine translation units, no `default:` over generated enums, and a grep lint for `strs.get(...) ==` and string_view literal compares in resolve/emit/layout/render and in the shell.
- P3 One source per vocabulary, generated everywhere: syntax.def, schema.json (kinds, attrs, props, settings, DECL namespaces), elements.json, resources.def/inputs.def, stages.def/products.def, diagnostics.def, languages.json, symbols.tsv/stdlib.tsv, url_policy.def, TextRules. Check: generated files are committed with a CI freshness check; any hand-written mirror (tree-sitter, TextMate) has a conformance test with an explicit allow-list.
- P4 Each layer reads only its declared inputs. Check: include lints (layout/break/paginate/render may not include model.h; typeset/paged render may not include config.h or emit.h; textrules.h only from shape/; api/ contains no typography); per-stage settings views make undeclared reads a compile error; nothing outside engine/src/api assumes a browser (I5), enforced by native tests with mocks.
- P5 Decide once, carry the decision as data. Break legality lives in the HList, run boundaries in the run table, positions/anchors/keeps/Sep in the VList, numbers and anchors in SemInfo. Downstream code cannot re-derive them because the raw inputs are not visible to it. Check: BF_* flags, FlowUnit, LineBox.special and ContentNode are unavailable to L6/L7; the DisplayList dump shows no classifier calls.
- P6 Sugar == constructor == region, for built-ins and users alike. Check: equivalence goldens `*x*` == `#strong[x]` == m`*x*` and `#!f(H) ... #f!` == `#f(H)[...]` at --stage=tree (modulo spans), before and after a $.ctor override.
- P7 One channel per concern, with explicit binding. Content travels as nodes; scoped presentation as deltas/rules on the schedule (emission-time, I6); definitions as DECL records with a declared hoisted or positional mode; reading-order state changes as content events; host data as settings; pre-Ingest blobs as declared inputs; post-Ingest needs as RES rows. Nothing binds by resolver walk order or by a node's post-resolve location. Check: the opcode, RES and INPUT lists change only with an architecture amendment; tests move and clone nodes (notes, TOC entries) and assert unchanged bindings.
- P8 No new mini-languages. The engine parses exactly two embedded languages: the math island grammar and NumberingPattern. Every other string sugar ('heading@1', 'prepend:first-para', selector shorthands) is parsed by the JS stdlib into structured records. Check: a lint on engine-side string parsers outside math/ and numbering.cc.
- P9 Total decoders, smallest-unit containment. Every boundary decoder (ops, LowerProgram, settings, inputs, RES answers, .tsmf, fragments) is total and fuzzed. Failure becomes an error node or diagnostic at the smallest unit (property, formula, pid, block), never at document level (I7). Check: one fuzz target per decoder in CI; containment fixtures where a throw, malformed header or bad formula in one block leaves the other blocks byte-identical.
- P10 Wire evolution is additive. Rows carry immutable `since`; only meaning changes raise MIN_COMPAT, and they are batched into named re-record waves; every golden diff is attributed to exactly one step. Check: schema.lock immutability, CI decoding of every recorded .ops, record-fixtures --check, and a per-step scripted 'only these attributes changed' diff.
- P11 Every rendered character has provenance. It carries either a real per-occurrence source span or an explicit syn kind with a copy policy. Check: the golden runner fails on @[0,0) outside declared synthetic roots and on a .tsr-line text run with neither data-s nor data-syn; the anchor-closure check passes (every internal href has exactly one id).
- P12 The measurement contract is encoded in the registry (I2). Only MEASURE rows enter MetricKey and metric CSS; PAINT rows map only to allow-listed non-geometric CSS; values are validated once at ingest; caches are keyed by complete content keys, never by StyleId/StrRef/NodeId across documents, and never invalidated by call. Check: the generator refuses a geometric Css column on a PAINT row; keys are std::has_unique_object_representations; a warm-vs-fresh differential test.
- P13 Determinism and performance are merge gates (I1, I9). Results are integer su with total-order ties, no unordered iteration in result-affecting code, and -ffp-contract=off. Each phase merges only if bench-edit at 7.8K/35K/87K stays within 5% of the previous phase and the editor fast path stays at about 6 ms for compile+execute+ingest. Check: lint plus CI bench jobs.

## Keep special

- **The rendering kind set stays closed. There are no user-defined kinds; new kinds such as `equations`, event and entry are engine schema rows.** — Layout stays closed under the kind table (document-model.md:66). Users get ctors, element classes, slots, EXT data and `field` on equal footing, without teaching emit and layout new nodes.
- **The keyword forms stay closed (#let/#if/#for/#while/#use), and there is no user-defined line-level syntax. Regions and fences are the open block forms.** — Loop variables must bind into content (v2 §3). Document-declared block rules would hit the registration paradox (v2 §4.1) and break the interruption guarantees. Config inline rows (T1 S15) remain optional.
- **Several CJK-first lexical choices stay as documented and are fixed by escaping:
- strict-pair emphasis;
- `$` always opens math;
- ASCII splice heads;
- the ` <id>` suffix only on unambiguous forms;
- Pair and link frames never own lines.** — These are documented decisions (v2 §5 l.157, §3). Heuristics would bring back the special cases this design removes.
- **Fragments (m``, m.parse) never evaluate splices that carry JS arguments.** — A tag function cannot see document scope, and synchronous eval conflicts with CSP. Values enter through holes.
- **The STYLE_PUSH/POP_TO schedule stays the only scope primitive, with `styled`/own style as the only node-level delta. letter-spacing and word-spacing are never author properties.** — v2 §12 commits to emission-time binding. Spacing is the engine's justification channel (v2 §8).
- **MAKE_TEXT stays a dedicated arg-less op, and soft breaks travel inside text.** — Text dominates the op stream (I9).
- **The base constructors stay sealed (text, seq, error, node), and built-in builders call base ctors directly.** — toContent, the interpreter and engine invariants depend on their exact meaning. Overriding them adds no expressiveness.
- **The engine parses only two embedded mini-languages: the math island grammar and NumberingPattern.** — Every other string sugar becomes structured records in JS, which keeps the fuzz surface and the parsers bounded (principle P8).
- **There are:
- no JS callbacks during or after resolve;
- no resolve<->paginate fixpoint;
- page numbers only in fixed-width slots;
- no per-page footnote numbering.** — I4 (the resolver is a pure single pass) and I2 (numbers must not depend on layout). Native post-ops goldens run without JS.
- **The following stay in userland JS:
- citation style logic (CSL, et al., disambiguation);
- collation sort keys;
- bibliography entry formatting.** — v2 §11.1 leaves CSL to userland, and the engine has no ICU. The formatter is a registry entry with the common ctx and frame.
- **One label namespace with shape-reserved aliases, not true namespaces.** — Published anchors (tsr-h-1.1, @fn-n) must not churn. Aliases built from raw components make reservation decidable.
- **The math core stays fixed:
- the TeX spacing matrix, Bin demotion and style tables;
- the closed C++ primitive table (14 layout primitives + 4 rewrites);
- codepoint painting with Euler embedded as the default;
- implicit names stay class Op.** — These are typographic law (TeXbook App. G). WASM cannot accept user layout procedures. Codepoint painting keeps paint identical to the precompiled metrics. Equal footing exists at the row level: every built-in family is a template a user could write.
- **Verbatim grid wrapping stays a greedy column algorithm, reachable only from verbatim content, with line-height-centred code rows.** — code-design §4 rules out Knuth-Plass for code by decision; monospace is a metric contract. What becomes general is its location, its policy data and its use as a table track.
- **There are no user-written layout algorithms (JS or WASM layouters). Host-measured Replaced boxes provide the escape hatch.** — Scripts never see measured values (I4), and the engine never calls synchronously into the worker.
- **The screen is one infinite page. Multi-column, vertical writing, RTL and complex-script segmentation are out of scope.** — v2 §10 and §14; progressive per-paragraph swaps (I8).
- **These stay closed or single-path:
- the paint primitive set;
- RawHtml as the single trusted unescaped path;
- element and attribute allowlists, extended only by an engine release.** — Every backend must implement every primitive. Each allowlist addition is a security review (document-model §9).
- **The semantic backend stays a pre-measure walk of the resolved tree, not a DisplayList consumer.** — First paint and Node export run without canvas measurement (v2 §9). It shares the writer, namer, PresentationMap, copy policy and math leaves.
- **The mock measurer's width classifier stays a frozen literal copy.** — It is the normative test oracle (testing.md §2). Regenerating the rule tables must not move every golden width.
- **Safety rails stay named constexprs, not settings: hl span limit, nargs <= 64, <64su guards, MetricStore packing bound. InstLimits is a HostOnly setting.** — They protect invariants, not typographic policy.
- **There are no estimate states, no dependency-recording product graph, and no persisted or shared Session.** — pages-design W ('no estimate states') supersedes architecture §2.4. v2 §9 makes a resize a full re-typeset, and arenas are per document (architecture §2.3). Forking from retained ops is cheaper than a graph.
- **Front matter is a host option, not a language construct. Tree-sitter and TextMate remain conformance-pinned approximations.** — Front matter is a static-site convention. Regex grammars cannot express strict pairing or tentative carries, so the engine is the authority.
- **These stay as they are:
- table rows never split internally;
- list markers stay unmeasured and right-anchored by default;
- block-boundary copy stays structural (.tsr-para / .tsr-band).** — Row splitting needs per-cell continuation state. Measuring markers would churn measurement buffers for no visual gain. The wrapper already carries block identity for commit.

## Top recommendations

1. **Adopt one shared schema with additive per-buffer versioning and a single ABI handshake** — This is the gate every other generalization passes through. Per-kind typed attributes with domains replace the flat ARGK namespace (ops.def:43-75) and the exact version check (ops.cc:100), which caused five OPS bumps in four days. One generator serves C++, JS, dumps, docs, the T4 property/settings rows and the T3/T8 DECL rows. It directly dissolves about 15 items (global-argk-namespace, open-arg-schema, version-bump-per-vocabulary, argk-overloading, CSS-injection domain hook, …). It also unblocks the element registry, the cascade wire change, traits and math kinds without repeated golden re-records. (T2-constructor-ir, T4-style-settings, T9-host-protocol)
2. **Replace role-string dispatch with one element-class registry with owner sections** — This is the largest ad-hoc family. 'figure', 'sidecar-lines', 'tsr-fn-' and kind switches are tested in resolve.cc:166, emit.cc:781, semantic_html.cc:286, typeset_html.cc:705 and shell.mjs:175. Counters are four struct fields and supplements a two-way language switch (config.h:89-102). One row per class carries semantic columns (T3) and style/layout/html sections (T4/T6/T7), declared the same way by built-ins and documents. Together with counters, templates and collections it dissolves roughly 45 items across T3, T6 and T7 and makes theorem families, appendices, indexes and custom numbered environments declarations. (T3-semantics, T4-style-settings, T6-layout-pagination, T7-render-runtime)
3. **One lowering, one constructor registry, frames at every block** — This makes the governing principle (every syntax form is sugar for a constructor) true. Sugar, explicit calls, regions, fences, m``, m.parse and sidecars share one program format and one interpreter. Built-ins become define() entries that users override with next. Generated code never shares user scope (codegen.cc:209-212), and every block is contained (I7, today absent at codegen.cc:214-229). This dissolves about 35 items in T1/T2 (closed ctor set, signature/sugar mismatch, private region builders, fragment.cc's second lowering, sidecar api pass, bibliography end-emission). (T2-constructor-ir, T1-surface-frontend)
4. **Land the P0 correctness batch before any refactor** — These are verified high-severity bugs with document-level blast radius:
- one throw or a `#let list` kills the document;
- scans run past their block and duplicate content (inline.cc:68-72);
- exponential DAG copy (model.cc:36-54);
- CSS injection through style strings;
- misnumbered citations in footnotes;
- stale relayout (doc.h:391 only clears laidOut);
- duplicate style attributes (typeset_html.cc:416-421);
- unserialized worker messages (worker.mjs:228-246).
Nearly all fixes are golden-neutral, and the batch also installs the contract checks, fuzzers and guard fixtures that every later phase relies on. (T1-surface-frontend, T2-constructor-ir, T3-semantics, T4-style-settings, T7-render-runtime, T9-host-protocol)
5. **Make presentation and configuration one typed property/settings registry with an emission-time cascade** — Kind presentation moves out of emit and the resolver into an env-0 default stylesheet in the user vocabulary:
- today emit OR-composes heading bold and size (emit.cc:52-63, 471-551);
- the resolver writes Styling literals (resolve.cc:177, 452);
- comments are detected by colour string (emit.cc:618-622).
The 34 global Config knobs become scoped properties. Per-knob setters, filename-encoded fixture settings and the region meta-arg hijack become one settings ABI. The MEASURE/PAINT effect column makes I2 checkable. This dissolves about 35 items across T4, T6 and T9. (T4-style-settings, T9-host-protocol, T6-layout-pagination)
6. **Rebuild the front end around a syntax table and an island-first lexer with block-owned line ownership** — Five bracket matchers, two comment scanners and four `$` lexers become one table-driven lexer. No scan can leave its leaf, which removes the class of content-duplication bugs. One argument/header/label grammar serves splices, regions and fences. Parse-time decisions (`|` cells, CJK joins, note spacing, display placement) move to their owning layers. Engine exports end grammar drift across eight artefacts. This dissolves about 40 T1 items. (T1-surface-frontend, T8-math)
7. **Introduce the explicit item list (HList), TextRules and the InlineObject protocol** — Five classifiers become one generated CC table. Ten fused BF_* flags (emit.h:10-22), which layout and render re-derive (layout.cc:514-521, typeset_html.cc:531-567), become typed items with TeX legality. Math, image, raw and user boxes enter lines through one two-phase protocol. Run identity gets one definition used for copy, kerning and DOM. Locales, hyphenation and Unicode controls become data. This dissolves about 30 T5 items plus the math-only inline box. (T5-text-shaping, T8-math, T6-layout-pagination)
8. **Make layout a pure function of a width-independent box tree** — This brings:
- TeX breaker semantics with tagged penalties and rescue (today an infeasible paragraph collapses to one line, break.cc:122);
- breaking inside layout against live exclusions (the float tracker in api/doc.h:258-361 goes away);
- a BlockLayouter registry instead of the FlowUnit kind switch and LineBox.special codes (layout.h:20);
- one materializeLines instead of four copies;
- ParShape vectors, TableSpec, a VList and a paginate stage instead of a paginator inside the serializer.
User regions get geometry exactly as built-ins do. This dissolves about 30 T6 items and the stale-relayout bug structurally. (T6-layout-pagination, T7-render-runtime)
9. **Route every document-level definition through one DECL channel with declared binding modes, plus content events** — Elements, counters, collectors, locales and tailorings, document settings, font roles and math symbols/macros get one versioned, validated, fuzzed channel. Today only LABEL/REF/COLLECT exist, and side-effect EMITs land before their block (model.cc:94-96). Hoisted vs positional binding is a schema column, not per-feature folklore. Counter state changes stay content events in reading order. This prevents four themes from each inventing a private declaration op. (T2-constructor-ir, T3-semantics, T4-style-settings, T5-text-shaping, T8-math)
10. **One typed resource protocol, a stage model with rerun classes, and a content-keyed Session** — Four hand-built pull channels and five caches with ad hoc keys become RES rows keyed by complete content, with one wait state, per-pid deferral and local failure degradation. Answers stop overwriting author args. stages.def records which stages may be re-run, so invalidation is mechanical (fork-on-rebuild), and one drive loop serves worker, Node, tsrc, goldens and fuzzers. This dissolves about 20 T9 items and the whole-document math re-emit. (T9-host-protocol, T5-text-shaping, T8-math, T6-layout-pagination)
11. **Make paint a stateless annotation of layout records behind one writer and one render protocol** — The DisplayList over Fragments stops the renderer from finishing layout:
- eqno and CJK gaps from Config;
- mutable lastAnchored.
HtmlWriter and AnchorNamer make duplicate attributes and id drift impossible. The PresentationMap (element html sections) gives user classes elements, ARIA, hooks and previews. RenderResult plus a single commit path replaces regex chunking. Behaviors replace DOM scraping. This dissolves about 35 T7 items. (T7-render-runtime, T6-layout-pagination, T3-semantics)
12. **Make labels, slots, syn/copy, style and spans universal attributes with per-occurrence provenance** — Universal labels, captions and tags as slots, a generic copy policy, exact data-s and synthetic-text handling all come from the same few attributes instead of per-kind args and kind-based guesses (BF_REF -> data-syn). This closes span loss on splices and regions (@[0,0)), dangling footnote ids and copy dropping prose next to citations. (T2-constructor-ir, T3-semantics, T5-text-shaping, T7-render-runtime, T1-surface-frontend)
13. **Make math ordinary content: MathDict, MathRow templates, typed holes and a positional math DECL namespace** — Spelling stops deciding behaviour:
- one SymbolInfo per symbol, generated from pinned UCD/MathML data rather than the font compiler;
- built-in families are template rows a user could write;
- errors stay local to the formula;
- values and content enter through holes;
- the font becomes a runtime object.
Math consumes the T5/T6/T7 protocols instead of the private LinebreakBlock::math, FlowUnit::Math and mathTextMissing paths. This dissolves about 24 T8 items. (T8-math, T1-surface-frontend, T2-constructor-ir)
14. **Enforce the principles mechanically** — The generalizations decay without enforcement:
- include and identifier lints;
- -Werror=switch-enum;
- equal-footing and sugar-equivalence goldens;
- a fuzz target per decoder;
- anchor-closure and provenance checks;
- schema.lock immutability;
- per-step scripted golden diff attribution;
- bench gates.
They are cheap and keep new features from re-entering as special cases. (T1-surface-frontend, T2-constructor-ir, T3-semantics, T4-style-settings, T5-text-shaping, T6-layout-pagination, T7-render-runtime, T8-math, T9-host-protocol)
15. **Decide the stretch model (v2 §8 over App C) and run one shaping wave** — Breaker capacity (0.1em, design-decisions-v2.md:369) and renderer weight (k = 0.6) disagree, although v2 §8 (l.196) and document-model.md:174 promise agreement by construction. The decision unblocks T5 step 11 / T6 S16, which delete fuseLegacy, capSu and the adapter chain. Batching all CJK typography churn into one reviewed wave limits reviewer fatigue. (T5-text-shaping, T6-layout-pagination)

## Roadmap (as integrated; PLAN.md §5 is the executable order)

### P0 Safety net and verified high-severity fixes

Goal: Stop whole-document failures and content corruption, close the injection and blow-up holes, and install the contract/fuzz/guard-fixture safety net, all without changing the IR.

- Safety net: T7 S1 contract checks in the golden runner (anchor closure, unique ids, no repeated attributes, allowlisted elements, data-s on text lines) with an xfail list; T6 S0 guard fixtures (snap+sidecar, float in list, 240px paged boundary, nested 16/18px lists); T9 M1F libFuzzer targets for the ops reader, linepass and inline (short run per PR, nightly 30 min); T1 S1 -Werror=switch-enum on front-end TUs and the missing Note dump case.
- Front-end containment (T1 subset with T8 S0): bound contiguous() at the leaf (inline.cc:68-72) and make cell splitting island-aware, so $...$, comments and splices cannot escape a block or cell; recover an unterminated top-level #let / #{ at the first blank line.
- Interim execution containment (I7): route generated calls through one reserved namespace object instead of the 31-name destructured prologue (codegen.cc:209-212, so `#let list` no longer kills the document); wrap each top-level content block's __emit in try/catch that yields an error node + diagnostic (codegen.cc:214-229); statements stay at module scope. This is T2 S5's fallback path.
- IR hardening: T2 S1 schema extraction and reader validation (key range checks, per-kind domains, ARG_NODE rejection, bit masking) with the T4 M2 style-value canonicaliser at decode (closes CSS injection); T2 S3 explicit-stack instantiation with InstLimits (exponential DAG copy at model.cc:36-54) and normalize.cc relocation.
- Style hygiene: T4 M0 (MetricStore key (u64)s<<32 instead of <<24 at measure.h:21; float canonicalisation; JS popTo clamp) and T4 M1 single emPx honouring sizePx (emit.cc:251).
- Semantics: T3 S0 (citation ordinals in document order with note bodies counted at their markers, per-key grouped citations, label-duplicate for every kind, universal label registration, collector aliasing, equation numbers on the semantic page, reserved-shape label check after a corpus scan).
- Render: T7 S2 HtmlWriter + AnchorNamer (one style attribute, fixing snap-kerning at typeset_html.cc:416-421; hyphen run inside its link); T7 S6 interim run key with the ref/synthetic flag; T7 S7 list projection hoists tight-item anchors onto <li>.
- Host: T9 M0 (per-doc mailbox and generations; relayout/paginate fork a fresh doc from retained ops instead of tsr_set_width on the live doc, worker.mjs:228-246 and doc.h:391; capability RPC ids; NUL escape; parallel image fetch; late-font cache clearing) and M1 (origin-sliced diagnostics; validated KP memo key).

Golden impact: Neutral except these deliberate fixes, each in its own commit with a scripted diff check:
- notes/{basic,explicit,cjk-glue}.ast.txt (missing note lines; the goldens were wrong);
- every *.js.txt (namespace + try wrappers; .ops byte-identical under record-fixtures --check);
- style/patch blocks/breaks/layout/html (emPx);
- from T3 S0: notes/* tree marker style [SUPx0.70], notes/*.semantic footnote ids, math/eqref.semantic equation numbers;
- cite/basic and cite/unknown-diag html (run key);
- notes/* semantic <li id> (T7 S7).
Everything else adds new guard, contract or fuzz fixtures only.

OPS: None. The wire is unchanged and the reader only becomes stricter on invalid input; all 48 fixtures decode with zero ops-invalid diagnostics.

### P1 Foundations: single sources, registries and seams in compatibility mode

Goal: Introduce every shared mechanism while reproducing today's output: schema generator and version window, syntax table and island-first lexer, settings ABI and stage model, element registry with built-ins as rows, HList/TextRules/InlineObject, breaker semantics + box tree + Fragments + DisplayList, ResourceTable + Session, MathDict/MathFont/MathRow.

- Schema: T2 S2 per-buffer versioning (reader window MIN_COMPAT..OPS_VERSION, immutable schema.lock; amend architecture.md:127 and ops.def:3) plus the single tsr2_abi handshake; T4 M2 property rows in the shared generator (generated Styling, applyPatch, dumps, JS key tables, run-attribute mapping).
- Front end: T1 S2 syntax.def + CallAST + generic dump (byte-identical); S3 SurfaceLexer/SpanCursor (CRLF, structural joins, one bracket counter, multi-backtick code); S4 BlockAutomaton (container protocol, by-name region resync, relative dedent); S5 line ownership and content bodies in Blocks mode (fixes the inline-comment leak); S6 engine tokens/outline/astjson exports with in-engine tsm tokens.
- Settings and pipeline, one landing (T4 M3 + T9 M2/M3): sectioned codec and tsr2_set_config with REBUILD/REEXECUTE; stages.def/products.def; shared drive loop for tsrc, tests and fuzzers; X.fixture.json profiles replacing filename conventions (engine/test/tests.cc:417-424); Resolve split from ingest; fork primitive; per-stage settings views.
- Semantics: T3 S1 element registry, Index and staged resolver, with built-in rows in engine/data/elements.json and term tables already in LocalePack format.
- Shaping: T5 step 1 (TextRules compat tables, one classifier, frozen mock classifier); step 2 (HList + run instances + fuseLegacy with CI field-by-field equivalence); step 3 (InlineObject registry; closed flatten table generated from schema inline kinds; hardbreak -> Forced penalty).
- Layout/paint seam: T6 S1 breaker semantics bundle (atomic: TeX discard, Forced paragraph end, rescue instead of one-line collapse at break.cc:122); S2 KP proper + validated cache (perf-gated); S3 breaking moves into layout (float tracker becomes an ExclusionMap; stages.def edited); S4 width-independent emit (SizeSpec/IntrinsicSize); S6 one materializeLines; S7 box tree + BlockLayouter registry + VList/Fragments, jointly with T7 S3 (DisplayList over Fragments, stateless anchors, paged cutter moved to layout/paginate).
- Faces: T4 M4 FaceTable/faceOf/fontRoles; MetricStore and requests keyed by FaceId; root contract (role variables, base size, lang).
- Resources: T9 M4 ResourceTable + complete MetricKey; M5 per-pid deferral with waiters (deletes the Doc::mathTextMissing whole-document re-emit); M6 Session (content-keyed answers, answerers, BreakMemo slot).
- Math: T8 S1 MathDict split; S2 MathFont object + registry + paint decoupling; S3 MathRow registry + Call IR + Error leaves (a bare dot/hat/abs no longer degrades the whole formula); S6 lazy layout through the generic deferral.

Golden impact: Mostly byte-identical; CI gates on every skeleton/ast/js/tree/blocks/layout/html golden. Called-out churn:
- T1 S4: container spans in doc/structure, notes/cjk-glue, region/figure, code/tsm-hl.
- T1 S6: code/tsm-hl highlight goldens.
- T6 S1: breakpoints in 12 paragraphs across 11 fixtures and printed costs in 25 breaks.txt (CJK owner reviews).
- T6 S4: blocks.txt of the 5 image fixtures.
- T6 S6: caption joins in figure/block, figure/pull-diag, region/figure.
- T8 S3: math/parse-diag partial layout.
- T4 M4: the .tsr-doc root line of all 48 html goldens.
New goldens: hlist, blocktree, vlist, index, tokens/outline/astjson, mathir.

OPS: None. The reader window widens, but every buffer still encodes version 6 byte for byte.

### P2 IR wave: one constructor model, universal attributes, declarations

Goal: Move to the target IR in one coordinated wire wave: one lowering on one interpreter with frames, a public constructor registry, Node values, occurrence spans, universal attributes, the DECL channel, semantic declarations, structured refs, the new style wire, math as ordinary content, and soft breaks on the wire.

- T2 S4: Node values, toContent, DIAG warnings.
- T2 S5: LowerProgram + hole module + frames at every depth. Bench-gated at 7.8K/35K/87K; the fallback prints JS from the same CallTree.
- T2 S6: constructor ABI + Registry. Regions are ctors with a Body param, and table/figure/default region/default fence/bib formatter are define() entries.
- T2 S7: occurrence spans (AT, containment).
- T1 S7: one argument/header/label/ref grammar.
- T1 S8: statements anywhere, keyword forms, content literals, Error lowering.
- T1 S9 lands atomically with T2 S8: region provenance without '|' splitting, through body.rows()/prov.rows and public table/figure ctors.
- T1 S11 + T2 S10: re-entrant fragments in the same program format; sidecars in the default fence (codeblock.margin slot).
- T2 S11: #bibliography in place.
- T2 S12: keyword forms.
- Soft break on the wire as U+000A in inline text, resolved in emit by the cjkish predicate extracted unchanged into support/; delete inline.cc:48-59.
- T2 S9: universal role/label/slot/class/style/syn/copy/EXT attributes, DECL with binding modes, `field`.
- T3 S2: semantic DECL namespaces, the event/entry/slot/when/each kinds, $.element/$.counter/$.collector and counterUpdate.
- T3 S3: structured references; @[a, b] becomes a parent ref with child refs.
- T4 M5 style wire:
  - Styling bits retire: weight/italic/decoration become rows, size becomes {absPx, mul}, fontRole replaces CLS_CODE, and baseline/size/attach replace CLS_SUP;
  - STYLE_PUSH carries a delta node;
  - region style: merges into the node's own delta.
- T8 S8: math/mathsrc kinds with typed holes and the positional math DECL namespace.
- Meaning changes batched into the same wave: raw/image level Adaptive (for T5 objects), tcell body Blocks (for T6), the `equations` kind and the universal tag slot.

Golden impact: One coordinated re-record, reviewed per step:
- every .ops (version byte);
- every .js.txt replaced by .lower.txt plus a hole-module .js.txt (T2 S5);
- tree spans become real (@[0,0) removed) for splices, regions and builder children (S7);
- the 10 region fixtures at all stages: lossless text, display math promoted and labelled, `*a | b*` as one cell;
- splice/dot-rule: the 'undefined' text is removed;
- cite/basic: .ops/.js for structured refs and the bibliography span;
- code sidecar fixtures: margin slot and real notes;
- ast/js/tree of the 9 multi-line fixtures (soft break);
- style/patch tree: the styled wrapper folds into the group's own style;
- notes trees gain role fn-marker; figure trees gain slot caption.
blocks/breaks/layout/html are unchanged except the region and sidecar bug fixes.

OPS: Exactly one MIN_COMPAT raise, carried by the commit that changes meaning: style bits, newline in inline text as a soft break, the math kind, raw/image level, tcell body model, structured refs and the equations kind. Additive vocabulary takes successive since values in the same window: DIAG, AT, DECL, universal attributes, EXT, margin/tag slots, event/entry/slot/when/each and field. Buffers advertise only what they use.

### P3 Generalizations on the foundations (parallel tracks)

Goal: Turn the remaining ad-hoc features into configurations of the public mechanisms: cascade with rules and a default stylesheet, slots/sites/collections/flows, exclusions/tables/grid/pagination/traits, the render protocol with behaviors and the PresentationMap, math families, the host locator, code manifests, project builds and front-end tooling.

- Style track:
  - T4 M6: cascade, rules and defaults.json as env 0; NodeProps; emit stops composing presentation; resolver fabricators use Cascade.make.
  - T4 M7: Config knobs become scoped properties.
  - T4 M8: run classes rendered; theme.css split.
  - T4 M9: LocalePack, $.doc/$.locale, termsLang.
- Semantics track:
  - T3 S4: slots, sites, tag content, frozen-title clones, counter-backed markers.
  - T3 S5 + T7 S9: identity decoupled from DOM spelling (AnchorIds, render.idPrefix, refPreview instead of 'tsr-fn-' scraping).
  - T3 S6: index, lof/lot, per-chapter notes, named multi-marker notes, Deferred placement with T6 S11.
- Layout track:
  - T6 S5: ParShape + side-tagged exclusions.
  - T6 S8: LineEnds.
  - T6 S9: table layouter.
  - T6 S10: grid layouter; codeblock+sidecar as a two-track table.
  - T6 S11: VList + paginate, with keeps relaxed in order.
  - T6 S12: trait surface as property aliases.
  - T6 S13: generalized placement + InlineBlocks (subfigures).
  - T6 S14 with T9 M11: host-measured boxes.
  - T6 S15 with T8 S9: display rows for the `equations` kind.
  - T2 S8b: para/block/para{cont} split; remove mathblock INLINE_FALLBACK.
- Render/runtime track:
  - T7 S4: RenderResult + commit.
  - T7 S5: shell core + Behavior registry.
  - T7 S8: separators + copy contract.
  - T7 S10: geometry authority (margins from su, Placement edge=End).
  - T7 S11: baseline authority.
  - T7 S12: PresentationMap as element-row html sections.
  - T7 S13: semantic math boxes + a11y.
  - T7 S14: export bundle.
- Math track:
  - T8 S4: SymbolInfo identity and data-driven families.
  - T8 S5: operator atoms and a single mlist->item conversion; fracPadEm in its own commit.
  - T8 S7: adopt the T5/T6/T7 protocols.
  - T8 S9: grid, equations, remaining primitives.
- Host/tooling track:
  - T9 M7: ResourceHost, locator, references, static export.
  - T9 M8: code-highlight manifest + engine-side overlays.
  - T9 M9: ABI completion + doc amendments.
  - T9 M10 + T3 S7: declared label inputs + two-pass project driver + #use.
  - T9 M12: viewport.width affects Layout only.
  - T1 S10: prose guards, autolink, escapes, hard break (after the Forced penalty exists).
  - T1 S12: printer, escaper, converter kit, translate-tsm.

Golden impact: Each step lands alone with a scripted 'only these attributes changed' check:
- M6: about 22 tree goldens show effective styles; up to 10 semantic goldens change unless T7's role map ships with M6.
- M8: 11 html files gain tsr-pre; 6 html and 2 semantic files get token classes.
- T6 S5: figure/float, figure/stack.
- T6 S8: su shifts in captions and 'c'/'r' cells.
- T6 S11: pages/paged-doc.paged.
- T7 S10: 33 html margin-bottom 19.2 -> 19.203px.
- T7 S8: about 25 html for separators/data-track.
- T7 S13: 7 math semantic goldens.
- T3 S5: tree url args become target anchors.
- T1 S10: doc/url-break, inline/emph, figure/pull-diag.
- T8 S5: fracPadEm moves every fraction; math/break segmentation.

OPS: No MIN_COMPAT change. Additive since rows only: locale/tailoring DECL payloads, raw measure/minWidth, cell spans/valign, pagebreak, list.marker. Trait properties ride the node style delta, so they need no wire key.

### P4 Shaping wave (CJK/Latin typography; CJK owner sign-off)

Goal: Replace node-local emission with paragraph-level shaping driven by data, give run identity and spans one definition, and unify breaker and renderer stretch per v2 §8.

- T5 step 4: run formation by run instance (supersedes T7 S6; inline code becomes Rigid runs with word-spacing:0).
- T5 step 5: paragraph-level shaper (context across style/link/code/object edges, ambiguous quotes); soft breaks resolved by TextRules joinsWithoutSpace instead of the extracted predicate.
- T1 S14: note attachment moves out of the parser, landing with T5 step 10's attach semantics; emit.cc:89-90 deleted.
- T5 step 6: per-item source spans from the offset maps transported in P2.
- T5 step 7:
  - TextProps v1 (hyphens, overflowWrap, punct, autospace, whiteSpace);
  - punctuation blanks and defined widths as data;
  - explicit px replaces tsr-sqL/R;
  - prerequisite: the Playwright punct matrix exists before merge.
- T5 step 8: UCD-derived classes (RULES_VERSION 1), Unicode controls, kern eligibility by class.
- T5 step 9: hyphenation registry by BCP-47 through the hyphenPatterns RES row; ExHyphen; Chicago URL sides.
- T5 step 11 + T6 S16: item-native breaker on T5 items with unified stretch (App C amended); delete fuseLegacy, capSu and the LinebreakBlock adapter; retire blocks goldens in favour of hlist.

Golden impact: The project's deliberate typography churn, one step per commit, with rules-diff and a corpus re-audit:
- step 5: script-edge fixtures (cjk/softwrap, code/runs, figure/*, math/eqref, notes/cjk-glue, style/patch, inline/quotes, doc/url-break);
- step 6: data-s in most html;
- step 7: all CJK html (explicit px), plus possible +1su breaks in cjk/punct*;
- step 8: inline/quotes, style/kern-boundary;
- step 9: doc/hyphen, doc/url-break, cite/*, hyphenated compounds;
- step 10: notes/*;
- step 11: wide breaks/layout/html churn on justified CJK and mixed lines.

OPS: None required. Soft breaks have travelled as U+000A since P2, and TextProps are additive property rows.

### P5 Extensions and owner-decision items

Goal: Use the finished mechanisms for features that need owner decisions or fixtures that do not exist yet.

- T5 step 12: $.locale text tailorings, hbox ObjectKind, ja/zh-Hant/ko packs once fixtures exist.
- T8 S10: multi-font chain, host .tsmf as declared inputs, optional reference ink.
- T6 margin placement (sidenotes, margin figures), once a container declares a margin column.
- T1 S15 config-declared inline rows, only if the owners adopt them.
- MathML feed (opt-in), page-number references through fixed-width slots, PDF backend decision.

Golden impact: New fixtures only. Exception: style/patch changes if the zh-Hant pack lands (zh-TW scope); this is called out.

OPS: Additive only.

## Residual gaps (decided in PLAN.md §3)

- Style scope of nested statements is unspecified. T1 allows `#{ $.set(...) }` inside list items and regions, T2's content frames pop pushes with 'style-in-value', and T4 leaves the question open. Recommendation: Typst-like scope until the end of the enclosing container, lowered to style.where around the following siblings. This needs a joint T1/T2/T4 spec before P2's T1 S8.
- There is no force layer for style rules. HostForce exists only for settings rows, so a site theme cannot force a rule over document $.set rules.
- DECL namespaces have no region-scoped form. Math macros, locale tailorings and element definitions are document-global (from their position, or hoisted). T8 and T4 left region frames open.
- Product decisions are still owed:
  - default doc.lang ('zh-CN' vs 'und');
  - default copy policy for prose refs;
  - ExHyphen enabled by default;
  - data-role on built-in group lines and default frames for figure/quote;
  - the a11y.mathLabel default;
  - figure-wrapping-table numbering;
  - nested figures as subfigures;
  - `@id[supplement]` adjacency in CJK prose;
  - one cell vs GFM cells for `*a | b*`;
  - math `{}` grouping;
  - the implicitNames diagnostic default.
- Incremental editing is not designed end to end. T1 exports LexState snapshots and RevertedWindows, T2 caches hole modules by hash and T9 forbids a product graph, but no theme specifies how an edit re-lexes, re-lowers and re-executes only the touched blocks under the about-6ms budget.
- There is no joint performance budget. The LowerProgram interpreter (T2 S5), per-keystroke membership and MATERIALIZE (T3), the cascade memo (T4), per-cluster classification (T5) and active-list KP (T6) each have a gate, but nobody has allocated the 87K editor budget across them.
- A dedicated security review is needed before T9 M7 and T7 S12 ship. It must cover the trusted RawHtml path, element/attribute allowlists, locator confinement and requester classes, document-registered providers, persistent #use module state, and the new decoders (settings, inputs, RES answers, .tsmf, LowerProgram).
- Author-facing deltas have no owner. Several themes change language behaviour:
  - T1: comment lines no longer split paragraphs; `N.` interruption; intraword guards; backslash rules; `<my eq>` is no longer a label; `#let x = [..]` is content;
  - T8: `#` holes in math, adjacency-only calls, single-token operands;
  - T4: absolute size replaces composition; rules beat stack pushes.
  There is no consolidated changelog or migration guide, and no owner for re-converting the real-world corpus (pbr, HoTT, zball-io).
- Sidenotes and margin figures are deferred. A user can retarget `note` by overriding the ctor, but there is no margin column in screen or PageSpec to place it in.
- The PDF/canvas backend question is unresolved. The DisplayList carries text runs, not glyph positions; whether PageResult should ever feed a PDF writer decides that.
- Several small items are open:
  - where project configuration lives (renderTsm option vs a project JSON shared with the static-export manifest);
  - AnchorNamer escaping for key-bodied aliases (percent-encode vs slugify), which affects published permalinks;
  - whether fnref aliases are user-addressable.
- Description lists and list-item labels have no surface beyond the function forms #item(term: ...) and #item({label}). Owners may want sugar.
- Policies are undecided for:
  - hyphenation dictionaries (resident vs pulled);
  - UCD/RULES_VERSION updates (tie to releases?);
  - an optional DOM-measurement backend that would honour lang/features in FaceKey.
- Layout policy questions remain:
  - keep relaxation order (Avoid1 before Avoid2);
  - greedy vs global DP page builder;
  - ragged end-stretch defaults per role;
  - unifying float and block caption alignment;
  - wide-table overflow on screen (scroll vs visible).
- Math feature questions remain: cancel needs a slanted-line paint primitive; reference ink for text leaves; whether the static page uses MathML or finalized boxes.
- Coverage audit: the workflow reports no undispositioned ids, but about 80 ids are 'owned-by-other-theme'. CI or review should confirm that each owning step lists them in its fixes. Two are only implied: resolver/missed:5 (observability stages) and real-world-evidence/missed:5 (caption guesswork, split between T2's figure builder and T7's serializer).
- Doc amendments must land with their steps or the docs drift again:
  - v2 §2: LowerProgram.
  - architecture.md:127: version window.
  - v2 §12 l.246: set rules with selectors.
  - v2 §5: holes in math.
  - document-model §3: size semantics.
  - v2 §6: Session owns measurement reuse.
  - v2 §9 / architecture §2.4: estimate states superseded.
  - v2 §11.1: resolver input = tree + settings + declared inputs.
  - App C: stretch.
  - document-model §9.3: DOM copy contract.
  - code-design §2: the engine tokenizes .tsm.
