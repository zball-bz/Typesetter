#include "emit.h"
#include "emit_internal.h"
#include "../shape/textrules.h"

#include <functional>
#include <type_traits>

#include "../hyphen/hyphen.h"

namespace tsr {

namespace {

// Word spaces absorb cross-space kerning (e.g. Georgia "s. A"): sum-of-words
// measurement misses it, leaving every justified line systematically short.
// Each plain space between two words of one run (style and link) gets the
// neighbouring codepoints as its KernCtx — resolveWidths turns that into gap =
// m(prev+' '+next) - m(prev) - m(next) — and a hyphen point gets its JUNCTION
// bigram: the pieces shape as one run when the break is not taken, and the
// browser kerns across the boundary.
void kernContexts(HList& h, Interner& strs) {  // the carriers, before the TeX form
  const u32 n = (u32)h.items.size();
  auto text = [&](const HItem& it) { return strs.get(h.specs[it.aux].str); };
  auto isWord = [&](const HItem& it) {
    if (it.k != IK::Box) return false;
    const RunRec& r = h.runs[it.run];
    return (r.rc == RealizeClass::Plain || r.rc == RealizeClass::Rigid) && h.specs[it.aux].str != 0;
  };
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
    // the browser only kerns INSIDE one shaped run: a style or link
    // boundary (italic title → roman period, real-world-report.md) splits
    // the run, so no cross-space kern exists there to budget for
    const RunRec& ra = h.runs[a.run];
    const RunRec& rb = h.runs[b.run];
    if (ra.face != rb.face || ra.face != h.runs[it.run].face || ra.link != rb.link) continue;
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


// ---- the inline stream as HList items (plan P1-12) --------------------------
// The per-node logic is the one the LinebreakBlock emitter had (legacy.cc
// keeps that emitter as the oracle): every push is a carrier — a Box, a Glue
// or a Disc — with today's break penalty after it (`pend`) and its run key.
// finish() writes the penalties in TeX form, adds the InterChar glue where
// the rendered gap is CJK, and forms the run instances.
struct HlInline final : InlineSink {
  EmitEnv& E;
  Interner& strs;
  StyleTable& styles;
  const Config& cfg;
  // the open unit's list is built here (inline streams never nest) and
  // copied to the unit, exactly sized, by finish(): the scratch keeps its
  // capacity from unit to unit
  FlowUnit* cur = nullptr;
  HList B;
  std::vector<float> pend;  // per carrier: the break penalty after it (kPenInf = none)
  std::vector<u8> gapKind;  // per carrier: 1 a CJK char, 2 a closing glyph, 0 other
  bool single = false;      // the open run admits no other carrier
  explicit HlInline(EmitEnv& e) : E(e), strs(e.strs), styles(e.styles), cfg(e.cfg) {}

  void open(FlowUnit& u) {
    if (cur == &u) return;
    cur = &u;
    B.items.clear();
    B.side.clear();
    B.cold.clear();
    B.specs.clear();
    B.discs.clear();
    B.runs.clear();
    B.objs.clear();
    pend.clear();
    gapKind.clear();
    single = false;
  }
  static RunRec key(StyleId face, StrRef url, u16 addFlags, RealizeClass rc) {
    RunRec r;
    r.face = face;
    r.link = url;
    r.syn = (addFlags & BF_REF) ? SynKind::Ref : SynKind::Content;
    r.rc = rc;
    return r;
  }
  static u8 firstCc(std::string_view s) {
    if (s.empty()) return 0;
    u32 i = 0;
    return (u8)ccOf(utf8Next(s, i));
  }
  u32 push(FlowUnit& u, IK k, u8 cls, u8 attrs, const RunRec& rk, const AdvanceSpec& spec, Span span,
           float x, float pen) {
    open(u);
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
  // a run while (face, link, syn, copyText, rc) agree; punctuation glyphs,
  // pinned boxes, objects, indents and spacer glue (autospace, object space)
  // are runs of their own; a blank joins its glyph's run (an opening glyph's
  // leading blank opens it); penalties and InterChar glue take their
  // owner's run (finish).
  void joinRun(HItem& it, const RunRec& rk) {
    HList& h = B;
    const size_t i = h.items.size();
    const bool isBlankGlue = it.k == IK::Glue && it.cls == (u8)GC::Blank;
    const bool leadingBlank = isBlankGlue && (it.attrs & IA_OwnedByNext);
    const bool joins = (isBlankGlue && !leadingBlank) ||
                       (it.k == IK::Box && rk.rc == RealizeClass::BlankBearing && i > 0 &&
                        isBlank(i - 1, /*ownedByNext=*/true));
    if (joins && !h.runs.empty()) {
      it.run = (u32)h.runs.size() - 1;
      return;
    }
    RunRec k = rk;
    if (leadingBlank) k.rc = RealizeClass::BlankBearing;  // its glyph's run
    const bool alone = leadingBlank ||
                       (it.k == IK::Box && (k.rc == RealizeClass::BlankBearing ||
                                            k.rc == RealizeClass::Pinned || k.rc == RealizeClass::Object)) ||
                       (it.k == IK::Glue && (it.cls == (u8)GC::Autospace || it.cls == (u8)GC::ObjectSpace));
    if (h.runs.empty() || alone || single || !sameRunKey(h.runs.back(), k)) {
      h.runs.push_back(k);
      single = alone;
    }
    it.run = (u32)h.runs.size() - 1;
  }
  static bool sameRunKey(const RunRec& a, const RunRec& b) {
    return a.face == b.face && a.link == b.link && a.syn == b.syn && a.copyText == b.copyText &&
           a.rc == b.rc;
  }
  const RunRec& runOf(size_t i) const { return B.runs[B.items[i].run]; }
  // a synthetic or object item: its width is defined at emit
  void fixWidth(FlowUnit&, u32 i, double px, Su w, Su cap) {
    HItem& it = B.items[i];
    it.w = w;
    it.st |= IS_Resolved;
    ColdRec& c = B.cold[it.cold];
    c.rawPx = px;
    c.capSu = cap;
  }
  void pop(FlowUnit&) {  // the last carrier, a punctuation blank
    HList& h = B;
    h.items.pop_back();
    h.specs.pop_back();
    h.cold.pop_back();
    pend.pop_back();
    gapKind.pop_back();
  }
  size_t count(const FlowUnit& u) const { return cur == &u ? B.items.size() : 0; }
  // carrier predicates (the open unit)
  bool isCjkChar(size_t i) const { return gapKind[i] == 1; }  // a CJK char, pinned or letter-spaced
  bool isObject(size_t i) const { return B.items[i].k == IK::Box && runOf(i).rc == RealizeClass::Object; }
  bool isGlyph(size_t i, bool open) const {
    const HItem& it = B.items[i];
    return it.k == IK::Box && runOf(i).rc == RealizeClass::BlankBearing &&
           ((kCCFlags[it.cls] & kCC_open) != 0) == open;
  }
  bool isBlank(size_t i, bool ownedByNext) const {
    const HItem& it = B.items[i];
    return it.k == IK::Glue && it.cls == (u8)GC::Blank &&
           ((it.attrs & IA_OwnedByNext) != 0) == ownedByNext;
  }

  // -- the item kinds ---------------------------------------------------------
  void autospace(FlowUnit& u, StyleId st, const ICtx& ctx, Span span) {
    double px = kCjkBoundaryEm * E.fontPx(st);
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = kCjkBoundaryEm;
    sp.str = E.spaceRef;
    u32 i = push(u, IK::Glue, (u8)GC::Autospace, 0, key(st, ctx.url, ctx.addFlags, RealizeClass::Plain),
                 sp, span, 1.0f, 0.0f);
    fixWidth(u, i, px, suRoundPx(px), suRoundPx(px));
  }
  void blank(FlowUnit& u, StyleId st, const ICtx& ctx, Span span, double px, bool ownedByNext, float pen) {
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = kPunctHalfEm;
    sp.str = E.spaceRef;
    u32 i = push(u, IK::Glue, (u8)GC::Blank, ownedByNext ? IA_OwnedByNext : 0,
                 key(st, ctx.url, ctx.addFlags, RealizeClass::Plain), sp, span, 0.0f, pen);
    fixWidth(u, i, px, suRoundPx(px), suRoundPx(0.0));
  }
  void word(std::string_view w, const ContentNode* n, FlowUnit& u, StyleId st, StrRef url, float pen,
            u16 addFlags) {
    AdvanceSpec sp;
    sp.str = strs.intern(w);
    push(u, IK::Box, firstCc(w), 0, key(st, url, addFlags, RealizeClass::Plain), sp, n->span, 0.0f, pen);
  }
  void hyphenPoint(const ContentNode* n, FlowUnit& u, StyleId st, StrRef url, u16 addFlags) {
    open(u);
    HList& h = B;
    AdvanceSpec hs;
    hs.str = E.hyphenRef;
    HItem pre;
    pre.cls = (u8)ccOf('-');
    pre.aux = (u32)h.specs.size();
    h.specs.push_back(hs);
    pre.cold = (u32)h.cold.size();
    ColdRec pc;
    pc.srcStart = n->span.start;
    pc.srcEnd = n->span.end;
    h.cold.push_back(pc);
    DiscRec d;
    d.pre = (u32)h.side.size();
    d.preN = 1;
    h.side.push_back(pre);
    const float pen = (float)cfg.hyphenPenalty;
    u32 i = push(u, IK::Disc, 0, 0, key(st, url, addFlags, RealizeClass::Plain), AdvanceSpec{}, n->span,
                 pen, pen);
    h.specs.pop_back();  // a Disc's aux is its DiscRec
    h.items[i].aux = (u32)h.discs.size();
    h.discs.push_back(d);
  }

  // -- the walk ---------------------------------------------------------------
  void walk(const ContentNode* n, FlowUnit& u, ICtx ctx) override {
    switch (n->kind) {
      case Kind::text:
        emitText(n, u, ctx);
        return;
      case Kind::link: {
        ICtx c2 = ctx;
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::url && a.tag == ArgTag::Str) c2.url = a.ref;
        c2.addBits |= CLS_LINK;
        for (const ContentNode* k : n->kids) walk(k, u, c2);
        return;
      }
      case Kind::code: {
        // inline code: one unbreakable box, mono style
        if (!n->kids.empty() && n->kids[0]->kind == Kind::text) {
          StyleId st = E.compose(n->style, ctx.addBits | CLS_CODE, ctx.mul * (float)cfg.codeScale);
          AdvanceSpec sp;
          sp.str = n->kids[0]->str;
          push(u, IK::Box, firstCc(strs.get(sp.str)), 0,
               key(st, ctx.url, ctx.addFlags, RealizeClass::Plain), sp, n->span, 0.0f, kPenInf);
        }
        return;
      }
      case Kind::ref: {
        // resolver output: kids = display text, url arg = "#tsr-<label>"
        ICtx c2 = ctx;
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::url && a.tag == ArgTag::Str) {
            c2.url = a.ref;
            c2.addBits |= CLS_LINK;
          }
        c2.addFlags |= BF_REF;
        const size_t before = count(u);
        for (const ContentNode* k : n->kids) walk(k, u, c2);
        if (count(u) > before) {
          // labelled ref = inline anchor (footnote marker, notes-design.md
          // §1); a superscript marker also glues to what precedes it —
          // never a line start, like a closing punct
          HList& h = B;
          HItem& first = h.items[before];
          const bool startsRun = before == 0 || h.items[before - 1].run != first.run;
          for (const ArgVal& a : n->args)
            if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref) {
              first.attrs |= IA_Anchor;
              h.cold[first.cold].anchor = a.ref;
              if (startsRun) h.runs[first.run].anchor = a.ref;  // the run's first item
            }
          if ((styles.get(runOf(before).face).bits & CLS_SUP) && before > 0) pend[before - 1] = kPenInf;
        }
        return;
      }
      case Kind::error: {
        std::string msg = "\xE2\x9A\xA0 ";  // ⚠
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::message && a.tag == ArgTag::Str) msg += strs.get(a.ref);
        ContentNode tmp;
        tmp.kind = Kind::text;
        tmp.span = n->span;
        tmp.style = n->style;
        tmp.str = strs.intern(msg);
        ICtx c2 = ctx;
        c2.addBits |= CLS_CODE;
        emitText(&tmp, u, c2);
        return;
      }
      case Kind::mathinline: {
        StrRef srcRef = 0;
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::src && a.tag == ArgTag::Str) srcRef = a.ref;
        StyleId st = E.compose(n->style, ctx.addBits, ctx.mul);
        // CJK–formula boundary glue (App C: formulas are Latin-class)
        if (count(u) > 0 && isCjkChar(count(u) - 1)) autospace(u, st, ctx, n->span);
        std::vector<MathSeg> segs = layoutMathSegments(strs.get(srcRef), /*display=*/false,
                                                       E.fontPx(st), E.arena, strs, E.diags,
                                                       n->span, E.mathText);
        for (size_t k = 0; k < segs.size(); k++) {
          InlineObject ob;
          ob.math = segs[k].box;
          ob.src = srcRef;
          ob.part = (u32)k;
          ob.glueBefore = segs[k].glueBefore;
          open(u);
          const u32 obj = (u32)B.objs.size();
          B.objs.push_back(ob);
          if (k) {
            // the break-point glue: discardable at a break, rigid otherwise;
            // synthetic for copy (§9.3); the previous part is unbreakable-after
            double pen = segs[k].brkBefore == 1 ? cfg.mathRelAfterPenalty
                         : segs[k].brkBefore == 2 ? cfg.mathRelBeforePenalty
                                                  : cfg.mathBinAfterPenalty;
            pend.back() = kPenInf;
            AdvanceSpec gs;
            gs.k = AdvanceSpec::Object;
            gs.obj = obj;
            gs.str = E.spaceRef;
            u32 g = push(u, IK::Glue, (u8)GC::ObjectSpace, 0,
                         key(st, ctx.url, ctx.addFlags, RealizeClass::Plain), gs, n->span, 0.0f,
                         (float)pen);
            fixWidth(u, g, suToPx(segs[k].glueBefore), segs[k].glueBefore, 0);
          }
          AdvanceSpec bs;
          bs.k = AdvanceSpec::Object;
          bs.obj = obj;
          bs.str = k == 0 ? srcRef : 0;  // copy: the source rides the first part
          // a CJK-context break after a formula is legal
          u32 b = push(u, IK::Box, 0, 0, key(st, ctx.url, ctx.addFlags, RealizeClass::Object), bs,
                       n->span, 0.0f, 0.0f);
          fixWidth(u, b, suToPx(segs[k].box->w), segs[k].box->w, 0);
        }
        return;
      }
      case Kind::comment:
        return;
      case Kind::group: {
        // inline-embedded labeled group (e.g. a term spliced mid-paragraph):
        // the containing unit carries the anchor so refs still land
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref && !u.anchor) u.anchor = a.ref;
        for (const ContentNode* k : n->kids) walk(k, u, ctx);
        return;
      }
      default:
        for (const ContentNode* k : n->kids) walk(k, u, ctx);
        return;
    }
  }

  void emitWord(std::string_view w, const ContentNode* n, FlowUnit& u, StyleId st, StrRef url,
                bool noHyphen, u16 addFlags) {
    // lead / core / trail split (ASCII letters core) for hyphenation
    u32 a = 0, b = (u32)w.size();
    auto isL = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    while (a < b && !isL(w[a])) a++;
    u32 e = b;
    while (e > a && !isL(w[e - 1])) e--;
    bool coreLetters = a < e;
    for (u32 k = a; k < e && coreLetters; k++)
      if (!isL(w[k])) coreLetters = false;

    std::vector<u32> pts;
    if (!noHyphen && coreLetters && e - a >= 5 && cfg.hyphenPenalty < kPenInf)
      pts = hyphenPoints(w.substr(a, e - a));
    if (pts.empty()) {
      // long unhyphenatable tokens (URLs, paths, identifiers): break
      // opportunities after separators, glyph-free — the browser's own
      // "break after slash" convention, under KP control (no hyphen glyph,
      // penalty urlBreakPenalty). Pieces stay one shaped run when unbroken.
      if (!noHyphen && w.size() >= cfg.urlBreakMinLen && cfg.urlBreakPenalty < kPenInf) {
        std::vector<u32> cuts;
        for (u32 k = 1; k + 1 < w.size(); k++) {
          char c = w[k];
          if (c == '/' || c == '?' || c == '&' || c == '=' || c == '.' || c == '-' || c == '_')
            if (k - (cuts.empty() ? 0 : cuts.back()) >= 3) cuts.push_back(k + 1);
        }
        if (!cuts.empty()) {
          u32 from = 0;
          for (u32 cut : cuts) {
            word(w.substr(from, cut - from), n, u, st, url, (float)cfg.urlBreakPenalty, addFlags);
            from = cut;
          }
          word(w.substr(from), n, u, st, url, kPenInf, addFlags);
          return;
        }
      }
      word(w, n, u, st, url, kPenInf, addFlags);
      return;
    }
    u32 prev = 0;  // within core
    for (size_t k = 0; k <= pts.size(); k++) {
      u32 end = (k < pts.size()) ? pts[k] : e - a;
      std::string seg;
      if (k == 0) seg += w.substr(0, a);  // lead
      seg += w.substr(a + prev, end - prev);
      if (k == pts.size()) seg += w.substr(e);  // trail
      word(seg, n, u, st, url, kPenInf, addFlags);
      if (k < pts.size()) hyphenPoint(n, u, st, url, addFlags);
      prev = end;
    }
  }

  void emitText(const ContentNode* n, FlowUnit& u, ICtx ctx) {
    StyleId st = E.compose(n->style, ctx.addBits, ctx.mul);
    StyleId stCjk = E.compose(st, CLS_CJK, 1.0f);
    std::string_view s = strs.get(n->str);
    const double halfPx = kPunctHalfEm * E.fontPx(stCjk);
    const Su glueSu = suRoundPx(cfg.cjkGlueEm * E.fontPx(stCjk));

    enum class Prev : u8 { None, Latin, Cjk, Punct };  // Punct: CJK punctuation glyph
    Prev prev = Prev::None;
    std::string wordBuf;
    u32 i = 0;

    auto flushWord = [&] {
      if (!wordBuf.empty()) {
        emitWord(wordBuf, n, u, st, ctx.url, ctx.noHyphen, ctx.addFlags);
        wordBuf.clear();
      }
    };
    auto boundary = [&] { autospace(u, st, ctx, n->span); };
    {  // formula → CJK boundary: the previous inline item was a formula
      if (count(u) > 0 && isObject(count(u) - 1) && !s.empty()) {
        u32 j0 = 0;
        u32 first = utf8Next(s, j0);
        if (isIdeo(first)) boundary();
      }
    }
    auto lastIsCloseSp = [&] {  // a closing/dot punct's trailing half
      return count(u) > 0 && isBlank(count(u) - 1, /*ownedByNext=*/false);
    };
    auto lastIsOpenGlyph = [&] { return count(u) > 0 && isGlyph(count(u) - 1, /*open=*/true); };
    // definedEm > 0: the box's width is DEFINED, never measured, and the
    // renderer pins it to exactly that advance (RealizeClass::Pinned). Used
    // for U+2014/U+2026 (1em single, 2em pairs — App C): canvas and DOM
    // disagree on their advance (full-width-ization, cluster shaping), so
    // measurement cannot predict rendering for them.
    auto pushCjkChar = [&](std::string_view chars, double definedEm = 0) {
      AdvanceSpec sp;
      sp.str = strs.intern(chars);
      if (definedEm > 0) {
        sp.k = AdvanceSpec::Defined;
        sp.em = definedEm;
      }
      const RealizeClass rc = definedEm > 0 ? RealizeClass::Pinned : RealizeClass::LetterSpaced;
      u32 b = push(u, IK::Box, firstCc(chars), 0, key(stCjk, ctx.url, ctx.addFlags, rc), sp, n->span,
                   (float)cfg.cjkJustifyK, 0.0f);
      B.cold[B.items[b].cold].capSu = glueSu;  // stretch capacity for the cost fn (App C)
      if (definedEm > 0) {
        double px = definedEm * E.fontPx(stCjk);
        fixWidth(u, b, px, suRoundPx(px), glueSu);
      }
    };
    auto pushPunct = [&](std::string_view ch, bool open) {
      const PunctCompress mode = cfg.punctCompress;
      // an opening punct's leading half is owned by its glyph (IA_OwnedByNext):
      // the renderer squeezes a glyph only when its OWN half is absent
      if (open) {
        if (lastIsCloseSp()) {
          // closing/dot + opening
          if (mode == PunctCompress::Full) pop(u);  // set solid
          else if (mode == PunctCompress::None) blank(u, stCjk, ctx, n->span, halfPx, true, 0.0f);
          // Book: the closer's breakable half stays as the breathing space
        } else if (lastIsOpenGlyph()) {
          // opening + opening: solid (a breakable gap here would let the
          // first opener dangle at a line end — 禁则); None keeps a RIGID half
          if (mode == PunctCompress::None) blank(u, stCjk, ctx, n->span, halfPx, true, kPenInf);
        } else {
          blank(u, stCjk, ctx, n->span, halfPx, true, 0.0f);  // leading half — breakable, NOT stretchable
        }
      } else {
        // 禁则: no break before a closing punct (inline formulas included)
        if (count(u) > 0 && (isCjkChar(count(u) - 1) || isObject(count(u) - 1)))
          pend.back() = kPenInf;
        if (lastIsCloseSp()) {
          // closing + closing: solid; None keeps the half but rigid (a break
          // would put the second closer at a line start — 禁则)
          if (mode == PunctCompress::None) pend.back() = kPenInf;
          else pop(u);
        }
      }
      AdvanceSpec sp;
      sp.k = AdvanceSpec::MeasuredMinusBlanks;
      sp.str = strs.intern(ch);
      push(u, IK::Box, firstCc(ch), 0, key(stCjk, ctx.url, ctx.addFlags, RealizeClass::BlankBearing), sp,
           n->span, 0.0f, kPenInf);
      if (!open) blank(u, stCjk, ctx, n->span, halfPx, false, 0.0f);
    };

    while (i < s.size()) {
      u32 start = i;
      u32 cp = utf8Next(s, i);
      if (cp == ' ' || cp == '\t') {
        flushWord();
        AdvanceSpec sp;
        sp.str = E.spaceRef;
        push(u, IK::Glue, (u8)GC::Word, IA_SourceSpace, key(st, ctx.url, ctx.addFlags, RealizeClass::Plain),
             sp, n->span, 1.0f, 0.0f);
        prev = Prev::None;
        continue;
      }
      // the em dash and ellipsis (ambiguous classes) sit outside the wide
      // ranges but are CJK-class here (em-dash/ellipsis pairs, App C) —
      // without this they would take the Latin path and grow spurious
      // boundary glue on both sides.
      if (isIdeo(cp) || isAmbDashOrEllipsis(cp)) {
        // em-dash / ellipsis: defined-width pinned boxes — 2em as a pair,
        // 1em alone (App C; advance is unmeasurable, see pushCjkChar).
        // BUT an English em dash / ellipsis — single, with no CJK on either
        // side — is ordinary text: it measures in the Latin face, where the
        // 1em convention would over-budget it (blog EN pages showed ~2px).
        if (isAmbDashOrEllipsis(cp)) {
          u32 j = i;
          u32 cp2 = (i < s.size()) ? utf8Next(s, j) : 0;
          const bool pair = cp2 == cp;
          const bool cjkAfter = cp2 != 0 && (isWide(cp2) || isAmbDashOrEllipsis(cp2));
          if (!pair && prev != Prev::Cjk && !cjkAfter) {
            wordBuf.append(s.data() + start, i - start);
            prev = Prev::Latin;
            continue;
          }
          flushWord();
          if (prev == Prev::Latin) boundary();
          if (pair) {
            pushCjkChar(s.substr(start, j - start), /*definedEm=*/2.0);
            i = j;
          } else {
            pushCjkChar(s.substr(start, i - start), /*definedEm=*/1.0);
          }
          prev = Prev::Cjk;
          continue;
        }
        flushWord();
        if (prev == Prev::Latin) boundary();
        pushCjkChar(s.substr(start, i - start));
        prev = Prev::Cjk;
        continue;
      }
      if (isOpenPunct(cp) || isClosePunct(cp)) {
        // Latin-context curly quotes / apostrophes (real-world-report.md):
        // “…” and don’t between Latin text are ordinary Latin glyphs, not
        // full-width CJK punctuation with half-em compressible spaces
        if (isAmbQuote(cp) && prev != Prev::Cjk) {
          u32 j = i;
          u32 cp2 = (i < s.size()) ? utf8Next(s, j) : 0;
          if (cp2 == 0 || !(isWide(cp2) || isOpenPunct(cp2) || isClosePunct(cp2))) {
            wordBuf.append(s.data() + start, i - start);
            prev = Prev::Latin;
            continue;
          }
        }
        flushWord();
        // no CJK–Latin boundary glue next to full-width punctuation: （1322
        // 年） sets solid (GB/T 15834; real-world-report.md)
        pushPunct(s.substr(start, i - start), isOpenPunct(cp));
        prev = Prev::Punct;
        continue;
      }
      if (prev == Prev::Cjk) boundary();
      wordBuf.append(s.data() + start, i - start);
      prev = Prev::Latin;
    }
    flushWord();
  }

  void indent(FlowUnit& u, StyleId st, Span span, double px) override {
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = cfg.paraIndentEm;
    sp.str = E.spaceRef;
    RunRec rk = key(st, 0, 0, RealizeClass::Pinned);
    rk.syn = SynKind::Indent;
    u32 i = push(u, IK::Box, 0, 0, rk, sp, span, 0.0f, kPenInf);
    fixWidth(u, i, px, suRoundPx(px), suRoundPx(0.0));
  }
  void finish(FlowUnit& u) override;
  void toCell(FlowUnit& tmp, TableCell& tc) override { tc.hl = std::move(tmp.hl); }
  void done(std::vector<TopBlock>&) override {}
};

// ---- the block walk (shared by the HList and the legacy inline sinks) -------
struct Emitter {
  EmitEnv& E;
  InlineSink& sink;
  Arena& arena;
  DiagSink& diags;
  Interner& strs;
  StyleTable& styles;
  const Config& cfg;
  const MathTextCtx* mathText;
  StrRef pendingAnchor = 0;  // labeled container (group): first unit anchors
  int figDepth = 0;          // inside group{role:figure}: paras are captions
  // innermost block with a source span: diagnostics on generated nodes
  // without one (a figure's image) point at it instead of @[0,0)
  Span blockSpan{};
  Emitter(EmitEnv& e, InlineSink& s)
      : E(e), sink(s), arena(e.arena), diags(e.diags), strs(e.strs), styles(e.styles), cfg(e.cfg),
        mathText(e.mathText) {}
  Span diagSpan(const ContentNode* n) const { return n->span.empty() ? blockSpan : n->span; }
  StrRef takeAnchor() {
    StrRef a = pendingAnchor;
    pendingAnchor = 0;
    return a;
  }
  StyleId compose(StyleId base, u64 addBits, float mul) { return E.compose(base, addBits, mul); }
  double fontPx(StyleId st) { return E.fontPx(st); }

  // ---- block walk ---------------------------------------------------------
  void blockWalk(const ContentNode* n, Su indent, StrRef marker, TopBlock& tb) {
    struct SpanScope {
      Span& at;
      Span saved;
      SpanScope(Span& a, Span s) : at(a), saved(a) { if (!s.empty()) at = s; }
      ~SpanScope() { at = saved; }
    } spanScope(blockSpan, n->span);
    switch (n->kind) {
      case Kind::para: {
        FlowUnit u;
        u.src = n;
        u.indent = indent;
        u.marker = marker;
        u.markerStyle = compose(n->style, 0, 1.0f);
        u.anchor = takeAnchor();
        for (const ArgVal& a : n->args)  // labelled paragraph (note bodies)
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref) u.anchor = a.ref;
        ICtx ctx;
        if (figDepth > 0) {
          // figure caption (figure-design.md §3): centred ragged lines,
          // no hyphenation, never indented
          u.ragged = true;
          u.centered = true;
          ctx.noHyphen = true;
        } else if (cfg.paraIndentEm > 0 && marker == 0) {  // 首行缩进 (App C)
          double px = cfg.paraIndentEm * fontPx(n->style);
          sink.indent(u, n->style, n->span, px);
        }
        sink.walk(n, u, ctx);
        sink.finish(u);
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::heading: {
        int level = attrInt(n, ArgK::level, 1);
        FlowUnit u;
        u.src = n;
        u.indent = indent;
        u.marker = marker;
        u.markerStyle = n->style;
        u.anchor = takeAnchor();
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::label && a.tag == ArgTag::Str) u.anchor = a.ref;
        u.ragged = true;  // display line: ragged right, no hyphenation
        ICtx ctx;
        ctx.addBits = CLS_BOLD;
        ctx.mul = (float)headingSizeMul(level);
        ctx.noHyphen = true;
        sink.walk(n, u, ctx);
        sink.finish(u);
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::list: {
        bool ordered = attrBool(n, ArgK::ordered, false);
        int num = attrInt(n, ArgK::start, 1);
        Su childIndent = indent + suRoundPx(cfg.listIndentEm * cfg.baseSizePx);
        size_t listStart = tb.units.size();
        for (const ContentNode* item : n->kids) {
          std::string m = ordered ? std::to_string(num++) + "." : "\xE2\x80\xA2";  // •
          StrRef mref = strs.intern(m);
          size_t before = tb.units.size();
          bool first = true;
          for (const ContentNode* k : item->kids) {
            blockWalk(k, childIndent, first ? mref : 0, tb);
            first = false;
          }
          (void)before;
          if (item->kids.empty()) {  // empty item still shows its marker
            FlowUnit u;
            u.src = item;
            u.indent = childIndent;
            u.marker = mref;
            u.markerStyle = item->style;
            tb.units.push_back(std::move(u));
          }
        }
        // everything inside a list after its first unit packs tighter
        for (size_t k = listStart + 1; k < tb.units.size(); k++)
          tb.units[k].tightAbove = true;
        return;
      }
      case Kind::quote: {
        Su childIndent = indent + suRoundPx(cfg.quoteIndentEm * cfg.baseSizePx);
        for (const ContentNode* k : n->kids) blockWalk(k, childIndent, 0, tb);
        return;
      }
      case Kind::codeblock: {
        FlowUnit u;
        u.kind = FlowUnit::K::Code;
        u.src = n;
        u.indent = indent;
        u.marker = marker;
        u.codeStyle = compose(n->style, CLS_CODE, (float)cfg.codeScale);
        u.markerStyle = u.codeStyle;
        u.chRef = strs.intern("0");
        u.cjkChRef = strs.intern("\xE4\xB8\xAD");
        if (StrRef lang = attrStr(n, ArgK::lang)) u.codeLang = lang;
        u.codeWrap = attrBool(n, ArgK::wrap, u.codeWrap);
        u.codeLineNo = attrInt(n, ArgK::lineNo, u.codeLineNo);
        if (StrRef hl = attrStr(n, ArgK::hl))  // "3,5-7": validated by the reader
          parseRangeSet(strs.get(hl), u.hlLines);
        // sidecar rows (verbatim-design §5): the trailing group becomes one
        // TableCell-shaped inline stream per logical line — the whole body
        // pipeline (KP, math, links) applies inside each
        std::vector<const ContentNode*> bodyKids;
        for (const ContentNode* k : n->kids) {
          bool isSidecar = false;
          if (k->kind == Kind::group)
            for (const ArgVal& a : k->args)
              if (a.key == ArgK::role && a.tag == ArgTag::Str &&
                  strs.get(a.ref) == std::string_view("sidecar-lines"))
                isSidecar = true;
          if (isSidecar) {
            u.sidebarW = suRoundPx(cfg.sidebarFrac * (cfg.widthPx - suToPx(indent)));
            for (const ContentNode* lineNode : k->kids) {
              TableCell tc;
              FlowUnit tmp;
              for (const ContentNode* c2 : lineNode->kids) sink.walk(c2, tmp, {});
              sink.finish(tmp);
              sink.toCell(tmp, tc);
              u.cells.push_back(std::move(tc));
            }
          } else {
            bodyKids.push_back(k);
          }
        }
        // Two body forms (CH1): a single text child = plain lines split on
        // \n; otherwise each child is one line (seq of styled runs — the
        // leaves' styles were already folded at instantiation).
        if (bodyKids.size() == 1 && bodyKids[0]->kind == Kind::text) {
          std::string_view body = strs.get(bodyKids[0]->str);
          size_t pos = 0;
          while (pos <= body.size()) {
            size_t eol = body.find('\n', pos);
            if (eol == std::string_view::npos) eol = body.size();
            u.codeRuns.push_back(
                {{strs.intern(body.substr(pos, eol - pos)), u.codeStyle}});
            if (eol == body.size()) break;
            pos = eol + 1;
          }
        } else {
          const StrRef commentColor = strs.intern("var(--tsr-tok-comment)");
          std::function<void(const ContentNode*, std::vector<FlowUnit::CodeRun>&)>
              collect = [&](const ContentNode* k, std::vector<FlowUnit::CodeRun>& out) {
                if (k->kind == Kind::text) {
                  bool cm = styles.get(k->style).color == commentColor;
                  out.push_back(
                      {k->str, compose(k->style, CLS_CODE, (float)cfg.codeScale), cm});
                  return;
                }
                if (k->kind == Kind::comment) return;
                for (const ContentNode* c : k->kids) collect(c, out);
              };
          for (const ContentNode* lineNode : bodyKids) {
            std::vector<FlowUnit::CodeRun> runs;
            collect(lineNode, runs);
            u.codeRuns.push_back(std::move(runs));
          }
        }
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::rule: {
        FlowUnit u;
        u.kind = FlowUnit::K::Rule;
        u.src = n;
        u.indent = indent;
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::table: {
        FlowUnit u;
        u.kind = FlowUnit::K::Table;
        u.src = n;
        u.indent = indent;
        u.anchor = takeAnchor();
        int cols = attrInt(n, ArgK::cols, 1);
        StrRef alignRef = attrStr(n, ArgK::align);
        if (StrRef lab = attrStr(n, ArgK::label)) u.anchor = lab;
        if (cols < 1) cols = 1;
        u.tCols = (u32)cols;
        std::string_view al = strs.get(alignRef);
        for (int c = 0; c < cols; c++)
          u.tAligns.push_back(c < (int)al.size() ? (u8)al[(size_t)c] : (u8)'l');
        for (const ContentNode* row : n->kids) {
          if (row->kind != Kind::trow) continue;
          u32 c = 0;
          for (const ContentNode* cell : row->kids) {
            if (cell->kind != Kind::tcell || c >= u.tCols) continue;
            TableCell tc;
            FlowUnit tmp;  // cell content flattens to one inline stream (v1)
            for (const ContentNode* k : cell->kids) sink.walk(k, tmp, {});
            sink.finish(tmp);
            sink.toCell(tmp, tc);
            u.cells.push_back(std::move(tc));
            c++;
          }
          while (c < u.tCols) {
            u.cells.push_back({});
            c++;
          }
        }
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::raw: {
        // pre-rendered passthrough (v2 §4.1); height declared by the
        // handler, defaulting to one leading
        FlowUnit u;
        u.kind = FlowUnit::K::Raw;
        u.src = n;
        u.indent = indent;
        u.anchor = takeAnchor();
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::html && a.tag == ArgTag::Str) u.rawHtml = a.ref;
          if (a.key == ArgK::h && a.tag == ArgTag::Num) u.rawHpx = a.num;
        }
        if (u.rawHpx <= 0) u.rawHpx = cfg.lineHeight * cfg.baseSizePx;
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::image: {
        // figure-design.md §3: display box from intrinsic dims (author-
        // declared or pull-provided) + optional scale; placeholder on an
        // unsafe scheme or a failed load (0×0)
        FlowUnit u;
        u.kind = FlowUnit::K::Image;
        u.src = n;
        u.indent = indent;
        u.ragged = true;
        u.anchor = takeAnchor();
        double iw = 0, ih = 0, scale = 0;
        StrRef srcRef = 0, altRef = 0, sideRef = 0;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::src && a.tag == ArgTag::Str) srcRef = a.ref;
          if (a.key == ArgK::alt && a.tag == ArgTag::Str) altRef = a.ref;
          if (a.key == ArgK::side && a.tag == ArgTag::Str) sideRef = a.ref;
          if (a.key == ArgK::w && a.tag == ArgTag::Num) iw = a.num;
          if (a.key == ArgK::h && a.tag == ArgTag::Num) ih = a.num;
          if (a.key == ArgK::scale && a.tag == ArgTag::Num) scale = a.num;
        }
        const double measurePx = cfg.widthPx - suToPx(indent);
        bool safe = srcRef && safeImageSrc(strs.get(srcRef));
        if (srcRef && !safe)
          diags.add(Sev::Warning, "image-src", diagSpan(n),
                    "image src scheme not allowed");
        double dw, dh;
        if (safe && iw > 0 && ih > 0) {
          dw = scale > 0 ? scale * measurePx : iw;
          if (dw > measurePx) dw = measurePx;
          if (dw < 1) dw = 1;
          dh = dw * ih / iw;
          u.imgSrc = srcRef;
        } else {
          dw = measurePx;
          dh = measurePx / 3;
        }
        u.imgAlt = altRef;
        u.imgW = suRoundPx(dw);
        u.imgH = suRoundPx(dh);
        std::string_view side = sideRef ? strs.get(sideRef) : std::string_view{};
        u.floatSide = side == "left" ? 1 : side == "right" ? 2 : 0;
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::mathblock: {
        FlowUnit u;
        u.kind = FlowUnit::K::Math;
        u.src = n;
        u.indent = indent;
        u.ragged = true;
        u.anchor = takeAnchor();
        StrRef srcRef = 0;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::src && a.tag == ArgTag::Str) srcRef = a.ref;
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref) u.anchor = a.ref;
          if (a.key == ArgK::name && a.tag == ArgTag::Str) u.eqTag = a.ref;
        }
        u.mathBox = layoutMathFormula(strs.get(srcRef), /*display=*/true,
                                      fontPx(n->style), arena, strs, diags, n->span, mathText);
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::error: {
        FlowUnit u;
        u.src = n;
        u.indent = indent;
        u.ragged = true;
        u.anchor = takeAnchor();
        ICtx ctx;
        sink.walk(n, u, ctx);  // error case renders ⚠ + message
        sink.finish(u);
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::comment:
        return;
      case Kind::group: {
        bool isFigure = false;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref)
            pendingAnchor = a.ref;
          if (a.key == ArgK::role && a.tag == ArgTag::Str &&
              strs.get(a.ref) == std::string_view("figure"))
            isFigure = true;
        }
        if (isFigure) {
          // float form (figure-design.md §4): the caption breaks to the
          // float's width and rides the image unit as cells — the float box
          // is image + caption rows, placed out of flow by layout
          const ContentNode* img = nullptr;
          for (const ContentNode* k : n->kids)
            if (k->kind == Kind::image) { img = k; break; }
          bool floats = false;
          if (img)
            for (const ArgVal& a : img->args)
              if (a.key == ArgK::side && a.tag == ArgTag::Str) {
                std::string_view s = strs.get(a.ref);
                floats = s == "left" || s == "right";
              }
          if (floats) {
            blockWalk(img, indent, 0, tb);
            FlowUnit& iu = tb.units.back();
            for (const ContentNode* k : n->kids) {
              if (k->kind != Kind::para) continue;
              TableCell tc;
              FlowUnit tmp;
              ICtx cctx;
              cctx.noHyphen = true;
              sink.walk(k, tmp, cctx);
              sink.finish(tmp);
              sink.toCell(tmp, tc);
              iu.cells.push_back(std::move(tc));
            }
            pendingAnchor = 0;
            return;
          }
        }
        if (isFigure) figDepth++;
        for (const ContentNode* k : n->kids) blockWalk(k, indent, 0, tb);
        if (isFigure) figDepth--;
        pendingAnchor = 0;
        return;
      }
      default:
        for (const ContentNode* k : n->kids) blockWalk(k, indent, 0, tb);
        return;
    }
  }
};

}  // namespace

std::vector<TopBlock> emitWith(const ContentTree& tree, EmitEnv& env, InlineSink& sink) {
  std::vector<TopBlock> tops;
  if (!tree.root) return tops;
  env.spaceRef = env.strs.intern(" ");
  env.hyphenRef = env.strs.intern("-");
  env.bulletRef = env.strs.intern("\xE2\x80\xA2");
  Emitter e(env, sink);
  u32 pid = 0;
  for (const ContentNode* child : tree.root->kids) {
    TopBlock tb;
    tb.pid = pid++;
    tb.node = child;
    e.blockWalk(child, 0, 0, tb);
    if (!tb.units.empty()) tops.push_back(std::move(tb));
  }
  sink.done(tops);
  return tops;
}

std::vector<TopBlock> emitDoc(const ContentTree& tree, Arena& arena, Interner& strs,
                              StyleTable& styles, const Config& cfg, DiagSink& diags,
                              const MathTextCtx* mathText) {
  EmitEnv env{arena, diags, strs, styles, cfg, mathText};
  HlInline sink(env);
  return emitWith(tree, env, sink);
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
void HlInline::finish(FlowUnit& u) {
  if (cur != &u) return;
  HList& h = B;
  const size_t n = h.items.size();
  kernContexts(h, strs);
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
  cur = nullptr;
}

MeasureRequest resolveWidths(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const Config& cfg) {
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
  auto resolveItems = [&](HList& h) {
    for (HItem& it : h.items) {
      if (it.k == IK::Penalty || (it.k == IK::Glue && it.cls == (u8)GC::InterChar)) continue;
      const StyleId st = h.runs[it.run].face;
      needStyle(st);
      if (it.st & IS_Resolved) continue;
      ColdRec& c = h.cold[it.cold];
      if (it.k == IK::Disc) {
        // the hyphen box measures; the junction kern applies when NOT broken
        // here: the pieces shape as one run
        const DiscRec& d = h.discs[it.aux];
        HItem& pre = h.side[d.pre];
        const StrRef hy = h.specs[pre.aux].str;
        const AdvanceSpec* ks = d.spec != ~0u ? &h.specs[d.spec] : nullptr;
        const bool ready = ctxReady(ks, st);
        if (store.hasWord(hy, st) && ready) {
          const WordMet& w = store.word(hy, st);
          pre.w = w.su;
          pre.st |= IS_Resolved;
          h.cold[pre.cold].rawPx = w.px;  // only added to a line when it ends here
          if (ks) {
            double k = ctxPx(*ks, st);
            c.rawPx = (double)(float)k;
            it.w = suRoundPx(k);  // feeds KP's in-line width sum
          }
          it.st |= IS_Resolved;
        } else {
          ask(hy, st);
        }
        continue;
      }
      const AdvanceSpec& sp = h.specs[it.aux];
      const bool ready = ctxReady(sp.k == AdvanceSpec::KernCtx ? &sp : nullptr, st);
      if (store.hasWord(sp.str, st) && ready) {
        const WordMet& w = store.word(sp.str, st);
        if (sp.k == AdvanceSpec::MeasuredMinusBlanks) {
          // glyph advance minus its compressible half (App C): the half is
          // the adjacent Blank glue (or was compressed away)
          double halfPx = kPunctHalfEm * emPx(cfg, styles.get(st));
          double gpx = w.px - halfPx;
          if (gpx < 0) gpx = 0;
          it.w = suCeilPx(gpx);
          c.rawPx = gpx;
          if (kCCFlags[it.cls] & kCC_open) c.blankLpx = (float)halfPx;
          else c.blankRpx = (float)halfPx;
        } else if (it.k == IK::Glue) {
          double px = w.px;
          if (sp.k == AdvanceSpec::KernCtx) {
            // cross-space kerning correction: gap = m(tri) - m(prev) - m(next)
            px = ctxPx(sp, st);
            if (px < 0) px = 0;
          }
          Su su = suCeilPx(px) + (Su)cfg.epsilonPerWordSu;
          c.capSu = su;
          it.w = su;
          c.rawPx = px;
        } else {
          it.w = w.su;
          c.rawPx = w.px;
        }
        it.st |= IS_Resolved;
      } else {
        ask(sp.str, st);
      }
    }
  };
  for (TopBlock& tb : tops) {
    for (FlowUnit& u : tb.units) {
      if (u.kind == FlowUnit::K::Code) {
        needStyle(u.codeStyle);
        if (u.codeWrap) {
          for (StrRef probe : {u.chRef, u.cjkChRef}) {
            if (!probe || store.hasWord(probe, u.codeStyle)) continue;
            ask(probe, u.codeStyle);
          }
        }
      }
      resolveItems(u.hl);
      for (TableCell& c : u.cells) resolveItems(c.hl);
    }
  }
  return req;
}

// ---- fuseLegacy: the specified lowering HList → LinebreakBlocks -------------
// One block per carrier (Box, Disc, Glue other than InterChar):
//   Box Plain/Rigid            word or inline code           flags: -
//   Box LetterSpaced           CJK char                      BF_CJK; weight = x, capacity = capSu
//   Box Pinned (content)       defined-width dash/ellipsis   BF_CJK|BF_PAIR; weight, capacity
//   Box Pinned (indent)        paragraph indent              BF_INDENT
//   Box BlankBearing           punctuation glyph             BF_CJK|BF_PUNCT_GLYPH[|BF_PUNCT_OPEN]
//   Box Object                 formula part                  math = the part's box
//   Glue Word                  typed space                   BF_SPACE; KernCtx → ctx fields
//   Glue Autospace/ObjectSpace boundary / formula glue       BF_SPACE|BF_BOUND
//   Glue Blank                 punctuation half              BF_SPACE|BF_PUNCT_SP[|BF_PUNCT_OPEN if owned by next]
//   Disc                       hyphen point                  BF_HYPHEN; width = unbroken (junction kern),
//                                                            breakWidth/rawPx/text = the pre box
// every block: BF_REF for a Ref run; style/link of the run; anchorId for
// IA_Anchor; width, rawPx, capacity (spaceWidth), span from the item.
// Penalties: one right before a Glue (not InterChar) is the glue's when it is
// not 0 or follows another penalty (then the first is the box's); any other
// is the preceding carrier's. Defaults: Box kPenInf, except 0 when InterChar
// glue follows it directly; Glue 0; Disc its x. InterChar glue folds into
// the box before it.
namespace {

template <class Block>
void lowerHList(const HList& h, std::vector<Block>& out, std::vector<u32>& start) {
  constexpr bool kFull = std::is_same_v<Block, LinebreakBlock>;
  out.clear();
  start.clear();
  const std::vector<HItem>& v = h.items;
  const size_t n = v.size();
  out.reserve(n);
  start.reserve(n + 1);
  bool glueOwned = false;
  float gluePen = 0;
  u32 glueFrom = 0;
  for (size_t i = 0; i < n; i++) {
    const HItem& it = v[i];
    if (it.k == IK::Penalty) {
      const bool toGlue = i + 1 < n && v[i + 1].k == IK::Glue && v[i + 1].cls != (u8)GC::InterChar &&
                          (it.x != 0 || (i > 0 && v[i - 1].k == IK::Penalty));
      if (toGlue) {
        glueOwned = true;
        gluePen = it.x;
        glueFrom = (u32)i;
      } else if (!out.empty()) {
        out.back().breakPenalty = it.x;
      }
      continue;
    }
    if (it.k == IK::Glue && it.cls == (u8)GC::InterChar) {
      if (i > 0 && v[i - 1].k == IK::Box && !out.empty()) out.back().breakPenalty = 0;  // the gap is the break
      continue;
    }
    const RunRec& r = h.runs[it.run];
    const ColdRec& c = h.cold[it.cold];
    const u16 ref = r.syn == SynKind::Ref ? BF_REF : 0;
    Block& b = out.emplace_back();
    b.width = it.w;
    if constexpr (kFull) {
      b.rawPx = c.rawPx;
      b.style = r.face;
      b.linkUrl = r.link;
      b.anchorId = (it.attrs & IA_Anchor) ? c.anchor : 0;
      b.widthResolved = (it.st & IS_Resolved) != 0;
      b.span = Span{c.srcStart, c.srcEnd};
    }
    start.push_back(it.k == IK::Glue && glueOwned ? glueFrom : (u32)i);
    switch (it.k) {
      case IK::Box: {
        const AdvanceSpec& sp = h.specs[it.aux];
        if constexpr (kFull) b.text = sp.str;
        b.breakPenalty = kPenInf;
        bool weighted = false;
        switch (r.rc) {
          case RealizeClass::LetterSpaced:
            b.flags = (u16)(BF_CJK | ref);
            weighted = true;
            break;
          case RealizeClass::Pinned:
            if (r.syn == SynKind::Indent) {
              b.flags = BF_INDENT;
            } else {
              b.flags = (u16)(BF_CJK | BF_PAIR | ref);
              weighted = true;
            }
            break;
          case RealizeClass::BlankBearing:
            b.flags = (u16)(BF_CJK | BF_PUNCT_GLYPH | ((kCCFlags[it.cls] & kCC_open) ? BF_PUNCT_OPEN : 0) | ref);
            break;
          case RealizeClass::Object:
            b.flags = ref;
            if constexpr (kFull) b.math = h.objs[sp.obj].math;
            break;
          case RealizeClass::Plain:
          case RealizeClass::Rigid:
            b.flags = ref;
            break;
        }
        if (weighted) {
          b.spaceWidth = c.capSu;
          if constexpr (kFull) b.stretchWeight = it.x;
        }
        break;
      }
      case IK::Glue: {
        const AdvanceSpec& sp = h.specs[it.aux];
        b.breakPenalty = glueOwned ? gluePen : 0.0f;
        glueOwned = false;
        b.spaceWidth = c.capSu;
        if constexpr (kFull) {
          b.text = sp.str;
          b.stretchWeight = it.x;
        }
        switch ((GC)it.cls) {
          case GC::Word:
            b.flags = (u16)(BF_SPACE | ref);
            if constexpr (kFull)
              if (sp.k == AdvanceSpec::KernCtx) {
                b.ctxTrigram = sp.tri;
                b.ctxPrev = sp.prev;
                b.ctxNext = sp.next;
              }
            break;
          case GC::Autospace:
          case GC::ObjectSpace:
            b.flags = (u16)(BF_SPACE | BF_BOUND | ref);
            break;
          case GC::Blank:
            b.flags = (u16)(BF_SPACE | BF_PUNCT_SP | ((it.attrs & IA_OwnedByNext) ? BF_PUNCT_OPEN : 0) | ref);
            break;
          case GC::InterChar:
            break;
        }
        break;
      }
      case IK::Disc: {
        const DiscRec& d = h.discs[it.aux];
        const HItem& pre = h.side[d.pre];
        b.flags = (u16)(BF_HYPHEN | ref);
        b.breakPenalty = it.x;
        b.breakWidth = pre.w;
        if constexpr (kFull) {
          b.kernPx = (float)c.rawPx;
          b.rawPx = h.cold[pre.cold].rawPx;
          b.text = h.specs[pre.aux].str;
          if (d.spec != ~0u) {
            const AdvanceSpec& ks = h.specs[d.spec];
            b.ctxTrigram = ks.tri;
            b.ctxPrev = ks.prev;
            b.ctxNext = ks.next;
          }
        }
        break;
      }
      case IK::Penalty:
        break;
    }
  }
  start.push_back((u32)n);
}

}  // namespace

void fuseLegacy(const HList& h, std::vector<LinebreakBlock>& blocks, std::vector<u32>& start) {
  lowerHList(h, blocks, start);
}
void fuseLegacy(const HList& h, std::vector<BreakBlock>& blocks, std::vector<u32>& start) {
  lowerHList(h, blocks, start);
}

void fuseLegacy(std::vector<TopBlock>& tops) {
  for (TopBlock& tb : tops)
    for (FlowUnit& u : tb.units) {
      fuseLegacy(u.hl, u.blocks, u.blockStart);
      for (TableCell& c : u.cells) fuseLegacy(c.hl, c.blocks, c.blockStart);
    }
}

static void unitHeader(std::string& out, const FlowUnit& u, const Interner& strs) {
  const char* k = u.kind == FlowUnit::K::Text ? "text"
                  : u.kind == FlowUnit::K::Code ? "code"
                  : u.kind == FlowUnit::K::Raw ? "raw"
                  : u.kind == FlowUnit::K::Table ? "table"
                  : u.kind == FlowUnit::K::Math ? "math"
                  : u.kind == FlowUnit::K::Image ? "img" : "rule";
  appendf(out, " unit %s indent=%dsu", k, u.indent);
  if (u.anchor) {
    out += " anchor=\"";
    appendEscaped(out, strs.get(u.anchor));
    out += "\"";
  }
  if (u.marker) {
    out += " marker=\"";
    appendEscaped(out, strs.get(u.marker));
    out += "\"";
  }
  if (u.kind == FlowUnit::K::Code) {
    appendf(out, " lines=%zu", u.codeRuns.size());
    if (u.codeWrap) out += " wrap";
    if (u.codeLineNo) appendf(out, " lineNo=%d", u.codeLineNo);
    if (!u.hlLines.empty()) appendf(out, " hl=%zu", u.hlLines.size());
  }
  if (u.kind == FlowUnit::K::Table) appendf(out, " cols=%u cells=%zu", u.tCols, u.cells.size());
  if (u.kind == FlowUnit::K::Math && u.mathBox)
    appendf(out, " w=%dsu asc=%dsu desc=%dsu", u.mathBox->w, u.mathBox->asc,
            u.mathBox->desc);
  if (u.kind == FlowUnit::K::Image) {
    appendf(out, " w=%dsu h=%dsu%s", u.imgW, u.imgH,
            u.imgSrc ? "" : " placeholder");
    if (u.floatSide) out += u.floatSide == 1 ? " float=left" : " float=right";
  }
  if (u.centered) out += " centered";
  out += "\n";
}


std::string dumpBlocks(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles) {
  std::string out;
  for (const TopBlock& tb : tops) {
    appendf(out, "top pid=%u units=%zu\n", tb.pid, tb.units.size());
    for (const FlowUnit& u : tb.units) {
      unitHeader(out, u, strs);
      auto dumpBlock = [&](const LinebreakBlock& b) {
        out += "  ";
        if (b.math) {
          out += "math \"";
          appendEscaped(out, strs.get(b.text));
          appendf(out, "\" w=%dsu asc=%dsu desc=%dsu", b.width, b.math->asc,
                  b.math->desc);
        }
        else if (b.flags & BF_INDENT) appendf(out, "indent w=%dsu", b.width);
        else if (b.flags & BF_BOUND) appendf(out, "boundary w=%dsu stretch=%g", b.width, (double)b.stretchWeight);
        else if (b.flags & BF_PUNCT_SP) appendf(out, "punct-sp w=%dsu", b.width);
        else if (b.isPunctGlyph()) {
          out += (b.flags & BF_PUNCT_OPEN) ? "punct-open \"" : "punct-close \"";
          appendEscaped(out, strs.get(b.text));
          appendf(out, "\" w=%dsu", b.width);
        }
        else if (b.isCjkChar()) {
          out += "cjk \"";
          appendEscaped(out, strs.get(b.text));
          appendf(out, "\" w=%dsu glue=%dsu wt=%g pen=%s", b.width, b.spaceWidth,
                  (double)b.stretchWeight, b.breakPenalty >= BREAK_INF ? "INF" : "0");
        }
        else if (b.isSpace()) appendf(out, "space w=%dsu stretch=%g", b.spaceWidth, (double)b.stretchWeight);
        else if (b.isHyphen()) appendf(out, "hyphen bw=%dsu pen=%.2f", b.breakWidth, (double)b.breakPenalty);
        else {
          out += "word \"";
          appendEscaped(out, strs.get(b.text));
          appendf(out, "\" w=%dsu pen=%s", b.width, b.breakPenalty >= BREAK_INF ? "INF" : "0");
        }
        const Styling& st = styles.get(b.style);
        if (st.bits & CLS_BOLD) out += " BOLD";
        if (st.bits & CLS_EM) out += " EM";
        if (st.bits & CLS_CODE) out += " CODE";
        if (st.bits & CLS_LINK) out += " LINK";
        if (b.flags & BF_REF) out += " SYN";
        if (st.sizeMul != 1.0f) appendf(out, " x%.2f", (double)st.sizeMul);
        appendStyleFields(out, st, strs);
        appendf(out, " @[%u,%u)\n", b.span.start, b.span.end);
      };
      std::vector<LinebreakBlock> full;
      std::vector<u32> start;
      fuseLegacy(u.hl, full, start);
      for (const LinebreakBlock& b : full) dumpBlock(b);
      for (size_t ci = 0; ci < u.cells.size(); ci++) {
        appendf(out, "  cell %zu\n", ci);
        fuseLegacy(u.cells[ci].hl, full, start);
        for (const LinebreakBlock& b : full) dumpBlock(b);
      }
    }
  }
  return out;
}

std::string dumpHLists(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles) {
  std::string out;
  for (const TopBlock& tb : tops) {
    appendf(out, "top pid=%u units=%zu\n", tb.pid, tb.units.size());
    for (const FlowUnit& u : tb.units) {
      unitHeader(out, u, strs);
      dumpHList(out, u.hl, strs, styles, "  ");
      for (size_t ci = 0; ci < u.cells.size(); ci++) {
        appendf(out, "  cell %zu\n", ci);
        dumpHList(out, u.cells[ci].hl, strs, styles, "   ");
      }
    }
  }
  return out;
}

std::string dumpMathBoxes(const std::vector<TopBlock>& tops, const Interner& strs) {
  std::string out;
  for (const TopBlock& tb : tops) {
    for (const FlowUnit& u : tb.units) {
      if (u.kind == FlowUnit::K::Math && u.mathBox) {
        appendf(out, "display pid=%u\n", tb.pid);
        out += dumpMathBox(u.mathBox, strs);
      }
      for (const HItem& it : u.hl.items) {
        if (it.k != IK::Box || u.hl.runs[it.run].rc != RealizeClass::Object) continue;
        const AdvanceSpec& sp = u.hl.specs[it.aux];
        appendf(out, "inline pid=%u \"", tb.pid);
        appendEscaped(out, strs.get(sp.str));
        out += "\"\n";
        out += dumpMathBox(u.hl.objs[sp.obj].math, strs);
      }
    }
  }
  return out;
}

std::string dumpBreaks(const std::vector<TopBlock>& tops) {
  std::string out;
  for (const TopBlock& tb : tops) {
    for (size_t ui = 0; ui < tb.units.size(); ui++) {
      const FlowUnit& u = tb.units[ui];
      if (u.kind != FlowUnit::K::Text) continue;
      appendf(out, "top pid=%u unit=%zu lines=%zu cost=%.4f breakpoints=[", tb.pid, ui,
              u.breakpoints.size(), u.breakCost);
      for (size_t k = 0; k < u.breakpoints.size(); k++)
        appendf(out, "%s%u", k ? "," : "", u.breakpoints[k]);
      out += "]\n";
    }
  }
  return out;
}

}  // namespace tsr
