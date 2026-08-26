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
- Variant chains (vert + horiz): per base codepoint, the ordered
  `(codepoint, advance)` list — **codepoints, not glyph ids** (per §1.1).
- Assemblies: part lists `(codepoint, startOverlap, endOverlap, fullAdvance,
  isExtender)` + `MinConnectorOverlap`.
- A generated coverage bitmap so emission can diagnose "symbol not in math
  font" at compile time rather than rendering tofu (`measure-fallback` diag).

Estimated size: ~3.5k glyph records ≈ 60–90 KB of header — in line with the
hyphenation artifact. The operator dictionary artifact (v2 §13: codex-style
names + atom classes from the MathML Core dictionary) is generated by the
same tool run, filtered against this coverage bitmap.

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
`ScriptScriptPercentScaleDown = 50%` (Euler-Math), floored by a config
`minMathSizePx`. Cramped-ness selects constants (e.g.
`SuperscriptShiftUpCramped` 289 vs 363) but not size.

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
  `DisplayOperatorMinHeight` (1400), center on axis; limits attach per above
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
- Children paint glyphs by **codepoint** (guaranteed addressable, §1) in
  `@font-face` Euler-Math; adjacent same-style glyphs coalesce into runs
  positioned by our advances (`text-rendering: geometricPrecision`; the font
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
  math-bearing fixtures; one visual specimen page per milestone.

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
