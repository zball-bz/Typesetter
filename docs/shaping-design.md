# Text shaping: TextRules and the shaping pipeline (design; as built from plan P1-11)

How the engine decides what a character *is* for line breaking, spacing,
fonts and kerning. Design source: `docs/remediation/design/T5-text-shaping.md`
(TextRules; steps 1 → P1-11, 2 → P1-12, 3 → P1-13, 4–11 → P4-01…P4-08).
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

## 5. Next steps

P1-12 (HList and run instances), P1-13 (inline objects), then P4-01…P4-08:
run formation, the paragraph shaper, per-item spans, TextProps and locale
sections (punctuation, blanks, autospace as data), UCD-derived classes,
hyphenation registry, attach edges and the item-native breaker.
