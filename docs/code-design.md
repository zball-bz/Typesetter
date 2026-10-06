# Code Highlighting Design (CH)

Status: **implemented (CH1–CH5, 2026-08-26)** — §7 records the as-built deltas.
Companion to [design-decisions-v2.md](design-decisions-v2.md) §4.1 (fence
handlers), [document-model.md](document-model.md) §2/§3, and the M7
precedent ([math-design.md](math-design.md)) for pull-state resources and
committed artifacts. Convention as in v2: every section states the
**decision** and the **why**.

---

## 1. Parser: tree-sitter, build-time grammars only

**Decision: tree-sitter (the C original of the Lezer lineage) parses code;
grammars are compiled at BUILD time — no runtime grammar loading of any
kind.** (Runtime grammar feeding — Lezer `buildParser`, cpp-peglib PEG —
was considered and REJECTED by decision: a blog's language set changes at
build cadence, and one pipeline beats two.)

- Pure C11 runtime, no deps; GLR + error recovery designed for perpetually
  half-broken editor buffers — blog snippets (elided pseudo-code, fragments)
  are exactly that. Regex highlighters (Prism/TextMate) rejected on quality.
- `grammar.js → tree-sitter generate → parser.c`: custom DSLs use the SAME
  pipeline as mainstream languages — author a grammar, add it to the build
  list, done. No second mechanism, no PEG error-recovery caveat.
- Per-grammar `highlights.scm` queries (S-expression patterns) are the
  ecosystem-maintained AST→token-class mapping; the query engine is in the
  same C runtime. Full pipeline runs without JS.
- Cost, accepted: wasm size per grammar (~30KB json … ~350KB gz for cpp),
  lazily loaded per document need.

## 2. Topology: dual wasm, engine pulls tokens

**Decision: tree-sitter is NOT linked into typesetter.wasm. It runs as its
own wasm (web-tree-sitter runtime + grammar side modules) in the worker;
the engine pulls token runs through a new pull-state, exactly like
measurement.**

```
typeset() → NEED_TOKENS { blockId, lang }
  worker: lazy-load grammar side module, parse, run highlights query
tsr_provide_tokens(doc, blockId, (start,end,tagId)*)  → resume
```

As built (plan P1-19): the pull is the `codeTokens` row of the resource
table (docs/host-protocol-design.md §4a), keyed by (language, body) — two
identical blocks ask once — and answered in the binary batch
(`tsr2_requests` / `tsr2_provide`; `tsr_provide_tokens` stays as a shim).
The answer is never folded into the tree: emit and the semantic product
both apply it (`tokenLines`, code/tokens.h). An unacceptable answer
(unsorted, overlapping, off a UTF-8 boundary, past the body, an unknown
tag) fails whole: plain code and a `provider-invalid` warning.

- Why not linking in: it would force MAIN_MODULE/dlopen on the engine
  module (size, call overhead, build complexity) for zero layout benefit.
  The engine stays the layout authority; the tokenizer is an async
  RESOURCE, and the pull loop is this architecture's native way to absorb
  async resources (NEED_MEASURE precedent).
- Unknown lang / grammar failed to load → provide zero tokens: plain
  monochrome code, never an error (measure-fallback discipline).
- **One exception: the engine tokenizes its own language** (plan P1-09).
  A ```` ```tsm ```` block is answered inside the engine at Resolve with
  the engine's own tokens (`syntaxTokens`, syntax/exports.h) — no
  NEED_TOKENS round trip, the same tokens the editor shows. The
  tree-sitter-tsm grammar remains the editor's cold-start fallback, held to
  the engine by the native conformance check (tests.cc,
  `unitTokenConformance`).
- **Native goldens get stronger, not weaker**: tree-sitter is plain C, so
  the native test binary links the runtime + grammars STATICALLY — the
  same parse tables produce byte-identical tokens, and highlighting
  becomes a deterministic golden stage (the mathbox property). The wasm
  side uses stock web-tree-sitter; no fork of either.

## 3. Content model: existing kinds, structured code lines

**Decision: no new node kind.** The highlighter (worker-side, between
provide and instantiate — concretely: the engine folds provided token runs
at emit) renders into what already exists:

- `codeblock` gains args: `wrap` (bool), `lineNo` (bool | start number),
  `hl` (line ranges "3,5-7"). New ARGK keys → **OPS_VERSION 3**.
- Token styling rides `Styling`: color (existing) + **new decoration bits**
  (underline / overline / line-through) on the u64 — rendered as
  `text-decoration`, serialized by both HTML paths. Extending InlineStyle
  is the documented model-version event (document-model §3).
- Copy/semantic/goldens inherit automatically: lines are real text runs;
  the semantic serializer emits `<pre><code>` with `<span class="tsr-tok-*">`,
  so the static-export path is highlighted for free.

## 4. Grid renderer (K::Code becomes a character grid)

**Decision: monospace is a METRIC CONTRACT, not a measurement problem**
(the defined-width-dash precedent): every char is DEFINED 1ch, CJK/fullwidth
2ch; the engine measures exactly one thing per code style — `ch` itself.

- Fonts must be duplexed (bold/italic same advance). An audit measures
  bold 'M' vs regular 'M' once and diags on mismatch instead of silently
  drifting. Recommended default stack leads with a strict-2:1 CJK mono
  (Sarasa/更纱黑体 class) so Chinese comments stay on the grid.
- **Word wrap is a column computation** (no Knuth, by decision):
  `cols = floor(measure/ch)`, greedy fill with a two-level break
  preference — token boundary (space/punct) over mid-identifier — plus a
  configurable continuation indent. As built (plan P3-11): the boundary is
  a break-AFTER character class (`GridParams.breakAfter`, default
  `space tab , ; ) } ] >`; CJK between any two characters by kinsoku) —
  no token or language information is read; the wrapper is the pure
  `wrapGridLine` (layout/grid.cc) and the alignment (snap-kerning, the
  measured CJK ratio) no longer depends on wrapping (`wrap: false` kept
  snap-kerning off). Wrap is ON by default: lines are
  absolutely positioned with no scroll container, so overflow has nowhere
  to go.
- Line numbers reuse the gutter mechanism (`.tsr-marker`, right:100%):
  number rides `line.marker`, continuation rows unnumbered, `data-syn`
  keeps them out of copy. `hl` ranges render as full-width line
  backgrounds (diff/focus styling).
- Token style set: `{ color, weight, italic, underline, overline,
  line-through, background }` — none of which move the grid (weight/italic
  guaranteed by the duplex contract).

## 5. Theme and plugin surface

- Token classes adopt tree-sitter's highlight-tag names (`keyword`,
  `type`, `function`, `comment`, `string`, `number`, `operator`,
  `punctuation`, `constant`, `variable`) — ecosystem-standard, grammar-
  agnostic. tagId↔class table is a build product beside the grammar list.
- Theme = tag→style map in Config (JSON), CSS custom properties in the
  shell for light/dark. The "plugin system" therefore reduces to:
  build-time grammar list + runtime theme config + the existing
  `$.fence` override (a document can still take over any language tag).
  No new JS plugin contract is needed for CH; revisit plugins when a
  feature demands runtime code.

## 6. Milestones

- **CH1 — model**: decoration bits, structured code lines
  (seq/styled/text per line), multi-run K::Code render; no wrap/numbers
  yet. Goldens over hand-written token runs.
- **CH2 — pull state**: NEED_TOKENS + tsr_provide_tokens; native tests
  statically link tree-sitter + one small grammar (json) → first real
  highlight goldens; worker loads web-tree-sitter lazily.
- **CH3 — build pipeline**: grammar list → generate → emcc side modules +
  compiled highlights queries + tagId table; theme in Config; light/dark
  CSS.
- **CH4 — grid**: ch contract + duplex audit, wrap, line numbers, hl
  lines.
- **CH5 — polish**: specimen, e2e audits (grid-alignment invariant),
  docs, corpus spot checks.

Defaults taken unless overridden: initial grammar list js, ts, python,
cpp, rust, json (+tsm itself later); cpp ships despite its size (lazy
load); wrap continuation indent 2ch with no marker glyph.

## 7. As-built deltas (CH completion)

**Literate C++ fragments (2026-09-01; superseded by plan P3-22, §8).** pbrt-style `<<Fragment Name>>`
references and `<<Name>>=` / `+=` definition headers are not C++, so
tree-sitter-cpp shredded them (Function → type, Definitions → variable,
`>>` → operators) and degraded the surrounding parse. Rather than fork
the grammar (530K-line parser.c to vendor; `<<` fights the shift
operator lexically), the token provider handles them: `tokenize()` in
runtime/src/worker/tokens.mjs recognizes fragment spans by regex, emits
each as one `label` token (priority above grammar captures), and hands
tree-sitter the text with those spans blanked to spaces — same length,
so no offset mapping. Applies to the cpp family only. Editor side: the
VSCode extension injects `entity.name.tag.fragment.literate.tsm` into
`meta.embedded.block.cpp` via a TextMate injection grammar (0.2.2). The
native token provider links only json+tsm, so goldens are unaffected.

- **Side modules are self-compiled** (tools/codehl-assets.mjs): the
  prebuilt `tree-sitter-wasms` package was dylink-ABI-incompatible with
  web-tree-sitter 0.26; emcc SIDE_MODULE=2 over the grammar packages'
  checked-in parser.c produces far smaller modules anyway (json 5KB …
  cpp 3.3MB). No `tree-sitter generate` at build time either — every
  grammar repo checks its generated parser.c in.
- **Query inheritance is flattened at asset build**: web-tree-sitter has
  no `; inherits:` support; typescript concatenates javascript's
  highlights, cpp concatenates c's (node names line up).
- **UTF-16→UTF-8 mapping in the worker provider**: web-tree-sitter node
  indices are JS string offsets; the engine folds by byte — a CJK char or
  en-dash in a comment shifted every later token until mapped.
- **Priority contract**: captures sort (start asc, patternIndex asc),
  earlier pattern wins on overlap — implemented identically in the native
  (C++) and worker (JS) providers; the tag set (kTokenTags / TOKEN_TAGS)
  is generated from syntax.def since plan P1-05, the alias table from the
  language manifest since plan P3-22 (§8).
- **.tsr-code gained white-space:pre** — leading indentation collapsed in
  every code path until CH3's graphical pass caught it.
- **Continuation indent is two literal spaces in the text flow**, not an
  absolute px offset: static export renders on fonts the engine never
  measured, and a baked 2×ch offset misaligns there — real spaces are
  exactly 2ch in whatever mono font paints. Synthetic
  (data-syn="cont" + user-select:none) for both copy paths.
- **Wrapped rows emit data-ragged + data-join="none"**: ragged exempts
  code rows from the justify audits; the join keeps the §9.3 copy rebuild
  emitting LOGICAL lines (e2e-pinned).
- **Deferred**: duplex-font audit (bold/italic 'M' width probe — the
  contract is documented, the audit is not yet armed), Sarasa as the
  default code stack (user call), tsm's own grammar, line-number column
  in the static-export path.

- (plan P2-08) Comment-aware hanging reads the run's style: a comment token's
  style is italic with `code.hang: content` (foldTokens), and an authored run
  asks for it with `style({code: {hang: 'content'}}, …)` — emit no longer
  compares a run's colour with `var(--tsr-tok-comment)`. Token runs carry
  their class `tok-<tag>` (rendered from P3-18); the colour stays inline.

## 8. The language manifest and engine overlays (plan P3-22; T9 A6)

**One manifest.** `engine/schema/languages.json` defines:
- the classes: syntax.def's TOKEN_TAGS, each with the editor's semantic
  token type;
- the capture aliases (`conditional` → keyword, …);
- the languages, each with its fence-tag aliases, its tree-sitter sources
  and queries, and whether the native build links it;
- the overlays and the fence profiles.

`tools/gen-languages.mjs` (run by gen-all) generates:
- the worker's and the editor's tables: `runtime/src/shared/languages.gen.mjs`;
- the extension's legend: `editors/vscode-tsm/src/hl.gen.js`;
- the engine's tables: `engine/src/code/languages.gen.h`;
- the native build's grammar list: `engine/native_grammars.gen.cmake`.
  CMake builds the grammars and embeds their queries from it.

`tools/codehl-assets.mjs` builds the web side modules from the same list.
A new built-in language is one manifest entry and an asset rebuild.

The generator refuses a manifest whose classes are not the token tags, a
class the theme does not colour, and a native grammar whose queries use
`#match?`.

**One core.** `runtime/src/shared/hl-core.mjs` holds a capture's class
(`tagOf`) and the priority contract (`resolveCaptures`): a stable sort by
(start, pattern), ties in the cursor's order, an earlier capture winning
an overlap. Both the worker's provider and the editor's cold-start
tokenizer use it.

The native twin (`code/native_tokens.cc`) is the same contract in C++:
- `std::stable_sort`;
- the string predicates `#eq?`, `#not-eq?`, `#any-of?` and
  `#not-any-of?`, which web-tree-sitter evaluates too.

**Overlays are the engine's** (`code/overlay.{h,cc}`). A code block's
`codeblock.overlays` names manifest overlays. noweb is `<<`, a name
without `<`, `>` or a line break, `>>`, then an optional `+=` or `=`. It
can be set three ways:
- a fence argument: ```` ```py(overlays: ["noweb"]) ````;
- a rule: `$.set({kind: 'codeblock', lang: 'py'}, {codeblock: {overlays: 'noweb'}})`;
- a fence profile: ```` ```cpp-literate ````, which is cpp plus noweb by a
  built-in default rule. A later rule with `overlays: []` switches it off.

At Resolve the engine finds the overlay spans in the body. It asks the
provider for the tokens of the body with every span byte blanked to a
space. The text keeps its UTF-8 length, so every offset outside a span
holds, and a CJK fragment name shifts nothing. The provider only ever
sees plain code.

Emit and the semantic page set the spans over the answer as `label`
tokens; a span wins any overlap. The need's key is (language, body,
overlays). The Session stores the provider's answer by the text it was
sent.

The cpp-only regex in the worker is gone, so plain ```` ```cpp ```` keeps
`(1 << n) >> 2` as shifts. `tools/convert/pbr2tsm.mjs` writes its literate
fragments as ```` ```cpp-literate ````.

**Hosts add languages at run time.** A resource provider may declare
`match(row)`. A row goes to the latest registered provider that accepts
it, so `createEngine({providers: [{kind: 'codeTokens', module}]})` with
`match: (r) => r.lang === 'tla'` adds a language beside the built-in
highlighter (host-protocol-design §4b).

**As-built deltas from T9 A6:**
- The manifest lives with the other vocabulary tables in
  `engine/schema/`, not in `runtime/hl/`.
- The classes' order stays syntax.def's TOKEN_TAGS, which the in-engine
  tsm tokenizer shares. The manifest maps each class to the editor's type
  and is checked against that order.
- The answer carries no `canonLang`. `code.fontFeaturesByLang` applies a
  built-in language's features to every tag that names it: `c++`, `cc`
  and `cpp-literate` are cpp, and a tag configured itself wins. A host
  provider's language is its own fence tag.
- A codeblock selector's `lang` is the code's language (its own
  attribute); `textLang` selects the text's language.
