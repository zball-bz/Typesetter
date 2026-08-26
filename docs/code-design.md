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

- Why not linking in: it would force MAIN_MODULE/dlopen on the engine
  module (size, call overhead, build complexity) for zero layout benefit.
  The engine stays the layout authority; the tokenizer is an async
  RESOURCE, and the pull loop is this architecture's native way to absorb
  async resources (NEED_MEASURE precedent).
- Unknown lang / grammar failed to load → provide zero tokens: plain
  monochrome code, never an error (measure-fallback discipline).
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
  configurable continuation indent. Wrap is ON by default: lines are
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
  (C++) and worker (JS) providers; kTokenTags + alias table shared by
  hand (comment: keep in sync).
- **.tsr-code gained white-space:pre** — leading indentation collapsed in
  every code path until CH3's graphical pass caught it.
- **Wrapped rows emit data-ragged + data-join="none"**: ragged exempts
  code rows from the justify audits; the join keeps the §9.3 copy rebuild
  emitting LOGICAL lines (e2e-pinned).
- **Deferred**: duplex-font audit (bold/italic 'M' width probe — the
  contract is documented, the audit is not yet armed), Sarasa as the
  default code stack (user call), tsm's own grammar, line-number column
  in the static-export path.
