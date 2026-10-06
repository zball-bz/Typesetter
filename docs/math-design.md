# Math Design Study (M7)

Status: **implemented (M7a–M7d, 2026-08-26)** — §13 records the as-built deltas.
Companion to [design-decisions-v2.md](design-decisions-v2.md) §13 (the standing
decisions this study details) and [document-model.md](document-model.md).

Sources studied (2026-08-26):

- **KaTeX** (`src/Style.ts`, `buildHTML.ts`, `buildCommon.ts` vlist,
  `spacingData.ts`, `fontMetrics.ts`, `functions/{supsub,op,utils/assembleSupSub}.ts`,
  `delimiter.ts`) — the TeX Appendix G lineage, rendered as HTML.
- **Typst** (`crates/typst-layout/src/math/{fraction,radical,scripts,run,mod}.rs`,
  `fragment/{mod,glyph}.rs`, `typst-library/src/math/ir/process.rs`) — the
  OpenType MATH lineage, rendered as positioned frames.
- **Fonts, inspected with fontTools**: `neo-euler` (khaledhosny/euler-otf,
  abandoned) and its successor **Euler-Math 0.75** (CTAN `euler-math`).

Convention as in v2: every section states the **decision** and the **why**.

---

## 1. Font: Euler-Math, not neo-euler

**Decision: bundle Euler-Math (CTAN, currently 0.75, OFL) as the math font.
The v2 §13 choice of "Neo Euler" transfers to its maintained successor.**

Measured comparison (fontTools, MATH table):

| | neo-euler | **Euler-Math 0.75** |
|---|---|---|
| glyphs / cmapped | 1904 / 680 | 3531 / **3530** |
| U+2200–22FF operators | 114/256 | **240/256** |
| math alphanumerics U+1D400+ | 259 | **927** |
| arrows | 30 | 80 |
| vert / horiz variant chains | 42 / 9 | **58 / 52** |
| top-accent attachments | 267 | **1062** |
| italic-correction entries | 59 | 75 |
| MathKernInfo (cut-in kerning) | none | none |
| status | "abandoned, archæological" | maintained on CTAN |

Two findings gate the whole design:

1. **Every glyph referenced from variant chains and assemblies (528 refs) is
   reachable through cmap.** The browser can only paint what a codepoint
   addresses; because Euler-Math encodes its size variants and assembly parts,
   the HTML text-rendering path works without KaTeX's compromise (KaTeX ships
   its own Size1–Size4 fonts specifically to give variants codepoints). A
   build-time check in the metrics compiler asserts this property so a font
   upgrade cannot silently break it.
2. **Neither Euler has MathKernInfo**, so OpenType cut-in kerning of scripts
   (the staircase kern against `∫`-like glyphs) is dropped from scope — the
   spec-correct algorithm (Typst `math_kern`) is recorded below for the day a
   font provides it.

The pipeline stays **font-agnostic**: everything reads the precompiled MATH
artifact, nothing hardcodes Euler. STIX Two Math is the designated swap-test
font (full MathKernInfo, richer assemblies) for validating that neutrality.

## 2. Two lineages, one verdict

**KaTeX** reimplements TeX Appendix G against *private* font parameters: the
σ/ξ arrays extracted from cmsy/cmex TFMs (`fontMetrics.ts` — `sup1..3`,
`sub1..2`, `num1..3`, `denom1..2`, `axisHeight`, `bigOpSpacing1..5`, …) plus
per-glyph `[depth, height, italic, skew, width]` tables. Rendering is nested
spans: a `vlist` construct fakes vertical positioning with a `pstrut` (an
oversized zero-width strut that pins each child's baseline) inside
`table-cell; vertical-align:bottom` rows. It is a heroic fight against
browser line-box semantics.

**Typst** reads the **OpenType MATH table** (`MathConstants`,
`MathItalicsCorrection`, `TopAccentAttachment`, `MathVariants` with glyph
assemblies) and lays out `Frame`s with explicit `(x, y)` positions and an
explicit baseline. Every construct is a direct transcription of MATH
constants with TeXbook rules as tiebreakers.

**Decision: Typst's lineage for layout (MATH constants + explicit frames),
KaTeX's lineage for what MATH does not cover** — the inter-atom spacing
matrix, bin→ord demotion, the style-transition algebra, and the linebreak
policy — because those live in the TeXbook, not in the font. This is a
natural fit: our renderer already owns absolute positioning (v2 §8), so we
get Typst's clean frame model without KaTeX's vlist/pstrut contortions; and
our engine is C++ with a build-time artifact pipeline, so MATH extraction
mirrors the hyphenation-pattern precedent exactly.

Notably, Typst's simplified class-pair spacing (`ir/process.rs::spacing`) is
a *derivation* of the TeX matrix (thin after punct, thick around Rel, medium
around Bin, nothing inside Open/Close, thin around Large except before
opening). We take the full TeX matrix (KaTeX `spacingData.ts`) since our
operator dictionary already commits to TeX atom classes (v2 §13).

## 3. Precompiled metrics artifact

**Decision: `tools/mathc.py` (python3 + fontTools, CI-installable) compiles
`fonts/Euler-Math.otf` → committed `engine/gen/euler_math.h`** — the same
committed-artifact pattern as `hyphen_en_us.h`. Contents:

- `MathConstants` — all ~56 values, in font design units + `upem`, converted
  to `su` at use time (`su = units * sizePx * 64 / upem`, rounded once).
- Per-glyph records for the covered ranges we ship (ASCII, Greek, math
  operators/arrows/misc-technical, math alphanumerics): codepoint, advance,
  ink ascent/descent (from CFF bounds — needed for delimiter targeting and
  accurate box extents; hhea metrics are line metrics, not ink), italic
  correction, top-accent attachment.
- Vertical variant chains: per base codepoint, the ordered
  `(codepoint, advance)` list — **codepoints, not glyph ids** (per §1.1).
  Horizontal chains are not compiled (plan P1-22: nothing read them;
  horizontal stretch is a recorded deferral), their glyphs still are.
- Assemblies: part lists `(codepoint, startOverlap, endOverlap, fullAdvance,
  isExtender)` + `MinConnectorOverlap`.
- A generated coverage bitmap so emission can diagnose "symbol not in math
  font" at compile time rather than rendering tofu (`measure-fallback` diag).

Estimated size: ~3.5k glyph records ≈ 60–90 KB of header — in line with the
hyphenation artifact (as built: 2023 records).

**The vocabulary is not the font's** (plan P1-22; design T8 MathDict):
`engine/data/math/symbols.tsv` (one row per spelling: code point, TeX atom
class, flags, UCD negation, the code point's default row, whether the class
is MathML Core's or an override) is compiled by `tools/mathdict.py` with the
pinned UCD (`engine/rules/ucd/17.0.0/UnicodeData.txt`) and the pinned MathML
Core operator dictionary (`engine/data/mathml/operator-dictionary.tsv`) into
`engine/gen/math_dict.h` and `engine/src/math/atom.h`; `math/dict.h`
(`MathDict`) is the API: names by binary search, operator keys by a trie
(the lexer's maximal munch), a bare code point's class by its default row,
negations from the UCD. The generator fails on duplicate names, ambiguous
code points, keys mixing letters and operator characters (the `!word`
rows aside), keys longer than the munch, and a class that differs from
MathML Core without `class_source=override`. Font coverage is no longer a
vocabulary filter: `mathc.py` ships its ranges (plus U+02B0–U+02FF) and
every chain reference, and a unit test keeps the record set a superset of
`engine/data/math/glyph-cps.baseline.txt`.

## 4. The model: MathBox IR

**Decision: one arena-allocated box type, laid out in `su`, fully
deterministic.**

```
MathBox {
  w, asc, desc   : Su          // extents relative to the box baseline
  italic         : Su          // italic correction (glyph/base boxes)
  cls            : AtomClass   // Ord Op Bin Rel Open Close Punct Inner
  kind           : Glyph | Rule | HBox | Spacer
  text           : StrRef      // Glyph: the character(s) to paint
  style          : MathStyle   // size index for font-size emission
  kids           : [(dx, dy, MathBox*)]   // dy: child baseline vs own baseline
}
```

Everything is an `HBox` of positioned children in the end — fractions,
scripts, radicals just compute the `(dx, dy)` and extents. No VBox type:
vertical stacking is a layout *procedure*, not a box kind (Typst does the
same with `Frame::push_frame`). `Rule` covers fraction bars, radical
overbars, and `\overline`.

The big property this buys: **math layout consumes zero browser
measurements**. Given the precompiled artifact, the entire box tree is a
pure function of (source, config) in integer `su` — natively golden-testable
(`--stage=mathbox` dump), no Playwright in the loop. Math is the one part of
the pipeline that is *more* deterministic than text.

## 5. Style algebra

Eight styles: `D, D', T, T', S, S', SS, SS'` (display/text/script/
scriptscript × cramped). Transition tables verbatim from the TeXbook (KaTeX
`Style.ts` encodes them as arrays — we adopt the same encoding):

```
sup:     D→S  T→S  S→SS SS→SS   (cramped follows cramped)
sub:     always cramped sup target
fracNum: D→T  T→S  S→SS SS→SS
fracDen: cramped fracNum target
cramp:   X→X'
```

Size factors come from the font, not TeX: `ScriptPercentScaleDown = 70%`,
`ScriptScriptPercentScaleDown = 50%` (Euler-Math). As built there is no
size floor (a `minMathSizePx` was planned and not implemented).
Cramped-ness selects constants (Euler-Math: `SuperscriptShiftUp` 450 vs
`SuperscriptShiftUpCramped` 350) but not size.

## 6. Atom classes, spacing, demotion

- Classes come from the operator dictionary; unknown ordinary content is Ord.
- Inter-atom glue: the TeX pair matrix in mu (1 mu = 1/18 em): thin(3)/
  medium(4)/thick(5), with the tight subset (thin only, Ord↔Op) in S/SS
  styles. Encoded as an 8×8 table in the artifact’s companion header.
- **Bin→Ord demotion** exactly as TeXbook Rules 5–6 (KaTeX
  `binLeftCanceller/binRightCanceller`): Bin after {start, Bin, Op, Rel,
  Open, Punct} demotes; Bin before {end, Rel, Close, Punct} demotes. This is
  what makes `-x`, `(-1)`, `a + -b` come out right and it costs one linear
  pass over the top-level run.

## 7. Construct algorithms (MATH-first, TeXbook tiebreakers)

Each construct is a small function from child boxes + constants to an HBox.
Crosswalk: T = Typst file, K = KaTeX file.

- **Run**: place children left to right with pair glue; italic correction is
  *not* added between adjacent glyphs (upright Euler barely needs it) but is
  carried on the box for scripts/limits. [T `run.rs`]
- **Scripts** (`x^a_b`): shifts via `SuperscriptShiftUp(Cramped)`,
  `SuperscriptBottomMin`, `SubscriptShiftDown`, `SubscriptTopMax`,
  `Sub/SuperscriptBaselineDrop{Max,Min}`; joint collision resolution grows
  `shift_up` first up to `SuperscriptBottomMaxWithSubscript`, then splits the
  remaining `SubSuperscriptGapMin` deficit both ways. Subscript hangs back by
  the base's italic correction; `SpaceAfterScript` (50) pads the right edge.
  [T `scripts.rs::compute_script_shifts` — adopt verbatim; K rule 18a–f]
- **Limits** (display-style big ops): above/below the base, gaps
  `Upper/LowerLimitGapMin` with baseline mins `UpperLimitBaselineRiseMin` /
  `LowerLimitBaselineDropMin`; horizontal centers offset by ±italic/2 (the
  slant trick — K `assembleSupSub` uses `bigOpSpacing1..5` for the same
  effect; we use the MATH constants). [T `compute_limit_shifts`]
- **Fractions**: numerator up by `FractionNumerator(DisplayStyle)ShiftUp`,
  denominator down by the mirror constant, bar of `FractionRuleThickness` at
  `AxisHeight`, gaps clamped by `Fraction{Num,Denom}(DisplayStyle)GapMin`,
  baseline = bar + axis. Stacks (`binom`-style, no bar) use the `StackTop/
  Bottom…` constants with the leftover-gap split. [T `fraction.rs` — adopt
  verbatim]
- **Radicals**: gap `Radical(DisplayStyle)VerticalGap`, rule
  `RadicalRuleThickness`, `RadicalExtraAscender`; surd stretched to
  radicand height + gap + rule via the variant chain; leftover distributed
  half above, half below (TeXbook p.443 item 11); degree raised by
  `RadicalDegreeBottomRaisePercent = 60%` with the kern-before/after
  constants. [T `radical.rs`]
- **Delimiters** (`\left…\right` semantics for our bracket forms): target
  height = 2 × max(content ascent − axis, axis + content descent), shortfall
  ~10% tolerated (Typst's `short_fall`); walk the variant chain, else
  assemble parts with extender repetition, overlaps ≥ `MinConnectorOverlap`
  (20); center the result on the axis. [T `fragment/glyph.rs::stretch`;
  K `delimiter.ts` stacked path]
- **Big operators**: in display style swap to the variant satisfying
  `DisplayOperatorMinHeight` (1130 in Euler-Math), center on axis; limits attach per above
  when style is display, as scripts otherwise (K `op.ts` delegation rule).
- **Accents**: position by `TopAccentAttachment` of accentee and accent
  (1062 entries in Euler-Math; fallback (w+italic)/2), cramped style for the
  base; flatten via `flac` is unavailable in CFF-land — skip, small accents
  only. [T `fragment/glyph.rs` accent attach]

Deferred with rationale: cut-in kerning (no font data, §1), stretchy
horizontal accents/over-underbraces beyond the 52 horiz chains (later),
`\phantom`-class tricks (userland can fake with color), equation tags/multline
alignment (resolver already numbers `mathblock`; alignment points are a
region-shaped feature for later).

## 8. Rendering contract

**Decision: a formula is one inline box; inside it, absolutely positioned
glyph-run spans in the bundled font — the line model recursed one level
down.**

```html
<span class="tsr-math" style="width:_px;height:_px;vertical-align:_px">
  <span class="tsr-mg" style="left:_px;top:_px;font-size:_px">𝑎</span>
  <span class="tsr-mr" style="left:_px;top:_px;width:_px;height:_px"></span>  <!-- rule -->
  …
</span>
```

- `.tsr-math { position:relative; display:inline-block }`, width/height from
  the box, `vertical-align: -descent` pins the engine baseline to the text
  baseline — the same trick as KaTeX's strut, but on one box, computed by
  us, not fought out of line-height.
- The font is a runtime object (plan P1-23; `math/font.h`): a process-wide
  `MathFontRegistry` holds `MathFont`s (Euler-Math, embedded, is id 0) and
  the layouter reads every constant, glyph record and chain from it; a
  glyph box records the font it is painted in (`MathBox::font`, or
  `kTextFont` for names measured by the host), and the writer pins a glyph
  span by that font's hhea line box from the registry. Today's literals are
  `MathPolicy` (the short_fall 1/10 as an integer rational, the assembly
  repeat cap, the uncovered glyph's stand-in box). `tools/mathc.py` also
  writes the paint subset (`fonts/euler-math.woff2`: the glyph record set
  plus U+0020) and its content hash into the header and the font manifest
  `runtime/src/shared/mathfont.gen.mjs`, which the shell installs as a
  declared face of role `math` (no second @font-face path, no fallback
  family) and static export and packaging copy.
- Children paint glyphs by **codepoint** (guaranteed addressable, §1) in
  Euler-Math (`font-kerning: none`: the engine's advances are the layout),
  one positioned span per glyph (coalescing
  same-style glyphs into runs was planned and not done) at our advances (`text-rendering: geometricPrecision`; the font
  is bundled, so browser advances == artifact advances — same file). Rules
  are background-colored divs.
- Font loading: `@font-face` with `font-display: block` scoped to
  `.tsr-math`; since metrics are precompiled, layout never waits — v2 §9's
  "math exact from t=0" holds by construction, only paint waits for the
  ~430KB font (subset at build time to the shipped coverage; expect
  ~150–200KB woff2).
- Copy: the whole formula run carries `data-syn="math"` + `data-s/e`; the
  copy rebuild emits the **source text** (`$…$`) — positioned glyph soup is
  not content text (document-model §9.3 extension).
- Semantic fallback serializer emits the source in `<code class="tsr-mathsrc">`
  for now; MathML output stays rejected (v2 §13), revisit only for a11y.

## 9. Line integration

- `mathinline` emits **one unbreakable LinebreakBlock per breakable segment**:
  break points exist only between top-level atoms (scripts, fractions,
  delimited groups are opaque). Three break classes, penalties independently
  configurable: **after Rel**, **before Rel**, **after Bin** (no before-Bin —
  neither TeX nor AMS style admits it). After-Rel/after-Bin follows TeX
  (TeXbook p.173, `\relpenalty=500` < `\binoppenalty=700`; K
  `buildHTMLUnbreakable`, T `into_par_items`); before-Rel is added because
  CJK/Russian convention puts the relation at the head of the continuation
  line — for a Chinese-language target both sides of `=` read fine, and KP
  picks the globally better cut. Defaults: after-Rel ≈ before-Rel < after-Bin,
  all high enough that breaking mid-formula loses to any decent whole-line
  alternative. At a break the thick/medium space beside the atom is
  discardable glue. Each segment is a block with `content = inlineBox`,
  `breakPenalty` per its class, no stretch. The block carries its own
  `asc/desc` su so `layoutDoc`'s per-line max-advance picks it up (extend
  `LinebreakBlock` with optional intrinsic vertical extents — the mechanism
  headings-in-line already wants).
- `mathblock` becomes its own FlowUnit (kind `Math`): display style, centered
  on the measure, `ragged`, participates in labels/`@ref` via the resolver's
  existing equation counter hooks (document-model §5 already reserves it).

## 10. Syntax and pipeline placement

Grammar per v2 §13 (calculator infix, `/` fractions binding tighter than
relations, `^`/`_` scripts, three-tier shorthands, `AA…ZZ` blackboard, `!`
negation, greedy big operators until a Rel/Close/end). Two placement
decisions:

- **Math parses engine-side at emission, not in JS**: `mathinline{src}` /
  `mathblock{src}` nodes carry the raw source through the ops contract
  unchanged (kinds and args already exist). The math parser is a hand-rolled
  Pratt parser in `engine/src/math/` — consistent with the linepass/inline
  house style; PackCC stays optional for later (v2 §15 already scoped it
  down once).
- The operator dictionary resolves names → (codepoint, class, flags
  stretchy/largeOp) at parse; unknown name → `error` box + diagnostic, never
  a crash.

### 10.1 As built: the IR and the row registry (plan P1-24)

`engine/src/math/ir.{h,cc}`: a formula parses to one tree of a closed node
set (`Sym Num Text Run Attach Frac Group BigOp Call Param Error`), with every
construct a `Call` of a **MathRow**: the C++ primitives `frac`, `stack`,
`radical`, `lr` (fenced, stretched to its body), `accent`, `rule`, and the
template rows of `engine/data/math/stdlib.tsv` (`sqrt root abs norm floor
ceil binom overline underline bar` and the accents), written in the template
language a document will use (`abs(x) = lr(|, #x, |)`; a symbol slot takes
one token) and expanded at bind. One validator, `checkRow`, checks them (the
unit tests run it over every row). Layout switches over the primitives only:
no family name is known below the parser.

- A call binds only on an adjacent `name(`; otherwise a row's `bare`
  meaning applies (`dot` → ⋅ Bin, `hat` → ˆ), else the dictionary, else the
  implicit-name rule (`sqrt x`, `abs` are names). `not` stays ¬.
- Arity comes from the row's slots: a missing argument is an empty Error
  leaf, extra ones one Error leaf after the call, both `math-arity`
  warnings.
- Errors are local: a malformed stretch (`^2` with no operand, a stray
  `)`) is an **Error leaf** — its source slice, resynchronised at `,` `)`
  `;` a relation or the end, set in the text font (measured like names) —
  and the rest of the formula lays out. Each problem is a diagnostic on its
  sub-span (at most 8 per formula); `math-coverage` names each uncovered
  code point once, in hex.
- Primes and an explicit superscript merge: `f'^2` = f^{′2}.
- `tsrc --stage=mathir` prints the tree and each formula's diagnostics
  (goldened for every math-bearing fixture).

### 10.2 As built: formulas as values, holes, declarations (plan P2-15)

Design T8 S8 (MathValue, MathEnv), D-L13, D-M02.

- **On the wire** a formula is one kind, `math{display?, label?}` (since
  12), whose kids are `mathsrc{src}` fragments — one per source line,
  container prefixes stripped, each with its own SPAN, its text as written
  (the math lexer decodes `\$` and `\#`) — and **holes**. Instantiation
  maps it to the engine's two level forms, `mathblock` (display) and
  `mathinline`, keeping the kids: the level system stays per kind, and an
  old buffer's `mathinline{src}` / `mathblock{src}` still reads.
- **Holes** in a `$…$` island (D-L13): `#ident` (letters, then letters or
  digits; a `.`, `(`, `[` or `;` after it is formula text) and `#(expr)`.
  Each is a splice in its own frame: a throw is an error hole, an error leaf
  `⚠ message` inside the formula. Its value (`__rt.std.mathHole`): a number
  is a math value (its digits; an exponent as `m times 10^(e)`), a string
  text, content itself. A `#` that starts neither is itself (info
  `math-hash`; write `\#`).
- **The formula source** (`math/env.h` `mathSource`): the fragments joined
  with `\n` where adjacent, each hole a delimited unit — a math value
  `\x01…\x02`, a string `\x03…\x04`, an error `\x05…\x06` — with a map
  placing every offset in its fragment (sub-span diagnostics of a
  multi-line island in a quote land on its line) and the copy text (the
  source as written: data-src, the semantic page). A math value is
  **parse-isolated**: its brackets and names cannot reach the formula; it is
  one unit as an operand or a script's base (`x^#n` with n = −3 is
  x^{−3}), otherwise its atoms join the run (TeX macro semantics). Other
  inline content in a hole is set as its text (info `math-hole-kind`;
  content in formulas is P4's), a block an error leaf.
- **Declarations** (MathEnv, D-M02): `$.math.symbol(name, {char, class,
  claimCp})`, `$.math.op(name, {limits})`, `$.math.fn(name, [params],
  body, {bare})` are positional DECLs (math.symbol / op / fn). Instantiation
  stamps every node with its **declEpoch** — the positional declarations
  whose flow index is at most its EMIT's — and a formula binds names against
  the rows in force at that epoch, so a footnote's formula, moved to the end
  by the resolver, binds as of where it was written. A symbol row is a
  name for its character and class; `claimCp` gives a typed character that
  class too. An op row is an upright name (`limits: display` puts scripts
  under and over in display style). A fn row is a template in the template
  language (`'#a simeq #b'`; the body is data — a JS function is a
  TypeError), checked by `checkRow` and bound against the declarations
  before it. Shadowing a built-in is info `math-shadow`; `std.name` always
  reaches the built-in (`std.` names are reserved); a malformed declaration
  is `math-decl` and ignored.
- **Names**: a dotted name (`std.frac`, a declared `arrow.long`) is one word
  when it names something; otherwise `.` is a decimal point or punctuation.
- **JS**: `` math`x^${n}` `` (the raw literal parts are fragments, each
  `${}` a hole), `math(src, {display, label})`, `math.sym('⟨' | 'alpha')`,
  `math.call(name, ...args)` (the arguments are holes). `mathinline(src)`
  and `mathblock(src, label)` remain as deprecated forms. `math.equations`
  is P3-29.

## 11. Testing

- **Native goldens carry the whole weight**: `--stage=mathbox` (indented box
  dump: kind, class, w/asc/desc, dx/dy, text) + the existing tree/blocks/
  layout/html stages for integration. Deterministic by construction — no
  mock needed, the artifact IS the metrics.
- Corpus: Typst's `tests/suite/math/*.typ` cases become harvestable once the
  `$…$` reject is lifted for the shared-syntax subset (small: our syntax
  diverges from Typst's, so translation is per-construct opt-in).
- e2e: baseline-alignment audit (math box baseline vs adjacent text baseline
  within ε), plus the existing right-edge/line-integrity audits over
  math-bearing fixtures; one visual specimen page per milestone. As built
  (plan P1-23): `math: font, glyph coverage and baseline audit` — the one
  declared math face is loaded, every glyph span is set in it with kerning
  off and covered by it, U+0020 is covered, and every inline formula's
  baseline lies within 0.5px of its line's text baseline.

## 12. Milestones inside M7

- **M7a — artifact + box core**: mathc.py, euler_math.h, MathBox, runs,
  glue/demotion, scripts, fractions; `--stage=mathbox` goldens; inline
  rendering + baseline integration.
- **M7b — stretch machinery**: variant chains, assemblies, delimiters,
  radicals, big operators + limits; display `mathblock` FlowUnit.
- **M7c — syntax completeness**: full shorthand tiers, operator dictionary
  artifact, accents, `@ref`-able equations.
- **M7d — polish**: font subsetting, specimen, corpus opt-ins, STIX swap
  test, a11y pass decision.

## 13. As-built deltas (M7 completion)

- **Factorial**: postfix `!` folds into the preceding atom (`n!/2` has an
  `n!` numerator); `!=`-style negations stay lexer sequences.
- **`inf`** is the infimum text operator; ∞ is `oo`/`infty`/`infinity`
  (v1 listed `inf` as ∞ — collision, recorded deviation).
- **v2 §13's dotted codex names** (`arrow.r`, `subset.eq`) are not
  delivered: names are the flat ones of `symbols.tsv`. Classes are now
  checked against MathML Core (plan P1-22); 54 rows are recorded overrides
  (text operators, the prime and the Typst-style ASCII keys).
- **Groups always survive parsing** (no atom splicing): a group box carries
  firstCls=Open/lastCls=Close, so neighbour glue and demotion are unchanged
  while delimiters stretch adaptively (natural glyph until the content
  outgrows it; §7 target with 10% short_fall).
- **Segmentation demotion preview**: inline break segmentation simulates
  Rules 5–6 over top-level effective classes before cutting, so unary minus
  never opens a break point; segments lay out with edge-aware assembly
  (a segment-final Bin stays a Bin).
- **Equation numbers render at the right margin** (`.tsr-eqno`, synthetic,
  skipped by copy/audits); only labelled display formulas number; @ref
  displays cfg.supEquation + "(n)".
- **禁则 extends to formulas**: no break between an inline formula and a
  following closing punct; quarter-em boundary glue on CJK–formula seams.
- **Copy across segments**: a split formula carries its `$src$` on the
  first segment only; later segments contribute empty data-src.
- **Accents are SPACING glyphs; `bar` is a rule.** Firefox and Chromium
  disagree on isolated combining-mark placement (different shaping
  fallbacks), so the dictionary maps hat/tilde/dot/… to their spacing
  forms (U+02C6, U+02DC, U+02D9, …) — origin-anchored in every browser,
  TopAccentAttachment present in Euler for all of them. `bar` (and the
  `overline`/`underline` calls) render as Overbar*/Underbar* RULE
  constructs — plain boxes, shaping-independent, the user-preferred form.
  `vec` (U+20D7) has no spacing equivalent and stays combining: the one
  shaping-dependent accent left (Firefox may place it differently).
- **STIX swap test: negative result, gate works.** mathc.py over STIX Two
  Math 2.13 FAILS the §1 gating check — all ~500 of its variant/assembly
  glyphs (`parenleft.s1`, `uni222B.dsp`, …) are unencoded. Euler-Math's
  cmapped variants are the exception, not the rule; the codepoint-painting
  render path is therefore a real constraint on font choice, and the gate
  rejects unusable fonts at compile time instead of shipping tofu. A future
  font swap needs either a font with encoded variants or a build step that
  injects PUA cmap entries for the referenced glyphs (fontTools can; noted
  as the designated escape hatch).
- **Deferred**: cut-in kerning (MathKernInfo absent in Euler-Math), wide
  horizontal accents/over-braces (52 horiz chains unused so far), corpus
  math opt-ins, MathML/a11y output (semantic fallback emits source text).

## 14. Text-font runs — names, operators, quoted text (as built 2026-08)

The real-world corpus (HoTT) showed formulas full of multi-letter names:
`Id_U(A,B) → Equiv(A,B)`. Set letter-by-letter in Euler they read as
products of variables. TeX sets `\operatorname{Id}` as one upright roman
word with \mathop spacing; in the Euler tradition (Concrete Mathematics)
variables are Euler cursive and names are the *text* roman. Neo-Euler has
one alphabet only (its math-italic letters are the same glyphs as the
upright ones), so the contrast has to come from the document's body font.

Rules (math.cc `parseWord`, Typst's convention):
- a word of ≥2 letters that is not a dictionary symbol/function is a
  **name**: one `Text` node, class Op (thin space before an Ord, none
  before `(`), scripts bind to the whole word — `Id_(U)(A,B)`;
- single letters stay variables in the math font;
- named operators (`sin`, `lim`, …) and `"quoted text"` are text runs too
  (`"…"` is new: Ord class, spaces preserved).

Layout: a `MeasureNeeds` (metrics, style table, interner, base size, and
where to record what is missing; plan P1-20 replaced `MathTextCtx`) rides
into `layoutMathSegments`/`layoutMathFormula`. A text run needs the body
font's width and vertical metrics at the style's size (`Styling{sizeMul}`
→ StyleId). **Lazy layout** (plan P1-25; design T8 S6): emit only
prepares a formula — it parses (the parse diagnostics are the block's) and
leaves a pending object: an inline formula one placeholder part, a display
formula a `MathData` without a box. Measure finalizes it through the
pending-object hook of `resolveWidths` (the object table's finalizer; the
loop never names math): once the text runs it needs are measured, the
formula lays out, an inline one's parts are spliced into its list, and its
layout diagnostics (coverage) are reported once, on that pass. Nothing
upstream of Measure reads the metric store, and no block is ever emitted
again for a formula. A code point the math font does not cover is a
measured text leaf (warned once per code point), never a stand-in box. A
formula paints in its run's style: its colour, inside its link. Text
leaves carry the text font (`MathBox::font`); the renderer paints them as `.tsr-mg.tsr-mt`
(`font-family: inherit; font-style: normal`) with the line box sized from
the measured ascent/descent so the baseline lands exactly. Native goldens
use the mock measurer like every other word.

Not done: bold/sans/calligraphic alphabets (Euler-Math does carry bold and
sans alphabets; the vocabulary has no names for them yet — T8 adds the
variant map), and text runs inside radicals/fractions are fine
but inherit the math style size only — no separate text-style scaling.
