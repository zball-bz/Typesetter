# Design Decisions v2

Status: **draft for review**. Supersedes [design-decisions.md](design-decisions.md) (v1) once accepted; v1 is kept as historical record. This revision consolidates the 2026-07 architecture review: the scripting pivot (Lua → compiled standard JS), the measurement/rendering robustness contract, concrete syntax design (splice delimiting, block structure, structured regions, comments), the style stack, native-fallback rendering, and scope clarifications.

Convention: every section states the **decision** and the **why**. Remaining open questions are collected in §17.

---

## 0. Motivation and scope

**Goal: a Typst-grade typesetting experience running natively in the browser, embeddable in ordinary web page flow (box-model based).**

Consequences that drive everything below:

- **Runtime engine, not a static generator.** Web fonts are not guaranteed; the actually-rendered font can differ per machine (system font stacks, fallbacks). Browser measurement is therefore the ground truth, and typesetting must run in the reader's browser in real time.
- **Static export is a derived capability**: force web fonts → metrics become deterministic → pre-render with the same engine at export time.
- **Trust model**: all rendered content is trusted (single-author publishing). Document scripts get full JS power; preventing arbitrary code execution is the caller's concern, not the engine's.
- **Size budget**: no hard constraint. A few MB of cacheable WASM + data is acceptable.
- **Expected hot path** is rendering source into the block stream (parse, evaluation, emission), not the breaker itself (PoC: sub-ms per paragraph).

## 1. Architecture overview

C++/Emscripten engine + minimal JS shell. **Why C++**: author preference for library development in C++ over TS (recorded as the honest rationale — the performance argument alone would not justify it), plus the hot path (parse/codegen/emission/breaking) stays in one native codebase.

Pipeline (revised from v1 — the evaluator has moved out of C++, see §2):

```
markup source
 → C++ line-structure pass                 → block skeleton (containers, regions, verbatim islands)
 → C++ PEG inline pass + splice lexer      → AST
 → C++ codegen                             → one JS program + source map
 → JS execution (trusted)                  → content ops (flat buffer)
 → C++ content tree reconstruction
 → C++ resolver pass (counters, labels, refs, collectors)
 → C++ emission (script segmentation, hyphenation, block emission)
 → measurement (JS shim, batched)          → widths + per-style vertical metrics
 → C++ Knuth–Plass breaker                 → line results
 → C++ block layout                        → absolute line/block positions
 → C++ HTML generation                     → JS shell injects / progressively upgrades
```

**Execution home: a Worker.** The engine WASM, the measurement shim (OffscreenCanvas), and document-script execution all run off the main thread. The main thread keeps DOM injection/upgrade, the clipboard handler (§8), and resize/dppx observation (forwarded as events). Document scripts having no DOM access is a guarantee, not a limitation: constructors only write the op buffer (§2), so the model is worker-safe by construction; `#use` imports resolve inside the worker.

## 2. Scripting: standard JavaScript, whole-document compilation

**Decision: the document language's code mode is standard, unmodified JavaScript. Lua (and the v1 "modified Lua") is dropped entirely.**

Why: v1's modified-Lua plan had already replaced Lua's entire surface syntax; what remained was Lua's semantics and VM — exactly the parts that leak into the user language (indexing base, nil handling) and cannot be hidden by a transpiler. The "small hackable codebase" rationale only holds when adopting Lua wholesale. Standard JS costs zero embedded VM (the host engine is right there), zero grammar maintenance, and full tooling (highlighting, formatter, author familiarity). For the same reason, "slightly modified JS" is rejected: one modification forfeits the entire toolchain. All customization budget is spent at the markup/splice layer.

**Compilation model (MDX-style): the C++ parser compiles the whole document into a single JS function.** Markup becomes content-constructor calls; code segments are spliced verbatim; content blocks compile to content-value expressions.

Source:

```
#let avg = (a,b) => (a+b)/2
均值是 #avg(3,5)，*就地*计算。
```

Generated:

```js
async ({para, text, em, val, m}, $) => {
  let avg = (a,b) => (a+b)/2;
  para(text("均值是 "), val(avg(3,5)), text("，"), em(text("就地")), text("计算。"));
}
```

This one model settles:

- **Context injection**: the entire API surface is one generated destructured parameter (constructors + the `m` tag), plus `$` for document state (counters, labels, refs, the style stack §12). No globals, no `with`, no proxies, no prefixes in user code.
- **Scope**: `#let` compiles to `let`. The whole document shares one lexical scope; definitions flow top-to-bottom by plain JS scoping. No state-threading machinery.
- **Content is first-class**: `[...]` compiles to an expression producing a content value — storable, passable, composable. The v1 attachment/arity ambiguity (`#if c [a] [b]`) dissolves because JS syntax carries its own structure.
- **Errors**: execution is wrapped per top-level block; exceptions become error blocks in the document model (§11), with source positions via the codegen's JS-offset ↔ source-offset map.
- **Libraries**: `#use("./helpers.js")` hoists to a top-level `await import()`; the document function is async.
- **Dynamic markup in deep code**: tagged template `` m`*bold* ${x}` `` — standard JS; the tag re-enters the WASM parser at runtime (also exposed as `m.parse(string)` for fence handlers, §4).

**Execution boundary**: constructors are thin JS wrappers that append opcodes to a flat op buffer (typed arrays + string table). The buffer crosses into WASM once per document and is reconstructed into the content tree in C++. (Rejected alternative: constructors as direct WASM exports — workable but chattier; the buffer keeps the one-crossing-per-stage discipline.)

## 3. Splice grammar (`#`)

Bare splices allow exactly one "head chain"; everything else is explicitly parenthesized. Greedy, but only along **directly adjacent** continuations. Full rules in Appendix A; the five load-bearing choices:

1. **At splice level, `[...]` is a content argument, not JS indexing** (`#em[强调]` is the high-frequency form; indexing needs `#(arr[0])`). Inside `(...)`, `[...]` is plain JS again. Content literals exist only at splice-controlled positions; deep code uses `` m`…` ``. Two layers, each internally pure.
2. **Bare-splice identifiers are ASCII-only.** JS identifiers legally include CJK (ID_Continue), so `#avg的结果` would swallow `avg的结果` under real JS rules. The ASCII restriction cuts cleanly at `avg`; CJK-named bindings use `#(变量名)`.
3. **`.` continues only when followed by an identifier start** — `值是 #x. 下一句` keeps the period in prose; `#obj.field` chains.
4. **`#let`'s right-hand side terminates at a newline at bracket depth 0** (or `;`). Multi-line expressions must parenthesize; multi-statement definitions use `#{ … }`, whose braces are stripped so inner `let` lands in document scope.
5. **Regex literals are forbidden inside splices** (use `new RegExp`) — removes the one classic JS lexing ambiguity (`/` division vs regex), so the splice lexer is a pure state machine (strings, templates, comments, bracket balance).

Keyword forms are a closed set: `#let`, `#if (…) […] else […]` (else-chains; bare `else` continues only when followed by `[` or `if (`), `#for (const x of xs) […]` (loop variables bind into the content block — the reason keyword forms cannot be replaced by helper functions), `#use`. Everything else: `#(expr)` / `#{…}`.

## 4. Block structure: two-phase parsing

**Decision: block structure does not enter the PEG.** Phase 1 is a line-oriented, prefix-driven automaton (container stack, hand-written C++); it produces the block skeleton and carves out verbatim islands. Phase 2 (PackCC PEG) parses only inline markup + splices within blocks.

Why: (a) the PEG never touches context-sensitive indentation; (b) error recovery is naturally block-granular (a failed block becomes an error block, §11); (c) the line pass and the splice codegen emit into the same compiled JS program — `- a` and `#list(…)` produce identical ops.

Precision on the v1 "indentation is bad" judgment: what is bad is **scope-by-indentation inside inline code** (context-sensitive, entangled with prose line breaks). **Line-prefix indentation for block structure** is a different mechanism: resolved in a physical-line pre-pass, never interacting with inline grammar, and matching universal Markdown muscle memory. Lists nest by it.

Line grammar and nesting rules: Appendix B. Highlights:

- `=` headings (`#` is taken by code — the same reason Typst chose `=`).
- `-` unordered, `+` auto-numbered, `N.` explicit-numbered list items.
- **No lazy continuation** (CommonMark's largest single source of parsing insanity): continuation lines must be indented to the item's content column. Deterministic parsing, better errors.
- **Content blocks containing block-level markup**: a multi-line `[...]` is dedented (common leading indentation stripped), then run through the same line pass. This is how `#for (…) [- #x]` yields list items.
- **Headings are flat markers; the section tree is derived in the document model** (numbering, TOC, PDF outline). No syntactic section wrapping — moving a section while writing must not require rebalancing anything.

**Governing principle (first line of the language spec): every syntax form is sugar for a constructor.** `= 标题` ⇔ `heading(1)[…]`; `- a` ⇔ `list(item(…))`. Sugar is granted only to the highest-frequency forms; everything else lives in function space. This keeps the line pass, the PEG, and splices convergent on one op stream, and guarantees every syntactic capability has a programmable equivalent.

### 4.1 Structured block regions: `#!name(args) … #name!`

Block-level constructs with explicit, name-matched open/close markers (line-start, standing alone on their lines; nestable):

```
#!table(cols: 3)
姓名 | 年龄 | #avg(3,5)
李四 | 30   | 42
#table!
```

**Decision: the region interior is always standard markup** — full line pass (nested lists, nested regions) + inline + splices. The region node additionally preserves **per-source-line provenance**, so constructors impose row/cell structure at the content-tree level (the table constructor: one row per source line, cells split on literal `|` text nodes).

Why markup-interior rather than custom parse-time parsers:

- **The registration paradox**: whether an interior is raw must be known at parse time, but constructors are defined in JS, which runs *after* parse. Parse-time custom parsers would break the pipeline's stage separation (or force an ugly syntactic raw-marker).
- **Tree-level splitting is embedding-proof**: by interpretation time, splices are value nodes and code spans are code nodes — `#f("a|b")` and `` `a|b` `` can never be mis-split on `|`, with zero masking machinery. Raw-text custom parsers would need engine-provided splice masking to survive the same inputs. Scripting inside cells works for free.
- Unclosed / mismatched regions become error blocks spanning to the matching close or EOF — containment for free (§7, §11).

**True raw DSLs use the mechanism that already exists: fenced code blocks with a language tag.** Fences are verbatim islands regardless of tag (no parse-time knowledge needed); at runtime, unknown tags dispatch to registered JS handlers — *this* is where custom parsers live. Handlers receive the raw text + source offset and may re-enter the engine's parser via `m.parse()` for markup fragments; parameters travel in the fence info string or a wrapping region. Known cost, accepted: no syntax highlighting/formatting inside custom DSL fences, and in-region error positions are only as good as the handler's offset reporting.

**Fence handler contract** (resolved). Registration is document-order: `#use`d modules export `fences = { tag: handler }` (auto-registered after import) or register explicitly via `#{ $.fence(tag, fn) }`; registration must precede use (preamble feel). Codegen stays registration-agnostic — every fence compiles to a dispatcher call; an unknown tag at runtime falls back to a plain code block. The info string reuses the splice `(…)` argument lexer: ` ```graphviz(rankdir: "LR") `. Handler signature:

```js
handler(body, ctx) → Content | Promise<Content>
ctx = {
  args,                      // parsed info-string arguments
  offset,                    // absolute source offset of body start
  m,                         // m`…` and m.parse(str, {offset}) re-entry
  error(msg, localOffset),   // diagnostic anchored at offset+localOffset
  raw(html, {width, height}) // pre-rendered passthrough node
}
```

The offset contract is two-tier: when a handler parses a **substring** of the body it passes the offset and keeps full anchor/copy/error fidelity; transformed text passes none and anchors degrade to fence granularity. A thrown handler exception turns the fence into an error block (§7 containment). Handlers may be async (the document function already is); a slow handler slows the document — accepted under the trust model. `raw` nodes must declare a size (or be block-level auto-height) to participate in layout.

**Region provenance** (resolved). The region node records, for each direct child, its source-line span and the top-level segmentation of its inline runs; constructors read a small query API over this. `|`-splitting is the **table constructor's** convention, not a region mechanism — other constructs may pick other conventions. Cell-level block content reuses the content-column continuation rule: a line indented past the table's content column continues the previous row, attaching block content to that row's **last** cell (documented limitation); heavier cells use the escape hatches that already exist via nesting (`#cell[…]` or a nested `#!cell` region). `\|` escapes a literal pipe; `||` is an empty cell; a trailing `|` is optional. There is **no Markdown alignment-row micro-syntax**: column count and alignment are opener arguments (`#!table(cols: 3, align: "llr")`) — one less grammar, and alignment is a table-level property, not row content.

### 4.2 Comments: `%-- … --%`

- Distinct open/close delimiters — chosen over Obsidian's symmetric `%% %%` precisely because distinct delimiters make **nesting** unambiguous (commenting out a block that already contains comments just works).
- Inline or multi-line. Lexically dumb, like C block comments: no awareness of interior structure (`--%` inside a commented string still closes; accepted).
- Precedence: verbatim islands > comments > splices/markup. `%--` inside a code span is literal; a comment can comment out any markup, including splices.
- **Comments are document-model nodes**, not lexer discards: preserved in the AST and content tree, queryable, displayable by tooling (editor margins, review modes). Excluded from typeset output and from copy text.
- Escape: `\%--` for a literal `%--`; a lone `%` is always literal.

## 5. Inline markup

- **Emphasis rejects CommonMark flanking rules.** Left/right-flanking depends on spaces and punctuation classes and is a known disaster for spaceless CJK text (`**《书名》**` is the canonical failure). Rule here: strict pairing — `*` opens when not followed by whitespace, closes when not preceded by whitespace; CJK punctuation plays no role; `\*` escapes. A CJK-first syntax must part ways with Markdown exactly here.
- **Verbatim islands are carved out before everything else**: inline `` `code` ``, fenced blocks, `$…$` math. Inside them, `#`, `*`, and `%--` are literal.
- **Links**: `[text](url)` sugar is recognized only on `](` adjacency; otherwise `[` is a literal character in prose (it is only meaningful on a `#…` chain). Bare URLs autolink.

## 6. Measurement protocol

Per-paragraph batching as in v1, with these amendments:

- **The reply carries vertical metrics.** Per unique style tuple (family, size, weight, italic): `ascent`/`descent` (`TextMetrics.fontBoundingBoxAscent/Descent`; per-style, not per-string). Per unique (string × style): `width`. Vertical metrics feed **block layout** (leading, baselines — §10), not the breaker; their only leak into breaking is line-index-based width functions (§10).
- **Cache ownership**: the JS shim owns the measurement cache, keyed by (string, style, dppx). It survives across paragraphs and documents.
- **Invalidation**: a `devicePixelRatio` change (browser zoom, window moved across monitors) flushes the cache and re-typesets — one-shot `matchMedia('(resolution: …dppx)')` listeners, re-armed on each fire.
- **Font lifecycle**: no hard gate on `document.fonts.ready` — the native-fallback state machine (§9) renders before fonts/measurements settle and upgrades after.
- **Batch granularity is upgradeable**: per-paragraph for the canvas backend; if a backend forces reflow (DOM measurement), a per-document two-pass (collect all tuples → one measure → run all breakers) avoids layout thrash.
- **Backends are pluggable behind the same protocol**:
  1. Canvas `measureText` (default, via OffscreenCanvas in the worker; set `ctx.textRendering = 'geometricPrecision'` where supported).
  2. Batched DOM measurement (hidden container, one reflow, float rects; main-thread assisted) — same-medium as rendering, absorbs engine/zoom quirks entirely.
  3. **Precompiled font-file metrics for bundled fonts** — exact without any browser call; this is what makes math exact from t=0 (§9, §13).

## 7. Measurement–render robustness contract (the fractional-DPR problem)

Background (PoC failure): under fractional device scaling (e.g. 1.25×), DOM advances and canvas advances diverge by sub-pixel per-glyph amounts that accumulate over a line; the PoC renderer let the browser re-wrap (`white-space: normal` + `text-align-last: justify`), amplifying a 0.1px overflow into a spurious wrapped-and-stretched line.

**Contract: the two measurement paths are never assumed equal; error must only ever appear as ≤1px right-edge deviation, never as a structural re-break.**

Rules (implementation checklist in Appendix D):

1. Line boxes are `white-space: nowrap` — the browser has no re-break authority, ever.
2. All widths fed to the breaker and to spacing computation carry a **safety margin ε** (systematic slight over-estimation of word widths, or `measure − ε`); rendered lines may come out short by a hair, never long.
3. Container widths are read unrounded (`getBoundingClientRect`, never integer `clientWidth`).
4. Justification is applied as explicit computed spacing (§8), not browser `text-align: justify` — the browser's own distribution rules differ from the cost model's assumptions.
5. **No runtime repair pass.** Containment is structural and recursive: `nowrap` confines error to the line, atomic swaps (§9) to the paragraph, error blocks (§11) to the block. A dev-mode audit (§16) measures rendered deviation but never mutates the result.
6. `text-rendering: geometricPrecision` on typeset output.

## 8. Rendering contract: line-level takeover

**The takeover altitude is the line — not the word, not the glyph.** Above the line (break decisions, per-line y/left/width, per-line spacing values): engine. Inside the line (shaping, kerning, ligatures, font fallback, painting, selection, caret, IME): browser — line content is real text nodes.

- A line = absolutely positioned element (engine-owned `top`, plus `left`/`width` for shaped containers), `white-space: nowrap`, containing real text with real spaces, span tree per style run (v1 subset/superset algorithm).
- Justification: per-line `word-spacing` (Latin gaps) + per-run `letter-spacing` (CJK runs); punctuation-compression widths absorbed into adjacent spans' padding. A uniform per-gap Δ is the direct translation of the K-P uniform glue ratio.
- **Mixed-line distribution rule**: CJK inter-character gaps receive `Δcjk = k × Δword`, where `Δword` is the Latin word-gap adjustment and `k` is a configurable constant applied to the **absolute (px) adjustment** (not rescaled per font size). The breaker's stretchability accounting uses the same weights (`n_latin + k·n_cjk`), so the cost model and the renderer agree by construction.
- **DOM weight is O(lines + style runs)** — lighter than the PoC's span-per-word. Static after typesetting: no canvas repaint loop, no scroll listeners.
- **Absolute positioning must never descend to word level** (v1's "absolute positioning for all content placement" is hereby scoped to line/block granularity). Word-level is strictly worse: O(words) nodes, and inter-word spaces stop existing as characters, destroying copy.
- Copy fidelity: a `copy` listener on the container rebuilds clean **content text** from the run structure (synthetic runs — hyphens, resolved refs — are marked and skipped; per-line join rules handle consumed spaces; normative algorithm in the document-model spec §9.3). Source offsets (`data-s`/`data-e`) drive anchoring and diagnostics, not copy. Required regardless of renderer once hyphenation inserts glyphs.
- Accepted tradeoff: find-in-page phrase matching across line boundaries degrades (inherent to owned breaks; shared by every web K-P implementation). Optional screen-reader mitigation: `aria-hidden` lines + `sr-only` clean paragraph text.
- Perf hygiene: `contain: layout style paint` on lines; `content-visibility: auto` on paragraphs.

## 9. Native fallback and progressive upgrade

**Two-phase rendering: serve the browser's native layout immediately; upgrade to engine layout when measurements settle.**

- The content tree renders to **semantic flow HTML** first (browser breaks lines itself). This serializer is one component with four uses: first paint, SEO/crawlers, no-JS readers, and the base of static export.
- Block measurement states: `exact` (bundled-font precompiled metrics, or already measured) | `pending(estimate)` | `invalid` (dppx changed). The breaker may run on estimates; upgrades re-typeset affected paragraphs only.
- **Math is exact from the start**: its font is bundled (§13), so its metrics are precompiled — formulas never render natively (MathML output rejected on quality) and never wait for `measureText`.
- **Upgrades swap atomically per paragraph**, localizing height changes; native scroll anchoring absorbs most above-viewport shifts. The upgrade callback carries `(paragraph id, old rect, new rect, source-offset ↔ line map)`; **scroll anchoring is the caller's responsibility — the engine provides the information.** (Settled stance on incremental relayout: the algorithm targets static typesetting; a resize is a full re-typeset, not an incremental system.)
- Optional polish: `size-adjust`/`ascent-override` descriptors on fallback fonts to minimize the visual jump at upgrade time.

## 10. Block layout

- The engine owns all vertical placement. The leading model is computed from per-style vertical metrics (§6); CJK requires visually consistent leading across mixed-script lines (clreq).
- Shaped containers and drop caps use **line-index-based width functions** (TeX `\parshape` style). This is a recorded, intentional approximation: with varying line heights (inline math), "line index ≈ vertical position" is not exact.
- **The page model is retained for PDF export.** The web target is a single infinite page. (Exotic flowed layouts — n×m matrix flow — become possible with a page model but are non-goals.)

## 11. Document model — the central deliverable

The public API is designed on top of a full document model, specified before implementation:

- **Content tree**: post-execution, styled content (paragraphs, headings, lists, regions, math, comment nodes, error blocks; tables via regions).
- **Block stream**: per-paragraph linebreak blocks (v1 struct carried forward).
- **Layout result**: per-paragraph line list (break indices, per-line spacing values, positions).
- **Anchor system**: stable ids linking source offsets ↔ content nodes ↔ DOM elements (drives copy, upgrade callbacks, and caller-side scroll anchoring).
- **Comment nodes** (§4.2): queryable and tool-displayable; excluded from typeset output and copy.
- **Error blocks**: parse errors recover to the enclosing block boundary; runtime script errors are caught per top-level block. Both embed as renderable error blocks carrying source spans. PEG's silent backtracking is bounded by block-granular recovery. Error recovery also restores the style stack to the block's entry height (§12).
- **Serializers**: semantic flow HTML (§9) and typeset HTML (§8).
- **Diagnostics channel**: non-fatal findings (unresolved refs, duplicate labels, handler reports) collected alongside error blocks, queryable from the handle.
- The API surface (`typeset(source, container, opts) → handle { relayout, events, destroy }`) is designed on this model — deliberately after the model, not before.

### 11.1 References, terms, citations

**Execution declares; the resolver decides.** References are write-only during script execution: `LABEL` / `TERM` / `REF` / `COLLECT` ops only declare. The resolver pass (between content-tree reconstruction and emission) walks the tree in document order: runs the counter automata (sections from the derived section tree; figures, equations, footnotes), builds the label table, resolves `REF` placeholders type-aware (heading → "§2.1", figure → "图 3", equation → "(5)"; supplement strings configurable), and expands collectors.

- **No LaTeX two-run, no Typst fixpoint**: the web target has no layout-dependent references. PDF page-number references, if ever wanted, are one bounded extra layout iteration at export time.
- **Scripts cannot read resolved values** — `$.ref(…)` returns opaque placeholder content, never numbers. This keeps single-pass execution sound; computed numbering is done in user JS with user counters.
- **Syntax**: trailing `<id>` on block forms (`= 引言 <sec-intro>`, `$…$ <eq1>`); regions take `label:` arguments. Reference sugar `@id` — a `@` preceded by an identifier character is literal, so `user@domain` is safe; ASCII ids use the bare form, CJK labels use the bracketed form `@[排版]` (CJK has no whitespace to terminate a bare form — the same decision as the splice ASCII rule, reused). `\@` escapes. General form: `#ref("id", …)`.
- **Collectors unify TOC, glossary, list-of-figures, bibliography**: `COLLECT` nodes expand from resolver tables. `#term[名字][描述]` is a definition site with an auto-label (the `/ term:` line sugar stays dropped — this is a reference-shaped feature, not a list-shaped one); `#glossary()` is the collector over the term table.
- **Citations stay thin**: `#bibliography("refs.json")` loads data; `@key` shares the reference namespace; rendering style is a JS function (numeric first). CSL complexity is deliberately left to userland.
- **Failure modes are diagnostics, not hard errors**: an unresolved ref renders a "??" node; duplicate labels report through the diagnostics channel. The resolver runs before any rendering, so native-fallback HTML already carries final numbers (no "??" flash); labels become DOM anchors and refs render as real links in both serializers.

## 12. Style system: the style stack

**Decision: styling is a stack with save/restore semantics** (TeX grouping, not Typst set/show rules).

- Op-buffer level: `STYLE_PUSH(delta)` and `STYLE_POP_TO(height)`. Script level: `$.style.push({…})`, `$.style.height`, `$.style.popTo(h)` — save the height at any point, pop back to it later. Markup sugar (`*…*`, `_…_`) compiles to balanced push/pop pairs around its content ops.
- Effective style at any op = fold of the stack: class bitsets OR together; inline properties override nearest-wins. This maps directly onto the v1 `TextStyling` bitset + inline-style model (§15).
- **Binding time is emission-time (dynamic)**: a content value stored in a variable takes the styles active **where it is spliced/emitted**, not where it was constructed. The same value rendered in two places may look different — recorded as a deliberate feature (TeX-macro semantics), the natural consequence of the stack model.
- Containment: every block boundary snapshots the stack height; error recovery and block exit pop to the entry height, so style leaks are bounded by the same containment structure as errors (§7, §11).
- Typst-style set/show-rule sugar, if ever wanted, can be layered on this primitive later; the primitive itself is the commitment.

## 13. Math

- **Font: Neo Euler** (OFL revival of AMS Euler with an OpenType MATH table). v1's plan to extract metrics from original AMS Euler is void — it predates the MATH table, and math layout needs MATH-table parameters (italic correction, script sizes, radical/fraction geometry), not just advances.
- **Own box model, rendered as positioned inline boxes** integrating with the line model via width/height/baseline. MathML output rejected on rendering quality.
- Syntax: v1's design carries forward (calculator-style infix, `/` fractions, `^`/`_` scripts, three-tier symbol shorthands, `!` negation) with one fix: **greedy big operators bind rightward until a relation-class operator, a closing bracket, or end of expression** — v1's rule as stated contradicted its own example (`$sum_(i=0)^n i/n ~> …$`). "Relation-class" refers to the operator dictionary's Rel atom class (below).
- **Operator dictionary (decided)**: two layers, compiled at build time (same pattern as hyphenation patterns), filtered against Neo Euler glyph coverage with fallback marking for uncovered symbols.
  1. *Names/codepoints*: hierarchical dotted names following Typst's codex conventions (`arrow.r`, `subset.eq` — readable, battle-tested), with v1's tier-1 ASCII sequences and the `AA`..`ZZ` blackboard-bold rule layered on top.
  2. *Layout properties*: TeX atom classes (Ord/Op/Bin/Rel/Open/Close/Punct/Inner) plus stretchy/large-op flags, derived by mapping the W3C MathML Core operator dictionary.
- Precedence exists for display purposes; semantic evaluation of formulas is out of scope.
- Metrics are precompiled from the bundled font (§6, §9).

## 14. CJK

- Scope: horizontal writing, Simplified Chinese first. No vertical writing, no RTL.
- **Normative reference: [W3C clreq](https://www.w3.org/TR/clreq/)** (Requirements for Chinese Text Layout).
- Core rules: line-break prohibitions (禁则) + punctuation compression (标点挤压). Mixed CJK–Latin boundary spacing (0.25em, breakable, stretchable).
- Justification stance: every CJK char is its own block with breakable, slightly stretchable inter-character glue — pure-CJK lines are therefore near-flush naturally, and the same per-char glue is the justification mechanism when full justification is required. Distribution on mixed lines follows the `k × Δword` rule (§8).
- The v1/PoC block-emission rules carry forward as Appendix C.
- No italic for CJK; emphasis via `text-emphasis` dots.

## 15. Carried forward from v1 (unchanged)

- **Type system**: `TextStyling` with bitset base classes (`uint64_t`+ in C++), dynamic class ids ≥ 256, inline-style escape hatch with `CLS_HAS_STYLE`; span tree builder via subset/superset nesting. (Now driven by the style stack, §12.)
- **Hyphenation**: owned C++ Liang implementation; TeX patterns compiled to a compact runtime format (format decided at implementation time); English first, pluggable language packs.
- **Parser tooling**: PackCC PEG — with reduced scope (inline + splice lexing + math; block structure is the hand-written line pass, §4). UTF-8/CJK classification via C semantic predicates, not grammar rules.
- **Build**: CMake + emcc; PackCC at build time. Lua removed from the build.

## 16. Testing strategy

Every stage before rendering is deterministic and tested by golden files; rendering is tested by screenshots plus invariant audits.

- **Deterministic stages** (Node-compatible, mock measurer; PoC bench harness as seed): parse → AST snapshots; codegen → generated-JS snapshots; execution → op-buffer snapshots; emission → block-stream goldens; breaker → breakpoint/cost goldens.
- **Render stage**: screenshot regression, plus DOM invariant audits:
  - every line element renders exactly one line box (no premature browser break — the failure mode §7 exists to prevent);
  - rendered right-edge deviation ≤ ε;
  - upgrade swaps preserve anchor identity.
- The invariant audit is the same code as the §7 dev-mode audit — one implementation, used as a test assertion in CI and as a diagnostic in development. (Consistent with the v1 goal: every pipeline stage inspectable and debuggable.)

## 17. Open items

The previous three items are resolved and folded in: reference/glossary system → §11.1; fence handler contract and region provenance → §4.1. Remaining deferrals (recorded, not blocking):

- Citation styles beyond numeric — userland JS by design; revisit only if a stock style library is ever wanted.
- PDF page-number references — bounded extra layout iteration at export; design when the PDF target lands.

## 18. Migration and sequencing

1. **This document** (overall design) — review and accept.
2. **Document model spec** (§11, including style stack §12) — the next design artifact.
3. **Vertical slice**: markup subset → line pass → codegen → JS execution → block stream → measurement boundary → breaker → line-level renderer, end-to-end through WASM, inside the worker. De-risks the boundary and the measurement protocol first (the two real unknowns; the breaker is PoC-proven).
4. Widen: CJK emission, native fallback, hyphenation port, regions/fence handlers, math.

The TS PoC in `src/` is frozen as reference. The grammar can mature in parallel with the slice because code mode is lexically delimited (splices are opaque spans until codegen).

---

## Appendix A: `#` splice delimiting rules

```
#name                    value splice
#name.b(…)[…][…]         call-chain splice; continuations only when DIRECTLY adjacent:
                           .ident        ('.' must be followed by an identifier start)
                           (…)           (standard JS inside, lexical balanced scan)
                           […]           (content argument; repeatable; desugars to trailing args)
#(expr)                  arbitrary expression splice (100% standard JS inside)
#{ statements }          statement splice (braces stripped; 'let' lands in document scope)
#let name = expr         binding; RHS ends at a newline at bracket depth 0, or ';'
#if (c) […] else […]     keyword form; 'else' continues only before '[' or 'if ('
#for (const x of xs) […] keyword form; loop vars bind inside the content block
#use("./mod.js")         hoisted to top-level await import(); document fn is async
#x;                      ';' hard-terminates a splice (blocks accidental […] attachment)
\#                       literal '#'
```

Additional rules:

- Bare-splice identifiers are ASCII (`[A-Za-z_$][A-Za-z0-9_$]*`); CJK-named bindings via `#(…)`.
- Regex literals are forbidden in splices; use `new RegExp`.
- At splice level, `[…]` is always a content argument (indexing: `#(arr[0])`); inside `(…)` it is plain JS.
- Content literals appear only at splice-controlled positions (`#f[…]`, `#let x = […]`); in deep code, construct content with the tagged template `` m`…` ``.

## Appendix B: Line-structure grammar

````
= Heading            level by marker count (=, ==, ===)
- item               unordered list item
+ item               ordered item, auto-numbered
3. item              ordered item, explicit start number
> quote              blockquote container (stackable)
#!name(args)         structured region opener (own line; markup interior,
#name!                 line provenance kept; name-matched closer, own line)
%-- … --%            comment (nestable; preserved as a document-model node)
```lang              fenced code block (verbatim island; unknown lang tags
                       dispatch to JS fence handlers = custom DSL parsers)
$ … $                display math (verbatim island)
---                  thematic break (alone on line)
(blank line)         paragraph separator
````

Nesting rules:

1. A list marker establishes its **content column** (one column after the marker). Subsequent lines indented ≥ the content column belong to the item (multiple paragraphs, sub-lists).
2. A new marker at a deeper column opens a nested list; at a shallower column, pop the container stack.
3. **No lazy continuation**: continuation lines must reach the content column. (Deliberate CommonMark departure — determinism and error quality over permissiveness.)
4. A multi-line content block `[…]` is dedented (common leading indentation stripped) and re-enters this same line pass.
5. Regions nest; a region closer must match the innermost open region or the whole region becomes an error block (resync at the matching closer or EOF).
6. Escapes: `\=`, `\-`, `\+`, `\>`, `\%--` etc. at the relevant positions produce literal characters.

## Appendix C: CJK block emission rules (from PoC; normative reference: clreq)

```
Phoneme                  breakPenalty=INF, width=measured, breakWidth=0, spaceWidth=0
Hyphen point             breakPenalty>0, width=0, breakWidth=width('-'), spaceWidth=0
Existing hyphen break    breakPenalty=0, width=0, breakWidth=0   (e.g. after "bit-" in "bit-wise")
Space                    breakPenalty=0, width=spaceWidth=width(' ')
CJK char                 breakPenalty=0, width=1em, spaceWidth=0.1em (stretchable inter-char glue)
Opening punct            [breakable 0.5em space, sw=0] + [unbreakable 0.5em glyph]
Closing punct            [unbreakable 0.5em glyph] + [breakable 0.5em space, sw=0]
Consecutive punct        compressed (inter-punct spaces removed, no break between)
Em-dash pair ——          unbreakable, width=2em
Ellipsis pair ……         unbreakable, width=2em
CJK–Latin boundary       breakable, width=0.25em, sw=0.25em (stretchable)
Paragraph indent         2 × 1em unbreakable blocks (not CSS padding)
```

Notes:

- Punctuation compressible spaces are NOT stretchable.
- Line-start/-end compression falls out of the breakable half-width spaces vanishing at line edges.
- No CJK italic; emphasis via `text-emphasis: filled dot`.
- Mixed-line justification distribution: `Δcjk = k × Δword` on absolute values (§8).

## Appendix D: DPR robustness checklist (implementation)

- [ ] `.line { white-space: nowrap }` — no browser re-breaking, ever.
- [ ] ε safety margin on measured widths (systematic over-estimation ~0.02px/glyph, or measure − 0.5..1px).
- [ ] Container widths via `getBoundingClientRect().width` (never integer `clientWidth`).
- [ ] Explicit per-line `word-spacing` / per-run `letter-spacing` (mixed-line rule `Δcjk = k·Δword`); no `text-align: justify`.
- [ ] Measurement cache keyed by dppx; `matchMedia('(resolution: …dppx)')` invalidation, re-armed per fire.
- [ ] `text-rendering: geometricPrecision` on output; `ctx.textRendering = 'geometricPrecision'` on the canvas measurer (where supported).
- [ ] `copy` listener rebuilding clean text from source offsets (strips per-line `\n`, hyphenation artifacts, comment nodes).
- [ ] Dev-mode audit only — no runtime repair: batch-read rendered line widths, assert deviation ≤ ε; same code backs the §16 render tests.
