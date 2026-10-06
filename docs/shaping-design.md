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
| `isWide(cp)` | `isCjk` (support.h) | emit's CJK/Latin split, snap runs (typeset_html), grid breaks |
| `isPunctGlyph`, `isOpenPunct`, `isClosePunct` | `isPunctOpen`, `isPunctClose` | emit's punctuation glyphs (P4-05: the punct column, its blanks), grid break rules |
| `noStart(cp)` | — | P4-05: 禁则 — never at a line start (closers, stops, small kana, iteration marks, ー, 〜, ・) |
| `takesAutospace(cp)`, `ambWide(cp)` | — | P4-05: CJK–Latin glue beside it (Han, kana — not Hangul, not U+3000); evidence that sets an ambiguous neighbour CJK |
| `isIdeo(cp)` | `isCjkIdeo` | a CJK letter (wide, not a punctuation glyph) |
| `joinsWide(cp)` | inline.cc's `cjkish` | the joining classes (P2-10); since P4-02 read through `joinsWithoutSpace(prev, prevWide, next, nextWide)` — a soft break between two joining characters (an ambiguous quote joins when its context sets it wide) is nothing, else a space (model/softbreak.h) |
| `kernEligible(cp)` | emit's `isCjk(cp) \|\| cp >= 0x2000` | cross-space kerning contexts (P4-05: the kern column — letters and narrow punctuation) |
| `eawWide(cp)` | — | P4-05: a code grid's two-column characters (UAX #11 W, F) |
| `isAmbDashOrEllipsis`, `isAmbQuote` | emit's U+2014/U+2026 and curly-quote literals | the em dash / ellipsis and Latin-context quote rules |
| `cpInfo(cp)` | — | class + UAX #29 grapheme break + Extended_Pictographic + UAX #11 width; `clusterEnd(s, i)` — the extended grapheme cluster (GB3–GB13), what the shaper iterates (P4-02) |

A character's class (`CC`, `engine/rules/classes.def`: Alpha … Ideo, Kana,
OpenW … EllipsisW, OpenN … BreakBefore, the ambiguous quotes / dash /
ellipsis / middle dot, spaces and controls) is one per cluster base;
everything else is a *column* of the class (wide, punct, open, close,
nostart, autospace, ambwide, joins, kern; its blanks).

## 2. The tables

`tools/ucdc.mjs` (run by gen-all, checked by G9) compiles the engine's rules,
`engine/rules/locale/default.def` (plan P4-05: RULES_VERSION 1, the chain
und ← en ← zh-Hans), with the pinned UCD files (`engine/rules/ucd/17.0.0`:
LineBreak, EastAsianWidth, Scripts, emoji-data, GraphemeBreakProperty;
`--fetch` re-downloads them) into `engine/gen/textrules.h`:
- `und.def` derives the classes from UAX #14 (`LB(lb…, Class)`: letters,
  digits, narrow punctuation, Hangul, the spaces and break controls) and
  defines the columns;
- `en.def` is the Latin section (no tailoring yet);
- `zh-Hans.def` is the Han section: compat's wide ranges (`WIDE`, default
  Ideo), kana and Hangul by script within them (`SCRIPT`), the clreq
  punctuation (`CLASS`), and the coverage compat lacked — 〖〗｟｠, small
  kana (`CLASS_LB(CJ, …)`), ー, iteration marks, ・, 〜, U+3000 —, the
  punctuation blanks (`BLANK`: each class's leading and trailing blank, em
  → `kBlanks`), the defined advances (`ADVANCE`: —— 2em, — 1em, …… 2em,
  … 1em → `kDefinedAdvances`) and the constants (`cjkBoundaryEm`,
  `superRaiseEm`).

The packs tailor different scripts, so one table serves; a pack that
tailors a script another pack does (ja, zh-Hant, ko) is the point where the
table becomes per language. The (class, kern) table is two-level —
`kCCIndex[cp >> 7]` names one of ≈230 deduplicated blocks of 128, ~38 KB,
two loads per lookup (RULES_VERSION 1 has thousands of ranges); the UCD
columns stay ranges (≈2,500, read by the cluster iterator's slow path and
the grid). `engine/rules/locale/compat.def` (RULES_VERSION 0) stays as the
reference rules-diff compares with.

## 3. The mock measurer

`measure/mock.h`'s wide ranges are a frozen literal (`mockIsWide`), pinned
by the same test: golden metrics never move when the classes do.

## 4. Changing the rules

`tools/rules-diff.mjs --b <rules.def> [--corpus] [--allow f] [--check]`
compares a rules version with compat: the codepoints whose behaviour — the
columns the engine reads, the blanks, the ambiguous or control class, not
the class name — changes (as ranges) and, with `--corpus`, every fixture /
real-world boundary whose pair changes. `test/golden/RULES` records the
rules the goldens were made with (RULES_VERSION 1, UCD 17.0.0) and the
allowlist of changes from compat (`U+X[..U+Y] [kern]`: any change, or the
kerning only); `--check` fails on anything else, and `unitTextRules` pins
every codepoint the allowlist does not name to compat's literal predicates.

## 5. The item list (`engine/src/shape/hlist.h`, plan P1-12)

Every inline stream — a paragraph, a heading, a table cell, a caption row,
a sidecar line — is an **HList**: TeX items that say what they are, run
instances that say how their boxes paint, and side records.

| item | what | today from |
|---|---|---|
| Box | a word, inline code, a CJK char (LetterSpaced), a defined-width dash/ellipsis (Pinned), a punctuation glyph (BlankBearing), a formula part (Object), the paragraph indent (Pinned, syn indent) | `cls` = the CC of its first codepoint |
| Glue Word | a typed space (`IA_SourceSpace`), KernCtx when it sits between two words of one shaping run (below) | weight 1, capacity = width |
| Glue InterChar | the gap after a CJK char whose next item is a CJK char or a closing glyph (the topology layout and paint used) | weight `cjk.justifyK`; shares its char's cold record |
| Glue Autospace | CJK–Latin and CJK–formula boundary space | weight 1 |
| Glue Blank | a punctuation glyph's half em (`IA_OwnedByNext` for an opening glyph's leading half) | weight 0 |
| Glue ObjectSpace | the space between two formula parts | weight 0 |
| Glue Fill | a `fill` (plan P2-16): fil glue — no width, no finite stretch; a line holding one is fil for the breaker, and layout gives it the line's whole slack (on any line, the last too); painted as a spacer (`data-syn="fill"`) | weight 0 |
| Penalty | a numeric break penalty (`kPenInf` forbids) | |
| Disc | a break inside a word: its pre box in `side` — the dictionary's hyphen at a pattern or soft-hyphen point; none after an explicit hyphen or at an emergency break (plan P4-06) —, the junction KernCtx in `DiscRec::spec`, the unbroken width = the junction kern | |

The 24-byte `HItem` holds kind, class, attrs, state, run, aux (the
`AdvanceSpec` or `DiscRec`), width, x (glue weight; penalty; a CJK box's gap
weight) and its `ColdRec` (source span, raw px, resolved blanks, the
migration capacity `capSu`, an anchor). `AdvanceSpec` (32 bytes) says how
`resolveWidths` sizes it: Measured, Defined/Fixed em, MeasuredMinusBlanks,
KernCtx (`m(prev+str+next) − m(prev) − m(next)`), Object.

**Legality** is TeX's (the header states it): glue breaks only after a Box or
Disc, a penalty below `kPenInf` breaks, a Disc breaks, a Box never. Until the
item-native breaker (P4-08) the emitter writes the break structure the
legacy breaker's lowering reads in this form: a box that may break after it
gets `Penalty(p)` right after it (no penalty when InterChar glue follows:
that glue is the break, or `Penalty(INF)` before it when the char may not
break), a glue whose own penalty is not 0 gets it right before it.
`lintHList` checks every golden and the corpus: at most one legal
breakpoint per boundary, none after an opening or before a closing glyph,
runs numbered in order and contiguous, BlankBearing/Pinned/Object runs of
one box, every non-final box of a LetterSpaced run followed by InterChar
glue. The typst corpus had two documents (`！ ？` with a typed space) where
the emitter allowed a break before the closing glyph — the UAX #14 LB13
"even after spaces" case; since P4-02 the typed spaces before a closing
glyph, and the box before them, do not break, and the corpus run lints them.

**Run instances** form as the items arrive: consecutive boxes share a run
while (face, link, SynKind, copy policy, RealizeClass, error) agree; glyphs, pinned
boxes, objects, the indent and spacer glue are runs of their own; a blank
joins its glyph's run (a leading blank opens it); penalties and InterChar
glue take their owner's. Paint opens a DOM run exactly where the run
changes.

As built (plan P4-01; design T5 step 4):
- **Anchors are points.** A labelled reference (a footnote marker) names
  its anchor before its text is emitted; its first Box (or Disc) takes
  `IA_Anchor` and opens a run that carries it (`RunRec::anchor`), and what
  follows with the same key joins it — paint writes the id once, where the
  run starts, never on a later line's fragment. An index entry's empty
  anchor box stays a run of its own. `lintHList` checks that an anchor
  opens its run and that the run carries it.
- **Inline code is Rigid**: its spaces are inside its one box, measured as
  written, so paint writes `word-spacing:0` on the run when the line is
  justified and the text has a word separator (CSS Text §8.1) — whatever
  class the theme gives code.
- **Junction kerns follow shaping runs, not DOM runs.** A KernCtx (word
  glue, a hyphen point's junction) sits between two text boxes (Plain or
  Rigid) whose styles — and the space's — have one `FaceStyle`
  (`measure/face.h`: the Styling fields the face is made of, which
  `FaceTable::faceOf` reads through the same function). Measured in
  Chromium and Firefox (DejaVu Serif, "AV" 56.0px vs 58.0px apart): both
  kern across `<span>`, `<a>` and colour boundaries in one font, and
  neither across a font change, letter-spacing or an inline block. So a
  link's last letter and the space after it kern (the P2-07 audit XFAIL
  semantics/appendix passes), a citation's `[1, 2]` kerns like prose; an
  italic title against roman text still does not (real-world-report #1).
  Eligibility is the kern column (P4-05). Every break inside a word — a
  pattern point, an explicit hyphen's, an emergency break in a URL — is a
  Disc, so its junction is kerned like a hyphen point's (P4-06).
- `ICtx::synKind` (SynKind::Ref inside a resolver reference) replaces the
  `BF_REF` flag bit the run key read; the legacy oracle maps it back.

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

**The equivalence check.** `emit/legacy.cc` kept the pre-HList inline
emitter verbatim; the golden runner and `tsrc --fuse-check` compared
`fuseLegacy` with it field by field — every fixture, and the
real-world/typst/blog corpora (650 documents) were checked when the step
landed. It went with the paragraph shaper (P4-02), whose typography differs
from it by design; `tsrc --lint` keeps the legality lint, and the corpus run
(`tools/corpus-run.mjs`) lints every typst document.

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
lists the object table. Since the paragraph shaper (P4-02) the break after
an object is its neighbour's (below, §7): never before a closer, never
between a formula and Latin text or code glued to it; CJK autospace goes
beside formulas and inline code (Latin-class), not images or raw marks.

**Two phases for formulas.** Since plan P1-25 emit never lays a formula
out: it keeps a single placeholder part and flags the list (`hasDeferred`),
and `resolveWidths` finalizes every pending object through the object
table's finalizer once the formula's text-font runs are measured, splicing
its parts in place of the placeholder — the same items, with the runs
renumbered (the fuse check covers it: the legacy oracle still lays out at
emit). A display formula's box is made the same way.

**Hard breaks.** A `hardbreak` node (plan P3-33: `\` at the end of a line,
or `#linebreak`) makes the break
after the preceding item forced. The lowering keeps it as a block penalty of
`-BREAK_INF`, the breaker adapter turns that into `Penalty(Forced)`, and the
line before it ends ragged — fil stretch in the breaker (TeX's
`\hfil\break`), no justification and a real line boundary for copy in
layout. Fixtures for vocabulary without syntax declare their tree in
`X.tree.json`, which `tools/record-fixtures.mjs` encodes with the runtime's
OpBuf.

## 7. The paragraph shaper (plan P4-02; design T5 step 5)

A unit's inline content — a paragraph, a heading, a cell, a caption row —
is shaped as one paragraph. The emitter's `walk` and `indent` only record;
`finish` flattens the records into the **paragraph context**
(`shape/context.h`): one entry per grapheme cluster (`clusterEnd`) of every
text node in reading order, across style, link, reference and error edges;
inline code and formulas (and an error box) as one Narrow entry each — Latin
evidence —, images, raw marks, hard breaks and fills as Opaque, spaces and
tabs as Blank. `resolveContext` settles the ambiguous marks once:
- the em dash and the ellipsis are CJK (a defined-width box) as the run's
  language says (P3-30), else when doubled or beside a CJK character or
  wide punctuation;
- U+2019 between letters is an apostrophe (Latin, in its word);
- a curly quote is CJK punctuation as the run's language says, else when
  the character before it is CJK or wide punctuation (`他说：“Hello”`: the
  ： is evidence, finding emitter/missed:4) or a CJK character or
  punctuation follows it; a matched pair (`“ ”`, `‘ ’`) resolves jointly —
  either quote's evidence sets both, so a pair never splits between two
  fonts (`，“*强调*”。`). A blank gives no evidence: a quote set off by
  spaces stays Latin, and no document language decides one that has none.

The emission then replays the records with that context. A text node starts
from what precedes it in the paragraph (a CJK character in another node, a
Latin word, a formula, code) instead of a blank state, so markup never
changes typography (finding emitter/paragraph-blind-script-context):
- CJK–Latin boundary glue (0.25em, App C) goes at every script edge —
  `中文*English*中文`, `中文[链接](…)`, `中文`code`中文`, a reference's
  `(1)` before CJK — except beside a raised or lowered mark (a note's
  reference digit hugs the text on both sides) and at an `attach` edge (the
  glue would be the break the attach forbids);
- the break after an inline object is its neighbour's (finding
  emitter/missed:1): never before a closer (CJK, or `, . ; : ! ? ) ] } %`
  and quotes), never between a formula and Latin text or code glued to it
  (`$x$th`, `$f$(`: UAX #14 AL × AL, AL × OP); before a CJK character, an
  opening glyph, a blank or an image it may break;
- before a closing CJK glyph no break, even after typed spaces (UAX #14
  LB13);
- a long token's length counts its clusters — not bytes — across style
  edges (`abc.def/*ghij*/klmn.opq/rst` is one token); its breaks stay
  inside each node's text (§10);
- a CJK box is a whole cluster (an ideograph with its variation selector).

**Soft breaks** (`model/softbreak.{h,cc}`): instantiation leaves U+000A in
inline-model text and gives a mapped text an explicit cooked→raw map; right
after the normal form a pass walks the tree's inline streams (a block's
inline content through its containers; code and verbatim bodies are
evidence, notes and errors streams of their own), builds the same paragraph
context with each soft break a Blank, resolves it and rewrites each break
with `joinsWithoutSpace` of its two neighbours: `这是*强调*⏎中文`,
`他说“好”⏎然后` and `中文结尾⏎“引号”` join; the map is rebuilt as the
parser builds one. The tree, the semantic page, titles and emit read the
same text.

**Per-item spans** (plan P4-03; design T5 step 6). Every item's `ColdRec`
span is its own: emit maps a cluster's cooked bytes through its text node's
cooked→raw map (`TextSource`, the identity without one) when the node is its
own source (`srcExact`, set at instantiation, cleared when materialize
moves a node). A byte the parser inserted (the space after a reference) has
no extent; a byte it removed (a joined line's newline) belongs to neither
neighbour. Word pieces — hyphenation segments, long-token cuts — get their
slices; a hyphen and a boundary glue are points; a punctuation glyph's
blanks share its span. Made text keeps its node's span. Paint takes a run's
`data-s` from its first item with a source, layout a line's span from its
first such item to the furthest end.

## 8. Text properties, blanks and defined advances as data (plan P4-04; design T5 step 7)

- **Blanks.** A punctuation glyph is measured less its class's blanks
  (`blankOf`, em) plus the word epsilon; each blank stands as Blank glue.
  One rule compresses them, the run's `text.punct` (else
  `cjk.punctCompress`): where the previous glyph's trailing blank and this
  one's leading blank meet, none keeps both, book keeps one (the previous
  glyph's — the break between the two), full keeps neither; a leading blank
  right after a glyph that kept none (an opener) is solid (book, full) or
  rigid (none); a trailing blank before a glyph with no leading blank
  (closer + closer) is dropped (book, full) or kept without a break (none).
  Paint squeezes a side whose own blank does not stand with an explicit px
  margin (`margin-left`/`margin-right`: the blank's px) — the contract CSS
  no longer carries `tsr-sqL`/`tsr-sqR`, nor any engine number.
- **Defined advances.** A cluster the context sets wide where an ADVANCE
  sequence starts is one pinned box of that width; any sequence the rules
  list opts in (no code names U+2014 or U+2026).
- **Superscripts** are raised by the engine's px (`top`, `superRaiseEm` of
  the run's em); `.tsr-sup` keeps position and decoration only.
- **Text properties** (schema rows, the run's style, the cascade):
  `text.wrap` (wrap | nowrap — CSS text-wrap-mode: a nowrap text breaks
  nowhere inside), `text.space` (normal | pre — white-space-collapse: pre
  keeps its spaces in rigid boxes; normal collapses a run of spaces, as the
  browser does — a spliced string's too), `text.autospace` (none | normal:
  the boundary glue at its edges), `text.hyphens` (none | manual | auto)
  and `text.overflowWrap` (normal | separators | anywhere): §10.
- **The punctuation matrix** (`test/e2e/punct.spec.mjs`, setting
  `render.runWidths`): every combination of the classes in the three modes,
  justified and ragged, at four device pixel ratios — each run's rendered
  width within 1px of the engine's (`data-w`); before and after the change
  at most 0.016px.

## 9. UCD-derived classes and the break controls (plan P4-05; design T5 step 8)

The shaper reads the new columns and classes (findings
emitter/hardcoded-script-class-tables, emitter/missed:2):
- **Non-starters** (`nostart`: closers and stops, small kana, iteration
  marks, ー, 〜, ・) never begin a line: no break before them, nor at the
  spaces or the boundary glue before them; a non-starter's leading blank
  (・'s quarter em) does not break. The code grid's wrap and `lintHList`
  read the same column.
- **Hangul** is a CJK box (it breaks between syllables) that takes no
  CJK–Latin glue, sets its neighbours' quotes and dashes Latin, and keeps
  a source line break as a space; U+3000 is a CJK box without glue.
- **Break controls in plain text** (UAX #14): U+00A0 / U+2007 are spaces
  that stretch and never break (nor collapse); U+202F stays in its word;
  U+200B is a break and nothing else; U+2060 / U+FEFF forbid the break
  around them (inside a word the word stays one); U+00AD marks the word's
  only hyphenation points (a hyphen where the line breaks, nothing where
  it does not).
- **Kerning across a space** is the kern column: letters and narrow
  punctuation, curly quotes and dashes included — not CJK, marks or spaces.
- **A code grid's widths** are UAX #11's: wide and fullwidth characters
  take two columns.
- The goldens move only in the hlist's class names (Other → Alpha, Digit,
  Infix, …) and kern contexts beside quotes and dashes (the mock measures
  them at the space's width); `cjk/controls` covers the rest.

## 10. Hyphenation and emergency breaks (plan P4-06; design T5 step 9)

The breaks inside a word are the token pass's (`emitWord`), each a Disc
(findings emitter/hyphenation-en-us-only, emitter/url-break-special-path):
- **Two properties, no block flag.** `text.hyphens` (none | manual | auto)
  says where a word may hyphenate: nowhere, at its soft hyphens, or at its
  dictionary's points too; unset, the block's `par.hyphenate` decides (auto
  or manual). `text.overflowWrap` (normal | separators | anywhere; unset:
  separators) says how a long token with no other break opportunity
  breaks. The role stylesheet (`engine/data/defaults.json`) gives headings
  `hyphens: manual` and headings, captions and inline code
  `overflowWrap: separators`: a heading's URL wraps instead of running
  off, a long identifier in code breaks at its separators. (`ICtx::noHyphen`,
  which conflated the two, is gone.)
- **A word** is its core from its first letter (UCD General_Category L*)
  to its last, of letters, digits, apostrophes and hyphens. An explicit
  hyphen (U+002D, U+2010) between letters is a break that adds nothing
  (ExHyphen, D-X02: `break.exHyphenPenalty`, TeX's \exhyphenpenalty —
  equal to the hyphen penalty) when its pieces keep the dictionary's
  minima (`e-mail`, `X-ray` stay whole). Under hyphens auto each part — a
  run of letters — takes the points of the run's language's dictionary
  when all its letters (lower-cased: UnicodeData's simple mapping) are in
  the dictionary's alphabet and it is as long as its minimum: compounds
  hyphenate in their parts (`Ad-di-son-Wes-ley`), `content's` before its
  apostrophe, and `Übersetzung` not by en-US.
- **Dictionaries** (`hyphen/hyphen.{h,cc}`): Liang's patterns as TeX
  writes them, compiled into one trie form — the resident en-US
  (`engine/gen/hyphen_en_us.h`, from tools/hyphc.mjs) and any other
  language through the `hyphPatterns` resource row (D-X09): patterns,
  exceptions, minima, hyphen glyph. A language whose words hyphenate
  (its locale pack), that is not en/en-US/und and is not written in a CJK
  script asks once; Emit waits for the answer (a dictionary never arrives
  mid-document), the Session keeps it — a host without one too. Without
  one the language falls back along its BCP-47 chain to en-US (en-GB, info)
  or hyphenates nowhere (warning `hyph-unavailable`). The runtime's
  provider serves `runtime/assets/hyph` (tools/hyphc.mjs --assets, every
  language of the `hyphen` package; index.json maps tags to files); the
  goldens' is `test/hyph` (German). A point's Disc carries the
  dictionary's hyphen glyph, painted as its pre (`data-syn="hyphen"`).
- **Emergency breaks** (the emergency table, `und.def` EMERGENCY rows —
  the Chicago Manual's URL rule): after a colon or `//`; before `/ ~ . , -
  _ ? # %` and `@`; on either side of `=` and `&`; never inside a leading
  `scheme://`; each piece at least `emergencyMinPiece` (3) clusters. They
  apply to a token of `break.urlMinLen` clusters or more that has no other
  break (`https://` | `example` | `.com` | `/a/very` …); under
  `overflowWrap: anywhere` any cluster boundary is one too. A pre run
  (inline code) takes only these.

## 11. Next steps

P4-07, P4-08: attach edges and the item-native breaker (with it, the
canonical TeX form and the end of the lowering).
