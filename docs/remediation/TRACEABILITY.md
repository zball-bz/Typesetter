# Traceability: audit findings ↔ plan steps

369 findings, 101 plan steps. Every finding maps to at least one step (an interim fix and the structural fix may both be listed). The goal run ticks a finding only when its LAST listed step has landed and its regression fixture passes.

Status column: the goal run fills it with the commit hash (or `kept: <reason>` when the finding is resolved by a documented keep-special decision).


## A. By step


### P0-00 准备：分支、计划文档、基线、环境脚本

Theme steps: — · findings: 0


### P0-01 契约检查与守护用例

Theme steps: T7 S1, T6 S0 · findings: 0


### P0-02 编译器防护（去掉 default 分支，AST dump 补 Note）

Theme steps: T1 S1 · findings: 2

- `markup-language/ast-dump-note` — AST dump omits Note nodes
- `parser-frontend/ast-dump-missing-note` — dumpAst has no Note case, and the committed goldens enshrine blank lines

### P0-03 Fuzz 基础设施

Theme steps: T9 M1F · findings: 0


### P0-04 前端越界修复（过渡）

Theme steps: T8 S0 · findings: 5

- `markup-language/region-pipe-segmentation` — Table '|' segmentation is baked into the generic region mechanism, and it is byte-level, so it corrupts content
- `math/island-scan-escapes-block` — A math island can run past its block, duplicating content into headings and lists
- `math/missed:0` — Region table cell splitter ignores $…$ math islands, and the island scan crosses cell boundaries: formulas with | corrupt rows and duplicate content
- `parser-frontend/missed:0` — Line contiguity is byte adjacency of whitespace-trimmed spans, so CRLF and trailing spaces change the grammar
- `parser-frontend/unterminated-let-swallows-document` — An unterminated top-level #let consumes the rest of the document

### P0-05 执行容错（过渡）

Theme steps: — · findings: 9

- `codegen-ops-model/keyword-forms-uncompiled` — #if / #for / #use and `#let x = […]` content literals compile to invalid or wrong JS
- `codegen-ops-model/missed:0` — Codegen is unhygienic: one flat scope is shared by generated calls, the constructor parameter list and user bindings
- `codegen-ops-model/no-per-block-containment` — No per-top-level-block error containment; any script throw aborts the whole document
- `markup-language/nested-code-statements-dropped` — `#let` / `#{…}` inside list items, quotes and regions are silently dropped
- `markup-language/no-execution-containment` — Any JS error anywhere aborts the whole document; per-block containment (v2 §2, §11) is not implemented
- `markup-language/spec-features-unimplemented` — Specified surface features compile to invalid JS or are absent
- `parser-frontend/ctor-names-are-reserved-words` — Destructured constructor parameters make 31 common names un-bindable; #let list = .. fails the whole document
- `real-world-evidence/ctor-name-collision-fatal` — A #let that collides with any of ~31 destructured constructor names kills the whole document
- `real-world-evidence/missed:0` — Runtime script errors have no per-block containment at all; one throw kills the document and the blog build

### P0-06 模式抽取、读取器校验、样式值校验

Theme steps: T2 S1 · findings: 9

- `codegen-ops-model/argtag-node-dangling` — Node-valued args are accepted by the codec but copied as raw-buffer ids and never interpreted
- `codegen-ops-model/contract-doc-drift` — The normative ops/model spec has drifted from the code
- `codegen-ops-model/css-injection-style-values` — Unvalidated style strings inject arbitrary CSS into typeset runs and break the measurement–render contract
- `codegen-ops-model/global-argk-namespace` — One flat, overloaded ARGK namespace with no per-kind attribute schema or validation
- `codegen-ops-model/missed:3` — Numeric argument values are never validated or canonicalised: author-reachable undefined-behaviour casts and broken interning
- `codegen-ops-model/two-style-encodings-and-stack` — StyleDelta has two wire encodings; the schedule stack is only sound at top level and accepts raw class bits
- `emitter/codeblock-args-in-emit` — Codeblock argument mini-languages parsed in emit; code-only measurement probes special-cased in resolveWidths
- `real-world-evidence/missed:3` — Raw engine class bits are a public back door: $.style.push(number) and STYLE_PUSH bits are OR'd unmasked; the reader does not range-check ArgK
- `resolver/argk-overloading` — Arg keys are reused differently by each feature instead of having one generic meaning

### P0-07 实例化加固（显式栈 + InstLimits）

Theme steps: T2 S3 · findings: 2

- `codegen-ops-model/exponential-instantiation` — Per-emission DAG copying gives exponential blow-up from a tiny buffer; recursion is unbounded
- `resolver/rewrite-normalizations` — Structural cleanup (deleting empty paras, unwrapping blocks) lives in the resolver with its own inline-kind list

### P0-08 样式卫生与统一 em

Theme steps: T4 M0, T4 M1 · findings: 4

- `codegen-ops-model/metric-key-fragmentation` — MetricStore is keyed by the full StyleId, so paint-only variants re-request widths; NaN styles break interning
- `codegen-ops-model/popto-stack-divergence` — $.style.popTo(h) above the current height grows the JS stack with holes
- `codegen-ops-model/sizepx-ignored-in-emit` — Emit computes font size without sizePx (math, CJK boundary glue, punctuation compression, indent)
- `emitter/sizepx-em-mismatch` — Emitter's em ignores Styling::sizePx, while measurement and CSS honour it

### P0-09 语义正确性修复

Theme steps: T3 S0 · findings: 16

- `markup-language/label-namespace-collision` — Auto-labels (h-n, fn-n, fnref-n, bib-key, term names) share the user label namespace
- `real-world-evidence/grouped-cite-all-or-nothing` — One unknown key turns a whole grouped citation into '??'
- `real-world-evidence/labels-on-unsupported-nodes-silent` — Labels on groups other than figure, and trailing <x> on paragraphs, fail silently at the definition site
- `real-world-evidence/missed:1` — Internal auto-labels share the user label namespace via string prefixes (h-, fn-, fnref-, bib-), and the shell keys on the same prefixes
- `render-runtime/semantic-footnote-ids-dangle` — Semantic/static pages: every footnote link points to a nonexistent id, and the goldens lock this in
- `resolver/absolute-style-loss` — The figure caption prefix and term name use an absolute style and drop the inherited font, lang, color and size
- `resolver/cite-ordinal-pass-order` — A citation inside an implicit footnote gets a later number, is left out of the bibliography, and links to an anchor that does not exist
- `resolver/collector-aliasing` — Repeating #notes() or #bibliography() puts the same nodes into two subtrees (tree becomes a DAG): double rescale and duplicate anchors
- `resolver/duplicate-label-dom-ids` — Duplicate labels on figures, tables, equations and terms produce duplicate DOM ids
- `resolver/fragile-aggregate-init` — Resolver is constructed by positional aggregate initialization
- `resolver/grouped-cite-all-or-nothing` — In a grouped citation, one unknown key turns the whole group into '??', and the diagnostic names the joined string
- `resolver/missed:1` — The no-JS semantic HTML drops equation numbers because the resolver's tag is a string arg that only emit reads
- `resolver/missed:5` — Golden blind spots for resolver output: the tree dump cannot show CLS_SUP, and resolver diagnostics cannot be dumped
- `resolver/reserved-label-collision` — A user label can capture a footnote marker or another synthetic anchor
- `resolver/semantic-tight-item-anchor` — The no-JS HTML loses footnote body anchors, so markers point at nothing
- `resolver/untested-diagnostics` — Resolver diagnostics and edge cases have no test coverage

### P0-10 渲染正确性修复（HtmlWriter/AnchorNamer、run 键、列表锚点）

Theme steps: T7 S2, T7 S6, T7 S7 · findings: 8

- `api-measure-code/snap-kerning-duplicate-style-attr` — Snap-kerning emits a second style attribute, so the letter-spacing never applies in browsers (verified)
- `break-layout-pages/missed:1` — Snap-kerning code spans emit two style attributes, so snap-kerning is inert in browsers
- `emitter/missed:0` — Render run formation ignores BF_REF, so copy drops real prose next to citations and unresolved refs
- `render-runtime/duplicate-serializer-primitives` — Escaping, style→CSS mapping and anchor emission are duplicated across the two serializers and have already diverged
- `render-runtime/hyphen-in-link-or-ref` — A line-final hyphen inside a link or ref is rendered outside the <a>, and gets a duplicate data-syn inside refs
- `render-runtime/missed:0` — Run coalescing ignores the synthetic/ref flag, so copy deletes real prose next to citations and copies synthetic brackets
- `render-runtime/semantic-footnote-ids-dangle` — Semantic/static pages: every footnote link points to a nonexistent id, and the goldens lock this in
- `render-runtime/snap-kerning-duplicate-style` — Verbatim snap-kerning writes a second style attribute, so browsers drop its letter-spacing and the feature never takes effect

### P0-11 宿主卫生（worker 串行化、fork 重排、引擎卫生）

Theme steps: T9 M0, T9 M1 · findings: 12

- `api-measure-code/diag-format-and-duplication` — Diagnostics are append-only text whose layout is the de facto API, and re-runs duplicate them (verified)
- `api-measure-code/image-fetch-serial-and-decode` — Image dimensions are fetched serially, by fully decoding each image, from the wrong base URL
- `api-measure-code/image-w-only-overwritten` — An author-declared w without h is overwritten by the intrinsic width (verified)
- `api-measure-code/kp-cache-unverified-hash` — The KP memo trusts a 64-bit hash with no equality check, and its field packing XORs shifted signed values
- `api-measure-code/late-font-stale-measure-cache` — A font that lands after the 4 s deadline leaves fallback widths in the persistent measurer cache
- `api-measure-code/main-dims-rpc-race` — Concurrent main-thread dimension requests for the same src can leave a promise unresolved forever
- `api-measure-code/missed:0` — Literate-fragment masking corrupts UTF-8 token offsets for non-ASCII fragment names
- `api-measure-code/nul-byte-in-worker-source` — worker.mjs contains a raw NUL byte, so git treats the file as binary and review diffs are hidden
- `api-measure-code/relayout-stale-emit` — relayout and paginate reuse emit products computed at the old width (verified)
- `api-measure-code/worker-no-per-doc-serialization` — The worker processes messages for the same docId concurrently, so the last finisher wins
- `break-layout-pages/break-cache-robustness` — The global break cache trusts an unverified 64-bit hash of XOR-packed fields and wipes itself wholesale at 16384 entries
- `emitter/duplicate-diagnostics-on-reemit` — emit has diagnostic side effects but is re-run whole-document, so diagnostics duplicate

### P0-12 断行语义包

Theme steps: T6 S1 · findings: 7

- `break-layout-pages/break-inf-float-vs-double` — BREAK_INF (float 1e18f ≈ 9.99999984e17) is below the DP's INF (double 1e18), so unbreakable blocks become break candidates
- `break-layout-pages/missed:3` — Unbounded badness and magnitude sentinels: glue-less lines get astronomical costs that collide with INF, BREAK_INF and the 1e17 retry threshold
- `break-layout-pages/missed:5` — DP tie-breaking depends on std::unordered_map iteration order, which differs between native (libstdc++) goldens and WASM (libc++) production
- `break-layout-pages/overfull-collapses-paragraph` — When no feasible break exists, the whole paragraph is set as ONE line with huge negative word-spacing
- `break-layout-pages/trailing-glue-in-break-cost` — The DP counts the glue at the break (trailing space, closing-punct half) that layout later trims, so cost and rendered slack disagree
- `emitter/kp-counts-discardable-glue` — KP includes the break block and leading glue in line width and stretch, but layout trims them
- `emitter/negative-wordspacing-overfull` — Overfull ragged/last lines get unbounded negative word-spacing

### P1-01 版本窗口与 ABI 握手

Theme steps: T2 S2 · findings: 2

- `codegen-ops-model/version-bump-per-vocabulary` — Every new kind or argument key is a protocol version bump; the version and kind count are duplicated by hand
- `real-world-evidence/open-arg-schema` — Every feature knob is a frozen ARGK in the binary contract: five OPS_VERSION bumps in four days

### P1-02 属性注册表（props 行进入共享模式）

Theme steps: T4 M2 · findings: 5

- `codegen-ops-model/css-injection-style-values` — Unvalidated style strings inject arbitrary CSS into typeset runs and break the measurement–render contract
- `codegen-ops-model/fixed-styling-fields` — Styling is a fixed struct; every property is hand-coded in ~10 places across two languages, and derived values already drift
- `codegen-ops-model/tree-dump-omits-sup` — The golden tree dump cannot show CLS_SUP
- `render-runtime/duplicate-serializer-primitives` — Escaping, style→CSS mapping and anchor emission are duplicated across the two serializers and have already diverged
- `render-runtime/missed:2` — Inline-style values are raw CSS strings: document values smuggle declarations that break the nowrap/measurement contract

### P1-03 设置文档 ABI、阶段模型、驱动循环、用例配置

Theme steps: T4 M3, T9 M2, T9 M3 · findings: 9

- `api-measure-code/adhoc-invalidation-flags` — Stage invalidation is two booleans set per case; width changes do not invalidate width-dependent emit products
- `api-measure-code/config-plumbing-per-knob` — Host configuration is one C setter per knob, re-enumerated in worker and shell; most Config fields are unreachable
- `api-measure-code/magic-policy-constants` — Policy constants in the API, measure, worker and grid code are bare literals
- `api-measure-code/native-driver-config-divergence` — Native drivers duplicate the pull loop and configure by filename substrings; tsrc cannot reproduce the goldens
- `api-measure-code/snap-ignores-sidecar-partition` — In snap mode the column count ignores the sidecar partition, so code overflows into the sidecar column (verified)
- `api-measure-code/snap-kerning-duplicate-style-attr` — Snap-kerning emits a second style attribute, so the letter-spacing never applies in browsers (verified)
- `break-layout-pages/api-hosts-layout-policy` — api/doc.h hosts layout policy and a content transform, contrary to the module map
- `render-runtime/config-plumbing` — Feature knobs are threaded by hand through five layers, and defaults are duplicated and drifting
- `resolver/missed:5` — Golden blind spots for resolver output: the tree dump cannot show CLS_SUP, and resolver diagnostics cannot be dumped

### P1-04 字体面与根契约

Theme steps: T4 M4 · findings: 4

- `api-measure-code/font-role-split` — Font-role resolution is split across measure.h class-bit mapping, renderer classes and hard-coded shell CSS
- `api-measure-code/missed:3` — Metrics are keyed by presentation StyleId instead of the measurement tuple specified in v2 §6
- `codegen-ops-model/metric-key-fragmentation` — MetricStore is keyed by the full StyleId, so paint-only variants re-request widths; NaN styles break interning
- `emitter/missed:5` — Script-to-font mapping is a hard-coded three-way bit switch decided by emit, with CSS class rules as a second copy

### P1-05 syntax.def 与 CallAST

Theme steps: T1 S2 · findings: 1

- `parser-frontend/per-feature-ast-kinds` — AST encodes each sugar as its own node kind, with per-kind field overloading

### P1-06 SurfaceLexer

Theme steps: T1 S3 · findings: 6

- `markup-language/inline-scanner-overrun` — Inline sub-scans read past their span: math islands and comments consume later paragraphs and table cells
- `parser-frontend/contiguous-escapes-blocks` — contiguous() returns true past the last span, so math, splices and comments escape their block and content is duplicated
- `parser-frontend/inline-recognizer-cascade` — Inline parser is a first-byte if-cascade; each sugar brings its own scanner with different escape and nesting rules
- `parser-frontend/missed:0` — Line contiguity is byte adjacency of whitespace-trimmed spans, so CRLF and trailing spaces change the grammar
- `parser-frontend/missed:1` — Bracket-delimited constructs are pre-matched by island-unaware byte scanners, violating 'verbatim islands first'
- `parser-frontend/missed:4` — Inline code uses a one-off delimiter rule; fences use run-length delimiters

### P1-07 BlockAutomaton

Theme steps: T1 S4 · findings: 13

- `markup-language/missed:1` — Line-level comments are not invisible: a comment line inside a paragraph splits it
- `markup-language/missed:2` — Container spans (list/item/quote) are truncated to the opener line
- `markup-language/missed:4` — Indentation counts spaces only; a tab silently ends a list, quote or fence content
- `markup-language/missed:5` — List identity ignores marker class and blank lines; later start numbers are silently dropped
- `markup-language/region-error-recovery` — Region error handling deviates from App B rule 5; orphan closers become fatal splices
- `markup-language/same-line-trailing-text-dropped` — Text after a line-start block comment, `#{…}`, or `#let …;` on the same line is silently discarded
- `parser-frontend/fence-double-dedent-in-containers` — Fences inside list items strip the container indentation twice, corrupting code indentation
- `parser-frontend/missed:2` — Fenced blocks ignore the container stack: a fence in a quote or list item runs past the container's end
- `parser-frontend/missed:5` — No paragraph-interruption policy: any line starting with a block marker splits prose
- `parser-frontend/region-container-special-case` — Regions are bolted onto the implicit-close container stack with kind checks; forced closes are silent and mismatches do not resync
- `parser-frontend/span-fidelity` — Spans are imprecise: container spans cover only the first line, synthesized spaces get stale spans, and Text has no cooked-to-raw map
- `parser-frontend/trailing-text-after-block-closers-dropped` — Text after --%, } or ; on the closing line of a block construct is silently discarded
- `parser-frontend/unterminated-let-swallows-document` — An unterminated top-level #let consumes the rest of the document

### P1-08 行所有权与内容体

Theme steps: T1 S5 · findings: 4

- `markup-language/inline-delimiter-scanners` — Delimited forms are scanned by separate, divergent scanners, and islands are not carved out before line structure
- `parser-frontend/content-args-inline-only` — Content arguments are single-line and inline-only, so user constructors cannot receive block content
- `parser-frontend/cross-line-raw-scans` — Constructs that cross lines escape their block through raw-buffer scans plus a contiguous() guard and an open.empty() context flag
- `parser-frontend/inline-comment-leaks-block-structure` — An inline %-- opened mid-line does not hide the block markers it covers

### P1-09 前端导出与工具链

Theme steps: T1 S6 · findings: 5

- `markup-language/surface-grammar-drift` — Four hand-maintained descriptions of the surface grammar (engine, tree-sitter, TextMate, converter escapers) that already disagree
- `parser-frontend/docs-drift` — Front-end documentation describes components and features that do not exist
- `parser-frontend/editor-region-builder-list` — Editor completion hard-codes a list of region names
- `parser-frontend/multiple-tsm-grammars` — No single source of truth: the language is described by eight-plus artefacts, one of which (PackCC) does not exist
- `real-world-evidence/lexical-syntax-copies` — The lexical syntax exists in four-plus hand-synchronized copies

### P1-10 元素注册表、索引与分阶段解析器

Theme steps: T3 S1 · findings: 14

- `api-measure-code/supplements-and-lang` — Cross-reference supplements are one Config field per counted kind, localized by a two-way host-level lang switch
- `real-world-evidence/locale-terms-switch` — Supplement words are a two-branch if/else; document language is only a host option
- `resolver/collector-what-dispatch` — Collectors are four separate builders chosen by string; lof and index do not exist
- `resolver/fixed-counter-set` — Counters are hard-wired struct members, each with its own increment rule per node kind
- `resolver/footnote-pipeline` — Footnotes have their own pipeline: synthetic labels, a style bit as the marker signal, body lifting, implicit placement
- `resolver/invariant-declares-decides` — Q4: 'execution declares, resolver decides' holds for values, but the resolver also builds presentation and its correctness depends on walk order
- `resolver/label-registration-per-kind` — Only five hard-coded branches register labels; every other labelled node is silently ignored
- `resolver/missed:5` — Golden blind spots for resolver output: the tree dump cannot show CLS_SUP, and resolver diagnostics cannot be dumped
- `resolver/numbering-format-hardcoded` — Numbering format and policy are hard-coded: decimal, dotted, '(n)', equations numbered only when labelled, zero-filled level gaps
- `resolver/presentation-constants` — Presentation literals and magic numbers are embedded in the resolver
- `resolver/ref-display-switch` — Reference display is a switch on the target node's kind; the documented `form` is not implemented
- `resolver/spec-drift` — Much of the normative resolver contract in the docs is not implemented, and the resolver's tables are not kept
- `resolver/supplement-config` — Supplements are four fixed Config strings chosen by a two-way language test
- `resolver/term-rewrite` — term is rewritten to group{role:term} with a fixed bold name and ' — '; the glossary reads a private string table

### P1-11 TextRules 兼容表与单一分类器

Theme steps: T5 1. · findings: 0


### P1-12 HList 与 run 实例

Theme steps: T5 2. · findings: 2

- `emitter/bf-flag-overload-and-rederivation` — One-off BF_* bits with overloaded meanings; layout and render re-derive semantics from flag combinations
- `emitter/dump-hides-finite-penalties` — Block dump prints 'pen=0' for any finite penalty and labels math glue 'boundary'

### P1-13 InlineObject 注册表与扁平化表

Theme steps: T5 3. · findings: 3

- `emitter/math-only-inline-box` — The only inline object is math (LinebreakBlock::math); inline image, raw and mathblock vanish
- `emitter/silent-drops-of-unhandled-kinds` — Unhandled inline and block kinds vanish without a diagnostic
- `math/inline-math-special-block` — Inline formulas are a typed `MathBox*` on LinebreakBlock, with math branches in emit, layout (three copies), render, copy and audit

### P1-14 KP 正式化与校验缓存

Theme steps: T6 S2 · findings: 2

- `break-layout-pages/break-cache-robustness` — The global break cache trusts an unverified 64-bit hash of XOR-packed fields and wipes itself wholesale at 16384 entries
- `break-layout-pages/kp-window-heuristics` — Breaker search is a PoC sliding window with magic constants and a retry ladder, not the Knuth–Plass active list

### P1-15 断行移入布局（ExclusionMap）

Theme steps: T6 S3 · findings: 4

- `api-measure-code/doc-typeset-hosts-layout-logic` — Doc::typeset hosts the per-FlowUnit-kind break dispatch and an F2 float tracker that duplicates layout's gap rules
- `break-layout-pages/api-hosts-layout-policy` — api/doc.h hosts layout policy and a content transform, contrary to the module map
- `break-layout-pages/doc-drift` — Several layout and pagination docs disagree with the code
- `break-layout-pages/float-tracker-replay` — The float tracker lives in api/Doc::typeset, duplicates layout's spacing arithmetic, counts occlusion in baseLeading, and hands its decisions to layout through fields on FlowUnit

### P1-16 与宽度无关的 emit（SizeSpec）

Theme steps: T6 S4 · findings: 4

- `api-measure-code/relayout-stale-emit` — relayout and paginate reuse emit products computed at the old width (verified)
- `break-layout-pages/emit-reads-measure-stale-on-relayout` — Emit sizes images and sidecars from cfg.widthPx, but relayout and paginate never re-emit
- `emitter/measure-dependent-geometry-in-emit` — Emit applies per-feature geometry policy against cfg.widthPx (image clamp/scale, placeholder 1/3, sidecar fraction)
- `emitter/stale-emit-on-relayout` — Relayout keeps emit-time image and sidecar widths computed for the old measure (resize bug)

### P1-17 统一行物化（materializeLines）

Theme steps: T6 S6 · findings: 4

- `break-layout-pages/missed:2` — Copy 'join' is derived from alignment (isLast || ragged), so wrapped headings and block captions copy with newlines
- `break-layout-pages/nested-stream-copies` — 'TableCell' doubles as table cell, float caption and sidecar row, and the line-materialization loop is copy-pasted four times with diverging features
- `emitter/missed:3` — Sub-flows (table cells, sidecar rows, float captions) are second-class streams built in a throwaway FlowUnit
- `render-runtime/sidecar-hyphen-missing` — A sidecar row broken at a hyphen point shows no hyphen glyph

### P1-18 盒树、布局器注册表、Fragment、DisplayList

Theme steps: T6 S7, T7 S3 · findings: 13

- `break-layout-pages/baseline-not-communicated` — LayoutResult has line heights but no baselines, and text lines are emitted without height or line-height
- `break-layout-pages/group-role-dispatch` — Groups get geometry only through hard-coded role strings; user regions have no box model in typeset mode
- `break-layout-pages/missed:0` — Paged renderer anchor bookkeeping drops or duplicates label ids
- `break-layout-pages/unit-kind-switch` — Block layout is a switch over FlowUnit::K, producing a union LineBox tagged by magic `special` numbers
- `break-layout-pages/vertical-spacing-constants` — Vertical spacing is a global paraGap with magic fractions and per-kind advances, not style-driven vertical glue
- `emitter/anchor-opt-in-per-kind` — Label-to-anchor handling repeated per case; codeblock and rule never take the pending anchor
- `emitter/figure-role-string-dispatch` — role=='figure' (a kind in disguise) drives caption mode, float packing and dropped children, and is re-checked in 3 other layers
- `emitter/flowunit-kind-switch` — Block level is a 7-way kind switch over a fat FlowUnit, re-dispatched in the break loop, layout and both renderers
- `math/display-math-unit` — Display formulas get a dedicated FlowUnit kind and fields, LineBox special=4, renderer-side geometry, and an unmeasured CSS-positioned equation number
- `render-runtime/linebox-special-dispatch` — LineBox.special magic integers plus a renderer if-ladder that reaches back into FlowUnit, block streams and the content tree (no render IR)
- `render-runtime/marker-gutter` — List markers and code line numbers share one out-of-flow `.tsr-marker` CSS trick; line numbers are recognisable only because they happen to carry the code style
- `render-runtime/missed:1` — Paged serializer drops and duplicates anchor ids via shared lastAnchored state
- `render-runtime/render-layout-decisions` — The serializer re-derives layout decisions (eqno placement, display-math centring, CJK/punct spacing, glue widths, run boundaries, paragraph gaps) duplicated from layout and emit

### P1-19 资源表（ResourceTable）

Theme steps: T9 M4 · findings: 7

- `api-measure-code/image-dims-in-author-args` — Provided intrinsic image dims overwrite the author's w/h arg slots, and tokens rewrite the tree: resource results live in the authored model
- `api-measure-code/image-w-only-overwritten` — An author-declared w without h is overwritten by the intrinsic width (verified)
- `api-measure-code/missed:1` — Provider answers are trusted blindly: an out-of-range tag writes past a stack array, and offsets are never validated
- `api-measure-code/missed:3` — Metrics are keyed by presentation StyleId instead of the measurement tuple specified in v2 §6
- `api-measure-code/per-resource-pull-plumbing` — Each external resource is its own hand-built pull channel: state struct, Kind-keyed scan, JSON section, provide export, JS branch and native stub
- `api-measure-code/per-word-boundary-marshalling` — Measurement crosses the WASM boundary once per word, with JSON and re-interning, on every keystroke
- `api-measure-code/unchecked-boundary-invariants` — Boundary calls silently accept protocol violations

### P1-20 按段延迟（per-pid deferral）

Theme steps: T9 M5 · findings: 2

- `api-measure-code/math-text-measure-side-channel` — Math text-font metrics use a private side channel that forces a whole-document re-emit
- `emitter/full-reemit-for-math-text` — Emit consumes measurement output, and a missing math-text width re-runs emit for the whole document

### P1-21 Session 内容键缓存

Theme steps: T9 M6 · findings: 1

- `api-measure-code/adhoc-caches` — The editor fast path is five independent caches with ad hoc keys and eviction, not engine-side reuse

### P1-22 MathDict

Theme steps: T8 S1 · findings: 3

- `math/dead-data-and-params` — Dead data and parameters that obscure the real contract
- `math/doc-drift` — Design docs disagree with the as-built math subsystem
- `math/vocabulary-in-font-artifact` — The operator dictionary, atom-class enum and flags are hand-curated inside the font-metrics compiler and generated into the font header

### P1-23 MathFont 运行时对象

Theme steps: T8 S2 · findings: 2

- `math/compiled-in-font` — Exactly one math font, bound at compile time and named in four layers (C++ include, renderer, CSS, shell URL)
- `math/exactness-gaps-paint` — The zero-measurement exactness claim has untested paint-side assumptions

### P1-24 MathRow 注册表与 Call IR

Theme steps: T8 S3 · findings: 6

- `math/call-arity-silent` — Call arity is unchecked: extra arguments dropped, arity errors misreported, a missing '(' degrades the whole formula
- `math/call-construct-string-dispatch` — Structural constructs are dispatched by hard-coded name strings in both the parser and the layouter
- `math/diag-quality` — Diagnostics are coarse and can repeat
- `math/missed:1` — Function and accent names shadow their symbol meanings: a bare `dot`, `hat`, `bar` or `abs` degrades the whole formula, and the shipped HoTT example is broken
- `math/missed:5` — Parse-error containment is the whole formula, and the degrade path paints unverified glyphs
- `math/prime-then-script-degrades` — `f'^2` is a 'double script' parse error that degrades the whole formula to raw text

### P1-25 数学惰性布局

Theme steps: T8 S6 · findings: 5

- `api-measure-code/math-text-measure-side-channel` — Math text-font metrics use a private side channel that forces a whole-document re-emit
- `emitter/full-reemit-for-math-text` — Emit consumes measurement output, and a missing math-text width re-runs emit for the whole document
- `math/math-leaves-bypass-style` — Math ignores the style system: text leaves are measured with a synthesized Styling, and formula spans carry no run classes, colour, link or lang
- `math/math-text-pull-channel` — A per-feature pull state for math text runs forces whole-document re-emit and duplicates diagnostics
- `math/missing-glyph-fallback` — Uncovered codepoints get magic 600/700-unit boxes and a CSS fallback paint; magic constants inline in the algorithms

### P2-01 Node 值与 DIAG 通道

Theme steps: T2 S4 · findings: 6

- `codegen-ops-model/executor-errors-not-diagnostics` — Executor-built error nodes never reach the diagnostics channel; ctx.error ignores localOffset
- `codegen-ops-model/missed:4` — The m tag, the documented way to build content in deep code, cannot carry content values
- `codegen-ops-model/popto-stack-divergence` — $.style.popTo(h) above the current height grows the JS stack with holes
- `codegen-ops-model/shadow-mutability-forgery` — Shadows are mutable plain objects and node-ness is duck-typed
- `codegen-ops-model/val-coercion-adhoc` — Content coercion is ad hoc: functions are auto-called, null/arrays stringify, and node-ness is duck-typed
- `markup-language/value-coercion` — Splice value coercion is ad hoc: any function value is called with no arguments; arrays, null and undefined render as debug strings

### P2-02 LowerProgram 与帧

Theme steps: T2 S5 · findings: 9

- `codegen-ops-model/missed:0` — Codegen is unhygienic: one flat scope is shared by generated calls, the constructor parameter list and user bindings
- `codegen-ops-model/no-per-block-containment` — No per-top-level-block error containment; any script throw aborts the whole document
- `markup-language/missed:3` — Style-stack (schedule) ops are callable from inline splice positions but act at block granularity, retroactively, with no block-exit snapshot
- `markup-language/no-execution-containment` — Any JS error anywhere aborts the whole document; per-block containment (v2 §2, §11) is not implemented
- `parser-frontend/ctor-names-are-reserved-words` — Destructured constructor parameters make 31 common names un-bindable; #let list = .. fails the whole document
- `parser-frontend/missed:3` — Trailing content-argument desugaring is JS text surgery on a raw byte offset
- `parser-frontend/no-error-nodes` — There is no Error AST node; block-granular recovery is claimed but not implemented
- `real-world-evidence/ctor-name-collision-fatal` — A #let that collides with any of ~31 destructured constructor names kills the whole document
- `real-world-evidence/missed:0` — Runtime script errors have no per-block containment at all; one throw kills the document and the blog build

### P2-03 构造器 ABI 与注册表

Theme steps: T2 S6 · findings: 10

- `codegen-ops-model/ctor-signatures-break-sugar-equivalence` — Built-in constructor signatures are shaped by codegen, so the documented sugar⇔constructor equivalence fails
- `codegen-ops-model/missed:2` — Extension hooks are invoked under different contracts: regions are sync and uncontained, fences async and contained, formatters sync and contained
- `codegen-ops-model/private-region-builders-and-missing-ctors` — Tables, figures, groups, raw and error nodes are reachable only through private helpers; built-ins cannot be delegated to
- `codegen-ops-model/region-meta-args-hijack` — __region treats font/lang/color/sizePx as style meta-args for every region, including user handlers
- `markup-language/closed-constructor-set` — The constructor surface is a hard-coded list; several kinds produced by sugar or built-ins have no user-callable constructor
- `markup-language/ctor-signature-vs-content-args` — Positional constructor signatures break the `[…]` content-argument sugar; the documented equivalence `= 标题 ⇔ heading(1)[…]` is false
- `markup-language/region-builtin-privilege` — Region dispatcher hard-codes `table` and `figure` by name, sniffs style keys on every region, and gives region handlers a weaker contract than fence handlers
- `parser-frontend/region-fence-private-dispatch` — Region and fence sugar desugar to private dispatchers with bespoke encodings; their 'name(args)' headers are parsed twice, in different phases
- `real-world-evidence/sugar-dispatch-fixed` — Sugar compiles to fixed, non-rebindable constructors; regions have no callable constructor
- `resolver/missed:0` — No public constructor for the containers the resolver numbers (group / table / figure); `$.region` handlers cannot build them

### P2-04 出现级 span 与偏移表传输

Theme steps: T2 S7 · findings: 3

- `codegen-ops-model/occurrence-spans-unsound` — Spans are node-level (SPAN op) while content is a DAG copied per emission; spliced and user-built content is span-less
- `markup-language/span-loss` — Splice-produced and builder-produced nodes carry no source span
- `real-world-evidence/span-loss-synthesized-nodes` — Region-built nodes and splice values carry @[0,0) spans

### P2-05 通用属性、EXT、DECL、field

Theme steps: T2 S9 · findings: 3

- `codegen-ops-model/role-string-dispatch` — group{role:"…"} strings are an engine-private protocol spanning executor, ingest, resolver, emit and the semantic renderer
- `real-world-evidence/open-arg-schema` — Every feature knob is a frozen ARGK in the binary contract: five OPS_VERSION bumps in four days
- `resolver/argk-overloading` — Arg keys are reused differently by each feature instead of having one generic meaning

### P2-06 统一参数/区域头/标签/引用语法

Theme steps: T1 S7 · findings: 7

- `markup-language/arg-grammar-unification` — Four or more argument and label grammars: object-literal region/fence args, positional splice args, two label charsets, a ref charset and a comma micro-syntax
- `markup-language/universal-labels` — Labels are per-kind arguments with per-kind sugar; most blocks and all inline spans cannot be labelled
- `math/math-island-oneoff-syntax` — Island-level one-offs: whitespace-based display detection and a label honoured only on standalone displays (silently dropped otherwise)
- `parser-frontend/label-and-id-lexing-scattered` — Labels and reference ids are lexed by five unrelated routines with different alphabets; there is no generic postfix-label mechanism
- `parser-frontend/missed:3` — Trailing content-argument desugaring is JS text surgery on a raw byte offset
- `parser-frontend/region-fence-private-dispatch` — Region and fence sugar desugar to private dispatchers with bespoke encodings; their 'name(args)' headers are parsed twice, in different phases
- `real-world-evidence/missed:2` — Label syntax is lexed per construct; inline-math labels are silently swallowed, paragraph/item labels stay literal

### P2-07 语义声明、事件与公开语义构造器

Theme steps: T3 S2 · findings: 10

- `codegen-ops-model/bibliography-placeholder-and-end-emission` — Bibliography returns an empty placeholder, loads through an executor-private channel, and always emits at document end
- `codegen-ops-model/collector-switch-and-fixed-counters` — Collectors are a closed `what` string switch and counters are fixed C++ ints; users can declare neither
- `markup-language/numbered-env-hardcoding` — Counters, supplements, caption prefixes and ref text are hard-coded per kind and role; users cannot define numbered environments
- `math/equation-numbering` — Equation numbering is a hard-coded resolver counter with a hard-coded "(n)" format, carried in ArgK::name
- `real-world-evidence/counters-fixed-fields` — Counters are four hard-coded ints; the specified user counters and reset rules do not exist
- `real-world-evidence/role-figure-hardwired` — 'figure' is the only role with semantics; user roles get no number, label, caption, float or element
- `resolver/argk-overloading` — Arg keys are reused differently by each feature instead of having one generic meaning
- `resolver/extensibility-matrix` — Q2: what a user can define. Effectively none of it, and the blockers are structural
- `resolver/resolver-spans` — Resolver-built nodes for splice-built collectors and terms have empty [0,0) spans
- `resolver/rewrite-normalizations` — Structural cleanup (deleting empty paras, unwrapping blocks) lives in the resolver with its own inline-kind list

### P2-08 样式线格式变更（唯一一次 MIN_COMPAT 提升）

Theme steps: T4 M5 · findings: 8

- `api-measure-code/token-class-as-color-string` — Token class is encoded as a CSS color string, emit recovers 'comment' by string-comparing colors, and comment italics are hard-coded by tag index
- `break-layout-pages/comment-role-by-color` — Comment identity for wrapping is recovered from the CSS color string 'var(--tsr-tok-comment)'
- `codegen-ops-model/cls-sup-feature-bit` — CLS_SUP: a frozen class bit allocated for footnote markers that bundles raise, size and a line-breaking rule
- `codegen-ops-model/region-meta-args-hijack` — __region treats font/lang/color/sizePx as style meta-args for every region, including user handlers
- `codegen-ops-model/two-style-encodings-and-stack` — StyleDelta has two wire encodings; the schedule stack is only sound at top level and accepts raw class bits
- `emitter/comment-by-css-color` — Comment-aware hanging recovers the token class by comparing a run's colour to 'var(--tsr-tok-comment)'
- `real-world-evidence/missed:3` — Raw engine class bits are a public back door: $.style.push(number) and STYLE_PUSH bits are OR'd unmasked; the reader does not range-check ArgK
- `real-world-evidence/sup-attach-private` — Superscript and 'glue to previous' exist only for footnote markers; the public inline vocabulary lacks them

### P2-09 结构化引用

Theme steps: T3 S3 · findings: 3

- `markup-language/reference-forms-closed` — `@` sugar has one meaning and one form: no supplement, form or locator, and grouping is a comma string re-parsed by the resolver
- `real-world-evidence/ref-cite-format-in-cpp` — Reference and citation display forms are hard-coded in resolveRef/resolveCite; ref.form is dead
- `resolver/citation-path` — Citations: grouping by comma string, ordinals in rewrite order, a fixed numeric format, and an ordering contract with the executor

### P2-10 软换行上线（行内文本中的 U+000A）

Theme steps: T1 S13 · findings: 2

- `markup-language/cjk-softbreak-classifier` — The CJK soft-line-join rule lives in the inline parser with its own character class, which disagrees with emit's punctuation classes
- `parser-frontend/parser-owned-cjk-line-join` — The parser makes a script-dependent typographic decision at source line joins, and the predicate is duplicated in emit

### P2-11 区域无损溯源与层级范式

Theme steps: T1 S9, T2 S8 · findings: 11

- `codegen-ops-model/block-promotion-peepholes` — Block/inline level is decided by peepholes and hand lists in codegen, resolver and emit that disagree
- `codegen-ops-model/missed:1` — No level normalisation in either direction; `styled` and `link` are structural wrappers with a fixed inline level
- `codegen-ops-model/parse-time-pipe-segmentation` — Table '|' segmentation happens at parse/codegen time for every region and is lossily re-joined for non-tables
- `emitter/silent-drops-of-unhandled-kinds` — Unhandled inline and block kinds vanish without a diagnostic
- `markup-language/block-inline-placement` — Block/inline level is inferred by ad-hoc promotion and unwrapping rules split across codegen and the resolver
- `markup-language/missed:0` — Region interiors bypass ordinary block lowering: display math in any region becomes inline, loses its label, and receives the figure caption prefix
- `markup-language/region-pipe-segmentation` — Table '|' segmentation is baked into the generic region mechanism, and it is byte-level, so it corrupts content
- `math/math-span-lexer-triplication` — The `$…$` island is lexed by three independent grammars that already disagree on escapes and line-crossing
- `math/missed:0` — Region table cell splitter ignores $…$ math islands, and the island scan crosses cell boundaries: formulas with | corrupt rows and duplicate content
- `parser-frontend/display-math-by-ast-shape` — Display math is decided by a whitespace heuristic in the parser plus AST-shape pattern matching in codegen
- `parser-frontend/region-pipe-segmentation-in-parser` — The table's '|' cell convention is hard-coded into the parser for every region, via a masking lexer the design explicitly rejected

### P2-12 任意位置语句、关键字形式、内容字面量

Theme steps: T1 S8, T2 S12 · findings: 6

- `codegen-ops-model/keyword-forms-uncompiled` — #if / #for / #use and `#let x = […]` content literals compile to invalid or wrong JS
- `markup-language/nested-code-statements-dropped` — `#let` / `#{…}` inside list items, quotes and regions are silently dropped
- `markup-language/spec-features-unimplemented` — Specified surface features compile to invalid JS or are absent
- `parser-frontend/code-statements-top-level-only` — Statements are a special case of the document root: nested #let/#{ are parsed, then silently dropped
- `parser-frontend/keyword-forms-closed-set-missing` — Appendix A keyword forms and #let content literals are specified as a closed special set, and none is implemented
- `parser-frontend/no-error-nodes` — There is no Error AST node; block-granular recovery is claimed but not implemented

### P2-13 片段程序与默认 fence 中的 sidecar

Theme steps: T1 S11, T2 S10 · findings: 7

- `api-measure-code/sidecar-api-layer-rewrite` — Sidecar comments are a post-ops tree rewrite in api/doc.h, keyed by a magic role string, and lose source spans
- `codegen-ops-model/duplicate-lowering-fragment` — A second AST→content lowering (fragment.cc) bypasses ctors and ops; the m tag returns cooked text; text projection is duplicated
- `codegen-ops-model/sidecar-ingest-pass` — Code-block sidecars are a feature-specific ingest pass with its own lowering and a private role
- `emitter/sidecar-role-string` — Code sidecar rides a magic group{role:'sidecar-lines'} built in api/doc.h, recognized by string in emit
- `markup-language/sidecar-private-lowering` — Code sidecars are a fence-body micro-syntax implemented in the API layer, with a second, divergent markup→content lowering that users cannot access
- `parser-frontend/fragment-parallel-lowering` — Fragment re-entry lowers the AST with a second, hard-coded switch that bypasses constructors; m/m.parse are not wired
- `real-world-evidence/markup-reentry-missing` — m`…`/m.parse return plain text; fence handlers cannot produce markup, so the docs duplicate every example

### P2-14 参考文献就地生成

Theme steps: T2 S11 · findings: 1

- `codegen-ops-model/bibliography-placeholder-and-end-emission` — Bibliography returns an empty placeholder, loads through an executor-private channel, and always emits at document end

### P2-15 math/mathsrc 节点、洞与数学声明

Theme steps: T8 S8 · findings: 5

- `math/closed-vocabulary` — No user extension surface: documents and JS cannot define symbols, operators, math functions or macros
- `math/equation-numbering` — Equation numbering is a hard-coded resolver counter with a hard-coded "(n)" format, carried in ArgK::name
- `math/math-island-oneoff-syntax` — Island-level one-offs: whitespace-based display detection and a label honoured only on standalone displays (silently dropped otherwise)
- `math/math-opaque-string` — Math is an opaque verbatim string through ops: no splices, values, content, or structured construction from JS
- `math/toc-excerpt-drops-math` — Headings containing formulas lose them in the TOC and @ref text

### P2-16 其余 schema 变更（tcell 块体、equations、tag 槽、fill）

Theme steps: — · findings: 0


### P3-01 级联、规则、默认样式表、NodeProps

Theme steps: T4 M6 · findings: 9

- `break-layout-pages/vertical-spacing-constants` — Vertical spacing is a global paraGap with magic fractions and per-kind advances, not style-driven vertical glue
- `codegen-ops-model/kind-default-styles-in-emit` — Per-kind default presentation (heading bold/size, code mono/scale, link bit) is hard-coded in emit, downstream of every user style mechanism
- `codegen-ops-model/note-kind-and-lift` — Footnotes needed a dedicated kind (ops v6) and a bespoke resolver path for 'lift body, leave numbered marker'
- `codegen-ops-model/resolver-fabricated-styles` — Resolver fabricates presentation with fresh Styling literals that discard the inherited scope
- `codegen-ops-model/token-class-as-color` — Highlight token classes are smuggled through a colour string, and emit/layout recover semantics by string equality
- `emitter/kind-presentation-in-emit` — Heading/list/quote/code/error presentation and layout traits hard-coded in emit
- `markup-language/missed:3` — Style-stack (schedule) ops are callable from inline splice positions but act at block granularity, retroactively, with no block-exit snapshot
- `markup-language/style-surfaces` — Three styling surfaces with three different key sets, and capabilities that only built-ins can use
- `real-world-evidence/presentation-constants` — Presentation decisions are C++ literals; the static page needs a hand-written CSS copy

### P3-02 全局开关变为作用域属性

Theme steps: T4 M7 · findings: 5

- `api-measure-code/global-feature-knobs-no-cascade` — Feature knobs are global Config fields with no document, region or block scope; the only cascade is inline Styling
- `api-measure-code/magic-policy-constants` — Policy constants in the API, measure, worker and grid code are bare literals
- `emitter/codeblock-args-in-emit` — Codeblock argument mini-languages parsed in emit; code-only measurement probes special-cased in resolveWidths
- `emitter/global-typography-config` — Typographic knobs are document-global Config scalars, not per-style or per-locale properties
- `emitter/scattered-magic-constants` — Typographic magic numbers scattered in emit/layout and duplicated in CSS

### P3-03 slot、site、冻结标题克隆、计数器标记

Theme steps: T3 S4 · findings: 8

- `codegen-ops-model/resolver-fabricated-styles` — Resolver fabricates presentation with fresh Styling literals that discard the inherited scope
- `markup-language/structured-content-flattened` — Headings in the TOC, term names and label excerpts are flattened to strings, leaking footnote bodies and dropping math, refs and styling
- `real-world-evidence/heading-numbers-invisible` — Refs and the TOC print heading numbers that headings never display
- `resolver/excerpt-strings` — TOC and glossary entries are plain-text excerpts captured before anything is resolved
- `resolver/figure-role-string` — 'figure' is a role string matched by hand in four layers
- `resolver/missed:3` — A table inside a figure gets two numbers, because counters bind to node kind and not to the captioned unit
- `resolver/missed:4` — Footnote numbers have two independent sources: the scan counter for markers and the list ordinal for the notes section
- `resolver/site-display-injection` — Showing the number at the declaring node is per-kind tree surgery done inside the counting pass

### P3-04 身份与 DOM 拼写解耦（AnchorId）

Theme steps: T3 S5, T7 S9 · findings: 6

- `markup-language/label-namespace-collision` — Auto-labels (h-n, fn-n, fnref-n, bib-key, term names) share the user label namespace
- `real-world-evidence/notes-popups-dom-scraping` — Footnote popups reverse-engineer the typeset DOM through 'fn-'/'fnref-' string prefixes
- `render-runtime/anchor-namespace` — The DOM id prefix 'tsr-' and the label naming conventions are hard-coded across resolver, both serializers and the shell
- `render-runtime/shell-note-popups` — Footnote popups in the 'thin' shell scrape the engine DOM via resolver label names, a style class and assumptions about layout shape
- `resolver/anchor-namespace` — The anchor/URL scheme and the synthetic label prefixes leak into the user label namespace
- `resolver/missed:2` — The shell's footnote popup keys on resolver-internal URL prefixes and the superscript CSS class

### P3-05 RenderResult 与提交路径

Theme steps: T7 S4 · findings: 4

- `render-runtime/anchor-decode-duplication` — Source-offset ↔ DOM mapping is re-implemented outside the runtime and is missing for code, rule, raw and caption lines
- `render-runtime/missed:4` — Editing fast path reports no upgrade records
- `render-runtime/shell-chunk-byte-coupling` — The editing patch parses the serializer's exact byte layout instead of using a structured per-paragraph protocol
- `render-runtime/swap-whole-container` — Upgrades and relayout replace the whole container

### P3-06 shell 核心与 Behavior 注册表

Theme steps: T7 S5 · findings: 5

- `real-world-evidence/notes-popups-dom-scraping` — Footnote popups reverse-engineer the typeset DOM through 'fn-'/'fnref-' string prefixes
- `render-runtime/popup-breaks-patch` — A visible footnote popup disables the incremental patch path
- `render-runtime/shell-feature-inventory` — The 'thin' main-thread shell hard-wires about nine feature concerns into createEngine/typeset
- `render-runtime/shell-note-popups` — Footnote popups in the 'thin' shell scrape the engine DOM via resolver label names, a style class and assumptions about layout shape
- `resolver/missed:2` — The shell's footnote popup keys on resolver-internal URL prefixes and the superscript CSS class

### P3-07 分隔符与复制契约

Theme steps: T7 S8 · findings: 7

- `break-layout-pages/missed:2` — Copy 'join' is derived from alignment (isLast || ragged), so wrapped headings and block captions copy with newlines
- `render-runtime/audit-hint-attributes` — data-ragged and data-cell are audit hints decided per feature in the serializer, with overloaded meanings
- `render-runtime/copy-drops-blank-code-lines` — Copy collapses blank lines inside code blocks
- `render-runtime/copy-line-separators` — Line joins are a three-state attribute patched per feature; cells, sidecars and empty lines fall through to a newline
- `render-runtime/copy-syn-policy` — Copy semantics of data-syn are hand-coded per feature: math uses data-src, everything else is dropped, and resolver-synthesized text is treated inconsistently
- `render-runtime/missed:5` — Copy falls back to native copy for synthetic-only selections, copying exactly what §9.3 says to skip
- `render-runtime/normative-doc-drift` — The normative serializer and runtime docs contradict the as-built behaviour and each other

### P3-08 ParShape 与侧向排除区

Theme steps: T6 S5 · findings: 6

- `break-layout-pages/float-adds-paragraph-gap` — An 'out-of-flow' float still costs one paraGap in the flow
- `break-layout-pages/float-indent-geometry` — Occlusion is a width delta relative to each unit's own indent, so floats and wrapped text at different indents misalign
- `break-layout-pages/measure-definition-split` — Justification uses two definitions of the measure (unfloored widthPx vs su), kept to avoid golden churn
- `break-layout-pages/parshape-prefix-form` — LineWidths is a one-step prefix parshape (one narrow width for the first K lines), with the left offset stored separately on the unit
- `break-layout-pages/wide-float-overprints-text` — A float wider than measure − 1em − 1px neither narrows nor clears the following text, and very narrow wrap columns are accepted
- `real-world-evidence/parshape-prefix` — LineWidths is a two-piece prefix; stacked floats, hanging indent and first-line indent are separate special cases

### P3-09 LineEnds 取代对齐标志

Theme steps: T6 S8 · findings: 1

- `break-layout-pages/alignment-flags` — Alignment is per-feature flags and per-kind centering code, not line-end glue that the breaker knows about

### P3-10 表格布局器

Theme steps: T6 S9 · findings: 1

- `break-layout-pages/table-closed` — Table layout is one hard-wired algorithm (equal columns, constant padding, a rule after every row), duplicated in two modules

### P3-11 网格布局器；代码块 + sidecar 两轨表

Theme steps: T6 S10 · findings: 7

- `api-measure-code/grid-is-codeblock-only` — The character grid (verbatim layout) is reachable only through codeblock and lives inline in layoutDoc with hard-coded policy
- `api-measure-code/missed:4` — Grid budget, measured CJK ratio and snap-kerning are silently disabled by `wrap: false`
- `api-measure-code/snap-ignores-sidecar-partition` — In snap mode the column count ignores the sidecar partition, so code overflows into the sidecar column (verified)
- `break-layout-pages/code-sidecar-three-box` — The verbatim three-box model (gutter/code/sidecar) is hand-coded as Code-unit fields plus an equal-height row zip
- `break-layout-pages/code-wrap-in-layout` — Code wrapping is a second, private line breaker inside layoutDoc, with hard-coded break classes and magic column constants
- `break-layout-pages/snap-kerning-ignores-sidecar` — With snap-kerning on, the code column budget uses the full measure and ignores the sidecar partition
- `emitter/codeblock-args-in-emit` — Codeblock argument mini-languages parsed in emit; code-only measurement probes special-cased in resolveWidths

### P3-12 VList 与分页阶段

Theme steps: T6 S11 · findings: 5

- `break-layout-pages/doc-drift` — Several layout and pagination docs disagree with the code
- `break-layout-pages/missed:0` — Paged renderer anchor bookkeeping drops or duplicates label ids
- `break-layout-pages/paged-atoms-clipped` — Atomic bands taller than a sheet are clipped in print, losing content
- `break-layout-pages/paginator-in-serializer` — Pagination is a band cutter inside the HTML serializer that re-derives keep rules and atomicity from node kinds
- `render-runtime/paged-keep-rules-by-kind` — Pagination, with keep rules dispatched on FlowUnit and content kinds, lives inside the HTML serializer; page geometry lives in the shell

### P3-13 新集合、flow 与计数器

Theme steps: T3 S6 · findings: 2

- `codegen-ops-model/note-kind-and-lift` — Footnotes needed a dedicated kind (ops v6) and a bespoke resolver path for 'lift body, leave numbered marker'
- `markup-language/collectors-closed` — Collector set is a closed string switch; each collector has its own placement and data channel

### P3-14 作者面特征（traits）与表格扩展

Theme steps: T6 S12 · findings: 3

- `break-layout-pages/group-role-dispatch` — Groups get geometry only through hard-coded role strings; user regions have no box model in typeset mode
- `break-layout-pages/table-closed` — Table layout is one hard-wired algorithm (equal columns, constant padding, a rule after every row), duplicated in two modules
- `real-world-evidence/table-model-v1` — Table args are a column count plus a one-letter align string; no captions, headers, spans or widths

### P3-15 通用放置与独立布局（InlineBlock/子图）

Theme steps: T6 S13 · findings: 2

- `break-layout-pages/float-model-closed` — The float model is closed: images only, two sides, same-side stacking, everything else clears, and occlusion is a width delta
- `real-world-evidence/figure-model-single-image` — Figure = one image plus caption paragraphs; floating and subfigures are figure-only special cases

### P3-16 几何权威

Theme steps: T7 S10 · findings: 4

- `render-runtime/error-render-divergence` — Error nodes do not follow the §9.1 contract in typeset output
- `render-runtime/marker-gutter` — List markers and code line numbers share one out-of-flow `.tsr-marker` CSS trick; line numbers are recognisable only because they happen to carry the code style
- `render-runtime/paged-gutter-clipping` — Print sheets clip the line numbers of code blocks at the measure's left edge
- `render-runtime/trailing-float-and-gap-drift` — A trailing float overflows the document box; screen and layout disagree on paragraph y

### P3-17 块入行内的拆分策略

Theme steps: T2 S8b · findings: 1

- `markup-language/block-inline-placement` — Block/inline level is inferred by ad-hoc promotion and unwrapping rules split across codegen and the resolver

### P3-18 类名渲染与主题拆分

Theme steps: T4 M8 · findings: 3

- `render-runtime/css-contract-monolith` — TSR_CSS mixes the robustness contract, metric-bearing class mappings and per-feature paint in one hand-maintained string
- `render-runtime/no-class-channel` — Run styling is inline-only and there is no user class channel; dynClasses is specified but not implemented
- `render-runtime/token-theme-sniffing` — Code-token semantics travel as CSS colour strings, and emit detects comments by string-comparing a colour

### P3-19 基线权威

Theme steps: T7 S11 · findings: 1

- `render-runtime/host-line-height-leak` — The host page's CSS line-height leaks into vertical geometry inside lines

### P3-20 安全评审检查点

Theme steps: — · findings: 0


### P3-21 ResourceHost、定位器、引用清单、静态导出

Theme steps: T9 M7 · findings: 3

- `api-measure-code/resource-io-paths` — Resource I/O is special-cased per resource: URL base, transport, cache and failure policy all differ
- `real-world-evidence/resource-and-config-plumbing` — Per-resource pull states and per-knob C exports; bibliography uses a third mechanism; Node and browser get different configs
- `render-runtime/static-export-template` — The static exporter hard-codes the page template, fonts, language and hydration script, and cannot enumerate document resources

### P3-22 代码高亮清单与引擎侧 overlay

Theme steps: T9 M8 · findings: 4

- `api-measure-code/language-registry-scattered` — The language set and alias normalization are hard-coded in five places, with no runtime registration
- `api-measure-code/literate-cpp-special-case` — Literate-fragment recognition is hard-wired to the cpp provider and absent from the native provider
- `api-measure-code/token-tag-table-copies` — Token tag set, alias table and priority contract are hand-synced copies across C++, worker JS, editor JS and CSS
- `real-world-evidence/literate-cpp-hack` — Literate-programming fragments are a global regex special case inside the C++ token provider

### P3-23 PresentationMap（元素行的 html 段）

Theme steps: T7 S12 · findings: 5

- `api-measure-code/missed:2` — Sidecar notes disappear from the semantic and static-export output
- `real-world-evidence/missed:5` — Semantic figure serializer guesses structure from the first-paragraph-is-caption convention: captions concatenate, nested blocks land inside <figcaption>
- `render-runtime/semantic-role-switch` — The semantic serializer hard-codes role "figure" plus several per-construct shapes instead of using a role→element mapping
- `render-runtime/sidecar-dropped-in-semantic` — Sidecar comments are removed from the no-JS / static page
- `render-runtime/typeset-role-blind` — The typeset/paged DOM carries no role or kind hook: headings, captions, notes, theorems and TOC are anonymous lines

### P3-24 SymbolInfo 身份与数据驱动的数学族

Theme steps: T8 S4 · findings: 11

- `math/alphabet-variants` — Only blackboard bold exists, via 26 enumerated `AA..ZZ` rows; Euler's bold/fraktur/sans/mono/script alphabets are compiled in but unreachable
- `math/bracket-shedding-any-group` — Script and fraction arguments shed any bracket kind, not just parentheses
- `math/fence-pairs-ascii-only` — Only the three ASCII bracket pairs form stretchy groups; ⟨⟩, |…|, ‖…‖ and named delimiters never stretch, and the stretchy flag is dead
- `math/implicit-names-op-class` — Multi-letter word heuristics: implicit upright names classed Op, and script-context letter splitting that depends on dictionary membership
- `math/lexer-hardcoded-alphabet` — Lexer token classes are hand-coded and disagree with dictionary keys: unreachable entries, bespoke _|_ and !word paths
- `math/missed:2` — Converters carry a fourth, divergent copy of the math vocabulary that targets names the engine does not implement
- `math/missed:3` — Token kind, not symbol identity, drives the parser: typed Unicode loses LARGE/LIMITS/ACCENT, and typed relations do not end a big-operator body
- `math/missed:4` — No spacing, style or limits constructs: Spacer is unreachable from syntax, and display-ness is only expressible by whitespace inside the fences
- `math/negation-enumerated` — `!` negation is a list of enumerated pairs with inconsistent classes and wrong output for unlisted relations
- `math/vocabulary-in-font-artifact` — The operator dictionary, atom-class enum and flags are hand-curated inside the font-metrics compiler and generated into the font header
- `real-world-evidence/math-leniency-silent` — Converter-driven math leniencies hide malformed input without diagnostics

### P3-25 运算符原子与单一 mlist→item 转换

Theme steps: T8 S5 · findings: 3

- `math/bigop-greedy-body` — Big operators are a dedicated node with a layout-level greedy body; text operators with limits take a different path
- `math/segmentation-class-preview` — Inline break segmentation re-derives atom classes in a parallel 'preview' and passes break kinds as magic codes mapped to three config keys
- `math/spacing-edge-classes` — Small spacing-model inconsistencies

### P3-26 数学采用通用协议；公式编号由布局测量

Theme steps: T8 S7 · findings: 5

- `emitter/math-only-inline-box` — The only inline object is math (LinebreakBlock::math); inline image, raw and mathblock vanish
- `math/display-math-unit` — Display formulas get a dedicated FlowUnit kind and fields, LineBox special=4, renderer-side geometry, and an unmeasured CSS-positioned equation number
- `math/fallback-and-a11y` — Semantic fallback and accessibility leave free determinism unused
- `math/inline-math-special-block` — Inline formulas are a typed `MathBox*` on LinebreakBlock, with math branches in emit, layout (three copies), render, copy and audit
- `resolver/site-display-injection` — Showing the number at the declaring node is per-kind tree surgery done inside the counting pass

### P3-27 语义页数学盒与无障碍

Theme steps: T7 S13 · findings: 3

- `math/fallback-and-a11y` — Semantic fallback and accessibility leave free determinism unused
- `real-world-evidence/missed:4` — The static/semantic page (what the blog ships at build time and in RSS) prints math as $…$ source
- `render-runtime/typeset-a11y` — The typeset DOM has no accessibility semantics

### P3-28 宿主测量的替换盒（boxInfo）

Theme steps: T9 M11, T6 S14 · findings: 1

- `break-layout-pages/missed:4` — User blocks cannot be measured: raw has an author-declared fixed height, while built-ins get bespoke pull channels

### P3-29 数学网格、equations 与显示行

Theme steps: T8 S9, T6 S15 · findings: 2

- `math/display-math-unit` — Display formulas get a dedicated FlowUnit kind and fields, LineBox special=4, renderer-side geometry, and an unmeasured CSS-positioned equation number
- `real-world-evidence/math-extensibility` — Math has no macros, no 2-D array primitive and no alphabets; converters expand by regex and flatten

### P3-30 LocalePack 与文档语言

Theme steps: T4 M9 · findings: 3

- `api-measure-code/supplements-and-lang` — Cross-reference supplements are one Config field per counted kind, localized by a two-way host-level lang switch
- `markup-language/quote-context-heuristic` — Curly-quote width class is chosen from neighbouring characters with no markup-level override
- `real-world-evidence/locale-terms-switch` — Supplement words are a two-branch if/else; document language is only a host option

### P3-31 跨文档标签、项目驱动、#use

Theme steps: T3 S7, T9 M10 · findings: 4

- `codegen-ops-model/keyword-forms-uncompiled` — #if / #for / #use and `#let x = […]` content literals compile to invalid or wrong JS
- `markup-language/spec-features-unimplemented` — Specified surface features compile to invalid JS or are absent
- `parser-frontend/keyword-forms-closed-set-missing` — Appendix A keyword forms and #let content literals are specified as a closed special set, and none is implemented
- `real-world-evidence/no-cross-document-labels` — No cross-document references, project numbering or #use: books split into files lose every inter-file ref

### P3-32 boxInfo 在布局阶段消费；宽度依赖的编译期证明

Theme steps: T9 M12 · findings: 1

- `emitter/measure-dependent-geometry-in-emit` — Emit applies per-feature geometry policy against cfg.widthPx (image clamp/scale, placeholder 1/3, sidecar fraction)

### P3-33 正文防护、自动链接、转义、硬换行

Theme steps: T1 S10 · findings: 2

- `markup-language/ambiguity-hazards` — Surface ambiguity hazards in prose
- `parser-frontend/sigil-context-rules` — Sigil context rules are per-feature: '@' has an identifier lookbehind, '#' and '_' do not, and bare-URL autolink is missing

### P3-34 描述列表（/ term: desc）

Theme steps: — · findings: 1

- `real-world-evidence/description-list-missing` — No definition/description list; #term is a glossary site, so converters and the blog emulate one with bold text

### P3-35 打印器、转换器套件、front matter、语料重转

Theme steps: T1 S12 · findings: 5

- `markup-language/doc-drift` — User-facing and design docs describe syntax the engine does not accept
- `parser-frontend/front-matter-editor-only` — SSG front matter is recognised only by the VS Code preview
- `real-world-evidence/converter-code-duplication` — Converter lineages copy entity tables, inline rules and figure regexes
- `real-world-evidence/converter-fidelity-unchecked` — Corpus acceptance measures layout, not conversion fidelity; published examples contain conversion errors
- `real-world-evidence/no-tsm-printer` — No canonical .tsm printer: every converter concatenates strings with partial, divergent escaping

### P3-36 导出包

Theme steps: T7 S14 · findings: 1

- `render-runtime/static-export-template` — The static exporter hard-codes the page template, fonts, language and hydration script, and cannot enumerate document resources

### P3-37 ABI 收尾与文档修订

Theme steps: T9 M9 · findings: 3

- `api-measure-code/doc-drift-api-subsystem` — Normative docs describe API, measure and highlight behaviour that was never built
- `api-measure-code/per-feature-api-entry-points` — The C ABI grows one export per feature, stage inspection is native-only, and the documented general API was not built
- `real-world-evidence/spec-drift` — Normative docs describe mechanisms that do not exist; tools were written against the smaller reality

### P4-01 按 run 实例成 run

Theme steps: T5 4. · findings: 3

- `emitter/kern-context-postpass` — Cross-space/junction kerning: a post-pass that guesses the renderer's run boundaries; cutoff at U+2000
- `emitter/missed:0` — Render run formation ignores BF_REF, so copy drops real prose next to citations and unresolved refs
- `render-runtime/missed:0` — Run coalescing ignores the synthetic/ref flag, so copy deletes real prose next to citations and copies synthetic brackets

### P4-02 段落级成形器

Theme steps: T5 5. · findings: 9

- `emitter/boundary-glue-constant` — CJK-Latin 0.25em glue is a constexpr inserted only on in-node Latin/ideograph transitions
- `emitter/latin-quote-heuristic` — Curly-quote CJK/Latin decision: four codepoints plus a previous/next-character rule inside one node; lang ignored
- `emitter/missed:1` — Break after an inline formula is unconditional; Latin closers and commas can start a line
- `emitter/missed:4` — Curly-quote class is wrong even inside one text node after CJK punctuation
- `emitter/paragraph-blind-script-context` — Script/kinsoku context is reset per text node; math alone got cross-node patches
- `markup-language/cjk-softbreak-classifier` — The CJK soft-line-join rule lives in the inline parser with its own character class, which disagrees with emit's punctuation classes
- `markup-language/quote-context-heuristic` — Curly-quote width class is chosen from neighbouring characters with no markup-level override
- `parser-frontend/parser-owned-cjk-line-join` — The parser makes a script-dependent typographic decision at source line joins, and the predicate is duplicated in emit
- `real-world-evidence/codepoint-heuristics` — Corpus fixes classify text by hard-coded codepoints and byte length instead of lang/semantic properties

### P4-03 逐项源 span

Theme steps: T5 6. · findings: 2

- `emitter/coarse-source-spans` — Every block carries its whole text node's span; run and line offsets degenerate
- `render-runtime/missed:3` — Run-level data-s is the text-node start, not the run start (§9.1 1:1 anchoring not met)

### P4-04 TextProps v1；标点/空白/autospace 数据化

Theme steps: T5 7. · findings: 3

- `emitter/defined-width-dash-ellipsis` — ——/…… defined widths: two hard-coded codepoints, an in-node context heuristic, and a BF_PAIR renderer branch
- `emitter/punct-compression-control-flow` — clreq adjacency matrix and the 0.5em blank are spread across emit branches, a width rule, a renderer heuristic and CSS
- `emitter/scattered-magic-constants` — Typographic magic numbers scattered in emit/layout and duplicated in CSS

### P4-05 UCD 字符类（RULES_VERSION 1）与 Unicode 控制符

Theme steps: T5 8. · findings: 2

- `emitter/hardcoded-script-class-tables` — CJK/punctuation classes are hand-written ranges and switch lists, duplicated in 5 layers, SC-only
- `emitter/missed:2` — Unicode break-control characters are ignored: ZWSP, SHY, NBSP and U+3000

### P4-06 连字注册表、ExHyphen、hyphens/overflowWrap

Theme steps: T5 9. · findings: 3

- `break-layout-pages/break-policy-config-knobs` — Break-opportunity policy is one config knob per feature plus style-bit tests in emit
- `emitter/hyphenation-en-us-only` — Hyphenation: one compiled-in en-US trie, ASCII-only word core, lang ignored, explicit hyphens never break, '-' hard-coded in render
- `emitter/url-break-special-path` — Emergency (URL) breaks: separate code path with a byte threshold and fixed separators, disabled in headings and captions

### P4-07 attach 语义；脚注附着移出解析器

Theme steps: T5 10., T1 S14 · findings: 3

- `emitter/sup-bit-attach-rule` — Footnote-marker 'never start a line' keyed on the CLS_SUP style bit; inline anchors only via labelled ref
- `markup-language/footnote-sugar-oneoff` — `^[…]` footnote is a one-off inline form with its own scanner and spacing rule; its marker and note numbering are fixed
- `real-world-evidence/sup-attach-private` — Superscript and 'glue to previous' exist only for footnote markers; the public inline vocabulary lacks them

### P4-08 原生项断行器与统一伸缩模型

Theme steps: T5 11., T6 S16 · findings: 4

- `break-layout-pages/break-policy-config-knobs` — Break-opportunity policy is one config knob per feature plus style-bit tests in emit
- `break-layout-pages/glue-semantics-split` — Glue semantics are re-derived from flag bits in three layers, so breaker, layout and render disagree
- `break-layout-pages/hyphen-url-not-discretionary` — Hyphen and URL breaks are special block types, not a general discretionary item
- `emitter/kp-ignores-stretch-weight` — The k-rule claim 'cost model and renderer agree by construction' does not hold: KP never reads stretchWeight

### P5-01 多字体数学链与宿主数学字体

Theme steps: T8 S10 · findings: 2

- `math/compiled-in-font` — Exactly one math font, bound at compile time and named in four layers (C++ include, renderer, CSS, shell URL)
- `math/missing-glyph-fallback` — Uncovered codepoints get magic 600/700-unit boxes and a CSS fallback paint; magic constants inline in the algorithms

### P5-02 收尾：残留检查、文档、最终报告

Theme steps: — · findings: 1

- `real-world-evidence/spec-drift` — Normative docs describe mechanisms that do not exist; tools were written against the smaller reality

## B. By finding

| finding | kind | sev | plan steps | status |
|---|---|---|---|---|
| `markup-language/region-pipe-segmentation` | adhoc | high | P0-04, P2-11 | |
| `markup-language/inline-delimiter-scanners` | adhoc | high | P1-08 | |
| `markup-language/closed-constructor-set` | adhoc | high | P2-03 | |
| `markup-language/ctor-signature-vs-content-args` | adhoc | high | P2-03 | |
| `markup-language/arg-grammar-unification` | adhoc | medium | P2-06 | |
| `markup-language/numbered-env-hardcoding` | adhoc | high | P2-07 | |
| `markup-language/universal-labels` | adhoc | high | P2-06 | |
| `markup-language/label-namespace-collision` | adhoc | medium | P0-09, P3-04 | |
| `markup-language/region-builtin-privilege` | adhoc | medium | P2-03 | |
| `markup-language/block-inline-placement` | adhoc | medium | P2-11, P3-17 | |
| `markup-language/value-coercion` | adhoc | medium | P2-01 | |
| `markup-language/surface-grammar-drift` | adhoc | medium | P1-09 | |
| `markup-language/sidecar-private-lowering` | adhoc | medium | P2-13 | |
| `markup-language/collectors-closed` | adhoc | medium | P3-13 | |
| `markup-language/style-surfaces` | adhoc | medium | P3-01 | |
| `markup-language/cjk-softbreak-classifier` | adhoc | medium | P2-10, P4-02 | |
| `markup-language/reference-forms-closed` | adhoc | medium | P2-09 | |
| `markup-language/footnote-sugar-oneoff` | adhoc | low | P4-07 | |
| `markup-language/quote-context-heuristic` | adhoc | low | P3-30, P4-02 | |
| `markup-language/no-execution-containment` | issue | high | P0-05, P2-02 | |
| `markup-language/spec-features-unimplemented` | issue | high | P0-05, P2-12, P3-31 | |
| `markup-language/nested-code-statements-dropped` | issue | high | P0-05, P2-12 | |
| `markup-language/inline-scanner-overrun` | issue | high | P1-06 | |
| `markup-language/same-line-trailing-text-dropped` | issue | medium | P1-07 | |
| `markup-language/region-error-recovery` | issue | medium | P1-07 | |
| `markup-language/span-loss` | issue | medium | P2-04 | |
| `markup-language/structured-content-flattened` | issue | medium | P3-03 | |
| `markup-language/ambiguity-hazards` | issue | medium | P3-33 | |
| `markup-language/ast-dump-note` | issue | low | P0-02 | grep:plan P0-02 |
| `markup-language/doc-drift` | issue | low | P3-35 | |
| `markup-language/missed:0` | missed | high | P2-11 | |
| `markup-language/missed:1` | missed | medium | P1-07 | |
| `markup-language/missed:2` | missed | medium | P1-07 | |
| `markup-language/missed:3` | missed | medium | P2-02, P3-01 | |
| `markup-language/missed:4` | missed | low | P1-07 | |
| `markup-language/missed:5` | missed | low | P1-07 | |
| `parser-frontend/per-feature-ast-kinds` | adhoc | high | P1-05 | |
| `parser-frontend/inline-recognizer-cascade` | adhoc | high | P1-06 | |
| `parser-frontend/region-pipe-segmentation-in-parser` | adhoc | high | P2-11 | |
| `parser-frontend/cross-line-raw-scans` | adhoc | high | P1-08 | |
| `parser-frontend/content-args-inline-only` | adhoc | high | P1-08 | |
| `parser-frontend/keyword-forms-closed-set-missing` | adhoc | high | P2-12, P3-31 | |
| `parser-frontend/code-statements-top-level-only` | adhoc | high | P2-12 | |
| `parser-frontend/label-and-id-lexing-scattered` | adhoc | medium | P2-06 | |
| `parser-frontend/display-math-by-ast-shape` | adhoc | medium | P2-11 | |
| `parser-frontend/parser-owned-cjk-line-join` | adhoc | medium | P2-10, P4-02 | |
| `parser-frontend/fragment-parallel-lowering` | adhoc | high | P2-13 | |
| `parser-frontend/region-fence-private-dispatch` | adhoc | medium | P2-03, P2-06 | |
| `parser-frontend/region-container-special-case` | adhoc | medium | P1-07 | |
| `parser-frontend/multiple-tsm-grammars` | adhoc | high | P1-09 | |
| `parser-frontend/sigil-context-rules` | adhoc | medium | P3-33 | |
| `parser-frontend/front-matter-editor-only` | adhoc | low | P3-35 | |
| `parser-frontend/editor-region-builder-list` | adhoc | low | P1-09 | |
| `parser-frontend/contiguous-escapes-blocks` | issue | high | P1-06 | |
| `parser-frontend/inline-comment-leaks-block-structure` | issue | high | P1-08 | |
| `parser-frontend/unterminated-let-swallows-document` | issue | high | P0-04, P1-07 | |
| `parser-frontend/ctor-names-are-reserved-words` | issue | high | P0-05, P2-02 | |
| `parser-frontend/trailing-text-after-block-closers-dropped` | issue | medium | P1-07 | |
| `parser-frontend/fence-double-dedent-in-containers` | issue | medium | P1-07 | |
| `parser-frontend/span-fidelity` | issue | medium | P1-07 | |
| `parser-frontend/no-error-nodes` | issue | medium | P2-02, P2-12 | |
| `parser-frontend/docs-drift` | issue | medium | P1-09 | |
| `parser-frontend/ast-dump-missing-note` | issue | low | P0-02 | grep:plan P0-02 |
| `parser-frontend/missed:0` | missed | high | P0-04, P1-06 | |
| `parser-frontend/missed:1` | missed | high | P1-06 | |
| `parser-frontend/missed:2` | missed | medium | P1-07 | |
| `parser-frontend/missed:3` | missed | medium | P2-02, P2-06 | |
| `parser-frontend/missed:4` | missed | low | P1-06 | |
| `parser-frontend/missed:5` | missed | low | P1-07 | |
| `codegen-ops-model/ctor-signatures-break-sugar-equivalence` | adhoc | high | P2-03 | |
| `codegen-ops-model/private-region-builders-and-missing-ctors` | adhoc | high | P2-03 | |
| `codegen-ops-model/region-meta-args-hijack` | adhoc | medium | P2-03, P2-08 | |
| `codegen-ops-model/parse-time-pipe-segmentation` | adhoc | medium | P2-11 | |
| `codegen-ops-model/role-string-dispatch` | adhoc | high | P2-05 | |
| `codegen-ops-model/sidecar-ingest-pass` | adhoc | medium | P2-13 | |
| `codegen-ops-model/bibliography-placeholder-and-end-emission` | adhoc | medium | P2-07, P2-14 | |
| `codegen-ops-model/cls-sup-feature-bit` | adhoc | medium | P2-08 | |
| `codegen-ops-model/kind-default-styles-in-emit` | adhoc | medium | P3-01 | |
| `codegen-ops-model/resolver-fabricated-styles` | adhoc | medium | P3-01, P3-03 | |
| `codegen-ops-model/token-class-as-color` | adhoc | medium | P3-01 | |
| `codegen-ops-model/fixed-styling-fields` | adhoc | high | P1-02 | |
| `codegen-ops-model/global-argk-namespace` | adhoc | high | P0-06 | |
| `codegen-ops-model/version-bump-per-vocabulary` | adhoc | medium | P1-01 | |
| `codegen-ops-model/two-style-encodings-and-stack` | adhoc | medium | P0-06, P2-08 | |
| `codegen-ops-model/val-coercion-adhoc` | adhoc | medium | P2-01 | |
| `codegen-ops-model/block-promotion-peepholes` | adhoc | medium | P2-11 | |
| `codegen-ops-model/duplicate-lowering-fragment` | adhoc | medium | P2-13 | |
| `codegen-ops-model/note-kind-and-lift` | adhoc | medium | P3-01, P3-13 | |
| `codegen-ops-model/collector-switch-and-fixed-counters` | adhoc | medium | P2-07 | |
| `codegen-ops-model/occurrence-spans-unsound` | issue | high | P2-04 | |
| `codegen-ops-model/exponential-instantiation` | issue | high | P0-07 | |
| `codegen-ops-model/no-per-block-containment` | issue | high | P0-05, P2-02 | |
| `codegen-ops-model/keyword-forms-uncompiled` | issue | high | P0-05, P2-12, P3-31 | |
| `codegen-ops-model/css-injection-style-values` | issue | high | P0-06, P1-02 | |
| `codegen-ops-model/sizepx-ignored-in-emit` | issue | medium | P0-08 | |
| `codegen-ops-model/argtag-node-dangling` | issue | medium | P0-06 | |
| `codegen-ops-model/metric-key-fragmentation` | issue | medium | P0-08, P1-04 | |
| `codegen-ops-model/tree-dump-omits-sup` | issue | low | P1-02 | |
| `codegen-ops-model/executor-errors-not-diagnostics` | issue | low | P2-01 | |
| `codegen-ops-model/shadow-mutability-forgery` | issue | low | P2-01 | |
| `codegen-ops-model/popto-stack-divergence` | issue | low | P0-08, P2-01 | |
| `codegen-ops-model/contract-doc-drift` | issue | low | P0-06 | |
| `codegen-ops-model/missed:0` | missed | high | P0-05, P2-02 | |
| `codegen-ops-model/missed:1` | missed | high | P2-11 | |
| `codegen-ops-model/missed:2` | missed | medium | P2-03 | |
| `codegen-ops-model/missed:3` | missed | medium | P0-06 | |
| `codegen-ops-model/missed:4` | missed | low | P2-01 | |
| `resolver/fixed-counter-set` | adhoc | high | P1-10 | |
| `resolver/figure-role-string` | adhoc | high | P3-03 | |
| `resolver/label-registration-per-kind` | adhoc | high | P1-10 | |
| `resolver/ref-display-switch` | adhoc | high | P1-10 | |
| `resolver/collector-what-dispatch` | adhoc | high | P1-10 | |
| `resolver/footnote-pipeline` | adhoc | high | P1-10 | |
| `resolver/citation-path` | adhoc | high | P2-09 | |
| `resolver/supplement-config` | adhoc | medium | P1-10 | |
| `resolver/numbering-format-hardcoded` | adhoc | medium | P1-10 | |
| `resolver/site-display-injection` | adhoc | medium | P3-03, P3-26 | |
| `resolver/term-rewrite` | adhoc | medium | P1-10 | |
| `resolver/excerpt-strings` | adhoc | medium | P3-03 | |
| `resolver/presentation-constants` | adhoc | medium | P1-10 | |
| `resolver/anchor-namespace` | adhoc | medium | P3-04 | |
| `resolver/argk-overloading` | adhoc | medium | P0-06, P2-05, P2-07 | |
| `resolver/rewrite-normalizations` | adhoc | low | P0-07, P2-07 | |
| `resolver/cite-ordinal-pass-order` | issue | high | P0-09 | |
| `resolver/extensibility-matrix` | issue | high | P2-07 | |
| `resolver/invariant-declares-decides` | issue | high | P1-10 | |
| `resolver/collector-aliasing` | issue | medium | P0-09 | |
| `resolver/reserved-label-collision` | issue | medium | P0-09 | |
| `resolver/absolute-style-loss` | issue | medium | P0-09 | |
| `resolver/semantic-tight-item-anchor` | issue | medium | P0-09 | |
| `resolver/duplicate-label-dom-ids` | issue | low | P0-09 | |
| `resolver/spec-drift` | issue | medium | P1-10 | |
| `resolver/grouped-cite-all-or-nothing` | issue | low | P0-09 | |
| `resolver/resolver-spans` | issue | low | P2-07 | |
| `resolver/fragile-aggregate-init` | issue | low | P0-09 | |
| `resolver/untested-diagnostics` | issue | low | P0-09 | |
| `resolver/missed:0` | missed | high | P2-03 | |
| `resolver/missed:1` | missed | medium | P0-09 | |
| `resolver/missed:2` | missed | medium | P3-04, P3-06 | |
| `resolver/missed:3` | missed | medium | P3-03 | |
| `resolver/missed:4` | missed | low | P3-03 | |
| `resolver/missed:5` | missed | low | P0-09, P1-03, P1-10 | |
| `emitter/paragraph-blind-script-context` | adhoc | high | P4-02 | |
| `emitter/hardcoded-script-class-tables` | adhoc | high | P4-05 | |
| `emitter/punct-compression-control-flow` | adhoc | high | P4-04 | |
| `emitter/hyphenation-en-us-only` | adhoc | high | P4-06 | |
| `emitter/math-only-inline-box` | adhoc | high | P1-13, P3-26 | |
| `emitter/flowunit-kind-switch` | adhoc | high | P1-18 | |
| `emitter/figure-role-string-dispatch` | adhoc | high | P1-18 | |
| `emitter/measure-dependent-geometry-in-emit` | adhoc | high | P1-16, P3-32 | |
| `emitter/bf-flag-overload-and-rederivation` | adhoc | medium | P1-12 | |
| `emitter/url-break-special-path` | adhoc | medium | P4-06 | |
| `emitter/boundary-glue-constant` | adhoc | medium | P4-02 | |
| `emitter/defined-width-dash-ellipsis` | adhoc | medium | P4-04 | |
| `emitter/latin-quote-heuristic` | adhoc | medium | P4-02 | |
| `emitter/sup-bit-attach-rule` | adhoc | medium | P4-07 | |
| `emitter/kern-context-postpass` | adhoc | medium | P4-01 | |
| `emitter/sidecar-role-string` | adhoc | medium | P2-13 | |
| `emitter/comment-by-css-color` | adhoc | medium | P2-08 | |
| `emitter/kind-presentation-in-emit` | adhoc | medium | P3-01 | |
| `emitter/global-typography-config` | adhoc | medium | P3-02 | |
| `emitter/codeblock-args-in-emit` | adhoc | low | P0-06, P3-02, P3-11 | |
| `emitter/anchor-opt-in-per-kind` | adhoc | low | P1-18 | |
| `emitter/scattered-magic-constants` | adhoc | low | P3-02, P4-04 | |
| `emitter/stale-emit-on-relayout` | issue | high | P1-16 | |
| `emitter/sizepx-em-mismatch` | issue | high | P0-08 | |
| `emitter/kp-counts-discardable-glue` | issue | medium | P0-12 | |
| `emitter/kp-ignores-stretch-weight` | issue | medium | P4-08 | |
| `emitter/negative-wordspacing-overfull` | issue | medium | P0-12 | |
| `emitter/duplicate-diagnostics-on-reemit` | issue | medium | P0-11 | |
| `emitter/silent-drops-of-unhandled-kinds` | issue | medium | P1-13, P2-11 | |
| `emitter/coarse-source-spans` | issue | low | P4-03 | |
| `emitter/full-reemit-for-math-text` | issue | low | P1-20, P1-25 | |
| `emitter/dump-hides-finite-penalties` | issue | low | P1-12 | |
| `emitter/missed:0` | missed | high | P0-10, P4-01 | |
| `emitter/missed:1` | missed | medium | P4-02 | |
| `emitter/missed:2` | missed | medium | P4-05 | |
| `emitter/missed:3` | missed | medium | P1-17 | |
| `emitter/missed:4` | missed | medium | P4-02 | |
| `emitter/missed:5` | missed | medium | P1-04 | |
| `break-layout-pages/glue-semantics-split` | adhoc | high | P4-08 | |
| `break-layout-pages/hyphen-url-not-discretionary` | adhoc | medium | P4-08 | |
| `break-layout-pages/break-policy-config-knobs` | adhoc | medium | P4-06, P4-08 | |
| `break-layout-pages/parshape-prefix-form` | adhoc | high | P3-08 | |
| `break-layout-pages/float-tracker-replay` | adhoc | high | P1-15 | |
| `break-layout-pages/float-model-closed` | adhoc | high | P3-15 | |
| `break-layout-pages/unit-kind-switch` | adhoc | high | P1-18 | |
| `break-layout-pages/nested-stream-copies` | adhoc | high | P1-17 | |
| `break-layout-pages/table-closed` | adhoc | high | P3-10, P3-14 | |
| `break-layout-pages/alignment-flags` | adhoc | medium | P3-09 | |
| `break-layout-pages/vertical-spacing-constants` | adhoc | medium | P1-18, P3-01 | |
| `break-layout-pages/code-wrap-in-layout` | adhoc | medium | P3-11 | |
| `break-layout-pages/code-sidecar-three-box` | adhoc | medium | P3-11 | |
| `break-layout-pages/comment-role-by-color` | adhoc | low | P2-08 | |
| `break-layout-pages/paginator-in-serializer` | adhoc | high | P3-12 | |
| `break-layout-pages/group-role-dispatch` | adhoc | high | P1-18, P3-14 | |
| `break-layout-pages/measure-definition-split` | adhoc | low | P3-08 | |
| `break-layout-pages/kp-window-heuristics` | adhoc | low | P1-14 | |
| `break-layout-pages/overfull-collapses-paragraph` | issue | high | P0-12 | |
| `break-layout-pages/emit-reads-measure-stale-on-relayout` | issue | high | P1-16 | |
| `break-layout-pages/wide-float-overprints-text` | issue | medium | P3-08 | |
| `break-layout-pages/break-inf-float-vs-double` | issue | medium | P0-12 | |
| `break-layout-pages/trailing-glue-in-break-cost` | issue | medium | P0-12 | |
| `break-layout-pages/snap-kerning-ignores-sidecar` | issue | medium | P3-11 | |
| `break-layout-pages/float-adds-paragraph-gap` | issue | low | P3-08 | |
| `break-layout-pages/float-indent-geometry` | issue | low | P3-08 | |
| `break-layout-pages/paged-atoms-clipped` | issue | medium | P3-12 | |
| `break-layout-pages/break-cache-robustness` | issue | low | P0-11, P1-14 | |
| `break-layout-pages/api-hosts-layout-policy` | issue | medium | P1-03, P1-15 | |
| `break-layout-pages/baseline-not-communicated` | issue | low | P1-18 | |
| `break-layout-pages/doc-drift` | issue | low | P1-15, P3-12 | |
| `break-layout-pages/missed:0` | missed | medium | P1-18, P3-12 | |
| `break-layout-pages/missed:1` | missed | medium | P0-10 | |
| `break-layout-pages/missed:2` | missed | medium | P1-17, P3-07 | |
| `break-layout-pages/missed:3` | missed | medium | P0-12 | |
| `break-layout-pages/missed:4` | missed | medium | P3-28 | |
| `break-layout-pages/missed:5` | missed | low | P0-12 | |
| `render-runtime/semantic-role-switch` | adhoc | high | P3-23 | |
| `render-runtime/typeset-role-blind` | adhoc | high | P3-23 | |
| `render-runtime/linebox-special-dispatch` | adhoc | high | P1-18 | |
| `render-runtime/render-layout-decisions` | adhoc | high | P1-18 | |
| `render-runtime/paged-keep-rules-by-kind` | adhoc | medium | P3-12 | |
| `render-runtime/copy-syn-policy` | adhoc | medium | P3-07 | |
| `render-runtime/copy-line-separators` | adhoc | medium | P3-07 | |
| `render-runtime/shell-note-popups` | adhoc | high | P3-04, P3-06 | |
| `render-runtime/shell-chunk-byte-coupling` | adhoc | medium | P3-05 | |
| `render-runtime/anchor-namespace` | adhoc | medium | P3-04 | |
| `render-runtime/css-contract-monolith` | adhoc | medium | P3-18 | |
| `render-runtime/no-class-channel` | adhoc | high | P3-18 | |
| `render-runtime/token-theme-sniffing` | adhoc | medium | P3-18 | |
| `render-runtime/marker-gutter` | adhoc | low | P1-18, P3-16 | |
| `render-runtime/config-plumbing` | adhoc | medium | P1-03 | |
| `render-runtime/static-export-template` | adhoc | medium | P3-21, P3-36 | |
| `render-runtime/anchor-decode-duplication` | adhoc | medium | P3-05 | |
| `render-runtime/audit-hint-attributes` | adhoc | low | P3-07 | |
| `render-runtime/duplicate-serializer-primitives` | adhoc | low | P0-10, P1-02 | |
| `render-runtime/shell-feature-inventory` | adhoc | medium | P3-06 | |
| `render-runtime/snap-kerning-duplicate-style` | issue | high | P0-10 | |
| `render-runtime/semantic-footnote-ids-dangle` | issue | high | P0-09, P0-10 | |
| `render-runtime/copy-drops-blank-code-lines` | issue | medium | P3-07 | |
| `render-runtime/sidecar-hyphen-missing` | issue | medium | P1-17 | |
| `render-runtime/sidecar-dropped-in-semantic` | issue | medium | P3-23 | |
| `render-runtime/popup-breaks-patch` | issue | low | P3-06 | |
| `render-runtime/host-line-height-leak` | issue | medium | P3-19 | |
| `render-runtime/paged-gutter-clipping` | issue | medium | P3-16 | |
| `render-runtime/trailing-float-and-gap-drift` | issue | low | P3-16 | |
| `render-runtime/typeset-a11y` | issue | medium | P3-27 | |
| `render-runtime/error-render-divergence` | issue | low | P3-16 | |
| `render-runtime/hyphen-in-link-or-ref` | issue | low | P0-10 | |
| `render-runtime/normative-doc-drift` | issue | low | P3-07 | |
| `render-runtime/swap-whole-container` | issue | low | P3-05 | |
| `render-runtime/missed:0` | missed | high | P0-10, P4-01 | |
| `render-runtime/missed:1` | missed | medium | P1-18 | |
| `render-runtime/missed:2` | missed | medium | P1-02 | |
| `render-runtime/missed:3` | missed | medium | P4-03 | |
| `render-runtime/missed:4` | missed | low | P3-05 | |
| `render-runtime/missed:5` | missed | low | P3-07 | |
| `math/call-construct-string-dispatch` | adhoc | high | P1-24 | |
| `math/vocabulary-in-font-artifact` | adhoc | high | P1-22, P3-24 | |
| `math/lexer-hardcoded-alphabet` | adhoc | medium | P3-24 | |
| `math/negation-enumerated` | adhoc | medium | P3-24 | |
| `math/alphabet-variants` | adhoc | medium | P3-24 | |
| `math/implicit-names-op-class` | adhoc | medium | P3-24 | |
| `math/bigop-greedy-body` | adhoc | medium | P3-25 | |
| `math/segmentation-class-preview` | adhoc | medium | P3-25 | |
| `math/inline-math-special-block` | adhoc | medium | P1-13, P3-26 | |
| `math/display-math-unit` | adhoc | medium | P1-18, P3-26, P3-29 | |
| `math/equation-numbering` | adhoc | low | P2-07, P2-15 | |
| `math/math-island-oneoff-syntax` | adhoc | low | P2-06, P2-15 | |
| `math/math-opaque-string` | adhoc | high | P2-15 | |
| `math/closed-vocabulary` | adhoc | high | P2-15 | |
| `math/compiled-in-font` | adhoc | high | P1-23, P5-01 | |
| `math/fence-pairs-ascii-only` | adhoc | medium | P3-24 | |
| `math/math-leaves-bypass-style` | adhoc | medium | P1-25 | |
| `math/math-text-pull-channel` | adhoc | medium | P1-25 | |
| `math/math-span-lexer-triplication` | adhoc | medium | P2-11 | |
| `math/missing-glyph-fallback` | adhoc | low | P1-25, P5-01 | |
| `math/island-scan-escapes-block` | issue | high | P0-04 | |
| `math/prime-then-script-degrades` | issue | medium | P1-24 | |
| `math/bracket-shedding-any-group` | issue | medium | P3-24 | |
| `math/call-arity-silent` | issue | medium | P1-24 | |
| `math/exactness-gaps-paint` | issue | medium | P1-23 | |
| `math/spacing-edge-classes` | issue | low | P3-25 | |
| `math/diag-quality` | issue | low | P1-24 | |
| `math/dead-data-and-params` | issue | low | P1-22 | |
| `math/doc-drift` | issue | low | P1-22 | |
| `math/fallback-and-a11y` | issue | low | P3-26, P3-27 | |
| `math/toc-excerpt-drops-math` | issue | low | P2-15 | |
| `math/missed:0` | missed | high | P0-04, P2-11 | |
| `math/missed:1` | missed | high | P1-24 | |
| `math/missed:2` | missed | medium | P3-24 | |
| `math/missed:3` | missed | medium | P3-24 | |
| `math/missed:4` | missed | medium | P3-24 | |
| `math/missed:5` | missed | medium | P1-24 | |
| `api-measure-code/per-resource-pull-plumbing` | adhoc | high | P1-19 | |
| `api-measure-code/image-dims-in-author-args` | adhoc | high | P1-19 | |
| `api-measure-code/math-text-measure-side-channel` | adhoc | medium | P1-20, P1-25 | |
| `api-measure-code/config-plumbing-per-knob` | adhoc | high | P1-03 | |
| `api-measure-code/global-feature-knobs-no-cascade` | adhoc | high | P3-02 | |
| `api-measure-code/supplements-and-lang` | adhoc | medium | P1-10, P3-30 | |
| `api-measure-code/font-role-split` | adhoc | medium | P1-04 | |
| `api-measure-code/token-tag-table-copies` | adhoc | medium | P3-22 | |
| `api-measure-code/token-class-as-color-string` | adhoc | high | P2-08 | |
| `api-measure-code/language-registry-scattered` | adhoc | medium | P3-22 | |
| `api-measure-code/literate-cpp-special-case` | adhoc | low | P3-22 | |
| `api-measure-code/grid-is-codeblock-only` | adhoc | medium | P3-11 | |
| `api-measure-code/sidecar-api-layer-rewrite` | adhoc | high | P2-13 | |
| `api-measure-code/doc-typeset-hosts-layout-logic` | adhoc | medium | P1-15 | |
| `api-measure-code/adhoc-invalidation-flags` | adhoc | high | P1-03 | |
| `api-measure-code/adhoc-caches` | adhoc | high | P1-21 | |
| `api-measure-code/per-feature-api-entry-points` | adhoc | medium | P3-37 | |
| `api-measure-code/native-driver-config-divergence` | adhoc | medium | P1-03 | |
| `api-measure-code/resource-io-paths` | adhoc | medium | P3-21 | |
| `api-measure-code/magic-policy-constants` | adhoc | low | P1-03, P3-02 | |
| `api-measure-code/snap-kerning-duplicate-style-attr` | issue | high | P0-10, P1-03 | |
| `api-measure-code/relayout-stale-emit` | issue | high | P0-11, P1-16 | |
| `api-measure-code/image-w-only-overwritten` | issue | high | P0-11, P1-19 | |
| `api-measure-code/snap-ignores-sidecar-partition` | issue | medium | P1-03, P3-11 | |
| `api-measure-code/late-font-stale-measure-cache` | issue | medium | P0-11 | |
| `api-measure-code/main-dims-rpc-race` | issue | medium | P0-11 | |
| `api-measure-code/worker-no-per-doc-serialization` | issue | medium | P0-11 | |
| `api-measure-code/image-fetch-serial-and-decode` | issue | medium | P0-11 | |
| `api-measure-code/per-word-boundary-marshalling` | issue | medium | P1-19 | |
| `api-measure-code/kp-cache-unverified-hash` | issue | low | P0-11 | |
| `api-measure-code/diag-format-and-duplication` | issue | low | P0-11 | |
| `api-measure-code/nul-byte-in-worker-source` | issue | low | P0-11 | |
| `api-measure-code/unchecked-boundary-invariants` | issue | low | P1-19 | |
| `api-measure-code/doc-drift-api-subsystem` | issue | low | P3-37 | |
| `api-measure-code/missed:0` | missed | medium | P0-11 | |
| `api-measure-code/missed:1` | missed | medium | P1-19 | |
| `api-measure-code/missed:2` | missed | medium | P3-23 | |
| `api-measure-code/missed:3` | missed | medium | P1-04, P1-19 | |
| `api-measure-code/missed:4` | missed | low | P3-11 | |
| `real-world-evidence/role-figure-hardwired` | adhoc | high | P2-07 | |
| `real-world-evidence/counters-fixed-fields` | adhoc | high | P2-07 | |
| `real-world-evidence/open-arg-schema` | adhoc | high | P1-01, P2-05 | |
| `real-world-evidence/sugar-dispatch-fixed` | adhoc | high | P2-03 | |
| `real-world-evidence/no-tsm-printer` | adhoc | high | P3-35 | |
| `real-world-evidence/lexical-syntax-copies` | adhoc | medium | P1-09 | |
| `real-world-evidence/literate-cpp-hack` | adhoc | medium | P3-22 | |
| `real-world-evidence/ref-cite-format-in-cpp` | adhoc | medium | P2-09 | |
| `real-world-evidence/locale-terms-switch` | adhoc | medium | P1-10, P3-30 | |
| `real-world-evidence/codepoint-heuristics` | adhoc | low | P4-02 | |
| `real-world-evidence/sup-attach-private` | adhoc | medium | P2-08, P4-07 | |
| `real-world-evidence/description-list-missing` | adhoc | medium | P3-34 | |
| `real-world-evidence/table-model-v1` | adhoc | medium | P3-14 | |
| `real-world-evidence/figure-model-single-image` | adhoc | medium | P3-15 | |
| `real-world-evidence/parshape-prefix` | adhoc | medium | P3-08 | |
| `real-world-evidence/resource-and-config-plumbing` | adhoc | medium | P3-21 | |
| `real-world-evidence/notes-popups-dom-scraping` | adhoc | medium | P3-04, P3-06 | |
| `real-world-evidence/presentation-constants` | adhoc | medium | P3-01 | |
| `real-world-evidence/math-extensibility` | adhoc | medium | P3-29 | |
| `real-world-evidence/no-cross-document-labels` | adhoc | high | P3-31 | |
| `real-world-evidence/markup-reentry-missing` | adhoc | medium | P2-13 | |
| `real-world-evidence/ctor-name-collision-fatal` | issue | high | P0-05, P2-02 | |
| `real-world-evidence/heading-numbers-invisible` | issue | medium | P3-03 | |
| `real-world-evidence/labels-on-unsupported-nodes-silent` | issue | medium | P0-09 | |
| `real-world-evidence/span-loss-synthesized-nodes` | issue | medium | P2-04 | |
| `real-world-evidence/grouped-cite-all-or-nothing` | issue | low | P0-09 | |
| `real-world-evidence/spec-drift` | issue | medium | P3-37, P5-02 | |
| `real-world-evidence/converter-fidelity-unchecked` | issue | medium | P3-35 | |
| `real-world-evidence/math-leniency-silent` | issue | low | P3-24 | |
| `real-world-evidence/converter-code-duplication` | issue | low | P3-35 | |
| `real-world-evidence/missed:0` | missed | high | P0-05, P2-02 | |
| `real-world-evidence/missed:1` | missed | medium | P0-09 | |
| `real-world-evidence/missed:2` | missed | medium | P2-06 | |
| `real-world-evidence/missed:3` | missed | medium | P0-06, P2-08 | |
| `real-world-evidence/missed:4` | missed | medium | P3-27 | |
| `real-world-evidence/missed:5` | missed | low | P3-23 | |
