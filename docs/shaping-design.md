# Text shaping: TextRules, the item list, inline objects and the shaping pipeline (design; as built from plans P1-11…P1-13)

How the engine decides what a character *is* for line breaking, spacing,
fonts and kerning. Design source: `docs/remediation/design/T5-text-shaping.md`
(TextRules, HList, InlineObject; steps 1 → P1-11, 2 → P1-12, 3 → P1-13, 4–11 → P4-01…P4-08).
Decision D-X04: Unicode is pinned at 17.0.0 (Node's ICU 78 has the same
version, so the JS side classifies alike); `RULES_VERSION` rises only on
purpose, with the goldens re-recorded.

## 1. One classifier (`engine/src/shape/textrules.h`)

Every script-, punctuation- or width-dependent decision asks one API instead
of carrying its own ranges:

| API | was | used by |
|---|---|---|
| `isWide(cp)` | `isCjk` (support.h) | emit's CJK/Latin split, grid columns (layout), snap runs (typeset_html) |
| `isOpenPunct`, `isClosePunct` | `isPunctOpen`, `isPunctClose` | emit's punctuation blocks, grid break rules |
| `isIdeo(cp)` | `isCjkIdeo` | emit (solid 1em blocks, formula → CJK boundary) |
| `joinsWide(cp)` | inline.cc's `cjkish` | the seamless line join between CJK characters |
| `kernEligible(cp)` | emit's `isCjk(cp) \|\| cp >= 0x2000` | cross-space kerning contexts |
| `isAmbDashOrEllipsis`, `isAmbQuote` | emit's U+2014/U+2026 and curly-quote literals | the em dash / ellipsis and Latin-context quote rules |
| `cpInfo(cp)` | — | class + UAX #29 grapheme break + Extended_Pictographic + UAX #11 width (the shaper, P4-02) |

A character's class (`CC`, `engine/rules/classes.def`: Alpha … Ideo, Kana,
OpenW … EllipsisW, OpenN … BreakBefore, the ambiguous quotes / dash /
ellipsis / middle dot, spaces and controls) is one per cluster base;
everything else is a *column* of the class (today: wide, open, close,
joins; later: UAX #14 break class, blanks, autospace, font role).

## 2. The tables

`tools/ucdc.mjs` (run by gen-all, checked by G9) compiles
`engine/rules/locale/compat.def` — RULES_VERSION 0, today's classification
as data: the five wide ranges, the clreq punctuation sets, the ambiguous
classes, the columns, the kern cutoff, the App C constants (`punctHalfEm`
0.5, `cjkBoundaryEm` 0.25) — plus the pinned UCD files
(`engine/rules/ucd/17.0.0`: LineBreak, EastAsianWidth, Scripts,
emoji-data, GraphemeBreakProperty; `--fetch` re-downloads them) into
`engine/gen/textrules.h`: the class enum and columns, a range table of
(class, kern) (≈50 ranges; a lookup below U+2000 is one comparison) and one
of the UCD columns (≈380 ranges), ~3.4 KB of data.

`unitTextRules` pins the API against literal copies of the five old
classifiers over every codepoint, so RULES_VERSION 0 is today's behaviour
bit for bit.

## 3. The mock measurer

`measure/mock.h`'s wide ranges are a frozen literal (`mockIsWide`), pinned
by the same test: golden metrics never move when the classes do.

## 4. Changing the rules

`tools/rules-diff.mjs --b <rules.def> [--corpus] [--allow f] [--check]`
compares a rules version with compat: the codepoints whose class or columns
change (as ranges) and, with `--corpus`, every fixture / real-world boundary
whose class pair changes. P4-05 (RULES_VERSION 1, UCD-derived classes)
must keep compat's results for every codepoint compat classifies unless
allowlisted.

## 5. The item list (`engine/src/shape/hlist.h`, plan P1-12)

Every inline stream — a paragraph, a heading, a table cell, a caption row,
a sidecar line — is an **HList**: TeX items that say what they are, run
instances that say how their boxes paint, and side records.

| item | what | today from |
|---|---|---|
| Box | a word, inline code, a CJK char (LetterSpaced), a defined-width dash/ellipsis (Pinned), a punctuation glyph (BlankBearing), a formula part (Object), the paragraph indent (Pinned, syn indent) | `cls` = the CC of its first codepoint |
| Glue Word | a typed space (`IA_SourceSpace`), KernCtx when it sits between two words of one run | weight 1, capacity = width |
| Glue InterChar | the gap after a CJK char whose next item is a CJK char or a closing glyph (the topology layout and paint used) | weight `cjk.justifyK`; shares its char's cold record |
| Glue Autospace | CJK–Latin and CJK–formula boundary space | weight 1 |
| Glue Blank | a punctuation glyph's half em (`IA_OwnedByNext` for an opening glyph's leading half) | weight 0 |
| Glue ObjectSpace | the space between two formula parts | weight 0 |
| Penalty | a numeric break penalty (`kPenInf` forbids) | |
| Disc | a hyphenation point: its pre box (`"-"`) in `side`, the junction KernCtx in `DiscRec::spec`, the unbroken width = the junction kern | |

The 24-byte `HItem` holds kind, class, attrs, state, run, aux (the
`AdvanceSpec` or `DiscRec`), width, x (glue weight; penalty; a CJK box's gap
weight) and its `ColdRec` (source span, raw px, resolved blanks, the
migration capacity `capSu`, an anchor). `AdvanceSpec` (32 bytes) says how
`resolveWidths` sizes it: Measured, Defined/Fixed em, MeasuredMinusBlanks,
KernCtx (`m(prev+str+next) − m(prev) − m(next)`), Object.

**Legality** is TeX's (the header states it): glue breaks only after a Box or
Disc, a penalty below `kPenInf` breaks, a Disc breaks, a Box never. Until the
paragraph shaper (P4-02) the emitter writes **today's** break structure in
this form — the per-node logic is the old emitter's: a box that may break
after it gets `Penalty(p)` right after it (no penalty when InterChar glue
follows: that glue is the break, or `Penalty(INF)` before it when the char
may not break), a glue whose own penalty is not 0 gets it right before it.
`lintHList` checks every golden and the corpus: at most one legal
breakpoint per boundary, none after an opening or before a closing glyph,
runs numbered in order and contiguous, BlankBearing/Pinned/Object runs of
one box, every non-final box of a LetterSpaced run followed by InterChar
glue. The typst corpus has two documents (`！ ？` with a typed space) where
today's emitter allows a break before the closing glyph — the UAX #14 LB13
"even after spaces" case; P4-02's pair table with `spacesBetween` closes it.

**Run instances** form as the items arrive: consecutive boxes share a run
while (face, link, SynKind, copyText, RealizeClass) agree; glyphs, pinned
boxes, objects, the indent and spacer glue are runs of their own; a blank
joins its glyph's run (a leading blank opens it); penalties and InterChar
glue take their owner's. Paint opens a DOM run exactly where the run
changes; anchors are `IA_Anchor` items. Until P4-01 the key reproduces
today's DOM (inline code stays Plain; an anchor does not split a run).

**Layout and paint read the items.** A line is an item range: the breaker's
block breakpoints map through `blockStart`, leading and trailing glue and
penalties drop (TeX's discard). Natural width sums raw px (a mid-line Disc
adds its junction kern, a line-final one its hyphen), stretch sums glue
weights and capacities (InterChar = the CJK gap), the copy join is "a
source space was consumed". Paint: spacers for Autospace/ObjectSpace glue
and the indent, a Blank folds into its glyph's squeeze, a Pinned box is an
inline-block of its defined width, a LetterSpaced run takes the line's
letter-spacing (and a compensating margin when no gap follows on the line),
everything else paints its run's text.

**fuseLegacy** lowers an HList to the old blocks for the legacy breaker
until the item-native breaker (P4-08) deletes it with `capSu`: one block per
carrier (Box, Disc, non-InterChar Glue) by item kind, glue class and run
class (the table is in `emit.cc`); a penalty right before a glue is the
glue's when it is not 0 or follows another penalty, any other is the
preceding carrier's; a box defaults to "unbreakable" unless InterChar glue
follows it; InterChar folds into its char. Production keeps only what the
breaker reads (`BreakBlock`: widths, capacity, penalty, kind bits); the full
`LinebreakBlock` is built for the `blocks` dump and the check.

**The equivalence check.** `emit/legacy.cc` keeps the pre-HList inline
emitter verbatim (the block walk is shared through `InlineSink`); the golden
runner and `tsrc --fuse-check` compare `fuseLegacy` with it field by field —
every fixture, and the real-world/typst/blog corpora (650 documents) were
checked when the step landed. The oracle is native only (the WASM binaries
never reference it) and goes with the paragraph shaper (P4-02).

**Dump**: `tsrc --stage=hlist` (a golden for every typeset fixture) prints
kind, class, attrs, width, weight, numeric penalty, capacity, KernCtx, run
and source span per item, then the run table.

Performance (87K update, WASM): the item list costs ~0.8 ms of engine time
over the block stream; the step also moved the word metrics to a slot table
indexed by string (the hash lookups were a fifth of the engine) and px
formatting to integer arithmetic (render −2 ms), so the update stays at
29.5 ms.

## 6. Inline objects and the flatten table (plan P1-13)

**The flatten table** is the `inline` column of each kind in
`engine/schema/schema.json` (gen-schema rejects a kind without one; it is
generated into `KindInfo::inl`), and the emitter's inline walk switches on
it — closed, so no node vanishes silently:

| row | kinds | becomes |
|---|---|---|
| text | text | shaped text |
| container | styled, seq, group, link, ref, para | its children (link/ref set the run's link and syn; a para reaches an inline stream through a materialized term) |
| code | code | one rigid box |
| object | mathinline, image, raw | an inline object |
| break | hardbreak | a forced break (`Penalty(-INF)`) |
| error | error | breakable CODE-style text, as before |
| skip | comment | nothing |
| unsupported | every other kind | an Error object (`⚠ <kind>`) and a `shape-unsupported` warning |

The golden runner fails any fixture (other than one named for it) that
reports `shape-unsupported`, and the corpora were scanned clean when the
step landed. The semantic serializer paints inline images and raw marks the
same way instead of dropping them.

**The registry** (`engine/src/shape/objects.{h,cc}`): an `InlineObject` has
a kind, its node, edge classes (`firstCC`, `lastCC`: math and the error box
Alpha, images and raw marks Ideo), its source (formula TeX, image src, raw
markup) and its parts in `HList::parts` — each part a Box with
`AdvanceSpec::Object{part}` and extents (w, asc, desc); formula parts are
separated by ObjectSpace glue with the formula's break penalties. Layout
reads a part's asc/desc (`objectPart`, the shim over the three former
copies until P1-17); paint dispatches on the kind (formula box, `<img>`, a
`tsr-iraw` inline-block with the markup, or the error text); the hlist dump
lists the object table. Until the paragraph shaper reads the edge classes
(P4-02), objects keep the formula rules: a break is legal after one, a
closing glyph after it is kinsoku-protected, and only a formula gets
CJK autospace.

**Two phases for formulas.** Emit lays a formula out at once when the
store already has its text-font runs; otherwise it keeps a single
placeholder part and flags the list (`hasDeferred`). `resolveWidths` (given
the document's arena and styles) lays the formula out for that list alone,
adds still-missing runs to the request, and on success splices the parts in
place of the placeholder — the same items emit would have written, with the
runs renumbered (the fuse check covers both paths: the golden fixtures
defer and splice `Id_(A)` and `f(x) "if" x > 0`). Display formulas keep
the document re-emit until T8 (P3-26).

**Hard breaks.** A `hardbreak` node (no surface syntax yet) makes the break
after the preceding item forced. The lowering keeps it as a block penalty of
`-BREAK_INF`, the breaker adapter turns that into `Penalty(Forced)`, and the
line before it ends ragged — fil stretch in the breaker (TeX's
`\hfil\break`), no justification and a real line boundary for copy in
layout. Fixtures for vocabulary without syntax declare their tree in
`X.tree.json`, which `tools/record-fixtures.mjs` encodes with the runtime's
OpBuf.

## 7. Next steps

P4-01…P4-08:
run formation, the paragraph shaper (the boundary pass reading object edge
classes), per-item spans, TextProps and locale sections (punctuation,
blanks, autospace as data), UCD-derived classes, hyphenation registry,
attach edges and the item-native breaker.
