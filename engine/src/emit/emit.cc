#include "emit.h"

#include "../code/grid.h"

#include "../support/rails.h"
#include "emit_internal.h"
#include "../shape/objects.h"
#include "../shape/textrules.h"
#include "../boxtree/build.h"
#include "../math/ir.h"
#include "../math/env.h"

#include <functional>
#include <type_traits>

#include "../hyphen/hyphen.h"

namespace tsr {


void reportFormula(const ContentNode* n, StrRef formula, const MathScope& scope, const Interner& strs,
                   DiagSink& diags, Arena& arena);

namespace {

// Word spaces absorb cross-space kerning (e.g. Georgia "s. A"): sum-of-words
// measurement misses it, leaving every justified line systematically short.
// Each space between two words the browser shapes as one run gets the
// neighbouring codepoints as its KernCtx — resolveWidths turns that into gap
// = m(prev+' '+next) - m(prev) - m(next) — and a hyphen point gets its
// JUNCTION bigram: the pieces shape as one run when the break is not taken,
// and the browser kerns across the boundary.
//
// (plan P4-01; design T5 run instances) "one shaped run" is the browser's:
// text runs (Plain, Rigid) in one face — FaceStyle, the face's style share.
// A DOM run boundary is not a shaping boundary: Chromium and Firefox kern
// across a link, a reference or a colour change in the same font, and never
// across a font change (italic → roman, real-world-report #1), letter-
// spacing (LetterSpaced) or an inline block (Pinned, Object, a spacer), none
// of which is a text run of the same face. Eligibility stays compat
// (kernEligible: cp < U+2000) until the classes decide it (plan P4-05).
void kernContexts(HList& h, Interner& strs, const StyleTable& styles) {  // the carriers, before the TeX form
  const u32 n = (u32)h.items.size();
  auto text = [&](const HItem& it) { return strs.get(h.specs[it.aux].str); };
  auto isWord = [&](const HItem& it) {
    if (it.k != IK::Box) return false;
    const RunRec& r = h.runs[it.run];
    return (r.rc == RealizeClass::Plain || r.rc == RealizeClass::Rigid) && h.specs[it.aux].str != 0;
  };
  auto faceOf = [&](const HItem& it) { return faceStyleOf(styles.get(h.runs[it.run].face)); };
  auto lastCp = [&](std::string_view t) -> std::string {
    if (t.empty()) return {};
    u32 cp = utf8PrevCp(t, (u32)t.size());
    if (!kernEligible(cp)) return {};  // no cross-space kern vs CJK or symbols
    u32 i = (u32)t.size();
    while (i > 0 && ((u8)t[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0) i--;
    return std::string(t.substr(i));
  };
  auto firstCp = [&](std::string_view t) -> std::string {
    if (t.empty()) return {};
    u32 i = 0;
    u32 cp = utf8Next(t, i);
    if (!kernEligible(cp)) return {};
    return std::string(t.substr(0, i));
  };
  for (u32 i = 1; i + 1 < n; i++) {
    const HItem& it = h.items[i];
    const bool hyph = it.k == IK::Disc;
    if (!hyph && !(it.k == IK::Glue && it.cls == (u8)GC::Word)) continue;
    const u32 before = i - 1, after = i + 1;
    const HItem& a = h.items[before];
    const HItem& b = h.items[after];
    if (!isWord(a) || !isWord(b)) continue;
    // one shaped run: the words and the space between them in one face
    const FaceStyle fa = faceOf(a);
    if (!(fa == faceOf(b)) || !(fa == faceOf(it))) continue;
    std::string prev = lastCp(text(a));
    std::string next = firstCp(text(b));
    if (prev.empty() || next.empty()) continue;
    AdvanceSpec ks;
    ks.k = AdvanceSpec::KernCtx;
    ks.prev = strs.intern(prev);
    ks.next = strs.intern(next);
    ks.tri = strs.intern(hyph ? prev + next : prev + " " + next);
    if (hyph) {
      h.discs[it.aux].spec = (u32)h.specs.size();
      h.specs.push_back(ks);
    } else {
      ks.str = h.specs[it.aux].str;
      h.specs[it.aux] = ks;
    }
  }
}


// ---- the inline stream as HList items (plans P1-12, P4-02) ------------------
// The paragraph shaper. A unit's inline content is flattened first: walk()
// and indent() only record, and finish() scans the records into the
// paragraph context (shape/context.h) — every cluster in reading order
// across node edges, code and objects as evidence — and resolves it. The
// emission then replays the records in reading order with that context:
// what precedes a text node's first cluster, what follows an object, how an
// ambiguous mark is set are the paragraph's, never reset by markup
// (findings emitter/paragraph-blind-script-context, emitter/missed:1,
// emitter/missed:4). Every push is a carrier — a Box, a Glue or a Disc —
// with its break penalty after it (`pend`) and its run key; finish() writes
// the penalties in TeX form, adds the InterChar glue where the rendered gap
// is CJK, and forms the run instances.
struct HlInline final : InlineSink {
  EmitEnv& E;
  Interner& strs;
  StyleTable& styles;
  EmitSettings cfg;
  // the open unit's list is built here (inline streams never nest) and
  // copied to the unit, exactly sized, by finish(): the scratch keeps its
  // capacity from unit to unit
  Flow* cur = nullptr;
  HList B;
  std::vector<float> pend;  // per carrier: the break penalty after it (kPenInf = none)
  std::vector<u8> gapKind;  // per carrier: 1 a CJK char, 2 a closing glyph, 0 other
  bool single = false;      // the open run admits no other carrier
  StrRef anchorNext = 0;    // an anchor for the next Box or Disc (a labelled ref's first)
  explicit HlInline(EmitEnv& e) : E(e), strs(e.strs), styles(e.styles), cfg(e.cfg) {}

  // -- the flatten: records, then the paragraph context -----------------------
  struct Record {
    const ContentNode* n = nullptr;  // null: the paragraph indent
    ICtx ctx;
    StyleId st = 0;
    Span span;
    double px = 0, em = 0;
  };
  Flow* recUnit = nullptr;
  std::vector<Record> recs, todo;  // (todo: the replay's, its capacity kept)
  std::vector<CtxEntry> cx;  // the unit's paragraph context
  bool ambiguous = false;    // … holds an ambiguous mark
  struct Mark {
    const void* key;  // the text, code or object node (an error node: its ⚠ text)
    u32 at;           // its first entry
  };
  std::vector<Mark> marks;
  size_t markAt = 0;
  void record(Flow& u, Record r) {
    if (recUnit && recUnit != &u) replay(*recUnit);  // (inline streams never interleave)
    recUnit = &u;
    recs.push_back(r);
  }
  MarkClass marksOf(StyleId st) const {
    const StrRef lang = styles.get(st).lang;
    return markClassOf(lang ? strs.get(lang) : std::string_view{});
  }
  void scanText(const void* key, std::string_view s, StyleId st) {
    marks.push_back({key, (u32)cx.size()});
    const MarkClass m = marksOf(st);
    for (u32 i = 0; i < s.size();) {
      const u32 start = i;
      i = clusterEnd(s, i);
      u32 j = start;
      const u32 cp = (u8)s[start] < 0x80 ? (u8)s[start] : utf8Next(s, j);
      if (cp == ' ' || cp == '\t') cx.push_back({CtxEntry::Blank});
      else cx.push_back(ctxChar(cp, m, ambiguous));
    }
  }
  void scanEvidence(const void* key, CtxEntry::K k) {
    marks.push_back({key, (u32)cx.size()});
    cx.push_back({k});
  }
  // the flatten table again (shape()'s dispatch), for the context only
  void scan(const ContentNode* n) {
    switch (kKinds[(u16)n->kind].inl) {
      case InlineShape::Text:
        scanText(n, strs.get(n->str), n->style);
        return;
      case InlineShape::Container:
        for (const ContentNode* k : n->kids) scan(k);
        return;
      case InlineShape::Code:
        scanEvidence(n, CtxEntry::Narrow);
        return;
      case InlineShape::Object:
        scanEvidence(n, objectKind(objectKindOf(n->kind)).lastCC == CC::Alpha ? CtxEntry::Narrow : CtxEntry::Opaque);
        return;
      case InlineShape::Break:
      case InlineShape::Fill:
        cx.push_back({CtxEntry::Opaque});
        return;
      case InlineShape::Error:
        scanText(n, errorMessage(n), n->style);
        return;
      case InlineShape::Skip:
        return;
      case InlineShape::Unsupported:  // its error box
        scanEvidence(n, CtxEntry::Narrow);
        return;
    }
  }
  // a node's first context entry (~0u: none — the replay keeps the scan's order)
  u32 contextOf(const void* key) {
    for (size_t k = markAt; k < marks.size(); k++)
      if (marks[k].key == key) {
        markAt = k + 1;
        return marks[k].at;
      }
    return ~0u;
  }
  // what precedes entry `at` in the paragraph, as the text state machine
  // counts it: Cjk a CJK letter that takes CJK–Latin glue (Han, kana; a dash
  // or an ellipsis set wide), Wide one that takes none (Hangul, an
  // ideographic space: plan P4-05), Punct a CJK punctuation glyph
  // (plan P4-08) Latin: a letter or digit (CSS text-autospace's alpha and
  // numeric: CJK–Latin glue beside it); LatinPunct: Latin punctuation
  enum class Prev : u8 { None, Latin, LatinPunct, Cjk, Wide, Punct };
  static bool alnum(u32 cp) { return isLetter(cp) || ccOf(cp) == CC::Digit; }
  // UAX #14: no break before CL CP EX IS SY (LB13) or QU (LB19) — the
  // narrow non-starters —, nor after OP (LB14) or QU
  static bool narrowNoStart(u32 cp) {
    const CC c = ccOf(cp);
    return c == CC::CloseN || c == CC::Excl || c == CC::Infix || c == CC::Solidus || c == CC::QuoteN;
  }
  static bool narrowNoEnd(u32 cp) {
    const CC c = ccOf(cp);
    return c == CC::OpenN || c == CC::QuoteN;
  }
  // an entry set as CJK: its class, or an ambiguous mark as resolved
  static bool entryWide(const CtxEntry& e) {
    return isAmbQuote(e.cp) || isAmbDashOrEllipsis(e.cp) ? e.wide : isWide(e.cp);
  }
  Prev prevAt(u32 at) const {
    if (at == ~0u || at == 0 || at > cx.size()) return Prev::None;
    const CtxEntry& e = cx[at - 1];
    switch (e.k) {
      case CtxEntry::Blank:
      case CtxEntry::Opaque:
        return Prev::None;
      case CtxEntry::Narrow:
        return Prev::Latin;
      case CtxEntry::Char:
        if (!entryWide(e)) return alnum(e.cp) ? Prev::Latin : Prev::LatinPunct;
        if (isPunctGlyph(e.cp)) return Prev::Punct;
        return isAmbDashOrEllipsis(e.cp) || takesAutospace(e.cp) ? Prev::Cjk : Prev::Wide;
    }
    return Prev::None;
  }
  // the break after an inline object (finding emitter/missed:1), by what
  // follows it: never before a closer; a formula glued to Latin text or code
  // stays with it (UAX #14 AL × AL, AL × OP); before a CJK character, an
  // opening glyph, a blank or another object it may break
  float breakAfterObject(ObjKind k, u32 at) const {
    if (at == ~0u || at + 1 >= cx.size()) return 0.0f;
    const CtxEntry& e = cx[at + 1];
    const bool alpha = objectKind(k).lastCC == CC::Alpha;
    switch (e.k) {
      case CtxEntry::Blank:
      case CtxEntry::Opaque:
        return 0.0f;
      case CtxEntry::Narrow:
        return alpha ? kPenInf : 0.0f;
      case CtxEntry::Char: {
        const u32 c = e.cp;
        const bool latinCloser = c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?' || c == ')' ||
                                 c == ']' || c == '}' || c == '%' || c == '\'' || c == '"';
        if (isClosePunct(c) || noStart(c) || latinCloser) return kPenInf;
        return alpha && !entryWide(e) ? kPenInf : 0.0f;
      }
    }
    return 0.0f;
  }
  void replay(Flow& u) {
    cx.clear();
    marks.clear();
    markAt = 0;
    ambiguous = false;
    for (const Record& r : recs)
      if (r.n) scan(r.n);
    resolveContext(cx, ambiguous);
    todo.clear();
    todo.swap(recs);
    recUnit = nullptr;
    for (const Record& r : todo) {
      if (r.n) doWalk(r.n, u, r.ctx);
      else doIndent(u, r.st, r.span, r.px, r.em);
    }
    cx.clear();
    marks.clear();
  }
  std::string errorMessage(const ContentNode* n) const {
    std::string msg = "\xE2\x9A\xA0 ";  // ⚠
    for (const ArgVal& a : n->args)
      if (a.key == ArgK::message && a.tag == ArgTag::Str) msg += strs.get(a.ref);
    return msg;
  }

  void open(Flow& u) {
    if (cur == &u) return;
    cur = &u;
    B.items.clear();
    B.side.clear();
    B.cold.clear();
    B.specs.clear();
    B.discs.clear();
    B.runs.clear();
    B.objs.clear();
    B.parts.clear();
    B.hasDeferred = false;
    pend.clear();
    gapKind.clear();
    single = false;
    glueBan = ~(size_t)0;
    noBreakNext = false;
    collapsibleAt = ~(size_t)0;
  }
  static RunRec key(StyleId face, const ICtx& ctx, RealizeClass rc) {
    RunRec r;
    r.face = face;
    r.link = ctx.url;
    r.syn = ctx.synKind;
    r.copy = ctx.copy;
    r.synName = ctx.syn;
    r.copyText = ctx.copyText;
    r.copyGroup = ctx.copyGroup;
    r.rc = rc;
    r.error = ctx.error;
    return r;
  }
  static u8 firstCc(std::string_view s) {
    if (s.empty()) return 0;
    u32 i = 0;
    return (u8)ccOf(utf8Next(s, i));
  }
  bool nowrap = false;  // (plan P4-04, text.wrap) the text being shaped breaks nowhere inside
  bool noBreakNext = false;     // (plan P4-05) a word joiner: the glue before the next box breaks nowhere
  size_t collapsibleAt = ~(size_t)0;  // the item a typed space last made (the next one collapses)
  u32 push(Flow& u, IK k, u8 cls, u8 attrs, const RunRec& rk, const AdvanceSpec& spec, Span span,
           float x, float pen) {
    open(u);
    if (nowrap && pen > -kPenInf) pen = kPenInf;
    if (noBreakNext) {
      if (k == IK::Glue && pen > -kPenInf) pen = kPenInf;
      else if (k != IK::Glue) noBreakNext = false;
    }
    HList& h = B;
    HItem it;
    it.k = k;
    it.cls = cls;
    it.attrs = attrs;
    it.x = x;
    it.aux = (u32)h.specs.size();
    h.specs.push_back(spec);
    it.cold = (u32)h.cold.size();
    ColdRec c;
    c.srcStart = span.start;
    c.srcEnd = span.end;
    if (anchorNext && (k == IK::Box || k == IK::Disc)) {
      it.attrs |= IA_Anchor;
      c.anchor = anchorNext;
      anchorNext = 0;
    }
    h.cold.push_back(c);
    joinRun(it, rk);
    h.items.push_back(it);
    pend.push_back(pen);
    u8 gk = 0;
    if (k == IK::Box) {
      if (rk.rc == RealizeClass::LetterSpaced || (rk.rc == RealizeClass::Pinned && rk.syn != SynKind::Indent))
        gk = 1;
      else if (rk.rc == RealizeClass::BlankBearing && !(kCCFlags[cls] & kCC_open))
        gk = 2;
    }
    gapKind.push_back(gk);
    return (u32)h.items.size() - 1;
  }
  // Run instances, formed as the carriers arrive: consecutive carriers share
  // a run while (face, link, syn, copy, rc, error) agree; punctuation
  // glyphs, pinned boxes, objects, indents and spacer glue (autospace,
  // object space) are runs of their own; a blank joins its glyph's run (an
  // opening glyph's leading blank opens it); penalties and InterChar glue
  // take their owner's run (finish). (plan P4-01; design T5) An anchor is a
  // point: its item opens a run — the run's anchor, its id painted once,
  // where the run starts — and what follows may join it.
  void joinRun(HItem& it, const RunRec& rk) {
    HList& h = B;
    const size_t i = h.items.size();
    const bool anchored = it.attrs & IA_Anchor;
    // (a blank displaced by an attach, plan P4-07, is a spacer of its own)
    const bool isBlankGlue = it.k == IK::Glue && it.cls == (u8)GC::Blank && !(it.attrs & IA_Displaced);
    const bool leadingBlank = isBlankGlue && (it.attrs & IA_OwnedByNext);
    const bool joins = (isBlankGlue && !leadingBlank) ||
                       (it.k == IK::Box && rk.rc == RealizeClass::BlankBearing && i > 0 &&
                        isBlank(i - 1, /*ownedByNext=*/true));
    if (joins && !h.runs.empty()) {
      it.run = (u32)h.runs.size() - 1;
      if (anchored) h.runs.back().anchor = B.cold[it.cold].anchor;  // its glyph's run opened at its blank
      return;
    }
    RunRec k = rk;
    if (leadingBlank) k.rc = RealizeClass::BlankBearing;  // its glyph's run
    k.anchor = anchored ? B.cold[it.cold].anchor : 0;
    const bool alone = leadingBlank ||
                       (it.k == IK::Box && (k.rc == RealizeClass::BlankBearing || k.rc == RealizeClass::Pinned ||
                                            k.rc == RealizeClass::Object)) ||
                       (it.k == IK::Glue && (it.cls == (u8)GC::Autospace || it.cls == (u8)GC::ObjectSpace ||
                                              it.cls == (u8)GC::Fill || (it.attrs & IA_Displaced)));
    if (h.runs.empty() || alone || single || anchored || !sameRunKey(h.runs.back(), k)) {
      h.runs.push_back(k);
      single = alone;
    }
    it.run = (u32)h.runs.size() - 1;
  }
  static bool sameRunKey(const RunRec& a, const RunRec& b) {
    return a.face == b.face && a.link == b.link && a.syn == b.syn && a.copy == b.copy && a.synName == b.synName &&
           a.copyText == b.copyText && a.copyGroup == b.copyGroup && a.rc == b.rc && a.error == b.error;
  }
  const RunRec& runOf(size_t i) const { return B.runs[B.items[i].run]; }
  // a synthetic or object item: its width is defined at emit
  void fixWidth(Flow&, u32 i, double px, Su w) {
    HItem& it = B.items[i];
    it.w = w;
    it.st |= IS_Resolved;
    B.cold[it.cold].rawPx = px;
  }
  // the last carrier (a punctuation blank; plan P4-07: a typed space, a
  // blank an attach displaces), and its run when it was the run's only one —
  // the next carrier opens a run of its own
  void pop(Flow&) {
    HList& h = B;
    const u32 r = h.items.back().run;
    h.items.pop_back();
    h.specs.pop_back();
    h.cold.pop_back();
    pend.pop_back();
    gapKind.pop_back();
    if (h.items.empty() || h.items.back().run != r) {
      h.runs.pop_back();
      single = true;
    }
  }
  size_t count(const Flow& u) const { return cur == &u ? B.items.size() : 0; }
  // carrier predicates (the open unit)
  bool isCjkChar(size_t i) const { return gapKind[i] == 1; }  // a CJK char, pinned or letter-spaced
  bool isObject(size_t i) const { return B.items[i].k == IK::Box && runOf(i).rc == RealizeClass::Object; }
  const InlineObject& objectOf(size_t i) const { return B.objs[B.parts[B.specs[B.items[i].aux].obj].obj]; }
  bool isBlank(size_t i, bool ownedByNext) const {
    const HItem& it = B.items[i];
    return it.k == IK::Glue && it.cls == (u8)GC::Blank &&
           ((it.attrs & IA_OwnedByNext) != 0) == ownedByNext;
  }

  // -- the item kinds ---------------------------------------------------------
  size_t glueBan = ~(size_t)0;  // an attach edge (walk): no synthesized glue at this item
  void autospace(Flow& u, StyleId st, const ICtx& ctx, Span span) {
    // (plan P4-02) none at an attach edge — it would be the break the
    // attach forbids (design T5: synthesized glue suppressed); (plan P4-04)
    // none where either side's text.autospace is none — a note's reference
    // mark's (its role style, plan P4-07: the mark hugs the text on both
    // sides, notes-design §1; a raised baseline alone says nothing); none
    // after a punctuation blank (a blank an attach displaced: the glyph's
    // spacing is there already)
    if (count(u) == glueBan || styles.get(st).autospace == AUTOSPACE_NONE ||
        (count(u) > 0 && (styles.get(runOf(count(u) - 1).face).autospace == AUTOSPACE_NONE ||
                          isBlank(count(u) - 1, /*ownedByNext=*/false))))
      return;
    double px = kCjkBoundaryEm * E.fontPx(st);
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = kCjkBoundaryEm;
    sp.str = E.spaceRef;
    u32 i = push(u, IK::Glue, (u8)GC::Autospace, 0, key(st, ctx, RealizeClass::Plain),
                 sp, span, 1.0f, 0.0f);
    fixWidth(u, i, px, suRoundPx(px));
  }
  // a punctuation blank of `em` (the rules' BLANK rows, plan P4-04)
  void blank(Flow& u, StyleId st, const ICtx& ctx, Span span, double em, bool ownedByNext, float pen) {
    const double px = em * E.fontPx(st);
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = em;
    sp.str = E.spaceRef;
    u32 i = push(u, IK::Glue, (u8)GC::Blank, ownedByNext ? IA_OwnedByNext : 0,
                 key(st, ctx, RealizeClass::Plain), sp, span, 0.0f, pen);
    fixWidth(u, i, px, suRoundPx(px));
  }
  // (plan P4-03; design T5 per-item spans) the source of a text's cooked
  // bytes [a, b): exact through its cooked→raw map (the identity without
  // one) when the text is its own source, else the node's span; an empty
  // range is a point (a hyphen, the boundary glue before a cluster)
  struct TextSource {
    const ContentNode* n = nullptr;
    u32 raw(u32 k) const {  // the raw offset (from span.start) of cooked byte k
      if (!n->nrawmap) return k;
      u32 lo = 0, hi = n->nrawmap / 2;  // the last pair at or before k
      while (hi - lo > 1) {
        const u32 mid = (lo + hi) / 2;
        if (n->rawmap[2 * mid] <= k) lo = mid;
        else hi = mid;
      }
      return n->rawmap[2 * lo + 1] + (k - n->rawmap[2 * lo]);
    }
    // (a byte the parser inserted — a space after a reference — maps where
    // the next one does: it has no extent; a byte it removed — a joined
    // line's newline — is in neither neighbour's)
    Span of(u32 a, u32 b) const {
      if (!n->srcExact) return n->span;
      const u32 s0 = n->span.start, start = raw(a);
      if (a >= b) return Span{s0 + start, s0 + start};
      const u32 end = std::min(raw(b - 1) + 1, raw(b));
      return Span{s0 + start, s0 + std::max(start, end)};
    }
  };
  TextSource tsrc;  // the text emitText shapes
  void word(std::string_view w, Flow& u, StyleId st, float pen, const ICtx& ctx, Span span) {
    AdvanceSpec sp;
    sp.str = strs.intern(w);
    // (plan P4-04) text.space pre: its spaces are in its boxes, as written — Rigid
    const RealizeClass rc = styles.get(st).space == SPACE_PRE ? RealizeClass::Rigid : RealizeClass::Plain;
    push(u, IK::Box, firstCc(w), 0, key(st, ctx, rc), sp, span, 0.0f, pen);
  }
  // a break inside a word (plan P4-06): a discretionary whose pre is the
  // glyph the break adds at the line end — a dictionary's hyphen (a pattern
  // or soft-hyphen point) — or nothing (after an explicit hyphen, an
  // emergency break); unbroken, the pieces shape as one run
  void disc(Flow& u, StyleId st, const ICtx& ctx, Span span, StrRef glyph, float pen) {
    open(u);
    HList& h = B;
    DiscRec d;
    d.pre = (u32)h.side.size();
    if (glyph) {
      AdvanceSpec hs;
      hs.str = glyph;
      HItem pre;
      pre.cls = firstCc(strs.get(glyph));
      pre.aux = (u32)h.specs.size();
      h.specs.push_back(hs);
      pre.cold = (u32)h.cold.size();
      ColdRec pc;
      pc.srcStart = span.start;
      pc.srcEnd = span.end;
      h.cold.push_back(pc);
      d.preN = 1;
      h.side.push_back(pre);
    }
    u32 i = push(u, IK::Disc, 0, 0, key(st, ctx, RealizeClass::Plain), AdvanceSpec{}, span,
                 pen, pen);
    h.specs.pop_back();  // a Disc's aux is its DiscRec
    h.items[i].aux = (u32)h.discs.size();
    h.discs.push_back(d);
  }

  // -- the walk ---------------------------------------------------------------
  // -- the walk: the flatten table (schema `inline` column, plan P1-13) -------
  // attach (plan P2-08; design T4 Extent text.attach): no break between an
  // inline extent and the item before it (prev) or after it (next) — a
  // footnote marker glues to its word, never a line start
  void walk(const ContentNode* n, Flow& u, ICtx ctx) override { record(u, {n, ctx}); }
  // (plan P4-07; design T5 step 10, T1 S14) attached to what precedes it, a
  // node's first atom hugs it: the typed space before it is not set (`word
  // ^[note]. end` reads word¹. end, for a note however written), a
  // punctuation glyph's trailing blank moves after it — the marker hugs the
  // glyph (。¹), the blank stays a blank (displaced: a spacer) —, no
  // synthesized glue and no break come between; attached to what follows,
  // the same on its other side
  void doWalk(const ContentNode* n, Flow& u, ICtx ctx) {
    copyPolicy(n, u, ctx);
    const ArgVal* at = attr(n, ArgK::attach);
    if (!at || at->tag != ArgTag::Str) return shape(n, u, ctx);
    const std::string_view a = strs.get(at->ref);
    const bool prev = a != "next", next = a != "prev";
    struct {
      bool on = false;
      HItem it;
      AdvanceSpec spec;
      ColdRec cold;
      RunRec run;
      float pen = 0;
    } moved;
    if (prev && count(u) > 0) {
      if (collapsibleAt == count(u) - 1) popCarrier(u);  // a typed space
      const size_t c = count(u);
      if (c > 0 && isBlank(c - 1, /*ownedByNext=*/false)) {
        moved = {true, B.items[c - 1], B.specs[B.items[c - 1].aux], B.cold[B.items[c - 1].cold],
                 runOf(c - 1), pend[c - 1]};
        popCarrier(u);
      }
    }
    const size_t before = count(u);
    if (prev) glueBan = before;  // no glue between it and what precedes it
    shape(n, u, ctx);
    if (count(u) > before && prev && before > 0 && !(pend[before - 1] <= -kPenInf)) pend[before - 1] = kPenInf;
    if (moved.on) {
      const bool after = count(u) > before;  // (an empty node: the blank as it was)
      RunRec rk = moved.run;
      rk.rc = RealizeClass::Plain;
      const Span sp{moved.cold.srcStart, moved.cold.srcEnd};
      const u32 i = push(u, IK::Glue, (u8)GC::Blank, after ? IA_Displaced : 0, rk, moved.spec, sp, 0.0f, moved.pen);
      B.items[i].w = moved.it.w;
      B.items[i].st = moved.it.st;
      B.cold[B.items[i].cold] = moved.cold;
    }
    if (count(u) == before) return;
    if (next) {
      forbidLast();
      glueBan = count(u);           // nor between it and what follows it,
      collapsibleAt = count(u) - 1;  // nor the typed space after it
    }
  }
  void popCarrier(Flow& u) {
    pop(u);
    collapsibleAt = ~(size_t)0;
  }
  // (plan P3-07; design T7 CopyPolicy) a node's `copy` / `syn` attributes
  // set its text's copy policy, the innermost winning: copy "text",
  // "omit" or "replace:<text>" (taken once for the node, however many runs
  // and lines it spans); a `syn` alone marks synthetic text, which copy
  // omits. data-syn names the kind: the `syn`, else the node's kind.
  u32 replaceSeq = 0;
  void copyPolicy(const ContentNode* n, Flow& u, ICtx& ctx) override {
    const CopyAttr c = copyAttr(n, strs);
    if (!c.marked) return;
    if (c.mode == CopyAttr::Mode::Text) {
      ctx.copy = CopyMode::Text;
      ctx.syn = ctx.copyText = 0;
      ctx.copyGroup = 0;
      return;
    }
    ctx.syn = strs.intern(c.syn);
    if (c.mode == CopyAttr::Mode::Replace) {
      open(u);
      ctx.copy = CopyMode::Replace;
      ctx.copyText = strs.intern(c.replace);
      ctx.copyGroup = ++replaceSeq;  // unique within the top block (one emit pass)
    } else {
      ctx.copy = CopyMode::Omit;
      ctx.copyText = 0;
      ctx.copyGroup = 0;
    }
  }
  // error text is no content (D-R01): copy omits it
  void omitAsError(ICtx& ctx) {
    ctx.copy = CopyMode::Omit;
    ctx.syn = E.errorSyn;
    ctx.copyText = 0;
    ctx.copyGroup = 0;
  }
  void shape(const ContentNode* n, Flow& u, ICtx ctx) {
    switch (kKinds[(u16)n->kind].inl) {
      case InlineShape::Text:
        emitText(n, u, ctx);
        return;
      case InlineShape::Container:
        container(n, u, ctx);
        return;
      case InlineShape::Code:
        code(n, u, ctx);
        return;
      case InlineShape::Object:
        object(n, u, ctx, objectKindOf(n->kind));
        return;
      case InlineShape::Break:
        // a forced break after what precedes it (none at the stream start)
        if (count(u) > 0) pend.back() = -kPenInf;
        return;
      case InlineShape::Fill: {
        // fil glue (plan P2-16): no width, no finite stretch, its line's slack
        AdvanceSpec sp;
        sp.k = AdvanceSpec::Fixed;
        sp.str = E.spaceRef;
        u32 i = push(u, IK::Glue, (u8)GC::Fill, 0,
                     key(E.compose(n->style, ctx.add, ctx.mul), ctx, RealizeClass::Plain), sp,
                     n->span, 0.0f, 0.0f);
        fixWidth(u, i, 0.0, 0);
        return;
      }
      case InlineShape::Error:
        errorText(n, u, ctx);
        return;
      case InlineShape::Skip:
        // (plan P3-13) a labelled node that shows nothing (an index entry)
        // marks its place: a zero-width box carrying its anchor, in a run
        // of its own
        if (StrRef label = attrStr(n, ArgK::label)) {
          AdvanceSpec sp;
          sp.k = AdvanceSpec::Fixed;
          sp.str = E.emptyRef;
          const StrRef outer = anchorNext;
          anchorNext = label;
          const u32 i = push(u, IK::Box, 0, 0, key(E.compose(n->style, ctx.add, ctx.mul), ctx, RealizeClass::Plain),
                             sp, n->span, 0.0f, kPenInf);
          anchorNext = outer;
          single = true;  // nothing joins it: the entry shows nothing
          fixWidth(u, i, 0.0, 0);
        }
        return;
      case InlineShape::Unsupported:
        // a kind that cannot appear inline: an error box, never a silent drop
        // (a block-level one was reported by the normal form: block-in-inline)
        if (levelOf(n->kind) != Level::Block)
          E.diags.add(Sev::Warning, "shape-unsupported", diagSpan(n, u),
                      std::string(kKinds[(u16)n->kind].name) + " cannot appear inline");
        object(n, u, ctx, ObjKind::Error);
        return;
    }
  }
  // a generated node (no span of its own) reports at its unit
  Span diagSpan(const ContentNode* n, const Flow& u) const {
    return n->span.empty() && &u == E.leafFlow ? E.leafSpan : n->span;
  }
  // 禁则 (plans P4-02, P4-05): no break before a non-starter — a closing
  // punct, a stop, a small kana, an iteration mark, the prolonged sound
  // mark (the rules' nostart column) — inline formulas included, nor at the
  // spaces or the boundary glue before it (UAX #14 LB13: even after
  // spaces), so `！ ？` never puts the ？ at a line start
  void noBreakBefore(Flow& u) {
    size_t k = count(u);
    while (k > 0 && B.items[k - 1].k == IK::Glue &&
           (B.items[k - 1].cls == (u8)GC::Word || B.items[k - 1].cls == (u8)GC::Autospace))
      pend[--k] = kPenInf;
    if (k < count(u) && k > 0 && !(pend[k - 1] <= -kPenInf)) pend[k - 1] = kPenInf;
    if (count(u) > 0 && (isCjkChar(count(u) - 1) || isObject(count(u) - 1))) forbidLast();
  }
  // (plan P4-08) a CJK character after Latin punctuation, no glue between:
  // the break after the punctuation is a break (UAX #14: CP ÷ ID, SY ÷ ID,
  // IS ÷ ID), unless it is an opening one or a quote
  void breakAfterLatinPunct(Flow& u) {
    const size_t c = count(u);
    if (c == 0 || B.items[c - 1].k != IK::Box || !(pend[c - 1] >= kPenInf) || nowrap) return;
    const RealizeClass rc = runOf(c - 1).rc;
    if (rc != RealizeClass::Plain && rc != RealizeClass::Rigid) return;
    const std::string_view t = strs.get(B.specs[B.items[c - 1].aux].str);
    if (t.empty() || narrowNoEnd(utf8PrevCp(t, (u32)t.size()))) return;
    pend[c - 1] = 0.0f;
  }
  // the penalty after the last item: forbidden, unless a forced break
  void forbidLast() {
    if (!(pend.back() <= -kPenInf)) pend.back() = kPenInf;
  }

  void container(const ContentNode* n, Flow& u, ICtx ctx) {
    if (n->kind == Kind::link) {  // an internal target (plan P3-04: the resolver's anchor), else its URL
      if (n->anchorTo) ctx.url = {n->anchorTo, true, n->anchorDoc};
      else
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::url && a.tag == ArgTag::Str) ctx.url = {a.ref, false};
    } else if (n->kind == Kind::ref) {
      ref(n, u, ctx);
      return;
    }
    // (plan P4-07; finding emitter/sup-bit-attach-rule) a labelled inline
    // extent — a group (a term spliced mid-paragraph), a styled run — is an
    // inline anchor, as a labelled ref is: its first carrier takes it (the
    // anchor lands where it is, not at its paragraph's start)
    const StrRef outer = anchorNext;
    if (const StrRef label = attrStr(n, ArgK::label); label && !anchorNext) anchorNext = label;
    for (const ContentNode* k : n->kids) doWalk(k, u, ctx);
    if (anchorNext && anchorNext != outer) {  // nothing carried it: its unit does
      if (!u.anchor) u.anchor = anchorNext;
      anchorNext = outer;
    }
  }

  void code(const ContentNode* n, Flow& u, ICtx ctx) {
    // inline code: one box, mono style — Rigid (plan P4-01): its spaces are
    // inside the box, measured as written, so paint keeps the line's
    // justification off them (word-spacing: 0)
    if (!n->kids.empty() && n->kids[0]->kind == Kind::text) {
      StyleId st = E.compose(n->style, ctx.add, ctx.mul);  // mono and its size: rules (plan P3-01)
      // CJK–code boundary glue (plan P4-02: code is Latin-class, as a formula)
      if (count(u) > 0 && isCjkChar(count(u) - 1)) autospace(u, st, ctx, n->span);
      // (plan P4-06; D-X05) a long one is a token like any other: it breaks
      // at the emergency table's separators (its role's text.overflowWrap),
      // its pieces Rigid (its text.space pre)
      const Styling& sty = styles.get(st);
      const std::string_view text = strs.get(n->kids[0]->str);
      nowrap = sty.wrap == WRAP_NOWRAP;
      if (!nowrap && sty.overflowWrap != OVERFLOWWRAP_NORMAL && cfg.urlBreakPenalty < kPenInf &&
          text.size() >= cfg.urlBreakMinLen) {
        u32 chars = 0;
        for (u32 i = 0; i < text.size(); i = clusterEnd(text, i)) chars++;
        if (chars >= cfg.urlBreakMinLen) {
          tsrc.n = n->kids[0];
          emitWord(text, 0, u, st, ctx, chars);
          return;
        }
      }
      AdvanceSpec sp;
      sp.str = n->kids[0]->str;
      push(u, IK::Box, firstCc(text), 0, key(st, ctx, RealizeClass::Rigid), sp, n->span, 0.0f, kPenInf);
    }
  }

  void ref(const ContentNode* n, Flow& u, ICtx ctx) {
    // resolver output: kids = display text; a resolved one links to its
    // target's anchor (plan P3-04: SemInfo.targetAnchor)
    if (n->anchorTo) ctx.url = {n->anchorTo, true, n->anchorDoc};
    ctx.synKind = SynKind::Ref;
    // labelled ref = inline anchor (footnote marker, notes-design.md §1):
    // its first carrier takes the anchor and opens a run (push); the
    // marker glues to what precedes it through its attach (walk), never a
    // line start, like a closing punct
    const StrRef outer = anchorNext;
    if (StrRef label = attrStr(n, ArgK::label)) anchorNext = label;
    for (const ContentNode* k : n->kids) doWalk(k, u, ctx);
    anchorNext = outer;
  }

  void errorText(const ContentNode* n, Flow& u, ICtx ctx) {
    // an error node stays breakable CODE-style text (design T5 A22): tsr-err
    // runs titled with its message (plan P3-16, document-model §9.1)
    omitAsError(ctx);
    for (const ArgVal& a : n->args)
      if (a.key == ArgK::message && a.tag == ArgTag::Str) ctx.error = a.ref;
    ContentNode tmp;
    tmp.kind = Kind::text;
    tmp.span = n->span;
    tmp.style = n->style;
    tmp.str = strs.intern(errorMessage(n));
    emitText(&tmp, u, ctx, n);  // in its style: mono by the default rule (plan P3-01)
  }

  // -- objects (shape/objects.h) ------------------------------------------------
  u32 addObject(Flow& u, ObjKind k, const ContentNode* n, StyleId st) {
    open(u);
    InlineObject ob;
    ob.kind = k;
    ob.firstCC = (u8)objectKind(k).firstCC;
    ob.lastCC = (u8)objectKind(k).lastCC;
    ob.node = n;
    ob.style = st;
    B.objs.push_back(ob);
    return (u32)B.objs.size() - 1;
  }
  // one Box part with its extents; returns the item
  u32 objectBox(Flow& u, u32 obj, const ObjPart& part, StyleId st, const ICtx& ctx, Span span, StrRef str,
                bool resolved, float pen) {
    InlineObject& ob = B.objs[obj];
    if (ob.nParts == 0) ob.part0 = (u32)B.parts.size();
    ob.nParts++;
    B.parts.push_back(part);
    AdvanceSpec bs;
    bs.k = AdvanceSpec::Object;
    bs.obj = (u32)B.parts.size() - 1;
    bs.str = str;
    // the break after it: by what follows it in the paragraph (breakAfterObject)
    u32 b = push(u, IK::Box, ob.firstCC, 0, key(st, ctx, RealizeClass::Object), bs, span,
                 0.0f, pen);
    if (resolved) fixWidth(u, b, suToPx(part.w), part.w);
    return b;
  }

  void object(const ContentNode* n, Flow& u, ICtx ctx, ObjKind k) {
    const float pen = breakAfterObject(k, contextOf(n));
    switch (k) {
      case ObjKind::Math:
        math(n, u, ctx, pen);
        return;
      case ObjKind::Image: {
        // one box sitting on the baseline: the author's w × h, else (plan
        // P3-32; design T9 M12) the host's intrinsic size — a pending
        // object that Measure settles (the object table's image finalizer:
        // emit reads no answer); a 1em placeholder when its src is unsafe
        StyleId st = E.compose(n->style, ctx.add, ctx.mul);
        double iw = 0, ih = 0;
        StrRef src = 0, alt = 0;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::src && a.tag == ArgTag::Str) src = a.ref;
          if (a.key == ArgK::alt && a.tag == ArgTag::Str) alt = a.ref;
          if (a.key == ArgK::w && a.tag == ArgTag::Num) iw = a.num;
          if (a.key == ArgK::h && a.tag == ArgTag::Num) ih = a.num;
        }
        const bool safe = src && safeImageSrc(strs.get(src));  // unsafe: reported at Resolve
        const bool declared = iw > 0 && ih > 0;
        u32 obj = addObject(u, ObjKind::Image, n, st);
        B.objs[obj].src = safe ? src : 0;
        B.objs[obj].alt = alt;
        ObjPart pt;
        pt.obj = obj;
        if (safe && !declared) {
          B.objs[obj].deferred = true;
          B.hasDeferred = true;
          objectBox(u, obj, pt, st, ctx, n->span, 0, false, pen);
          return;
        }
        const double em = E.fontPx(st);
        pt.w = suRoundPx(safe ? iw : em);
        pt.asc = suRoundPx(safe ? ih : em);
        objectBox(u, obj, pt, st, ctx, n->span, 0, true, pen);
        return;
      }
      case ObjKind::Raw: {
        // handler-declared markup: one box of its declared size (1em when
        // undeclared), sitting on the baseline — or (plan P3-28, measure:
        // 'host') of its declared width, its height and baseline the
        // host's at that width (an unanswered box: the block waits)
        StyleId st = E.compose(n->style, ctx.add, ctx.mul);
        double w = 0, hh = 0;
        StrRef html = 0;
        bool host = false;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::html && a.tag == ArgTag::Str) html = a.ref;
          if (a.key == ArgK::w && a.tag == ArgTag::Num) w = a.num;
          if (a.key == ArgK::h && a.tag == ArgTag::Num) hh = a.num;
          if (a.key == ArgK::measure) host = attrEnum(n, ArgK::measure, strs) == 2;  // declared | host
        }
        const double em = E.fontPx(st);
        u32 obj = addObject(u, ObjKind::Raw, n, st);
        B.objs[obj].src = html;
        ObjPart pt;
        pt.obj = obj;
        pt.w = suRoundPx(w > 0 ? w : em);
        pt.asc = suRoundPx(hh > 0 ? hh : em);
        if (host && !(w > 0))
          E.diags.add(Sev::Warning, "raw-measure", n->span,
                      "an inline raw(measure: 'host') is measured at its width: give it w; its declared size is used");
        else if (host && E.boxes && html)
          if (const BoxAnswer a = E.boxes->ask(boxKindOf(strs.get(html)), html, w, n->span); a.ready) {
            pt.asc = suRoundPx(a.baseline);
            pt.desc = suRoundPx(a.h) - pt.asc;
          }
        objectBox(u, obj, pt, st, ctx, n->span, 0, true, pen);
        return;
      }
      case ObjKind::Error: {
        // the error box of a kind that cannot appear inline: its name,
        // measured in the CODE face, unbreakable
        StyleId st = E.compose(n->style, ctx.add + E.mono, ctx.mul);
        omitAsError(ctx);
        const StrRef text = strs.intern(std::string("\xE2\x9A\xA0 ") + kKinds[(u16)n->kind].name);  // ⚠
        ctx.error = strs.intern(std::string(kKinds[(u16)n->kind].name) + " cannot appear inline");
        u32 obj = addObject(u, ObjKind::Error, n, st);
        B.objs[obj].src = text;
        ObjPart pt;
        pt.obj = obj;
        objectBox(u, obj, pt, st, ctx, n->span, text, false, pen);
        return;
      }
    }
  }

  void math(const ContentNode* n, Flow& u, ICtx ctx, float pen) {
    // its source (plan P2-15): fragments and holes, bound as of its epoch
    // (one clean fragment, the common case, is its interned string)
    StrRef srcRef = mathSourceRef(n, strs), formula = srcRef;
    if (!srcRef) {
      const MathSource ms = mathSource(n, strs, &E.diags);
      srcRef = strs.intern(ms.copy);
      formula = strs.intern(ms.text);
    }
    const MathScope scope{E.math, n->declEpoch};
    StyleId st = E.compose(n->style, ctx.add, ctx.mul);
    // CJK–formula boundary glue (App C: formulas are Latin-class)
    if (count(u) > 0 && isCjkChar(count(u) - 1)) autospace(u, st, ctx, n->span);
    // prepare (plan P1-25; design T8 S6): the formula parses here — its
    // diagnostics are this block's — and lays out once the text-font runs it
    // needs are measured: a pending object with one placeholder part, which
    // resolveWidths finalizes through the object table (no layer below emit
    // reads metrics, no block re-emits)
    reportFormula(n, formula, scope, strs, E.diags, E.arena);
    u32 obj = addObject(u, ObjKind::Math, n, st);
    B.objs[obj].src = srcRef;
    B.objs[obj].formula = formula;
    B.objs[obj].epoch = n->declEpoch;
    B.objs[obj].deferred = true;
    B.hasDeferred = true;
    ObjPart pt;
    pt.obj = obj;
    objectBox(u, obj, pt, st, ctx, n->span, srcRef, false, pen);
  }

  // A word of one run — its bytes w at its cooked offset `at`; tokenChars:
  // the clusters of the token it is part of, across style edges (plan P4-02:
  // a URL whose middle is emphasized is one token) — and the breaks inside
  // it (plan P4-06; design T5 step 4, the token pass), each a Disc (the
  // pieces shape as one run unbroken; a junction kern is the Disc's):
  // - soft hyphens: the word's hyphenation points, its only ones (text.
  //   hyphens manual or auto; plan P4-05, finding emitter/missed:2);
  // - in a word (letters, digits, apostrophes, hyphens between its first
  //   and its last letter): an explicit hyphen between two letters, its
  //   pieces as long as the dictionary's minima (ExHyphen, D-X02: nothing
  //   added where the line breaks), and under hyphens auto the points of
  //   each of its parts (a run of letters) that the run's language's
  //   dictionary spells (all its letters in its alphabet, as long as its
  //   minimum);
  // - a token with none of these, as long as break.urlMinLen: the
  //   emergency table's separators (text.overflowWrap separators — Chicago's
  //   URL rule, never inside a scheme) or any cluster boundary (anywhere),
  //   leaving at least emergencyMinPiece clusters on either side (D-X05).
  // A pre run (text.space) takes only emergency breaks; a nowrap run none.
  enum : u8 { kCutNone, kCutPoint, kCutExplicit, kCutEmergency };
  struct Cut {
    u32 at, skip;  // the break's byte in the word, the bytes it drops (a soft hyphen)
    u8 kind;
  };
  std::vector<Cut> cuts_;
  std::vector<u32> clus_, bases_, lower_, pts_, cand_;  // the token pass's scratch
  StrRef dictLang_ = 0;  // the last word's language and its dictionary
  const HyphenDict* dict_ = nullptr;
  bool dictSet_ = false;
  void emitWord(std::string_view w, u32 at, Flow& u, StyleId st, const ICtx& ctx, u32 tokenChars) {
    auto src = [&](u32 a, u32 b) { return tsrc.of(at + a, at + b); };
    const Styling& sty = styles.get(st);
    const bool rigid = sty.space == SPACE_PRE || nowrap;
    const u8 hyphens = sty.hyphens ? sty.hyphens : ctx.hyphens;
    const u8 wrap = nowrap ? OVERFLOWWRAP_NORMAL : sty.overflowWrap ? sty.overflowWrap : OVERFLOWWRAP_SEPARATORS;
    // its soft hyphens (U+00AD) and explicit ones (U+002D, U+2010), in one pass
    bool hasShy = false, hasHyphen = false;
    for (u32 i = 0; i < w.size(); i++) {
      const u8 c = (u8)w[i];
      if (c == '-') hasHyphen = true;
      else if (c == 0xC2 && i + 1 < w.size() && (u8)w[i + 1] == 0xAD) hasShy = true;
      else if (c == 0xE2 && w.substr(i, 3) == "\xE2\x80\x90") hasHyphen = true;
    }
    // the common case: a word with no soft or explicit hyphen, no
    // emergency, that cannot hyphenate (no rule, or too short to)
    const bool plain = !hasShy && !hasHyphen && (wrap == OVERFLOWWRAP_NORMAL || tokenChars < cfg.urlBreakMinLen);
    const bool auto_ = !rigid && hyphens == HYPHENS_AUTO && cfg.hyphenPenalty < kPenInf;
    const HyphenDict* dict = nullptr;
    if (auto_ || hasShy || !plain) {  // (the last language's, the common case, without a lookup)
      if (sty.lang != dictLang_ || !dictSet_) {
        dictLang_ = sty.lang;
        dict_ = E.rt ? E.rt->hyphFor(sty.lang) : &residentHyphenDict();
        dictSet_ = true;
      }
      dict = dict_;
    }
    if (plain && (!auto_ || !dict || w.size() < dict->minWord())) {
      word(w, u, st, kPenInf, ctx, src(0, (u32)w.size()));
      return;
    }
    // the clusters: their first bytes (then the word's end) and base
    // codepoints, a single codepoint's flagged (bit 31: more than one)
    // (an ASCII word — no CR, no space inside a word — is a cluster per byte)
    std::vector<u32>& cl = clus_;
    std::vector<u32>& bs = bases_;
    cl.clear();
    bs.clear();
    if (std::all_of(w.begin(), w.end(), [](char c) { return (u8)c < 0x80 && c != '\r'; })) {
      for (u32 i = 0; i < w.size(); i++) {
        cl.push_back(i);
        bs.push_back((u8)w[i]);
      }
    } else {
      for (u32 i = 0; i < w.size();) {
        cl.push_back(i);
        u32 j = i;
        const u32 cp = utf8Next(w, j);
        i = clusterEnd(w, i);
        bs.push_back(cp | (j == i ? 0 : 0x80000000u));
      }
    }
    const u32 n = (u32)cl.size();
    cl.push_back((u32)w.size());
    auto base = [&](u32 k) { return bs[k] & 0x7FFFFFFFu; };
    std::vector<Cut>& cuts = cuts_;
    cuts.clear();
    if (hasShy) {
      const bool points = !rigid && hyphens != HYPHENS_NONE && cfg.hyphenPenalty < kPenInf;
      for (u32 k = 0; k < n; k++)
        if (base(k) == 0xAD) cuts.push_back({cl[k], 2, points ? kCutPoint : kCutNone});
    }
    if (!rigid) {
      u32 a = 0, b = n;  // the core: its first letter to its last
      while (a < b && !isLetter(base(a))) a++;
      while (b > a && !isLetter(base(b - 1))) b--;
      bool isWord = a < b;
      for (u32 k = a; k < b && isWord; k++) {
        const u32 c = base(k);
        isWord = isLetter(c) || ccOf(c) == CC::Digit || isHyphenChar(c) || c == '\'' || c == 0x2019 || c == 0xAD;
      }
      // (an explicit hyphen's pieces keep the dictionary's minima of
      // letters, as LuaTeX's: no e-|mail, X-|ray)
      const u32 lmin = dict ? dict->leftmin : 2, rmin = dict ? dict->rightmin : 2;
      if (isWord && hasHyphen && cfg.exHyphenPenalty < kPenInf)
        for (u32 k = a + 1; k + 1 < b; k++) {
          if (!isHyphenChar(base(k))) continue;
          u32 l = k, r = k + 1;
          while (l > a && isLetter(base(l - 1))) l--;
          while (r < b && isLetter(base(r))) r++;
          if (k - l >= lmin && r - (k + 1) >= rmin) cuts.push_back({cl[k + 1], 0, kCutExplicit});
        }
      if (isWord && auto_ && !hasShy && dict) {
        for (u32 p = a; p < b;) {
          u32 q = p;
          bool spelled = true;
          lower_.clear();
          while (q < b && isLetter(base(q))) {
            const u32 c = lowerOf(base(q));
            spelled = spelled && !(bs[q] >> 31) && dict->has(c);  // (one codepoint, in its alphabet)
            lower_.push_back(c);
            q++;
          }
          if (spelled && q - p >= dict->minWord()) {
            pts_.clear();
            dict->points(lower_.data(), q - p, pts_);
            for (u32 x : pts_)
              if (x > 0 && x < q - p) cuts.push_back({cl[p + x], 0, kCutPoint});
          }
          p = q + 1;
        }
      }
      auto byAt = [](const Cut& x, const Cut& y) { return x.at < y.at; };
      if (!std::is_sorted(cuts.begin(), cuts.end(), byAt)) std::sort(cuts.begin(), cuts.end(), byAt);
    }
    const bool breaks = std::any_of(cuts.begin(), cuts.end(), [](const Cut& c) { return c.kind != kCutNone; });
    if (!breaks && wrap != OVERFLOWWRAP_NORMAL && tokenChars >= cfg.urlBreakMinLen && cfg.urlBreakPenalty < kPenInf)
      emergencyCuts(w, wrap == OVERFLOWWRAP_ANYWHERE);
    // the pieces and the breaks between them (a break needs a piece on
    // either side; a soft hyphen's bytes are dropped wherever it stands)
    StrRef glyph = 0;
    u32 from = 0;
    for (const Cut& c : cuts) {
      const bool between = c.at > from && c.at + c.skip < w.size();
      if (c.at > from) word(w.substr(from, c.at - from), u, st, kPenInf, ctx, src(from, c.at));
      if (between && c.kind == kCutPoint) {
        if (!glyph) glyph = dict == &residentHyphenDict() ? E.hyphenRef : strs.intern(dict ? dict->hyphenChar : "-");
        disc(u, st, ctx, src(c.at, c.at + c.skip), glyph, (float)cfg.hyphenPenalty);
      } else if (between && c.kind != kCutNone) {
        disc(u, st, ctx, src(c.at, c.at), 0,
             (float)(c.kind == kCutExplicit ? cfg.exHyphenPenalty : cfg.urlBreakPenalty));
      }
      from = std::max(from, c.at + c.skip);
    }
    if (from < w.size()) word(w.substr(from), u, st, kPenInf, ctx, src(from, (u32)w.size()));
  }
  // the emergency table's breaks in a token (clus_: its clusters), into
  // cuts_: before and after its separators, never inside its scheme (the
  // prefix through a leading "scheme://"); anywhere: at every cluster
  // boundary too. Each leaves emergencyMinPiece clusters on either side.
  void emergencyCuts(std::string_view w, bool anywhere) {
    const std::vector<u32>& cl = clus_;
    const u32 n = (u32)cl.size() - 1;
    u32 scheme = 0;
    if (const size_t p = w.find("://"); p != std::string_view::npos && p > 0 && isLetter((u8)w[0])) {
      bool ok = true;
      for (size_t k = 0; k < p && ok; k++) {
        const char c = w[k];
        ok = (u8)c < 0x80 && (isLetter((u8)c) || (c >= '0' && c <= '9') || c == '+' || c == '.' || c == '-');
      }
      if (ok) scheme = (u32)p + 3;
    }
    std::vector<u32>& cand = cand_;  // cluster indices
    cand.clear();
    auto clusterAt = [&](u32 byte) { return (u32)(std::lower_bound(cl.begin(), cl.end(), byte) - cl.begin()); };
    for (u32 k = 0; k < n;) {
      u32 j = cl[k];
      const u32 first = utf8Next(w, j);
      u32 end = 0;
      const u8 side = emergencySepAt(w, cl[k], first, end);
      if (anywhere && k > 0) cand.push_back(k);
      if (!side) {
        k++;
        continue;
      }
      const u32 next = clusterAt(end);  // (a separator of two: one)
      if ((side & 1) && k > 0) cand.push_back(k);
      if ((side & 2) && next < n) cand.push_back(next);
      if (anywhere)
        for (u32 m = k + 1; m < next; m++) cand.push_back(m);
      k = std::max(next, k + 1);
    }
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    const u32 minPiece = (u32)kRule_emergencyMinPiece;
    u32 last = 0;
    for (u32 k : cand) {
      if (cl[k] < scheme || k - last < minPiece || n - k < minPiece) continue;
      cuts_.push_back({cl[k], 0, kCutEmergency});
      last = k;
    }
  }

  // a text node's clusters in its paragraph (`scanned`: the node its
  // context was scanned under — an error node for its ⚠ text)
  void emitText(const ContentNode* n, Flow& u, ICtx ctx, const void* scanned = nullptr) {
    StyleId st = E.compose(n->style, ctx.add, ctx.mul);
    StyleId stCjk = E.compose(st, E.cjk, 1.0f);
    std::string_view s = strs.get(n->str);

    // (plan P4-02) its place in the paragraph context: what precedes its
    // first cluster (a CJK character in the node before, a formula, code)
    // and how its ambiguous marks resolve (shape/context.h) — the context
    // of the run's language (plan P3-30) and of its neighbours across
    // node edges
    const u32 base = contextOf(scanned ? scanned : n);
    tsrc.n = n;
    auto src = [&](u32 a, u32 b) { return tsrc.of(a, b); };
    auto wideAt = [&](u32 k) { return base != ~0u && base + k < cx.size() && cx[base + k].wide; };
    Prev prev = prevAt(base);
    // (plan P4-04) text.space pre: spaces stay in the words, as written;
    // text.wrap nowrap: nothing inside breaks
    const bool pre = styles.get(st).space == SPACE_PRE;
    nowrap = styles.get(st).wrap == WRAP_NOWRAP;
    std::string wordBuf;
    u32 i = 0;
    u32 wordFrom = 0, wordEnd = 0, wordByte = 0;  // the clusters wordBuf holds, its first byte
    auto addWord = [&](u32 start, u32 at) {
      if (wordBuf.empty()) {
        wordFrom = at;
        wordByte = start;
      }
      wordBuf.append(s.data() + start, i - start);
      wordEnd = at + 1;
    };
    // a Latin token's characters (the emergency scan's measure, plan P4-02):
    // its clusters here, and — at an edge of the node — its continuation in
    // the nodes before and after (narrow characters, no blank between)
    auto narrowAt = [&](size_t k) { return cx[k].k == CtxEntry::Char && !cx[k].wide; };
    auto flushWord = [&](bool atEnd = false) {
      if (wordBuf.empty()) return;
      u32 chars = wordEnd - wordFrom;
      if (base != ~0u) {
        if (wordFrom == 0)
          for (size_t k = base; k > 0 && narrowAt(k - 1); k--) chars++;
        if (atEnd)
          for (size_t k = base + wordEnd; k < cx.size() && narrowAt(k); k++) chars++;
      }
      emitWord(wordBuf, wordByte, u, st, ctx, chars);
      wordBuf.clear();
    };
    // the boundary glue before the cluster at byte p: a point there
    auto boundary = [&](u32 p) { autospace(u, st, ctx, src(p, p)); };
    auto lastIsCloseSp = [&] {  // a closing/dot punct's trailing half
      return count(u) > 0 && isBlank(count(u) - 1, /*ownedByNext=*/false);
    };
    // a punctuation glyph that kept no trailing blank (an opener)
    auto lastIsGlyph = [&] {
      return count(u) > 0 && B.items[count(u) - 1].k == IK::Box && runOf(count(u) - 1).rc == RealizeClass::BlankBearing;
    };
    // definedEm > 0: the box's width is DEFINED, never measured, and the
    // renderer pins it to exactly that advance (RealizeClass::Pinned). Used
    // for U+2014/U+2026 (1em single, 2em pairs — App C): canvas and DOM
    // disagree on their advance (full-width-ization, cluster shaping), so
    // measurement cannot predict rendering for them.
    auto pushCjkChar = [&](std::string_view chars, Span span, double definedEm = 0) {
      AdvanceSpec sp;
      sp.str = strs.intern(chars);
      if (definedEm > 0) {
        sp.k = AdvanceSpec::Defined;
        sp.em = definedEm;
      }
      const RealizeClass rc = definedEm > 0 ? RealizeClass::Pinned : RealizeClass::LetterSpaced;
      u32 b = push(u, IK::Box, firstCc(chars), 0, key(stCjk, ctx, rc), sp, span,
                   (float)cfg.cjkJustifyK, 0.0f);
      if (definedEm > 0) {
        double px = definedEm * E.fontPx(stCjk);
        fixWidth(u, b, px, suRoundPx(px));
      }
    };
    // the run's text.punct (plan P3-02), else the document's cjk.punctCompress
    const u8 runPunct = styles.get(st).punct;
    // A punctuation glyph (plan P4-04; finding emitter/punct-compression-
    // control-flow): its blanks are its class's (the rules' BLANK rows: an
    // opener's before it, a closer's or a stop's after it), measured off
    // its advance and standing as glue. Where the previous glyph's trailing
    // blank and this one's leading blank meet, the run's punct mode keeps
    // both (none), one — the previous glyph's, the break between the two —
    // (book) or neither (full); a leading blank right after a glyph that
    // kept none (an opener) is solid (book, full) or rigid (none: a break
    // there would leave the first opener dangling, 禁则); a trailing blank
    // before a glyph with no leading blank (closer + closer) sets solid
    // (book, full) or stays without a break (none). An opener's leading
    // blank is owned by its glyph (IA_OwnedByNext): paint squeezes a side
    // whose own blank is absent. (span: the glyph's source; its blanks share it)
    auto pushPunct = [&](std::string_view ch, Span span, u32 cp) {
      const PunctCompress mode = runPunct ? (PunctCompress)(runPunct - 1) : cfg.punctCompress;
      const Blank bl = blankOf(cp);
      // (plan P4-05) a non-starter's leading blank (a middle dot's) never
      // breaks: the glyph would begin the line
      const bool nostart = isClosePunct(cp) || noStart(cp);
      if (nostart) noBreakBefore(u);
      const float leadPen = nostart ? kPenInf : 0.0f;
      if (bl.l > 0) {
        if (lastIsCloseSp()) {  // two blanks meet
          if (mode == PunctCompress::Full) pop(u);
          else if (mode == PunctCompress::None) blank(u, stCjk, ctx, span, bl.l, true, leadPen);
          else if (nostart) forbidLast();  // (book: the previous glyph's stays, unbreakable before this one)
        } else if (lastIsGlyph()) {  // after a glyph that kept no trailing blank
          if (mode == PunctCompress::None) blank(u, stCjk, ctx, span, bl.l, true, kPenInf);
        } else {
          blank(u, stCjk, ctx, span, bl.l, true, leadPen);  // breakable, NOT stretchable
        }
      } else if (lastIsCloseSp()) {
        if (mode == PunctCompress::None) forbidLast();
        else pop(u);
      }
      AdvanceSpec sp;
      sp.k = AdvanceSpec::MeasuredMinusBlanks;
      sp.str = strs.intern(ch);
      push(u, IK::Box, firstCc(ch), 0, key(stCjk, ctx, RealizeClass::BlankBearing), sp,
           span, 0.0f, kPenInf);
      if (bl.r > 0) blank(u, stCjk, ctx, span, bl.r, false, 0.0f);
    };

    for (u32 e = 0; i < s.size();) {  // e: the cluster (its context entry: base + e)
      const u32 start = i;
      const u32 at = e++;  // its context entry
      i = clusterEnd(s, i);  // one grapheme cluster: a base and its marks
      u32 j0 = start;
      const u32 cp = (u8)s[start] < 0x80 ? (u8)s[start] : utf8Next(s, j0);
      if ((cp == ' ' || cp == '\t') && pre) {
        addWord(start, at);
        prev = Prev::Latin;
        continue;
      }
      if (cp == ' ' || cp == '\t') {
        flushWord();
        // a space right after a space adds none — the browser collapses it
        // (text.space normal; plan P4-04: anywhere in a text — a spliced
        // string's run of spaces —, as at a text's start, where something
        // between two texts rendered nothing: an undefined splice, a counter
        // event), as TeX's input does
        const size_t c = count(u);
        if (c > 0 && collapsibleAt == c - 1) {
          prev = Prev::None;
          continue;
        }
        AdvanceSpec sp;
        sp.str = E.spaceRef;
        push(u, IK::Glue, (u8)GC::Word, IA_SourceSpace, key(st, ctx, RealizeClass::Plain),
             sp, src(start, i), 1.0f, 0.0f);
        collapsibleAt = count(u) - 1;
        prev = Prev::None;
        continue;
      }
      // (plan P4-05; finding emitter/missed:2) what a plain text says about
      // breaking (the rules' classes; UAX #14 GL, ZW, WJ): a no-break space
      // is a space that stretches and never breaks (and never collapses);
      // a zero-width space a break and nothing else; a word joiner no
      // break, around it (inside a word the word stays one)
      const CC ccl = cp < 0x80 ? CC::Other : ccOf(cp);
      if (ccl == CC::NbSpace) {
        flushWord();
        AdvanceSpec sp;
        sp.str = E.spaceRef;
        push(u, IK::Glue, (u8)GC::Word, IA_SourceSpace, key(st, ctx, RealizeClass::Plain),
             sp, src(start, i), 1.0f, kPenInf);
        prev = Prev::None;
        continue;
      }
      if (ccl == CC::ZwSpace) {
        flushWord();
        const size_t c = count(u);
        if (c > 0 && B.items[c - 1].k == IK::Box && pend[c - 1] >= kPenInf) pend[c - 1] = 0.0f;
        continue;
      }
      if (ccl == CC::WordJoiner) {
        if (!wordBuf.empty()) continue;
        if (count(u) > 0) forbidLast();
        noBreakNext = true;
        continue;
      }
      // a CJK character, or a mark the context sets wide (the em dash, the
      // ellipsis: ambiguous classes): a pinned box of a defined width when
      // a defined advance starts here (the rules' ADVANCE rows, plan P4-04:
      // canvas cannot predict their DOM advance — 2em doubled, 1em alone),
      // else a letter-spaced box. A mark the context sets Latin — an English
      // em dash — is ordinary text, below: it measures in the Latin face,
      // where the 1em convention would over-budget it (blog EN pages
      // showed ~2px).
      // (plan P4-05) one that takes no CJK–Latin glue (Hangul, an
      // ideographic space) leaves the state Wide; a non-starter (a small
      // kana, an iteration mark, the prolonged sound mark) never begins a line
      if (!isPunctGlyph(cp) && (isIdeo(cp) || wideAt(at))) {
        const bool glue = takesAutospace(cp) || isAmbDashOrEllipsis(cp);
        flushWord();
        if (prev == Prev::Latin && glue) boundary(start);
        else if (prev == Prev::LatinPunct) breakAfterLatinPunct(u);
        if (noStart(cp)) noBreakBefore(u);
        u32 end = 0;
        if (const DefinedAdvance* da = definedAdvanceAt(s, start, cp, end)) {
          if (end < i) end = i;  // (its first cluster whole)
          pushCjkChar(s.substr(start, end - start), src(start, end), da->em);
          e += da->len - 1;  // its further clusters, one codepoint each
          i = end;
        } else {
          pushCjkChar(s.substr(start, i - start), src(start, i));
        }
        prev = glue ? Prev::Cjk : Prev::Wide;
        continue;
      }
      if (isPunctGlyph(cp)) {
        // a curly quote or apostrophe the context sets Latin (real-world-
        // report #3; plan P4-02: joint pairs, wide punctuation as evidence)
        // is an ordinary Latin glyph, not full-width CJK punctuation with
        // half-em compressible spaces
        if (isAmbQuote(cp) && !wideAt(at)) {
          addWord(start, at);
          prev = Prev::Latin;
          continue;
        }
        flushWord();
        // no CJK–Latin boundary glue next to full-width punctuation: （1322
        // 年） sets solid (GB/T 15834; real-world-report.md)
        pushPunct(s.substr(start, i - start), src(start, i), cp);
        prev = Prev::Punct;
        continue;
      }
      // (plan P4-08) after a CJK character: the CJK–Latin glue before a
      // letter or a digit (CSS text-autospace ideograph-alpha/numeric, App
      // C), none before Latin punctuation — nor a break before a narrow
      // non-starter (圖/表: never a line starting with '/')
      if (prev == Prev::Cjk) {
        if (alnum(cp)) boundary(start);
        else if (narrowNoStart(cp) && count(u) > 0) forbidLast();
      }
      addWord(start, at);
      prev = alnum(cp) ? Prev::Latin : Prev::LatinPunct;
    }
    flushWord(/*atEnd=*/true);
    nowrap = false;
  }

  void indent(Flow& u, StyleId st, Span span, double px, double em) override {
    Record r;
    r.st = st;
    r.span = span;
    r.px = px;
    r.em = em;
    record(u, r);
  }
  void doIndent(Flow& u, StyleId st, Span span, double px, double em) {
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = em;
    sp.str = E.spaceRef;
    RunRec rk = key(st, ICtx{}, RealizeClass::Pinned);
    rk.syn = SynKind::Indent;
    u32 i = push(u, IK::Box, 0, 0, rk, sp, span, 0.0f, kPenInf);
    fixWidth(u, i, px, suRoundPx(px));
  }
  StyleId juBase = 0;
  void base(StyleId st) override { juBase = st; }
  void finish(Flow& u) override;
  void toCell(Flow& tmp, Flow& tc) override {
    tc.hl = std::move(tmp.hl);
    tc.anchor = tmp.anchor;  // a label inside the cell, kept (plan P1-17)
  }
};

// ---- the leaves --------------------------------------------------------------
// The box tree (boxtree/build.cc) decided the structure, the anchors, the
// markers and the indents; emit shapes each leaf's content: its inline
// stream, cells and rows, and its typed payload (plan P1-18).
struct Emitter {
  EmitEnv& E;
  InlineSink& sink;
  Arena& arena;
  DiagSink& diags;
  Interner& strs;
  StyleTable& styles;
  EmitSettings cfg;
  const MeasureNeeds* mathText;
  Emitter(EmitEnv& e, InlineSink& s)
      : E(e), sink(s), arena(e.arena), diags(e.diags), strs(e.strs), styles(e.styles), cfg(e.cfg),
        mathText(e.mathText) {}
  StyleId compose(StyleId base, const StyleDelta& d, float mul) { return E.compose(base, d, mul); }
  double fontPx(StyleId st) { return E.fontPx(st); }

  // an inline stream of `kids` into its own flow (a cell, a caption row, a
  // sidecar line)
  Flow cellOf(const ContentNode* row, ICtx ctx) {
    Flow tmp, tc;
    sink.base(row->style);
    for (const ContentNode* k : row->kids) sink.walk(k, tmp, ctx);
    sink.finish(tmp);
    sink.toCell(tmp, tc);
    return tc;
  }

  // a code block's grid (verbatim-design): its lines of styled runs, their
  // source, its numbering and highlight; its sidecar notes are a second
  // track of their own (plan P3-11: a two-track table)
  std::unordered_map<const ContentNode*, GridData> gridCache;  // per block, for its table rows
  void buildGrid(const ContentNode* n, const LeafSource& ls, const BlockTraits& tr, GridData& g) {
    g.codeStyle = n->style;  // mono at its size: the cascade's (plan P3-01)
    g.features = styles.get(n->style).features;
    g.chRef = strs.intern(kGridProbeLatin);  // the grid's probes (code/grid.h)
    g.cjkChRef = strs.intern(kGridProbeCjk);
    if (StrRef lang = attrStr(n, ArgK::lang)) g.lang = lang;
    g.wrap = attrBool(n, ArgK::wrap, g.wrap);
    g.lineNo = attrInt(n, ArgK::lineNo, g.lineNo);
    if (StrRef hl = attrStr(n, ArgK::hl))  // "3,5-7": validated by the reader
      parseRangeSet(strs.get(hl), g.hlLines, kRailRangeLines, kRailRangeNumber);
    g.snap = tr.snapKerning;
    std::vector<const ContentNode*> bodyKids;
    for (const ContentNode* k : n->kids)
      if (k != ls.sidecar) bodyKids.push_back(k);
    // Two body forms (CH1): a single text child = plain lines split on
    // \n, styled by its code tokens when they were answered (plan P1-19:
    // the answer is folded here, the tree is never rewritten); otherwise
    // each child is one line (seq of styled runs — the leaves' styles
    // were already folded at instantiation).
    const StrRef lang = attrStr(n, ArgK::lang);
    const TokenNeed* tok = bodyKids.size() == 1 && bodyKids[0]->kind == Kind::text && lang && E.rt
                               ? E.rt->tokens(lang, bodyKids[0]->str, tr.codeOverlays)
                               : nullptr;
    if (tok && tok->st == ResState::Ready) {
      std::vector<std::vector<TokenRun>> lines;
      tokenLines(strs.get(bodyKids[0]->str), bodyKids[0]->style, E.cascade, bodyKids[0]->env, tok->runs().data(),
                 tok->runs().size(), strs, styles, lines);
      for (const std::vector<TokenRun>& line : lines) {
        std::vector<CodeRun>& runs = g.lines.emplace_back();
        for (const TokenRun& r : line)
          runs.push_back({strs.intern(r.text), r.style,
                          styles.get(r.style).hang == HANG_CONTENT,
                          r.tag >= 0 ? strs.intern(std::string("tok-") + kTokenTags[r.tag]) : 0});
      }
    } else if (bodyKids.size() == 1 && bodyKids[0]->kind == Kind::text) {
      std::string_view body = strs.get(bodyKids[0]->str);
      size_t pos = 0;
      while (pos <= body.size()) {
        size_t eol = body.find('\n', pos);
        if (eol == std::string_view::npos) eol = body.size();
        g.lines.push_back({{strs.intern(body.substr(pos, eol - pos)), g.codeStyle}});
        if (eol == body.size()) break;
        pos = eol + 1;
      }
    } else {
      // authored structured lines: a run hangs at its content when its
      // style says code.hang content (plan P2-08; it was the comment colour)
      std::function<void(const ContentNode*, std::vector<CodeRun>&)> collect =
          [&](const ContentNode* k, std::vector<CodeRun>& out) {
            if (k->kind == Kind::text) {
              out.push_back({k->str, k->style,
                             styles.get(k->style).hang == HANG_CONTENT});
              return;
            }
            if (k->kind == Kind::comment) return;
            for (const ContentNode* c : k->kids) collect(c, out);
          };
      for (const ContentNode* lineNode : bodyKids) {
        std::vector<CodeRun> runs;
        collect(lineNode, runs);
        g.lines.push_back(std::move(runs));
        g.lineSpans.push_back(lineNode->span.empty() ? n->span : lineNode->span);
      }
      if (n->span.empty()) g.lineSpans.clear();  // generated code: no source
    }
    // (plan P3-07) a body's lines: its slices when it is the source as
    // written (its length is its span's), else the body as a whole
    if (g.lineSpans.empty() && bodyKids.size() == 1 && bodyKids[0]->kind == Kind::text && !n->span.empty()) {
      const ContentNode* t = bodyKids[0];
      const std::string_view body = strs.get(t->str);
      // the source as written, or with every line ending CRLF (the reader
      // keeps LF only): a line's start moves by one per line before it
      const size_t nl = (size_t)std::count(body.begin(), body.end(), '\n');
      const u32 len = t->span.end - t->span.start;
      const bool verbatim = len == body.size() && (!t->span.empty() || body.empty());
      const bool crlf = !verbatim && nl > 0 && len == body.size() + nl;
      const Span whole = t->span.empty() ? n->span : t->span;
      size_t pos = 0;
      u32 line = 0;
      while (pos <= body.size()) {
        size_t eol = body.find('\n', pos);
        if (eol == std::string_view::npos) eol = body.size();
        const u32 at = t->span.start + (u32)pos + (crlf ? line : 0);
        g.lineSpans.push_back(verbatim || crlf ? Span{at, at + (u32)(eol - pos)} : whole);
        line++;
        if (eol == body.size()) break;
        pos = eol + 1;
      }
      if (g.lineSpans.size() != g.lines.size()) g.lineSpans.assign(g.lines.size(), whole);
    }
  }

  void leaf(const LayoutBlock& b, const LeafSource& ls, FlowUnit& u) {
    const ContentNode* n = ls.node;
    E.leafFlow = &u;  // a generated node without a span reports at its leaf
    E.leafSpan = n->span;
    const BlockTraits& tr = b.tr;
    switch (b.layouter) {
      case LayouterId::Paragraph: {
        if (ls.role == LeafSource::Role::MarkerOnly) return;  // its marker alone
        ICtx ctx;
        ctx.hyphens = tr.hyphenate ? HYPHENS_AUTO : HYPHENS_MANUAL;  // (plan P4-06) its par.hyphenate
        sink.base(n->style);  // (plan P4-08) its justification unit's
        sink.copyPolicy(n, u, ctx);
        if (n->kind == Kind::error) {
          sink.walk(n, u, ctx);  // error case renders ⚠ + message
        } else {
          // (a heading's weight and size, its paragraphs' indent: the
          // cascade's, plan P3-01)
          if (ls.paraIndent.unit) {
            const double em = fontPx(n->style);
            const double px = ls.paraIndent.unit == 2 ? (double)ls.paraIndent.v : ls.paraIndent.v * em;
            sink.indent(u, n->style, n->span, px, ls.paraIndent.unit == 2 ? px / em : (double)ls.paraIndent.v);
          }
          if (ls.runIn) {  // (plan P3-34) a description item's term, then a space
            sink.walk(ls.runIn, u, ctx);
            ContentNode* sp = arena.make<ContentNode>();  // (the sink records it until finish)
            sp->kind = Kind::text;
            sp->span = {ls.runIn->span.end, ls.runIn->span.end};
            sp->style = n->style;
            sp->str = E.spaceRef;
            sink.walk(sp, u, ctx);
          }
          for (const ContentNode* k : n->kids) sink.walk(k, u, ctx);  // the block's content
        }
        sink.finish(u);
        return;
      }
      case LayouterId::Grid: {
        GridData& g = u.data.emplace<GridData>();
        if (ls.lineHi == ~0u) {
          buildGrid(n, ls, tr, g);
          return;
        }
        // (plan P3-11) a row of a code block's two-track table: its lines of
        // the block's grid, built once per block
        auto it = gridCache.find(n);
        if (it == gridCache.end()) {
          GridData full;
          buildGrid(n, ls, tr, full);
          it = gridCache.emplace(n, std::move(full)).first;
        }
        const GridData& full = it->second;
        const u32 lo = std::min(ls.lineLo, (u32)full.lines.size()), hi = std::min(ls.lineHi, (u32)full.lines.size());
        g = full;
        g.lines.assign(full.lines.begin() + lo, full.lines.begin() + hi);
        g.lineSpans.clear();
        if (full.lineSpans.size() == full.lines.size())
          g.lineSpans.assign(full.lineSpans.begin() + lo, full.lineSpans.begin() + hi);
        g.firstLine = lo;
        return;
      }
      case LayouterId::Table:  // a container (plan P3-10): its cells' leaves are units
        return;
      case LayouterId::Replaced:
        switch (b.painter) {
          case Painter::Rule:
            u.data.emplace<RuleData>();
            return;
          case Painter::Raw: {
            // pre-rendered passthrough (v2 §4.1); height declared by the
            // handler, defaulting to one leading
            RawData& r = u.data.emplace<RawData>();
            for (const ArgVal& a : n->args) {
              if (a.key == ArgK::html && a.tag == ArgTag::Str) r.html = a.ref;
              if (a.key == ArgK::h && a.tag == ArgTag::Num) r.size.h = a.num;
              if (a.key == ArgK::w && a.tag == ArgTag::Num) r.size.w = a.num;
              if (a.key == ArgK::minWidth && a.tag == ArgTag::Str) {  // (plan P3-14)
                const Len ml = parseLen(strs.get(a.ref));
                r.size.minW = ml.unit == 2 ? (double)ml.v : ml.v * fontPx(n->style);
              }
              // (plan P3-28) a box the host measures at layout's width (its
              // declared height: the fallback)
              if (a.key == ArgK::measure && attrEnum(n, ArgK::measure, strs) == 2)  // declared | host
                r.size.source = SizeSource::Host;
            }
            if (r.html) r.kind = boxKindOf(strs.get(r.html));
            if (r.size.h <= 0) r.size.h = cfg.lineHeight * cfg.baseSizePx;
            return;
          }
          case Painter::Image: {
            // figure-design.md §3: the size spec (the author's dims or, plan
            // P3-32, the host's intrinsic size — Provided, which Layout asks
            // for: emit reads no answer — and a scale); layout resolves the
            // display box. An unsafe scheme (reported by the Resolve scan,
            // plan P1-16) or a failed load paints a placeholder
            ImageData& im = u.data.emplace<ImageData>();
            double iw = 0, ih = 0, scale = 0;
            StrRef srcRef = 0;
            for (const ArgVal& a : n->args) {
              if (a.key == ArgK::src && a.tag == ArgTag::Str) srcRef = a.ref;
              if (a.key == ArgK::alt && a.tag == ArgTag::Str) im.alt = a.ref;
              if (a.key == ArgK::w && a.tag == ArgTag::Num) iw = a.num;
              if (a.key == ArgK::h && a.tag == ArgTag::Num) ih = a.num;
              if (a.key == ArgK::scale && a.tag == ArgTag::Num) scale = a.num;
            }
            const bool safe = srcRef && safeImageSrc(strs.get(srcRef));
            const bool declared = iw > 0 && ih > 0;
            if (safe) im.src = srcRef;
            im.size.w = iw;  // (Provided: the author's one dim, if any)
            im.size.h = ih;
            im.size.scale = scale;
            im.size.source = !safe ? SizeSource::Placeholder : declared ? SizeSource::Declared : SizeSource::Provided;
            // a float's caption rows break to its width (plan P4-06: as a
            // caption, by its role's style — hyphens manual)
            ICtx cctx;
            for (const ContentNode* k : ls.rows) u.cells.push_back(cellOf(k, cctx));
            return;
          }
          case Painter::MathRow: {
            // prepared here, laid out in Measure (plan P1-25): see math()
            MathData& m = u.data.emplace<MathData>();
            // (plan P3-26) its tag part: a track layout measures and places
            // (copied as the part says: an equation number is left out)
            for (const ContentNode* k : ls.rows) {
              ICtx cctx;
              if (const CopyAttr c = copyAttr(k, strs); c.marked && c.mode == CopyAttr::Mode::Omit) {
                cctx.copy = CopyMode::Omit;
                cctx.syn = strs.intern(c.syn);
              }
              u.cells.push_back(cellOf(k, cctx));
            }
            // (plan P2-15) its source; one clean fragment is its interned string
            const MathScope scope{E.math, n->declEpoch, n->style};
            m.src = m.formula = mathSourceRef(n, strs);
            if (!m.src) {
              const MathSource ms = mathSource(n, strs, &diags);
              m.src = strs.intern(ms.copy);
              m.formula = strs.intern(ms.text);
            }
            m.epoch = n->declEpoch;
            m.sizePx = fontPx(n->style);
            m.span = n->span;
            m.style = n->style;
            reportFormula(n, m.formula, scope, strs, diags, arena);
            if (mathText) {  // (plan P1-20) the metrics at hand: it lays out here — a block lacking its
                             // text runs is incomplete (EmitPass); without them resolveWidths finalizes it
              m.box = layoutMathFormula(strs.get(m.formula), /*display=*/true, m.sizePx, arena, strs, diags,
                                        n->span, mathText, true, &scope);
            }
            return;
          }
          case Painter::None:
            return;
        }
        return;
      case LayouterId::Stack:
        return;
    }
  }
};

}  // namespace

static void prepareEnv(EmitEnv& env) {
  env.mono.fontRole = FONTROLE_MONO;
  env.bold.weight = 700;
  env.cjk.script = SCRIPT_CJK;
  env.spaceRef = env.strs.intern(" ");
  env.hyphenRef = env.strs.intern("-");
  env.errorSyn = env.strs.intern("error");
  env.emptyRef = env.strs.intern("");
  env.bulletRef = env.strs.intern("\xE2\x80\xA2");
}
static void shapeTop(const BoxTree& bt, size_t t, Emitter& e, TopBlock& tb) {
  const TopTree& tt = bt.tops[t];
  tb.pid = tt.pid;
  tb.tree = &tt;
  tb.units.assign(tt.leaves.size(), {});
  for (size_t k = 0; k < tt.leaves.size(); k++) e.leaf(tt.blocks[tt.leaves[k]], bt.sources[t][k], tb.units[k]);
}

struct EmitPass::State {
  std::vector<MeasureItem> missing;
  MeasureNeeds needs;
  EmitEnv env;
  HlInline sink;
  Emitter e;  // (reads env.mathText at construction)
  State(EmitEnv en, const MetricStore* metrics)
      : needs{metrics, &en.styles, &en.strs, en.cfg.baseSizePx, &missing, en.cfg.mathReferenceInk}, env(en), sink(env),
        e(prepared(), sink) {}
  EmitEnv& prepared() {
    prepareEnv(env);
    if (needs.metrics) env.mathText = &needs;
    return env;
  }
};
EmitPass::EmitPass(const BoxTree& bt, Arena& arena, Interner& strs, StyleTable& styles, const EmitSettings& cfg,
                   DiagSink& diags, const MetricStore* metrics, const ResourceTable* rt, BoxAsker* boxes)
    : bt_(bt),
      st_(std::make_unique<State>(EmitEnv{arena, diags, strs, styles, cfg, nullptr, rt, bt.math, bt.cascade, boxes},
                                  metrics)) {}
EmitPass::~EmitPass() = default;
bool EmitPass::top(size_t t, TopBlock& out, std::vector<MeasureItem>& missing) {
  st_->missing.clear();
  shapeTop(bt_, t, st_->e, out);
  missing = st_->missing;
  return missing.empty();
}

// The unit's carriers → TeX form and run instances:
// - a Box that may break after it gets Penalty(p) right after it; when an
//   InterChar glue follows instead (the rendered gap after a CJK char is CJK:
//   the next item is a CJK char or a closing glyph — exactly the gap topology
//   layout and paint used), a break of 0 is that glue and any other penalty
//   precedes it;
// - a Glue whose own penalty is not 0 gets it right before it;
// - a Disc keeps its penalty in x.
// Runs: consecutive carriers share a run while (face, link, syn, copyText,
// rc) agree; punctuation glyphs, pinned boxes, objects, indents and spacer
// glue (autospace, object space) are runs of their own; a blank joins its
// glyph's run (a leading one opens it); penalties and InterChar glue take
// their owner's run.
void HlInline::finish(Flow& u) {
  if (recUnit == &u) replay(u);
  if (cur != &u) return;
  HList& h = B;
  const size_t n = h.items.size();
  kernContexts(h, strs, styles);
  // the TeX form, written exactly sized into the unit
  // the rendered gap after a CJK char is CJK: the next item is a CJK char
  // or a closing glyph
  auto gapAfter = [&](size_t i) { return gapKind[i] == 1 && i + 1 < n && gapKind[i + 1] != 0; };
  size_t extra = 0;
  for (size_t i = 0; i < n; i++) {
    const HItem& it = h.items[i];
    const float p = pend[i];
    if (it.k == IK::Glue) extra += p != 0;
    else if (it.k == IK::Box) extra += gapAfter(i) ? 1 + (p != 0) : (p < kPenInf);
  }
  std::vector<HItem>& out = u.hl.items;
  out.clear();
  out.reserve(n + extra);
  auto penalty = [&](float p, size_t i) {
    HItem& pi = out.emplace_back();
    pi.k = IK::Penalty;
    pi.st = IS_Resolved;
    pi.x = p;
    pi.run = h.items[i].run;
    pi.cold = h.items[i].cold;
  };
  for (size_t i = 0; i < n; i++) {
    HItem it = h.items[i];
    const float p = pend[i];
    if (it.k == IK::Glue) {
      if (p != 0) penalty(p, i);
      out.push_back(it);
      continue;
    }
    if (it.k == IK::Disc) {
      it.x = p;
      out.push_back(it);
      const DiscRec& d = h.discs[it.aux];
      for (u32 s = d.pre; s < d.pre + d.preN; s++) h.side[s].run = it.run;
      continue;
    }
    out.push_back(it);
    if (gapAfter(i)) {
      // the gap: letter-spacing weight and capacity of its char (whose
      // cold record it shares)
      if (p != 0) penalty(p, i);
      HItem& g = out.emplace_back();
      g.k = IK::Glue;
      g.cls = (u8)GC::InterChar;
      g.st = IS_Resolved;
      g.x = it.x;
      g.run = it.run;
      g.cold = it.cold;
    } else if (p < kPenInf) {
      penalty(p, i);
    }
  }
  HList& dst = u.hl;
  dst.side.assign(h.side.begin(), h.side.end());
  dst.cold.assign(h.cold.begin(), h.cold.end());
  dst.specs.assign(h.specs.begin(), h.specs.end());
  dst.discs.assign(h.discs.begin(), h.discs.end());
  dst.runs.assign(h.runs.begin(), h.runs.end());
  dst.objs.assign(h.objs.begin(), h.objs.end());
  dst.parts.assign(h.parts.begin(), h.parts.end());
  dst.hasDeferred = h.hasDeferred;
  dst.juStyle = juBase;
  dst.juStr = E.spaceRef;
  juBase = 0;
  cur = nullptr;
}

// A deferred formula (plan P1-13): lay it out now that the store may know
// its text-font runs. Still missing → those join `need`; complete → its parts
// replace the placeholder: Box, then [Penalty(p)] Glue(ObjectSpace) Box per
// part — exactly what emit writes for a formula it could lay out at once —
// each part and glue a run of its own; the placeholder's run, anchor and
// trailing penalty stay with the first / last part.
// a layout's own diagnostics (the parser's were reported at emit, plan
// P1-25): copied once, on the pass that succeeds
static void keepLayoutDiags(const DiagSink& scratch, ObjectEnv& env) {
  if (!env.diags) return;
  for (const Diag& d : scratch.items) env.diags->add(d.sev, d.code, d.span, d.msg);
}

// The formula finalizer (the object table's entry for ObjKind::Math): lay
// the formula out now that the store may know its text-font runs. Still
// missing → those join `need`; complete → its parts replace the
// placeholder: Box, then [Penalty(p)] Glue(ObjectSpace) Box per part —
// exactly what emit wrote for a formula before plan P1-25 — each part and
// glue a run of its own; the placeholder's run, anchor and trailing penalty
// stay with the first / last part.
static bool finalizeFormula(HList& h, size_t& at, MetricStore& store, const EmitSettings& cfg, ObjectEnv& env,
                            std::vector<MeasureItem>& need) {
  const HItem ph = h.items[at];
  const u32 objIdx = h.parts[h.specs[ph.aux].obj].obj;
  InlineObject& ob = h.objs[objIdx];
  {
    std::vector<MeasureItem> missing;
    MeasureNeeds mt{&store, &env.styles, &env.strs, env.docBasePx, &missing, cfg.mathReferenceInk};
    DiagSink scratch;
    const MathScope scope{env.math, ob.epoch, ob.style};
    std::vector<MathSeg> segs =
        layoutMathSegments(env.strs.get(ob.formula), /*display=*/false, emPx(cfg, env.styles.get(ob.style)),
                           env.arena, env.strs, scratch, Span{h.cold[ph.cold].srcStart, h.cold[ph.cold].srcEnd},
                           mathBreaks(cfg), &mt, /*parseDiags=*/false, &scope);
    if (!missing.empty()) {
      need.insert(need.end(), missing.begin(), missing.end());
      return false;
    }
    keepLayoutDiags(scratch, env);
    ob.deferred = false;
    const u32 r = ph.run;
    RunRec boxKey = h.runs[r];
    boxKey.anchor = 0;
    RunRec glueKey = boxKey;
    glueKey.rc = RealizeClass::Plain;
    const StrRef spaceRef = env.strs.intern(" ");
    std::vector<HItem> ins;
    std::vector<RunRec> newRuns;  // after r
    ob.part0 = (u32)h.parts.size();
    ob.nParts = (u32)segs.size();
    for (size_t k = 0; k < segs.size(); k++) {
      ObjPart pt;
      pt.obj = objIdx;
      pt.math = segs[k].box;
      pt.w = segs[k].box->w;
      pt.asc = segs[k].box->asc;
      pt.desc = segs[k].box->desc;
      pt.glueBefore = k ? segs[k].glueBefore : 0;
      h.parts.push_back(pt);
      if (k) {
        newRuns.push_back(glueKey);
        const u32 gr = r + (u32)newRuns.size();
        AdvanceSpec gs;
        gs.k = AdvanceSpec::Object;
        gs.obj = (u32)h.parts.size() - 1;
        gs.str = spaceRef;
        ColdRec gc;
        gc.srcStart = h.cold[ph.cold].srcStart;
        gc.srcEnd = h.cold[ph.cold].srcEnd;
        gc.rawPx = suToPx(segs[k].glueBefore);
        HItem pen;
        pen.k = IK::Penalty;
        pen.st = IS_Resolved;
        pen.x = segs[k].penalty;
        pen.run = gr;
        pen.cold = (u32)h.cold.size();
        if (pen.x != 0) ins.push_back(pen);
        HItem g;
        g.k = IK::Glue;
        g.cls = (u8)GC::ObjectSpace;
        g.st = IS_Resolved;
        g.run = gr;
        g.aux = (u32)h.specs.size();
        h.specs.push_back(gs);
        g.cold = (u32)h.cold.size();
        h.cold.push_back(gc);
        g.w = segs[k].glueBefore;
        ins.push_back(g);
      }
      HItem b = ph;
      b.attrs = k == 0 ? ph.attrs : 0;
      b.st = IS_Resolved;
      b.w = segs[k].box->w;
      AdvanceSpec bs;
      bs.k = AdvanceSpec::Object;
      bs.obj = (u32)h.parts.size() - 1;
      bs.str = k == 0 ? ob.src : 0;
      b.aux = (u32)h.specs.size();
      h.specs.push_back(bs);
      ColdRec bc = h.cold[ph.cold];
      bc.rawPx = suToPx(segs[k].box->w);
      if (k == 0) {
        h.cold[ph.cold] = bc;
        b.cold = ph.cold;
      } else {
        bc.anchor = 0;
        b.cold = (u32)h.cold.size();
        h.cold.push_back(bc);
        newRuns.push_back(boxKey);
        b.run = r + (u32)newRuns.size();
      }
      ins.push_back(b);
    }
    // renumber: the runs after r shift; the placeholder's own trailing
    // penalties now follow the last part
    const u32 shift = (u32)newRuns.size();
    for (size_t j = at + 1; j < h.items.size(); j++) {
      HItem& it = h.items[j];
      if (it.run == r && it.k == IK::Penalty && shift) it.run = r + shift;
      else if (it.run > r) it.run += shift;
    }
    for (HItem& sd : h.side)
      if (sd.run > r) sd.run += shift;
    h.runs.insert(h.runs.begin() + r + 1, newRuns.begin(), newRuns.end());
    h.items.erase(h.items.begin() + (long)at);
    h.items.insert(h.items.begin() + (long)at, ins.begin(), ins.end());
    at += ins.size() - 1;
  }
  return true;
}

// The image finalizer (plan P3-32; design T9 M12): an inline image's box from
// its intrinsic size (boxInfo at width 0; the author's one dim keeps the
// aspect ratio) once the host answered; a failed one is a 1em placeholder
// (its src cleared: paint writes the alt box). Unanswered, it stays pending
// and Measure with it (the image was asked for at Resolve).
static bool finalizeImage(HList& h, size_t& at, MetricStore&, const EmitSettings& cfg, ObjectEnv& env,
                          std::vector<MeasureItem>&) {
  if (!env.boxes) return false;
  HItem& it = h.items[at];
  ObjPart& part = h.parts[h.specs[it.aux].obj];
  InlineObject& ob = h.objs[part.obj];
  ColdRec& c = h.cold[it.cold];
  const BoxAnswer a = env.boxes->ask(BoxKind::Image, ob.src, 0, Span{c.srcStart, c.srcEnd});
  if (a.pending) return false;
  double iw = attrNum(ob.node, ArgK::w, 0), ih = attrNum(ob.node, ArgK::h, 0);
  if (a.ready) {
    intrinsicDims(iw, ih, a.w, a.h);
  } else {
    iw = ih = emPx(cfg, env.styles.get(ob.style));
    ob.src = 0;
  }
  part.w = suRoundPx(iw);
  part.asc = suRoundPx(ih);
  it.w = part.w;
  it.st |= IS_Resolved;
  c.rawPx = suToPx(part.w);
  ob.deferred = false;
  return true;
}

// The pending-object hook (plan P1-25; design T8 S6, T5 owns it later): a
// pending object of a kind with a finalizer is finalized by it in Measure;
// resolveWidths never names a kind.
using ObjectFinalizer = bool (*)(HList& h, size_t& at, MetricStore& store, const EmitSettings& cfg, ObjectEnv& env,
                                 std::vector<MeasureItem>& need);
static constexpr ObjectFinalizer kFinalizers[] = {
    /*Math*/ finalizeFormula, /*Image*/ finalizeImage, /*Raw*/ nullptr, /*Error*/ nullptr};
static void finalizePending(HList& h, MetricStore& store, const EmitSettings& cfg, ObjectEnv& env,
                            std::vector<MeasureItem>& need) {
  bool still = false;
  for (size_t at = 0; at < h.items.size(); at++) {
    const HItem& ph = h.items[at];
    if (ph.k != IK::Box || h.runs[ph.run].rc != RealizeClass::Object) continue;
    const InlineObject& ob = h.objs[h.parts[h.specs[ph.aux].obj].obj];
    if (!ob.deferred) continue;
    const ObjectFinalizer f = kFinalizers[(size_t)ob.kind];
    if (!f || !f(h, at, store, cfg, env, need)) still = true;
  }
  h.hasDeferred = still;
}

// a display formula (a leaf's MathData) lays out the same way: prepared at
// emit, finalized once its text-font runs are measured
static void finalizeDisplay(MathData& m, MetricStore& store, const EmitSettings& cfg, ObjectEnv& env,
                            std::vector<MeasureItem>& need) {
  std::vector<MeasureItem> missing;
  MeasureNeeds mt{&store, &env.styles, &env.strs, env.docBasePx, &missing, cfg.mathReferenceInk};
  DiagSink scratch;
  const MathScope scope{env.math, m.epoch, m.style};
  // (plan P3-29) its rows of cells; one cell is the formula's box
  MathRows* rows = env.arena.make<MathRows>();
  *rows = layoutMathRows(env.strs.get(m.formula), m.sizePx, env.arena, env.strs, scratch, m.span, &mt,
                         /*parseDiags=*/false, &scope);
  if (!missing.empty()) {
    need.insert(need.end(), missing.begin(), missing.end());
    return;
  }
  keepLayoutDiags(scratch, env);
  m.cells = rows;
  m.box = rows->rows[0][0].box;  // (an aligned one: replaced when its group aligns)
}

// (plan P3-29; design T8 S9, D-S11) the display formulas aligned together:
// an equations block's rows share their columns when one of them has `&`
// or a row break; a formula of several rows or cells outside one is a group
// of its own. A group waits until each member is laid out.
static void alignDisplays(TopBlock& tb, Arena& arena) {
  if (!tb.tree) return;
  auto mathOf = [&](u32 block) -> MathData* {
    const LayoutBlock& b = tb.tree->blocks[block];
    return b.leaf() && b.unit < tb.units.size() ? std::get_if<MathData>(&tb.units[b.unit].data) : nullptr;
  };
  auto align = [&](const std::vector<MathData*>& members) {
    std::vector<const MathRows*> group;
    for (MathData* m : members) group.push_back(m->cells);
    std::vector<std::vector<MathBox*>> rows;
    alignMathRows(group, arena, rows);
    for (size_t i = 0; i < members.size(); i++) {
      members[i]->rows.assign(rows[i].begin(), rows[i].end());
      members[i]->box = rows[i].empty() ? members[i]->box : rows[i][0];
      members[i]->grouped = true;
    }
  };
  const std::vector<LayoutBlock>& blocks = tb.tree->blocks;
  std::vector<bool> inGroup(blocks.size(), false);
  for (u32 i = 0; i < blocks.size(); i++) {
    if (blocks[i].traits != TraitsId::Equations) continue;
    std::vector<MathData*> members;
    bool ready = true, aligned = false, done = true;
    for (u32 k = i + 1; k < blocks[i].end; k = blocks[k].end)
      if (MathData* m = mathOf(k)) {
        inGroup[k] = true;
        members.push_back(m);
        ready = ready && m->cells;
        aligned = aligned || (m->cells && m->cells->aligned());
        done = done && m->grouped;
      }
    if (ready && aligned && !done) align(members);
  }
  for (u32 k = 0; k < blocks.size(); k++)
    if (MathData* m = mathOf(k); m && !inGroup[k] && m->cells && m->cells->aligned() && !m->grouped) align({m});
}

MeasureRequest resolveWidths(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const EmitSettings& cfg, ObjectEnv* objects) {
  MeasureRequest req;
  // requests are per measurement face (plan P1-04): paint-only variants of
  // a style share one face and are asked for once
  std::unordered_map<u64, bool> seenWord;
  std::vector<bool> seenStyle(styles.count(), false);
  std::vector<bool> seenFace;
  auto needStyle = [&](StyleId st) {
    if (st < seenStyle.size() && !seenStyle[st]) {
      seenStyle[st] = true;
      FaceId f = store.faceOf(st);
      if (seenFace.size() <= f) seenFace.resize(f + 1, false);
      if (!seenFace[f]) {
        seenFace[f] = true;
        if (!store.hasFaceVmet(f)) req.vmetFaces.push_back(f);
      }
    }
  };
  auto ask = [&](StrRef r, StyleId st) {
    FaceId f = store.faceOf(st);
    u64 k = MetricStore::key(r, f);
    if (!seenWord.count(k)) {
      seenWord[k] = true;
      req.words.push_back({r, f});
    }
  };
  // a KernCtx's three strings: asked for when missing; true when all known
  auto ctxReady = [&](const AdvanceSpec* ks, StyleId st) {
    bool ready = true;
    if (!ks) return ready;
    for (StrRef r : {ks->tri, ks->prev, ks->next})
      if (!store.hasWord(r, st)) {
        ready = false;
        ask(r, st);
      }
    return ready;
  };
  auto ctxPx = [&](const AdvanceSpec& ks, StyleId st) {
    return store.word(ks.tri, st).px - store.word(ks.prev, st).px - store.word(ks.next, st).px;
  };
  std::vector<MeasureItem> need;  // deferred formulas' text-font runs
  auto resolveItems = [&](HList& h) {
    if (h.hasDeferred && objects) finalizePending(h, store, cfg, *objects, need);
    // (plan P4-08; v2 §8) its justification unit: a space of its base style
    if (!h.juSu && h.juStr && !h.items.empty()) {
      if (store.hasWord(h.juStr, h.juStyle)) {  // (quantized as a word space is)
        h.juPx = store.word(h.juStr, h.juStyle).px;
        h.juSu = suCeilPx(h.juPx) + (Su)cfg.epsilonPerWordSu;
      } else {
        ask(h.juStr, h.juStyle);
      }
    }
    for (HItem& it : h.items) {
      if (it.k == IK::Penalty || (it.k == IK::Glue && it.cls == (u8)GC::InterChar)) continue;
      const StyleId st = h.runs[it.run].face;
      needStyle(st);
      if (it.st & IS_Resolved) continue;
      ColdRec& c = h.cold[it.cold];
      if (it.k == IK::Disc) {
        // the hyphen box measures (an explicit hyphen's or an emergency
        // break's Disc has none, plan P4-06); the junction kern applies when
        // NOT broken here: the pieces shape as one run
        const DiscRec& d = h.discs[it.aux];
        HItem* pre = d.preN ? &h.side[d.pre] : nullptr;
        const StrRef hy = pre ? h.specs[pre->aux].str : 0;
        const AdvanceSpec* ks = d.spec != ~0u ? &h.specs[d.spec] : nullptr;
        const bool ready = ctxReady(ks, st);
        if ((!pre || store.hasWord(hy, st)) && ready) {
          if (pre) {
            const WordMet& w = store.word(hy, st);
            pre->w = w.su;
            pre->st |= IS_Resolved;
            h.cold[pre->cold].rawPx = w.px;  // only added to a line when it ends here
          }
          if (ks) {
            double k = ctxPx(*ks, st);
            c.rawPx = (double)(float)k;
            it.w = suCeilPx(k);  // feeds KP's in-line width sum (ceil: never under the run, v2 §7)
          }
          it.st |= IS_Resolved;
        } else if (pre && !store.hasWord(hy, st)) {
          ask(hy, st);
        }
        continue;
      }
      const AdvanceSpec& sp = h.specs[it.aux];
      if (sp.k == AdvanceSpec::Object && h.objs[h.parts[sp.obj].obj].deferred) continue;  // the finalizer's
      const bool ready = ctxReady(sp.k == AdvanceSpec::KernCtx ? &sp : nullptr, st);
      if (store.hasWord(sp.str, st) && ready) {
        const WordMet& w = store.word(sp.str, st);
        if (sp.k == AdvanceSpec::MeasuredMinusBlanks) {
          // glyph advance less its blanks (the rules' BLANK rows, plan
          // P4-04): they are the adjacent Blank glue (or were compressed
          // away); the glyph budgets the word epsilon, as a space does
          const Blank bl = kBlanks[it.cls];
          const double em = emPx(cfg, styles.get(st));
          double gpx = w.px - (bl.l + bl.r) * em;
          if (gpx < 0) gpx = 0;
          it.w = suCeilPx(gpx) + (Su)cfg.epsilonPerWordSu;
          c.rawPx = gpx;
          c.blankLpx = (float)(bl.l * em);
          c.blankRpx = (float)(bl.r * em);
        } else if (it.k == IK::Glue) {
          double px = w.px;
          if (sp.k == AdvanceSpec::KernCtx) {
            // cross-space kerning correction: gap = m(tri) - m(prev) - m(next)
            px = ctxPx(sp, st);
            if (px < 0) px = 0;
          }
          Su su = suCeilPx(px) + (Su)cfg.epsilonPerWordSu;
          it.w = su;
          c.rawPx = px;
        } else {
          it.w = w.su;
          c.rawPx = w.px;
          if (sp.k == AdvanceSpec::Object) h.parts[sp.obj].w = w.su;  // an error box
        }
        it.st |= IS_Resolved;
      } else {
        ask(sp.str, st);
      }
    }
  };
  for (TopBlock& tb : tops) {
    if (objects && objects->diags) objects->diags->pid = tb.pid;  // a layout's diagnostics are its block's
    bool displays = false;
    for (FlowUnit& u : tb.units) {
      if (MathData* m = std::get_if<MathData>(&u.data); m && objects) {
        if (!m->box) finalizeDisplay(*m, store, cfg, *objects, need);
        displays = displays || !m->grouped;
      }
      if (const GridData* g = std::get_if<GridData>(&u.data)) {
        needStyle(g->codeStyle);
        if (g->wrap || g->snap) {  // (plan P3-11: snap-kerning without wrap measures them too)
          for (StrRef probe : {g->chRef, g->cjkChRef}) {
            if (!probe || store.hasWord(probe, g->codeStyle)) continue;
            ask(probe, g->codeStyle);
          }
        }
      }
      resolveItems(u.hl);
      for (TableCell& c : u.cells) resolveItems(c.hl);
    }
    if (displays && objects) alignDisplays(tb, objects->arena);  // (plan P3-29)
  }
  // a formula's text-font runs (math.cc textFontBox): their words, their
  // faces' vmet and, under math.referenceInk (plan P5-01), reference ink
  std::vector<bool> seenInk;
  for (const MeasureItem& m : need) {
    u64 k = MetricStore::key(m.str, m.face);
    if (!seenWord.count(k) && !store.hasFaceWord(m.str, m.face)) {
      seenWord[k] = true;
      req.words.push_back(m);
    }
    if (m.face < seenFace.size() ? !seenFace[m.face] : true) {
      if (seenFace.size() <= m.face) seenFace.resize(m.face + 1, false);
      seenFace[m.face] = true;
      if (!store.hasFaceVmet(m.face)) req.vmetFaces.push_back(m.face);
    }
    if (cfg.mathReferenceInk && (m.face < seenInk.size() ? !seenInk[m.face] : true)) {
      if (seenInk.size() <= m.face) seenInk.resize(m.face + 1, false);
      seenInk[m.face] = true;
      if (!store.hasFaceInk(m.face)) req.inkFaces.push_back(m.face);
    }
  }
  return req;
}

// a formula's parse diagnostics (plan P2-15), against its scope: the map
// placing them in its fragments is assembled only when there are some
void reportFormula(const ContentNode* n, StrRef formula, const MathScope& scope, const Interner& strs,
                   DiagSink& diags, Arena& arena) {
  const std::string_view text = strs.get(formula);
  const MathIR ir = parseMath(text, arena, &scope);
  if (ir.diags.empty()) return;
  const MathSource ms = mathSource(n, strs);
  reportMathDiags(ir, text, n->span, diags, &ms.map);
}

static void unitHeader(std::string& out, const LayoutBlock& b, const FlowUnit& u, const Interner& strs,
                       BoxAsker* boxes) {
  const char* k = "text";
  if (b.layouter == LayouterId::Grid) k = "code";
  else if (b.layouter == LayouterId::Replaced)
    k = b.painter == Painter::Raw ? "raw" : b.painter == Painter::MathRow ? "math" : b.painter == Painter::Image ? "img" : "rule";
  appendf(out, " unit %s indent=%dsu", k, b.x);
  if (StrRef a = b.carry ? b.carry : u.anchor) {
    out += " anchor=\"";
    appendEscaped(out, strs.get(a));
    out += "\"";
  }
  if (b.marker) {
    out += " marker=\"";
    appendEscaped(out, strs.get(b.marker));
    out += "\"";
  }
  if (const GridData* g = std::get_if<GridData>(&u.data)) {
    appendf(out, " lines=%zu", g->lines.size());
    if (g->wrap) out += " wrap";
    if (g->lineNo) appendf(out, " lineNo=%d", g->lineNo);
    if (!g->hlLines.empty()) appendf(out, " hl=%zu", g->hlLines.size());
  }
  if (const MathData* m = std::get_if<MathData>(&u.data); m && m->box)
    appendf(out, " w=%dsu asc=%dsu desc=%dsu", m->box->w, m->box->asc, m->box->desc);
  if (const ImageData* im = std::get_if<ImageData>(&u.data)) {
    // the size spec layout resolves (plan P1-16), with the host's intrinsic
    // size as layout takes it (plan P3-32: an answer, never emit's)
    IntrinsicSize s = im->size;
    if (s.source == SizeSource::Provided) {
      const BoxAnswer a = boxes && im->src ? boxes->ask(BoxKind::Image, im->src, 0, b.span) : BoxAnswer{};
      if (a.ready) intrinsicDims(s.w, s.h, a.w, a.h);
      else s.source = SizeSource::Placeholder;
    }
    if (s.w > 0 || s.h > 0) appendf(out, " intrinsic=%gx%gpx", s.w, s.h);
    if (s.scale > 0) appendf(out, " scale=%g", s.scale);
    if (s.placeholder()) out += " placeholder";
    if (b.floatSide) out += b.floatSide == 1 ? " float=left" : " float=right";
  }
  if (b.tr.align == BlockTraits::Align::Center) out += " centered";
  if (b.tr.singleCenter) out += " single-center";
  out += "\n";
}

static const LayoutBlock& leafOf(const TopBlock& tb, size_t k) { return tb.tree->blocks[tb.tree->leaves[k]]; }

std::string dumpHLists(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles, BoxAsker* boxes) {
  std::string out;
  for (const TopBlock& tb : tops) {
    appendf(out, "top pid=%u units=%zu\n", tb.pid, tb.units.size());
    for (size_t ui = 0; ui < tb.units.size(); ui++) {
      const FlowUnit& u = tb.units[ui];
      unitHeader(out, leafOf(tb, ui), u, strs, boxes);
      dumpHList(out, u.hl, strs, styles, "  ");
      for (size_t ci = 0; ci < u.cells.size(); ci++) {
        appendf(out, "  cell %zu\n", ci);
        dumpHList(out, u.cells[ci].hl, strs, styles, "   ");
      }
    }
  }
  return out;
}

// tsrc --stage=mathir (plan P1-24): each formula's IR and diagnostics, in
// document order
std::string dumpMathIRs(const std::vector<TopBlock>& tops, const Interner& strs, const MathEnv* math) {
  std::string out;
  Arena scratch;
  auto one = [&](const char* kind, u32 pid, std::string_view src, u32 epoch) {
    appendf(out, "%s pid=%u \"", kind, pid);
    appendEscaped(out, src);
    out += "\"\n";
    const MathScope scope{math, epoch};
    out += dumpMathIR(parseMath(src, scratch, &scope), src);
  };
  for (const TopBlock& tb : tops)
    for (const FlowUnit& u : tb.units) {
      if (const MathData* m = std::get_if<MathData>(&u.data); m && m->formula)
        one("display", tb.pid, strs.get(m->formula), m->epoch);
      for (const InlineObject& ob : u.hl.objs)
        if (ob.kind == ObjKind::Math && ob.formula) one("inline", tb.pid, strs.get(ob.formula), ob.epoch);
    }
  return out;
}

std::string dumpMathBoxes(const std::vector<TopBlock>& tops, const Interner& strs) {
  std::string out;
  for (const TopBlock& tb : tops) {
    for (const FlowUnit& u : tb.units) {
      if (const MathData* m = std::get_if<MathData>(&u.data); m && m->box) {
        appendf(out, "display pid=%u\n", tb.pid);
        if (m->rows.size() < 2) out += dumpMathBox(m->box, strs);
        for (size_t r = 0; m->rows.size() >= 2 && r < m->rows.size(); r++) {  // (plan P3-29) its rows
          appendf(out, "row %zu\n", r);
          out += dumpMathBox(m->rows[r], strs);
        }
      }
      for (const HItem& it : u.hl.items) {
        const ObjPart* pt = objectPart(u.hl, it);
        if (!pt || !pt->math) continue;
        appendf(out, "inline pid=%u \"", tb.pid);
        appendEscaped(out, strs.get(u.hl.specs[it.aux].str));
        out += "\"\n";
        out += dumpMathBox(pt->math, strs);
      }
    }
  }
  return out;
}


}  // namespace tsr
