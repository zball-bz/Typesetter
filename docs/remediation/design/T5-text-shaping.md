# T5-text-shaping

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

Line typography (spacing, kinsoku, quote width, hyphenation, emergency breaks, attachment) is a property of which grapheme clusters end up next to each other in a line, not of the content-tree nodes they came from. L5 therefore shapes each paragraph-like unit (paragraph, heading, cell, caption, sidecar line, inline box) in ONE pure pass over the resolved tree:
- flatten it through a closed, ops.def-generated handler table into one stream of grapheme-cluster atoms and inline-object edges;
- classify every atom with ONE tailorable character class (CC) from a generated Unicode table, whose break, blank, autospace and font-role behaviour are columns of per-script locale-pack sections;
- resolve every boundary with one function into an explicit TeX item list (HList: Box/Glue/Penalty/Disc) whose break legality is TeX's (glue breaks only after a non-discardable item; the pair penalty precedes the glue), so kinsoku and NBSP hold by construction and are lintable.

Each item declares, independently, its glue class, source span and run instance; each run instance declares its face, link, copy kind and realization class. Break, layout and paint read these declarations and never re-derive script rules, and paint never calls TextRules. Inline objects enter through one two-phase protocol (metric-free structure, then measured extents) with declared edge classes, so a formula, an image, a user box or a CJK character meet the same pair rules.

A new locale, break control, discretionary or inline object becomes a table row, a pack section or a registry entry, not another BF_* bit threaded through emit, break, layout and render. Migration keeps today's numbers bit-exact (float weights, double em constants, frozen mock) behind a specified legacy lowering until T6's item-native breaker can consume TeX items.

## Diagnosis

Every ad-hoc item in this theme traces to one missing product between the resolved content tree and the line breaker. Emit goes straight from tree nodes to the PoC's fused LinebreakBlock (emit.h:24-51) and decides typography while walking nodes.

(1) Context is per text node. emitText resets Prev per node (emit.cc:277-278) and its look-ahead stops at the node end (emit.cc:404, 437), so markup changes spacing, breaks and quote classes. Context is patched back retroactively: math (emit.cc:113-118, 293-299, 354), CLS_SUP (emit.cc:89-90), punctuation pop_back (emit.cc:337-361). Atoms are codepoints, so an IVS or ZWJ emoji is split into words with boundary glue (probe).

(2) The fused block mixes box, glue, penalty and realization in ten flag bits (emit.h:10-22); layout.cc:69-77, 514-521, 544-550 and typeset_html.cc:503-515, 531-590 re-derive meaning. Breaker capacity (0.1em, about 0.40 of a space) and renderer weight (k=0.6) disagree although v2 §8 says they agree by construction.

(3) Script knowledge is code: five classifiers (support.h:158-190, inline.cc:50, emit.cc:396/841, layout.cc:239-250, typeset_html.cc:402), punctuation as control flow plus a CSS em constant (shell.mjs:41-42) evaluated in a different em than emit (fontPx ignores sizePx, emit.cc:251). Styling::lang is read only by the dump (emit.cc:1094).

(4) Break opportunities come from feature knobs (config.h:39-50) and proxies (CLS_SUP, ICtx::noHyphen); Unicode controls are ignored; there is no white-space model, so line word-spacing stretches spaces inside inline code unbudgeted (probe: word-spacing:17.25px over two code spans).

(5) Objects and runs are implicit: math is a typed pointer (emit.h:44), inline image vanishes, three run predicates disagree and drop copy text next to citations, spans are per node. The parser decides CJK line joins from raw bytes (inline.cc:48-59).

## Abstractions

### HList: explicit horizontal item list with TeX legality, run instances and side records

**owner_layer**

L5 Shaping, engine/src/shape/hlist.h. One contract owned by T5; consumed by L6 break/layout (T6) and L7 paint/display list (T7). T6 owns block-level WidthSpec, lineExtents and materializeLines.

**purpose**

Replaces the fused PoC block and its flag bits. Each item says what it is; each run instance says how its boxes are realized; break legality is TeX's and is checkable on the dump. No downstream layer re-derives script semantics.

**definition**

```
enum class IK : u8 { Box, Glue, Penalty, Disc };                 // no stand-alone Kern (junctions live in Glue/Disc widths)
enum class GC : u8 { Word, InterChar, Autospace, Blank, ObjectSpace };
enum : u8 { IA_SourceSpace=1 /*typed space or soft break: copy ' '*/, IA_Anchor=2 /*first item of an anchored scope*/,
            IA_OwnedByNext=4 /*Blank belongs to the following glyph*/, IA_Displaced=8 /*Blank moved by attach: paint as spacer*/ };
struct HItem {            // 24 B hot record (I9)
  IK k; u8 cls /*GC, or CC of a text box*/; u8 attrs; u8 _;
  u32 run;                // RUN INSTANCE index (HList::runs); changes exactly where paint opens a new DOM run
  u32 aux;                // Box/Glue: AdvanceSpec index; Disc: DiscRec index
  Su w;                   // natural width, filled by resolveWidths
  float x;                // Glue: stretch weight (float, exactly today's stretchWeight); Penalty/Disc: penalty
  u32 cold; };            // ColdRec index
struct ColdRec { u32 srcStart, srcEnd /*source byte span of the item*/; double rawPx; float blankLpx, blankRpx /*resolved punct blanks*/;
                 Su capSu /*MIGRATION ONLY: today's breaker capacity, on CJK Boxes and synthetic glue; deleted in step 11*/; };
struct AdvanceSpec { enum : u8 { Measured, Defined, Fixed, MeasuredMinusBlanks, KernCtx, Object } k;
  StrRef str /*the codepoints actually painted (U+00A0 for NBSP)*/; double em /*Defined/Fixed, exact decimal from the pack*/;
  StrRef prev, next /*KernCtx: natural = m(prev+str+next) - m(prev) - m(next)*/; u32 obj, part /*Object*/; };
struct DiscRec { u32 pre, preN, post, postN /*Box ranges in HList::side*/; Su nobrW /*junction kern when unbroken, ceil-quantized*/; };
enum class RealizeClass : u8 { Plain, LetterSpaced, BlankBearing, Pinned, Rigid, Object };
struct RunRec { StyleId face /*T4 paint projection with font role composed*/; StrRef link; u8 syn /*SynKind, vocabulary owned by T7; 0 = content*/;
                StrRef copyText /*Replace*/; RealizeClass rc; StrRef anchor; };
struct HList { vector<HItem> items, side; vector<ColdRec> cold; vector<AdvanceSpec> specs; vector<DiscRec> discs;
               vector<RunRec> runs; vector<InlineObject> objs; u8 flags /*HasDeferredObject*/; };

Legality (TeX, normative):
- Glue is a legal breakpoint iff the item immediately before it is a Box or Disc. A break at glue drops it and every following Glue/Penalty up to the next Box/Disc.
- Penalty p is a legal breakpoint iff p < INF. p <= -INF forces a break (consumed only by T6's item-native breaker, step 11).
- Disc is a legal breakpoint: pre ends the line, post starts the next, otherwise nobrW is used.
- Box is never a breakpoint; adjacent Boxes are unbreakable.
Shaper emission order at a boundary is [Penalty?][Glue*]: Prohibit with glue => Penalty(INF) first; Prohibit without glue => nothing; Direct with glue => no Penalty (the first glue is the breakpoint); Direct without glue => Penalty(0); costed classes => Penalty(p) then glue. So each boundary has at most one breakpoint.
Lint (CI on every --stage=hlist golden): no legal breakpoint where the boundary class is Prohibit; at most one legal breakpoint per boundary; every RunRec homogeneous (below). ([Penalty p][Glue] is the intended form for costed classes: the glue is not a breakpoint.)

Run instances: consecutive Boxes share a run iff (face, link, syn, copyText, rc) are equal and the later Box has no IA_Anchor; BlankBearing, Pinned and Object boxes are singleton runs. Homogeneity: inside a LetterSpaced run every non-final Box is followed by InterChar glue. Paint opens a DOM run exactly when items[i].run changes (debug assertion) and emits id once, on the run that starts at the IA_Anchor item.

Paint is local inspection of resolved values (L7 never calls TextRules):
- Word glue: line word-spacing (U+0020/U+00A0 are painted as typed; Rigid runs get word-spacing:0).
- InterChar glue: letter-spacing of the preceding LetterSpaced run, otherwise an explicit margin; a LetterSpaced run whose last box has no InterChar after it compensates with a negative margin.
- Blank glue folds into its owner glyph's margin when adjacent: margin = -blankPx + (owned adjacent Blank present on this line ? its px : 0); IA_Displaced Blanks paint as spacers.
- Autospace and ObjectSpace glue paint as spacers (data-syn kinds from RunRec/GC, as today: 'boundary').
- Pinned boxes paint as inline-block of the defined width.

Dump: tsrc --stage=hlist prints kind, class, attrs, w, stretch, numeric penalty, run, src span; every field the DP reads enters breakKey (break.cc:149-174).
```

**surface**

None directly. Visible in --stage=hlist goldens and through paint as data-syn/data-join/margins/word-spacing.

**replaces**

- engine/src/emit/emit.h:10-22 (BF_* bits)
- engine/src/emit/emit.h:24-51 (LinebreakBlock spaceWidth/stretchWeight/kernPx/ctx*/math)
- engine/src/break/break.cc:67-68 (hyphen kern + breakWidth both counted at a break; fixed with T6 in step 11)
- engine/src/layout/layout.cc:69-77 and :544-550 (join re-derivation -> IA_SourceSpace)
- engine/src/layout/layout.cc:505-521 (CJK-gap topology and junction-kern accumulation)
- engine/src/render/typeset_html.cc:491-515 (boundary/indent spans, halfPresent squeeze)
- engine/src/render/typeset_html.cc:523-545 (BF_PAIR branch -> RealizeClass::Pinned)
- engine/src/render/typeset_html.cc:546-590 (two run predicates and gap re-derivation)
- engine/src/render/typeset_html.cc:481-490 (literal '-' hyphen -> Disc pre material)
- engine/src/emit/emit.cc:1053-1081 (dumpBlock labels, pen=INF|0 only)
- runtime/src/main/shell.mjs:41-42 (.tsr-sqL/.tsr-sqR -0.5em)

### TextRules: one tailorable character class + per-script locale-pack sections, resolved per document

**owner_layer**

L5 data. engine/rules/ (pinned UCD + locale/*.def) -> tools/ucdc.mjs -> engine/gen/textrules.h; runtime API engine/src/shape/textrules.{h,cc}; dictionaries move to engine/src/shape/hyph. The locale registry mechanics (BCP-47 chain, per-domain sections) are T4's; T5 owns the text-rules and hyphenation sections. Consumers: the shaper only (T6's grid reads classes through the shaper's Grid mode). L0 and L7 never call it.

**purpose**

The single classifier and the single source of every script-, locale- and typography-dependent constant: replaces five classifiers, punctuation control flow, codepoint heuristics, per-feature break knobs and the en-US-only hyphenator, with exact compat values.

**definition**

```
enum class CC : u8 {   // ONE class per cluster base; UAX#14 LB, blanks, autospace, font role, kern eligibility are COLUMNS of CC
  Alpha, Digit, Apostrophe, Hangul, Ideo, Kana, SmallKana, IterMark, Prolonged, IdeoSpace, Emoji,
  OpenW, CloseW, FullStopW, CommaW, ColonW, ExclW, MiddleDotW, DashW, EllipsisW,            // Wide (jlreq cl-01..08 analogues)
  OpenN, CloseN, QuoteN, Infix, Solidus, HyphenMinus, Excl, PrefixNum, PostfixNum, BreakAfter, BreakBefore,  // Narrow (UAX#14 OP CL QU IS SY HY EX PR PO BA BB)
  AmbOpenQuote, AmbCloseQuote, AmbDash, AmbEllipsis, AmbMiddleDot,                          // resolved to a W or N class by the shaper
  Space, NbSpace /*GL stretchable*/, NbRigid /*U+202F*/, ZwSpace, WordJoiner, SoftHyphen, NewLine /*BK CR LF NL U+2028*/, Other, N };
struct CpInfo { CC cc; u8 gcb:4 /*UAX#29 GCB*/, extPict:1, eaw:3 /*UAX#11*/; };   // 2-level trie, UCD pinned; ~2 B/entry
CpInfo cpInfo(u32 cp);   // ASCII fast path: 128-entry direct table
enum class SG : u8 { Han, Kana, Hangul, Latin, Common };      // script group = column of CC (+ trie bits for Latin vs Common)
enum class BreakClass : u8 { Prohibit, Direct, Hyphen, ExHyphen, Emergency, Forced };
struct Section {                       // one script group of one pack; numbers are exact decimals parsed to the same doubles as today's literals
  LB lbOf(CC) const;                   // projection; pair overrides below
  BreakClass pair(LB a, LB b, bool spacesBetween) const;      // UAX#14 §7 pair table incl. Indirect; tailored rows (e.g. Han: IN not line-start-prohibited, jlreq cl-08)
  struct { double l, r; } blank(CC) const;                    // em: compat 0.5
  u8 adjacent(PunctMode, CC a, CC b) const;                   // which Blanks survive: bit0 a.R, bit1 b.L (+ breakable)
  double autospaceEm(CC a, CC b) const;                       // compat Ideo|Kana x Alpha|Digit = 0.25
  bool interChar(CC a, CC b) const; double interCharCapEm, interCharWeight;   // compat: 0.1, 0.6 (layout.cc:514-521 topology as data)
  bool joinsWithoutSpace(CC a, CC b) const;                   // soft-break policy (Han/Kana: Wide x Wide; Hangul: never)
  const DefinedAdvance* defined(const CC* seq, u32 n) const;  // ---- / .... = 2em; single Dash/Ellipsis in Wide context = 1em
  FontRole fontRole(CC) const; bool kernEligible(CC) const; };
struct Pack { StrRef tag; const Pack* parent; const Section* sec[SG::N]; HyphenDictRef hyph[SG::N];
             float penalty[BreakClass::N] /*Direct 0, Hyphen 0.7, ExHyphen 0, Emergency 1.2*/; };
struct EmergencyTable {   // NOT per locale: keyed by overflowWrap mode
  struct Sep { CC cc; u32 cp; bool before; }; vector<Sep> seps; u8 minGraphemes, minPiece; };
struct HyphenDict { StrRef tag, alphabet; const Trie* trie; const ExcTable* exc; u8 leftmin, rightmin, minWord; StrRef hyphenChar; u32 version; };

Selection (two-level): the governing atom of a boundary is the Wide one when exactly one side is Wide, otherwise the later atom. Its lang selects a pack and its script group selects a Section, walking lang -> parents -> 'und' per section. 'und' Han = today's zh-Hans compat behaviour, so an 'en' document's CJK passages keep today's geometry; lang tailors only same-script behaviour and hyphenation.

Per-document resolution (no globals): class RulesResolver (in ShapeEnv) overlays the document's tailorings on immutable built-in packs and flattens one resolved table per (doc, tag) on first use. TextRulesId = hash(built-in RULES_VERSION, base chain, canonical tailoring spec, dict versions + availability); it is the only rules identity used in any key.

Packs: RULES_VERSION 0 'compat' reproduces support.h:158-190 and the emit.cc codepoint rules bit for bit. RULES_VERSION 1 ships und, en, zh-Hans: UCD classes with Han/Latin tailorings that reproduce compat's pair results for every codepoint compat classifies, enforced by tools/rules-diff (CI fails on any fixture boundary whose class changes unless allowlisted). ja, zh-Hant, ko are later data-only additions.
```

**surface**

Built-in packs are data files. Per-document tailoring is a document-global declaration delivered through T2's declaration-op channel, validated as data; last declaration per tag wins, with a diagnostic on a conflicting redefinition; v1 keys are per-codepoint only:
#{ $.text.tailor('ja-x-book', { base: 'ja', classes: { '〜': 'Prolonged' }, defined: { '〜〜': 2 }, hyphenate: ['ta-ble'] }) }
then #style({lang: 'ja-x-book'})[…]. Spacing matrices and pair overrides are built-in only.

**replaces**

- engine/src/support/support.h:158-190 (isCjk/isPunctOpen/isPunctClose/isCjkIdeo)
- engine/src/inline/inline.cc:50 (cjkish)
- engine/src/emit/emit.cc:396, :402-412, :430-443 (U+2014/U+2026 and curly-quote codepoint rules)
- engine/src/emit/emit.cc:841, :852 (cp >= 0x2000 kern cutoff)
- engine/src/layout/layout.cc:168-171, :199, :239-250 (grid break/width classes, via the shaper's Grid mode)
- engine/src/render/typeset_html.cc:400-406 (snap split by isCjk; class comes from grid items)
- engine/src/api/config.h:14-20, :50 (PunctCompress -> pack default + TextProp)
- engine/src/api/config.h:36-43 (cjkJustifyK, cjkGlueEm, hyphenPenalty, urlBreak* -> pack/EmergencyTable values)
- engine/src/api/config.h:76-77 (kPunctHalfEm, kCjkBoundaryEm)
- engine/src/emit/emit.cc:201, :208-213 (5-letter minimum, 20-byte threshold, separator list, 3-char piece)
- engine/src/hyphen/hyphen.h:10 and hyphen.cc:3-7, :30-38 (one en-US trie, ASCII-only)
- engine/src/emit/emit.cc:955-962 (punct width = measured - 0.5em clamp)

### Shaper: paragraph-level grapheme-cluster stream to HList, driven by shape-category TextProps

**owner_layer**

L5 Shaping, engine/src/shape/shaper.{h,cc}. Called by T6's block layouters for their inline content. TextProps are shape-category keys in T4's style-property registry (they enter neither measureKey nor paintKey).

**purpose**

One pure pass per paragraph-like unit. Context crosses style, link, ref, code and object edges. Soft breaks, Unicode controls, ambiguous width, white-space, hyphenation and emergency breaks are decided here, from data, once, with one precedence rule.

**definition**

```
struct TextProps {   // v1; shape-category style props resolved per StyleId by T4
  StrRef lang /*existing Styling::lang; unset = document*/; Hyphens hyphens /*none|manual|auto*/;
  OverflowWrap overflowWrap /*normal|separators|anywhere*/; PunctMode punct /*full|book|none; replaces Config::punctCompress*/;
  Autospace autospace /*none|normal*/; WhiteSpace whiteSpace /*normal|nowrap|pre*/; };
struct ShapeEnv { const StyleView& styles /*textProps(), face(StyleId, FontRole), measureKey(), emPx() from T4*/; RulesResolver& rules;
  ObjectRegistry& objects; Interner& strs; DiagSink& diags; StrRef docLang; ShapeMode mode /*Paragraph|Grid*/; };
struct Atom { u32 node, off; u16 len /*one extended grapheme cluster*/; u32 obj; StyleId st; u32 ctx /*link, syn, anchor, edge attrs*/; CC cc; u8 flags; };
HList shape(const ContentNode* unit, ShapeEnv&);                // pure function of ShapeInputs; reads no metrics
MeasureRequest resolveWidths(HList&, MetricStore&, ShapeEnv&);   // AdvanceSpec + object resolve phase; keys by T4 measureKey(face)
ShapeInputs = { subtree content (kinds, strings, attrs, per-node offset maps), TextProps+face per atom, TextRulesId, dict availability,
  settings read (paraIndentEm, codeScale, base size via emPx) } -- the exact list T9 hashes if it caches HLists; T5 itself keeps no cross-document cache.

Passes:
1. Flatten through a closed table generated from ops.def's post-normalization inline kinds (T2 runs level normalization first):
   - text -> clusters; U+000A/CR/NEL/U+2028 -> NewLine atoms = soft breaks (inline text is reflowed; deliberate UAX#14 deviation);
   - containers seq, styled, group, link, ref -> push context (link, SynKind/copyText from the node's copy attribute, anchor from label, edge attrs) and recurse;
   - code -> atoms under the code face (role defaults whiteSpace:pre, overflowWrap from T4's role stylesheet);
   - mathinline, image, raw, user hbox -> ObjectRegistry expand();
   - error -> atoms of '⚠ message' under the CODE face, breakable, as today (emit.cc:94-106);
   - hardbreak -> Forced boundary (lowered to Penalty(0) + warning until step 11; no public ctor before then);
   - comment -> nothing;
   - anything else (note, term, collect, a block kind) is an L2/L4 contract violation -> error object + 'shape-unsupported' diagnostic in this unit only; CI scans every fixture tree for kinds without a handler.
2. Classify: cpInfo(base).cc per cluster (combining marks, VS, ZWJ sequences absorbed: UAX#14 LB9/LB10 by construction); per-document tailoring.
3. Resolve, O(n) with one prefix and one suffix pass:
   - U+2019 between letters -> Apostrophe (UAX#29 MidLetter), before pairing;
   - Amb* -> W or N: matched quote pairs jointly; priority: local lang (style lang set and != docLang; its pack decides) > nearest strong neighbour across nodes (Wide punctuation is Wide evidence; spaces and soft breaks are neutral) > document lang > Narrow;
   - defined advances (sequence match);
   - NewLine: nothing if joinsWithoutSpace(governing), else Word glue with IA_SourceSpace measured as ' '.
4. Token pass (maximal Alpha/Digit/Apostrophe/Infix/HyphenMinus runs across style edges) produces annotations only:
   SoftHyphen -> Disc; hyphens:auto + dict + word inside dict.alphabet -> pattern Discs; HyphenMinus between letters -> ExHyphen (App C row);
   a token with no break opportunity of >= minGraphemes -> Emergency at EmergencyTable separators when overflowWrap != normal.
5. Boundary pass, one function per adjacent pair (a, b) incl. object edges:
   class = pair(lbOf(a), lbOf(b)); a token annotation may only LOWER Prohibit to Hyphen/ExHyphen/Emergency; Unicode controls: ZwSpace -> Direct, WordJoiner -> Prohibit;
   whiteSpace:nowrap -> Prohibit; whiteSpace:pre -> Space atoms stay inside the Box (Rigid run) and only Emergency survives;
   glue = Blanks by adjacent(punct) + Autospace + InterChar + Word (Space; NbSpace with class Prohibit) per the governing Section;
   edge attribute attach:'prev' on the scope's first atom (node attribute, not inherited) -> Prohibit, synthesized glue suppressed, a's trailing Blank moved after b with IA_Displaced; IA_SourceSpace glue is never removed, only made non-breaking;
   emit [Penalty?][Glue*] by the HList legality rule.
6. Boxes and runs: maximal atom sequences of one run with no boundary item between them coalesce into one text Box (Latin words, pre code spans); RealizeClass from font role / blanks / pinned / whiteSpace; run instances per HList rules; KernCtx on Word glue between Boxes of the same run whose edge CCs are kernEligible. Junction kerns on Disc go to DiscRec.nobrW; on Emergency penalties they wait for step 11.

Grid mode (T6 code grid): same flatten/classify; Boxes are clusters with Fixed column widths from eaw, boundaries carry pair classes only, no spacing glue.
```

**surface**

Markup / JS:
- #style({lang:'en'})[“OK”] (local lang overrides quote width), #style({punct:'full'})[…], #style({autospace:'none'})[…], #style({hyphens:'auto'})[…], #style({overflowWrap:'anywhere'})[…], #style({whiteSpace:'nowrap'})[…];
- edge attributes on inline containers through T2: attach('prev')[…] (a group with attach:'prev'); the resolver sets the same attribute on note markers;
- region scope: #!aside(lang: 'zh-TW') … #aside!;
- plain-text controls inside any text: U+200B (break here), U+2060 (never break), U+00A0 (non-breaking stretchable), U+202F (non-breaking rigid), U+00AD (soft hyphen); from JS e.g. #('10 km').
Role defaults (headings/captions hyphens:'manual' + overflowWrap:'separators'; inline code whiteSpace:'pre' + overflowWrap:'separators') come from T4's default role stylesheet.

**replaces**

- engine/src/emit/emit.cc:43-174 (inlineWalk dispatch with recursive default)
- engine/src/emit/emit.cc:56-69 (inline code as one unbreakable block with unbudgeted inner spaces)
- engine/src/emit/emit.cc:176-249 (emitWord lead/core/trail, URL pieces, hyphen blocks)
- engine/src/emit/emit.cc:270-456 (per-node emitText state machine, pushPunct, pushCjkChar)
- engine/src/emit/emit.cc:113-118, :293-299, :354 (math boundary/kinsoku patches)
- engine/src/emit/emit.cc:80-91 (CLS_SUP glue rule and first-block anchor)
- engine/src/emit/emit.cc:35-41, :474-476, :498-502, :806-808 (ICtx::noHyphen conflating hyphenation and emergency breaks)
- engine/src/emit/emit.cc:251 and its call sites :115, :120, :274, :275, :289, :324, :759 (fontPx ignores sizePx -> T4 emPx(face))
- engine/src/inline/inline.cc:43-59 (parser-owned CJK line join)
- runtime/src/worker/executor.mjs:103 (regionJoin always ' ', with T1/T2)
- engine/src/emit/emit.cc:836-889 (fillSpaceContexts post-pass)
- engine/src/emit/emit.cc:912-1011 (per-flag resolveWidths branches)

### InlineObject protocol (two-phase, declared edge classes)

**owner_layer**

L5 Shaping, engine/src/shape/objects.{h,cc} (registry and protocol). Math expander in engine/src/math (T8). Painters in engine/src/render (T7). Resource needs ride T9's protocol. lineExtents and line materialization are T6's; T5 exposes per-part asc/desc.

**purpose**

Every atomic inline thing (formula, image, raw HTML/SVG, user box) enters the line the same way: metric-free structure in shape(), extents in resolveWidths. Pair rules apply at its edges through declared CCs; layout, paint and copy never test for a specific kind.

**definition**

```
struct InlineObject { u16 kind; const ContentNode* node; CC firstCC, lastCC; u8 syn; StrRef copyText; u16 painter; u32 parts;
                      struct Ext { Su w, asc, desc; }* ext /*per part, filled by resolve*/; };
struct ObjectKind {
  const char* name;
  // phase 1 (in shape): structure only -- Box placeholders with AdvanceSpec::Object{obj, part}, ObjectSpace glue in em,
  // internal Penalties from the kind's class table; may return Deferred (migration only: structure needs metrics)
  ExpandResult (*expand)(const ContentNode*, StyleId, ObjectSink&, const ShapeEnv&);
  // phase 2 (in resolveWidths): per-part extents from metrics; misses go into the same MeasureRequest / T9 needs
  bool (*resolve)(InlineObject&, const MetricStore&, ResourceSink&);
  u16 painter; };
class ObjectRegistry { u16 add(ObjectKind); const ObjectKind* forKind(Kind) const; };
Edge classes: one table maps a declared cls name to (firstCC, lastCC); one name sets both edges, {first,last} sets them separately. Defaults: math Alpha/Alpha (App C: formulas are Latin-class, so CJK x formula gets autospace); image and raw Ideo/Ideo (kinsoku and InterChar as for an ideograph; no autospace); hbox from its first/last atoms. There is no 'Object' class in any pair, blank or autospace table.
An object never sets its own break-after: the boundary pass decides from lastCC.
Built-ins:
- math (T8): Box per segment, ObjectSpace glue, penalties from T8's class-indexed table (seeded 0.8/0.85/0.95). Until T8 separates segmentation from text-leaf widths, expand returns Deferred: the object is one placeholder with known edge classes; resolveWidths splices the segments for THIS paragraph when metrics arrive (not a whole-document re-emit) and the HList is flagged HasDeferredObject (never cached);
- image: one Box from intrinsic dims (NEED_IMAGES via T9), placeholder box on failure;
- raw: one Box from declared w/h, cls and copy through T2 attrs;
- hbox: content is an inline subtree shaped recursively (same shaper, same ShapeEnv) into an unbreakable HList; extents = sum of item widths + T6 BoxModel padding/border; painter = styled inline-block. Covers kbd/badge/boxed markers and later ruby bases.
```

**surface**

#image('icon.svg', {w:16, h:16}) inside a paragraph. #raw('<svg…/>', {w:14, h:12, cls:'Alpha', copy:'(c)'}) in inline position (T2 decides the level from position). #box({pad: 0.2em, border: '1px solid'})[Ctrl] for a user hbox (T2 constructor). $…$ unchanged.

**replaces**

- engine/src/emit/emit.h:42-44 (LinebreakBlock::math)
- engine/src/emit/emit.cc:108-158 (mathinline special path, BF_BOUND glue, unconditional break-after at :145)
- engine/src/emit/emit.cc:170-172 (silent default recursion for inline leaves)
- engine/src/layout/layout.cc:332-335, :446-449, :528-531 (three asc/desc copies -> T6 lineExtents over object parts)
- engine/src/render/typeset_html.cc:474-479, :587 (mathSpan dispatch, math excluded from coalescing)
- engine/src/math/math.h:55-66 (MathSeg::brkBefore codes -> T8 class table)
- engine/src/api/config.h:44-49 (three math penalty keys)
- engine/src/api/doc.h:247-254 (whole-document re-emit for math text; per-paragraph resolve instead, with T8/T9)
- runtime/src/main/copy.mjs:15-18 and runtime/src/main/audit.mjs:20 (data-syn=math inverted meaning; with T7 data-copy)

### Run instances: first-class run identity (face, link, SynKind, RealizeClass, anchor points) with per-item source spans

**owner_layer**

L5 Shaping, engine/src/shape/hlist.h (HList::runs). Consumed by resolveWidths (KernCtx), T6 line materialization and T7 display list; T7 owns the SynKind vocabulary and id emission.

**purpose**

'These glyphs are one shaped browser run' gets exactly one definition, so kerning budgets, DOM run splitting, letter-spacing/margin realization and copy/anchor attributes cannot disagree. Run-level and line-level data-s/data-e become exact.

**definition**

```
RunRec (see HList): face = T4 paintKey(StyleId) composed with the cluster's FontRole (Wide/Narrow; replaces compose(st, CLS_CJK), emit.cc:272) -- also the measurement face via T4 measureKey(face), so TextProps never split measurement or the JS measurer cache; link; syn (SynKind: 0 content, else a T7 kind such as ref/boundary/hyphen/indent/math/image) and copyText (Replace); rc (RealizeClass); anchor.
Copy role is a generic node-level attribute copy = Content | Synthetic(kind) | Replace(text), set by L4 on generated text (cite brackets, '??', ref display) and exposed to users by T2; the flattener honours it for any kind, so L5 hard-codes no L4 kinds.
Anchors are points: the first item of a labelled scope gets IA_Anchor, which starts a new run instance; paint emits id once there, never on later line fragments.
Source spans: each atom carries (srcStart, srcEnd) from the per-text-node offset map that T2 transports onto the content node at instantiation (L3); text without a map uses the node span. Items union their atoms' spans; data-s = first item's srcStart, line data-e = max srcEnd (layout.cc:532-537 semantics).
Invariants (asserted): paint opens a DOM run exactly at run changes; KernCtx exists only between Boxes of one run; data-syn, id and data-s describe the whole run.
```

**surface**

None. Visible as correct data-syn/id/data-s on .tsr-r runs, correct copy, word-spacing:0 on Rigid (pre) runs.

**replaces**

- engine/src/emit/emit.cc:870-872 (style/link run guess)
- engine/src/render/typeset_html.cc:437-471 (openRun stamps data-syn/id/data-s from the first block only)
- engine/src/render/typeset_html.cc:546-552, :582-590 (coalescing predicates that ignore BF_REF)
- engine/src/layout/layout.cc:440, :512 (junction kern accumulation)
- engine/src/emit/emit.cc:184, :262, :322, :388 (every block gets n->span)

## Subsumption (finding → mechanism)

- **subsumed** by *HList (IK + GC + attrs) + run instances (RealizeClass)*: `emitter/bf-flag-overload-and-rederivation`
  BF_* bits (emit.h:10-22) become item kind, glue class, four attrs and the run's RealizeClass. The CJK-gap topology (layout.cc:514-521; typeset_html.cc:531-538, 557-567) becomes 'InterChar glue present', decided once by the Section's interChar(a,b) column, which is exactly today's topology. Copy join (layout.cc:69-77, 544-550) becomes IA_SourceSpace. Math glue gets GC::ObjectSpace instead of BF_BOUND. Every field the DP reads enters breakKey.
- **subsumed** by *HList representation (T5) + T6 item-native breaker and unified stretch (step 11); shared with T6*: `break-layout-pages/glue-semantics-split`
  T5 delivers the representation; T6 delivers TeX discard, one measureLine and the stretch unit. Until step 11 the HList stores today's two quantities exactly (ColdRec.capSu = 0.1em capacity on CJK Boxes; HItem.x = float weight 0.6/1.0), so no stretch decision is baked into steps 2-10. The doc decision (App C 0.1em capacity vs v2 §8 / document-model §6.2 weights n_latin + k·n_cjk, design-decisions-v2.md:196 vs :368-372) is required before step 11, not step 2. Verifier corrections kept: breaker ratio about 0.40 vs renderer k=0.6.
- **subsumed** by *Shaper (paragraph cluster stream, one boundary function) + TextRules Section columns*: `emitter/paragraph-blind-script-context`, `emitter/boundary-glue-constant`, `emitter/missed:1`
  The Prev reset (emit.cc:277-278), the node-local look-ahead (:404, :437), the three math patches and the retroactive back().breakPenalty edits disappear; context crosses style, link, code and object edges. Autospace is a Section cell (Ideo|Kana x Alpha|Digit = 0.25em, stretch weight 1.0, Direct), the normative App C / v2 §14 value as an exact decimal; objects participate through their edge CCs (math = Alpha). Breaks after objects come from pair(lastCC, next), so ')' and ',' after a formula can no longer start a line (missed:1). Kinsoku-before-closer already worked across nodes (verifier); it now holds by TeX legality (Penalty INF precedes the InterChar glue) and is linted.
- **subsumed** by *TextRules (one CC per cluster, generated cpInfo, compat then UCD packs) + Shaper Unicode controls*: `emitter/hardcoded-script-class-tables`, `emitter/missed:2`
  One cpInfo() replaces five classifiers; compat reproduces today first (golden-neutral). UCD v1 adds Hangul (Wide font role, word-based breaking), 〖〗｟｠・ー々, small kana, 〜, and the controls: ZW -> Direct, WJ -> Prohibit, NBSP -> Word glue measured and painted as U+00A0 behind Penalty INF, U+202F -> rigid character inside the Box (CSS word-spacing does not apply to it), SHY -> Disc (lowers exactly to today's legacy hyphen block). U+3000 is decided: IdeoSpace, a measured Box with ideograph pair behaviour, kept at line start (today's behaviour, no churn). The mock keeps a frozen copy of the five ranges (mockIsWide), so regenerating tables cannot move mock widths. Hardcoded classes in the grid move to the shaper's Grid mode.
- **subsumed** by *TextRules Section blank/adjacent/defined columns + HList Blank glue + RealizeClass BlankBearing/Pinned*: `emitter/punct-compression-control-flow`, `emitter/defined-width-dash-ellipsis`
  blank(CC) + adjacent(PunctMode) replace pushPunct's branches (emit.cc:331-374), the clamp (emit.cc:955-962), halfPresent (typeset_html.cc:505-510) and the CSS squeeze (shell.mjs:41-42). resolveWidths writes blank px from T4 emPx(face) into ColdRec; paint prints them, which fixes the sizePx em mismatch. Only the 'adjacent' axis is modelled; the CSS text-spacing-trim start/end axes keep today's fixed behaviour (opener's leading Blank is discardable at line start, closer's trailing Blank at line end) and are deferred (not claimed equivalent to CSS keywords). A punct pair spanning two runs is governed by the later atom. DefinedAdvance rows replace the U+2014/U+2026 code; BF_PAIR becomes RealizeClass::Pinned. Rationale in document-model §6.1 kept.
- **subsumed** by *Shaper pass 3 (apostrophe, ambiguous-width resolution) + pass 4/EmergencyTable + TextProps.overflowWrap*: `emitter/latin-quote-heuristic`, `emitter/missed:4`, `markup-language/quote-context-heuristic`, `real-world-evidence/codepoint-heuristics`
  Amb* classes resolve once per paragraph in O(n): U+2019 between letters becomes Apostrophe first (don’t stays in the word); matched pairs resolve jointly; local lang (style lang != docLang) > nearest strong neighbour across nodes, Wide punctuation counting as Wide (fixes 他说：“Hello”) > document lang > Narrow. No separate ambiguousWidth prop: #style({lang:'en'})[…] is the override. The URL half of codepoint-heuristics (20 bytes, separator list, emit.cc:208-213) becomes the EmergencyTable (grapheme count, separators with side as data), scanned over the flattened token so markup inside a URL no longer defeats it, gated by overflowWrap instead of noHyphen.
- **subsumed** by *Shaper NewLine atom + Section.joinsWithoutSpace (encoding owned by T1, OPS bump)*: `markup-language/cjk-softbreak-classifier`, `parser-frontend/parser-owned-cjk-line-join`
  inline.cc:48-59 is deleted; T1 carries the soft break as U+000A in text with an OPS_VERSION bump and documented semantics ('newline characters in inline text are soft breaks'; JS-made text with '\n' changes meaning from word character to soft break, probe: word "alpha\nbeta"). The shaper resolves it with resolved classes on both sides across nodes (fixes '好” 然后', '强调 中文继续' and the raw-byte '*' join) and with the governing Section (Hangul joins with a space). regionJoin (executor.mjs:103) and the semantic serializer use the same encoding. The verifier's 'resolve at instantiation' is met by the paragraph-level shaper without merging text in L3.
- **subsumed** by *TextRules (BreakClass penalties per pack, HyphenDict registry, EmergencyTable) + TextProps (hyphens, overflowWrap, whiteSpace) + one boundary-resolution function*: `break-layout-pages/break-policy-config-knobs`, `emitter/url-break-special-path`, `emitter/hyphenation-en-us-only`
  hyphenPenalty, urlBreak*, punctCompress (config.h:39-50) become pack penalties, EmergencyTable values and TextProps; math penalties move to T8's class table. noHyphen (emit.cc:40, 476, 502, 807) splits into hyphens:'manual' and overflowWrap:'separators' from T4's role stylesheet, so long URLs in headings break (probe dw=-44851su). HyphenDict by BCP-47 with alphabet check (Übersetzung no longer cut by en-US), per-dict minima and hyphenChar; 'und' Latin keeps en-US (compat). Explicit '-' between letters becomes ExHyphen (App C row, design-decisions-v2.md:367). Precedence: pair() first, token annotations may only lower Prohibit. hardbreak -> Penalty -INF is T6's step-11 deliverable (the legacy DP cannot force breaks, break.cc:47-73), so that part of break-policy-config-knobs is fixed in step 11, not step 9.
- **subsumed** by *HList Disc (representation, step 9) + T6 item-native breaker (semantics, step 11)*: `break-layout-pages/hyphen-url-not-discretionary`
  Disc{pre: hyphenChar Box, nobrW: junction kern} replaces BF_HYPHEN and URL pieces; Disc pre/post/nobr enter breakKey. Until step 11 Disc lowers to today's hyphen block with width = max(0, ceil(nobr kern)) and breakWidth = pre width, which keeps the estimate on the over side; the under-estimate at break.cc:67-68 disappears only with the step-11 breaker. URL-piece junction kerns are deferred to step 11 for the same reason. The mock is additive (mock.h:15-19), so junction kerns are 0 in goldens.
- **subsumed** by *InlineObject protocol (two-phase)*: `emitter/math-only-inline-box`, `math/inline-math-special-block`
  LinebreakBlock::math, the three extents copies (to T6 lineExtents over object parts), mathSpan dispatch, the unconditional break-after (emit.cc:145) and the copy/audit inversions (data-copy contract with T7) go away. Inline image and raw get objects instead of vanishing. Math expansion is Deferred (per-paragraph splice at resolve time, uncached) until T8 separates segmentation from text-leaf metrics.
- **owned-by-other-theme** by *T8-math*: `math/segmentation-class-preview`
  The substance (effClsOf preview, duplicated Rules 5-6 at math.cc:1216-1243, 1281-1295) is T8's single mlist->item conversion. T5 owns only the item vocabulary, the ObjectSink API and the removal of the brkBefore codes' mapping to Config keys (emit.cc:126-128, config.h:44-49) in favour of T8's class-indexed penalty table.
- **subsumed** by *Run instances (+ node copy attribute, anchor points, per-node offset maps via T2)*: `emitter/kern-context-postpass`, `emitter/missed:0`, `render-runtime/missed:0`, `render-runtime/missed:3`
  One run definition serves paint, KernCtx and line kern accumulation. Copy role is in the key, so data-syn='ref' no longer swallows prose (cite/basic.html.txt '…]; hyphenation patterns follow') and the synthetic '[' is no longer copied. Kern eligibility comes from CC columns, not cp >= 0x2000 (switch in step 8 with listed churn). Boundary glue paints as a spacer, so no kern is needed there (verifier). data-s per run and data-e per line come from per-atom source spans (srcStart, srcEnd).
- **subsumed** by *Node edge attribute attach (flattener) + Unicode WJ*: `emitter/sup-bit-attach-rule`, `real-world-evidence/sup-attach-private`
  CLS_SUP stops carrying break semantics (emit.cc:89-90 deleted). attach is an edge attribute of an inline container, applied to the scope's first atom only (not an inherited style property): Prohibit before it, synthesized glue suppressed, the previous glyph's trailing Blank displaced after the marker and painted as a spacer (fixes notes-design.md:90-91), typed spaces kept but made non-breaking. The resolver sets it on note markers (T3); users reach it via T2's attach('prev')[…]; plain-text authors use U+2060. The presentational raise (script:super) is T4's paint-only prop; the converter output with 120 literal ^&dagger; in pbr-en is T1 tooling.
- **subsumed** by *TextRules exact-decimal fields + EmergencyTable + resolved px in ColdRec*: `emitter/scattered-magic-constants`
  0.5em blank, 0.25em autospace, 0.1em InterChar capacity, k=0.6, hyphen minima, URL minimum and piece length, and the U+2000 kern cutoff become pack/table fields written as decimals that parse to the same doubles as today's literals (so suRoundPx(0.1 * px) is bit-identical; 102su at 16px). Weights stay float as today. Geometry is emitted as explicit px, so the CSS copies disappear. Non-text constants stay with their owners: table pads and heading sizes (T4), paraGap/3, min 8 code columns and placeholder h/3 (T6), hl cap (T2), sup raise (T4/T7).
- **subsumed** by *Run instances source spans (offset maps transported by T2 onto content nodes)*: `emitter/coarse-source-spans`
  Non-ad-hoc input item. Fixed in step 6.
- **subsumed** by *Shaper closed flatten table + InlineObject protocol*: `emitter/silent-drops-of-unhandled-kinds`
  Non-ad-hoc input item. Closed table generated from ops.def's post-normalization inline kinds; containers (seq, styled, group, link, ref) recurse explicitly, so valid content never becomes an error. Fixed in step 3.
- **subsumed** by *HList dump (--stage=hlist)*: `emitter/dump-hides-finite-penalties`
  Non-ad-hoc input item. Numeric penalties, math items and non-Text units appear in the hlist dump. Fixed in step 2.
- **owned-by-other-theme** by *T2-constructor-ir (with T6 grid layouter and T4)*: `emitter/codeblock-args-in-emit`
  T2 normalizes hl/lineNo through a value-domain schema; T6's grid layouter declares its '0'/'中' probes; T4 makes code font features a style property. T5 provides only the shaper's Grid mode (classification and pair classes for grid wrap), replacing layout.cc:168-171 and 239-250 and removing the emit-vs-grid divergences listed by the verifier.

## User extension examples

### Typeset a Japanese passage (a new locale) with jlreq non-starters and middle-dot spacing

**Today**

Japanese gets Simplified-Chinese geometry. ー, ・, 々, small kana and 〜 are in no class (support.h:165-186), so they can start a line. Adding rules means editing support.h, emit.cc pushPunct, inline.cc:50, layout.cc:239-250 and the shell CSS.

**After**

Built in, data only: engine/rules/locale/ja.def adds Kana/Han sections (SmallKana and Prolonged non-starters, MiddleDotW blanks, adjacency rows, joinsWithoutSpace Wide x Wide, Latin dict 'en'); regenerate, no engine code.
Per document, no rebuild: #{ $.text.tailor('ja-x-novel', { base: 'ja', classes: { 'ー': 'Prolonged' } }) } then #!aside(lang: 'ja-x-novel') … #aside!

### Inline icon or SVG glyph inside CJK prose

**Today**

#image('icon.png', {w:16,h:16}) in a paragraph is silently dropped (probe: no box, no diagnostic). raw is block-only. Math is the only inline object (emit.h:44).

**After**

中文#image('icon.svg', {w:16, h:16})中文 is an InlineObject with Ideo edges: InterChar glue and kinsoku as for an ideograph, no autospace.
#raw('<svg…/>', {w:14, h:12, cls:'Alpha', copy:'(c)'}) gets autospace against CJK; copy yields '(c)'.

### A user keycap / badge whose content is styled text

**Today**

Impossible without engine changes: inline content is either text runs or math.

**After**

#let kbd = (s) => box({pad: '0.15em', border: '1px solid', radius: '2px'})[#style({font: 'mono'})[#s]]
Press #kbd[Ctrl]+#kbd[C]. Each is an hbox object: unbreakable, extents from its shaped content plus padding, edge classes from its first/last atoms, painted as an inline-block.

### A user sidenote / dagger marker that behaves like the built-in footnote marker

**Today**

Only resolver-built markers get CLS_SUP plus the 'never start a line' rule (emit.cc:89-90, resolve.cc:345). styleBits has no sup (executor.mjs:6-11); converters leave 120 literal ^&dagger; in pbr-en.

**After**

#let mark = (s) => attach('prev')[#style({script: 'super'})[#s]]
。#mark[†] never starts a line and hugs the 。 glyph, its trailing blank painted after the marker, exactly like built-in markers (the resolver sets the same attach attribute). In plain text: word⁠† (U+2060).

### German (or any language) hyphenation with exceptions

**Today**

Only en-US patterns on the ASCII core: Übersetzung -> Überset-zung; naïve, résumé get no points; well-known never breaks; literal '-' painted (typeset_html.cc:485).

**After**

#style({lang: 'de', hyphens: 'auto'})[Übersetzung] selects HyphenDict 'de' (resource kind 'hyph', key 'de', or compiled in by tools/hyphc.mjs): alphabet covers äöüß, hyphenChar from the dict, explicit hyphens break at ExHyphen. Exceptions: #{ $.text.tailor('de', { hyphenate: ['Ur-in-stinkt'] }) } (document-global, last wins).

### Author-controlled breaks and white space: non-breaking units, break hints, inline code

**Today**

NBSP is a rigid part of one word; ZWSP gives one unbreakable block; SHY disables hyphenation of the word (probes). Inline code is one unbreakable block whose inner spaces the line's word-spacing stretches without budget (probe: 17.25px per space).

**After**

NBSP is stretchable non-breaking glue (TeX ~); U+202F is rigid; ZWSP is a break; SHY is a Disc. Inline code is whiteSpace:'pre' by role default: its spaces stay in the Box, its run gets word-spacing:0, and long tokens get emergency breaks at separators. #style({whiteSpace:'nowrap'})[New York] keeps a phrase together; #style({overflowWrap:'anywhere'})[…] opts into breaks between any grapheme clusters.

### An English quotation inside Chinese prose that must stay Western

**Today**

他说“OK”: the quote class is a neighbour heuristic; lang is ignored (emit.cc:430-443); no override.

**After**

#style({lang: 'en'})[“OK”]: a lang narrower than the document is the strongest evidence; both quotes resolve jointly to Narrow (body font, no blanks), across any markup inside.

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  Widths stay integer su; shaping is table-driven integer logic. Pack constants are decimals parsed to the same doubles as today's literals (0.1, 0.25, 0.5, 0.6), weights stay float as today, so compat output is bit-identical (102su CJK capacity at 16px).
  Tables and dictionaries are generated from pinned inputs and committed; RULES_VERSION and dict versions live in test/golden/RULES and in TextRulesId; tailorings are document-global and order-independent (last wins), resolved per document, never via process globals, so output never depends on which other documents a worker holds.
  The mock gets a frozen copy of the five isCjk ranges (mockIsWide) in step 1, pinned by a CI test, because today mock.h:11 calls support.h's isCjk; the stale JS-mirror claim at mock.h:1-2 is corrected (runtime/src/shared/mockmeasure.mjs does not exist).
  Steps 1-3 are byte-identical; every churning step lists its fixtures, and tools/rules-diff turns 'which boundaries changed class' into a checked allowlist.
- **I2 measurement-render robustness contract (v2 §7)**
  All spacing is explicit engine data: word-spacing per line, letter-spacing per LetterSpaced run, px blank margins computed in resolveWidths from emPx(face) (sizePx honoured at all seven fontPx call sites), spacer widths, pinned widths. Rigid (pre) runs get word-spacing:0, which removes today's unbudgeted stretching of spaces inside inline code. NBSP/NNBSP are measured and painted as the same codepoint.
  Quantization stays on the over side: Measured and MeasuredMinusBlanks get ceil+eps (emit.cc:961 omits eps today); Disc nobr and KernCtx use ceil, and until step 11 legacy lowering uses max(0, ceil(kern)) at break items; KernCtx on emergency penalties is deferred to step 11. Atoms are grapheme clusters, so no glue or letter-spacing is budgeted inside a cluster.
  CSS: text-spacing-trim is already neutralized (shell.mjs:22 space-all); text-autospace is not set anywhere and must be set to no-autospace on lines now (T7). Lines stay nowrap; no repair pass.
- **I3 ops contract / OPS_VERSION discipline**
  Wire changes are bumps, never silent reinterpretations:
  - soft break as U+000A in text: OPS_VERSION bump with T1 in step 5 (it changes the meaning of '\n' in JS-made text and the ast/js/tree/.ops of 9 multi-line fixtures);
  - TextProps keys ride T4's style-property registry; if it is not open, one grouped bump at step 7;
  - raw cls/copy, attach and copy attributes, box(): T2 open attrs, otherwise conditional bumps recorded per step;
  - $.text.tailor rides T2's declaration channel as data; unknown keys/classes warn and are ignored, so the reader stays a fuzz target.
- **I4 'execution declares, resolver decides' (v2 §11.1)**
  The shaper runs after the resolver on the resolved tree and is a pure function of ShapeInputs. Tailorings are document-global declarations; the engine sees the final registry. Scripts cannot observe classes, breaks or widths. The resolver marks structure only (copy attribute on generated text, attach on note markers, role styles composed at resolver time as today) and makes no shaping decisions.
- **I5 dual-target rule (architecture §2.1)**
  engine/src/shape has no browser dependency; UCD and hyphenation generators are build-time Node tools like hyphc. Native tests run with compiled-in en-US plus fixture dictionaries through the mocked resource provider (tsrc). Painters stay in engine/src/render.
- **I6 emission-time style binding (v2 §12)**
  TextProps are shape-category style properties folded into the StyleId at instantiation, so a value emitted under two stacks shapes differently, as fonts do today; the DAG/schedule encoding is untouched. Measurement and paint use T4's measureKey/paintKey projections, so shape-only props neither fragment MetricStore (measure.h:21) nor split DOM runs. attach and copy are node attributes, not styles, so they do not depend on the style stack.
- **I7 block-granular containment (v2 §11)**
  Shaping is per paragraph-like unit: a kind that should never reach L5 becomes an error object plus 'shape-unsupported' in that unit only (and CI guarantees no fixture hits it); a malformed tailoring is a diagnostic and ignored; a failed object (image load) is a placeholder box; an unknown hyphenation dict falls back to hyphens:manual with a diagnostic.
- **I8 resumable pull loop; atomic per-paragraph upgrade (v2 §9)**
  shape() is metric-free; resolveWidths returns one MeasureRequest covering words, KernCtx strings, vmets and object needs, through T9's single NEED_RESOURCES state. Deferred (math) objects re-resolve for their paragraph only. A dictionary is resolved before the first typeset of text that needs it; fallback to hyphens:manual happens only on a definitive provider failure, never on a timeout (unlike the 4s font deadline, worker.mjs:36-38), and dict availability is part of TextRulesId, so breaks never change progressively and no stale fallback survives.
- **I9 performance (hot path; editor fast path ~6 ms)**
  One O(clusters) pass, ASCII fast path, 2-level trie, reused atom buffer; HItem is 24 B with cold data in side tables; Direct breaks add no Penalty items; Latin words and pre code spans coalesce into one Box. T5 keeps no cross-document cache (the editor builds a new document per keystroke, worker.mjs:20-23); it publishes ShapeInputs so T9 may add a content-hashed cache. Gate: tools/bench-edit.mjs end-to-end typeset latency (emit/shape + breakKey + KP + layout + render) on the 87K corpus must not regress more than 5% before step 5 merges.

## Interfaces

- **T1-surface-frontend** (consumes)
  (a) T1 owns the soft-break encoding: U+000A inside cooked text with an OPS_VERSION bump and documented semantics ('newline characters in inline text are soft breaks'), deleting inline.cc:48-59; regionJoin and fragments use the same. (b) Per-text-node offset maps (byte ranges per slice: escapes, joins, collapse) as an L0 product that T2 transports. (c) L0 performs no script classification. (d) Converter fixes (literal ^&dagger; in pbr-en) are T1 tooling.
- **T2-constructor-ir** (consumes)
  (a) Publish the post-normalization inline kind set (containers incl. seq, leaves), from which T5 generates its flatten table; level normalization runs before shaping; image/raw/box in inline position stay inline. (b) Open typed attrs for raw {cls, copy}, the node-level copy attribute (Content|Synthetic(kind)|Replace(text)), attach, and box(); otherwise each is a recorded OPS bump. (c) A declaration-op channel for $.text.tailor with document-global semantics. (d) Per-occurrence spans transporting T1's offset maps onto content text nodes (L3). (e) A hardbreak constructor only after T6 step 11. (f) Documented '\n' semantics in constructor text. (g) hl/lineNo normalization for codeblocks.
- **T3-semantics** (consumes)
  Resolver-generated inline content carries the generic copy attribute (cite brackets, separators, '??', ref display text) instead of being recognized by kind; note markers get attach:'prev' as a node attribute and their role style composed at resolver time (as resolve.cc:345 does). Locale packs share one BCP-47 registry and fallback chain (T4 mechanics) with T3's term tables.
- **T3-semantics** (provides)
  Inline anchor points on any inline node (IA_Anchor + RunRec.anchor, id emitted once): the paint hook universal inline labels need (today only labelled refs, emit.cc:86-88).
- **T4-style-settings** (consumes)
  One property registry with categories {measure, shape, paint, block-setting}. T5's v1 TextProps (hyphens, overflowWrap, punct, autospace, whiteSpace; lang exists) are shape-category; script:'super' is paint-only. Projections: measureKey(face) (family, size, weight, italic, lang, features), paintKey(StyleId), face(StyleId, FontRole), emPx(StyleId) honouring sizePx, textProps(StyleId), and the document lang with its origin. A default role stylesheet (headings/captions hyphens:'manual' + overflowWrap:'separators'; inline code whiteSpace:'pre' + overflowWrap:'separators') is a hard prerequisite of step 9, or a migration-only role->props shim stays in the engine. Settings cascade may override pack defaults per block (justify k).
- **T4-style-settings** (provides)
  The per-cluster FontRole (Wide/Narrow from the resolved CC), replacing compose(st, CLS_CJK) (emit.cc:272); pack defaults for TextProps (punct mode); the text-rules and hyphenation sections of the shared locale registry.
- **T6-layout-pagination** (provides)
  One hlist.h contract: item vocabulary, TeX legality rules, discard rule, exact weights (float) and ColdRec.capSu until step 11, source spans, per-part object extents, run instances. Shaper Grid mode for the code grid (cluster atoms, Fixed column widths from eaw, pair classes only). T5's item-level type is AdvanceSpec; block-level WidthSpec, lineExtents and materializeLines are T6's.
- **T6-layout-pagination** (consumes)
  Until step 11: the legacy breaker consumes fuseLegacy output (a specified lowering table per item/glue class that reproduces today's break opportunities, including the double opportunity before an opener's leading half). Step 11: an item-native breaker honouring TeX legality and discard, Disc pre/post/nobr, forced -INF (as a mandatory break, not a summed cost; break.cc:72-73 cannot today), KernCtx on break items, one measureLine for cost and justification, the stretch unit decision, and block layouters calling shape() for paragraphs, cells, captions, sidecar lines and hbox content.
- **T7-render-runtime** (provides)
  Per run instance: face, link, SynKind, copyText, RealizeClass, anchor; per item: GC, IA_SourceSpace/Anchor/OwnedByNext/Displaced, resolved px (blank margins, rawPx), source spans; object painter ids. Paint needs no TextRules.
- **T7-render-runtime** (consumes)
  The display list opens a run exactly at run-instance changes (asserted) and never splits further; T7 owns the SynKind vocabulary (today 9 kinds in goldens) and id emission once per anchor; realization channels as explicit px (no tsr-sqL/R); word-spacing:0 on Rigid runs; data-copy for Replace objects instead of overloading data-syn=math; Disc pre printed with data-syn='hyphen'; lang attribute only where it differs from the document lang; line CSS adds text-autospace:no-autospace now (text-spacing-trim is already space-all).
- **T8-math** (consumes)
  A math ObjectKind in two phases: expand = segmentation and class-indexed penalties without metrics (Box placeholders, ObjectSpace glue in em, edge CCs Alpha/Alpha by default); resolve = per-segment extents via MetricStore, misses reported as needs. Until then expand returns Deferred. T8 deletes effClsOf and the preview.
- **T8-math** (provides)
  The HItem vocabulary, ObjectSink API and Deferred protocol; optional Disc for operator repetition at math breaks (post = operator box) once T6's breaker consumes Disc post material (step 11).
- **T9-host-protocol** (consumes)
  One typed resource protocol: kind 'hyph' (key = BCP-47) and optionally 'textrules' packs; failure policy falls back only on a definitive provider answer, never on a timeout. KernCtx strings ride the ordinary MeasureRequest; object needs ride the single NEED_RESOURCES state. If T9 caches HLists, keys are content hashes of ShapeInputs (never StyleId numbers or StrRefs across documents), including TextRulesId (tailoring digest, dict availability).

## Migration

### 1. TextRules compat tables and one classifier API  → plan P1-11

Add tools/ucdc.mjs and engine/rules/locale/compat.def generating cpInfo() (CC, GCB, ExtPict, EAW) with compat classes bit-identical to support.h:158-190 and the emit.cc codepoint rules; constants as exact decimals.
Route through the API: emit.cc:396-447, 841, 852; inline.cc:50; layout.cc:168-171, 199, 239-250; typeset_html.cc:402-406.
mock.h gets a literal frozen mockIsWide() copy of the five isCjk ranges plus a CI test pinning it, so mock widths no longer depend on support.h; correct the JS-mirror claim at mock.h:1-2. The grid width class stays compat until step 8.

**Golden impact:** None; all goldens byte-identical (CI).

**OPS bump (as designed):** False

### 2. HList + run instances + fuseLegacy behind a CI equivalence check  → plan P1-12

Emit builds HItems with the existing per-node logic, in TeX form ([Penalty?][Glue*] per boundary). ColdRec carries source spans (start, end), rawPx and capSu on CJK Boxes; weights are today's floats; InterChar presence is a post-pass copying layout.cc:514-521 exactly.
fuseLegacy is a specified lowering table per item and glue class that reproduces today's break opportunities (incl. the box + leading-half double opportunity before openers and InterChar folded into the CJK Box). CI: fuse(HList) equals the old blocks field by field on all 49 fixtures (old path kept until step 5), plus all existing goldens.
Add tsrc --stage=hlist (numeric penalties, classes, spans) and the legality lint. Then move layout and render reads to items/run instances.

**Golden impact:** Existing goldens unchanged; 49 new *.hlist.txt goldens.

**OPS bump (as designed):** False

**Fixes:** `emitter/bf-flag-overload-and-rederivation`, `emitter/dump-hides-finite-penalties`

### 3. InlineObject registry (two-phase) and closed flatten table  → plan P1-13

Math becomes an ObjectKind whose expand returns Deferred (placeholder with Alpha edges) and whose items, spliced in resolveWidths, equal today's segments, glue and penalties (T8 table seeded from config.h:47-49). Per-part extents feed T6's lineExtents (replacing layout.cc:332-335, 446-449, 528-531); a painter table replaces typeset_html.cc:474-479.
The flatten table is generated from ops.def's inline kinds: containers seq/styled/group/link/ref recurse; image/raw inline become objects; hardbreak lowers to Penalty(0) + warning; note/term/collect or block kinds become an error object + 'shape-unsupported'. CI scans every fixture tree for unhandled kinds.

**Golden impact:** None for existing fixtures (tree scan: inline kinds in fixtures are text, ref, styled, link, mathinline, seq, code, comment; none places image or raw inline). New fixtures for inline image, raw and an unsupported kind; hlist goldens of math fixtures gain object records.

**OPS bump (as designed):** False

**Fixes:** `emitter/math-only-inline-box`, `math/inline-math-special-block`, `emitter/silent-drops-of-unhandled-kinds`

### 4. Run formation by run instance  → plan P4-01

Paint coalesces by run instance (face, link, SynKind, copyText, RealizeClass, anchor point). SynKind comes from the node copy attribute (interim: BF_REF -> Synthetic(ref) until T3 sets the attribute). Inline code runs are RealizeClass Rigid and get word-spacing:0. Ids are emitted once per anchor. KernCtx tagging uses the same runs; kern eligibility stays compat (cp < 0x2000) in this step.
Raw cls/copy (from step 3) and the copy attribute need T2 open attrs; otherwise one recorded bump here.

**Golden impact:** html: cite/basic, cite/unknown-diag (data-syn no longer covers prose; '[' becomes synthetic); notes/* where an anchor merged with a neighbour; doc/structure, inline/fence-edge, pages/paged-doc (word-spacing:0 on inline code runs on justified lines). blocks: possibly cite/* where a KernCtx disappears at a ref edge (mock float noise only). hlist run columns.

**OPS bump (as designed):** False

**Fixes:** `emitter/missed:0`, `render-runtime/missed:0`, `emitter/kern-context-postpass`

### 5. Paragraph-level shaper (with T1's soft-break encoding)  → plan P4-02

The flattened grapheme-cluster stream replaces the per-node state machine: autospace, kinsoku and break-after across style, link, code and object edges; apostrophe and joint quote-pair resolution (local lang first, Wide punctuation as evidence); defined advances with cross-node context; NewLine atoms resolved by joinsWithoutSpace; the emergency scan over the flattened token (compat separators and threshold).
Lands together with T1 emitting U+000A, deleting inline.cc:48-59 and regionJoin's ' ' (OPS_VERSION bump). The old emit path and its CI equality check are removed.

**Golden impact:** blocks/breaks/layout/html/semantic for fixtures with script edges at markup or line joins: cjk/softwrap, code/runs, code/sidecar, code/tsm-hl, figure/block, figure/float, math/eqref, math/stretch, notes/cjk-glue, pages/paged-doc, style/patch, inline/quotes, doc/url-break. ast/js/tree and .ops re-recording for all 49 fixtures (version byte) with content changes in the 9 multi-line fixtures: cjk/softwrap, doc/hyphen, doc/refs-diag, doc/refs, doc/structure, line/para-blank, region/figure, region/table, style/patch. All hlist goldens.

**OPS bump (as designed):** True

**Fixes:** `emitter/paragraph-blind-script-context`, `emitter/boundary-glue-constant`, `emitter/missed:1`, `emitter/missed:4`, `emitter/latin-quote-heuristic`, `markup-language/quote-context-heuristic`, `markup-language/cjk-softbreak-classifier`, `parser-frontend/parser-owned-cjk-line-join`, `real-world-evidence/codepoint-heuristics`

### 6. Per-item source spans  → plan P4-03

Atoms take (srcStart, srcEnd) from the offset maps T2 transports onto content text nodes; paint emits data-s per run instance and data-e per line from item spans. Text without a map keeps the node span.

**Golden impact:** data-s in most html goldens and line spans @[s,e) in layout goldens (mechanical); a script checks monotonicity and that each run's data-s points at its source slice. hlist src columns.

**OPS bump (as designed):** False

**Fixes:** `emitter/coarse-source-spans`, `render-runtime/missed:3`

### 7. TextProps v1 + punctuation/blank/autospace as data + explicit px  → plan P4-04

Register hyphens, overflowWrap, punct, autospace, whiteSpace as shape-category props (T4); Config.punctCompress becomes the pack default. All seven fontPx call sites use T4 emPx(face) (sizePx honoured). Box blanks become px margins written by resolveWidths; tsr-sqL/R CSS removed; MeasuredMinusBlanks gets eps; DefinedAdvance rows replace the U+2014/U+2026 code.

**Golden impact:** html for all CJK fixtures (classes become inline margins); style/patch blocks/breaks/layout/html (sizePx em fix); cjk/punct, cjk/punct-full and other fixtures with fullwidth punctuation may move breaks (+1su eps per glyph). hlist goldens. Called out.

**OPS bump (as designed):** True

**Fixes:** `emitter/punct-compression-control-flow`, `emitter/defined-width-dash-ellipsis`, `emitter/scattered-magic-constants`

### 8. UCD-derived classes (RULES_VERSION 1), Unicode controls, kern eligibility by class  → plan P4-05

Switch default packs from compat to und/en/zh-Hans: UCD classes with Han/Latin tailorings that reproduce compat pair results for compat-classified codepoints (Han: IN not line-start-prohibited per jlreq cl-08, so 号|…… keeps its break; Wide-resolved quotes re-tag to OpenW/CloseW; Latin: SY/HY/BA/EX/IS inside alphabetic tokens Prohibit, reachable only via ExHyphen/Emergency). New: Hangul, 〖〗｟｠・ー々, small kana, 〜, ZW/WJ/GL/NNBSP/SHY controls (SHY Disc lowers to today's hyphen block). Kern eligibility from CC. Grid width class from EAW (Grid mode). tools/rules-diff must report only allowlisted boundary changes.

**Golden impact:** inline/quotes and style/kern-boundary blocks/breaks/layout (kern-tagged spaces next to curly quotes: 257su -> 258su under the mock); any fixture containing newly classified codepoints, enumerated by rules-diff before merge; all hlist goldens. Recorded in test/golden/RULES.

**OPS bump (as designed):** False

**Fixes:** `emitter/hardcoded-script-class-tables`, `emitter/missed:2`

### 9. Hyphenation registry, ExHyphen, hyphens/overflowWrap split  → plan P4-06

HyphenDict by BCP-47 (alphabet check, per-dict minima, hyphenChar, exceptions); Disc pre from hyphenChar, painted with data-syn='hyphen'; explicit '-' between letters -> ExHyphen (App C); noHyphen replaced by hyphens/overflowWrap from T4's role stylesheet (hard prerequisite, or an engine role->props shim); headings, captions and inline code get emergency breaks; EmergencyTable with Chicago sides and grapheme thresholds; non-en dictionaries through T9 ('hyph').

**Golden impact:** doc/hyphen, doc/url-break, cite/basic and cite/unknown-diag (bibliography URLs/DOIs: break side flips before '.' and '/'), headings/captions with long tokens, every fixture with hyphenated compounds (new ExHyphen breaks), fixtures with long inline code tokens, all hlist goldens. Called out.

**OPS bump (as designed):** False

**Fixes:** `emitter/hyphenation-en-us-only`, `emitter/url-break-special-path`, `break-layout-pages/break-policy-config-knobs`

### 10. attach edge attribute; CLS_SUP loses break semantics  → plan P4-07

The resolver sets attach:'prev' on note markers (T3 prerequisite); the shaper applies it to the scope's first atom and displaces the previous glyph's trailing Blank after the marker as a spacer. emit.cc:89-90 deleted. T2's attach() for users.

**Golden impact:** notes/* blocks, breaks and html (marker hugs the punct glyph); hlist goldens.

**OPS bump (as designed):** False

**Fixes:** `emitter/sup-bit-attach-rule`, `real-world-evidence/sup-attach-private`

### 11. Item-native breaker with TeX discard and unified stretch (T6-led)  → plan P4-08

Delete fuseLegacy and capSu. The breaker reads the HList: legality and discard (trailing glue and blanks no longer counted), Disc pre/post/nobr, forced -INF as mandatory (hardbreak constructor lands with T2), KernCtx on Disc and emergency penalties with ceil. Capacity = sum of weights x juSu after the documented App C vs v2 §8 decision. Retire blocks goldens in favour of hlist.

**Golden impact:** Wide churn in breaks, layout and html for justified fixtures, CJK above all; the one big churn step, owned with T6.

**OPS bump (as designed):** False

**Fixes:** `break-layout-pages/glue-semantics-split`, `break-layout-pages/hyphen-url-not-discretionary`, `break-layout-pages/break-policy-config-knobs`

### 12. Extensions: document tailorings, hbox objects, further packs  → plan 可选扩展，不在本计划目标内（见 PLAN §9）

$.text.tailor over T2's declaration channel (document-global, last wins + diagnostic; v1 keys classes/defined/hyphenate) feeding TextRulesId; hbox ObjectKind with T2's box() and T6's BoxModel; ja/zh-Hant/ko packs as data once fixtures exist.

**Golden impact:** None for existing fixtures except style/patch (zh-TW scope) if the zh-Hant pack lands; new fixtures per feature.

**OPS bump (as designed):** False

## Not generalized (kept special)

- **Vertical writing, RTL/bidi, complex-script segmentation (UAX#14 SA)** — Out of scope by v2 §14 ('horizontal writing… no vertical writing, no RTL'). SA maps to Alpha (no internal breaks) with an info diagnostic; the class table leaves room to add them as data.
- **The rest of the UAX#14 rule engine beyond the pair table (regional indicators, LB21a, emoji modifiers beyond cluster atoms)** — LB9/LB10 come from cluster atoms and LB30 from the OpenW/OpenN split; the remaining rules only marginally affect nowrap lines. YAGNI until a corpus needs them.
- **Mandatory UAX#14 classes (BK, CR, LF, NL) inside inline text** — Deliberate deviation: inline text is reflowed, so newline characters are soft breaks (T1's encoding). Forced breaks come only from the hardbreak kind, after T6's step 11.
- **Glyph-level shaping and kerning inside runs** — v2 §8 leaves shaping inside the line to the browser. KernCtx models only junction effects at break opportunities, not font kerning tables.
- **Rewriting typed spaces between CJK and Latin into autospace (normalizeTypedSpace)** — It changes author text; the verifier asked for opt-in only. A typed space stays Word glue with IA_SourceSpace, so copy round-trips it.
- **White-space collapsing beyond today's behaviour** — Under whiteSpace:'normal' each Space atom is one Word glue as today; source whitespace normalization stays L0's. Only pre and nowrap are added.
- **CSS text-spacing-trim start/end axes, lineBreak strictness, wordBreak, an explicit ambiguous-width property, langLocal** — No corpus or fixture needs them (corpus: 0 kana, 0 Hangul, six 「」). Today's start/end behaviour is kept as fixed rules; the class table keeps a mode column so adding them later is data plus a registered prop. lang is the ambiguous-width override.
- **Token-type-specific emergency rules (url vs path vs identifier)** — One separator table keyed by overflowWrap covers URLs, paths and identifiers in the corpus; no evidence for distinct per-token-type rules.
- **Hanging punctuation / optical margins, ruby and warichu** — No corpus evidence. Hanging belongs to T6's LineEnds; ruby can later be an hbox-based ObjectKind without changing the protocol.
- **The code-grid breaking algorithm** — Greedy column breaking stays T6's (monospace is a metric contract, verbatim-design). T5 supplies the shaper's Grid mode for classification only.
- **Defined (pinned) advances only for a curated table (——, ……, single —/… in Wide context)** — Measurement is the default (document-model §6.1); pinning only where canvas and DOM provably disagree.
- **The mock measurer's width classifier** — It is the normative test oracle (testing.md §2); a frozen literal copy keeps table regenerations from moving every golden width.
- **Which atoms inside a formula are breakable** — Math-internal break selection stays T8's mlist logic; T5 shares only the item vocabulary, the two-phase protocol and edge classes.
- **Browser-native text-spacing-trim / text-autospace as the realization of TextRules** — The engine must own and budget all spacing (v2 §7 rule 4, §8); paint disables native spacing instead.
- **Paragraph indent as ParShape** — It stays a synthetic Fixed-width Box (SynKind indent; App C: blocks, not CSS padding) until T6's ParShape offers a first-line indent.

## Risks

- Golden churn concentrates in steps 5, 7, 8, 9 and 11; CJK correctness then rests on reviewers, rules-diff and the e2e audit. A Playwright matrix (punct full/book/none x Chromium/Gecko) must exist before step 7 merges.
- fuseLegacy fidelity (steps 2-10): the lowering table must reproduce legacy break opportunities exactly; mitigated by the field-by-field CI check until step 5 and by goldens afterwards, but semantics drift is possible where the shaper emits a form the table did not anticipate.
- Cross-theme sequencing: step 4 wants T3/T2 copy attributes (interim BF_REF mapping), step 5 needs T1's soft-break bump, step 6 T2's offset-map transport, step 7 T4's registry and projections, step 9 T4's role stylesheet, step 10 T3's attach on markers, step 11 T6's breaker, step 3's math phases T8. Slips keep fuseLegacy and Deferred math longer.
- Deferred math expansion is per paragraph but uncacheable; until T8 splits segmentation from text-leaf metrics, documents with many formulas pay a resolve-time splice each typeset.
- Fonts versus locale geometry: future zh-Hant/ja packs assume glyph ink positions; the default CJK stack is SC (config.h:28-29), so T4 must select locale font stacks before those packs ship.
- Browser features: text-autospace is unset today (shell.mjs has only text-spacing-trim); browsers shipping it can add unbudgeted space at Latin/CJK span edges until T7 adds no-autospace. typeset_html.cc:517-520 already depends on Blink full-width-izing dashes.
- Enabling ExHyphen (App C) and Chicago URL sides changes Latin line breaking corpus-wide (5861 hyphen compounds in pbr); readers may notice different breaks.
- WASM size: the cluster/class trie plus packs is estimated at 15-25KB; hyphenation dictionaries (30-60KB each) are pulled lazily, adding first-typeset latency for non-English documents.
- Performance: per-cluster classification and the atom buffer sit on the hot path; the end-to-end bench gate may require further ASCII fast paths.
- Unified stretch (step 11) changes CJK line breaking globally (capacity per CJK gap from 0.1em to k x juSu) and needs the documented decision plus a corpus re-audit.

## Open questions (decided in PLAN.md §3)

- Stretch model (needed before step 11, not before step 2): App C fixes CJK breaker capacity at 0.1em; v2 §8 and document-model §6.2 promise weights n_latin + k·n_cjk that agree by construction. Proposal: weights x per-paragraph juSu (T6 recalibrates shrinkThreshold 0.37).
- Should ExHyphen breaks (App C 'existing hyphen break', penalty 0) be enabled by default in step 9, given the corpus-wide churn, or ship as a pack value initially set to a high penalty?
- How much may a document tailor? v1 allows per-codepoint classes, defined advances and hyphenation exceptions; should blank/adjacency matrices ever be declarable?
- UCD version pin and update policy: tie regenerations (RULES_VERSION bumps, golden re-recording) to engine releases only?
- Default emergency behaviour: overflowWrap:'separators' for inline code, headings and captions as proposed; should link text get a lower emergency penalty than ordinary tokens?
- Default edge class for inline image/raw: Ideo (proposed: kinsoku and InterChar like an ideograph, no autospace) or Alpha (autospace like a formula)?
- Should inline error text ('⚠ message') be marked copy Synthetic? Correct for copy fidelity, but it changes error fixtures' html; T7 decides with its SynKind vocabulary.
- Run granularity: copy kind, RealizeClass and anchor points split a few more spans (refs, anchors, punctuation). Expected negligible under the O(lines + style runs) DOM budget (v2 §8); to be confirmed by the step-4 DOM count on the 87K corpus.

## Changelog (critique responses)

- [A1 blocker] HList legality / pass-5 order. ACCEPT. Restructured the HList contract around TeX legality (glue breaks only after a Box/Disc; Penalty < INF breaks at itself) and fixed emission to [Penalty?][Glue*]; Direct-with-glue emits no Penalty, so each boundary has at most one breakpoint. Verified the counterexample against emit.cc:353-355 and cjk/mixed.blocks.txt. Lint is property-based (no legal breakpoint at a Prohibit boundary), which subsumes the proposed syntactic 'no Glue after a finite Penalty' check; [Penalty][Glue] is legal TeX and used deliberately.
- [A2 major] Fixed-point units cannot encode 0.1em / k=0.6. ACCEPT. Verified: config.h:36-37, glue=102su in cjk/mixed.blocks.txt:3. Dropped em8 and u16/256: pack constants are decimals parsed to the same doubles as today's literals, weights stay float (HItem.x), capSu keeps today's computation until step 11.
- [A3 major] RunKey cannot realize letter-spacing/margins; font role missing. ACCEPT. Run identity is now a run instance over (face incl. FontRole, link, SynKind, copyText, RealizeClass) with singleton BlankBearing/Pinned/Object runs and an asserted homogeneity rule; KernCtx uses the same partition. Verified typeset_html.cc:503-515, 546-552, 585-590 and emit.cc:272.
- [A4 major] Closed table omits seq. ACCEPT. Verified seq in splice/content-args.tree.txt and by a tree-golden scan (inline kinds under text roots: text, ref, styled, link, mathinline, seq, code, comment). The flatten table is generated from ops.def with explicit containers; note/term/collect are L4 inputs that must not reach L5; CI scans fixture trees.
- [A5 major] U+000A soft break without OPS bump. ACCEPT. Probe confirms JS '\n' is a word character today (word "alpha\nbeta"); skeleton scan confirms 9 multi-line fixtures. Step 5 now carries an OPS_VERSION bump owned by T1, documents the new '\n' meaning as a breaking change, and lists ast/js/tree/.ops churn.
- [A6 major] -INF before the item-native breaker. ACCEPT. Verified break.cc:47-73 has no forced-break handling. BK/CR/LF/NL become soft breaks (deliberate UAX#14 deviation); hardbreak lowers to Penalty(0) + warning and gets no public ctor until step 11; the hardbreak part of break-policy-config-knobs moved to step 11; forced breaks are an explicit T6 deliverable.
- [A7 major] KernCtx extension churn and under-estimate. ACCEPT. The mock arithmetic (257su -> 258su) is plausible from mock.h:8-19; eligibility-by-class moved to step 8 with inline/quotes and style/kern-boundary churn listed; break-item kerns: Disc lowers with max(0, ceil(kern)), emergency-penalty kerns deferred to step 11.
- [A8 major] UAX#14 switch is not narrow; LB not re-tagged after ambiguity. PARTIALLY ACCEPT. Single CC class makes LB a projection of the resolved class (re-tag by construction), OpenW/OpenN split covers LB30, cluster atoms cover LB9, and step 8 churn is now enumerated by a rules-diff tool. REJECT adopting LB22 (x IN) for Han: jlreq cl-08 inseparables are not line-start-prohibited, so the Han section keeps today's break before …… (cjk/punct.blocks.txt:54-55 unchanged).
- [A9 major] HList cache keyed on per-document ids. ACCEPT. Verified worker.mjs:20-23 (new doc per keystroke). T5 keeps no cross-document cache; it publishes ShapeInputs, and any T9 cache keys on content hashes incl. TextRulesId. The I9 argument now rests on an end-to-end bench gate, not a cache.
- [A10 major] Global rulesFor leaks tailorings across documents. ACCEPT. RulesResolver lives in ShapeEnv per document over immutable built-ins; TextRulesId includes the tailoring digest. Verified worker.mjs:14 keeps many docs alive.
- [A11 major] Pack selection by run lang breaks CJK in 'en' documents. ACCEPT. Two-level selection: governing atom's lang picks a pack, its script group picks a Section, falling back per section to 'und', whose Han section is today's compat behaviour. Verified applyLang only touches supplements (config.h:89-102). T7 prints lang only where it differs from the document.
- [A12 major] attach as inherited per-atom style. ACCEPT (also B2). attach is a node edge attribute applied to the scope's first atom; typed spaces are never deleted, only made non-breaking; the displaced Blank gets IA_Displaced and paints as a spacer.
- [A13 major] shape() not metric-free with math inside. ACCEPT (also B5). Two-phase ObjectKind; math returns Deferred until T8 splits structure from widths, the HList is flagged and never cached, and re-resolution is per paragraph (verified doc.h:247-254 re-emits the whole document today).
- [A14 major] Fuse-adapter fidelity. ACCEPT. ColdRec carries (srcStart, srcEnd) (layout.cc:532-537 unions spans); InterChar presence is a post-pass copying layout.cc:514-521; capSu is a Box field during migration; fuseLegacy is a specified lowering table; CI compares blocks field by field and all layout/html goldens.
- [A15 minor] Mock 'frozen classifier' claim false. ACCEPT. Verified mock.h:11 calls support.h isCjk and runtime/src/shared/mockmeasure.mjs does not exist. Step 1 copies the ranges into mockIsWide with a pinning test and fixes the mock.h:1-2 comment; the grid width class stays compat until step 8 (EAW), churn listed.
- [A16 minor] Step 9 golden list incomplete. ACCEPT. Added cite/basic and cite/unknown-diag, replaced 'hyphen/words' with doc/hyphen, and every step from 3 on lists hlist churn.
- [A17 minor] CSS requirement misstated. ACCEPT. Verified shell.mjs:22 already sets text-spacing-trim: space-all and nothing sets text-autospace; corrected I2 and asked T7 for text-autospace: no-autospace now.
- [A18 minor] Tailor hoisting analogy wrong. ACCEPT. Verified executor.mjs:268 is order-dependent. Tailorings are document-global, last wins with a diagnostic, digest in TextRulesId.
- [A19 minor] Apostrophe, O(n^2), soft-break evidence. ACCEPT. U+2019 between letters becomes Apostrophe before pairing (inline/quotes has don’t); nearest-strong computed with one prefix and one suffix pass; soft breaks and spaces are neutral evidence.
- [A20 minor] No GCB in CpInfo. ACCEPT (merged with B3). CpInfo carries GCB and ExtPict; atoms are extended grapheme clusters; the unused full script field is dropped for a script-group column; size estimate revised.
- [A21 minor] Dictionary fallback timing. ACCEPT. Fallback only on a definitive provider failure, never a timeout (contrast worker.mjs:36-38); availability is part of TextRulesId.
- [A22 minor] InlineObject edge-class contradictions; error box. ACCEPT. One cls -> (firstCC, lastCC) table, no 'Object' class in any table, image/raw default Ideo (no autospace), math Alpha; inline error stays breakable CODE-style text as today (emit.cc:94-106), not an atomic box.
- [A23 minor] GL glue measured as ' '. ACCEPT. AdvanceSpec measures and paint emits the actual codepoint (U+00A0 as Word glue; U+202F stays inside the Box, where CSS word-spacing does not apply), so stretchable vs rigid matches CSS.
- [A24 minor] Steps 9-10 prerequisites. ACCEPT. T4's role stylesheet is a hard prerequisite of step 9 (or an engine role->props shim), T3's marker attribute of step 10; both listed in risks and interfaces.
- [A25 minor] TextProps vs measurement keys. ACCEPT (also B6). TextProps are shape-category; measurement keys on T4 measureKey(face); KernCtx exists only inside one run, so its joined string has one face. Verified measure.h:21 and :60-71.
- [A missing items] break-policy-config-knobs hardbreak -> step 11 (accepted); hyphen-url-not-discretionary -> representation step 9, fix step 11 (accepted); scattered-magic-constants -> exact decimals (accepted); kern-context-postpass -> eligibility step 8, break-item kerns step 11 (accepted); missed:2 -> U+3000 decided as IdeoSpace Box with today's behaviour, NBSP/NNBSP measure and paint specified (accepted).
- [A overlaps] T1 owns the soft-break encoding with a bump and provides byte ranges (accepted); T5 owns hlist.h incl. legality and exact weights, T6 delivers -INF/Disc before T5 relies on them (accepted); T7 asserts T5's run instances and adds no-autospace now (accepted); T4 single registry with shape category and projections, doc-lang origin, role stylesheet prerequisite (accepted); T8 two-phase math (accepted); T9 content-hashed keys and definitive-failure fallback (accepted); T2 publishes inline kinds and channels, bumps conditional (accepted); T3 sets copy/attach as node attributes and composes role styles at resolver time (accepted).
- [B1 blocker] Pair penalty after glue; indirect breaks across SP. ACCEPT (same fix as A1). Section.pair takes spacesBetween (UAX#14 §7 Indirect); the penalty precedes the first space glue; Direct breaks add no Penalty items; CI lint on hlist goldens.
- [B2 major] attach belongs to the span's leading edge. ACCEPT. Modelled as a non-inherited node edge attribute applied by the flattener to the first atom; user surface attach('prev')[…] via T2; the note-marker role uses the same attribute; script:super stays a separate paint-only prop.
- [B3 major] Codepoint atoms split clusters. ACCEPT. Probe reproduced (葛+U+E0100 gives a boundary-glued word; the ZWJ family emoji becomes a Latin word). Atoms are extended grapheme clusters classified by base; InterChar, Autospace and KernCtx are cluster-boundary-only; Emoji class behaves ID-like; fixtures for IVS, ZWJ and combining marks added in step 5.
- [B4 major] No white-space model; code spaces stretched unbudgeted. ACCEPT. Probe reproduced (word-spacing:17.25px over 'pMin.x <= pMax.x'). Added whiteSpace {normal, nowrap, pre} and RealizeClass Rigid (word-spacing:0); code defaults to pre (spaces stay inside the Box, today's unbreakability preserved) with emergency separators from step 9; the overflow fix lands in step 4 with doc/structure, inline/fence-edge, pages/paged-doc html churn. Collapse semantics are not changed (L0's job).
- [B5 major] ObjectKind lacks a measured phase. ACCEPT. expand (structure, AdvanceSpec::Object{obj, part}) + resolve (per-part extents, misses as needs) with a Deferred escape for math until T8's split.
- [B6 major] TextProps in StyleId fragment MetricStore; no measurement style on items. ACCEPT. One T4 registry with categories; measureKey/shapeKey/paintKey projections; the run instance's face carries FontRole and is the measurement face; lang is in measureKey (locl).
- [B7 major] Cache key omits tailorings; merge policy. ACCEPT. TextRulesId = hash(pack versions, base chain, canonical tailoring, dict versions + availability); last-wins with diagnostic; T9 stamps use it. T5 itself keeps no cross-document cache (see A9).
- [B8 major] UAX#14 Latin policy changes English breaking. ACCEPT. Latin sections tailor SY/HY/BA/EX/IS inside alphabetic tokens to Prohibit; '-' between letters goes to ExHyphen (App C, step 9, churn called out); '/' and friends only via the EmergencyTable, so Chicago sides stay consistent. Verified emit.cc:379-391 breaks only at ' '/'\t' today.
- [B9 major] Steps 8-10 emit items the legacy breaker cannot represent. ACCEPT. Each new item has a defined lowering (SHY/pattern Disc -> legacy hyphen block with max(0, kern); ExHyphen/Emergency -> word piece penalties); Disc post material and -INF are not produced before step 11; hyphen-url-not-discretionary and the hardbreak part of break-policy-config-knobs are now fixed in step 11. Step 11 is kept last rather than moved earlier because it is T6-led and the widest churn.
- [B10 major] No user-level composite inline box. PARTIALLY ACCEPT. Added the built-in hbox ObjectKind (recursively shaped unbreakable content, BoxModel extents, edge classes from first/last atoms) reachable via T2's box(); it makes the protocol closed over shaped content. REJECT routing error text and note markers through it: error must stay breakable (A22) and markers are fully covered by the attach edge attribute. Lands in step 12 with no existing-fixture churn.
- [B11 major] TC and LB dual class systems. ACCEPT. One tailorable CC per cluster; LB, blanks, autospace, interChar, font role and kern eligibility are columns of CC per Section; tailorings change exactly one value per codepoint; the separate LB hook is gone.
- [B12 major] Surface over-built for the evidence. PARTIALLY ACCEPT. v1 TextProps are lang (existing), hyphens, overflowWrap (renamed from emergency), punct, autospace, whiteSpace; lineBreak, wordBreak, ambiguousWidth, langLocal, punct start/end axes are deferred; v1 packs are compat/und/en/zh-Hans, ja/zh-Hant/ko later as data. REJECT deferring $.text.tailor entirely: the owners require user extensions on equal footing with built-ins; it is kept with per-codepoint keys only and lands last (step 12).
- [B13 major] Junction kerning encoded three ways. ACCEPT. IK::Kern and HItem.kern are deleted; a word-space junction lives in the Word glue's KernCtx natural width, a hyphen/emergency junction in DiscRec.nobrW; discard follows TeX by construction.
- [B14 major] L5 reading the L0 SourceMap. ACCEPT. T2 transports per-node offset maps onto content text nodes at instantiation (L3); the shaper reads only the resolved tree.
- [B15 minor] Paint consuming TextRules / em arithmetic in two places. ACCEPT. resolveWidths writes resolved blank px into ColdRec; 'L7 never calls TextRules' is an invariant; the grid's per-cluster class comes from Grid-mode items; all seven fontPx call sites (emit.cc:115, 120, 274, 275, 289, 324, 759) move to emPx(face).
- [B16 minor] 3-valued copy role; anchor duplicates ids. ACCEPT. Generic node copy attribute Content|Synthetic(kind)|Replace(text) set by L4 and exposed by T2; RunRec carries SynKind (vocabulary T7's); anchors are IA_Anchor points that start a run instance and emit id once.
- [B17 minor] Break-opportunity precedence. PARTIALLY ACCEPT. One boundary function: pair() first, token annotations may only lower Prohibit; one penalty per boundary; EmergencyTable moved out of locale packs and keyed by overflowWrap. REJECT a url/path/identifier token-class table: one separator table covers the corpus and no fixture distinguishes them.
- [B18 minor] PunctEdge axes have no item encoding. ACCEPT (defer). start/end axes removed from v1; today's behaviour is fixed rules (leading/trailing Blank discardable); discardability lives only in item kind (IA_Keep and Realize.discardable removed).
- [B19 minor] Bench gate scope / HItem size. ACCEPT. Gate is end-to-end typeset latency; HItem is 24 B with spans, rawPx, blanks, discs, specs and runs in side tables; Direct breaks and coalesced words add no items.
- [B20 minor] Subsumption completeness. PARTIALLY ACCEPT. Added rows for coarse-source-spans, silent-drops-of-unhandled-kinds and dump-hides-finite-penalties; segmentation-class-preview is owned-by T8; glue-semantics-split is marked shared with T6 and step 3/raw attrs carry a conditional bump. The stretch decision is required before step 11 rather than step 2, because HItem now stores today's exact quantities (capSu, float weights) and bakes in neither model.
- [B missing items] glue-semantics-split shared with T6 (accepted, decision before step 11); hyphen-url-not-discretionary fixed at step 11 (accepted); segmentation-class-preview owned by T8 (accepted); sup-attach-private now uses an edge attribute, depends on T4 script prop, converter output assigned to T1 tooling (accepted); missed:2 U+3000 decided and SHY Disc lowered to the legacy hyphen block (accepted); three non-ad-hoc items given rows (accepted).
- [B overlaps] T6: item type renamed AdvanceSpec, lineExtents/materializeLines/WidthSpec are T6's (accepted); T6 grid: Grid mode supplied, not_generalized row reworded (accepted); T4: one registry with categories and TextRulesId per run via projections (accepted); T3/T4: one locale registry, per-domain sections, T4 mechanics (accepted); T7: SynKind + anchor points, CSS contract for Rigid runs and no-autospace (accepted); T8: two-phase ObjectKind (accepted); T1/T2: offset maps transported by T2, '\n' documented with a bump (accepted); T2: open attrs and declaration channel as hard dependencies, bumps conditional (accepted).
