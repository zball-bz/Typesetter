# Audit findings (2026-10-05)

Generated from the two-round audit (survey + adversarial verification). Each finding lists its plan step(s); see PLAN.md for the steps and TRACEABILITY.md for the step index. Verdict "accurate/partly" is the verifier's factual check; "verifier-found" items were discovered by the verifier.

Line numbers refer to commit ecc3a89 (main, 2026-09-01).


## markup-language — Markup language surface design (.tsm)

<details><summary>Design summary (as audited)</summary>

.tsm is parsed in two hand-written C++ phases. linepass (engine/src/linepass/linepass.cc) is a prefix-driven container automaton: headings `=`..`======` with an optional trailing ` <id>`, lists `-`/`+`/`N.` with content columns and no lazy continuation, `>` quotes, `---`, backtick fences of N>=3 with an info string `tag(args)`, line-start `%--…--%` comments, `#let …` and `#{…}` code statements, and name-matched `#!name(args) … #name!` regions. Its output is a skeleton of blocks, each holding a list of line spans. The inline parser (engine/src/inline/inline.cc) runs over each block's span list. It handles backslash escapes, `%--` comments, `$…$` math with a Typst-style display rule and an optional ` <id>`, single-backtick code spans, `[t](u)` links, strict-pair `*`/`_`, `#` splices (an ASCII head chain plus `(…)` and directly adjacent `[…]` content arguments, or `#(expr)`), `^[…]` footnotes, and `@id`/`@[…]` refs. Inside regions, every source line is pre-split at top-level `|` into Row/Cell AST nodes (splitCells).

codegen (engine/src/codegen/codegen.cc) lowers the AST to one async JS module. Each top-level block becomes `__emit(__at(ctor(…), s, e))`. Splices become `val(expr)`, content arguments are appended as trailing call arguments, fences become `await __fence(tag, ({args}), body, offset)`, and regions become `__region(name, ({args}), [rows-of-cells | block values])`. The constructor surface is a fixed destructuring list (codegen.cc:209-212) bound in runtime/src/worker/executor.mjs. The executor writes ops (MAKE_NODE/EMIT/STYLE_*). It also holds the built-in builders: table, figure, generic `group{role}`, bibliography loading, and the style-arg wrapping of regions. Extension points are `$.fence(tag, fn)`, `$.region(name, fn)`, `$.style.*` and `$.bib.format`. References are write-only during execution (§11.1). The C++ resolver numbers headings, tables, `group{role:"figure"}`, labelled display math and footnotes, then resolves `ref` against labels and then bibliography keys, and expands the four collectors.

Verdict: the stated rule, 'every syntax form is sugar for a constructor' (v2 §4 l.107), holds for the simple forms. It does not hold at the edges:
- The table and figure regions have no constructor, and neither do group, raw or table nodes.
- The documented equivalence `= 标题 ⇔ heading(1)[…]` does not compile correctly.
- Keyword forms, content literals, multi-line content blocks and `m` markup re-entry are specified but not implemented.

The set of sugars is closed, which is a deliberate and documented choice. The deeper problem is that many generic mechanisms carry one feature's convention: table `|` splitting in every region, the figure role in numbering, footnote/bib/heading auto-labels in the user namespace, and per-kind label and argument grammars.


Strengths:

- Two-phase parsing with a hand-written line pass and no lazy continuation: parsing is deterministic and errors are block-granular (v2 §4). Moving structure into prefix lines while keeping inline grammar context-free is the right call for a CJK-first language.
- Code mode is standard, unmodified JavaScript compiled into one async module (v2 §2). Scope, closures, async imports and tooling come for free, and the ops DAG/schedule split cleanly encodes emission-time style binding (document-model §3/§4).
- The backslash escape is universal: any byte after `\` is literal (inline.cc:235-239). Line-level escapes (`\=`, `\-`, `\#!`, `\%--`) work because of this, not because of per-form rules. Probe mk4.tsm confirmed every listed escape.
- The CJK-aware decisions are principled and consistent with each other. Strict-pair emphasis (no flanking rules) makes `**《书名》**` parse. Bare splice identifiers are ASCII-only, with `#(…)` for CJK. Bare `@` refs are ASCII, with `@[…]` for CJK labels. Soft line breaks between CJK characters join without a space. `#avg的结果` correctly cuts at `avg` (fixture splice/ascii-cut.tsm).
- Comments use distinct open/close delimiters, nest, and are kept as content nodes rather than discarded by the lexer (v2 §4.2). Tooling can query them, and nested commenting-out works.
- Regions use name-matched closers and keep a markup interior. The v2 §4.1 rationale (the 'registration paradox', and splitting at the tree level being robust against embedded markup) is sound; the implementation deviated from it, see adhoc `region-pipe-segmentation`.
- Fence handlers are a genuinely open extension surface: runtime dispatch, an unknown tag falls back to a code block, async is allowed, and a handler exception becomes an error node (executor.mjs:142-161) — the one place where execution errors are contained.
- 'Execution declares, the resolver decides' (§11.1). Refs are opaque during execution and resolved before any serializer, so the semantic/static HTML already has final numbers. Footnotes and citations reuse the label table and collector machinery instead of adding new pipelines (notes-design §3).
- Every stage can be inspected with `tsrc --stage=…`. This made it cheap to confirm every claim in this review.

</details>


### `markup-language/region-pipe-segmentation` — Table '|' segmentation is baked into the generic region mechanism, and it is byte-level, so it corrupts content

- kind: adhoc · severity: high · verdict: accurate · plan: **P0-04, P2-11**
- locations: `engine/src/inline/inline.cc:454-508`; `engine/src/inline/inline.cc:601-627`; `engine/src/codegen/codegen.cc:157-193`; `runtime/src/worker/executor.mjs:94-113`; `runtime/src/worker/executor.mjs:116-121`; `examples/real-world/hott-introduction.tsm:158`; `(zball-io) src/docs/example-hott.tsm:164`

Every paragraph inside ANY region is split by splitCells at unescaped top-level `|` into Row/Cell AST nodes, and codegen passes nested arrays of rows of cells to `__region`. Non-table builders (`regionJoin`) glue the pieces back together with ' | ' between cells and ' ' between source lines. splitCells treats only backtick code spans and splices as opaque, not `$…$`, links, `^[…]` or `%--`. Verified results:
- `#!aside` containing `绝对值 $|x|$ 的性质` renders as `绝对值 [|x|] | x | $ 的性质`.
- A figure caption `[a|b](http://x)` becomes literal text `[a | b](http://x)`.
- In the published HoTT table row `$(x,x) | x ∈ A$ | path space $A^I$`, the math splits, a garbage formula `| path space` appears, and the real last cell is silently dropped by `r.slice(0, cols)`.
- Multi-line CJK region paragraphs gain a spurious space (`第一行中文 第二行中文`).
- Region paragraphs and trow/tcell nodes lose their spans (@[0,0)).
The tex2tsm workaround (commit d454e24: `\mid` mapped to U+2223 'because | splits table cells') patches the symptom in a converter instead of fixing the cause.

*Why ad hoc:* v2 §4.1 (l.145) says '`|`-splitting is the **table constructor's** convention, not a region mechanism' and promises a 'small query API' over provenance, with cells split 'at tree level' so that splices and code 'can never be mis-split'. The implementation moved one constructor's convention into the parser and codegen, which are generic layers, and replaced tree-level splitting with a byte scanner that has its own opacity list. Every region pays for the table's rule.

*Proposed generalization (survey):* Make line provenance a property of ordinary content, and make splitting a library function over shadows.
(1) The inline parser emits a `softbreak` inline node at every source-line join instead of choosing ' ' or '' itself. This is Pandoc's SoftBreak: a new KIND, or the reserved `hardbreak` slot's sibling, with emit deciding space/no-space (see `cjk-softbreak-classifier`).
(2) The inline parser emits every unescaped `|` as its own single-character text node flagged `sep` (an ArgK bit). `\|` stays ordinary text. Lossless: a non-table consumer sees exactly the source text.
(3) codegen lowers a region like any container: `__region(name, args, [para(…), list(…), …])` with real spans. The Row/Cell AST kinds and splitCells are deleted.
(4) The executor stdlib exports `prov.rows(para, {sep: '|'})`. It splits a para's child shadows at `softbreak` (rows) and at `sep` text nodes (cells). Code, math, link, note and splice shadows are atoms by construction, which is the v2 embedding-proof argument actually realised. The table builder and user handlers both call it. Indentation-based row continuation stays a block child appended to the last row, as now.

*Verifier:* I reproduced all five symptoms with the shipped binary and executor.
- `#!aside` containing `$|x|$` gives seq(text,mathinline('|x|')),' | ','x',' | ','$ 的性质'.
- The HoTT row gives a 5th cell mathinline('| path space'), and the 6th cell is dropped by `r.slice(0, cols)` (executor.mjs:88).
- CJK lines are joined with ' '.
- Spans are @[0,0).
The as-built design is documented, which the report omits. document-model.md:185-190 says 'Region provenance is materialized at codegen… Non-tabular regions rejoin the segmentation (" | ")', and architecture.md:222 says 'codegen-materialized | segmentation provenance'. It contradicts v2 §4.1 l.120/145 but is not undocumented.
The report misses one consequence. The Row/Cell path (codegen.cc:166-185) bypasses the Para→mathblock promotion (codegen.cc:97-107). So display math inside any region becomes mathinline and loses its label (see missed).
The cell-math overrun is caused by contiguous() (inline.cc:68-72): a single-span cell always passes.

*Verifier notes:* The direction is right and is closer to v2 §4.1 l.120 ('cells split on literal | text nodes') than the as-built design. Fixes needed:
(a) A 'text node flagged sep (an ArgK bit)' has nothing to carry the flag: MAKE_TEXT takes no args (opbuf.mjs makeText; document-model §4.3). Make `sep` a JS-shadow-only property instead: codegen emits `__sep()` = makeText('|') with `shadow.sep=true`. This needs no ops change, and non-table consumers see a plain '|'.
(b) Resolve softbreak at instantiation, not in emit: merge it into adjacent text as ' ' or '' using the App C classifier plus the run's resolved lang. Emit never applies App C classes across node edges (notes-design §1 as-built: 'cross-node boundaries were never inserted'). Leaving softbreak nodes for emit would therefore split every multi-line paragraph's text and churn every golden, which contradicts the report's 'byte-identical' estimate. With instantiation-time merging, non-region tree/blocks goldens stay identical; only the .ops bytes change (OPS_VERSION bump for the softbreak kind).
With (a) and (b), step (3) (regions lowered through the ordinary value() path) also fixes the missed mathblock and caption bugs.

### `markup-language/inline-delimiter-scanners` — Delimited forms are scanned by separate, divergent scanners, and islands are not carved out before line structure

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-08**
- locations: `engine/src/inline/inline.cc:68-72`; `engine/src/inline/inline.cc:114-142`; `engine/src/inline/inline.cc:240-265`; `engine/src/inline/inline.cc:266-313`; `engine/src/inline/inline.cc:314-330`; `engine/src/inline/inline.cc:384-403`; `engine/src/inline/inline.cc:404-432`; `engine/src/inline/inline.cc:454-508`; `engine/src/linepass/linepass.cc:253-261`; `docs/design-decisions-v2.md:151`; `docs/design-decisions-v2.md:158`; `(zball-io) src/docs/syntax.tsm:37`

Each bracketed or island form has its own scanner with its own rules:
- `matchBracket`: honours escapes, nests, single line.
- Footnote `^[`: nests, ignores escapes and code spans (`^[含 `]` 代码]` closes inside the code span; `^[含 \] 转义]` closes at the escaped bracket).
- `@[`: no nesting, no escapes.
- Code spans: a single backtick only, so the blog's own `` `` `code` `` `` renders as two empty code spans around `code`.
- Math: may cross lines only when `contiguous()` holds, so an indented continuation line of a multi-line display formula turns it into literal text, inside list items and at top level alike.
- Inline `%--`: scans to end of file regardless of block structure. `text %-- …\n\n= Hidden\n\n… --%` both comments out AND renders the heading and paragraph.
- splitCells: yet another opacity list.
Footnotes, links and content arguments cannot span a soft line wrap; they degrade to literal text, while `*…*` can. The linepass knows only fences and line-start comments, so `$` islands and mid-line comments cannot protect line starters. v2 §4.2/§5 state the precedence 'verbatim islands > comments > splices/markup' and 'carved out before everything else'.

*Why ad hoc:* Every form re-implements delimiter matching, so each new form (footnotes, cites) brought its own policy. The documented precedence is not implemented as a phase; it emerges, inconsistently, from per-scanner choices and from `contiguous()`.

*Proposed generalization (survey):* Add a table-driven phase 0, `IslandLexer`, in engine/src/source/ that runs before the linepass:
- Rule shape: `struct IslandRule {open, close, kind: Fence|Comment|Math|Code, nest, escapes, crossLines: Never|WithinBlock|Anywhere, lineAnchored, closeLen: SameAsOpen}`.
- Rules: fence (line-anchored, N>=3 backticks), comment (nesting `%--`/`--%`), math (`$` with `\$`, WithinBlock), code span (Markdown variable-length backticks).
- Output: a sorted `IslandMap`.
The linepass treats an island's interior lines as owned by the line where it opens: no starter recognition inside, and container prefixes are stripped to the opener's content column, the rule fences already use. The inline parser consumes islands as atoms. One `scanDelimited(pos, open, close, policy)` serves `[…]`, `^[…]`, `@[…]`, `#f[…]` and `(url)`; it skips islands and escapes and walks the block's whole span list, so a soft wrap is just whitespace. Generate the editor grammars from the same rule table (see `surface-grammar-drift`).

*Verifier:* Every per-scanner divergence reproduces in --stage=ast:
- ^[ ignores code spans and escapes.
- `` `` `code` `` `` gives two empty code spans.
- `@sec:intro` gives ref 'sec' plus text ':intro'.
- An indented multi-line `$` inside a list item becomes literal.
The blog example is at zball-io src/docs/syntax.tsm:38, not :37.
The overrun is worse than described: the swallowed source is ALSO parsed by its own blocks, so content is duplicated. Example: `para one $ x\n\nnext para $ b…` yields math src 'x\n\nnext para' and the next paragraph again. `text %-- …\n\n= Hidden…--%` yields a comment containing '= Hidden' AND a heading 'Hidden'.

*Verifier notes:* A context-free 'phase 0 before the linepass' is unsound for the `$` and `%--` rules.
- The proposed WithinBlock policy needs block boundaries (blank lines, container exits), which only the linepass computes.
- A raw byte scan cannot strip `> ` or item indentation inside an island.
Use the same rule table INSIDE the linepass loop, the way fence() already works (linepass.cc:163-200). When a leaf line opens an unclosed island, the linepass enters island mode and continues through matchPrefixes, which dedents in a container-aware way. A WithinBlock island ends at a blank line or container exit, with a diagnostic.
These parts are sound:
- the shared scanDelimited over the block's span list for [..]/^[..]/@[..]/#f[..]/(url);
- Markdown-style variable-length code spans;
- generating the editor tables from it.
Bounding islands at the block also defuses `$5 … $10` across paragraphs.

### `markup-language/closed-constructor-set` — The constructor surface is a hard-coded list; several kinds produced by sugar or built-ins have no user-callable constructor

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-03**
- locations: `engine/src/codegen/codegen.cc:209-212`; `runtime/src/worker/executor.mjs:162-237`; `runtime/src/shared/opbuf.mjs:53-57`; `docs/design-decisions-v2.md:107`; `docs/document-model.md:66`

codegen destructures a fixed list of 31 names (`para, text, em, …, image, __region, __fence`). `group`, `table`/`trow`/`tcell`, `raw` (available only as `ctx.raw` inside fence handlers), `hardbreak`, `error`, a generic `collect`, and a `figure` constructor do not exist. KIND and makeNode are not exposed, and opbuf throws on any arg key not in ops.def. document-model §2.1 says 'custom constructs are built from `group`/`styled`/`raw`', but user code can call neither `group` nor `raw`. The only route to a role-tagged, labelled group from user code is calling the private `__region(name, …)` dispatcher.

*Why ad hoc:* It violates the governing principle (v2 l.107: 'guarantees every syntactic capability has a programmable equivalent'). Built-in builders use private capabilities (`ob.makeNode(KIND.group/table…)`) that user extensions cannot reach, so extensions are not on equal footing with built-ins.

*Proposed generalization (survey):* Generate the standard library from ops.def. Extend the X-macro to `KIND(name, id, LEVEL, CTOR_SIG)` and have gen-ops-ts emit `stdlib.gen.mjs`: one constructor per kind plus a generic `node(kind, opts, ...kids)`. codegen's destructuring list is generated from the same table, so it cannot drift, and `#use` exports join the same scope. Add one open-ended arg, `ArgK::attrs` (a JSON-string map, rendered as `data-*` in semantic HTML and opaque to layout), so user-defined roles can carry parameters without an OPS_VERSION bump per feature knob. Expose `raw(html, {width, height})` globally rather than only on fence ctx.

*Verifier:* The list has 31 names (codegen.cc:209-212). `group`, `table`, `trow`, `tcell`, `raw`, `hardbreak`, `error` and `collect` have no constructor. opbuf throws on an unknown arg key (opbuf.mjs:56). `__region` is the only path to a labelled group{role}.

*Verifier notes:* Generating constructors from ops.def is sound and keeps layout closed under the kind table (document-model §2.1 l.66).
One piece is missing. Once user code can build any kind via `node(kind,…)`, the structural guarantees that the builders give today implicitly must move into ingest validation: trow only under table, tcell only under trow, mathblock childless, collect params. document-model §4.3 l.118 validates ids and kinds only, not the parent/child kind grammar. Add a CHILDREN column to the KIND X-macro and turn violations into error nodes at ingest, keeping containment block-granular.
An open `attrs` map should be declared opaque to emit/layout/resolver. Otherwise it becomes a back door for per-feature knobs.

### `markup-language/ctor-signature-vs-content-args` — Positional constructor signatures break the `[…]` content-argument sugar; the documented equivalence `= 标题 ⇔ heading(1)[…]` is false

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-03**
- locations: `runtime/src/worker/executor.mjs:171-172`; `runtime/src/worker/executor.mjs:190-191`; `runtime/src/worker/executor.mjs:219-220`; `engine/src/codegen/codegen.cc:67-94`; `engine/src/codegen/codegen.cc:118-124`; `docs/design-decisions-v2.md:107`

Content arguments desugar to trailing positional arguments (`f(a)[c]` becomes `f(a, c)`), but the constructors put options positionally: `heading(level, label, ...kids)`, `list(ordered, start, ...items)`, `mathblock(src, label)`. Verified with probe mk18.tsm:
- `#heading(2)[Title via ctor]` produces an EMPTY heading. The content shadow lands in the label slot and the resolver then assigns the auto label `h-0.1`.
- `#list(false)[a]` produces `start=%5` (a node id stored as the start number).
Each constructor has its own idiosyncratic order (codeblock(lang, body, opts), image(src, opts), bibliography(src, opts), link(url, ...kids)).

*Why ad hoc:* Each constructor's signature was chosen per feature for codegen's convenience. No calling convention is shared by sugar, explicit calls and content arguments, so 'sugar is a constructor' cannot be tested mechanically.

*Proposed generalization (survey):* One calling convention for every constructor: `ctor(opts?, ...content)`, where `opts` is recognised as a plain object without `opId`. `label`, `role`, `style` and `attrs` are universal options. codegen lowers every sugar to exactly that form, e.g. `heading({level: 1, label: 'x'}, …)` and `list({ordered: true, start: 3}, item(…))`. `#heading(level: 1)[…]` (with the named-argument sugar from `arg-grammar-unification`) then compiles to byte-identical JS as `= …`. Add a golden test asserting that `--stage=js` of each sugar equals that of its explicit call; the governing principle becomes an executable invariant.

*Verifier:* Verified:
- `#heading(2)[T]` gives an empty heading with label 'h-0.1'.
- `#list(false)[a]` gives start=%<id of the text node> (%3 in my probe; the id varies, it is not always %5).

*Verifier notes:* A uniform `ctor(opts?, ...content)` forces `link({url})`, `image({src})` and `ref({target})`, which is needlessly verbose for high-frequency splices. The invariant actually needed is narrower: no option slot may be reachable by a trailing content argument. Typst-style signatures satisfy it:
- required positional params first;
- then an optional plain-object options slot, detected by `!(x && 'opId' in x)`;
- then content.
That keeps `heading(2)[T]` and `link(url)[t]`.
The proposed 'byte-identical --stage=js' test cannot hold. Sugar lowers to `__at(heading(…),s,e)`, while `#heading(…)[…]` lowers to `para(val(heading(…)))` and relies on the resolver unwrap (resolve.cc:490-492), with different spans. Assert equality at --stage=tree modulo spans instead.

### `markup-language/arg-grammar-unification` — Four or more argument and label grammars: object-literal region/fence args, positional splice args, two label charsets, a ref charset and a comma micro-syntax

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-06**
- locations: `engine/src/codegen/codegen.cc:148-152`; `engine/src/codegen/codegen.cc:158-161`; `engine/src/inline/inline.cc:566-592`; `engine/src/linepass/linepass.cc:272-283`; `engine/src/inline/inline.cc:292-306`; `engine/src/inline/inline.cc:410-418`; `engine/src/resolve/resolve.cc:244-252`; `runtime/src/worker/executor.mjs:214-217`; `runtime/src/worker/executor.mjs:116-121`

The forms that take arguments each follow a different grammar:
- Region and fence args are pasted into `({…})`, so they must be an object-literal body. `#!table(3)` becomes `({3})`, a SyntaxError that aborts the whole document (verified).
- Splice calls take positional JS arguments.
- A fence info string's tag is everything before `(`, so ```` ```dot rankdir=LR ```` dispatches the tag `dot rankdir=LR`.
- Heading labels may not contain spaces or `<>`; math labels allow spaces (`<eq a>` accepted).
- Bare refs allow `[A-Za-z0-9_$-]`, so `= T <sec:intro>` is a valid label but `@sec:intro` resolves `sec` and renders `??:intro` (verified).
- Cite groups are a comma string re-split by the resolver.
- Image options are positional `src` in `#image` but named `src:` in `#!figure`, and `float` vs `side` are aliases.

*Why ad hoc:* Each construct picked whatever was locally convenient (pasting an object body, reusing the JS call). v2 §4.1 says the info string 'reuses the splice `(…)` argument lexer', but only the lexer is shared, not the grammar or the lowering.

*Proposed generalization (survey):* One `ArgList` production, lexed by the existing scanJs. Top-level commas separate arguments. An argument that begins with `IDENT ':'` is named and is collected into a trailing options object; anything else is positional. A JS ternary or object literal cannot start an argument with `IDENT:`, so the rule is unambiguous without a JS parser. It applies identically to `#f(…)`, `#!name(…)` and ```` ```tag(…) ````, all lowered to `f(...positional, {named})` (`__region(name, pos, named, body)`). A fence info string without parentheses splits at the first whitespace: the tag is the first word and the rest goes to `ctx.info`.

One label grammar: `LabelChar = [^\s<>@\[\]]` everywhere `<…>` appears. Bare `@` accepts `[A-Za-z0-9_.:-]` minus trailing `.`/`:`/`-` (sentence punctuation), and `@[…]` accepts any label. Ref groups are parsed in codegen into a target array (see `reference-forms-closed`).

*Verifier:* Verified:
- `#!table(3)` gives `({3})` and a SyntaxError that kills the document.
- `<eq a>` is accepted, and its anchor is the invalid `#tsr-eq a`.
- The bare-ref charset is isIdentCont+'-' = [A-Za-z0-9_$-] (jslex.h:90-93, inline.cc:416).

*Verifier notes:* The `IDENT ':'` named-argument rule is unambiguous in a JS argument position. Applying it inside `#f(…)` would break a documented invariant: v2 §3 rule 1 and App A l.332 say that inside `(…)` the content is plain JS, and v2 §2 calls code mode unmodified JS.
Either amend that invariant explicitly, or confine the rewrite to `#!name(…)` openers and fence info strings. Those are already non-JS object bodies; lower them as `__region(name, positional[], named{}, body)`, and let constructors take a plain options object from ordinary JS.
Two object-body features would change meaning: shorthand `{src}` and spread `{...o}`. Neither appears in the corpus; every opener in test/, examples/ and zball-io uses `k: v`.

### `markup-language/numbered-env-hardcoding` — Counters, supplements, caption prefixes and ref text are hard-coded per kind and role; users cannot define numbered environments

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-07**
- locations: `engine/src/resolve/resolve.cc:62`; `engine/src/resolve/resolve.cc:158-195`; `engine/src/resolve/resolve.cc:283-292`; `engine/src/api/config.h:66-71`; `engine/src/api/config.h:89-102`; `engine/src/api/wasm_api.cc:39`; `engine/src/api/wasm_api.cc:62-63`; `engine/src/emit/emit.cc:776-786`; `engine/src/render/semantic_html.cc:285-306`; `docs/document-model.md:64`

The resolver keeps `tableNo, figNo, eqNo` (and noteNo). It numbers a `group` only if `role == "figure"`, inserts the caption prefix `supFigure + n + capSep` only for figures, and formats refs through a per-kind switch (`supHeading`, `supTable`, `supFigure`, `supEquation`+'(n)'). Supplements are switchable only between zh/ja and everything-else (`applyLang`). `tsr_doc_new()` takes no config, despite document-model §11. The `counters.resetAt` and `noteMarks` options described in docs are not implemented. Consequences:
- `#!theorem(label: "t1")` produces an un-numbered group, and `@t1` renders `??`. The same is verified for `#!aside(label: "as1")`, although document-model §2.1 lists `group` as labelable.
- Tables have no caption mechanism. The HoTT converter's `_caption_ <tab-pov>` is literal text and `@tab-pov` renders `??`.
- Nesting a `#!table` in a `#!figure` advances both counters.
The 'figure is a convention' choice (document-model §2.1 l.64) is documented, but the convention is keyed by a magic role string in three layers.

*Why ad hoc:* Each numbered thing was added as its own counter and its own config string. The role 'figure' is an implicit protocol between the resolver, emit and both renderers. There is no LaTeX `\newtheorem` or Typst `figure(kind:)` equivalent.

*Proposed generalization (survey):* A declarative `EnvSpec` table owned by the resolver:
`{select: {kind, role?}, counter: string (envs may share one), resetAt: 0|headingLevel, numbering: '1'|'1.1'|'i'|'a'|'①'|'(1)', supplement: {lang→string}, refFormat: '{supplement}{number}', captionFormat: '{supplement}{number}{sep}', numberUnlabelled: bool, captionSlot: 'firstPara'|'role:caption'}`.
Defaults reproduce heading, figure, table, equation and footnote. Sources of entries:
- Config JSON (make `tsr_doc_new(config_json)` real).
- Markup: `$.env('theorem', {...})` writes a top-level DECLARE op (or an `envdecl` meta node) that the resolver reads before pass 1.
The declaration is write-only data, so §11.1's 'scripts cannot read resolved values' still holds. Pass 1 becomes one loop: look up the spec, bump the counter (with reset), register the label, set `ArgK::name` to the formatted number, and insert the caption prefix into the caption slot. resolveRef applies `spec.refFormat`. Emit and render dispatch on a `caption` slot/role and an `env` attribute, not on 'figure'.

*Verifier:* Verified:
- `#!theorem(label:"t1")` / `#!aside(label:"as1")` give `@t1`/`@as1` = '??'.
- `tsr_doc_new()` takes no args (wasm_api.cc:39).
- `resetAt` and `noteMarks` appear only in docs (document-model.md:322, notes-design.md:73).
The report omits that the spec already promises the general mechanism: document-model §5 l.124 lists counter classes 'figure | equation | footnote | <user>' with reset rules from config. The double count in a figure-wrapped table is part of documented usage: figure-design §1 says '#!figure keeps working with no src (a figure whose body is a table/code/math block)'.

*Verifier notes:* Consistent with §11.1, since DECLARE is write-only data, and the resolver stays a pure function of (tree, config) (document-model §5 l.122). Make the caption slot explicit: a role:'caption' child produced by the builder, rather than 'firstPara'. The firstPara rule is exactly what currently writes '图 1：' into a display formula inside a figure (see missed). Number formats must be C++ templates for determinism, as the report says.

### `markup-language/universal-labels` — Labels are per-kind arguments with per-kind sugar; most blocks and all inline spans cannot be labelled

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-06**
- locations: `engine/src/linepass/linepass.cc:272-283`; `engine/src/inline/inline.cc:292-306`; `engine/src/codegen/codegen.cc:95-117`; `engine/src/resolve/resolve.cc:132-227`; `runtime/src/worker/executor.mjs:123-139`; `docs/design-decisions-v2.md:239`; `(zball-io) src/docs/syntax.tsm:40`

Trailing ` <id>` is recognised in exactly two places by two different routines: heading lines (linepass) and immediately after a closing `$` (inline). The math label is silently dropped unless the formula is the paragraph's sole child. Verified: `- 列表项 $ y = 2 $ <eq-in-list>` loses `eq-in-list` with no diagnostic. Regions use `label:` args. Terms auto-label with their (flattened) name. Paragraphs, list items, table rows, quotes, fences and inline spans cannot be labelled. `段落尾部标签 <para-l>` is literal text. The blog's syntax page claims 图/表 accept `<标签>`, which they do not. A heading like `= vector <T>` silently takes `T` as its label.

*Why ad hoc:* Labels were added wherever a feature needed them, each with its own scanner, charset and lowering. The resolver registers labels only for the kinds it numbers.

*Proposed generalization (survey):* Labels become a universal node attribute with one surface syntax and one lowering:
- Block level: a trailing ` <id>` at the end of the LAST line of any leaf block, including a region opener line (`#!figure(src: …) <fig-a>`) and a fence opener line. It is recognised by one linepass helper, `takeTrailingLabel(lineSpan)`, with the single LabelChar class.
- Inline level: `#label('id')[…]` or Djot-style `[…]{#id}`.
Both lower to the universal `label` option (see `ctor-signature-vs-content-args`), i.e. ArgK::label on any node. The resolver registers EVERY labelled node, with its EnvSpec if any. Unnumbered targets resolve through `refFormat` defaulting to the excerpt text. Emit anchors any labelled node, as it already does for groups. A label that cannot attach (e.g. inline display math) emits a `label-dropped` diagnostic.

*Verifier:* Verified:
- `- 列表项 $ y = 2 $ <eq-in-list>`: the AST holds the label, and codegen drops it (the para has 2 kids, so mathinline).
- `段落尾部标签 <para-l>` is literal text.
- `= vector <T>` takes label T.
The blog claim is at zball-io src/docs/syntax.tsm:44, not :40. Figures and tables DO take labels via `label:` (e.g. `#!table(cols: 3, align: "lrr", label: "tbl-bench")`); only the `<标签>` syntax is wrong.
v2 §11.1 l.239 documents 'trailing <id> on block forms' in general; only headings and post-`$` math implement it.

*Verifier notes:* The direction is sound, with three changes:
(a) It does NOT subsume the HoTT `<tab-pov>` case. A trailing label on the caption paragraph that follows `#table!` labels that paragraph, so `@tab-pov` would render its excerpt, not 表 n. Captions need the explicit caption slot (numbered-env-hardcoding), with the label on the table or figure.
(b) Recognise the label on the inline result, not on raw bytes in the linepass: a final Text node ending in ` <id>`. The raw-byte check would have to honour `\<` and know island boundaries, which only the inline parser does.
(c) The single LabelChar class must exclude whitespace; math labels with spaces already yield invalid anchors.

### `markup-language/label-namespace-collision` — Auto-labels (h-n, fn-n, fnref-n, bib-key, term names) share the user label namespace

- kind: adhoc · severity: medium · verdict: accurate · plan: **P0-09, P3-04**
- locations: `engine/src/resolve/resolve.cc:149-155`; `engine/src/resolve/resolve.cc:205-215`; `engine/src/resolve/resolve.cc:216-227`; `engine/src/resolve/resolve.cc:271-281`; `engine/src/resolve/resolve.cc:373-375`; `engine/src/resolve/resolve.cc:397-405`

The label table is a flat `string → Entry` map. Footnotes register `fn-<n>` and `fnref-<n>`, headings without a label get `h-<n>`, bibliography paragraphs anchor `bib-<key>`, and terms register their own name. A user label wins on collision, first come. Verified (probe mk21.tsm): with `= Intro <fn-1>`, the first footnote marker renders `§1` and links to the heading. With `#term[h-1][…]`, `@h-1` renders the term name. Two DOM elements get `id=tsr-fn-1`. Citations fall back to bib keys only after labels, so a heading label equal to a cite key silently captures the citation.

*Why ad hoc:* Each feature minted its reserved prefixes in the shared namespace instead of the label table having namespaces.

*Proposed generalization (survey):* Key the `LabelTable` by `(ns, id)`, with ns ∈ {user, heading-auto, note, note-ref, bib, term}. `@x` looks up user, then term, then bib, in a documented order. Internal namespaces are reachable only through explicit syntax (`@fn:1` if wanted) or not at all. DOM anchors derive from the namespace (`tsr-u-…`, `tsr-fn-…`), so ids cannot clash. A user label matching a reserved pattern, or ambiguous across user and bib, emits a diagnostic.

*Verifier:* Verified:
- `= Intro <fn-1>` makes the footnote marker render '§1'.
- `#term[h-9]` captures `@h-9`.
The duplicate `id="tsr-fn-1"` appears twice in typeset HTML (--stage=html) but only once in semantic HTML. The `bib-<key>` label is set at resolve.cc:421, not 397-405.
The shared namespace is documented: v2 §11.1 l.241 ('@key shares the reference namespace'), notes-design §1 Pipeline ('Notes are also entries in the label table so @fn-3 works like any reference'), and notes-design §2 As built (labels first, then bib).

*Verifier notes:* Sharing is a documented feature. What is accidental is the missing reservation and diagnostics. Making the internal namespaces 'reachable only through explicit syntax or not at all' breaks the documented `@fn-n`, and full namespacing churns the anchors of published pages.
A cheaper fix is enough. Keep one table but reserve the auto-label shapes (`h-<num>`, `fn-<n>`, `fnref-<n>`, `bib-*`):
- a user label that matches one gets a `label-reserved` diagnostic and a rename;
- a user label equal to a bib key gets `ref-ambiguous`;
- the heading fallback (resolve.cc:150-154) stops silently re-using an id whose registration failed, which is what produces duplicate anchors today.

### `markup-language/region-builtin-privilege` — Region dispatcher hard-codes `table` and `figure` by name, sniffs style keys on every region, and gives region handlers a weaker contract than fence handlers

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-03**
- locations: `runtime/src/worker/executor.mjs:76-139`; `runtime/src/worker/executor.mjs:142-161`; `runtime/src/worker/executor.mjs:267-269`; `(zball-io) src/posts/vscode-tsm.tsm:65`

`__region` checks: user handler, else `name === 'table'`, else `name === 'figure'`, else `group{role: name, label}`. It then wraps ANY region's result in `styled{font, lang, color, sizePx}` if those four arg names are present. `bold`/`italic` are ignored, and a user `#!callout(color: …)` handler gets its own `color` re-applied with no way to opt out. Region handlers receive `(args, childrenArrays)` without ctx: no `raw`, `error`, `offset`, `m` or span. Fence handlers get `(body, ctx)`. The built-in builders use private helpers (`regionJoin`, `tableBuild`, `figureBuild`) that a user handler cannot call to wrap or extend the default. The blog post (vscode-tsm.tsm:65) treats a `#!plot` builder as something the engine must ship.

*Why ad hoc:* Built-ins live as if-branches in the dispatcher rather than as registered handlers, and one feature's knobs (region-level style) are hard-wired into the generic path.

*Proposed generalization (survey):* Use one `Registry<Handler>` for fences and regions with a unified signature `handler(input, ctx)`:
- `input` is the body string for fences and the block content list for regions.
- `ctx = {args, named, span, offset, prov /* see region-pipe-segmentation */, m /* incl. m.parse */, error, raw, style, next /* previously registered or default handler */}`.
The built-ins are registered from a `stdlib.mjs` through the public `$.region.define(name, fn)`, so `next` lets a document wrap the default table. The dispatcher has no name checks. Region style becomes a reserved `style:` named arg carrying the single StylePatch schema (see `style-surfaces`). It is applied only if the handler did not read `ctx.style`.

*Verifier:* The report misses one asymmetry: region handlers are not awaited. codegen emits `__region(` without `await` (codegen.cc:158) and the executor toShadow's the raw return (executor.mjs:126). An async `$.region` handler therefore renders the text '[object Promise]' (verified), whereas fences are awaited (codegen.cc:148).

*Verifier notes:* The unified registry with ctx and `next` is sound. Three things need to change:
(a) 'Apply style only if the handler did not read ctx.style' is itself an implicit heuristic (getter tracking). Make it explicit: the dispatcher always wraps the result unless the handler was registered with `{ownsStyle:true}`.
(b) Top-level `font/lang/color/sizePx` on regions is the documented authoring surface (document-model §3 l.83, `#!aside(font:'…', lang:"zh-TW")`). Keep them as aliases of `style:`.
(c) Dispatch must be `await __region(…)`, the same as fences.

### `markup-language/block-inline-placement` — Block/inline level is inferred by ad-hoc promotion and unwrapping rules split across codegen and the resolver

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-11, P3-17**
- locations: `engine/src/codegen/codegen.cc:95-117`; `engine/src/resolve/resolve.cc:469-475`; `engine/src/resolve/resolve.cc:484-493`; `engine/src/inline/inline.cc:280-290`; `docs/document-model.md:135`

Placement is decided in two layers by unrelated rules:
- codegen: a paragraph whose only child is display math becomes `mathblock`; any other display math silently becomes `mathinline`, and its label is dropped.
- The resolver unwraps 'a para whose sole child is a non-inline kind' (generalized from term/collect) and deletes empty paragraphs (for the bibliography placeholder).
Block constructors spliced mid-paragraph are accepted without any diagnostic. Verified: `#toc` inside a sentence puts a whole nested list inside the paragraph; `#heading(…)`, `#image`, `#mathblock` mid-paragraph produce block nodes inside `para`.

*Why ad hoc:* The model has no notion of a kind's level. Each feature that needed block placement from a splice added its own patch, in whichever layer was convenient.

*Proposed generalization (survey):* Declare the level in ops.def (`KIND(heading, 2, BLOCK)`, `KIND(math, …, BOTH)`), and merge `mathinline`/`mathblock` into `math{display}` if desired. Model instantiation, a single layer, enforces placement. A block value in inline position splits the enclosing paragraph into para, block, para (Typst semantics) and emits a `block-in-inline` info diagnostic. An inline value at block level is wrapped in a para. Display is an attribute, and placement decides the layout unit. The codegen promotion, the resolver's unwrap and emptyPara hacks, and the silent label drop all go away.

*Verifier:* Verified: `x #heading(2)[mid] y #toc z` produces a heading node and a whole nested list inside a `para`, with no diagnostic.

*Verifier notes:* Declaring LEVEL in ops.def and enforcing placement in one layer is right. Splitting a paragraph needs two more rules:
(a) Mark the trailing half as a continuation: no first-line indent and no paragraph spacing. Otherwise every mid-paragraph `#toc` or display formula becomes a visual paragraph break, and CJK paragraphs get a fresh 2em indent (App C).
(b) Not every container can be split. A block value inside a heading, a link or a table cell's inline run must become a diagnostic plus an error node, not a split.
The current 'mid-paragraph display degrades to inline' rule is documented only in a code comment (codegen.cc:113). Changing it is a behaviour change and should be written into math-design.

### `markup-language/value-coercion` — Splice value coercion is ad hoc: any function value is called with no arguments; arrays, null and undefined render as debug strings

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-01**
- locations: `runtime/src/worker/executor.mjs:59-62`; `runtime/src/worker/executor.mjs:230`

`toShadow` calls `x()` when a splice value is a function, so that bare `#toc`/`#glossary` work. It does this for ALL functions. Verified: `#box[` on a line followed by a block (the content arg is not seen) renders the text `undefined`, and `#em` alone yields an empty styled node. `#(xs.map(x => em(x)))` renders `[object Object],[object Object]`; `#(null)` and `#(undefined)` render `null` and `undefined`.

*Why ad hoc:* A convenience for two collectors was put into the generic value→content path, and the content-conversion protocol was never specified.

*Proposed generalization (survey):* A `toContent` protocol in the stdlib:
- shadow → itself
- string/number/bigint → text (numbers optionally formatted by `$.format`)
- Array/iterable → `seq(...map(toContent))`
- null/undefined/false → empty
- an object with `[Symbol.for('tsm.content')]()` → call it
- function → `splice-function` diagnostic plus an error node
Collectors are exported as content VALUES (`toc` is a frozen collect shadow factory with `Symbol.for('tsm.content')`), so `#toc` keeps working without the function hack. User types such as tables-from-data get the same hook as built-ins.

*Verifier:* Verified:
- `#(null)`/`#(undefined)` render 'null'/'undefined'.
- An array of shadows renders '[object Object],[object Object]'.
- Bare `#em` gives an empty styled node.
- An unclosed `#box[` renders 'undefined'.

*Verifier notes:* Sound. Compatibility note: zero-arg user helpers spliced bare (`#today`) currently work by accident and would become diagnostics. That is acceptable: the behaviour is documented only in the toShadow comment, for #toc/#glossary.

### `markup-language/surface-grammar-drift` — Four hand-maintained descriptions of the surface grammar (engine, tree-sitter, TextMate, converter escapers) that already disagree

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-09**
- locations: `grammar/tree-sitter-tsm/grammar.js:39-45`; `grammar/tree-sitter-tsm/grammar.js:57`; `grammar/tree-sitter-tsm/grammar.js:88-98`; `runtime/assets/hl/tsm.scm:1-2`; `editors/vscode-tsm/syntaxes/tsm.tmLanguage.json`; `editors/vscode-tsm/src/tokens.js:1-6`; `tools/convert/html2tsm.mjs:68`; `tools/convert/wiki2tsm.mjs:108`; `tools/convert/pbr2tsm.mjs:429`; `tools/convert/pbr2tsm.mjs:499`

tree-sitter-tsm is 'kept in sync by hand' (tsm.scm header) and is the VSCode extension's only tokenizer ('one grammar, two consumers', tokens.js:1-6), yet it disagrees with the engine:
- It colours `user@domain` as a reference; the engine treats it as literal.
- It colours `<id>` anywhere as a label; the engine accepts labels only after headings and `$`.
- It colours every `|` as a cell bar; in the engine `|` matters only inside regions.
- Its comments do not nest; the engine's do.
- Its math and footnotes are single-line with no nesting.
- Its `code_statement` covers only `#let`, not `#{`.
- A grammar comment says region args 'may span lines'; the engine requires a single line.
The TextMate grammar lacks footnotes entirely. Three converters copy-paste the same partial escaper, which escapes `$ # @` but not `* _ \` ^[ %-- |` or a trailing `<id>`.

*Why ad hoc:* The grammar is restated in each consumer, so every engine change (footnotes, cites) has to be manually replicated four times; drift is guaranteed and already present.

*Proposed generalization (survey):* Make the engine the single source of truth:
(a) A `tsr_tokens(doc)` export that maps AST/island spans to the 14-tag contract (code-design §5). The VSCode extension already runs the WASM engine for preview and uses it for semantic tokens. The `tsm` fence highlighter calls the engine's own parser instead of tree-sitter-tsm.
(b) A `tsr_escape(text, context: 'inline'|'cell'|'heading'|'link-text')` export, generated from the same delimiter and sigil table as `inline-delimiter-scanners`, which the converters import.
Keep tree-sitter-tsm only as a best-effort injection grammar for third-party editors, generated from that table (a `syntax.def` X-macro, like ops.def).

*Verifier:* Every listed disagreement holds in grammar.js:
- the `reference` regex has no look-behind;
- `label` matches anywhere;
- `cell_bar` matches every '|';
- the comment regex does not nest;
- `code_statement` covers only `#let`;
- l.39 says the region args 'may span lines'.
The TextMate grammar has no footnote rule. The three converters share the identical partial escaper.

*Verifier notes:* grammar.js:1-6 declares itself 'deliberately line-oriented and approximate'. However, editors/vscode-tsm/src/tokens.js:1-6 promotes it to the editor's semantic-token source and claims coloring 'matches published pages'. The documented approximation thus became a correctness claim.
Engine-derived tokens fit the dual-target rule, because the WASM core is host-neutral.

### `markup-language/sidecar-private-lowering` — Code sidecars are a fence-body micro-syntax implemented in the API layer, with a second, divergent markup→content lowering that users cannot access

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-13**
- locations: `engine/src/api/doc.h:83-140`; `engine/src/inline/fragment.cc:34-92`; `runtime/src/worker/executor.mjs:151`; `docs/verbatim-design.md:3-6`

The `sidecar: "///"` fence argument is interpreted after execution, in `Doc::extractSidecars` (api/doc.h). It splits lines at the marker and parses the comment part with `parseInlineFragment`. Its `Conv` is a C++ re-implementation of codegen's AST→content mapping, and it already diverges: `^[…]` notes are flattened into plain inline text (default branch), splices stay literal, and display math is impossible. The fence handler surface gets `ctx.m`, which is cooked text with no markup (executor.mjs:151, 232-236). A user-written fence transform therefore cannot do what the built-in sidecar does. verbatim-design marks the JS `m.parse` as 'still deferred'.

*Why ad hoc:* A built-in feature uses a private engine capability (re-entrant markup parsing) through a private lowering, in the API layer rather than the fence-handler layer.

*Proposed generalization (survey):* Deliver `ctx.m.parse(str, {offset})` and the `m` tag as real markup re-entry. `tsr_parse_fragment` runs the SAME linepass/inline/codegen on the fragment and returns JS (or ops plus shadows) that the executor splices, rebasing ids (document-model §4.1 already designs this). Then express sidecar as a default fence transform registered in stdlib JS: split at the marker, `m.parse` the comment part, and return `codeblock(…)` plus `group({role: 'sidecar-lines'}, …)`. Delete `Conv` and `extractSidecars`; one lowering remains.

*Verifier:* fragment.cc also drops comments entirely (`case AstKind::Comment: return;`, fragment.cc:78-79), unlike the main path, where comments are content nodes (v2 §4.2). It is documented as interim: verbatim-design.md:3-6 says the JS `ctx.m.parse` is deferred.

*Verifier notes:* A single lowering via a real `m.parse` is right. But a fragment compiled at runtime cannot see the document module's lexical scope: `#let` bindings are locals of the default-export function. So 'the SAME codegen' does not give fragment splices their meaning.
Specify `m.parse(str, {offset, scope})`. Splices then resolve against the stdlib plus an explicit scope object, and unresolved names become fragment-local error nodes.
Moving sidecar splitting to JS also means the no-handler fence path must become a registered default handler. That fits region-builtin-privilege's registry.

### `markup-language/collectors-closed` — Collector set is a closed string switch; each collector has its own placement and data channel

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-13**
- locations: `engine/src/resolve/resolve.cc:431-440`; `runtime/src/worker/executor.mjs:176-189`; `runtime/src/worker/executor.mjs:242-266`; `engine/src/resolve/resolve.cc:469-482`; `docs/document-model.md:5`

`collect{what}` dispatches `toc|glossary|notes|bibliography`; anything else is `collect-unknown`. `lof`, documented in document-model §5, does not exist. Placement and data channels differ per collector:
- `#toc` expands in place.
- notes expand at `#notes()` or are implicitly appended at the end.
- `#bibliography(src)` returns an empty text placeholder. The executor loads the data after the program and emits the collector at document END regardless of where it was written; the resolver then deletes the empty paragraph.
TOC entries are plain strings (see other_issues `structured-content-flattened`).

*Why ad hoc:* Each collector was implemented as its own resolver routine with its own lifecycle. Asynchronous data loading forced a special 'emit at end' path for one of them.

*Proposed generalization (survey):* One generic collector node: `collect({select: {kind?, role?, env?, ns?}, order: 'document'|'first-citation'|'alpha', item: <stdlib template name>, data?: resourceId})`. One resolver routine walks the label/env tables and instantiates the item template from a small table of C++ item templates (list-of-links, notes-list, bib-entries).
Resources are hoisted: codegen gathers `#bibliography(src)` like `#use` into a preamble `await __load(…)` that runs before the document body. The collector then expands in place and the placeholder and emptyPara hacks disappear.
lof/lot/theorem lists fall out of `select: {env: …}` (see `numbered-env-hardcoding`).

*Verifier:* `lof` is at document-model.md:127, not :5. The bibliography-at-end behaviour and the empty placeholder are documented as-built in notes-design §2 As built: 'The bibliography position is always document end in this version'.

*Verifier notes:* (a) Hoisting `#bibliography(src)` into a preamble changes evaluation order. `src` may reference an earlier `#let`, and `#use`, the model being copied, is itself unimplemented.
Keep document order instead:
- the call emits an in-place `collect{what:'bibliography', data:reqId}`;
- the executor appends the loaded entries as a data node `group{role:'bibdata', name:reqId}`;
- the resolver joins the two by id.
This still follows 'execution declares, resolver decides'.
(b) Expanding in place requires first-citation ordinals to be final before the collector is built. citeOrdinal currently runs during rewrite (resolve.cc:234-241, 264); the end-of-document placement is what guarantees completeness today. The generic collector must therefore expand in a post-pass after all refs are rewritten, which is how notes effectively work via notesPlaced.

### `markup-language/style-surfaces` — Three styling surfaces with three different key sets, and capabilities that only built-ins can use

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-01**
- locations: `runtime/src/worker/executor.mjs:6-11`; `runtime/src/worker/executor.mjs:131-137`; `runtime/src/worker/executor.mjs:225-229`; `runtime/src/worker/executor.mjs:274-284`; `engine/src/resolve/resolve.cc:342-347`; `engine/src/resolve/resolve.cc:376-386`; `engine/src/api/config.h:82-84`; `docs/design-decisions-v2.md:252`; `docs/document-model.md:46-48`

The three surfaces accept different keys:
- `#style({…})` accepts font/lang/color/sizePx plus bold/italic/underline/overline/strike.
- `$.style.push` accepts the same, OR a raw bit number.
- Region args accept only font/lang/color/sizePx.
Built-ins use styling no user surface can reach. Footnote markers use `CLS_SUP` and `sizeMul *= 0.7`, and note bodies use `sizeMul 0.85`, set directly by the resolver. `sizeMul` (relative size), sup/sub and dynamic classes/roles (`addDyn` in document-model §3) are not reachable from markup, so a user cannot build a marker that looks like the built-in one. Heading sizes are a C++ constant table (`headingSizeMul`). There is no way to restyle all instances of a kind or role; v2 §12 documents set/show sugar as possible later.

*Why ad hoc:* Each surface whitelists the keys its first feature needed, and built-in features bypass the surfaces entirely by writing Styling in C++.

*Proposed generalization (survey):* One `StylePatch` schema generated from the C++ `Styling`/`InlineStyle` fields: named bits including sup/sub, `sizeMul`, the InlineStyle props, and `class` → dynamic class id. `#style`, `$.style.push` and the reserved region/fence `style:` arg all accept it. Add a selector rule op: `$.style.rule({kind, role?, env?}, patch)` writes STYLE_RULE, and instantiation folds matching rules into a node's effective style at emission time. This is exactly the 'set rule layered on the stack' that v2 §12 anticipates, so emission-time binding is preserved. Built-ins become default rules: the note marker gets `{sup, sizeMul .7}`, note bodies `{sizeMul .85}`, and headings come from the config table.

*Verifier:* Class bits are not all unreachable. CLS_SUP (1<<19, model.h:22) and any other bit can be set via `$.style.push(<number>)` (executor.mjs:277; opbuf stylePush writes raw bits), though only as a block-granular schedule op, not inline. `sizeMul` is unreachable.
The 'document-model.md:46-48' citation should be §3 l.80-87. `dynClasses`/addDyn are documented (l.80, 84) but not implemented in Styling at all (model.h:29-38).

*Verifier notes:* v2 §12 l.246 deliberately chose a stack over set/show. A STYLE_RULE is consistent only if rules are entries on the schedule stack: scoped by popTo and captured at each EMIT. Otherwise global selectors replace emission-time binding.
'Built-ins become default rules' does not work as stated. Note markers, the notes list and TOC entries are fabricated by the RESOLVER after instantiation (resolve.cc:296-394), so rules folded at instantiation never see them. Either keep the active rule set alive into the resolver, applying it to fabricated nodes via the originating node's stack snapshot, or have the resolver fabricate `styled{class}` wrappers that the rules match.
One StylePatch schema shared by #style, $.style.push and region args is sound.

### `markup-language/cjk-softbreak-classifier` — The CJK soft-line-join rule lives in the inline parser with its own character class, which disagrees with emit's punctuation classes

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-10, P4-02**
- locations: `engine/src/inline/inline.cc:48-59`; `engine/src/support/support.h:158-186`; `runtime/src/worker/executor.mjs:102-103`

`advanceSpanIfNeeded` joins two source lines without a space only if both sides are `isCjk(c) || c == U+2014 || c == U+2026`. Curly quotes are not in that class, but emit treats them as CJK punctuation in CJK context. Verified (probe mk8.tsm): `他说“好”\n然后` becomes `好” 然后`, and `中文结尾\n“引号开头”` becomes `结尾 “引号`, a spurious space in Chinese prose. `regionJoin` always inserts ' ' between region lines, so even plain CJK lines inside regions gain spaces.

*Why ad hoc:* Script classification happens at parse time with a third private class list, while App C classification is owned by emit. The join decision is made before the information it needs (style `lang`, punctuation class) exists.

*Proposed generalization (survey):* Emit a `softbreak` node (see `region-pipe-segmentation`). Emit resolves it with the App C classifier (isCjkIdeo / isPunctOpen / isPunctClose plus the curly-quote context rule and the run's `lang`), into a breakable space, nothing, or boundary glue. Copy's `data-join` already models 'space vs none'.

*Verifier:* Verified:
- `他说“好”\n然后` gives '他说“好” 然后'.
- `中文结尾\n“引号开头”` gives '中文结尾 “引号开头”'.
isCjk (support.h:158-162) excludes U+2018-201D, while isPunctOpen/Close include them.

*Verifier notes:* As with region-pipe-segmentation, resolve softbreak at instantiation, where the run's StyleId and lang are known, by merging adjacent text. Do not leave it to emit, which applies App C only within a text node. A minimal interim fix is to make inline.cc's `cjkish` call support.h's isCjk||isPunctOpen||isPunctClose so that the two classifiers cannot drift.

### `markup-language/reference-forms-closed` — `@` sugar has one meaning and one form: no supplement, form or locator, and grouping is a comma string re-parsed by the resolver

- kind: adhoc · severity: medium · verdict: partly · plan: **P2-09**
- locations: `engine/src/inline/inline.cc:404-432`; `runtime/src/worker/executor.mjs:173`; `engine/src/resolve/resolve.cc:244-299`; `engine/src/ops/ops.def:59`; `docs/design-decisions-v2.md:239`; `docs/document-model.md:106`

`@id` and `@[…]` lower to `ref(target)`. The `ref()` constructor ignores everything except the target, even though `ArgK::form` exists and the docs specify `#ref("id", …)` with `form = number | name | full`. Grouped citations are written `@[k1, k2]`. The resolver splits that string on commas (an embedded micro-syntax) and succeeds only if EVERY key is a bibliography key, so `@[sec-a, kp81]` renders `??`. There is no way to write 'see Figure 3' versus '3', `p. 33` locators (Pandoc `[@k, p. 33]`) or a custom supplement (Typst `@fig[Fig.]`). `@fn-n` works only because of the shared namespace.

*Why ad hoc:* The cite feature was bolted onto the ref sugar by overloading the target string, rather than by extending the ref node with structured fields.

*Proposed generalization (survey):* `ref({targets: [{id, locator?}], form: 'number'|'name'|'full'|'page', supplement?: content}, …)`.
Sugar:
- `@id` → one target.
- `@id[supplement]`: a directly adjacent content argument, like Typst.
- `@[a; b, p. 3]`: parsed by codegen (not the resolver) into targets and locators with `;` as the separator, so labels may contain commas.
The resolver resolves each target against the namespaced label table or the bibliography independently, so mixed groups work. Rendering goes through the target's EnvSpec.refFormat for labels and `$.bib.cite` for citations, a JS template analogous to `$.bib.format`, applied at collect time.

*Verifier:* ArgK::form is NOT unused. It carries `form:'all'` on collect{what:'bibliography'} (executor.mjs:264, resolve.cc:404-406); only `ref` ignores it. The comma split is documented as-built: notes-design §2 As built says '`@[k1, k2]` splits on commas', and §2 Syntax shows `@[knuth84, liang83]`. `@[sec-a, kp81]` giving '??' is confirmed by resolveCite requiring every key to be in bib (resolve.cc:257-258).

*Verifier notes:* Switching the target separator to `;` silently changes the meaning of existing sources. `@[kp81, liang83]` (test/fixtures/cite/basic.tsm:3) and the many `@[a, b, …]` groups in examples/real-world/hott-introduction.tsm (l.13, 115, 127, 142) and zball-io example-hott.tsm would parse as one target plus the locator 'liang83'.
Keep `,` as the target separator. Carry locators and supplements via the adjacent-content form (`@kp81[p. 33]`) or an explicit keyword.
Parsing groups in codegen into structured targets, stored as child `reftarget` nodes within the ops value types, and resolving each target independently (so mixed label/bib groups work) is sound.

### `markup-language/footnote-sugar-oneoff` — `^[…]` footnote is a one-off inline form with its own scanner and spacing rule; its marker and note numbering are fixed

- kind: adhoc · severity: low · verdict: accurate · plan: **P4-07**
- locations: `engine/src/inline/inline.cc:384-403`; `engine/src/resolve/resolve.cc:205-215`; `engine/src/resolve/resolve.cc:342-386`; `docs/notes-design.md:12-20`

The `^[` branch does not call `spaceBeforeItem()`, so the space before the marker is dropped (`文本 ^[x] 后文` becomes `文本`·marker·` 后文`). That may be intentional (the marker hugs the text) but is undocumented. Its bracket scan ignores escapes and code spans and stays on one line. The named form `#note(name)[…]` / `^[name]` is designed but not implemented. Note rendering (an ordered list at 0.85, ↩ back-links, superscript at 0.7, arabic numerals) is fixed in resolver code. The constructor `note(...)` exists, which is good, so `#note[…]` is a real programmable equivalent.

*Why ad hoc:* The sugar has a private scanner and spacing policy. Its meaning is pinned to one constructor and one rendering, so margin notes, endnotes per chapter, or circled marks need engine changes.

*Proposed generalization (survey):* Treat `^[…]` as an instance of a small reserved sigil-bracket family (`^[…]`, `@[…]`, and maybe `~[…]` reserved for later). They are lexed by the shared `scanDelimited` (see `inline-delimiter-scanners`) and lowered by codegen to `__sigil('^', content)`. The stdlib binds `'^' → note` by default, and a document may rebind it with `$.sigil('^', sidenote)`. The syntax stays fixed at parse time, so there is no registration paradox; only the meaning is late-bound. Numbering and marks come from the footnote EnvSpec, styling from default style rules, and named or reused notes from universal labels (`#note(label: 'a')[…]` plus `@a` with form 'marker').

*Verifier:* The space is relocated, not simply dropped. `文本 ^[x]后文` gives '文本', marker, ' 后文': the source space moves after the marker, because pendingSpace survives flushText (inline.cc:392-397). notes-design §1 As built documents the marker hugging the text ('the digit hugs the text') but not this relocation.

*Verifier notes:* `^[…]` is a granted high-frequency sugar that maps to a real user-callable constructor, `note`, so the v2 §4 l.107 principle holds. The defects are the private scanner and the spacing bug.
Late-binding `^` via `$.sigil` is sound with respect to the registration paradox. However, `@[…]` does not belong in the family: its body is a label string, not content, so `__sigil('@', content)` would parse labels as markup. Restrict sigils to content-bodied forms.
The EnvSpec and universal-label parts are sound.

### `markup-language/quote-context-heuristic` — Curly-quote width class is chosen from neighbouring characters with no markup-level override

- kind: adhoc · severity: low · verdict: accurate · plan: **P3-30, P4-02**
- locations: `engine/src/emit/emit.cc:431-441`; `docs/real-world-report.md:30-33`

U+2018/2019/201C/201D are treated as Latin glyphs unless a CJK neighbour exists; otherwise they become full-width CJK punctuation with compressible halves. The style `lang` that the markup can set (`#style({lang: 'en'})`) is ignored by this decision. An English quotation inside Chinese prose (`他说“OK”`) therefore cannot be marked as Western punctuation.

*Why ad hoc:* A heuristic patch for one corpus failure (real-world report item 3). The general mechanism the markup already offers (`lang`) is bypassed.

*Proposed generalization (survey):* Punctuation width class = f(run `lang`, neighbour heuristic as fallback). `zh*`/`ja*` runs use the CJK class and other languages the Latin class. Authors override locally with `#style({lang: 'en'})[“OK”]`, or a `punct: 'latin'|'cjk'` StylePatch key. Emit owns the decision; the markup only supplies `lang`.

*Verifier notes:* This is a documented corpus fix (real-world-report.md:29-31). Lang first, with the heuristic as fallback, is cheap and uses the documented lang channel (document-model §3). Caveat: the document language set by tsr_set_lang lives in Config, not on runs, so most documents still hit the fallback unless the document lang is folded into the base style.

### `markup-language/no-execution-containment` — Any JS error anywhere aborts the whole document; per-block containment (v2 §2, §11) is not implemented

- kind: issue · severity: high · verdict: partly · plan: **P0-05, P2-02**
- locations: `engine/src/codegen/codegen.cc:214-229`; `runtime/src/worker/executor.mjs:314-322`; `runtime/src/worker/worker.mjs:123-131`; `docs/design-decisions-v2.md:73`

codegen emits plain `__emit(...)` statements (codegen.cc:226-228). `execute()` imports and runs the module with no per-block try/catch, and the worker throws on failure (executor.mjs:314-321; worker.mjs ~l.123-131). Verified fatal cases, each of which makes the whole document fail:
- `issue #todo here` (ReferenceError)
- `#!table(3)` (SyntaxError from `({3})`)
- an orphan `#aside!` line (ReferenceError)
- `#if (c) [..]` (SyntaxError)
- `#let x = [*b*]` (SyntaxError)
There is no source position, and a SyntaxError cannot be contained by try/catch at all. Fix: compile each top-level block into its own `try { … } catch (e) { __error(e, s, e2) }`, and pre-validate the generated JS per block (e.g. `new Function` per block or an acorn parse in the worker) so syntax errors become error blocks with spans instead of module-load failures.

*Verifier:* All five fatal cases reproduce (ReferenceError/SyntaxError abort execute()). The worker path is mis-cited: runtime/src/worker/worker.mjs:202 calls execute and :220-222 catches, frees the doc and posts an error for the whole document. Lines 123-131 are measurement provisioning. runtime/src/node/render.mjs:33 has the same uncontained call.

*Verifier notes:* Per-block try/catch is right, with four conditions:
(a) CodeStmts (`#let`, `#{…}`) must stay unwrapped at function scope. v2 §3 rule 4 strips the braces so that `let` lands in document scope; only `__emit` statements can be wrapped.
(b) The catch must pop the style stack to the block's entry height (v2 §11 l.228; document-model §3 l.87).
(c) Pre-validation must use the AsyncFunction constructor, because blocks contain `await __fence`. `new Function` is CSP-sensitive, so an acorn parse in the worker is more robust.
(d) A `#let` whose RHS fails validation must still declare its name, so that later uses fail as contained ReferenceErrors.
One `__block(s, e, async () => …)` wrapper delivers containment, style snapshot and span in one place.

### `markup-language/spec-features-unimplemented` — Specified surface features compile to invalid JS or are absent

- kind: issue · severity: high · verdict: accurate · plan: **P0-05, P2-12, P3-31**
- locations: `engine/src/inline/inline.cc:153-217`; `engine/src/codegen/codegen.cc:67-94`; `runtime/src/worker/executor.mjs:232-236`; `docs/design-decisions-v2.md:89`; `docs/design-decisions-v2.md:321-333`; `docs/design-decisions-v2.md:358`

The following are specified but not implemented:
- Keyword forms `#if (…) […] else […]`, `#for (…) […]` and `#use` (v2 §3 l.89, App A) lex as head-chain splices, e.g. `val((if))`.
- Content literals in `#let x = […]` (App A l.333) pass straight into JS as an array literal.
- Multi-line content blocks re-entering the line pass (App B rule 4) are missing: content args are matched within one line (inline.cc:204-212).
- `m` markup re-entry: `m` is cooked text (executor.mjs:232-236).
- Bare-URL autolinking (v2 §5 l.159).
- `$.ref`/user counters (§11.1 l.238).
- Config JSON at `tsr_doc_new` (document-model §11).
None of these is listed as deferred except `#use` and `m.parse`. Recommended unified design: one 'content block' production. `[` at a splice-controlled position compiles to `__content(() => <nested linepass+inline+codegen of the dedented block>)`. Keyword forms are then plain JS around closures: `#if (c) [A] else [B]` becomes `val(c ? __content(A) : __content(B))`, and `#for (const x of xs) [B]` becomes `val(__seq(function*(){ for (const x of xs) yield __content(B) }()))`. Loop variables bind because the thunk is a closure, which is exactly the doc's stated reason keyword forms are needed.

*Verifier:* Verified:
- `#if`/`#for` lex as `val((if))`/`val((for))` (SyntaxError).
- `#use(...)` gives a ReferenceError.
- `#let x = [*b*]` passes through as a JS array literal.
- A multi-line `#f[\n…\n]` gives 'undefined' plus literal brackets.
Only `#use`, m.parse and cell continuation are listed as deferred (document-model.md:194-195, architecture.md:225).

*Verifier notes:* The `__content(() => …)` thunk lowering is sound. It matches v2 §3's closure rationale, and evaluating thunks at splice time preserves emission-time binding.
One dependency is unstated. To dedent and re-run the line pass on a multi-line `[…]`, the linepass must find the matching `]` across lines inside containers, skipping code spans, math, comments and escapes. That is the same container-aware island machinery as inline-delimiter-scanners and should be stated as a dependency.

### `markup-language/nested-code-statements-dropped` — `#let` / `#{…}` inside list items, quotes and regions are silently dropped

- kind: issue · severity: high · verdict: accurate · plan: **P0-05, P2-12**
- locations: `engine/src/codegen/codegen.cc:198-200`; `engine/src/codegen/codegen.cc:214-225`; `engine/src/linepass/linepass.cc:358-385`

The linepass creates CodeLet/CodeBlock nodes inside containers, but codegen handles CodeStmt only among the document's direct children; the nested case falls to `default: text("")`. Verified (probe mk5.tsm): `#let z = 3` inside a list item, `#{ $.region(…) }` inside a quote, and `#let w = 5` inside a region all vanish. A later `#z`/`#w` then throws a ReferenceError that kills the document (see no-execution-containment). Block-scoped `$.style.push` inside a list item is therefore impossible.

*Verifier:* Verified (probe): `#let` inside a list item, `#{…}` inside a quote and `#let` inside a region each compile to `text("")`, via codegen default:198-200.

*Verifier notes:* The report proposes no fix. Codegen lowers containers as nested expressions (`item(para(…), …)`), where statements cannot appear. A sound fix must define the scope:
- either hoist the declaration (`let z;`) to document scope and lower the statement as a sequenced assignment inside the container expression;
- or lower containers as statement blocks that build child arrays.
At minimum, emit a `code-in-container` diagnostic instead of the silent empty text.

### `markup-language/inline-scanner-overrun` — Inline sub-scans read past their span: math islands and comments consume later paragraphs and table cells

- kind: issue · severity: high · verdict: accurate · plan: **P1-06**
- locations: `engine/src/inline/inline.cc:68-72`; `engine/src/inline/inline.cc:245`; `engine/src/inline/inline.cc:269-276`

`contiguous(to)` returns true whenever `to` lies beyond the LAST span (the loop never runs), so a `$` scan inside a cell or paragraph continues into following source. Verified results:
- In probe mk7.tsm (the HoTT table row), a formula grabbed text across a cell boundary.
- In probe mk2/t2, a cell's `$` produced `math @[116,294)` containing the next row, the `#table!` closer and two later paragraphs.
- In probe mk15.tsm, an empty display formula spanned a blank line into the next paragraph.
The inline comment scan uses `hardEnd = all.size()` with no span check (inline.cc:245). The generalization is in adhoc `inline-delimiter-scanners`; the immediate fix is to bound every scan by the block's last span end.

*Verifier:* Worse than stated: the swallowed source is ALSO parsed as its own blocks, so content is duplicated. In my probe, the math src contains '= Hidden' and a heading 'Hidden' is emitted as well.

*Verifier notes:* Bounding every scan at the block's last span end is the correct immediate fix. The structural fix is the linepass-integrated island handling.

### `markup-language/same-line-trailing-text-dropped` — Text after a line-start block comment, `#{…}`, or `#let …;` on the same line is silently discarded

- kind: issue · severity: medium · verdict: accurate · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:257-261`; `engine/src/linepass/linepass.cc:371`; `engine/src/linepass/linepass.cc:383`

After `blockComment`, `#let` or `#{`, the linepass advances `ln` past every line starting at or before the construct's end, discarding the remainder of the closing line. Verified (probe mk3.tsm): `%-- c --% tail after comment` and `#{ let q = 1 } tail after block` lose their tails with no diagnostic. v2 §4.2 says comments are 'inline or multi-line', which invites exactly this usage.

*Verifier:* Verified: `%-- c --% tail…` and `#{ let q2 = 1 } tail…` lose their tails, and the comment line is consumed as a block comment because the `%--` check (linepass.cc:257) runs before addParaLine.

*Verifier notes:* Either parse the remainder as a paragraph line or emit a diagnostic. This fits the leaf-owned comment rule proposed under 'missed'.

### `markup-language/region-error-recovery` — Region error handling deviates from App B rule 5; orphan closers become fatal splices

- kind: issue · severity: medium · verdict: accurate · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:35-38`; `engine/src/linepass/linepass.cc:303-357`; `engine/src/linepass/linepass.cc:388-392`

Region errors are handled inconsistently:
- A mismatched closer still closes the innermost region (diagnostic, but no error block or resync) (linepass.cc:345-353).
- A region closed implicitly by container exit (a `> #!aside` whose closer lacks `>`) is popped by `closeTo` with NO `region-unclosed` diagnostic (that check runs only at EOF, l.388-392). Its closer line then becomes the paragraph text `#aside!`, which is a `#aside` splice and a fatal ReferenceError (verified, probe mk9/mk10).
- Region args are confined to the opener line (scanJs over `all.substr(0, le)`, l.312).

*Verifier:* Verified:
- `> #!aside` / `> body` / `#aside!` pops the region via closeTo with no diagnostic; the closer line becomes `val((aside))` plus '!', a ReferenceError.
- A mismatched closer closes the innermost region (`#a!` closes `b`).

*Verifier notes:* Aligning with App B rule 5 is correct. A closer should close only a matching open region, and a non-matching closer line should become text plus a diagnostic.

### `markup-language/span-loss` — Splice-produced and builder-produced nodes carry no source span

- kind: issue · severity: medium · verdict: accurate · plan: **P2-04**
- locations: `engine/src/codegen/codegen.cc:67-94`; `engine/src/codegen/codegen.cc:157-193`; `runtime/src/worker/executor.mjs:76-121`; `docs/document-model.md:1`

document-model §1 says 'splice-produced nodes get the splice span'. In practice codegen wraps `val(...)` and the nodes inside region bodies without `__at`, and builders create nodes without spans. In tree dumps, `#ref("h2")`, `#note[…]`, `#image`, region groups, the paragraphs inside them, trow/tcell/seq and term groups all show @[0,0). Anchors, diagnostics and copy degrade for every programmable construct, and editor round-trip from a rendered node to its source fails for them. Fix: `__at(val(...), s, e)` on every splice, and have builders inherit the span of their region or call (pass `ctx.span`).

*Verifier:* Verified: @[0,0) on splice results, region groups' paras, trow/tcell, term groups and TOC entries. The report misses that container nodes (list/item/quote) have spans truncated to their first line (see missed).

*Verifier notes:* `__at` attaches spans through a SPAN op keyed by node id (opbuf.mjs:96-103), and DAG nodes are shared. Stamping a stored value at every splice site overwrites its span (the last site wins) and mis-anchors earlier emissions. Stamp only span-less nodes and propagate to their span-less descendants, or carry the splice span on the emission instance during instantiation, which already copies per emission (document-model §3 l.86).

### `markup-language/structured-content-flattened` — Headings in the TOC, term names and label excerpts are flattened to strings, leaking footnote bodies and dropping math, refs and styling

- kind: issue · severity: medium · verdict: accurate · plan: **P3-03**
- locations: `engine/src/resolve/resolve.cc:17-24`; `engine/src/resolve/resolve.cc:144-147`; `engine/src/resolve/resolve.cc:315-318`; `runtime/src/worker/executor.mjs:67-71`; `runtime/src/worker/executor.mjs:174-175`

`excerptInto` concatenates text descendants. Verified (probe mk3.tsm): the TOC entry for `= 标题含脚注^[头注] 与 $x^2$ 和 *粗* @h2` reads `1 标题含脚注头注 与  和 粗 `. The footnote body is included; the math and the ref are missing. `term` names go through `shadowText`, so `#term[$x$ 名称]` gets the name and label ` 名称`. Collectors should copy (or reference) the heading's inline content subtree with notes removed, rather than build strings.

*Verifier:* Reproduced exactly: the TOC entry reads '1 标题含脚注头注 与  和 粗 ', and `#term[$x$ 名称]` gets label/name ' 名称' with a leading space.

*Verifier notes:* Copying the heading's inline subtree is right. The copy must:
- strip label and anchor args (e.g. `fnref-n` on the footnote marker, resolve.cc:349), or DOM ids duplicate;
- drop notes;
- flatten refs to their resolved text, since the TOC entry is itself a link and nested <a> is invalid HTML.

### `markup-language/ambiguity-hazards` — Surface ambiguity hazards in prose

- kind: issue · severity: medium · verdict: accurate · plan: **P3-33**
- locations: `engine/src/inline/inline.cc:351-383`; `engine/src/linepass/linepass.cc:117-130`; `engine/src/linepass/linepass.cc:272-283`

Several prose patterns are misread:
- Any `#` followed by an ASCII letter is a splice, so `see #todo`, `color #fff` or a hashtag is fatal at runtime rather than a diagnostic. Typst has the same lexical rule but reports at compile time.
- Strict-pair emphasis applies intraword to ASCII: `snake_case_name` becomes `snake`·em(`case`)·`name`, `2*3*4` makes `3` bold, and `file_a and file_b` turns ` a and file` into emphasis (verified, probe mk12.tsm). A CJK-neutral fix: a marker flanked on BOTH sides by ASCII alphanumerics is literal.
- `$5 and $10` becomes a formula.
- A heading ending in ` <T>` is silently taken as a label.
- `+ a` followed by `1. b` continues the same list (ordered flags match) and ignores the start number; the list's marker column is stored in a repurposed `u8 level` field (linepass.cc:120-127).

*Verifier:* Verified:
- `snake_case_name` gives em('case').
- `file_a and file_b` gives em('a and file').
- `$5 and $10` gives math '5 and'.
The list case is worse than stated: a blank line does not separate lists either. `+ a`, `1. b`, blank, `7. c`, `+ d` is ONE list with start=1; the 7 is lost.

*Verifier notes:* Strict pairing (v2 §5) and the `#` lexical rule (v2 §3) are documented choices. Treating a marker as literal when ASCII alphanumerics flank it on both sides is CJK-neutral and sound. The fatal `#todo` becomes a contained diagnostic once per-block containment exists.

### `markup-language/ast-dump-note` — AST dump omits Note nodes

- kind: issue · severity: low · verdict: accurate · plan: **P0-02**
- locations: `engine/src/inline/inline.cc:664-755`

`dumpNode` has no `case AstKind::Note`, so `--stage=ast` prints an indented empty line for every footnote (verified, probe t1/t2). Golden ASTs containing footnotes are therefore ambiguous. The switch has no default branch, so the compiler warning was the only guard.

*Verifier:* Verified: footnotes print as an empty indented line, and dumpNode (inline.cc:664-754) has no `case AstKind::Note` and no default.

### `markup-language/doc-drift` — User-facing and design docs describe syntax the engine does not accept

- kind: issue · severity: low · verdict: partly · plan: **P3-35**
- locations: `docs/design-decisions-v2.md:159`; `docs/design-decisions-v2.md:310-360`; `engine/src/codegen/codegen.cc:113`; `tools/convert/tex2tsm.mjs`

Examples:
- The blog syntax page (zball-io src/docs/syntax.tsm:40) says figures and tables carry `<标签>`.
- Its inline-code example renders wrongly (the double-backtick form is unsupported).
- The HoTT converter emits `_caption_ <tab-pov>`, which is literal text.
- v2 promises bare-URL autolinks, keyword forms and `#let x = […]`.
- The `contiguous()` comment and the tree-sitter grammar comment both mis-state region-arg behaviour.
- 'mid-paragraph display degrades to inline' appears only as a code comment (codegen.cc:113), not in math-design.
Recommend a conformance fixture set derived from the v2 appendices, so the spec is executable.

*Verifier:* The claim that the contiguous() comment mis-states region-arg behaviour is wrong. That comment (inline.cc:65-67) says nothing about region args; it mis-states its OWN behaviour, since it returns true whenever `to` is past the last span. The blog label claim is at syntax.tsm:44, not :40. The other items are accurate; the display-math degrade appears nowhere in docs/*.md.

*Verifier notes:* A conformance fixture set derived from the v2 appendices is the right fix.

### `markup-language/missed:0` — Region interiors bypass ordinary block lowering: display math in any region becomes inline, loses its label, and receives the figure caption prefix

- kind: missed · severity: high · verdict: verifier-found · plan: **P2-11**
- locations: `engine/src/codegen/codegen.cc:95-107`; `engine/src/codegen/codegen.cc:166-185`; `runtime/src/worker/executor.mjs:94-121`; `engine/src/resolve/resolve.cc:173-179`; `docs/figure-design.md (§1: '#!figure keeps working with no src (a figure whose body is a table/code/math block)')`

The Row/Cell lowering emits each cell through value(), so the Para→mathblock promotion never runs inside regions. Verified with `#!figure(label:"f1")` / `$ a + b $ <eq-f>` / blank / `Caption text` / `#figure!`. The tree is group{figure} → para['图 1：', mathinline('a + b')], para['Caption text'], and `@eq-f` renders '??'. The documented figure-with-math-body case is therefore broken: the formula is inline, unnumbered, and the resolver's 'first para child is the caption' rule writes the caption prefix into the formula.

*Proposed generalization (survey):* Lower region children through the same codegen value() path as top-level blocks (the region-pipe fix, step 3). Make the caption an explicit slot (a role:'caption' child produced by the builder, or ArgK caption) instead of 'first para child'. EnvSpec captionFormat then targets the slot.

### `markup-language/missed:1` — Line-level comments are not invisible: a comment line inside a paragraph splits it

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:203-229`; `engine/src/linepass/linepass.cc:257-261`; `engine/src/inline/inline.cc:241-242`; `docs/design-decisions-v2.md:150-152`

A line starting with `%--` inside an open paragraph goes to blockComment(), which calls closeLeaf(). `line one` / `%-- note --%` / `line two` becomes para, comment, para (verified in semantic HTML as two <p>). Commenting out a source line changes document structure and gives a CJK paragraph a fresh indent. The inline path promises the opposite ('a comment is invisible to the text around it').

*Proposed generalization (survey):* Comments are islands owned by the enclosing leaf. When a leaf is open, a comment-only line (or a multi-line comment starting at line start) is appended to the leaf's line spans and becomes an inline comment node. It is a block-level comment only at a block boundary. This is the same leaf-ownership rule as the linepass-integrated island handling.

### `markup-language/missed:2` — Container spans (list/item/quote) are truncated to the opener line

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:84-91`; `engine/src/linepass/linepass.cc:124-133`; `engine/src/linepass/linepass.cc:35-38`; `docs/document-model.md:26`

tryStarters sets quote/list/item span = {p, le} of the first line, and nothing extends it. Only regions update span.end, at the closer (l.349). Verified in semantic HTML: `<ul data-s=60 data-e=68>` contains an <li> whose paragraph spans to 87, and `<blockquote data-e=93>` contains a <p data-e=98>. Anchors, upgrade callbacks and editor round-trip rely on data-s/data-e, and document-model §1 requires every node to carry its span.

*Proposed generalization (survey):* Handle it once, in the container stack. On every line that matchPrefixes attributes to an open container, or that is consumed inside it (fences, comments, code statements), set span.end of every matched container to that line's end. Equivalently, compute each container's span as the union of its children's spans at closeTo().

### `markup-language/missed:3` — Style-stack (schedule) ops are callable from inline splice positions but act at block granularity, retroactively, with no block-exit snapshot

- kind: missed · severity: medium · verdict: verifier-found · plan: **P2-02, P3-01**
- locations: `runtime/src/worker/executor.mjs:274-284`; `engine/src/codegen/codegen.cc:226-228`; `docs/design-decisions-v2.md:251`; `docs/document-model.md:87`

`before #($.style.push({bold:true})) after` bolds 'before' as well. STYLE_PUSH is written while the paragraph's constructor arguments are evaluated, before its EMIT. The splice also renders 'undefined', and the push leaks into every following block (verified). v2 §12 and document-model §3 l.87 promise that block boundaries snapshot the stack height and pop to it on exit and error; nothing implements that. The documented scope forms are `#{…}` statements and region args, but the language does not stop the inline misuse.

*Proposed generalization (survey):* Codegen wraps every top-level block as `__block(s, e, async () => value)`. The executor then (a) snapshots and restores the style height, (b) contains exceptions, and (c) sets an in-block-evaluation flag so that `$.style.*` called from inside a block expression raises a diagnostic, or is deferred until after that block's EMIT. One wrapper subsumes no-execution-containment, style containment and splice spans.

### `markup-language/missed:4` — Indentation counts spaces only; a tab silently ends a list, quote or fence content

- kind: missed · severity: low · verdict: verifier-found · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:54`; `engine/src/linepass/linepass.cc:66`; `engine/src/linepass/linepass.cc:80`; `engine/src/linepass/linepass.cc:98`; `engine/src/linepass/linepass.cc:194`; `docs/design-decisions-v2.md:355-357`

Every prefix matcher compares `all[p] == ' '`, and list markers need a literal space after them. A tab-indented continuation of `- item a` closes the item with no diagnostic and becomes a separate top-level paragraph (verified). `-\titem` is not a list item. App B talks about content columns but never defines tab width. This is a silent, undocumented determinism hole in the 'no lazy continuation' design.

*Proposed generalization (survey):* Use one columnOf(line, pos) helper with an explicit, documented tab policy (tab advances to the next multiple of 4, or tabs in indentation are rejected with a `tab-indent` diagnostic). All prefix matchers, starters and fence dedent use it.

### `markup-language/missed:5` — List identity ignores marker class and blank lines; later start numbers are silently dropped

- kind: missed · severity: low · verdict: verifier-found · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:117-130`; `docs/design-decisions-v2.md:340-341`; `docs/design-decisions-v2.md:355-356`

A list continues whenever the parent's last child is a List with the same `ordered` flag and the same marker column. The column is stored in the repurposed `u8 level` field. `+` (auto) and `N.` (explicit) are not distinguished, and blank lines never end a list. So `+ a`, `1. b`, blank, `7. c`, `+ d` is one list with start=1, and the author's 7 is lost with no diagnostic (verified). App B gives `+` and `N.` different meanings, but the skeleton erases the difference.

*Proposed generalization (survey):* Key list identity on (marker class ∈ {bullet, auto, explicit-number}, column), stored in dedicated fields. An explicit `N.` after a blank line whose N is not the expected next ordinal starts a new list (or triggers a `list-renumber` diagnostic). Keep the marker class on the list node so that the constructor equivalence `list({ordered, start})` round-trips.

## parser-frontend — Parser front end (linepass, inline, splice lexer, fragments, AST)

<details><summary>Design summary (as audited)</summary>

The front end is two hand-written passes. Despite architecture.md §2.2 and v2 §4/§15, there is no PackCC. Phase 1 (linepass.cc, 458 LOC) is an automaton over physical lines. matchPrefixes() re-matches the open container stack: Quote is matched by the '>' prefix, Item by its content column, and Region always matches. tryStarters() opens Quote/List/Item. Then a fixed if-cascade classifies the leaf: ``` fence (consumes lines to the closer and dedents), %-- block comment (raw nested scan), '= ' heading (plus a right-to-left '<id>' label scan), '---' rule, '#!name(args)' region opener (pushed onto the same container stack), '#name!' closer, '#let'/'#{' statements (JS scanned by jslex.h scanJs, multi-line only when no container is open), and otherwise a paragraph line. SkelNode reuses its fields per kind. Phase 2 (inline.cc, 765 LOC) has two parts. AstBuilder::build() maps SkelNodes to AstNodes; it splits a fence info string into lang plus JS args, and it pre-splits every region paragraph into Row/Cell using a separate mini-lexer, splitCells(). InlineParser::run() is a first-byte if-cascade over a block's prefix-stripped line spans. It handles soft-space joins (with a hard-coded CJK no-space rule), '\\' escapes, '%--' comments, '$' math (display when padded, plus a ' <id>' label), '`' code, '[..](..)' links, strict-pair '*'/'_' frames, '#' splices (ASCII head chain, JS call args, repeatable single-line [content] args parsed by recursive single-span sub-parsers, ';' terminator), '^[' notes and '@'/'@[..]' refs. Constructs that cross lines read the raw buffer and are admitted by a contiguous() guard. The AST is one fat struct with 22 kinds and overloaded fields. 15 of those kinds are 1:1 sugar for a constructor. Three exhaustive switches interpret it: codegen (which emits constructor calls), fragment.cc (which lowers the same AST directly to ContentNodes for code-block sidecars, bypassing JS and constructors) and the dump. There is no Error node: a malformed construct becomes literal text plus a diagnostic, is silently dropped, or is pasted into JS that fails the whole module. Spans are absolute byte offsets. The language is re-described, approximately, by a tree-sitter grammar, a TextMate grammar, editor regexes, translation masks and converter escapers.


Strengths:

- Phase 1 is small, deterministic and fuzzable. It has an explicit container stack, no lazy continuation (v2 App B rule 3), and the list content-column rule is implemented cleanly (engine/src/linepass/linepass.cc:48-142).
- jslex.h scanJs is a pure, unit-tested state machine (strings, templates with ${} nesting, comments, no regex by spec) and is already reused by splices, #let/#{, region args and fence info strings (engine/src/inline/jslex.h:22, engine/test/tests.cc:47-60). This is the right kind of shared primitive; it just is not the only one.
- The Appendix A splice delimiting rules are implemented faithfully and covered by fixtures (test/fixtures/splice/*): the ASCII identifier cut, '.' continuing only before an identifier start, the ';' hard terminator, and trailing-call desugaring f(a)[c] to f(a, c) (engine/src/codegen/codegen.cc:73-90).
- Representing a block as a list of prefix-stripped line spans is the correct primitive for containers. Every AST node carries an absolute span, and spans are attached to op-stream values out of band via __at(node,s,e), so constructor signatures stay clean.
- Strict-pair emphasis with a tiny frame stack implements the documented CJK-first departure from CommonMark flanking (v2 §5). Unclosed frames degrade deterministically to literal markers.
- Fragment parsing reuses the real InlineParser rather than a subset reimplementation, so the grammar is shared by construction. Only the lowering diverges.
- Every stage has a byte-stable dump with goldens, and tsrc exposes skeleton/ast/js/diags. Every finding in this report was reproduced with it.
- The tooling grammar is honestly labelled approximate (grammar/tree-sitter-tsm/grammar.js:1-6). Fences are verbatim regardless of tag and are dispatched at runtime, which avoids the parse-time registration paradox (v2 §4.1).

</details>


### `parser-frontend/per-feature-ast-kinds` — AST encodes each sugar as its own node kind, with per-kind field overloading

- kind: adhoc · severity: high · verdict: partly · plan: **P1-05**
- locations: `engine/src/ast/ast.h:6`; `engine/src/ast/ast.h:12`; `engine/src/linepass/linepass.h:9`; `engine/src/linepass/linepass.cc:127`; `engine/src/codegen/codegen.cc:26`; `engine/src/inline/fragment.cc:34`; `engine/src/inline/inline.cc:658`; `test/golden/notes/basic.ast.txt:6`

AstKind has 22 kinds: Doc, Para, CodeStmt, Text, Styled, Splice, Heading, ListB, Item, Quote, CodeBlockB, Rule, Comment, Link, Code, SpliceArg, Ref, Region, Row, Cell, Math, Note. Fifteen of them are pure sugar: codegen's only treatment of each is 'call constructor X with these args and kids' (codegen.cc:26-200). Per-kind payload lives in overloaded fields of one struct. `str` holds text, code, comment body, ref target, region name or math source. `aux` holds fence lang, link url, heading label or math label. `tag` holds the emphasis marker, let-vs-block, heading level or math display flag. `num` holds the list start or the fence body offset. `expr` holds splice JS, fence args or region args. ast.h:15-22 documents only some of these. SkelNode does the same: langSpan is either the fence info or the region name, and the list marker column is stored in `level` ('repurposed', linepass.cc:127). Every new sugar therefore touches three exhaustive switches: codegen value(), fragment Conv::conv() and dumpNode(). The footnote commit 57340a8 touched 17 files across 9 layers. Note was forgotten in dumpNode, so the committed goldens test/golden/notes/{basic,explicit,cjk-glue}.ast.txt contain bare blank lines where the note node belongs. In fragment.cc, Note hits `default:` and the footnote body is flattened into running text.

*Why ad hoc:* Under the language's governing principle (v2 §4, design-decisions-v2.md:107: every syntax form is sugar for a constructor), the constructor call is the only semantic content of these nodes. Yet each is a distinct kind, and a user call (Splice) has a different shape from built-in sugar. The AST layer is therefore not closed under the language's own principle. This is not documented as a trade-off.

*Proposed generalization (survey):* Collapse the sugar kinds (and Row/Cell/SpliceArg) into one generic node, owned by ast/:
  enum class AstKind : u8 { Doc, Text, SoftBreak, Call, Stmt, Comment, Error };
  struct AstArg { StrRef key /*0 = positional*/; enum : u8 { Str, Num, Bool, Js } tag; StrRef s; double n; Span js; };
  struct AstNode { AstKind kind; Span span; Span marker; u16 rule /*syntax-rule id for tooling*/; StrRef ctor /*'heading','note',.. or 0*/; Span callee /*JS head chain for #f..*/; std::vector<AstArg> args; std::vector<AstNode*> kids; StrRef str; };
Under this node, `= T <x>` is Call{ctor:heading, args:[1, label:'x'], kids:[T]}. `^[b]` is Call{ctor:note, kids:[b]}. `#f(a)[b]` is Call{callee:f, args:[Js a], kids:[b]}: the same shape as sugar. Codegen becomes one emitCall(), dump becomes one generic printer ('call heading level=1 label="x" @[s,e)'), and fragment lowering becomes table-driven (see fragment-parallel-lowering). The ctor names come from the syntax registry (see inline-recognizer-cascade and multiple-tsm-grammars).

*Verifier:* The kind count (22) and the field overloading are accurate. ast.h:15-22 does not document Math's aux/tag, CodeBlockB's num/expr or Region's expr. SkelNode::level is reused as the list marker column (linepass.cc:127). dumpNode has no Note case (inline.cc:664-754), and the notes goldens record blank lines (test/golden/notes/basic.ast.txt:6,16). fragment.cc:228-230 flattens Note through `default:`. Codegen also has a silent `default: text("")` (codegen.cc:198-200). OVERSTATED: commit 57340a8 touched 19 non-golden files, but only ast.h, inline.cc and codegen.cc belong to the front end. The rest (resolve.cc, emit.cc, render, ops.def, executor) carry note semantics that no AST shape would remove. A generic Call node therefore saves about two edits per sugar, not seventeen. The missing dump case was not undetectable either: engine/CMakeLists.txt:27 compiles with -Wall -Wextra, and -Wall includes -Wswitch, which warns on a default-less switch with an unhandled enumerator. What is missing is -Werror=switch, not a new AST.

*Verifier notes:* v2 §4 (design-decisions-v2.md:107) says sugar and constructors must converge on one op stream. They already do at codegen: `= T` and `#heading(1,null)[T]` emit the same call. Distinct typed kinds therefore do not break the governing principle; that part of the 'why_adhoc' argument is overstated. The real smells are the untyped overloaded fields, the three parallel switches with `default:` fall-throughs, and Row/Cell. The proposed `StrRef ctor` (a ctor *name* string) brings back the name-string dispatch the owners list as ad hoc, and it drops parser-time typing (heading level 1–6, list start). Better: an X-macro ast.def / ctors.def that gives each sugar a generated enum id, a typed payload schema, a dump printer and a codegen signature (positional order). Keep Splice distinct only because its callee is JS text. Remove all `default:` arms and build with -Werror=switch. This keeps the codegen output byte-identical, so the js/ops goldens stay stable.

### `parser-frontend/inline-recognizer-cascade` — Inline parser is a first-byte if-cascade; each sugar brings its own scanner with different escape and nesting rules

- kind: adhoc · severity: high · verdict: partly · plan: **P1-06**
- locations: `engine/src/inline/inline.cc:219`; `engine/src/inline/inline.cc:114`; `engine/src/inline/inline.cc:130`; `engine/src/inline/inline.cc:240`; `engine/src/inline/inline.cc:266`; `engine/src/inline/inline.cc:314`; `engine/src/inline/inline.cc:331`; `engine/src/inline/inline.cc:351`; `engine/src/inline/inline.cc:380`; `engine/src/inline/inline.cc:384`; `engine/src/inline/inline.cc:404`; `engine/src/inline/inline.cc:477`; `engine/src/linepass/linepass.cc:203`; `engine/src/inline/jslex.h:97`; `engine/src/inline/inline.cc:170`

Mapping of each recognizer branch in run() to the feature it serves:
- ' ', '\t', '\r': soft-space state.
- '\\': literal next byte.
- '%--': nested comment; scans to the end of the whole buffer (:240).
- '$': math island (:266). Raw scan to the next unescaped '$', display if padded, then a ' <id>' label lookahead.
- '`': code span. Single line, no escapes (:314).
- '[': link (:331), only if matchBracket succeeds, then '](' follows, then matchParen succeeds.
- '*' / '_': strict-pair frames (:351).
- '#': handleSplice (:380).
- '^[': footnote (:384). It skips spaceBeforeItem(), which silently swallows the authored space before the marker.
- '@': ref with a raw-byte lookbehind (:404).
Bracket matching alone exists in five variants with different semantics:
- matchBracket (:114): escape-aware, nesting.
- matchParen (:130).
- the footnote loop (:387-391): nesting, NO escapes. Verified: `^[a \\] b]` closes at the escaped bracket.
- the `@[` loop (:411-413): no nesting, no escapes.
- splitCells' content-arg loop (:477-486).
Comment scanning is written twice (linepass.cc:203-229 and inline.cc:240-265). The splice head chain is written three times (handleSplice :170-192 with lastCall tracking, scanSpliceHead jslex.h:97-116, and splitCells :467-490).

*Why ad hoc:* Recognizers are not parameterised. Every feature added a branch (git log: M4 @refs, M7a $ islands, M7c equation labels, footnotes 57340a8), each re-deciding escapes, nesting, line-crossing, precedence and spacing. The precedence spec (v2 §4.2: verbatim > comments > markup) is encoded implicitly by branch order and scan extents.

*Proposed generalization (survey):* Add a rule table in a new engine/src/syntax/ module, generated from syntax.def (see multiple-tsm-grammars):
  struct InlineRule { u16 id; std::string_view open, close; enum Body : u8 { Verbatim, Inline, Ident, Pair, CallChain } body; u16 flags /*Escapes, Nest, CrossLines, PrevNotIdent, PaddedDisplay, AcceptLabel, AttachLeft*/; u8 prec; StrRef ctor; ArgMap argMap /*body->kids | body->str arg | '(..)' suffix -> url*/; };
  std::array<SmallVec<u16>,256> byFirstByte;
Dispatch: for byte b, try its rules in prec order. The first success yields Call{ctor,args,kids}; Inline bodies recurse.
One cursor primitive, SpanCursor::scanDelimited(open, close, flags) -> std::optional<Span>, and one scanBalanced(open, close, flags) replace the five matchers.
Emphasis keeps its strict-pair stack as the Pair body mode. Splices are the CallChain body mode over jslex. Block comments reuse the Verbatim scanner.
The same table drives fragments, region provenance and the generated tooling grammars.

*Verifier:* Mostly accurate. Verified: `^[a \] b]` closes at the escaped bracket; `@[` has no nesting and no escape (inline.cc:411-413); inline comments scan to all.size() (inline.cc:245); the comment scanner is duplicated (linepass.cc:203-229 vs inline.cc:240-265). Two corrections. (1) The '^[' branch does not 'swallow' the space before the marker: it skips spaceBeforeItem() (inline.cc:393), so pendingSpace survives and the space is re-emitted AFTER the note. Verified: `word ^[note]. end` gives text("word"), note, text(" . end"), i.e. a stray space before the punctuation. This looks like an undocumented attempt at notes-design.md's 'marker glued to the preceding block' rule. (2) The head chain is implemented twice, not three times: handleSplice (inline.cc:170-192) and scanSpliceHead (jslex.h:97-116). splitCells calls scanSpliceHead (inline.cc:474), which has no other caller, and only duplicates the content-argument bracket loop (inline.cc:477-486).

*Verifier notes:* The proposed scanDelimited/scanBalanced byte scanner keeps the deeper defect the report missed: none of the bracket matchers respects verbatim islands. v2 §5 (design-decisions-v2.md:158) says islands are carved out before everything else. Verified: `#f[code `a]b` here]` closes the argument inside the code span; `[range $[0,1)$](u)` is not a link; `^[arr `x[0]]` q]` closes inside the code span. A general fix cannot be another byte scanner. Bracket bodies must be found by the inline parser itself: '[' pushes a frame like the emphasis Frame stack, and at the matching top-level ']' the parser decides link / content-arg / note / literal (a CommonMark-style delimiter stack). Alternatively, one shared inline LEXER tokenizes islands, escapes and comments first. A rule table whose flags are each used by one rule (PaddedDisplay, AcceptLabel only by math; PrevNotIdent only by '@') moves the ad-hoc logic into flags rather than removing it. Pick a table only if it also drives the tooling generation; the shared lexer/primitives are the high-value part.

### `parser-frontend/region-pipe-segmentation-in-parser` — The table's '|' cell convention is hard-coded into the parser for every region, via a masking lexer the design explicitly rejected

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-11**
- locations: `engine/src/inline/inline.cc:454`; `engine/src/inline/inline.cc:601`; `engine/src/codegen/codegen.cc:157`; `engine/src/codegen/codegen.cc:166`; `runtime/src/worker/executor.mjs:76`; `runtime/src/worker/executor.mjs:94`; `docs/design-decisions-v2.md:125`; `docs/design-decisions-v2.md:145`

For EVERY region, not just tables, AstBuilder turns each paragraph into Row/Cell nodes by splitting raw source lines at '|'. It uses splitCells(), a masking lexer that skips only code spans and splices. Codegen detects this case by `k->kids[0]->kind == AstKind::Row` and emits nested JS arrays. tableBuild consumes them; every other region runs regionJoin(), which re-glues cells with ' | ' and rows with ' '. Verified with tsrc:
- In a #!table, the row `norm | $|x|$` yields 4 cells. The math island escapes its cell span, producing math '|x|' plus duplicated texts 'x' and '$'.
- `[a|b](u)` and `^[n|m]` are split too.
- In #!figure or #!aside, the author's `a|b` becomes `a | b`.
- CJK prose that spans two source lines inside a region gets a space inserted, because rows are joined in JS and never pass the CJK join rule.

*Why ad hoc:* v2 §4.1 decided the opposite, and explains why: 'Tree-level splitting is embedding-proof … zero masking machinery' (design-decisions-v2.md:125), and '`|`-splitting is the table constructor's convention, not a region mechanism' (:145). The implementation moved one constructor's semantics into the parser and AST for all regions and added a fourth lexer. architecture.md:221 records this as 'codegen-materialized | segmentation'. The decision is documented but contradicts its own rationale.

*Proposed generalization (survey):* Return to the documented design, with a generic provenance primitive:
(1) Line boundaries are kept as SoftBreak nodes (see parser-owned-cjk-line-join).
(2) The inline rule table declares a 'bare separator' byte set, initially just '|'. An unescaped '|' at top level becomes its own Text{str:'|', flags:BARE} node. `\\|` and any '|' inside Math/Code/Link/Note/Splice nodes are naturally not top-level.
(3) Region children are passed as ordinary content values.
(4) The runtime adds a provenance query API on shadow nodes, as v2 §4.1 promised: $.lines(para) -> Content[][] splits at SoftBreak, and $.split(line, '|') splits only at BARE nodes.
`table` becomes a public constructor written on these queries. Other regions receive their paragraphs untouched.

*Verifier:* All reproduced. `norm | $|x|$` gives 4 cells: math '|x|' plus text 'x' and '$'. In #!aside, `[a|b](u)` and `^[n|m]` are split into literal text. CJK rows are joined with makeText(' ') (executor.mjs:103). Minor: the as-built note is at architecture.md:222, not :221.

*Verifier notes:* The contradiction with v2 §4.1 (design-decisions-v2.md:125,145) is real. architecture.md:222 records the deviation as built, but gives no rationale for it. Proposed step (2), splitting EVERY paragraph's Text at top-level '|' into BARE nodes, changes emission everywhere. emitText resets its word buffer and Prev class per node (emit.cc:270-290) and flushes at node end (emit.cc:455), and no CJK/Latin boundary glue is inserted across nodes. `a|b` in ordinary prose would therefore become three blocks with different segmentation, and every golden containing '|' would shift. Restrict separator marking to region interiors (AstBuilder already knows the context, inline.cc:601). Better still, follow v2 §4.1:145 literally: record the top-level segmentation as provenance metadata on the region's paragraph (line spans plus separator offsets) and leave the Text nodes intact; the table constructor then queries it. Either way the shadow node needs the data, i.e. a new ARGK or kind, which means an OPS_VERSION bump (ops.def:3-5). That is acceptable but must be planned.

### `parser-frontend/cross-line-raw-scans` — Constructs that cross lines escape their block through raw-buffer scans plus a contiguous() guard and an open.empty() context flag

- kind: adhoc · severity: high · verdict: partly · plan: **P1-08**
- locations: `engine/src/inline/inline.cc:68`; `engine/src/inline/inline.cc:160`; `engine/src/inline/inline.cc:186`; `engine/src/inline/inline.cc:245`; `engine/src/inline/inline.cc:269`; `engine/src/linepass/linepass.cc:364`; `engine/src/linepass/linepass.cc:376`

Phase 2 should see only its block's line spans. Four constructs instead read the raw buffer `all` beyond the block:
- inline comments (hardEnd = all.size(), :245)
- math islands (:269-275)
- `#(..)` splices (:160)
- head-chain call args (:186)
They are admitted by contiguous(to). That function only checks gaps between the block's own spans, and returns true when `to` is past the last span. A construct can therefore run out of its paragraph, quote, link text, footnote, content argument or table cell; see other_issues contiguous-escapes-blocks. In phase 1, multi-line `#let`/`#{` scanning is switched by the context flag `open.empty()`: at top level it scans the whole buffer, inside a container it is cut at end of line. The same construct thus has different syntax depending on nesting.

*Why ad hoc:* Each cross-line construct got its own escape hatch around the phase boundary, instead of one rule for which constructs may span lines and who owns them. The phase separation that v2 §4 cites as the reason recovery is block-granular is therefore not enforced.

*Proposed generalization (survey):* (1) Introduce SpanCursor { const std::vector<Span>* lines; u32 line, off; char peek(); bool atJoin(); Span sliceTo(const SpanCursor&); } as the ONLY way phase-2 code reads source. scanJs takes a cursor in which lines are joined by a virtual '\n', so splices and #let behave identically at top level and inside containers. Codegen pastes the joined logical text, with a per-line offset table for source mapping.
(2) Phase 1 owns every construct that may cross a block boundary: fence, block comment, region, multi-line #let/#{, and an inline %-- opened mid-line. Linepass tracks %--/--% depth per line (it already lexes those bytes) and suppresses block-marker recognition while depth > 0.
(3) The inline rule flag CrossLines permits crossing line joins within the same block only.

*Verifier:* Inline comments are NOT admitted by contiguous(). The comment branch (inline.cc:240-265) scans to all.size() and calls seekTo(p) unconditionally, with no guard at all. Only math (inline.cc:275) and splices (inline.cc:160,186) consult contiguous(). Also, contiguity is byte adjacency of whitespace-trimmed spans (`spans[k+1].start != spans[k].end + 1`, inline.cc:70), and addParaLine trims trailing whitespace including '\r' (linepass.cc:147-148). Hence a multi-line formula or splice that works with LF fails with CRLF or with one trailing space. Verified: `a $x + \ny$ b` becomes literal text; `v #(f(1,\r\n2)) w` gives 'unbalanced #(...)'.

*Verifier notes:* Step (2) says linepass 'already lexes those bytes'. It does not: linepass only recognizes %-- at line start (linepass.cc:257). To track a mid-line %-- it must know code spans, math islands, escapes (\%--) and JS strings inside splices (#f("%--")), because v2 §4.2:151 puts verbatim above comments. That means re-implementing the inline lexer in phase 1, which is the duplication the report criticizes. There are two coherent options. (a) Share one inline lexer between both phases. (b) Confine inline comments to their block: only a line-initial %-- may span blocks, and a mid-line %-- left unclosed at block end becomes a diagnostic. Option (b) slightly narrows v2 §4.2's 'lexically dumb' wording but keeps block-granular containment (v2 §4(b)). The SpanCursor must define line joins structurally (same block, no prefix stripped), not by byte adjacency.

### `parser-frontend/content-args-inline-only` — Content arguments are single-line and inline-only, so user constructors cannot receive block content

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-08**
- locations: `engine/src/inline/inline.cc:144`; `engine/src/inline/inline.cc:204`; `docs/design-decisions-v2.md:104`; `docs/design-decisions-v2.md:358`

`[..]` content arguments are parsed by parseSub() as a single-span inline parse, bounded by the splice's current line (`while (after < lim && all[after] == '[')`, :204). App B rule 4 (a multi-line `[...]` is dedented and re-enters the line pass) is not implemented, and neither is v2 §4's `#for (..) [- #x]` example. Verified:
- `#box[first line\n- second]` reports 'unclosed content argument', and '- second]' becomes a real list item.
- `#f(1,\n2)[ok]` silently turns `[ok]` into text, because the content-arg loop reuses the first line's `lim`.
Built-in sugar (lists, quotes, regions) can contain blocks. A user-callable constructor can only ever receive inline content.

*Why ad hoc:* Block structure is available only to syntax forms hard-wired into linepass. User extensions are not on equal footing with built-ins, which contradicts v2 §4 ('guarantees every syntactic capability has a programmable equivalent').

*Proposed generalization (survey):* A single re-entrant front-end entry point, parseContent(const std::vector<Span>& lines, ContentMode mode), with mode in {Inline, Blocks, Auto}. linepass is refactored to iterate over a supplied list of line spans instead of SourceText lines; spans stay absolute. Content args use Auto: if the bracket body (matched with the SpanCursor) contains a line join, the continuation lines are dedented and run through linepass and then AstBuilder, yielding block kids; otherwise the body is parsed inline. The same entry serves region interiors, keyword-form bodies, `#let x = [..]` and m.parse in block mode.

*Verifier:* Verified. `#box[first line\n- second]` reports 'unclosed content argument', and '- second]' becomes a real list item. In `#f(1,\n2)[ok]` the [ok] stays literal, because the arg loop uses the first span's lim (inline.cc:153,204). App B rule 4 (design-decisions-v2.md:358) and v2 §4:104 are unimplemented.

*Verifier notes:* The proposal detects multi-line bodies in phase 2 ('if the bracket body contains a line join'). By then phase 1 has already classified the continuation lines: in the verified case '- second]' is already a List skeleton node. Phase 1 must own multi-line content arguments, as v2 §4:104 implies: they 're-enter the line pass'. That requires the island-aware inline lexer at paragraph lines (see inline-recognizer-cascade). The 'Auto' heuristic would also silently change `#em[foo\nbar]` from inline into block content. A more uniform rule: the body of a content argument is always parsed as blocks, and a body that is a single paragraph is unwrapped. resolve.cc:486-492 already implements a generic sole-child unwrap that could be reused.

### `parser-frontend/keyword-forms-closed-set-missing` — Appendix A keyword forms and #let content literals are specified as a closed special set, and none is implemented

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-12, P3-31**
- locations: `docs/design-decisions-v2.md:89`; `docs/design-decisions-v2.md:321`; `docs/design-decisions-v2.md:322`; `docs/design-decisions-v2.md:323`; `engine/src/inline/inline.cc:170`; `engine/src/linepass/linepass.cc:358`

Appendix A specifies `#if (c) [..] else [..]`, `#for (const x of xs) [..]`, `#use(..)` and content literals `#let x = [..]`. The parser has no keyword table. Verified:
- `#if (ok) [yes] else [no]` lexes `if` as an ordinary head chain and compiles to `val((if))` followed by the literal text ' (ok) [yes] else [no]'. That is a JS SyntaxError that fails the whole module, and tsrc --stage=diags is empty.
- `#let c = [*bold* content]` is pasted verbatim as `let c = [*bold* content];`, another SyntaxError.
v2 §3 itself frames keyword forms as 'a closed set', justified by 'loop variables bind into the content block'.

*Why ad hoc:* A closed list of keywords, each to be hand-coded, is the special-casing the owners want to avoid. The stated justification (variables bind into the content) holds for every JS compound statement, not just if/for.

*Proposed generalization (survey):* One 'statement splice with content bodies' rule: `#<kw> (head) [body] (else (<kw> (head))? [body])*`. kw is any JS compound-statement keyword (if, for, while), recognised by a small reserved-word table in jslex rather than a feature list. It desugars to:
  await (async () => { const __o = []; <kw> (<head>) { __o.push(<body>); } else { __o.push(<body2>); } return seq(...__o); })()
Loop variables bind by plain JS scoping and await stays legal. The App A else-attachment rule is kept. Bodies use parseContent(Auto). The #let rule recognises an RHS starting with '[' at depth 0 and lowers it to `let name = seq(<content>)`. `#use` hoists through the same Stmt-hoisting path codegen uses for #let.

*Verifier:* Verified. `#if (ok) [yes] else [no]` compiles to `val((if))`, `#let c = [*bold* content]` is pasted verbatim, and `#use("./m.js")` becomes `val((use("./m.js")))`. All produce empty diags. architecture.md:225 records #use as deferred.

*Verifier notes:* The closed set is a documented decision with a stated rationale. v2 §3 (design-decisions-v2.md:89) says loop variables bind into the content block, so keyword forms cannot be replaced by helper functions. The proposal also ends in a reserved-word table, so 'not a feature list' is a distinction without much difference. The genuinely ad-hoc defect is silent failure: a JS reserved word in bare-head position can never be a value, yet handleSplice accepts it (inline.cc:170-176), and the result is a whole-module SyntaxError with no diagnostic. The cheap general fix is to reject reserved words as bare heads in the head-chain lexer, with a diagnostic, or to route them to the keyword-form rule. The desugaring has three gaps. (1) `#let x = [` conflicts with JS array literals; App A:333 already makes `[…]` a content literal there, so `#let xs = [1,2]` must become `([1,2])`, and this must be diagnosed and documented. (2) Bodies containing #let need the statement-list lowering (see code-statements-top-level-only). (3) The IIFE must be async and awaited, which the report does write.

### `parser-frontend/code-statements-top-level-only` — Statements are a special case of the document root: nested #let/#{ are parsed, then silently dropped

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-12**
- locations: `engine/src/linepass/linepass.cc:358`; `engine/src/linepass/linepass.cc:374`; `engine/src/codegen/codegen.cc:214`; `engine/src/codegen/codegen.cc:198`

linepass recognises `#let`/`#{` at the start of any line, including inside list items, quotes and regions, and AstBuilder keeps them as CodeStmt children of those containers. Codegen handles CodeStmt only in the document-level loop (codegen.cc:214-229). Inside value() it falls through to `default: out += "text(\"\")"` (:198-200). Verified: `- item\n  #let x = 3\n  value #x` compiles to `item(para(..), text(""), para(text("value "), val((x))))`. The binding vanishes without a diagnostic, and x is a ReferenceError at run time.

*Why ad hoc:* Statements are not a node kind that is legal in any content list. They are a property of the root that codegen special-cases, while the parser accepts them everywhere.

*Proposed generalization (survey):* Make every content list a statement list. In the generic Call lowering, a container whose kids include Stmt nodes compiles to `(() => { <stmts and const __kN = <content kid> in source order>; return ctor(args, __k0, __k1, ..); })()`. Containers without statements keep today's flat expression, at zero cost. The scoping decision must be stated explicitly. Block scope is JS-natural and is what keyword-form bodies need; hoisting to document scope would preserve v2 §2's 'one lexical scope'.

*Verifier:* Verified. `- item\n  #let x = 3\n  value #x` compiles to `item(para(..), text(""), para(.., val((x))))` with no diagnostic. CodeStmt is handled only at codegen.cc:214-225, and value() falls to `default: text("")` (codegen.cc:198-200). Nested #let/#{ is also single-line only (`open.empty() ? all : all.substr(0, le)`, linepass.cc:364,376).

*Verifier notes:* The proposed `(() => {...})()` is a synchronous IIFE. Fences inside containers compile to `await __fence(...)` (codegen.cc:148), so the result would be a SyntaxError; it must be `await (async () => {...})()`. Block-scoping a nested #let would also contradict v2 §2 (design-decisions-v2.md:71: 'The whole document shares one lexical scope') and App A:319 (#{ braces stripped so let lands in document scope). A lowering that keeps the documented semantics is A-normal form at document scope: emit the container's content kids as temporaries in source order, interleaving the statements: `const __k0 = para(..); let x = 3; const __k1 = para(.., val(x)); __emit(list(.., item(__k0, __k1)));`. It needs no IIFE and has the same duplicate-binding behaviour as top level.

### `parser-frontend/label-and-id-lexing-scattered` — Labels and reference ids are lexed by five unrelated routines with different alphabets; there is no generic postfix-label mechanism

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-06**
- locations: `engine/src/linepass/linepass.cc:272`; `engine/src/inline/inline.cc:292`; `engine/src/inline/inline.cc:407`; `engine/src/inline/inline.cc:410`; `engine/src/inline/inline.cc:414`; `grammar/tree-sitter-tsm/grammar.js:94`; `grammar/tree-sitter-tsm/grammar.js:95`; `editors/vscode-tsm/src/extension.js:140`; `docs/document-model.md:63`

The five routines:
- Heading `<id>`: a right-to-left scan from line end that requires ' <', allows CJK, forbids spaces (linepass.cc:272-283).
- Equation label: a lookahead after the closing '$' that allows spaces. `$ x $ <my eq>` yields label 'my eq' (verified; inline.cc:292-306).
- Bare ref: accepts [A-Za-z_$][A-Za-z0-9_$-]*, so `@$weird` is a ref (verified), guarded by a raw-byte lookbehind `all[i-1]`.
- `@[..]`: accepts anything except ']', with no nesting or escape.
- Region labels: only as a JS `label:` argument.
Tooling uses yet another alphabet, `<[A-Za-z][A-Za-z0-9_-]*>` anywhere on a line (tree-sitter, TextMate, VS Code completion). As a result `x<y> z` is coloured and harvested as a label though the engine treats it as text, and CJK labels are never offered. There is no label syntax for paragraphs, lists, quotes, fences or images, although the model labels group and table (document-model §2.1).

*Why ad hoc:* Each labelable feature grew its own postfix recognizer when it shipped (M4 headings, M7c equations). The id grammar is not a shared definition.

*Proposed generalization (survey):* Two registry primitives.
(a) One IdLexer, shared by refs and labels and exported to the tooling generator: bareId uses ASCII per v2 §11.1; bracketId is '[' .. ']' with escapes and nesting, accepting any Unicode.
(b) A generic postfix attribute '<id>', accepted by any block or inline rule with the AcceptLabel flag and recognised by one trailing-attribute scanner. For block rules it applies at the end of the opener line: heading, fence info line, region opener, display-math paragraph. For inline rules it applies right after the closer (math). It always desugars to args.label. Region/fence `label:` args remain the programmable equivalent.

*Verifier:* Verified: `= Title <标签>` gives label '标签'; `= Title <a b>` gives no label; `$ x $ <my eq>` gives label 'my eq'; `@$weird` gives ref '$weird'; `@[a]b]` gives target 'a'; `x<y> z` is text in the engine, but grammar.js:95 and extension.js:140 treat it as a label. The model's labelable kinds are heading/group/table/term/mathblock (document-model.md:63).

*Verifier notes:* Sharing the IdLexer with tooling is the high-value part. The postfix '<id>' attribute is already ambiguous with prose at line end: a heading `= Generic Vec <T>` yields label 'T'. Keep it on openers whose tail is not prose (fence info, region opener, display math) and on headings, where the precedent is documented (v2 §11.1:239). Do not extend it to paragraphs or items. On region openers it duplicates the documented `label:` arg, so pick one canonical form and make the other sugar for it.

### `parser-frontend/display-math-by-ast-shape` — Display math is decided by a whitespace heuristic in the parser plus AST-shape pattern matching in codegen

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-11**
- locations: `engine/src/inline/inline.cc:284`; `engine/src/codegen/codegen.cc:95`; `engine/src/codegen/codegen.cc:112`; `docs/design-decisions-v2.md:348`

InlineParser marks `$ x $` as display when both ends of the body are whitespace (:280-284). Codegen then pattern-matches 'a Para whose only child is a display Math' to emit mathblock (codegen.cc:95-107). A padded formula anywhere else silently degrades to inline (codegen.cc:112-117); the code comment calls this 'documented', but no design doc says so. App B (design-decisions-v2.md:348) lists `$ .. $` as a line-structure form, yet linepass has no such rule.

*Why ad hoc:* A desugaring decision (which constructor a syntax form maps to) is made by codegen from AST shape instead of by the syntax rule, so codegen contains grammar.

*Proposed generalization (survey):* Give InlineRule an optional blockCtor ('mathblock' for padded $). A generic 'sole-inline promotion' step in AstBuilder (phase 2, not codegen) rewrites a paragraph whose only non-comment content is one such match into Call{blockCtor}. The same mechanism serves future block-capable inline forms, such as a lone image call. A padded formula mid-paragraph emits an Info diagnostic instead of degrading silently.

*Verifier:* Verified at codegen.cc:95-117 and inline.cc:280-284. No design doc documents mid-paragraph degradation; math-design.md has no such statement. App B (design-decisions-v2.md:348) lists `$ … $` as a line form, but linepass has no rule for it.

*Verifier notes:* The proposal adds a third 'sole child' promotion site, while the engine already has a generic one. resolve.cc:486-492 unwraps a paragraph whose only child is any non-inline kind; its comment says it was 'generalized from the term/collect special case' (document-model.md:135 is stale on this). The general mechanism: padded `$ … $` desugars to a block-kind constructor (mathblock(src,label)) at parse time, so codegen keeps no grammar; the existing resolver unwrap promotes the sole-child case. Then one generic policy covers 'block-kind value in inline position' (degrade plus diagnostic) for mathblock, #codeblock(..), #image and handler-built tables spliced mid-paragraph. That policy belongs in model or emit, not in codegen or AstBuilder.

### `parser-frontend/parser-owned-cjk-line-join` — The parser makes a script-dependent typographic decision at source line joins, and the predicate is duplicated in emit

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-10, P4-02**
- locations: `engine/src/inline/inline.cc:48`; `engine/src/inline/inline.cc:50`; `engine/src/emit/emit.cc:407`; `runtime/src/worker/executor.mjs:103`

When InlineParser crosses a line join, it decides whether the newline becomes a space. There is none if both neighbours are 'cjkish' (isCjk, U+2014 or U+2026); otherwise a soft space is inserted. The decision is baked into the cooked Text string, so the emitter cannot tell an authored space from a line break. The same ad-hoc class predicate is duplicated at emit.cc:407. Region rows bypass the rule (regionJoin joins with ' '), and fragments never see joins. The rule appears in no design doc, only in a code comment.

*Why ad hoc:* Script-dependent spacing belongs to emission, which already owns the CJK classes (App C). The parser reaches across the layer boundary and destroys information.

*Proposed generalization (survey):* Add an AST SoftBreak node and an ops kind `softbreak` next to the reserved `hardbreak` (ops.def). Emit resolves each softbreak with a single support/ function, joinPolicy(prevCp, nextCp), backed by a table over script-class pairs and colocated with the App C rules. copy.mjs treats a softbreak like a consumed space. Regions, fragments and table provenance then share identical behaviour.

*Verifier:* Accurate. Additional evidence that the parser-side decision is wrong, not just misplaced: it classifies RAW BYTES at the join (utf8PrevCp(all, prevEnd), inline.cc:54), not glyphs. Verified: `这是*强调*\n中文继续` gives text " 中文继续", a space inserted between 调 and 中 because the last raw byte is '*'. `这是\*\n中文` and `公式$x$\n中文` also get spaces. The duplicated predicate is at emit.cc:396/407.

*Verifier notes:* This matches CommonMark's softbreak-node design. The ops bump is acknowledged. Emit needs cross-node neighbour context to resolve a softbreak, because emitText's Prev state is per node (emit.cc:277-278). That context is the same thing the CJK/Latin cross-node boundary currently lacks, so both should be fixed together. JS-side shadowText and regionJoin must also treat softbreak.

### `parser-frontend/fragment-parallel-lowering` — Fragment re-entry lowers the AST with a second, hard-coded switch that bypasses constructors; m/m.parse are not wired

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-13**
- locations: `engine/src/inline/fragment.cc:34`; `engine/src/inline/fragment.cc:47`; `engine/src/inline/fragment.cc:59`; `engine/src/inline/fragment.cc:78`; `engine/src/inline/fragment.cc:89`; `engine/src/api/doc.h:131`; `engine/src/codegen/codegen.cc:34`; `runtime/src/worker/executor.mjs:6`; `runtime/src/worker/executor.mjs:151`; `runtime/src/worker/executor.mjs:232`; `docs/architecture.md:120`

parseInlineFragment reuses InlineParser, which is good, but lowers the AST with its own switch directly to ContentNodes:
- `*` maps to CLS_BOLD and `_` to CLS_EM (fragment.cc:47). This is a third copy of the marker semantics, after codegen.cc:34 ('*' to strong) and executor.mjs:6,169-170 (strong to CLS_BOLD).
- Comments are dropped (:78), although v2 §4.2 makes comments document-model nodes.
- Note falls to `default:`, and its body is inlined into the running text.
- Math always becomes mathinline; display and label are discarded.
- Splices stay literal, with an Info diagnostic.
- Every node receives the same outer span.
The only caller is code-block sidecar extraction inside the API handle (doc.h:88-150). The JS-facing re-entry that v2 §2 centres on does not exist:
- `m` is a cooked-text stub (executor.mjs:232-236).
- Fence ctx.m says 'm.parse (WASM re-entry) is deferred' (:151); the deferral is documented in verbatim-design.md:3-6.
- wasm_api.cc exports no tsr_parse_fragment, although architecture.md:120 lists it.
Capability therefore descends: document prose, then sidecars (an inline subset without notes, comments or display math), then m-tags and fence handlers (plain text).

*Why ad hoc:* A second semantic lowering gives built-in sugar two meanings. Constructor overrides can never reach fragments, and every new inline feature must be added twice; Note already was not. The documented deferral reason ('needs an ops-slice return channel') is real, but the engine half was built in a shape that channel cannot reuse.

*Proposed generalization (survey):* Have one lowering of the uniform Call AST, owned by a generated constructor table. Add ctors.def next to ops.def, for example:
  CTOR(strong, KIND(styled), ARGS(bits=CLS_BOLD), KIDS(inline))
  CTOR(note, KIND(note), ARGS(), KIDS(inline))
gen-ops-ts consumes it to generate the JS built-in constructors. C++ gets lowerCall(ctorId, args, kids) from it for fragments.
For JS re-entry, export tsr_parse_fragment(doc, str, nHoles, mode) returning a compact AST buffer. The ${} holes of m`..` are encoded as U+FFFC plus an index and come back as Splice{hole:i}. The worker lowers the AST through the SAME __c constructor namespace that generated code uses, so user overrides apply, and substitutes the interpolated values for the holes. Sidecars use the same path and gain Note, Comment and display support automatically. Spans become fragment-relative plus a base offset, matching the two-tier contract of v2 §4.1.

*Verifier:* Verified: fragment.cc:186 ('*'→CLS_BOLD else CLS_EM), :198-203 (mathinline only, display/label dropped), :217-218 (comments dropped), :228-230 (Note flattened), and every node gets the `outer` span (:159). The m tag is cooked text (executor.mjs:232-236); ctx.m is a stub (:151); there is no tsr_parse_fragment in engine/src. The deferral is documented in verbatim-design.md:3-6.

*Verifier notes:* Generating the JS constructors from a ctors.def is over-scoped. Many constructors carry runtime logic that a table cannot express: codeblock array bodies (executor.mjs:196-206), term's shadowText, bibliography side effects, heading's null label. The C++ lowerCall table would also remain a second lowering. The minimal design with one lowering: tsr_parse_fragment returns the fragment as the SAME codegen value() output (a JS expression, with ${} holes as `__hole(i)`), or an AST buffer lowered by the worker through the same constructor namespace. Sidecar extraction then moves from api/doc.h:88-150 into the JS codeblock constructor, which calls the fragment parser. fragment.cc's Conv is deleted and user overrides apply. This respects the dual-target rule (api/ stays a thin adapter) and determinism (synchronous).

### `parser-frontend/region-fence-private-dispatch` — Region and fence sugar desugar to private dispatchers with bespoke encodings; their 'name(args)' headers are parsed twice, in different phases

- kind: adhoc · severity: medium · verdict: partly · plan: **P2-03, P2-06**
- locations: `engine/src/codegen/codegen.cc:145`; `engine/src/codegen/codegen.cc:157`; `engine/src/inline/inline.cc:566`; `engine/src/linepass/linepass.cc:303`; `runtime/src/worker/executor.mjs:123`; `runtime/src/worker/executor.mjs:127`; `runtime/src/worker/executor.mjs:142`; `docs/design-decisions-v2.md:107`

`#!name(args) .. #name!` compiles to the private `__region("name", ({args}), [[[cells]]])`, and fences compile to `__fence(lang, ({args}), body, offset)`. The executor special-cases name === 'table' and name === 'figure' before falling back to the generic group{role:name}. No public table(...) or figure(...) constructor exists, so `#!table` has no user-callable equivalent. Args of both forms are pasted into an object-literal body `({ .. })`, so only key: value args work; positional args, as in `#f(a, b)`, are impossible. The same header shape is parsed in two phases with different failure modes. Region args are parsed in linepass, and a failure turns the line into paragraph text. Fence info is parsed in AstBuilder by searching for the first '('; a failure leaves the parenthesis in the language tag. Verified: info ' graphviz x (a: 1) junk' yields lang 'graphviz x', and ' junk' is silently dropped.

*Why ad hoc:* Region and fence sugar does not desugar to constructors, which violates v2 §4 (design-decisions-v2.md:107). The two headers are parallel special cases of the splice call-chain grammar.

*Proposed generalization (survey):* One 'callable header' grammar, ident(args), lexed by jslex as a call chain with positional and named arguments, shared by splice heads, region openers and fence info strings. It produces Call{ctor:name, args:Js(span)}.
Regions desugar to __c.region(name)(...args)(...blocks), resolved in this order: a $.region user handler, then a public built-in constructor of that name, then group({role:name}). table and figure become ordinary exported constructors, also usable as #table(cols: 3)[..].
Fences desugar to __c.fence(tag)(body, ctx, ...args), resolved as $.fence user handler, then codeblock. Trailing junk after ')' is a diagnostic.

*Verifier:* Verified: `__region("name", ({args}), [[[cells]]])` and `__fence(lang, ({args}), body, offset)` (codegen.cc:145-193). The executor special-cases 'table' and 'figure' (executor.mjs:127-128). `#!aside(1, 2)` compiles to `({1, 2})` and info `js(a: 1, 2)` to `({a: 1, 2})`: SyntaxErrors with no diagnostic. Trailing ' junk' after a fence's ')' is silently dropped (inline.cc:573-580). Overstated: '#!table has no user-callable equivalent' is not strictly true. __region is a destructured parameter in the same scope as user code (codegen.cc:209-212), so `#(__region("table", {cols:2}, [[[..]]]))` works. It is a private-by-convention dispatcher with a bespoke nested-array encoding, not an unreachable one.

*Verifier notes:* Resolving a region name against ALL public constructors would collide with constructors whose signatures are not (args, blocks): `#!code`, `#!list`, `#!text`, and `#!quote` (which the editor even offers). The lookup must go through a registry of block constructors with a uniform (args, children) signature. Making `k: v` named arguments part of a header grammar 'shared by splice heads' would change the JS inside `#f(…)`, which v2 §2:48 ('slightly modified JS is rejected') and App A:318 ('100% standard JS inside') forbid. Keep the `k: v` object-body convention only for region and fence headers, where it already deviates from JS, and diagnose positional args there.

### `parser-frontend/region-container-special-case` — Regions are bolted onto the implicit-close container stack with kind checks; forced closes are silent and mismatches do not resync

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:51`; `engine/src/linepass/linepass.cc:245`; `engine/src/linepass/linepass.cc:333`; `engine/src/linepass/linepass.cc:350`; `engine/src/linepass/linepass.cc:388`; `engine/src/linepass/linepass.cc:127`; `docs/design-decisions-v2.md:359`

Regions share the `open` stack with quotes and items and are special-cased in three places: matchPrefixes treats them as always matching (:51), the closer searches for the innermost Region (:333-357), and a final loop reports regions still open at end of input (:388-392). Verified consequences:
(a) When an enclosing list item ends by dedent, closeTo() pops the region silently. No region-unclosed diagnostic is issued, and the later `#aside!` closer becomes the splice `val((aside))`, a runtime ReferenceError.
(b) A mismatched closer still closes the innermost region, and the next closer then closes the outer one (`#!a #!b .. #a! .. #b!`). App B rule 5 instead requires an error block with resync at the matching closer.
SkelNode stores the region name in langSpan and its args in inner, and List stores its marker column in `level`.

*Why ad hoc:* The container automaton has no concept of explicitly-closed containers. Regions are emulated with kind tests at each use site.

*Proposed generalization (survey):* Introduce a container protocol in linepass:
  struct ContainerRule { SkelKind kind; bool explicitClose; bool (*continues)(LineCursor&, OpenC&); bool (*tryOpen)(LineCursor&, OpenC&); };
closeTo() calls onForcedClose(OpenC&). For explicitClose containers this emits region-unclosed and wraps the partial region in an Error node. A closer searches the stack by name and closes any intermediate containers with diagnostics (resync). An unmatched closer becomes an Error node rather than splice text. Per-kind payload moves into the typed Call header instead of repurposed SkelNode fields.

*Verifier:* Both reproduced. (a) In `- item\n  #!aside\n  inside\nout\n#aside!` the dedent pops the region silently, and the closer becomes `val((aside))` plus text '!'. (b) In `#!a #!b x #a! y #b!` the first closer closes b and the second closes a; two region-mismatch diagnostics are emitted, but there is no resync or error block (App B rule 5, design-decisions-v2.md:359).

*Verifier notes:* The ContainerRule protocol with explicitClose and onForcedClose is the right generalization. Under App B rule 5 the whole region becomes an error block; the proposal's wrapper preserves that.

### `parser-frontend/multiple-tsm-grammars` — No single source of truth: the language is described by eight-plus artefacts, one of which (PackCC) does not exist

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-09**
- locations: `grammar/tree-sitter-tsm/grammar.js:1`; `grammar/tree-sitter-tsm/highlights.scm:2`; `editors/vscode-tsm/syntaxes/tsm.tmLanguage.json:5`; `editors/vscode-tsm/src/extension.js:9`; `editors/vscode-tsm/src/extension.js:140`; `docs/editor-design.md:95`; `tools/translate-tsm.mjs:57`; `tools/convert/html2tsm.mjs:68`; `docs/architecture.md:15`; `docs/architecture.md:57`; `docs/architecture.md:175`; `docs/design-decisions-v2.md:93`; `docs/design-decisions-v2.md:278`

The language is described by:
(1) the authoritative C++ front end (linepass.cc, plus inline.cc including splitCells and AstBuilder's info-string parse);
(2) tree-sitter-tsm grammar.js. It is 'deliberately … approximate', and its highlights.scm is 'kept in sync by hand'. The generated parser.c is vendored twice (grammar/tree-sitter-tsm/src and third_party/grammars/tsm) and highlights.scm three times. It serves both ```tsm fences on published pages and VS Code semantic tokens;
(3) the TextMate grammar;
(4) regexes in extension.js for the outline (HEADING ignores containers and fences), folding and label completion. editor-design.md:95 claims these come 'from the tree-sitter parse';
(5) a front-matter regex in preview.js;
(6) translate-tsm.mjs masking and validation regexes;
(7) converters that emit tsm with partial escaping. html2tsm.mjs:68 escapes only $, # and @, not * _ ` [ ^[ %-- or |;
(8) the PackCC PEG that the docs describe as phase 2 (architecture.md:15, 57-61, 175; v2:93, 228, 278-279). There is no engine/grammar directory, no .peg file and no PackCC step in CMake. Only math-design.md:296 admits that the parsers are hand-rolled.
Concrete drifts:
- Comments nest in the engine but not in either tooling regex.
- Fence closers differ. The engine accepts at least N backticks, indented. TextMate requires exactly N at column 0. Tree-sitter accepts any line starting with ```.
- `#{..}` is unknown to both tooling grammars.
- Strict-pair emphasis versus the regex \*[^*\n]+\*.
- Multi-line versus single-line math.
- '@' without the user@domain rule.
- Labels anywhere versus only after headings and math.
- Escapes are ignored by tooling.

*Why ad hoc:* Every consumer re-encodes the syntax by hand, and the docs describe a fourth, non-existent implementation. The tree-sitter approximation is documented as deliberate, which is reasonable for colouring. But it has quietly become the editor's structural model, and nothing tests its drift.

*Proposed generalization (survey):* engine/src/syntax/syntax.def, an X-macro like ops.def, lists every block and inline rule: open/close delimiters, body mode, flags, ctor and highlight capture. Consumers:
(a) the C++ rule tables;
(b) tools/gen-syntax.mjs, which emits the tree-sitter grammar.js tokens and highlights.scm, the TextMate patterns, and a JS escapeTsm(text, ctx) used by every converter and by translate-tsm;
(c) a generated docs table.
Structural editor services move to the engine. tsr_outline(doc) returns JSON (headings with level, label and span; regions; labels; fences) from the skeleton and AST that the preview already computes, so extension.js stops regex-parsing. A conformance test runs tree-sitter and the engine over all fixtures and asserts agreement on construct start/end offsets for every rule both claim to know. The PackCC claims are deleted or re-scoped.

*Verifier:* Verified: there is no engine/grammar dir and no .peg file. architecture.md:57-61 frames PackCC as 'lands with the M2 grammar', and M2 shipped hand-rolled. parser.c exists twice (grammar/tree-sitter-tsm/src, third_party/grammars/tsm). highlights.scm exists three times (plus runtime/assets/hl/tsm.scm). The TextMate fence closer is exactly N backticks at column 0; the tree-sitter fence_delim matches any line starting with ```; neither knows `#{`. editor-design.md:95 contradicts extension.js:9-10,104. Further drift: translate-tsm.mjs:61 keeps lines starting with '//' as raw, although .tsm has no // comments.

*Verifier notes:* Tree-sitter's documented 'approximate' status (grammar.js:1-6) is a reasonable trade-off for colouring only. Generation from a syntax table can only cover delimiters, because strict-pair emphasis and jslex call chains are not regex tokens. The report concedes this. The two highest-value pieces are cheap and respect the dual-target rule: an engine-provided outline/labels export in api/, and a fixture-wide conformance test.

### `parser-frontend/sigil-context-rules` — Sigil context rules are per-feature: '@' has an identifier lookbehind, '#' and '_' do not, and bare-URL autolink is missing

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-33**
- locations: `engine/src/inline/inline.cc:380`; `engine/src/inline/inline.cc:407`; `engine/src/inline/inline.cc:351`; `docs/design-decisions-v2.md:159`; `docs/design-decisions-v2.md:239`

v2 §11.1 gives '@' a 'preceded by an identifier character is literal' rule (inline.cc:407, implemented as a raw-byte check). The '#' sigil has no such guard. Verified: `https://x.org/a#intro` and `C#sharp` compile to `val((intro))` and `val((sharp))`, runtime ReferenceErrors that take the whole document down (no per-block containment). Strict-pair '_' has no intraword guard either: `https://example.com/a_b_c` and snake_case_ids become emphasis (verified). v2 §5 promises 'bare URLs autolink', which would shield URLs, but no autolink rule exists.

*Why ad hoc:* The lookbehind exists only for the feature where a bug was anticipated. The rule is not a property available to every sigil.

*Proposed generalization (survey):* Add rule flags in the inline table: PrevNotIdent (ASCII alnum only, so CJK-adjacent splices keep working) applies uniformly to '#', '@' and '_'. Add an Autolink verbatim rule (scheme '://' up to whitespace or a closing bracket) with higher precedence than markup, desugaring to Call{link}. The deliberate strict-pair decision (v2 §5) is preserved, because the guard only fires between ASCII alphanumerics, which is outside the CJK rationale.

*Verifier:* Verified. Stronger evidence: a committed fixture enshrines the bug. test/fixtures/doc/url-break.tsm:1 contains `…/Metal_movable_type.jpg`, and test/golden/doc/url-break.ast.txt:4-5, .js.txt:2, .semantic.txt:2 and .html.txt:5 all record 'movable' in italics with the underscores deleted, from AST through rendered HTML. No autolink rule exists in inline.cc despite v2 §5:159.

*Verifier notes:* Applying PrevNotIdent uniformly to '#', '@' and '_' (ASCII alnum only) preserves the CJK-first strict-pair rationale (v2 §5:157). Autolink must take precedence over '_' and '#'.

### `parser-frontend/front-matter-editor-only` — SSG front matter is recognised only by the VS Code preview

- kind: adhoc · severity: low · verdict: accurate · plan: **P3-35**
- locations: `editors/vscode-tsm/src/preview.js:182`; `engine/src/linepass/linepass.cc:290`

preview.js blanks a leading `---\n..\n---` block line by line before sending text to the engine. The engine itself would render the '---' lines as thematic rules and the YAML as paragraphs. The Node renderer, export-static and the playground have no equivalent handling.

*Why ad hoc:* A language-level construct is defined in one host.

*Proposed generalization (survey):* A linepass preamble rule, valid only at offset 0: '---' .. '---' becomes Call{ctor:'meta', str:raw}. It is preserved like a comment node (queryable, excluded from output and copy) and optionally exposed to scripts as $.meta. All hosts then agree, and spans stay exact without blanking.

*Verifier:* Only preview.js:179-186 handles front matter; no .tsm file in the repo begins with ---.

*Verifier notes:* Front matter is an SSG convention, not part of the documented .tsm language (App B has no such form), so an editor-side strip is defensible. If hosts must agree, the general mechanism is a host-level 'excluded source prefix' option in the doc config, preserving offsets as the preview's blanking already does. It would be shared by the preview, the Node renderer and export-static. Adding a language-level 'meta' rule is a scope decision the owners should make explicitly, not a fix for ad-hoc code.

### `parser-frontend/editor-region-builder-list` — Editor completion hard-codes a list of region names

- kind: adhoc · severity: low · verdict: accurate · plan: **P1-09**
- locations: `editors/vscode-tsm/src/extension.js:123`; `runtime/src/worker/executor.mjs:127`

Completion offers BUILDERS = ['figure','table','quote','center','right','columns']. Only table and figure are special in the executor; the rest fall back to group{role:name}. Regions registered by users via $.region or #use modules are invisible to completion.

*Why ad hoc:* Knowledge of the extension surface is duplicated in the editor as a literal list.

*Proposed generalization (survey):* The worker reports the region names it encountered plus the registered handler names (both known at execution) in the outline/diagnostics payload, alongside a generated list of built-in constructors from ctors.def. Completion is sourced from that.

*Verifier:* Accurate (extension.js:123). Worse than stated: 'center', 'right' and 'columns' appear nowhere in engine/src or the executor. A grep finds only 'figure' role checks (semantic_html.cc:286, emit.cc:782, resolve.cc:166). The completion list advertises constructs that render as plain groups.

### `parser-frontend/contiguous-escapes-blocks` — contiguous() returns true past the last span, so math, splices and comments escape their block and content is duplicated

- kind: issue · severity: high · verdict: accurate · plan: **P1-06**
- locations: `engine/src/inline/inline.cc:68`; `engine/src/inline/inline.cc:160`; `engine/src/inline/inline.cc:275`

`contiguous(to)` checks only the gaps between consecutive spans of the current block. When `to` lies beyond spans.back().end, the loop exits and it returns true. Verified with tsrc:
- `para one $x\n\nsecond para y$ end` produces math 'x\n\nsecond para y' AND re-renders the second paragraph.
- `> quoted $a\n> b$ end\n\nplain $a\nb$ end` makes a math island start inside the quote and swallow the next paragraph as display math.
- `[price $5](u) and $x$` makes math escape the link-text sub-parser.
- In table cells, `$|x|$` escapes its cell.
- `value #(f(1,\n\n= Heading\n\n2))` places a heading inside a JS expression while also rendering it as a block.
Minimal fix: `if (to > spans.back().end) return false;`. The structural fix is the SpanCursor (adhoc cross-line-raw-scans).

*Verifier:* All five cases reproduced exactly with tsrc --stage=ast. Example: `[price $5](u) and $x$` gives link-internal math '5](u) and' plus a duplicated ' and ' and math 'x'.

*Verifier notes:* This is a bug in the escape hatch, not a separate ad-hoc feature. The minimal fix (return false when `to` > spans.back().end) is sound for single-span sub-parsers and paragraph ends. It does not address the second defect of the same predicate: byte adjacency makes contiguity depend on trailing whitespace and CRLF (see cross-line-raw-scans and the missed item).

### `parser-frontend/inline-comment-leaks-block-structure` — An inline %-- opened mid-line does not hide the block markers it covers

- kind: issue · severity: high · verdict: accurate · plan: **P1-08**
- locations: `engine/src/inline/inline.cc:240`; `engine/src/inline/inline.cc:245`; `engine/src/linepass/linepass.cc:257`

Inline comments are scanned to the end of the whole buffer (hardEnd = all.size()), but linepass knows nothing about them. Lines inside the comment are still classified as headings, list items or fences. Verified on `text %-- start\n= Not a heading\n- not an item\nend --% after`:
- the commented-out heading and list item are rendered;
- ' after' is lost from the first paragraph;
- the last paragraph renders the literal 'end --% after'.
This violates v2 §4.2 ('a comment can comment out any markup').

*Verifier:* Reproduced exactly: the heading and list item render, ' after' is lost, and 'end --% after' renders as text.

*Verifier notes:* The cause is the unguarded comment scan (inline.cc:245,263), which the report misattributes to contiguous() in cross-line-raw-scans. The fix must choose between a phase-1 inline lexer and confining mid-line comments to their block (see that verdict). 'linepass tracks depth' is not sufficient without island awareness.

### `parser-frontend/unterminated-let-swallows-document` — An unterminated top-level #let consumes the rest of the document

- kind: issue · severity: high · verdict: accurate · plan: **P0-04, P1-07**
- locations: `engine/src/linepass/linepass.cc:365`; `engine/src/linepass/linepass.cc:371`; `engine/src/linepass/linepass.cc:377`

For #let, linepass uses s.end even when scanJs fails (linepass.cc:365), and that is EOF for an unclosed bracket. Every remaining line is skipped. Verified: `#let x = f(1` followed by a heading and prose yields a single code-let spanning to EOF. Recovery for `#{` is bounded (`s.ok ? s.end : le`, :377), so the two statement forms recover inconsistently. In both cases the pasted JS then breaks the whole module. A bounded resync policy is needed: stop at the first blank line or block-marker line and emit an Error node.

*Verifier:* Reproduced: `#let x = f(1` followed by a heading and prose gives one code-let @[0,35) to EOF. linepass.cc:365 vs :377 confirms the asymmetric recovery.

*Verifier notes:* A bounded resync (first blank line or block-marker line, then an Error node) is sound. In ToEol mode only an unbalanced bracket can continue past a newline (jslex.h:74-76). The same policy should cover unterminated fences, which also run to EOF (linepass.cc:198-199).

### `parser-frontend/ctor-names-are-reserved-words` — Destructured constructor parameters make 31 common names un-bindable; #let list = .. fails the whole document

- kind: issue · severity: high · verdict: accurate · plan: **P0-05, P2-02**
- locations: `engine/src/codegen/codegen.cc:209`; `engine/src/codegen/codegen.cc:214`

Generated code is `export default async ({__emit, __at, para, text, em, strong, val, m, heading, list, item, quote, codeblock, rule, comment, link, code, seq, ref, term, toc, glossary, notes, note, bibliography, style, mathinline, mathblock, image, __region, __fence}, $) => {..}`, and `#let` compiles to a top-level `let` in the same function scope. Verified with node: `let list = [1,2]` there throws 'SyntaxError: Identifier list has already been declared'. Any document binding text, list, item, code, link, style, image, note or similar fails as a whole. Built-in sugar targets are effectively reserved words, and adding a constructor requires a codegen edit (codegen boundary, but triggered by the parser's #let).

*Verifier:* Verified with node: `let list` inside `async ({list, text}, $) => {...}` gives 'SyntaxError: Identifier 'list' has already been declared'. The parameter list has 31 names (codegen.cc:209-212).

*Verifier notes:* Destructuring is a documented choice (v2 §2:70, 'no prefixes in user code'), but its reserved-name consequence is not discussed. Cheapest general fix, keeping unprefixed names: sugar calls a private namespace (`__c.list`), and user code runs in an inner block where the public names are `const`-destructured, so a user `let list` shadows legally instead of colliding.

### `parser-frontend/trailing-text-after-block-closers-dropped` — Text after --%, } or ; on the closing line of a block construct is silently discarded

- kind: issue · severity: medium · verdict: accurate · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:257`; `engine/src/linepass/linepass.cc:371`; `engine/src/linepass/linepass.cc:383`

Block comments, #{ and #let are consumed by skipping whole lines up to the construct's end offset (`while (lineStart(ln+1) <= end) ln++`), so the rest of the closing line is lost with no diagnostic. Verified: `%-- note --% visible tail text`, `#{ let a = 1 } trailing prose` and `#let b = 2; more prose` all drop the trailing prose.

*Verifier:* Reproduced for all three forms (linepass.cc:259,371,383). For `#let b = 2; more prose`, App A:320 explicitly makes ';' the RHS terminator, so the remainder of the line is by spec ordinary content.

*Verifier notes:* Generalization: the line pass should support re-entering the remainder of a line after a block construct closes mid-line, or at minimum emit a diagnostic. The same 'line remainder' primitive is needed for multi-line content arguments.

### `parser-frontend/fence-double-dedent-in-containers` — Fences inside list items strip the container indentation twice, corrupting code indentation

- kind: issue · severity: medium · verdict: accurate · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:163`; `engine/src/linepass/linepass.cc:194`; `engine/src/inline/inline.cc:584`

fence() receives `col`, the absolute column of the opener (container content column included). Content lines have already had the container prefix stripped by matchPrefixes, yet up to openCol more spaces are stripped again (:194). Verified: in `- item\n  ```py\n  x = 1\n   y\n  ````, the body is 'x = 1\ny' instead of 'x = 1\n y'; Python semantics change. The fence also passes a single body offset (`f->num = lineSpans[0].start`, inline.cc:584) although the body is assembled from non-contiguous dedented lines. Handler offset arithmetic (v2 §4.1 two-tier contract) and token mapping are therefore wrong for every line after the first.

*Verifier:* Reproduced: the body is 'x = 1\ny' instead of 'x = 1\n y'. `col` passed to fence() is absolute (linepass.cc:254), while content lines are already prefix-stripped (linepass.cc:178,194). One body offset (inline.cc:584) is used for non-contiguous lines.

*Verifier notes:* A related, larger defect the report missed: fence() discards matchPrefixes' return value (linepass.cc:178), so fences ignore the container's end entirely (see missed items).

### `parser-frontend/span-fidelity` — Spans are imprecise: container spans cover only the first line, synthesized spaces get stale spans, and Text has no cooked-to-raw map

- kind: issue · severity: medium · verdict: partly · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:85`; `engine/src/linepass/linepass.cc:128`; `engine/src/linepass/linepass.cc:132`; `engine/src/linepass/linepass.cc:284`; `engine/src/inline/inline.cc:98`

Problems:
- Quote, List and Item spans are set from the first line and never extended. Verified: item @[0,6) while its children run to byte 30, and quote @[32,48) for a two-line quote.
- A heading's span excludes its own label (h->span = {pos, e} with e before the label).
- spaceBeforeItem() creates a Text ' ' whose span is the stale bufEnd. Verified: `text @[83,83)` emitted after a splice at @[84,90), so spans are non-monotonic.
- Text spans exclude collapsed spaces and joined line breaks, and the cooked string loses the offset map (escapes, collapsing, joins). Emit therefore stamps the whole Text span on every word (emit.cc:66), and anchors, jump-to-source and diagnostics are run-granular.
- Diagnostics for list or quote content point at the first line only.
This matters for editor jump-to-source and the v2 §8 copy/anchor model.

*Verifier:* Mislocated: emit.cc:66 is the inline-code case. Word blocks take the whole Text span in pushWordBlock (emit.cc:183) and pushCjkChar (emit.cc:322). The other claims were reproduced: item/quote spans cover only the first line (e.g. item @[0,6) with children to 30); the heading span excludes its label; `abc #x #y` yields `text @[3,3)` after a splice at @[4,6), so spans are non-monotonic; `text @[0,1) str="x "` has a span shorter than its cooked string.

### `parser-frontend/no-error-nodes` — There is no Error AST node; block-granular recovery is claimed but not implemented

- kind: issue · severity: medium · verdict: accurate · plan: **P2-02, P2-12**
- locations: `engine/src/ast/ast.h:6`; `engine/src/codegen/codegen.cc:214`; `engine/src/codegen/codegen.h:6`; `docs/document-model.md:299`

The parser has no error node. Failures become literal text plus a diagnostic, are silently dropped, or are pasted into JS. The docs claim otherwise:
- document-model §10 says 'parse-inline … block became error node';
- v2 §11 (line 228) says 'PEG's silent backtracking is bounded by block-granular recovery';
- v2 §2 says execution is wrapped per top-level block.
Codegen emits no per-block try/catch (codegen.cc:214-229), and JsProgram carries no source map (codegen.h:6-8, contrary to v2:73 and architecture.md:112). Consequently every parser leniency that emits invalid JS takes down the entire document: #if, unmatched #name! closers, #let content literals, URL fragments as splices, nested #let.

*Verifier:* Verified: there is no Error AstKind (ast.h:6-9). There is no try/catch around program blocks in executor.mjs (only fence/bib/import sites at :156,245,292-308). JsProgram is text only (codegen.h). The doc claims are at document-model.md:296 (parse-inline 'block became error node'), design-decisions-v2.md:73 and :228.

### `parser-frontend/docs-drift` — Front-end documentation describes components and features that do not exist

- kind: issue · severity: medium · verdict: accurate · plan: **P1-09**
- locations: `docs/architecture.md:15`; `docs/architecture.md:55`; `docs/design-decisions-v2.md:93`; `docs/design-decisions-v2.md:159`; `docs/verbatim-design.md:102`; `engine/src/api/doc.h:109`

Drifts found:
- PackCC (architecture.md:15, 57-61, 175; v2:93, 228, 278-279): no .peg files, no grammar dir, no build step.
- architecture.md:55 places per-line provenance in linepass; it is actually in AstBuilder (inline.cc:601-627).
- source/ is said to contain a 'SourceMap builder'; source.h has none.
- Spec'd but unimplemented: bare-URL autolink (v2 §5), App B rule 4 multi-line content dedent, the `$ .. $` line form (App B), Appendix A keyword forms and #let content literals, and the m tag (documented as deferred only in verbatim-design.md:3-6 and executor comments).
- verbatim-design.md:102 names the sidecar group role 'sidecar'; doc.h:109 interns 'sidecar-lines'.
- editor-design.md:95 says outline, folding and completion come from tree-sitter; extension.js uses regexes.

*Verifier:* Verified, including source.h having no SourceMap builder and doc.h:109 'sidecar-lines' vs verbatim-design.md:102 'sidecar'. Additional drift: document-model.md:135 still describes the resolver unwrap as term/collect-only, while resolve.cc:486-492 says it was generalized to every non-inline kind.

### `parser-frontend/ast-dump-missing-note` — dumpAst has no Note case, and the committed goldens enshrine blank lines

- kind: issue · severity: low · verdict: partly · plan: **P0-02**
- locations: `engine/src/inline/inline.cc:664`; `test/golden/notes/basic.ast.txt:6`; `test/golden/notes/basic.ast.txt:16`

dumpNode's switch over AstKind lacks Note, so a note prints as indentation plus a newline, with no header or span. test/golden/notes/{basic,explicit,cjk-glue}.ast.txt record this broken output as the byte-exact contract (document-model §12). No -Wswitch-enum gate caught it. This is a symptom of per-kind switches (adhoc per-feature-ast-kinds).

*Verifier:* The missing case and the enshrined blank lines are accurate (test/golden/notes/basic.ast.txt:6,16). However, 'No -Wswitch-enum gate caught it' is misleading. engine/CMakeLists.txt:27 builds with -Wall -Wextra, and -Wall's -Wswitch already warns for this switch, which has no default arm. The warning was not fatal.

*Verifier notes:* The fix is -Werror=switch plus removing `default:` arms in codegen.cc:198 and fragment.cc:228, so all three AST switches are compiler-checked. This gets most of the safety without restructuring the AST.

### `parser-frontend/missed:0` — Line contiguity is byte adjacency of whitespace-trimmed spans, so CRLF and trailing spaces change the grammar

- kind: missed · severity: high · verdict: verifier-found · plan: **P0-04, P1-06**
- locations: `engine/src/inline/inline.cc:68`; `engine/src/inline/inline.cc:70`; `engine/src/linepass/linepass.cc:147`; `engine/src/source/source.h:24`

contiguous() requires `spans[k+1].start == spans[k].end + 1`. addParaLine trims trailing ' ', '\t' and '\r' from each line span, and SourceText::lineEnd excludes only '\n'. As a result, any CRLF document and any line with a trailing space are 'non-contiguous'. Verified: `a $x +\ny$ b` gives math 'x +\ny', but `a $x + \ny$ b` (one trailing space) and the CRLF version give literal text. `v #(f(1,\r\n2)) w` gives 'error splice-js unbalanced #(...)'. Whether a construct may cross a line therefore depends on invisible bytes and on the platform's line-ending convention.

*Proposed generalization (survey):* Define line joins structurally. Normalize line terminators once in SourceText (treat \r\n as the terminator, with offsets preserved). Let the SpanCursor join a block's logical lines with a virtual '\n' regardless of trimmed whitespace; 'may cross' then means 'same block, no container prefix in between', which is a property of the skeleton, not of byte gaps.

### `parser-frontend/missed:1` — Bracket-delimited constructs are pre-matched by island-unaware byte scanners, violating 'verbatim islands first'

- kind: missed · severity: high · verdict: verifier-found · plan: **P1-06**
- locations: `engine/src/inline/inline.cc:114`; `engine/src/inline/inline.cc:204`; `engine/src/inline/inline.cc:332`; `engine/src/inline/inline.cc:387`; `engine/src/inline/inline.cc:410`; `engine/src/inline/inline.cc:477`; `docs/design-decisions-v2.md:158`

Content args, link text, footnotes, @[..] and splitCells' arg loop all find their closing ']' by scanning raw bytes before the body is parsed. None skips inline code or math islands; splitCells skips code spans but not math. Verified: `#f[code `a]b` here]` closes the argument inside the code span, leaving 'b` here] y' as text. `[range $[0,1)$](u)` is not a link and leaves '](u)' as text. `^[arr `x[0]]` q]` closes inside the code span. v2 §5 says islands are carved out before everything else.

*Proposed generalization (survey):* Stop pre-matching. Treat '[' as a delimiter frame in the inline parser, like the existing emphasis Frame stack. Islands, escapes, splices and comments are consumed normally inside the frame, and at the top-level ']' the parser decides link (needs '(' next), content arg (frame opened by a splice), note ('^' opener), ref ('@' opener) or literal. Alternatively, run one shared inline lexer that emits island tokens first. Either one subsumes all five matchers and splitCells' masking.

### `parser-frontend/missed:2` — Fenced blocks ignore the container stack: a fence in a quote or list item runs past the container's end

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:178`; `engine/src/linepass/linepass.cc:174`; `engine/src/linepass/linepass.cc:198`; `docs/design-decisions-v2.md:357`

fence() calls matchPrefixes for each body line but discards its return value. A line that no longer carries the quote's '>' or the item's indentation is still consumed as fence content, and an unclosed fence swallows the rest of the document. Verified: `> ```\n> code\nnot quoted\n= Heading\n\nmore` yields quote > codeblock with body 'code\nnot quoted\n= Heading\n\nmore\n' and 'unterminated fence'. This contradicts App B's no-lazy-continuation determinism and block-granular containment. blockComment() likewise scans raw `all` with no container awareness.

*Proposed generalization (survey):* Run verbatim block rules under the same per-line container protocol as every other block (the proposed ContainerRule): if matchPrefixes matches fewer containers than were open at the opener, the verbatim block ends there with a bounded 'unterminated fence' diagnostic, which is CommonMark's semantics. One resync policy (stop at container end, else EOF) should then serve fences, comments and #let/#{.

### `parser-frontend/missed:3` — Trailing content-argument desugaring is JS text surgery on a raw byte offset

- kind: missed · severity: medium · verdict: verifier-found · plan: **P2-02, P2-06**
- locations: `engine/src/ast/ast.h:56`; `engine/src/inline/inline.cc:184`; `engine/src/codegen/codegen.cc:73`

The AST records only lastCallStart, the byte offset of the final '('. codegen then splices source text: prefix + '(' + inner + ', ' + content args, with a whitespace-only 'innerEmpty' test. Valid JS argument lists break it. Verified: `#f(a,)[c]` gives `f(a,, c)`, and `#g(/* c */)[d]` gives `g(/* c */, d)`. Both are SyntaxErrors that fail the whole module, with no diagnostic.

*Proposed generalization (survey):* Desugar structurally, not textually. `f(<inner>)[c]` becomes `f(...[<inner>], c)`: an array literal accepts every valid argument list, including trailing commas, comments and spreads, so no comma reasoning is needed. Alternatively, jslex can report the top-level arg structure (empty modulo comments, trailing comma). Either way the AST should carry the call's argument span, not a single offset.

### `parser-frontend/missed:4` — Inline code uses a one-off delimiter rule; fences use run-length delimiters

- kind: missed · severity: low · verdict: verifier-found · plan: **P1-06**
- locations: `engine/src/inline/inline.cc:314`; `engine/src/linepass/linepass.cc:165`; `engine/src/linepass/linepass.cc:187`

Fences open with N≥3 backticks and close with ≥N (linepass.cc:165-187). Inline code spans close at the first '`' on the same line, have no escapes and cannot cross lines, although math islands can cross lines. A backtick therefore cannot appear in inline code at all. Verified: `x ``a`b`` y` gives code(''), text 'a', code('b'), code(' y and ')..., and "`\``" fails too.

*Proposed generalization (survey):* Use one 'delimiter-run verbatim' primitive (opener run length N, closer of exactly N, optional single-space padding strip) shared by fences and inline code, and possibly by $$-style math if it is ever wanted. Use the same CrossLines policy for every inline verbatim island.

### `parser-frontend/missed:5` — No paragraph-interruption policy: any line starting with a block marker splits prose

- kind: missed · severity: low · verdict: verifier-found · plan: **P1-07**
- locations: `engine/src/linepass/linepass.cc:246`; `engine/src/linepass/linepass.cc:101`; `engine/src/linepass/linepass.cc:262`; `docs/design-decisions-v2.md:357`

tryStarters and the leaf cascade run on every non-blank line regardless of an open paragraph. A hard-wrapped prose line that happens to begin with `N. `, `- `, `+ `, `> `, `= `, `---` or `#!x` silently restructures the document. Verified: `born in\n1984. Then more` gives para 'born in' plus an ordered list with start=1984. The converters and the translation harness re-wrap lines, so they can trigger this. The design docs specify no interruption rule; CommonMark restricts interruption to e.g. `1.` only.

*Proposed generalization (survey):* Make 'can interrupt an open paragraph' an explicit per-rule property in the proposed BlockRule table (for example: headings, fences, regions and quotes interrupt; ordered items only with start 1; '+'/'-' only when non-empty). Document it next to App B rule 3, and have converters and translate-tsm escape line-initial markers through the shared escapeTsm.

## codegen-ops-model — Codegen, ops contract, content-tree model, style system, JS executor

<details><summary>Design summary (as audited)</summary>

Codegen (engine/src/codegen/codegen.cc) lowers the AST into one ES module, `export default async ({__emit, __at, para, text, …, __region, __fence}, $) => {…}`. Markup turns into constructor calls, and every node is wrapped in `__at(node, s, e)` to attach its source span. `#{…}` and `#let` are copied in verbatim as JS. Fences go through a runtime dispatcher `__fence(tag, args, body, offset)`. Regions become `__region(name, args, rows)`, where each paragraph is pre-split into rows and cells as nested JS arrays.

The executor (runtime/src/worker/executor.mjs) builds the constructor object over an OpBuf (opbuf.mjs). Each constructor appends `MAKE_TEXT` or `MAKE_NODE(kind, args, childIds)` to a flat binary buffer: a "TSOP" magic, a version byte, a string table, then varint ops. It returns a JS "shadow" `{kind, args, children, opId, span}` so user code can walk and regroup content. Reusing a value gives a DAG of shared ids. `EMIT`, `STYLE_PUSH(bits, patch)` and `STYLE_POP_TO` form the schedule, and `SPAN` attaches spans after the fact. After the program runs, the executor loads and formats bibliographies itself and emits them at the end of the document.

The vocabulary comes from engine/src/ops/ops.def, an X-macro with 6 opcodes, 28 kinds and 31 argument keys in one global key namespace. tools/gen-ops-ts.mjs regex-generates the matching JS ids. The C++ `decodeOps` (ops.cc) checks the version with exact equality, range-checks string, node and child ids, and caps argument and patch counts. It does not validate which arguments a kind may carry.

`instantiate` (model.cc) walks the schedule. It folds the style stack plus the deltas of `styled` nodes along the path into a value-interned `Styling {bits u64, sizeMul, fontFamily, lang, color, sizePx}` held in a StyleTable. It copies the DAG once per emission, so styles bind at emission time. The resulting ContentTree is then changed in place by an ingest pass (sidecar extraction in doc.h) and by the resolver, which handles labels, counters, refs, collectors, footnote lifting and term rewriting. After that it goes to emit.

Semantic roles travel as `group{role:"…"}` strings. Kind-level default presentation (bold headings, mono code, link and superscript bits) is added in the resolver and emit, outside the style system. Styling is split across two encodings (the stack and styled nodes) and a fixed set of class bits, some of which serve as hooks for single features (SUP, CODE, LINK).


Strengths:

- The value-DAG-plus-schedule encoding expresses emission-time style binding structurally, and instantiate's per-emission copy (model.cc:36-54) implements it in ~20 lines; tested by re-emitting a shared value under different scopes.
- ops.def is a real single source for C++ enums and JS ids (X-macro + generator); decodeOps is defensive about truncation, string/node id ranges and child-before-parent ordering, and decodes in place to avoid SSO dangling views (ops.h:66-70 comment).
- Styles are interned by value into StyleIds with deterministic first-seen id assignment; blocks and runs carry ids, never copies.
- Shadow nodes let constructors traverse and regroup content without querying WASM mid-execution — the table builder regroups existing opIds without re-encoding (executor.mjs:76-93).
- Fence dispatch is registration-agnostic at codegen time (runtime lookup, executor.mjs:142-161): user handlers and the default codeblock path are on equal footing, async handlers work, and a throwing handler is contained into an error node.
- Emit lowers roles into presentational FlowUnit flags (centered, cells, sidebarW), so layout and the typeset renderer stay role-free — a clean boundary worth preserving.
- Bibliography formatting is an overridable JS function and the default is exposed through the `$.bib.format` getter, so users can wrap rather than replace it.
- Every stage is dumpable (tsrc --stage=js|ops|tree|blocks…), which made all the findings below verifiable from recorded behaviour rather than inference.

</details>


### `codegen-ops-model/ctor-signatures-break-sugar-equivalence` — Built-in constructor signatures are shaped by codegen, so the documented sugar⇔constructor equivalence fails

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-03**
- locations: `runtime/src/worker/executor.mjs:171-172`; `runtime/src/worker/executor.mjs:190-191`; `runtime/src/worker/executor.mjs:219-220`; `runtime/src/worker/executor.mjs:196-217`; `engine/src/codegen/codegen.cc:118-124`; `engine/src/codegen/codegen.cc:130-134`; `docs/design-decisions-v2.md:107`

`= T` compiles to `heading(1, null, ...kids)`, `- a` to `list(false, 1, item(...))`, a display-math paragraph with a label to `mathblock(src, label)`. The positional label/ordered/start parameters are codegen's private calling convention exposed as the public signature. Verified with tsrc+executor: `#heading(2)[Title via ctor]` (exactly the form v2 §4 quotes) yields `MAKE_NODE heading level=2 label=%0 children=[]` — the title becomes a Node-tagged label arg, the heading is empty, the resolver numbers it `h-0.1` and the TOC entry is "0.1 ". `#list(item(…), …)` likewise lands an item shadow in `ordered`. Other ctors use unrelated conventions: `image(src, opts)` (renames `float`→`side`), `codeblock(lang, body, opts)` (maps `lineNo:true`→1), `style(patch, ...kids)`, `link(url, ...kids)`, `term(name, ...desc)`.

*Why ad hoc:* The governing principle (design-decisions-v2.md:107) says every syntax form is sugar for a constructor and cites `heading(1)[…]` and `list(item(…))`; instead each ctor got whatever positional shape its codegen site needed, so there is no uniform convention and the sugar is not reproducible by a user call.

*Proposed generalization (survey):* One calling convention for every content constructor: `name(attrs?, ...children)`, where `attrs` is an optional plain (non-shadow) object validated against the per-kind attribute schema (see global-argk-namespace). Codegen emits `heading({level:1, label:"x"}, …)`, `list({ordered:false, start:1}, …)`, `mathblock({src, label})`. Positional convenience (`#heading(2)[…]`, `#link("u")[…]`) is defined once, as an arity adapter in a generated constructor manifest: each kind declares a `primary` attribute (heading→level, link→url, image→src) that a leading number/string binds to. The manifest (ctor name → kind, primary attr, attr schema) is generated from ops.def and consumed by codegen's prologue, the executor, and editor completions, so codegen and users always hit the same function.

*Verifier:* Reproduced in scratchpad/vcg/a1.tsm. `#heading(2)[Title via ctor]` gives `MAKE_NODE heading level=2 label=%2 children=[]`, and `#list(item(text("a")), item(text("b")))` gives `list ordered=%6 start=%8 children=[]`. The 'h-0.1' / TOC '0.1' numbering only appears when no level-1 heading precedes the call; with one, the label is h-1.1. Signatures confirmed at executor.mjs:171-172, 190-191, 196-206, 214-220 and codegen.cc:99-105, 118-124, 130-134.

*Verifier notes:* A single `(attrs?, ...children)` convention (the hyperscript shape) is sound. The proposed primary-attr adapter contradicts itself, though. cost_risk limits it to number/boolean primaries, yet the example `#link("u")[…]` has a string primary, and toShadow turns bare strings into text children (executor.mjs:61), so `link("u")` cannot be told apart from `link("text")` at runtime.
Fix: resolve positional convenience in codegen, where `(…)` args and `[…]` content args are already syntactically separate (codegen.cc:73-84 splits them for `f(a)[c]`). Pass content args through a branded wrapper, or as a separate trailing array, so constructors never have to guess.
Telling an attrs object from a child also requires branded shadows (see val-coercion). No invariant conflict; every JS golden changes once.

### `codegen-ops-model/private-region-builders-and-missing-ctors` — Tables, figures, groups, raw and error nodes are reachable only through private helpers; built-ins cannot be delegated to

- kind: adhoc · severity: high · verdict: partly · plan: **P2-03**
- locations: `runtime/src/worker/executor.mjs:76-139`; `runtime/src/worker/executor.mjs:141-161`; `runtime/src/worker/executor.mjs:162-237`; `runtime/src/worker/executor.mjs:267-285`; `engine/src/codegen/codegen.cc:209-212`; `docs/document-model.md:66`

The prologue exposes `__emit, __at, __region, __fence` beside the public ctors. There is no `group`, `table`/`trow`/`tcell`, `raw`, `error`, generic `collect`, generic `styled`, or `figure` ctor; `tableBuild`/`figureBuild`/`regionJoin` are closures in buildContext. The only way to build a table or a numbered figure from code is the private `__region("table", {cols}, [[[…]]])` with codegen's private array encoding (verified: works, spans [0,0)). `raw`/`error` exist only on the fence ctx. `$.region('table', fn)` overrides the builtin (handler lookup precedes the `name === 'table'` branch, :126-128) with no way to call the default. Extension contexts are inconsistent: fence handlers get `(body, {args, offset, m, error, raw})`, region handlers `(args, children)` with no ctx, bib formatters `(entry, ctors)` — the full ctor object including the `__` helpers — while document code only sees a destructured subset and cannot name the ctor object to pass to libraries. Each new ctor edits the hard-coded destructure string in codegen.cc and churns all 48 test/golden/*/*.js.txt (git log of test/golden/cjk/basic.js.txt: 8 such churns).

*Why ad hoc:* Built-ins are special branches (`name === 'table'`, `name === 'figure'`) rather than entries in the namespace user handlers use; document-model.md:66 says custom constructs are built from group/styled/raw, none of which is callable from document scope. The extension surface is strictly weaker than the built-in one.

*Proposed generalization (survey):* One constructor namespace `__c` built from the manifest: every kind has a public ctor (`group`, `table`, `row`, `cell`, `raw`, `error`, `collect`, `styled`, plus generic `node(kind, attrs, ...kids)`), and region builders are ordinary ctors (`table`, `figure`). `#!name(args) … #name!` → `__region(name, args, prov)` = `($.regions.get(name) ?? __c[name] ?? defaultRegion(name))(args, prov, rctx)`; `$.region(name, fn)` registers with `rctx.next` bound to the previous binding so overrides can delegate (`(a, p, r) => wrap(r.next(a, p))`). One extension context type `ExtCtx {ctors, args, offset, span, m, error(msg, localOffset), load(src)}` is passed to fence, region and formatter hooks alike. Codegen emits `async (__c, $) => { const {<generated list>} = __c; … }` and `$.ctors = __c` exposes the namespace to `#use` modules.

*Verifier:* test/golden/cjk/basic.js.txt has 8 commits, but one is its creation (6118f9a). 7 rewrote the prologue: ab078dc, 8d5b0a9, 85312ee, da45d9d, 217b3b7, 57340a8, 59f56e3. Everything else checks out:
- the prologue (codegen.cc:209-212) has no group, table, raw, error, collect or figure;
- handler lookup precedes the builtins (executor.mjs:124-128);
- fence ctx is built at :148-155;
- region handlers are called as `h(args, children)` (:126);
- the bib formatter is called as `fmt(e, ctors)` with the full ctor object (:258).

*Verifier notes:* The report misses that the hook asymmetry is behavioural as well as a matter of signatures. Region handlers run synchronously and nothing contains them: codegen emits `__region(` without await (codegen.cc:158) but `await __fence(` (:148).
Reproduced (scratchpad/vcg/b5.tsm, b6.tsm): an async region handler renders the text "[object Promise]", and a throwing region handler aborts the whole document ("EXEC ERROR: boom"). A throwing fence handler, by contrast, becomes `error{code:"fence-error"}`.
The unified ExtCtx must therefore also unify how hooks are invoked: await, containment and content coercion.
Exposing group/styled/raw keeps a documented promise (document-model.md:66) rather than adding scope. `rctx.next` delegation is sound, and document-order registration is preserved.

### `codegen-ops-model/region-meta-args-hijack` — __region treats font/lang/color/sizePx as style meta-args for every region, including user handlers

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-03, P2-08**
- locations: `runtime/src/worker/executor.mjs:131-137`; `runtime/src/worker/executor.mjs:225-229`; `runtime/src/worker/executor.mjs:275-281`; `engine/src/ops/ops.def:74-75`; `docs/document-model.md:83`

After any region returns (built-in or user handler), `__region` wraps the node in `styled{font, lang, color, sizePx}` if any of those keys is present in its args. Verified: `#!code(lang: "rust")` yields `styled lang="rust"` around `group{role:"code"}`, and every run renders with `lang="rust"` as a BCP-47 attribute. A handler cannot opt out; bold/italic/decorations are accepted by `style()` but not here. The same four-key list is written out three times in the executor.

*Why ad hoc:* Region argument space and style-property space share one flat namespace, separated only by a hard-coded list of four names. The documented reuse of ARGK `lang` (ops.def:74) leaks up into the authoring surface.

*Proposed generalization (survey):* Reserve one presentation meta-arg, `#!aside(style: {font: …, lang: …})`, handled by the default region pipeline through the style-property registry (see fixed-styling-fields). `__region` splits off `args.style`, validates it with the generated `styleFromObject`, and wraps the result. Handlers receive args without it, plus `rctx.applyStyle(node)` if they want to apply it themselves. The three key lists collapse into one generated function.

*Verifier:* Reproduced in scratchpad/vcg/a2.tsm: `#!code(lang:"rust")` produces `styled lang="rust"` around `group role="code"`, and every run carries `[base lang=rust]`. A side effect the report does not mention: `__at` attaches the region span to the styled wrapper, so the group itself is spanless (@[0,0), visible in b4.tsm for aside, table and figure). The surface is documented (document-model.md:83: 'the region wraps itself in a styled node'), so it is deliberate, not accidental.

*Verifier notes:* The behaviour is documented, but the doc gives no rationale for colliding with region arguments. `lang` is exactly the key that ops.def:74-75 already overloads at the wire level. A single reserved `style:` meta-arg, checked by the same validator as style() and $.style.push, is the minimal general fix and leaves handler argument space clean.

### `codegen-ops-model/parse-time-pipe-segmentation` — Table '|' segmentation happens at parse/codegen time for every region and is lossily re-joined for non-tables

- kind: adhoc · severity: medium · verdict: partly · plan: **P2-11**
- locations: `engine/src/codegen/codegen.cc:157-193`; `runtime/src/worker/executor.mjs:94-113`; `engine/src/ast/ast.h:9`; `docs/design-decisions-v2.md:145`

Codegen emits every region paragraph as nested JS arrays (rows of cells, from AstKind::Row/Cell), whatever the region name. `regionJoin` rebuilds non-table paragraphs by inserting synthetic `" | "` text nodes. Verified: inside `#!aside`, `ls|wc -l and a||b` becomes `ls | wc -l and a |  | b`, and the separators carry no span ([0,0)), so copy text and anchors drift from the source. Handlers must know the implicit encoding (Array = paragraph; nested arrays = rows/cells).

*Why ad hoc:* v2 §4.1 (design-decisions-v2.md:145) says '|'-splitting is the table constructor's convention, not a region mechanism, and that the table splits at tree level. The implementation instead moved table syntax into the parser and codegen, and every region pays for it.

*Proposed generalization (survey):* Make region provenance lossless. Codegen passes each region child as a `seq` per source line (span = line span) with its inline runs untouched. Top-level unescaped '|' become source-anchored text nodes with a schema attribute (`text{sep:true}`, or a tiny inline `sep` kind). A `prov` object (`prov.lines()`, `prov.paragraphs()`, `prov.split(isSep)`) is the "small query API" v2 §4.1 promised. `ctors.table` uses `prov.split`; other regions flatten lines into paragraphs and keep the original text, separators included.

*Verifier:* The behaviour is confirmed (scratchpad/vcg/a2.tsm): inside #!aside, `ls|wc -l and a||b` becomes `ls | wc -l and a |  | b`, the synthetic separators are @[0,0), and region paragraphs are @[0,0). But the report presents this as accidental, and it is a documented as-built deviation:
- document-model.md:184-190: 'Region provenance is materialized at codegen … Non-tabular regions rejoin the segmentation (" | ")';
- architecture.md:222: "codegen-materialized '|' segmentation provenance".
The report cites only v2 §4.1.

*Verifier notes:* The as-built docs record the deviation but give no reason for leaving v2 §4.1's tree-level split. The rejoin also loses information: whitespace is normalised, leading and trailing empty cells are dropped (document-model.md:188), and spans are lost. The proposal (marked separators, untouched runs, per-line spans, a prov query API) is the original v2 design plus a marker. It is consistent with v2's embedding-proof argument, because code spans and splices are already distinct nodes at tree level.

### `codegen-ops-model/role-string-dispatch` — group{role:"…"} strings are an engine-private protocol spanning executor, ingest, resolver, emit and the semantic renderer

- kind: adhoc · severity: high · verdict: partly · plan: **P2-05**
- locations: `runtime/src/worker/executor.mjs:120`; `runtime/src/worker/executor.mjs:130`; `runtime/src/worker/executor.mjs:260`; `engine/src/api/doc.h:107-110`; `engine/src/resolve/resolve.cc:166-178`; `engine/src/resolve/resolve.cc:365`; `engine/src/resolve/resolve.cc:402`; `engine/src/resolve/resolve.cc:447`; `engine/src/emit/emit.cc:19`; `engine/src/emit/emit.cc:471-476`; `engine/src/emit/emit.cc:585-589`; `engine/src/emit/emit.cc:777-818`; `engine/src/render/semantic_html.cc:285-311`; `docs/document-model.md:63-64`

Where role dispatch starts and how far it travels. Producers: the executor writes role `figure`, `<region name>` and `bibentry`; doc.h writes `sidecar-lines`; the resolver writes `notes`, `bibliography` and `term`. Consumers: the resolver reads `figure` (figNo counter, label registration, bold "图 n：" caption prefix). Emit reads `figure` (float form; figDepth gives centred, unhyphenated captions) and `sidecar-lines` (sidecar column). The semantic renderer reads `figure` (<figure>/<figcaption>) and prints any other role as data-role. Layout and the typeset renderer are role-free. Verified consequence: `#!aside(label:"a1")` followed by `@a1` renders "??", because only figure groups register labels (resolve.cc:166-170), although document-model.md:63 lists group as labelable. User region names share this namespace with engine protocol roles, so `#!notes` or an override of `figure` silently gains or loses engine behaviour.

*Why ad hoc:* Role is documented as a convention ("Figure is a convention, not a kind", document-model.md:64), but in practice it is a set of hard-coded string comparisons across four layers and two languages. It is a public tag and a smuggling channel at once (sidecar-lines and bibentry are internal handoffs).

*Proposed generalization (survey):* A role/trait registry owned by model/: `struct RoleInfo { StrRef name; CounterId counter; bool labelable; RefForm refForm; CaptionMode caption; SemanticTag htmlTag; bool floatable; ClassSet cls; }`. The stdlib entries (figure, aside, term, notes, bibliography) are data. Documents extend it with `$.role(name, {counter, label, semantic, caption})`, emitted as a name-keyed header record. Resolver, emit and the semantic renderer ask `roleInfo(node)` for traits and never compare names. Engine-internal handoffs stop using roles: the sidecar becomes a node-valued `codeblock.margin` attribute, and bib entries become typed `entry{key}` children of `collect`.

*Verifier:* Producers and consumers confirmed: resolve.cc:166, 365, 402, 447; emit.cc:19, 587-588, 781-782; semantic_html.cc:285-311; doc.h:107-109. The `#!aside(label:"a1")` → @a1 renders '??' claim is confirmed (resolve.cc:166-170); scratchpad/vcg/b4.tsm also shows an unlabelled `#!table` giving '??' for @t1.
Overstated: nothing in the engine or runtime reads role "notes". Only resolve.cc:365 writes it, and grep finds no reader, so '#!notes silently gains engine behaviour' has no support. Overriding `figure` does lose engine behaviour.
Also missed: emit sets pendingAnchor for any labelled group (emit.cc:779-780). The anchor therefore exists while the ref shows '??', so the two layers disagree about whether a group is labelable.

*Verifier notes:* A trait registry carried as ordered declarations is deterministic and consistent with 'execution declares, resolver decides'.
Two refinements:
1. Labelability should come from per-kind data. group is labelable per document-model.md:63, whatever its role; roles should only add counter and supplement traits.
2. The role protocol leaks beyond the engine. runtime/src/main/shell.mjs:46, 146 and 175 key the footnote popup on `.tsr-sup` and the `#tsr-fn-` / `#tsr-fnref-` label prefixes. The registry should emit a role or class attribute into the DOM for consumers to use instead.

### `codegen-ops-model/sidecar-ingest-pass` — Code-block sidecars are a feature-specific ingest pass with its own lowering and a private role

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-13**
- locations: `engine/src/api/doc.h:69-81`; `engine/src/api/doc.h:85-150`; `engine/src/emit/emit.cc:579-601`; `engine/src/inline/fragment.cc:98-111`

`Doc::ingest` runs `extractSidecars` between instantiate and resolve. For any codeblock with a `sidecar` marker argument it rewrites the body text, parses the comment parts with the engine's inline grammar (parseInlineFragment; splices stay literal with an Info diag), and appends a synthetic `group{role:"sidecar-lines"}` with one `seq` per line. Emit recognises that group by string comparison to build the margin column.

*Why ad hoc:* A feature-shaped tree pass in the API layer, a second AST→content lowering, and a role string used as an internal handoff. It exists because the planned `m.parse` re-entry was never built, so the split could not live in the `codeblock` ctor.

*Proposed generalization (survey):* (1) Node-valued attributes as a general mechanism: the schema declares `ATTR(codeblock, margin, NODE)`, and instantiate copies node-valued attributes as subtrees, once per emission. (2) Do the split in the `codeblock` ctor through the general fragment re-entry `m.parse(str, {offset})`, backed by `tsr_parse_fragment` returning an ops slice (architecture.md §2.5). Margin content then flows through ctors and ops like all other content, splices inside margin notes work, and emit reads the schema slot instead of a role string.

*Verifier:* Confirmed at doc.h:69-81 (ingest order), doc.h:85-150 (extractSidecars), emit.cc:579-601 and fragment.cc:98-111. One extra limitation the report does not mention: extraction only runs when the codeblock has exactly one text child (doc.h:91-92). Structured-body codeblocks (the CH1 array form, which fence handlers produce) therefore can never have sidecars.

*Verifier notes:* Moving the split into the codeblock constructor through m.parse is right. The WASM module is available during execution, because compile runs before it in the same worker or Node process.
Node-valued attributes, however, add a second kind of edge that every generic walker must learn: Resolver::scan/rewrite, excerptInto, scanTokenReqs/scanImageReqs, dumpTree and the semantic renderer. If any of them misses it, refs and labels inside margin notes silently stay unresolved.
Use schema-declared child slots instead: tag each codeblock child as body or margin in the kind's content model, or use a typed `margin` child kind. Margin content then stays in `kids`, and every generic pass sees it with no special handling.

### `codegen-ops-model/bibliography-placeholder-and-end-emission` — Bibliography returns an empty placeholder, loads through an executor-private channel, and always emits at document end

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-07, P2-14**
- locations: `runtime/src/worker/executor.mjs:40-56`; `runtime/src/worker/executor.mjs:185-188`; `runtime/src/worker/executor.mjs:238-266`; `engine/src/resolve/resolve.cc:195-203`; `engine/src/resolve/resolve.cc:397-427`; `engine/src/resolve/resolve.cc:476-486`; `docs/notes-design.md:136-154`

`bibliography(src)` records a request and returns `text('')`. After the program has run, the executor reads or fetches the CSL-JSON itself — a fourth resource channel beside the engine's NEED_MEASURE/TOKENS/IMAGES pulls. It formats entries with `$.bib.format` and emits `collect{what:"bibliography", form}` at document END, with children `group{role:"bibentry", name:key}`. The resolver's empty-paragraph deletion exists only to remove the placeholder paragraph, and the position the author chose is discarded. Footnotes carry a parallel placement special case (`notesPlaced`, the implicit append in resolveDoc).

*Why ad hoc:* This is a documented as-built deviation (notes-design.md:136-154). Its rationale is that the executor already runs async JS with host access. That rationale justifies loading in JS; it does not justify fixed placement, placeholder cleanup, or a private executor→resolver encoding.

*Proposed generalization (survey):* (a) Async content values: the document function is already async and codegen already awaits fences. Let `val`/ctors accept thenables (codegen emits `await` for splice values; the executor awaits in program order), so `#bibliography("refs.json")` becomes an async ctor that returns its collector in place. A generic `ctx.load(src)` helper serves fences, regions, bibliography and `#use`. (b) Resolver phase order: expand collectors in a third pass, after every ref has been rewritten. Citation order, notes and the TOC are then complete wherever the collector sits, which removes `notesPlaced`, the end-of-document append and the empty-paragraph hack. Entries become schema-typed children (`entry{key}`), not role-tagged groups.

*Verifier:* Confirmed:
- executor.mjs:185-188 (placeholder `text('')`);
- :242-266 (load, format and emit after the program);
- resolve.cc:476-486 (emptyPara deletion, whose comment names the placeholder);
- resolve.cc:519-528 (implicit notes append).
Minor: emptyPara deletes every paragraph made only of empty text nodes, not just the placeholder.

*Verifier notes:* The documented rationale (notes-design.md:136-154) only justifies loading the data in JS, as the report says.
In (a), 'costs a microtask only where a value is a thenable' is wrong. Codegen cannot know statically which splices return thenables, so it has to `await` every splice, and awaiting a non-thenable still costs a microtask. The cost is negligible but not zero. EMIT order stays deterministic.
(b) is needed on its own merits. Collectors are expanded during the same rewrite pass that assigns citation ordinals, so a bibliography placed before its citations would see an incomplete citeOrder. Today's forced end placement only hides this.

### `codegen-ops-model/cls-sup-feature-bit` — CLS_SUP: a frozen class bit allocated for footnote markers that bundles raise, size and a line-breaking rule

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-08**
- locations: `engine/src/model/model.h:20-22`; `engine/src/resolve/resolve.cc:342-351`; `engine/src/emit/emit.cc:85-90`; `engine/src/render/typeset_html.cc:39`; `engine/src/render/semantic_html.cc:64-66`; `engine/src/model/model.cc:119-127`; `runtime/src/worker/executor.mjs:6-11`

Bit 19 was allocated for footnote markers (notes-design §1). Only the resolver sets it (`bits |= CLS_SUP; sizeMul *= 0.7f`). `styleBits` has no `sup`/`sub` key, so users can get a superscript only through the raw-number `$.style.push(1<<19)` backdoor (verified). In emit, this presentational bit drives a line-breaking rule: a ref whose first block is SUP forces `breakPenalty=INF` on the preceding block. The semantic renderer maps it to `<sup>` ahead of strong/em, so a bold superscript loses its bold. The golden tree dump omits the bit (verified: the marker prints as `[basex0.70]`).

*Why ad hoc:* A feature hook encoded as a frozen style bit. Three unrelated behaviours (paint-only raise, size, no-break-before) are welded into one bit named after one feature.

*Proposed generalization (survey):* Split it into general primitives: (1) a registry style property `baselineShift` (em; paint-only, affectsMetrics=false), used with sizeMul, exposed as `style({shift, size})` and as stdlib `sup`/`sub` ctors; (2) a general inline no-break attribute (`glue: "before"|"after"` on any inline kind, or a `nobreak` inline kind with U+2060 semantics) that emit reads in place of the SUP test; (3) footnote markers become `ref{glue:"before"}` with the class tag `fn-marker`, styled by a default rule. `<sup>` then comes from the class→semantic-tag map and can combine with strong/em.

*Verifier:* Confirmed, including that <sup> takes precedence over <strong> in the semantic renderer (semantic_html.cc:63-65) and that the tree dump omits the bit (scratchpad/vcg/a3.tsm prints the marker as `[basex0.70]`). The as-built section of notes-design.md documents CLS_SUP × 0.7 and the INF penalty.

*Verifier notes:* This is a documented as-built choice whose only rationale is the footnote marker. Splitting it into a paint-only baselineShift, a general no-break attribute and a class is compatible with the §7 contract as long as the raise stays paint-only; shell.mjs:46 already uses position:relative.
notes-design.md is internally inconsistent here: its Measurement section says 'raised 0.35em via vertical-align', while the as-built section says position:relative. An affectsMetrics=false registry flag would make this contract checkable instead of prose.

### `codegen-ops-model/kind-default-styles-in-emit` — Per-kind default presentation (heading bold/size, code mono/scale, link bit) is hard-coded in emit, downstream of every user style mechanism

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-01**
- locations: `engine/src/emit/emit.cc:48-55`; `engine/src/emit/emit.cc:56-69`; `engine/src/emit/emit.cc:71-79`; `engine/src/emit/emit.cc:94-106`; `engine/src/emit/emit.cc:497-503`; `engine/src/emit/emit.cc:551`; `engine/src/emit/emit.cc:624`; `engine/src/measure/measure.h:60-71`; `engine/src/render/typeset_html.cc:33-40`

Emit turns kind semantics into style bits after styles have been resolved: link/ref add CLS_LINK; code, codeblock and error add CLS_CODE and compose `cfg.codeScale`; heading adds CLS_BOLD and `headingSizeMul(level)`. CLS_CODE then selects the mono family in describeStyle and `.tsr-code` in the renderer. CLS_LINK is never rendered (only dumped), but it still splits StyleIds and therefore metric entries. A user cannot ask for a non-bold heading, a link colour or a different code size, because this composition happens after every user-reachable style mechanism.

*Why ad hoc:* Default presentation per kind is code in the emitter rather than style data, and CODE and LINK are semantic markers posing as style classes.

*Proposed generalization (survey):* A style-rule table applied at instantiation: `struct StyleRule { Selector sel; StyleDelta delta; }`, with `Selector = {Kind kind; StrRef role; StrRef cls; AttrPred pred /* level==1 */}`. The defaults (heading{level=n} → bold + size scale[n]; code → family mono, size 0.85, class code; link → class link) are stdlib data. Users append rules with `$.rules.set(sel, delta)`, which is Typst `set` layered on the stack primitive exactly as v2 §12 anticipates (design-decisions-v2.md:252). copy() folds: effective = schedule ∘ matching rules (in rule order) ∘ styled path. `show` rules (structural transforms) run in the executor as JS functions keyed by kind/role and are applied by `__emit` to shadows before EMIT, which keeps execution single-pass and deterministic.

*Verifier:* Confirmed at emit.cc:48-55, 56-69, 71-90, 94-106, 497-503, 551 and 624, measure.h:60-71, and typeset_html.cc:33-40. CLS_LINK has no rendering consumer; it appears only in dumps (model.cc:124, emit.cc:1086). Minor: headingSizeMul is a hard-coded function with magic constants (config.h:82-84), not a Config knob as the subsumes list implies.

*Verifier notes:* Rules as data is the right direction, and v2 §12 explicitly leaves room for set-rule sugar over the stack (design-decisions-v2.md:252).
But deltas are monotone today: bits are OR-ed and applyPatch only sets values. The report's own registry keeps `u64 flags` with OR semantics, so a user rule could add bold but never remove it, and the motivating 'non-bold heading' still could not be expressed. Weight, italic and decorations must become nearest-wins tri-state properties (inherit/on/off) before rules can override kind defaults.
Rules applied at instantiation also miss nodes the resolver creates later, unless the resolver calls the exported fold (the report's restyle()).

### `codegen-ops-model/resolver-fabricated-styles` — Resolver fabricates presentation with fresh Styling literals that discard the inherited scope

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-01, P3-03**
- locations: `engine/src/resolve/resolve.cc:173-177`; `engine/src/resolve/resolve.cc:450-452`; `engine/src/resolve/resolve.cc:342-351`; `engine/src/resolve/resolve.cc:352-357`; `engine/src/resolve/resolve.cc:371-386`

Synthesized text uses `styles.idOf(Styling{CLS_BOLD, 1.0f})`. Verified: inside `#!figure(color:"red", sizePx:24)` the caption prefix "图 1：" is `[BOLD]`, black and base-size, while the caption itself is red at 24px; a term name under `#style({color:"blue"})` loses its colour the same way. The footnote marker multiplies sizeMul by 0.7, and note bodies are recursively `rescale`d by 0.85. The positional aggregate `Styling{CLS_BOLD, 1.0f}` silently depends on field order.

*Why ad hoc:* The resolver writes presentation directly, as bits and multipliers, instead of creating semantic content under the parent's style. That bypasses the emission-time binding rule every other node follows (document-model §3).

*Proposed generalization (survey):* The resolver creates semantic nodes (for example a caption-label `styled{class:"caption-label"}`) and styles them with the same pure fold instantiate uses, exported from model/: `restyle(node, parentStyleId) = fold(parent, rules(node), delta(node))`. The 0.7 and 0.85 sizes become default style rules on class/role (`fn-marker`, role `notes`). No resolver code touches Styling fields.

*Verifier:* Only two of the four cited sites discard the inherited scope: the figure caption prefix (resolve.cc:173-176) and the term name (resolve.cc:450-451), both of which use `Styling{CLS_BOLD, 1.0f}`.
The footnote marker composes on the note's own inherited style (resolve.cc:343-346: `Styling s = styles.get(note->style); s.bits |= CLS_SUP; s.sizeMul *= 0.7f;`, with a comment saying so). Note bodies rescale their own styles (resolve.cc:352-357, 383-385).
The figure case is reproduced in scratchpad/vcg/a3.tsm: the prefix is `[BOLD]` while the caption is `[base color=red size=24px]`.

*Verifier notes:* Exporting instantiate's fold as a pure function over StyleTable is cheap and deterministic. The marker and notes sites are not scope bugs, but they still hard-code presentation (0.7 and 0.85), which style rules as data would absorb.

### `codegen-ops-model/token-class-as-color` — Highlight token classes are smuggled through a colour string, and emit/layout recover semantics by string equality

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-01**
- locations: `engine/src/code/tokens.cc:40-50`; `engine/src/emit/emit.cc:618-624`; `engine/src/emit/emit.h:79`; `engine/src/layout/layout.cc:177-180`; `docs/document-model.md:80`; `docs/design-decisions-v2.md:276`

Token tags are folded into styles as `color = "var(--tsr-tok-<tag>)"`, and `tag == 3` (comment) also adds CLS_EM by magic index. Emit then compares each run's colour StrRef with the interned string "var(--tsr-tok-comment)" to set CodeRun.isComment, which layout uses for comment-aware hanging indents. A user run coloured with that var string is treated as a comment, while a fence handler that produces comment runs in any other colour loses the behaviour.

*Why ad hoc:* A semantic class rides on a paint property's value. The open mechanism that was designed for this (`dynClasses: sorted u32[]`, document-model.md:80; "dynamic class ids ≥ 256", design-decisions-v2.md:276) was never implemented.

*Proposed generalization (survey):* Implement class tags in Styling as a registry property `classes`, an interned sorted small set of StrRef (dynamic class ids). Tokens fold `class:"tok-comment"`. The theme becomes style rules on classes (`.tok-comment → color var(--tsr-tok-comment), italic`). Emit and layout test `hasClass(style, kTokComment)` using a declared class id. Users get `style({class:"…"})` and rules on classes; renderers emit `tsr-c-<class>`.

*Verifier:* Confirmed at tokens.cc:44-46 (`var(--tsr-tok-<tag>)`, with `tag == 3` adding CLS_EM), emit.cc:618-621 (comparison against the comment colour StrRef), emit.h:79 and layout.cc:180.

*Verifier notes:* The proposal is the mechanism the docs already specify but never built: dynClasses (document-model.md:80) and dynamic class ids ≥256 (design-decisions-v2.md:276). This completes the documented design rather than adding scope. Paint-only classes must stay out of the metric key.

### `codegen-ops-model/fixed-styling-fields` — Styling is a fixed struct; every property is hand-coded in ~10 places across two languages, and derived values already drift

- kind: adhoc · severity: high · verdict: partly · plan: **P1-02**
- locations: `engine/src/model/model.h:25-74`; `engine/src/model/model.cc:14-34`; `engine/src/model/model.cc:109-145`; `engine/src/measure/measure.h:60-71`; `engine/src/render/typeset_html.cc:44-72`; `engine/src/render/semantic_html.cc:62-111`; `engine/src/emit/emit.cc:251`; `engine/src/emit/emit.cc:478`; `engine/src/emit/emit.cc:958`; `runtime/src/worker/executor.mjs:6-11`; `runtime/src/worker/executor.mjs:131-137`; `runtime/src/worker/executor.mjs:225-229`; `runtime/src/worker/executor.mjs:275-281`; `runtime/src/shared/opbuf.mjs:78-90`; `docs/document-model.md:81-82`

Styling = {bits, sizeMul, fontFamily, lang, color, sizePx}. Each field is spelled out in operator==, Hash, applyPatch, styleStr, describeStyle, styleInto, the semantic serializer and three executor key lists. Adding lang (ops v2) and decorations (v3) each meant an ops bump and edits at all of these sites. Derived values have drifted: emit's `fontPx`, the punctuation half-em and the paragraph indent use `cfg.baseSizePx * sizeMul` and ignore `sizePx`, while measurement and rendering honour it. Values are never validated: strings are pasted into CSS, so `color: "red; letter-spacing: 9px; display: block"` reaches the run's style attribute (verified). That defeats the deliberate exclusion of letterSpacingPx (document-model.md:82). `sizePx: NaN` is accepted.

*Why ad hoc:* Each property is a bespoke field with bespoke code in every layer. The model doc itself declares that extending the set is "a model version bump" (document-model.md:81).

*Proposed generalization (survey):* A style-property registry owned by model/: `STYLE_PROP(id, name, ValueKind, inherits, affectsMetrics, cssName)`, with ValueKind ∈ {Flag, Number(range), Length, Color, FontList, LangTag, ClassSet, Enum}. `Styling = { u64 flags /* registry-allocated presentational flags */; SmallVec<pair<PropId, Value>> props /* sorted */ }`, interned by a hash over canonical bytes. Values are canonicalised and validated at patch time: colours parsed to #rrggbbaa or a whitelisted `var(--tsr-*)`, font lists tokenised and re-serialised, numbers range-checked; anything invalid becomes a `style-value` diag and is dropped. One set of derived accessors: `FontDesc fontOf(Styling, Config)` for measure, emit fontPx, punct/indent maths and renderers, and `cssOf(Styling)` generated from cssName. On the JS side a generated `STYLE_PROPS` table drives `style()`, `$.style.push` and region meta-args. On the wire, patches are keyed by property name, so a new property needs no ops bump.

*Verifier:* Wrong: 'Adding lang (ops v2) and decorations (v3) each meant an ops bump.'
- ARGK lang existed before v2; 85312ee only added font, color and sizePx.
- Decorations needed no ops change because they use existing bit space. v3's bump (e04c0db) was for the codeblock args wrap, lineNo and hl.
Everything else is confirmed:
- per-field code at model.h:36-39 and 58-70, model.cc:14-34 and 109-145, measure.h:60-71, typeset_html.cc:44-72, semantic_html.cc:62-90;
- three JS key lists at executor.mjs:135, 228 and 279;
- emit size drift at emit.cc:251, 478 and 958.

*Verifier notes:* A registry with canonicalised values is sound and deterministic. It needs three changes:
1. Styling.bits mixes author-facing flags with emit-internal script classification (LATIN, CJK, PUNCT_*, SPACE; document-model.md:73-77), and describeStyle picks the CJK font from CLS_CJK. Today `$.style.push(2)` lets an author set CLS_CJK. The registry must keep classification out of author-writable space.
2. Flags need override semantics (see kind-default-styles).
3. Every built-in uses relative size (sizeMul: headings, code, notes, marker), but authors can only set absolute sizePx. Relative size is a built-in-only capability that the registry should expose.
The semantic renderer drifts as well: it emits font-size from sizePx and ignores sizeMul (semantic_html.cc:76-80).

### `codegen-ops-model/global-argk-namespace` — One flat, overloaded ARGK namespace with no per-kind attribute schema or validation

- kind: adhoc · severity: high · verdict: accurate · plan: **P0-06**
- locations: `engine/src/ops/ops.def:43-75`; `engine/src/ops/ops.cc:67-90`; `engine/src/ops/ops.cc:129-151`; `runtime/src/shared/opbuf.mjs:53-66`; `engine/src/resolve/resolve.cc:7-15`; `docs/document-model.md:32-61`

ARGK is a single global id space whose keys carry several meanings. `lang` is both the codeblock tag and the BCP-47 style tag (documented at ops.def:74). `name` serves as term name, bibentry key, mathblock "(n)" display and note ordinal. `src` is both an image URL and math source; `w`/`h` are image intrinsic dims and the raw node's declared box; `label` is a definition label on heading/group/para but an anchor on ref (fnref-n). `body` (5) is never read or written (dead), and `code` (13) is written on error nodes but never read. `readArg` casts `(ArgK)rd.varint()` without a range check, accepts any key with any tag on any kind, and allows Node tags that nothing consumes. Consumers silently skip wrong tags (`if (a.key == K && a.tag == T)`), so `heading("2")` quietly becomes level 1. The kind table in document-model §2.1 has the level/args/children columns ops.def lacks, and is stale (no seq, image or note).

*Why ad hoc:* Keys were appended per feature with no per-kind schema. Validation and meaning live inside each consumer's loop.

*Proposed generalization (survey):* Declare the schema in ops.def: `KIND(heading, 2, BLOCK, KIDS_INLINE, LABELABLE)`, `ATTR(heading, level, NUM, REQUIRED)`, `ATTR(heading, label, STR, OPTIONAL)`, `ATTR(codeblock, margin, NODE, OPTIONAL)`. Generate C++ `constexpr KindInfo kKinds[]` (level, content model, labelable, attribute span) and `AttrSpec kAttrs[]`, plus a JS `SCHEMA` for ctor validation and editor completion. Wire keys become per-kind attribute indices (or names; see the next item), so `codeblock.lang` and `style.lang` are distinct by construction. decodeOps validates: an unknown attribute, wrong tag or missing required one produces an `ops-arg` diag and the attribute is dropped, without rejecting the buffer. Consumers use typed accessors such as `attrNum(n, A::heading_level)`, and node-typed attributes are instantiated as subtrees.

*Verifier:* Confirmed:
- ArgK::body has 0 engine references and the executor never writes it;
- ArgK::code has 0 engine references and is written only at executor.mjs:147 and 250;
- readArg casts with no range check (`(ArgK)rd.varint()`, ops.cc:68). This truncates u64 to u16, so key 65536 aliases `label`;
- `heading("2")` becomes `level="2"`, which the resolver treats as level 1 (scratchpad/vcg/a3.tsm).
One more overload the report does not list: `form` means the ref form in the docs (document-model.md:57, 126) but bibliography 'all' in code (resolve.cc:404-406).

*Verifier notes:* Sound. The schema should also carry value domains (integer ranges, enums), not just tags. A tag check passes `level: 1e300`, which reaches undefined-behaviour casts; see the missed item on numeric values.

### `codegen-ops-model/version-bump-per-vocabulary` — Every new kind or argument key is a protocol version bump; the version and kind count are duplicated by hand

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-01**
- locations: `engine/src/ops/ops.cc:100`; `engine/src/ops/ops.h:7`; `engine/src/ops/ops.h:28`; `engine/src/ops/ops.def:1-5`; `tools/gen-ops-ts.mjs:14`; `runtime/src/shared/opbuf.mjs:46`; `git:85312ee,e04c0db,0c2ef30,217b3b7,57340a8 (ops.def history)`

The header version is checked for exact equality. History: v2 added font/color/sizePx and the patch payload in a byte reserved since v1; v3 wrap/lineNo/hl; v4 sidecar; v5 the image kind plus scale/alt/side; v6 the note kind. That is five bumps between 25 and 28 August, all append-only: every v(n) buffer is a valid v(n+1) buffer. Each bump nevertheless re-recorded every fixture .ops, byte-identical apart from the version byte. OPS_VERSION exists twice: `constexpr u8 OPS_VERSION = 6` in ops.h and a `// OPS_VERSION(6)` comment in ops.def that gen-ops-ts reads with a regex. KIND_COUNT=28 is maintained by hand, and forgetting to bump it makes the decoder reject the new kind. opbuf.mjs hard-codes `kind: 17 /* text */`.

*Why ad hoc:* The contract has no concept of an open vocabulary, so any feature that needs a new noun or attribute becomes a protocol event. The churn is evidence that the extension points live in the wrong layer.

*Proposed generalization (survey):* Separate the codec from the vocabulary. (1) The version byte covers only codec structure (op encodings, header layout), declared as an `OPS_CODEC(n)` directive rather than a comment; KIND_COUNT is generated by X-macro counting and opbuf uses `KIND.text`. (2) The header carries a vocabulary section listing, as string-table refs, the kind names and (kind, attribute) names the buffer uses. The reader maps them through the registry from global-argk-namespace: an unknown kind becomes an `error` node plus a diag, as document-model.md:118 already promises; an unknown attribute becomes a diag and is dropped. Style-property names, role and counter records use the same mechanism. Additive features then never bump the version, and recorded .ops stay valid.

*Verifier:* Confirmed: five append-only bumps between 85312ee (25 Aug) and 57340a8 (28 Aug). Checking test/fixtures/cjk/basic.ops across e04c0db, 0c2ef30 and 57340a8, each re-record differs by exactly one byte: offset 5, the version. OPS_VERSION is duplicated at ops.h:7 and ops.def:5, KIND_COUNT=28 is maintained by hand (ops.h:28), and opbuf.mjs:46 hard-codes 17.

*Verifier notes:* Exact-match versioning is a documented rule (ops.def:3; architecture.md:127, 'mismatch is a hard error'). Its real job is catching JS/WASM skew, since writer and reader ship together (cf. 6b58255, content-hash versioning).
The proposed name-keyed vocabulary header changes a stated invariant and adds a decode-time name map, mostly to avoid fixture churn. A lighter fix keeps the discipline:
- annotate each ops.def entry with `since=n`;
- have the reader accept MIN_COMPAT ≤ v ≤ OPS_VERSION and reject any kind or key whose `since` is newer than the buffer's version, so a new writer against an old reader still fails hard;
- generate KIND_COUNT and use KIND.text.
Name-keyed header records are only warranted once user-declared registries (roles, counters) must travel in the buffer. Add them then, as data records.

### `codegen-ops-model/two-style-encodings-and-stack` — StyleDelta has two wire encodings; the schedule stack is only sound at top level and accepts raw class bits

- kind: adhoc · severity: medium · verdict: partly · plan: **P0-06, P2-08**
- locations: `engine/src/ops/ops.cc:158-177`; `runtime/src/shared/opbuf.mjs:49-71`; `runtime/src/shared/opbuf.mjs:78-95`; `engine/src/model/model.cc:58-100`; `runtime/src/worker/executor.mjs:63-64`; `runtime/src/worker/executor.mjs:274-284`; `docs/design-decisions-v2.md:248`; `docs/architecture.md:132`

The same delta is encoded two ways. STYLE_PUSH carries bits as a varint and a patch count as a raw byte (≤16), allows only Num/Str values, and the JS writer silently drops unknown keys. `styled` node args carry bits as an f64 `bits` arg under ARGK keys, and unknown keys throw. Markup sugar compiles to styled nodes, contrary to v2 §12 (design-decisions-v2.md:248); architecture.md:132 records the change. So the stack now serves only `$.style.push/popTo`, and it is unsound outside top-level statements. Pushes write schedule ops in JS evaluation order, so `Mid #($.style.push({color:"red"})) text` colours the whole paragraph, "Mid " included (verified). Nothing enforces document-model §4.3's "only valid at top level". `$.style.push(number)` lets users push any raw bit, such as CODE, SUP or CJK (verified CODE+SUP). JS composes bits with 32-bit `|`, so the u64 frozen bit space is effectively 31 bits on the authoring side.

*Why ad hoc:* Two parallel mechanisms for one concept, each with its own codec limits and validation policy.

*Proposed generalization (survey):* Make the styled delta the only encoding and drop STYLE_PUSH/POP_TO from the wire. The executor keeps the stack in JS, and `__emit(n)` wraps the emitted root in `styled(fold(stack))` whenever the stack is non-empty. Emission-time binding then holds by construction, because the wrapper is created at emit. The schedule collapses to EMIT. Deltas are registry patches keyed by name (fixed-styling-fields); raw-number pushes are removed. Codegen brackets each top-level block with `__block(span, fn)`, so a style push during value construction raises a `style-in-value` diag instead of retroactively restyling.

*Verifier:* Confirmed:
- two encodings (ops.cc:158-177 versus MAKE_NODE args);
- the JS writer drops unknown patch keys (opbuf.mjs:82-83);
- retroactive colouring (scratchpad/vcg/a2.tsm: 'Mid ' is `[base color=red]`, and the push's return value also splices a literal "undefined" text);
- raw-bit push (bits=0x80040 on the wire, a3.tsm).
Overstated:
(a) The decoder accepts Null, Bool, Num and Str patch values (readArg with allowNode=false, ops.cc:71-79). Only the JS writer restricts them to Num/Str, turning booleans into strings.
(b) 'Unknown keys throw' for styled nodes cannot be reached from user code. style() and $.style.push both whitelist font/lang/color/sizePx and silently drop anything else (executor.mjs:225-229, 278-280), so users see the same policy on both paths.
(c) The 31-bit limit applies only to styleBits' `|`. `$.style.push(number)` and the f64 bits on styled nodes carry up to 2^53.

*Verifier notes:* The split is deliberate and documented (architecture.md:132; v2 §12, 'the primitive itself is the commitment').
Wrapping each emitted root in `styled(fold(stack))` keeps emission-time binding, but it makes styled-as-structural-node worse. An inline-level styled wrapper around block content already breaks dispatch (missed item: `#style(...)[#codeblock(...)]` renders as plain words), and the resolver and sidecar code match exact child shapes (resolve.cc:490, doc.h:91). The scheme therefore requires styled nodes to be dissolved after instantiation.
The wrapper alone does not fix the retroactive case either, because the push runs during paragraph construction, before __emit. The proposed __block diagnostic is what fixes that.
A cheaper unification: one StyleDelta codec shared by STYLE_PUSH and styled args.

### `codegen-ops-model/val-coercion-adhoc` — Content coercion is ad hoc: functions are auto-called, null/arrays stringify, and node-ness is duck-typed

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-01**
- locations: `runtime/src/worker/executor.mjs:59-62`; `runtime/src/worker/executor.mjs:230`; `runtime/src/shared/opbuf.mjs:64`; `git:fddc417`

`toShadow` calls any function value with no arguments. This was added in fddc417 so that a bare `#toc` or `#glossary` would work, but it means `#f` silently invokes any user function `f()`. `null` and `undefined` splice as the literal text "null" and "undefined". Arrays are stringified: `#(["a", em(text("b"))])` produces "a,[object Object]" (verified). Node-ness is tested with `'opId' in x`, so any object with an `opId` field, for example from JSON data, is taken as a node reference, and shadows can be mutated after their op has been written.

*Why ad hoc:* A one-feature fix (#toc without parentheses) was implemented as a global coercion rule; there is no content-coercion protocol.

*Proposed generalization (survey):* One `toContent(x)` protocol, used by `val`, every ctor's children, handler returns and bib formatters. Branded shadows (a module-private Symbol or WeakSet; frozen objects) map to themselves. string/number/bigint become text. null, undefined and false become an empty seq. Arrays and iterables become a flattened seq. Objects implementing `[Symbol.for('tsm.content')]()` are called, which gives users a hook for content-like classes. A function is called only if it is marked as a nullary ctor (`NULLARY` on toc, glossary, notes, rule); any other function becomes an `error` node with a `splice-function` diag. Anything else becomes `text(String(x))` with an info diag.

*Verifier:* Confirmed in scratchpad/vcg/a2.tsm: `#(["a", em(text("b"))])` gives "a,[object Object]" and leaves an orphan styled node in the buffer; null and undefined splice as literal text. fddc417 introduced the call-through. The same stringification hits the m tag: `m` + "`see ${v}`" gives "see [object Object]" (executor.mjs:232-235; b8.tsm).

*Verifier notes:* Sound. The nullary marker should be public (for example the same well-known Symbol) so user-defined nullary constructors work like toc and glossary. toContent should also apply to fence, region and bib-formatter results.

### `codegen-ops-model/block-promotion-peepholes` — Block/inline level is decided by peepholes and hand lists in codegen, resolver and emit that disagree

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-11**
- locations: `engine/src/codegen/codegen.cc:95-111`; `engine/src/codegen/codegen.cc:198-200`; `engine/src/resolve/resolve.cc:26-35`; `engine/src/resolve/resolve.cc:476-492`; `engine/src/emit/emit.cc:170-172`; `engine/src/emit/emit.cc:685-700`

Codegen turns a paragraph containing exactly one display formula into `mathblock` (an AST-shape peephole). The resolver unwraps a paragraph whose only child is missing from its own `isInlineKind` list. The lists disagree: `raw` is inline for the resolver and for document-model §2.1, but emit handles raw only in blockWalk, so a spliced raw node is dropped silently (verified: `#(saved)` renders an empty paragraph of height 0). The unwrap also discards the paragraph's span, which is the splice span, so every block-level splice (#toc, #codeblock(...), #heading(...)) ends up at @[0,0) (verified). For any unhandled AST kind, codegen's default case silently emits `text("")`.

*Why ad hoc:* Kind level is not data, so each layer keeps its own list and its own peephole.

*Proposed generalization (survey):* Use KindInfo.level from the schema (global-argk-namespace) and a single promotion rule in instantiate: a para whose children are exactly one BLOCK-level node becomes that node and inherits the para's span when its own is empty. Kinds with level BOTH (raw, error) are inline boxes in inline context and units at block level, and emit dispatches on level rather than a hand-written list. The math peephole is either expressed through that same rule (display mathinline = BOTH) or kept and documented as sugar. Codegen's default case becomes an `error` node plus a `codegen-unhandled` diag.

*Verifier:* Confirmed:
- raw inside a para is dropped (scratchpad/vcg/a5.tsm: pid 1 gets an empty unit and layout h=0su);
- the unwrap loses the span (a1.tsm: heading @[0,0));
- codegen's default case emits `text("")` (codegen.cc:198-200).

*Verifier notes:* The proposed rule only goes one way. The reverse case is worse and not mentioned: inline content at block level is silently dropped. emit.cc:822-824 walks the kids of a top-level text node, so nothing is emitted. A fence handler that returns text, em(...) or the result of ctx.m therefore renders an empty pid (b7.tsm).
Inline wrappers also defeat the unwrap. resolve.cc:490-491 treats `styled` as inline, so `#style({…})[#codeblock(…)]` renders as plain inline words (b4.tsm).
Normalisation therefore has to be bidirectional, with an anonymous para around inline runs at block positions, and delta-only wrappers must be transparent when deciding level. A 'BOTH' level for raw implies inline raw boxes in emit, which is a new feature; promoting or emitting a diagnostic is the simpler path.

### `codegen-ops-model/duplicate-lowering-fragment` — A second AST→content lowering (fragment.cc) bypasses ctors and ops; the m tag returns cooked text; text projection is duplicated

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-13**
- locations: `engine/src/inline/fragment.cc:34-93`; `engine/src/codegen/codegen.cc:26-217`; `runtime/src/worker/executor.mjs:68-71`; `runtime/src/worker/executor.mjs:232-236`; `engine/src/resolve/resolve.cc:17-24`

There are two lowering paths. One is codegen → ctors → ops → instantiate. The other is fragment.cc's `Conv` (used for sidecars), which folds `*`/`_` straight into bits without a styled node, flattens notes (`^[x]` in a margin note inlines its body through the default branch), and leaves splices literal. Meanwhile the `m` tag returns cooked text with no markup parsing, although fragment.cc can parse markup. Plain-text projection is duplicated as well: `shadowText` in JS (term names) and `excerptInto` in C++ (labels, TOC), with different comment handling.

*Why ad hoc:* The planned single re-entry (`tsr_parse_fragment` returning ops, architecture.md §2.5; `m.parse` in v2 §2) was replaced by a C++-only shortcut built for one feature.

*Proposed generalization (survey):* A single lowering: `tsr_parse_fragment(str, offset)` returns an ops slice produced by the same codegen-to-ctor mapping (codegen in fragment mode, executed against the live OpBuf, with ids rebased as document-model §4.1 describes). `m`, `m.parse`, fence handlers and the codeblock sidecar all use it, and fragment.cc is deleted. Text projection becomes one engine function, mirrored in JS as `c.plain(x)` from the same spec.

*Verifier:* Confirmed:
- fragment.cc:46-49 folds `*`/`_` into bits without a styled node;
- AstKind::Note falls through to the default branch (fragment.cc:87-89);
- splices stay literal (:79-86);
- the m tag returns one cooked text node (executor.mjs:232-236);
- shadowText includes comment text, because comment() creates a text child (executor.mjs:208), while excerptInto skips comments (resolve.cc:18).

*Verifier notes:* Sound. For m.parse, interpolated values should become opaque placeholder slots in the fragment parse and be mapped back to their shadows, never re-lexed as markup. This is the same embedding-proof argument v2 §4.1 makes for tree-level splitting.

### `codegen-ops-model/note-kind-and-lift` — Footnotes needed a dedicated kind (ops v6) and a bespoke resolver path for 'lift body, leave numbered marker'

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-01, P3-13**
- locations: `engine/src/ops/ops.def:41`; `runtime/src/worker/executor.mjs:177-180`; `engine/src/resolve/resolve.cc:61-67`; `engine/src/resolve/resolve.cc:205-213`; `engine/src/resolve/resolve.cc:338-392`; `engine/src/resolve/resolve.cc:505-513`; `engine/src/resolve/resolve.cc:519-528`; `docs/notes-design.md:76-96`

Kind `note` exists only to mean "move my body into a collector and leave a numbered marker". The resolver hard-codes the rest: buildMarker, buildNotes, the `notes` vector, `notesPlaced`, the `fn-n`/`fnref-n` labels, the ↩ glyph, the 0.85× and 0.7× sizes, the rule + ordered-list section shape, and implicit placement at document end in resolveDoc.

*Why ad hoc:* A kind and a resolver path for one feature. Endnotes, margin notes, per-chapter notes, todo lists and list-of-figures entries all need the same shape.

*Proposed generalization (survey):* TeX-style inserts. A kind `insert{class, label?}` (inline, kids = body) plus `collect{what:"inserts", class}`. The resolver treats them generically: gather inserts per class in document order, number them with the counter declared for the class (role/counter registry), replace each with `ref{target:<class>-n, glue:"before", class:"<class>-marker"}`, and build the section from a declarative template (anchored list items with back-links) or from a JS formatter registered like `$.bib.format`. `^[…]` desugars to the stdlib ctor `footnote(…)` = `insert({class:"footnote"}, …)`.

*Verifier:* Confirmed; documented in the as-built section of notes-design.md. One coupling not listed: the main-thread shell hard-codes the label scheme (runtime/src/main/shell.mjs:146 `a[href^="#tsr-fnref-"]`, :175 `a.tsr-sup[href^="#tsr-fn-"]`).

*Verifier notes:* Class-tagged inserts are the right generalization (TeX \insert), and they also fit the designed bottom-of-sheet placement. The shell popup should key on a class or role attribute emitted for the insert class rather than on label prefixes.

### `codegen-ops-model/collector-switch-and-fixed-counters` — Collectors are a closed `what` string switch and counters are fixed C++ ints; users can declare neither

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-07**
- locations: `engine/src/resolve/resolve.cc:58-68`; `engine/src/resolve/resolve.cc:157-163`; `engine/src/resolve/resolve.cc:277-291`; `engine/src/resolve/resolve.cc:429-440`; `runtime/src/worker/executor.mjs:176-189`; `docs/document-model.md:124-130`

`collect{what}` is dispatched by a closed string switch over toc, glossary, notes and bibliography. `lof` is documented but absent, and anything else yields a `collect-unknown` warning and an empty group. Counters are hard-wired ints (tableNo, figNo, eqNo, noteNo, secc), although document-model §5 promises counter classes `figure|equation|footnote|<user>` with reset rules from config; none of that exists. Ref display text is a switch on Kind with per-kind config supplements (supHeading, supTable, supFigure, supEquation).

*Why ad hoc:* Each numbering or collection feature is its own resolver code path. Because "execution declares, the resolver decides" (v2 §11.1), users have no route to equivalent features, contrary to the goal of user extensions on equal footing.

*Proposed generalization (survey):* Declarative registries consumed by a generic resolver. `CounterSpec {name, resetAt: none|heading(n), format: decimal|roman|circled|alpha, supplement}` and `CollectorSpec {name, query:{kinds, roles, counter, class}, template: nestedList|section, item: "{number} {text}"}`. The built-ins (section, figure, table, equation, footnote; toc, lof, lot, glossary, notes, bibliography) are stdlib entries; documents add more with `$.counter(name, spec)` and `$.collector(name, spec)`, written as name-keyed header records. A label snapshots every counter bound to its node's kind or role, and refs are formatted through the counter's supplement and format. Single-pass soundness holds because specs are data and scripts still cannot read resolved values.

*Verifier:* Confirmed at resolve.cc:58-68, 157-163, 277-291 and 431-440. lof is promised at document-model.md:127 and user counter classes at :124.

*Verifier notes:* Declarative CounterSpec/CollectorSpec is the only shape compatible with 'scripts cannot read resolved values' (design-decisions-v2.md:238). A JS formatter called back from the C++ resolver would break single-pass execution. Keep templates structural (fields and forms), not a string mini-language, to avoid a third language.

### `codegen-ops-model/occurrence-spans-unsound` — Spans are node-level (SPAN op) while content is a DAG copied per emission; spliced and user-built content is span-less

- kind: issue · severity: high · verdict: accurate · plan: **P2-04**
- locations: `engine/src/ops/ops.cc:179-185`; `runtime/src/shared/opbuf.mjs:96-103`; `runtime/src/worker/executor.mjs:166`; `engine/src/codegen/codegen.cc:67-93`; `engine/src/model/model.cc:36-54`; `docs/document-model.md:26`

`__at` writes `SPAN id s e` against the shared node, and the last write wins in decodeOps (ops.cc:179-185). Verified: a fence handler that returns one cached node, used twice, gives both emissions the second fence's span @[89,101); the first fence's anchor is lost. Inline `val(...)` splices are never wrapped in `__at` (codegen.cc:67-93). Children created inside ctors (code, comment and codeblock text, region paragraphs, figure captions) get no span, and the resolver's para-unwrap discards the splice span. Result: everything spliced or built by user code sits at @[0,0), contradicting document-model §1 ("splice-produced nodes get the splice span"). Fix: make spans occurrence-level. `AT id s e → id'` creates an alias node that instantiate resolves by copying the target with the alias's span. copy() gives empty-span nodes the parent's span. Codegen wraps val splices.

*Verifier:* Reproduced in scratchpad/vcg/a3.tsm: a fence handler that returns one cached node produces two emissions, and both carry the second fence's span @[87,99). Unwrapped val(...) splices produce spanless text (a2.tsm, "undefined" @[0,0)).

*Verifier notes:* This is a design defect, not special-casing. An alias op plus inheriting the parent span for synthetic children matches document-model.md:26. It needs one op addition, so one version bump.

### `codegen-ops-model/exponential-instantiation` — Per-emission DAG copying gives exponential blow-up from a tiny buffer; recursion is unbounded

- kind: issue · severity: high · verdict: accurate · plan: **P0-07**
- locations: `engine/src/model/model.cc:36-54`; `engine/src/model/model.cc:94-96`; `engine/src/ops/ops.cc:135`; `engine/src/ops/ops.cc:143`

copy() recurses with no node budget and no depth limit. Verified: a 143-byte .ops (20 doublings of `x = seq(x, x)`) instantiates 2.1M ContentNodes at 493 MB RSS in tsrc. A few more doublings exceed the 4 GB WASM heap and crash. This violates document-model.md:118 ("the reader is a fuzz target … must reject, never crash"). The decoder's per-op caps (nargs ≤ 64, nchildren ≤ 2^20) do not bound the instantiated size. Fix: iterative copy with a configurable node budget (for example maxNodes), producing an `ops-too-large` diag and an error node when exceeded.

*Verifier:* Reproduced in scratchpad/vcg/a6.tsm: 18 doublings give a 122-byte .ops that instantiates into 524,288 tree lines, using about 100 MB RSS in 0.09 s.

*Verifier notes:* Severity 'high' is overstated given the documented trust model (design-decisions-v2.md:17: authors already have full JS and can loop forever). The breach is of the reader's fuzz contract (document-model.md:118).
A node budget is the right fix. Memoising copies per (rawId, inherited StyleId) would not be safe, because the resolver mutates the tree in place per occurrence (setArgStr labels, heading numbers).

### `codegen-ops-model/no-per-block-containment` — No per-top-level-block error containment; any script throw aborts the whole document

- kind: issue · severity: high · verdict: accurate · plan: **P0-05, P2-02**
- locations: `engine/src/codegen/codegen.cc:206-232`; `runtime/src/worker/executor.mjs:314-322`; `runtime/src/worker/worker.mjs:200-223`; `docs/design-decisions-v2.md:73`

Codegen emits every top-level block into one function with no try/catch (codegen.cc:206-232). The worker reports a single `error` for the whole document (worker.mjs:220-223), and the `script-error` diagnostic code (document-model §10) is never produced. v2 §2 and §11 specify that exceptions are caught per top-level block and become error blocks, and §12 specifies that the style stack is restored to the block's entry height. Neither is implemented. Fix: wrap each block as `__block(s, e, () => …)` in the executor, with try/catch producing an error node and diag, a style-height snapshot, and a pop to that height on error.

*Verifier:* Confirmed: codegen.cc:206-232 has no try/catch, worker.mjs:220-222 posts one error for the whole document, and `script-error` appears only in the docs (document-model.md:301).

*Verifier notes:* Wrapping each top-level block in `__block(…)` covers content blocks only. Two cases remain:
1. Statement splices (`#let`, `#{…}`) cannot be wrapped in a closure or try block without moving their declarations out of document scope. For `#let name = expr`, codegen knows the binding name, so it can hoist `let name;` and wrap only the assignment.
2. JS syntax errors fail the module import before any code runs. `#(1 + )` is pasted verbatim (scratchpad/vcg/a8.tsm). Containing them needs a fallback that compiles each top-level block or splice on its own to find the bad one, replaces it with an error node, and re-imports.

### `codegen-ops-model/keyword-forms-uncompiled` — #if / #for / #use and `#let x = […]` content literals compile to invalid or wrong JS

- kind: issue · severity: high · verdict: accurate · plan: **P0-05, P2-12, P3-31**
- locations: `engine/src/codegen/codegen.cc:214-225`; `engine/src/codegen/codegen.cc:67-93`; `engine/src/ast/ast.h:6-10`; `docs/design-decisions-v2.md:320-333`

Appendix A (design-decisions-v2.md:320-333) defines these as language forms, but the AST has no nodes for them. Verified: `#for (const k of [1,2]) [- item #k]` compiles to `val((for))`, and `#if` to `val((if))`; the module fails to parse, so the whole document fails. `#let x = [shared *value*]` is pasted verbatim as JS (`let x = [shared *value*];`, a SyntaxError). Because codegen splices raw JS with no per-splice validation or containment, one malformed splice takes down the whole document.

*Verifier:* Reproduced in scratchpad/vcg/a4.tsm: `val((for))` and `val((if))` are generated, `let x = [shared *value*];` is pasted verbatim, and `tsrc --stage=diags` reports nothing. The #use deferral is documented (architecture.md:225, document-model.md:195). #if and #for are not listed as deferred anywhere.

*Verifier notes:* This is a gap in parser/codegen coverage, not an ad-hoc feature. The parser should at least emit a diagnostic and an error node for these forms rather than invalid JS.

### `codegen-ops-model/css-injection-style-values` — Unvalidated style strings inject arbitrary CSS into typeset runs and break the measurement–render contract

- kind: issue · severity: high · verdict: accurate · plan: **P0-06, P1-02**
- locations: `engine/src/render/typeset_html.cc:44-72`; `engine/src/render/semantic_html.cc:66-80`; `engine/src/model/model.cc:14-34`; `runtime/src/worker/executor.mjs:225-229`

fontFamily and color are escaped for HTML only and then concatenated into the run's style attribute, so `;` passes through. Verified: `#style({color: "red; letter-spacing: 9px; display: block"})[…]` renders `style="color:red; letter-spacing: 9px; display: block"` on a run whose width was measured without them. That defeats the documented decision not to expose letterSpacingPx (document-model.md:82) and the line-level robustness contract (v2 §7). It is fixed by registry value canonicalisation (see fixed-styling-fields).

*Verifier:* Confirmed: typeset_html.cc:59-63 and semantic_html.cc:66-75 apply HTML escaping only, so `;` passes through into the style attribute.

*Verifier notes:* Under the documented trust model (design-decisions-v2.md:17) this is not a security issue; authors can already pass raw HTML through ctx.raw. The valid concern is robustness: unmeasured CSS such as letter-spacing defeats the §7 contract and the deliberate letterSpacingPx exclusion. Severity is closer to medium than high. Canonicalising values through a registry fixes it.

### `codegen-ops-model/sizepx-ignored-in-emit` — Emit computes font size without sizePx (math, CJK boundary glue, punctuation compression, indent)

- kind: issue · severity: medium · verdict: accurate · plan: **P0-08**
- locations: `engine/src/emit/emit.cc:251`; `engine/src/emit/emit.cc:478`; `engine/src/emit/emit.cc:958`; `engine/src/measure/measure.h:60-71`

emit.cc:251 `fontPx` returns `cfg.baseSizePx * sizeMul`, and the punctuation half-em (:958) and the first-line indent (:478) use baseSizePx in the same way. describeStyle (measure.h:66) and the typeset renderer (typeset_html.cc:46) do honour sizePx. Inside `#style({sizePx: 22})` or a sized region, inline math is therefore laid out at 18px, CJK boundary glue is under-sized, and punctuation compression subtracts the wrong half-em from glyphs measured at 22px. Root cause: no single derived `fontOf(Styling)` accessor.

*Verifier:* Confirmed at emit.cc:251, 478 and 958. Precision: the paragraph indent (emit.cc:478, `paraIndentEm * cfg.baseSizePx`) ignores sizeMul as well as sizePx, which is not quite 'the same way' as fontPx.

*Verifier notes:* A single derived fontOf(Styling, Config) accessor is the correct root fix.

### `codegen-ops-model/argtag-node-dangling` — Node-valued args are accepted by the codec but copied as raw-buffer ids and never interpreted

- kind: issue · severity: medium · verdict: accurate · plan: **P0-06**
- locations: `engine/src/ops/ops.cc:80-86`; `engine/src/model/model.cc:46-50`; `runtime/src/shared/opbuf.mjs:64`

readArg accepts ArgTag::Node in MAKE_NODE. instantiate copies it into ContentNode.args unchanged, so a raw-buffer id meaningless after instantiation is neither instantiated nor re-pointed, and nothing reads it. It is produced by accident whenever a shadow is passed where an attribute is expected (`heading(1, content)` → `label=%0`, verified). Either give node-valued attributes real semantics (instantiate as per-emission subtrees, schema-declared) or reject them in the decoder with a diag.

*Verifier:* Confirmed: scratchpad/vcg/a1.tsm shows `ordered=%6 start=%8` persisted into the content tree as raw-buffer ids (model.cc:46-50 copies them unchanged).

*Verifier notes:* Until a schema exists, rejecting ARG_NODE in MAKE_NODE with a diagnostic is the safe interim fix. If node-valued slots are wanted later, prefer typed child slots (see sidecar-ingest-pass).

### `codegen-ops-model/metric-key-fragmentation` — MetricStore is keyed by the full StyleId, so paint-only variants re-request widths; NaN styles break interning

- kind: issue · severity: medium · verdict: accurate · plan: **P0-08, P1-04**
- locations: `engine/src/measure/measure.h:19-35`; `engine/src/model/model.h:36-73`; `engine/src/api/wasm_api.cc:100-125`

`MetricStore::key(str, StyleId)` (measure.h:21) includes paint-only state: colour, decorations, lang, CLS_LINK, and each highlight token colour. The same word in black, in red, or inside a link becomes a separate engine-side request and entry, and highlighted code multiplies requests per token tag. The JS measurer deduplicates by font string, but the request/provide JSON and engine maps grow. `Styling::operator==` compares floats, so `sizePx: NaN` never matches and every `idOf` allocates a fresh StyleId (unbounded growth). The `s << 24 | st` packing collides once StyleId ≥ 2^24. Fix: key on a FontId, the interned projection onto the properties that affect metrics, and canonicalise or reject NaN.

*Verifier:* Confirmed: tsr_measure_requests groups requests by StyleId (wasm_api.cc:100-125), and the JS cache deduplicates by font string (canvas_measure.mjs:25-31).
A related defect is missing from the report: StyleTable::Hash hashes the float bit patterns (model.h:60-68) while operator== uses float `==` (model.h:36-39). `sizePx:-0` therefore compares equal to the base style but hashes differently, which violates std::unordered_map's hash/equality contract. NaN, conversely, never equals itself.

*Verifier notes:* A FontId projection plus value canonicalisation (reject NaN, map -0 to +0) fixes both.

### `codegen-ops-model/tree-dump-omits-sup` — The golden tree dump cannot show CLS_SUP

- kind: issue · severity: low · verdict: accurate · plan: **P1-02**
- locations: `engine/src/model/model.cc:109-145`; `engine/src/emit/emit.cc:1083-1088`

styleStr lists nine bits and leaves out CLS_SUP, so a footnote marker prints as `[basex0.70]` (verified), and emit's block dump (emit.cc:1083-1086) also lacks it. document-model §12 says the dump prints the resolved class bits. Generating the dump from a bit or property registry would prevent this kind of omission.

*Verifier:* Confirmed: model.cc:119-127 and emit.cc:1083-1086 have no SUP entry, and a3.tsm prints the marker as `[basex0.70]`.

*Verifier notes:* Generating the dump from a registry removes this whole class of omission.

### `codegen-ops-model/executor-errors-not-diagnostics` — Executor-built error nodes never reach the diagnostics channel; ctx.error ignores localOffset

- kind: issue · severity: low · verdict: accurate · plan: **P2-01**
- locations: `runtime/src/worker/executor.mjs:146-160`; `runtime/src/worker/executor.mjs:248-251`; `engine/src/emit/emit.cc:94-106`; `docs/design-decisions-v2.md:131-139`

Fence and bibliography failures become `error{message, code:'fence-error'|'bib-load'}` nodes, but no engine code reads ArgK::code or turns error nodes into DiagSink entries, so the documented `fence-error` diagnostic is never reported. `ctx.error(msg)` ignores the `localOffset` parameter in the v2 §4.1 handler contract, and handler error nodes get only whatever span `__at` gives the outer splice.

*Verifier:* Confirmed: there are no ArgK::code readers, and Kind::error appears only in emit and the semantic renderer (emit.cc:94, 763; semantic_html.cc:161, 348). ctx.error is `mkErr(msg)`, which takes no offset (executor.mjs:146-152).

*Verifier notes:* The fix belongs with the unified hook-invocation wrapper (see missed items): one place should turn handler failures into an error node plus a DiagSink entry with an offset-adjusted span.

### `codegen-ops-model/shadow-mutability-forgery` — Shadows are mutable plain objects and node-ness is duck-typed

- kind: issue · severity: low · verdict: accurate · plan: **P2-01**
- locations: `runtime/src/shared/opbuf.mjs:40-71`; `runtime/src/worker/executor.mjs:59-62`

Mutating `shadow.children` or `shadow.args` after construction changes nothing in the already-written buffer, yet stays visible to JS traversal, so the two diverge silently. Any object with an `opId` property is accepted as a node reference by toShadow and as an ARG_NODE by makeNode. Fix: freeze shadows and brand them with a module-private Symbol.

*Verifier:* Confirmed at opbuf.mjs:46, 64 and 70 and executor.mjs:61.

*Verifier notes:* Freezing and branding shadows is also a prerequisite for the attrs-object convention in ctor-signatures.

### `codegen-ops-model/popto-stack-divergence` — $.style.popTo(h) above the current height grows the JS stack with holes

- kind: issue · severity: low · verdict: accurate · plan: **P0-08, P2-01**
- locations: `runtime/src/worker/executor.mjs:282-283`; `engine/src/model/model.cc:84-92`

`styleStack.length = Math.max(0, h)` extends the array when h > height, so `$.style.height` reports h. Meanwhile the engine clamps with a `style-underflow` warning, and the two stacks diverge from then on.

*Verifier:* Confirmed: executor.mjs:283 versus model.cc:86-89, which clamps with a style-underflow warning.

*Verifier notes:* Clamp on the JS side as well and raise the same diagnostic.

### `codegen-ops-model/contract-doc-drift` — The normative ops/model spec has drifted from the code

- kind: issue · severity: low · verdict: accurate · plan: **P0-06**
- locations: `docs/document-model.md:32-61`; `docs/document-model.md:84`; `docs/document-model.md:98`; `docs/document-model.md:118`; `docs/architecture.md:142`; `docs/design-decisions-v2.md:248`

Mismatches between the spec and the code:
- document-model §2.1 lacks seq, image and note, and says code/comment carry `str`/`body` args, whereas the executor gives them text children and ARGK body is dead.
- §4.2 still shows "version u8 (=2)".
- §4.3 says an unknown kind gives an error node plus a diag; the decoder actually rejects the whole buffer (ops.cc:132).
- §3 StyleDelta.addDyn and dynClasses are unimplemented.
- architecture §4.1 describes `__reg`, fences.ts and `m` calling tsr_parse_fragment; none exists.
- v2 §12 says markup sugar compiles to push/pop pairs.

For a contract marked normative, the stale text misleads anyone extending the system.

*Verifier:* All listed drifts confirmed. Minor: the executor.ts paragraph in architecture §4.1 is line 143, not 142. Further drift: document-model.md:184-190 and notes-design.md, which record as-built decisions, are more current than v2 §4.1 and §12, but nothing marks v2 as superseded on those points.

*Verifier notes:* Generating the kind/arg table in document-model §2.1 from ops.def would stop this class of drift.

### `codegen-ops-model/missed:0` — Codegen is unhygienic: one flat scope is shared by generated calls, the constructor parameter list and user bindings

- kind: missed · severity: high · verdict: verifier-found · plan: **P0-05, P2-02**
- locations: `engine/src/codegen/codegen.cc:209-229`; `engine/src/codegen/codegen.cc:29-197`; `docs/design-decisions-v2.md:70-71`

The document is a single function. Its constructors are destructured parameters, `#let` lowers to a bare `let` in that same scope, and generated markup calls constructors by bare name (`strong(`, `heading(`, `text(`). Reproduced in scratchpad/vcg:
(a) b1.tsm: `#let x = 1` followed later by `#let x = 2` raises "Identifier 'x' has already been declared", and the whole document fails.
(b) b2.tsm: `#let toc = 3` fails the same way. Any binding named like a constructor (text, link, image, code, style, item, list, note, notes, ref, seq, m, val, …) is fatal. Every new built-in constructor is therefore a silent breaking change for documents that already used the name: image in 217b3b7, note and notes in 57340a8, bibliography in 59f56e3.
(c) b3.tsm: reassigning a parameter, as in `#{ strong = (...k) => style({color:"red"}, ...k) }`, changes how `*…*` compiles. Markup sugar is captured by an accidental, undocumented 'show rule'.

*Proposed generalization (survey):* Hygienic lowering:
1. Generated code calls through a private namespace (`__c.strong(...)`), so user bindings can never capture sugar.
2. User-visible names are introduced in an outer scope (`const {…} = __c;`), and user code runs in a nested block, so shadowing a built-in is legal.
3. Each `#let` opens a nested block (`let x = …; { … rest … }`), giving ML/Typst let-in rebinding semantics while keeping lexical closures correct.
4. If overriding sugar is wanted, make it explicit through the same `$.ctor(name, next => fn)` registry proposed for regions.

### `codegen-ops-model/missed:1` — No level normalisation in either direction; `styled` and `link` are structural wrappers with a fixed inline level

- kind: missed · severity: high · verdict: verifier-found · plan: **P2-11**
- locations: `engine/src/emit/emit.cc:822-824`; `engine/src/emit/emit.cc:170-172`; `engine/src/resolve/resolve.cc:490-491`; `runtime/src/worker/executor.mjs:157`; `runtime/src/worker/executor.mjs:225-229`; `engine/src/model/model.cc:36-54`

Inline content at block level is silently dropped. emit's blockWalk default only walks children, and a text node has none. scratchpad/vcg/b7.tsm: a fence handler returning `em(text(…))`, or `__emit(text(…))`, produces an empty pid. The natural fence-handler result `ctx.m` + "`…`" returns text and therefore renders nothing.
In the other direction, block content inside an inline wrapper is degraded. `#style({color:"red"})[#codeblock("js","let a = 1")]` renders as an ordinary justified inline paragraph with no mono font and no grid (b4.tsm), because the resolver's para-unwrap treats `styled` as inline.
`styled` survives instantiation as a real node even though its delta has already been folded onto the leaves. Every pass that pattern-matches shape trips over it: the para-unwrap, sidecar detection `kids[0]->kind==text`, and figure caption lookup.

*Proposed generalization (survey):* 1. Make `styled` a pure delta carrier. After instantiate folds its delta into the descendants' StyleIds, splice its children into the parent. Alternatively, mark it TRANSPARENT in a generated KindInfo table and have every matcher skip transparent kinds through one shared helper.
2. Run one bidirectional normalisation in instantiate, driven by KindInfo.level. At block positions, wrap maximal runs of inline nodes in an anonymous para (as CSS does with anonymous block boxes). A para whose only child is a block becomes that block and inherits the para's span. A block inside inline context either splits the para or yields an error node plus a diagnostic, never a silent drop.

### `codegen-ops-model/missed:2` — Extension hooks are invoked under different contracts: regions are sync and uncontained, fences async and contained, formatters sync and contained

- kind: missed · severity: medium · verdict: verifier-found · plan: **P2-03**
- locations: `runtime/src/worker/executor.mjs:123-139`; `runtime/src/worker/executor.mjs:142-161`; `runtime/src/worker/executor.mjs:257-259`; `engine/src/codegen/codegen.cc:148`; `engine/src/codegen/codegen.cc:158`

Codegen emits `await __fence(...)` but a plain `__region(...)`. scratchpad/vcg/b5.tsm: an async region handler renders the text "[object Promise]". b6.tsm: a throwing region handler aborts the entire document ("EXEC ERROR: boom"), while a throwing fence handler becomes `error{code:"fence-error"}`. Bibliography formatters are sync and contained, but their failure is rendered as a ⚠ text run, not an error node.
The three hook kinds therefore differ in sync versus async, containment, result coercion, span stamping and context object. A user region can never do what a user fence can, such as loading data or computing asynchronously.

*Proposed generalization (survey):* One `invokeHook(kind, fn, args, ctx)` used by every dispatch site: it awaits the result, catches throws into an error node plus a diagnostic (with the localOffset contract from design-decisions-v2.md:138), applies the single toContent coercion, and stamps the call-site span. Codegen awaits every hook-dispatch site, which is cheap because the document function is already async. This is the runtime half of the report's unified ExtCtx proposal.

### `codegen-ops-model/missed:3` — Numeric argument values are never validated or canonicalised: author-reachable undefined-behaviour casts and broken interning

- kind: missed · severity: medium · verdict: verifier-found · plan: **P0-06**
- locations: `engine/src/resolve/resolve.cc:133-135`; `engine/src/emit/emit.cc:488`; `engine/src/emit/emit.cc:512`; `engine/src/emit/emit.cc:558`; `engine/src/model/model.cc:16-17`; `engine/src/model/model.cc:28-29`; `engine/src/model/model.h:36-39`; `engine/src/model/model.h:58-70`

Values travel as raw f64 and are cast to integers wherever they are consumed. scratchpad/vcg/a9.tsm: `#heading(1e300, null)[…]`, `#list(true, -1e300, …)` and `#codeblock("js", "x", {lineNo: 1e300})` all reach `(int)a.num`. The ASan/UBSan build engine/build-debug/tsrc reports 'outside the range of representable values of type int' at resolve.cc:135, emit.cc:488, 512 and 558. Under WASM, depending on the nontrapping-fptoint setting, such a cast may trap.
`(u64)a.num` for styled bits (model.cc:17) has the same exposure for negative or NaN values in a crafted buffer, which breaches the fuzz contract at document-model.md:118.
StyleTable compounds this: it hashes float bit patterns but compares with float `==`, so NaN never interns and -0 hashes differently from +0 although the two compare equal.

*Proposed generalization (survey):* Give each schema attribute a value domain (Int[min,max], Enum, Bool, Length, Color) and validate it once, at decode or instantiate time. Canonicalise as part of the same step: reject NaN and infinities, map -0 to +0, clamp into range. Out-of-domain values produce an `ops-arg` diagnostic and fall back to the default. Consumers get typed accessors that return already-validated integers and never cast a double. This extends the report's global-argk-namespace schema, which as proposed checks tags but not domains.

### `codegen-ops-model/missed:4` — The m tag, the documented way to build content in deep code, cannot carry content values

- kind: missed · severity: low · verdict: verifier-found · plan: **P2-01**
- locations: `runtime/src/worker/executor.mjs:232-236`; `docs/design-decisions-v2.md:75`; `docs/design-decisions-v2.md:333`

Appendix A says that in deep code you construct content with the tagged template m. The implementation joins every interpolation with String() and returns a single text node. scratchpad/vcg/b8.tsm: `m` + "`see ${em(text(\"x\"))} and *not bold*`" yields the literal text "see [object Object] and *not bold*". Markup is not parsed (an acknowledged deferral), but content interpolation is lost as well, which no doc mentions. In deep code (callbacks, fence handlers) authors therefore have no way to mix stored content values into text other than calling constructors by hand.

*Proposed generalization (survey):* Even before m.parse exists, have m build `seq(text(s0), toContent(v0), text(s1), …)`. When fragment re-entry lands, pass interpolations into the parse as opaque placeholder slots that are mapped back to their shadows and never re-lexed as markup, applying v2 §4.1's 'tree-level splitting is embedding-proof' argument to fragments. The same mechanism then serves m.parse, sidecars and fence handlers.

## resolver — Resolver: counters, labels, refs, collectors, notes, citations, figures, terms

<details><summary>Design summary (as audited)</summary>

The resolver lives in engine/src/resolve/resolve.cc (532 lines) behind a single entry point, `resolveDoc`. It runs inside `Doc::ingest`, after instantiation and before emission (engine/src/api/doc.h:74), and edits the ContentTree in place, so both serializers see final numbers.

All of its state sits in one struct, `Resolver`, and each feature has its own fields:
- a section stack `secc`;
- one counter per feature: `tableNo`, `figNo`, `eqNo`, `noteNo`;
- `labels: map<string, Entry{Kind kind; string number; string excerpt}>`;
- `toc` and `gloss` vectors;
- a footnote list;
- a bibliography table `bib` with `citeNo`/`citeOrder`.

Execution is three hard-coded steps:
1. `scan` is one document-order walk that switches on `n->kind`. It steps the counter that belongs to each kind (a figure is recognised by `role=="figure"`; an equation is numbered only if it has a label) and registers labels for five cases only. It also already writes presentation into the tree: the bold "图 n：" caption prefix, the equation tag `name="(n)"`, and the note number.
2. `rewrite` is a second walk. It deletes empty paragraphs, unwraps a paragraph that holds a single block, and dispatches four kinds:
   - `collect`: string dispatch on toc/glossary/notes/bibliography to separate builders;
   - `term`: becomes `group{role:term}` with a bold name and " — ";
   - `ref`: a switch on the target `Entry.kind` using the Config supplements, falling back to a citation resolver that splits on commas and assigns ordinals during rewrite;
   - `note`: becomes a superscript `ref`, with the body moved later.
3. A tail step in `resolveDoc` appends the notes section if `#notes()` was never placed.

The output uses only engine kinds (a ref's kids are its display text, plus `url="#tsr-<label>"`), so emit and layout mostly do not need to know about the resolver. The exceptions are `role=="figure"` checks in emit and the semantic serializer, and CLS_SUP, which emit treats as a no-break signal.

Q3 answer: this is a fixed series of passes, one per feature, not a general counters + labels + queries engine. Each feature has its own counter variable, increment rule, label prefix, display recipe and placement path, and nothing is driven by data. Features also special-case each other:
- Footnote numbers are assigned in scan (document order), but citation ordinals are assigned in rewrite, where note bodies are deferred until the notes section is built. This causes a real numbering/omission bug.
- Back-links `fnref-n` are faked as label entries of `Kind::ref` whose excerpt is "↩".
- The bibliography only works because the executor emits its collector at document end.

The only configuration is four supplement strings, switched by `applyLang` (zh/ja vs everything else). The following are described in document-model §5/§11 and v2 §11.1 but do not exist: the JSON `supplements`/`counters` config, `form`, `lof`, `resetAt`, label-table node ids, and `$` counters.


Strengths:

- Placement is right. Resolving between instantiation and emission means the no-JS semantic HTML already has final numbers and real links (v2 §11.1). Verified: `tsrc --stage=semantic` on test/fixtures/doc/refs.tsm shows `<a href="#tsr-intro">§1</a>`.
- Scripts cannot read resolved values: there is no API for it, and `ref()` only writes a placeholder (runtime/src/worker/executor.mjs:173). Document execution therefore stays single-pass with no Typst-style fixpoint; this is the right base invariant to keep.
- Output uses only engine kinds (`ref` keeps its kind with display kids and a url; collectors expand to list/item/para/link/rule). Emit, break and layout stay closed under the kind table and never see `collect`, `term` or `note`.
- Failures are diagnostics, not hard errors: `ref-unresolved` renders "??", `label-duplicate` keeps the first, `collect-unknown` gives an empty group. A duplicate heading label falls back to the auto label `h-<n>` (resolve.cc:150-154).
- The footnote marker builds its style on top of the note's own style (`buildMarker`, resolve.cc:344-346), so color, font and lang scopes carry into it. This is the correct pattern; other synthesized text does not follow it.
- Note bodies and bibliography entries are fed back through `rewrite` (resolve.cc:372, 425), so refs and links inside them resolve the same as anywhere else.
- Bibliography data and entry formatting stay out of C++: CSL-JSON is loaded and formatted in JS (`formatEntryDefault`, overridable through `$.bib.format`, executor.mjs:16-38, 270-273), and the engine never parses JSON.
- The code is small and readable, and the post-resolve tree is a byte-exact golden stage (`--stage=tree`), so changes to resolver output are reviewed as diffs.

</details>


### `resolver/fixed-counter-set` — Counters are hard-wired struct members, each with its own increment rule per node kind

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:61-67`; `engine/src/resolve/resolve.cc:132-157`; `engine/src/resolve/resolve.cc:158-164`; `engine/src/resolve/resolve.cc:165-170`; `engine/src/resolve/resolve.cc:184-195`; `engine/src/resolve/resolve.cc:205-208`; `engine/src/resolve/resolve.cc:234-241`; `docs/document-model.md:124`; `docs/notes-design.md:34-36`

Complete list of counter classes (Q1):
1. `secc`: a hierarchical section stack keyed on `ArgK::level`, clamped to 1..6, with gaps filled by zeros. It also creates the auto-label `h-<n.n>`.
2. `tableNo`: stepped by every `Kind::table`, labelled or not.
3. `figNo`: stepped by a `group` whose role string equals "figure".
4. `eqNo`: stepped by a `mathblock` only when it has a label, so the numbering policy is 'labelled only'.
5. `noteNo`: stepped by every `Kind::note`.
6. Citation ordinals (`citeNo`/`citeOrder`): assigned lazily in the rewrite pass by `citeOrdinal`, not in scan.

There are no reset rules, no shared counters and no user counter classes. `Entry.kind` stores the node Kind (heading, table, group, mathblock, note, term, ref), and `resolveRef` later uses it as a stand-in for the counter class. document-model §5 promises `figure | equation | footnote | <user>` with `resetAt` from config, and notes-design §1 promises a footnote reset of `none|section`. Neither is implemented.

*Why ad hoc:* The counter is tied to the node kind (or a role string) instead of a declared class, so the resolver has no 'counter' abstraction at all. Every new feature means a new member, a new scan case and a new display case. Tying the counter to the kind also rules out a figure-like float numbered as 'Table n', an unnumbered data table, or a lemma that shares the theorem counter. This is not a documented trade-off: document-model §5 describes the general version.

*Proposed generalization (survey):* Add an element-class registry with one generic counter automaton (the full proposal is in cross_cutting_notes).

C++ data:
- `struct CounterDef { u32 id; std::string name; int parent=-1; u8 resetDepth; bool hierarchical; Gap gap /*skip|zero*/; }`
- `struct CounterState { std::vector<int> v; }`
- `struct ElementClass { u32 id; std::string name; u32 counter; NumberPolicy policy /*always|labelled|never|firstUse*/; NumberingPattern fmt; LocalizedStr supplement; TemplateId site, refForms[...], entry; FlowId flow; }`

Membership: a node declares its class with one generic arg, `class:"<name>"`. Constructors set it: heading uses class `heading` with depth = level, `#!figure` uses `figure`, mathblock uses `equation`, `^[…]` uses `footnote`, and a declared `#!theorem` uses `theorem`.

Phase A (locate) then runs one code path for every class: `if (auto* c = classOf(n); c && c->policy.allows(n)) { step(c->counter, depth(n)); snap = state[c->counter].v; }`.

The built-in classes become a default declaration table in data (config/prelude) that config JSON or the document can override:
- `heading{hierarchical, numbering:'1.1', supplement:'§'}`
- `figure{numbering:'1', supplement:{zh:'图 ',en:'Figure '}}`
- `table{...}`
- `equation{policy:'labelled', numbering:'(1)'}`
- `footnote{flow:'notes'}`
- `citation{policy:'firstUse', table:'bib'}`

User API: `$.element('theorem',{counter:'thm', within:'heading@1', numbering:'1.1', supplement:'Theorem'})` and `$.element('lemma',{counter:'thm', supplement:'Lemma'})`. The shared counter is amsthm's `\newtheorem{lemma}[theorem]`.

*Verifier:* All counter facts check out. Struct members are at resolve.cc:61-66. The heading stack is zero-filled through `secc.resize(level,0)` at resolve.cc:138-139. `tableNo` is unconditional (159), `figNo` is keyed on the role string (166), `eqNo` counts labelled equations only (188-189), `noteNo` is at 206, and `citeOrdinal` runs lazily in rewrite (234-241). The doc promises exist: document-model.md:124 and notes-design.md:34-36. One more consequence of tying counters to kinds, from my probe vr/v1-fig-table.tsm: a `#!table` inside a `#!figure`, which figure-design.md:28-31 explicitly supports, is counted twice, as 图 1 and as 表 1.

*Verifier notes:* The direction is right, but three fixes are needed.
(1) Class membership should default from (kind, role) through the declaration table, not from a new `class:` arg that every constructor writes. `dumpTree` prints every arg, so new args change every tree golden. That conflicts with the report's 'tree goldens stay identical' claim. A default mapping also lets the generic `#!theorem` region fallback, which produces `group{role:'theorem'}` (executor.mjs:130), become numbered with no executor change.
(2) Declarations must also exist as document-order events, not only as hoisted Phase-0 meta. LaTeX's `\appendix`, `\setcounter` and `\frontmatter`/`\mainmatter` switch numbering at a position. A hoist-only model cannot express a switch to 'A.1' at a given point.
(3) `firstUse` citation ordinals are a keyed ordinal over a table: they deduplicate by key and are not stepped per element occurrence. Model them as a separate 'ordinal over table rows' mechanism, not as a NumberPolicy of an element counter.
The rationale for user counters in v2 §11.1 ('computed numbering is done in user JS with user counters', design-decisions-v2.md:238) is documented, so this is partly a deliberate deferral. But the JS route cannot register labels, so the documented escape hatch does not actually work.

### `resolver/figure-role-string` — 'figure' is a role string matched by hand in four layers

- kind: adhoc · severity: high · verdict: partly · plan: **P3-03**
- locations: `engine/src/resolve/resolve.cc:166`; `engine/src/emit/emit.cc:19`; `engine/src/emit/emit.cc:471-476`; `engine/src/emit/emit.cc:779-818`; `engine/src/render/semantic_html.cc:286-305`; `runtime/src/worker/executor.mjs:116-121`; `runtime/src/worker/executor.mjs:128-130`; `docs/document-model.md:64`; `docs/figure-design.md:36-40`

`group{role:"figure"}` is documented as 'a convention, not a kind' (document-model §2.1), yet four layers compare the string:
- The executor builds it (`figureBuild`, and `name === 'figure'` in `__region`).
- The resolver counts it and injects the caption prefix (resolve.cc:166).
- Emit sets `figDepth`, which makes every para a ragged, centred, unhyphenated caption, and takes the float path (image plus paras as TableCells) (emit.cc:471-476, 779-818).
- The semantic serializer emits `<figure><figcaption>` (semantic_html.cc:286).

Every other region (`#!aside`, `#!theorem`) becomes a plain `group{role:name}` (executor.mjs:130) with no numbering, labels or captions. A user region handler (`$.region`) that wants numbering must emit the magic string, and then inherits layout and HTML meant only for figures.

*Why ad hoc:* One undeclared string carries three separate meanings: counter class (resolver), a layout policy for caption paragraphs (emit), and the semantic HTML element (render). Each layer works out figure-ness on its own. The minimal-kind-set rationale (document-model §2.1) is deliberate and sound, but a convention should be a declared mapping read by each layer, not a literal hard-coded in each.

*Proposed generalization (survey):* Keep the kind set minimal, but split the three meanings into separate declared properties:
- (a) Counter class: `class:"figure"`, resolved through the element-class registry (fixed-counter-set).
- (b) Caption layout: generic paragraph args `para{align:'center', hyphenate:false, indent:false}` set by the class's site template or by the constructor. Emit already has the fields (`u.centered`, `u.ragged`, `ctx.noHyphen`) and would read args instead of `figDepth`. Float handling keys on the `side` arg of an `image` child in ANY group, which is already the real test (emit.cc:789-797).
- (c) Semantic element: a data map `semanticMap: {figure:{tag:'figure', caption:'figcaption'}, theorem:{tag:'section', cls:'theorem'}}`, consumed by semantic_html.cc and extensible through `$.element(name,{html:{...}})`.

The `sidecar-lines` role check in emit.cc:587 is the same pattern and would use the same mechanism.

*Verifier:* The string checks are real: resolve.cc:166, emit.cc:776-781 (`isFigure`, then `figDepth` at 816-818 and the float path at 783-811), and semantic_html.cc:286. The executor does not compare the role. It compares the region name (`name === 'figure'`, executor.mjs:128) and writes the role (executor.mjs:120).
'A user region handler that wants numbering must emit the magic string' understates the problem. A `$.region` handler has no public constructor that can produce a `group` at all: there is no `group` in ctors (executor.mjs:162-237) or in the codegen prelude (codegen.cc:209-212). My probe vr/v2-region-handler.tsm returns a group-shaped object, and it renders as the text "[object Object]". The only route is the internal `__region('figure', …)`.

*Verifier notes:* Parts (a) and (c) of the generalization are sound. Part (b)'s float rule is unsound as written: 'Float handling keys on the side arg of an image child in ANY group'. Today a floated image outside a figure is its own float unit with `u.floatSide` (emit.cc:701-740), and the sibling paragraphs flow around it as body text. If any group containing a floated image absorbed its paragraphs as float-caption TableCells, an `#!aside` with a floated photo would swallow its body text into the float box. The caption/float rule must key on a declared caption slot (`slot:'caption'` children), not on the presence of an image. The kind-minimality rationale is documented in document-model.md:64,66; the per-layer string tests are not.

### `resolver/label-registration-per-kind` — Only five hard-coded branches register labels; every other labelled node is silently ignored

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:148-154`; `engine/src/resolve/resolve.cc:160-162`; `engine/src/resolve/resolve.cc:168-170`; `engine/src/resolve/resolve.cc:187-192`; `engine/src/resolve/resolve.cc:216-223`; `runtime/src/worker/executor.mjs:130`; `runtime/src/worker/executor.mjs:173-175`; `docs/document-model.md:63`

`addLabel` is called only from the heading, table, figure, mathblock and term cases, plus the synthetic `fn-`/`fnref-` labels.
- `#!theorem(label: "thm-a")` produces `group{role:"theorem", label:"thm-a"}`, but the label never enters the table. Scratch probe p2-theorem.tsm: `@thm-a` resolves to "??" with `ref-unresolved`. document-model §2.1 lists `group` as a labelable kind.
- Paragraphs, list items and quotes cannot carry labels at all: no constructor takes one, and the `<id>` sugar exists only for headings and display math. In examples/real-world/hott-introduction.tsm:161 the table caption `_…_ <tab-pov>` therefore keeps ` <tab-pov>` as literal text, and `@tab-pov` resolves to "??".
- A term's documented `label?` arg is overwritten with the term name (resolve.cc:219).

*Why ad hoc:* Registering a label is a generic fact ('this node is addressable'), but here it is fused with stepping a counter, so only kinds that got a counter branch can be referenced.

*Proposed generalization (survey):* Phase A registers every node that carries a `label`, using one code path:
`LabelEntry{ContentNode* node; u32 cls /*or none*/; std::vector<int> snap; ContentNode* title /*class-declared title slot: heading inline kids, caption para, theorem title*/; AnchorKey anchor;}`.

A ref to an unnumbered or classless target falls back to the class `name` form: the `title` content if present, otherwise the label text. Every block constructor gets a uniform `label` option (para, list, item, quote; regions already accept `label:`). The linepass `<id>` sugar becomes a generic trailing-label rule for any block line.

*Verifier:* The probe confirms it: `#!theorem(label:"thm-a")` keeps `label="thm-a"` on the node and becomes a DOM id, but `@thm-a` resolves to "??". HoTT's `<tab-pov>` stays literal text (hott-introduction.tsm:161) and `@tab-pov` resolves to "??" (regenerated tree). The term-label overwrite at resolve.cc:219 is real, but the public `term(name, ...desc)` constructor (executor.mjs:174-175) cannot pass a label anyway, so only raw ops can hit that path.

*Verifier notes:* Registering every labelled node generically is sound. The proposed 'generic trailing-label rule for any block line' is risky. The id regex used by the grammar, `<[A-Za-z][A-Za-z0-9_-]*>` (grammar.js:95), matches ordinary prose such as a paragraph ending '…wrap it in <div>'. That text would be silently consumed as a label. Restrict the trailing label to forms with an unambiguous terminator (headings, display math, region openers, caption slots), or require the `label:` option or an escape on paragraphs. The fallback 'name form = label text' for classless targets is acceptable but should raise a diagnostic, because it would display a raw id.

### `resolver/ref-display-switch` — Reference display is a switch on the target node's kind; the documented `form` is not implemented

- kind: adhoc · severity: high · verdict: partly · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:271-294`; `engine/src/resolve/resolve.cc:284-291`; `engine/src/resolve/resolve.cc:212`; `runtime/src/worker/executor.mjs:173`; `docs/document-model.md:57`; `docs/document-model.md:126`; `docs/document-model.md:133`

Complete list of ref forms (Q1):
- heading → `supHeading + "2.1"` ("§2.1")
- table → `supTable + n` ("表 1")
- group (figure only) → `supFigure + n`
- mathblock → `supEquation + "(" + n + ")"` ("式 (1)")
- note → the bare digit
- term → excerpt, which is the term name
- `fnref-*` (registered as `Kind::ref` with excerpt "↩") → "↩"
- anything else → the target string
- bibliography fallback → "[n]" or "[n, m]" with one link per number
- unresolved → "??"

The display is always a single text node. The `ref(target)` constructor takes no options, so `#ref("intro", "name")` silently drops its second argument (probe p6-misc.tsm). `resolveRef` never reads `ArgK::form`, although document-model §5 specifies `form = number | name | full`.

*Why ad hoc:* Each target kind has its own hard-coded string recipe. Users cannot produce 'Theorem 3', 'Chapter 2' / 'Part I', 'Fig. 3(b)', or a name-only or number-only reference, and the display cannot contain markup or inherit the ref's style.

*Proposed generalization (survey):* `ref{target, form?}` resolves to a `LabelEntry`, then instantiates the template the target class declares for that form: `ElementClass.refForms: small_map<StrRef form, TemplateId>`. Defaults:
- `number` = `[supplement][number]`
- `name` = `[title]`
- `full` = `[supplement][number] [title]`
- plus any user-defined forms.

A template is a content subtree with `slot{name}` inline nodes (number, supplement, title, label, `counter:<name>`). Phase B instantiates it by deep copy, building on the ref's own style.

Constructor: `ref(target, {form, supplement})`; the per-ref supplement override matches Typst's `ref(supplement:)`. The unresolved display becomes the template `unresolved`, defaulting to "??".

*Verifier:* The display table is accurate (resolve.cc:284-291), as are `form` never being read and `#ref("intro","name")` silently dropping its second argument (probe p6 renders §1). Wrong: 'the display cannot … inherit the ref's style'. The text is built with `mkText(disp, r->span, r->style)` (resolve.cc:293), and cite pieces also use `r->style` (259-267), so ref display does inherit the ref's style. The true limitation is that the display is a single unstructured text.

*Verifier notes:* Per-class form templates are the right abstraction. Three corrections:
(1) Emission-time style binding (§12). Templates declared via EMITted meta nodes would bake the schedule stack at the declaration site into their leaves, because instantiation folds styles into leaves (model.cc:38-55). Templates must be instantiated style-neutral, or stored as deltas, and composed onto the ref's style at the use site.
(2) The `name`/`full` forms copy a title that may itself contain `form:name` refs. Two headings that name each other recurse forever. A cycle or depth guard is needed to keep the 'no fixpoint' claim.
(3) Golden-neutrality needs adjacent same-style text slots merged. Otherwise '§'+'1' becomes two text nodes, the tree dump changes, and the cross-node boundary rules in emit may change the block streams.

### `resolver/collector-what-dispatch` — Collectors are four separate builders chosen by string; lof and index do not exist

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:431-440`; `engine/src/resolve/resolve.cc:296-322`; `engine/src/resolve/resolve.cc:324-336`; `engine/src/resolve/resolve.cc:362-394`; `engine/src/resolve/resolve.cc:400-429`; `runtime/src/worker/executor.mjs:176-189`; `runtime/src/worker/executor.mjs:263-264`; `docs/design-decisions-v2.md:240`; `docs/document-model.md:127`

`buildCollect` compares `what` against "toc", "glossary", "notes" and "bibliography". Anything else gets a `collect-unknown` warning and an empty group. Each builder hard-codes its own shape:
- toc: a nested `ul` of links whose text is the string `number + " " + text`. It uses its own nesting algorithm with a `minLevel = 7` sentinel.
- glossary: a flat `ul` of "name — desc" strings.
- notes: `group{role:notes}`, a rule plus an `ol`.
- bibliography: `group{role:bibliography}`, a rule plus "[n] " paragraphs, where `form:"all"` means 'include uncited entries'.

Collectors take no parameters (no depth, filter, title or order). `lof` is promised by v2 §11.1 and document-model §5 but missing; there is no index; and the executor has one fixed constructor per collector.

*Why ad hoc:* The docs present 'collector' as the unifying abstraction ('Collectors unify TOC, glossary, list-of-figures, bibliography'). In the code each one is a separate function over a separate private table with no query model, so users cannot add a list of theorems or tables, an index, or a TOC limited to depth 2.

*Proposed generalization (survey):* One generic collector that runs a query, orders the result and renders it through a template over the Phase-A tables: `collect{select, where?, order?, nest?, entry, flow?, scope?}`.
- `select`: an element-class list or a named entry table (`heading`, `figure`, `theorem|lemma`, `glossary`, `index`, `bib`).
- `where`: a tiny predicate language (`depth<=2`, `cited`, `labelled`, `outline`).
- `order`: document | firstUse | key | sortKey.
- `nest`: none | depth.
- `entry`: a template with slots (number, supplement, title, link-to-target, body, backlinks).

The built-ins become named presets, as data:
- `toc = collect{select:'heading', where:'outline', nest:'depth', entry:'[link:[number] [title]]'}`
- `lof = collect{select:'figure', entry:…}`
- `glossary = collect{select:'glossary'}`
- `bibliography = collect{select:'bib', where:'cited', order:'firstUse', entry:'[[number]] [body]'}`
- `notes = collect{flow:'notes'}`

JS: a `collect({...})` constructor plus `$.collector(name, preset)`. `#toc()` then becomes a call to a preset, and a user's `#theorems()` is defined exactly the same way.

*Verifier notes:* A query-plus-entry-template collector is the right unification; v2:240 already claims it. Three fixes:
(a) Ops arg values are only null/bool/f64/strRef/nodeId (document-model.md:116), so `select:['theorem','lemma']` cannot be an arg. Encode it as child nodes or as a declared selector name; do not use joined strings.
(b) A string `where` mini-language adds a second expression language next to JS. Use structured args (`maxDepth`, `cited`, `outline`) instead.
(c) An index needs a group-by dimension (one row per key, many back-links) that `{select, where, order, nest}` lacks. Collation must arrive as JS-provided sort keys: the engine has no ICU, and CJK indexes need pinyin or stroke order.
Keeping move-semantics flows separate from copy-semantics collectors (the `flow` param) is correct.

### `resolver/footnote-pipeline` — Footnotes have their own pipeline: synthetic labels, a style bit as the marker signal, body lifting, implicit placement

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:64-68`; `engine/src/resolve/resolve.cc:205-215`; `engine/src/resolve/resolve.cc:342-357`; `engine/src/resolve/resolve.cc:362-394`; `engine/src/resolve/resolve.cc:506-511`; `engine/src/resolve/resolve.cc:526-529`; `engine/src/model/model.h:20-22`; `engine/src/emit/emit.cc:84-90`; `docs/notes-design.md:28-61`; `docs/notes-design.md:76-96`

What the footnote path does:
- Counter `noteNo`.
- Two synthetic labels: `fn-n` for the body and `fnref-n` for the marker. `fnref-n` is registered as a `Kind::ref` Entry with excerpt "↩" so that `resolveRef` prints the back-arrow.
- The number is stored in `ArgK::name` (notes-design promised `ArgK::number`).
- The marker is a `ref` styled `CLS_SUP` × 0.7.
- Emit detects CLS_SUP and forbids a line break before the marker (emit.cc:89), so a style bit carries a line-breaking rule.
- The body is lifted into `group{role:"notes"}` (a `rule` plus an ordered list). `rescale` multiplies the style of every body node by 0.85 in place, and " " + "↩" is appended.
- Placement is either an explicit `#notes()` or, when the `notesPlaced` flag is false, an implicit append by `resolveDoc`.

Designed but not implemented: named `#note(name)[…]`, the `note-undefined` diagnostic, `resetAt: section`, `noteMarks: circled`, and bottom-of-sheet inserts in print.

*Why ad hoc:* 'Content that moves to a collection point and leaves a marker behind' is a general idea: per-chapter endnotes, sidenotes and margin notes, todo lists, and later float queues all fit it. Here it is coded for exactly one feature. The marker's semantics reach emit through a styling bit. Users cannot build a sidenote or per-chapter endnotes.

*Proposed generalization (survey):* A generic flow mechanism. The element class declares `flow:"<name>"` (`footnote.flow = "notes"`).
- Phase A: each flow node gets its counter snapshot and a `FlowItem{FlowId flow; ContentNode* node; std::vector<int> snap; ScopeKey scope /*doc or heading@k ordinal*/}`.
- Phase B: the node is replaced by the class `site` template. The default is `sup[link→item:[number]]`, carrying an explicit inline arg `attach:"before"` (no break before; emit reads it generically) instead of signalling through CLS_SUP. Each body is placed exactly once: by the first `collect{flow:'notes', scope}` whose scope contains it, otherwise by the class's `implicit: 'end'|'section-end'|'none'`.
- The entry template defaults to `[body] [backref ↩]` with a relative-size style patch (`size×0.85`) applied while instantiating copies, so shared nodes are never mutated.
- Anchors for items and markers are engine-private keys (anchor-namespace), not `fn-n` in the user namespace.
- Paged print (notes-design §1) reads the same `FlowItem` list to place sheet-bottom inserts without matching the role "notes".

*Verifier:* Confirmed: resolve.cc:205-213, 342-394 and 526-529; emit.cc:88-89 (the CLS_SUP→BREAK_INF rule fires only inside the `Kind::ref` case); notes-design.md:76-96 as-built. The same body text renders at ×0.72 when `#notes()` is repeated (probe p4).

*Verifier notes:* The flow abstraction is sound, and so is a generic `attach`/no-break-before inline property. But 'anchors … engine-private keys, not fn-n in the user namespace' would break a documented feature: notes-design.md:37-38 says '@fn-3 works like any reference', and the golden fixture test/fixtures/notes/basic.tsm uses `@fn-1`. Users need a supported way to address notes: a reserved, validated alias namespace, or note labels (the designed `#note(name)` / `^[…]` label). The shell popup also hard-codes these spellings (shell.mjs:146,175), so the flow model must give renderers a declared attribute to key on. Golden-neutrality is overstated: a site template that produces a different marker node shape, plus a new `attach` arg, changes the tree dumps.

### `resolver/citation-path` — Citations: grouping by comma string, ordinals in rewrite order, a fixed numeric format, and an ordering contract with the executor

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-09**
- locations: `engine/src/resolve/resolve.cc:70-76`; `engine/src/resolve/resolve.cc:197-204`; `engine/src/resolve/resolve.cc:234-269`; `engine/src/resolve/resolve.cc:274-277`; `engine/src/resolve/resolve.cc:400-429`; `runtime/src/worker/executor.mjs:185-188`; `runtime/src/worker/executor.mjs:241-266`; `docs/notes-design.md:118-131`; `docs/notes-design.md:148-154`; `tools/convert/tex2tsm.mjs:183`

How citations resolve today:
- `@key` is looked up in the label table first; if that fails, `resolveCite` takes over. `@[k1, k2]` reaches the resolver as the single string "kp81, liang83", which it splits on commas.
- Every key must exist, or the whole group becomes "??".
- Ordinals are assigned in rewrite order (`citeOrdinal`), and the output is "[" n ", " m "]" with one link per number to "#tsr-bib-<key>".
- Scan fills the bibliography table from the kids of `collect{what:"bibliography"}`; the string "bibliography" is matched again at resolve.cc:198. Those kids are `group{role:"bibentry", name:key}`, built by the executor's `finishBibliographies`.
- `buildBibliography` emits a rule plus "[n] " paragraphs labelled `bib-<key>`; `form:"all"` (reusing the ref-form arg key) appends uncited entries.
- Correctness depends on the executor emitting the collector at document end ("The bibliography position is always document end", notes-design.md:154).
- Grouped refs to labels such as `@[sec-a, thm-a]` cannot work; tools/convert/tex2tsm.mjs:183 keeps only the first `\cref` target to cope.

*Why ad hoc:* A citation is simply a reference into a keyed table, with a counter numbered on first use and a grouped display. Here it is a second, private reference system nested inside `resolveRef`, with its own numbering policy, format and placement rule. 'Rendering style is a JS function' (notes-design §2) is only true for the entry body; the in-text form, the order and the "[n] " label are fixed in C++.

*Proposed generalization (survey):* 1. `bib` becomes an entry table of keyed rows with fields. JS declares it with order-independent `meta` declarations (`decl:'entry', table:'bib', key`, children = formatted body, args = selected CSL fields). The executor can still load the data asynchronously and emit these declarations at the end, because tables are hoisted in Phase 0 and placement no longer depends on emission order. `#bibliography(src)` returns the real collector node at the call site.
2. A citation is `ref{targets:[k1,k2], table:'bib'}` with structured targets. The parser turns `@[a, b]` into multiple target children, so grouping becomes generic and `@[sec-a, thm-b]` also works for labels through the class `group` template (join or collapse ranges, e.g. [1–3]).
3. The citation counter is declared with policy `firstUse` and stepped in Phase A at the citation's document position, visiting note bodies where they occur. Ordinals then stop depending on pass order.
4. Cite and entry display are class templates: `citation.ref = "[[numbers ', ']]"` and `bib.entry = "[[number]] [body]"`, with `slot{field:'author'}` access so author-year styles are just templates. Users of citeproc can supply a precomputed `citeText` field.
5. The bibliography is `collect{select:'bib', where:'cited'|'all', order:'firstUse'|'key'}` and can sit anywhere.

*Verifier:* Confirmed: resolve.cc:197-204, 244-269 and 274-277; executor.mjs:185-188 and 241-266; notes-design.md:148-154; tex2tsm.mjs:183 keeps only the first `\cref` target. Scan does not check `role=="bibentry"`: any kid with a `name` arg is taken as an entry (resolve.cc:199-201).

*Verifier notes:* Entry tables, first-use ordinals in Phase A, and the bibliography as an ordinary query are all sound. Returning a key-less `collect` at the call site while entries arrive later as order-independent declarations also fixes the document-end ordering contract.
One thing must change: splitting `@[a, b]` at parse time collides with the documented use of the bracket form to quote CJK labels (v2 §11.1, design-decisions-v2.md:239: `@[排版]`; the grammar's reference token is the same, grammar.js:94). Today the resolver tries the whole string as a label first and only then splits on ASCII commas (resolve.cc:274-276, 247-255), which is what keeps label-first precedence (notes-design.md:118-119). Either keep the whole-string-label-first rule after parsing (parse into children, but rejoin and try it as one label first), or introduce a distinct grouping syntax.
'Author-year styles are just templates' is overstated: disambiguation (2020a/b) and et-al. rules are not expressible as slots. The precomputed `citeText` escape hatch is the real mechanism there.

### `resolver/supplement-config` — Supplements are four fixed Config strings chosen by a two-way language test

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-10**
- locations: `engine/src/api/config.h:66-71`; `engine/src/api/config.h:86-102`; `engine/src/api/wasm_api.cc:62-65`; `engine/src/api/native_cli.cc:60-66`; `docs/document-model.md:321-322`; `docs/notes-design.md:72-74`; `docs/real-world-report.md:42-44`

Complete list of supplements (Q1): `supHeading "§"`, `supTable "表 "`, `supFigure "图 "`, `supEquation "式 "`, and `capSep "："`. Footnotes have no supplement.

`applyLang` hard-codes zh and ja to the CJK words and gives every other language "Table ", "Figure ", "Eq. " and ": ". Headings stay "§" in every language.

The only host entry point is `tsr_set_lang`. The JSON config in document-model §11 (`supplements`, `counters`) has no parser, because `tsr_doc_new()` takes no config. Documents cannot set supplements, and `supNote`/`noteMarks` (notes-design §1) do not exist. The native CLI never calls `applyLang`, so the golden tests never cover English supplements.

*Why ad hoc:* Supplements belong to element classes and to languages, but here they are struct fields named after four built-in features. Per-level heading words (Part / Chapter / Section, which HoTT needs) and new classes such as Theorem have nowhere to go, and the language choice is a binary `if`.

*Proposed generalization (survey):* `ElementClass.supplement` becomes a localized map (`{"":"§", zh:"图 ", en:"Figure "}`) resolved with BCP-47 fallback (zh-TW → zh → ""). Hierarchical classes can give one supplement per depth (`heading.supplement:['Part ','Chapter ','Section ']`).

It can be set in three places: config JSON (`elements:{…}`), the document (`$.element(name,{supplement})`), or a single ref (`ref(t,{supplement})`). `capSep` moves into the figure/table `site` template. A small lang → supplement data table replaces `applyLang`.

*Verifier:* Confirmed: config.h:66-71 and 89-102, wasm_api.cc:63-65, and native_cli.cc:60-66 (no `applyLang`). The worker does call `_tsr_set_lang`, with the shell default `lang='zh-CN'` (shell.mjs:333, worker.mjs `typeset`). `renderTsm` calls it only when `opts.lang` is given, and tools/export-static.mjs:36 never passes it. So the language for supplements is a per-host default and cannot be declared by the document.

*Verifier notes:* A localized per-class supplement map with BCP-47 fallback is right. Make the document language itself declarable (a doc-level meta or front matter), with host settings as defaults. Otherwise the same .tsm renders '图 1：' through export-static and 'Figure 1: ' through a host that passes `lang:'en'`.

### `resolver/numbering-format-hardcoded` — Numbering format and policy are hard-coded: decimal, dotted, '(n)', equations numbered only when labelled, zero-filled level gaps

- kind: adhoc · severity: medium · verdict: partly · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:133-145`; `engine/src/resolve/resolve.cc:162`; `engine/src/resolve/resolve.cc:170`; `engine/src/resolve/resolve.cc:186-193`; `engine/src/resolve/resolve.cc:207`; `engine/src/resolve/resolve.cc:264`; `engine/src/resolve/resolve.cc:423`; `docs/notes-design.md:72-74`; `docs/notes-design.md:174-176`; `examples/real-world/hott-introduction.tsm:1-11`

- Every number comes from `std::to_string`.
- Headings join with '.' at every level, levels are clamped to 1..6, and gaps are filled with zeros. `= Introduction` followed by `=== Type theory` gives "1.0.1": the HoTT example gets anchors h-1.0.1 … h-1.0.9 and TOC lines like "1.0.1 Type theory" (scratch tree of hott-introduction).
- Equations are numbered only when labelled, and their tag "(n)" is stored in `ArgK::name` as a string.
- Figures, tables and notes are always numbered.
- Missing entirely: unnumbered headings (`\chapter*`, which is HoTT's Introduction), per-chapter resets ('图 2-3', 'Figure 2.3'), roman or alphabetic numbering (Part I, Appendix A), and circled footnote marks.

*Why ad hoc:* Formatting a number is a pure function of (counter vector, pattern), the classic data-driven mechanism (LaTeX `\the<counter>`, Typst numbering patterns). Here it is inlined separately in each feature.

*Proposed generalization (survey):* A `NumberingPattern` parsed once from Typst-style strings. Tokens `1 a A i I ① 一` plus literal affixes apply to each component of the counter vector:
- heading "1.1", appendix "A.1", part "I";
- equation "(1.1)" with `within: heading@1` (the chapter component comes from the parent counter snapshot taken in Phase A);
- footnote "①".

One formatter does it all: `std::string formatNumber(const NumberingPattern&, std::span<const int>)`, used by every `slot{number}`. Each class has a `NumberPolicy{always, labelled, never, firstUse}`, which can be overridden per node (`heading(level,{numbered:false})`). Hierarchical counters choose `gap: 'zero'|'skip'`.

*Verifier:* The substance holds: `to_string` everywhere, '.' joins, the 1..6 clamp, zero-filled gaps, and the '(n)' string. The HoTT evidence is mislocated. hott-introduction.tsm has no `#toc()`, so there are no TOC lines like '1.0.1 Type theory' in its tree. Only the anchors `h-1.0.1`… exist (regenerated tree). The TOC effect is real and shows in probe p3: '1.0.1 Deep jump'. The zero-fill also produces empty wrapper `<li>` elements (a visible empty bullet) in the semantic TOC.

*Verifier notes:* `NumberingPattern` + `NumberPolicy` + gap policy is the standard, sound mechanism. Circled marks need an overflow fallback, since ①… runs out at 50.

### `resolver/site-display-injection` — Showing the number at the declaring node is per-kind tree surgery done inside the counting pass

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-03, P3-26**
- locations: `engine/src/resolve/resolve.cc:171-180`; `engine/src/resolve/resolve.cc:193`; `engine/src/resolve/resolve.cc:208`; `engine/src/emit/emit.cc:756`; `engine/src/render/typeset_html.cc:232-239`; `docs/figure-design.md:36-40`; `examples/real-world/hott-introduction.tsm:161`

Each kind shows (or fails to show) its own number in a different way:
- Figures: scan inserts the bold text "图 n：" (`cfg.supFigure + n + cfg.capSep`) as the first kid of the first `para` child. Its style is the absolute `Styling{CLS_BOLD,1.0f}`. Probe p5-style.tsm shows "图 1：" losing `color=blue` while the rest of the caption keeps it. Figures without a caption get nothing.
- Equations: the string "(n)" is stored in `ArgK::name`, emit reads it as `eqTag`, and the typeset renderer prints it verbatim. It is a string, not content, so it cannot be styled or templated.
- Headings: no number is shown at the heading at all. A ref says "§2" for a heading that never displays a 2.
- Tables: numbered, but there is no caption mechanism. HoTT works around this with a following italic paragraph plus a `<tab-pov>` that stays literal.
- Notes: the marker (covered in footnote-pipeline).

*Why ad hoc:* Every class needs 'show my number here' (caption prefix, heading number, theorem head, equation tag, footnote marker). That one need is implemented five different ways, or not at all. The counting pass (`scan`) also edits presentation, which mixes computing values with producing output.

*Proposed generalization (survey):* Each element class declares a `site` template plus an attachment mode:
- `prepend:caption`: prepend into the child the constructor marked `slot:"caption"` (figure, table, theorem title line);
- `prepend:self`: the number before the heading text, when `heading.site` is set;
- `tag`: instantiate as the mathblock's tag content. The tag becomes a `seq` child, so emit lays out content instead of a StrRef;
- `replace`: the footnote marker.

Attachment happens only in Phase B, so scan becomes read-only. Styles are combined as `compose(target->style, tpl->style)`. The `#!figure` and `#!table` constructors mark caption paragraphs structurally (`group{slot:'caption'}`), which gives tables captions for free.

*Verifier:* Confirmed: resolve.cc:171-180 (absolute `Styling{CLS_BOLD,1.0f}`; p5 shows the prefix losing `color=blue`), resolve.cc:193 → emit.cc:756 → typeset_html.cc:232-240, and headings show no number in either serializer (test/golden/doc/refs.semantic.txt). Missed consequence: the semantic serializer never renders the equation tag. semantic_html.cc:260-266 prints only `$ src $`, and the golden test/golden/math/eqref.semantic.txt:3-5 locks this in, while the prose says '式 (1)'.

*Verifier notes:* Site templates with attach modes are sound. 'The #!table constructor marks caption paragraphs… tables get captions for free' does not work: `tableBuild` turns every interior source paragraph into rows via `|` segmentation, and drops block content before the first row (executor.mjs:76-93). A table caption needs either a region arg or figure(kind:table) wrapping, as in Typst. Giving `mathblock` a tag child changes the kind contract (document-model.md:50 says children '—'). That is fine, but the doc must change, and the tag must go through the measurement pull loop (today it is CSS-positioned and unmeasured, so it can overlap a wide formula).

### `resolver/term-rewrite` — term is rewritten to group{role:term} with a fixed bold name and ' — '; the glossary reads a private string table

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:216-226`; `engine/src/resolve/resolve.cc:324-336`; `engine/src/resolve/resolve.cc:444-467`; `engine/src/resolve/resolve.cc:497-501`; `runtime/src/worker/executor.mjs:67-71`; `runtime/src/worker/executor.mjs:174-175`; `docs/document-model.md:134`; `docs/document-model.md:256`

Scan:
- The term's label is set to its name, overwriting any user label. Free text with spaces then becomes a DOM id.
- The label is registered with excerpt = name.
- A `GlossItem{name, desc}` is pushed, with the description flattened to plain text.

Rewrite (Q1 'rewrite term → group'):
- term becomes `group{role:"term", label}`.
- The first paragraph holds the bold name, with the absolute style `Styling{CLS_BOLD,1.0f}`. Probe p5-style.tsm shows it losing the inherited Kai font, zh-TW lang and red color.
- Then " — " and the inline description kids follow; block kids are appended afterwards, which reorders mixed inline/block content.

Names are `shadowText` plain-text projections computed in JS, so markup in a name is lost. In probe p3, `#term[@sec-a][…]` produces an empty name, no label and no glossary entry. document-model §9.2 maps `term→dl>dt+dd`, but the renderer never sees a `term` node.

*Why ad hoc:* A term combines three things: how it looks where it is defined, an entry in a keyed table, and a label. All three are fused into one rewrite with fixed presentation, and terms are the only way to put anything into a collectable table, so there are no index entries.

*Proposed generalization (survey):* A generic entry declaration: `entry{table:'glossary', key, label?}[title][body]`. JS `term(name, desc)` becomes a preset: `entry` plus element class `term`, with site template `[*title*] — [body]` and the HTML map `dl/dt/dd`.

Tables are Phase-A `std::unordered_map<StrRef, std::vector<Row>>` with `Row{key; ContentNode* node; ContentNode* title; ContentNode* body; std::vector<int> snap}`, and collectors select from them. An index becomes `#idx[word]`: an entry with `site:none`, collected by an `index` collector that groups rows by key with back-links to every occurrence. Keys stay plain strings; titles stay content.

*Verifier:* Confirmed by p3 (`#term[@sec-a]` gives an empty bold name, no label and no glossary row; the glossary renders an empty `<ul>`), p5 (the name loses Kai/zh-TW/red) and p6 (term 'intro' keeps `label="intro"` after `label-duplicate`). The rewrite shape is itself documented (document-model.md:134), so this is a documented as-built behaviour, not an accident.

*Verifier notes:* A generic `entry{table,key}[title][body]` with term as a preset is sound and gives index and notation lists. Keys stay strings, titles stay content.

### `resolver/excerpt-strings` — TOC and glossary entries are plain-text excerpts captured before anything is resolved

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-03**
- locations: `engine/src/resolve/resolve.cc:17-24`; `engine/src/resolve/resolve.cc:146-147`; `engine/src/resolve/resolve.cc:155`; `engine/src/resolve/resolve.cc:220-223`; `engine/src/resolve/resolve.cc:296-336`

At scan time `excerptInto` joins all text descendants, skipping only comments. As a result:
- Math disappears (`mathinline` has no text kids).
- Refs disappear (they have no kids yet).
- Footnote bodies are included.
- Emphasis, code and style are lost.

A TOC entry is a link with text `number + " " + text` in the base style, so lang and font are lost. Probe p3-heading-note.tsm: the heading `= Title with note^[note body text] and @sec-b ref` produces the TOC line "1 Title with notenote body text and  ref". The TOC also lists every heading, including the 'Table of contents' and 'Glossary' headings themselves, with no way to opt out and no depth limit.

*Why ad hoc:* Collecting content was reduced to collecting strings because the collectors were written before any content-copy or template mechanism existed.

*Proposed generalization (survey):* Each collector row points at the target's `title` content node. Phase B deep-copies it with one shared `cloneForCollector(node)` that:
- (a) drops flow nodes (footnote markers) and labels/anchors, since copies must not duplicate ids;
- (b) resolves refs inside the copy like anywhere else;
- (c) combines styles.

Heading opt-out becomes `heading(level,{outline:false})` together with the `where:'outline'` filter, and depth becomes `where:'depth<=2'`.

*Verifier:* Probe p3's TOC line 'Title with notenote body text and  ref' is confirmed. TOC links are built by `mkLink`, which takes no style (resolve.cc:111-115), so they use base style 0 even inside a styled scope.

*Verifier notes:* The cost claim 'refs.tree.txt identical' is overstated. Replacing the single text node 'number + " " + text' with a number text plus cloned title nodes changes the tree structure, unless adjacent same-style texts are merged. Otherwise the clone helper (drop flows and anchors, resolve refs, compose styles) is right.

### `resolver/presentation-constants` — Presentation literals and magic numbers are embedded in the resolver

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-10**
- locations: `engine/src/resolve/resolve.cc:113`; `engine/src/resolve/resolve.cc:177`; `engine/src/resolve/resolve.cc:212`; `engine/src/resolve/resolve.cc:259-267`; `engine/src/resolve/resolve.cc:279`; `engine/src/resolve/resolve.cc:288`; `engine/src/resolve/resolve.cc:298`; `engine/src/resolve/resolve.cc:300`; `engine/src/resolve/resolve.cc:317`; `engine/src/resolve/resolve.cc:331`; `engine/src/resolve/resolve.cc:345-346`; `engine/src/resolve/resolve.cc:367-369`; `engine/src/resolve/resolve.cc:378-382`; `engine/src/resolve/resolve.cc:417-423`; `engine/src/resolve/resolve.cc:452-457`

Q1 inventory of synthesized presentation:
- "??" for unresolved refs.
- "↩", smuggled in as a label-table excerpt.
- " — " in term and glossary joins.
- "[", ", ", "]" in citations; "[n] " as the bibliography label.
- "(" and ")" around equation numbers.
- `number + " " + text` for TOC lines.
- CLS_BOLD for the caption prefix and term name.
- CLS_SUP and ×0.7 for the footnote marker; ×0.85 for note bodies.
- `rule` separators before the notes and the bibliography.
- An ordered list for notes, unordered lists for TOC and glossary.
- The `minLevel = 7` sentinel and the 1..6 heading clamp.

*Why ad hoc:* These are style and template decisions. The resolver should compute values (numbers, ordinals, targets, membership) and instantiate presentation that has been declared elsewhere. Today none of these can be overridden, and changing any of them requires an engine release.

*Proposed generalization (survey):* Every one of these becomes a default template of a built-in element class or collector preset, shipped as data. Either a C++ table of template sources parsed once with the inline parser, or a JS prelude that emits `meta` template declarations, so users override them through the same API.

The resolver then holds no literal strings other than diagnostics. The size multipliers become relative-size style patches inside templates. This needs a `size` (relative) patch key in `#style`: today only the absolute `sizePx` is exposed, and `sizeMul` is internal (engine/src/model/model.h:30-35).

*Verifier:* Every cited line checks out. The 0.7 and 0.85 factors are documented design values (notes-design.md:65-68), but documented values are still not overridable. `sizeMul` is not user-reachable: the style patch keys are font/lang/color/sizePx only (executor.mjs:225-229).

*Verifier notes:* Prefer C++-side default templates (a generated data table) over a JS prelude. A prelude's meta nodes would be recorded into every .ops fixture, so every golden would depend on, and churn with, the prelude. User overrides can still come through meta declarations.

### `resolver/anchor-namespace` — The anchor/URL scheme and the synthetic label prefixes leak into the user label namespace

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-04**
- locations: `engine/src/resolve/resolve.cc:111-116`; `engine/src/resolve/resolve.cc:149`; `engine/src/resolve/resolve.cc:211-212`; `engine/src/resolve/resolve.cc:263`; `engine/src/resolve/resolve.cc:292`; `engine/src/resolve/resolve.cc:348-349`; `engine/src/resolve/resolve.cc:376`; `engine/src/resolve/resolve.cc:421`; `engine/src/render/semantic_html.cc:46`; `engine/src/render/semantic_html.cc:137`; `engine/src/render/typeset_html.cc:213`

- The resolver builds URLs as `"#tsr-" + label` (in `mkLink`, `resolveRef` and `resolveCite`), while both serializers add the `tsr-` id prefix on their own. One contract is duplicated across three files.
- The synthetic labels `h-<n>`, `fn-<n>`, `fnref-<n>` and `bib-<key>` share one namespace with user labels. In probe p8-reserved.tsm, a heading `<fn-1>` captures the first footnote marker, which renders as a superscript "§1" linking to the heading; the only signal is a `label-duplicate` warning.
- `bib-<key>` anchors are not in the label table at all.
- Duplicate labels keep their `label` arg on figures, tables, equations and terms, so the DOM gets duplicate ids (p8: two `id="tsr-fig-a"`). Only headings fall back to an auto label.
- Term names (free text) become ids without URL escaping.

*Why ad hoc:* Anchor syntax (prefix, escaping) is a renderer concern and node identity is a model concern. The resolver hand-builds strings that encode both, and it reserves prefixes inside a namespace that users write into.

*Proposed generalization (survey):* Use two namespaces:
- User labels: validated (printable, no whitespace; a diagnostic otherwise).
- Engine anchors: `AnchorKey` (u32), assigned in Phase A to every addressable node and impossible for users to type.

Every addressable node gets `anchor:<num>`, and refs resolve to `targetAnchor` (document-model §5 already names `targetAnchor`). Both serializers share one helper, `anchorId(AnchorKey, StrRef label) → "tsr-" + (label ? escape(label) : autoName)`. On a duplicate label, the losing node keeps its anchor but drops the label, the same as headings today.

*Verifier:* Confirmed (p8: the marker renders '§1' and links to the heading; two `id="tsr-fig-a"`). Missed layer: the shell also hard-codes the scheme. `markerAt` matches `a.tsr-sup[href^="#tsr-fn-"]` and `bodyOf` strips `a[href^="#tsr-fnref-"]` (runtime/src/main/shell.mjs:146,175). In p8 the popup would therefore show the heading text 'Setup'.

*Verifier notes:* The proposal contradicts itself. It promises separate namespaces, and also byte-identical HTML by keeping the auto spellings (`fn-1`, `h-1.1`). But if auto ids still render as `tsr-fn-1`, a user label `fn-1` still produces the same DOM id.
There are two consistent options:
(a) Give engine anchors a DOM spelling that users cannot produce, by validating the label charset and using a reserved separator, and accept the HTML golden churn.
(b) Keep the spellings, but reserve and validate the h-/fn-/fnref-/bib- prefixes, with a diagnostic on user labels.
Numeric `AnchorKey`s must never reach DOM ids, because NodeIds are unstable across recompiles (document-model.md:24) and permalinks would break.

### `resolver/argk-overloading` — Arg keys are reused differently by each feature instead of having one generic meaning

- kind: adhoc · severity: medium · verdict: accurate · plan: **P0-06, P2-05, P2-07**
- locations: `engine/src/resolve/resolve.cc:193`; `engine/src/resolve/resolve.cc:200`; `engine/src/resolve/resolve.cc:208`; `engine/src/resolve/resolve.cc:404-406`; `engine/src/resolve/resolve.cc:432`; `engine/src/resolve/resolve.cc:376`; `engine/src/emit/emit.cc:756`; `engine/src/emit/emit.cc:466-467`; `runtime/src/worker/executor.mjs:260-264`; `engine/src/ops/ops.def:43-74`; `docs/notes-design.md:37`

- `ArgK::name` means four things: the term name (input), the bibentry key (input), the footnote number (resolver output), and the equation tag "(n)" (resolver output that emit consumes).
- `ArgK::form` is specified as the ref display form, but it is only ever used as the `collect{form:"all"}` flag.
- `ArgK::what` holds the collector name.
- `ArgK::label` on synthetic paragraphs serves as an anchor carrier, which emit honours (emit.cc:466-467).

notes-design.md:37 promised a dedicated `ArgK::number` that was never added.

*Why ad hoc:* Each feature picked a free key. What a key means depends on the node kind and on the pass, and the resolver talks to emit through these overloaded keys.

*Proposed generalization (survey):* A small set of generic keys, each with exactly one meaning:
- `class` (element class);
- `key` (entry key);
- `select`, `where`, `order` (collector query);
- `form` (ref form only);
- `anchor` and `targetAnchor` (identity).

Resolved display goes into content children built from templates, never into string args. Add a per-kind arg schema to ops.def (allowed keys and value tags), generating both the C++ validator (`ops-invalid` diagnostic) and the JS constructor argument checks.

*Verifier notes:* Per-kind arg namespaces are a documented convention: ops.def:74-75 says 'per-kind namespaces over one key' (for lang), and document-model.md:50 documents `name?` (resolver: "(n)") on mathblock. Reusing `name` across kinds is therefore deliberate. The real defect is narrower: resolver output is written into string args that only one consumer reads (emit's `eqTag`; the semantic serializer ignores it, see the missed items). A generated per-kind arg schema in ops.def, plus the rule 'resolved display is content, never a string arg', is sound and needs an OPS_VERSION bump.

### `resolver/rewrite-normalizations` — Structural cleanup (deleting empty paras, unwrapping blocks) lives in the resolver with its own inline-kind list

- kind: adhoc · severity: low · verdict: accurate · plan: **P0-07, P2-07**
- locations: `engine/src/resolve/resolve.cc:26-35`; `engine/src/resolve/resolve.cc:469-474`; `engine/src/resolve/resolve.cc:479-492`; `runtime/src/worker/executor.mjs:185-188`; `docs/document-model.md:34-60`; `engine/src/ops/ops.def:14-41`

- `rewrite` deletes every paragraph whose kids are all empty text. This was added for the `#bibliography` placeholder `text('')`, but it applies to any empty paragraph from any source.
- It unwraps a paragraph whose only child is a non-inline kind (the CH1 generalization of the term/collect case), using `isInlineKind`. That function is a hand-maintained copy of document-model §2.1's 'level' column, which ops.def does not encode.
- Block splices in the middle of a paragraph are not handled, and no diagnostic is raised. `See #toc() inline here.` produces a `list` inside a `para`, and `#term[…][…] Ref: …` produces a `group` inside a `para` (probe p6-misc.tsm).

*Why ad hoc:* These are model well-formedness rules that every consumer needs. They ended up in this feature pass because it happened to hit the problem first. The kind-level table is now duplicated: resolve.cc, document-model §2.1, and implicitly the switch cases in emit and the semantic serializer.

*Proposed generalization (survey):* Add the level to ops.def (`KIND(note, 27, inline)`, `KIND(error, 15, both)`) and generate `kindLevel()` for both C++ and JS. Normalize generically during instantiation (model.cc): hoist paragraphs whose only child is a block, and split paragraphs around block children (or emit a `block-in-inline` diagnostic). `#bibliography` returns its collector node directly, so the empty-paragraph deletion is no longer needed.

*Verifier:* `emptyPara` also deletes a paragraph with zero kids, not only one with empty text kids (resolve.cc:469-474). The unwrap rule is documented (document-model.md:135, 'CH1' generalization note at resolve.cc:486-489), so the behaviour is deliberate. Its location in the resolver and the hand-copied `isInlineKind` are the ad hoc parts.

*Verifier notes:* Encoding the level in ops.def with a generated `kindLevel()`, and normalizing at instantiation, is sound. Splitting mid-paragraph blocks, or emitting a `block-in-inline` diagnostic, fixes the p6 list-in-para case.

### `resolver/cite-ordinal-pass-order` — A citation inside an implicit footnote gets a later number, is left out of the bibliography, and links to an anchor that does not exist

- kind: issue · severity: high · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:234-241`; `engine/src/resolve/resolve.cc:370-372`; `engine/src/resolve/resolve.cc:400-416`; `engine/src/resolve/resolve.cc:524-529`; `runtime/src/worker/executor.mjs:241-266`

Citation ordinals are assigned when `rewrite` reaches a ref, not in document order. Note bodies are only rewritten inside `buildNotes`, and the implicit notes section is built after the whole tree has been rewritten (resolveDoc: rewrite, then `buildNotes`). The executor emits the bibliography collector at document end, so it is already built before the implicit notes. Scratch probe p1-cite-in-note.tsm: `First claim^[As shown by @knuth84.] and later @liang83.` gives liang83 = [1] and knuth84 = [2], even though knuth84 is cited first. The bibliography lists only liang83, and the footnote's "[2]" links to `#tsr-bib-knuth84`, which does not exist. Root cause: two counters use different ordering semantics (footnotes in scan, citations in rewrite), plus a cross-layer emission-order contract. Fix: assign first-use ordinals in Phase A (see citation-path).

*Verifier:* Reproduced independently (vr/p1-cite-in-note.tsm): liang83=[1], knuth84=[2], the bibliography contains only bib-liang83, and the footnote's link `#tsr-bib-knuth84` has no target. The bug needs the implicit notes section: an explicit `#notes()` always precedes the document-end bibliography collector.

*Verifier notes:* The minimal fix is golden-neutral for the current fixtures. Split the keys and call `citeOrdinal` in `scan`'s `Kind::ref` case, which already visits note bodies in place, and leave rewrite unchanged. The Phase-A firstUse design subsumes it.

### `resolver/extensibility-matrix` — Q2: what a user can define. Effectively none of it, and the blockers are structural

- kind: issue · severity: high · verdict: partly · plan: **P2-07**
- locations: `engine/src/resolve/resolve.cc:61-67`; `engine/src/resolve/resolve.cc:165-183`; `engine/src/resolve/resolve.cc:431-440`; `runtime/src/worker/executor.mjs:130`; `runtime/src/worker/executor.mjs:173`; `engine/src/api/wasm_api.cc:62-65`; `docs/design-decisions-v2.md:70`; `docs/architecture.md:143`

- New counter class: NO. Counters are struct members (resolve.cc:61-67). There is no `$` counter API, although v2:70 and architecture.md:143 say `$` exposes counters. There is no declaration op, and the documented config is not parsed (wasm_api.cc has only `tsr_set_lang`).
- Numbered environment (theorem, lemma, definition, exercise; HoTT): NO. `#!theorem(label:'thm-a')` becomes `group{role:'theorem'}` and its label is dropped, so `@thm-a` resolves to "??" (probe p2). Every workaround is broken. Borrowing `role:'figure'` gives '图 n：', centred captions (emit.cc:471-476) and `<figure>`. Counting in JS gives display text but no label entry, and miscounts under emission-time binding. The `#term` hack lands in the glossary with bold + ' — '.
- New collector (list of theorems, index): NO. `buildCollect` dispatches on strings, and anything else gives `collect-unknown` and an empty group. Scripts cannot query tables (by design), and the only way to declare a table entry is `term`.
- New reference display form: NO. The `ref(target)` constructor has no options (executor.mjs:173), and `form` is never read.
- Per-kind supplements: NO for documents; hosts can only pick zh vs English, the heading supplement is always '§', and there are no per-level words.
- Numbering schemes: NO. Everything is decimal; there are no resets, no roman/alpha numbering, and no unnumbered headings. Level gaps produce '1.0.1'.

Common blocker: the rules for counters, labels, display and collectors live in code paths selected by Kind, role or `what` strings. Apart from node kinds and a fixed set of args, execution has no way to declare anything to the resolver.

*Verifier:* 'its label is dropped' is wrong: the label stays on the group and becomes a DOM id (p2 tree). It is only never registered. 'Counting in JS … miscounts under emission-time binding' is overstated: JS counters number in construction order, which matches document order for normal top-level splices. The decisive gap is that nothing JS computes can enter the label table. Understated: a `$.region` handler cannot build any group or table, because no public constructor exists (executor.mjs:162-237, probe v2).

*Verifier notes:* The matrix is otherwise correct: no `$` counters despite design-decisions-v2.md:70 and architecture.md:143, `collect-unknown` for user collectors, and `ref(target)` taking no options.

### `resolver/invariant-declares-decides` — Q4: 'execution declares, resolver decides' holds for values, but the resolver also builds presentation and its correctness depends on walk order

- kind: issue · severity: high · verdict: partly · plan: **P1-10**
- locations: `docs/design-decisions-v2.md:233-242`; `docs/document-model.md:85-86`; `docs/notes-design.md:122-127`; `docs/notes-design.md:154`; `engine/src/resolve/resolve.cc:146-147`; `runtime/src/worker/executor.mjs:16-38`

- (a) Holds: scripts cannot read numbers, so document execution is single-pass and sound (v2 §11.1).
- (b) The resolver decides values AND builds presentation: bold caption and term-name paragraphs, ' — ' joins, '[n]', '(n)', '??', '↩', rules, list shapes, and 0.7/0.85 sizes (see presentation-constants). That presentation belongs to a template layer that the document or style declares.
- (c) One feature's presentation is split across two languages and two layers. The bibliography entry body comes from JS (`formatEntryDefault`), while the in-text '[n]', the '[n] ' label, the order and the rule are C++. So notes-design §2's 'rendering style is a JS function' only half holds.
- (d) Resolver correctness depends on walk order, not document order. Citation ordinals are assigned in rewrite order (bug cite-ordinal-pass-order), implicit notes are built after rewrite, and the executor must emit the bibliography last (notes-design.md:154).
- (e) Scan-time excerpts read content that has not been resolved yet: the TOC includes note bodies and drops refs and math.
- (f) v2 §11.1's fallback, 'computed numbering is done in user JS with user counters', is unsound under emission-time binding. document-model §3 copies a DAG value every time it is emitted, so a JS counter counts constructions, not occurrences: a theorem stored in a `#let` and emitted twice is numbered once. Positional numbering must stay in the resolver; the fix is to let JS declare classes, not to have it count.

*Verifier:* Points (a) to (e) are confirmed. Point (f) calls v2 §11.1's fallback 'unsound'. The fallback is documented, and construction-time numbering is a coherent semantics; it differs from per-occurrence numbering only for values stored and emitted more than once. The actual unsoundness is that JS-computed numbers cannot be referenced (no label entry), and that labels duplicate when a value is emitted twice (model.cc:38-55 copies per EMIT).

*Verifier notes:* The conclusion stands: execution should declare classes and the resolver should count. Presentation built inside the resolver is the main leak.

### `resolver/collector-aliasing` — Repeating #notes() or #bibliography() puts the same nodes into two subtrees (tree becomes a DAG): double rescale and duplicate anchors

- kind: issue · severity: medium · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:352-357`; `engine/src/resolve/resolve.cc:370-380`; `engine/src/resolve/resolve.cc:201`; `engine/src/resolve/resolve.cc:418-424`

`buildNotes` pushes the note's original kid pointers into a new para and calls `rescale(k, 0.85f)`, which mutates their styles. `buildBibliography` pushes `bib[key]->kids` the same way. A second `#notes()` reuses the same nodes. In probe p4-two-notes.tsm, both sections show the body at ×0.72 (0.85²) and two `fn-1` anchors. In probe p7-two-bibs.tsm, the second bibliography reuses the FIRST file's entry nodes, because `bib` keeps the first entry for each key, and repeats the `bib-kp81`/`bib-knuth84` anchors. There is no diagnostic. A flow model ('each item placed exactly once', copies via clone) prevents this by construction.

*Verifier:* p4 is reproduced (×0.72, two `label="fn-1"`, shared node pointers). In p7 both bibliographies load the same file, so 'reuses the FIRST file's entry nodes' is shown by the code, not by the probe: `bib` keeps the first entry per key (resolve.cc:201) and `buildBibliography` reads `bib[key]` (419). Each bibliography also lists every cited key, because `citeOrder` is global.

*Verifier notes:* Clone-on-place with anchors dropped from copies is right. Multiple bibliographies are a copy use case, so per-bibliography anchors must be decided explicitly.

### `resolver/reserved-label-collision` — A user label can capture a footnote marker or another synthetic anchor

- kind: issue · severity: medium · verdict: partly · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:211-212`; `engine/src/resolve/resolve.cc:119-128`; `engine/src/resolve/resolve.cc:149`

Labels are first-wins in document order, and `fn-<n>`/`fnref-<n>`/`h-<n>` share the user namespace. In probe p8-reserved.tsm, `= Setup <fn-1>` followed by `A claim^[the body].` renders the marker as superscript "§1" linking to the heading. The footnote body is still labelled `fn-1`, so the semantic HTML gets two `tsr-fn-1` targets. Only a `label-duplicate` warning is emitted. See anchor-namespace for the fix.

*Verifier:* 'so the semantic HTML gets two tsr-fn-1 targets' is wrong. The semantic page has one, the heading's, because the tight `<li>` path drops the note para's id (semantic_html.cc:207-209). The typeset HTML has two (`--stage=html` on p8 shows `id="tsr-fn-1"` twice). The shell popup then shows the heading text (shell.mjs:130,175).

*Verifier notes:* The fix inherits the needs_change from anchor-namespace: reserve and validate the prefixes, or give engine anchors a DOM spelling users cannot type.

### `resolver/absolute-style-loss` — The figure caption prefix and term name use an absolute style and drop the inherited font, lang, color and size

- kind: issue · severity: medium · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:175-177`; `engine/src/resolve/resolve.cc:450-452`; `engine/src/resolve/resolve.cc:342-347`

`styles.idOf(Styling{CLS_BOLD, 1.0f})` (resolve.cc:177, 452) throws away the enclosing style. In probe p5-style.tsm, the figure prefix '图 1：' loses `color=blue` and the term name '名稱' loses `font=Kai lang=zh-TW color=red`, while their siblings keep them. CJK text without a lang tag also loses 'locl' forms. `buildMarker` (resolve.cc:344) does it correctly by combining with `note->style`. The fix is a single helper, `compose(parentStyle, +bits, ×mul)`, used by every synthesized node.

*Verifier:* Reproduced (p5). The same base-style problem also affects `mkLink` text (TOC and glossary links, resolve.cc:111-115) and the synthesized list/item/para nodes in `buildNotes` (style 0).

*Verifier notes:* Use one `compose(parent, +bits, ×mul)` helper for every synthesized node. Emit already has a `compose` (emit.cc:466) that could be shared.

### `resolver/semantic-tight-item-anchor` — The no-JS HTML loses footnote body anchors, so markers point at nothing

- kind: issue · severity: medium · verdict: accurate · plan: **P0-09**
- locations: `engine/src/render/semantic_html.cc:204-213`; `engine/src/resolve/resolve.cc:374-376`; `docs/notes-design.md:92-93`

The resolver anchors each note body by putting `label="fn-n"` on the para inside the list item (resolve.cc:376). The semantic serializer renders a single-para item as tight via `inlineKids(b)`, which skips `attrs()`, so the label is dropped (semantic_html.cc:208-211). `tsrc --stage=semantic` on test/fixtures/notes/basic.tsm shows `<a href="#tsr-fn-1" …><sup>1</sup></a>` but `<li>The note body…` has no id, so the link has no target before the typeset upgrade. This is a cross-layer contract ("paragraphs honor label as anchors", notes-design.md:93) that only emit honours. Generic anchors on any node (anchor-namespace) plus one shared `attrs()` path fix it.

*Verifier:* The actual lines are semantic_html.cc:207-209. Confirmed against the committed golden test/golden/notes/basic.semantic.txt:9-10 (`<li>` without an id). On the semantic page the shell popup's `bodyOf` therefore finds no element.

*Verifier notes:* The minimal fix is to emit the single para's label as the `<li>` id. The general fix, one shared `attrs()` path for every anchored node, is right.

### `resolver/duplicate-label-dom-ids` — Duplicate labels on figures, tables, equations and terms produce duplicate DOM ids

- kind: issue · severity: low · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:160-162`; `engine/src/resolve/resolve.cc:168-170`; `engine/src/resolve/resolve.cc:187-192`; `engine/src/resolve/resolve.cc:219-222`

Only headings switch to an auto label when `addLabel` fails (resolve.cc:150-154). The other kinds keep their `label` arg, and emit and render turn it into `id="tsr-<label>"` twice (probe p8: two `id="tsr-fig-a"`). A term named like an existing label (probe p6: term 'intro' vs heading `<intro>`) also keeps `label="intro"`. Because instantiation copies values per emission, a labelled value emitted twice will hit this as well.

*Verifier notes:* Dropping the label from the losing node, as headings already do at resolve.cc:150-154, generalizes cleanly.

### `resolver/spec-drift` — Much of the normative resolver contract in the docs is not implemented, and the resolver's tables are not kept

- kind: issue · severity: medium · verdict: accurate · plan: **P1-10**
- locations: `docs/document-model.md:15`; `docs/document-model.md:120-135`; `docs/document-model.md:256`; `docs/document-model.md:310-327`; `docs/notes-design.md:30-38`; `docs/notes-design.md:70-74`; `engine/src/api/doc.h:74`

Documented but missing:
- document-model §0 says the label, term and bib tables live on the handle. They are locals of `resolveDoc` (doc.h:74), so editors get no API for go-to-label or label completion.
- §5: the label entry with `{nodeId, counters}`, `form`, `lof`, `targetAnchor`, and counter classes `<user>` with `resetAt`.
- §11: the JSON config `supplements`/`counters` (there is no config parser).
- §9.2: `term→dl>dt+dd`, `collect→nav|section`. The renderer never sees these kinds; it emits `div[data-role]`.
- notes-design: `ArgK::number`, `#note(name)`, `note-undefined`, `resetAt`, `noteMarks`, the `.tsr-notes`/`.tsr-bib` classes.
- v2 §11.1 and architecture.md:143: `$` counters/labels.

The status lines call the docs 'normative' (document-model.md:3), so readers and converter authors (tools/convert/tex2tsm.mjs) design against features that do not exist.

*Verifier:* `Resolver r` is a local in `resolveDoc` (resolve.cc:523), so the tables do not survive on the handle. No `.tsr-notes`/`.tsr-bib` classes exist anywhere in engine/src or runtime/src.

*Verifier notes:* This is drift between docs and code, not special-casing. Either mark these sections as not implemented, or implement them.

### `resolver/grouped-cite-all-or-nothing` — In a grouped citation, one unknown key turns the whole group into '??', and the diagnostic names the joined string

- kind: issue · severity: low · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:244-258`; `engine/src/resolve/resolve.cc:274-280`

`resolveCite` returns false if any key is missing (resolve.cc:257-258), so `@[kp81, nosuch]` renders "??". The `ref-unresolved` message then quotes the whole target 'kp81, nosuch' instead of the bad key. A key that is both a label and a bib id silently resolves to the label, with no shadowing diagnostic.

*Verifier:* Label-before-bib precedence is documented (notes-design.md:118-119). The silent shadowing is the undocumented part.

*Verifier notes:* Resolve each key separately, with a per-key diagnostic and a shadowing warning.

### `resolver/resolver-spans` — Resolver-built nodes for splice-built collectors and terms have empty [0,0) spans

- kind: issue · severity: low · verdict: accurate · plan: **P2-07**
- locations: `engine/src/codegen/codegen.cc:67-93`; `engine/src/resolve/resolve.cc:296-336`; `docs/document-model.md:26`

document-model §1 says resolver-produced content takes the span of its REF/COLLECT site. Trees for the TOC, glossary, term groups and bibliography show `@[0,0)` (test/golden/doc/refs.tree.txt; cite/basic). Codegen does not wrap splices in `__at` (engine/src/codegen/codegen.cc:67-93), so `collect`/`term` nodes arrive without a span, and `mkNode(…, c->span)` copies the empty span. Diagnostics such as `collect-unknown` cannot point at the source.

*Verifier:* This affects more than collectors and terms. The splice-built `#ref("intro",…)` in p6 has `@[0,0)`, so `ref-unresolved` from a `#ref()` call cannot point at its source either.

*Verifier notes:* Either codegen wraps splice values in `__at`, or the resolver inherits the para span when it unwraps.

### `resolver/fragile-aggregate-init` — Resolver is constructed by positional aggregate initialization

- kind: issue · severity: low · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:523`

`Resolver r{arena, strs, styles, cfg, diags, {}, {}, {}, {}, 0, 0};` relies on member declaration order: the trailing `0, 0` initialize `tableNo` and `figNo`. Adding or reordering a member, which every new feature has done, silently shifts these initializers. Use a constructor or designated initializers.

*Verifier notes:* Members have default initializers, so `Resolver r{arena, strs, styles, cfg, diags};` or designated initializers would be enough.

### `resolver/untested-diagnostics` — Resolver diagnostics and edge cases have no test coverage

- kind: issue · severity: low · verdict: accurate · plan: **P0-09**
- locations: `engine/test/tests.cc`; `test/golden/doc/refs-diag.tree.txt`

There are no `.diags` goldens, and engine/test/tests.cc has no resolver tests. `refs-diag` and `unknown-diag` check only the tree, not the emitted diagnostics. None of the issues found here with scratch probes (p1 citation in a note, p4 double #notes, p7 double bibliography, p8 reserved-label capture, p2 theorem label drop) is caught by CI.

*Verifier:* Adding goldens alone is not enough. `tsrc --stage=diags` dumps diagnostics before ingest (native_cli.cc:72), so resolver diagnostics are unobservable from the CLI. The golden runner has no diags stage (tests.cc:433-465).

*Verifier notes:* Add a post-ingest diagnostics golden stage.

### `resolver/missed:0` — No public constructor for the containers the resolver numbers (group / table / figure); `$.region` handlers cannot build them

- kind: missed · severity: high · verdict: verifier-found · plan: **P2-03**
- locations: `runtime/src/worker/executor.mjs:116-139`; `runtime/src/worker/executor.mjs:162-237`; `engine/src/codegen/codegen.cc:158`; `engine/src/codegen/codegen.cc:209-212`; `docs/design-decisions-v2.md:107`; `docs/document-model.md:66`

The generic region fallback `group{role:name, label}`, `tableBuild` and `figureBuild` are reachable only through `#!name` syntax or the internal `__region`. The ctor set has para/list/quote/heading/… but no `group` and no `table`. A user handler registered with `$.region('theorem', fn)` cannot return a labelled block container. My probe vr/v2-region-handler.tsm returns a group-shaped object, and it renders as the text "[object Object]" while `@thm-a` resolves to "??".
This violates the language's governing principle (v2:107: 'every syntax form is sugar for a constructor … guarantees every syntactic capability has a programmable equivalent') and document-model §2.1's 'user constructors compose engine kinds'. Even after the resolver gains element classes, user environments could not opt in from JS.

*Proposed generalization (survey):* Add public `group(opts, ...kids)` (role, label, class and style options), `table(opts, rows)` and `figure(opts, ...kids)` constructors. Make `#!name` desugar to `(userRegions[name] ?? builtinRegions[name] ?? group)`, and register the built-in table and figure builders as ordinary region handlers that users can wrap or override.

### `resolver/missed:1` — The no-JS semantic HTML drops equation numbers because the resolver's tag is a string arg that only emit reads

- kind: missed · severity: medium · verdict: verifier-found · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:193`; `engine/src/render/semantic_html.cc:260-266`; `engine/src/emit/emit.cc:756`; `engine/src/render/typeset_html.cc:232-240`; `test/golden/math/eqref.semantic.txt:3-5`; `docs/design-decisions-v2.md:241`

The resolver writes "(n)" into `ArgK::name`. Emit copies it to `FlowUnit.eqTag`, and only the typeset renderer prints it. The semantic serializer's mathblock case prints `$ src $` with no number. The committed golden shows `<p class="tsr-mathblock" id="tsr-emc">…$ E = m c^2 $…</p>` next to prose that says '式 (1)'. This breaks v2 §11.1's promise that 'native-fallback HTML already carries final numbers'. It is a direct consequence of resolver output being a per-consumer arg instead of content.

*Proposed generalization (survey):* Make it a rule that the resolver's display output is always content in the tree, never a string arg for one serializer. The equation number becomes a tag child (the `tag` site template) that both serializers walk. The semantic serializer then gets equation numbers, and heading or theorem numbers too, without extra work.

### `resolver/missed:2` — The shell's footnote popup keys on resolver-internal URL prefixes and the superscript CSS class

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-04, P3-06**
- locations: `runtime/src/main/shell.mjs:125-176`; `engine/src/resolve/resolve.cc:211-212`; `engine/src/resolve/resolve.cc:348-349`; `engine/src/render/typeset_html.cc:39`

`markerAt` matches `a.tsr-sup[href^="#tsr-fn-"]`. `bodyOf` finds the target id and rebuilds the body by walking sibling `.tsr-line` boxes up to the next id or marker, stripping `a[href^="#tsr-fnref-"]`. So the main-thread shell depends on the resolver's synthetic label spelling, the CLS_SUP→`tsr-sup` class, and the notes list's DOM layout.
With p8's `= Setup <fn-1>`, the popup shows the heading 'Setup'. On the semantic page the `<li>` has no id, so there is no popup. A prose `@fn-1` (not superscript) gets none. Any user-built note-like construct cannot get one.

*Proposed generalization (survey):* The flow model (footnote-pipeline) should emit declared semantic attributes. For example, markers carry `data-flow="notes" data-flow-item="<anchorId>"` and bodies carry `data-flow-item` on their container. The shell keys only on those, so any declared flow class (sidenotes, endnotes) gets popups without depending on spelling.

### `resolver/missed:3` — A table inside a figure gets two numbers, because counters bind to node kind and not to the captioned unit

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-03**
- locations: `engine/src/resolve/resolve.cc:158-170`; `docs/figure-design.md:28-31`

figure-design §1 explicitly supports '#!figure … a figure whose body is a table'. In probe vr/v1-fig-table.tsm the figure becomes '图 1：' and the inner table independently steps `tableNo`, so `@fig-t`→'图 1' and `@tab-in`→'表 1' for the same object. There is no notion that a nested countable is already counted by its container, and no way to say 'this figure is a Table'.

*Proposed generalization (survey):* Make the counted unit the captioned container, as in Typst's `figure(kind:)`. The container's kind selects the counter, auto-detected from its body (table→'table', image→'figure') or declared, and countables nested inside a counted container are not counted separately. In the element-class registry this is one rule: a class may declare `counts: 'container'` together with a kind→counter map.

### `resolver/missed:4` — Footnote numbers have two independent sources: the scan counter for markers and the list ordinal for the notes section

- kind: missed · severity: low · verdict: verifier-found · plan: **P3-03**
- locations: `engine/src/resolve/resolve.cc:206-208`; `engine/src/resolve/resolve.cc:358-376`; `engine/src/emit/emit.cc:520-535`

Markers display `noteNo`, which `scan` stores in `ArgK::name`. The notes section rebuilds the number as `i + 1` for the `fn-n` labels and relies on `<ol>` list-marker numbering for the visible number ('the list marker IS the number', resolve.cc:359). The two agree only because `notes` is filled in scan order and placed once. Any planned numbering feature, such as per-section reset (notes-design.md:34-36), circled marks (72-74) or symbol marks, changes the marker but not the list ordinal. The notes section is numbered by the list renderer, not by the resolver.

*Proposed generalization (survey):* The entries of the notes collector carry their number as content from the counter snapshot (entry template `[number] [body] [backref]`). The list is unnumbered, or its marker comes from the entry. Labels and anchors come from the FlowItem, never from list position.

### `resolver/missed:5` — Golden blind spots for resolver output: the tree dump cannot show CLS_SUP, and resolver diagnostics cannot be dumped

- kind: missed · severity: low · verdict: verifier-found · plan: **P0-09, P1-03, P1-10**
- locations: `engine/src/model/model.cc:109-125`; `engine/src/api/native_cli.cc:72`; `engine/test/tests.cc:433-465`

`styleStr` is a hand-maintained bit→name list. It was not updated when CLS_SUP (bit 19, model.h:22) was added, so footnote markers dump as `[basex0.70]` and the sup bit is outside the byte-exact tree contract. `--stage=diags` dumps before ingest, so `ref-unresolved`, `label-duplicate` and `collect-unknown` never appear in any golden. A regression that drops the marker bit or changes diagnostics would pass the tree stage, and only partly show in later stages.

*Proposed generalization (survey):* Generate the style-bit name table from a single declaration, in the same X-macro style as ops.def, so a new bit cannot be left out of the dumps. Add a post-ingest diagnostics stage to tsrc and to the golden runner.

## emitter — Emitter: content tree -> block streams / flow units (script segmentation, CJK, hyphenation, inline boxes)

<details><summary>Design summary (as audited)</summary>

emitDoc (engine/src/emit/emit.cc:891) walks the resolved ContentTree's top-level children. Each child becomes a TopBlock (pid) holding FlowUnits. Block dispatch is a Kind switch in Emitter::blockWalk (emit.cc:459-826):
- para / heading / error -> K::Text
- codeblock -> K::Code: per-line CodeRuns, plus an optional sidecar stored in `cells`
- rule -> K::Rule
- table -> K::Table: row-major TableCell streams
- raw -> K::Raw
- image -> K::Image
- mathblock -> K::Math
- list / quote / group / default recurse, carrying indent, marker and pendingAnchor state

Two role strings change behaviour. `group{role:'figure'}` (emit.cc:781) either turns every descendant paragraph into a caption (figDepth) or packs image + captions into a float unit. `group{role:'sidecar-lines'}` (emit.cc:588) is synthesized by Doc::extractSidecars.

Inline dispatch (inlineWalk, emit.cc:43-174) handles text, link, code, ref (BF_REF, anchorId, and a CLS_SUP-keyed glue rule), error, mathinline (MathSeg -> MathBox blocks plus break glue), group (anchor) and comment. Every other kind falls into a recursive default and leaf kinds vanish.

The stream unit is the PoC-derived LinebreakBlock: one fused item holding box width, glue capacity (spaceWidth), stretchWeight, a break-after penalty, and a 10-bit BF_* flag set. emitText (emit.cc:270-456) is a Latin/CJK/Punct state machine run separately for each text node. It implements v2 Appendix C:
- each CJK char is a block with 0.1em capacity and weight k;
- punctuation is a glyph plus breakable half-em blocks, with three compression modes;
- 0.25em boundary glue is inserted at ideograph/Latin transitions;
- ——/…… become defined-width pairs;
- Latin words go to emitWord, which applies en-US Liang hyphenation to the ASCII core, or URL-style cut points otherwise.

fillSpaceContexts then tags spaces and hyphens with kerning-context strings. resolveWidths (emit.cc:912) fills widths from the MetricStore using per-flag rules (hyphen breakWidth + junction kern, punct glyph minus 0.5em, space context kern + epsilon) and reports what is still missing. Emit never measures, except that inline math reads metrics through MathTextCtx.

Downstream re-dispatches on the same kinds:
- Doc::typeset (api/doc.h:245) runs a per-kind break loop (float tracker, tables, sidecars, text) over a content-hash-cached KP.
- layoutDoc branches on FlowUnit::K, emits magic LineBox::special codes, and re-derives semantics from BF flag combinations.
- renderLineBox branches on special codes and flags.

There is no generic inline-box abstraction (math is the only inline object) and no block-box abstraction (FlowUnit is a fat struct holding every kind's fields).


Strengths:

- Clean measurement seam: emit produces strings + StyleIds and never calls a measurer; resolveWidths returns a deduped MeasureRequest, which keeps emit deterministic and native-testable with the mock (document-model §6-§7).
- Fixed-point su for the breaker and raw px for justification (document-model §6.1), with the epsilon policy applied at one place (MetricStore::provideWord) — a sound split between overflow safety and edge precision.
- Defined-not-measured widths are an explicit, documented concept (——/…… pairs, math boxes from precompiled metrics, code ch grid) rather than hoping canvas and DOM agree — the right instinct, worth generalizing.
- The KP breaker is already generic over a block vector + LineWidths and is reused for table cells, sidecar rows and float captions; the parshape prefix form needed zero DP changes (figure-design §8); the break cache keys exactly the inputs the DP reads (break.cc breakKey).
- Evidence-driven fixes with goldens: cross-space and hyphen-junction kerning budgets, style-boundary exclusion, URL breaks, Latin-context quotes and no boundary glue beside fullwidth punct all trace to real-world-report.md defects with fixtures (style/kern-boundary, inline/quotes).
- App C semantics are mostly right where they apply: kinsoku INF before closers and after openers, line-edge compression via breakable halves that are trimmed, and three documented compression modes with clreq as the normative reference.
- Copy fidelity is designed in: synthetic content is flagged (BF_REF, BF_BOUND/INDENT/PUNCT_SP drive data-join='none'), and math carries its source on the first segment.
- Math line integration follows TeX (break only between top-level atoms, three penalty classes, discardable break glue) and was designed with a general inline-box extents field in mind (math-design §9).
- Every stage has a byte-exact dump (tsrc --stage=blocks/breaks/layout/html), which made each defect in this review reproducible in one command.

</details>


### `emitter/paragraph-blind-script-context` — Script/kinsoku context is reset per text node; math alone got cross-node patches

- kind: adhoc · severity: high · verdict: partly · plan: **P4-02**
- locations: `engine/src/emit/emit.cc:277-278`; `engine/src/emit/emit.cc:293-299`; `engine/src/emit/emit.cc:113-118`; `engine/src/emit/emit.cc:354`; `engine/src/emit/emit.cc:402-412`; `engine/src/emit/emit.cc:434-443`; `engine/src/emit/emit.cc:43-47`; `docs/notes-design.md:89`

emitText starts a fresh `Prev prev = Prev::None` state machine for every text node, and its one-character look-ahead (`cp2`) stops at the node end. Styling markup, links, refs and inline code all split text nodes, so typography changes with markup. Probes (blocks dumps) confirm:
- `中文*English*中文和`code`中文`: no 0.25em boundary glue anywhere; 'lish' (pen=INF) is followed directly by '中' and 'code' (pen=INF) directly by '中', so there is no break opportunity at either script edge.
- `中文[link](...)中文`: the same.
- `，“*强调*”。`: the opening “ ends its text node, so cp2==0 sends it down the Latin path (word block in the body font) while ” takes the CJK punct path (CJK font, half-space). One quote pair renders in two fonts with different spacing.
- `他说：*“Hello”*之后`: both quotes are treated as Latin, whereas the same text without emphasis (golden inline/quotes.blocks.txt:91-101) treats them as CJK.
Only inline math got patches that look across node edges: CJK->formula boundary at :114, formula->CJK at :293-299, and `|| back().math` in kinsoku at :354. notes-design.md:89 records it: 'cross-node boundaries were never inserted'.

*Why ad hoc:* The rules concern adjacent glyphs in the rendered line, but they are evaluated on content-tree node boundaries. Style structure is metric-neutral per v2 §12, yet here it changes spacing and breaking. Each inline kind that needs context gets its own patch (math has three), and other inline kinds (code, link, ref, image) get none.

*Proposed generalization (survey):* Flatten first, then shape the paragraph in one pass (component P2):
- `struct Atom { u32 cp; /*U+FFFC for objects*/ const InlineBox* obj; StyleId style; StrRef link; u32 srcOff; u16 attrs; }`.
- inlineWalk only flattens a unit into `std::vector<Atom>` and composes styles. It no longer decides breaks or spacing.
- `Shaper::run(atoms, rulesFor(style))` then makes one pass over the whole unit:
  1. resolve TypoClass for every atom, settling ambiguous quotes and dashes from strong neighbours across style edges;
  2. look up the spacing matrix and kinsoku table for each adjacent pair;
  3. emit P1 items, cutting runs at style/link/script changes.
- Style edges cut runs but never reset context.
- Inline objects declare a TypoClass (math/code = Alpha-like, image = Ideo-like or per arg), so the same matrix cells apply to them.
This lives in a new engine/src/shape/. emit.cc keeps only tree traversal.

*Verifier:* Confirmed (my blocks probe): with `中文*English*中文和`code`中文` and `中文[link](..)中文` there is no boundary glue, and 'lish' (pen=INF) and 'code' (pen=INF) sit directly before 中, so there is no break at the Latin->CJK edge. `，“*强调*”。` gives `word "“"` next to `punct-close "”"`.

Overstated: kinsoku-before-closer and punct-adjacency compression already work across node edges, because they read the shared stream:
- lastIsCloseSp/lastIsOpenGlyph at emit.cc:300-307;
- `u.blocks.back()` at emit.cc:354.
Probe `中文*强调*）后（*括号*）` gives 调 pen=INF before ）, and the halves pop correctly. Only `Prev prev` (emit.cc:278) and the cp2 look-ahead (emit.cc:404, 437) reset per node. So 'math alone got cross-node patches' is wrong: the CLS_SUP rule (emit.cc:89-90) and the punct helpers also reach back.

Wrong: the report says `他说：“Hello”之后` without emphasis is set as CJK. My probe of that exact unmarked text gives `word "“Hel"` (Latin) and `punct-close "”"` (CJK). Prev::Punct counts as non-CJK at emit.cc:435. The cited golden (inline/quotes.blocks.txt:91-101) is different text, 时“English inside”也, where prev is an ideograph.

*Verifier notes:* Not documented as a choice; notes-design.md:89 only records it as a fact. The current stream is half paragraph-level through retroactive edits (pop_back of halves, `back().breakPenalty = INF` in four places). That makes correctness depend on node order. The shaper should own both look-behind and look-ahead.

The proposal fits §12, because StyleIds are already folded per node. Two additions:
- Atom offsets need a per-node offset map, since text is not 1:1 with the source (see coarse-source-spans).
- The shaper should also decide break-after for inline objects (see missed item 'break after inline formula').

### `emitter/hardcoded-script-class-tables` — CJK/punctuation classes are hand-written ranges and switch lists, duplicated in 5 layers, SC-only

- kind: adhoc · severity: high · verdict: partly · plan: **P4-05**
- locations: `engine/src/support/support.h:158-190`; `engine/src/emit/emit.cc:396`; `engine/src/emit/emit.cc:407`; `engine/src/inline/inline.cc:50`; `engine/src/layout/layout.cc:168-171`; `engine/src/layout/layout.cc:239-250`; `engine/src/render/typeset_html.cc:402-406`; `engine/src/emit/emit.cc:841`; `engine/src/emit/emit.cc:852`; `engine/src/measure/mock.h:11`; `test/fixtures/style/patch.tsm`

Script and punctuation classes are hard-coded:
- `isCjk` is five literal ranges. It has no Hangul (AC00-D7AF/1100-11FF, which therefore become Latin words). It counts U+3000 ideographic space and all of FF00-FFEF as stretchable 'ideographs'.
- isPunctOpen/isPunctClose are switch lists. Missing: 〖〗〘〙｟｠, ・(30FB), ー(30FC), 々(3005), small kana, 〜, ·, ‼, ⁇. This means jlreq non-starters may begin a line.
- U+2014/U+2026 are made 'CJK-class' by inline code twice (emit.cc:396 and :407) and a third time in the soft-wrap joiner (inline.cc:50 `cjkish`).
- Code-grid wrapping re-implements kinsoku with a different rule (layout.cc:245-250): `isCjk` includes punct, there is no consecutive-closer handling, and a separate ASCII breakable set sits at :168-171.
- The renderer's snap split, the kern-context cutoff `cp >= 0x2000`, and the mock measurer each classify on their own.
Emit never reads Styling::lang. A zh-TW scope (fixture style/patch.tsm) therefore gets Simplified-Chinese punctuation geometry, although clreq (positioning of punctuation marks) centres TC 。，、：；. The 0.5em right-half squeeze then eats real ink.

*Why ad hoc:* Script behaviour is code rather than data. Supporting Japanese non-starters, Korean keep-all, or TC positioning means editing support.h, emit, inline, layout and render together, and the copies can drift (they already differ between emit and layout).

*Proposed generalization (survey):* Generate a class table and per-locale tailorings (component P2 TextRules):
- `engine/rules/typo-classes.def` is generated from UCD (LineBreak.txt UAX#14, EastAsianWidth.txt UAX#11, Scripts.txt UAX#24) by a tool, the way hyphc/ops.def work.
- It compiles to a two-level trie behind `TC classOf(u32 cp)`, with TC in {Ideo, Kana, SmallKana, Hangul, Alpha, Digit, Open, Close, Dot, Comma, Colon, MiddleDot, Dash, Ellipsis, IterMark, Prolonged, AmbQuoteOpen, AmbQuoteClose, Space, IdeoSpace, Object, Other}.
- Per-locale tailorings live in `engine/rules/{zh-Hans,zh-Hant,ja,ko,und}.def` and compile to:
  `struct TextRules {
    TC override(u32 cp, TC base) const;
    KinsokuSets k[3] /* strict|normal|loose: noStart, noEnd, noSplit bitsets over TC */;
    SpacingCell spacing[TC_N][TC_N];
    Blank blank[TC_N];
    DefinedAdvance defined[];
    u8 hyphDict, indentEm8;
    AutoSpace autospace;
  };`
  The table is reached through `const TextRules& rulesFor(StrRef bcp47)` with a fallback chain (zh-TW -> zh-Hant -> zh -> und).
- One API serves every consumer: the inline soft-wrap joiner (`rules.joinsWithoutSpace(prev, next)`), the shaper, the code-grid wrap (kinsoku sets plus a 'code' tailoring), renderer snap splitting, kern-context eligibility, and the mock measurer.
- A RULES_VERSION stamp keeps goldens reproducible.

*Verifier:* Confirmed:
- isCjk (support.h:158-162) has no Hangul syllables or jamo (AC00-D7AF, 1100-11FF).
- U+3000 and all of FF00-FFEF (including halfwidth katakana) are stretchable ideographs; probe: `中　文` gives cjk "　" glue=115su.
- 〖〗｟｠・ー々, small kana and 〜 are not in isPunctOpen/isPunctClose, so a break before them is allowed.
- 0x2014/0x2026 appear at emit.cc:396 and :407 and inline.cc:50.

Wrong: 'no consecutive-closer handling' in the grid. layout.cc:245-250 refuses a break whenever the next cp is isPunctClose, which covers closer+closer.
The real emit-vs-grid divergences are:
- curly quotes are not isCjk, so the grid treats them as unbreakable Latin;
- there is no break opportunity at Latin->CJK in the grid (only after a CJK char), whereas emit breaks at boundary glue in both directions;
- U+2014/U+2026 take 1 column in the grid;
- a separate ASCII breakable set (layout.cc:168-171).

*Verifier notes:* v2 §14 (design-decisions-v2.md:267) documents 'Simplified Chinese first'. The TC/ja/ko gaps are deliberate scope. The duplication of classifiers across five sites is not.

The mock measurer (mock.h:8-13) is normative and mirrored in runtime/src/shared/mockmeasure.mjs. If it reads the generated table, then a table regeneration or RULES_VERSION change silently moves every golden width. Either generate both mock copies from the same .def under a pinned version, or freeze the mock's own classifier.

The proposal should list UAX#14 break-control classes (ZW, WJ, GL, SHY/BA) explicitly; the emitter ignores them today (see missed).

### `emitter/punct-compression-control-flow` — clreq adjacency matrix and the 0.5em blank are spread across emit branches, a width rule, a renderer heuristic and CSS

- kind: adhoc · severity: high · verdict: accurate · plan: **P4-04**
- locations: `engine/src/emit/emit.cc:331-374`; `engine/src/api/config.h:20`; `engine/src/api/config.h:50`; `engine/src/api/config.h:76`; `engine/src/emit/emit.cc:955-962`; `engine/src/render/typeset_html.cc:502-515`; `runtime/src/main/shell.mjs:41-42`; `engine/src/layout/layout.cc:73`; `engine/src/layout/layout.cc:546`; `docs/design-decisions-v2.md:383`

The rule is implemented in four places:
- pushPunct encodes close+open, open+open and close+close for {Full, Book, None} as nested branches that pop the previous half-space block or push rigid/breakable ones.
- resolveWidths re-derives the glyph width as measured minus kPunctHalfEm·em, clamped to 0 (emit.cc:958-960). The clamp silently hides fonts and locales where the glyph is not fullwidth.
- The renderer infers afterwards whether a half survived by inspecting neighbours (`halfPresent`) and adds class tsr-sqL/tsr-sqR.
- The CSS for those classes (`margin:-0.5em`) is a second copy of kPunctHalfEm, measured in a different em than the engine's (see other_issues sizepx-em-mismatch).
The blank side is fixed by the open/close class (the Simplified Chinese convention), with no per-locale geometry. punctCompress is a document-global Config enum. The adjacency rule is documented and deliberate (v2 App C, design-decisions-v2.md:383); its encoding is what is ad hoc.

*Why ad hoc:* clreq, jlreq and JIS X 4051 state this as a 2-D table (class x class -> spacing amount, breakability, compressibility) plus a per-glyph blank model. Here it is hand-written control flow in emit, a special width rule, a renderer heuristic, and a stylesheet constant.

*Proposed generalization (survey):* Put the rule in TextRules:
- Blank model: `struct Blank { u8 left8, right8; }` per class and locale, in eighths of an em. SC: Close/Dot = {0,4}, Open = {4,0}. TC: Dot/Comma/Colon = {2,2}. ja per jlreq.
- Adjacency: `SpacingCell { u8 glueClass; i8 nat8, stretch8, shrink8; u8 brk /*forbid|allow*/; }` in one matrix per trim mode. The modes mirror CSS Text 4 text-spacing-trim: space-all = none, normal = book, trim-both = full.
The shaper emits:
- the glyph as `Box{widthRule: Measured minus blank(left8+right8)}`;
- between punctuation, a Glue or Kern item whose natural width is the surviving blanks per the matrix cell, discardable at line edges.
The renderer realizes Kern/Glue as explicit engine-computed px margins on the run, so no CSS em constant and no neighbour inspection remain. The trim mode becomes a per-run text property (`#style({punct:'book'})`), defaulted from lang.

*Verifier:* Confirmed:
- nested branches at emit.cc:331-374;
- the clamped width rule at emit.cc:955-962 (it also ignores sizePx);
- halfPresent at typeset_html.cc:505-510;
- `.tsr-sqL/R{margin:-0.5em}` at shell.mjs:41-42.
The em mismatch reproduces: in a sizePx:36 scope, emit budgets '，' as 1728su (27px) plus a 576su (9px) half, while the CSS squeeze is 0.5em of 36px.

*Verifier notes:* The CSS mapping in the proposal is inaccurate.
- CSS text-spacing-trim `normal` = space-first + trim-adjacent + allow-end. It trims close+open, which is closer to Full than Book, and it keeps line-start openers full-width.
- The engine always trims the opener's leading half at line start in every mode (breakable half, emit.cc:349-350). So None is not `space-all`, and Book matches no CSS keyword.
Model the CSS axes separately: line-start (space-first | trim-start), line-end (allow-end | trim-end) and the adjacent policy (Full/Book/None). Reuse the CSS vocabulary without claiming equivalence.

A per-run trim mode also needs a rule for a punct pair that spans two runs (which run's matrix governs).

Otherwise this is the right direction. Explicit engine-computed px margins satisfy §7 better than an em constant evaluated in a different em.

### `emitter/hyphenation-en-us-only` — Hyphenation: one compiled-in en-US trie, ASCII-only word core, lang ignored, explicit hyphens never break, '-' hard-coded in render

- kind: adhoc · severity: high · verdict: accurate · plan: **P4-06**
- locations: `engine/src/hyphen/hyphen.h:9`; `engine/src/hyphen/hyphen.cc:3-7`; `engine/src/hyphen/hyphen.cc:23-27`; `engine/src/emit/emit.cc:190-202`; `engine/src/emit/emit.cc:237-246`; `engine/src/render/typeset_html.cc:481-490`; `tools/hyphc.mjs:12`; `docs/design-decisions-v2.md:277`; `docs/design-decisions-v2.md:367`

The hyphenation path has these limits:
- `hyphenPoints(word)` takes no language. The pattern trie is a fixed `hyphen_en_us` namespace include, and hyphc compiles only `hyphen/patterns/en-us`.
- emitWord treats only ASCII letters as the word core, with leading and trailing non-ASCII stripped as 'punctuation'. Probe p1: `Übersetzung` becomes 'Überset' + hyphen + 'zung', i.e. a German word hyphenated by en-US patterns after the umlaut was discarded. `naïve` and `résumé` silently get no points.
- Explicit hyphens never break. Probe: `well-known` and `bit-wise` are single unbreakable word blocks, so the App C row 'Existing hyphen break (after "bit-" in "bit-wise")' (design-decisions-v2.md:367) is unimplemented. Only tokens of 20+ bytes get URL cuts at '-'.
- The minimum core of 5 letters and leftmin/rightmin 2/2 are hard-coded.
- The renderer writes a literal '-' (typeset_html.cc:485) and ignores the block's text, so there is no per-language hyphen character and no discretionary spelling changes.
- Styling::lang exists (document-model §3) but emit never consults it.

*Why ad hoc:* v2 §15 promises 'English first, pluggable language packs' (design-decisions-v2.md:277), but neither the API nor the item model can express a language or a discretionary.

*Proposed generalization (survey):* Introduce a dictionary registry and discretionary items (component P5):
- `struct HyphenDict { std::string_view tag; const Trie* trie; const ExcTable* exc; u8 leftmin, rightmin, minWord; StrRef hyphenChar; };`
- `const HyphenDict* hyphenFor(StrRef lang)` resolves through a BCP-47 fallback chain.
- tools/hyphc.mjs emits engine/gen/hyphen_<tag>.h per language. Optionally a dictionary arrives as a generic pull resource (NEED_RESOURCE{kind:'hyph', key:'de'}) to keep the wasm small.
- Words are segmented with UAX#29 using the TypoClass Alpha set from the class table, with a small case-fold table.
- Points become `Disc{pre:[hyphenChar], post:[], nobreak:[], penalty:hyphenPenalty, flagged:true}`.
- An explicit U+002D/U+2010 becomes `Disc{pre:[], penalty:exHyphenPenalty}` (TeX \exhyphenpenalty).
- The renderer prints the Disc's `pre` items at a line end instead of a literal '-'.

*Verifier:* All confirmed by probe:
- `Übersetzung` -> 'Überset' | hyphen | 'zung';
- naïve and résumé get no points;
- well-known and bit-wise are single INF word blocks;
- the literal '-' at typeset_html.cc:485;
- the hard-coded 5-letter minimum (emit.cc:201) and leftmin/rightmin of 2 (hyphen.cc:30, 76-80);
- grep shows emit reads Styling::lang only in the dump (emit.cc:1094).
Nit: the umlaut is not discarded from the output. It is kept as the 'lead' (emit.cc:233) and only excluded from the pattern core.

*Verifier notes:* Deliberate sequencing ('English first, pluggable language packs', design-decisions-v2.md:277). However, the API takes no language, and the App C 'Existing hyphen break' row (design-decisions-v2.md:367) is simply unimplemented.

Requirements for the proposal:
- A lazily pulled dictionary must block typeset the way NEED_IMAGES does, or fall back deterministically with a diagnostic. It must never change breaks progressively.
- It should ride a single generic resource pull state, not a fourth per-feature one.
- Disc pre/post widths must enter breakKey (break.cc:147-148 requires every field the DP reads).
- U+00AD should also become a Disc. Today a soft hyphen disables hyphenation of the whole word, because the ASCII-core check fails (see missed).

### `emitter/math-only-inline-box` — The only inline object is math (LinebreakBlock::math); inline image, raw and mathblock vanish

- kind: adhoc · severity: high · verdict: partly · plan: **P1-13, P3-26**
- locations: `engine/src/emit/emit.h:42-44`; `engine/src/emit/emit.cc:108-158`; `engine/src/emit/emit.cc:170-172`; `engine/src/emit/emit.cc:857`; `engine/src/layout/layout.cc:332-335`; `engine/src/layout/layout.cc:446-449`; `engine/src/layout/layout.cc:528-531`; `engine/src/render/typeset_html.cc:474-479`; `engine/src/api/config.h:47-49`; `docs/math-design.md:262-280`; `docs/document-model.md:162-172`; `docs/figure-design.md:30-32`

Inline objects are math-specific end to end:
- The block carries a typed `const MathBox* math`.
- Layout reads `b.math->asc/desc` in three copies, the renderer dispatches `if (b.math) mathSpan(...)`, and kinsoku and kern-context code test `.math`.
- Math break classes cross the module boundary as a magic `MathSeg::brkBefore` u8 (1 = after Rel, 2 = before Rel, else after Bin), which emit maps to three Config keys (emit.cc:126-128).
- The inter-segment glue reuses BF_BOUND, so it dumps as 'boundary' and renders as `data-syn="boundary"`.
The documents promised more general mechanisms: `content: text strRef | inlineBox nodeId` (document-model §6.2), and math-design §9 asked for 'optional intrinsic vertical extents — the mechanism headings-in-line already wants'.
Result: `#image(...)`, advertised in figure-design §1 'for inline/handler use', is silently dropped inside a paragraph (probes p3/p5: no box, no diagnostic). A `mathblock` spliced into a paragraph vanishes the same way. `raw`, which document-model §2.1 lists as inline-level, has no inline path.

*Why ad hoc:* Inline objects are a general category: formulas, images or icons, raw HTML or SVG, ruby, kbd/badge boxes, custom markers. One of them was hard-wired through four layers and the others have no path, so users cannot add one.

*Proposed generalization (survey):* Define a uniform inline-box protocol (component P3):
`struct InlineBox { Su w, asc, desc; u16 painter; const void* payload; StrRef copyText; u8 typoClass; u8 bindPrev:1, bindNext:1, pending:1; };`
It is carried by `Item::Box{obj}`.
- Layout uses only w/asc/desc through one shared line-metrics function.
- Render dispatches through `Painter painters[]`: math (mathLeaves), image (`<img>` with explicit box), raw (passthrough), and placeholder/error.
- A pending box (image dims, math text widths) blocks typeset through the generic pull, like an unmeasured word.
- The math module returns `std::vector<Item>` (boxes, Glue{discardable}, Penalty{value from cfg by class}) instead of MathSeg plus codes.
- Expose `ctors.raw(html,{w,h,inline:true})` and allow `ctors.image` inline.
- Unknown inline kinds produce an error box plus diagnostic.

*Verifier:* Confirmed:
- `LinebreakBlock::math` (emit.h:44);
- three asc/desc copies (layout.cc:332-335, 446-449, 528-531);
- `if (b.math) mathSpan` (typeset_html.cc:474-479);
- brkBefore codes mapped at emit.cc:126-128;
- math glue flagged BF_BOUND (emit.cc:130).
Probe `Text before #image("x.png", {w:20,h:20}) text after.`: the tree has `image @[0,0)` inside the para, and the blocks contain no box and no diagnostic.

Overstated: figure-design.md:33-34 describes `#image(...)` as a 'splice for inline/handler use (a bare image block without figure numbering)'. That is an inline splice producing a block, not a promise of in-line image boxes.

The docs contradict each other on raw: document-model.md:59 lists it as inline, document-model.md:193 says raw nodes are block units.

*Verifier notes:* The painter table is fine under the dual-target rule, because render/ is already the HTML serializer. User-defined inline objects must still go through the trusted raw path.

An inline box should not carry its own break-after penalty. Today `b.breakPenalty = 0` after every formula (emit.cc:145), and that is wrong in Latin context (see missed). The penalty must come from the neighbour pair, using the box's declared class.

### `emitter/flowunit-kind-switch` — Block level is a 7-way kind switch over a fat FlowUnit, re-dispatched in the break loop, layout and both renderers

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-18**
- locations: `engine/src/emit/emit.h:63-116`; `engine/src/emit/emit.cc:459-826`; `engine/src/api/doc.h:288-343`; `engine/src/layout/layout.cc:32-490`; `engine/src/layout/layout.h:20`; `engine/src/render/typeset_html.cc:183-297`; `engine/src/render/typeset_html.cc:681-705`

FlowUnit holds the union of every kind's fields:
- code: codeRuns, chRef, cjkChRef, codeLang, sidebarW, codeLineNo, hlLines
- raw: rawHtml, rawHpx
- image: imgSrc, imgAlt, imgW, imgH, floatSide
- math: mathBox, eqTag
- table: tCols, tAligns, cells
- float tracker: narrow, narrowK, narrowLeft, floatShiftSu, floatClearSu
`cells` means table cells, code sidecar rows, or float-figure caption rows depending on kind plus flags.
Every kind is re-dispatched downstream:
- its own branch in blockWalk and in Doc::typeset's break loop;
- in layoutDoc: line metrics (natural width, asc/desc, span) computed in three near-identical copies (text :509-539, table :437-457, sidecar :325-343), and not at all for float captions, which use flat baseLeading at :79;
- LineBox::special magic codes 0..5 (layout.h:20 documents only 0..4; 5 = image);
- per-special branches in renderLineBox, plus kind checks in renderPages.
Rule, raw, image and display math are all 'an atomic box of height h with a painter', yet each has its own branch and code. verbatim-design §6 deliberately keeps the code grid off KP, which is reasonable, but that rationale does not require the rest of the duplication.

*Why ad hoc:* There is no block-box or sub-flow abstraction, so every block feature is a cross-layer edit to five files, and layout and render know feature kinds.

*Proposed generalization (survey):* Give FlowUnit a shape plus traits (component P4):
`struct FlowUnit { const ContentNode* src; BlockTraits traits; Su indent; StrRef marker, anchor; Shape shape; };`
Shapes:
- `Paragraph{std::vector<Item>}`
- `Atomic{BlockBox{Su w,h,asc; WidthSpec spec; Align align; FloatSide side; u16 painter; const void* payload; StrRef copyText;}}`
- `Composite{std::vector<SubFlow> flows; Arrangement arr;}`, where `SubFlow{std::vector<Item> items; ColumnRef col; Align align; bool ragged;}` and Arrangement is one of Grid(cols, widths) for tables, Zip(equalHeight) for code+sidecar, or StackBelow(box) for float captions.
- `CodeGrid{runs}`, keeping the documented column algorithm.
There is one `LineMetrics measureLine(items, lo, hi, store)` for every line of every shape. LineBox carries `u16 painter` instead of special codes, and both serializers share one painter table.

*Verifier:* Confirmed:
- FlowUnit field union (emit.h:63-116) and the `cells` overloading;
- layout.h:20 documents special codes 0..4 while layout.cc:41/370 use 5;
- three line-metric copies;
- per-kind branches in doc.h:288-343 and typeset_html.cc:681-705.

*Verifier notes:* The proposal misses the second copy of vertical geometry. The float tracker in Doc::typeset charges captions as `breakpoints.size() * baseLeading` (doc.h:307-309) and re-implements layout's gap rule (doc.h:282 vs layout.cc:28). That duplication is documented in figure-design §8. For 'one measureLine' to subsume flat-leading captions, the break phase must call the same line-metric function and the same gap function. Otherwise occlusion and the real caption height diverge.

Keep the documented store-decide-replay pattern, and make the vertical advance a single shared function. The code grid can stay a separate shape (verbatim-design §6).

### `emitter/figure-role-string-dispatch` — role=='figure' (a kind in disguise) drives caption mode, float packing and dropped children, and is re-checked in 3 other layers

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-18**
- locations: `engine/src/emit/emit.cc:776-821`; `engine/src/emit/emit.cc:19`; `engine/src/emit/emit.cc:471-476`; `engine/src/resolve/resolve.cc:166`; `engine/src/render/semantic_html.cc:286`; `runtime/src/worker/executor.mjs:115-121`; `runtime/src/worker/executor.mjs:127-130`; `docs/document-model.md:64`; `docs/document-model.md:66`; `docs/figure-design.md:159-185`

In emit, `strs.get(a.ref) == "figure"` (emit.cc:781-782) triggers one of two behaviours:
- Caption mode: figDepth turns every paragraph in the subtree, including those nested in quotes or lists, into a centred, ragged, unhyphenated, unindented caption.
- Float mode: if the first image child's `side` is left/right (re-parsed at :792-798), the image unit plus the paragraph kids are packed into `cells`. Non-paragraph kids are dropped (documented, figure-design §8).
The same role string is checked elsewhere:
- The resolver counts and labels only role=='figure' groups (resolve.cc:166). A labelled generic region therefore cannot be referenced: probe p8, `@lst-a` -> '??'.
- The semantic serializer emits `<figure>` for it (semantic_html.cc:286).
- Only the executor's internal figureBuild creates it.
User code cannot replicate any of this: `ctors` has no `group` constructor, so a `$.region` handler cannot return a labelled group with a role. document-model.md:64 makes 'figure is a convention, not a kind' a deliberate choice to keep the kind set minimal, and :66 claims user constructs compose group/styled/raw.

*Why ad hoc:* The convention is implemented as four string comparisons in four layers, which is the coupling of a kind without a kind's declaration. User regions are not on equal footing with the built-in one.

*Proposed generalization (survey):* Keep the minimal kind set but make roles data, through a role registry:
`struct RoleTraits { CounterClass counter; bool labelable; StyleDelta childPara; BlockTraits childParaTraits /*ragged, centered, hyphenate, indentFirst*/; bool floatable; StrRef supplementKey; SemanticTag tag /*figure|aside|section|div*/; };`
- Entries live in Config/JSON. Built-ins: figure, table, term, notes, bibliography, bibentry, sidecar.
- Scripts register their own: `$.role('listing', {counter:'listing', labelable:true, ...})`.
- A public `ctors.group(role, {label}, ...kids)` is added.
- Emit keeps a stack of active RoleTraits instead of figDepth. 'Floatable group whose first Atomic child has side' becomes a generic Composite StackBelow rule.
- The resolver's counters and labels and the semantic serializer's tag read the same traits.

*Verifier:* Confirmed: emit.cc:781-782 and 792-798, resolve.cc:166, semantic_html.cc:286, executor.mjs:116-121.
Addition: a generic region without a handler already yields `group{role:name, label}` (executor.mjs:130). So user markup can create labelled role groups; what is missing is that the resolver and emit treat only 'figure'.

*Verifier notes:* Documented as a convention (document-model.md:64), but the four literal compares across layers are ad hoc.

The claim 'No ops change' holds only for Config-supplied traits. A script-side `$.role('listing', {...})` cannot reach the C++ resolver or emitter without an ops path: either a ROLE_DEF op or trait args on each group. Both need an OPS_VERSION bump and re-recorded fixtures.

A childPara StyleDelta should be applied at instantiation (a styled wrapper), per §12 emission-time binding. Then the semantic serializer sees the same styles. Emit should read only layout traits (ragged, centered, hyphenate, floatable).

### `emitter/measure-dependent-geometry-in-emit` — Emit applies per-feature geometry policy against cfg.widthPx (image clamp/scale, placeholder 1/3, sidecar fraction)

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-16, P3-32**
- locations: `engine/src/emit/emit.cc:701-744`; `engine/src/emit/emit.cc:721`; `engine/src/emit/emit.cc:735`; `engine/src/emit/emit.cc:591`; `engine/src/emit/emit.cc:792-798`; `engine/src/emit/emit.cc:723-725`; `docs/architecture.md:98`; `docs/architecture.md:122`

The image branch computes `measurePx = cfg.widthPx - indent` and from it:
- display width: `scale × measure`, clamped to the measure, minimum 1px;
- the placeholder box: `measure × measure/3`;
- the float side, parsed from a string.
It also runs the URL safety check and emits its diagnostic. The code branch sizes the sidecar column as `sidebarFrac × (widthPx - indent)`. These are layout constraints resolved by the emitter, against architecture §2.4: relayout is 're-break only' and 'reuses cached block streams'. The consequence is a real resize bug (see other_issues stale-emit-on-relayout).

*Why ad hoc:* Each block feature carries its own sizing policy in the stream builder instead of declaring a constraint for layout to resolve.

*Proposed generalization (survey):* Make emit measure-independent (component P6):
- emit produces `WidthSpec { Su intrinsicW, intrinsicH; float scale /*of measure*/; bool capToMeasure; }` on the BlockBox, and `ColumnSpec{fraction|fixed}` for Composite sub-flows.
- Doc::typeset's break phase (float tracker) and layoutDoc resolve specs against the current measure.
- The safety policy moves to ingest: scanImageReqs already checks safeImageSrc, so it can mark the node once and emit a single diagnostic.

*Verifier:* Confirmed:
- `measurePx = cfg.widthPx - indent`, scale, clamp and placeholder `measurePx/3` (emit.cc:721-736);
- sidecar `sidebarFrac * (widthPx - indent)` (emit.cc:591);
- side parsed twice (emit.cc:740-741 and 795-797).
The consequence is reproduced in other_issues stale-emit-on-relayout.

*Verifier notes:* WidthSpec resolved at break/layout fits architecture §2.4 ('re-break only'). The image w/h in the blocks dump would change to the spec, which is minor golden churn.

### `emitter/bf-flag-overload-and-rederivation` — One-off BF_* bits with overloaded meanings; layout and render re-derive semantics from flag combinations

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-12**
- locations: `engine/src/emit/emit.h:10-22`; `engine/src/emit/emit.h:46-50`; `engine/src/emit/emit.cc:117`; `engine/src/emit/emit.cc:130`; `engine/src/emit/emit.cc:335`; `engine/src/emit/emit.cc:364`; `engine/src/layout/layout.cc:69-77`; `engine/src/layout/layout.cc:514-521`; `engine/src/layout/layout.cc:544-550`; `engine/src/render/typeset_html.cc:491-515`; `engine/src/render/typeset_html.cc:531-539`; `engine/src/render/typeset_html.cc:557-567`; `engine/src/render/typeset_html.cc:585-590`; `engine/src/emit/emit.cc:1061-1063`

There are ten flag bits, each added for one feature: BF_PAIR for dashes, BF_REF for resolver text, BF_INDENT, BF_BOUND, and so on. Meanings overlap:
- BF_BOUND marks both CJK-Latin glue and math break glue.
- BF_PUNCT_OPEN means 'opening glyph' on a glyph and 'leading half' on a half-space.
Consumers then re-derive semantics:
- 'Did this break consume a real source space' is `isSpace && !(BOUND|PUNCT_SP|INDENT)`, computed twice in layout (:69-77, :544-550).
- 'Does this CJK char realize a stretch gap' (next is a CJK char or a closing punct glyph) is computed identically in layout (:514-521) and twice in render (:533, :559-561). Layout and render thus encode CJK justification topology themselves.
- The renderer's Latin-run predicate lists six exclusions.
- dumpBlocks labels math glue 'boundary'.

*Why ad hoc:* Item semantics are inferred from ad-hoc bit combinations rather than declared, so each feature adds a bit and edits every consumer. This is also how layout and render came to know script semantics.

*Proposed generalization (survey):* Introduce explicit items with orthogonal attributes (component P1):
`struct Item { u8 kind /*Box|Glue|Penalty|Disc|Kern*/; u8 glueClass; u16 attrs /*Synthetic(copy-skip) | SourceSpace(copy join) | Pinned(defined width) | Anchor*/; StyleId style; u32 runId; Span span; ... };`
A `GlueClass` table {word, interChar, autospace, punct, indent, mathBreak} maps each class to:
`{ float breakerWeight; RenderChannel ch /*WordSpacing|LetterSpacing|Spacer|Margin*/; bool discardable; }`.
The shaper emits an inter-char Glue item exactly where a gap exists. Layout sums the weights of the Glue items present, and the renderer realizes each through its channel. Neither re-derives script rules.

*Verifier:* Confirmed:
- ten bits (emit.h:10-22);
- BF_BOUND on math glue (emit.cc:130);
- BF_PUNCT_OPEN with two meanings (emit.cc:335 vs :364);
- join re-derived twice (layout.cc:69-77, 544-550);
- the CJK-gap topology in layout.cc:514-521 and typeset_html.cc:532-534 and 559-561;
- the six-way Latin-run predicate (typeset_html.cc:585-589).

*Verifier notes:* Any new field the DP reads (glue class, discardable) must enter breakKey. The P1 attributes should include the copy-synthetic and anchor attributes as run-key members. The current render merges BF_REF blocks into normal runs (see missed).

### `emitter/url-break-special-path` — Emergency (URL) breaks: separate code path with a byte threshold and fixed separators, disabled in headings and captions

- kind: adhoc · severity: medium · verdict: accurate · plan: **P4-06**
- locations: `engine/src/emit/emit.cc:203-225`; `engine/src/api/config.h:42-43`; `engine/src/emit/emit.cc:40`; `engine/src/emit/emit.cc:498-502`; `engine/src/emit/emit.cc:474-476`; `engine/src/emit/emit.cc:806-808`; `engine/src/emit/emit.cc:56-69`; `docs/real-world-report.md:30-32`

The emergency-break path is narrow and inconsistent:
- It is reached only when hyphenation found nothing.
- The threshold is `w.size()` in bytes, so a 10-letter Cyrillic word qualifies.
- Separators `/ ? & = . - _` and a minimum piece of 3 are hard-coded. 'https://' splits as `https:/`|`/example.` (probe p3).
- Pieces are word blocks with a penalty rather than break items, so no junction kerning is budgeted, unlike hyphen points, even though the renderer joins the pieces into one run.
- Inline `code` is a single unbreakable block and never gets cuts.
- `ICtx::noHyphen` (headings, block and float captions) disables these overflow-safety breaks along with hyphenation. Probe p4: a heading with a 950px URL becomes one line, and layout sets word-spacing −700px (dw=-44851su; see other_issues negative-wordspacing-overfull).
The fix itself is documented (real-world-report #2).

*Why ad hoc:* This is yet another break-opportunity source with its own representation, and a typographic preference (no hyphenation) is conflated with a safety mechanism (no overflow).

*Proposed generalization (survey):* Express emergency breaks as break-opportunity rules in TextRules (a 'url' tailoring following Chicago Manual of Style URL breaking: break after '//', before '/ . ? # & = _', never inside the scheme). They materialize as `Disc{pre:[], post:[], penalty: urlBreakPenalty}` through the same code as hyphen points. Thresholds count grapheme clusters. BlockTraits splits into `hyphenate` and `emergencyBreaks`; the latter defaults to true everywhere, including headings, captions and inline code (Alpha-class tailoring inside CLS_CODE runs).

*Verifier:* Confirmed by probe:
- 'https:/' | '/example.';
- URL pieces dump `pen=0` while their real penalty is 1.2;
- a heading URL gives dw=-44851su;
- `!noHyphen` gates URL cuts (emit.cc:208).
Addition: real-world-report.md:28 says 'tokens ≥ 20 chars', but the code compares bytes (`w.size()`, emit.cc:208). Doc and code disagree.

*Verifier notes:* The documented fix (real-world-report #2) is fine as a patch, but the conflation of noHyphen with no emergency breaks is accidental.

Inline code is currently one block before any word logic (emit.cc:56-69), so it must be routed through the word path to get cuts.

Adopting the Chicago rule (break before '.' and '/') flips the break side of current URL goldens.

### `emitter/boundary-glue-constant` — CJK-Latin 0.25em glue is a constexpr inserted only on in-node Latin/ideograph transitions

- kind: adhoc · severity: medium · verdict: accurate · plan: **P4-02**
- locations: `engine/src/api/config.h:77`; `engine/src/emit/emit.cc:288-292`; `engine/src/emit/emit.cc:425`; `engine/src/emit/emit.cc:451`; `engine/src/render/typeset_html.cc:491-501`; `docs/design-decisions-v2.md:375`; `docs/real-world-report.md:40-42`

kCjkBoundaryEm is a compile-time constant, outside Config and outside styles. The stretch weight 1.0 and capacity 0.25em are hard-coded in boundary(). The glue is inserted only between ideographs and Latin inside one text node, plus the math patches. It is never inserted next to inline code, links, refs or images (see paragraph-blind-script-context). A typed space between CJK and Latin stays a Latin space, so spacing depends on the author's typing habit. The glue renders as a dedicated `tsr-sp data-syn=boundary` inline-block. The 'no boundary beside fullwidth punct' rule is a documented fix (real-world-report #5).

*Why ad hoc:* Inter-script spacing is a single cell of the spacing matrix (CSS Text 4 `text-autospace: ideograph-alpha ideograph-numeric`), but it is implemented as a dedicated path with its own flag.

*Proposed generalization (survey):* Make it SpacingCell entries Ideo×Alpha, Ideo×Digit and Ideo×Object = {glueClass: autospace, nat 2/8em, stretch 2/8em, brk: allow} in TextRules. A per-run `autospace` property (none | ideograph-alpha | ideograph-numeric) and `normalizeTypedSpace` (replace a typed space between Ideo and Alpha by the autospace glue) are settable from #style and defaulted per locale. Inline objects declare a TypoClass so the same cell covers formulas, code and images.

*Verifier:* Confirmed:
- `kCjkBoundaryEm` constexpr (config.h:77);
- weight 1.0 and capacity hard-coded in boundary() (emit.cc:288-292);
- a typed space resets `prev` to None (emit.cc:390), so `中文 English` gets a plain space.

*Verifier notes:* The 0.25em breakable/stretchable boundary is normative in v2 §14 and App C (design-decisions-v2.md:375). Being a constexpr rather than Config is minor. The substantive defect is the missing cross-node insertion, which item 1 already covers. A spacing-matrix cell pays off only together with P2.

'normalizeTypedSpace' changes author text. It must be opt-in, and copy must still yield the typed space (SourceSpace attribute).

### `emitter/defined-width-dash-ellipsis` — ——/…… defined widths: two hard-coded codepoints, an in-node context heuristic, and a BF_PAIR renderer branch

- kind: adhoc · severity: medium · verdict: accurate · plan: **P4-04**
- locations: `engine/src/emit/emit.cc:308-330`; `engine/src/emit/emit.cc:393-423`; `engine/src/emit/emit.h:19`; `engine/src/render/typeset_html.cc:517-545`; `engine/src/inline/inline.cc:50`; `docs/document-model.md:198-205`; `docs/design-decisions-v2.md:373`

U+2014/U+2026 are pulled into the CJK path and paired greedily into a 2em block, or kept as a 1em single. They fall back to Latin when 'single, previous not CJK, next not CJK' holds, and that test sees only the current node. The renderer has a dedicated BF_PAIR branch that pins `display:inline-block;width` and re-derives the CJK-gap margin. The principle is deliberate and well argued (document-model §6.1: canvas cannot predict DOM's fullwidth-ization).

*Why ad hoc:* The principle (a defined advance for glyphs whose DOM advance is unpredictable) is general, but only two codepoints can use it. Others with the same problem cannot opt in without code: 〜 U+301C, ― U+2015, ⋯ U+22EF, ‥ U+2025, ・ U+30FB, and the ambiguous-width quotes.

*Proposed generalization (survey):* Keep definition over measurement and make it data. TextRules carries `DefinedAdvance { u32 seq[3]; u8 len; u8 em8; u16 ctxMask /*which neighbour classes activate it*/; }`. The shaper emits `Box{widthRule: Defined(em8), attrs: Pinned}`. The renderer's pinned path keys on the Pinned attribute (generic inline-block width), and its margin comes from the following Glue item (P1), not from re-derivation. Context comes from the paragraph stream (paragraph-blind-script-context).

*Verifier:* Confirmed: emit.cc:313-330 and 402-423, BF_PAIR branch at typeset_html.cc:523-545, cjkish at inline.cc:50.

*Verifier notes:* The documented rationale (definition over measurement, document-model §6.1) is preserved. A data table costs little and reproduces the current goldens.

### `emitter/latin-quote-heuristic` — Curly-quote CJK/Latin decision: four codepoints plus a previous/next-character rule inside one node; lang ignored

- kind: adhoc · severity: medium · verdict: partly · plan: **P4-02**
- locations: `engine/src/emit/emit.cc:430-443`; `engine/src/support/support.h:165-177`; `docs/real-world-report.md:33-36`; `test/golden/inline/quotes.blocks.txt:91-101`

U+2018/2019/201C/201D sit in the CJK open/close lists. emit overrides them to Latin when `prev != Cjk` and the next character in the same node is not CJK or punctuation. Styling::lang plays no part. Emphasis around a quoted word flips the outcome, and a pair can be split across classes (probe p2, see paragraph-blind-script-context). The fix is documented (real-world-report #3).

*Why ad hoc:* Resolving ambiguous-width characters by context or language is a general Unicode concept (UAX #11 East Asian Width 'A'). Here it is a single-purpose patch for four codepoints.

*Proposed generalization (survey):* Add a TypoClass `AmbQuoteOpen/AmbQuoteClose` (and Amb for §, ·, —, …) with one resolution rule in the shaper:
1. An explicit run lang decides: zh/ja/ko -> fullwidth class, else Alpha-attached.
2. Otherwise the nearest strong neighbour scripts across node boundaries decide, and a matched pair resolves jointly (both quotes take one class).
3. Otherwise the document lang decides.
The resolved class then indexes the spacing matrix like any other.

*Verifier:* The heuristic fails inside a single node, not only across markup. `prev != Prev::Cjk` (emit.cc:435) treats Prev::Punct (after ：，、) as Latin context. My probe of unmarked `他说：“Hello”之后` gives a Latin “ (`word "“Hel"`) and a CJK ” (`punct-close`, plus a half), so the pair is split in two fonts with no emphasis at all.
The report's statement that the unmarked text is set CJK, citing quotes.blocks.txt:91-101, is wrong. That golden is preceded by an ideograph (时).

*Verifier notes:* The rule in real-world-report #3 is 'no CJK neighbour', and a CJK punct is a CJK neighbour, so the implementation is narrower than its own documented rule.

In the proposal, step 1 ('explicit run lang decides') must mean a lang scope narrower than the document default. Otherwise every English quote inside a zh document becomes fullwidth. Strong-neighbour evidence should beat an inherited document lang.

### `emitter/sup-bit-attach-rule` — Footnote-marker 'never start a line' keyed on the CLS_SUP style bit; inline anchors only via labelled ref

- kind: adhoc · severity: medium · verdict: accurate · plan: **P4-07**
- locations: `engine/src/emit/emit.cc:71-93`; `engine/src/model/model.h:20-22`; `engine/src/resolve/resolve.cc:345`; `runtime/src/worker/executor.mjs:6-11`; `docs/notes-design.md:84-91`

A labelled `ref` sets anchorId on its first block. If that block's style has CLS_SUP, the previous block becomes unbreakable. Break semantics therefore hang on a presentation bit that only the resolver sets (resolve.cc:345). Users cannot set it: styleBits (executor.mjs:8-11) exposes bold, italic and decorations only. Inline anchors exist only for refs. The documented nit (notes-design As built) — the marker follows a closing punct's trailing half instead of hugging the glyph — is a consequence of the fused block encoding.

*Why ad hoc:* 'Attach to the previous glyph / never start a line' is a general property (TeX \nobreak, U+2060 WORD JOINER, CSS `white-space:nowrap` on a pair) that user constructs also want: superscript citations, unit symbols, custom markers.

*Proposed generalization (survey):* Add a generic inline node arg `bind: 'prev'|'next'|'both'` (new ARGK). The shaper emits Penalty(+inf) on that side and suppresses that side's spacing-matrix glue, so the marker hugs the glyph, not the half, which fixes the nit. `label` on any inline node sets the Anchor attr on its first item. The resolver sets bind:'prev' on note markers. Expose `sup` (and `sub`) in styleBits so user superscripts render and measure the same way.

*Verifier:* Confirmed:
- the CLS_SUP check at emit.cc:89-90;
- CLS_SUP is set only by the resolver (resolve.cc:345);
- styleBits has no sup (executor.mjs:8-11);
- the nit is documented at notes-design.md:90-91.

*Verifier notes:* A more general primitive already exists and needs no new ARGK or OPS bump: Unicode break controls (UAX#14 WJ U+2060, GL U+00A0, ZW U+200B). The emitter ignores them today; probe `foo​bar` gives one unbreakable word. Honouring them in the class table gives authors a plain-text attach and break mechanism. The resolver can then insert WJ before note markers.

A `bind` arg becomes optional sugar. Exposing `sup` to users needs a preset (bit, sizeMul 0.7 and raise), because the resolver applies the 0.7 separately (resolve.cc:346).

### `emitter/kern-context-postpass` — Cross-space/junction kerning: a post-pass that guesses the renderer's run boundaries; cutoff at U+2000

- kind: adhoc · severity: medium · verdict: partly · plan: **P4-01**
- locations: `engine/src/emit/emit.cc:832-889`; `engine/src/emit/emit.cc:870-872`; `engine/src/emit/emit.cc:841`; `engine/src/emit/emit.cc:928-954`; `engine/src/emit/emit.cc:963-976`; `engine/src/render/typeset_html.cc:582-590`; `docs/real-world-report.md:22-28`

fillSpaceContexts re-implements the renderer's run-grouping predicate (same style and same link) to decide where the browser will kern. Contexts exist only for plain spaces and hyphen blocks. URL pieces get none, although the renderer joins them into one shaped run, and boundary glue gets none. `cp >= 0x2000` excludes curly quotes and dashes. resolveWidths then has two separate copies of the trigram arithmetic, one for hyphens and one for spaces. The fix is documented and evidence-based (real-world-report #1).

*Why ad hoc:* The fact being modelled is that two glyphs in the same shaped run kern. The renderer decides the runs and the emitter guesses them independently, so any change to run formation in render silently invalidates the budget.

*Proposed generalization (survey):* Make shaped runs a first-class emit product. The shaper assigns `runId` (same StyleId, link and script, no object between). The renderer must open a span exactly at runId changes, which can be asserted in debug builds. Junction kerning is computed once, generically, for every break item (Glue or Disc) whose neighbours share a runId: `kern = m(prev ⊕ sep ⊕ next) − m(prev) − m(next)`, where sep is ' ' for word glue and '' for a Disc. Eligibility comes from TypoClass, not `cp >= 0x2000`.

*Verifier:* Confirmed:
- the style/link run guess (emit.cc:870-872);
- the `cp >= 0x2000` cutoff (emit.cc:841, 852);
- two copies of the trigram arithmetic (emit.cc:945-954 and 965-972);
- URL pieces get no junction context.
Not a gap: 'boundary glue gets none'. The renderer emits boundary glue as its own inline-block span (typeset_html.cc:491-500), so no shaped run crosses it and there is nothing to kern.

*Verifier notes:* runId must also include the copy-synthetic attribute. Today neither fillSpaceContexts nor the renderer splits runs at BF_REF, which drops real text from copy (see missed).

### `emitter/sidecar-role-string` — Code sidecar rides a magic group{role:'sidecar-lines'} built in api/doc.h, recognized by string in emit

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-13**
- locations: `engine/src/api/doc.h:84-150`; `engine/src/api/doc.h:107-110`; `engine/src/emit/emit.cc:579-602`; `engine/src/api/doc.h:335-341`; `engine/src/layout/layout.cc:118-128`; `engine/src/layout/layout.cc:309-360`; `engine/src/code/tokens.cc:24-25`; `docs/verbatim-design.md:71-106`

Doc::extractSidecars, in the API layer, splits code lines at a fence-declared marker and parses the notes as inline fragments into a synthetic group with role 'sidecar-lines'. foldTokens must preserve that trailing group (tokens.cc:24-25). emit recognizes it by string compare and stores per-line streams in the Code unit's `cells`. Doc::typeset breaks them in a Code-specific branch, and layout zips rows with an equal-height rule. The three-box model is documented (verbatim-design §5), but the doc names the role 'sidecar' while the code uses 'sidecar-lines', so the two already disagree.

*Why ad hoc:* A content transformation lives in api/ instead of a constructor or a model pass, and one role string couples api, code/tokens, emit and layout.

*Proposed generalization (survey):* Move extraction into the codeblock constructor path: a model/ pass alongside foldTokens, or JS when the fence declares `sidecar`. It produces typed structure: each line `seq` with an optional trailing child marked by a role constant from the registry. emit maps codeblock to `Composite{CodeGrid(code), SubFlow column per sidecar, Zip(equalHeight)}`. Role identifiers are interned once (`Role::Sidecar`), never re-spelled as string literals.

*Verifier:* Confirmed: doc.h:84-150, tokens.cc:23-24, emit.cc:583-598, doc.h:335-341.
The doc/code mismatch goes beyond the name. verbatim-design.md:101-103 specifies a `group{role:"sidecar"}` child of each line seq. The code builds one trailing `group{role:"sidecar-lines"}` on the codeblock with one seq per line.
Extraction also fires only for plain-body codeblocks (doc.h:90-91), so the structured `ctors.codeblock(lang, [lines], {sidecar})` form silently gets no sidecar.

*Verifier notes:* The 'JS' route needs m.parse (WASM re-entry), which is still deferred (document-model §6.3). A model/ pass next to foldTokens is the viable route now.

### `emitter/comment-by-css-color` — Comment-aware hanging recovers the token class by comparing a run's colour to 'var(--tsr-tok-comment)'

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-08**
- locations: `engine/src/emit/emit.cc:618-624`; `engine/src/code/tokens.cc:49-53`; `engine/src/code/tokens.h:9-17`; `engine/src/code/tokens.cc:5-17`; `runtime/src/worker/tokens.mjs:6-11`

foldTokens turns a token tag into a CSS colour string. emit then reverse-maps that exact string to set CodeRun::isComment, which drives continuation alignment. Any structured code body built by a user (ctors.codeblock with array lines, or a fence handler), or any recolouring, loses comment hanging silently. Comment italics are hard-coded engine-side (`if (tag == 3) s.bits |= CLS_EM`), contradicting 'theming lives entirely in CSS' (tokens.h:30). The tag list and alias table exist twice, in C++ (tokens.h:9-15, with aliases mapped to magic indices 5/0/6/4/10 at tokens.cc:8-15) and in JS (tokens.mjs:6-11), held together by a 'keep in sync' comment. Native goldens use the C++ copy and production uses the JS copy.

*Why ad hoc:* A semantic attribute is encoded in presentation and then decoded from it, and the contract is duplicated across languages.

*Proposed generalization (survey):* Make the token tag a semantic run attribute. Implement document-model §3's `dynClasses` (designed but not built) or a `role` arg on text/styled nodes, and carry `tokenTag` through Styling. Code-layout behaviour comes from a trait table `tokenTraits[tag] = {hangAsComment, ...}`. Presentation is CSS classes `tsr-tok-<tag>`, with italics in the theme. Generate the tag and alias table from one .def file (as ops.def + gen-ops-ts already do) for both C++ and JS.

*Verifier:* Confirmed: emit.cc:618-624, tokens.cc:43-46, the C++ alias table tokens.cc:11-16 vs tokens.mjs:6-11. native_tokens.h:65 uses the C++ copy.
The 'theming lives entirely in CSS' line is tokens.h:29, not :30.

*Verifier notes:* tokens.cc:46 says '(duplex contract)': the italic was put engine-side deliberately so that both serializers agree. Moving it to CSS is safe for the ch-grid, which does not measure per run. The semantic serializer must then receive the same token class.

Generating tags and aliases from one .def, as ops.def does, is the right fix for the C++/JS drift.

### `emitter/kind-presentation-in-emit` — Heading/list/quote/code/error presentation and layout traits hard-coded in emit

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-01**
- locations: `engine/src/emit/emit.cc:485-505`; `engine/src/emit/emit.cc:507-538`; `engine/src/emit/emit.cc:540-544`; `engine/src/emit/emit.cc:62-63`; `engine/src/emit/emit.cc:94-107`; `engine/src/api/config.h:51`; `engine/src/api/config.h:64-65`; `engine/src/api/config.h:82-84`; `engine/src/layout/layout.cc:28`; `engine/src/api/doc.h:282-283`; `engine/src/render/typeset_html.cc:704-705`

Presentation that belongs in a stylesheet is fixed in emit:
- Headings: CLS_BOLD plus `headingSizeMul(level)` (1.6/1.35/1.15/1.0) are OR-ed and multiplied into the style. Bits only OR, so a user style cannot un-bold or resize headings. ragged and noHyphen are fixed.
- List markers are literally '•' and 'N.': no per-depth styles, no 一、 or ① counter styles, no hook.
- List and quote indents are Config scalars.
- `tightAbove` applies only inside lists, with a paraGap/3 constant duplicated in layout.cc:28 and doc.h:282.
- Inline-code size comes from cfg.codeScale.
- The error presentation is '⚠ ' + message in CLS_CODE.
- Paged keep-with-next is decided by `tb.node->kind == Kind::heading`, which only catches top-level headings.

*Why ad hoc:* The kind-to-presentation mapping should be a stylesheet (the style system and StyleDelta exist) and the kind-to-layout mapping a trait table. Today emit is both.

*Proposed generalization (survey):* Three pieces:
- A default stylesheet `kindStyle[Kind][variant] -> StyleDelta`, with StyleDelta extended to {setBits, clearBits, mulSize, patch}. It is applied at instantiation like a `styled` wrapper, so heading level 2 = {set BOLD, mul 1.35}. Users override it via `$.style.rule('heading', {level:2}, {...})`.
- `BlockTraits kindTraits[Kind]` = {ragged, hyphenate, emergencyBreaks, indentFirst, keepWithNext, gapBeforeEm, tight}. Emit copies these onto FlowUnit, and layout/renderPages read traits instead of kinds.
- List markers come from a counter-style table (decimal, disc, cjk-decimal, circled) selected per depth, plus a JS hook `$.list.marker = (n, depth) => content`.

*Verifier:* Confirmed:
- heading CLS_BOLD and headingSizeMul (emit.cc:499-502);
- markers '•' and 'N.' (emit.cc:517);
- tightAbove (emit.cc:536-537) with paraGap/3 duplicated (layout.cc:28, doc.h:282);
- error presentation (emit.cc:95-105);
- heading keep checks only the top-level node (typeset_html.cc:704).

*Verifier notes:* v2 §12 explicitly allows set/show-rule sugar layered on the stack primitive. Applying a default kind stylesheet at instantiation respects emission-time binding. StyleDelta clear-bits is a model version event, as stated.

### `emitter/global-typography-config` — Typographic knobs are document-global Config scalars, not per-style or per-locale properties

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-02**
- locations: `engine/src/api/config.h:36-50`; `engine/src/emit/emit.cc:477-480`; `engine/src/api/config.h:89-104`; `test/fixtures/style/patch.tsm`

cjkJustifyK, cjkGlueEm, paraIndentEm, hyphenPenalty, urlBreak*, the three math penalties and punctCompress are all document-wide.
- paraIndentEm (首行缩进) applies to every paragraph without a marker outside a figure, including quotes, note bodies, bibliography entries and continuation paragraphs in list items. It is computed from cfg.baseSizePx rather than the paragraph's own size.
- applyLang only swaps supplement words for zh/ja prefixes.
So a zh-Hans document cannot have an English abstract without indent and with hyphenation, and the zh-TW aside in patch.tsm cannot get TC punctuation or compression.

*Why ad hoc:* Each knob was added as a global for one feature, and locale is not a dimension of the configuration.

*Proposed generalization (survey):* Add a `TextRulesId` per run, interned like StyleId. It is resolved from Styling.lang through rulesFor(), plus explicit overrides carried as InlineStyle props (`#style({indent, punct, autospace, hyphenate, justifyK})`). Config supplies per-locale defaults in JSON (`"locales": {"zh-Hans": {...}, "en": {...}}`). Paragraph-level properties such as indent and justification come from the paragraph's leading run.

*Verifier:* Note bodies are not indented. Each note body is the first paragraph of an ordered-list item and carries the marker (resolve.cc:374-389), and indent applies only when `marker == 0` (emit.cc:477).
Bibliography entries (resolve.cc:418-424), quote paragraphs and list continuation paragraphs are indented, as claimed. The indent uses cfg.baseSizePx (emit.cc:478), confirmed.

*Verifier notes:* Deriving paragraph-level properties (indent, justification, trim mode) from 'the paragraph's leading run' is itself ad hoc: a zh paragraph that opens with an English word would flip.
Use the block node's own style (`n->style`), which §12 already binds where the paragraph was emitted, resolved through rulesFor(lang).

### `emitter/codeblock-args-in-emit` — Codeblock argument mini-languages parsed in emit; code-only measurement probes special-cased in resolveWidths

- kind: adhoc · severity: low · verdict: accurate · plan: **P0-06, P3-02, P3-11**
- locations: `engine/src/emit/emit.cc:553-578`; `engine/src/emit/emit.cc:993-1005`; `engine/src/render/typeset_html.cc:332-343`; `engine/src/api/config.h:63`

`hl: "3,5-7"` is parsed inside emit with atoi and a magic 10000 cap. `lineNo` is accepted as either Num or Bool. The probe strings '0' and '中' are interned in emit and requested by a Code-only branch in resolveWidths. codeFontFeaturesByLang is looked up by the renderer from the unit's language tag.

*Why ad hoc:* Argument decoding for one feature sits in the stream builder, and that feature also needs its own measurement-request branch.

*Proposed generalization (survey):* Constructors normalize arguments: JS turns `hl` into a number list (an `ArgTag::NumList`, or child `seq` markers) and `lineNo` into a Num. A unit declares `probes: [(StrRef, StyleId)]`, which resolveWidths requests generically for any unit. Font features become an InlineStyle property set at instantiation from the code language.

*Verifier:* Confirmed: emit.cc:555-578 (atoi and the 10000 cap), emit.cc:553-554 and 993-1005, typeset_html.cc:332-338.

*Verifier notes:* A NumList ArgTag is an OPS_VERSION bump. Normalizing to child seq/num nodes in JS avoids that.

### `emitter/anchor-opt-in-per-kind` — Label-to-anchor handling repeated per case; codeblock and rule never take the pending anchor

- kind: adhoc · severity: low · verdict: accurate · plan: **P1-18**
- locations: `engine/src/emit/emit.cc:18-24`; `engine/src/emit/emit.cc:467-469`; `engine/src/emit/emit.cc:495-497`; `engine/src/emit/emit.cc:652-658`; `engine/src/emit/emit.cc:692`; `engine/src/emit/emit.cc:710`; `engine/src/emit/emit.cc:751-755`; `engine/src/emit/emit.cc:545-638`; `engine/src/emit/emit.cc:161-168`

Each block case calls takeAnchor() and re-scans its args for ArgK::label itself. codeblock and rule never call it, so a labelled container whose first unit is code loses its anchor: probe p8, a `#!listing(label: "lst-a")` holding only a fence renders no id. Inline groups set the unit anchor only if it is unset, and labelled refs use a third mechanism (anchorId).

*Why ad hoc:* A cross-cutting concern is implemented opt-in per case.

*Proposed generalization (survey):* Route every unit through one `pushUnit(FlowUnit&&)` that applies (own label, else pendingAnchor) to the first unit pushed, whatever its kind. Inline, any node with `label` sets the Anchor attribute on its first item (shared with sup-bit-attach-rule).

*Verifier:* Confirmed: codeblock (emit.cc:545-638) and rule (emit.cc:639-646) never call takeAnchor, and group resets pendingAnchor (emit.cc:819).
Addition: para and heading call takeAnchor() and then overwrite with their own label (emit.cc:467-469, 495-497). A labelled group whose first child is a labelled paragraph therefore loses the group id.

*Verifier notes:* pushUnit fixes only top-level units. Sub-flows (table cells, sidecar rows, float captions) are emitted into a throwaway `FlowUnit tmp` (emit.cc:594-596, 671-673, 805-809). The `u.anchor` set by inline groups (emit.cc:161-168) is discarded there.
Probe: `#term[gizmo]` in a table cell. `@gizmo` renders href="#tsr-gizmo", but no element carries that id.
The anchor must be an item attribute that layout realizes per LineBox in every sub-flow.

### `emitter/scattered-magic-constants` — Typographic magic numbers scattered in emit/layout and duplicated in CSS

- kind: adhoc · severity: low · verdict: accurate · plan: **P3-02, P4-04**
- locations: `engine/src/emit/emit.cc:201`; `engine/src/emit/emit.cc:213`; `engine/src/emit/emit.cc:573`; `engine/src/emit/emit.cc:735`; `engine/src/emit/emit.cc:841`; `engine/src/api/config.h:76-80`; `engine/src/api/config.h:82-84`; `engine/src/layout/layout.cc:28`; `engine/src/layout/layout.cc:145`; `runtime/src/main/shell.mjs:41-42`; `runtime/src/main/shell.mjs:46`

Examples:
- minimum hyphenation core 5 (:201) and minimum URL piece 3 (:213);
- hl cap 10000 (:573) and placeholder height measure/3 (:735);
- kern-context cutoff U+2000 (:841);
- half 0.5em, boundary 0.25em, table pads 0.4/0.3em (config.h:76-80);
- heading size table (config.h:82-84);
- tight gap paraGap/3 (layout.cc:28) and minimum 8 code columns (:145);
- CSS squeeze −0.5em and sup raise −0.45em, against 0.35em in notes-design.
Several exist both in C++ and in the shell CSS.

*Why ad hoc:* The parameters are unnamed and unversioned, and they are copied across the C++/CSS boundary, where they can drift.

*Proposed generalization (survey):* Name every typographic parameter in TextRules or Config with units (em8 or su), under a RULES_VERSION. CSS stops carrying engine numbers: the engine emits explicit px for anything that affects geometry (squeeze, raise), and any remaining CSS parameters are generated as custom properties from Config by pack-dist.

*Verifier:* Confirmed: shell.mjs:46 uses top:-0.45em while notes-design.md:66 says 0.35em. `.tsr-code{font-family:monospace}` (shell.mjs:28) is not cfg.monoFont, which measure uses (measure.h:62).

*Verifier notes:* Emitting geometry-affecting values as explicit px is consistent with §7.

### `emitter/stale-emit-on-relayout` — Relayout keeps emit-time image and sidecar widths computed for the old measure (resize bug)

- kind: issue · severity: high · verdict: accurate · plan: **P1-16**
- locations: `engine/src/api/doc.h:392`; `engine/src/api/doc.h:247`; `engine/src/emit/emit.cc:721`; `engine/src/emit/emit.cc:591`; `runtime/src/worker/worker.mjs:248-254`; `docs/architecture.md:98`

Doc::setWidth (api/doc.h:392) only clears `laidOut`, and typeset() skips emit while `emitted` is true (doc.h:247). Yet emit reads cfg.widthPx for image display size and placeholders (emit.cc:721-741) and for the sidecar column width (emit.cc:591). The production resize path calls `_tsr_set_width` then re-typesets (runtime/src/worker/worker.mjs:248-254).
WASM probe with a 1000x500 image:
- width 600 -> `width:600px`;
- relayout to 200 -> still `width:600px`, overflowing by 400px (the e2e audit would flag it);
- relayout to 900 -> still 600, where 900 is expected.
This violates architecture.md:98/122 ('relayout(width) re-break only, reuses cached block streams'). Cheap fix: setWidth also clears `emitted`. Proper fix: measure-independent WidthSpec (adhoc measure-dependent-geometry-in-emit).

*Verifier:* Reproduced with the WASM build. A 1000x500 image gives width:600px at 600; after `_tsr_set_width(200)` it is still width:600px.
The print path is affected too: paginate (worker.mjs:228-241) sets the page width and then restores it. Printed images and sidecars are therefore sized for the screen measure.

*Verifier notes:* This is a defect caused by the ad-hoc item, not an ad-hoc feature itself.
The cheap fix (setWidth clears `emitted`) re-runs whole-document emit on every resize. Emit has diagnostic side effects, so image-src and math diagnostics would duplicate on each resize (I verified duplication on re-emit), and every word would be re-hyphenated. Use it only together with the duplicate-diagnostics fix. WidthSpec is the real fix.

### `emitter/sizepx-em-mismatch` — Emitter's em ignores Styling::sizePx, while measurement and CSS honour it

- kind: issue · severity: high · verdict: accurate · plan: **P0-08**
- locations: `engine/src/emit/emit.cc:251`; `engine/src/emit/emit.cc:274-275`; `engine/src/emit/emit.cc:958`; `engine/src/emit/emit.cc:120`; `engine/src/emit/emit.cc:759`; `engine/src/measure/measure.h`; `runtime/src/main/shell.mjs:41-42`; `test/golden/style/patch.blocks.txt:71-74`

`Emitter::fontPx` (emit.cc:251) is `cfg.baseSizePx * sizeMul` and ignores the absolute `sizePx`, whereas describeStyle (measure.h) and the renderer (typeset_html.cc:46) use `sizePx > 0 ? sizePx : baseSizePx`. Every em-derived quantity is wrong inside `#style({sizePx})` or a region with sizePx:
- CJK glue capacity: golden style/patch.blocks.txt:71-74 shows 22px chars with glue=102su, which is 0.1×16px;
- punct half in emit (:274) and in resolveWidths (:958);
- boundary glue (:289);
- ——/…… defined widths (:324);
- inline and display math size (:120, :759): probe p9, a formula in a 36px scope is laid out at 18px (w=606su, identical to the base-size formula);
- paragraph indent (:478).
Measurement and render then disagree. Probe p1 at sizePx 36 budgets the glyph at 27px plus a 9px half, while CSS `.tsr-sqL/R{margin:-0.5em}` (shell.mjs:41-42) squeezes 18px, so a squeezed punct renders 9px short. Fix: a single `emPx(StyleId)` used by emit, resolveWidths and MathTextCtx (whose docBasePx has the same flaw).

*Verifier:* Reproduced in `#style({sizePx:36})[中文，测试$x+y$。]` against the same text at base size:
- glue=115su and punct-sp 576su are identical in both;
- '，' is 1728su (27px measured minus 9px);
- math 'x+y' w=1732su in both;
- boundary 288su in both.
fontPx (emit.cc:251) and the punct rule (emit.cc:958) ignore sizePx; describeStyle (measure.h:66) and the renderer (typeset_html.cc:46) honour it.

*Verifier notes:* A single emPx(StyleId) shared with describeStyle and MathTextCtx (docBasePx, math.h:44) is right.

### `emitter/kp-counts-discardable-glue` — KP includes the break block and leading glue in line width and stretch, but layout trims them

- kind: issue · severity: medium · verdict: accurate · plan: **P0-12**
- locations: `engine/src/break/break.cc:63-66`; `engine/src/layout/layout.cc:498-499`; `test/golden/style/kern-boundary.breaks.txt`; `test/golden/cjk/punct.layout.txt`; `docs/design-decisions-v2.md:382`

breakLines computes contentW = widthPsum[i+1] − widthPsum[j+1] (break.cc:63-64), which includes the break block itself (the trailing space, punct half or boundary glue) and any leading glue. layoutDoc trims leading and trailing isSpace blocks (layout.cc:498-499). Recomputed from the goldens:
- style/kern-boundary: every line broken at a space has KP width = rendered width + 257su (4.02px);
- cjk/punct: line 0 counts the paragraph-initial 「 half (512su = 8px) that layout trims.
The discardables also add their spaceWidth to KP's stretch capacity. Effects:
- badness is biased, since KP sees lines one space tighter than they are;
- lines ending in a closing punct look 0.5em longer, so KP disfavours exactly the breaks where App C's 'line-end compression falls out' (design-decisions-v2.md:382);
- shrinkThreshold feasibility is tested on the wrong width.
TeX semantics fix this: glue is discarded at a break. With the fused encoding, mark blocks `discardable` and exclude trailing and leading discardables from the DP sums.

*Verifier:* Verified on the golden. In style/kern-boundary, lines broken at a space have a KP sum (widthPsum, break.cc:67-68) 257su larger than the trimmed range layout uses (layout.cc:498-499). cjk/punct block 0 is the paragraph-initial 「 half, which is counted by KP and trimmed by layout (layout L0 blocks=[1,22)).

*Verifier notes:* Inherited from the PoC (break.h:1-3). TeX discard semantics are right. The discardable flag must be added to breakKey (break.cc:147-148 contract).

### `emitter/kp-ignores-stretch-weight` — The k-rule claim 'cost model and renderer agree by construction' does not hold: KP never reads stretchWeight

- kind: issue · severity: medium · verdict: accurate · plan: **P4-08**
- locations: `engine/src/break/break.cc:11-19`; `engine/src/break/break.h:25-29`; `engine/src/layout/layout.cc:513-521`; `docs/document-model.md:174`; `docs/design-decisions-v2.md:196`

costFn uses Σ spaceWidth as stretch capacity. break.h and breakKey state the DP reads only width/spaceWidth/breakWidth/breakPenalty. Layout, by contrast, distributes slack by stretchWeight (layout.cc:513-521, 578-585). Concretely:
- a Latin space has capacity 257su and a CJK char 102su, a 2.5:1 ratio, while the renderer weights them 1:0.6 (1.67:1);
- KP counts N CJK capacities for N chars, while the renderer realizes N−1 gaps, and only those followed by CJK or a closing punct.
document-model.md:174 and design-decisions-v2.md:196 assert agreement, which fails for mixed lines. Fix via the P1 GlueClass table read by both the breaker and layout.

*Verifier:* Confirmed:
- costFn takes Σ spaceWidth (break.cc:11-19, 70);
- breakKey has no stretchWeight (break.cc:168);
- layout distributes by weight and only for realized CJK gaps (layout.cc:513-521);
- this contradicts document-model.md:174 and design-decisions-v2.md:196.

*Verifier notes:* KP needs a px-valued capacity. One GlueClass table must define capacity (px) and render weight together, and inter-character glue must exist only where a gap is realized. Otherwise the N vs N−1 mismatch remains.

### `emitter/negative-wordspacing-overfull` — Overfull ragged/last lines get unbounded negative word-spacing

- kind: issue · severity: medium · verdict: partly · plan: **P0-12**
- locations: `engine/src/layout/layout.cc:569-581`; `engine/src/emit/emit.cc:208`

layout.cc:569-581 zeroes `d` on ragged or last lines only when slack > 0. An overfull ragged line therefore gets `d = slack/totalWeight` without bound. Probe p4: a heading containing a 950px URL at a 300px measure gives dw = −44851su (about −700px word-spacing), and the words overlap. d should be clamped to the shrink limit (cost.shrinkThreshold × capacity) and the line flagged as overflowing for the audit. The root cause on the emit side is adhoc url-break-special-path (no emergency breaks in noHyphen contexts).

*Verifier:* Reproduced: the heading with a long URL gives dw=-44851su.
Broader than stated: layout.cc:577-581 zeroes d only for last/ragged lines with positive slack. Any overfull line that has stretch weight, including a justified one that KP was forced into, gets an unbounded negative d. Ragged lines are not the only case.

*Verifier notes:* Clamping to the shrink limit and flagging overflow is correct. The emit-side root cause is the noHyphen gate on emergency breaks.

### `emitter/duplicate-diagnostics-on-reemit` — emit has diagnostic side effects but is re-run whole-document, so diagnostics duplicate

- kind: issue · severity: medium · verdict: partly · plan: **P0-11**
- locations: `engine/src/emit/emit.cc:723-725`; `engine/src/api/doc.h:232`; `engine/src/api/doc.h:242`; `engine/src/api/doc.h:247-254`; `runtime/src/worker/executor.mjs:115-121`

emit adds diagnostics directly: image-src at emit.cc:724, and math parse diagnostics via layoutMathSegments/layoutMathFormula. Doc::typeset re-runs emitDoc whenever `emitted` is false: after math-text metrics arrive (doc.h:247-254) and after provideImage/provideTokens (doc.h:232, 242). WASM probe p6 reports `warning image-src @[0,0)` twice for one figure. The span is also empty because figureBuild's image node gets no SPAN. Fix: make emit pure by buffering diagnostics per emit and replacing them on re-emit, or move the checks to ingest (scanImageReqs already inspects src).

*Verifier:* Verified with WASM. An unsafe figure alone gives one `image-src` diagnostic. Adding `$f(x) "if" x$` gives two.
Only the math-text re-emit (doc.h:253-254) triggers the duplicate. provideImage and provideTokens (doc.h:232, 242) clear `emitted`, but typeset() returns at doc.h:246 until every request is answered, so they always precede the first emit. The empty span `@[0,0)` is confirmed.

*Verifier notes:* Making emit pure (buffered diagnostics replaced per emit) is also a precondition for the cheap stale-emit fix.

### `emitter/silent-drops-of-unhandled-kinds` — Unhandled inline and block kinds vanish without a diagnostic

- kind: issue · severity: medium · verdict: accurate · plan: **P1-13, P2-11**
- locations: `engine/src/emit/emit.cc:170-172`; `engine/src/emit/emit.cc:822-824`; `engine/src/emit/emit.cc:802-811`; `docs/figure-design.md:30-32`

Both walkers' `default:` recurse into children, so leaf kinds without a handler disappear silently: inline image, inline raw, mathblock inside a paragraph, hardbreak, and block-level text/link/code nodes. Probes p3/p5: `Text before #image("x.png", {w:20,h:20}) text after.` produces no box and no diagnostic. Float figures also drop non-paragraph kids (documented in figure-design §8, still silent at runtime). Fix: a closed handler table per (level, kind) whose fallback emits an error box plus an `emit-unsupported` diagnostic.

*Verifier:* Inline image confirmed by probe. The 'hardbreak' example cannot occur: the syntax is reserved and not granted (document-model.md:60), and there is no ctor.

*Verifier notes:* The open `default:` recursions (emit.cc:170-172, 822-824) are the ad-hoc part. A closed (level, kind) table with an error-box fallback matches block-granular containment.

### `emitter/coarse-source-spans` — Every block carries its whole text node's span; run and line offsets degenerate

- kind: issue · severity: low · verdict: accurate · plan: **P4-03**
- locations: `engine/src/emit/emit.cc:184`; `engine/src/emit/emit.cc:321`; `engine/src/emit/emit.cc:388`; `test/golden/cjk/mixed.html.txt`; `test/golden/cjk/punct.layout.txt`

pushWordBlock, pushSynthetic, pushCjkChar and the space path all assign `n->span` (emit.cc:184, 262, 321, 388). As a result:
- golden cjk/mixed.html.txt has 17 runs, all with `data-s="0"`;
- in cjk/punct.layout.txt every line claims `@[0,192)`.
document-model §9.1 shows per-run offsets, and v2 §9's upgrade payload 'source-offset ↔ line map', which callers use for scroll anchoring, collapses to the whole paragraph for single-text paragraphs. Nothing consumes run offsets today, so this is latent, but it blocks click-to-source and per-line diagnostics. Fix: atoms carry byte offsets (node start + i when the text maps 1:1 to the source), which the P2 flattened stream gives naturally.

*Verifier:* Confirmed: n->span at emit.cc:184, 262, 322 and 388. cjk/punct.layout.txt has four lines all `@[0,192)`. cjk/mixed.html.txt has data-s="0" ×17 (lines plus runs).

*Verifier notes:* 'Node start + i' is wrong whenever text is not 1:1 with the source: escapes, CJK soft-wrap joins that drop the newline (inline.cc:48-59), and resolver-made text with empty spans. The inline parser must emit a per-text-node offset map (run-length deltas).

### `emitter/full-reemit-for-math-text` — Emit consumes measurement output, and a missing math-text width re-runs emit for the whole document

- kind: issue · severity: low · verdict: accurate · plan: **P1-20, P1-25**
- locations: `engine/src/api/doc.h:247-255`; `engine/src/emit/emit.cc:119-121`; `docs/math-design.md:370-400`

MathTextCtx feeds MetricStore into emit (doc.h:249). This inverts the layering, since emit sits upstream of measurement. Any missing name width re-runs emitDoc in full (doc.h:247-255), re-hyphenating every word and re-laying out every formula. In the editing loop this doubles emit cost for documents containing names. The behaviour is documented (math-design §14). Fix: text leaves inside a formula become pending InlineBox parts resolved in resolveWidths, with box packing deferred (P3).

*Verifier:* Confirmed at doc.h:247-255. Documented in math-design §14 (math-design.md:385-389).

*Verifier notes:* Formula box packing depends on text widths, so the fix is a per-formula re-layout when the widths arrive. That is still far cheaper than a whole-document emit.

### `emitter/dump-hides-finite-penalties` — Block dump prints 'pen=0' for any finite penalty and labels math glue 'boundary'

- kind: issue · severity: low · verdict: accurate · plan: **P1-12**
- locations: `engine/src/emit/emit.cc:1062`; `engine/src/emit/emit.cc:1073`; `engine/src/emit/emit.cc:1080`

dumpBlocks prints only INF or '0' (emit.cc:1073, 1080). URL pieces at penalty 1.2 show `pen=0` (probe p3). Math break glue prints as `boundary` with no penalty (emit.cc:1062), and punct-sp and boundary lines print no penalty at all. Goldens therefore cannot catch penalty regressions or kinsoku changes on glue. Fix: print the numeric penalty for every block.

*Verifier:* Confirmed: URL pieces print pen=0 for 1.2 (probe).
Also: math blocks print no penalty at all (emit.cc:1055-1059), and dumpBreaks skips every non-Text unit (emit.cc:1140). Table-cell, sidecar and float-caption breakpoints are never in any breaks golden.

### `emitter/missed:0` — Render run formation ignores BF_REF, so copy drops real prose next to citations and unresolved refs

- kind: missed · severity: high · verdict: verifier-found · plan: **P0-10, P4-01**
- locations: `engine/src/render/typeset_html.cc:454`; `engine/src/render/typeset_html.cc:582-590`; `engine/src/render/typeset_html.cc:546-552`; `engine/src/emit/emit.cc:79`; `engine/src/resolve/resolve.cc:259-267`; `engine/src/resolve/resolve.cc:279`; `test/golden/cite/basic.html.txt:7-11`; `runtime/src/main/copy.mjs:19`

BF_REF is a per-block flag. Neither the Latin-run predicate nor the CJK-run predicate splits on it, and openRun stamps `data-syn="ref"` from the run's first block only. The copy rebuild skips every run that has data-syn.

Cite brackets ('[' and ']', resolve.cc:259-267) and unresolved '??' (resolve.cc:279) have the surrounding style and no URL, so they merge with real text. The checked-in golden already shows it: `<span class="tsr-r" data-syn="ref">]; hyphenation patterns follow`. Copy would drop '; hyphenation patterns follow'.

Probe: `中文@nolabel after text` renders `<span data-syn="ref">?? after text</span>`. This violates the copy-fidelity contract (v2 §8, document-model §9.3).

*Proposed generalization (survey):* Run identity is a declared key computed in emit: (StyleId, link, copy-synthetic, anchor, script). It is carried as a runId on items. The renderer opens a span exactly at runId changes (assertable in debug), and fillSpaceContexts uses the same key. Copy-skip becomes a run attribute that cannot leak onto neighbours.

### `emitter/missed:1` — Break after an inline formula is unconditional; Latin closers and commas can start a line

- kind: missed · severity: medium · verdict: verifier-found · plan: **P4-02**
- locations: `engine/src/emit/emit.cc:145`; `engine/src/emit/emit.cc:353-355`

Every final math segment gets `breakPenalty = 0` ('CJK-context break after a formula is legal'). Only CJK closing punct retro-sets INF (emit.cc:354), and a following Latin ')' or ',' gets no rule.

Probe: `... then ($a=b$) more words ...` at widths 100 and 180 yields a line that begins with ') more words'. `$x+y+z$,` also admits a break before ','.

The inline object decides its own break-after without looking at its neighbour. This is the same paragraph-blind pattern, and the report missed this concrete case.

*Proposed generalization (survey):* Break opportunities between adjacent atoms come from one pair table (UAX#14-style classes such as CL/CP/IS/EX/QU, tailored per locale). Each inline object declares a class: math is Alphabetic-like, an image is an ideograph-like object. The shaper evaluates the pair; boxes never set their own break-after.

### `emitter/missed:2` — Unicode break-control characters are ignored: ZWSP, SHY, NBSP and U+3000

- kind: missed · severity: medium · verdict: verifier-found · plan: **P4-05**
- locations: `engine/src/emit/emit.cc:379-391`; `engine/src/emit/emit.cc:192-198`; `engine/src/support/support.h:158-162`

Only ' ' and '\t' are spaces.

Probe results:
- `foo​bar` is one unbreakable word block, so ZWSP gives no break.
- `ex­tra­ordinary` is one block with no points. The soft hyphen is neither a discretionary nor transparent, and its non-ASCII bytes fail the ASCII-core test, so adding SHY disables hyphenation of the word.
- `10 km` is a rigid word. NBSP is not stretchable, unlike TeX `~`.
- U+3000 is a stretchable 'ideograph'.

Authors have no plain-text way to add or forbid a break. The only attach rule is the CLS_SUP special case.

*Proposed generalization (survey):* Derive break classes from UAX#14 into the class table:
- ZW -> zero-width Penalty(0);
- WJ -> Penalty(INF);
- GL (NBSP, NNBSP) -> stretchable glue with Penalty(INF);
- SHY -> Disc{pre:[hyphenChar]} at hyphenPenalty;
- IdeoSpace -> fixed 1em glue.
This subsumes the footnote attach rule (resolver inserts U+2060) with no ARGK and no OPS bump.

### `emitter/missed:3` — Sub-flows (table cells, sidecar rows, float captions) are second-class streams built in a throwaway FlowUnit

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-17**
- locations: `engine/src/emit/emit.cc:592-598`; `engine/src/emit/emit.cc:670-674`; `engine/src/emit/emit.cc:802-810`; `engine/src/emit/emit.cc:161-168`; `engine/src/emit/emit.cc:1140`

Cells, sidecar rows and float captions are emitted as `FlowUnit tmp; inlineWalk(k, tmp, {})`, and only `tmp.blocks` is kept.

Consequences:
- Unit-level effects are lost. An inline labelled group (term) sets `tmp.anchor`, which is discarded. Probe: `#term[gizmo][..]` in a table cell; `@gizmo` resolves to href="#tsr-gizmo", but no element has that id (dangling link).
- The inherited ICtx is reset to `{}`. Float captions set only noHyphen.
- dumpBreaks skips all three, so their breaks are never golden-tested.
- Block structure inside a cell is flattened through the default recursion.

*Proposed generalization (survey):* Use one paragraph-emission function, `emitFlow(node, BlockTraits, ICtx) -> SubFlow{items, anchors}`, for top-level paragraphs and every sub-flow. Anchors are item attributes that layout realizes on the containing LineBox. Dumpers iterate a generic 'all streams of a unit' accessor.

### `emitter/missed:4` — Curly-quote class is wrong even inside one text node after CJK punctuation

- kind: missed · severity: medium · verdict: verifier-found · plan: **P4-02**
- locations: `engine/src/emit/emit.cc:434-443`

`prev != Prev::Cjk` lets Prev::Punct through. After '：', '，' or '、', an opening quote followed by a Latin letter goes Latin (body font, no half). Its closing quote, followed by CJK, goes CJK (CJK font with a compressible half).

Probe: unmarked `他说：“Hello”之后` gives `word "“Hel"` … `punct-close "”"` + punct-sp. This is a common mixed-text pattern. The report asserted the opposite for this exact text.

*Proposed generalization (survey):* Resolve ambiguous quotes as matched pairs in the shaper. Treat CJK punctuation as strong CJK evidence, apply explicit local lang first, and resolve both members of a pair to one class.

### `emitter/missed:5` — Script-to-font mapping is a hard-coded three-way bit switch decided by emit, with CSS class rules as a second copy

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-04**
- locations: `engine/src/emit/emit.cc:272`; `engine/src/measure/measure.h:61-64`; `engine/src/api/config.h:23-30`; `engine/src/render/typeset_html.cc:42-56`; `runtime/src/main/shell.mjs:28`; `runtime/src/main/shell.mjs:43`

Emit's script classification becomes a style bit (`compose(st, CLS_CJK)`). Measurement then picks the family by priority (fontFamily > CLS_CODE monoFont > CLS_CJK cjkFont > bodyFont). The renderer emits no family for code or CJK and relies on CSS (`.tsr-code{font-family:monospace}` and the .tsr-cjk var rule).

Consequences:
- Families are document-global.
- A `#style({font})` patch replaces the family for both scripts at once (style/patch.tsm).
- cfg.monoFont is measured but never rendered.
- CJK under CLS_EM is measured italic (measure.h:69) but rendered upright by `.tsr-cjk.tsr-i{font-style:normal}`.
- Heuristics such as the quote and dash Latin fallback silently choose the font.

*Proposed generalization (survey):* Make a style carry a per-script family map, e.g. `font: {latin, cjk, mono}`, resolved by TypoClass script into a concrete family per run. describeStyle and the renderer both emit the resolved family and italic, so the CSS class rules hold no font policy. This follows the §7 measurement-robustness principle that render must use exactly what was measured.

## break-layout-pages — Line breaking, block layout, floats, tables, pagination

<details><summary>Design summary (as audited)</summary>

This subsystem is spread over five places. emit produces FlowUnit/LinebreakBlock (emit.h:24-116). break/ holds a ~150-line windowed Knuth–Plass port (break.cc:21-145) with a content-hash cache and a retry ladder (break.cc:176-194). Doc::typeset() in api/doc.h:245-365 decides what width each unit breaks at: the F2 float tracker, equal table columns and sidecar widths. It stores those decisions on FlowUnit for layout to replay. layoutDoc (layout.cc:9-612) is a single per-kind if-chain that produces LineBoxes tagged with a magic `special`. Pagination is renderPages, a greedy band cutter inside the HTML serializer (typeset_html.cc:642-765).

Q1 Breaker. The dynamic program (DP) is feature-agnostic: it reads width/spaceWidth/breakWidth/breakPenalty plus LineWidths.at(lineIndex) and has no CJK/math/URL branches. It is not a box/glue/penalty engine, though:
- There is one block type with BF_* flags, and spaceWidth serves as both stretch and shrink.
- Glue at a break is not discarded.
- There are no forced breaks, discretionaries or fitness classes.
- When no solution exists, the whole paragraph comes back as one line.
- Feature knowledge lives in emit as one config knob per feature, and layout and render re-derive glue semantics from flags.

Q2 Layout. There is no general vertical model. Each kind has its own advance (rule band, raw declared height, code rows plus the sidecar zip, centred image/math, a table grid with 0.4em/0.3em padding, text lines). The only shared rule is paraGap, or paraGap/3 for tightAbove.

Q3 Floats. Only images can float, left or right. Same-side floats stack; an opposite-side float or any non-text unit clears. The float region carries across paragraphs but not across pages. Occlusion is a prefix parshape counted in baseLeading lines.

Q4 Pagination. It is not a separate stage. It hard-codes keep-with-next for headings and block images, widow/orphan = 2 for text units only, and atomic tables, floats and code lines (clipped when taller than a sheet). There are no footnote inserts, per-page float placement, running heads or page refs.

Q5 Tables. The table layout is closed: equal columns, constexpr padding, a rule after every row, inline-only cells, no spans, atomic for pagination, and duplicated between doc.h and layout.cc.

Q6. A general replacement is proposed in cross_cutting_notes: a TeX item model, ParShape built from an ExclusionMap, a block-layouter protocol over nested containers, a pluggable TableLayouter, and a VList-driven page-builder stage.


Strengths:

- The DP is genuinely feature-agnostic: breakLines reads only four block fields plus a line-indexed width function (break.cc:21-145). Parshape support needed zero DP changes (figure-design.md §8), which is the right foundation to generalize from.
- The break cache is keyed by block geometry and shared across documents (break.cc:147-194). It gives the editing loop a re-break-only-what-changed fast path, and the key's invariant is documented in place ('any new field the DP starts reading MUST be added here').
- Fixed-point su geometry, with quantized widths (ceil+ε) feeding the breaker and raw px feeding justification (document-model §6.1), is a principled answer to determinism and the DPR robustness contract. Every unit kind respects the nowrap line-takeover rule.
- Storing float decisions on units and replaying them in layout guarantees that break and layout cannot disagree at runtime, given the (questionable) split between the two stages.
- A single gutter mechanism (LineBox.marker, CSS right:100%) serves both list bullets and code line numbers (layout.cc:298-304). That is already the kind of reuse the rest of the subsystem lacks.
- The rational-grid solver is an isolated pure function (code/grid.h). The budget-vs-alignment split in verbatim-design §1 is clearly reasoned.
- Every stage has a byte-exact dump and goldens (blocks, breaks, layout, paged), and tsrc makes each stage inspectable. That made every claim in this review checkable.
- Underfull lines with no stretchable glue are set ragged instead of fake-justified (layout.cc:573-576), as TeX does with an underfull box.
- Pagination always terminates and is deterministic: a greedy cut with back-off and a greedy fallback (typeset_html.cc:732-742).

</details>


### `break-layout-pages/glue-semantics-split` — Glue semantics are re-derived from flag bits in three layers, so breaker, layout and render disagree

- kind: adhoc · severity: high · verdict: partly · plan: **P4-08**
- locations: `engine/src/emit/emit.h:10-22`; `engine/src/emit/emit.h:24-51`; `engine/src/break/break.cc:11-19`; `engine/src/break/break.cc:33-38`; `engine/src/break/break.cc:67-70`; `engine/src/layout/layout.cc:497-522`; `engine/src/render/typeset_html.cc:491-502`; `engine/src/render/typeset_html.cc:531-538`; `engine/src/render/typeset_html.cc:557-567`; `engine/src/emit/emit.cc:126-142`; `engine/src/emit/emit.cc:313-318`

LinebreakBlock is one struct whose meaning is carried by BF_* flags (SPACE, HYPHEN, CJK, PUNCT_GLYPH, PUNCT_SP, PUNCT_OPEN, BOUND, INDENT, PAIR, REF).

What each layer does:
- **Breaker:** uses `spaceWidth` as both stretch and shrink capacity (costFn: x = slack/Σ spaceWidth, shrink limit 0.37).
- **Layout:** ignores spaceWidth and justifies by `stretchWeight` (Latin 1, CJK k=0.6). It decides which CJK gaps stretch with a flag pattern: the next block is a CJK char or a closing punct glyph (layout.cc:514-522).
- **Render:** repeats that pattern twice to decide margin-right compensation (typeset_html.cc:531-538, 557-567).

Line-edge discarding (BF_SPACE is trimmed) exists only in layout (layout.cc:498-499) and render. The DP counts the trailing space or punct half in both contentW and totalSpace.

BF_BOUND, the CJK–Latin boundary flag, is reused as 'synthetic rigid glue' for math break points (emit.cc:130), so render prints data-syn="boundary" for math glue.

document-model.md:174 and design-decisions-v2.md:196 claim the cost model and the renderer 'agree by construction (n_latin + k·n_cjk)'. They do not: in the fixtures the breaker's CJK:Latin stretch ratio is 102su/289su ≈ 0.35 (0.1em glue vs the measured space), while the renderer uses k = 0.6.

*Why ad hoc:* No boundary separates what an item is (glue with stretch/shrink, discardable at a break) from how it is realized (word-spacing, letter-spacing on the preceding run, a margin or a pinned width). Every consumer re-encodes per-feature knowledge (CJK, punct compression, math, indent) from flags, so each new glue kind needs coordinated edits in emit, layout and render. The documented agreement-by-construction is not implemented, which makes this accidental rather than a deliberate trade-off.

*Proposed generalization (survey):* Replace the flag-driven block with an explicit TeX item list owned by emit/:
`struct HItem { enum K:u8{Box,Glue,Penalty,Disc}; Su w; Su stretch, shrink; u8 order; float pen; bool flagged; Realize real; double rawPx; Payload p; }`
where `Realize` is WordSpace | LetterSpacePrev | Margin | Fixed.

Breaker rules: apply TeX discard semantics (a break at glue drops it; glue and penalties after a break are dropped up to the next box).

Shared metrics: one `LineMetrics measureLine(items, from, to)` returns natural su width, Σstretch per order, Σshrink and raw px. The breaker's cost function and layout's justification both use it.

Layout: computes one adjustment ratio per line. Each glue item's realized delta = ratio × its stretch, so CJK k becomes the CJK glue's stretch value instead of a separate weight.

Emit: decides once whether a CJK gap carries glue (it emits none before Latin or an opening punct). The 'next is CJK' rule is deleted from layout and render.

Render: maps `real` to CSS with no flag inspection.

*Verifier:* (1) The stretch ratio is wrong. Goldens run at a 16px base: CJK glue=102su (test/golden/cjk/basic.blocks.txt) against a Latin space of 257su (4px + 1su epsilon). tsrc runs at an 18px base: glue=115su against a 289su space. Either way the breaker's CJK:Latin ratio is about 0.40, not 0.35; the report paired a 16px glue with an 18px space. The core claim still stands: breaker 0.40 (spaceWidth, break.cc:37,70) vs renderer k=0.6 (stretchWeight, layout.cc:513-519,583).
(2) The contradiction is inside the docs as well as between doc and code. App C (docs/design-decisions-v2.md:368-372) prescribes spaceWidth=0.1em for CJK chars and sw=width(' ') for spaces, which is exactly what emit.cc:318 and emit.cc:973-975 implement. v2 §8 (line 196) and document-model.md:174 instead promise breaker weights n_latin + k·n_cjk. The code follows App C in the breaker and §8 in layout.
(3) Render does not trim. It renders the range layout already trimmed (typeset_html.cc:472).
(4) The opening-punct half (emit.cc:349-350) is breakable BEFORE the glyph. When that break is taken, the half is TRAILING glue of the previous line and is counted there. A leading half occurs only when the break is taken at the preceding CJK char.
Everything else is confirmed: BF_BOUND reused for math glue (emit.cc:130) renders data-syn="boundary" (typeset_html.cc:491-498), and the next-is-CJK pattern is triplicated (layout.cc:514-522; typeset_html.cc:531-538, 557-567).

*Verifier notes:* This is a genuine special case, and the docs' own claim ('agree by construction') is false in code, so it is not a deliberate trade-off. The HItem/discard/measureLine direction is right, but the stretch unit needs pinning down.
- v2 §8 requires a UNIFORM per-gap Δ (one line-level CSS word-spacing) and Δcjk = k·Δword in ABSOLUTE px ('not rescaled per font size').
- If each glue's stretch is its own measured width, TeX-style (delta = ratio × stretch), Latin spaces of different sizes on one line get different deltas, and line-level word-spacing can no longer express that.
- Fix: give every WordSpace glue the same stretch s_ref (per paragraph) and CJK glue k·s_ref. Otherwise Realize::WordSpace must be emitted per run, which grows the DOM.
- shrinkThreshold=0.37 and the shrink limit are calibrated relative to the measured space width (break.cc:15-16). They must be re-derived once stretch and shrink become explicit item fields.
- The cache key (break.cc:149-174) must hash the new fields. The report notes this.

### `break-layout-pages/hyphen-url-not-discretionary` — Hyphen and URL breaks are special block types, not a general discretionary item

- kind: adhoc · severity: medium · verdict: accurate · plan: **P4-08**
- locations: `engine/src/emit/emit.cc:201-246`; `engine/src/emit/emit.cc:942-954`; `engine/src/break/break.cc:67-68`; `engine/src/layout/layout.cc:511-512`; `engine/src/layout/layout.cc:540-541`; `engine/src/render/typeset_html.cc:480-490`; `engine/src/api/config.h:39-43`

**Hyphen points.** A hyphen point is a BF_HYPHEN block:
- `breakWidth` = width("-").
- `width` = the junction kern, which applies only when the line does NOT break there (emit.cc:949-953). It is suRoundPx'd, so it gets no ε.
- The renderer prints a hard-coded "-" (typeset_html.cc:485).

When breaking AT the hyphen, the breaker adds both the kern and breakWidth (break.cc:67-68). Layout correctly drops the kern on a broken line (layout.cc:511-512). So with a negative kern the breaker under-estimates the line, against the ε over-estimate rule.

**URL breaks.** These are a second flavour: glyph-free penalties on word pieces, with a hard-coded separator set `/ ? & = . - _`, a minimum token length of 20 and a minimum of 3 chars between cuts (emit.cc:208-224).

The URL scan runs per text node, so inline markup inside a URL defeats it. In test/fixtures/doc/url-break.tsm, `Metal_movable_type.jpg?width=800` is split by `_movable_` emphasis, which leaves an unbreakable 10369su run 'type.jpg?width=800' (tsrc --stage=blocks --width=180).

*Why ad hoc:* Every break that inserts or removes material (hyphen glyph, glyph-free URL break, and later soft hyphen U+00AD, German ck→k-k, a repeated operator at math breaks, code continuation indents) becomes a new flag plus branches in break, layout and render. TeX's \discretionary is the general concept.

*Proposed generalization (survey):* Add `HItem::Disc { u32 pre, post; Su noBreakW; double noBreakRawPx; }`, where pre and post index a side array of HItems.

Breaker:
- Inside a line, a Disc contributes noBreakW.
- At a break, the line gets the pre-break width.
- The next line starts with the post-break material.

Emit makes a Disc for:
- hyphenation: pre = '-' box in the run's style, noBreakW = kern;
- URL separators: pre = ∅;
- U+00AD;
- optional operator repetition: pre = operator box.

Layout and render emit whatever material the chosen branch holds; there is no hard-coded '-'. The URL heuristic becomes a pass over the unit's flattened item list, after inline flattening, so markup boundaries no longer hide break opportunities.

*Verifier:* All claims confirmed:
- kern stored as width via suRoundPx with no epsilon (emit.cc:949-953);
- the breaker counts kern + breakWidth at a hyphen break (break.cc:67-68 with widthPsum including block i);
- layout drops the kern on a broken line (layout.cc:511-512);
- literal '-' (typeset_html.cc:485);
- the URL separator set, min length 20 and 3-char spacing (emit.cc:208-213).
Markup defeating the URL scan is reproduced: tsrc --stage=blocks --width=180 on doc/url-break gives the unbreakable piece 'type.jpg?width=800' w=10369su after the '_movable_' emphasis. The mock measurer is additive per codepoint (engine/src/measure/mock.h:15-19), so junction kerns are 0 and the 'no golden churn' claim holds.

*Verifier notes:* A Disc with pre/post/noBreak widths subsumes BF_HYPHEN and the URL pieces with no invariant conflict.
- Copy contract §9.3: pre-break material is data-syn.
- Robustness contract: the noBreak width should be quantized with the same epsilon policy as boxes; today it is suRoundPx.
- Running the URL heuristic over the flattened item list also fixes the per-text-node blindness.

### `break-layout-pages/break-policy-config-knobs` — Break-opportunity policy is one config knob per feature plus style-bit tests in emit

- kind: adhoc · severity: medium · verdict: accurate · plan: **P4-06, P4-08**
- locations: `engine/src/api/config.h:35-50`; `engine/src/emit/emit.cc:56-69`; `engine/src/emit/emit.cc:86-90`; `engine/src/emit/emit.cc:126-128`; `engine/src/emit/emit.cc:201`; `engine/src/emit/emit.cc:331-374`; `engine/src/ops/ops.def:38`; `engine/src/emit/emit.cc:170-172`

Penalties are set by feature-specific code paths, each with its own config key or implicit rule:
- Config keys: hyphenPenalty, urlBreakPenalty, urlBreakMinLen, mathRelAfterPenalty, mathRelBeforePenalty, mathBinAfterPenalty, punctCompress.
- Footnote markers glue backwards only when the run's style has CLS_SUP (emit.cc:89-90), so a presentation bit stands in for 'note marker'.
- 禁则 patches INF onto the previous CJK or math block before a closing punct (emit.cc:353-355).
- Inline code is one unbreakable block (emit.cc:56-69).
- Hyphenation requires at least 5 ASCII letters (emit.cc:201).

There are no forced breaks. Kind::hardbreak exists in ops.def:38 and renders as <br> in semantic HTML, but emit's inlineWalk default case drops it, and the DP has no −INF penalty.

*Why ad hoc:* The breaker is generic, but the policy that feeds it is a set of per-feature branches and knobs. Users cannot add a break class (e.g. no break before a unit symbol, breaks allowed inside long inline code), and presentation bits (CLS_SUP) are used as semantic proxies.

*Proposed generalization (survey):* Make the break policy a single data-driven pass in emit/:

1. Classify each item boundary as a pair (classBefore, classAfter). The class set is UAX #14 extended with engine classes: CJK-Ideo, CJK-Open/Close, MathRel/MathBin, UrlSep, NoteMark, InlineCode, HardBreak.
2. Resolve with `pen = policy.pair[a][b]`, plus Disc-creation rules, from `Config::breakPolicy{pairs, hyphen{minWord, penalty, flagged}, perLang overrides}`.
3. Assign classes from node traits (note marker, math atom, code span), never from style bits.
4. HardBreak maps to penalty −INF; the DP treats a penalty ≤ −INF as mandatory, which also replaces the special zero-break endpoint logic.
5. Expose class registration to the executor so user constructors can declare the break class of their inline nodes.

*Verifier:* Confirmed:
- per-feature knobs (config.h:39-50);
- CLS_SUP gating (emit.cc:89-90);
- 禁则 INF patch (emit.cc:353-355);
- unbreakable inline code (emit.cc:56-69);
- the ≥5-letter hyphenation gate (emit.cc:201);
- hardbreak (ops.def:38) has no emit case. The inlineWalk default (emit.cc:170-172) only recurses, and hardbreak has no kids, so it is dropped. No public constructor produces it.

*Verifier notes:* Additional conflation the report missed: ICtx.noHyphen ('display context') also disables URL break opportunities (emit.cc:208 `!noHyphen && w.size() >= urlBreakMinLen`). Headings (emit.cc:502), block captions (emit.cc:476) and float captions (emit.cc:807) therefore cannot break a long URL or path at all, and such a heading can only go overfull/collapse. The class table should keep 'no hyphenation' separate from the other break classes.

Caveats:
- Not everything is pairwise. punctCompress Full/Book/None changes the item structure (it pops or inserts half-space blocks, emit.cc:337-361), and URL detection needs token-length context. The report correctly restricts the class table to the break side.
- Letting user constructors declare a break class needs a new op arg, so OPS_VERSION must be bumped and fixtures re-recorded (CLAUDE.md conventions). The report does not mention this.

### `break-layout-pages/parshape-prefix-form` — LineWidths is a one-step prefix parshape (one narrow width for the first K lines), with the left offset stored separately on the unit

- kind: adhoc · severity: high · verdict: accurate · plan: **P3-08**
- locations: `engine/src/break/break.h:14-21`; `engine/src/break/break.cc:164-166`; `engine/src/emit/emit.h:102-104`; `engine/src/layout/layout.cc:558-563`; `engine/src/api/doc.h:311-312`; `engine/src/api/doc.h:344-351`; `docs/real-world-report.md:51-62`

`LineWidths{constant, narrow, narrowK}` with `at(i) = i<narrowK ? narrow : constant`. The horizontal offset is not part of the shape: it is a separate `narrowLeft` bool on FlowUnit that layout applies as `left += lineWidth − narrow`.

Consequences:
- Stacked floats of different widths are approximated by the wider one (doc.h:311-312).
- A float starting mid-paragraph, left and right floats at once, and drop caps (design-decisions-v2 §10) cannot be expressed.

real-world-report.md:51-62 already asks for a 'piecewise LineWidths' and a 'hanging-indent unit' for notes and bibliography. Built on this struct, each would be one more special case.

*Why ad hoc:* The shape is specialized to one scenario: a single float that begins at the paragraph start. Hanging indents, drop caps, two floats and list hanging markers all need the same mechanism with different data. Splitting width (in break) from offset (on the unit) makes the shape incomplete as an abstraction.

*Proposed generalization (survey):* `struct ParShape { std::vector<LineSlot> lines; LineSlot rest; }` with `LineSlot{Su left, width;}`, i.e. TeX \parshape.

Consumers:
- The breaker reads `width(i)`.
- Layout reads `left(i)`.
- The cache hashes the whole vector.

Producers:
- the ExclusionMap (see float-model-closed);
- paragraph traits hangIndent/hangAfter (bibliography, notes);
- drop caps;
- list hanging.

The shape is computed in layout and never stored on FlowUnit.

*Verifier:* Confirmed:
- LineWidths{constant,narrow,narrowK} (break.h:16-21);
- the key mixes narrow/narrowK (break.cc:164-166);
- narrowLeft lives on FlowUnit and layout applies left += lineWidth−narrow (layout.cc:559-563);
- widest-of-stack (doc.h:312);
- real-world-report.md:53-61 asks for piecewise widths and hanging indent.

*Verifier notes:* Documented as the F2 prefix form (figure-design.md:87-96), but the doc's own deltas and real-world-report already outgrow it. A \parshape vector of {left,width} is the minimal closure: there is no DP change (the DP already indexes widths.at(line), figure-design §8), and drop caps (v2 §10) need exactly this. Output is identical for today's prefix shapes as long as the justification formula stays as is (see measure-definition-split).

### `break-layout-pages/float-tracker-replay` — The float tracker lives in api/Doc::typeset, duplicates layout's spacing arithmetic, counts occlusion in baseLeading, and hands its decisions to layout through fields on FlowUnit

- kind: adhoc · severity: high · verdict: partly · plan: **P1-15**
- locations: `engine/src/api/doc.h:263-361`; `engine/src/api/doc.h:282-283`; `engine/src/api/doc.h:307-310`; `engine/src/api/doc.h:347`; `engine/src/api/doc.h:355-357`; `engine/src/emit/emit.h:102-106`; `engine/src/layout/layout.cc:28-29`; `engine/src/layout/layout.cc:46-48`; `engine/src/layout/layout.cc:606`; `docs/figure-design.md:98-117`; `docs/figure-design.md:171-176`

The float decision procedure runs inside the API handle's typeset loop.

Duplicated arithmetic. It re-implements layout's spacing:
- doc.h:282 `tightAbove ? paraGapSu/3 : paraGapSu` duplicates layout.cc:28;
- the inter-block gap duplicates layout.cc:606;
- the table colW and the sidecar breaking (doc.h:322-342) duplicate layout as well.

Replay fields. It writes five mutable fields into the emit product (narrow, narrowK, narrowLeft, floatShiftSu, floatClearSu), and layout replays them.

baseLeading approximation. Occlusion is consumed as `breakpoints.size() × baseLeading` (doc.h:356), even for units that are already fully broken and whose true heights are knowable. Headings are 1639su per line (paged-doc and table goldens) versus a baseLeading of 1536su, and lines with inline math are taller still. The error accumulates across units, and the float region under-clears by the sum. Float captions are charged and laid out at a flat baseLeading (doc.h:308-309, layout.cc:79-80).

Documented. figure-design §4 ('FloatTracker runs inside Doc::typeset()') and §8 ('mirrors layout's exact gap accounting') record this, and design-decisions-v2 §10 calls line-index widths an intentional approximation.

*Why ad hoc:* The break/layout split forces the tracker to predict layout instead of observing it. Layout policy ends up in api/ (architecture.md §2.2 assigns 'Breaks + vertical metrics → Frames' to layout/), and FlowUnit becomes scratch state.

On the documented rationale: the TeX \parshape-in-lines approximation is only unavoidable inside the paragraph currently being broken. For completed units the approximation is self-inflicted by the stage split.

*Proposed generalization (survey):* Fold breaking into layout:
- The paragraph layouter calls `breakLinesRetry(items, shape, params)` when it reaches the unit. The call is still content-hash cached, so the editing fast path survives.
- `shape` is built from the live ExclusionMap at the current cursor y.
- After breaking, the layouter materializes lines with real heights and advances y exactly.

The remaining approximation, line y inside the current paragraph, is refined by at most one deterministic re-break when realized heights cross an exclusion boundary (fixed pass count of 2).

FlowUnit becomes immutable emit output, and LayoutResult owns every decision.

*Verifier:* (1) The direction of the baseLeading error is wrong.
- Every laid-out line advances max(baseLeading, asc+desc) (layout.cc:596-597), which is never less than baseLeading.
- Charging lines × baseLeading (doc.h:356) therefore OVER-estimates the remaining occlusion.
- The result is over-narrowing (narrowK = ceil(remain/baseLeading) lines extend below the float) and over-clearing of later non-text units (floatClearSu = tracker remain ≥ real remain, doc.h:318). That is extra whitespace, not overlap.
- The only genuine under-count is layout skipping all-space lines (layout.cc:501 `if (lo >= hi) continue`) that the tracker still counts via breakpoints.size().
- figure-design.md:115-117 uses the same wrong 'under-clears' wording.
(2) Sidecar breaking is not duplicated in layout. layout only reads u.sidebarW and the cell breakpoints (layout.cc:126, 312-358). It is misplaced in api/, not duplicated.
(3) Confirmed: the gap mirroring (doc.h:282-283 vs layout.cc:28, 606), colW duplication (doc.h:324-327 vs layout.cc:404-408), the five replay fields (emit.h:102-106), and heading lines of 1639su vs baseLeading 1536su (test/golden/region/table.layout.txt:3).

*Verifier notes:* Documented in figure-design §4/§8 ('the only place that already visits units in reading order with breakpoints in hand'), but layoutDoc also visits units in reading order, so the rationale is location convenience, and it produces cross-module lockstep code.

The generalization is sound against the invariants:
- Breaking inside layout keeps the content-hash cache, so the editing fast path survives.
- A fixed refinement count keeps it deterministic.
- v2 §10's 'line index ≈ position' approximation is then confined to the paragraph being broken, which is all §10 claims to need.

Correct the motivation, though: the practical gain is removing duplicated policy and wasted whitespace, not fixing an overlap.

### `break-layout-pages/float-model-closed` — The float model is closed: images only, two sides, same-side stacking, everything else clears, and occlusion is a width delta

- kind: adhoc · severity: high · verdict: accurate · plan: **P3-15**
- locations: `runtime/src/worker/executor.mjs:211-217`; `engine/src/emit/emit.cc:740-741`; `engine/src/emit/emit.cc:785-814`; `engine/src/api/doc.h:288-321`; `engine/src/api/doc.h:345`; `engine/src/layout/layout.cc:32-86`; `engine/src/layout/layout.cc:609`; `docs/pages-design.md:124-126`

What can float. Only `image` nodes (`side: opts.float ?? opts.side`). Float figures keep only `para` kids as caption cells and silently drop everything else (emit.cc:802-803; documented in figure-design §8).

Placement rules, hard-coded in doc.h:288-321:
- same side: stack (floatShiftSu);
- opposite side: clear the previous float, so text can never run between two floats;
- every non-Text unit clears: code, table, math, rule, raw and block images. figure-design §4 documents this ('magazine-style code wrap is out of scope').

Geometry problems:
- Occlusion is 'measure minus (imgW + 1em)', taken relative to each wrapped unit's own indent, not an absolute x-interval. The box itself sits at its own unit's indent (layout.cc:37-38).
- Narrowing engages only when occl < measure − 1px (doc.h:345). There is no minimum usable column width, and wide floats get neither narrowing nor clearing (see other_issues).
- A float can only sit at its source position: there is no top/bottom/page placement and no deferral.
- Pagination does not re-place floats (pages-design §5).
- A trailing float needs a docHeight watermark special case (layout.cc:609).

*Why ad hoc:* Floating is a property of one leaf kind, implemented as a hand-written three-case state machine. Users cannot float a table, code listing, aside box or pull-quote. Exclusions are not first-class geometry, so text between two floats and per-page re-placement cannot be expressed.

*Proposed generalization (survey):* Four changes:

1. `float: left|right|inline-start|…` becomes a block trait on any block or group. The block's layouter lays it out off-flow in a Container at its declared or shrink-to-fit width, and hands the resulting Box to a layout-owned FloatManager.
2. `ExclusionMap { std::vector<Rect> rects; Interval available(Su y0, Su y1, Interval measure) const; }` holds exclusions in absolute su. A paragraph's ParShape is computed by querying it line by line. This gives left and right floats at once, stacks of different widths, floats starting at any y, and correct geometry across different indents.
3. Units declare a capability `narrowable` (text; code with first-fit wrapping; tables with Auto columns). Units that are not narrowable get `clear`: y advances to the first band whose available width is at least the unit's minimum. A paragraph also clears when the available width is below `minWrapWidth` (for example 8em).
4. Each float gets a placement policy `here|top|bottom|page` and a deferred queue that the page builder consumes.

*Verifier:* Confirmed:
- only image takes `side` (executor.mjs:214-217, emit.cc:740-741);
- non-para float-figure kids are dropped (emit.cc:802-803);
- same-side stack, opposite clears, non-text clears (doc.h:294-321);
- the 1px threshold (doc.h:345);
- relative-indent occlusion (doc.h:344-346, layout.cc:37-38, 562);
- the docHeight watermark (layout.cc:609);
- no per-page re-placement (pages-design.md:124-126).

*Verifier notes:* The scope is documented (figure-design §4: 'Simple, predictable; magazine-style code wrap is out of scope'), but structurally it is a hand-written three-case state machine bound to one leaf kind, which is the owners' concern.

The ExclusionMap in absolute su plus a 'narrowable' capability is the right closure, and it depends on float-tracker-replay first.
- Placement 'top|bottom|page' only means something on the paged path. The screen path (one infinite page, v2 §10) keeps 'here', so progressive per-paragraph swap is unaffected.
- Deferred floats change visual order but not DOM/copy order, so copy (§9.3) is fine.

### `break-layout-pages/unit-kind-switch` — Block layout is a switch over FlowUnit::K, producing a union LineBox tagged by magic `special` numbers

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-18**
- locations: `engine/src/emit/emit.h:63-116`; `engine/src/layout/layout.h:7-32`; `engine/src/layout/layout.cc:32-602`; `engine/src/layout/layout.cc:614-660`; `engine/src/render/typeset_html.cc:177-431`; `engine/src/render/typeset_html.cc:681-709`; `engine/src/emit/emit.cc:1019-1024`

FlowUnit is a union of every kind's fields:
- code: codeRuns, chRef, cjkChRef, codeLang, sidebarW, codeLineNo, hlLines;
- raw: rawHtml, rawHpx;
- image: imgSrc/Alt/W/H, floatSide;
- math: mathBox, eqTag;
- table: tCols, tAligns, cells;
- plus the float replay fields.

layoutDoc is an if-chain (float image, Rule, Raw, Code, block Image, Math, Table, Text), each branch with its own advance rule.

LineBox carries code-only fields (codeLine, cbLo/cbHi, codeCont, contCols, snapLatinPx/CjkPx, codeHl) next to justification fields. It is tagged with `special` = 0 text, 1 rule, 2 code, 3 raw, 4 math, 5 image; layout.h:20 documents only 0–4.

The renderer (renderLineBox), dumpLayout, dumpBlocks and renderPages each switch again on `special` and/or FlowUnit::K. The renderer even recomputes the display-math advance from cfg (typeset_html.cc:226-230, 246-249).

Adding a block kind touches eight places: emit.h, emit.cc, doc.h, layout.h, layout.cc, renderLineBox, renderPages and two dumpers. The only user-extensible block is `raw` (opaque HTML plus a declared height).

*Why ad hoc:* Layout and render know every block's semantic identity instead of consuming a geometric protocol. The layer is not closed under extension: built-ins are privileged code paths that user constructors cannot reach.

*Proposed generalization (survey):* Geometric output:
- `struct Box { BoxType type; Su x, y, w, h, depth; u32 payload; }`. BoxType comes from a small registry (TextLine, Rule, Image, MathDisplay, Raw, CodeRow, Frame), and payload indexes a per-type side table.
- Render dispatches through a `BoxRenderer` registry; pagination never looks at types.

Input protocol:
- `struct BlockLayouter { virtual void layout(const FlowUnit&, LayoutCtx&, VList& out) = 0; }`, registered per kind or role.
- `LayoutCtx` exposes the measure interval, the ExclusionMap, metrics and `layoutChildren(container, rect)`.
- FlowUnit becomes `{kind, traits, std::variant<payloads>}`.

Built-ins become registrations. `raw` handlers, and other user constructors, can request a built-in layouter by name (paragraph, intrinsic-size box, grid, container), so extensions reuse the built-in algorithms on equal footing.

*Verifier:* Confirmed:
- `special` 0-5 with layout.h:20 documenting only 0-4;
- per-kind if-chain (layout.cc:32-602);
- re-switching in dumpLayout (layout.cc:621-651), dumpBlocks (emit.cc:1019-1050), renderLineBox (typeset_html.cc:183-431) and renderPages (typeset_html.cc:681-709);
- the renderer recomputes the display-math advance from cfg (typeset_html.cc:226-230, 246-249).

*Verifier notes:* Sound, with one clarification. User constructors run in JS (the executor) and cannot register C++ BlockLayouters in the WASM build. 'Equal footing' therefore means users COMPOSE registered layouters through traits and data, not implement new ones.

That is still a large gain over today, where the only user-reachable block geometry is raw with a declared height. The registry and dispatch are dual-target clean.

### `break-layout-pages/nested-stream-copies` — 'TableCell' doubles as table cell, float caption and sidecar row, and the line-materialization loop is copy-pasted four times with diverging features

- kind: adhoc · severity: high · verdict: partly · plan: **P1-17**
- locations: `engine/src/emit/emit.h:57-61`; `engine/src/emit/emit.cc:592-598`; `engine/src/emit/emit.cc:665-681`; `engine/src/emit/emit.cc:802-811`; `engine/src/layout/layout.cc:49-83`; `engine/src/layout/layout.cc:312-358`; `engine/src/layout/layout.cc:423-485`; `engine/src/layout/layout.cc:491-602`; `runtime/src/worker/executor.mjs:79-82`

Four separate loops turn breakpoints into LineBoxes, each with a different feature set:
- Text (layout.cc:491-602): space trimming, vmet and math heights, justification, join, centering, markers, parshape.
- Table cells (423-485): trimming, heights, l/c/r shift, endsWithHyphen. No join, no justification.
- Sidecar rows (312-358): trimming, heights, srcSpan. No alignment, no hyphen flag.
- Float captions (49-83): trimming and join, but a flat baseLeading with no vmet or math heights (figure-design §8: 'formulas in float captions may sit tight'). No centering, although block captions are centered.

Cells are inline-only: emit flattens cell kids through inlineWalk (emit.cc:672). The block content that the executor appends to the last cell ('continuation → last cell', executor.mjs:80-82) is therefore silently concatenated.

*Why ad hoc:* 'A paragraph nested inside a box' is one concept, but each feature grew its own copy with its own subset of behaviour. The copies have already drifted: join exists for captions but not cells, and vmet heights exist for cells but not captions.

*Proposed generalization (survey):* Use one `materializeLines(const ItemList&, const BreakResult&, const ParShape&, const LineEnds&, const MetricStore&, JoinPolicy) → std::vector<LineBox>` for every paragraph.

Nested content becomes a real container: `struct Container { std::vector<FlowUnit> units; BoxModel box; }`, laid out through `ctx.layoutChildren(container, innerRect)`.

Table cells, float boxes, sidecar cells and figure bodies all hold Containers. Cells can then contain lists, code, display math and nested tables, and captions get the same treatment everywhere. The copy-contract difference (document-model §6.3: cells carry no data-join) becomes a `JoinPolicy` per container instead of a code divergence.

*Verifier:* Four line loops confirmed: layout.cc:49-83, 312-358, 423-485, 491-602.

The first 'drift' example is wrong. Table cells carrying no data-join is SPECIFIED (document-model.md:183-184), not drift. Float captions lacking vmet is a documented limitation (figure-design.md:176-178).

Better evidence of real drift:
- Sidecar rows never set endsWithHyphen (layout.cc:344-357). Yet sidecar cells are emitted with hyphenation ON (emit.cc:595 uses a default ICtx), and the renderer prints '-' only when l.endsWithHyphen (typeset_html.cc:482). A sidecar line broken at a hyphen point therefore renders with no hyphen glyph, and copy then glues the pieces.
- Float captions are never centered (cl.left = boxLeft, layout.cc:63), while block captions are (emit.cc:475, layout.cc:588-594).

Cell block content concatenated through inlineWalk is confirmed (emit.cc:672; executor.mjs:81-82).

*Verifier notes:* One materializeLines plus Containers is the right closure.

The cost claim is slightly optimistic. If captions really get 'the same treatment everywhere', float captions become centered and their goldens churn (figure/float, figure/stack), not only those with tall content.

No ops change is needed: tcell kids can already be block nodes from the executor.

### `break-layout-pages/table-closed` — Table layout is one hard-wired algorithm (equal columns, constant padding, a rule after every row), duplicated in two modules

- kind: adhoc · severity: high · verdict: accurate · plan: **P3-10, P3-14**
- locations: `engine/src/layout/layout.cc:401-490`; `engine/src/api/doc.h:322-334`; `engine/src/api/config.h:78-80`; `engine/src/emit/emit.cc:647-684`; `runtime/src/worker/executor.mjs:76-93`; `engine/src/render/typeset_html.cc:681-684`; `docs/document-model.md:176-196`; `docs/real-world-report.md:58-59`

Geometry:
- `colW = (measure − indent)/tCols` is computed both in Doc::typeset (to break cells) and in layoutDoc (to place them), each with kTableCellPadEm and a 64su (1px) floor.
- Padding of 0.4em/0.3em is a constexpr outside Config, so neither hosts nor documents can change it.
- Rules are full-width, before the first row and after every row: a full grid, not booktabs/三线表.

Missing features:
- Alignment is per-column 'l'/'c'/'r' from a character string only.
- No vertical alignment (top only).
- No width specs or content-fitted columns (real-world-report.md:58-59: the HoTT table needs them).
- No row/col spans, no header rows, no per-edge rules.

Other defects:
- Cells beyond `cols` are silently dropped, in both executor.mjs:88 and emit.cc:669.
- trow/tcell carry no source spans (the table.tree golden shows @[0,0)).
- Pagination treats the whole table as one atom, clipped on an oversized sheet (pages-design §5).

Documented as 'M6 v1' in document-model §6.3.

*Why ad hoc:* There is no seam to swap column sizing, rule style or cell alignment. The geometry is duplicated across two modules that must agree, and user fence handlers cannot express any table geometry beyond cols/align.

*Proposed generalization (survey):* `TableLayouter` in layout/, driven by:
`struct TableSpec { std::vector<TrackSpec> cols; RuleSpec rules; Su padX, padY; std::vector<CellPlacement> cells; u32 headerRows; }`
- `TrackSpec` = Fixed(su) | Fraction(f) | Auto | MinContent | MaxContent.
- `RuleSpec` = rule weight per edge, with presets grid/booktabs/none.
- `CellPlacement` = {row, col, rowspan, colspan, halign, valign}.

Column sizing is a pluggable `ColumnSizer`:
- Equal (today's behaviour).
- Auto: a CSS-like two-pass using per-cell min-content (the widest unbreakable run, read off the item list) and max-content (the unbroken width).
- Fixed.

Cells are Containers (see nested-stream-copies). Horizontal alignment goes through LineEnds glue and vertical alignment through a row-box offset.

Pagination: each row goes into the VList as a Box with a Penalty of 0 between rows, plus Marks so header rows can repeat.

Authoring: `#!table(cols, align, widths:, rules:, header:)` and user fence handlers both build the same TableSpec.

*Verifier:* Confirmed:
- duplicated colW/padding/64su floor (doc.h:324-327 vs layout.cc:404-408);
- constexpr padding (config.h:79-80);
- a rule before the first row and after every row (layout.cc:419, 487);
- 'l'/'c'/'r' shift (layout.cc:460-466);
- silent truncation (executor.mjs:88, emit.cc:669);
- trow/tcell @[0,0) (test/golden/region/table.tree.txt:5-6);
- whole-table atomic paging (typeset_html.cc:681-684; pages-design.md:121-123).

*Verifier notes:* Documented v1 scope (document-model §6.3), but the duplication across api/ and layout/ and the unconfigurable constants are accidental.

TableSpec + ColumnSizer + Container cells is sound. The report correctly flags the OPS_VERSION bump and the full re-record. Auto columns need min/max-content breaks per cell. These are deterministic and cacheable through the same break cache.

### `break-layout-pages/alignment-flags` — Alignment is per-feature flags and per-kind centering code, not line-end glue that the breaker knows about

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-09**
- locations: `engine/src/emit/emit.h:67-69`; `engine/src/emit/emit.cc:471-476`; `engine/src/emit/emit.cc:498`; `engine/src/layout/layout.cc:569-594`; `engine/src/layout/layout.cc:371-373`; `engine/src/layout/layout.cc:388-390`; `engine/src/layout/layout.cc:460-466`; `engine/src/render/typeset_html.cc:226-241`; `engine/src/render/typeset_html.cc:246-249`; `engine/src/render/typeset_html.cc:324`

Five separate mechanisms place slack on a line, all on the layout or render side:
- `ragged` (headings, captions, images, math, errors);
- `centered` (figure captions only);
- `isLast` (`bp==size || ragged`, with d=0 when slack>0);
- `noGlue` (underfull lines);
- table `tAligns` shifts.

Display math and block images are centered by their own code.

The equation number is positioned by the renderer from `cfg.widthPx` (typeset_html.cc:232-241). The renderer also recomputes the display line height from `cfg.lineHeight*baseSizePx` instead of reading `l.height`.

The breaker evaluates ragged and centered paragraphs with the justified cost function.

*Why ad hoc:* Each alignment need added its own branch. The breaker cannot optimize ragged text the way TeX does with a stretchable \rightskip, and the renderer reaches back into config to recompute layout geometry.

*Proposed generalization (survey):* Add `struct LineEnds { Glue leftSkip, rightSkip, parFillSkip; }` to BreakParams; the shared measureLine includes it.

Presets:
- justified = (0, 0, 0+fil)
- ragged-right = (0, 0+2em finite stretch, 0+fil)
- centered = (0+fil, 0+fil, 0)
- flush-right = (0+fil, 0, 0)
- cell halign maps onto the same presets.

Layout distributes slack by stretch order. isLast, ragged, centered, tAligns and noGlue all disappear; an underfull line is simply one where the highest-order glue absorbs the slack.

Display math and display images become one-box paragraphs with centered ends. The equation number is a flush-right box on the same line (TeX's \eqno idea), positioned in layout and stored in the Box. The renderer never reads cfg geometry.

*Verifier:* Confirmed:
- ragged/centered flags (emit.h:67-69; emit.cc:474-475, 498, 709, 750, 767);
- isLast/noGlue (layout.cc:569-586);
- centering code for math/image/cells (layout.cc:371-373, 388-390, 460-466);
- eqno placed from cfg.widthPx and display height recomputed from cfg (typeset_html.cc:226-241, 246-249);
- breakLinesRetry uses cfg.cost regardless of ragged (doc.h:260-262).

*Verifier notes:* LineEnds with ordered (fil) glue is the TeX closure and needs the stretch-order field from glue-semantics-split.

An extra benefit the report did not name: making eqno a layout box enables collision handling. Today layout centers the formula on the full lineWidth (layout.cc:388) and the renderer pins the number to the measure edge, so a wide formula and its number can overlap undetected. TeX would move the number to its own line.

Alignment must be decoupled from the copy 'join' as well. See the missed item join-conflated-with-ragged.

### `break-layout-pages/vertical-spacing-constants` — Vertical spacing is a global paraGap with magic fractions and per-kind advances, not style-driven vertical glue

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-18, P3-01**
- locations: `engine/src/layout/layout.cc:13-14`; `engine/src/layout/layout.cc:28`; `engine/src/layout/layout.cc:93-96`; `engine/src/layout/layout.cc:394-396`; `engine/src/layout/layout.cc:604-606`; `engine/src/emit/emit.cc:535-537`; `engine/src/emit/emit.cc:697`; `engine/src/api/config.h:31-33`; `engine/src/api/config.h:64-65`; `engine/src/api/config.h:82-84`; `engine/src/render/typeset_html.cc:625-628`

Every unit boundary gets `paraGap` (1.2em), or `paraGap/3` when `tightAbove`. tightAbove is set on every unit after the first in a list, not only on item starts (emit.cc:535-537).

Missing concepts:
- per-kind space before/after: headings get no extra space above, and display math has no display skips;
- stretch/shrink, so pages cannot be balanced;
- margin collapsing.

Per-kind advances and constants:
- A Rule is a baseLeading band with y stored at its midline.
- A Raw block defaults to one leading.
- Display math advances by max(box, baseLeading).
- Heading sizes are a hard-coded 1.6/1.35/1.15 table.
- listIndentEm and quoteIndentEm are dedicated config keys.

The flowing renderer separately emits `margin-bottom: paraSpacingEm*baseSizePx` in unrounded px. LayoutResult's fr.y uses the su-rounded paraGap, so DOM positions and LayoutResult y can drift by a fraction of an su per paragraph.

*Why ad hoc:* Spacing knowledge is split between emit (tightAbove), layout (fractions) and render (margin). It cannot be configured per role, and user regions cannot request any spacing.

*Proposed generalization (survey):* Layouters append `VGlue{natural, stretch, shrink, discardableAtBreak}` taken from a role/style spacing table:
`spacing[role] = {before, after, collapse: max|sum, stretch, shrink}`.

Layout resolves it with margin collapsing. Defaults reproduce today's numbers: para 1.2em, list-internal 0.4em, heading before/after, display skips.

The renderer positions paragraph frames from LayoutResult su values only, never through a parallel config formula.

*Verifier:* Confirmed:
- tightAbove is applied to every unit after the first in a list (emit.cc:535-537), contradicting the emit.h:67 comment 'list-item start';
- paraGap/3 (layout.cc:28);
- rule midline band (layout.cc:93-96);
- display-math floor (layout.cc:394-395);
- raw default one leading (emit.cc:697);
- headingSizeMul (config.h:82-84).
The renderer emits margin-bottom = paraSpacingEm·baseSizePx unrounded (typeset_html.cc:625-628), while fr.y uses suRoundPx: 21.6px vs 1382su = 21.59375px at 18px.

*Verifier notes:* Collapsible VGlue with a role spacing table reproduces today's numbers and also removes float-adds-paragraph-gap.

The flowing DOM never uses fr.y or docHeightSu (paras are position:relative in normal flow). The cleanest fix for the drift is to have the renderer derive frame positions or margins from LayoutResult su values only.

### `break-layout-pages/code-wrap-in-layout` — Code wrapping is a second, private line breaker inside layoutDoc, with hard-coded break classes and magic column constants

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-11**
- locations: `engine/src/layout/layout.cc:112-363`; `engine/src/layout/layout.cc:145`; `engine/src/layout/layout.cc:157-163`; `engine/src/layout/layout.cc:168-171`; `engine/src/layout/layout.cc:188-220`; `engine/src/layout/layout.cc:231-232`; `engine/src/code/grid.h:20-58`; `docs/code-design.md:84-89`; `docs/verbatim-design.md:108-116`

About 250 lines inside layoutDoc perform:
- greedy column filling with a fixed breakable set `' ' '\t' , ; ) } ] >`. code-design §4 speaks of a 'token boundary' preference, but no token or language information is used;
- CJK 禁则 via isPunctOpen/isPunctClose;
- an 8-column minimum, written out in four places (145, 163, 217, 232);
- snap-kerning acceptance of `d ≤ 0.1·ch` (157-158);
- hanging continuation = the line's own leading whitespace + cfg.verbatimContIndent;
- comment-aware hanging via an ASCII alnum scan for the comment lead-in (196-215).

Because wrapping happens in layout:
- Doc::typeset cannot know code heights, so code must clear floats;
- pagination has to rediscover logical-line grouping from LineBox fields.

This is deliberate: code-design §4 says 'no Knuth, by decision', and verbatim-design §6 says 'two code paths stay'.

*Why ad hoc:* The documented rationale concerns the algorithm (greedy, grid widths) and the grid contract. Neither requires a second breaker living in the layout stage with its own item model. The break classes and the continuation policy are hard-coded, even though the token provider already carries the language knowledge.

*Proposed generalization (survey):* Express each logical code line as an HItem list:
- boxes with grid widths (latin = q atoms, CJK = p atoms, × atomSu from GridSpec);
- Penalty items: low at token boundaries, high between identifier chars, supplied by the token fold or a per-language class table;
- Disc items whose post-break material is an indent box of contCols columns. Emit computes the hanging or comment-aware column from the run structure.

Break with `Strategy::FirstFit` through the shared breaker API, then the shared materializeLines.

Snap-kerning stays a render-level realization (letter-spacing per script run). The code layouter adds only gutter and row-height rules. Byte-lossless copy keeps trailing spaces in the row (layout.cc:264-268) as a Disc convention.

*Verifier:* Confirmed:
- the fixed break set (layout.cc:168-171);
- the 8-column floor in four places (145, 163, 217, 232);
- d ≤ 0.1·ch acceptance (157-158);
- the ASCII lead-in scan (layout.cc:201-212, not 196-215).

code-design.md:85-86 defines the preference as 'token boundary (space/punct)', so the fixed set matches the doc's own definition. The gap is that no token or language information is used, not doc drift.

*Verifier notes:* Deliberate and documented: code-design §4 'no Knuth, by decision'; verbatim-design §6 'two code paths stay'. The report engages correctly by keeping a distinct strategy (FirstFit) and unifying only the item and penalty model and the location.

One gap stops a static Disc from subsuming contColsAt. The continuation indent is clamped against the measure (`cc > colCap - 8`, layout.cc:216-217), and row availability is floored at 8 atoms (231-232). Post-break material is therefore width-dependent and cannot be a fixed Disc computed in a measure-independent emit. Either the FirstFit strategy evaluates a clamp on post-break indent boxes at break time, or the clamp becomes a minimum-content-width rule in LineEnds.

Atom-unit widths ARE measure-independent: p, q and atomPx in solveGrid do not depend on maxCols, which only feeds `exact` (grid.h:47-48), and `exact` is unused by layout. So that part is fine.

### `break-layout-pages/code-sidecar-three-box` — The verbatim three-box model (gutter/code/sidecar) is hand-coded as Code-unit fields plus an equal-height row zip

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-11**
- locations: `engine/src/api/doc.h:84-150`; `engine/src/api/doc.h:335-342`; `engine/src/emit/emit.cc:579-602`; `engine/src/layout/layout.cc:120-128`; `engine/src/layout/layout.cc:309-360`; `engine/src/api/config.h:53`; `docs/verbatim-design.md:71-106`

verbatim-design §5 defines a code block as 'three horizontally arranged boxes … share exactly ONE constraint: rows belonging to the same logical line are equal-height'.

The implementation:
- adds sidebarW, computed in emit from cfg.widthPx·sidebarFrac;
- reuses `cells` for sidecar rows and breaks them in Doc::typeset;
- inserts a gap of one code em, with a 64su floor;
- zips rows by tracking rowTop/cy;
- puts line numbers on a third mechanism (LineBox.marker).

The sidecar split itself is a string-level content transform in api/doc.h (`extractSidecars`). It creates `group{role:"sidecar-lines"}`, which emit then recognises by string comparison (emit.cc:587-589).

*Why ad hoc:* The documented definition is literally a grid with three column tracks and equal-height rows, which is what a table is. It was built as a Code-only special case with its own column partition, row zip and role string, and with a content transform in api/.

*Proposed generalization (survey):* Lower a code block with sidecar or line numbers into a grid container:
`TableSpec{cols: [Auto (gutter), Fraction(1−f) (code: FirstFit grid paragraph), Fraction(f) (sidecar: KP paragraph)], rules: none, rows: one per logical line}`.

Reuse TableLayouter and Container cells (see table-closed and nested-stream-copies).

The marker split moves to the fence/executor level, which produces ordinary trow/tcell structure, so no role string is needed. Line numbers become the gutter track or stay as markers.

*Verifier:* Minor: the 64su floor applies to the code column width (layout.cc:127), not to the gap. The gap is one code em, suRoundPx(baseSizePx·codeScale) (layout.cc:125).

Everything else is confirmed: extractSidecars in api/ (doc.h:88-150), role string compare (emit.cc:587-589), sidebarW from cfg.widthPx (emit.cc:591), and the zip (layout.cc:281, 312-359).

As-built also drifts from verbatim-design.md:101-103, which specifies a group{role:"sidecar"} child per line seq. The code makes one trailing group{role:"sidecar-lines"} on the codeblock (doc.h:101-110, 145).

*Verifier notes:* Lowering the three-box model to a grid is the right closure, but 'move the marker split to the fence/executor level' does not work as stated.
- The sidecar text is parsed by the ENGINE's parseInlineFragment (doc.h:131). The JS-side `m.parse` is explicitly still deferred (verbatim-design.md:3-6, 92-96).
- Tokenization must stay WHOLE-BODY. NEED_TOKENS is issued on the joined code body (doc.h:152-169), and multi-line tokens such as block comments and strings span rows.

Keep the split engine-side but move it out of api/ into a model/resolve transform that emits trow/tcell structure. Tokenize the joined body, then fold tokens back into per-row code cells.

### `break-layout-pages/comment-role-by-color` — Comment identity for wrapping is recovered from the CSS color string 'var(--tsr-tok-comment)'

- kind: adhoc · severity: low · verdict: accurate · plan: **P2-08**
- locations: `engine/src/emit/emit.cc:618-624`; `engine/src/code/tokens.cc:44-46`; `engine/src/emit/emit.h:76-80`; `test/fixtures/code/hang.tsm:3`

The token fold encodes the highlight tag as a style color `var(--tsr-tok-<tag>)`, and makes `tag == 3` italic. Emit marks a CodeRun `isComment` only when the run's color equals the interned string "var(--tsr-tok-comment)". The hang fixture has to fake a comment with `style({color: "var(--tsr-tok-comment)"}, …)`. Layout uses isComment for comment-aware hanging.

*Why ad hoc:* A semantic role crosses layers through a presentation value. Re-theming, or a user-styled comment with a different colour, silently changes layout behaviour, and the magic index 3 couples the fold to the order of the tag table.

*Proposed generalization (survey):* Carry token roles as a first-class run attribute: `CodeRun{text, style, u16 roleTag}`, or more generally a non-presentational `role` id on Styling. The token fold sets roleTag, and user styling can set the same through `style({role: 'comment'})`. Layout tests `roleTag == Tag::Comment`, and CSS colours derive from the role in render.

*Verifier:* Confirmed:
- the color string compare (emit.cc:618-622);
- tag==3 italic and the var(--tsr-tok-<tag>) color (code/tokens.cc:43-46);
- the fixture fakes a comment through style({color: "var(--tsr-tok-comment)"}) (test/fixtures/code/hang.tsm:3).

*Verifier notes:* Carry roles as a run or node attribute, as in the report's first option. Do NOT put them on Styling.
- MetricStore keys on (str, StyleId), and styles are interned by value. A role field would split otherwise-identical styles and duplicate measurement requests for prose.
- Adding 'role' as a style-patch key changes the op args, which means an OPS_VERSION bump.

Token-fold runs already get distinct styles per tag, so code pays no extra cost under the node-attribute option.

### `break-layout-pages/paginator-in-serializer` — Pagination is a band cutter inside the HTML serializer that re-derives keep rules and atomicity from node kinds

- kind: adhoc · severity: high · verdict: accurate · plan: **P3-12**
- locations: `engine/src/render/typeset_html.cc:642-765`; `engine/src/render/typeset_html.cc:671-674`; `engine/src/render/typeset_html.cc:681-694`; `engine/src/render/typeset_html.cc:702-709`; `engine/src/render/typeset_html.cc:723-731`; `engine/src/layout/layout.cc:93-95`; `docs/pages-design.md:37-56`; `docs/notes-design.md:53-61`

renderPages groups lines into Bands using node kinds:
- FlowUnit::K Table and float Image are whole-unit atoms; Code groups by codeLine/cellIdx.
- `Kind::heading` on the TopBlock's node sets stickAfter, so only top-level headings keep with what follows.
- A block Image sets stickAfter even when it has no caption.

Widow/orphan uses a hard-coded 2 and applies to Text units only. As a result:
- 3-line paragraphs become atomic (pages-design §2 says 1–2);
- code logical lines get no protection: the paged-doc golden puts the code block's closing `}` alone at the top of sheet 3.

Rules need a `special==1 ? height/2` correction because Rule LineBoxes store y at their midline (layout.cc:93-95), while table rules have height 0.

What it does not produce:
- a page model (it emits HTML only);
- footnote inserts (notes-design §1 plans them as an extension of renderPages itself);
- per-page float placement;
- running heads or page numbers;
- page-number refs (open in design-decisions-v2 §17).

Documented in pages-design §2 as 'a post-pass … no re-break, no new layout mode'.

*Why ad hoc:* The serializer layer knows semantic roles and duplicates layout knowledge. Each new keep rule or insert class becomes another branch in an HTML writer, and users cannot mark their own blocks keep-together or keep-with-next.

*Proposed generalization (survey):* Add a new stage `paginate/` between layout and render, consuming a document VList:
`VItem = Box{h,d,payload} | Glue{n,st,sh,discardable} | Penalty{p} | Insert{class, VList} | FloatRef{id, placement} | Mark{class, text}`.

Layouters emit the penalties:
- the paragraph layouter puts clubPenalty, widowPenalty and brokenPenalty between lines, from config;
- the `keepWithNext` trait (headings by default) emits +INF after;
- figure image→caption gets +INF;
- table rows get 0, with header Marks;
- code logical lines get INF inside and 0 between, with an optional widow penalty.

The page builder:
- runs TeX's page builder, or a small DP over feasible page breaks for global optimality;
- cost = vertical badness + penalty + insert cost;
- inserts (footnotes) reduce the page goal;
- deferred floats go to the top of the next page;
- Marks give first/last per page for running heads.

Output: `PageResult{pages:[{boxes in page coords, inserts, marks, number}]}`, which renderPages serializes with no knowledge of kinds.

Page-number refs: a bounded fixed point — resolve → layout → paginate → re-resolve changed refs → re-break affected paragraphs — stopping when stable or after N=3 passes.

*Verifier:* Confirmed:
- kind tests (typeset_html.cc:681-694);
- the heading test only on the TopBlock node, last band (704-705);
- block image stickAfter (702-703);
- hard-coded 2 makes 3-line paragraphs atomic (727-729);
- the rule midline correction (671-674).
The paged-doc golden puts the code '}' alone at the top of sheet 3 (test/golden/pages/paged-doc.paged.txt:21). That is allowed by pages-design.md:49-50, but it is a widowed logical line.

*Verifier notes:* Documented as a post-pass (pages-design §2). The VList page builder is sound, and the screen path skipping paginate preserves progressive swap.

The page-reference part conflicts with v2 §11.1. design-decisions-v2.md:235 says 'No LaTeX two-run, no Typst fixpoint … one bounded extra layout iteration at export time', and §17 repeats it. A resolve↔paginate loop of up to N=3 passes is a fixpoint, may not converge, and leaves the cap behaviour undefined.

Fix that keeps the resolver single-pass: page refs render into fixed-width slots (widest digit string × the max digits of the page count). One post-pagination fill pass is then exact and needs no re-break.

### `break-layout-pages/group-role-dispatch` — Groups get geometry only through hard-coded role strings; user regions have no box model in typeset mode

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-18, P3-14**
- locations: `engine/src/emit/emit.cc:776-820`; `engine/src/emit/emit.cc:471-476`; `engine/src/emit/emit.cc:507-544`; `runtime/src/worker/executor.mjs:123-139`; `test/golden/region/figure.semantic.txt:6`; `test/golden/region/figure.layout.txt`

Emit's block walk special-cases two role strings:
- `role == "figure"` triggers the float form, and `figDepth` turns every paragraph at any depth inside the figure into a centered, unhyphenated caption.
- `sidecar-lines` is matched by string comparison.

list and quote are separate kinds with indent-only geometry (listIndentEm, quoteIndentEm), and the markers '•' and 'n.' are hard-coded at emit.cc:517.

Every other region (`#!aside`, user roles) becomes `group{role}` and is flattened with no geometric effect. Semantic HTML keeps `<div data-role="aside">`, which CSS can style, but the typeset upgrade drops it: no frame, padding, background or indent. In the region/figure layout golden, the aside's paragraph sits at left=0.

Region args only carry style patches (font, lang, color, sizePx).

*Why ad hoc:* Container geometry is decided by name in emit instead of through a box-model trait that built-ins and user regions share. A user region cannot get what quote, list or figure get, and the semantic and typeset views disagree about regions.

*Proposed generalization (survey):* Introduce a `BoxModel` trait on group nodes:
`{margin{t,b}, padding{t,r,b,l}, indent{l,r}, border per edge, background, childAlign, marker{text, gutter}, keepTogether, keepWithNext, float, captionOf}`.

Where it comes from:
- a role→trait table in Config, with defaults for figure, quote, list item, aside and notes;
- overrides through region args, e.g. `#!aside(box: {padding: '0.5em', rule: 'left'})`;
- the same API in executor handlers, so user and built-in roles are on equal footing.

What the container layouter does with it:
- shrinks the measure (padding/indent);
- emits a Frame Box (background/border) spanning the children's extent;
- turns keep traits into VList penalties.

figDepth caption handling becomes `captionOf` plus paragraph LineEnds, applied to direct children only. Typeset HTML emits the frame, preserving data-role.

*Verifier:* Confirmed:
- role=="figure" (emit.cc:781-783) and figDepth applying to paragraphs at any depth (emit.cc:471-476, 816-818);
- hard-coded markers (emit.cc:517);
- generic regions become group{role} with only style patches (executor.mjs:130-136);
- the aside paragraph sits at left=0 in typeset (test/golden/region/figure.layout.txt:6) while the semantic HTML keeps data-role (figure.semantic.txt:6).

*Verifier notes:* A BoxModel trait shared by built-ins and user roles is the right closure. It is unaffected by §12, because traits are node args, not style-stack state.

The report omits that region/handler-settable box args change the op args, which needs OPS_VERSION + gen-ops-ts + a full re-record. Frames split across pages also need open-edge rules, which the report does acknowledge.

### `break-layout-pages/measure-definition-split` — Justification uses two definitions of the measure (unfloored widthPx vs su), kept to avoid golden churn

- kind: adhoc · severity: low · verdict: accurate · plan: **P3-08**
- locations: `engine/src/layout/layout.cc:12`; `engine/src/layout/layout.cc:570-572`; `docs/figure-design.md:182-184`

The breaker's line width is suFloorPx(widthPx) − indent. Justification slack for non-narrowed lines instead uses `cfg.widthPx − suToPx(indent)`, unfloored, while narrowed lines use `suToPx(u.narrow)`. figure-design §8 documents this: it 'keeps the historical justification formula … zero golden churn'.

*Why ad hoc:* The line's own width (LineBox.width) should be the single source of truth. The branch on 'narrowed' exists only for golden stability.

*Proposed generalization (survey):* Compute slack = suToPx(slot.width) − naturalPx for every line, from the ParShape. If sub-su container precision matters (document-model §6.1 floors container widths), carry `rawPx` in each LineSlot and use it everywhere.

*Verifier:* Confirmed: layout.cc:570-572 vs the breaker's suFloorPx(widthPx) − indent (doc.h:344), documented in figure-design.md:182-184.

*Verifier notes:* Documented, but the only rationale is avoiding golden churn, and the fix costs one re-record. Using the slot width everywhere only reduces slack (floor ≤ raw), which is the safe direction under the §7 epsilon rule.

### `break-layout-pages/kp-window-heuristics` — Breaker search is a PoC sliding window with magic constants and a retry ladder, not the Knuth–Plass active list

- kind: adhoc · severity: low · verdict: accurate · plan: **P1-14**
- locations: `engine/src/break/break.h:23-24`; `engine/src/break/break.cc:46-55`; `engine/src/break/break.cc:88-96`; `engine/src/break/break.cc:176-194`

The search is limited in two ways:
- Candidate predecessors j for break i are restricted to ±5 candidate indices around the previous best parent (`cursorSearchRange=5`).
- dp entries are pruned to line counts within ±1 of the best.

Infeasibility, and only infeasibility, triggers a retry ladder {10, 20, 50, ∞} with the threshold `cost ≥ 1e17`. The cache is wiped at 16384 entries.

Because of the BREAK_INF precision bug (other_issues), the window counts unbreakable blocks as candidates too.

A Python re-implementation reproduced tsrc's costs exactly on 13 fixture paragraphs at several widths and found the window exact there. The single divergence (doc/url-break at 220px: 2695678909926008 vs exact 2695678909921306) disappears once INF blocks are excluded.

*Why ad hoc:* Tuning constants with no stated model, and an approximation level that is not a parameter. A tolerance-based active list is exact for the cost model and usually as cheap.

*Proposed generalization (survey):* Implement Knuth–Plass proper: deactivate an active node once the line from it is overfull beyond its shrink (feasibility pruning), with optional fitness classes. Replace the window constants with `BreakParams{tolerance, emergencyStretch, looseness, maxActive}`. If a bounded-cost mode is needed for very long CJK paragraphs, express it as an explicit `maxActive`. Keep the content-hash cache.

*Verifier:* Code claims confirmed: window ±5 over bkIdx (break.cc:46-55), ±1 line-count pruning (88-94), ladder {10,20,50,∞} and 1e17 threshold (183-189), clear at 16384 (191).

The Python-reproduction numbers were not independently verified. tsrc --stage=breaks --width=220 on doc/url-break prints cost=2695678909926012, while the report quotes …6008 for 'the port'.

*Verifier notes:* A tolerance-based active list with explicit BreakParams is the right closure. The per-line-count state must be kept while the ParShape varies (TeX keeps line number in active nodes up to the last parshape line).

The magnitude-based sentinels behind the ladder are part of the same problem; see the missed item unbounded-badness-sentinels.

### `break-layout-pages/overfull-collapses-paragraph` — When no feasible break exists, the whole paragraph is set as ONE line with huge negative word-spacing

- kind: issue · severity: high · verdict: partly · plan: **P0-12**
- locations: `engine/src/break/break.cc:103-122`; `engine/src/break/break.cc:183-190`; `engine/src/layout/layout.cc:576-586`; `docs/pages-design.md:52`

breakLines returns `{{n}, INF}` (break.cc:122) when no path reaches the end. Layout then justifies that single line by negative slack over all its glue (layout.cc:577-581).

Reproduced:
- `tsrc --stage=layout --width=180 --ops=test/fixtures/doc/url-break.ops test/fixtures/doc/url-break.tsm` gives one line with `dw=-5011su` (≈ −78px per space) for a 206-char paragraph.
- doc/english at 40px gives dw=−3145su.
- At 100–140px, structure.tsm, refs.tsm, notes, cite and kern-boundary also report cost=1e18.

Realistic triggers are any unbreakable run wider than the line:
- inline code spans, which are a single block (emit.cc:56-69);
- URLs split by markup;
- digit strings;
- narrow measures such as mobile, float-narrowed lines (no minimum column width) and table cells.

This breaks the design-decisions-v2 §7 contract ('error must only ever appear as ≤1px right-edge deviation, never as a structural re-break'). pages-design.md:52,123 cites a 'KP hard-cut fallback' that does not exist.

Fix, as TeX does:
- a second pass with emergency stretch;
- then a final pass that keeps the least-bad overfull candidate when no active node remains, so only the offending line overflows;
- report an `overfull-line` diagnostic.

*Verifier:* Reproduced:
- doc/url-break@180: one line, dw=-5011su;
- doc/english@40: dw=-3145su;
- cost=1e18 for structure, refs, notes/explicit and cite/basic at 100-120px, and for style/kern-boundary at 100px.

pages-design.md:52 and :123 do cite a 'KP hard-cut fallback' that break.cc does not have (break.cc:122 returns {{n}, INF}).

The §7 attribution is overstated. The v2 §7 contract (design-decisions-v2.md:179) concerns divergence between the measurement paths, and an unbreakable run wider than the measure is content, not measurement error. What is genuinely violated is §7 rule 5's containment principle ('nowrap confines error to the line', line 187): one bad run turns into a whole-paragraph overprint.

*Verifier notes:* The TeX final pass, emergency stretch and diagnostic are the right fix, but they need the sentinel arithmetic reworked. Today dpmin starts at INF=1e18 (break.cc:59), so any path whose running cost reaches 1e18 never enters dp[i] (break.cc:79-96) and cannot be represented as an overfull candidate. Sequence this fix together with break-inf and trailing-glue (see those verdicts).

### `break-layout-pages/emit-reads-measure-stale-on-relayout` — Emit sizes images and sidecars from cfg.widthPx, but relayout and paginate never re-emit

- kind: issue · severity: high · verdict: accurate · plan: **P1-16**
- locations: `engine/src/emit/emit.cc:591`; `engine/src/emit/emit.cc:721-739`; `engine/src/api/doc.h:247`; `engine/src/api/doc.h:392`; `runtime/src/worker/worker.mjs:234`; `runtime/src/worker/worker.mjs:254`

Emit depends on the measure:
- image display width (scale × measure, clamped to the measure) and the placeholder (measure × measure/3) at emit.cc:721-739;
- the sidecar width (sidebarFrac × measure) at emit.cc:591.

Doc::setWidth (doc.h:392) only clears `laidOut`, and typeset() re-emits only when `!emitted` (doc.h:247). The worker's relayout and paginate call only `_tsr_set_width` (worker.mjs:234, 239, 254).

Consequences:
- After a relayout to a narrower width, images keep their old display width and overflow the measure. A float can then exceed the measure and trigger wide-float-overprints-text.
- print pagination at 666px uses screen-width image and sidecar geometry.

This breaks the architecture.md:98 premise that relayout(width) is 're-break only' over width-independent stage products.

Fix: emit records intent (scale, intrinsic dims, sidebarFrac), and layout resolves sizes against the actual container. This also enables container-relative sizing inside cells and floats.

*Verifier:* Confirmed:
- image sizing reads cfg.widthPx (emit.cc:721-739) and so does sidebarW (emit.cc:591);
- setWidth only clears laidOut (doc.h:392), and typeset re-emits only when !emitted (doc.h:247);
- worker relayout and paginate call only _tsr_set_width (worker.mjs:234, 239, 254);
- architecture.md:98 says relayout = 're-break only'.

Note that v2 §9 (design-decisions-v2.md:211) says 'a resize is a full re-typeset'. The docs disagree with each other, but the shipped relayout path is the re-break-only one, so the bug is real either way.

*Verifier notes:* Emit should record intent (scale, intrinsic dims, sidebarFrac) and leave the measure to layout. This restores width-independence of stage products and enables container-relative sizing in cells and floats. Also note that the placeholder box (measure × measure/3, emit.cc:734-735) goes stale.

### `break-layout-pages/wide-float-overprints-text` — A float wider than measure − 1em − 1px neither narrows nor clears the following text, and very narrow wrap columns are accepted

- kind: issue · severity: medium · verdict: partly · plan: **P3-08**
- locations: `engine/src/api/doc.h:316-321`; `engine/src/api/doc.h:344-351`; `engine/src/emit/emit.cc:726-731`

Narrowing applies only when `flOccl < lw.constant − 64` (doc.h:345). Otherwise flRemain stays positive, yet Text units are neither narrowed nor cleared, because clearing only happens for non-Text units (doc.h:316-321). Text is therefore laid out over the float box.

Trigger: a float image without `scale` whose intrinsic width is at least the measure. Emit clamps imgW to the measure (emit.cc:728-729), so occl = measure + 1em. This is common for photos on mobile, and inevitable after a narrowing relayout (see emit-reads-measure-stale-on-relayout).

The opposite end is also unguarded: any column wider than 1px is accepted. figure/float.tsm at --width=70 produces 22 lines of 1536su (24px), one CJK char per line. For Latin text, such slivers lead straight to overfull-collapses-paragraph.

Fix: clear when the available width falls below `minWrapWidth` (for example 8em), and never leave occlusion pending without either narrowing or clearing.

*Verifier:* The mechanism is confirmed. When flOccl ≥ lw.constant−64 there is no narrowing (doc.h:345), Text units are never cleared (doc.h:316 clears only non-Text), and emit clamps imgW to the measure (emit.cc:728-729).

The '22 lines of 1536su' example is wrong. At --width=70 (tsrc base 18px), para pid=2 has 22 lines, but only L0-L1 are 1536su wide (one CJK char each); L2-L21 are the full 4480su. narrowK = ceil((1344 img + 1728 caption + 1382 gap − 1382 gapBefore)/1728) = 2.

The point that a 24px column is accepted with no minimum width still stands.

*Verifier notes:* Adding minWrapWidth clearing and the 'never leave occlusion pending without narrowing or clearing' invariant is right. Under the proposed ExclusionMap this becomes the general 'narrowable with minimum width, else clear' rule.

### `break-layout-pages/break-inf-float-vs-double` — BREAK_INF (float 1e18f ≈ 9.99999984e17) is below the DP's INF (double 1e18), so unbreakable blocks become break candidates

- kind: issue · severity: medium · verdict: accurate · plan: **P0-12**
- locations: `engine/src/emit/emit.h:53`; `engine/src/break/break.cc:9`; `engine/src/break/break.cc:48`; `engine/src/break/break.cc:107`

`penalty(i) < INF` (break.cc:48) is true for BREAK_INF blocks. Every word piece, CJK char before a closing punct, punct glyph and similar block therefore enters bkIdx, and the endpoint scan (break.cc:107) does not skip them.

Effects:
- The ±5 candidate window is measured over all blocks rather than over real breakpoints.
- In infeasible cases, breaks at forbidden positions (mid-word without a hyphen, before a closing punct) are reachable at cost ≈1e18. Results ≥1e17 are accepted after the retry ladder, so such breaks can be emitted.
- Results already differ measurably: on doc/url-break at 220px the port gives 2695678909926008, while excluding INF candidates gives 2695678909921306 (Python reproduction of break.cc).

Fix: one shared constant with an explicit `forbidden` test (`pen >= BREAK_INF` in float), or a dedicated bit.

*Verifier:* Confirmed: (double)1e18f = 9.99999984e17 < INF (break.cc:9, 48, 107; emit.h:53).

Precision on reachability: a break at a BREAK_INF block survives only if the rest of the path costs < ~1.57e10 (1e18 − 9.99999984e17). Entries with dpNext ≥ 1e18 never set dpminLine (break.cc:59, 79-88) and are dropped.

Concrete instances found:
- doc/structure@160: heading 'Typesetting Notes', cost=999999988237035904, breakpoints=[1,3]. That is a break AFTER the INF word 'Typesetting', not at the space.
- notes/explicit@60: pid2 unit1, cost=999999997703233792.

*Verifier notes:* The shared-constant / explicit-forbidden-bit fix is right but must NOT land alone. In both instances above, the INF break is what currently rescues the paragraph: breaking at the space is infeasible only because the DP counts the trailing space (see trailing-glue-in-break-cost). Fixing BREAK_INF first would turn these headings and notes into single overfull, negatively spaced lines. Land it together with glue discard at breaks and the overfull final pass, which is what the report's migration step 1 bundles.

### `break-layout-pages/trailing-glue-in-break-cost` — The DP counts the glue at the break (trailing space, closing-punct half) that layout later trims, so cost and rendered slack disagree

- kind: issue · severity: medium · verdict: accurate · plan: **P0-12**
- locations: `engine/src/break/break.cc:67-70`; `engine/src/layout/layout.cc:497-522`; `docs/document-model.md:174`; `docs/design-decisions-v2.md:196`

contentW = widthPsum[i+1] − widthPsum[j+1] includes block i, the block the line breaks after, and so does totalSpace (break.cc:67-70). Layout then trims trailing and leading BF_SPACE blocks (layout.cc:498-499).

Example: in figure/float, pid0 L0 has breakpoint 21 but is rendered as blocks [0,20). The 512su punct half is in the breaker's width but not on the page, so the breaker's slack is 243su against a rendered slack of 755su.

Leading BF_PUNCT_OPEN halves at line start are similarly counted. App C's 'line-start/-end compression falls out of the half-spaces vanishing at line edges' (design-decisions-v2 App C) therefore holds only in layout, not in the optimizer.

Separately, the breaker normalizes stretch by Σ spaceWidth (px) while layout distributes by Σ stretchWeight. That contradicts the 'agree by construction' claim in document-model.md:174 and design-decisions-v2.md:196.

*Verifier:* Confirmed: figure/float pid0 breakpoints=[21,36] (test/golden/figure/float.breaks.txt:1), and block 20 is a punct-sp of 512su that layout trims (float.layout.txt:3 renders [0,20)).

Stronger evidence: in doc/structure@160 the break at the space is infeasible only because of the counted trailing space. 'Typesetting'+space = 10139+462 = 10601su > 10240su gives x = −361/462 = −0.78 < −0.37 → INF (break.cc:16). The DP therefore breaks at the INF word instead.

The leading-open-half nuance is under glue-semantics-split.

*Verifier notes:* TeX discard semantics remove this whole class. App C's 'line-edge compression falls out of the half-spaces vanishing at line edges' (design-decisions-v2.md:378) then also holds in the optimizer.

### `break-layout-pages/snap-kerning-ignores-sidecar` — With snap-kerning on, the code column budget uses the full measure and ignores the sidecar partition

- kind: issue · severity: medium · verdict: accurate · plan: **P3-11**
- locations: `engine/src/layout/layout.cc:122-129`; `engine/src/layout/layout.cc:144`; `engine/src/layout/layout.cc:162`

layout.cc:144 computes `cols` from `lineWidthCode`, which is the measure minus the sidecar and gap. The snap-kerning branch then recomputes `cols = (i32)(lineWidth / atomSu)` (layout.cc:162) from the full `lineWidth`. When verbatimSnapKerning is on and a sidecar is present, code rows wrap at the full measure and overprint the sidecar column. `lineWidthFull` is dead code (layout.cc:122, 129).

*Verifier:* Confirmed: cols comes from lineWidthCode (layout.cc:144), but the snap branch recomputes it from the full lineWidth (layout.cc:162). lineWidthFull is dead (122, 129).

*Verifier notes:* A plain bug. The one-line fix is lineWidthCode / atomSu. It also gets subsumed by the grid lowering.

Related: snap-kerning is effectively inert in browsers anyway (see the missed item snap-style-attr-duplicated), and no golden or e2e covers it.

### `break-layout-pages/float-adds-paragraph-gap` — An 'out-of-flow' float still costs one paraGap in the flow

- kind: issue · severity: low · verdict: accurate · plan: **P3-08**
- locations: `engine/src/layout/layout.cc:604-606`; `engine/src/api/doc.h:282-283`; `test/golden/figure/float.layout.txt`; `test/golden/figure/stack.layout.txt`

A float figure is its own TopBlock with h=0, but layout adds `paraGap` after every frame (layout.cc:606), and the tracker mirrors this (doc.h:282-283).

Golden evidence:
- figure/float.layout: pid0 ends at 3072su and pid2 starts at 5530su, a gap of 2×1229su.
- figure/stack.layout: two floats in a row give 3 gaps (3072 → 6759su) before the heading.

This contradicts figure-design.md:124 ('the float image unit contributes zero advance (out of flow)'). The fix is to skip the inter-frame gap after zero-height out-of-flow frames, or to model the gap as collapsible VGlue.

*Verifier:* Confirmed in goldens:
- float.layout: pid0 h=3072, pid1 (float frame, h=0) y=4301, pid2 y=5530, i.e. 2×1229su;
- stack.layout: pid3 at 6759 after two zero-height float frames.

Nuance: figure-design.md:171-173 says the tracker 'mirrors layout's … inter-block paraGap', so the gap is mirrored knowingly. It still contradicts the intent of §4's 'zero advance (out of flow)' (figure-design.md:124). The tracker additionally charges one paraGap of clearance (doc.h:310).

*Verifier notes:* Collapsible VGlue, or skipping the frame gap after out-of-flow-only frames, is the right fix.

### `break-layout-pages/float-indent-geometry` — Occlusion is a width delta relative to each unit's own indent, so floats and wrapped text at different indents misalign

- kind: issue · severity: low · verdict: accurate · plan: **P3-08**
- locations: `engine/src/layout/layout.cc:37-38`; `engine/src/layout/layout.cc:558-563`

The float box sits at its own unit's indent (layout.cc:37-38). A narrowed line instead starts at `unit.indent + (lineWidth − narrow)` = unit.indent + occl (layout.cc:562).

If a left float sits inside a list or quote (indent > 0) and is followed by unindented text, the float spans [indent, indent+imgW] while the text starts at imgW + 1em. They overlap whenever the indent exceeds 1em; list indent is 1.5em.

List markers (CSS right:100%) on narrowed lines beside a left float also land in the 1em gap or over the image.

This follows from code reading; no fixture covers it. An absolute-coordinate ExclusionMap removes the whole class of problem.

*Verifier:* Confirmed by code reading:
- box at the float unit's own indent (layout.cc:37-38);
- narrowed text at text.indent + occl (doc.h:344-346, layout.cc:562);
- list indent 1.5em (config.h:64), so a left float inside a list overlaps following unindented text by 0.5em;
- markers are drawn at right:100% (shell.mjs TSR_CSS .tsr-marker), into the 1em gap.
No fixture covers it.

*Verifier notes:* Absolute-coordinate exclusions remove the problem class.

### `break-layout-pages/paged-atoms-clipped` — Atomic bands taller than a sheet are clipped in print, losing content

- kind: issue · severity: medium · verdict: accurate · plan: **P3-12**
- locations: `engine/src/render/typeset_html.cc:681-684`; `engine/src/render/typeset_html.cc:735-741`; `engine/src/render/typeset_html.cc:749`; `docs/pages-design.md:121-123`

Tables (whole), float boxes with captions, and code logical lines are atomic. When one is taller than the sheet, the cutter falls back to the greedy position (typeset_html.cc:737), and the `.tsr-sheet` has `overflow:hidden` (typeset_html.cc:749). A long table or a tall float therefore loses rows or caption lines in print, silently.

This is documented as accepted in pages-design §5. It is still a correctness problem that a row-splittable VList would remove.

*Verifier:* Confirmed: oversized-atom fallback (typeset_html.cc:737), overflow:hidden sheets (749). Documented as accepted in pages-design.md:121-123.

*Verifier notes:* Documented trade-off ('blog tables are small'). A row-splittable VList, with header Marks, removes it at no cost to the screen path.

### `break-layout-pages/break-cache-robustness` — The global break cache trusts an unverified 64-bit hash of XOR-packed fields and wipes itself wholesale at 16384 entries

- kind: issue · severity: low · verdict: accurate · plan: **P0-11, P1-14**
- locations: `engine/src/break/break.cc:149-174`; `engine/src/break/break.cc:176-194`

The cache is a function-static `unordered_map<u64, BreakResult>` (break.cc:178), shared process-wide.

Key construction: block fields are combined as `(width<<21) ^ (spaceWidth<<42) ^ breakWidth` (break.cc:168). Negative widths (junction kerns, emit.cc:953) sign-extend into the high bits.

Hits are not verified. A collision returns another paragraph's breakpoints, and layout indexes `bl[hi-1]` with no bounds check, which is UB.

Eviction is `cache.clear()` at 16384 entries (break.cc:191). Any document with more than 16384 broken streams (cells and captions included) thrashes on every pass, which hurts the whole-book editing case in real-world-report.

Fix: store n plus a second hash and validate `breakpoints.back() == blocks.size()`; replace the wipe with LRU.

*Verifier:* Confirmed:
- function-static map (break.cc:178);
- XOR packing (168);
- unverified hits (180-181);
- wholesale clear (191);
- unchecked bl[hi-1] in layout (layout.cc:540).

Collision probability is negligible in practice: with 64-bit FNV and ≤16384 entries, p ≈ 1e-11. The packing loses information only for negative values or widths ≥ 2^21 su, so the low severity is right.

*Verifier notes:* Validation on hit plus LRU is right. The cache affects only speed, not results (absent collisions), so the eviction policy does not touch determinism.

### `break-layout-pages/api-hosts-layout-policy` — api/doc.h hosts layout policy and a content transform, contrary to the module map

- kind: issue · severity: medium · verdict: accurate · plan: **P1-03, P1-15**
- locations: `engine/src/api/doc.h:84-150`; `engine/src/api/doc.h:263-361`; `docs/architecture.md:44`; `docs/architecture.md:48-80`

architecture.md §2.1–2.2 describes api/ as the boundary seam and assigns 'Breaks + vertical metrics → Frames' to layout/. Doc::typeset (doc.h:263-361) nevertheless contains:
- the float tracker;
- the table column-width policy;
- the sidecar width breaking.

It also mutates FlowUnit. Doc::extractSidecars (doc.h:84-150) is a content-tree rewrite in the API handle.

Together these mean layout behaviour is split across two modules that must stay in lockstep.

*Verifier:* Confirmed: doc.h:88-150 and 263-361.

architecture.md:44 is even stronger than the report states: 'the WASM glue stays a thin adapter that cannot accumulate logic'.

*Verifier notes:* Folding the tracker and policies into layout/ (float-tracker-replay) and moving extractSidecars into a model transform resolves it.

### `break-layout-pages/baseline-not-communicated` — LayoutResult has line heights but no baselines, and text lines are emitted without height or line-height

- kind: issue · severity: low · verdict: accurate · plan: **P1-18**
- locations: `engine/src/layout/layout.h:7-32`; `engine/src/render/typeset_html.cc:326-358`; `engine/src/layout/layout.cc:13`; `docs/document-model.md:227`

document-model.md:227 says 'Baseline of line i sits at top_i + ascent_i', and advance = max(baseLeading, asc+desc).

In practice, renderLineBox emits only top/left/width/word-spacing for text lines (typeset_html.cc:326-358). The baseline position inside each absolutely positioned line therefore depends on the host page's CSS line-height, not on the engine's vertical model. Lines with tall inline formulas get two independently computed heights: the engine's advance and the browser's line box.

Also, document-model §8 says baseLeading comes 'from the paragraph style', but the code uses the global cfg (layout.cc:13).

Fix: carry a `baseline` field in the box and pin it in render, as display math already does.

*Verifier:* Confirmed:
- text lines get no height or line-height (typeset_html.cc:326-357); only code rows set line-height (344-353);
- TSR_CSS sets no line-height on .tsr-line (shell.mjs:23);
- layout uses the global cfg baseLeading (layout.cc:13) although document-model.md:227 says 'from the paragraph style'.

A visible consequence the report misses: headings at 1.6× get advance = asc+desc = 1639su (table.layout.txt:3), i.e. zero leading between wrapped heading lines.

*Verifier notes:* Pinning line-height (or a baseline offset) per line from LayoutResult is the code-row precedent generalized. Expect e2e audit churn.

### `break-layout-pages/doc-drift` — Several layout and pagination docs disagree with the code

- kind: issue · severity: low · verdict: partly · plan: **P1-15, P3-12**
- locations: `engine/src/layout/layout.h:20`; `engine/src/code/grid.h:24-34`; `docs/verbatim-design.md:26`; `docs/pages-design.md:45`; `docs/code-design.md:84-89`; `docs/document-model.md:60`

- layout.h:20 lists special values 0–4 (5 = image is missing).
- verbatim-design.md:26 says 'take the FIRST convergent satisfying BOTH', but grid.h:24-34 picks the best q≤7 mediant.
- pages-design.md:45 says '1-2-line paragraphs are atomic', but the code makes paragraphs of up to 3 lines atomic (typeset_html.cc:727-729). pages-design.md:49 says 'table rows (rule-to-rule)' (superseded by §5).
- code-design.md:84-89 says 'token boundary' preference, but the implementation uses a fixed char set.
- document-model §8 lists an `estimated` field that ParaFrame lacks.
- Kind::hardbreak is reserved in ops (document-model.md:60) but would currently be dropped by emit.

*Verifier:* Accurate:
- layout.h:20 (no 5);
- grid.h:24-28 picks the best q≤7 mediant vs 'FIRST convergent' (verbatim-design.md:25-26, and grid.h:2's own header comment);
- pages-design.md:45 ('1-2-line') vs 3-line atomicity;
- pages-design.md:49 'table rows';
- the missing `estimated` field (document-model.md:213).

Not drift:
- code-design.md:85-86 defines 'token boundary (space/punct)', which matches the fixed char set;
- hardbreak is 'syntax reserved, not yet granted' (document-model.md:60), so dropping it is consistent.

Additional drift:
- verbatim-design.md:101-103 (per-line group{role:"sidecar"}) vs the as-built single group{role:"sidecar-lines"} (doc.h:109);
- figure-design.md:115-117 'under-clears' is the wrong direction (see float-tracker-replay);
- emit.h:67 'list-item start' vs emit.cc:535-537.

### `break-layout-pages/missed:0` — Paged renderer anchor bookkeeping drops or duplicates label ids

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-18, P3-12**
- locations: `engine/src/render/typeset_html.cc:752-759`; `engine/src/render/typeset_html.cc:211-216`; `engine/src/render/typeset_html.cc:265-270`; `engine/src/render/typeset_html.cc:310-318`; `engine/src/render/typeset_html.cc:612`

renderLineBox decides whether to print id="tsr-<label>" with a stateful check, `au.anchor && l.unitIdx != lastAnchored`. unitIdx is PARAGRAPH-LOCAL.
- renderTypeset resets lastAnchored per paragraph (612).
- renderPages resets it per SHEET (752).

Failure (a): on one sheet, an anchored unit 0 sets lastAnchored=0, for example a labelled heading. Any later paragraph's anchored unit 0 on the same sheet then loses its id, for example a labelled display equation or figure image. Unanchored paragraphs in between do not reset the state. The @ref link target vanishes from the print/PDF, and Chrome's PDF keeps internal #links, so they break.

Failure (b): a unit that continues onto the next sheet re-emits its id, which gives duplicate ids.

The paged-doc golden does not happen to trigger either case.

*Proposed generalization (survey):* Make anchors layout data instead of serializer state. Layout sets `anchorId` on exactly one Box per anchored unit, the first one materialized. Every serializer (flowing, paged, future page-builder output) prints it statelessly, and pagination never copies it onto continuation boxes. The lastAnchored parameter threaded through renderLineBox disappears.

### `break-layout-pages/missed:1` — Snap-kerning code spans emit two style attributes, so snap-kerning is inert in browsers

- kind: missed · severity: medium · verdict: verifier-found · plan: **P0-10**
- locations: `engine/src/render/typeset_html.cc:412-422`; `engine/src/render/typeset_html.cc:80-90`; `engine/src/render/typeset_html.cc:458-468`; `test/golden/pages/paged-doc.paged.txt:15`

The code-row path writes runStyleAttr(), which already emits style="font-size:13.6px" for every code run because codeScale is 0.85 (see the paged-doc golden). It then appends ` data-snap="1" style="letter-spacing:…"`.

The HTML tokenizer drops duplicate attributes, keeping the first. The letter-spacing that snap-kerning depends on is therefore discarded, while layout has already budgeted columns in atom units (layout.cc:159-163).

The text path does this correctly: openRun merges extraStyle into one style attribute (458-468). The code path hand-builds the span instead.

No golden or e2e enables verbatimSnapKerning: grep finds no 'snap' under test/, and tsrc has no flag for it. The bug and snap-kerning-ignores-sidecar therefore went unnoticed.

*Proposed generalization (survey):* Use one run-emission primitive for every line kind (text, cells, captions, code rows): a style property map that is merged and serialized once. Realization data such as letter-spacing, margins and pinned widths should be structured properties derived from the item's Realize tag (see glue-semantics-split), never attribute strings appended at the call site. Add a golden with snap-kerning on (a tsrc --snap flag).

### `break-layout-pages/missed:2` — Copy 'join' is derived from alignment (isLast || ragged), so wrapped headings and block captions copy with newlines

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-17, P3-07**
- locations: `engine/src/layout/layout.cc:569`; `engine/src/layout/layout.cc:587`; `engine/src/layout/layout.cc:66-78`; `runtime/src/main/copy.mjs:31-36`; `test/golden/region/figure.layout.txt:4-5`

`isLast = (bp == bl.size()) || u.ragged` serves both justification and the copy contract: `line.join = isLast ? 0 : …`.
- Every line of a ragged unit gets join=0: headings, block-figure captions, display-error units.
- copy.mjs treats an absent data-join as a real line boundary and inserts '\n'.
- Result: a two-line block caption copies as 'A pretend figure body line\ncontinuing…' (region/figure.layout golden: both caption lines join=last). A heading wrapped at 160px copies as 'Typesetting\nNotes'.

Float captions compute join explicitly (layout.cc:66-78, comment '§9.3: wrapped caption rows rejoin on copy'). The same caption text therefore copies differently depending on whether the figure floats.

*Proposed generalization (survey):* Join is a property of the BREAK, not the alignment: consumed real glue → space, Disc/none → none, paragraph end → newline. Compute it once in the shared materializeLines (nested-stream-copies) from the item at the break. Alignment comes only from LineEnds (alignment-flags). The float-caption join loop and the text join loop (544-550, 587) collapse into one.

### `break-layout-pages/missed:3` — Unbounded badness and magnitude sentinels: glue-less lines get astronomical costs that collide with INF, BREAK_INF and the 1e17 retry threshold

- kind: missed · severity: medium · verdict: verifier-found · plan: **P0-12**
- locations: `engine/src/break/break.cc:11-19`; `engine/src/break/break.cc:59`; `engine/src/break/break.cc:79-96`; `engine/src/break/break.cc:183-190`; `engine/src/emit/emit.h:53`

costFn floors totalSpace at 0.001px, so a line with no stretchable glue (URL pieces, a long inline-code block, a lone word) costs (1000·slack_px)^3.
- Examples: 'Typesetting' 1.58px underfull costs 3.9e9 (structure@160); url-break@220 totals 2.7e15.
- At about 464px of slack a single line exceeds 1e17 and triggers the retry ladder: four extra DP runs, the last with an unbounded window.
- At about 1000px slack the line hits INF and is excluded.
- Running totals ≥ 1e18 are silently pruned because dpmin starts at INF.
- Feasibility, forbiddenness (BREAK_INF ≈ 9.99999984e17) and quality share one double scale. Whether a forbidden break or an overfull collapse wins depends on these magnitudes, not on an explicit rule.
The report treats the window, the ladder and BREAK_INF separately but not this root cause.

*Proposed generalization (survey):* Adopt TeX's bounded model:
- badness b = min(100·|r|^3, 10000), with 10000 ('inf_bad') for underfull lines without stretch;
- explicit feasibility classes: overfull, beyond tolerance, ok;
- demerits (l+b)^2 ± p^2 in a bounded range;
- penalties as a tagged value {Normal(p), Forbidden, Forced}, with the constants defined once and shared by emit and break.
The retry ladder is then driven by 'no feasible active node' (tolerance/emergency-stretch passes), not by a cost threshold. Still deterministic: doubles over su inputs, fixed pass order.

### `break-layout-pages/missed:4` — User blocks cannot be measured: raw has an author-declared fixed height, while built-ins get bespoke pull channels

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-28**
- locations: `engine/src/emit/emit.cc:685-699`; `engine/src/layout/layout.cc:100-111`; `engine/src/render/typeset_html.cc:183-196`; `engine/src/api/doc.h:40-48`; `engine/src/api/doc.h:177-233`; `runtime/src/main/shell.mjs:36`

The only user-extensible block geometry is `raw`.
- Its height is whatever the handler declares, defaulting to one leading (emit.cc:697).
- Layout advances by exactly that height (layout.cc:106-108).
- .tsr-raw is overflow:hidden, so taller content is clipped silently, and width-dependent content (wrapping HTML, embeds) cannot follow a relayout.

Built-in replaced content gets per-feature machinery the public surface cannot reach:
- images have a dedicated NEED_IMAGES pull state and provider ABI (doc.h:40-48, 177-233) plus emit-side sizing;
- math has precompiled metrics;
- code has NEED_TOKENS.

This is the 'per-feature pull states' anti-pattern applied to block geometry. Extensions are not on equal footing with built-ins.

*Proposed generalization (survey):* Add one generic replaced-box measurement request through the existing measurement seam: NEED_BOX {id, kind: image|html|embed, payload (src or html), availableWidthPx} → {w, h, baseline?}. The native mock answers it, so the dual-target rule holds.
- Images become one client of it.
- raw/fence handlers can opt into host-measured height at the actual container width.
- Layout consumes a uniform IntrinsicSize{w, h, aspect, baseline} for any replaced box.
- Relayout re-requests only width-dependent entries.

### `break-layout-pages/missed:5` — DP tie-breaking depends on std::unordered_map iteration order, which differs between native (libstdc++) goldens and WASM (libc++) production

- kind: missed · severity: low · verdict: verifier-found · plan: **P0-12**
- locations: `engine/src/break/break.cc:57-58`; `engine/src/break/break.cc:79-94`; `engine/src/break/break.cc:109-118`

Per candidate break, the DP collects entries in unordered_maps (dps/dpp) and copies them into dp[i] in map iteration order. That order then decides exact ties in two places:
- `if (dpNext < dpmin)` chooses dpminLine, which centres the ±1 line-count pruning and moves cursorB;
- the endpoint scan `if (e.val < bestVal)` chooses bestLine among same-i entries with equal value, which changes the breakpoints.
Iteration order is implementation-defined. The byte-exact goldens run natively, while production runs the libc++ build from Emscripten, so a tie could resolve differently in production than in tests without any golden noticing. Exact ties are rare (for example lines with zero slack), so the risk is small, but it directly touches the su-determinism and byte-exact-golden invariant.

*Proposed generalization (survey):* Replace the maps with a fixed small array or sorted vector indexed by line count (at most three survive), and define a total order for ties: lower cost, then fewer lines, then earlier break. More generally, ban unordered-container iteration in result-affecting code (a lint), and run a subset of goldens against the WASM build in CI.

## render-runtime — Renderers (typeset/semantic HTML), shell, copy, static export

<details><summary>Design summary (as audited)</summary>

Rendering is three C++ serializers in two files plus a JavaScript main-thread runtime. renderTypeset (engine/src/render/typeset_html.cc:604-636) walks LayoutResult.paras in parallel with the emit products (TopBlock, FlowUnit, LinebreakBlock). For each root child it writes one `.tsr-para`: a normal-flow container with position:relative, an explicit height, margin-bottom, data-pid and a paragraph-relative data-s0. Inside it, each LineBox becomes one absolutely positioned, nowrap `.tsr-line`. renderLineBox (:177-602) switches on LineBox.special (0 text, 1 rule, 2 code, 3 raw, 4 display math, 5 image). For a text line it walks the block range again and makes local decisions: it coalesces blocks into style/link runs, emits CJK letter-spacing and compensating margins, punctuation-squeeze classes, glue spans, hyphen glyphs, inline math boxes (the MathBox flattened into positioned glyph spans), anchors, and data-syn/data-join markers for copy. renderPages (:642-765) reuses renderLineBox after a greedy band pagination with kind-based keep rules. semantic_html.cc walks the post-resolve ContentTree with a Kind switch plus one role=="figure" case, producing flow HTML for first paint, no-JS readers and static export. There is no render IR. The serializers read layout, emit, model (ContentNode args) and Config directly.

On the JavaScript side, shell.mjs/createEngine owns these jobs:
- worker transport;
- injecting the CSS contract (TSR_CSS);
- the container font contract;
- the semantic→typeset swap and its upgrade records;
- the editing patch, which re-chunks the HTML string at `.tsr-para` boundaries;
- copy installation and footnote hover popups;
- paginate/print with A4 constants;
- webfont and math-font injection;
- the NEED_IMAGES fallback that reads image dimensions on the main thread.

copy.mjs rebuilds content text from `.tsr-line` children using data-syn, data-join and data-src. audit.mjs checks line integrity, the right edge, overflow and stacking. node/render.mjs runs compile→execute→ingest→tokens→semantic in Node, and tools/export-static.mjs wraps that output in a fixed page template with optional hydration.

The robustness contract (nowrap lines, explicit spacing, atomic paragraph containers, a single unescaped path) is realized consistently. The three boundaries around the renderer are ad hoc:
- layout→render is a bag of per-feature fields and magic `special` codes;
- render→shell is an exact byte format plus naming conventions (`tsr-`, `fn-`, `fnref-`, `.tsr-sup`);
- the theme surface is an accidental subset of classes. Roles never reach the typeset DOM, and metric-bearing CSS is mixed with paint.


Strengths:

- The line-level takeover (v2 §8) is realized faithfully. Each text line is one absolutely positioned `white-space:nowrap` element with engine-computed word-spacing and letter-spacing, so measurement error stays inside the line, and DOM weight is O(lines + style runs).
- There is one line serializer, renderLineBox, shared by the screen and paged outputs (typeset_html.cc:631, :759). Pagination is a post-pass with a greedy fallback that cannot loop, matching KP's hard-cut fallback (pages-design §2).
- Escaping is consistent: there is exactly one unescaped path (raw.html, typeset_html.cc:194, semantic_html.cc:343-347), and safeImageSrc is enforced in emit, in the semantic serializer and in the pull scan.
- Px formatting is deterministic (fmtPx). Source offsets are paragraph-relative (data-s0 + data-s), so untouched paragraphs serialize byte-identically across edits. That property is what makes the editor's cheap per-paragraph DOM patch possible (editor-design §3–§4: 42→13 ms).
- The copy contract is normative and covered by e2e tests: hyphens are dropped, CJK breaks rejoin exactly, code wrap rows rejoin, and math copies as `$src$`. Copy works from the run structure, never from source offsets.
- The semantic serializer renders from leaf effective styles (pages-design §5). Resolver-produced prefixes, token colours and style patches therefore reach the no-JS and static page, and static export is resolver-complete without a browser.
- Some paint is properly separated from geometry. The superscript raise is paint-only (`position:relative; top:-.45em`). The token palette, highlight-line colour, CJK font and popup colours are CSS custom properties with dark-mode variants.
- Audits are diagnostic-only and never repair. audit.mjs is a single implementation that serves both dev diagnostics and the Playwright assertions.
- Fonts are declared to the worker, not discovered, so the settle race cannot occur by construction (pages-design §1).

</details>


### `render-runtime/semantic-role-switch` — The semantic serializer hard-codes role "figure" plus several per-construct shapes instead of using a role→element mapping

- kind: adhoc · severity: high · verdict: partly · plan: **P3-23**
- locations: `engine/src/render/semantic_html.cc:284-313`; `engine/src/render/semantic_html.cc:209-210`; `engine/src/render/semantic_html.cc:246`; `engine/src/render/semantic_html.cc:281`; `engine/src/render/semantic_html.cc:331-333`; `runtime/src/worker/executor.mjs:120`; `runtime/src/worker/executor.mjs:130`; `engine/src/resolve/resolve.cc:166`; `engine/src/emit/emit.cc:782`; `docs/document-model.md §2.1 Notes, §9.2`

`case Kind::group` reads ArgK::role and branches on `if (role == "figure")`. That branch emits <figure> and merges ALL para children into a single <figcaption> via inlineKids. Two caption paragraphs therefore concatenate with no separator, and a non-para kid that follows a caption para is emitted inside the still-open <figcaption>. Every other role, built-in (notes, bibliography, term, bibentry, sidecar-lines) or user-defined (`#!aside`, `#!theorem`), falls through to `<div data-role=…>`.

The same switch hard-codes several other shapes:
- A tight single-para list item is inlined. This drops the para's label id, which breaks footnote targets (see other_issues).
- Codeblock `group` kids are skipped as "display-layer only", so sidecar comments vanish.
- Images get `style="max-width:100%"` inline.
- Table alignment is emitted as inline `text-align`.

The normative §9.2 mapping (`term→dl>dt+dd`, `collect→nav|section`, `styled→span[class]`) is not what is built. The resolver rewrites terms and collectors into role groups or bare lists before the serializer runs, so the TOC becomes a plain <ul> and notes become `<div data-role="notes"><hr><ol>`.

*Why ad hoc:* The stated extension story is: "User constructors compose engine kinds … custom constructs are built from group/styled/raw" (document-model §2.1). Yet only the built-in figure gets real HTML semantics; a user region can never produce <aside>, <nav>, <details> or <dl>.

"Figure is a convention, not a kind" (§2.1) is a deliberate, documented choice, but the convention is enforced by string compares in four layers (executor, resolver, emit, semantic serializer). A role check sits inside an otherwise generic tree walk.

*Proposed generalization (survey):* Introduce a RoleTable owned by the model layer (engine/src/model/roles.h).

It is populated from built-in defaults plus declarations. Declarations come from the document program (`#{ $.role('theorem', {...}) }` → a ROLE_DECL op hoisted like fence registration) or from the host Config `roles` key.

`struct RoleSpec {`
- `StrRef name;`
- `Semantic { element ('figure'|'aside'|'nav'|'section'|'dl'|'div'), captionElement ('figcaption'|''), captionSelect (FirstPara|LastPara|AllParas|None), ariaRole };`
- `Typeset { cssClass; bool box; Su insetL, insetR, insetT, insetB (metric-bearing, consumed by emit) };`
- `Numbering { counter; supplementKey };`
- `Layout { floatable; captionCells; keepWithNext; atomic };`
- `CopyPolicy copy; }`

The semantic serializer's group case becomes a lookup, `const RoleSpec& r = roles.get(role); open(r.semantic) …`. Unknown roles keep today's output (`div[data-role]`) and add the class `tsr-role-<name>`. Figure becomes the first table entry instead of a branch. The same table drives resolver numbering and supplements; today `case Kind::group: disp = cfg.supFigure` (resolve.cc:287) makes any labelled group display as a figure.

*Verifier:* The serializer facts check out. semantic_html.cc:284-306 opens one <figcaption> and keeps it open through later non-para kids (block(k,-1) at :300). The tight-item inline is at :209-210, the codeblock group skip at :246, max-width at :281 and text-align at :331-333.

The wrong claim is in the generalization text: "`case Kind::group: disp = cfg.supFigure` (resolve.cc:287) makes any labelled group display as a figure". In fact a Kind::group label entry is only added inside `if (role == "figure")` (resolve.cc:165-170), and no other addLabel registers groups (:150, :162, :170, :191, :211-212, :222). So non-figure labelled groups are not referenceable at all. Verified with a scratch doc `#!aside(label: "box")` + `@box`:
- the typeset output renders "See ?? and ??.";
- both serializers still emit id="tsr-box".
That is the real defect: document-model §2.1 lists group as labelable, and v2 §11.1 says labels become anchors and refs become links.

*Verifier notes:* Ad hoc, despite the documented rationale. "Figure is a convention, not a kind" (document-model §2.1), yet the convention is enforced by string compares in four layers: executor.mjs:120/128, resolve.cc:166, emit.cc:781-783, semantic_html.cc:286.

The RoleTable is the right direction, but the RoleSpec as proposed puts HTML element names and CSS classes into a model-layer struct, so the model would reach into the renderers.

Better split:
- The model-level RoleSpec holds only semantic facts: counter/supplement, labelable/referenceable, caption policy, keep/atomic, a landmark category (figure | aside | navigation | section | definition-list | generic).
- Each backend maps category → element/ARIA/class in its own table, which Config/theme can override.
- RoleSpec must also drive label registration in resolver pass 1, so `@x` to any labelled role resolves using its supplement. The figure-only entry kind is today's actual bug.

The ROLE_DECL op needs an OPS_VERSION bump. It fits §11.1 because it only declares. Goldens can stay byte-exact if the defaults reproduce today's markup.

### `render-runtime/typeset-role-blind` — The typeset/paged DOM carries no role or kind hook: headings, captions, notes, theorems and TOC are anonymous lines

- kind: adhoc · severity: high · verdict: partly · plan: **P3-23**
- locations: `engine/src/emit/emit.cc:776-822`; `engine/src/emit/emit.h:113-166`; `engine/src/layout/layout.h:7-32`; `engine/src/render/typeset_html.cc:604-636`; `engine/src/render/typeset_html.cc:298-325`; `runtime/src/main/shell.mjs:16-85`

Emit's `case Kind::group` flattens every group into its parent's units; only a pending label anchor survives. FlowUnit has no role field, nor does LineBox. renderTypeset emits only `.tsr-para` / `.tsr-line` plus geometry.

Verified with tsrc (notes/basic, figure/float, region/table):
- A heading is a `.tsr-line` carrying `tsr-b` and an inline font-size.
- A float caption is `.tsr-line[data-cell]`.
- The notes section is a bare `.tsr-para` with `.tsr-marker` lines.
- The bibliography, TOC and any `#!theorem` region are plain lines.

Answer to Q5: a user cannot restyle figures, captions, notes or theorem boxes in the typeset page without engine changes. That holds even for paint-only properties (colour, background, border), and groups produce no rectangle that a background could be painted on. The semantic page exposes `data-role` (groups only) and <figure>. The two outputs therefore offer different theme surfaces.

*Why ad hoc:* The only semantic channel into the DOM is the style bits (tsr-b, tsr-i, tsr-cjk, tsr-code, tsr-sup). Feature hooks were added as one-off classes on specific element shapes (tsr-eqno, tsr-hlline, tsr-img, tsr-marker) rather than through a general role channel. User regions are therefore not on equal footing with built-ins.

*Proposed generalization (survey):* Make roles data throughout the pipeline.

Emit:
- Emit keeps a RoleScope stack while walking groups.
- `FlowUnit.roles` is an interned RoleId list (innermost first).
- Each group also appends `GroupExtent { RoleId role; u32 firstUnit, lastUnit; StrRef label; }` to its TopBlock.

Layout:
- For every extent whose `RoleSpec.typeset.box` is set, layout produces `DLBox::Region { rect = union of member line rects + insets; roleId }`.
- RoleSpec insets are metric-bearing and are applied by emit as indent and gaps.

Serializer:
- `.tsr-para` gets `class="tsr-para tsr-role-<outer>"` and `data-role`.
- Lines get `data-role` for unit-level roles (caption, heading with `data-level`).
- Region boxes render as `<div class="tsr-box tsr-role-theorem" style="top;left;width;height">`, emitted before the lines (z-order behind them).

Themes then style backgrounds, borders and colours purely in CSS. This must land together with shell-chunk-byte-coupling, because chunkParas regex-matches the exact `.tsr-para` opening tag.

*Verifier:* emit.h:113-166 is out of range: emit.h has 140 lines. FlowUnit is at emit.h:63-117 and TopBlock at :119-123.

The group flattening is confirmed (emit.cc:816-819, only pendingAnchor survives), as is the DOM evidence (tsrc notes/basic, figure/float).

"Cannot restyle … even paint-only properties (colour)" is overstated. An author can colour or resize a user region through region args: executor.mjs:131-136 wraps the region in styled{font, lang, color, sizePx}. What is impossible is theme (CSS) targeting and any background/border, because no element or rect exists for the group.

*Verifier notes:* Genuinely ad hoc. The only DOM channel for semantics is the style bits, and feature classes such as tsr-eqno, tsr-hlline and tsr-img are one-offs.

The generalization is sound under the invariants:
- RoleIds are data.
- Region rects come from layout.
- The group never crosses a root child, so it stays inside one .tsr-para atomic swap unit. (Wording: groups do nest, but they always stay within one TopBlock.)

Two additions:
- Role names are user strings. Class tokens must be validated or escaped as CSS identifiers, not only HTML-escaped.
- As the report says, the structured paragraph protocol must land first, because chunkParas regex-matches the exact .tsr-para opening (shell.mjs:264).

### `render-runtime/linebox-special-dispatch` — LineBox.special magic integers plus a renderer if-ladder that reaches back into FlowUnit, block streams and the content tree (no render IR)

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-18**
- locations: `engine/src/layout/layout.h:20`; `engine/src/layout/layout.cc:41`; `engine/src/layout/layout.cc:91`; `engine/src/layout/layout.cc:103`; `engine/src/layout/layout.cc:285`; `engine/src/layout/layout.cc:370`; `engine/src/layout/layout.cc:387`; `engine/src/layout/layout.cc:621-646`; `engine/src/render/typeset_html.cc:177-602`; `engine/src/render/typeset_html.cc:242-245`; `engine/src/render/typeset_html.cc:96-172`

The layout→render interface is LineBox {special, unitIdx, cellIdx, blockBegin/End, codeLine, cbLo/cbHi, codeCont, contCols, snapLatinPx/snapCjkPx, codeHl, marker, markerStyle, join, noGlue …}. The comment says `special` ranges 0-4, but 5 (image) is also used and is undocumented.

renderLineBox tests `special == 3` (raw), `== 1` (rule), `== 4` (math), `== 5` (image), `== 2` (code), and otherwise treats the line as text. Each branch reaches into feature payloads:
- `tb.units[l.unitIdx]`: rawHtml, rawHpx, mathBox, eqTag, imgSrc, imgAlt, codeRuns, codeLang, codeStyle;
- block streams in `u.cells[cellIdx]` or `u.blocks`;
- for display math, the model itself: `for (const ArgVal& a : mu.src->args) if (a.key == ArgK::src …)`.

mathLeaves flattens the MathBox tree inside the serializer. dumpLayout repeats the same ladder. Each feature commit (figures 217b3b7, verbatim 0c2ef30/383d91d, math cd8fb55, footnotes 57340a8) added a code or fields plus a renderer branch.

*Why ad hoc:* An open set of features is encoded as a closed enum spread across three structs and two layers. The serializer must understand every emit-level payload. No second backend (SVG, canvas, PDF) can be written without duplicating these 425 lines, and no extension can add a line kind.

*Proposed generalization (survey):* Add a paint pass at the end of layout that produces a display list (engine/src/paint/displaylist.h). Every backend consumes it.

- `DisplayList { i64 height; vector<DLBlock> blocks; }`
- `DLBlock { u32 pid; Span src; Su y, h; RoleIds roles; vector<DLBox> boxes; vector<DLLine> lines; }`
- `DLLine { Su x, y, w, h, baseline; Justify just; Sep sepAfter; StrRef sepText; u8 track; RoleIds roles; StrRef anchor; double wordSpacing; Span src; vector<DLItem> items; }`
- `DLItem` is one of:
  - `Run { StyleId; StrRef text; double letterSpacing, marginL, marginR, fixedWidth; StrRef href, anchor; SynKind syn; StrRef copyText; Span src; }`
  - `Glue { double px; SynKind syn; }`
  - `InlineBox { BoxId; Su w, asc, desc; StrRef copyText; }`
  - `Marker { StrRef text; StyleId; Su x; Su w; RoleId role; }`
- `DLBox` is one of `Rule{rect}`, `Image{rect, src, alt}`, `Raw{rect, html}`, `Glyphs{rect, vector<GlyphRun>, vector<RuleRect>}` (the flattened MathBox, display or inline), or `Region{rect, roleId}`.

The HTML backend becomes a mechanical walk of about 250 lines using one HtmlWriter. The paged backend and future SVG/canvas/PDF backends read the same list. Extensions add DLBox kinds or role classes, not special codes.

*Verifier:* Minor:
- Commit 0c2ef30 (V3 three-box) touched neither typeset_html.cc nor layout.h (git show --stat). Sidecar rows instead overloaded FlowUnit.cells (emit.h:89-90, "sidecar rows reuse `cells`"), which is why data-cell later means three things.
- FlowUnit::K (emit.h:64) already enumerates the unit kinds, so LineBox.special is a second, partly redundant enum. special==1 also has two y conventions:
  - Rule units store the midline with height = baseLeading (layout.cc:88-98).
  - Table rules store the top edge with height 0 (layout.cc:410-418).
  - This is why renderPages needs `x.special == 1 ? x.height/2 : 0` (typeset_html.cc:673).

*Verifier notes:* The ladder is confirmed: typeset_html.cc:183/198/208/254/298-301, dumpLayout layout.cc:621-646, and the model reach-back at :242-245.

The display list is a sound way to close the layout→render boundary. It keeps su determinism, and the HTML goldens can stay byte-exact.

The SVG/canvas/PDF motivation is overstated. v2 §8 puts text shaping inside the line in the browser, and the engine does not shape (measurement is canvas-provided). A non-DOM backend would need its own shaper and would fall outside the §7 robustness contract.

The concrete wins are:
- one source for screen and paged HTML;
- stateless per-paragraph render caching (editor-design §4);
- an extension point for new box kinds.

### `render-runtime/render-layout-decisions` — The serializer re-derives layout decisions (eqno placement, display-math centring, CJK/punct spacing, glue widths, run boundaries, paragraph gaps) duplicated from layout and emit

- kind: adhoc · severity: high · verdict: partly · plan: **P1-18**
- locations: `engine/src/render/typeset_html.cc:226-230`; `engine/src/render/typeset_html.cc:246-249`; `engine/src/layout/layout.cc:394-396`; `engine/src/render/typeset_html.cc:233-241`; `engine/src/render/typeset_html.cc:493`; `engine/src/render/typeset_html.cc:503-510`; `engine/src/render/typeset_html.cc:531-535`; `engine/src/render/typeset_html.cc:558-562`; `engine/src/layout/layout.cc:514-521`; `engine/src/render/typeset_html.cc:585-590`; `engine/src/layout/layout.cc:512`; `engine/src/emit/emit.cc:870-873`; `engine/src/render/typeset_html.cc:332-343`; `engine/src/render/typeset_html.cc:395-427`; `engine/src/render/typeset_html.cc:626-627`

Concrete duplications:

1. Display math advance. Layout sets `line.height = max(asc+desc, baseLeading)`. The renderer ignores l.height and recomputes `suRoundPx(cfg.lineHeight*cfg.baseSizePx)` twice, once for the line height and once for the centring offset.
2. Eqno position. Its `right` offset is computed in the renderer from `cfg.widthPx`, so horizontal placement is decided in render.
3. CJK stretch rule. The predicate `nx.isCjkChar() || (nx.isPunctGlyph() && !(nx.flags & BF_PUNCT_OPEN))` decides whether a CJK char stretches. It exists in layout (totalWeight, which drives slack per gap) and twice in the renderer (the margin-right cancellation for pairs and for CJK runs). If one copy drifts, justified lines miss the measure.
4. Punctuation squeeze. The `halfPresent` decision and the boundary-glue width (`rawPx + wordDeltaPx*stretchWeight`) are computed in render.
5. Shaped-run boundaries. What counts as one shaped run is decided in three places: emit fillSpaceContexts tags kerning only within the same style and link, layout adds kernPx for unbroken hyphen junctions assuming the pieces render as one text node, and the renderer's coalescing loop re-implements the boundary.
6. Config read by the renderer. It reads `cfg.codeFontFeaturesByLang` and splits code runs by script for snap-kerning.
7. Paragraph gaps. Layout positions paragraphs with `suRoundPx(paraSpacingEm*base)`, but the screen serializer emits the unrounded float as margin-bottom. The paged serializer positions by fr.y. The two outputs place paragraphs by different authorities, with sub-pixel drift per paragraph.

*Why ad hoc:* Each feature branch finishes the layout locally. v2 §8 says the cost model and the renderer "agree by construction"; in practice that agreement rests on copy-paste, not on shared code.

*Proposed generalization (survey):* Move every geometric and spacing decision into the paint pass that builds the display list.

Add `layout/gapmodel.h`:
- `struct GapModel { bool cjkStretches(const Block& b, const Block* next) const; double boundaryPx(const Block&, const LineBox&) const; bool sameShapedRun(const Block& a, const Block& b) const; SqueezeSide squeeze(const Block* prev, const Block& b, const Block* next) const; }`
- Breaker slack accounting, emit's kern-context tagging and paint all call the same object.

Paint writes the results into the display list:
- DLItem gets `letterSpacing`, `marginL`, `marginR` and `fixedWidth`.
- The eqno becomes a positioned Run with an explicit x.
- The display-math box offset is explicit.
- Code font features are resolved per run as a style property.

DLBlock.y is the single vertical authority: screen output emits block tops or margins derived from consecutive DLBlock.y. After this the serializer contains zero Config reads.

*Verifier:* Item 1: the renderer recomputes advH = max(box, suRoundPx(lineHeight*baseSizePx)) (typeset_html.cc:226-228, :246-249). This is numerically identical to layout's line.height (layout.cc:13, :394-396): duplicated, not divergent today.

Item 4: the boundary/indent width rawPx + wordDeltaPx×stretchWeight (typeset_html.cc:492-493) is the documented realization of LayoutResult's raw-px deltas (document-model §8: "wordDeltaPx, cjkDeltaPx … what the serializer emits"). halfPresent (:503-510) reads layout's trimmed [blockBegin, blockEnd) range, so it is derived from layout rather than decided independently.

These are confirmed real duplications:
- the CJK-stretch predicate exists three times (layout.cc:514-521; typeset_html.cc:531-535, :558-562);
- the eqno right offset comes from cfg.widthPx (:233-238);
- the run-boundary invariant exists three times (emit.cc:870-872, layout.cc:512, typeset_html.cc:584-590);
- codeFontFeaturesByLang is a Config read in the renderer (:332-343);
- the unrounded margin-bottom (:627) differs from paraGap = suRoundPx (layout.cc:14).

*Verifier notes:* A shared GapModel plus a paint pass is the right generalization. One fix is needed: the eqno cannot become "a positioned Run with an explicit x" unless the engine measures the tag. Today it is right-anchored (`right:` offset, typeset_html.cc:236-237) precisely because its width is never measured. Keep a right-edge anchor in the DL (x = right edge, align = end), or add the tag to the measure requests.

Per-gap px realization (raw-px path, document-model §6.1/§8) should stay in the paint pass, as the report says.

Another consumer the report missed: the run-boundary predicate must also include the synthetic/ref flag (see the missed item on run coalescing). Today's coalescing key (style, link) is already wrong for copy.

### `render-runtime/paged-keep-rules-by-kind` — Pagination, with keep rules dispatched on FlowUnit and content kinds, lives inside the HTML serializer; page geometry lives in the shell

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-12**
- locations: `engine/src/render/typeset_html.cc:642-765`; `engine/src/render/typeset_html.cc:673`; `engine/src/render/typeset_html.cc:681-694`; `engine/src/render/typeset_html.cc:702-705`; `engine/src/render/typeset_html.cc:723-731`; `runtime/src/main/shell.mjs:423`; `runtime/src/main/shell.mjs:448-452`; `docs/pages-design.md §2, §5`; `docs/notes-design.md §1 (paged render)`

renderPages builds bands with a closed list of cases:
- Table and float-Image units are atomic.
- A code logical line plus its sidecar rows is atomic.
- A block Image sticks to the next band.
- It inspects the content tree directly (`tb.node->kind == Kind::heading`) for keep-with-next.
- widows/orphans = 2 is a literal.
- Rules are special-cased as stored at their midline (`x.special == 1 ? x.height/2 : 0`).
- Output is HTML directly.

Footnote print inserts are planned to go into renderPages too (notes-design §1), which would be another feature branch in the serializer. Page size is an A4 literal in the shell, both in `@page { size: A4 }` and in the Gecko margin clamping.

Documented choice: "a post-pass over the finished LayoutResult — no re-break, no new layout mode" (pages-design §2).

*Why ad hoc:* The post-pass decision itself is sound. Housing it in the HTML serializer is the problem, for three reasons:
- A PDF or canvas backend cannot reuse it.
- keep/atomic semantics are limited to built-in kinds; a user `#!theorem` or a code-only figure cannot ask to be kept together.
- Page size is a shell constant rather than a document or print property.

*Proposed generalization (survey):* Keep it a post-pass, but over the display list and in the layout layer: `layout/paginate.h: PageLayout paginate(const DisplayList&, const PageSpec&)`, returning `pages: [{ bands: [{pid, lineLo, lineHi, yShift}], inserts: [{label, lines}] }]`.

Paint stamps break properties on each DLLine, taken from RoleSpec and FlowUnit:
- `u32 atomicGroup`: lines sharing it are indivisible (table, float box, code logical line, display math).
- `bool keepWithNext`: from the heading role, block-image role, or any user role.
- `u8 widows, orphans`: defaults from PageSpec, overridable per role.

`PageSpec { Su w, h; Su marginT, marginR, marginB, marginL; std::string cssSize; u8 widows = 2, orphans = 2; Su gutterBleed; }` lives in Config. The shell's print() derives `@page` from it. renderPages becomes: for each page, for each band, emit the lines rebased.

*Verifier:* Confirmed:
- typeset_html.cc:681-694 (atomic table, float and code logical line);
- :702-705 (block image stickAfter; tb.node->kind == Kind::heading);
- :727-728 (literal 2);
- :673 (rule midline);
- shell.mjs:423 and :448-452 (666×995, A4, Gecko clamp).

One minor doc/code mismatch: pages-design §2 says a heading "sticks to ≥2 lines of the next frame", but violates() only forbids a cut directly after the heading band (:725). Note also that page geometry is documented as "All numbers are options" (pages-design §2), while widows/orphans are engine literals.

*Verifier notes:* The documented decision is a post-pass, "no re-break, no new layout mode" (pages-design §2), and it is preserved.

The ad hoc part is where the logic lives (the HTML serializer) and the dispatch on built-in kinds and root node kind. A heading nested in a region never keeps.

Paginating over the DL with per-line atomicGroup/keepWithNext/widows from RoleSpec is sound and adds no layout iteration. It also gives the planned footnote inserts (notes-design §1) a home outside the serializer.

The same relocation should fix anchor emission. renderPages' per-sheet lastAnchored state drops and duplicates ids (see missed items).

### `render-runtime/copy-syn-policy` — Copy semantics of data-syn are hand-coded per feature: math uses data-src, everything else is dropped, and resolver-synthesized text is treated inconsistently

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-07**
- locations: `runtime/src/main/copy.mjs:15-19`; `engine/src/render/typeset_html.cc:149`; `engine/src/render/typeset_html.cc:236`; `engine/src/render/typeset_html.cc:276`; `engine/src/render/typeset_html.cc:287`; `engine/src/render/typeset_html.cc:364`; `engine/src/render/typeset_html.cc:379`; `engine/src/render/typeset_html.cc:454`; `engine/src/render/typeset_html.cc:484`; `engine/src/render/typeset_html.cc:494`; `engine/src/emit/emit.cc:70-91`; `engine/src/resolve/resolve.cc:166-180`; `engine/src/resolve/resolve.cc:258-266`; `test/e2e/typeset.spec.mjs:137-150`; `docs/document-model.md §9.3, §9.4`

There are nine data-syn values: hyphen, marker, ref, math, eqno, cont, indent, boundary, image. copy.mjs special-cases exactly one, `math`, which copies `dataset.src`; it drops all the others.

Whether resolver-produced text survives copy depends on which code path made it, not on a policy:
- Dropped: anything wrapped in Kind::ref gets emit's BF_REF and renders as data-syn="ref". That covers `@s`→`§1`, `@fig`→`图 1`, citation groups `[1, 2]`, footnote digits and the `↩` back-link. The e2e test asserts that '见 @s 一节' copies as '见  一节', with a double space and the reference gone.
- Copied: equally synthetic caption prefixes `图 1：`, bibliography `[n] ` and the term ` — ` are plain text runs.

Other gaps:
- `data-syn="image"` sits on <img> and `.tsr-imgph` elements that are not `.tsr-line` children, so copy never visits it. The attribute is dead.
- A split inline formula copies only when its FIRST segment intersects the selection, because later segments carry `data-src=""`.
- In the semantic first-paint phase native copy runs, and it does include refs. Copy output therefore changes when the typeset upgrade lands.

Documented: §9.3 "skip data-syn runs (hyphens, markers, resolved refs)".

*Why ad hoc:* Copy behaviour is an enumerated JS rule set keyed on attribute values the engine chooses. Every new synthetic construct must be decided in copy.mjs, as math was with its "§9.3 extension". Whether fabricated text is skipped is an accident of the node kind that produced it.

*Proposed generalization (survey):* Use one engine-side policy expressed through one DOM attribute.

Engine side:
- DLItem carries `SynKind` (None, Hyphen, Marker, Ref, Math, EqNo, Cont, Glue, Image, Prefix, Custom) and `StrRef copyText`.
- The paint pass resolves copyText from `Config.copy = { ref: 'display'|'source'|'omit', math: 'source', eqno: 'omit', marker: 'omit', prefix: 'display', … }`. Source text comes from the node span; the shell already owns the source (architecture §4.2).
- The resolver tags fabricated text (caption prefix, `[n] `, ` — `) as SynKind::Prefix, so policy rather than code path decides.

Serializer: write `data-syn` (still used by audits and styling) plus `data-copy="…"` whenever the copy text differs from the visible text; an empty value means omit.

copy.mjs: `text = run.hasAttribute('data-copy') ? (run is the first segment of its copy group ? run.dataset.copy : '') : slice(run)`. A shared `data-copy-group` id handles split formulas.

*Verifier:* Confirmed:
- the nine data-syn values;
- copy.mjs:15-19 special-cases only math;
- data-syn="image" sits on <img> and .tsr-imgph siblings of lines (typeset_html.cc:276, :287), never visited by copy.mjs:11;
- native copy runs in the semantic phase (installCopy only after swapIn, shell.mjs:372-376).

Overstated: test/e2e/typeset.spec.mjs:147-149 asserts only `not.toContain('§')` and `toContain('见')`, not '见  一节'. The double space is real, though: tsrc on '见 @s 一节。' gives runs 见, ' ', <a data-syn=ref>§1</a>, ' ', 一节.

The split-formula first-segment-only behaviour is documented in document-model §9.4.

Citation groups are not cleanly "dropped". The coalescing loop (typeset_html.cc:584-590) ignores BF_REF:
- the "[" merges into the preceding prose run and is copied;
- "]" begins a data-syn="ref" run that swallows following prose (test/golden/cite/basic.html.txt: `data-syn="ref">]; hyphenation patterns follow`).

*Verifier notes:* Policy-by-node-kind is genuinely ad hoc. The engine-side SynKind plus copyText plus data-copy approach is right, but it needs two changes:

1. SynKind must be part of the run-coalescing key. Otherwise a run carries one data-syn or data-copy for mixed synthetic and real text, which is exactly today's citation bug.
2. The handler must always own copy inside typeset containers. The empty-text native fallback (copy.mjs:52) must go, or synthetic-only selections still copy "§1".

Default 'omit' for refs keeps the e2e tests stable. 'display' is arguably the better reader default and should be a config decision.

No ops impact. Golden churn is limited to data-copy attributes.

### `render-runtime/copy-line-separators` — Line joins are a three-state attribute patched per feature; cells, sidecars and empty lines fall through to a newline

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-07**
- locations: `engine/src/layout/layout.h:17`; `engine/src/layout/layout.cc:66-78`; `engine/src/layout/layout.cc:544-550`; `engine/src/layout/layout.cc:587`; `engine/src/layout/layout.cc:467-482`; `engine/src/layout/layout.cc:344-357`; `engine/src/render/typeset_html.cc:301-309`; `engine/src/render/typeset_html.cc:322-323`; `runtime/src/main/copy.mjs:29-36`

LineBox.join takes three values: 0 = last line, 1 = space, 2 = none. copy.mjs maps an absent join to '\n' and a change of `.closest('.tsr-para')` to '\n\n'.

Each line kind sets the join its own way:
- Code rows: the renderer peeks at the next line (`fr.lines[li+1].special == 2 && codeCont`) and emits data-join="none".
- Float captions: layout computes joins in their own loop.
- Text lines: layout computes joins in a separate loop.
- Table cells and sidecar rows: no join is ever set.

Consequences:
- A hyphenated or URL-broken word in a table cell copies as 'exam\nple'.
- A table copies as one cell per line, losing cell and row structure.
- A code block with a sidecar copies as code line, newline, comment, newline… The `///` marker is lost and wrapped comment rows are split.
- On paged sheets `.closest('.tsr-para')` is null for every line, so paragraph breaks collapse.

*Why ad hoc:* Separator semantics depend on the line kind, and each line producer decides them — or forgets to — on its own. The renderer adds a code-specific patch on top.

*Proposed generalization (survey):* Layout stamps every line with `Sep sepAfter` (None, Space, Newline, Paragraph, Cell, Row, Custom) plus `StrRef sepText`.
- One helper, `Sep joinAfter(const vector<Block>&, u32 bp, LineRole)`, is used by all line producers: text, cell, sidecar, caption and code.
- Cell = '\t' and Row = '\n'.
- Custom carries ' /// ' after the last code row of a logical line that has a sidecar.
- Paragraph comes from DLBlock boundaries, not from DOM ancestry.

Serialize it as `data-sep`, keeping data-join as a compatibility alias. copy.mjs becomes a dumb concatenator: it never skips a line that lies inside the range, even an empty one, and always appends that line's separator.

*Verifier:* Confirmed:
- LineBox.join is 3-state (layout.h:17);
- the caption join loop (layout.cc:66-78) and the text join (:544-550, :587);
- no join on table cells (:467-482) or sidecar rows (:344-357);
- the renderer's next-line peek (typeset_html.cc:305-308).

Verified with tsrc: a hyphenated table cell renders interna-/tional-/ization rows with no data-join, so it copies 'interna\ntional…'.

The paged-sheet consequence is latent only. installCopy is attached solely to the live container (shell.mjs:376); paginate() returns HTML and print() injects it under #tsr-print-root, and neither has a copy handler (only the e2e harness calls contentTextFromRange).

*Verifier notes:* Join semantics decided separately in each line producer is the copy-side symptom of the four line producers in layout.cc. An explicit Sep per line is set by one helper. Cell, Row and Custom separators come from layout, and Paragraph separators come from block boundaries rather than DOM ancestry. That is sound and makes copy.mjs a dumb concatenator.

The Custom ' /// ' for sidecars settles verbatim-design §5's OPEN copy contract, so it should be recorded there.

### `render-runtime/shell-note-popups` — Footnote popups in the 'thin' shell scrape the engine DOM via resolver label names, a style class and assumptions about layout shape

- kind: adhoc · severity: high · verdict: partly · plan: **P3-04, P3-06**
- locations: `runtime/src/main/shell.mjs:47-55`; `runtime/src/main/shell.mjs:126-197`; `runtime/src/main/shell.mjs:137-150`; `runtime/src/main/shell.mjs:146`; `runtime/src/main/shell.mjs:162-165`; `runtime/src/main/shell.mjs:175`; `runtime/src/main/shell.mjs:353`; `runtime/src/main/shell.mjs:376-378`; `engine/src/resolve/resolve.cc:209-216`; `engine/src/resolve/resolve.cc:340-348`; `docs/notes-design.md §1 ('shell concern, not engine')`; `docs/architecture.md §4.2 ('thin by design')`

Markers are recognised by the conjunction of three conventions:
- the CLS_SUP style bit, rendered as class `tsr-sup`;
- the renderer's `#tsr-` id prefix;
- the resolver's `fn-n` / `fnref-n` label scheme.

The body is located by assuming the notes list is ONE `.tsr-para` and that the next item starts at the next sibling line carrying an id or a `.tsr-marker`. Text is extracted with `cloneNode().textContent` and joined with ' '. This bypasses the copy contract:
- line-final hyphen glyphs stay, giving 'exam- ple';
- CJK line breaks gain spurious spaces;
- math becomes Euler glyph soup;
- styling is lost.

The popup is appended to `.tsr-doc`, which is also patchIn's root, and that breaks incremental patching while it is visible (see other_issues). On the semantic page the target ids do not exist (dangling footnote ids). Its CSS and a popup-specific custom property (`--tsr-pop-font`) are installed by the core typeset() path.

Only footnotes get previews. Citations, figure, equation and heading refs, and glossary terms share the same ref machinery but cannot have them.

*Why ad hoc:* A feature behaviour is hard-wired into the shell core and coupled to three other layers' private conventions. Users cannot replicate it, replace it, or extend it to other ref kinds. The docs accept it as a "shell concern", but it contradicts "thin by design".

*Proposed generalization (survey):* Three pieces.

1. Ref metadata. Every ref run gets `data-ref="<label>" data-ref-kind="note|figure|equation|heading|table|bib|term"`; the resolver already has the label-table entry kind (resolve.cc:285-290).

2. Fragment channel. The worker result carries `fragments: {[label]: html}`, rendered by the semantic serializer from each referenced target's subtree via a new `renderSemanticFragment(const ContentNode*)`. It can be lazy: a `fragment?` request on first hover.

3. Behaviour registry.
- `createEngine({ behaviors: [refPreview({ kinds: ['note'] }), …] })`
- `Behavior = { name, css?: string, install(ctx) → uninstall }`
- `ctx = { container, overlay /* positioned sibling of .tsr-doc, outside the patch root */, onCommit(cb), fragment(label), anchorOf(label), offsetAt(el), elementsAt(offset) }`

The footnote popup then becomes `refPreview` filtered to kind 'note'. Citation previews, glossary previews and user behaviours register the same way.

*Verifier:* Confirmed:
- the selector conjunction `a.tsr-sup[href^="#tsr-fn-"]` (shell.mjs:175) and `a[href^="#tsr-fnref-"]` (:146);
- the one-.tsr-para assumption (:133-143, commit 25b89ee);
- textContent joined with ' ' (:144-150), so hyphen glyphs, inter-CJK spaces and Euler glyph text leak into the popup;
- the popup appended to .tsr-doc (:162-165);
- --tsr-pop-font set in typeset() (:353).

Overstated: "On the semantic page the target ids do not exist" does not affect the popup. installNotePopups runs only after the typeset swap (shell.mjs:372-377), including the static-export hydration (progressive:false), so the semantic branch `else lines.push(el)` (:143) is dead in every shipped flow. The dangling semantic ids hurt no-JS and static readers, not the popup.

*Verifier notes:* Documented as a "shell concern, not engine" (notes-design §1). That contradicts "thin by design" (architecture §4.2) and couples the shell to resolver label naming, a style bit and layout shape.

The three-part fix is sound:
- data-ref/data-ref-kind on ref runs;
- lazily rendered semantic fragments;
- a Behavior registry with an overlay outside the patch root.

The worker protocol gains one message, and ops are unaffected.

The fragment should come from the post-resolve tree so @refs inside note bodies stay resolved. renderSemanticFragment over the note's item node does that.

### `render-runtime/shell-chunk-byte-coupling` — The editing patch parses the serializer's exact byte layout instead of using a structured per-paragraph protocol

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-05**
- locations: `runtime/src/main/shell.mjs:251-278`; `runtime/src/main/shell.mjs:264`; `runtime/src/main/shell.mjs:273`; `runtime/src/main/shell.mjs:279-309`; `runtime/src/main/shell.mjs:312-328`; `engine/src/render/typeset_html.cc:604-636`; `docs/architecture.md §4.3`; `docs/editor-design.md §3`; `docs/design-decisions-v2.md §9`

chunkParas depends on exact bytes from the serializer:
- the string prefix `'<div class="tsr-doc">'`;
- `indexOf('<div class="tsr-para"')` to find each paragraph;
- a regex that fixes attribute order, `^(<div class="tsr-para" data-pid="\d+" data-s0=)"(\d+)"`;
- a literal tail, `'</div>\n</div>\n'`.

data-s0 must be normalized out of each chunk and patched back afterwards. Any serializer change (a role class, a reordered attribute, a wrapper element) silently disables the fast path and falls back to a full swap.

architecture §4.3 specified a structured `paragraphs{docId, [{pid, html, rect, lineMap}]}` message. The as-built single-string protocol was retrofitted. swapIn replaces the whole container via innerHTML, although v2 §9 says "upgrades swap atomically per paragraph".

*Why ad hoc:* This couples C++ string building to JS string parsing as a wire format, and was introduced for one feature (the editor).

*Proposed generalization (survey):* Make the result structured:
`{ head: { classes, cssVars, idPrefix }, paras: [{ pid, key: hash64(para-relative html), s0, h, html }], height }`
The serializer renders per DLBlock.

The shell diffs by `key`. With content-keyed pids this also fixes the documented tail-shift limitation for inserted or deleted paragraphs (editor-design §3). It sets data-s0 from the field. Initial upgrade, relayout and update all use the same per-pid commit, so v2 §9's atomic per-paragraph swaps become real, and upgrade records are computed per swapped pid.

*Verifier:* Confirmed:
- shell.mjs:252-253 (exact prefixes), :264 (attribute-order regex), :273 (literal tail);
- architecture §4.3 specifies `paragraphs{docId, [{pid, html, rect, lineMap}]}`;
- v2 §9: "Upgrades swap atomically per paragraph";
- swapIn uses container.innerHTML (:316).

*Verifier notes:* The structured result is right. The diff key needs care, though:
- The key must exclude every container attribute that shifts with position: data-pid as well as data-s0. Positional pids are the documented tail-shift cause (editor-design §3), so hashing HTML that still contains data-pid would not fix it.
- The diff must stay order-preserving (common prefix/suffix, or an LCS over keys), not a key→element map, because identical paragraphs (repeated rules, empty notes groups) produce colliding keys.
- pid and s0 should be fields that the shell writes onto the container it creates.

With those changes, the generalization subsumes swapIn and patchIn into one per-pid commit.

### `render-runtime/anchor-namespace` — The DOM id prefix 'tsr-' and the label naming conventions are hard-coded across resolver, both serializers and the shell

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-04**
- locations: `engine/src/resolve/resolve.cc:113`; `engine/src/resolve/resolve.cc:263`; `engine/src/resolve/resolve.cc:292`; `engine/src/resolve/resolve.cc:149`; `engine/src/resolve/resolve.cc:211-212`; `engine/src/render/typeset_html.cc:213`; `engine/src/render/typeset_html.cc:267`; `engine/src/render/typeset_html.cc:314`; `engine/src/render/typeset_html.cc:450`; `engine/src/render/semantic_html.cc:46`; `engine/src/render/semantic_html.cc:137`; `runtime/src/main/shell.mjs:146`; `runtime/src/main/shell.mjs:175`; `runtime/src/main/shell.mjs:462-469`

The resolver, a model-layer pass, writes presentation URLs into the content tree: `setArgStr(r, ArgK::url, "#tsr-" + target)`, plus `#tsr-bib-…` and `#tsr-<anchor>` for TOC links. Each serializer prefixes ids independently, four times in the typeset serializer and twice in the semantic one. The shell matches `#tsr-fn-` and `#tsr-fnref-`.

Two engine documents on one page produce duplicate ids and cross-wired refs. Examples: a blog index, side-by-side previews, or the print root that print() injects next to the live document.

*Why ad hoc:* The id namespace is a render concern that has leaked into the model. Implicit label conventions (fn-n, fnref-n, bib-<key>, h-<n>) act as an undeclared ABI between the resolver and the shell.

*Proposed generalization (survey):* The resolver stores only `ArgK::target` (the label) for internal refs; external links keep ArgK::url. The render layer maps labels through one `AnchorNamer { std::string prefix; std::string id(label); std::string href(label); }`, configured by `Config.idPrefix` (default 'tsr-'; the shell can pass a per-instance prefix). The result exposes idPrefix, and behaviours resolve labels through `ctx.anchorOf(label)`. Label kinds travel as data-ref-kind, so nothing outside the resolver parses 'fn-'.

*Verifier:* Confirmed:
- resolve.cc:113, :263, :292 write "#tsr-…" urls into the model;
- the id sites at typeset_html.cc:213/267/314/450 and semantic_html.cc:46/137;
- shell.mjs:146 and :175.

Note that the convention is normative: document-model.md:132 says "Labeled units render `id=\"tsr-<label>\"` … resolved refs render as `<a href=\"#tsr-<label>\" data-syn=\"ref\">`". It is documented, but it is still a model→presentation leak.

*Verifier notes:* An AnchorNamer in the render layer, with the resolver storing only the target, is sound and golden-neutral with the default prefix.

One caveat: the default prefix must stay stable per document. Static-export ids and the hydrated typeset ids must be identical, or shared deep links (post#tsr-fig-1) break on upgrade. Per-instance prefixes belong only to secondary instances: the print root, side-by-side previews and multi-document pages.

### `render-runtime/css-contract-monolith` — TSR_CSS mixes the robustness contract, metric-bearing class mappings and per-feature paint in one hand-maintained string

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-18**
- locations: `runtime/src/main/shell.mjs:16-85`; `runtime/src/main/shell.mjs:39`; `runtime/src/main/shell.mjs:67-68`; `runtime/src/main/shell.mjs:11-12`; `engine/src/measure/measure.h:60-71`; `engine/src/api/config.h:20-31`; `tools/export-static.mjs:66-68`; `editors/vscode-tsm/media/preview.html:11-14`; `docs/document-model.md §9.1`

The single string contains four kinds of rules:

(a) The robustness contract: `.tsr-line { position:absolute; white-space:nowrap }`.

(b) Metric-bearing mappings that must equal describeStyle's measurement mapping:
- `.tsr-b { font-weight:700 }` ↔ weight 700;
- `.tsr-i` ↔ italic;
- `.tsr-code { font-family: monospace }` ↔ `cfg.monoFont`;
- `.tsr-cjk { font-family: var(--tsr-cjk-font) }` ↔ `cfg.cjkFont`, with the CJK stack copied into shell.mjs.
A theme overriding `.tsr-b` or `.tsr-code` silently breaks the measure/render contract, and nothing guards against it.

(c) Per-feature paint for math fonts, eqno, hl lines, footnote popups, the token palette and list markers. The link colour is a literal (`.tsr-doc a { color:#1a5276 }`) with no variable and no dark variant, so the VS Code preview overrides it.

(d) Dead rules. `.tsr-marker.tsr-code { font-size:.85em }` is always overridden by the inline font-size the renderer emits for codeScale runs.

Semantic-page CSS is also split: the exporter adds `.tsr-flow img/pre/.tsr-err` rules that the live first paint never gets. Answer to Q5: of the paint values, only the token palette, hl-line, CJK font and popup colours are variables. Everything else is a literal or an inline style.

*Why ad hoc:* Each feature appended CSS to the core contract. The bits→font mapping exists twice (C++ and CSS) with nothing keeping them in sync, and the theme surface is accidental rather than designed.

*Proposed generalization (survey):* Split the CSS into three layers with distinct owners.

1. contract.css is generated from one table, `render/style_css.def` (for each bit: `{measure: weight|italic|familyKey, css: declarations}`). describeStyle and the CSS generator both read that table. It also holds the nowrap and positioning rules, and is never themed.

2. Feature and behaviour CSS ships with each module: math, notes preview, code grid, print.

3. Theme tokens. Every paint value becomes a custom property with light and dark defaults (`--tsr-link`, `--tsr-rule-color`, `--tsr-rule-opacity`, `--tsr-marker-color`, `--tsr-eqno-gap`, `--tsr-imgph-border`, …). Themes either set variables or target role classes.

The docs should mark each property as metric or paint-only. Metric changes go through Config (fonts, roles), never CSS.

*Verifier:* Confirmed:
- shell.mjs:23 contract;
- :26-29 metric mappings duplicated from measure.h:60-70;
- the TSR_CJK_FONT copy at :11-12;
- the literal link colour at :39, overridden in editors/vscode-tsm/media/preview.html:11-13;
- the dead `.tsr-marker.tsr-code` font-size at :67-68 (tsrc shows inline font-size:15.3px on every code marker);
- exporter-only rules at export-static.mjs:66-68.

Two additions:
- `.tsr-flow code .tsr-err` (export-static.mjs:68) never matches, because semantic errors are not emitted inside <code> (semantic_html.cc:161-167, :348-356).
- The CSS and describeStyle already disagree on precedence. describeStyle picks monoFont over cjkFont when both CODE and CJK are set (measure.h:62-64), but `.tsr-cjk` is declared after `.tsr-code` at equal specificity, so CSS paints the CJK stack. This is latent: no golden run carries both classes.

*Verifier notes:* Generating the metric-bearing CSS from the same table describeStyle reads is the right fix. The generator must also encode precedence (family resolution order), not just declarations.

The split is contract / feature module / theme tokens. Marking each property metric or paint-only matches the §7 measurement-robustness contract.

### `render-runtime/no-class-channel` — Run styling is inline-only and there is no user class channel; dynClasses is specified but not implemented

- kind: adhoc · severity: high · verdict: partly · plan: **P3-18**
- locations: `engine/src/model/model.h:29-40`; `docs/document-model.md §3`; `docs/document-model.md §9.2`; `engine/src/render/typeset_html.cc:44-90`; `engine/src/render/semantic_html.cc:66-119`; `runtime/src/worker/executor.mjs:131-136`

Styling holds `{bits, sizeMul, fontFamily, lang, color, sizePx}`. The spec's `TextStyling = { classBits, dynClasses, inline }` and §9.2's `styled→span[class]` do not exist.

Authors can set font, lang, color, sizePx, bold and italic, through #style or region args, but cannot attach a class or role to an inline span or a block. Colour, size and decorations are emitted inline on every run, and inline styles beat any theme rule; this includes token colours as `color:var(--tsr-tok-…)` strings. Every code token span repeats `font-size:15.3px`.

A user who wants 'warning' text styled by a theme can only hard-code a colour in the document.

*Why ad hoc:* The style model kept only the properties early features needed. Classes were replaced by fixed bits and inline CSS strings, so theming is closed to users and to user-defined constructs.

*Proposed generalization (survey):* Implement dynClasses as specified. Styling gets `SmallVec<u32> classes` (interned names, sorted, included in the hash and equality). Classes are metric-neutral by contract: describeStyle ignores them.

Authoring: `#style({class: 'warn'})[…]`, a region arg `class:`, and RoleSpec.cssClass.

Serializer:
- emit generated per-StyleId classes for metric-bearing properties (`.tsr-s7 { font-size:15.3px; font-family:… }`) in a per-document style block or in the structured result's head;
- emit user and role classes verbatim, with a `tsr-u-` prefix;
- keep inline styles only for geometry and justification;
- route user-set paint properties through a variable on the generated class so a theme can override them.

*Verifier:* Styling is {bits, sizeMul, fontFamily, lang, color, sizePx} (model.h:29-35). dynClasses (document-model §3) and styled→span[class] (§9.2) are unimplemented.

Overstated: token colours are emitted as inline `color:var(--tsr-tok-*)` and are themeable through the custom properties (shell.mjs:71-84). "Inline styles beat any theme rule" holds for author colours, sizes and families, not for the token palette.

*Verifier notes:* Ad hoc: the documented class channel was replaced by fixed bits plus inline CSS strings.

The proposed per-StyleId generated classes (`.tsr-s7`) conflict with the editor's byte-identity invariant (editor-design §3). Each update creates a fresh doc (worker.mjs:166 `_tsr_doc_new`), and StyleIds are assigned in interning (document) order. A new style introduced early in the document renumbers every later StyleId, which changes the bytes of untouched paragraphs and defeats patchIn.

Fix: name generated classes by a stable content hash of the Styling, or keep metric declarations inline and use classes only for metric-neutral user/role/token classes.

Class names from documents must be validated as CSS identifiers. The OPS_VERSION bump for a class arg on styled is correct.

### `render-runtime/token-theme-sniffing` — Code-token semantics travel as CSS colour strings, and emit detects comments by string-comparing a colour

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-18**
- locations: `engine/src/code/tokens.cc:44-46`; `engine/src/emit/emit.cc:618-622`; `engine/src/code/tokens.h:10-17`; `runtime/src/worker/tokens.mjs:5-8`; `runtime/src/main/shell.mjs:69-84`; `docs/code-design.md §5`

foldTokens writes the token tag into Styling.color as the string `var(--tsr-tok-<tag>)`, and makes comments italic via the magic index `if (tag == 3) s.bits |= CLS_EM` — a theme decision baked into a metric bit.

Comment-aware hanging (verbatim §4) then recovers 'is a comment' with `styles.get(k->style).color == strs.intern("var(--tsr-tok-comment)")`.

The tag list is maintained three times — tokens.h kTokenTags, tokens.mjs TAGS and the CSS variable list — each with a 'keep in sync' comment. code-design §5 promised a generated tagId↔class table.

*Why ad hoc:* The theme layer (CSS variable names) is used to carry a layout decision, so wrapping behaviour depends on a colour string. A user who restyles a token run with #style({color}) silently changes how the line wraps.

*Proposed generalization (survey):* Add `code/tokens.def` as the single source, like ops.def. It generates kTokenTags, the JS TAGS/ALIAS table and the CSS variable defaults.

A token becomes a class id in Styling (no-class-channel): `classes = {tok-comment}`. `CodeRun.isComment = (tag == Tok::comment)` is set when tokens are folded. Comment italics become a metric theme entry in Config (`code.tokenStyle.comment = {italic: true}`) read by foldTokens. Colour is pure CSS on `.tsr-tok-comment { color: var(--tsr-tok-comment) }`.

*Verifier:* Confirmed:
- tokens.cc:44 builds `var(--tsr-tok-<tag>)` into Styling.color;
- :46 `if (tag == 3) s.bits |= CLS_EM; // comment: italic (duplex contract)`;
- emit.cc:618-622 recovers isComment by comparing the colour against the interned "var(--tsr-tok-comment)";
- the tag list is maintained by hand in tokens.h:10-17 and tokens.mjs:5-8 (code-design §7 admits "shared by hand"). code-design §5 promised a generated tagId↔class table.

The example is imprecise. foldTokens overwrites colour on token leaves, so a #style({color}) around a fence does not reach token runs. The realistic failure is a fence handler, or a future theme pass, that colours comments differently: comment-aware hanging (layout.cc:180, :188-215) then silently turns off.

*Verifier notes:* A tokens.def single source is sound.

CodeRun.isComment already exists (emit.h:79), but CodeRuns are built in emit from ContentNodes, after folding. "Set isComment when tokens are folded" therefore needs the tag to survive into the content tree, as a class id on the leaf (which needs no-class-channel) or an arg. emit then reads the tag, not the colour.

Comment italics are a documented "duplex contract" (a metric-neutral italic in duplex mono fonts). Moving them to a Config token-style map is fine.

### `render-runtime/marker-gutter` — List markers and code line numbers share one out-of-flow `.tsr-marker` CSS trick; line numbers are recognisable only because they happen to carry the code style

- kind: adhoc · severity: low · verdict: accurate · plan: **P1-18, P3-16**
- locations: `engine/src/emit/emit.cc:552`; `engine/src/layout/layout.cc:298-304`; `engine/src/render/typeset_html.cc:360-369`; `runtime/src/main/shell.mjs:30-31`; `runtime/src/main/shell.mjs:67-68`; `engine/src/render/typeset_html.cc:749`; `docs/verbatim-design.md §5`

The code gutter column of the three-box model (verbatim §5: "the gutter is the DEGENERATE column") is realized as a list-marker span. CSS positions it with `right:100%; padding-right:.55em`; layout never gives it an x or a width.

Line numbers are themeable only through `.tsr-marker.tsr-code`, which works because emit sets `markerStyle = codeStyle`. That rule's font-size is dead, since the inline font-size wins.

Because the gutter sits outside the line and outside the measure:
- paged sheets (`overflow:hidden`) clip the line numbers of top-level code blocks;
- the audit's overflow check (scrollWidth) cannot see overflow to the left.

*Why ad hoc:* Two different features, list enumerators and code gutters, reuse one CSS positioning trick, and placement is decided by CSS rather than by layout.

*Proposed generalization (survey):* Make markers display-list items: `DLItem::Marker { StrRef text; StyleId style; Su x /* relative to line, may be < 0 */; Su w; RoleId role ∈ {list-marker, line-number, …} }`, positioned by the paint pass.

Layout reserves the gutter width: measured digits × ch for code, the existing indent for lists. PageSpec carries a gutter bleed so sheets include it. The serializer emits `class="tsr-marker tsr-role-line-number"` with an explicit left.

*Verifier:* Confirmed:
- emit.cc:552 (markerStyle = codeStyle);
- layout.cc:298-304;
- typeset_html.cc:360-369;
- CSS `right:100%` (shell.mjs:30-31);
- `.tsr-sheet` `overflow:hidden` (typeset_html.cc:749).

The audit's scrollWidth check (audit.mjs:136-142) cannot see negative-x overflow.

*Verifier notes:* verbatim-design §5 deliberately reuses the marker mechanism ("the gutter is the DEGENERATE column … fixed x, out of flow"). Leaving placement to CSS is what makes it ad hoc and causes the print clipping.

The proposal is sound with one clarification. "Layout reserves the gutter width" must not shrink the code measure, which §5 keeps independent. Layout should compute the gutter extent and an explicit negative x for the Marker item, and PageSpec should carry the bleed so sheets and audits include it.

### `render-runtime/config-plumbing` — Feature knobs are threaded by hand through five layers, and defaults are duplicated and drifting

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-03**
- locations: `runtime/src/main/shell.mjs:331-337`; `runtime/src/main/shell.mjs:358-362`; `runtime/src/main/shell.mjs:393-397`; `runtime/src/worker/worker.mjs:157-193`; `engine/src/api/wasm_api.cc:42-76`; `engine/src/api/config.h:20-71`; `runtime/src/node/render.mjs:24-28`; `tools/export-static.mjs:36`; `tools/export-static.mjs:49`; `tools/export-static.mjs:56`; `tools/export-static.mjs:64`; `docs/document-model.md §11`

Each option — codeFontFeatures, codeFontFeaturesByLang, verbatimSnapKerning, punctCompress, lang, fonts and the rest — follows the same path. It is a destructured shell parameter, repeated in both the typeset and the update message, destructured again in the worker, and set through its own C export (tsr_set_font, tsr_set_cjk_font, tsr_set_snap_kerning, tsr_set_code_features, tsr_set_lang …). Many Config fields have no setter at all: monoFont, codeScale, sidebarFrac, listIndentEm, the supplements and the cost params.

Defaults disagree:
- Body font: 'Georgia, serif' in the shell versus '"Crimson Text", Georgia, serif' in config.h, the exporter and the hydrate script.
- Document language defaults to zh-CN in three places (shell, the exporter's `<html lang>`, the Config supplement defaults). render.mjs accepts a lang but the exporter never passes one, so English documents export as '图 1' / '表 1'.

document-model §11 specifies a single JSON config with diagnostics for unknown keys.

*Why ad hoc:* Individual features' knobs are hard-coded into the public API. Adding one knob touches five files, and none of it is extensible.

*Proposed generalization (survey):* Add a `config.def` X-macro listing key path, type, default and doc string. It generates:
- the C++ Config struct plus a JSON parser that reports unknown keys (`tsr_config_json(doc, json)`, replacing the per-key exports);
- JS defaults and types;
- the documentation table.

Keys are namespaced: code.fontFeatures, code.snapKerning, page.size, copy.ref, idPrefix, roles.*.

The shell API becomes `typeset(src, el, { engine: {...} /* passed through opaquely */, host: { fonts, progressive, behaviors } })`. The document can declare `#set({lang: 'en'})`, with a host override.

*Verifier:* Confirmed:
- the option is threaded through shell.mjs:331-337, :358-362 and :393-397, worker.mjs:157-193, and per-key exports in wasm_api.cc:42-76;
- no setters exist for monoFont, codeScale, sidebarFrac, listIndentEm, cost or supplements (supplements are reachable only via applyLang, config.h:89-101);
- the font default differs: 'Georgia, serif' in shell.mjs:332 vs config.h:23, export-static.mjs:49/64;
- zh-CN is the default in shell.mjs:333, the hard-coded `<html lang>` in export-static.mjs:56, and the Config supplement defaults (config.h:68-71);
- renderTsm is never given lang (export-static.mjs:36).

*Verifier notes:* The single JSON config with unknown-key diagnostics is already the documented design (document-model §11), so the X-macro generator implements the spec rather than adding a new mechanism. It is deterministic and needs no ops change. Keep the per-key exports as wrappers for one release.

### `render-runtime/static-export-template` — The static exporter hard-codes the page template, fonts, language and hydration script, and cannot enumerate document resources

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-21, P3-36**
- locations: `tools/export-static.mjs:36`; `tools/export-static.mjs:42-53`; `tools/export-static.mjs:55-76`; `tools/export-static.mjs:81-93`; `runtime/src/node/render.mjs:20-52`; `runtime/src/worker/executor.mjs:40-53`; `docs/pages-design.md §3`

renderTsm is called with defaults:
- `#bibliography("refs.json")` resolves against the process cwd, not the .tsm file's directory;
- the document language is never set.

The hydration script re-typesets in the browser. There the executor fetches refs.json relative to out/index.html, but the file was never copied, so the upgrade swaps a correct static bibliography for a failed one. Relative image srcs are not copied either.

The template, fonts and additional CSS are literals, and the hydrate script duplicates shell options. Only engine-internal assets (wasm, math font, hl grammars) are copied.

*Why ad hoc:* Resources are pulled by three unrelated mechanisms: NEED_IMAGES in the worker pull loop, executor fetch for bibliographies, and shell options for fonts. No layer can list a document's dependencies, so the exporter special-cases only the assets it already knows about.

*Proposed generalization (survey):* Produce a resource manifest. The executor and the pull loop record every resolved resource as `{ kind: 'image'|'bib'|'font'|'module', src, resolved: path|url }`. renderTsm returns `{ html, diags, ok, resources, lang, title }`.

The exporter then copies resources, preserving relative paths, or embeds resolved data in the hydration payload (for example a `bib: {id → entry}` script tag that the executor consults before fetching).

The page template becomes a user-overridable function `(parts) → html`. Hydration options are serialized from the same Config object (config-plumbing).

*Verifier:* Confirmed:
- renderTsm(source) without baseDir means bibliography paths resolve against cwd (executor.mjs:48-49);
- hydration fetches relative to document.baseURI (shell.mjs:362, executor.mjs:52-53), and refs.json is never copied;
- on failure the executor emits a bib-load error node and typesetting proceeds (executor.mjs:246-250), so the upgrade does replace a good static bibliography with an error block and '??' citations;
- relative images are not copied.

*Verifier notes:* A resource manifest from the executor and the pull loop is the right primitive. It also unifies the three fetch paths: NEED_IMAGES, the bibliography fetch, and declared fonts.

Embedding resolved data in the hydration payload (bib entries) avoids the copy-path problem entirely and is consistent with single-pass resolution.

### `render-runtime/anchor-decode-duplication` — Source-offset ↔ DOM mapping is re-implemented outside the runtime and is missing for code, rule, raw and caption lines

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-05**
- locations: `editors/vscode-tsm/media/preview.html:96-127`; `engine/src/render/typeset_html.cc:613-620`; `engine/src/layout/layout.cc:282-307`; `engine/src/layout/layout.cc:88-99`; `engine/src/layout/layout.cc:58-81`; `docs/design-decisions-v2.md §9`

The VS Code preview decodes `data-s0 + data-s` itself. It assumes `.tsr-para` s0 values are monotone and scans `:scope > [data-s][data-e]`.

The engine side is incomplete:
- Code rows, rules, raw blocks and float-caption rows carry no line span.
- srcBase therefore falls back to 0. tsrc on code/sidecar.tsm shows `data-pid="1" data-s0="0"` for the code block, so a double-click inside a code block jumps to offset 0.
- Float caption runs carry absolute offsets disguised as relative ones, so the figure paragraph's chunk changes after any upstream edit.
- The notes section's s0 (42 in notes/basic) is out of document order.

v2 §9 promised that upgrade records carry a 'source-offset ↔ line map'. As built, every consumer re-derives it.

*Why ad hoc:* An editor feature reverse-engineers the anchoring encoding instead of calling an API, and anchoring completeness differs per line kind.

*Proposed generalization (survey):* Every DLLine gets a source span from one helper shared by all line producers:
- code rows: the logical line's byte range from codeRuns offsets plus the node span;
- cells and captions: the union of their block spans.

The runtime exports `offsetAt(node, offsetInNode) → byte` and `elementsAt(byte) → Element[]` on the engine handle and in the behaviour ctx. Both are maintained from the structured paragraph result (shell-chunk-byte-coupling); the preview calls them instead of decoding attributes.

*Verifier:* Verified with tsrc:
- code/sidecar pid 1 has data-s0="0", with no data-s on code rows and none on sidecar rows (sidecar blocks carry no spans after extractSidecars);
- the float-figure caption runs carry data-s="210"/"746" inside paragraphs with data-s0="0", i.e. absolute offsets;
- the notes/basic notes section has s0=42 after s0=227;
- preview.html:96-127 decodes the attributes itself and assumes monotone s0 (`else break`).

The report misses one thing: run-level data-s is also not 1:1 (see missed items). Every block of a text node shares the node's span start.

*Verifier notes:* One span helper for all line producers, plus offsetAt/elementsAt on the engine handle, is sound. It realizes v2 §9's promised "source-offset ↔ line map". The helper must also produce per-run sub-spans, or offsetAt stays text-node-granular.

### `render-runtime/audit-hint-attributes` — data-ragged and data-cell are audit hints decided per feature in the serializer, with overloaded meanings

- kind: adhoc · severity: low · verdict: accurate · plan: **P3-07**
- locations: `engine/src/render/typeset_html.cc:220`; `engine/src/render/typeset_html.cc:304`; `engine/src/render/typeset_html.cc:324-325`; `runtime/src/main/audit.mjs:20`; `runtime/src/main/audit.mjs:52`; `runtime/src/main/audit.mjs:91`; `runtime/src/main/audit.mjs:103`

Code rows are always marked ragged (`// code rows are ragged by nature (the audit's justify checks do not apply)`), as are display-math lines and text lines whose unit is ragged or noGlue.

`data-cell` marks table cells, but also sidecar rows and float-caption rows, where it means 'secondary column, skip the stacking check'.

The audit special-cases math (`dataset.syn === 'math'`) and uses literal thresholds: 1px for the right edge, −2.5px for word-spacing, 0.5px for line integrity.

*Why ad hoc:* Production DOM attributes exist to steer one consumer, the audit. They are chosen per line kind and their names are overloaded.

*Proposed generalization (survey):* Layout sets `DLLine.just ∈ {Justify, Ragged, Centered, Fixed}` and `DLLine.track`: 0 for the main column, k > 0 for table columns, sidecar or float caption. They are serialized uniformly as data-just and data-track. Audit thresholds come from one exported constants module that the tests share.

*Verifier:* Confirmed: typeset_html.cc:220, :304, :324-325 and audit.mjs:20, :52, :91, :103.

The 1px right-edge threshold is not a magic constant. It is the documented contract ("error must only ever appear as ≤1px right-edge deviation", v2 §7). The −2.5px compression and 0.5px integrity thresholds are heuristics.

*Verifier notes:* Code rows being ragged is documented (code-design §7: "Wrapped rows emit data-ragged + data-join=none").

The accidental part is data-cell meaning three things: table cell, sidecar row and float caption. That traces back to FlowUnit.cells being overloaded (emit.h:89-90, :111).

A uniform data-just/data-track is trivial and sound.

### `render-runtime/duplicate-serializer-primitives` — Escaping, style→CSS mapping and anchor emission are duplicated across the two serializers and have already diverged

- kind: adhoc · severity: low · verdict: partly · plan: **P0-10, P1-02**
- locations: `engine/src/render/typeset_html.cc:9-20`; `engine/src/render/typeset_html.cc:44-90`; `engine/src/render/typeset_html.cc:310-318`; `engine/src/render/typeset_html.cc:449-452`; `engine/src/render/semantic_html.cc:7-18`; `engine/src/render/semantic_html.cc:40-50`; `engine/src/render/semantic_html.cc:58-119`; `engine/src/render/semantic_html.cc:137-140`

escapeHtml and esc are byte-identical copies.

The inline style mapping exists twice and differs. The semantic serializer:
- ignores sizeMul and uses only sizePx (inside a heading, #style({sizePx:20}) renders at 20px there versus 32px in typeset);
- formats with %g instead of fmtPx;
- escapes the whole style string rather than each component;
- maps bits to tags by priority, sup over strong over em, so sup+bold drops the bold.

Anchor emission also differs. Typeset puts the id on the first line of an anchored unit via lastAnchored. Semantic puts it on the element, except when the tight-list path swallows the element, which is how footnote ids dangle.

*Why ad hoc:* Two independent walkers each own their primitives; nothing is shared beyond appendf.

*Proposed generalization (survey):* Add `render/html_writer.h`:
- `HtmlWriter { void text(sv); void attr(k, v); void styleDecl(const Styling&, const Config&) /* single mapping from StyleDesc / style_css.def */; void anchor(StrRef label) /* via AnchorNamer */; }`

Both serializers use it. The semantic element choice comes from the RoleTable (semantic-role-switch), and anchors are emitted for every labelled node regardless of which element shape is chosen.

*Verifier:* Confirmed:
- byte-identical escapers (typeset_html.cc:9-20, semantic_html.cc:7-18);
- semantic ignores sizeMul (semantic_html.cc:78-82). Verified: `= Big #style({sizePx: 20})[small]` gives 32px in typeset and 20px in semantic;
- %g formatting;
- sup beats bold (:64-66). Verified: a bold note marker becomes plain `<sup>1</sup>`.

Not a divergence: "escapes the whole style string rather than each component". Escaping is per-character, so escaping components (typeset) or the concatenation (semantic) gives identical bytes. Neither escapes CSS-significant ';' (see missed item on style smuggling).

*Verifier notes:* A shared HtmlWriter with styleDecl and anchor helpers is sound and mechanical.

It should also be the only place attributes are assembled. Today the code-run snap path (:419) and the hyphen path (:484) hand-append attributes and produce duplicate attributes.

### `render-runtime/shell-feature-inventory` — The 'thin' main-thread shell hard-wires about nine feature concerns into createEngine/typeset

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-06**
- locations: `runtime/src/main/shell.mjs:16-85`; `runtime/src/main/shell.mjs:87-90`; `runtime/src/main/shell.mjs:199-209`; `runtime/src/main/shell.mjs:95-121`; `runtime/src/main/shell.mjs:126-197`; `runtime/src/main/shell.mjs:218-229`; `runtime/src/main/shell.mjs:251-309`; `runtime/src/main/shell.mjs:312-328`; `runtime/src/main/shell.mjs:346-356`; `runtime/src/main/shell.mjs:375-378`; `runtime/src/main/shell.mjs:421-478`; `docs/architecture.md §4.2`

Inventory (Q4):
1. CSS contract plus all feature CSS (TSR_CSS).
2. Math-font @font-face injection (ensureCss).
3. Declared webfonts: ensureFontFaces and a 4-second settle race.
4. Footnote hover popups.
5. NEED_IMAGES fallback: the worker's 'image-dims?' message is answered with an Image() on the main thread.
6. Paragraph patching: chunkParas/patchIn.
7. Upgrade records: swapIn with getBoundingClientRect per pid.
8. The container measure contract: font family and size, `--tsr-cjk-font`, the popup-only `--tsr-pop-font`, lang.
9. Copy and popups installed unconditionally on every typeset.
10. paginate/print: 666×995 defaults, A4 @page, Gecko fractional-px margin clamping, the `#tsr-print-root` injection into document.body.

architecture §4.2 planned separate shell/dom/copy/observe modules. As built, all of this sits in one closure, and none of it is optional or replaceable.

*Why ad hoc:* Feature behaviours accrete in the core API closure. Hosts cannot drop popups, swap the print strategy or add behaviours.

*Proposed generalization (survey):* Shell core = worker transport + container measure contract + the commit of structured paragraph results. Everything else registers:
- `Behavior { name, css?, install(ctx) → uninstall }` for DOM-side features: copyRebuild, refPreview, print (consuming PageSpec), devAudit.
- `HostCapability { name, handle(request) }` for worker callbacks that need the main thread: imageDims and later DOM-measure (architecture §4.3 needMainMeasure).

`createEngine({ behaviors = defaultBehaviors, capabilities = defaultCapabilities })`. Each module carries its own CSS, and core injects only contract.css. The worker protocol routes `capability?` requests generically instead of through a dedicated 'image-dims?' type.

*Verifier:* The inventory matches shell.mjs. Two minor points:
- copy is already its own module (copy.mjs), unlike popups, print and image dims;
- the "4-second settle race" is settleFonts' bounded paint wait (shell.mjs:114-121). It is not the measurement settle race that pages-design §1 eliminates.

*Verifier notes:* architecture §4.2 planned separate dom/copy/observe modules and "thin by design".

Behavior and HostCapability registries, with defaults reproducing today's behaviour, are sound and API-compatible. Behavior CSS ordering must be deterministic.

### `render-runtime/snap-kerning-duplicate-style` — Verbatim snap-kerning writes a second style attribute, so browsers drop its letter-spacing and the feature never takes effect

- kind: issue · severity: high · verdict: accurate · plan: **P0-10**
- locations: `engine/src/render/typeset_html.cc:412-422`; `engine/src/render/typeset_html.cc:80-90`; `engine/src/layout/layout.cc:151-167`

In the code-run loop, `runStyleAttr(out, cst, cfg, strs)` already writes ` style="font-size:…"`. Every code style has sizeMul = codeScale = 0.85, so styleInto always emits font-size.

The snap branch then appends ` data-snap="1" style="letter-spacing:…"`. HTML parsers keep the first of two duplicate attributes, so the snap letter-spacing that layout budgeted for (layout.cc:151-167, where the column budget switches to atom units) is never applied. Rows can therefore overrun or underfill the grid that layout computed.

No test exercises verbatimSnapKerning (a repo-wide grep finds only plumbing). The text-line path merges extra styles correctly through openRun; the code path does not.

*Verifier:* Confirmed:
- runStyleAttr (typeset_html.cc:416) always writes style="font-size:…", because code styles compose sizeMul = codeScale = 0.85 (emit.cc:551, :624; config.h:51);
- then `data-snap="1" style="letter-spacing:…"` is appended (:418-422);
- an HTML parser keeps the first duplicate attribute;
- the only references to verbatimSnapKerning are plumbing (no test).

A second bug sits in the same feature: with snap active, layout recomputes `cols = lineWidth / atomSu` (layout.cc:162) from the full lineWidth, not lineWidthCode. A sidecar's code column budget therefore ignores the sidecar.

*Verifier notes:* The root cause is ad hoc attribute assembly in a second run-writing path, not going through openRun or a shared writer. The implied fix (one HtmlWriter that merges style declarations and rejects duplicate attributes) is sound. Duplicate-attribute checks over goldens would catch the whole class.

### `render-runtime/semantic-footnote-ids-dangle` — Semantic/static pages: every footnote link points to a nonexistent id, and the goldens lock this in

- kind: issue · severity: high · verdict: accurate · plan: **P0-09, P0-10**
- locations: `engine/src/render/semantic_html.cc:205-212`; `engine/src/resolve/resolve.cc:355-387`; `test/golden/notes/basic.semantic.txt`

Notes are built as list items containing one labelled para (`fn-n`, resolve.cc:368-371). The semantic serializer's tight-list shortcut (`if (b->kind == Kind::para && !sub && k->kids.size() == 1) inlineKids(b)`) never calls attrs() for that para, so `id="tsr-fn-n"` is never emitted while the markers still link to `#tsr-fn-n`.

A scan of test/golden/*/*.semantic.txt finds dangling hrefs in all three footnote fixtures (notes/basic, notes/cjk-glue, notes/explicit); typeset goldens have none. Footnotes on the static export, no-JS readers and the first paint are therefore broken, and the shell popup cannot find bodies there.

Suggested invariant for the native golden runner: every internal `href="#x"` has a matching `id="x"`, for both serializers.

*Verifier:* Verified by scanning goldens: dangling hrefs in all three notes/*.semantic.txt (tsr-fn-1…5) and none in *.html.txt.

The cause is the tight-item shortcut (semantic_html.cc:209-210), which bypasses attrs() for the labelled para built at resolve.cc:375-376. This contradicts notes-design "As built" ("paragraphs honor `label` as anchors") and v2 §11.1.

*Verifier notes:* The fix is to emit anchors for every labelled node regardless of element shape, for example by putting the id on the <li> when its para is inlined. An anchor-closure invariant in the golden runner is the right guard, and it would also catch the paged-anchor bug.

### `render-runtime/copy-drops-blank-code-lines` — Copy collapses blank lines inside code blocks

- kind: issue · severity: medium · verdict: partly · plan: **P3-07**
- locations: `runtime/src/main/copy.mjs:29`; `engine/src/render/typeset_html.cc:385-394`

An empty code line renders as an empty `.tsr-line` (verified: tsrc on code/tsm-hl.tsm at top:27px and top:108px). contentTextFromRange then does `if (lineText === '') continue;` before appending any separator, so 'a\n\nb' copies as 'a\nb'. The same happens for any empty line, such as a blank table cell row.

*Verifier:* Verified with tsrc: an empty code line renders `<div class="tsr-line" …></div>`, and copy.mjs:29 `continue`s before pushing any separator, so 'a\n\nb' copies as 'a\nb'.

The extension to "a blank table cell row" is wrong. Empty cells and empty text lines produce no LineBox at all (layout.cc:432, :501, `if (lo >= hi) continue`), so only code rows are affected.

*Verifier notes:* Subsumed by the explicit-separator design: never skip an in-range line, and always append its separator.

### `render-runtime/sidecar-hyphen-missing` — A sidecar row broken at a hyphen point shows no hyphen glyph

- kind: issue · severity: medium · verdict: accurate · plan: **P1-17**
- locations: `engine/src/layout/layout.cc:316-358`; `engine/src/layout/layout.cc:458-476`; `engine/src/render/typeset_html.cc:481-490`

Sidecar text is emitted with hyphenation enabled (`inlineWalk(c2, tmp, {})`, emit.cc:592-595). The sidecar line loop in layout never sets `sl.endsWithHyphen`, whereas the table-cell loop does (layout.cc:476). The renderer emits the '-' only `if (i == l.blockEnd - 1 && l.endsWithHyphen)`, so the word is split with no hyphen. The cell rows also get no join (see copy-line-separators).

The root cause is in layout: there are four copy-pasted line producers (text, table cell, sidecar, float caption) that disagree on endsWithHyphen, join and srcSpan.

*Verifier:* Verified with tsrc on a scratch sidecar (`/// internationalization considerations …`, width 240): rows read 'interna' / 'tionaliza' / 'tion con' / 'sidera' with no hyphen glyph and no data-s.

The sidecar is emitted with a default ICtx, so hyphenation is on (emit.cc:595). The sidecar line producer never sets endsWithHyphen (layout.cc:344-357), while table cells do (:458, :476). Float captions are immune only because they set noHyphen (emit.cc:806-807).

*Verifier notes:* This is the correct diagnosis: four copy-pasted line producers. One makeLine(blocks, lo, hi, width, role) helper, owning endsWithHyphen, join/sep and span, is sound.

### `render-runtime/sidecar-dropped-in-semantic` — Sidecar comments are removed from the no-JS / static page

- kind: issue · severity: medium · verdict: accurate · plan: **P3-23**
- locations: `engine/src/render/semantic_html.cc:241-252`; `engine/src/api/doc.h:84-160`

extractSidecars strips the `/// …` text from the code body at ingest (doc.h:84-160). The semantic codeblock path then skips the resulting `group{role:sidecar-lines}` child as "display-layer only". The no-JS page and the static export lose the comments entirely, including their math, links and refs (verified with tsrc --stage=semantic on code/sidecar.tsm).

*Verifier:* Verified with tsrc --stage=semantic on code/sidecar: the <pre> has no comment text, and the resolved @base ref inside the sidecar is gone. The skip is explicit (semantic_html.cc:242-246, "display-layer only (verbatim §5)").

*Verifier notes:* This is a deliberate, documented choice. verbatim-design §5 calls the sidecar "display content", with the copy contract OPEN. It still loses author content, including math, links and refs, from the no-JS page and the static export.

The report names no target shape. A concrete one: render each logical line's sidecar inline as `<span class="tsr-sidecar">/// …</span>` after the code text, or as an aligned `<aside>`. That matches whatever copy contract is chosen (' /// ' + content text) and should be decided in the same place.

### `render-runtime/popup-breaks-patch` — A visible footnote popup disables the incremental patch path

- kind: issue · severity: low · verdict: accurate · plan: **P3-06**
- locations: `runtime/src/main/shell.mjs:162-165`; `runtime/src/main/shell.mjs:279-283`

The popup is appended to `.tsr-doc` (shell.mjs:162-165), which is exactly `container.firstElementChild`, the patch root. patchIn requires `root.children.length === prev.chunks.length` (shell.mjs:282-283), so every update while a popup is open falls back to a full innerHTML swap. In the editor preview that means every keystroke while hovering a marker.

*Verifier:* Confirmed: pop is appended to container.querySelector('.tsr-doc') (shell.mjs:162-165), which is container.firstElementChild. patchIn bails when root.children.length !== prev.chunks.length (:282-283). The subsequent full swapIn also silently destroys the popup DOM while `current` still references the old marker.

*Verifier notes:* The overlay should be a sibling outside the patch root, which is part of the behaviour-registry proposal.

### `render-runtime/host-line-height-leak` — The host page's CSS line-height leaks into vertical geometry inside lines

- kind: issue · severity: medium · verdict: partly · plan: **P3-19**
- locations: `runtime/src/main/shell.mjs:347-356`; `runtime/src/main/shell.mjs:23`; `engine/src/render/typeset_html.cc:326-357`

The shell pins font-family and font-size on the container as "the measure/render contract" (shell.mjs:347-350). It passes lineHeight to the engine but never sets CSS line-height, and TSR_CSS sets none on `.tsr-doc` or `.tsr-line`.

Text lines have no explicit height or line-height (only code rows and display math do), so each text baseline sits at top + half-leading of whatever line-height the host page inherits. document-model §8 says the baseline is at top_i + ascent_i. With the exporter's or preview's default line-height (normal), prose sits about 3px higher in its 27px slot than code rows, which are centred via line-height. Its offset relative to engine-placed rules, images and the eqno (`top:50%`) also varies with the host.

Fix: the serializer emits `line-height:{l.height}px` per line, or the contract sets `.tsr-doc { line-height: <cfg.lineHeight> }`.

*Verifier:* The mechanism is confirmed. The shell pins only font-family, font-size and lang (shell.mjs:349-356). TSR_CSS sets no line-height on .tsr-doc or .tsr-line (:17-23). Text lines carry no height or line-height (typeset_html.cc:326-358), so a host `line-height: 1.8` shifts every text baseline by the half-leading relative to engine-placed rules, images and the eqno.

The comparison with code rows is a separate, deliberate choice. Code rows are centred via line-height (typeset_html.cc:344-348: "baseline sits centred in the row"). With line-height: normal, prose matches document-model §8 ("Baseline of line i sits at top_i + ascent_i"), so the ~3px prose-vs-code offset is not caused by the host.

*Verifier notes:* Both proposed fixes contradict §8. Emitting `line-height:{l.height}px` or `.tsr-doc { line-height: cfg.lineHeight }` moves the text baseline down by half the leading, which the engine does not model.

Pin the contract instead: `.tsr-line { line-height: normal }` (or an explicit per-line ascent-derived value), so the baseline stays at top + ascent independent of the host. Then decide separately whether code rows should follow §8 or §8 should change.

### `render-runtime/paged-gutter-clipping` — Print sheets clip the line numbers of code blocks at the measure's left edge

- kind: issue · severity: medium · verdict: accurate · plan: **P3-16**
- locations: `engine/src/render/typeset_html.cc:749`; `runtime/src/main/shell.mjs:30-31`

`.tsr-sheet` is `position:relative; overflow:hidden`, and markers are positioned `right:100%` of their line. Code rows at indent 0 therefore put their line numbers at negative x, where the sheet clips them, so printed listings lose their numbers. List markers survive only because list indent leaves room.

*Verifier:* By code: `.tsr-sheet` has `position:relative; overflow:hidden` (typeset_html.cc:749); markers are `right:100%` (shell.mjs:30); top-level code rows have left = u.indent = 0 (layout.cc:295). It is not exercised by the paged golden, which has no line numbers.

*Verifier notes:* This duplicates marker-gutter. An explicit marker x plus a PageSpec bleed fixes it.

### `render-runtime/trailing-float-and-gap-drift` — A trailing float overflows the document box; screen and layout disagree on paragraph y

- kind: issue · severity: low · verdict: accurate · plan: **P3-16**
- locations: `engine/src/layout/layout.cc:604-610`; `engine/src/render/typeset_html.cc:622-628`

Layout extends docHeightSu with floatBottomAbs so that a trailing float is not clipped (layout.cc:609). The screen serializer never uses docHeightSu: the float's `.tsr-para` has height 0, so content after `.tsr-doc` overlaps the float.

Separately, the margin-bottom between paragraphs is the unrounded `paraSpacingEm*baseSizePx` (21.6px), while layout's fr.y uses the su-rounded 21.594px. The returned heightPx and the DOM height drift by about 0.006px per paragraph, and the paged output (which uses fr.y) differs from the screen output.

*Verifier:* Confirmed:
- docHeightSu includes floatBottomAbs (layout.cc:609-610), while renderTypeset never reads docHeightSu and the float's paragraph has height 0 (layout.cc:85; tsrc figure/float pid 1 shows height:0px);
- margin-bottom = cfg.paraSpacingEm*baseSizePx unrounded (typeset_html.cc:627), versus paraGap = suRoundPx(…) (layout.cc:14). In su that is 21.6 vs 21.59375px, about 0.006px per paragraph;
- paged uses fr.y (typeset_html.cc:757).

*Verifier notes:* A single vertical authority, the su-rounded gap from layout, fixes the drift. The document needs its extent (min-height = docHeight) for the trailing float.

### `render-runtime/typeset-a11y` — The typeset DOM has no accessibility semantics

- kind: issue · severity: medium · verdict: accurate · plan: **P3-27**
- locations: `engine/src/render/typeset_html.cc:604-636`; `engine/src/render/typeset_html.cc:146-172`; `docs/design-decisions-v2.md §8`

Headings, lists, tables and figures are anonymous absolutely positioned divs. Math is Euler glyph soup with no `role="math"` or aria-label, even though data-src carries the source. List and gutter markers are user-select:none. There is no ARIA anywhere in engine/ or runtime/.

v2 §8 lists 'aria-hidden lines + sr-only clean paragraph text' as an optional mitigation, and it is not implemented. The RoleTable proposed in the adhoc findings could carry ariaRole/level per role, and math DLBoxes could carry an aria-label taken from copyText.

*Verifier:* grep finds no aria- in engine/src or runtime/src. v2 §8 lists 'aria-hidden lines + sr-only clean paragraph text' as an optional mitigation, and it is unimplemented.

*Verifier notes:* This is a missing feature rather than ad hoc special-casing. A RoleTable ARIA role alone does not fix screen-reader output: lines are absolutely positioned fragments with hyphen glyphs and split words.

The documented mitigation needs an engine-produced clean content text per paragraph. That should be the same projection the copy policy computes (SynKind/copyText + separators), so a11y and copy cannot drift. Math can expose its copyText as the aria-label.

### `render-runtime/error-render-divergence` — Error nodes do not follow the §9.1 contract in typeset output

- kind: issue · severity: low · verdict: accurate · plan: **P3-16**
- locations: `engine/src/emit/emit.cc:94-106`; `engine/src/emit/emit.cc:763-772`; `engine/src/render/semantic_html.cc:161-167`; `docs/document-model.md §9.1`

document-model §9.1 requires `error` to render as `<span|div class="tsr-err" title=…>`. In the typeset path, emit instead turns errors into code-styled text runs '⚠ message' (emit.cc:94-106, :763-772). These have no class, so they cannot be themed, and no data-syn, so the fabricated warning is copied as content. The semantic serializer follows the contract, so the two outputs diverge.

*Verifier:* Confirmed:
- emit.cc:94-106 turns errors into a CLS_CODE text run '⚠ message', with no class and no data-syn, so it is copied as content;
- emit.cc:763-772 is the block form;
- semantic follows §9.1 (semantic_html.cc:161-167, :348-356).

*Verifier notes:* This should become a SynKind::Error run/line carrying class tsr-err and a title through the same run key. It should be themeable and governed by copy policy.

### `render-runtime/hyphen-in-link-or-ref` — A line-final hyphen inside a link or ref is rendered outside the <a>, and gets a duplicate data-syn inside refs

- kind: issue · severity: low · verdict: accurate · plan: **P0-10**
- locations: `engine/src/render/typeset_html.cc:481-490`; `engine/src/render/typeset_html.cc:437-471`

The hyphen branch calls `openRun(styles.get(b.style), 0 /*url*/, b, …)`, so the hyphen leaves the link and is not underlined or clickable. It then string-inserts `data-syn="hyphen"` before '>'. When the block also has BF_REF, openRun has already written `data-syn="ref"`, producing a duplicate attribute, and the first one wins.

*Verifier:* Confirmed:
- openRun is called with url 0 for the hyphen (typeset_html.cc:483), so it renders outside the <a>;
- `out.insert(out.size()-1, " data-syn=\"hyphen\"")` (:484) can follow an already-written `data-syn="ref"` (:454), giving a duplicate attribute.

It is rare in practice. Refs with URLs are usually short.

*Verifier notes:* Fixed by the shared run key and writer: the hyphen inherits its run's link and run key, and syn becomes a single attribute.

### `render-runtime/normative-doc-drift` — The normative serializer and runtime docs contradict the as-built behaviour and each other

- kind: issue · severity: low · verdict: accurate · plan: **P3-07**
- locations: `docs/document-model.md §9.1-§9.2`; `docs/architecture.md §4.2-§4.3`; `docs/design-decisions-v2.md Appendix D`; `docs/pages-design.md §2`; `engine/src/layout/layout.h:20`

Mismatches between docs and build:
- document-model §9.1 requires `contain: layout style paint`. The CSS deliberately omits paint (shell.mjs:24-25 explains why), but the doc was never updated.
- §9.2 maps `term→dl`, `collect→nav|section`, `styled→span[class]`; none of these are built.
- architecture §4.2 says copy rebuilds 'selection → data-s/e offsets → clean source text', and v2 Appendix D says 'copy listener rebuilding clean text from source offsets'. Both contradict §9.3, which says copy never uses source offsets.
- architecture §4.3's `paragraphs{…}` protocol and dom.ts/observe.ts modules do not exist.
- pages-design §2 describes a hidden print iframe; the shell uses a parent print root.
- The LineBox `special` comment lists 0-4, but 5 is used.

Tests are written against whichever text the implementer read.

*Verifier:* All confirmed:
- document-model §9.1 `contain: layout style paint` vs shell.mjs:23-25;
- §9.2 term→dl, collect→nav|section, styled→span[class] are unbuilt;
- architecture §4.2 copy.ts "selection → data-s/e offsets → clean source text" and v2 Appendix D "from source offsets" contradict v2 §8/§9.3;
- the §4.3 paragraphs protocol and dom.ts/observe.ts do not exist;
- pages-design §2 iframe vs the parent print root (shell.mjs:429-433), not recorded in pages §5 deltas;
- layout.h:20 "0-4".

Also drift: pages-design §2's heading keep is "≥2 lines", which renderPages does not implement.

*Verifier notes:* Documentation hygiene. Contract tests (anchor closure, no duplicate attributes, copy simulator) are the durable fix.

### `render-runtime/swap-whole-container` — Upgrades and relayout replace the whole container

- kind: issue · severity: low · verdict: accurate · plan: **P3-05**
- locations: `runtime/src/main/shell.mjs:312-328`; `runtime/src/main/shell.mjs:412-420`

swapIn does `container.innerHTML = html` for the first upgrade, relayout and the patch fallback. User selection, focus and any host-added DOM inside the container are lost on every resize relayout (the VS Code preview relayouts on panel resize). That is stronger atomicity than the robustness contract needs, but not the per-paragraph swap v2 §9 describes, and it makes scroll anchoring entirely the caller's problem even for unchanged paragraphs.

*Verifier:* Confirmed: swapIn does `container.innerHTML = html` (shell.mjs:316), used by the initial upgrade (:372), relayout (:416) and the patch fallback (:402).

*Verifier notes:* The full swap satisfies the robustness contract: atomicity is at least per paragraph (v2 §7 rule 5), and it is the simplest correct choice. It does fall short of v2 §9's per-paragraph swap and loses selection, focus and host DOM on every resize relayout.

The structured per-pid commit subsumes it at little cost.

### `render-runtime/missed:0` — Run coalescing ignores the synthetic/ref flag, so copy deletes real prose next to citations and copies synthetic brackets

- kind: missed · severity: high · verdict: verifier-found · plan: **P0-10, P4-01**
- locations: `engine/src/render/typeset_html.cc:584-590`; `engine/src/render/typeset_html.cc:546-552`; `engine/src/render/typeset_html.cc:449-456`; `engine/src/emit/emit.cc:71-93`; `engine/src/resolve/resolve.cc:259-267`; `test/golden/cite/basic.html.txt`

Both run-coalescing loops group blocks only by (style, linkUrl). openRun, however, stamps data-syn="ref", id= and data-s from the FIRST block of the run.

Resolver-synthesized blocks without a URL are therefore merged with neighbouring prose. These are citation brackets and separators from resolveCite, which all carry BF_REF via emit.cc:79, and unresolved "??".

Verified with tsrc on test/fixtures/cite/basic:
- `<span class="tsr-r" data-s="0">Optimal line breaking is due to Knuth and Plass [</span>`: the synthetic "[" is copied as content.
- `<span class="tsr-r" data-syn="ref">]; hyphenation patterns</span>`
- `<span class="tsr-r" data-syn="ref">] once more keeps its number; the uncited entry is omitted.</span>`

copy.mjs:19 skips such runs entirely, deleting real sentences from the clipboard. Four instances are locked into cite/basic.html.txt and one into cite/unknown-diag.html.txt.

The same first-block rule silently drops an anchorId that is not on the first block, and mislabels data-s.

*Proposed generalization (survey):* Define one RunKey = (StyleId, link, SynKind, anchor) per block at emit, with SynKind derived from BF_REF/BF_HYPHEN/etc., and make a key change the only run-boundary predicate in the serializer (and in the DisplayList Run item). data-syn and id then describe the whole run by construction.

emit's kern-context tagging (emit.cc:870-872) and layout's junction-kern accounting should consult the same predicate, so the 'one shaped run' invariant has one definition.

### `render-runtime/missed:1` — Paged serializer drops and duplicates anchor ids via shared lastAnchored state

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-18**
- locations: `engine/src/render/typeset_html.cc:752`; `engine/src/render/typeset_html.cc:211-216`; `engine/src/render/typeset_html.cc:265-270`; `engine/src/render/typeset_html.cc:311-317`; `engine/src/render/typeset_html.cc:612`; `docs/design-decisions-v2.md §11.1`

renderLineBox decides 'first line of an anchored unit' through caller-owned mutable state, `lastAnchored`, keyed by unitIdx alone. renderTypeset resets it per paragraph (:612). renderPages keeps one per sheet (:752), shared by bands from different paragraphs.

Verified by running engine/build/tsr_tests --update on a scratch fixture root (repo untouched): `= Alpha`, `== Beta`, two labelled display formulas, then `@eq-a and @eq-b`, at 240px sheets. The paged output contains only id="tsr-h-1". The ids tsr-h-1.1, tsr-eq-a and tsr-eq-b are missing, while href="#tsr-eq-a" and href="#tsr-eq-b" remain. Every anchored unit whose unit index equals the previous anchored unit's on the same sheet loses its id.

Conversely, the per-sheet reset re-emits the id of an anchored unit that continues onto the next sheet (duplicate ids; by code).

Internal links in print-to-PDF break, contrary to v2 §11.1 ("labels become DOM anchors"). The repo's paged golden misses it because its anchored units land on different sheets.

*Proposed generalization (survey):* Anchors are layout data, not serializer state. Layout sets LineBox.anchor (DLLine.anchor) on exactly the first line of each anchored unit, and serializers emit it statelessly in any order or subset (screen, paged, cached paragraphs).

Add an anchor-closure invariant to the golden runner: every internal href="#x" has exactly one id="x", across the html, paged and semantic outputs.

### `render-runtime/missed:2` — Inline-style values are raw CSS strings: document values smuggle declarations that break the nowrap/measurement contract

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-02**
- locations: `engine/src/render/typeset_html.cc:44-71`; `engine/src/render/semantic_html.cc:68-91`; `engine/src/render/semantic_html.cc:106-110`; `runtime/src/worker/executor.mjs:131-136`; `engine/src/model/model.h:29-35`; `engine/src/measure/measure.h:60-70`; `docs/document-model.md §3`

Styling.fontFamily and Styling.color are opaque strings that both serializers paste into style="" with HTML escaping only. HTML escaping does not neutralize ';'.

Verified with tsrc on `#style({color: "red;letter-spacing:5px;white-space:normal", font: "x;font-size:40px"})[styled words here]`. Both the typeset and the semantic output emit `style="font-family:x;font-size:40px;color:red;letter-spacing:5px;white-space:normal"`.

This breaks three things:
- It re-enables browser wrapping inside an engine-owned nowrap line (v2 §7 rule 1).
- It injects letter-spacing, which document-model §3 deliberately refuses to expose ("letter-spacing is the engine's CJK justification channel").
- It paints a font size the measurer never saw, because describeStyle hands the same string to canvas as a family name.

The report's strength "exactly one unescaped path" holds for HTML context only.

*Proposed generalization (survey):* Use typed InlineStyle values, validated once at ingest:
- color: CSS colour grammar, or a reference to a `var(--tsr-…)` theme token;
- fontFamily: family-list grammar, with no `;{}`;
- sizePx: a number.

Invalid values produce a diagnostic and are dropped. Serializers emit declarations only from typed fields through one writer (HtmlWriter.styleDecl). Measurement and paint read the same validated value. Token colours become class or token references rather than strings.

### `render-runtime/missed:3` — Run-level data-s is the text-node start, not the run start (§9.1 1:1 anchoring not met)

- kind: missed · severity: medium · verdict: verifier-found · plan: **P4-03**
- locations: `engine/src/emit/emit.cc:150`; `engine/src/emit/emit.cc:184`; `engine/src/emit/emit.cc:322`; `engine/src/emit/emit.cc:388`; `engine/src/render/typeset_html.cc:455-456`; `editors/vscode-tsm/media/preview.html:96-103`; `docs/document-model.md §9.1`

Every LinebreakBlock produced from a text node gets `b.span = n->span`, and openRun writes `first.span.start`. A run's data-s is therefore the start of its source text node.

Verified with tsrc:
- In figure/float, every CJK run of pid 2 carries data-s="0".
- In notes/basic, ' contin' and the next line's 'ues after the marker' both carry data-s="88".

document-model §9.1 says runs carry data-s "when they map 1:1 to a source slice", and its example shows per-run offsets 120/131. As a result, the VS Code preview's dblclick-to-source (closest [data-s]) jumps to the node start, and no consumer can map a caret or selection to source at run granularity.

*Proposed generalization (survey):* Emit records exact per-block byte sub-spans: the node span start plus the word's byte offset when the node text is a contiguous source slice. Escape-bearing or synthetic text is flagged so it carries data-syn or no offset, as §9.1 says.

One span helper is shared by all line producers. The runtime exposes offsetAt/elementsAt so consumers stop decoding attributes.

### `render-runtime/missed:4` — Editing fast path reports no upgrade records

- kind: missed · severity: low · verdict: verifier-found · plan: **P3-05**
- locations: `runtime/src/main/shell.mjs:398-405`; `runtime/src/main/shell.mjs:312-328`; `docs/design-decisions-v2.md §9`

update() sets `ups = []` whenever patchIn succeeds and calls onUpgrade([]). The dominant editing path therefore reports no old/new rects, even for the edited paragraph whose height changed.

v2 §9 makes this callback the engine's whole scroll-anchoring contract ("the engine provides the information"), but only the full-swap path honours it. What a caller receives depends on which internal path the shell happened to take.

*Proposed generalization (survey):* Use one per-pid commit routine for upgrade, relayout and update. It always returns {pid, old, new} for each replaced pid, and is driven by the structured paragraph protocol rather than by HTML string chunking.

### `render-runtime/missed:5` — Copy falls back to native copy for synthetic-only selections, copying exactly what §9.3 says to skip

- kind: missed · severity: low · verdict: verifier-found · plan: **P3-07**
- locations: `runtime/src/main/copy.mjs:52`; `runtime/src/main/shell.mjs:372-378`

installCopy returns to native copy whenever the rebuilt text is empty, citing "(e.g. semantic phase)". The handler is installed only after the typeset swap, so the semantic-phase case never occurs.

The fallback actually fires when the selection contains only synthetic runs: a resolved ref, an eqno, list markers or blank code rows. Native copy then emits '§1' or '图 1'. Selecting a ref alone copies it, while selecting it together with its neighbours omits it.

*Proposed generalization (survey):* Fold this into an engine-declared copy policy (SynKind + data-copy). Inside typeset containers the handler always takes over and emits the policy text or ''. Native copy is never a semantic fallback.

## math — Math: syntax, parser, symbol/operator tables, box model, fonts

<details><summary>Design summary (as audited)</summary>

Math is a string carried through the engine and parsed only at emit. The inline lexer cuts out `$…$` as a verbatim island (engine/src/inline/inline.cc:266-310). If there is whitespace just inside both fences, the formula is display math. A trailing ` <id>` is taken as a label. Codegen emits `mathinline(src)`, or `mathblock(src, label)` when one display formula is the whole paragraph (engine/src/codegen/codegen.cc:96-117). The JS constructors pass the string through ops unchanged (runtime/src/worker/executor.mjs:218-220).

At emit, `layoutMathSegments` (inline) and `layoutMathFormula` (display) run a hand-written lexer and recursive-descent parser (math.cc:106-525). The parser has 12 token kinds and 8 MNode kinds (Run, Atom, Text, Script, Frac, Group, BigOp, Call). A Layouter then maps the tree to an arena MathBox tree (math.cc:528-1210). MathBox has four kinds (Glyph, Rule, HBox, Spacer), uses integer su, and kids sit at dx and baseline-relative dy.

Layout reads these OpenType MATH tables from engine/gen/euler_math.h:
- 56 constants
- 2023 glyph records (advance, ink extents, italic correction, top-accent attachment)
- 58 vertical and 52 horizontal variant chains, plus assemblies

tools/mathc.py generates that header from fonts/Euler-Math.otf. The same script also hand-curates the 322-entry operator dictionary (name → cp, TeX class, flags Large/Stretchy/Limits/TextOp/Accent) and the atom-class enum. TeX-side rules live in math.cc: the 8×8 spacing matrix, Bin demotion (Rules 5–6) and the style transition arrays.

Multi-letter names, text operators and "quoted" runs are text-font leaves. The host measures them through a dedicated pull channel (Doc::mathTextMissing), and any missing metric forces a whole-document re-emit.

Inline formulas are cut at top-level Rel/Bin atoms into unbreakable segments. Each segment becomes a `LinebreakBlock{math = MathBox*}`, with synthesized glue and one of three config penalties between segments. Display formulas become `FlowUnit::K::Math` and then `LineBox special=4`. The resolver numbers labelled displays and writes "(n)" into ArgK::name.

The renderer flattens the box tree into absolutely positioned `.tsr-mg` glyph spans, painted by codepoint in a bundled @font-face. The baseline is pinned with an hhea-height line-height. Rules are painted divs, and `.tsr-eqno` is placed by CSS. Copy is handled by `data-syn=math` plus `data-src`.

Everything inside a formula is closed to extension. The vocabulary, the constructs and the font are fixed at C++/Python build time. Documents and JS cannot define symbols, operators, functions or macros, cannot splice values in, and can only produce formulas as source strings.


Strengths:

- The box tree is a pure function of (source, sizePx, display) over integer su and precompiled MATH data, so `--stage=mathbox` goldens run natively with no browser (math-design §4). Text-font names are the only measured input, and they go through the ordinary pull loop instead of being guessed.
- Each construct is a short transcription of OpenType MATH rules with a Typst/KaTeX/TeXbook citation: scripts with joint collision resolution (math.cc:815-879), fractions (882-914), radicals (1030-1071), limits (956-985), and assemblies with uniform connector overlap (639-699). The style algebra is four 8-entry tables (math.cc:36-39).
- A build-time gate turns a font property into a build failure: every variant or assembly glyph must be reachable through cmap, or the build fails (tools/mathc.py:311-322). The STIX swap test was run and its negative result recorded honestly (math-design §13).
- Errors are total and non-fatal. A parse error degrades the formula to one upright text box plus a diagnostic (math.cc:1253-1256), so the line model always receives exactly one box.
- Atom-class spacing and Bin demotion are integrated with line breaking and CJK rules: segments with a segment-final Bin stay Bin (startEdge/endEdge in assemble, math.cc:753-797), 禁则 extends to formulas, and CJK–formula boundary glue is added.
- Several rendering choices remove browser variance by construction: rule boxes for fraction, radical and over/under bars; spacing (not combining) accent forms because shaping fallbacks differ between browsers (math-design §13); and one inline-block per formula, aligned with vertical-align: -desc.
- Copy-as-source keeps the clipboard semantic (`$src$` on the first segment only), and positioned glyph soup never leaks into the copied text.

</details>


### `math/call-construct-string-dispatch` — Structural constructs are dispatched by hard-coded name strings in both the parser and the layouter

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-24**
- locations: `engine/src/math/math.cc:231-235`; `engine/src/math/math.cc:437-439`; `engine/src/math/math.cc:473-500`; `engine/src/math/math.cc:1178-1209`; `engine/src/math/math.cc:1183`; `engine/src/math/math.cc:1189-1192`; `engine/src/math/math.cc:1102-1147`; `engine/src/math/math.cc:919-948`; `tools/mathc.py:241-245`

Two string lists must stay in sync.

1. `isCallName` decides at parse time which words are calls: `w == "sqrt" || w == "root" || w == "frac" || w == "binom" || w == "abs" || w == "norm" || w == "floor" || w == "ceil" || w == "overline" || w == "underline"`.
2. `layoutCall` is an if-chain on `n->txt`:
   - "overline"||"bar" → layoutHRule
   - "underline"
   - accent flag
   - "sqrt", "root"
   - "abs"→fencedRun('|','|'), "norm"→U+2016, "floor"→U+230A/B, "ceil"→U+2308/9
   - "frac" && kids>=2
   - "binom" && kids>=2
   - otherwise a 'math-unknown-call' warning.

A third list is the ACCENT rows in mathc.py. `bar` is intercepted by name in layout, so its dictionary cp 0xAF is dead data.

There is no arity model. Verified:
- `frac(a)` and `binom(n)` fall through to "unknown construct 'frac'" and render the literal word.
- `abs(x, y)` and `hat(a, b)` silently drop the extra arguments.
- `sqrt x` (no parentheses) is a parse error that degrades the whole formula.

`binom` re-implements fencing: it always calls centerOnAxis(stretchVert) (1133-1134), while fencedRun keeps the natural glyph when it is tall enough (936). So `binom(n,k)` and `(n/k)` place their parentheses differently.

Adding a construct means editing isCallName, layoutCall and possibly mathc.py, then regenerating the font header. Users cannot add one at all.

*Why ad hoc:* The syntax layer knows layout vocabulary by name. The layout dispatch is a closed if-chain with no function abstraction. So the governing principle (design-decisions-v2.md:107, 'every syntax form is sugar for a constructor') has no realization inside math. This is not a documented trade-off: math-design §10 only says 'hand-rolled Pratt parser … house style'.

*Proposed generalization (survey):* Give the math layout layer a MathFn registry (engine/src/math/fn.h):

`struct MathFnSpec { std::string_view name; u8 minArgs, maxArgs; std::span<const std::string_view> named; u8 cls; ShedMode argShed; MathBox* (*layout)(MathCtx&, const MCall&, u8 style); };`

The table is a sorted static array of built-ins plus the per-document overlay (see closed-vocabulary).

- **Parser:** `Word '('` always produces `MNode::Call{name, positional args, named args 'k: v'}`. The parser never consults names.
- **Layout:** `MathFnTable::find(name)` → arity check → diagnostic `math-arity` carrying the argument's sub-span; an unknown name → `math-unknown-fn`, degrading only that call.
- **Built-ins become data rows:**
  - Accent rows `{name, cp, mode: Glyph|OverRule|UnderRule|StretchH}`; bar, overline and underline are rows.
  - Fence rows `{abs:'|','|'}`, `{norm:'‖','‖'}`, `{floor:'⌊','⌋'}`, `{ceil:'⌈','⌉'}`, all lowered to one generic `lr(open, body, close)`.
  - `binom(a,b) := lr('(', stack(a,b), ')')`.
  - frac, sqrt, root, attach, op, stack, mat, cases, underbrace, cancel and color are rows.
- **Sugar desugars in the parser to calls:** a/b→frac, x^a_b→attach(x,t:a,b:b), (…)→lr, "…"→text, Name→name/op, AA→bb(A), !rel→not(rel), x'→attach(x,t:prime).

--stage=ast/mathbox would then show one node shape for all of these.

*Verifier:* All cited code matches: isCallName at engine/src/math/math.cc:231-235, the layoutCall if-chain at math.cc:1178-1209, and binom's unconditional centerOnAxis(stretchVert) at math.cc:1133-1134 versus fencedRun's natural-glyph early return at math.cc:936. I re-ran tsrc --stage=mathbox on hand-encoded .ops: `frac(a)` renders the glyph run "frac" followed by a; `abs(x, y)` renders |x|; `hat(a, b)` renders â; `sqrt x` degrades the whole formula to the glyph run "sqrt x". The `bar` cp 0xAF is indeed dead, because layoutCall intercepts on the name at math.cc:1183. The dictionary row is still needed, though: its ACCENT flag is what routes `bar` into parseCall (math.cc:439).

*Verifier notes:* The registry idea is right, but two parts of the parser rule would break documented behaviour.

(1) 'Word ( always produces Call; unknown name -> math-unknown-fn' would turn the documented HoTT name application into an error. math-design §14 shows `Id_(U)(A,B)` / `Id(A,B)` as a name followed by a group; it is used in examples/real-world/hott-introduction.tsm:43 (`$Id(v,a)rA$`), and `sin(x)` / `max(a,b)` are dictionary text operators, not functions. Calls must bind only when the name resolves to a registry function. Otherwise the parser keeps today's name-plus-group parse, and the commas stay Punct.

(2) The registry needs a symbol fallback for bare uses. Today `dot`, `hat`, `tilde`, `bar` and `abs` used without `(` raise a whole-formula parse error (math.cc:480-483, 1253-1256). That already breaks the shipped HoTT example: `$p dot q$` (hott-introduction.tsm:73, produced by tex2tsm `\cdot -> dot`) renders as raw source; see missed item 2.

With those two rules the proposal does subsume the accent, fence and binom duplication, and it violates no ops or determinism invariant, since it is engine-internal.

### `math/vocabulary-in-font-artifact` — The operator dictionary, atom-class enum and flags are hand-curated inside the font-metrics compiler and generated into the font header

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-22, P3-24**
- locations: `tools/mathc.py:96-252`; `tools/mathc.py:324-333`; `tools/mathc.py:456-466`; `engine/src/math/mathfont.h:46-56`; `engine/src/math/math.h:23`; `engine/src/math/math.cc:214-225`; `docs/design-decisions-v2.md:259-261`; `docs/architecture.md:176-180`

The language vocabulary (Greek, big operators, 35 text operators, about 70 relations, arrows, AA..ZZ, accents, delimiters) is a Python list inside the script that compiles Euler-Math.otf. It is filtered by that font's cmap and emitted into `tsr::mathfont` alongside glyph metrics. The TeX atom-class enum, a layout concept, is defined by the generated font header; MathBox::cls is commented 'mathfont::kOrd..kInner'.

v2 §13 decided two layers:
1. Typst-codex dotted names (`arrow.r`, `subset.eq`).
2. Classes derived from the W3C MathML Core operator dictionary.

architecture §5 lists separate `opdict` and `fontmetrics` tools. As built, names are flat TeX-ish words curated by hand, and dotted names silently mis-parse. Verified: `arrow.r` → name 'arrow', '.', r; `subset.eq` → ⊂, '.', name 'eq'. This deviation is not in the math-design §13 deltas.

Directly typed Unicode gets its class from a linear scan of kOps in strcmp order, taking the first hit (math.cc:220-225). The class therefore depends on how names sort. Verified: ∄ is Rel (first hit '!exists') while `nexists` is Ord; ⊥ is Rel (via '_|_'), `perp` is Rel, `bot` is Ord. Swapping the font silently changes the language, because uncovered entries are dropped.

*Why ad hoc:* This crosses the vocabulary/metrics boundary: the meaning of a name depends on which font produced the header. Classes are per-row handwork instead of derived from a normative table. The cp→class reverse map is an accident of sort order.

*Proposed generalization (survey):* Split along the boundary.

(1) Vocabulary data, `data/math/symbols.tsv` with columns `name | cp | class | flags(large,stretchy,limits,accent) | form(prefix|infix|postfix) | negates | aliases | default-for-cp`. Seed it from the MathML Core operator dictionary and the Typst codex (dotted names). The v1 ASCII tier and AA sugar become alias rows.

(2) A generator, `tools/opdict.py` → engine/gen/math_dict.h, producing:
- a sorted name table
- an explicit cp→{class, flags} default table, where a conflicting cp without a `default-for-cp` row is a build error
- the negation map and the alphabet map (see the corresponding items)
- a lexer trie

The AtomClass enum moves to engine/src/math/atom.h, hand-written and shared.

(3) mathc.py emits only font data plus a per-font coverage bitset.

(4) Runtime: `MathDict::lookup(name)` → SymbolInfo; `MathDict::classOf(cp)` is an O(log n) generated index. Coverage is checked against the active MathFont at layout time (diagnostic plus fallback chain), not at vocabulary build time.

*Verifier:* Verified:
- DICT is curated inside the font compiler (tools/mathc.py:96-252) and filtered by cmap (mathc.py:324-333).
- The atom-class enum is emitted into the font header (mathc.py:456-459).
- The linear first-hit scan is at math.cc:220-225.

I also verified the class ambiguities:
- Typed ∄ is Rel, while `nexists` is Ord.
- Typed ⊥ is Rel, while `bot` is Ord.
- `arrow.r` becomes the upright name 'arrow', then '.', then r.
- `subset.eq` becomes ⊂, then '.', then the name 'eq'.

The drop is not fully silent: mathc.py prints the dropped list at build time (mathc.py:477-479). It is silent at runtime. kOpCount is 322 (engine/gen/euler_math.h:2767).

*Verifier notes:* The deviation from v2 §13 (docs/design-decisions-v2.md:259-261: codex dotted names plus classes derived from MathML Core) is real and is not listed among the math-design §13 deltas.

The split proposal respects every invariant: it is generated, committed and deterministic, and it changes no ops. MathML Core gives form/lspace/rspace/largeop/movablelimits rather than TeX classes, so the 'class' column still needs a documented mapping rule.

The same table should also generate the converters' TeX-to-tsm maps (see missed item 3). Otherwise a fourth vocabulary copy stays outside the gate.

### `math/lexer-hardcoded-alphabet` — Lexer token classes are hand-coded and disagree with dictionary keys: unreachable entries, bespoke _|_ and !word paths

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-24**
- locations: `engine/src/math/math.cc:95-104`; `engine/src/math/math.cc:196-213`; `engine/src/math/math.cc:164-177`; `engine/src/math/math.cc:178-195`; `engine/src/math/math.cc:128-135`; `engine/src/math/math.cc:150`; `tools/mathc.py:150`; `tools/mathc.py:218`; `docs/design-decisions.md:99-104`

`isOpChar` hard-codes 17 ASCII characters. Maximal munch is capped at 4 characters (`runEnd - i < 4`). Words are ASCII-letter runs, and `'` is always a Prime token.

I scripted a check of all 322 keys against these rules. Unreachable keys: `o+`, `o-`, `o.` (lexed as Word 'o' + op because 'o' is a letter) and `:'` (lexed as ∶ + prime). These are v1 tier-1 shorthands that design-decisions.md lists as supported; verified `a o+ b` renders o, +. Also dead: '(' ')' '[' ']' '{' '}' and "'" (handled structurally).

`_` needs a bespoke 3-character lookahead for ⊥ (164-177). `!` followed by letters has its own negation path (178-195). Any future key longer than 4 characters, or mixing letters and symbols, silently becomes unreachable. No build check ties the data to the tokenizer.

*Why ad hoc:* The token grammar is an independent hand-written mirror of the data. Every new shorthand shape needs C++ lexer code, and drift is silent.

*Proposed generalization (survey):* Generate the lexer's symbol layer from the dictionary as a trie of all keys that are not pure letter words, including mixed keys such as `o+` and `|->`. Tokenize as follows:

- **Reserved structure:** `^ _ / ' ( ) [ ] { } "` are structural unless a strictly longer trie match starts there (`_|_`, `|->`).
- **Otherwise:** the longest trie match wins if it does not end mid-letter-word; a letter-final key requires a non-letter after it, so `ox` vs `oxford` resolves; else take a letter-run Word.
- **Negation:** `!` before a symbol token becomes a prefix operator in the parser (`not(sym)`, see negation-enumerated), not a lexer branch.

The generator asserts `tokenize(key) == [key]` for every row and fails the build otherwise. This is the same gate philosophy as mathc.py's cmap check.

*Verifier:* Verified:
- isOpChar has 17 characters (math.cc:95-104).
- Munch is capped at 4 (math.cc:198).
- `_|_` has a bespoke lookahead (math.cc:164-177) and `!word` its own path (math.cc:178-195).

Rendered checks:
- `a o+ b` renders a, o, +, b, so o+ is unreachable.
- `:' x` renders ∶ with a prime superscript, so `:'` is unreachable.

*Verifier notes:* The build gate `tokenize(key)==[key]` is the right idea. Making mixed letter+symbol keys lexable is not.

With a trie at letter starts, `o-1`, `o+x` and `o.5` (o as a variable) would lex as ⊖, ⊕ and ⊙. The single-letter-variable reading is far more common than the v1 tier-1 shorthand. The gate should instead reject keys that mix letters and op characters (or make them require surrounding whitespace), and the dictionary should drop `o+`, `o-`, `o.` in favour of `oplus`/`ominus`/`odot`. That is a documented-language change against design-decisions.md:99-104, which needs a delta entry.

Moving `!` negation into the parser must keep the adjacency rule. `!` directly followed by a Rel-class token is negation; otherwise it is postfix factorial (math.cc:326-336). Without that rule, `n!<m` changes meaning.

### `math/negation-enumerated` — `!` negation is a list of enumerated pairs with inconsistent classes and wrong output for unlisted relations

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-24**
- locations: `tools/mathc.py:179-187`; `tools/mathc.py:202`; `tools/mathc.py:207`; `engine/src/math/math.cc:178-195`; `docs/design-decisions.md:114`

The v1 'negation pattern' (a `!` prefix negates) is implemented as 27 dictionary keys ('!in', '!exists', '!<', '!->', …).

Verified failures:
- Unlisted relations produce wrong glyph sequences: `!models` → '!' ⊨, and `!<->` → ≮ → (the 4-character munch hits '!<').
- '!exists' sits in the relations list (class Rel), while `exists` and `nexists` (same cp U+2204) are Ord. A negated quantifier therefore gets thick Rel spacing.

*Why ad hoc:* A general transformation (negate this symbol) is encoded as data enumeration, and the class of each negated row is chosen per row instead of inherited from its base.

*Proposed generalization (survey):* Add `not(x)` as a MathFn registry function; the parser desugars `!` + symbol-token to `not(sym)`.

- `MathDict::negate(cp)` is generated by inverting Unicode canonical decompositions (every precomposed negation decomposes to base + U+0338).
- Class and flags are inherited from the base symbol.
- Fallback when there is no precomposed form or the font lacks it: an overlay HBox (base glyph plus U+0338, or a slanted Rule) centred on the base's ink box. This is pure MathBox, so it stays measurement-free.

`!=` stays as an alias row for the common case.

*Verifier:* There are 28 keys starting with '!' in engine/gen/euler_math.h, which is 27 excluding '!='. Verified: `!<->` renders as ≮ followed by →, and `!exists` has class Rel while `exists`/`nexists` are Ord.

The `!models` case is worse than described. The unknown-negation path emits a bare '!' Chr (math.cc:189-194). attachPostfix then folds it as a factorial into the preceding operand (math.cc:326-335), so `a !models b` lays out as (a!) ⊨ b.

*Verifier notes:* Inheriting class and flags from the base, and deriving negate(cp) from canonical decompositions (base + U+0338), are both sound.

The fallback is not:
- An overlay of an isolated U+0338 is exactly the 'isolated combining-mark placement' that math-design §13 rejects because Firefox and Chromium disagree.
- MathBox Rule is an axis-aligned rectangle (math.h:12), so a 'slanted Rule' does not exist.

Safer: allow negation only where a precomposed cp exists and the font covers it. Enforce this at generation time with the same gate as cmap reachability. For uncovered cases emit a token-scoped diagnostic and render the base.

### `math/alphabet-variants` — Only blackboard bold exists, via 26 enumerated `AA..ZZ` rows; Euler's bold/fraktur/sans/mono/script alphabets are compiled in but unreachable

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-24**
- locations: `tools/mathc.py:247-252`; `tools/mathc.py:87-94`; `docs/math-design.md:399-401`; `docs/design-decisions.md:110-112`

fontTools check of fonts/Euler-Math.otf: bold 52/52, bold italic 52/52, bold fraktur 52/52, sans 52/52, sans bold 52/52, mono 52/52, fraktur 47/52, script 18/52, double-struck 45/52, bold digits 10/10. RANGES ships glyph records for all of U+1D400–1D7FF.

Yet the only syntax is AA..ZZ doubling: letters only, no `bb(1)` and no `bb(x)` for lowercase. math-design §14 says 'bold/sans/calligraphic alphabets (Euler has none; a second math font would be needed)', which is wrong for the shipped Euler-Math 0.75. Bold vectors, `cal(A)` and `frak(g)` can be written only by typing raw Unicode.

*Why ad hoc:* A Unicode-wide mechanism (Mathematical Alphanumeric Symbols) is exposed as 26 hand-written rows for a single alphabet, and the sugar does not desugar to any callable function.

*Proposed generalization (survey):* Generate a `MathVariantTable` from Unicode's math alphanumerics, including the letterlike holes (ℂℍℕℙℚℝℤ, ℬℰℱℋℐℒℳℛ, ℭℌℑℜℨ): `(variant, baseCp) → cp`.

Add registry functions `bb, cal, frak, bold, sans, mono, upright, italic, bolditalic`. Each applies the map to every Atom or Text-glyph leaf of its argument (a MathNode transform before layout), with a coverage diagnostic and base-letter fallback per leaf.

`AA` stays as sugar but desugars to `bb(A)`, keeping v1's 'doubling looks double-struck' rationale.

*Verifier:* Counted from the generated header's glyph records:
- bold 52, bold italic 52, bold fraktur 52, sans 52, sans bold 52, mono 52
- fraktur 47, double-struck 45, script 18, bold script 26
- bold digits 10

This matches the claim. math-design.md:399-401 ('Euler has none') contradicts the same doc's own §1 table (927 alphanumerics, math-design.md:33). Further confirmation: tex2tsm flattens `\mathbf{..}`/`\mathcal{..}` to plain letters (tools/convert/tex2tsm.mjs:124-127), because no alphabet construct exists.

*Verifier notes:* A Unicode math-alphanumeric map plus bb/cal/frak/bold/sans/mono functions is the standard mechanism (MathML mathvariant semantics). It touches no invariant. It should skip textFont leaves (body-font names) and emit a per-leaf coverage diagnostic, as proposed.

### `math/implicit-names-op-class` — Multi-letter word heuristics: implicit upright names classed Op, and script-context letter splitting that depends on dictionary membership

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-24**
- locations: `engine/src/math/math.cc:433-470`; `engine/src/math/math.cc:451-457`; `engine/src/math/math.cc:458-468`; `engine/src/math/math.cc:802-813`; `docs/math-design.md:370-386`

Any unknown word of two or more letters becomes a text-font Text node with class Op (documented, §14). Verified consequences:
- A Bin after a name is demoted by Rule 5. `Id + 1` and `2ab + c` render '+' as Ord with no medium space, and `AB - C` renders '−' as Ord.
- Typos and juxtapositions silently become upright words: `ab`, `dx`, `sinx`. The HoTT example contains `Id(v,a)rA` and `Sn^1`, both rendered as names with no diagnostic.

In single-token (script or denominator) context the word is split to its first letter unless it is a dictionary key:
- `y_ij` → y_i j
- `x_in` → x_∈
- `x_to` → x_→
- `x_max` → x_{max}

Text operators with LIMITS (lim, max) are a Script-over-Text special case in layoutScript rather than an operator property.

*Why ad hoc:* The same token shape gets three behaviours depending on context and on whether the spelling happens to be a dictionary key. Class Op is hard-coded for what is usually an identifier: TeX's \mathrm is Ord, \operatorname is Op. The choice is documented (§14), but its demotion side effect and the context-dependent split are not discussed.

*Proposed generalization (survey):* Make the policy explicit:
- Add `op(text, limits: never|display|always)` and `upright(text)` to the MathFn registry.
- The implicit-name rule desugars to `name(text)`, whose class comes from `MathPolicy.nameClass`: default Ord (TeX \mathrm); a document or host may choose Op.
- `MathPolicy.implicitNames = allow|warn|error`; warn emits an info diagnostic `math-implicit-name` with the word's sub-span, catching `rA`/`Sn`.
- Script-argument rule: the argument is one token, and a non-symbol letter word is always one name (`x_max`, `x_ij`). Splitting never depends on the dictionary; `x_i j` needs a space.
- Limits become an operator property read by the single attach layout (see bigop-greedy-body).

*Verifier:* Verified:
- `Id + 1` and `2ab + c` render '+' as Ord.
- `y_ij` renders y_i then j.
- `x_to` renders x_→.
- HoTT contains `$Id(v,a)rA$` (hott-introduction.tsm:43) and `Sn^1`.

Note that the Bin demotion is TeX-faithful. TeXbook Rule 5 demotes a Bin after an Op, so `\operatorname{Id}+1` also gets a unary '+' in TeX. The real issue is classifying arbitrary identifiers such as `ab` and `AB` as Op, not the demotion itself. The single-token script rule (x^ab = x^a b) exists only in a code comment (math.cc:360, 451-457) and in no doc.

*Verifier notes:* The Op class for names is documented with a rationale (math-design §14: thin space before Ord, none before `(`). That part is a defensible trade-off; the dictionary-dependent split is accidental.

The proposed script rule, 'a non-symbol letter word is always one name', would set the indices in `x_ij` as an upright body-font word. That is typographically wrong (TeX x_{ij} is two italic variables). Better: in script and argument position, a letter run that is not a dictionary key becomes a Run of single-letter variables, and only an explicit `op()`/`upright()` or `"…"` makes a name. This removes the dictionary-dependent split without upright indices.

`MathPolicy.nameClass=Ord` as the default would drop the documented thin space in `Id x`. Keep Op as the default and add the `math-implicit-name` info diagnostic, which is the valuable part.

### `math/bigop-greedy-body` — Big operators are a dedicated node with a layout-level greedy body; text operators with limits take a different path

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-25**
- locations: `engine/src/math/math.cc:504-524`; `engine/src/math/math.cc:988-1025`; `engine/src/math/math.cc:990-993`; `engine/src/math/math.cc:802-809`; `engine/src/math/math.cc:1230-1238`; `tools/mathc.py:125-143`; `docs/design-decisions-v2.md:258`

`sum`, `int` and similar become MNode::BigOp, whose body greedily takes everything up to a Rel, Close or end (a v2 §13 decision). Layout just places the body after the operator with Op–X glue. All the effects are side effects:

1. The body is a single top-level kid, so inline segmentation cannot break inside it. Verified: `sum_i a_i + b_i + c_i + d_i = x+y` produces one 9929su (≈155px) unbreakable segment in a 120px measure, and it overflows.
2. Consecutive sums nest. Verified: `sum_i a_i + sum_j b_j` puts the second sum inside the first sum's body.
3. lim/max/min (TextOp|Limits) get no body and use a Script special case. layoutBigOp's `textOp` branch is unreachable, because no TEXTOP row carries LARGE (mathc.py:137-143).

*Why ad hoc:* A semantic scoping rule, useful for a11y and evaluation, is implemented as a layout node. Glyph and text operators get two unrelated structural treatments. The rule is documented as deliberate (v2 §13), but its rationale is reading precedence, not layout, and the layout consequences are not discussed.

*Proposed generalization (survey):* Use one Operator atom, `{source: GlyphCp | Text, cls=Op, largeop, limits: Never|Display|Always}`, produced by dictionary rows or `op()`. A single `attach(base, t, b)` layout chooses limits when `base.limits == Always || (Display && isDisplay(st))`, regardless of glyph or text; that subsumes layoutScript:803-809.

If the greedy scope is kept for semantics, it becomes a non-layout annotation (`scopeEnd` index on the operator node, or an `MNode::Scope` that layout flattens into the parent run). Demotion, glue and segmentation then see the body atoms at top level. TeX behaviour (break after + inside a sum, governed by the Bin penalty) falls out.

*Verifier:* Verified:
- `sum_i a_i + b_i + c_i + d_i = x+y` gives a first segment of w=9929su, and --stage=layout at --width=120 places all 7 blocks on one overfull line.
- `sum_i a_i + sum_j b_j` nests the second sum in the first body.
- The layoutBigOp textOp branch is unreachable: TEXTOP rows lack LARGE (mathc.py:137-143), and the TextOp check precedes the Large check in parseWord (math.cc:440-448).

The report misses one point: the body terminator `atRel()` tests only `tok.k == Tok::Op` (math.cc:264, 271). So a directly typed relation does not end the body. Verified: `sum_i a_i ≤ b` puts `≤ b` inside the sum's body.

*Verifier notes:* The greedy scope is documented (design-decisions-v2.md:258), but as a reading-precedence rule. Making the body an opaque layout child is accidental.

Flattening the body into the parent run under a non-layout scope annotation keeps TeX spacing (Rule 5 after Op already demotes a leading Bin), fixes segmentation, and needs no ops change. The scope terminator must use the dictionary class of any token, typed Unicode included, to honour v2 §13 ('Relation-class refers to the operator dictionary's Rel atom class').

### `math/segmentation-class-preview` — Inline break segmentation re-derives atom classes in a parallel 'preview' and passes break kinds as magic codes mapped to three config keys

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-25**
- locations: `engine/src/math/math.cc:1216-1243`; `engine/src/math/math.cc:1281-1295`; `engine/src/math/math.cc:753-772`; `engine/src/math/math.cc:1303-1320`; `engine/src/math/math.h:55-66`; `engine/src/emit/emit.cc:122-146`; `engine/src/emit/emit.cc:131`; `engine/src/api/config.h:44-49`; `engine/src/api/wasm_api.cc:42-47`

`effClsOf` mirrors layout's firstCls/lastCls by hand and has already drifted:
- For Script it returns `l = f` (Open for a parenthesized base), while attachScripts sets lastCls to base->cls (Ord).
- Frac and Call map to Ord, while abs() and binom() produce Open/Close.

Verified with a patched .ops: `(a+b)^2 - c` gets no after-Bin break point, while `x^2 - c` does. The rendered '−' is still Bin with medium spaces, so only the break is lost.

Other problems:
- Demotion Rules 5–6 are coded twice (assemble, and the preview).
- Breaks are only considered at bare MNode::Atom nodes, so a scripted relation (`=^def`) or a text relation is never a break point.
- The break kind is a `u8 brkBefore` (0 first, 1 after-Rel, 2 before-Rel, 3 after-Bin) that emit translates into `cfg.mathRelAfterPenalty`, `mathRelBeforePenalty` or `mathBinAfterPenalty`. These are three bespoke keys, and none is exposed via tsr_config.
- The break glue reuses BF_BOUND, documented as 'CJK–Latin boundary glue', so `--stage=blocks` prints it as 'boundary'.

*Why ad hoc:* Break policy lives in an enum shared across the math/emit boundary. Class computation is duplicated. Per-feature config knobs replace a class-indexed table.

*Proposed generalization (survey):* Adopt TeX's mlist→hlist conversion as the single path.

1. Lay out the top-level run once, with demotion done only in `assemble`.
2. Convert it into a generic item list: `struct MathItem { enum K{Box,Glue,Penalty} k; MathBox* box; Su w; float penalty; bool discardable; };`.
3. Penalties come from `Config::math.penaltyAfter[8]` and `penaltyBefore[8]`, indexed by the laid-out box's effective lastCls/firstCls. Defaults: after[Rel]=0.8, before[Rel]=0.85, after[Bin]=0.95, all others INF.
4. `MathPolicy.breakDepth` controls whether items inside operator scopes are eligible.

Emit maps items 1:1 onto LinebreakBlocks without knowing math: Box → atomic inline object, Glue → discardable synthetic glue (a generic BF_SYNTH_GLUE, not BF_BOUND), Penalty → breakPenalty of the previous block. Segment boxes are re-assembled per segment with startEdge/endEdge as today. effClsOf is deleted. The config arrays are exposed through the generic config path.

*Verifier:* Verified with tsrc --stage=blocks: `(a+b)^2 - c` gets no break block, while `x^2 - c` gets one 'boundary' glue block. The rendered '−' stays Bin with 256su glue. BF_BOUND is documented as 'CJK–Latin boundary glue' (engine/src/emit/emit.h:17). No wasm export exists for the three math penalties; engine/src/api/wasm_api.cc exposes only tsr_config, punct, font and cjk-font setters.

Two overstatements:
- 'A text relation is never a break point': no Text node can carry class Rel (names are Op, quoted text and numbers Ord; math.cc:364-381, 440-468), so that case does not exist. The scripted-relation case is real.
- cost_risk 'Layout runs once instead of twice per inline formula' is wrong. layoutMathSegments lays each slice out exactly once (math.cc:1327-1334); effClsOf is a parse-tree walk, not a layout. Formulas are re-parsed and re-laid-out only on whole-document re-emits.

The Call/Frac → Ord mismatch is real, but it has no demotion consequence. Rule 5 treats Close and Ord alike, and Rule 6 reads `cls`, which is Ord for groups (math.cc:766, 946).

*Verifier notes:* A single mlist→item conversion over laid-out boxes, with class-indexed penalty tables, removes the duplicate Rules 5–6, the brkBefore codes and the BF_BOUND reuse. It touches no ops. Exposing the tables needs the generic config ingestion noted in the cross-cutting notes, because wasm_api has bespoke setters per knob.

### `math/inline-math-special-block` — Inline formulas are a typed `MathBox*` on LinebreakBlock, with math branches in emit, layout (three copies), render, copy and audit

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-13, P3-26**
- locations: `engine/src/emit/emit.h:42-44`; `engine/src/emit/emit.cc:113-118`; `engine/src/emit/emit.cc:293-299`; `engine/src/emit/emit.cc:354`; `engine/src/layout/layout.cc:332-335`; `engine/src/layout/layout.cc:446-449`; `engine/src/layout/layout.cc:528-531`; `engine/src/render/typeset_html.cc:474-478`; `engine/src/render/typeset_html.cc:587`; `runtime/src/main/copy.mjs:15-18`; `runtime/src/main/audit.mjs:20-24`; `docs/math-design.md:277-280`

Every generic stage tests `b.math`:
- **Emit:** inserts CJK boundary glue and enforces 禁则 by asking whether the previous block is math (the formula is treated as a Latin-class atom).
- **Layout:** pastes the same 'max asc/desc from b.math' snippet three times (sidecar rows, table cells, text lines).
- **Render:** branches to mathSpan and excludes math from run coalescing.
- **copy.mjs:** gives `data-syn="math"` the opposite meaning of every other data-syn value (copy data-src rather than skip).
- **audit.mjs:** special-cases it as one fragment.

math-design §9 had planned the general mechanism ('extend LinebreakBlock with optional intrinsic vertical extents — the mechanism headings-in-line already wants'). A typed pointer was built instead.

*Why ad hoc:* Layout, render and copy know about a semantic feature. Inline images, inline code boxes, an inline `box()` or SVG would each need the same five branches.

*Proposed generalization (survey):* Add an atomic inline object:

`struct InlineObject { Su w, asc, desc; u8 lbClass /* UAX#14-like: AL, ID, OP, CL, … */; StrRef copyText; u16 painterId; const void* payload; };`

with `LinebreakBlock::obj` (nullptr for text).
- **Layout:** one helper, `lineExtents(b) = max(vmet(b.style), obj ? {asc, desc} : 0)`.
- **Emit:** CJK boundary glue and 禁则 key off `lbClass`; math registers AL, a CJK-context inline image could register ID.
- **Render:** a `painters[painterId](out, obj, x, baseline, ctxStyle)` table; math registers paintMathBox.
- **Copy contract:** a generic `data-copy="…"` replacement-text attribute on any atomic object. `data-syn` keeps its single meaning (synthetic, skip), and audit treats any `[data-atomic]` element as one fragment.

*Verifier:* Verified:
- emit.h:42-44 holds `const MathBox* math`.
- The CJK boundary and 禁则 checks are at emit.cc:113-118, 293-299 and 354.
- Three identical asc/desc snippets sit at layout.cc:332-335, 446-449 and 528-531.
- The renderer branches at typeset_html.cc:474-478 and excludes math from coalescing at :587.
- copy.mjs:15-18 inverts data-syn, and audit.mjs:20-24 special-cases math.

math-design.md:278-280 did plan the generic 'intrinsic vertical extents' mechanism.

*Verifier notes:* The InlineObject with lbClass, a painter id and a data-copy contract is the right abstraction, and it stays dual-target (painters live in engine/src/render). Because math blocks carry linkUrl (emit.cc:148) but mathSpan ignores it, the generic object should also go through openRun. That ties this item to math-leaves-bypass-style.

### `math/display-math-unit` — Display formulas get a dedicated FlowUnit kind and fields, LineBox special=4, renderer-side geometry, and an unmeasured CSS-positioned equation number

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-18, P3-26, P3-29**
- locations: `engine/src/emit/emit.h:64`; `engine/src/emit/emit.h:107-108`; `engine/src/emit/emit.cc:745-761`; `engine/src/layout/layout.cc:382-399`; `engine/src/layout/layout.h:20`; `engine/src/render/typeset_html.cc:208-252`; `runtime/src/main/shell.mjs:65`

FlowUnit grows mathBox and eqTag fields.

Layout:
- Writes `special = 4`.
- Clamps the centring shift to 0, so a formula wider than the measure overflows to the right silently. Verified: a 40605su (≈634px) display in a 150px measure, with no diagnostic.
- Has no display skips above or below.

Renderer:
- Recomputes `suRoundPx(cfg.lineHeight * cfg.baseSizePx)` twice.
- Vertically centres the box at `(adv - boxH)/2` itself. That is layout arithmetic, and it means the baseline is not on any grid.

The tag is a `.tsr-eqno` positioned by CSS `top:50%; transform:translateY(-50%)`:
- Its text is never measured, so a tag/formula collision is undetectable (the tag overlaps wide formulas).
- Its baseline is not aligned with the formula baseline.
- Audits skip it.

Images (special=5) duplicate the same centre-on-measure code (layout.cc:362-380).

*Why ad hoc:* The renderer performs layout. A text run (the tag) bypasses the measurement contract. Display math shares no abstraction with other block-level atomic boxes.

*Proposed generalization (survey):* Add a generic DisplayBox flow unit:

`{ InlineObject body; HAlign align; Su skipAbove, skipBelow (Config::display.skipEm); std::vector<LinebreakBlock> tag /* ordinary measured inline content */; TagSide side; }`

Layout places body and tag at absolute coordinates on a shared baseline and applies a collision policy: if body.w + tagGap + tag.w > measure, the tag drops to its own line under the body. Overflow emits a `display-overflow` diagnostic (and, later, an optional break of the display at Rel). Layout emits LineBoxes the renderer paints with no arithmetic. Images (special=5) use the same unit with an image painter.

*Verifier:* Verified:
- special=4 with the shift clamped to 0 (layout.cc:382-399).
- Duplicated `suRoundPx(cfg.lineHeight * cfg.baseSizePx)` at typeset_html.cc:227 and :247.
- Renderer-side centring at :248-249.
- `.tsr-eqno` positioned by CSS (shell.mjs:65).

Nuance: displays do get the ordinary inter-top-block paragraph spacing (test/golden/math/display.layout.txt: para 0 ends at 1536su, display at y=2765su). What is missing is display-specific skips, not all vertical space.

The report also misses that audit.mjs:19 skips every child with `position: absolute`. The display mathSpan is position:absolute (typeset_html.cc:248), so display formulas, not only the tag, are never audited.

*Verifier notes:* A generic DisplayBox with measured tag content and a layout-side collision policy is sound. The tag becomes ordinary measured inline content under the §7 contract, and the image path (special=5, layout.cc:362-380) folds in. A display-overflow diagnostic should be a warning on the block, consistent with block-granular containment.

### `math/equation-numbering` — Equation numbering is a hard-coded resolver counter with a hard-coded "(n)" format, carried in ArgK::name

- kind: adhoc · severity: low · verdict: accurate · plan: **P2-07, P2-15**
- locations: `engine/src/resolve/resolve.cc:184-196`; `engine/src/resolve/resolve.cc:288`; `engine/src/emit/emit.cc:756`; `engine/src/api/config.h:69`; `engine/src/api/config.h:80-96`; `docs/document-model.md:50`

`eqNo++` runs only for labelled mathblocks. The string `"(" + num + ")"` is built in two places (the tag and @ref display). The rendered tag travels in the generic `name` argument, which also carries bibliography keys and note numbers. The supplement comes from a per-kind `cfg.supEquation` set by applyLang.

Not supported: section-relative numbering (1.3), numbering without a label, a label without a number, and user number formats.

*Why ad hoc:* It is one more per-kind counter next to tableNo and figNo (the latter keyed on role=="figure"). The format is code, not data, and an argument slot is overloaded.

*Proposed generalization (survey):* Give the resolver a counter facility:

`CounterSpec{ key: "equation", resetOn: "heading:1" | none, pattern: "(1)" | "(1.1)" | "(1a)" }`

registered per kind, with defaults in Config and overrides from JS via `$.numbering("equation", "(1.1)")`. The pattern is a string interpreted engine-side, not a JS callback, so resolve stays single-pass and scripts never read resolved numbers (v2 §11.1).

- `math(…, {numbered: auto|true|false})` controls whether a display gets a number.
- The formatted tag goes to a dedicated ArgK::tag.
- @ref display = supplement + format(n), using the same formatter.

The facility is shared with headings, tables and figures.

*Verifier:* Verified:
- `eqNo++` happens only when a label is present (resolve.cc:184-196).
- "(" + num + ")" is built at resolve.cc:192 and :288.
- The tag travels in ArgK::name (emit.cc:756).
- figNo is keyed on role=="figure" (resolve.cc:163-164).

'Only labelled display formulas number' is recorded as an as-built delta (math-design.md:341-343), but the format is not configurable.

*Verifier notes:* An engine-interpreted pattern string keeps resolve single-pass, and scripts still cannot read numbers (v2 §11.1). Carrying `$.numbering(...)` from JS needs a declaration in ops, a new kind or a doc-level arg, beyond the ArgK::tag mentioned. Both require the OPS_VERSION bump that the report already lists.

### `math/math-island-oneoff-syntax` — Island-level one-offs: whitespace-based display detection and a label honoured only on standalone displays (silently dropped otherwise)

- kind: adhoc · severity: low · verdict: partly · plan: **P2-06, P2-15**
- locations: `engine/src/inline/inline.cc:279-285`; `engine/src/inline/inline.cc:291-305`; `engine/src/codegen/codegen.cc:96-107`; `engine/src/codegen/codegen.cc:112-117`; `runtime/src/worker/executor.mjs:218-220`

`$ x $` versus `$x$` is decided by whitespace inside the fences in the inline lexer (the Typst rule, documented). The ` <id>` label is parsed after every island, but only codegen's 'paragraph is exactly one display formula' path forwards it.

Verified with tsrc --stage=js:
- `$x = 1$ <inl>` and `$ a = b $ <eqa> trailing text` drop the label silently; `@inl` is then unresolved.
- `$a$<id>` (no space before the label) becomes literal text '<id>'.
- Mid-paragraph display silently degrades to inline.

`mathinline(src)` has no label or display parameter, so the sugar can express things (display-ness, labels) that the constructor cannot.

*Why ad hoc:* This violates 'every syntax form is sugar for a constructor' (design-decisions-v2.md:107). Label attachment is coded per form.

*Proposed generalization (survey):* Use one constructor, `math(src | parts, {display: bool, label?: string, numbered?})`. Codegen always emits it. The emitter decides placement by policy: a display alone in its paragraph → DisplayBox; a mid-paragraph display → inline box in display style, or a forced block break, chosen in Config.

The `<id>` sugar becomes generic: any constructor that accepts `label` gets it. A label that cannot attach raises a `label-orphan` diagnostic instead of vanishing.

*Verifier:* Verified with tsrc --stage=js:
- `$x = 1$ <inl>` and `$ a = b $ <eqa> trailing` both emit mathinline without a label, with no diagnostic.
- `$a$<id>` leaves the literal text '<id> x'.
- Only a whole-paragraph display forwards the label (codegen.cc:96-107).

Correction: the whitespace display rule is not documented in docs. It appears only in code comments (inline.cc:280, codegen.cc:113 'documented'). design-decisions-v2 Appendix B (docs/design-decisions-v2.md:348) specifies `$ … $` display math as a line-structure construct (a verbatim island in the line pass), yet engine/src/linepass has no `$` handling at all.

*Verifier notes:* One `math(src, {display,label,numbered})` constructor with a generic `<id>` label sugar and a `label-orphan` diagnostic is sound; it costs an ops bump and a re-record. The general fix should also restore the Appendix B design, with display math recognised by the line pass. That fixes the heading/list mis-parse of continuation lines (see island-scan-escapes-block).

### `math/math-opaque-string` — Math is an opaque verbatim string through ops: no splices, values, content, or structured construction from JS

- kind: adhoc · severity: high · verdict: partly · plan: **P2-15**
- locations: `engine/src/inline/inline.cc:266-275`; `engine/src/codegen/codegen.cc:96-117`; `runtime/src/worker/executor.mjs:218-220`; `engine/src/emit/emit.cc:108-121`; `engine/src/emit/emit.cc:756-759`; `docs/math-design.md:292-297`; `docs/design-decisions-v2.md:158`

`$…$` is a verbatim island (v2 §5). The source string rides ArgK::src and is parsed at emit; math-design §10 makes this a deliberate choice: 'kinds and args already exist', so no ops change.

Consequences:
- Verified: `$x^#n$` is passed as src 'x^#n' and renders '#n'.
- The only composition is string concatenation, `#mathinline("x^" + n)`. It breaks on values containing `)`, `,` or `"`.
- No content can enter a formula: no link, styled word, footnote marker, cross-reference or `#val`.
- JS, fence handlers and the m`` tag can produce math only as source strings.

*Why ad hoc:* Every other content kind is a tree of constructor calls in ops; math alone is a string DSL inside one argument. This is documented as deliberate (math-design §10), but it is the clearest instance of syntax without a constructor model, and the justification ('no ops change') has expired now that ops versioning is routine.

*Proposed generalization (survey):* Keep engine-side parsing, for determinism and speed, but make the math tree an ops-level structure.

New kinds:
- `mathcall {fn, cls?}`, whose kids are argument sequences
- `mathsym {name | cp}`
- `mathsrc {src}`, a string fragment the engine parses into the same tree

Any inline content node may also be a kid; it is laid out as an hbox leaf measured through the normal path (see math-text-pull-channel).

Codegen compiles `$x^#n + #f(y)$` to `math(mathsrc("x^"), val(n), mathsrc(" + "), val(f(y)))`. The inline scanner treats `#` splices inside math as holes using the existing splice lexer, with `\#` for a literal. Values convert as follows:
- number/string → literal leaf (Ord)
- math node → subtree
- content → text-font hbox leaf

JS gets thin constructors over the same kinds: `math.frac(a,b)`, `math.attach(x,{t,b})`, `math.lr(o,body,c)`, `math.sym("alpha")`, `math.op("Hom",{limits})`. These are exactly the MathFn registry names.

*Verifier:* Verified that `$x^#n$` reaches ops as src 'x^#n' and renders x^{#} followed by n (the '#' is the superscript, n follows), not '#n' as a unit.

The m`` tag claim is wrong: m`` produces a plain text node (runtime/src/worker/executor.mjs:232-236), so it cannot produce math at all. Only mathinline/mathblock(string) can.

*Verifier notes:* This is documented twice. math-design §10 makes it a deliberate choice, and v2 §5 says verbatim islands make `#` literal inside them (docs/design-decisions-v2.md:158).

The proposal (`#` splices as holes inside math, `\#` for a literal) reverses that v2 §5 rule. It also breaks `#` as a math glyph, for example cardinality `#A`. It needs an explicit language-change entry and probably a distinct hole syntax, or only `#(`/`#ident(` heads.

The structured-ops part (mathcall/mathsym/mathsrc kinds; JS constructors over registry names) is sound and deterministic, provided content leaves go through the lazy measurement path. It requires an OPS_VERSION bump and a re-record.

### `math/closed-vocabulary` — No user extension surface: documents and JS cannot define symbols, operators, math functions or macros

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-15**
- locations: `engine/src/math/mathfont.h:46-56`; `engine/src/math/math.cc:231-235`; `engine/src/math/math.cc:1178-1209`; `runtime/src/worker/executor.mjs:218-220`; `engine/src/codegen/codegen.cc:211`; `docs/design-decisions-v2.md:130`

The dictionary is a constexpr table and the call names are a C++ if-chain. There is no `$.math` namespace and no ops kind for definitions.

A document cannot express any of the following:
- `Hom` or `colim` with limits
- an alias for ≔
- 𝟙 by name
- a `norm2(x)` macro
- a new relation class

Unknown names silently become implicit Op text with no limits control. Fence handlers have a registration contract (v2 §4.1: `$.fence(tag, fn)`, document-order registration); math has none, so built-ins are not on equal footing with user code.

*Why ad hoc:* Built-in features cannot be replicated with the public extension surface, which is the owners' explicit concern.

*Proposed generalization (survey):* Add a per-document MathEnv overlay on the base dictionary and MathFn registry, populated from ops in document order.

**JS API:**
- `$.math.symbol(name, {cp | text, class: "rel", large, limits, stretchy})`
- `$.math.op(name, {limits})`
- `$.math.def(name, params, body)`, where body is math source with `#param` holes, or a node tree built with the math constructors

**Ops:** these lower to a new kind `mathdef {name, kind: sym|op|macro, cp?, text?, cls?, flags?, params?}` with an optional body subtree.

**Engine:**
- A resolver pre-pass collects mathdefs into MathEnv. Registration must precede use, like fences; use-before-def raises a diagnostic.
- The parser resolves names via MathEnv → base dict → registry → implicit-name policy.
- Macros expand at parse by substituting argument subtrees into a cloned template, with a depth limit (diagnostic on recursion).

The design stays single-pass and deterministic with no JS at emit, and user rows go through the same coverage, class and lexer-trie machinery as built-ins. The lexer trie gets a per-document overlay, so a user may add `|=>` and similar.

*Verifier:* Verified:
- The dictionary is a constexpr table (mathfont.h:46-56 via euler_math.h).
- Calls are a C++ if-chain.
- The ops/JS surface has only mathinline/mathblock(src) (executor.mjs:218-220).
- Fences have a registration contract (design-decisions-v2.md:130); math has none.
- Unknown names silently become upright Op text (math.cc:462-468). Verified: `x quad y` renders the word "quad".

*Verifier notes:* A per-document MathEnv populated from a `mathdef` ops kind fits 'execution declares, resolver decides'. The resolver already walks in document order and emit runs after it.

The proposal must state whether definitions are positional (snapshot per formula) or document-global. 'Registration must precede use' implies positional scoping, so the pre-pass must index definitions by document position. User rows must pass the same lexer-trie and coverage gates at runtime, as diagnostics rather than build failures.

### `math/compiled-in-font` — Exactly one math font, bound at compile time and named in four layers (C++ include, renderer, CSS, shell URL)

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-23, P5-01**
- locations: `engine/src/math/mathfont.h:4`; `engine/src/math/mathfont.h:9-11`; `engine/src/render/typeset_html.cc:5`; `engine/src/render/typeset_html.cc:103-108`; `runtime/src/main/shell.mjs:59-60`; `runtime/src/main/shell.mjs:89-90`; `runtime/src/main/shell.mjs:203-206`; `tools/export-static.mjs:87`; `tools/pack-dist.mjs:26`; `engine/src/math/math.h:30`; `docs/math-design.md:55-57`; `docs/document-model.md:314`

The algorithms are font-agnostic in form, since they read `C::` constants. The binding, however, is `#include "../../gen/euler_math.h"` with constexpr globals (kConstants, kGlyphs, kUpem).

Other layers name the font directly:
- The renderer includes the same header to read kAscender/kDescender for the baseline pin, which crosses the render/font boundary.
- CSS hard-codes `'Euler Math', 'STIX Two Math', serif`. The STIX fallback is unsafe: its metrics differ, and it failed the §1 gate.
- shell.mjs hard-codes the woff2 URL, and export-static and pack-dist copy it by path.
- `fonts.math` in the documented config is prose only; there is no such key.

The woff2 subset was produced out of band (commit 70520e1 message; no tool in repo). Verified: it lacks U+0020, which the degraded-formula path paints.

MathBox has `bool textFont`, a hard two-font world.

*Why ad hoc:* The 'font-agnostic MATH artifact' abstraction (math-design §1) exists only as a code-reading convention, not as a runtime object. Swapping fonts requires recompiling the engine and editing CSS and JS.

*Proposed generalization (survey):* Introduce a MathFont runtime object:

`struct MathFont { u16 id; u16 upem; std::array<i16, kNumConstants> c; std::span<const GlyphRec> glyphs; std::span<const VarChain> vert, horiz; std::span<const AsmPart> parts; i16 contentAsc, contentDesc; std::string cssFamily; std::string contentHash; };`

It is loaded from a binary blob (.tsmf). mathc.py produces the blob and the woff2 subset in one run with a shared content hash, and extends the gates:
- cmap reachability (as today)
- woff2 cps ⊇ blob cps
- typo == hhea when USE_TYPO_METRICS
- no GPOS kern pairs among co-occurring text-leaf cps, or CSS disables kern

Euler's blob stays embedded, so the zero-measurement default holds. Extra fonts arrive via a NEED_FONT pull (like images) or are registered by the host.

- MathCtx carries a `const MathFont*`.
- Glyph leaves carry `LeafSource{fontId | StyleId}`, replacing textFont.
- The renderer takes font-family and content metrics from the registry.
- The shell installs @font-face from the registry list.
- Config gets `math.font` (id/url) and `math.nameStyle`.
- The CSS fallback list is removed: a missing glyph is a coverage diagnostic, never a silent fallback.

*Verifier:* Verified:
- The renderer includes mathfont.h (typeset_html.cc:5) and uses kAscender/kDescender (typeset_html.cc:103-108).
- The CSS hard-codes `'Euler Math', 'STIX Two Math', serif` (shell.mjs:59-60), and the woff2 URL is hard-coded too (shell.mjs:89-90).
- export-static.mjs:87 and pack-dist.mjs:26 copy fonts/euler-math.woff2 by path.
- Config has no math-font key.

With fontTools: the woff2 cmap has 2023 cps, equal to the artifact's 2023 records (none missing), and lacks U+0020. Commit 70520e1 describes an ad-hoc fontTools subset; no tool in the repo produces it.

*Verifier notes:* The compile-time artifact itself is a documented choice (math-design §3: the hyphenation precedent, for determinism). The cross-layer naming of the font in renderer, CSS, shell and packaging is the accidental part.

A content-hashed blob plus woff2 from one mathc.py run, with Euler embedded as the default, keeps goldens deterministic and the dual-target rule intact (blob loading through api/). The extended gates (woff2 ⊇ blob, typo==hhea, kern) are good. The OTF has 96 GPOS kern pairs (74 survive in the woff2).

### `math/fence-pairs-ascii-only` — Only the three ASCII bracket pairs form stretchy groups; ⟨⟩, |…|, ‖…‖ and named delimiters never stretch, and the stretchy flag is dead

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-24**
- locations: `engine/src/math/math.cc:151-160`; `engine/src/math/math.cc:393-410`; `engine/src/math/math.cc:1189-1192`; `tools/mathc.py:221-229`; `engine/src/math/mathfont.h:46-56`

The lexer emits Open/Close tokens only for `( [ {` and `) ] }`, and only those form Groups, which fencedRun stretches.

`langle`, `rangle`, `lceil`, `lfloor` and directly typed ⟨ ⟩ are dictionary atoms classed OPEN/CLOSE, but they never pair and never stretch. Verified: `langle 1/2 rangle` and `⟨1/2⟩` keep natural-size angles around a display fraction. `|1/2|` does not stretch, and `|` is Ord, so `{x | x > 0}` has no relation spacing (TeX \mid).

kFlagStretchy (mathc.py:103) is never read in C++. Arrows marked STRETCHY never stretch (horizontal stretching is a documented deferral). Stretchiness comes from 'is a Group delimiter or one of abs/norm/floor/ceil', not from data.

*Why ad hoc:* Grouping is keyed on three hard-coded ASCII pairs instead of the dictionary's Open/Close classes and stretchy flag.

*Proposed generalization (survey):* Drive grouping from the dictionary through `lr(open, body, close)`.

- Any token whose SymbolInfo class is Open opens a group closed by any Close-class token; mixed pairs stay allowed.
- Symmetric fences (`|`, `‖`) carry a `fence` flag (MathML Core 'fence' property). The parser pairs them by identity, with a heuristic: open at operand position, close at operator position. Explicit `lr(|, x, |)` and `abs(x)` stay as unambiguous forms.
- Stretch is applied iff the delimiter's stretchy flag is set, read from data.
- A `mid(x)` function (or a dictionary `mid` class) gives `|` and `:` in set-builder notation Rel spacing and stretch.

*Verifier:* Verified: `langle 1/2 rangle` and `|1/2|` keep natural-size delimiters around a display fraction, and `{x | x > 0}` gets no spacing around `|`. kFlagStretchy is never read anywhere in engine/src.

Minor: a Rel `mid` row (∣ U+2223, mathc.py:175) already exists, so set-builder relation spacing is available by name. It just never stretches, and bare `|` is Ord.

*Verifier notes:* Driving grouping from Open/Close classes plus the stretchy flag is sound. The heuristic pairing of bare `|` needs the proposed diagnostic-plus-Ord fallback, and the `mid` row should be the documented spelling of the separator. The horizontal chains (52, compiled but unused) would gain a reader through the same flag.

### `math/math-leaves-bypass-style` — Math ignores the style system: text leaves are measured with a synthesized Styling, and formula spans carry no run classes, colour, link or lang

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-25**
- locations: `engine/src/math/math.cc:579-599`; `engine/src/render/typeset_html.cc:146-172`; `engine/src/render/typeset_html.cc:474-478`; `runtime/src/main/shell.mjs:64`; `engine/src/emit/emit.cc:111`; `engine/src/api/config.h:24-29`

textFontBox builds `Styling sty; sty.sizeMul = basePx*scale/docBasePx`, dropping the context style's bits, fontFamily, lang and color. The renderer paints `.tsr-mt { font-family: inherit }`. mathSpan never goes through openRun.

As a result, a formula inside `style(color: …)`, a link, a lang scope or a font override renders in the default colour, unlinked, with no lang. Measurement and paint stay consistent only because both fall back to the body font. CJK inside `"…"` bypasses the explicit CJK stack rule.

Text leaves take the body font's line ascent/descent (vmet) as box extents, whereas Euler leaves use ink extents (math-design §3). Scripts and fractions around names are therefore placed against inconsistent geometry: superscripts on `Id` sit higher than on Euler letters.

*Why ad hoc:* The engine has one style mechanism (StyleId → runClasses/styleInto), and math re-implements a reduced private copy of it.

*Proposed generalization (survey):* MathCtx carries `StyleId ctxStyle`.
- Text leaves use `styles.idOf(compose(ctxStyle, {sizeMul: scale, bits: −CLS_EM}))`.
- Euler glyph leaves inherit color/lang from ctxStyle.
- mathSpan is wrapped by the same openRun(style, url) as text runs, which brings link, colour, lang and decoration for free.

The measurement protocol gains an optional per-word ink box (TextMetrics.actualBoundingBoxAscent/Descent), requested only for leaves flagged wantInk. Math extents use ink; the content-area vmet keeps driving the paint-side baseline pin.

*Verifier:* Verified:
- textFontBox builds `Styling sty; sty.sizeMul = …` only (math.cc:582-584).
- mathSpan is emitted without openRun (typeset_html.cc:474-478), although emit sets b.linkUrl on math blocks (emit.cc:148).
- `.tsr-mt { font-family: inherit }` (shell.mjs:64).
- Text leaves take vmet ascent/descent (math.cc:593-595), while Euler leaves take ink extents (math.cc:563-564).

*Verifier notes:* Composing with ctxStyle and wrapping in openRun matches emission-time style binding (v2 §12), so that part is sound.

The 'per-word ink box' is not. It extends the measurement protocol beyond v2 §6, which deliberately makes vertical metrics per style ('per-style, not per-string', design-decisions-v2.md:165). It also adds a new browser-variable quantity under the §7 contract. A per-style reference ink (cap-height/x-height measured once per style, for example from 'H'/'x' actualBoundingBox) keeps §6 granularity, is easy to mock natively, and is enough to align scripts on names with scripts on Euler letters.

### `math/math-text-pull-channel` — A per-feature pull state for math text runs forces whole-document re-emit and duplicates diagnostics

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-25**
- locations: `engine/src/api/doc.h:53-56`; `engine/src/api/doc.h:246-254`; `engine/src/api/doc.h:367-378`; `engine/src/math/math.h:40-46`; `engine/src/emit/emit.h:124-129`; `engine/src/emit/emit.cc:827`; `engine/src/support/support.h:133-138`

`Doc::mathTextMissing` is a second missing-metrics channel beside resolveWidths, merged by hand in pendingRequests. If any formula has an unmeasured name, `emitted=false` is set and the whole document is re-emitted after the round trip, re-parsing and re-laying-out every formula and paragraph.

Diagnostics raised during emit (math-parse, math-coverage) are appended again on each pass, because DiagSink has no dedup. (Inferred from the code; tsrc does not dump post-ops diagnostics.)

MathTextCtx threads MetricStore, StyleTable and Interner pointers into the math layer. The pattern parallels imageReqs and tokenReqs: each feature adds its own pending list and re-emit trigger.

*Why ad hoc:* It is a per-feature pull state instead of the general measure-request mechanism. The math layer reaches into the metric store.

*Proposed generalization (survey):* Make math layout lazy.

1. Emit stores `PendingMath{MNode* tree; StyleId ctx; std::vector<MeasureItem> leaves; Span span;}` on the block, with widthResolved=false.
2. resolveWidths enumerates the leaves like words.
3. A per-block `finalize()` runs layout once all leaves are present and fills w/asc/desc and the item list.
4. Diagnostics are emitted only in finalize, so they are raised exactly once.

More generally, define `struct Pending { virtual void collect(MeasureRequest&); virtual bool ready(const MetricStore&); virtual void finalize(); }`, implemented by images, tokens and math, so there is one pull protocol and no whole-doc re-emit. This is also the enabler for content-in-math (math-opaque-string), whose content leaves are just more PendingMath leaves.

*Verifier:* Verified:
- mathTextMissing (doc.h:53-56).
- Whole-document re-emit when it is non-empty (doc.h:246-254).
- Hand merge in pendingRequests (doc.h:367-378).
- DiagSink appends with no dedup (support.h:133-138).

The duplication is confirmed by code, not merely inferred: nothing in Doc ever clears or truncates `diags`. It is broader than math, because every re-emit also triggers it after a token or image provision (emitted=false at doc.h:232 and :242). All emit-time diagnostics duplicate, not only math-parse and math-coverage.

*Verifier notes:* A generic Pending/finalize protocol is a good unification. It is safe because segment count and inter-segment classes are known at parse time, so block structure can be fixed at emit with widths filled later.

It does not fix duplicate diagnostics on its own, since images and tokens still re-emit. The general fix belongs in Doc: record diags.items.size() after resolve and truncate to it before each emit, or key emit diagnostics by (code, span).

### `math/math-span-lexer-triplication` — The `$…$` island is lexed by three independent grammars that already disagree on escapes and line-crossing

- kind: adhoc · severity: medium · verdict: partly · plan: **P2-11**
- locations: `engine/src/inline/inline.cc:266-275`; `grammar/tree-sitter-tsm/grammar.js:88`; `editors/vscode-tsm/syntaxes/tsm.tmLanguage.json:108-110`; `engine/src/math/math.cc:110-227`

The three lexers:
- inline.cc skips `\x` escapes inside the island and lets it cross lines.
- tree-sitter and TextMate both use `/\$[^$\n]*\$/`: no escape, single line.
- The math lexer does not know the escape at all.

Verified: `a \$ b` renders the glyphs 'a' '\' '$' 'b', so a literal `$` cannot be typeset in math without a visible backslash. Multi-line display formulas highlight wrongly in the editor and collide with the line pass (see other_issues island-scan-escapes-block).

*Why ad hoc:* The same lexical rule is duplicated across the C++, tree-sitter and TextMate layers with no shared spec or test.

*Proposed generalization (survey):* Write one normative island spec (math-design §10) covering:
- the delimiter
- escapes (`\$` → `$`, `\\` → `\`), decoded in the island before the math lexer sees the text
- the line-crossing rule: only within the line-pass block, with the line pass treating an unclosed `$` at end of line like an open fence

Implement it once in C++, shared by inline.cc and the math lexer. Generate or test the tree-sitter external scanner and the TextMate begin/end patterns against a shared corpus (test/fixtures/math/island-*.tsm), with golden token boundaries from all lexers.

*Verifier:* Verified:
- inline.cc:270-273 skips backslash escapes inside the island and has no line limit.
- tree-sitter `/\$[^$\n]*\$/` (grammar/tree-sitter-tsm/grammar.js:88) and TextMate (tsm.tmLanguage.json:108-110) are single-line with no escapes.
- `a \$ b` renders a, \, $, b.

Under-counted: there is a fourth island lexer that disagrees most. splitCells (engine/src/inline/inline.cc:454-500) treats code spans and splices as opaque but ignores `$…$` entirely (see missed item 1).

*Verifier notes:* One normative island spec with a shared C++ scanner, plus a shared corpus driving tree-sitter and TextMate tests, is sound. splitCells must consume the same island table rather than re-lexing.

### `math/missing-glyph-fallback` — Uncovered codepoints get magic 600/700-unit boxes and a CSS fallback paint; magic constants inline in the algorithms

- kind: adhoc · severity: low · verdict: accurate · plan: **P1-25, P5-01**
- locations: `engine/src/math/math.cc:546-554`; `engine/src/math/math.cc:568-572`; `engine/src/math/math.cc:620`; `engine/src/math/math.cc:933`; `engine/src/math/math.cc:1132`; `engine/src/math/math.cc:658`; `runtime/src/main/shell.mjs:59-60`; `docs/math-design.md:108-109`

A codepoint missing from the artifact (CJK in a formula, U+0020 in degraded text, emoji) gets w=600, asc=700 font units. The browser then paints it in 'STIX Two Math' or serif with a different advance, which breaks the measurement–render contract silently.

Only the first miss per formula is diagnosed. The message prints the cp in decimal ("U+" + std::to_string(cp), so a space reads 'U+32').

Other policy constants are inline literals rather than named data:
- The delimiter shortfall `target -= target/10` is written twice (933, 1132). math-design attributes it to Typst's short_fall, but the code applies a proportional TeX \delimiterfactor-like cut.
- The assembly repetition cap is r ≤ 64.

*Why ad hoc:* Magic constants stand in for a defined fallback chain and a policy table.

*Proposed generalization (survey):* Add a leaf-source chain in the MathFont registry: for each cp, use the first registered MathFont that covers it; otherwise a text-font leaf (ctx StyleId) measured through the pull loop (math-text-pull-channel). Emit a `math-coverage` diagnostic per distinct cp, in hex, with its sub-span.

Move the policy numbers into `struct MathPolicy { double delimiterFactor = 0.9; Su delimiterShortfallEm = 0; int maxAssemblyRepeats = 64; … }` inside Config::math, documented next to the font-derived constants.

*Verifier:* Verified:
- w=600/asc=700 at math.cc:568-572 and 620.
- `"U+" + std::to_string(cp)` prints decimal (math.cc:551).
- The one-shot coverageWarned flag (math.cc:548).
- `target -= target/10` twice (math.cc:933, 1132).
- r ≤ 64 (math.cc:658).

Typst's DELIM_SHORT_FALL is an absolute 0.1em, so the proportional cut is \delimiterfactor-like, as stated. Consequence: U+0020 is absent from the artifact (RANGES start at 0x21, mathc.py:88), so every degraded formula containing a space raises a spurious 'math-coverage … U+32' warning in addition to math-parse.

*Verifier notes:* A MathPolicy struct plus a leaf-source fallback chain is sound. The fallback to a text-font leaf through the pull loop keeps the §7 contract, since the leaf is measured rather than guessed.

### `math/island-scan-escapes-block` — A math island can run past its block, duplicating content into headings and lists

- kind: issue · severity: high · verdict: accurate · plan: **P0-04**
- locations: `engine/src/inline/inline.cc:68-72`; `engine/src/inline/inline.cc:266-275`; `engine/src/inline/inline.cc:160`; `engine/src/inline/inline.cc:186`

`contiguous(to)` (inline.cc:68-72) only checks gaps between the block's own spans while `spans[k].end < to`. When the closing `$` lies beyond the block's last span, the loop simply ends and returns true. The `$` scan (inline.cc:269-275) walks `all`, the whole source.

Verified with tsrc --stage=js on 'Before.\n\n$ x\n  = y $\n\nAfter $a\n- b$ end.':
- codegen emits `mathblock("x\n  = y")` and ALSO `heading(1, …, text("y "), mathinline("After"))`. The line pass made `  = y $` a heading, and that heading's own `$` scanned across a blank line into the next paragraph.
- `$a\n- b$` is emitted as math and also as a list item 'b$ end.'.

The same predicate guards splices (inline.cc:160, 186). The line pass is unaware of math islands, while the inline pass crosses lines without bound.

*Verifier:* Reproduced exactly with tsrc --stage=js:
- mathblock("x\n  = y") is emitted.
- heading(1, text("y "), mathinline("After")) is emitted.
- mathinline("a\n- b") is emitted, plus a list item 'b$ end.'.

Root-cause addition: design-decisions-v2 Appendix B (docs/design-decisions-v2.md:348) lists `$ … $` as a line-structure verbatim island, but engine/src/linepass has no `$` handling. The as-built design departs from the documented one.

*Verifier notes:* The cross-cutting fix (linepass continuation handling for islands, like fences) is the documented Appendix B design. It also covers splices that share contiguous() (inline.cc:160, 186). The same unbounded scan also corrupts table cells (missed item 1).

### `math/prime-then-script-degrades` — `f'^2` is a 'double script' parse error that degrades the whole formula to raw text

- kind: issue · severity: medium · verdict: accurate · plan: **P1-24**
- locations: `engine/src/math/math.cc:321-323`; `engine/src/math/math.cc:337-353`

A prime creates `f->sup` as a Run (math.cc:337-353). A following `^` then finds the slot occupied and calls err("double script") (math.cc:321-323). Any parse error degrades the entire formula. Verified: `f'^2` renders as the literal glyph run "f'^2".

TeX and Typst both treat this as f^{′2}. The fix is for primes and an explicit superscript to merge into one sup Run.

*Verifier:* Verified that `f'^2` degrades to the glyph run "f'^2". The prime creates the sup Run (math.cc:345-352) and the next `^` hits err("double script") (math.cc:321-322).

*Verifier notes:* This is a correctness bug, not ad hoc. Merging into the existing sup Run is the TeX/Typst behaviour. Under the registry proposal it falls out of attach(x, t: prime ++ arg).

### `math/bracket-shedding-any-group` — Script and fraction arguments shed any bracket kind, not just parentheses

- kind: issue · severity: medium · verdict: accurate · plan: **P3-24**
- locations: `engine/src/math/math.cc:301`; `engine/src/math/math.cc:292-293`; `engine/src/math/math.cc:314`

`shed()` strips one Group layer regardless of delimiter (math.cc:301). Verified:
- `[a, b]/2` renders 'a, b' over 2: the interval brackets vanish.
- `y_[i]` renders y_i.
- `x^{1, 2}` loses its braces.

Typst sheds only round parentheses. Authors have to double brackets to keep them, as in the HoTT example `Sigma_((x:A))`. Shedding should be limited to `(…)` and recorded as a property of the argument slot.

*Verifier:* Verified: `[a, b]/2` renders 'a, b' over 2, `y_[i]` renders y_i, and `x^{1, 2}` loses its braces. shed() is at math.cc:301.

*Verifier notes:* Typst's math_unparen strips only round parentheses, so `[…]` shedding is clearly wrong: intervals and commutators vanish. Brace shedding is plausibly intended as TeX-style invisible grouping, but the docs say nothing either way.

Recommend recording in math-design whether `{…}` is an invisible group. Then make shedding an argument-slot property covering `(…)` and, if decided, `{…}`, never `[…]`.

### `math/call-arity-silent` — Call arity is unchecked: extra arguments dropped, arity errors misreported, a missing '(' degrades the whole formula

- kind: issue · severity: medium · verdict: partly · plan: **P1-24**
- locations: `engine/src/math/math.cc:473-500`; `engine/src/math/math.cc:1178-1209`

Verified:
- `abs(x, y)` renders |x| and `hat(a, b)` renders â; the extra arguments disappear without a diagnostic.
- `frac(a)` and `binom(n)` produce 'math-unknown-call: unknown construct' and render the literal word.
- `sqrt x` raises 'function needs (…) argument', which degrades the entire formula instead of only that call.

`static MNode empty` (math.cc:1180) supplies missing arguments silently, so `sqrt()` and `root(3)` render empty radicands.

*Verifier:* `abs(x, y)` → |x|, `hat(a, b)` → â, `frac(a)` → 'unknown construct' and `sqrt x` → whole-formula degrade are all verified.

The claim that `root(3)` renders an empty radicand is wrong. `static MNode empty` defaults to k=Atom, cp=0 (math.cc:66-68, 1180). The missing radicand therefore lays out as glyphBox(U+0000): a 600-unit stand-in box (w=691su at 18px), plus a spurious 'math-coverage' U+0 warning. escapeHtml (typeset_html.cc:9-20) writes a raw NUL byte into the HTML, which is verified in the --stage=html output. `sqrt()` does render an empty radicand, because the parsed empty argument is a Run.

*Verifier notes:* The fix is the arity spec in the MathFn registry, plus an Error leaf for missing arguments. Never synthesize a default MNode.

### `math/exactness-gaps-paint` — The zero-measurement exactness claim has untested paint-side assumptions

- kind: issue · severity: medium · verdict: partly · plan: **P1-23**
- locations: `runtime/src/main/shell.mjs:59-64`; `engine/src/render/typeset_html.cc:96-140`; `runtime/src/main/audit.mjs:20-24`; `docs/math-design.md:311-313`

These are the assumptions the claim depends on but nothing tests:
- Euler-Math has a GPOS `kern` feature with 96 pairs, e.g. P with comma or period, and slash with letters. Multi-glyph Euler leaves (numbers, degraded formulas, text operators before metrics arrive) are painted as one span with summed advances, but `.tsr-mg` does not set font-kerning:none or font-feature-settings.
- The woff2 subset lacks U+0020 (verified), yet degraded formulas paint spaces with `white-space: pre`.
- The CSS falls back to 'STIX Two Math' and serif.
- The `ssty` script alternates are ignored.

The e2e suite checks math only as one opaque fragment, plus copy (test/e2e/typeset.spec.mjs:92-104). The 'baseline-alignment audit' and glyph-position checks planned in math-design §11 do not exist.

*Verifier:* Confirmed with fontTools:
- Euler-Math.otf GPOS has feature 'kern' with 96 pair records (74 in the woff2).
- GSUB has 'ssty'.
- The woff2 lacks U+0020.
- shell.mjs:59-60 sets no font-kerning.

Overstated: no kern pair involves digits or '.' (the pairs are P/Ρ with comma or period, and slash or fraction-slash with letters). Number leaves are therefore unaffected. The risk is confined to degraded formulas (raw source painted as one span) and the pre-measurement Euler stand-ins of names. The e2e math test (test/e2e/typeset.spec.mjs:92-104) checks copy only.

*Verifier notes:* This is a contract and test gap, not ad hoc. Adding `font-kerning: none` to .tsr-mg and the planned baseline and glyph-position audits (math-design §11) closes it.

### `math/spacing-edge-classes` — Small spacing-model inconsistencies

- kind: issue · severity: low · verdict: accurate · plan: **P3-25**
- locations: `engine/src/math/math.cc:853`; `engine/src/math/math.cc:901-913`; `engine/src/math/math.cc:791`; `engine/src/math/math.cc:1080-1084`

Two inconsistencies, both verified:
- attachScripts sets `firstCls = base->cls` instead of base->firstCls (math.cc:853). `sin (a+b)^2` therefore gets an Op–Ord thin space, while `sin(a+b)` gets none (Op–Open).
- Fractions have no side padding (TeX \nulldelimiterspace, Typst's 0.1em around fractions). In `1/2 1/3` the two bars touch at x=575.

layoutAccent patches the topAccent of a single-glyph base by re-reading the glyph record (math.cc:1080-1084) because assemble does not propagate a lone child's topAccent.

*Verifier:* Verified: `sin (a+b)^2` gets a 192su Op–Ord glue, while `sin(a+b)` gets none. attachScripts sets firstCls and lastCls from base->cls (math.cc:853, 877).

In `1/2 1/3` the second fraction starts exactly at the first's width: at x=402 inline, and at 575 in display, the case the report quotes. The layoutAccent topAccent patch is at math.cc:1080-1084, and assemble sets topAccent=x/2 (math.cc:791).

*Verifier notes:* These are correctness bugs. Propagating base->firstCls/lastCls, adding fraction side padding (Typst's 0.1em), and having assemble propagate a lone child's topAccent are the right fixes.

### `math/diag-quality` — Diagnostics are coarse and can repeat

- kind: issue · severity: low · verdict: partly · plan: **P1-24**
- locations: `engine/src/math/math.cc:247-249`; `engine/src/math/math.cc:550-551`; `engine/src/api/native_cli.cc:79-82`

- 'math-coverage' prints the cp in decimal and fires once per formula.
- 'math-parse' records only the first error, on the whole-formula span. The parser knows `tok.pos`, so it could report span.start + 1 + pos.
- Diagnostics are re-added on every emit pass (math-text-pull-channel).
- The post-ops diagnostics of parse-diag.tsm are not goldened; the CLI has no post-ops diags stage.

*Verifier:* Decimal cp and once-per-formula are verified (math.cc:548-551), as are first-error-only on the whole span (math.cc:247-249) and re-addition on every re-emit (see the math-text-pull-channel correction: any re-emit, not just math).

Location: the CLI's `diags` stage is native_cli.cc:72, and it runs before ops ingest. Lines 79-82 are only the '--ops required' guard. The point stands: there is no post-ops diagnostics stage.

*Verifier notes:* Sub-span diagnostics (span.start + 1 + tok.pos) are cheap and fit block-granular containment.

### `math/dead-data-and-params` — Dead data and parameters that obscure the real contract

- kind: issue · severity: low · verdict: accurate · plan: **P1-22**
- locations: `tools/mathc.py:103`; `engine/src/math/math.cc:303-304`; `engine/src/math/math.cc:372`; `engine/src/math/math.cc:990-993`

- kFlagStretchy is never read.
- kHorizChains (52) and asmItalic are compiled but unused, a documented deferral.
- The dictionary rows '(' ')' '[' ']' '{' '}' "'" are unreachable, as are o+, o-, o. and :' (scripted check over all 322 keys).
- The accent row 'bar' has a dead cp.
- `allowFraction` and `scriptArg` are unused parameters (math.cc:303-304, 372).
- layoutBigOp's textOp branch is unreachable.

*Verifier:* Verified: kFlagStretchy is never read in engine/src. asmItalic and the horizontal chains are unused: mathChain(…, false) is never called; kHorizChains is referenced only at mathfont.h:33. `allowFraction` and `scriptArg` are explicitly voided (math.cc:304, 372).

*Verifier notes:* These are hygiene issues. Most of this dead data is subsumed by the vocabulary split and the lexer gate.

### `math/doc-drift` — Design docs disagree with the as-built math subsystem

- kind: issue · severity: low · verdict: accurate · plan: **P1-22**
- locations: `docs/math-design.md:159-162`; `docs/math-design.md:214`; `docs/math-design.md:247-249`; `docs/math-design.md:399-401`; `docs/architecture.md:15`; `docs/architecture.md:178-180`; `docs/document-model.md:314`; `engine/src/math/math.h:34`

- math-design §5 promises a `minMathSizePx` floor; it is not implemented (styleScale, math.cc:43-47).
- §5 cites SuperscriptShiftUp(Cramped) 289/363, and §7 cites DisplayOperatorMinHeight 1400. The artifact has 450/350 and 1130.
- §8 promises coalescing same-style glyphs into runs; mathLeaves emits one span per glyph.
- §14 says Euler has no bold/sans alphabets; it has them.
- Code comments cite 'math-design.md §10' for text runs (math.h:34, doc.h:53, emit.cc:827); the section is §14.
- v2 §13 dotted codex names and MathML Core classes are not delivered and not recorded as deltas.
- architecture.md §1 and §5 list `engine/grammar/math.peg` and `opdict`/`fontmetrics` tools, none of which exist.
- document-model §11 shows a `fonts.math` config key that does not exist.

*Verifier:* Constants verified in euler_math.h:76:
- SuperscriptShiftUp is 450 and Cramped is 350 (not 363/289).
- DisplayOperatorMinHeight is 1130 (not 1400).

math.h:34 cites §10 for text runs, which are §14. architecture.md:15 lists math.peg and architecture.md:178-180 lists opdict/fontmetrics; none exist. document-model.md:314 shows fonts.math, which Config lacks.

One more: v2 Appendix B's line-level `$ … $` (design-decisions-v2.md:348) is not built and not recorded.

*Verifier notes:* Documentation drift, not ad hoc.

### `math/fallback-and-a11y` — Semantic fallback and accessibility leave free determinism unused

- kind: issue · severity: low · verdict: accurate · plan: **P3-26, P3-27**
- locations: `engine/src/render/semantic_html.cc:155-159`; `engine/src/render/semantic_html.cc:260-265`; `engine/src/render/typeset_html.cc:146-172`

Math boxes are measurement-free except for names, yet the semantic fallback emits the source in `<code>` (semantic_html.cc:155-159, 260-265). v2 §9's 'math exact from t=0' is therefore not realized in the progressive phase.

`.tsr-math` has no role="math" or aria-label. Stretched delimiters paint PUA variant codepoints into DOM text (e.g. the abs() bars), so screen readers read garbage. The source is available in data-src and could serve as aria-label until a MathML or a11y decision is made.

*Verifier:* Verified:
- The semantic fallback emits `<code class="tsr-mathsrc">` (semantic_html.cc:155-159, 260-265).
- 237 of the variant cps in kVariantCps are PUA (U+E000–F8FF), so stretched delimiters put PUA characters into DOM text.
- .tsr-math has no role or aria attributes.

*Verifier notes:* MathML/a11y is an explicitly deferred decision (math-design §8, §13). aria-label from data-src is a cheap, sound interim fix.

### `math/toc-excerpt-drops-math` — Headings containing formulas lose them in the TOC and @ref text

- kind: issue · severity: low · verdict: accurate · plan: **P2-15**
- locations: `engine/src/resolve/resolve.cc:17-24`; `engine/src/resolve/resolve.cc:147`

excerptInto flattens text nodes only (resolve.cc:17-24, used at 147). `= Proof of $x^2$` appears in the TOC as 'Proof of '. Heading excerpts should clone inline content nodes (or carry a copy-text fallback such as the `$src$`) rather than a flattened string.

*Verifier:* excerptInto recurses only into text nodes (resolve.cc:17-24), and headings use it at resolve.cc:147. A mathinline has no kids, so it contributes nothing.

*Verifier notes:* This is a generic excerpt limitation, not a math special case. A copy-text fallback ($src$) is the minimal fix. Cloning inline nodes changes the TOC entry type from string to content.

### `math/missed:0` — Region table cell splitter ignores $…$ math islands, and the island scan crosses cell boundaries: formulas with | corrupt rows and duplicate content

- kind: missed · severity: high · verdict: verifier-found · plan: **P0-04, P2-11**
- locations: `engine/src/inline/inline.cc:450-500`; `engine/src/inline/inline.cc:266-275`; `docs/design-decisions-v2.md:145`; `docs/design-decisions-v2.md:158`; `tools/convert/tex2tsm.mjs:129`

splitCells documents itself as treating 'code spans and splices (head chains, #(…), content args)' as opaque, and it does skip backticks and # heads. It has no case for `$`, so every `|` inside a formula cuts a cell.

The per-cell inline parse then runs the unbounded `$` scan (contiguous() passes) across the cut into the next cell.

Verified with tsrc --stage=js on
```
#!table(cols: 2)
$abs(x) = |x|$ | `a|b`
#table!
```
The row becomes four cells: [mathinline("abs(x) = |x|") spanning 17-31, text("x"), text("$"), code("a|b")]. With cols:2 the table shows the formula, then a stray 'x'.

This contradicts v2 §5 ('verbatim islands are carved out before everything else') and §4.1 ('tree-level splitting is embedding-proof'). tex2tsm already works around it by rewriting `\mid` to U+2223 (commit d454e24, 'because | splits table cells'). That is a converter compensating for a lexer layer.

*Proposed generalization (survey):* Compute island spans (code, math, splice, comment) once in a shared scanner. Run cell segmentation over that island table instead of re-lexing; equivalently, follow v2 §4.1 literally and split on top-level `|` text nodes after inline parsing. Bound every island by its enclosing cell or line-pass span. This is the same fix as island-scan-escapes-block and math-span-lexer-triplication, applied to the fourth scanner.

### `math/missed:1` — Function and accent names shadow their symbol meanings: a bare `dot`, `hat`, `bar` or `abs` degrades the whole formula, and the shipped HoTT example is broken

- kind: missed · severity: high · verdict: verifier-found · plan: **P1-24**
- locations: `engine/src/math/math.cc:231-235`; `engine/src/math/math.cc:437-439`; `engine/src/math/math.cc:480-483`; `engine/src/math/math.cc:1253-1256`; `tools/mathc.py:241-245`; `examples/real-world/hott-introduction.tsm:73`; `tools/convert/tex2tsm.mjs:118`

parseWord routes every isCallName word and every ACCENT-flag dictionary word (hat, tilde, bar, vec, dot, ddot, breve, check, ring, acute, grave) to parseCall. parseCall requires `(`; otherwise it calls err("function needs (…) argument"), and any error replaces the entire formula with raw source.

tex2tsm maps `\cdot` to ` dot `, so the real-world HoTT document has `$p dot q$` and `$q dot p$`. Verified: `p dot q` renders as the single upright glyph run "p dot q".

The same happens for any prose-style use of `bar`, `hat` or `abs`. A name has exactly one meaning, chosen by a hard-coded list, and a mismatch costs the whole formula.

*Proposed generalization (survey):* Give each vocabulary name two possible bindings, a symbol value and a callable, as Typst does.

- `name(` resolves to the callable.
- A bare `name` resolves to the symbol: `dot` → ⋅ U+22C5 Bin, `tilde` → ∼, `hat` → ^, `bar` → |.
- If there is no symbol, it falls back to the implicit-name rule with a token-scoped diagnostic.

The MathFn registry row carries an optional `symbolFallback`. Parse errors produce an MNode::Error leaf over the offending token range, so the rest of the formula still lays out (block-granular containment applied inside math).

### `math/missed:2` — Converters carry a fourth, divergent copy of the math vocabulary that targets names the engine does not implement

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-24**
- locations: `tools/convert/tex2tsm.mjs:116-140`; `tools/convert/pbr2tsm.mjs`; `tools/typ2tsm.mjs`; `tools/mathc.py:96-252`

tex2tsm's MATH table ('LaTeX math → Typst-ish math') emits vocabulary the engine renders wrongly. Verified with tsrc --stage=mathbox:
- `\langle`/`\rangle` → `angle.l`/`angle.r` render as ∠ . l and ∠ . r (dotted codex names are unimplemented).
- `\circ` → `compose` renders the upright word "compose".
- `\cdot` → `dot` degrades the whole formula.
- `\quad`/`\,` → ' ' (there is no spacing construct), and `x quad y` renders the word "quad".
- `\mathbf{..}`/`\mathcal{..}` → plain letters (alphabets unreachable).
- `\bot` → typed ⊥, which lexes as Rel because of the strcmp-order first hit, while TeX `\bot` is Ord.

No test ties converter output to the engine's lexer and dictionary.

*Proposed generalization (survey):* Add alias columns (tex, typst/codex, unicode) to the single vocabulary table proposed in vocabulary-in-font-artifact. Generate the converter maps from it, and add a CI check: every converter-emitted math token must lex, through the same engine lexer, to the intended SymbolInfo (cp, class). Constructs the engine lacks (spaces, alphabets, angle brackets) then surface as gate failures instead of silent mis-rendering.

### `math/missed:3` — Token kind, not symbol identity, drives the parser: typed Unicode loses LARGE/LIMITS/ACCENT, and typed relations do not end a big-operator body

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-24**
- locations: `engine/src/math/math.cc:214-226`; `engine/src/math/math.cc:264`; `engine/src/math/math.cc:271`; `engine/src/math/math.cc:382-392`; `engine/src/math/math.cc:504-523`; `docs/design-decisions-v2.md:258`

The direct-character path takes only a class from the linear kOps scan and returns Tok::Chr; parseFactor's Chr case builds a flag-less atom.

Verified in display style: typed `∑_(i=1)^n a_i` stays the natural 864su glyph with side scripts and no body scope. `sum_(i=1)^n a_i` swaps to the display variant with limits.

The greedy-body terminator `atRel()` checks only `tok.k == Tok::Op`, so typed relations (≤ ∈ ⇒ ∣, exactly what tex2tsm emits) are absorbed into the body. Verified: `sum_i a_i ≤ b` places `≤ b` inside the ∑ body, which also removes the ≤ break point. This violates v2 §13: 'Relation-class refers to the operator dictionary's Rel atom class'.

*Proposed generalization (survey):* The lexer resolves every symbol spelling (name, ASCII sequence, typed codepoint) to the same SymbolInfo {cp, class, flags} through the generated cp→SymbolInfo index. Parser predicates (big operator, accent, Rel terminator, Open/Close pairing) read only SymbolInfo. A spelling then cannot change layout, which is the invariant the vocabulary split should guarantee.

### `math/missed:4` — No spacing, style or limits constructs: Spacer is unreachable from syntax, and display-ness is only expressible by whitespace inside the fences

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-24**
- locations: `engine/src/math/math.h:12`; `engine/src/math/math.cc:35-47`; `engine/src/math/math.cc:1003`; `engine/src/inline/inline.cc:279-285`; `tools/convert/tex2tsm.mjs:135`

The box model has a Spacer kind and a full eight-style algebra, but the language exposes neither.

Missing:
- quad, thin/med/thick space
- display/inline/script style switches (TeX \displaystyle, Typst display()/inline())
- limits/nolimits overrides

The consequences:
- `quad` becomes an upright name (verified).
- An inline `$sum$` can never take limits.
- A display formula cannot set a sub-expression in text style.
- Converters must drop `\quad`, `\displaystyle` and `\limits`.

Limits behaviour is fixed per dictionary row (kFlagLimits) and per style (`isDisplay(st)` at math.cc:1003, plus the layoutScript text-op special case).

*Proposed generalization (survey):* Add dictionary rows of kind Space (width in mu → Spacer). Add MathFn registry entries `style(D|T|S|SS, body)` (a direct index into the existing style tables), `limits(x)` and `scripts(x)` that override the operator's limits property, and `cramped(x)`. This puts user syntax on equal footing with the internal style algebra and subsumes the text-op limits branch.

### `math/missed:5` — Parse-error containment is the whole formula, and the degrade path paints unverified glyphs

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-24**
- locations: `engine/src/math/math.cc:247-249`; `engine/src/math/math.cc:1251-1256`; `engine/src/math/math.cc:1271-1274`; `engine/src/math/math.cc:546-554`; `engine/src/math/math.cc:1180`

Any parse error replaces the entire formula with `L.textBox(src, kOrd, …)`. Examples:
- an unclosed bracket
- `f'^2`
- `sqrt x`
- `p dot q`
- a stray `^`

That box sums Euler advances over the raw source. U+0020 is not in the artifact, so each space becomes a 600-unit stand-in and raises a spurious 'math-coverage U+32' warning. The browser then paints the space in the CSS fallback font, and the GPOS kern pairs (P, / …) apply inside the single span: two measurement–render mismatches on the error path.

Missing call arguments use a static default MNode (k=Atom, cp=0). This paints a raw NUL byte into the HTML (verified with `root(3)` at --stage=html).

Only the first error is reported, and it covers the whole formula span.

*Proposed generalization (survey):* Add MNode::Error {tokLo, tokHi} produced by parser resynchronisation at `,`, `)`, Rel or the end of the formula. It lays out as a text-font leaf of exactly that source slice (measured through the pull loop, like names), carries a sub-span diagnostic, and lets the surrounding tree lay out normally.

The registry arity check produces Error leaves for missing arguments instead of synthesising default nodes. This is v2's block-granular containment recursed into the formula, and the degraded path then never paints artifact-unverified glyphs.

## api-measure-code — Engine API, config, pull-loop resources (measure/tokens/images), code highlighting, incremental update

<details><summary>Design summary (as audited)</summary>

The API layer is one struct, `tsr::Doc` (engine/src/api/doc.h). It owns every stage product in one document arena and exposes a resumable `typeset()`. `compile()` runs linepass → inline → codegen. The host then executes the generated JS and passes the ops back to `ingest()`, which:
- instantiates the tree,
- runs `extractSidecars` (a verbatim-specific tree rewrite),
- runs the resolver,
- scans the tree for two resource kinds: codeblocks with a `lang` become `TokenReq`, and images without w+h become `ImageReq`.

`typeset()` runs a fixed sequence of guarded phases:
1. A whole-document barrier while any token or image request is unanswered.
2. `emitDoc`. It re-runs whenever `emitted` is cleared (token or image answer, math text-font metrics).
3. `resolveWidths`, which reports the missing (word, style) and vmet entries.
4. Still inside doc.h: a per-FlowUnit-kind break dispatch plus the F2 float tracker.
5. `layoutDoc`.

All resource families share `Status::NeedMeasure`, which the C ABI returns as 1. wasm_api.cc is a thin C ABI with 24 exports. It has one setter per host-tunable knob and one provide function per resource kind, each with its own marshalling. `tsr_measure_requests` builds a JSON object by hand with fixed sections: styles (with words), tokens, images.

The worker (worker.mjs) drives the loop. Each round it answers:
- images: fetch → createImageBitmap, with a main-thread `<img>` fallback over a one-off RPC;
- tokens: web-tree-sitter side modules through tokens.mjs, cached by `lang\0text`;
- words and vmets: `CanvasMeasurer`, memoised by the CSS font shorthand.

Each round-trips through its own export. Native drivers (tsrc, tests.cc) read Doc's request vectors directly and use stubs: the normative mock measurer, json+tsm grammars linked statically, and a fixed 512×384 for images.

`Config` (api/config.h) is a flat struct of about 34 global knobs plus a few constexprs. resolve, emit, break, layout and render read it directly. Only about 10 knobs are reachable from JS, and nothing can be overridden per document, region or block.

Highlighting folds (start, end, tag) byte triples into per-line seq/text runs. The only trace of the token class left afterwards is `Styling.color = "var(--tsr-tok-<tag>)"`. The grid (`FlowUnit::K::Code`) is laid out inline in `layoutDoc`. It measures two probe characters ("0" and "中") and supports a Stern–Brocot snap grid, hanging and comment-aware continuation, and a sidecar column.

Incremental editing does not reuse the Doc. Every keystroke builds a fresh one, because the arena cannot be reset. Reuse comes only from outside the Doc: a process-global KP cache in break.cc, plus JS caches for widths, tokens, image dims and fonts, each with its own key and eviction policy.


Strengths:

- The resumable pull loop (architecture §2.4) is the right foundation. The engine never blocks, needs neither SAB nor ASYNCIFY, and keeps the dual-target rule at include level: no Emscripten outside api/, and native goldens drive the same Doc.
- Fallback discipline is consistent. Every request must be answered: zero tokens means plain code, 0x0 means a placeholder plus an 'image-load' diagnostic. A missing grammar or a broken image never stalls the document (code-design §2, figure-design §2).
- The token provider contract is minimal and engine-agnostic: sorted byte-range triples, and the engine never tokenizes. The UTF-16 to UTF-8 mapping is handled at the provider boundary (tokens.mjs u16ToU8Map). The priority contract is specified, and highlighting is a deterministic native golden stage for json and tsm.
- 'Monospace is a metric contract' (code-design §4) is a robust, cheap idea: per code style the engine measures only two probes, ch and the CJK cell. Continuation indent is literal spaces, so it is font-independent and correct in static export. solveGrid is a pure, unit-tested function (tests.cc:241-254).
- MetricStore quantizes on ingestion (ceil plus epsilon in su) for overflow safety and keeps raw px for justification. This makes the ε policy arithmetic rather than scattered fudge factors.
- The KP memo is keyed on exactly the DP's inputs and has the retry ladder folded into the cached computation. That is the right shape for a pure-function cache, with the invariant documented (editor-design §2, break.cc:147-148).
- Progressive semantic paint straight after ingest, a per-paragraph DOM patch with paragraph-relative anchors, and phase timings on every result message are well-engineered, measurable editing features.
- The design docs consistently record the decision, the why, and the as-built deltas. This review could therefore separate deliberate trade-offs from drift precisely.

</details>


### `api-measure-code/per-resource-pull-plumbing` — Each external resource is its own hand-built pull channel: state struct, Kind-keyed scan, JSON section, provide export, JS branch and native stub

- kind: adhoc · severity: high · verdict: partly · plan: **P1-19**
- locations: `engine/src/api/doc.h:30-48`; `engine/src/api/doc.h:60`; `engine/src/api/doc.h:152-204`; `engine/src/api/doc.h:208-243`; `engine/src/api/doc.h:246`; `engine/src/api/wasm_api.cc:96-184`; `runtime/src/worker/worker.mjs:94-137`; `runtime/src/node/render.mjs:40-49`; `engine/src/api/native_cli.cc:240-252`; `engine/test/tests.cc:314`

Q1: no new status code was ever added. `Doc::Status` is still `{Ok, NeedMeasure}` (doc.h:60), and NEED_TOKENS / NEED_IMAGES exist only in prose (code-design §2, figure-design §2). Each new resource did, however, add all of the following:
(1) A request struct and vector (`TokenReq`, `ImageReq`). The id is the vector index, so every resource has its own id space.
(2) A tree scan hard-wired to one Kind and its argument conventions. `scanTokenReqs` matches a codeblock whose single child is text and whose `ArgK::lang` is non-empty. `scanImageReqs` matches an image without w&&h that passes `safeImageSrc`.
(3) A pending predicate OR-ed into the guard: `if (tokensPending() || imagesPending()) return Status::NeedMeasure;`.
(4) A provide method with its own semantics: `provideTokens` folds the answer into the tree; `provideImage` writes it into node args.
(5) A hand-written JSON section in `tsr_measure_requests`: `"tokens":[{id,lang,text}]`, `"images":[{id,src}]`.
(6) A dedicated export with its own wire format. `tsr_provide_tokens` takes a flat u32 triple array. `tsr_provide_image` takes scalars (id, w, h). `tsr_provide_word` takes one malloc'd C string per word, re-interned engine-side. `tsr_provide_vmet` takes (style, asc, desc).
(7) A JS loop branch with its own cache (worker.mjs:106-134).
(8) A stub in each native driver (native_cli.cc:241-243: `provideNativeTokens` plus a hard-coded 512x384 for any src), duplicated in tests.cc:314ff, and a third partial driver in render.mjs that answers tokens only.

Git history confirms the pattern. 7a3c058 (tokens) touched doc.h (+49), wasm_api.cc (+26), worker.mjs, shell.mjs and native_cli.cc. 217b3b7 (images) touched doc.h (+72), wasm_api.cc (+17) and worker.mjs (+21). figure-design §2 calls images 'exactly symmetric' to tokens: the symmetry comes from copy-paste, not from a shared abstraction. The barrier is also whole-document. A probe on an image-only document shows round 0 requests only images and round 1 only then requests words, so resources are serialized into extra round trips.

*Why ad hoc:* The pull loop is general in architecture §2.4 but special-cased per resource in code. The engine/host boundary (the engine wants a datum, the host knows how to get it) is renegotiated for every feature. The kind of resource is encoded by Kind-specific scans in the api/ layer, not declared by the consumer that needs it. Neither a user extension nor a new feature can add a resource without touching about five files, and the native and WASM drivers implement the protocol differently: native reads Doc fields directly, WASM goes through JSON.

*Proposed generalization (survey):* Introduce a typed resource protocol with ONE need-state, owned by the engine core (browser-free, so the dual-target rule holds).

(a) `engine/src/resource/resources.def`, an X-macro and single source of truth, just like ops.def:
`RES(textWidth,1,(StyleId style, Str text),(F64 px),Columnar)`
`RES(fontVmet,2,(StyleId style),(F64 asc,F64 desc),Columnar)`
`RES(codeTokens,3,(Str lang, Str text),(U32Array runs, StrArray classes),Blob)`
`RES(imageDims,4,(Str url),(F64 w,F64 h),Columnar)`
`RES(fontFace,5,(Str family,U16 weight,U8 style),(U8 state),Columnar)`
`RES(dataText,6,(Str url),(Bytes),Blob)`
`RES(hyphenPatterns,7,(Str lang),(Bytes),Blob)`
A tool (gen-res-ts, the analogue of gen-ops-ts) emits `runtime/src/shared/resources.gen.mjs` with the codecs.

(b) `class ResourceTable`:
- `ResId need(ProviderId, KeyView)` dedups by (provider, key) with a full equality check;
- `State state(ResId)` returns Pending | Ready | Failed;
- `template<class A> const A* get(ResId)`;
- `void provide(ResId, Status, span<const u8>)` decodes through the generated codec;
- `u32 version(ResId)` bumps on re-provide, for example when a late font lands.

(c) Stages declare their own needs through a `ResourceCtx&`. emit's codeblock case calls `rc.need(codeTokens, {lang, body})`, the image case calls `rc.need(imageDims, url)`, `resolveWidths` calls `rc.need(textWidth, …)`, and math layout uses the same path. Kind-scans disappear from doc.h. Stages run to their frontier: pending resources produce placeholder products marked `incomplete(resIds)`, so one pass collects ALL outstanding needs and there is no barrier.

(d) `Doc::Status {Ok, NeedResources}`. The C ABI becomes `tsr_requests(doc) → {ptr,len}` and `tsr_provide(doc, ptr, len)`. Requests are a binary envelope: for each provider, [providerId u16, n u32, resIds[], key columns as references into a shared string blob]. Answers reference ResIds, so no word string ever travels back (`textWidth` answers are one Float64Array).

(e) Host side, `runtime/src/worker/providers.mjs`:
`registry.register(name, {resolve(batch, ctx) → answers, runsOn: 'worker'|'main', cacheKey?, ttlMs?})`.
`drive(doc, registry)` replaces measureLoop: decode the envelope, `Promise.all` over providers, encode, one provide call per round. Main-thread providers go through one generic RPC (`{type:'provide?', provider, batch}`).
Public surface: `createEngine({providers})`. `#use` modules may export `providers` the same way they export `fences`. Fence handlers get `ctx.resource(name, key)` against the same registry, so typeset-time and execute-time resources share one cache.

(f) Native side: `struct ProviderSet { std::array<std::function<void(const Batch&, Answers&)>, N> }` and `bool driveToCompletion(Doc&, ProviderSet&, int maxRounds)` in `engine/src/api/driver.h`. The mock measurer, static tree-sitter and fixed image dims become providers, and tsrc, tests and fuzzers share that one loop.

*Verifier:* The substance checks out against the code: doc.h:30-48, :60 (Status {Ok, NeedMeasure}), :152-243, :246; wasm_api.cc:96-184; worker.mjs:94-137; render.mjs:40-49. The git stats for 7a3c058 and 217b3b7 match. My own WASM probe confirms that an image-only document requests images in round 0 and words only in round 1.

Wrong locations:
- native_cli.cc is only 108 lines. The stub lives at native_cli.cc:24-36 (provideNativeTokens at :25, 512x384 at :27, round cap at :28) and is duplicated at tests.cc:314-325.
- The image placeholder path is emit.cc:733-736, not 731-733.

cost_risk is wrong in two ways. The .ops fixtures are executor output and never contain request JSON, so nothing needs re-recording. A C-ABI change does not touch ops.def or OPS_VERSION.

*Verifier notes:* The core proposal is sound and keeps the dual-target rule: one need-state, a typed ResourceTable, codecs generated like ops.def, and one shared native driver. Three changes are needed.

(1) Keep the whole-document barrier as the default until emit can be re-run per TopBlock. Without the barrier, a late token or image answer re-runs emitDoc for the whole document, exactly as the math side channel does (doc.h:247-255). That doubles emit work, grows the arena, and duplicates the append-only diagnostics (document-model §0).

(2) Remove fontFace as an engine-requested, versioned resource. pages-design §1 (W) deliberately makes fonts DECLARED and preloaded: 'No re-typeset machinery … settling impossible to observe'. Versioned face resources would bring back the settle re-typeset that W closed. Face status may be reported (for example to enable a measure-fallback diagnostic) but must not drive invalidation unless W is reopened.

(3) dataText (bibliography) is an execute-time resource: executor.mjs:242-266 runs before ingest. It belongs in the host provider registry only, not the engine ResourceTable.

The codecs should also validate answers (tag range, ordering, UTF-8 boundaries, finite numbers); see missed items.

### `api-measure-code/image-dims-in-author-args` — Provided intrinsic image dims overwrite the author's w/h arg slots, and tokens rewrite the tree: resource results live in the authored model

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-19**
- locations: `engine/src/api/doc.h:180-189`; `engine/src/api/doc.h:208-233`; `engine/src/emit/emit.cc:713-733`; `engine/src/code/tokens.cc:21-90`; `docs/figure-design.md §1, §3`

`provideImage` writes the provider's answer into the image node's `ArgK::w`/`ArgK::h` via setNum. Those are the same slots that carry the author's declared display size. figure-design §1 documents the overloading ('w/h double as author-declared intrinsic dims'). But §3 rule 1 makes a lone `w` the display width, and scanImageReqs still pulls when only `w` is given.

Probe (WASM build, width 600, provider 800x400): `#image("x.png", {w: 100})` renders `width:600px`. The author's 100 is silently replaced by the intrinsic 800, then clamped.

The same pattern applies to tokens. `provideTokens` → `foldTokens` mutates the post-resolve tree from one text child into per-line seq/text runs. As a result, the semantic HTML differs depending on whether it is rendered before or after the answer: the worker's progressive paint has no highlighting, while render.mjs answers first and gets highlighting.

*Why ad hoc:* There is no separation between authored data and acquired data. Each resource picked a convenient place to stash its answer: an arg slot for images, tree structure for tokens. Consequently, products derived from the tree cannot tell which inputs came from a resource. That blocks dependency tracking (no way to invalidate when an image changes) and causes the correctness bug above.

*Proposed generalization (survey):* Make node args immutable after resolve; the authored model stays authored.
- Resource answers live only in the ResourceTable (per-resource-pull-plumbing).
- emit stores the `ResId` on the FlowUnit (`u.res`) and computes the display box with explicit inputs: `displayBox(authorW, authorH, scale, intrinsic = rc.get<ImageDims>(u.res), measure)`. This implements figure-design §3 precedence literally, with intrinsic dims as a separate parameter.
- Tokens fold at emit time into `FlowUnit::codeRuns`, which is the representation layout already consumes (emit.cc:603-635). Alternatively they can be kept as a derived per-node decoration overlay `{ResId → runs}`. Either way the tree is never mutated, the semantic serializer reads the same overlay, and both HTML paths agree.

*Verifier:* My probe reproduces it: #image("x.png", {w: 100}) with an 800x400 answer at width 600 renders width:600px.

One addition: emit.cc:727-728 evaluates `dw = scale > 0 ? scale*measurePx : iw`, so scale beats w. figure-design §3 says the opposite (w first, then scale).

*Verifier notes:* Keeping node args immutable and holding resource answers separately is right. But the proposed `displayBox(authorW, authorH, scale, intrinsic, measure)` computed in emit bakes cfg.widthPx into emit again, which is the relayout-stale-emit bug. The display box must be computed at break or layout time, where width is an input.

The claim that this subsumes the 'semantic-vs-typeset highlight divergence' is wrong. The progressive semantic paint (worker.mjs:212-213) is posted before the token round by design. It lacks highlighting wherever tokens are stored, and render.mjs differs only because it answers tokens first (pages-design §5 delta). The real gain is dependency tracking and immutability, not HTML agreement.

### `api-measure-code/math-text-measure-side-channel` — Math text-font metrics use a private side channel that forces a whole-document re-emit

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-20, P1-25**
- locations: `engine/src/api/doc.h:53-56`; `engine/src/api/doc.h:247-255`; `engine/src/api/doc.h:367-378`; `engine/src/emit/emit.h:124-129`

emit reports missing body-font words through `MathTextCtx::mathTextMissing`. `typeset()` sets `emitted = mathTextMissing.empty()` and re-runs `emitDoc` for the ENTIRE document when they arrive. `pendingRequests()` merges these words into the measure request using a separate, linear dedupe over `vmetStyles`.

Re-emit re-allocates every emit product in the doc arena and re-appends emit diagnostics. Probe: a document with `$\sin x$` and an unsafe image src yields the 'image-src scheme not allowed' warning twice.

*Why ad hoc:* Measurement already has a general request path (`resolveWidths`). Math invented a parallel one because the math box must exist before block widths do. The general concept, 'a stage that needs a datum before it can produce its product', is the same as for tokens and images, yet it got a third bespoke implementation with whole-document granularity.

*Proposed generalization (survey):* Math layout calls `rc.need(textWidth, {style, str})` and `rc.need(fontVmet, style)` on the shared ResourceTable (per-resource-pull-plumbing), building with stand-ins while the needs are pending. The product graph (adhoc-invalidation-flags) records that this TopBlock's emit depends on those ResIds, so when they arrive only the paragraphs containing the formula re-emit. Diagnostics become per-(stage, pid) slices that are replaced on re-run, not appended.

*Verifier:* My probe confirms it: '$\sin x$' plus an unsafe image src produces two identical 'warning image-src @[0,0)' lines.

The whole-document re-emit is documented in math-design §14 ('emits again once they arrive — the second emit finds every metric'). The report does not cite it.

*Verifier notes:* This is documented but still a third, bespoke request path, so it counts as ad hoc. The generalization is sound.

A cheaper, stage-local alternative avoids needing the product graph at all. Construct the widths of formula text runs inside resolveWidths, the pass that already fills block widths from the MetricStore. Text-run metrics then become ordinary MeasureItems in the same request and need no re-emit. This works because math segmentation depends on atom classes, not widths.

### `api-measure-code/config-plumbing-per-knob` — Host configuration is one C setter per knob, re-enumerated in worker and shell; most Config fields are unreachable

- kind: adhoc · severity: high · verdict: partly · plan: **P1-03**
- locations: `engine/src/api/wasm_api.cc:42-76`; `runtime/src/worker/worker.mjs:157-193`; `runtime/src/main/shell.mjs:331-336`; `runtime/src/main/shell.mjs:358-362`; `runtime/src/main/shell.mjs:393-397`; `runtime/src/node/render.mjs:24-28`; `engine/src/api/native_cli.cc:255-281`; `engine/src/api/config.h:22-73`; `docs/document-model.md §11`

`tsr_config(d, widthPx, baseSizePx, lineHeight, paraIndentEm)` is positional and uses inconsistent sentinels: `>0` keeps base and lineHeight, `>=0` keeps indent, and width has no sentinel at all. Next to it sit `tsr_set_punct_compress`, `_font`, `_cjk_font`, `_lang`, `_snap_kerning` and `_code_features`.

The worker destructures 16 named fields, and the shell lists them again in both `typeset()` and `update()`. Git confirms the cost: 864cb14, 264368b, b5696fc, 383d91d and c916342 each touched config.h, wasm_api.cc, worker.mjs and shell.mjs for one knob.

Of the roughly 34 Config fields, only about 10 are reachable from JS. Native-only fields include monoFont, paraSpacingEm, list and quote indents, codeScale, sidebarFrac, verbatimContIndent, the hyphen, URL and math penalties, cost{}, cjkJustifyK, cjkGlueEm, epsilon, and the individual supplements.

There are three more config channels: tsrc flags (--width, --indent, --punct), the golden runner's filename substrings (tests.cc:419-426), and render.mjs, which forwards only lang. document-model §11 specifies `tsr_doc_new(config_json)` with nested groups and 'Unknown keys → diagnostic'; none of that is built.

*Why ad hoc:* Every config key is wired by hand through four layers (C struct, C setter, worker protocol, shell API). Each layer hard-codes the key list, so knobs drift: monoFont is configurable in C++ but invisible everywhere else. The API boundary carries a feature's knobs instead of a typed settings document.

*Proposed generalization (survey):* A settings registry, generated from one table.

`engine/src/config/settings.def`:
`SET(id, "dotted.path", Type, default, Scope{Doc|Block|Inline}, Inherit, Affects{MEASURE|EMIT|BREAK|LAYOUT|RENDER})`, for example
`SET(lineHeight,"text.lineHeight",F64,1.5,Block,Inherit,EMIT|BREAK|LAYOUT|RENDER)` and
`SET(codeSnap,"code.snapKerning",Bool,false,Block,NoInherit,LAYOUT|RENDER)`.

A generator (tools/gen-settings) emits:
(1) a C++ `Settings` struct with typed accessors, replacing Config;
(2) a JSON patch parser / serializer, with unknown keys → diagnostic as §11 promises;
(3) `runtime/src/shared/settings.gen.mjs` (defaults, validation, TS types);
(4) a markdown table for the docs;
(5) tsrc flags `--set k=v` and `--settings=f.json`.

The ABI shrinks to `tsr_set_config(doc, const char* json) → int nDiags`. The worker and shell forward an opaque `config` object. The shell's existing named options stay as sugar that maps to dotted keys.

*Verifier:* - native_cli.cc:255-281 does not exist. tsrc parses flags at native_cli.cc:41-49 and applies config at :60-65.
- The worker destructures 17 fields including id.
- Everything else is verified: the inconsistent sentinels at wasm_api.cc:42-48, and commits 864cb14, 264368b, b5696fc, 383d91d and c916342 each touching config.h, wasm_api.cc, worker.mjs and shell.mjs.

*Verifier notes:* document-model §11 already specifies the JSON shape and 'unknown keys → diagnostic', so the registry implements an existing decision.

- Number parsing must stay deterministic (strtod, no locale).
- Host-only keys such as page width must be scoped so a document cannot set them.
- render.mjs should accept the same settings object.

### `api-measure-code/global-feature-knobs-no-cascade` — Feature knobs are global Config fields with no document, region or block scope; the only cascade is inline Styling

- kind: adhoc · severity: high · verdict: accurate · plan: **P3-02**
- locations: `engine/src/api/config.h:31-71`; `engine/src/api/config.h:75-84`; `engine/src/layout/layout.cc:151`; `engine/src/layout/layout.cc:189`; `engine/src/emit/emit.cc:477-478`; `engine/src/emit/emit.cc:501`; `engine/src/emit/emit.cc:514`; `engine/src/emit/emit.cc:541`; `engine/src/emit/emit.cc:551`; `engine/src/emit/emit.cc:591`; `engine/src/render/typeset_html.cc:332-342`; `runtime/src/worker/executor.mjs:131-137`; `docs/verbatim-design.md §1, §3, §5`

emit, layout and render read the following straight from `cfg`: verbatimSnapKerning, codeFontFeatures(+ByLang), sidebarFrac, verbatimContIndent, codeScale, paraIndentEm, listIndentEm, quoteIndentEm, paraSpacingEm, punctCompress, cjkJustifyK, cjkGlueEm, hyphenPenalty, urlBreak*, and the three math penalties. The constexprs kTableCellPadEm, kTableRowPadEm, kPunctHalfEm and kCjkBoundaryEm, and `headingSizeMul` (1.6 / 1.35 / 1.15), are not even config.

The design docs specify finer scopes that were never built:
- verbatim-design §1: alignment is 'Enabled per feature, not globally'.
- verbatim-design §3: 'per block: ligatures on → budget mode … fence-arg override' for font features.
- verbatim-design §5: '`sidebarCol` configures the code box's right edge'.

A document cannot say 'this region uses 首行缩进' or 'this listing snaps'. The one cascade that exists, Styling via StyleDelta (bits plus font/lang/color/sizePx), cannot carry layout parameters. The region-arg-to-style lifting in the executor is itself a hard-coded four-key list: `if (args.font !== undefined || args.lang … || args.color … || args.sizePx …)`.

*Why ad hoc:* Each feature added its parameters as a global field read at its point of use. There is no notion of where a parameter applies, nor of who may set it (host, document, region, block). The documented per-block semantics were therefore approximated by globals.

*Proposed generalization (survey):* Add a settings cascade that parallels the style cascade, using the registry from config-plumbing-per-knob.

Layers, lowest to highest priority: defaults < locale pack (selected by `doc.lang`) < host JSON < document `$.set({...})` < region/fence args < block args < inline style. The document layer is a new op, `SETTINGS_PUSH patch`, on the schedule stack next to STYLE_PUSH and popped by STYLE_POP_TO.

Any region or fence arg whose key matches a registered path is lifted automatically into a settings patch, replacing the four-key list in executor.mjs:131-137. That makes ```` ```js(code.snapKerning: true, code.fontFeatures: "'calt' 0") ```` and `#!aside(par.indentEm: 2)` work without new code.

At instantiation, each ContentNode gets a `SettingsId` (interned like StyleId; most blocks share one). FlowUnit carries it. Consumers read `S(u).code.snapKerning`. Per-language maps become selector rules, `$.set({when: {lang: 'haskell'}}, {code: {fontFeatures: …}})`, or stay map-typed settings resolved at emit. Each setting's Affects mask is what the product graph uses for invalidation (adhoc-invalidation-flags).

*Verifier:* All direct cfg reads are confirmed: emit.cc:477-478, 514, 541, 551, 591; layout.cc:151, 189; typeset_html.cc:332-342. The executor's four-key lifting list (executor.mjs:131-137) and the verbatim §1/§3/§5 quotes are also confirmed.

Minor: emit.cc:501 reads headingSizeMul(), not cfg.

*Verifier notes:* (1) Fence and region args compile to JS object literals; codegen emits `__fence("js", ({sidecar: "///", lineNo: 1}), …)`. The proposed ```js(code.snapKerning: true)``` is therefore a JS syntax error. Use nesting (`code: {snapKerning: true}`) or a reserved `set:` key.

(2) Implicitly lifting any arg whose key matches a registered path recreates the namespace collision the report itself flags: `lang` is already a codeblock arg and a style key. Require an explicit namespace.

(3) Settings that change measurement (font roles, code.scale) must feed StyleDesc/StyleId, not only SettingsId, or MetricStore keys go stale.

Binding at instantiation on the schedule stack is consistent with v2 §12. The `when:{lang}` selector rules are set/show-rule sugar that v2 §12 explicitly defers, so they are optional.

### `api-measure-code/supplements-and-lang` — Cross-reference supplements are one Config field per counted kind, localized by a two-way host-level lang switch

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-10, P3-30**
- locations: `engine/src/api/config.h:66-71`; `engine/src/api/config.h:86-102`; `engine/src/api/wasm_api.cc:62-65`; `engine/src/resolve/resolve.cc:176`; `engine/src/resolve/resolve.cc:285-288`; `runtime/src/main/shell.mjs:333`; `runtime/src/node/render.mjs:24-28`

The supplements are separate fields: `supHeading`, `supTable`, `supFigure`, `supEquation` and `capSep`. The resolver picks one by switching on Kind (heading / table / group→figure / mathblock).

`applyLang` is binary. zh or ja gives 图/表/式/：; every other language gets 'Figure '/'Table '/'Eq. '/': ' (so ko, de and fr all become English), and supHeading is never touched.

Language is a host option (`tsr_set_lang`; the shell defaults to 'zh-CN'; config.h defaults to the zh strings). A .tsm source cannot declare its own language for these purposes, even though `#style({lang})` exists for runs. A user-numbered construct such as `#!theorem` or a code listing has no supplement slot. document-model §11 sketched `supplements` and `counters: {figure: {resetAt}}` maps.

*Why ad hoc:* The counters and supplements are a closed list hard-wired to built-in kinds. Built-ins and user regions are therefore not on equal footing: a user role cannot be numbered and referenced like `group{role:"figure"}`. Localization is a hard-coded `if` instead of data.

*Proposed generalization (survey):* Add a `counters` setting map: `counters.<name> = {supplement: Str, captionSep: Str, format: '1'|'1.1'|'i'|…, resetAt: <counter>|none}`. The resolver attaches a counter name to each numbered node by role or kind, from a declarative table: heading→'heading', table→'table', group{role:R}→R if `counters.R` exists, mathblock→'equation'. User roles get numbering and @ref text by declaring a counter.

Locale packs are data files (`locale/zh.json`, `locale/en.json`, …) that act as settings patches selected by a `doc.lang` setting. The document can set it (`$.set({doc: {lang: 'en'}})`), and the host default comes from tsr_set_config. `applyLang` is deleted.

*Verifier notes:* This keeps 'execution declares, resolver decides': counters become declared data and numbering stays inside the single resolver pass (resolve.cc:158-170, 284-288). A document-level lang setting is instantiated before resolveDoc runs (doc.h:72-74), so the locale pack is available in time.

### `api-measure-code/font-role-split` — Font-role resolution is split across measure.h class-bit mapping, renderer classes and hard-coded shell CSS

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-04**
- locations: `engine/src/measure/measure.h:60-71`; `engine/src/api/config.h:23-30`; `engine/src/render/typeset_html.cc:33-40`; `runtime/src/main/shell.mjs:8-12`; `runtime/src/main/shell.mjs:28-29`; `runtime/src/main/shell.mjs:349-353`; `runtime/src/main/shell.mjs:332`

`describeStyle` maps semantic class bits to CSS font descriptors: CLS_CODE→cfg.monoFont, CLS_CJK→cfg.cjkFont, otherwise bodyFont; CLS_BOLD→700 else 400; CLS_EM→italic. The typeset renderer emits only classes (`tsr-code`, `tsr-cjk`). On the paint side those classes resolve through shell CSS:
- `.tsr-code { font-family: monospace }` is a literal. It matches `cfg.monoFont = "monospace"` only because the defaults coincide, and monoFont has no setter.
- `.tsr-cjk { font-family: var(--tsr-cjk-font) }` is set by the shell from its own option, whose default TSR_CJK_FONT is a hand copy of config.h ('mirrors the engine default').

bodyFont defaults also differ: config.h has "Crimson Text", Georgia, serif, while the shell has 'Georgia, serif'. The shell comment calls this 'the measure/render contract, not styling sugar', yet the contract is upheld by three files agreeing by hand. Only two weights are expressible.

code-design §4 recommends a Sarasa-class code stack as the default, but there is no host-level way to set the code font.

*Why ad hoc:* The measure layer knows semantic roles (code, CJK) and turns them into fonts, while the render layer delegates the same decision to page CSS. The measure/render contract has no single owner, which is a layering leak across measure, render and shell.

*Proposed generalization (survey):* Add font roles as settings: `font.body`, `font.cjk`, `font.mono`, `font.math`.

Styling gets an explicit `fontRole` (an enum or interned role name; CLS_CODE and CLS_CJK stop doubling as font selectors) and a numeric `weight`. One function, `resolveFont(Styling, Settings) → StyleDesc`, is used by measurement. The serializers write the same resolved families as CSS variables on the root (`<div class="tsr-doc" style="--tsr-font-body:…;--tsr-font-mono:…;--tsr-font-cjk:…">`), and TSR_CSS references only those variables.

The shell stops duplicating defaults: it applies the root element's variables and never sets families itself. Static export and print inherit correctness automatically.

*Verifier:* Nuance on the bodyFont default mismatch (config.h:23 vs shell.mjs:332): it is latent in the browser, because the shell always forwards fontFamily and cjkFontFamily (shell.mjs:359-361 → worker.mjs:171-180). It affects only native tsrc and goldens versus browser defaults.

The two-weight limitation is a documented model decision (document-model §3: 'fontWeight/italic ride the class bits').

*Verifier notes:* CLS_CJK must stay as a script class: CJK justification and punctuation rules read it. Only its second job as a font selector should go. Emitting resolved families as root CSS variables is the right single owner for the measure/render contract.

### `api-measure-code/token-tag-table-copies` — Token tag set, alias table and priority contract are hand-synced copies across C++, worker JS, editor JS and CSS

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-22**
- locations: `engine/src/code/tokens.h:14-22`; `engine/src/code/tokens.cc:5-18`; `runtime/src/worker/tokens.mjs:6-18`; `runtime/src/worker/tokens.mjs:130-138`; `editors/vscode-tsm/src/tokens.js:12-21`; `editors/vscode-tsm/src/tokens.js:44-56`; `engine/test/native_tokens.h:59-80`; `runtime/src/main/shell.mjs:71-84`

The token vocabulary exists in several hand-synced copies:
- C++: `kTokenTags` (14 names; the tag is a u8 index) and the alias if-chain in `tokenTagFromCapture`.
- worker: `TAGS` + `ALIAS` in tokens.mjs, marked 'keep in sync'.
- editor: `TYPE_OF` in the VSCode extension, which has NO alias table. Captures like @conditional, @method and @field are therefore coloured on pages but dropped in the editor, despite the comment 'editor coloring matches published pages'.
- CSS: fourteen `--tsr-tok-*` variables, twice (light and dark), in TSR_CSS.

The (start asc, patternIndex asc, earlier wins) priority contract is implemented three times: native_tokens.h, tokens.mjs and editor tokens.js. code-design §5 says 'tagId↔class table is a build product beside the grammar list'; it is not built.

*Why ad hoc:* A cross-language contract is maintained by convention ('keep in sync') instead of by one generated source, which is exactly the drift ops.def was created to prevent. The tag set is fixed at 14 because a u8 index is baked into the fold.

*Proposed generalization (survey):* Generate from one manifest. `tools/codehl-assets.mjs` writes:
- `runtime/assets/hl/captures.json` with `{classes:[…], alias:{…}}`;
- `engine/gen/token_classes.h`;
- a shared JS module, `runtime/src/shared/hl.mjs`, exporting `resolveCaptures(matches, alias) → runs` (the priority contract implemented once), imported by both the worker and the editor.

The native provider includes the generated header. In the target design (token-class-as-color-string), tokens carry class names, so the closed 14-tag enumeration and the CSS variable list are no longer a contract at all, just a default theme.

*Verifier:* The editor drift is latent, not present. editors/vscode-tsm highlights only tsm, and third_party/grammars/tsm/highlights.scm uses only canonical heads (@keyword, @function, @attribute, @type, …), never @conditional, @method or @field. So no capture is dropped today.

The editor maps tags to VSCode types differently anyway (punctuation→operator, constant→enumMember; tokens.js:12-18). 'Matches published pages' was never an identity mapping.

The dark theme declares 12 variables, not 14.

*Verifier notes:* Sharing hl.mjs between the worker and the editor still leaves two implementations of the priority contract: C++ in native_tokens.h:72-80 and JS. A native-vs-JS triple-equality test on the json and tsm fixtures would close that gap.

### `api-measure-code/token-class-as-color-string` — Token class is encoded as a CSS color string, emit recovers 'comment' by string-comparing colors, and comment italics are hard-coded by tag index

- kind: adhoc · severity: high · verdict: accurate · plan: **P2-08**
- locations: `engine/src/code/tokens.cc:39-50`; `engine/src/emit/emit.cc:618-624`; `engine/src/emit/emit.h:76-80`; `engine/src/layout/layout.cc:180`; `engine/src/render/semantic_html.cc:73-77`; `engine/src/model/model.h:29-40`; `runtime/src/worker/executor.mjs:196-206`; `docs/code-design.md §3-§5`

`foldTokens` sets `s.color = strs.intern("var(--tsr-tok-" + kTokenTags[tag] + ")")`. After folding, that string is the only trace of the token class. `if (tag == 3) s.bits |= CLS_EM;` makes comments italic, an engine-level styling decision keyed by array index.

emit then derives `CodeRun.isComment` as `styles.get(k->style).color == strs.intern("var(--tsr-tok-comment)")`, and that flag drives comment-aware hanging in layout (layout.cc:180, verbatim §4).

Consequences:
(1) A user fence handler that returns a pre-highlighted structured body (executor.mjs:196-206 supports one) gets comment-aware hanging only if it forges exactly that color string.
(2) Themes can change color only, because no class reaches the DOM. code-design §3 promised `<span class="tsr-tok-*">`, §4 a token style set {color, weight, italic, underline, overline, line-through, background}, and §5 'Theme = tag→style map in Config (JSON)'; none of these is built. Bold keywords or backgrounds are impossible.
(3) The model cannot carry a class at all. Styling has bits, sizeMul, fontFamily, lang, color and sizePx, and document-model §3's `dynClasses` was never implemented.

*Why ad hoc:* A semantic property (the token class) is encoded in a presentation attribute (a CSS color value), and a later stage reverse-engineers the semantics from that presentation. This crosses the model → presentation → semantics boundary backwards and makes user-produced code second-class.

*Proposed generalization (survey):* Add `StrRef cls` to Styling: the promised dynClasses, as an interned space-separated class list, with a model-version bump. foldTokens sets `cls = "tok-comment tok-comment-line"` from the full capture name instead of setting a color. Both serializers emit `class="… tsr-tok-comment tsr-tok-comment-line"`.

Add a `code.theme` setting that maps class → StyleDelta. It is applied at fold only for properties the engine must know (weight and italic, guarded by the duplex-font audit code-design §4 describes); everything paint-only stays in CSS keyed by class.

emit uses `hasClass(style, "tok-comment")`. Users mark runs with `ctors.style({class: 'tok-comment'}, …)` and are on equal footing. The token answer format becomes (start, end, classIdx) plus a per-answer class-name table, so captures pass through instead of being collapsed to 14 tags.

*Verifier:* The color is set in the styleFor lambda at tokens.cc:41-51, and `tag == 3` is at tokens.cc:46. The other citations are verified: emit.cc:618-624, layout.cc:180, semantic_html.cc:73-77, model.h:29-40. dynClasses is unbuilt.

*Verifier notes:* - Styling::operator== and the StyleTable hash (model.h:36-39, 59-69) must include the class field.
- document-model §3 already specifies dynClasses as a sorted u32[] of interned ids. Use that instead of a space-separated string, so hasClass("tok-comment") is an integer test rather than a new string comparison.

### `api-measure-code/language-registry-scattered` — The language set and alias normalization are hard-coded in five places, with no runtime registration

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-22**
- locations: `tools/codehl-assets.mjs:23-41`; `runtime/src/worker/tokens.mjs:21-24`; `engine/test/native_tokens.h:15-24`; `engine/CMakeLists.txt:43-52`; `editors/vscode-tsm/src/tokens.js:24-34`; `engine/src/render/typeset_html.cc:332-338`; `engine/src/api/config.h:62-63`; `docs/code-design.md §1, §5`

The set of languages is defined five times:
- `GRAMMARS` in the asset build;
- the `LANGS` alias map in the worker;
- the `nativeGrammar` if-chain plus a compile-time `TSR_REPO_ROOT` query path in the native provider;
- the CMake source list;
- the editor, which handles tsm only.

Alias normalization exists only in the worker. The renderer looks up `codeFontFeaturesByLang` with the raw fence tag, so 'js' and 'javascript' need separate entries, and the token cache is also keyed by the raw tag.

Users cannot add a language at runtime. code-design §1 deliberately rejects runtime grammar *compilation* ('one pipeline beats two'), and §5 reduces the plugin surface to the `$.fence` override, which bypasses the grid's token path entirely.

*Why ad hoc:* The registry is implicit and replicated, so each consumer has its own partial view. The documented rationale rejects a second parsing mechanism, not data-driven registration of prebuilt side modules, so the current hard-coding is stricter than the decision requires.

*Proposed generalization (survey):* One manifest, `hl/languages.json`, generated by codehl-assets from a single source: `{name, aliases[], wasm, scm, overlays[], defaults: {fontFeatures?}}`.

Consumers:
- worker provider and editor read it;
- CMake `configure_file`s it into `gen/languages.h`, listing the statically linked grammars and EMBEDDED query text, which removes the TSR_REPO_ROOT runtime dependency;
- the engine normalizes `code.lang` through the alias table, delivered as a setting or returned as the canonical name inside the token answer.

Runtime registration: `engine.registerLanguage({name, aliases, wasmUrl, scmUrl, overlays})` adds an entry that loads a PREBUILT side module through the same web-tree-sitter pipeline. This keeps code-design §1's 'one pipeline' and adds no runtime compilation. A `providers['code.tokens']` override slot (per-resource-pull-plumbing) lets a JS tokenizer serve a DSL with the same output format.

*Verifier:* The report misquotes code-design §1. The decision is 'grammars are compiled at BUILD time — no runtime grammar loading of any kind', justified by 'a blog's language set changes at build cadence'. It does not reject only runtime compilation.

The five copies are verified: tokens.mjs:21-24, native_tokens.h:20-29, CMakeLists.txt:43-52, codehl-assets.mjs:23-41, and the editor. Also verified: the raw-tag lookup at typeset_html.cc:334-337 and the raw-tag token cache key at worker.mjs:28.

*Verifier notes:* A single build-time manifest subsumes all the copies and the alias normalization; keep that. Drop runtime `engine.registerLanguage`, or explicitly reopen code-design §1. Runtime extension is already possible through a provider override.

Normalize the language tag exactly once: the engine receives, or the token answer echoes, the canonical name. codeFontFeaturesByLang and every cache then key on the canonical name.

### `api-measure-code/literate-cpp-special-case` — Literate-fragment recognition is hard-wired to the cpp provider and absent from the native provider

- kind: adhoc · severity: low · verdict: accurate · plan: **P3-22**
- locations: `runtime/src/worker/tokens.mjs:79-93`; `runtime/src/worker/tokens.mjs:102-110`; `runtime/src/worker/tokens.mjs:124-129`; `docs/code-design.md §7`

`if (name === 'cpp') { fragments = literateSpans(text); … }` matches FRAGMENT_RE, blanks the matches to spaces before parsing, and injects `label` tokens at `pat: -1` so they win overlaps.

This is documented as deliberate (code-design §7: avoid vendoring and forking a 530K-line parser.c). But it is a per-language branch inside a generic provider. It cannot be enabled for C, Rust or Python, even though noweb is language-neutral, and it cannot be turned off per document. The native provider does not implement it (§7: 'goldens are unaffected'), so a provider feature has no deterministic test, contrary to code-design §2's 'highlighting becomes a deterministic golden stage'.

*Why ad hoc:* A per-language preprocessing rule is encoded as an `if` on the language name inside the generic provider, not declared as data. The documented rationale (don't fork the grammar) is fine and is fully preserved by the generalization below.

*Proposed generalization (survey):* Per-language `overlays: [{pattern, class: 'label', mask: true, priority: 'over'}]` in the language manifest (language-registry-scattered). They are applied once in the shared `hl.mjs` capture resolver (token-tag-table-copies) and in a small C++ overlay pass in the native provider, so native goldens cover it with a cpp-free test grammar (json plus an overlay fixture). Documents can opt in or out with `code.overlays` (a settings key) or the fence arg `(code.overlays: ['noweb'])`.

*Verifier:* This is documented in code-design §7 ('Applies to the cpp family only'; the native provider links json+tsm only, so goldens are unaffected).

*Verifier notes:* The cited code has a correctness bug the report missed. Masking replaces each fragment with ASCII spaces of equal UTF-16 length, and u16ToU8Map is then built from the MASKED text (tokens.mjs:107-111). A non-ASCII fragment name therefore shifts every later token offset.

Probe: tokenize('cpp', '<<初始化>>=\nint x = 1;') yields tokens covering "<<初始" and "��>", which split UTF-8 sequences.

The proposed overlay mechanism must mask with filler that preserves byte length, or compute the offset map on the original text.

### `api-measure-code/grid-is-codeblock-only` — The character grid (verbatim layout) is reachable only through codeblock and lives inline in layoutDoc with hard-coded policy

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-11**
- locations: `engine/src/emit/emit.cc:545-638`; `engine/src/emit/emit.h:73-92`; `engine/src/layout/layout.cc:112-290`; `engine/src/layout/layout.cc:145`; `engine/src/layout/layout.cc:157`; `engine/src/layout/layout.cc:168-171`; `engine/src/layout/layout.cc:196-212`; `engine/src/layout/layout.cc:217`; `engine/src/render/typeset_html.cc:298-428`; `docs/verbatim-design.md header, §1, §4, §6`

verbatim-design claims it 'Generalizes the CH4 character grid … into a generic monospace/verbatim text layer'. In practice:
- `FlowUnit::K::Code` is created only by `case Kind::codeblock`.
- It forces CLS_CODE plus `cfg.codeScale`, so the font is always monoFont.
- It reads codeblock-specific ArgKs (wrap, lineNo, hl, sidecar).

The grid algorithm (about 180 lines) sits inline in `layoutDoc` with policy hard-coded in code:
- `isBreakable` = {space, tab , ; ) } ] >};
- an ASCII-only lead-in scan for comment hanging;
- `if (cols > 0 && cols < 8) cols = 8` and `cc > colCap - 8`;
- 64su floors;
- a snap tolerance of `0.1 * chLpx`;
- the probe strings "0" and "中", interned per unit in emit.cc:553-554;
- the continuation strategy as a bare int (verbatim §4 lists fixed / hanging / comment-aware / bracket).

Poetry, ASCII diagrams, aligned plain text, 稿纸 manuscript grids and proportional verbatim cannot use the grid. Gutter numbering and highlighted-row backgrounds are code-only. The three-box model (gutter | body | sidecar with row-height coupling, verbatim §5) is generic in concept but is implemented as code-unit fields (`sidebarW`, `cells` reused for sidecar rows).

*Why ad hoc:* A general layout mechanism is bound to one node kind and one font role, and its policy is code rather than data. verbatim §6 deliberately keeps grid and KP as separate species, which is fine. The issue is that the grid species has only one producer.

*Proposed generalization (survey):* Use `FlowUnit::K::Verbatim` with a `VerbatimSpec` resolved from settings (global-feature-knobs-no-cascade):
`{fontRole, scale, wrap, contIndent: {Fixed n | Hanging n | CommentAware | Bracket}, breakAfter: {charClasses | tokenClasses}, minCols, align: {Budget | Snap}, probes}`.

Producers: codeblock, plus a general `verbatim(lines, opts)` constructor (or any block arg `layout: "grid"`).

Move the algorithm into `layout/verbatim.{h,cc}` as a pure function, `wrapRows(runs, spec, metrics) → rows`, unit-testable like solveGrid.

Model the three-box structure as a general row-synchronized unit, `RowSync {columns: [{kind: Markers|Grid|Flow, width: setting}], rows coupled by logical index}`. The code sidecar becomes one instance; parallel bilingual text and line-numbered poetry are others.

Break preference can come from token classes (punctuation/operator) rather than a char set. This answers verbatim §6's note that the provider channel is the place for language knowledge.

*Verifier:* 'Poetry, ASCII diagrams … cannot use the grid' is overstated. Any ```text fence or `codeblock("", body)` reaches FlowUnit::K::Code: with no or unknown lang it gets plain runs.

The real restrictions are:
- CLS_CODE, cfg.codeScale and monoFont are forced (emit.cc:551);
- the semantic output is `<pre><code>`;
- all knobs are code-specific.

The other citations are verified (layout.cc:112-290, 145, 157, 168-171, 196-212, 217; emit.cc:553-554).

*Verifier notes:* verbatim §6 (grid and KP stay separate species) is respected. Keep the char-class break preference as the fallback, because token classes are absent for unknown languages. Making wrapRows a pure function, like solveGrid, is a good testability step.

### `api-measure-code/sidecar-api-layer-rewrite` — Sidecar comments are a post-ops tree rewrite in api/doc.h, keyed by a magic role string, and lose source spans

- kind: adhoc · severity: high · verdict: partly · plan: **P2-13**
- locations: `engine/src/api/doc.h:84-150`; `engine/src/api/doc.h:109`; `engine/src/emit/emit.cc:579-602`; `engine/src/render/semantic_html.cc:240-247`; `runtime/src/worker/executor.mjs:196-206`; `docs/verbatim-design.md §5`

`Doc::extractSidecars` runs in the API layer, between instantiate and resolve. For every codeblock with `ArgK::sidecar` it:
- splits lines at the marker;
- re-interns the code body;
- parses note text with `parseInlineFragment`;
- appends `group{role:"sidecar-lines"}`.

emit recognizes that group by string-comparing the role (emit.cc:588). The semantic serializer skips EVERY group child of a codeblock (`if (n->kids[li]->kind == Kind::group) continue;`), assuming it is the sidecar.

Spans: the fragment receives the body text's span, which is empty. Probe (WASM): an unresolved `@nowhere` in a sidecar reports `ref-unresolved @[0,0)` instead of offset 64. `tsrc --stage=tree` on test/fixtures/code/sidecar.tsm shows every sidecar node, and the code body itself, at `@[0,0)`. The VSCode diagnostics and click-to-source mapping therefore point at the top of the file.

The `///` syntax desugars to no user-callable constructor (v2: every syntax form is sugar for a constructor). A `$.fence` handler can only reproduce a sidecar by forging the role string. Doc drift: verbatim §5 specifies a trailing `group{role:"sidecar"}` inside each line seq; the as-built structure is a single trailing group per codeblock.

*Why ad hoc:* A markup feature is implemented as an engine post-pass in api/, the layer the architecture says must stay a thin adapter (architecture §2.1). It is identified by a string literal duplicated in two files, plus an implicit 'any group' rule in a third, and it is invisible to and unreproducible by the user extension surface.

*Proposed generalization (survey):* Move splitting to the front end, where source offsets are known.

Codegen emits `ctors.codeblock(lang, body, {sidecar: '///', bodyOffset})`. The constructor splits lines in JS and parses each note through `ctx.m.parse(note, offset)`, which is backed by `tsr_parse_fragment(doc, str, baseOffset)`. That is the deferred WASM re-entry from architecture §2.5, and it already exists engine-side as `parseInlineFragment`. It returns an ops slice with correct spans.

The result has an explicit, documented shape: codeblock with body lines plus a `columns(side)` child of the RowSync unit (grid-is-codeblock-only). Alternatively it can use a role constant from a shared `roles.def` that emit and both serializers include, so no string literals are compared.

User fence handlers call the same constructor. The same mechanism finally delivers `m.parse` for fences, which verbatim-design's header lists as deferred.

*Verifier:* '`///` desugars to no user-callable constructor' is wrong. codegen emits `__fence("js", ({sidecar: "///", lineNo: 1}), body, 86)`, and the default path calls the public `codeblock(lang, body, {sidecar})` (executor.mjs:145, 196-199). A `$.fence` handler can call the same constructor because the document program closes over ctors.

What users cannot do:
- combine a sidecar with a structured (pre-highlighted) body, because extractSidecars requires a single text child (doc.h:90-91);
- supply sidecar content other than inline-markup text.

Root cause of the @[0,0) spans: the body text node is never spanned. `tsrc --stage=tree` shows `text @[0,0)` for the code body itself, even though __fence receives the body offset (86) and the default path drops it.

Verified: the role string at doc.h:109 and emit.cc:588, the skip of any group at semantic_html.cc:246, and the drift from verbatim §5.

*Verifier notes:* There is a cheaper fix that suffices:
(a) span the body text from the offset __fence already receives;
(b) have extractSidecars pass `sp.start + lineStart + cut + marker.size()` into parseInlineFragment;
(c) replace the "sidecar-lines" string with a structural marker (a dedicated ArgK or kind) shared by emit and both serializers;
(d) move the pass out of api/ into model/ as a post-instantiate normalization.

JS-side splitting through tsr_parse_fragment is a valid long-term route. But it needs the ops-slice return channel that the verbatim-design header defers, and it makes the executor depend on a live WASM doc during execution, including the Node render.mjs path.

### `api-measure-code/doc-typeset-hosts-layout-logic` — Doc::typeset hosts the per-FlowUnit-kind break dispatch and an F2 float tracker that duplicates layout's gap rules

- kind: adhoc · severity: medium · verdict: partly · plan: **P1-15**
- locations: `engine/src/api/doc.h:258-361`; `engine/src/layout/layout.cc:26-29`; `docs/figure-design.md §4`

The API-layer loop switches on `FlowUnit::K` (floating Image, Table, Code-with-sidecar, Text). It computes table cell widths (`colW - 2*pad` with a `cellW < 64` floor) and occlusion widths (`flOccl < lw.constant - 64`).

Its float tracker re-derives layout's vertical rules; the comment says 'mirroring layout's gap accounting': `ui > 0 ? (u.tightAbove ? paraGapSu / 3 : paraGapSu) : (firstBlock ? 0 : paraGapSu)`, which matches layout.cc:28. It also assumes every line is `baseLeading` tall (`breakpoints.size() * baseLeading`), although layout grows lines for inline math and oversized boxes.

figure-design §4's decision (store the decisions on the units so layout replays them) is sound. The problem is that the logic lives in api/ and duplicates layout's rules: each new unit kind adds a branch here as well as in layout and render.

*Why ad hoc:* Algorithmic layout policy lives in the boundary layer, and two copies of the gap rules must stay in sync by comment. The API layer knows every unit kind.

*Proposed generalization (survey):* Add a `break/` stage entry, `BreakPlan breakDoc(tops, settings, BreakCache&)`, that owns the dispatch through a per-kind ops table:
`struct UnitOps { LineWidths widths(const FlowUnit&, const FlowCtx&); void breakInto(FlowUnit&, const LineWidths&, BreakCache&); i64 advance(const FlowUnit&, const FlowCtx&); }`.

The vertical rules are defined once in `layout/geometry.h` (`gapBefore(unit, prev, first)`, `lineAdvance(line, metrics)`), and both the tracker and layoutDoc use them. Doc::typeset only sequences stages. New unit kinds (e.g. Verbatim, RowSync) register one UnitOps row.

*Verifier:* Where the tracker lives is documented. figure-design §4 places the FloatTracker 'inside Doc::typeset()'s unit walk (the only place that already visits units in reading order with breakpoints in hand)'. The baseLeading-per-line approximation is explicitly accepted there ('a taller line … under-clears by the excess') and in v2 §10.

The undocumented, accidental parts are confirmed: the gap rules are duplicated (doc.h:282-283 vs layout.cc:28; inter-block gap at layout.cc:606), and the table and code-sidecar dispatch sits in api/ (doc.h:322-342).

*Verifier notes:* Moving the dispatch into break/ with a per-kind table and a shared gapBefore() is golden-neutral and correct.

A shared lineAdvance() is NOT golden-neutral. The tracker deliberately counts baseLeading per line, while layout uses max(baseLeading, asc+desc) (layout.cc:596-597). Unifying them changes float narrowing in goldens. That is a behaviour fix to a documented approximation and should be presented as one.

### `api-measure-code/adhoc-invalidation-flags` — Stage invalidation is two booleans set per case; width changes do not invalidate width-dependent emit products

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-03**
- locations: `engine/src/api/doc.h:51`; `engine/src/api/doc.h:57-58`; `engine/src/api/doc.h:79-80`; `engine/src/api/doc.h:232`; `engine/src/api/doc.h:242`; `engine/src/api/doc.h:363`; `engine/src/api/doc.h:392`; `engine/src/measure/measure.h:33-36`; `engine/src/emit/emit.cc:591`; `engine/src/emit/emit.cc:721-733`; `runtime/src/worker/worker.mjs:228-260`; `docs/design-decisions-v2.md:210`; `docs/document-model.md §0`

Q4: Doc tracks only `emitted` and `laidOut`.
- `laidOut` is written (doc.h:80, 363, 392) but never read.
- `emitted` is cleared by ingest, provideTokens, provideImage and the math side channel.
- `setWidth` clears only the dead `laidOut`.

But emit reads `cfg.widthPx` for the image display box (emit.cc:721-733) and the sidecar width (emit.cc:591), so `relayout` and `paginate` reuse stale emit products. Probe (WASM): an image with w:1000, h:500 at width 600 renders at 600px; after `tsr_set_width(300)` and a converged typeset it is still 600px, while a fresh doc at 300 gives 300px. `paginate` (worker.mjs:228-246) changes the width the same way.

`MetricStore::invalidate()` is never called. There is no dppx or font invalidation, although document-model §0 says 'entries invalidated by dppx change'. Diagnostics are append-only, so re-running emit duplicates warnings.

v2 :210's settled stance is 'a resize is a full re-typeset'. setWidth is actually a partial re-typeset that relies on an implicit, and wrong, assumption about what depends on width.

*Why ad hoc:* Dependencies are encoded as hand-placed flag clears at each mutation site, not derived from what each stage actually read. Every new width-dependent decision in emit silently breaks relayout.

*Proposed generalization (survey):* A product graph with recorded dependencies.

Each stage product is stamped per TopBlock:
`struct Stamp { u64 inputKey; u64 settingsAffectsRead; SmallVec<pair<ResId,u32 ver>> res; }`
`struct ParaProducts { Stamp emit, brk, lay; TopBlock tb; ParaFrame frame; DiagSlice diags[kStages]; }`

Settings are read through accessors that OR the setting's Affects bit into the active stamp. Resources are read through `rc.get`, which records (ResId, version).

`setWidth` becomes a settings change on `page.width`. The Doc drops exactly the products whose stamps read it. Better still, move width-dependent sizing (image display box, sidecar width) from emit into break/layout so that emit is width-independent by construction, which matches v2 :210's intent for emit.

Diagnostics are stage-owned slices, replaced on re-run. `MetricStore` entries carry the `fontFace` ResId version, so a late font or a dppx change invalidates through the same mechanism.

*Verifier:* Verified:
- laidOut is written at doc.h:80, 363 and 392 and never read;
- MetricStore::invalidate has no caller;
- my probe: an image with w:1000, h:500 renders at width:600px, and after tsr_set_width(300) plus a converged typeset it is still 600px.

*Verifier notes:* The primary fix should be structural, and it is already implied by the lifetimes table in document-model §0 (BlockStreams are rebuilt only by recompile): emit must not read cfg.widthPx. Move the image display box (emit.cc:721-739) and sidebarW (emit.cc:591) into the break phase.

A full dependency-recording product graph is an incremental system, which v2 §9 (line 210: 'a resize is a full re-typeset, not an incremental system') settled against. Adopt it only if that stance is formally revised. Do not version MetricStore by fontFace either (pages-design W).

Stage-owned diagnostic slices that are replaced on re-run are sound and cheap on their own.

### `api-measure-code/adhoc-caches` — The editor fast path is five independent caches with ad hoc keys and eviction, not engine-side reuse

- kind: adhoc · severity: high · verdict: accurate · plan: **P1-21**
- locations: `engine/src/break/break.cc:147-191`; `runtime/src/worker/worker.mjs:16-35`; `runtime/src/worker/worker.mjs:41-65`; `runtime/src/worker/worker.mjs:77-90`; `runtime/src/worker/worker.mjs:166`; `runtime/src/worker/canvas_measure.mjs:8-40`; `runtime/src/worker/executor.mjs:43-56`; `runtime/src/worker/executor.mjs:242-266`; `docs/editor-design.md §2`

Q4: every keystroke builds a fresh Doc (worker.mjs:166), because the single document arena cannot be reset (architecture §2.3). Reuse comes from these caches instead:
- KP: a process-global `static unordered_map` keyed by an FNV hash of the DP inputs. Keeping it correct depends on a documented manual rule ('Any new field the DP starts reading MUST be added to breakKey'). It stores the hash only, with no equality check, and clears everything at 16384 entries.
- Measurer: keyed by the CSS font shorthand string → word px. Clears everything at 200k entries. It is cleared only when a NEW font is declared, not when a font lands.
- Tokens: keyed by `lang\0text`, clear-all at 400.
- imageDims: keyed by raw src, kept forever, including failures; not keyed by base URL.
- loadedFonts: keyed by family|weight|style, kept forever; an entry is marked before the load succeeds, so a failed font is never retried.
- Bibliography: NO cache. Every keystroke refetches and reparses the CSL-JSON in finishBibliographies.

The engine's MetricStore dies with each Doc, so every word in the document crosses the WASM boundary again on every keystroke (the canvas_measure.mjs header acknowledges this).

*Why ad hoc:* Incremental reuse was added one cost center at a time, each cache with its own key, eviction and failure policy. None of them is tied to the engine's model of what changed, so correctness depends on hand-kept invariants: the breakKey field list, and the rule that the font cache is cleared before load.

*Proposed generalization (survey):* Make the Doc persistent across revisions with a two-tier memory model.
- SessionArena: Interner, StyleTable, SettingsTable, MetricStore, ResourceTable. It persists.
- RevisionArena: source, AST, tree, tops, layout. It is reset on update.

Add `tsr_update(doc, src)`. It recompiles into the revision tier and keys each top-level paragraph by `hash(post-resolve subtree serialization ⊕ SettingsId ⊕ StyleIds ⊕ resource versions)`, reusing that paragraph's ParaProducts on a match (verified by comparing the stored key material, not by hash alone). The global KP static becomes a per-session memo inside ParaProducts; no manual field list is needed, because the key is the block vector itself, compared on hit.

The resource table persists, so widths, tokens and image dims are not re-requested. Host providers keep only I/O caches, through one shared `LruCache({maxBytes, ttlMs, failureTtlMs})` helper. A compaction rule (rebuild the session when the interner exceeds N× the live vocabulary) bounds memory.

*Verifier:* The caches are a documented design (editor-design §2). The report missed one more failure policy: tokens.mjs:52-55 caches grammar-load failures for the worker's lifetime (langs.set(name, null)), and the empty result also enters tokenCache. A transient fetch failure therefore disables highlighting for that language for the whole session.

*Verifier notes:* The SessionArena/RevisionArena split reverses the explicit arena-per-document decision in architecture §2.3 ('wholesale free is both the fastest and the simplest correct policy'). A lighter mechanism reaches the same per-keystroke goal and keeps §2.3: a `tsr_doc_new_from(prev)` / seeding step that copies resolved answers into the new doc engine-side, namely:
- word widths and vmets keyed by (string, font descriptor);
- token answers keyed by (canonical lang, body);
- image dims keyed by resolved URL.
No word then crosses the boundary again. Separately, the KP memo should verify hits against stored key material. Paragraph-product reuse can follow later if the profile demands it.

### `api-measure-code/per-feature-api-entry-points` — The C ABI grows one export per feature, stage inspection is native-only, and the documented general API was not built

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-37**
- locations: `engine/src/api/wasm_api.cc:18-21`; `engine/src/api/wasm_api.cc:39-216`; `engine/src/api/native_cli.cc:254-321`; `editors/vscode-tsm/src/preview.js:128`; `docs/architecture.md §2.1, §2.5`; `docs/document-model.md §10`

Q5: there are 24 exports. They include `tsr_set_punct_compress`, `_set_font`, `_set_cjk_font`, `_set_lang`, `_set_snap_kerning`, `_set_code_features`, `tsr_provide_image`, `_tokens`, `_word`, `_vmet`, and three render entry points (`tsr_render`, `tsr_render_semantic`, `tsr_render_pages`) that share per-kind output buffers (`jsOut`, `htmlOut`, `semOut`, `reqOut`, `diagOut`; `tsr_render_pages` overwrites `htmlOut`).

architecture §2.5 specifies `tsr_doc_new(config_json)`, a batched `tsr_measure_provide(doc, buf)`, `tsr_render_typeset(doc, range?)`, `tsr_layout_info`, `tsr_parse_fragment`, `tsr_diagnostics` (JSON per document-model §10) and `tsr_relayout`. None was built as such.

Diagnostics are a text format, `sev code @[s,e) msg`, which the VSCode extension parses with a regex, so the text layout is the de facto API. Stage dumps (blocks, breaks, layout, mathbox) exist only in tsrc, so 'every stage inspectable' does not hold for the browser or playground.

tsrc dispatches stages through an if/else chain. architecture §2.1 lists a 'typeset' stage that does not exist (`tsrc --stage=typeset` prints 'unknown stage typeset'; the stage is 'html'), and paged rendering has no CLI stage.

*Why ad hoc:* The ABI mirrors the feature list instead of the pipeline's abstractions (configure, ingest, step, exchange resources, render a target, inspect a stage). Each feature therefore adds exports, buffers and worker code.

*Proposed generalization (survey):* A pipeline-shaped ABI:
`tsr_doc_new()` / `tsr_doc_free`
`tsr_set_config(doc, json) → nDiags`
`tsr_update(doc, src)` (compile, keeping the session tier)
`tsr_ingest(doc, ops, len)`
`tsr_typeset(doc) → 0 OK | 1 NEED_RESOURCES`
`tsr_requests(doc)` / `tsr_provide(doc, buf, len)`
`tsr_render(doc, const char* target /* typeset|semantic|pages */, const char* optsJson)`
`tsr_get(doc, const char* what /* js|sourcemap|diags.json|layout-info|height */)`
`tsr_dump(doc, const char* stage)`

A `StageRegistry` table, `{name, needs: Compiled|Ingested|Typeset, dump(Doc&) → string}`, lives in engine/src/api/stages.h and is shared by tsrc, tests.cc and tsr_dump, so stage names cannot drift between docs, CLI and tests. Diagnostics are JSON with `related[]` spans, per §10.

*Verifier:* Verified:
- the 24 exports;
- the per-kind buffers (wasm_api.cc:20), with tsr_render_pages overwriting htmlOut (:205);
- the regex diagnostic parse at preview.js:128;
- `tsrc --stage=typeset` printing 'unknown stage typeset'.

The stage if/else chain is at native_cli.cc:69-104, not :254-321.

*Verifier notes:* A string-dispatched `tsr_render(doc, target)` or `tsr_dump(doc, stage)` should be backed by the generated StageRegistry table rather than a hand-written string switch. Otherwise the drift just moves inside the ABI.

### `api-measure-code/native-driver-config-divergence` — Native drivers duplicate the pull loop and configure by filename substrings; tsrc cannot reproduce the goldens

- kind: adhoc · severity: medium · verdict: partly · plan: **P1-03**
- locations: `engine/src/api/native_cli.cc:10`; `engine/src/api/native_cli.cc:240-252`; `engine/src/api/native_cli.cc:276-281`; `engine/test/tests.cc:314-325`; `engine/test/tests.cc:414-426`; `engine/test/tests.cc:464`; `engine/CMakeLists.txt:57-64`; `docs/document-model.md §12`

`typesetWithMock` exists twice, in tsrc and in tests.cc. The golden runner chooses config from fixture-name substrings: 'indent' → paraIndentEm=2, 'punct-full', 'punct-none', and 'paged' → renderPaged(240). It also sets baseSizePx=16, while tsrc keeps the Config default of 18.

Result: `tsrc --stage=layout` on test/fixtures/code/wrap.tsm gives `doc h=11750su`, but the golden test/golden/code/wrap.layout.txt says `doc h=8909su`. This contradicts document-model §12 ('tsrc --stage=… and the golden tests share it verbatim').

There is no channel to golden snap-kerning, English supplements, codeScale or the sidecar fraction. That is how the snap-kerning render bug (other_issues) shipped without coverage.

tsrc is a tool in src/api, yet it includes `../../test/native_tokens.h` and reads highlight queries from a compile-time absolute `TSR_REPO_ROOT`.

*Why ad hoc:* Test configuration is encoded in file names, and the CLI and the test runner are two drivers with two default profiles. Both are symptoms of the missing settings registry and the missing shared driver.

*Proposed generalization (survey):* Put `driveToCompletion(Doc&, ProviderSet&, int maxRounds)` in engine/src/api/driver.h, shared by tsrc, tests and fuzzers. Per-fixture settings come from a `% tsr-settings: {...}` first-line comment or a sibling `<fixture>.settings.json`, parsed by the generated settings parser (config-plumbing-per-knob). tsrc gets `--settings`, `--set k=v` and `--profile=golden`, where golden is the shared default the runner uses. The native token provider moves to `engine/src/code/native_provider.{h,cc}` as an optional target, with queries embedded at build time from the language manifest.

*Verifier:* tsrc's driver is at native_cli.cc:24-36 and its config at :38-65. The cited :240-281 do not exist.

The 'share it verbatim' phrase in document-model §12 refers to the dump functions, and tsrc and tests do share those. The real divergence is the config profile: tests.cc:416 sets baseSizePx=16, while tsrc keeps the Config default of 18.

Verified: `tsrc --stage=layout` on code/wrap gives doc h=11750su, while golden/code/wrap.layout.txt has 8909su.

*Verifier notes:* The fix is mechanical once the settings parser exists. A `--profile=golden` that reproduces tests.cc:415-426 keeps every golden byte-identical.

### `api-measure-code/resource-io-paths` — Resource I/O is special-cased per resource: URL base, transport, cache and failure policy all differ

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-21**
- locations: `runtime/src/worker/executor.mjs:43-56`; `runtime/src/worker/executor.mjs:185-188`; `runtime/src/worker/executor.mjs:242-266`; `runtime/src/worker/worker.mjs:41-65`; `runtime/src/worker/worker.mjs:66-90`; `runtime/src/worker/worker.mjs:202`; `runtime/src/main/shell.mjs:95-110`; `runtime/src/main/shell.mjs:218-229`

Each resource handles I/O its own way:
- Bibliography: the executor's `loadResource` uses Node fs (rootDir/baseDir) or `fetch(new URL(src, baseUrl))`. It loads after the program has run and turns a failure into an error node, 'bib-load'.
- Images: the worker calls `fetch(src)` WITHOUT the baseUrl the worker received. Relative srcs resolve against the worker script URL (runtime/src/worker/), so they fail, after which every relative image goes through a one-off main-thread RPC (`'image-dims?'` / `'image-dims'` messages). That is a second message protocol outside architecture §4.3.
- Fonts: the worker creates `FontFace(url(src))`, also resolved against the worker URL, while the shell injects an `@font-face` with the same src string resolved against the page. That is two loads that can hit different URLs. Fonts are loaded before every typeset, outside the engine's knowledge, so document-model §10's 'measure-fallback' diagnostic cannot exist.
- `#use`: plain JS import.

Timeouts are also per-resource: 4000 ms for fonts, 15000 ms for main-thread image dims.

*Why ad hoc:* There is no shared notion of a resource locator or transport. Each feature resolved URLs, picked a thread and set its own timeout. Only the bibliography got the base URL, because it happened to live in the executor.

*Proposed generalization (survey):* Put a shared `ResourceLocator {baseUrl, baseDir, rootDir, resolve(src)}` and a transport choice (worker fetch | main-thread RPC | Node fs), declared per provider capability, inside the provider registry (per-resource-pull-plumbing). Timeout and failure handling are uniform: `Answer{status: Failed, diag}`, and the engine turns that into a diagnostic.

Bibliography data becomes a `dataText` resource that the executor requests through `ctx.resource` and the engine phase could also use. Fonts become `fontFace` resources requested by the engine for each StyleDesc family, answered loaded | fallback | failed. This enables 'measure-fallback' and records the metric dependency on the face version, which is consistent with pages-design W: declare fonts, load them before the first measure, and treat a late landing as an explicit, policy-controlled event.

*Verifier notes:* A shared ResourceLocator (baseUrl/baseDir/rootDir) and uniform failure handling are sound, and they fix the relative-image and font-URL divergence.

Turning fonts into engine-requested fontFace resources whose version invalidates metrics conflicts with pages-design §1 W ('No re-typeset machinery … settling impossible to observe'). Keep fonts declared and preloaded. Report the face status (loaded, fallback or failed) so the measure-fallback diagnostic can exist, and key the host measurer cache by face status.

The bibliography is execute-time, so it uses a host provider, not the engine table.

### `api-measure-code/magic-policy-constants` — Policy constants in the API, measure, worker and grid code are bare literals

- kind: adhoc · severity: low · verdict: partly · plan: **P1-03, P3-02**
- locations: `runtime/src/worker/worker.mjs:96`; `engine/src/api/native_cli.cc:243-244`; `runtime/src/worker/worker.mjs:63`; `runtime/src/worker/worker.mjs:74`; `runtime/src/worker/worker.mjs:26`; `runtime/src/worker/canvas_measure.mjs:8`; `engine/src/break/break.cc:191`; `engine/src/api/doc.h:327`; `engine/src/api/doc.h:345`; `engine/src/layout/layout.cc:127`; `engine/src/layout/layout.cc:145`; `engine/src/layout/layout.cc:157`; `engine/src/layout/layout.cc:217`; `engine/src/emit/emit.cc:573`; `engine/src/emit/emit.cc:553-554`; `engine/src/code/grid.h:41-46`

Examples:
- the round cap of 64 (worker and native, independently);
- 4000 ms font deadline and 15000 ms main-dims timeout;
- cache caps of 400 (tokens), 200000 (words) and 16384 (KP), all evicted by clearing everything;
- the native image stub of 512x384;
- `cellW < 64` and `flOccl < lw.constant - 64`;
- `lineWidthCode < 64`, a minimum of 8 grid columns, and `colCap - 8`;
- snap tolerance `0.1 * ch`;
- the hl range guard `k - lo < 10000`;
- the grid probe strings "0" and "中";
- the Stern–Brocot q ≤ 7, which is documented as a user bound in verbatim §2.

Some of these are user-visible policy (minimum columns, tolerance, timeouts); others are safety rails.

*Why ad hoc:* Policy and safety rails are mixed into code as unnamed numbers. Users cannot tune the policy ones, and the duplicated ones (the 64 rounds) can drift.

*Proposed generalization (survey):* Policy values become registered settings (`code.grid.minCols`, `code.snap.tolerance`, `code.snap.maxQ`, `resources.timeoutMs.<provider>`, `driver.maxRounds`). Safety rails become named constexprs in one header, e.g. `kMinLineSu = 64` and `kHlRangeMax`. Cache sizes become LruCache byte budgets (adhoc-caches).

*Verifier:* - The native round cap and image stub are at native_cli.cc:27-28, not 243-244.
- The Stern–Brocot q≤7 bound is grid.h:34 (`if (qm > 7) break;`); grid.h:41-46 is the result assignment.
- Missed: the canvas_measure.mjs:46-47 vmet fallback `actualBoundingBox… * 1.2`.
- The remaining locations are verified.

*Verifier notes:* Separating user-visible policy (settings) from safety rails (named constexprs) is the right split.

### `api-measure-code/snap-kerning-duplicate-style-attr` — Snap-kerning emits a second style attribute, so the letter-spacing never applies in browsers (verified)

- kind: issue · severity: high · verdict: accurate · plan: **P0-10, P1-03**
- locations: `engine/src/render/typeset_html.cc:410-421`; `engine/src/render/typeset_html.cc:80-90`; `engine/src/render/typeset_html.cc:44-50`

In the code-row path of `renderTypeset`, `runStyleAttr` always emits ` style="font-size:…"` for code runs, because codeScale 0.85 makes `sizeMul != 1` (styleInto, typeset_html.cc:44-50). The snap branch then appends ` data-snap="1" style="letter-spacing:…"`.

Probe (WASM, verbatimSnapKerning on, CJK probe 0.95em): `<span class="tsr-r tsr-code" style="font-size:15.3px" data-snap="1" style="letter-spacing:0.177px">`. HTML parsers drop the duplicate attribute, so the V1.5 snap feature is a no-op in every browser, and its grid alignment silently degrades to budget mode.

There is no golden or e2e coverage: tests.cc only unit-tests solveGrid, and no fixture can enable snap (native-driver-config-divergence). Fix: merge the letter-spacing into the single style string.

*Verifier:* My probe reproduces the output exactly: `<span class="tsr-r tsr-code" style="font-size:15.3px" data-snap="1" style="letter-spacing:0.177px">`. The snap branch is at typeset_html.cc:417-422. A grep of test/, tools/ and editors/ finds no snap coverage.

*Verifier notes:* This is a plain bug. The fix is a single style builder per span. It also needs a golden channel to enable snap, which depends on native-driver-config-divergence.

### `api-measure-code/relayout-stale-emit` — relayout and paginate reuse emit products computed at the old width (verified)

- kind: issue · severity: high · verdict: accurate · plan: **P0-11, P1-16**
- locations: `engine/src/api/doc.h:392`; `engine/src/emit/emit.cc:591`; `engine/src/emit/emit.cc:721-733`; `runtime/src/worker/worker.mjs:228-260`

`setWidth` only clears the unused `laidOut` flag. Image display boxes (emit.cc:721-733) and the sidecar column width (emit.cc:591) are baked at emit time from `cfg.widthPx`.

Probe: an image with w:1000, h:500 at 600px renders 600px wide; after `tsr_set_width(300)` it is still 600px, overflowing the 300px measure. A fresh doc gives 300px.

Print pagination (worker paginate → `tsr_set_width(pageWidthPx)`) is affected the same way. Minimal fix: `setWidth` sets `emitted = false`, consistent with v2 :210 ('a resize is a full re-typeset'). Structural fix: adhoc-invalidation-flags.

*Verifier notes:* The proposed minimal fix (`emitted = false` in setWidth) makes every relayout and paginate re-run emitDoc for the whole document. That appends emit diagnostics again (they are append-only) and grows the arena per resize or print. Either clear emit-stage diagnostics before re-emitting, or (better) take width out of emit as described under adhoc-invalidation-flags.

### `api-measure-code/image-w-only-overwritten` — An author-declared w without h is overwritten by the intrinsic width (verified)

- kind: issue · severity: high · verdict: accurate · plan: **P0-11, P1-19**
- locations: `engine/src/api/doc.h:212-227`; `engine/src/emit/emit.cc:724-729`

`#image("x.png", {w: 100})` with an 800x400 provider renders at the full measure (600px), not 100px. The cause is that `provideImage` setNum's `ArgK::w`. This violates figure-design §3 rule 1. See image-dims-in-author-args for the structural fix.

*Verifier:* Additional deviation from figure-design §3: emit.cc:728 applies scale before w.

*Verifier notes:* The local fix is to stop setNum'ing author slots: provideImage should store intrinsic dims separately. The structural fix is image-dims-in-author-args, with the display box moved out of emit.

### `api-measure-code/snap-ignores-sidecar-partition` — In snap mode the column count ignores the sidecar partition, so code overflows into the sidecar column (verified)

- kind: issue · severity: medium · verdict: accurate · plan: **P1-03, P3-11**
- locations: `engine/src/layout/layout.cc:121-128`; `engine/src/layout/layout.cc:151-165`

The budget path computes `cols = lineWidthCode / chSu`. The snap branch recomputes `cols = (i32)(lineWidth / atomSu)`, using the FULL measure instead of `lineWidthCode`.

Probe: a 99-char line with a `///` sidecar at width 400 wraps into 5 rows without snap but only 3 rows with snap, so rows are drawn wider than the code box, under the sidecar. This is masked today because snap letter-spacing never applies (snap-kerning-duplicate-style-attr), but the column budget is still wrong.

*Verifier:* layout.cc:162 computes `cols = (i32)(lineWidth / atomSu)`. My probe confirms it: a 99-char line with a `///` sidecar at width 400 gives 5 rows without snap and 3 rows with snap.

*Verifier notes:* The fix is to use lineWidthCode at layout.cc:162.

### `api-measure-code/late-font-stale-measure-cache` — A font that lands after the 4 s deadline leaves fallback widths in the persistent measurer cache

- kind: issue · severity: medium · verdict: accurate · plan: **P0-11**
- locations: `runtime/src/worker/worker.mjs:41-65`; `runtime/src/worker/canvas_measure.mjs:17-24`

`loadFonts` calls `measurer.clearCache()` BEFORE racing the loads against 4 s. A font that lands later is `self.fonts.add`'ed, but nothing clears the cache. In an editing session, cached words keep their fallback widths while new words are measured in the real face, so one document mixes metrics while paint uses the real face throughout. This leads to overflow and justification errors.

This contradicts editor-design §2 ('Invalidated when a new FontFace lands'). Also, `loadedFonts.add(key)` runs before the load succeeds, so failed fonts are never retried for the worker's lifetime. pages-design §1 accepts fallback metrics only 'until the file lands'.

*Verifier notes:* Verified at worker.mjs:43-50: loadedFonts.add runs before the load, and clearCache runs before the race. Clearing (or versioning) the measurer cache when a late face lands affects only later docs, which is consistent with W. Failed loads should be retried.

### `api-measure-code/main-dims-rpc-race` — Concurrent main-thread dimension requests for the same src can leave a promise unresolved forever

- kind: issue · severity: medium · verdict: accurate · plan: **P0-11**
- locations: `runtime/src/worker/worker.mjs:69-76`; `runtime/src/worker/worker.mjs:269-275`

`askMainForDims` stores ONE resolver per src: `mainDims.set(src, resolve)` overwrites. If two typesets (for example an API consumer's relayout racing an update, or two docs) miss the worker cache for the same cross-origin src concurrently:
1. The second set replaces the first resolver.
2. The reply resolves only the second.
3. The first's 15 s timeout runs `if (mainDims.delete(src)) resolve(...)`, but the entry is already gone, so delete returns false.

The first promise never settles. That typeset hangs, its WASM doc is never freed, and the shell promise never resolves. Fix: a Map of src → resolver list, or a per-request id.

*Verifier notes:* The race is reachable through the unserialized concurrent `update` messages described in worker-no-per-doc-serialization. A per-request id fixes it.

### `api-measure-code/worker-no-per-doc-serialization` — The worker processes messages for the same docId concurrently, so the last finisher wins

- kind: issue · severity: medium · verdict: accurate · plan: **P0-11**
- locations: `runtime/src/worker/worker.mjs:216-218`; `runtime/src/worker/worker.mjs:269-280`; `runtime/src/main/shell.mjs:391-420`

`onmessage` launches the async `typeset`, `update`, `relayout` and `paginate` handlers without a per-docId queue. If an older update finishes after a newer one, `docs.set(docKey, doc)` installs the OLDER document, and subsequent relayout and paginate render stale content.

The VSCode preview serializes on its own side (preview.html pump), but the public shell API (`handle.update`, `handle.relayout`) does not. paginate also temporarily changes the live doc's width, which can interleave with a relayout.

*Verifier:* It is worse than described. When the older update finishes last, worker.mjs:216-217 (`prev !== doc` → `_tsr_doc_free(prev)`) FREES the newer doc before installing the older one.

*Verifier notes:* A per-docId promise chain in the worker is the minimal fix.

### `api-measure-code/image-fetch-serial-and-decode` — Image dimensions are fetched serially, by fully decoding each image, from the wrong base URL

- kind: issue · severity: medium · verdict: accurate · plan: **P0-11**
- locations: `runtime/src/worker/worker.mjs:77-90`; `runtime/src/worker/worker.mjs:106-110`; `runtime/src/worker/worker.mjs:202`

measureLoop does `for (const im of req.images) { await imageSize(im.src) … }`, so N images cost N sequential round trips. Each one downloads and fully decodes a bitmap (`createImageBitmap`) just to read its width and height; header sniffing (PNG IHDR, JPEG SOF, GIF, WebP) would need about 1 KB.

`fetch(src)` ignores the `baseUrl` the worker received (it is passed only to `execute`), so relative srcs resolve against runtime/src/worker/, fail, and every relative image pays a failed fetch plus the main-thread fallback. Fix in place: `Promise.all`, `new URL(src, baseUrl)`, and Range or header sniffing.

*Verifier notes:* Promise.all and `new URL(src, baseUrl)` are sound. Header sniffing has two traps:
- createImageBitmap applies EXIF orientation, so raw JPEG SOF dimensions are swapped for rotated photos;
- SVG has no pixel header and needs width/height/viewBox parsing.
Sniff with a decode fallback for those cases.

### `api-measure-code/per-word-boundary-marshalling` — Measurement crosses the WASM boundary once per word, with JSON and re-interning, on every keystroke

- kind: issue · severity: medium · verdict: accurate · plan: **P1-19**
- locations: `engine/src/api/wasm_api.cc:96-133`; `engine/src/api/wasm_api.cc:178-180`; `runtime/src/worker/worker.mjs:121-134`

`tsr_measure_requests` serializes every missing word as a JSON string. JS parses that, then for each word calls `stringToNewUTF8` + `_tsr_provide_word` (which runs `strs.intern(word)`, hashing the word again) + `_free`.

Because the Doc and its MetricStore are rebuilt per keystroke, this happens for the whole vocabulary on every edit, even though the JS measurer cache hits. architecture §2.4/§2.5 specified a batched `tsr_measure_provide(doc, buf)`.

An answer buffer aligned with the request (one Float64Array, no strings) removes about 3 calls per word.

*Verifier notes:* The traffic is further multiplied because metrics are keyed by presentation StyleId: identical fonts differing only in color or decoration are requested separately (see missed items).

### `api-measure-code/kp-cache-unverified-hash` — The KP memo trusts a 64-bit hash with no equality check, and its field packing XORs shifted signed values

- kind: issue · severity: low · verdict: partly · plan: **P0-11**
- locations: `engine/src/break/break.cc:147-191`

`breakLinesRetry` returns `cache[key]` on a hash match without comparing inputs. Inside the hash, each block is mixed as `(width << 21) ^ (spaceWidth << 42) ^ breakWidth` using sign-extended i64→u64 values. Hyphen junction kerns make `width` negative (emit.cc:961), so the shifted ranges overlap.

A collision returns breakpoints for a different block vector. Their counts can exceed `blocks.size()`, and layout then indexes out of range (undefined behaviour).

The probability is small, but the cache is process-global and long-lived in editor sessions, and correctness also depends on the manual 'MUST add new fields' rule. Fix: mix each field separately and store the key material (or a 128-bit hash plus the block count) to verify on hit.

*Verifier:* Negative block widths come from the junction kern at emit.cc:953 (`b.width = suRoundPx(k)`). emit.cc:961 is the punctuation glyph width, which is clamped to ≥0. The field-packing overlap at break.cc:168 is confirmed.

*Verifier notes:* breakLines reads exactly width, spaceWidth, breakWidth and breakPenalty (break.cc:30-37, 67-70), so the key is complete. Only the mixing and the missing verification are at fault.

### `api-measure-code/diag-format-and-duplication` — Diagnostics are append-only text whose layout is the de facto API, and re-runs duplicate them (verified)

- kind: issue · severity: low · verdict: accurate · plan: **P0-11**
- locations: `engine/src/api/doc.h:394-403`; `engine/src/api/doc.h:247-255`; `editors/vscode-tsm/src/preview.js:128`

Re-emit triggered by math text metrics appends emit-time warnings again; the probe shows two identical 'image-src' warnings. Several emit-produced diagnostics carry an empty span: the `#image` splice reports `@[0,0)`, and sidecar refs do too. The VSCode extension parses the text with a regex (preview.js:128), so changing the format breaks the editor. document-model §10 specifies JSON with `related` spans.

*Verifier:* document-model §0 declares diagnostics append-only by design, rebuilt by recompile. The duplication comes from re-emit within one compile, which that table does not anticipate.

*Verifier notes:* Making diagnostics stage-owned slices fixes this and the relayout-fix side effect.

### `api-measure-code/nul-byte-in-worker-source` — worker.mjs contains a raw NUL byte, so git treats the file as binary and review diffs are hidden

- kind: issue · severity: low · verdict: accurate · plan: **P0-11**
- locations: `runtime/src/worker/worker.mjs:25`; `runtime/src/worker/worker.mjs:28`

The token cache key separator is a literal U+0000 in the source (lines 25 and 28: `lang + '<NUL>' + text`). `git show c916342 --stat` reports `runtime/src/worker/worker.mjs | Bin`, so changes to the pull-loop driver no longer appear as text diffs in reviews. Use the `'\0'` escape instead.

*Verifier:* Verified: `git show --stat c916342` shows `worker.mjs | Bin 10292 -> 10418 bytes`, and `cat -v` shows ^@ at lines 25 and 28.

### `api-measure-code/unchecked-boundary-invariants` — Boundary calls silently accept protocol violations

- kind: issue · severity: low · verdict: accurate · plan: **P1-19**
- locations: `engine/src/api/doc.h:209`; `engine/src/api/doc.h:238`; `engine/src/api/wasm_api.cc:186-216`; `engine/src/measure/measure.h:21`

- A provide with an unknown or out-of-range id is silently ignored (doc.h:209, 238). The probe shows a wrong-id token answer leaves the request pending, and the host only learns about it after 64 rounds as 'did not converge'.
- `tsr_render`, `tsr_render_pages` and `tsr_doc_height_px` do not check that typeset converged.
- `Doc::compile`/`ingest` can be called again on the same Doc and accumulate arena, diagnostics and stale requests.
- `MetricStore::key` packs `(StrRef << 24) | StyleId` without asserting StyleId < 2^24.

A generic protocol should report unmatched answers as diagnostics and expose a 'stalled: ids' status.

*Verifier notes:* The list is incomplete. Token answers are not validated either:
- wasm_api.cc:174 narrows the tag with `(u8)`;
- tokens.cc styleFor then indexes the stack arrays `tagStyle[14]` and `tagStyleMade[14]` and reads `kTokenTags[tag]` with no bounds check;
- the 'sorted, non-overlapping, codepoint-aligned' contract is never enforced.
See missed items.

### `api-measure-code/doc-drift-api-subsystem` — Normative docs describe API, measure and highlight behaviour that was never built

- kind: issue · severity: low · verdict: partly · plan: **P3-37**
- locations: `engine/src/measure/mock.h:1-2`; `docs/document-model.md §6.4, §7, §10, §11`; `docs/architecture.md §2.1, §2.5`; `docs/code-design.md §3, §5`; `docs/verbatim-design.md §5`

Mismatches between documentation and code:
- mock.h:1-2 claims a JS twin at runtime/src/shared/mockmeasure.mjs whose 'drift … fails CI'. No such file exists.
- document-model §6.4 describes exact / pending(estimate) / invalid states; MetricStore has only present/absent, and pages-design W abandoned estimates.
- document-model §7 (buffers), §10 (JSON diagnostics) and §11 (config JSON, unknown-key diagnostics) are unbuilt.
- architecture §2.5's API list is unbuilt, and §2.1's 'typeset' stage name is wrong.
- code-design §3 promises `<span class="tsr-tok-*">` from the semantic serializer, which actually emits `style="color:var(--tsr-tok-…)"`.
- code-design §5's 'Theme = tag→style map in Config' does not exist.
- verbatim-design §5's per-line `group{role:"sidecar"}` is built differently.

Because these docs are marked normative, the drift misleads anyone extending the system.

*Verifier:* document-model §7 is semantically built: the JSON request carries style descriptors with per-style words and needVmet, and provide works per item. What is missing is the buffer form, i.e. the batched `tsr_measure_provide` from architecture §2.5.

The other items are verified:
- runtime/src/shared contains only opbuf.mjs and ops.gen.mjs, with no mockmeasure.mjs;
- the semantic serializer emits style="color:var(--tsr-tok-…)", not classes;
- architecture §2.1 lists a 'typeset' stage that tsrc rejects.

Also unbuilt: v2 §6 dppx invalidation. There is no devicePixelRatio or matchMedia code anywhere in runtime/.

### `api-measure-code/missed:0` — Literate-fragment masking corrupts UTF-8 token offsets for non-ASCII fragment names

- kind: missed · severity: medium · verdict: verifier-found · plan: **P0-11**
- locations: `runtime/src/worker/tokens.mjs:102-111`; `runtime/src/worker/tokens.mjs:62-76`; `runtime/src/worker/tokens.mjs:125-129`; `engine/src/code/tokens.cc:53-122`

Masking replaces each `<<…>>` span with ASCII spaces of equal UTF-16 length. u16ToU8Map is then computed from the MASKED text. If that text is now all ASCII, the map is null (identity). Every token after a non-ASCII fragment name is therefore shifted by the lost bytes, and tokens land mid-codepoint.

Probe: tokenize('cpp', '<<初始化>>=\nint x = 1;') returns ranges covering "<<初始" and "��>". foldTokens then interns invalid-UTF-8 runs, which flow into measurement and HTML.

This matters because the project is zh-first, and Chinese fragment names are the expected case for its users.

*Proposed generalization (survey):* Preprocessing overlays must work in original-text coordinates. Either mask with filler that preserves byte length (repeat a 1-byte filler for each UTF-8 byte and adjust the UTF-16 mapping), or build the offset map from the original text before masking.

Do the coordinate mapping once, in the shared capture resolver, from (originalText, masks), and add a fixture with a masked non-ASCII span.

### `api-measure-code/missed:1` — Provider answers are trusted blindly: an out-of-range tag writes past a stack array, and offsets are never validated

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-19**
- locations: `engine/src/api/wasm_api.cc:170-176`; `engine/src/code/tokens.cc:39-51`; `engine/src/code/tokens.h:27-31`; `engine/src/api/doc.h:208-243`

tsr_provide_tokens narrows the tag with `(u8)triples[i*3+2]`. foldTokens' styleFor then indexes `tagStyle[kTokenTagCount]` and `tagStyleMade[kTokenTagCount]` (14-element stack arrays) and reads `kTokenTags[tag]` with no bounds check. A tag of 14 or more is an out-of-bounds stack write, which corrupts silently in WASM.

The 'sorted, non-overlapping' contract is only stated in tokens.h:28. Codepoint alignment (see the previous item) and finite image dimensions (provideImage accepts NaN or inf) are not checked at all.

Today only the built-in tokenizer answers. The report's own generalizations, user providers and `providers['code.tokens']` overrides, would make this reachable from documents and plugins.

*Proposed generalization (survey):* Validation belongs to the codec layer of the resource protocol. Each answer is checked for tag/class range, monotonic non-overlapping ranges within the body, UTF-8 boundary alignment, and finite non-negative numbers. Violations are clamped or dropped, fall back to the documented degradation (plain code or a placeholder), and raise a 'provider-invalid' diagnostic. This is a prerequisite for opening the provider surface to users.

### `api-measure-code/missed:2` — Sidecar notes disappear from the semantic and static-export output

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-23**
- locations: `engine/src/api/doc.h:123-146`; `engine/src/render/semantic_html.cc:239-251`; `runtime/src/node/render.mjs:40-50`; `docs/pages-design.md §3`

extractSidecars strips the `/// …` text out of the code body and moves it into a group. The semantic serializer then skips every group child of a codeblock.

`tsrc --stage=semantic` on test/fixtures/code/sidecar.tsm shows the code with neither the marker nor the notes, including their math and @ref. pages-design §3 makes the semantic page THE exported artifact, and it is also the SEO, no-JS and first-paint output. Authored content is therefore lost from static export, not just restyled.

*Proposed generalization (survey):* Every display-layer structure needs a defined semantic projection, dispatched on a structural marker rather than 'skip any group'. For example, each line's note becomes a trailing `<span class="tsr-side">` or a per-line `<aside>`, or the original `/// note` comment text is restored. The serializer contract (document-model §9.2) should list each display-only structure with its semantic fallback, just as copy rules are listed in §9.3.

### `api-measure-code/missed:3` — Metrics are keyed by presentation StyleId instead of the measurement tuple specified in v2 §6

- kind: missed · severity: medium · verdict: verifier-found · plan: **P1-04, P1-19**
- locations: `engine/src/measure/measure.h:19-41`; `engine/src/measure/measure.h:60-71`; `engine/src/emit/emit.cc:913-935`; `engine/src/api/wasm_api.cc:98-132`; `docs/design-decisions-v2.md §6`

MetricStore::key and resolveWidths dedupe by (StrRef, StyleId). StyleId also encodes color, lang, decoration and link bits, none of which affect advances.

Probe: 'hello world' set plain, red, underlined and inside a link produces 4 style ids with identical family, size, weight and italic. Each requests the same words ('hel', 'lo', 'world', …), and vmet is requested per id.

v2 §6 specifies 'Per unique style tuple (family, size, weight, italic)' and '(string × style)'. Combined with per-keystroke fresh Docs and per-word marshalling, this multiplies boundary traffic, metric entries and vmet requests, and it blocks cross-style sharing of kerning contexts.

*Proposed generalization (survey):* Intern a measurement key, FontKey: the describeStyle projection (family, size, weight, italic), extended later with lang and font-features when a backend honours them. Map it to a MetricStyleId, cached per StyleId in the StyleTable.

MetricStore, MeasureRequest, vmets and the request JSON (or the textWidth resource) then key on MetricStyleId. This also gives the typed textWidth resource a key that carries the full descriptor, and stops later presentation-only Styling fields (such as token classes) from inflating measurement.

### `api-measure-code/missed:4` — Grid budget, measured CJK ratio and snap-kerning are silently disabled by `wrap: false`

- kind: missed · severity: low · verdict: verifier-found · plan: **P3-11**
- locations: `engine/src/layout/layout.cc:133-167`; `engine/src/emit/emit.cc:553-557`; `docs/verbatim-design.md §1`

chSu is read only when `u.codeWrap` is true (layout.cc:134). The CJK ratio and the snap branch both require chSu > 0 (layout.cc:139, 151).

A non-wrapping block, the natural choice for aligned ASCII figures and tables, which are exactly the alignment use case of verbatim §1, never gets snap-kerning. It silently falls back to natural flow even when the host enabled verbatimSnapKerning. verbatim §1 treats budget (wrap) and alignment as separate uses, but the code couples them through an incidental guard.

*Proposed generalization (survey):* Solve the grid per code style unconditionally once the probes are measured. Make alignment (Snap or Budget) and wrapping independent fields of the proposed VerbatimSpec, so each feature declares which grid uses it needs.

## real-world-evidence — Real-world evidence: converters, corpus report, blog usage, feature cost from git history

<details><summary>Design summary (as audited)</summary>

This subsystem is the evidence loop, not one module. Four converters under tools/convert/ plus tools/typ2tsm.mjs build .tsm by regex and string concatenation. tools/translate-tsm.mjs re-lexes .tsm in order to machine-translate it. examples/real-world/ holds the committed outputs (HoTT introduction, the en and zh Wikipedia pages) and a gitignored 163-section pbr-book conversion. docs/real-world-report.md records what the corpus exposed. The zball-io blog uses the engine through Eleventy: renderTsm produces semantic HTML at build time, and shell.mjs hydrates it in the browser. The git history (82 commits; the engine work runs 2026-08-22 to 09-01) shows that each feature lands as a vertical slice through many layers.

What the evidence says about the design: the public extension surface is #let/#{} JS, #!name regions (which become group{role:name}, or go to a $.region handler), ``` fences ($.fence) and a fixed list of constructors passed into the document function. The services that make a construct first-class are tied to hard-coded kinds (heading, table, mathblock, note) and to one role string ('figure'). Those services are counters, label registration, supplement words and caption prefixes, floating, semantic elements, popups and the citation format. The public constructor list also has no group, raw, table or figure. So every source construct outside the built-in set reaches the engine lossily, as bold text, flat paragraphs, literal text or '??'. That covers theorems, description lists, table captions, subfigures, sidenotes, matrices, math macros and cross-document refs.

The converters have no printer or exported AST to target. Each one therefore re-implements escaping, and they collide with sugar (^[…](url), intraword _). Every feature that needed a new arg key or kind paid an OPS_VERSION bump plus a full fixture re-record: five bumps in four days. Features that reused generic kinds (citations) did not. This is the clearest quantitative sign that the missing abstraction is an open, data-driven schema for kinds, roles and args.

The design docs already promise much of the general machinery: user counter classes, resetAt and JSON config (document-model §5/§11), m.parse and #use (v2 §2), set/show layering (v2 §12), named notes (notes-design). When the real-world work needed these, it routed around their absence with special cases instead of building them.


Strengths:

- The corpus loop is disciplined. The converters, the report, the regression fixtures and the e2e audit at 680px turned real text into engine fixes that are mostly general rules: kern contexts bounded by style runs (emit.cc fillSpaceContexts), underfull lines set ragged (TeX's underfull box), and CJK–Latin boundary glue only between ideographs and Latin (GB/T 15834).
- Citations (59f56e3) needed the least shotgun surgery of any feature: 8 layers, 9 non-test files, no OPS bump. They reuse ref, collect and group plus a JS entry formatter. This shows that 'execution declares, the resolver decides' together with generic group/collect works when it is actually used.
- Region interiors are ordinary markup with provenance, so table cells cannot be mis-split on '|' inside math or code. Verified: cells `$x | y$` and `p|q` parse as one cell each. The tex2tsm \mid workaround (d454e24) was about the converter's own & splitting, not the engine.
- A user $.region/$.fence handler takes precedence over the built-in table/figure builders (executor.mjs:126-128), which is a start towards putting user extensions on equal footing.
- Failures are diagnostics, not errors: unresolved refs render '??' and fences fall back to code blocks. Converted documents with gaps still typeset, which is what made the 163-section pbr run (163/163 sections, 526 → 0 math diagnostics) possible.
- The math syntax works well as a conversion target: MathSpeak recovery maps 8,047 pbr formulas mechanically (pbr2tsm.mjs:64-377), and the text-run rule (math-design §14) fixed the HoTT name problem generally rather than per macro.
- translate-tsm.mjs validates its output against the source byte-for-byte for fences, display math, the inline-math multiset and figure lines (translate-tsm.mjs:100-112). That is the only fidelity check in the toolchain and a good model for the converters.
- Every stage can be inspected (tsrc --stage=…), and a recorded-ops path lets every claim in this review be checked against the real executor.

</details>


### `real-world-evidence/role-figure-hardwired` — 'figure' is the only role with semantics; user roles get no number, label, caption, float or element

- kind: adhoc · severity: high · verdict: partly · plan: **P2-07**
- locations: `engine/src/resolve/resolve.cc:165-181`; `engine/src/resolve/resolve.cc:284-291`; `engine/src/emit/emit.cc:471-476`; `engine/src/emit/emit.cc:776-818`; `engine/src/render/semantic_html.cc:285-306`; `runtime/src/worker/executor.mjs:114-130`; `runtime/src/worker/executor.mjs:162-237`; `docs/document-model.md:63-66`; `tools/convert/tex2tsm.mjs:197-198`; `zball-io/src/posts/vscode-tsm.tsm:67`

Several stages special-case the role string 'figure' or a fixed set of kinds:
- **Resolver:** it numbers a group only when role=='figure' (figNo; inserts cfg.supFigure+n+capSep in bold into the first para). It registers labels only for heading, table, figure-group, mathblock, note and term. resolveRef maps any Kind::group target to supFigure.
- **Emit:** paras under a 'figure' group become centred, ragged captions, and float wrap is enabled only there.
- **Semantic serializer:** emits <figure>/<figcaption> only for 'figure'; every other role becomes <div data-role>.
- **Executor:** keeps tableBuild/figureBuild private and exposes no group, raw, table or figure ctor.

Verified with recorded ops:
- `#!theorem(label: "thm-a")` followed by `@thm-a` resolves to '??'.
- A `$.region('theorem', fn)` handler cannot keep the label at all: `typeof group`, `raw`, `table` and `figure` are all 'undefined', and the handler's output is used as-is.

Real-world consequences:
- tex2tsm deletes table/figure environments and turns \caption into an italic paragraph (tex2tsm.mjs:197-198).
- Theorem-like environments have nothing to map to.
- The blog explicitly asks for a semantic 'experiment environment' block (vscode-tsm.tsm:67).

*Why ad hoc:* document-model §2.1 deliberately keeps the kind set minimal ('Figure is a convention, not a kind'; 'There is no user-defined kind … keeps layout closed under the kind table'). That rationale is sound. However, three C++ stages then consume the convention by string comparison, so 'role' is a closed enum in disguise. The doc's own extension story ('custom constructs are built from group/styled/raw') cannot be reached because group and raw are not constructors. The kind set can stay closed; it is the services that should be driven by role rather than by kind.

*Proposed generalization (survey):* Add a RoleSpec registry, held as data rather than code.
- Proposed struct: `struct RoleSpec { StrRef name; StrRef counter /* '' = unnumbered */; StrRef supplementTerm /* locale-table key */; CaptionSpec caption {slot: first-para|last-para|lead|none; prefix: '{sup} {n}{sep}'; align; ragged; hyphenate}; bool floatable; bool labelable = true; StrRef semanticElement /* figure|aside|section|div */; bool atomicInPagination; StrRef preview /* popover|none */; }`.
- Declare roles from JS with `$.role('theorem', {counter: 'theorem', supplement: 'theorem', caption: {slot: 'lead', prefix: '*{sup} {n}.* '}, element: 'section'})`. A JS prelude registers figure, table, equation, footnote, bibentry, notes and term through the same call.
- Carry the declarations to C++ as a doc-level meta node with string-keyed args (see open-arg-schema).
- resolve.cc scan(): `if (auto* rs = roleSpec(n))` performs counter step, label entry and caption prefix generically. resolveRef formats from the spec, not from the Kind switch.
- emit: caption and float policy come from the spec flags. semantic_html: the element comes from the spec.
- Expose `group(role, args, ...kids)`, `raw(html, {w,h})`, `table(args, rows)` and `figure(args, ...kids)` as public ctors, so regions become pure sugar again.

*Verifier:* (1) The handler claim is wrong. `__region` and `__fence` are destructured parameters of the document function (engine/src/codegen/codegen.cc:209-212), so user code can call them; I verified `typeof __region === 'function'`. `$.region('theorem', (a,k) => __region('figure', a, k))` produces a numbered figure that @thm-a resolves to (图 1). `__region('thm-inner', a, k)` keeps label="t1" on the group. So a handler CAN keep the label. What it cannot do is make a non-figure label resolvable.
(2) Float wrap is not figure-only. emit.cc:740-741 sets floatSide on every image, and the float tracker in engine/src/api/doc.h (around lines 299-326) floats any Image unit. I verified that a standalone `#image(src, {float:"right", w, h})` narrows the next 3 lines. Only the caption-as-TableCell path (emit.cc:785-815) is specific to figures.
(3) `raw` is reachable from fence handlers through ctx.raw (executor.mjs:153-154). It is not reachable from region handlers or from document code.
(4) The anomaly is worse than reported. semantic_html.cc emits id="tsr-thm-a" for the unregistered theorem group (verified), so the anchor exists while @thm-a prints '??'.
(5) document-model.md:63 lists `group` as a labelable kind. The code contradicts the spec, not just a convention.

*Verifier notes:* The kind-closed / role-open diagnosis is correct. However, RoleSpec as proposed bundles three things:
- semantic fields (counter, supplement, labelable, element);
- presentation fields (caption align/ragged/hyphenate);
- host-UI fields (preview).

That means emit, layout and the shell would consult a semantic registry, which is the 'renderer knows roles' problem again through indirection. It also duplicates the report's own presentation-rule proposal and $.show. Split it: a semantic RoleSpec read only by the resolver and the semantic element choice, with presentation as one rule mechanism keyed by role and slot.

Cheapest first step: register every node that carries a label (any kind or role) in the label table, with an Entry that records the role. The number is optional and display falls back to the excerpt. That removes the '??' for theorem and aside, and matches document-model §2.1 at almost no cost.

The proposal also has to engage design-decisions-v2.md §11.1, which states that 'computed numbering is done in user JS with user counters'. Moving user numbering into the resolver contradicts that line, although it is consistent with 'execution declares, resolver decides' as long as specs are pure data. The declaration-order semantics need to be stated: global, or registration-precedes-use as with $.fence. The specs also need a transport in the op stream: a new kind (an OPS bump under current policy) or an extension-arg channel.

### `real-world-evidence/counters-fixed-fields` — Counters are four hard-coded ints; the specified user counters and reset rules do not exist

- kind: adhoc · severity: high · verdict: partly · plan: **P2-07**
- locations: `engine/src/resolve/resolve.cc:62`; `engine/src/resolve/resolve.cc:131-155`; `engine/src/resolve/resolve.cc:157-213`; `docs/document-model.md:124`; `docs/document-model.md:321-322`; `docs/notes-design.md:33-35`; `docs/notes-design.md:70-74`; `tools/convert/pbr2tsm.mjs:428`; `tools/convert/pbr2tsm.mjs:508-509`

The resolver has `int tableNo, figNo, eqNo` plus noteNo and the heading stack secc. Nothing in engine/ or runtime/ reads counter configuration, although:
- document-model §5 promises 'counter classes figure | equation | footnote | <user> with reset rules from config (none | section)';
- §11 shows `counters: {figure: {resetAt: 'none'}}`;
- notes-design promises a footnote reset rule and circled marks.

Number formats are literals inside resolveRef: '§'+n, '('+n+')'.

Verified: nested #!figure numbers the subfigure as a sibling (图 1 / 图 2). Heading numbers are computed but never shown: the semantic output has `<h2>Sub</h2>` while the ref prints '§1.1'.

Real-world consequences:
- pbr-book's chapter-relative numbers cannot be reproduced. pbr2tsm strips 'Figure N.N:' from captions and section numbers from headings (pbr2tsm.mjs:428, 508-509).
- The local pbr-en corpus therefore keeps 568 'Figure N.M', 11 'Figures', 410 'Section N.M', 11 'Sections' and 103 'Chapter N' references as plain text.
- A heading-numbering offset for multi-file books is impossible.

*Why ad hoc:* Each numbered thing got its own field, its own increment site and its own format branch. There is no counter abstraction even though the spec describes one.

*Proposed generalization (survey):* Introduce a CounterTable in the resolver.
- Proposed struct: `struct Counter { int value; StrRef resetOn /* parent counter, '' = never */; StrRef format /* '{n}', '{h1}.{n}', '{n:a}', '{n:i}', '{n:circled}', '({n})' */; int start; }`, keyed by name.
- Heading levels become counters h1..h6 chained by resetOn.
- RoleSpec.counter names a counter. Several roles may share one, e.g. lemma and theorem.
- The resolver steps counters in document order, as now, and snapshots the formatted string plus the raw vector into each label Entry. The spec already says values are 'snapshotted into the label table'.
- Declare counters with `$.counter('figure', {resetOn: 'h1', format: '{h1}.{n}'})`. Project config supplies start offsets (e.g. chapter number).
- Whether a heading displays its number becomes a RoleSpec prefix property, so headings, TOC and refs agree.

*Verifier:* The corpus counts do not reproduce. Over examples/real-world/pbr-en/*/*.tsm I get:
- 518 'Figure N.M';
- 11 'Figures N.M';
- 346 'Section N.M';
- 9 'Sections N.M';
- 103 'Chapter N'.

That is about 987, not 568/11/410/11/103.

'§' is not a literal. It is cfg.supHeading (config.h:67), a Config field that has no exposed setter. '('+n+')' is a literal (resolve.cc:193, 288).

Confirmed:
- `int tableNo, figNo, eqNo` (resolve.cc:62);
- no counter config anywhere in Config (config.h:22-73);
- a nested #!figure is numbered 图 1 / 图 2 as a sibling (verified);
- the semantic output is `<h2>Sub</h2>` while the ref prints '§1.1' (verified);
- pbr2tsm strips the numbers (pbr2tsm.mjs:428, 508-509).

*Verifier notes:* The docs conflict, and the report should say so. document-model §5 puts `<user>` counter classes in the resolver, while design-decisions-v2 §11.1 says user numbering lives in user JS. JS-side counters can never produce referenceable numbers, because the label table belongs to the resolver. The CounterTable should therefore be adopted explicitly as superseding §11.1.

Keying counters through RoleSpec.counter does not cover headings, which are a Kind and not a role, so counters need a (kind|role) key. Chapter offsets require project-level input, which depends on the cross-document item. Determinism is unaffected, since the formatted value is snapshotted into each Entry.

### `real-world-evidence/open-arg-schema` — Every feature knob is a frozen ARGK in the binary contract: five OPS_VERSION bumps in four days

- kind: adhoc · severity: high · verdict: partly · plan: **P1-01, P2-05**
- locations: `engine/src/ops/ops.def:3-5`; `engine/src/ops/ops.def:40-75`; `runtime/src/worker/executor.mjs:212-217`; `runtime/src/shared/ops.gen.mjs`; `git:85312ee`; `git:e04c0db`; `git:0c2ef30`; `git:217b3b7`; `git:57340a8`

ops.def enumerates node arguments as numbered ARGKs (font, color, sizePx, wrap, lineNo, hl, sidecar, scale, alt, side…). Every addition is a protocol bump that re-records every .ops fixture:

| commit | date | bump | what | .ops re-recorded |
|---|---|---|---|---|
| 85312ee | 2026-08-25 | v2 | style patches | 23 |
| e04c0db | 2026-08-26 | v3 | code-grid args | 30 |
| 0c2ef30 | 2026-08-26 | v4 | sidecar | 34 |
| 217b3b7 | 2026-08-26 | v5 | image/scale/alt/side | 36 |
| 57340a8 | 2026-08-28 | v6 | note kind | 42 |

CLAUDE.md makes each bump require gen-ops-ts plus a full re-record. ARGK(lang) is reused for the codeblock language and the BCP-47 style tag ('per-kind namespaces over one key', ops.def:74-75). The authoring name `float` silently maps to ARGK side (executor.mjs:216). User region args such as `#!theorem(numbered: true)` cannot reach C++ at all.

*Why ad hoc:* The one boundary that is supposed to be stable carries a closed, per-feature vocabulary. The args are already a key→value map, but the keys are compile-time enums.

*Proposed generalization (survey):* Make arg keys open.
- Make the argKey a string-table reference (same tag-prefixed value encoding). Alternatively use two tiers: core keys 0..63 stay frozen, and keys ≥ 64 are strRef+64.
- Generate a per-kind arg schema (type, required, default) from one schema file, consumed by gen-ops-ts and by OpReader validation. An unknown key on a core kind produces a diagnostic and is kept in `node.extArgs`, where RoleSpecs can read it.
- Stages resolve key ids once per document (`K_scale = keys.intern("scale")`) and compare integers as now.
- OPS_VERSION then changes only for encoding changes.

*Verifier:* The bump table is confirmed (git log -p engine/src/ops/ops.def; .ops re-record counts 23/30/34/36/42). Two corrections:

1. Open arg keys would not have avoided two of the five bumps. v6 (57340a8) added only KIND(note). v5 (217b3b7) added KIND(image) alongside ARGK scale/alt/side. Only v2, v3 and v4 were pure ARGK additions. All five were append-only, and the v2 STYLE_PUSH patch used 'the byte reserved since M1' (85312ee message).

2. The full re-record comes from the header version byte, the strict check `if (buf[4] != OPS_VERSION)` (ops.cc:100), and the policy 'Changing any number is a protocol version bump' (ops.def:3). Numeric keys are not the cause.

In addition, the C++ reader's readArg (ops.cc, around line 67) accepts any varint as ArgK with no range check. Only opbuf.mjs throws 'unknown arg key'. So the contract is already open on the reader side and strict on the writer side.

*Verifier notes:* The closed, versioned binary contract is documented and deliberate: document-model §4 calls it a 'normative binary contract' with the reader as a fuzz target, and ops.def:74-75 documents the ARGK(lang) reuse.

The better generalization has two parts:

(1) Additive-compatible versioning. A major version for encoding or renumbering changes; a minor version for appended KIND/ARGK values that is not part of the strict check. The reader accepts older minors, and recordings are re-recorded only when their bytes change. This would have removed all five full re-records with no hot-path cost and no weaker validation, which string keys cannot claim.

(2) One typed escape for user and region args instead of a string-keyed core. For example, ARGK `ext` points to a child meta node holding string-keyed pairs, or holds a strRef to canonical JSON. It is opaque to core stages and read only by the resolver and RoleSpec extension points. Core keys stay closed and get the missing range validation.

Converting the core key space to strings loses the compile-time `ArgK::scale` checks throughout emit and layout, does nothing for kind additions, and widens the fuzz surface.

### `real-world-evidence/sugar-dispatch-fixed` — Sugar compiles to fixed, non-rebindable constructors; regions have no callable constructor

- kind: adhoc · severity: high · verdict: partly · plan: **P2-03**
- locations: `engine/src/codegen/codegen.cc:26-60`; `engine/src/codegen/codegen.cc:122-127`; `engine/src/codegen/codegen.cc:209-212`; `runtime/src/worker/executor.mjs:123-139`; `runtime/src/worker/executor.mjs:162-237`; `docs/design-decisions-v2.md:107`; `docs/design-decisions-v2.md:252`

codegen emits `strong(…)`, `em(…)`, `note(…)`, `ref(…)`, `heading(…)` and `list(…)` against a destructured parameter list hard-coded in codegen.cc:209-212, which must mirror the executor's ctors object.

Verified: `#let strong = (...k) => em(...k)` is a module-level SyntaxError ('Identifier strong has already been declared'). So *…*, = …, ^[…] and @x cannot be restyled or redirected; v2 §12 defers set/show to 'layered later'.

#!table and #!figure compile to the internal `__region` dispatcher, whose builders are private. They are therefore sugar for nothing a user can call, even though v2 §4 states that every syntax form is sugar for a constructor and 'guarantees every syntactic capability has a programmable equivalent'.

Converters consequently have to target the sugar strings directly, with no constructor-level alternative when the sugar is ambiguous (see no-tsm-printer).

*Why ad hoc:* The constructor equivalence holds for built-ins but cannot be replaced, and two block forms have no equivalent at all. Sugar is wired to specific identifiers instead of a dispatch table.

*Proposed generalization (survey):* Turn sugar into a dispatch table.
- Compile sugar to calls through a dispatch object owned by `$`: codegen emits `__s.strong(…)`, where `__s = $.sugar` holds the defaults.
- Add `$.show(target, fn)`. target is a sugar name or a role, and fn receives the default as `next`, e.g. `$.show('heading', (lvl, label, kids, next) => next(lvl, label, [style({font: 'sans'}, ...kids)]))`.
- Move user code into a nested block scope (or put the ctors under one namespace object), so user `#let` names never collide with ctor names.
- Make `__region(name, args, kids)` equal `($.regions[name] ?? $.regions.default)(args, kids)`, with the table and figure defaults registered in the same table.
- Export table, figure, group and raw as ordinary ctors.
- Show functions run only during execution, so the resolver and emit are untouched.

*Verifier:* '#!table and #!figure … are therefore sugar for nothing a user can call' is overstated. They compile to `__region("table", ({…}), [[[cell,…],…]])` (codegen.cc:157-192), and `__region` is in user scope (codegen.cc:209-212; verified callable). It is undocumented, carries a double-underscore name, and takes codegen's row-array encoding, but it is reachable.

Confirmed:
- the `#let strong = …` SyntaxError ('Identifier strong has already been declared');
- the v2 §4 principle (design-decisions-v2.md:107);
- v2 §12 decided 'TeX grouping, not Typst set/show rules', with set/show as optional later layering (line 252).

*Verifier notes:* Two steps, in order:
1. Fix the collision independently and first. Pass the ctor bag as one parameter and compile sugar to `__c.strong(…)`, so a user `#let` can never collide with a ctor name.
2. Promote `__region` to a documented `region(name, args, ...children)` constructor with a documented children shape, and register the table and figure builders in the same table so a user handler can wrap them via `next`.

$.show runs only during execution. It does not conflict with emission-time binding (§12), because show functions build styled nodes whose deltas still fold at emission.

### `real-world-evidence/no-tsm-printer` — No canonical .tsm printer: every converter concatenates strings with partial, divergent escaping

- kind: adhoc · severity: high · verdict: accurate · plan: **P3-35**
- locations: `tools/convert/html2tsm.mjs:61-68`; `tools/convert/pbr2tsm.mjs:429`; `tools/convert/pbr2tsm.mjs:492-499`; `tools/convert/wiki2tsm.mjs:55-60`; `tools/convert/wiki2tsm.mjs:97-108`; `tools/convert/html2tsm.mjs:51-57`; `tools/convert/tex2tsm.mjs:184`; `tools/convert/tex2tsm.mjs:201`; `examples/real-world/wiki-typesetting.tsm:60`; `examples/real-world/hott-introduction.tsm:13`; `examples/real-world/hott-introduction.tsm:134`

The converters escape only `$ # @` (html2tsm.mjs:68, pbr2tsm.mjs:499, wiki2tsm.mjs:108).

Intraword `_` and `*` open emphasis under the strict-pairing rule. Verified: `x_i and y_i` produces an EM run. The pbr-en prose therefore turns `SampleT_maj()`, `Approximate_dp_dxy()`, `STAT_PIXEL_COUNTER` and URLs inside link text into emphasis (11 hits).

wiki2tsm builds `^[body]` first; its later inline pass rewrites `[url text]` inside the body into `[text](url)`, giving `^[text](url)`. That parses as a footnote followed by the literal text '(https://…)' (verified). 8 footnotes in the committed wiki-typesetting.tsm lose their URL this way.

html2tsm's non-depth-aware fragment regex put about 120 lines of prose inside a ```cpp fence (pbr-1-2.tsm:52-175). Only pbr2tsm's later depth-aware scan fixed it.

tex2tsm leaks `Martin-L\"of`, `equiv\-a\-lent` and `⟨\mathsf⟩LEM_n` into the committed HoTT file.

The entity tables are copied between converters and incomplete: 683 `&thinsp;` remain in pbr-en.

*Why ad hoc:* The language has a parser, which desugars syntax to constructors, but no inverse. Each tool re-implements a different subset of the lexical rules, and the inline pass is applied to text that already contains generated markup.

*Proposed generalization (survey):* Add a canonical printer, `runtime/src/shared/print.mjs` (or a C++ tsr_print exported from wasm).
- Signature: `print(shadowTree, {preferSugar}) → string`.
- Converters build shadow trees with the same ctor API. `buildContext(new OpBuf())` in executor.mjs:58 already returns these ctors.
- The printer emits sugar only when the inverse parse is exact; for example, a note body containing `](` is printed as `#note[…]`.
- It escapes per context using character-class tables generated from inline.cc, the same way ops.gen.mjs is generated from ops.def.
- Otherwise it falls back to the explicit `#ctor(args)[…]` form.
- A CI property test checks `parse(print(t)) ≡ t` over fixtures and corpus.
- Shared converter utilities (HTML entities via a real decoder, BibTeX→CSL) live next to it instead of being copied.

*Verifier:* The wiki mechanism is slightly different from what is described. cleanRef (wiki2tsm.mjs:48-49) strips every '[' and ']' from ref bodies. A body that starts with a URL is then wrapped in the converter's own `^[…]`, and the external-link regex (wiki2tsm.mjs:100) rewrites the footnote's own brackets into `^[title](url)`. That gives 8 occurrences in wiki-typesetting.tsm (lines 60, 92, 94, 96, 100, 106; verified as a footnote followed by literal '(url)') and 0 in wiki-huozi.

The pbr identifiers that hit emphasis (SampleT_maj, Approximate_dp_dxy, STAT_PIXEL_COUNTER) sit in figure captions. pbr2tsm builds captions by stripping tags (pbr2tsm.mjs:427) instead of using `inline()`, so `<tt>` is dropped before escaping. That is a second, divergent inline path inside one converter.

I did not reproduce the '11 hits' count.

Other claims verified: the pbr-1-2.tsm fence over lines 52-175 (a local gitignored file), 683 `&thinsp;`, and the tex2tsm leaks at hott-introduction.tsm:13 and :134.

*Verifier notes:* Intraword `_` opening emphasis is a documented language decision (design-decisions-v2.md:157 §5: strict pairing, CommonMark flanking rejected for CJK). The report should cite it.

A printer can escape it, but a cheaper language-level fix exists: `_` or `*` between two ASCII alphanumerics never toggles emphasis, and CJK is unaffected. The tree-sitter grammar already assumes this rule (grammar.js:5-6: 'word rule keeping identifiers (foo_bar) intact'), so the highlighter and the real parser disagree on exactly this case today.

'Character-class tables generated from inline.cc' assumes a table-driven lexer. inline.cc is hand-written (inline.cc:351), so the tables must be factored out first.

For converters whose only goal is typesetting, emitting ops through buildContext directly also works and needs no printer. The printer is needed only for editable .tsm.

### `real-world-evidence/lexical-syntax-copies` — The lexical syntax exists in four-plus hand-synchronized copies

- kind: adhoc · severity: medium · verdict: partly · plan: **P1-09**
- locations: `engine/src/inline/inline.cc:384-430`; `grammar/tree-sitter-tsm/grammar.js:1-6`; `grammar/tree-sitter-tsm/grammar.js:87-97`; `editors/vscode-tsm/syntaxes/tsm.tmLanguage.json`; `tools/translate-tsm.mjs:48-73`; `engine/src/code/tokens.h:10-17`; `runtime/src/worker/tokens.mjs:5-8`; `git:57340a8`

The C++ linepass/inline parser is authoritative. The other copies:
- **tree-sitter-tsm** approximates it with regex tokens, which already differ: the footnote token `\^\[[^\]\n]*\]` does not nest while C++ does, and strong/emphasis are single-line regexes. It is vendored twice; the footnote commit regenerated about 695 lines of parser.c in each copy.
- **VSCode TextMate grammar:** has no footnote rule at all, so it has already drifted.
- **translate-tsm.mjs:** masks `$…$`, code, `](…)` and `<<…>>` but not `@ref`, `<label>`, `^[` or `#splice`.
- **Token tags:** the 14 tags are duplicated in C++ and JS with a 'keep in sync' comment.

grammar.js documents itself as 'deliberately … approximate'.

*Why ad hoc:* Every consumer of the syntax re-derives it, because the real parser's AST and spans are not exported from the wasm module. Only compile→JS is exported.

*Proposed generalization (survey):* Export the real front end.
- `tsr_parse_json(src)` returns `{nodes: [{kind, span: [s,e], attrs}]}`.
- `tsr_syntax_tokens(src)` returns (start, end, tag) triples in the 14-tag contract, computed from the linepass+inline AST: markers, labels, refs, splices and island bodies.
- Uses:
  - NEED_TOKENS for lang 'tsm' is answered by the engine itself;
  - the VSCode semantic-tokens provider;
  - translate-tsm segments become AST text nodes;
  - printer round-trip checks;
  - editor outline and folding.
- tree-sitter becomes only the cold-start fallback.
- Generate the tag list for both languages from one header via gen-ops-ts.

*Verifier:* 'Vendored twice; the footnote commit regenerated about 695 lines of parser.c in each copy' is wrong. git tracks only third_party/grammars/tsm/parser.c. grammar/tree-sitter-tsm/src/ is gitignored (.gitignore:5) and is a build product. 57340a8 changed +368/-327 lines in that single tracked file. What is duplicated in git is highlights.scm (grammar/tree-sitter-tsm/ and third_party/grammars/tsm/). The cross-cutting 'regenerated twice' is wrong for the same reason.

Confirmed:
- the non-nesting footnote token (grammar.js:90);
- single-line strong and emphasis tokens;
- no footnote rule in tsm.tmLanguage.json;
- translate-tsm's INLINE_RE (translate-tsm.mjs:48) masks no @ref, <label> or ^[, even though its header (translate-tsm.mjs:4-6) claims 'footnote markers are masked';
- the 14 tags duplicated with keep-in-sync comments (tokens.h:10-17, tokens.mjs:5-8).

*Verifier notes:* tree-sitter-tsm is deliberately approximate, for code-block colouring (grammar.js:1-6). Exported spans and tokens from the real parser are the right source for the editor, translation and the printer.

If the engine answers NEED_TOKENS for lang 'tsm' itself, it must compile the nested source on a separate doc handle. Otherwise diagnostics, labels and string interning leak into the outer document and threaten golden determinism.

### `real-world-evidence/literate-cpp-hack` — Literate-programming fragments are a global regex special case inside the C++ token provider

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-22**
- locations: `runtime/src/worker/tokens.mjs:78-93`; `runtime/src/worker/tokens.mjs:102-111`; `runtime/src/worker/tokens.mjs:125-129`; `editors/vscode-tsm/syntaxes/tsm-literate-injection.json`; `docs/code-design.md (§7 'Literate C++ fragments')`; `engine/src/code/tokens.h:27-31`; `tools/convert/pbr2tsm.mjs:478-485`

Driven by pbr-book (9e53177). `if (name === 'cpp')`, every C++ block in every document has `/<<[^<>\n]+>>(?:\+?=)?/g` spans blanked for tree-sitter and tagged 'label'.

Verified false positives on ordinary C++:
- `std::cout << x >> y;` matches '<< x >>';
- `(1 << n) >> 2` matches '<< n) >>';
- `x <<= a >> b` matches '<<= a >>'.

The same regex is duplicated in a TextMate injection grammar.

The literate structure itself (definitions, `+=` continuations, links from use to definition) is not modelled. pbr2tsm emits ```cpp with the fragment name as the first code line (pbr2tsm.mjs:482-485).

*Why ad hoc:* A corpus-specific dialect is hard-wired into a language-generic provider with no opt-in. It could not be a user fence handler because tokens are not composable: structured codeblock bodies skip NEED_TOKENS (tokens.h:27-31), so a handler cannot combine its own tokens with tree-sitter's. #use is also missing, so it could not ship as a library.

*Proposed generalization (survey):* Make token providers composable.
- Registry: `$.tokens.register(tag, (text, base) => triples)`, where `base(langTag, maskedText)` calls the tree-sitter provider.
- Register `cpp-literate`, or a fence arg `cpp(literate: true)`, in a #use-able library.
- Engine side: add a codeblock arg `overlay` ([[s, e, tag], …]) that foldTokens merges with provider tokens, overlay first. A fence handler can then return `codeblock('cpp', body, {overlay})` and still get NEED_TOKENS.
- Fragment index, cross-links and continuation numbering become a user region/fence library built on labels and refs.

*Verifier:* 'Structured codeblock bodies skip NEED_TOKENS' is mislocated. The rule is in engine/src/api/doc.h:151-168 (scanTokenReqs requires kids[0]->kind == text); tokens.h:27-31 only documents foldTokens.

The feature is documented as deliberate in docs/code-design.md:133-145 ('Applies to the cpp family only'; the rationale is avoiding a fork of the 530K-line parser.c). The same section notes that the native provider links only json and tsm, so no golden covers this path.

The false positives on `std::cout << x >> y;`, `(1 << n) >> 2` and `x <<= a >> b` follow directly from FRAGMENT_RE (tokens.mjs:86).

*Verifier notes:* The minimal correct fix is opt-in, not global. pbr2tsm controls its own output, so it can emit ```cpp-literate (a LANGS alias) or a fence arg `literate: true`.

The composable provider registry and overlay arg are the right long-term design. The overlay is a new ARGK (a bump under current policy), and the token cache key must include the overlay and provider id.

### `real-world-evidence/ref-cite-format-in-cpp` — Reference and citation display forms are hard-coded in resolveRef/resolveCite; ref.form is dead

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-09**
- locations: `engine/src/resolve/resolve.cc:244-266`; `engine/src/resolve/resolve.cc:271-293`; `engine/src/resolve/resolve.cc:400-428`; `engine/src/ops/ops.def:59`; `runtime/src/worker/executor.mjs:173`; `runtime/src/worker/executor.mjs:241-266`; `tools/convert/tex2tsm.mjs:179-183`; `examples/real-world/hott-introduction.tsm:187`; `docs/design-decisions-v2.md:241`

Citation forms are C++ literals:
- `[n]` and `[1, 2]`, with no ranges and no author-year;
- the bibliography entry prefix `[n] `;
- the bibliography always goes at document end.

Only the entry bodies are JS (`$.bib.format`).

A grouped cite with one unknown key fails entirely (resolve.cc:258). Verified: `@[Church-1940tu, nope]` renders '??'.

`ref` has an ARGK `form` (ops.def:59) that resolveRef never reads and that the `ref(target)` ctor cannot set. Display is a switch on Kind: '§'+n, '表 '+n, '图 '+n, '式 ('+n+')'.

tex2tsm maps \cref, \Cref, \autoref, \ref and \eqref all to `@x`, keeps only the first key of a comma list (tex2tsm.mjs:183), and lets \crefrange survive as `⟨\crefrange⟩` (hott-introduction.tsm:187).

*Why ad hoc:* v2 §11.1 says citation 'rendering style is a JS function', but only half of it is. Display forms are code branches per kind rather than data.

*Proposed generalization (survey):* Resolve to records, format by template.
- The resolver produces a record per ref: `{targets: [{label, role, counters, formatted, ordinal, url, excerpt}], form}`.
- Text is materialized through templates from RoleSpec/config, e.g. `refForms: {figure: {number: '{sup}{n}', name: '{excerpt}', full: '{sup}{n} {excerpt}'}, cite: {numeric: '[{ordinals:ranges}]', authorYear: '({author} {year})'}}`.
- The ctor becomes `ref(targets, {form, supplement})`. A missing key degrades only its own slot.
- An optional JS formatter can run as a bounded post-resolve pass: a pure function of the records with no feedback into execution, so v2's single-pass soundness holds.
- The bibliography is placed like any collector.

*Verifier:* ARGK form is not dead overall. It carries collect's bibliography 'all' flag (resolve.cc:404-406; executor.mjs:264), which is another per-kind key reuse. On `ref` it is unread, as stated.

Confirmed:
- the literals in resolveCite (resolve.cc:259-267) and buildBibliography (resolve.cc:423);
- all-or-nothing failure (resolve.cc:257-258);
- the bibliography is always emitted at document end (executor.mjs:242-265);
- tex2tsm keeps only the first key (tex2tsm.mjs:183) and leaves \crefrange at hott-introduction.tsm:187.

*Verifier notes:* The optional post-resolve JS formatter is the unsound part.
- It adds a second JS↔WASM crossing after ingest.
- It makes every post-ops stage depend on a JS runtime. tsrc currently runs the tree, blocks, breaks and layout goldens natively from recorded .ops without any JS.

Preferred approach: the executor already formats entries in JS during execution. It should also pre-format a short cite label per entry (an author-year form) as data on group{role:bibentry}. The C++ resolver then only selects: ordinals compressed into ranges by a template, or the pre-formatted label. This keeps §11.1 single-pass, keeps 'rendering style is a JS function', and lets a missing key degrade only its own slot.

### `real-world-evidence/locale-terms-switch` — Supplement words are a two-branch if/else; document language is only a host option

- kind: adhoc · severity: medium · verdict: accurate · plan: **P1-10, P3-30**
- locations: `engine/src/api/config.h:66-71`; `engine/src/api/config.h:86-102`; `engine/src/api/wasm_api.cc:63-66`; `runtime/src/node/render.mjs:24-28`; `zball-io/eleventy.config.js:29-32`; `tools/convert/wiki2tsm.mjs:39-40`; `engine/src/resolve/resolve.cc:212`; `engine/src/resolve/resolve.cc:331`; `docs/document-model.md:82`

applyLang maps 'zh'/'ja' to 图/表/式 plus '：', and every other language to Figure/Table/Eq. plus ': '. '§' is fixed for both.

Wrong results it already produces:
- Japanese gets Simplified 图 (U+56FE); Japanese uses 図.
- zh-TW/zh-Hant also get 图, where 圖 is expected; the corpus' zh-Wikipedia page is largely Traditional.

Other user-visible strings are C++ literals: ↩, ' — ', '[n] ', '??'.

The language reaches the engine only from the host (tsr_set_lang, renderTsm opts.lang):
- English documents rendered 图 until report fix #7.
- The blog regex-parses YAML front matter to find the language (eleventy.config.js:29-32).
- wiki2tsm hard-codes '参见：'/'See also: ' because there is no localized term to target.
- The per-run `lang` style patch (document-model §3) is not consulted.

*Why ad hoc:* Locale data is embedded as code branches, and the document's language is a host knob rather than a document property.

*Proposed generalization (survey):* Move terms into a locale table held as data.
- Example: `{"en": {figure: 'Figure', table: 'Table', equation: 'Eq.', section: '§', capSep: ': ', notes: 'Notes', references: 'References', seeAlso: 'See also', theorem: 'Theorem', unresolved: '??'}, "zh-Hans": {…}, "zh-Hant": {figure: '圖', …}, "ja": {figure: '図', …}}`, compiled into gen/ with a BCP-47 fallback chain.
- RoleSpec.supplementTerm names a key.
- The effective language at a node is the style stack's `lang` patch, falling back to the document language.
- The document language and metadata are declared in the document (`#set(lang: 'en')` or a doc({lang, title}) statement producing a meta node). Hosts read them back from the compile result, so front-matter regexes become unnecessary.
- Users extend the table with `$.terms('de', {…})`.

*Verifier notes:* Verified points:
- applyLang (config.h:89-102): Japanese gets U+56FE 图 rather than 図, and zh-Hant gets 图 rather than 圖.
- The default without a host lang is zh (wasm_api.cc:62).
- The blog front-matter regex is at eleventy.config.js:29-32.

Declaring the language in the document as `#set(lang:…)` would add a new keyword form, but v2 §3 treats keyword forms as a closed set. Use a constructor instead, such as `$.doc({lang, title})` or `#doc(…)`, consistent with 'every syntax form is sugar for a constructor'.

The per-node language is already available after instantiation as Styling.lang (model.h).

### `real-world-evidence/codepoint-heuristics` — Corpus fixes classify text by hard-coded codepoints and byte length instead of lang/semantic properties

- kind: adhoc · severity: low · verdict: accurate · plan: **P4-02**
- locations: `engine/src/emit/emit.cc:430-445`; `engine/src/emit/emit.cc:207-226`; `engine/src/api/config.h:40-43`; `docs/real-world-report.md (fixes #2, #3)`

Two corpus fixes are heuristics:
- **Fix #3 (quotes):** U+2018, 2019, 201C and 201D are Latin glyphs when the previous character is not CJK and the next is neither CJK nor punctuation. Dashes and ellipses have separate rules.
- **Fix #2 (long tokens):** any unhyphenatable token with `w.size() >= 20` bytes gets break opportunities after `/ ? & = . - _` at urlBreakPenalty, whether or not it is a URL. Identifiers such as `Approximate_dp_dxy()` and long compounds qualify. The `link`/autolink node, which knows it is a URL, is not consulted.

*Why ad hoc:* These are patches for individual incidents, keyed on codepoints and byte length. The information that actually decides each case already exists elsewhere in the model: the run language, and whether the node is a link or code.

*Proposed generalization (survey):* (a) Resolve ambiguous widths by language. Use a UAX #11 'A'-class table (curly quotes, dashes, ellipsis, middle dot, ×, °, …), resolved per run from the effective `lang` (style stack or document). Fall back to the neighbour heuristic only when lang is unset.

(b) Add a `breakPolicy` style property (normal | url | identifier | anywhere | none).
- link/autolink/code ctors set it, and `#style({breaks: 'url'})` can set it explicitly.
- Per-policy break tables and penalties live in config, e.g. `breakPolicies.url = {after: '/?&=.-_', penalty: 1.2, minLen: 20}`.

*Verifier:* One more symptom of the coupling the report describes: URL break opportunities are gated on `!noHyphen` (emit.cc:208). Figure captions (emit.cc:476) and headings (emit.cc:502) set noHyphen, so long URLs in captions and headings get no break points. The hyphenation policy and the URL-break policy are wired together.

Doc drift: real-world-report.md:27 lists `_` among the separators, but the comment at config.h:40-41 omits it.

*Verifier notes:* A breakPolicy style property separates the two policies. The lang-resolved ambiguous-width table is compatible with §7, because font selection, and therefore measurement, already follows the class split.

### `real-world-evidence/sup-attach-private` — Superscript and 'glue to previous' exist only for footnote markers; the public inline vocabulary lacks them

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-08, P4-07**
- locations: `engine/src/model/model.h:20-22`; `engine/src/emit/emit.cc:84-90`; `engine/src/resolve/resolve.cc:342-350`; `runtime/src/worker/executor.mjs:6-11`; `runtime/src/worker/executor.mjs:225-229`; `tools/convert/pbr2tsm.mjs:497`; `tools/convert/html2tsm.mjs:66`; `tools/convert/wiki2tsm.mjs:106`; `tools/convert/tex2tsm.mjs:176`; `docs/document-model.md:70-77`

Only buildMarker uses CLS_SUP (bit 19, which is missing from document-model's frozen bit list) and sizeMul. emit makes the block before a labelled superscript ref non-breakable by testing that bit.

`#style` exposes only bold, italic, underline, overline, strike and an absolute sizePx. There is no sub, sup, small caps or relative size, and there is no inline raw ctor.

Converters therefore lose these constructs:
- wiki2tsm strips `<sup>`, `<sub>`, `<small>`, `<u>` and `<s>`.
- tex2tsm drops \textsc.
- html2tsm and pbr2tsm emit `^text`, which tsm treats as literal: 120 literal `^&dagger;` sidenote anchors remain in pbr-en.

*Why ad hoc:* A built-in feature relies on capabilities the public surface cannot express. User note systems (sidenote marks, ①, custom markers) therefore cannot reproduce footnote behaviour.

*Proposed generalization (survey):* Add a typed inline style vocabulary in StyleDelta, with string-keyed args:
- `script: 'super' | 'sub'`;
- `sizeMul`;
- `smallCaps`;
- `attach: 'prev' | 'next' | 'both'`, a general glue/no-break property that emit honours for any inline and that replaces the CLS_SUP test.

Also add a public inline `raw(html, {w, h})` ctor.

The footnote marker then becomes `style({script: 'super', sizeMul: 0.7, attach: 'prev'})[ref(…)]`, built in the JS prelude.

*Verifier:* The glue rule applies to any ref whose first block carries CLS_SUP, labelled or not (emit.cc:89).

There is also a block-scope back door. `$.style.push(number)` pushes raw class bits (executor.mjs:276-277). I verified that `$.style.push(1<<19)` turns a paragraph into `<sup>`. So SUP is reachable as raw bits at block level, but not as a typed inline property.

*Verifier notes:* The 'script' raise must stay paint-only, as CLS_SUP is today (position: relative), with the size going through the measured sizeMul. That is what the §7 measurement contract requires.

### `real-world-evidence/description-list-missing` — No definition/description list; #term is a glossary site, so converters and the blog emulate one with bold text

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-34**
- locations: `engine/src/resolve/resolve.cc:214-224`; `engine/src/resolve/resolve.cc:444-468`; `tools/typ2tsm.mjs:74-78`; `tools/convert/tex2tsm.mjs:185-189`; `tools/convert/wiki2tsm.mjs:124`; `examples/real-world/hott-introduction.tsm:57`; `zball-io/src/posts/vscode-tsm.tsm:26-37`; `zball-io/src/posts/vscode-tsm.tsm:49-53`; `docs/design-decisions-v2.md:240`

`#term[name][desc]` auto-labels with the name, enters the glossary, and renders as the bold name + ' — ' + desc (C++ literals).

Every source with description lists works around it:
- typ2tsm maps Typst term lists to glossary terms, so a repeated name raises label-duplicate.
- tex2tsm turns `description` into `- *term* body`.
- wiki2tsm turns `;`/`:` lines into plain paragraphs.
- The blog hand-writes `- *语法高亮*：…` items and bold run-in paragraph heads.

Documented trade-off: v2 §11.1 drops the `/ term:` sugar because 'this is a reference-shaped feature, not a list-shaped one'.

*Why ad hoc:* One construct conflates two orthogonal concerns, list layout and glossary registration, and its presentation is fixed in the resolver. The real-world sources (LaTeX, wiki, blog) need the list shape, and the language offers only the reference shape.

*Proposed generalization (survey):* Keep `term` as reference-shaped and add list shapes.
- Signature: `list({form: 'description' | 'bullet' | 'ordered', marker, runIn}, item({term: […]}, …blocks))`.
- Items carry an optional term slot. The semantic serializer emits dl/dt/dd; emit uses hanging or run-in layout via ParShape (see parshape-prefix).
- Glossary registration becomes a trait: `item({term, define: true})`, or a 'term' RoleSpec with labelable plus collect.
- Run-in heads: `heading({runIn: true})`, or a paragraph `lead` slot that theorem headers can also use.

*Verifier:* typ2tsm is a smoke-corpus extractor for Typst's own test suite (typ2tsm.mjs:1-15), not a real-world converter. Its mapping of `/ term:` to #term is its own choice, so it is weak evidence.

*Verifier notes:* design-decisions-v2.md:240 (§11.1) documents term as reference-shaped and deliberately drops the `/ term:` list sugar. What the corpus shows is a missing list shape, not an ad-hoc term. The genuinely ad-hoc parts are the presentation literals in buildTerm (bold name plus ' — ', resolve.cc:451-457). Adding list form 'description' while keeping term a definition site respects the documented split.

### `real-world-evidence/table-model-v1` — Table args are a column count plus a one-letter align string; no captions, headers, spans or widths

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-14**
- locations: `runtime/src/worker/executor.mjs:76-93`; `engine/src/emit/emit.cc:647-684`; `engine/src/api/config.h:78-80`; `docs/document-model.md:176-195`; `docs/design-decisions-v2.md:145`; `tools/convert/tex2tsm.mjs:190-198`; `tools/typ2tsm.mjs:96-151`; `tools/convert/wiki2tsm.mjs:18`; `examples/real-world/hott-introduction.tsm:144-161`; `zball-io/src/posts/vscode-tsm.tsm:66`

The model is `table{cols, align: 'llr', label}`:
- columns are equal width;
- each cell is flattened into one inline stream (emit.cc:672);
- there is no header row, colspan/rowspan or caption.

Consequences in the corpus:
- tex2tsm counts [lcr] and drops the spec, \multicolumn, the rules and the caption environment.
- The HoTT caption becomes `_…_ <tab-pov>`. The label renders as literal text and `@tab-pov` resolves to '??' (verified), visible in the published blog example.
- typ2tsm rejects any table with headers or spans (typ2tsm.mjs:141).
- wiki2tsm deletes all wikitables.
- The blog lists column widths as a gap.

Documented as v1 geometry in document-model §6.3.

*Why ad hoc:* Per-column properties are a positional character string that cannot grow (widths, rules, numeric alignment) without a new ARGK and an OPS bump. Captions are figure-only.

*Proposed generalization (survey):* Give tables a structured spec.
- Column spec: `cols: [{width: 'auto' | '1fr' | '30%' | px, align: 'l' | 'c' | 'r' | 'num', rule}]`, or an integer for N auto columns.
- `header: n` on the table; `{colspan, rowspan, header}` args on tcell.
- Cell content becomes a list of FlowUnits rather than one inline stream.
- Intrinsic sizing: max-content is the cell broken at infinite width, min-content its longest unbreakable block. Then apply CSS-like auto table layout.
- Captions come from RoleSpec('table').caption, so tables, figures and listings share captioning.

*Verifier:* typ2tsm skips untranslatable Typst test cases by design (typ2tsm.mjs:141, 'not translatable'), so it is weak real-world evidence.

'align' is an opaque string, so per-column widths could be encoded in it without a new ARGK. The point about the encoding is valid, but 'cannot grow without a new ARGK' is overstated.

Equal columns are documented v1 (architecture.md M6 entry 'equal columns'; doc.h comment 'equal columns (v1)').

Verified: the HoTT `<tab-pov>` stays literal (hott-introduction.tsm:161) and @tab-pov renders '??'.

*Verifier notes:* Auto table layout fits the pull loop: word widths arrive before breaking, so min-content and max-content can be computed in typeset() ahead of the cell breaks. Captions belong in the general role or label mechanism, not in a table-specific caption arg.

### `real-world-evidence/figure-model-single-image` — Figure = one image plus caption paragraphs; floating and subfigures are figure-only special cases

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-15**
- locations: `engine/src/emit/emit.cc:701-743`; `engine/src/emit/emit.cc:776-818`; `runtime/src/worker/executor.mjs:114-121`; `runtime/src/worker/executor.mjs:212-217`; `docs/figure-design.md:159-184`; `tools/convert/pbr2tsm.mjs:420-432`; `tools/convert/wiki2tsm.mjs:89`; `examples/real-world/wiki-typesetting.tsm:56`

Float wrap is enabled only for a group with role 'figure' that contains an image with side left/right. A floated figure drops its non-paragraph children (figure-design §8), and the float caption is a TableCell. Nested figures number as siblings (verified).

Display sizing is scale-only and alt text is required, so converters hard-code `alt: "figure"` and `scale: 0.8` or `0.45`. A Commons .ogv video is emitted as an image (wiki-typesetting.tsm:56) and falls back to a placeholder.

*Why ad hoc:* Placement (float) and media type belong to a particular role+kind combination instead of being properties of block units.

*Proposed generalization (survey):* Generalize figures, placement and media.
- Figure is a RoleSpec with a `body` slot (any blocks, including nested figures, code, tables, math) and a `caption` slot.
- A subfigure is a role whose counter resets on 'figure' with format '(a)'.
- Floating becomes a general `place: {float: 'left' | 'right', width, clear}` arg on any block unit, honoured by the float tracker.
- Media generalizes to `image{src, type, sources: [{src, w}]}` (srcset/DPR); video and iframe become raw-backed media units with declared size.

*Verifier:* 'Float wrap is enabled only for a group with role figure' is wrong. Any image with side=left|right floats (emit.cc:740-741; doc.h float tracker; verified with a standalone #image).

What is specific to figures:
- caption paragraphs ride as TableCells (emit.cc:799-813);
- a float figure drops its non-paragraph children (documented in figure-design.md §8).

'Alt text is required' is unsupported. No check or diagnostic exists, and emit.cc:715 just reads the arg. The converters' `alt: "figure"` is their own choice.

Confirmed: video emitted as an image (wiki-typesetting.tsm:56) and nested figures numbered as siblings.

*Verifier notes:* Generalizing 'place' to any block unit needs a float box width for units that have no intrinsic width, such as tables and code. That depends on the shrink-to-fit intrinsic sizing from table-model-v1.

The report also misses a direct consequence of the first-paragraph-is-caption convention in the semantic serializer; see the missed items.

### `real-world-evidence/parshape-prefix` — LineWidths is a two-piece prefix; stacked floats, hanging indent and first-line indent are separate special cases

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-08**
- locations: `engine/src/break/break.h:14-21`; `engine/src/api/doc.h:265-350`; `engine/src/emit/emit.cc:476-481`; `docs/figure-design.md:83-127`; `docs/real-world-report.md (Engine gaps: stacked floats; bibliography hanging indent)`

Line widths are `LineWidths{constant, narrow, narrowK}`, plus per-unit `narrowLeft`, `floatShiftSu` and `floatClearSu`.

The 'stacked floats' gap was closed by adding floatShiftSu. It handles the same side only; opposite-side floats still clear, and the report itself proposes piecewise widths.

Bibliography and note entries cannot hang (open gap in the report). Notes get their hanging look from list indentation. The paragraph first-line indent is a synthetic BF_INDENT block.

*Why ad hoc:* Each paragraph-shape need adds a field. figure-design names TeX \parshape semantics but implements a prefix.

*Proposed generalization (survey):* Replace the prefix with a general paragraph shape.
- Proposed struct: `struct ParShape { std::vector<Seg{u32 lines; Su left; Su width;}> segs; Seg tail; Su at(u32) const; Su leftAt(u32) const; }`.
- An exclusion tracker produces it from any number of active exclusions per side, `{side, topSu, bottomSu, widthSu}`.
- The breaker is unchanged; KP already calls at(i). Layout reads leftAt(i).
- Paragraph style props `indent` (first line), `hang: {indent, after}` and `shape` compile to segments.

*Verifier notes:* The prefix form is documented (break.h:14-15; figure-design.md §8, 'KP needed zero changes'). real-world-report.md:51-56 itself proposes piecewise widths, so ParShape follows the project's own analysis. KP calls widths.at(line), so the change is confined to the type and the cache-key hash.

### `real-world-evidence/resource-and-config-plumbing` — Per-resource pull states and per-knob C exports; bibliography uses a third mechanism; Node and browser get different configs

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-21**
- locations: `engine/src/api/wasm_api.cc:42-76`; `engine/src/api/wasm_api.cc:96-184`; `runtime/src/node/render.mjs:20-50`; `runtime/src/worker/executor.mjs:185-188`; `runtime/src/worker/executor.mjs:241-266`; `docs/notes-design.md:113-120`; `docs/notes-design.md:134-146`; `docs/document-model.md:310-327`; `zball-io/eleventy.config.js:61-75`; `zball-io/src/_includes/base.njk:34-36`

Resources:
- NEED_MEASURE, NEED_TOKENS and NEED_IMAGES each have their own JSON section in tsr_measure_requests and their own provide export.
- Bibliography data was loaded in the executor instead (notes-design 'As built'), so the designed NEED_BIB never happened.
- renderTsm answers only tokens.

Config:
- It is not the documented JSON with 'unknown keys → diagnostic' (document-model §11). It is tsr_config positional params plus tsr_set_lang, _font, _cjk_font, _punct_compress, _snap_kerning and _code_features.
- The Node and browser paths get different subsets. The blog passes fonts and paraIndentEm only to the client engine, then re-implements the 2em indent in CSS for the static page.

*Why ad hoc:* Each resource and each knob added its own channel. Nothing lets the document itself carry settings.

*Proposed generalization (survey):* Unify resources and config.
- One resource protocol: `ResourceReq{id, kind: strRef, key: strRef, params}`. `tsr_requests()` lists them generically and `tsr_provide(id, bytes)` answers through per-kind decoders registered in C++.
- The host keeps a registry `providers[kind]`: image dims, tokenize, CSL-JSON, fonts, plots.
- Config: a single `tsr_configure(json)` validated against a generated schema.
- A document-level `#set({…})` writes the same keys as a meta node, applied before emit, so Node, browser and static export all see one configuration.

*Verifier notes:* notes-design 'As built' records why bibliography loading moved into the executor: it 'already runs async JS with host access'. That is the more general channel for anything the document program needs (data, modules, a #!plot builder, includes). Routing CSL-JSON back into a C++ pull protocol would be a regression.

Pull states should stay for needs that only the engine can discover after ingest: word metrics, image dimensions, tokens. Unifying those three into one request/response schema is the sound part.

Document-level `#set` keys must be limited to document semantics (lang, paragraph indent, terms, counters). Viewport and host keys (widthPx, font loading) must stay host config. Otherwise a document can override the host's layout, which defeats the static-versus-typeset parity the proposal is meant to achieve.

### `real-world-evidence/notes-popups-dom-scraping` — Footnote popups reverse-engineer the typeset DOM through 'fn-'/'fnref-' string prefixes

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-04, P3-06**
- locations: `runtime/src/main/shell.mjs:123-200`; `runtime/src/main/shell.mjs:146`; `runtime/src/main/shell.mjs:175`; `engine/src/resolve/resolve.cc:203-213`; `engine/src/resolve/resolve.cc:342-394`; `tools/convert/wiki2tsm.mjs:50-54`; `docs/notes-design.md:23-35`; `docs/notes-design.md:76-96`

installNotePopups:
- finds markers with `a.tsr-sup[href^="#tsr-fn-"]`;
- walks sibling .tsr-line elements until the next id or .tsr-marker;
- strips `a[href^="#tsr-fnref-"]` links.

This is a string contract with resolve.cc's label prefixes. It took four follow-up shell commits (95da13d, c1d0aae, 25ff46c, 25b89ee).

Notes render as an ordered list at 0.85× with a ↩ link, all C++ constants. Named notes (`#note(name)[…]`) are designed but not built, so wiki2tsm copies each named <ref> body into every use site.

*Why ad hoc:* A presentation feature for one kind of ref is implemented in the shell against private DOM structure. Citations, figures and equations get no equivalent.

*Proposed generalization (survey):* Define a generic ref-preview contract.
- The layout/render result carries `refs: [{anchor, targetLabel, targetPid, role}]`.
- Every labelled unit can be rendered on its own (`tsr_render_label(label)`).
- The shell shows popovers for any ref according to RoleSpec.preview.
- Named notes reuse the definition-site/use-site join that terms already use (`#note(name)[…]` plus `^[name]`).

*Verifier:* 95da13d introduced the popups. The follow-ups are three commits (c1d0aae, 25ff46c, 25b89ee), not four.

*Verifier notes:* The DOM contract also depends on the user label namespace through the 'fn-' and 'fnref-' prefixes. A user label `fn-1` hijacks a footnote marker; see the missed items. A ref-preview record in the render result removes both couplings.

### `real-world-evidence/presentation-constants` — Presentation decisions are C++ literals; the static page needs a hand-written CSS copy

- kind: adhoc · severity: medium · verdict: accurate · plan: **P3-01**
- locations: `engine/src/api/config.h:64-65`; `engine/src/api/config.h:76-84`; `engine/src/emit/emit.cc:471-481`; `engine/src/emit/emit.cc:517`; `engine/src/resolve/resolve.cc:331`; `engine/src/resolve/resolve.cc:346`; `engine/src/resolve/resolve.cc:378`; `engine/src/resolve/resolve.cc:423`; `engine/src/resolve/resolve.cc:457`; `runtime/src/main/shell.mjs:71-79`; `zball-io/src/_includes/base.njk:34-36`; `zball-io/src/_includes/base.njk:48-56`

Hard-coded presentation values:
- headingSizeMul 1.6/1.35/1.15;
- captions centred, ragged and unhyphenated;
- list markers '•' and 'N.';
- note text at 0.85×, marker at 0.7×;
- ' — ' for glossary and term entries;
- '[n] ' for bibliography entries;
- table padding constants.

TSR_CSS styles only the typeset DOM; its .tsr-flow rules are token colours only. The blog therefore re-implements headings, figure/figcaption, blockquote and the 2em 中文 indent for the static page (base.njk:34-36, 48-56). That is two styling systems that can drift.

*Why ad hoc:* Each feature's look is code inside the stage that builds it, and nothing is shared between the two serializers.

*Proposed generalization (survey):* Express presentation as a data spec matched on kind, role and level.
- Example rules: `[{match: {kind: 'heading', level: 1}, style: {sizeMul: 1.6, bold: true}, block: {ragged: true, hyphenate: false}}, {match: {role: 'figure', slot: 'caption'}, block: {align: 'center', ragged: true}}, {match: {kind: 'list'}, marker: {bullet: '•', ordered: '{n}.'}}]`.
- It lives in config or the JS prelude and can be overridden with `$.show`/`#set`.
- Instantiation and emit apply it on the typeset path.
- The semantic serializer compiles the same spec to CSS (`tsr_render_css()`) for static pages.

*Verifier notes:* The direction is right, but the review proposes three overlapping presentation mechanisms for the same knobs: RoleSpec.caption {align, ragged, hyphenate}, this match-rule spec, and $.show. Define one layering instead: a semantic RoleSpec, then data presentation rules matched on kind, role and slot, then $.show for execution-time overrides.

Metric-affecting values (sizeMul, bold) must still be applied engine-side before measurement (§7). Compiling the spec to CSS is only for the semantic or static page, where exactness is not required.

### `real-world-evidence/math-extensibility` — Math has no macros, no 2-D array primitive and no alphabets; converters expand by regex and flatten

- kind: adhoc · severity: medium · verdict: partly · plan: **P3-29**
- locations: `tools/convert/tex2tsm.mjs:73-113`; `tools/convert/tex2tsm.mjs:125-128`; `tools/convert/tex2tsm.mjs:140`; `tools/convert/tex2tsm.mjs:166-167`; `tools/convert/pbr2tsm.mjs:187-215`; `tools/convert/pbr2tsm.mjs:259-263`; `tools/convert/pbr2tsm.mjs:327-330`; `examples/real-world/hott-introduction.tsm:43`; `examples/real-world/hott-introduction.tsm:109`; `examples/real-world/hott-introduction.tsm:123`; `docs/math-design.md:222-226`; `docs/math-design.md:399-401`

Macros: two regex tables pre-expand the HoTT macros (\eqv, \idtype, \prd, \sm…). They fail on nested arguments, producing `$Id(v,a)rA$`, a garbled Σ/Π formula and `⟨\mathsf⟩LEM_n`. Unknown macros become identifiers.

Alphabets: \mathbb{N} becomes 'NN' by convention, and \mathcal, \mathbf and \mathsf are dropped ('the engine has no bold/sans/cal', cdc9177).

2-D layout:
- pbr2tsm emits matrices as `[a, b; c, d]` ('tsm has no matrix construct yet').
- MathSpeak StartLayout blocks (aligned/cases) become rows joined by ' ; '.
- tex2tsm deletes & and \\ in align and multline.

Math islands are verbatim strings, so `#let` definitions cannot be used inside `$…$`.

*Why ad hoc:* Each missing math capability is papered over in each converter, while the math language keeps a closed function set.

*Proposed generalization (survey):* (a) Math definitions. A per-document macro table, declared with `$.math.def('eqv', 2, '#1 simeq #2')` or a JS function returning math source, is serialized as a meta node. The C++ math parser consults it and reports arity diagnostics.

(b) One 2-D `array` MathBox primitive: rows × cols, column alignment, delimiters, gaps and alignment points. `mat(…)`, `cases(…)`, `binom` and multi-line display with `&`/`\\` all desugar to it.

(c) Alphabet variant functions `bb`, `cal`, `frak`, `bold`, `sans` and `mono`, mapped to Unicode Mathematical Alphanumerics, with a fallback math font for the ranges Euler lacks.

*Verifier:* `\mathbb{N}` → `NN` is not lossy. `AA…ZZ` is the designed blackboard-bold shorthand (design-decisions-v2 §13; math-design.md:288; tools/mathc.py:247), and I verified that `$NN$` lays out as the glyph ℕ in the mathbox stage. So 'NN-doubling convention' and 'alphabets … doubled' misdescribe a correct mapping.

What is actually dropped:
- \mathcal, \mathbf and \mathsf (tex2tsm.mjs:125-127);
- MathSpeak bold, script and fraktur (pbr2tsm.mjs:327-330).

Confirmed:
- macro failures (hott-introduction.tsm:43 `$Id(v,a)rA$`, :109, :123);
- matrix and StartLayout flattening (pbr2tsm.mjs:187-215, 259-263);
- tex2tsm deleting & and \\ (tex2tsm.mjs:166-167).

*Verifier notes:* Partly documented: math-design §7 defers alignment points as 'a region-shaped feature for later', and alphabets are 'not done' because they need a second font (math-design.md:399-401). The converters' regex macro tables are the ad-hoc part.

There is a cheaper alternative to a second macro language inside the C++ math parser: allow `#f(…)` splices inside math islands, compiled by codegen into string concatenation for the src of the already-public mathinline/mathblock constructors. Macros become ordinary #let JS functions, which honours 'every syntax form is sugar for a constructor', and math still parses engine-side at emission (math-design §10).

The 2-D array primitive is the right subsuming construct for mat, cases and aligned.

### `real-world-evidence/no-cross-document-labels` — No cross-document references, project numbering or #use: books split into files lose every inter-file ref

- kind: adhoc · severity: high · verdict: partly · plan: **P3-31**
- locations: `tools/convert/pbr2tsm.mjs:496`; `tools/convert/pbr2tsm.mjs:508-509`; `tools/convert/tex2tsm.mjs:183`; `examples/real-world/hott-introduction.tsm:179-199`; `engine/src/resolve/resolve.cc:271-283`; `engine/src/codegen/codegen.cc:209-212`; `docs/architecture.md:219-226`; `docs/real-world-report.md (Converter gaps: cross-chapter \cref → ??)`

The published HoTT example resolves 58 refs to '??': 19 distinct part-*, cha-* and sec-* targets, verified by rendering the corpus file through recorded ops.

pbr2tsm drops every non-http link (pbr2tsm.mjs:496). The 163-section conversion therefore keeps about 1,100 references as plain text ('Figure 1.2', 'Section 4.3.1', 'Chapter 5').

#use is unimplemented. Verified: `#use("./x.js")` compiles to `val((use("./x.js")))`, which throws ReferenceError at runtime. So shared definitions cannot cross files either, and the blog cannot link labels across posts.

*Why ad hoc:* The label table is per-document by construction, and multi-document projects have no representation at all. Both corpora that motivated the report are multi-file books.

*Proposed generalization (survey):* Add label manifests and project-level resolution.
- Each compile can emit a deterministic `labels.json`: label → {role, counters, formatted, url, excerpt}.
- Documents import manifests via `#import-labels('ch2.labels.json')`, or via project config passed to renderTsm/export-static.
- The resolver adds imported labels as external entries with absolute URLs.
- Counters take start/prefix values from the project (chapter numbers).
- Implement #use through the same project resolver (module path → ES import).
- At project level this is two passes (compile all, then resolve with manifests). Each document's resolve stays single-pass.

*Verifier:* Verified 58 '??' with 19 distinct targets. One of them, `tab-pov` (1 ref), is in-document and is a table-caption failure, so 18 targets and 57 refs are cross-chapter.

The pbr textual ref count is about 987, not about 1,100.

The HoTT '??' are disclosed as intentional on the published page ('cross-chapter references resolve to ?? on purpose', zball-io/src/docs/example-hott.tsm:215) and in real-world-report.md:103-105.

Confirmed: `#use("./x.js")` compiles to `val((use("./x.js")))` and throws ReferenceError.

*Verifier notes:* This is an absent mechanism rather than a special case, and v2 §11.1 deliberately scopes resolution per document. Label manifests keep per-document single-pass resolution. They must be declared inputs, like resources, and included in fixture inputs so the goldens stay deterministic.

### `real-world-evidence/markup-reentry-missing` — m`…`/m.parse return plain text; fence handlers cannot produce markup, so the docs duplicate every example

- kind: adhoc · severity: medium · verdict: accurate · plan: **P2-13**
- locations: `runtime/src/worker/executor.mjs:146-155`; `runtime/src/worker/executor.mjs:231-236`; `docs/design-decisions-v2.md:75`; `docs/architecture.md:219-226`; `docs/notes-design.md:122-127`; `zball-io/src/docs/figures.tsm:23-33`; `zball-io/src/docs/code.tsm:32-46`; `zball-io/src/docs/math.tsm:9-17`

The tagged template is 'M1: cooked-text tag; runtime markup re-entry … is M2', and the fence ctx.m behaves the same way. The engine half (parseInlineFragment) exists, but there is no JS surface for it.

Effects:
- The blog's documentation pages write every sample twice: once as a ````tsm source listing, then the same markup live.
- `$.bib.format` returns ctor shadows, although notes-design specifies 'formatEntry(entry) → inline markup'.
- A converter cannot be packaged as a fence handler (```latex → parse).

*Why ad hoc:* v2 §2 makes m`…` a core mechanism. Its absence pushes features into C++ or into duplication.

*Proposed generalization (survey):* Implement `m.parse(str, {offset})` returning shadow nodes via tsr_parse_fragment: the ops slice is rebased into the current OpBuf, and spans are offset for diagnostics. Implement m`…` with values interpolated as splice holes. Ship an `example` fence handler (` ```tsm(render: true) `) that renders the code block and the live content from one copy.

*Verifier:* `$.bib.format` returning constructor shadows (executor.mjs:253-261) is not evidence of the gap. Shadows are more general than the designed 'formatEntry → inline markup string': they are composable and need no re-parse.

The deferral is documented (architecture.md M6; executor.mjs:151 and 231).

*Verifier notes:* The doc-example duplication does not need runtime re-entry. Give region and fence handlers the interior source text and span; codegen already has the provenance. A user `#!example … #example!` handler can then return seq(codeblock('tsm', src), ...kids) from a single copy, with no WASM re-entry and nothing in the ops contract touched.

Keep m.parse for markup that is genuinely generated at runtime, such as converter fences. It needs a parse callback injected into execute(), because the executor is engine-agnostic (render.mjs and worker.mjs pass only JS text), and it must keep Node/browser parity.

### `real-world-evidence/ctor-name-collision-fatal` — A #let that collides with any of ~31 destructured constructor names kills the whole document

- kind: issue · severity: high · verdict: partly · plan: **P0-05, P2-02**
- locations: `engine/src/codegen/codegen.cc:209-221`; `docs/design-decisions-v2.md:73`

The document function destructures text, para, em, strong, list, item, code, link, rule, seq, val, m, style, image, notes, note, term, ref… as parameters. A top-level `#let` with any of these names is a module-level SyntaxError (verified with `#let strong`), so the document renders nothing. This breaks v2 §2's per-top-level-block containment and §11's error blocks. Generic names like list, code, text, style and image are likely user identifiers.

*Verifier:* The collision is accurate: 31 destructured names (codegen.cc:209-212), and `#let strong` raises a SyntaxError (verified).

'This breaks v2 §2's per-top-level-block containment' implies the containment otherwise exists. It does not exist for any runtime error. codegen.cc:214-229 emits bare `__emit(…)` statements with no wrapper. I verified that `Value: #(nope.x).` aborts the whole document ('nope is not defined'), render.mjs:33 propagates the error, and eleventy.config.js:33 then fails the blog build.

*Verifier notes:* Moving the constructors out of the parameter scope fixes name collisions only. The documented containment (design-decisions-v2.md:73 and 229; the document-model diagnostic 'script-error … per top-level block') also needs two things:
- a per-top-level wrapper that emits an error node with the item's span and pops the style stack to its entry height;
- separate compilation units for code statements, because a module-level SyntaxError cannot be caught per block.

### `real-world-evidence/heading-numbers-invisible` — Refs and the TOC print heading numbers that headings never display

- kind: issue · severity: medium · verdict: accurate · plan: **P3-03**
- locations: `engine/src/resolve/resolve.cc:131-155`; `engine/src/resolve/resolve.cc:285`; `tools/convert/pbr2tsm.mjs:508-509`

The resolver computes '1.1' and uses it in refs ('§1.1') and the TOC. The heading text itself has no number (verified: semantic output `<h2>Sub</h2>`). pbr2tsm strips the source numbers, expecting the engine to number, so in converted books section numbers vanish entirely while refs cite them.

*Verifier notes:* Unnumbered heading display is a defensible default for the blog. The defect is that refs and the TOC print numbers the heading itself never shows, which is an inconsistency. A display flag driven by the counter or presentation spec resolves it.

### `real-world-evidence/labels-on-unsupported-nodes-silent` — Labels on groups other than figure, and trailing <x> on paragraphs, fail silently at the definition site

- kind: issue · severity: medium · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:157-181`; `engine/src/inline/inline.cc:290-306`; `examples/real-world/hott-introduction.tsm:161`

`#!aside(label: …)` and theorem regions are never added to the label table, and a trailing `<x>` on a paragraph stays literal text (verified for HoTT's `<tab-pov>`). The only signal is a generic ref-unresolved warning at each use. A definition-site diagnostic ('label on a node kind that cannot be referenced') would have caught the converter bug.

*Verifier:* It is worse than reported in two places:
- The semantic serializer emits id="tsr-<label>" for unregistered group labels (verified for aside and theorem), so the anchor exists while the ref fails.
- inline.cc:291-306 consumes ` <id>` after ANY `$…$`, including inline math, and codegen.cc:112-117 then drops it. `$x^2$ <eq-in>` loses its label with no diagnostic (verified: the AST carries label="eq-in", the JS is mathinline("x^2") only, and @eq-in renders '??').

*Verifier notes:* A definition-site diagnostic is a good minimum. The general fix is one label rule plus registration of every labelled node (see the missed items).

### `real-world-evidence/span-loss-synthesized-nodes` — Region-built nodes and splice values carry @[0,0) spans

- kind: issue · severity: medium · verdict: accurate · plan: **P2-04**
- locations: `runtime/src/worker/executor.mjs:76-121`; `docs/document-model.md:24-26`

trow, tcell, region paras, figure images and val() splice texts all dump with span @[0,0) (verified in tree dumps). document-model §1 requires splice-produced nodes to take the splice or region span. This breaks editor double-click, diagnostics anchoring and data-s anchors inside tables and figures.

*Verifier notes:* Verified with the tree dumps:
- region paras, trow and tcell are @[0,0);
- splice values from `#tg` are text @[0,0);
- region-built paras such as the figure caption para are @[0,0).

The executor builders (executor.mjs:76-121) should give synthesized nodes the region span or the union of their children's spans, as document-model.md:26 requires.

### `real-world-evidence/grouped-cite-all-or-nothing` — One unknown key turns a whole grouped citation into '??'

- kind: issue · severity: low · verdict: accurate · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:256-258`

resolveCite returns false if any key is missing, so `@[a, b, missing]` renders '??' and loses the two valid citations (verified).

*Verifier notes:* resolve.cc:257-258 returns false when any key is missing, and the whole group then renders '??'.

### `real-world-evidence/spec-drift` — Normative docs describe mechanisms that do not exist; tools were written against the smaller reality

- kind: issue · severity: medium · verdict: partly · plan: **P3-37, P5-02**
- locations: `docs/document-model.md:70-82`; `docs/document-model.md:124`; `docs/document-model.md:310-327`; `docs/notes-design.md:76-80`; `docs/architecture.md:219-226`

Missing pieces:
- document-model §11 JSON config with unknown-key diagnostics;
- §5 user counter classes and resetAt;
- §3's 'frozen' bit list, which omits CLS_SUP (bit 19);
- notes-design: named notes and noteMarks;
- v2 §2: m`…` re-entry;
- #use (architecture M6 'Deferred').

The converters and the blog encode workarounds for exactly these gaps (wiki named refs duplicated, chapter numbers stripped, examples duplicated).

*Verifier:* Several listed items are recorded as deferred or not built in the as-built notes: named notes (notes-design 'As built': 'NOT implemented'), m.parse and #use (architecture.md M6 'Deferred'; executor.mjs:151 and 231). That is recorded scope, not silent drift.

Real drift the report missed:
- the document-model.md:98 buffer header says 'version u8 (=2)', but OPS_VERSION is 6;
- CLAUDE.md says 'executor (ops v5)';
- design-decisions-v2.md:248 (§12) says sugar compiles to balanced push/pop pairs, but codegen emits strong()/em() styled nodes (codegen.cc:33-37);
- per-top-level-block catching of runtime errors (v2 §11) is documented but not implemented;
- the header of translate-tsm.mjs:4-6 claims footnote markers are masked, but INLINE_RE does not mask them.

### `real-world-evidence/converter-fidelity-unchecked` — Corpus acceptance measures layout, not conversion fidelity; published examples contain conversion errors

- kind: issue · severity: medium · verdict: partly · plan: **P3-35**
- locations: `docs/real-world-report.md`; `examples/real-world/wiki-typesetting.tsm:60`; `examples/real-world/hott-introduction.tsm:101`; `examples/real-world/hott-introduction.tsm:161`

Acceptance is the e2e audit: no re-break, right edge within 1px. Committed and blog-published files contain:
- 8 wiki footnotes with their URL spilled out as text;
- a literal `<tab-pov>`;
- `\"o` and `\-` leaks;
- `⟨\name⟩` survivors;
- 58 '??' in example-hott.

Only translate-tsm validates structure. Cheap checks to add:
- a count of ref-unresolved diagnostics;
- no surviving `<label>`, `\` or `⟨\` in text nodes;
- printer round-trip (no-tsm-printer);
- a link-count parity check between source HTML and output.

*Verifier:* Two of the listed 'errors' are intentional and disclosed:
- The 58 '??' in example-hott are documented on the published page (zball-io/src/docs/example-hott.tsm:215) and in real-world-report.md:103-105.
- The `⟨\name⟩` survivors are made visible on purpose (the comment at tex2tsm.mjs:201 reads 'survivors = visible gaps').

Genuinely undetected errors, verified:
- `<tab-pov>` left as literal text (hott-introduction.tsm:161);
- 8 wiki footnotes with the URL spilled out as text;
- the `\"o` and `\-` leaks (hott-introduction.tsm:13 and :134);
- in the gitignored corpus, about 123 prose lines inside a ```cpp fence.

*Verifier notes:* The cheap checks proposed are right. Adding 'count of ref-unresolved diagnostics excluding a declared allow-list' handles the intentional '??'.

### `real-world-evidence/math-leniency-silent` — Converter-driven math leniencies hide malformed input without diagnostics

- kind: issue · severity: low · verdict: accurate · plan: **P3-24**
- locations: `engine/src/math/math.cc (Parser: Tok::Close / Tok::Slash cases)`; `tools/convert/pbr2tsm.mjs:356-369`

Commit 15bef3b made two changes:
- any group may close with any delimiter;
- a dangling `/` renders as an ordinary slash, with no info diagnostic.

Interval notation is legitimate, but truncated formulas from converters now pass silently, and pbr2tsm additionally balances delimiters itself (pbr2tsm.mjs:356-369). An info-level diagnostic for mismatched pairs or a dangling operator would keep the leniency without masking converter bugs.

*Verifier notes:* The leniency is documented as TeX-grade in real-world-report.md:80-83, with the diff in 15bef3b at math.cc Parser Tok::Close and Tok::Slash. An info-level diagnostic keeps the behaviour while surfacing truncated converter output. pbr2tsm's own delimiter balancing (pbr2tsm.mjs:356-369) shows the two layers already overlap.

### `real-world-evidence/converter-code-duplication` — Converter lineages copy entity tables, inline rules and figure regexes

- kind: issue · severity: low · verdict: accurate · plan: **P3-35**
- locations: `tools/convert/html2tsm.mjs:19-22`; `tools/convert/html2tsm.mjs:51-68`; `tools/convert/pbr2tsm.mjs:382-389`; `tools/convert/pbr2tsm.mjs:492-499`

pbr2tsm.mjs copies html2tsm.mjs's entities(), inline() and figure/fragment code, then fixes bugs only in the copy: depth-aware fragment scan, unclosed <li>. html2tsm is still referenced by the README and the report. The entity table is incomplete in both (683 `&thinsp;`, `&omega;` and others survive in pbr-en). A shared converter kit (real HTML entity decoder, escaping via the printer) removes the drift.

*Verifier notes:* The entity tables are confirmed to diverge: html2tsm.mjs:19-22 lacks the acute/grave/uml/hellip entries that pbr2tsm.mjs:382-389 has, and both lack &thinsp; and &dagger;. The depth-aware fragment scan exists only in pbr2tsm (pbr2tsm.mjs:445-488). Within pbr2tsm, captions (line 427) and body text (inline(), lines 492-499) are also divergent paths.

### `real-world-evidence/missed:0` — Runtime script errors have no per-block containment at all; one throw kills the document and the blog build

- kind: missed · severity: high · verdict: verifier-found · plan: **P0-05, P2-02**
- locations: `engine/src/codegen/codegen.cc:214-229`; `runtime/src/worker/executor.mjs:314-321`; `runtime/src/node/render.mjs:33`; `/home/dev/typeset/zball-io/eleventy.config.js:33`; `docs/design-decisions-v2.md:73`; `docs/design-decisions-v2.md:229`; `docs/document-model.md (diagnostics table: script-error 'per top-level block')`

codegen emits each top-level item as a bare `__emit(...)` statement inside one async module function, with no try/catch and no style-height restore. Any exception therefore aborts execute():
- an undefined variable in a splice;
- a throwing #let RHS;
- a region handler bug.

Verified: `Value: #(nope.x).` fails with 'nope is not defined' and no ops are produced. renderTsm rejects, and the blog's compile hook turns that into a build failure.

The documents promise the opposite. v2 §2 says 'execution is wrapped per top-level block; exceptions become error blocks'. v2 §11 says 'runtime script errors are caught per top-level block'. document-model's diagnostics table lists script-error 'per top-level block'. Only fence handlers are wrapped (executor.mjs:156-160).

This is the single largest robustness gap for real-world documents and for converters that emit JS-bearing region args by string concatenation.

*Proposed generalization (survey):* Have codegen wrap every top-level item as `__block(s, e, h0, async () => …)`.
- The runtime catches the exception, emits KIND.error with the item's span, and calls `$.style.popTo(h0)`, the same containment as v2 §12.
- Compile `#let` and `#{}` statements so that a SyntaxError is also local. Options: per-statement `new Function`/AsyncFunction units that share a scope object, or a codegen-time JS syntax pre-check that replaces the bad statement with an error-node emit.

This is block-granular containment as designed and keeps outputs deterministic.

### `real-world-evidence/missed:1` — Internal auto-labels share the user label namespace via string prefixes (h-, fn-, fnref-, bib-), and the shell keys on the same prefixes

- kind: missed · severity: medium · verdict: verifier-found · plan: **P0-09**
- locations: `engine/src/resolve/resolve.cc:149-153`; `engine/src/resolve/resolve.cc:211-212`; `engine/src/resolve/resolve.cc:376`; `engine/src/resolve/resolve.cc:263`; `engine/src/resolve/resolve.cc:421`; `runtime/src/main/shell.mjs:175`; `runtime/src/main/shell.mjs:162`

The resolver registers synthesized labels in the same `labels` map as user labels, with first-wins semantics:
- `h-<num>` for every heading;
- `fn-<n>` and `fnref-<n>` for notes.
It also emits `bib-<key>` and `#tsr-bib-…` anchors into the same DOM id space. The shell recognizes footnote markers by `href^="#tsr-fn-"`.

Verified: `= Intro <fn-1>` followed by `Claim^[A note.]` renders the footnote marker as superscript '§1', linking to the heading. The note's own label loses to the user label, and the only signal is a label-duplicate warning attributed to the note.

Citing a key that equals any label (including an auto `h-1`) is silently shadowed, because labels are tried before the bibliography (resolve.cc:274-276).

*Proposed generalization (survey):* Separate the namespaces.
- Internal anchors are derived from node identity and kind (e.g. `tsr-n<ordinal>`, or a reserved character that the label lexer rejects) and never enter the user label table.
- The label table is keyed by (namespace, label).
- User labels that match reserved patterns get a definition-site diagnostic.
- The shell consumes a ref/anchor record exported by the engine instead of string prefixes (subsumes the notes-popups item).

### `real-world-evidence/missed:2` — Label syntax is lexed per construct; inline-math labels are silently swallowed, paragraph/item labels stay literal

- kind: missed · severity: medium · verdict: verifier-found · plan: **P2-06**
- locations: `engine/src/inline/inline.cc:291-306`; `engine/src/codegen/codegen.cc:96-117`; `docs/design-decisions-v2.md:237`; `docs/document-model.md:63`; `engine/src/resolve/resolve.cc:130-229`

`<id>` attaches through a separate ad-hoc rule for each construct:
- the heading trailing form;
- a ` <id>` directly after a closing `$` (inline.cc:291-306);
- the region `label:` arg.

The math rule fires for inline math too. codegen keeps aux only for a paragraph that is exactly one display formula (codegen.cc:96-107), and emits `mathinline(src)` otherwise. Verified: `Inline $x^2$ <eq-in> here. See @eq-in.` gives AST label="eq-in", JS without it, and the ref renders '??' with no diagnostic at the definition.

A trailing `<id>` on a paragraph or list item stays literal text (verified), although v2 §11.1 says 'trailing <id> on block forms'.

On the resolver side, registration is again per kind. This is the root of HoTT's literal `<tab-pov>` and of theorem/aside refs printing '??'.

*Proposed generalization (survey):* Use one rule at the line-pass level: a trailing ` <id>` at the end of a block's last line labels that block, whatever its kind, and lowers uniformly to the `label` arg.
- Inline constructs that cannot carry a label produce a definition-site diagnostic rather than being consumed.
- The resolver registers any node that has a label arg through one path. The Entry carries kind/role and an optional number; display falls back to the excerpt.
- Numbering stays a separate concern (counters/RoleSpec).

### `real-world-evidence/missed:3` — Raw engine class bits are a public back door: $.style.push(number) and STYLE_PUSH bits are OR'd unmasked; the reader does not range-check ArgK

- kind: missed · severity: medium · verdict: verifier-found · plan: **P0-06, P2-08**
- locations: `runtime/src/worker/executor.mjs:274-281`; `engine/src/model/model.cc:16-17`; `engine/src/ops/ops.cc:66-90`; `engine/src/model/model.h:7-23`; `docs/document-model.md:70-83`

`$.style.push(x)` forwards a numeric x directly as STYLE_PUSH bits. instantiate() ORs `ArgK::bits` into Styling.bits without masking. User code can therefore forge internal classes: CJK, PUNCT_OPEN/CLOSE, INDENT, HYPHEN, CODE, LINK and SUP. Emit, break and the renderers assume the engine set these.

Verified: `$.style.push((1<<19)|(1<<6)|(1<<13))` renders a paragraph as `<sup>` with CODE+LINK classes. The tree dump does not even print bit 19.

The typed surface (`style({bold, italic, …})`) cannot express sup, while the untyped surface can express anything. JS bitwise operators also cap this at 32 bits, so the u64 'frozen' ABI is half reachable.

Separately, readArg accepts any varint as an ArgK, while opbuf.mjs rejects unknown names. The validation sits on the JS writer side, not on the fuzz-target reader that the contract (document-model §4.3) makes responsible.

*Proposed generalization (survey):* Make typed StyleDelta properties the only public style vocabulary: bold, italic, decorations, script, sizeMul, smallCaps, attach.
- The reader masks pushed and styled bits to a documented public subset and rejects or diagnoses out-of-range ArgK and Kind ids.
- Keep raw bits for internal use only (the resolver and emit compose them in C++).

### `real-world-evidence/missed:4` — The static/semantic page (what the blog ships at build time and in RSS) prints math as $…$ source

- kind: missed · severity: medium · verdict: verifier-found · plan: **P3-27**
- locations: `engine/src/render/semantic_html.cc:157`; `engine/src/render/semantic_html.cc:263`; `runtime/src/node/render.mjs:46`; `/home/dev/typeset/zball-io/_site/feed.xml`; `docs/architecture.md (M7: 'MathBox layout in su, zero measurement')`

renderTsm's semantic serializer emits `<code class="tsr-mathsrc">$x^2$</code>` for every formula (verified: `$NN$` comes out as the literal source).

The blog's no-JS page, its pre-hydration flash, and feed.xml (4 occurrences) therefore show raw math syntax.

The engine already has a measurement-free MathBox layout from precompiled metrics. Only text-font runs need host measurement. So the static path discards a capability that is deterministic and target-neutral.

It is a per-serializer special case: math is the one content kind whose fallback is its source text.

*Proposed generalization (survey):* Lay out math once and serialize it per target.
- Typeset HTML: positioned boxes, as now.
- Semantic and static output: MathML Core generated from the MathBox/MNode tree. The quality objection in math-design §8 applies to the typeset path, not to the fallback and accessibility path.
- Alternatively, positioned boxes with the precompiled metrics, plus an aria-label carrying the source.

This keeps the dual-target rule (no browser assumption) and gives feed readers real math.

### `real-world-evidence/missed:5` — Semantic figure serializer guesses structure from the first-paragraph-is-caption convention: captions concatenate, nested blocks land inside <figcaption>

- kind: missed · severity: low · verdict: verifier-found · plan: **P3-23**
- locations: `engine/src/render/semantic_html.cc:284-306`; `engine/src/resolve/resolve.cc:171-180`; `engine/src/emit/emit.cc:471-476`

Because a figure is just group{role:'figure'} with no caption/body slots, the semantic serializer works like this:
- it opens `<figcaption>` at the first para and never closes it until the end;
- it inlines every later para into it with no separator;
- it serializes every later non-para block (lists, code, nested figures) inside the figcaption.

Verified:
- `#!figure` with two paras plus a list gives `<figcaption><strong>图 1：</strong>First caption para.Second para.<ul>…</ul></figcaption>`;
- a nested figure is emitted inside its parent's figcaption.

The resolver prefixes only the first para, and emit treats every para under a figure as a centred caption (figDepth > 0). Three stages each guess the same implicit structure differently.

*Proposed generalization (survey):* Give role groups explicit slots: `group{role, label}` with children tagged caption or body. Region builders and user constructors set the slots, and the default figure builder puts the region's paragraph text in caption and media/blocks in body.

The resolver prefixes the caption slot; emit styles the caption slot; the semantic serializer maps slots to figcaption/body. Subfigures and table/listing captions then follow without guessing (this is the slot part of the report's RoleSpec).
