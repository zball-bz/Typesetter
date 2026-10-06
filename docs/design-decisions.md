# Design Decisions

Settled during architecture discussions. This is the agreed foundation for the new engine.

> Historical (v1). Superseded by [design-decisions-v2.md](design-decisions-v2.md): there is no Lua evaluator and no PackCC parser — the front end is hand-written C++ driven by `engine/src/syntax/syntax.def` ([syntax-design.md](syntax-design.md)).

## Architecture

**C++/Emscripten core.** The entire pipeline runs in WASM:

```
Typst-like markup
  -> C++ parser (PackCC-generated PEG) -> AST with code nodes
  -> C++ evaluator drives Lua 5.4 (content generation, directives, scripting)
  -> Content block tree (paragraphs, headings, math, tables...)
  -> C++ emission (per-paragraph): text runs -> measurement batch -> block stream -> breaker -> line results
  -> C++ block layout (vertical positioning, flow)
  -> C++ HTML string generation
  -> JS shell: inject HTML into DOM
```

**JS is minimal.** Only two things stay in JavaScript:
- Text measurement (Canvas API `measureText`) — called synchronously from WASM
- DOM insertion of the generated HTML string

**Text measurement protocol:** Per-paragraph batching. For each paragraph:
1. C++ emission collects all unique (string, font, size, weight, italic) tuples
2. One synchronous call to JS via `EM_JS` / `EM_ASM`: "measure these N strings"
3. JS measures all via Canvas, returns N widths
4. C++ populates block widths, runs breaker, continues

One WASM<->JS boundary crossing per paragraph.

## Markup Language

**Typst-like, not Markdown.** Motivations:
- Extensible block syntax (Markdown lacks this)
- Embedded scripting for content generation
- Better lists, quotes, tables than Markdown
- Ability to embed computed content
- Markdown edge-case compatibility is a non-goal

**Three modes:**
- Markup mode: Typst-style inline formatting (`*bold*`, `_italic_`, `` `code` ``, `@ref`, `[link](url)`, etc.)
- Code mode: `#` enters a single Lua expression (modified syntax)
- Math mode: `$...$` enters math

**Code mode details:**
- `#expr` evaluates one Lua expression inline. The expression extends as far as it can parse.
- `#expr;` — semicolon forces early termination of the expression.
- Square brackets `[...]` are content blocks: `#heading[This is *bold* content]`.
- Content blocks nest arbitrarily: `#if cond [text #func[nested]] [else text]`.

## Modified Lua Syntax

The scripting language is Lua 5.4 with the following modifications:

**Square brackets repurposed for content blocks.** `[...]` is no longer Lua table indexing — it is exclusively used for content blocks (inline markup regions). This is the mechanism for embedding prose inside code.

**Table indexing replacement:** Use dot access for known keys (`t.key`) or `t.(expr)` for computed keys (replaces `t[expr]`).

**Indentation-based blocks.** Curly braces are used up (object notation), so multi-line blocks use indentation for scoping. This is not for aesthetics — it is because all bracket types are consumed by other syntax roles.

**Object notation:** `{"key": value, 1, 2, 3}` — JSON-like fused table/array, matching Lua's unified table model but with JSON-familiar surface syntax.

**Scripting is not a first-day priority.** The Lua evaluator and content generation API are deferred. The parser and inline typesetting pipeline come first.

**Directives are plain Lua.** No special directive system — user-defined directives are Lua functions that emit content.

## Parser

**PackCC** (PEG parser generator).
- Input: `.peg` grammar file with inline C actions
- Output: self-contained `.c` file compiled into WASM module
- PEG chosen because:
  - Ordered choice eliminates ambiguity (critical for prose/code overlap)
  - No separate lexer needed; mode switches are rule calls
  - Nested context switching is natural recursion
  - Supports left recursion (useful for expression parsing in code mode)

## Math

**Typst-style calculator expressions.** Human-readable infix notation, not LaTeX backslash commands.

Example: `$sum_(i=0)^n i/n ~> (n+1)/2$`

**Syntax:**
- `/` for fractions: `a/b` renders as a fraction, `(a+b)/(c+d)` for compound fractions
- `^` for superscript, `_` for subscript
- `~>` for arrows
- Parentheses `()` for explicit grouping
- Juxtaposition for multiplication

**Precedence:** Standard infix rules as humans naturally read them. `/` and juxtaposition follow conventional mathematical precedence.

**Big operators** (`sum`, `prod`, `int`, etc.) are **greedy** — they bind everything to their right until a closing bracket or end of expression. `sum_(i=0)^n i/n` means the `i/n` is the body of the sum, not a separate expression. Use parentheses to limit scope when needed. (As built, plan P3-25: the scope is a reading annotation — up to the next relation, typed ones included — and layout sets the body's atoms in the formula's own run, so an inline sum breaks inside its body like any other run.)

**Symbol shorthands — three tiers:**

1. **ASCII sequences** for symbols with natural visual representations:
   - Arrows: `->`, `<-`, `=>`, `<=>`, `-->`, `<->`, `|->`, `|-->`
   - Relations: `!=`, `>=`, `<=`, `>>`, `<<`, `:=`, `::=`, `~=`, `-=`
   - Logic: `|--` (⊢), `|==` (⊨), `--|` (⊣), `_|_` (⊥)
   - Operators: `+-` (±), `-+` (∓), `o+` (⊕), `ox` (⊗), `o.` (⊙), `xx` (×)
   - Misc: `...`, `:.` (∴), `:'` (∵)

2. **Short names** for symbols that don't have intuitive ASCII art:
   - `forall` (∀), `exists` (∃), `in` (∈), `subset` (⊂), `cap` (∩), `cup` (∪), `empty` (∅), `inf` (∞), `partial` (∂), `nabla` (∇)
   - Greek letters by name: `alpha`, `beta`, `gamma`, etc.
   - Text operators: `sin`, `cos`, `log`, `lim`, `det`, `max`, etc.

3. **`AA`..`ZZ`** reserved exclusively for blackboard bold: `NN` (ℕ), `ZZ` (ℤ), `QQ` (ℚ), `RR` (ℝ), `CC` (ℂ), etc.

**Design principle:** Prefer readable names over cryptic abbreviations. `forall` is far clearer than `AA` for ∀. Double-letter shorthands are only for blackboard bold, where the doubling visually matches the double-struck appearance.

**Negation pattern:** `!` prefix negates: `!=`, `!in`, `!exists`, `!|`, `!||`.

**As built (plan P3-24; the deltas from Typst and from the tiers above):**
- *One identity per symbol.* A typed symbol is its name: `∑` is `sum` (large, limits), `≤` is `<=` (a relation: it ends a sum's body), `⟨` is `langle` (it opens a stretching group). The vocabulary is `engine/data/math/symbols.tsv`; the TeX converters' map is generated from it.
- *Negation is a rule, not a list.* A `!` touching a symbol negates it by the Unicode data (`!=` ≠, `!in` ∉, `!models` ⊭, `!|` ∤, `!||` ∦). A relation with no negation is an error leaf (`!<<`). A `!` before anything else, or with a space, is a factorial (`n! = n (n-1)!`).
- *ASCII keys are operator characters only.* `_|_` and `o+`-style keys are gone; ⊥ is `perp` or `bot`, ⊕ is `oplus`.
- *Only parentheses disappear (D-M01).* `x^(a b)` sets a b as the exponent without brackets; `x^{a b}` and `x^[a]` keep theirs. `class(ord, …)` makes an atom without brackets.
- *An operand is one token; an unknown word as an operand is its letters* (plan P3-25): `x^ab` is x^{ab}, `a/bc` is a/(bc) and `ab/c` is italic ab over c; a name is one token (`x^pi` is x^π). A relation name as a script (`x_in`) is that symbol, with an info diagnostic.
- *A call needs its parenthesis adjacent.* `abs(x)` is a call; `abs (x)` is the name abs and a group.
- *A bar alone in a group is its middle.* `{x | x > 0}` and `P(A | B)` stretch the bar with the group and space it as a relation; two bars (`|x|`, `||v||`) stay as they are.
- *Alphabets are rows.* `bb(R)`, `cal(A)`, `frak(g)`, `bold(v)`, `italic(h)`, `sans(x)`, `mono(x)`; `AA`..`ZZ` stay the blackboard shorthand.
- *Spacing, style and limits are rows.* `thin`, `med`, `thick`, `quad`, `wide`; `display(…)`, `inline(…)`, `script(…)`, `sscript(…)`; `limits(…)` and `scripts(…)` choose where an operator's scripts go.
- *An unknown word says so.* It is still set upright as an operator name (Typst's rule), with an info diagnostic `math-implicit-name` (D-M04).
- `vec` stays an accent (Typst draws an arrow over); `cases` rows are separated by `;`; `#` starts a hole (plan P2-15).

## Type System

**Class-oriented styling with inline style escape hatch.**

`TextStyling` with `bigint` class bitset (from `types2.ts` design, ported to C++).
- `bigint` in JS / `uint64_t` or wider in C++ — 32 bits is not enough, already 15+ engine-defined classes
- Dynamic class IDs (>= 256) for markup-defined styles
- Span tree builder uses subset/superset relationships for efficient nested HTML

**Inline styles** (color, weight, etc.) are supported. Blocks with inline styles are marked with a special class bit (`CLS_HAS_STYLE`) to indicate they need special treatment during rendering. Everything else is pure class-based.

## Block Layout

**Absolute positioning.** The engine outputs HTML using absolute positioning for all content placement.

- Caller provides container specifications: width, page height (can be infinite), page count (can be infinite)
- Caller must ensure the container element is CSS `position: relative` (or `absolute`/`fixed`) so that absolutely-positioned children behave correctly
- Engine does not create container structure — it emits content that goes inside the caller's container
- Engine owns all vertical placement; CSS flow layout is not used

## Build System

- **CMake + emcc** (Emscripten compiler)
- Lua 5.4 source compiled in statically
- PackCC runs at build time: `grammar.peg` -> `parser.c` -> compiled into module

## Pipeline (Detailed)

1. **Parsing** — PackCC PEG parser produces AST
2. **Content generation** — Lua evaluator processes code nodes, produces content block tree
3. **Content block generation** — typed block tree (paragraphs, headings, math, tables, etc.)
4. **Inline typesetting** — for blocks needing line breaking:
   - Script segmentation (Latin, CJK, punctuation, boundaries)
   - Hyphenation (owned Liang implementation in C++)
   - Block emission (CJK rules, punctuation compression, boundary spaces)
   - Text measurement (batch call to JS)
   - Knuth-Plass optimal line breaking
5. **Block layout** — absolute vertical positioning, spacing, pagination
6. **HTML rendering** — span tree builder, output absolutely-positioned HTML string

## Hyphenation

Owned C++ implementation of Liang's algorithm.
- Compile TeX pattern sources into a compact runtime format (format TBD at implementation time; priority is compactness and easy parsing)
- English first, language packs as pluggable units
- Runtime independent from DOM and browser globals

## Project Goals

1. Build a real paragraph and inline layout engine for web-first publishing.
2. **Demonstrate that a relatively simple, fully-featured typesetting system is achievable** — core algorithms are straightforward; running efficiently on web alone for "publication quality blogs" is the interesting proposition.
3. Support Latin, CJK, and mixed-script text.
4. Custom math rendering (AMS Euler, simpler syntax than LaTeX).
5. Keep every pipeline stage inspectable and debuggable.

## Debugging and Testing

- Unit tests for each pipeline stage
- A demonstrative webpage for each stage, built incrementally as stages are implemented
- No upfront inspector app — inspection tools grow with the engine

## Migration

- Current `src/` becomes frozen PoC reference
- New development in C++ under the engine directory
- Start from the parser (grammar definition is the first deliverable)

## Open

- Math operator dictionary — full set of supported symbols with name (e.g. `arrow.right`), abbreviation (e.g. `->`), and Unicode codepoint (e.g. `\u2192`). Needs research to compile the table.
- AMS Euler font metrics source — need to find a way to extract metrics (possibly download the font and parse with opentype.js or similar). Low priority: math rendering is late in the dev timeline and largely independent from the rest of the engine.
