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

// the shortest piece a long unhyphenatable token (a URL, a path) breaks into
// after a separator (plan P3-02: a named parameter)
constexpr u32 kUrlMinPiece = 3;


void reportFormula(const ContentNode* n, StrRef formula, const MathScope& scope, const Interner& strs,
                   DiagSink& diags, Arena& arena);

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
  EmitSettings cfg;
  // the open unit's list is built here (inline streams never nest) and
  // copied to the unit, exactly sized, by finish(): the scratch keeps its
  // capacity from unit to unit
  Flow* cur = nullptr;
  HList B;
  std::vector<float> pend;  // per carrier: the break penalty after it (kPenInf = none)
  std::vector<u8> gapKind;  // per carrier: 1 a CJK char, 2 a closing glyph, 0 other
  bool single = false;      // the open run admits no other carrier
  explicit HlInline(EmitEnv& e) : E(e), strs(e.strs), styles(e.styles), cfg(e.cfg) {}

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
  }
  static RunRec key(StyleId face, const ICtx& ctx, RealizeClass rc) {
    RunRec r;
    r.face = face;
    r.link = ctx.url;
    r.syn = (ctx.addFlags & BF_REF) ? SynKind::Ref : SynKind::Content;
    r.copy = ctx.copy;
    r.synName = ctx.syn;
    r.copyText = ctx.copyText;
    r.copyGroup = ctx.copyGroup;
    r.rc = rc;
    return r;
  }
  static u8 firstCc(std::string_view s) {
    if (s.empty()) return 0;
    u32 i = 0;
    return (u8)ccOf(utf8Next(s, i));
  }
  u32 push(Flow& u, IK k, u8 cls, u8 attrs, const RunRec& rk, const AdvanceSpec& spec, Span span,
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
                       (it.k == IK::Glue && (it.cls == (u8)GC::Autospace || it.cls == (u8)GC::ObjectSpace ||
                                              it.cls == (u8)GC::Fill));
    if (h.runs.empty() || alone || single || !sameRunKey(h.runs.back(), k)) {
      h.runs.push_back(k);
      single = alone;
    }
    it.run = (u32)h.runs.size() - 1;
  }
  static bool sameRunKey(const RunRec& a, const RunRec& b) {
    return a.face == b.face && a.link == b.link && a.syn == b.syn && a.copy == b.copy && a.synName == b.synName &&
           a.copyText == b.copyText && a.copyGroup == b.copyGroup && a.rc == b.rc;
  }
  const RunRec& runOf(size_t i) const { return B.runs[B.items[i].run]; }
  // a synthetic or object item: its width is defined at emit
  void fixWidth(Flow&, u32 i, double px, Su w, Su cap) {
    HItem& it = B.items[i];
    it.w = w;
    it.st |= IS_Resolved;
    ColdRec& c = B.cold[it.cold];
    c.rawPx = px;
    c.capSu = cap;
  }
  void pop(Flow&) {  // the last carrier, a punctuation blank
    HList& h = B;
    h.items.pop_back();
    h.specs.pop_back();
    h.cold.pop_back();
    pend.pop_back();
    gapKind.pop_back();
  }
  size_t count(const Flow& u) const { return cur == &u ? B.items.size() : 0; }
  // carrier predicates (the open unit)
  bool isCjkChar(size_t i) const { return gapKind[i] == 1; }  // a CJK char, pinned or letter-spaced
  bool isObject(size_t i) const { return B.items[i].k == IK::Box && runOf(i).rc == RealizeClass::Object; }
  const InlineObject& objectOf(size_t i) const { return B.objs[B.parts[B.specs[B.items[i].aux].obj].obj]; }
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
  void autospace(Flow& u, StyleId st, const ICtx& ctx, Span span) {
    double px = kCjkBoundaryEm * E.fontPx(st);
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = kCjkBoundaryEm;
    sp.str = E.spaceRef;
    u32 i = push(u, IK::Glue, (u8)GC::Autospace, 0, key(st, ctx, RealizeClass::Plain),
                 sp, span, 1.0f, 0.0f);
    fixWidth(u, i, px, suRoundPx(px), suRoundPx(px));
  }
  void blank(Flow& u, StyleId st, const ICtx& ctx, Span span, double px, bool ownedByNext, float pen) {
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = kPunctHalfEm;
    sp.str = E.spaceRef;
    u32 i = push(u, IK::Glue, (u8)GC::Blank, ownedByNext ? IA_OwnedByNext : 0,
                 key(st, ctx, RealizeClass::Plain), sp, span, 0.0f, pen);
    fixWidth(u, i, px, suRoundPx(px), suRoundPx(0.0));
  }
  void word(std::string_view w, const ContentNode* n, Flow& u, StyleId st, float pen, const ICtx& ctx) {
    AdvanceSpec sp;
    sp.str = strs.intern(w);
    push(u, IK::Box, firstCc(w), 0, key(st, ctx, RealizeClass::Plain), sp, n->span, 0.0f, pen);
  }
  void hyphenPoint(const ContentNode* n, Flow& u, StyleId st, const ICtx& ctx) {
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
    u32 i = push(u, IK::Disc, 0, 0, key(st, ctx, RealizeClass::Plain), AdvanceSpec{}, n->span,
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
  void walk(const ContentNode* n, Flow& u, ICtx ctx) override {
    copyPolicy(n, u, ctx);
    const ArgVal* at = attr(n, ArgK::attach);
    if (!at || at->tag != ArgTag::Str) return shape(n, u, ctx);
    const size_t before = count(u);
    shape(n, u, ctx);
    if (count(u) == before) return;
    const std::string_view a = strs.get(at->ref);
    if (a != "next" && before > 0 && !(pend[before - 1] <= -kPenInf)) pend[before - 1] = kPenInf;
    if (a != "prev") forbidLast();
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
        fixWidth(u, i, 0.0, 0, 0);
        return;
      }
      case InlineShape::Error:
        errorText(n, u, ctx);
        return;
      case InlineShape::Skip:
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
  // the penalty after the last item: forbidden, unless a forced break
  void forbidLast() {
    if (!(pend.back() <= -kPenInf)) pend.back() = kPenInf;
  }

  void container(const ContentNode* n, Flow& u, ICtx ctx) {
    if (n->kind == Kind::link) {  // an internal target (plan P3-04: the resolver's anchor), else its URL
      if (n->anchorTo) ctx.url = {n->anchorTo, true};
      else
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::url && a.tag == ArgTag::Str) ctx.url = {a.ref, false};
    } else if (n->kind == Kind::ref) {
      ref(n, u, ctx);
      return;
    } else if (n->kind == Kind::group) {
      // inline-embedded labeled group (e.g. a term spliced mid-paragraph):
      // the containing unit carries the anchor so refs still land
      for (const ArgVal& a : n->args)
        if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref && !u.anchor) u.anchor = a.ref;
    }
    for (const ContentNode* k : n->kids) walk(k, u, ctx);
  }

  void code(const ContentNode* n, Flow& u, ICtx ctx) {
    // inline code: one unbreakable box, mono style
    if (!n->kids.empty() && n->kids[0]->kind == Kind::text) {
      StyleId st = E.compose(n->style, ctx.add, ctx.mul);  // mono and its size: rules (plan P3-01)
      AdvanceSpec sp;
      sp.str = n->kids[0]->str;
      push(u, IK::Box, firstCc(strs.get(sp.str)), 0, key(st, ctx, RealizeClass::Plain),
           sp, n->span, 0.0f, kPenInf);
    }
  }

  void ref(const ContentNode* n, Flow& u, ICtx ctx) {
    // resolver output: kids = display text; a resolved one links to its
    // target's anchor (plan P3-04: SemInfo.targetAnchor)
    if (n->anchorTo) ctx.url = {n->anchorTo, true};
    ctx.addFlags |= BF_REF;
    const size_t before = count(u);
    for (const ContentNode* k : n->kids) walk(k, u, ctx);
    if (count(u) > before) {
      // labelled ref = inline anchor (footnote marker, notes-design.md
      // §1); the marker glues to what precedes it through its attach
      // (walk), never a line start, like a closing punct
      HList& h = B;
      HItem& first = h.items[before];
      const bool startsRun = before == 0 || h.items[before - 1].run != first.run;
      for (const ArgVal& a : n->args)
        if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref) {
          first.attrs |= IA_Anchor;
          h.cold[first.cold].anchor = a.ref;
          if (startsRun) h.runs[first.run].anchor = a.ref;  // the run's first item
        }
    }
  }

  void errorText(const ContentNode* n, Flow& u, ICtx ctx) {
    // an error node stays breakable CODE-style text (design T5 A22)
    omitAsError(ctx);
    std::string msg = "\xE2\x9A\xA0 ";  // ⚠
    for (const ArgVal& a : n->args)
      if (a.key == ArgK::message && a.tag == ArgTag::Str) msg += strs.get(a.ref);
    ContentNode tmp;
    tmp.kind = Kind::text;
    tmp.span = n->span;
    tmp.style = n->style;
    tmp.str = strs.intern(msg);
    emitText(&tmp, u, ctx);  // in its style: mono by the default rule (plan P3-01)
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
                bool resolved) {
    InlineObject& ob = B.objs[obj];
    if (ob.nParts == 0) ob.part0 = (u32)B.parts.size();
    ob.nParts++;
    B.parts.push_back(part);
    AdvanceSpec bs;
    bs.k = AdvanceSpec::Object;
    bs.obj = (u32)B.parts.size() - 1;
    bs.str = str;
    // a break after an object is legal (as after a formula); the boundary
    // pass that reads its edge classes is the paragraph shaper's (P4-02)
    u32 b = push(u, IK::Box, ob.firstCC, 0, key(st, ctx, RealizeClass::Object), bs, span,
                 0.0f, 0.0f);
    if (resolved) fixWidth(u, b, suToPx(part.w), part.w, 0);
    return b;
  }

  void object(const ContentNode* n, Flow& u, ICtx ctx, ObjKind k) {
    switch (k) {
      case ObjKind::Math:
        math(n, u, ctx);
        return;
      case ObjKind::Image: {
        // one box from the declared or intrinsic dims (the image pull fills
        // them), sitting on the baseline; a 1em placeholder otherwise
        StyleId st = E.compose(n->style, ctx.add, ctx.mul);
        double iw = 0, ih = 0;
        StrRef src = 0, alt = 0;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::src && a.tag == ArgTag::Str) src = a.ref;
          if (a.key == ArgK::alt && a.tag == ArgTag::Str) alt = a.ref;
          if (a.key == ArgK::w && a.tag == ArgTag::Num) iw = a.num;
          if (a.key == ArgK::h && a.tag == ArgTag::Num) ih = a.num;
        }
        const bool safe = src && safeImageSrc(strs.get(src));  // unsafe: reported at ingest
        if (safe) E.imageDims(src, iw, ih);
        const bool sized = safe && iw > 0 && ih > 0;
        const double em = E.fontPx(st);
        u32 obj = addObject(u, ObjKind::Image, n, st);
        B.objs[obj].src = sized ? src : 0;
        B.objs[obj].alt = alt;
        ObjPart pt;
        pt.obj = obj;
        pt.w = suRoundPx(sized ? iw : em);
        pt.asc = suRoundPx(sized ? ih : em);
        objectBox(u, obj, pt, st, ctx, n->span, 0, true);
        return;
      }
      case ObjKind::Raw: {
        // handler-declared markup: one box of its declared size (1em when
        // undeclared), sitting on the baseline
        StyleId st = E.compose(n->style, ctx.add, ctx.mul);
        double w = 0, hh = 0;
        StrRef html = 0;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::html && a.tag == ArgTag::Str) html = a.ref;
          if (a.key == ArgK::w && a.tag == ArgTag::Num) w = a.num;
          if (a.key == ArgK::h && a.tag == ArgTag::Num) hh = a.num;
        }
        const double em = E.fontPx(st);
        u32 obj = addObject(u, ObjKind::Raw, n, st);
        B.objs[obj].src = html;
        ObjPart pt;
        pt.obj = obj;
        pt.w = suRoundPx(w > 0 ? w : em);
        pt.asc = suRoundPx(hh > 0 ? hh : em);
        objectBox(u, obj, pt, st, ctx, n->span, 0, true);
        return;
      }
      case ObjKind::Error: {
        // the error box of a kind that cannot appear inline: its name,
        // measured in the CODE face, unbreakable
        StyleId st = E.compose(n->style, ctx.add + E.mono, ctx.mul);
        omitAsError(ctx);
        u32 obj = addObject(u, ObjKind::Error, n, st);
        const StrRef text = strs.intern(std::string("\xE2\x9A\xA0 ") + kKinds[(u16)n->kind].name);  // ⚠
        B.objs[obj].src = text;
        ObjPart pt;
        pt.obj = obj;
        objectBox(u, obj, pt, st, ctx, n->span, text, false);
        return;
      }
    }
  }

  void math(const ContentNode* n, Flow& u, ICtx ctx) {
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
    objectBox(u, obj, pt, st, ctx, n->span, srcRef, false);
  }

  void emitWord(std::string_view w, const ContentNode* n, Flow& u, StyleId st, const ICtx& ctx) {
    const bool noHyphen = ctx.noHyphen;
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
    if (!noHyphen && coreLetters && e - a >= kHyphenMinLetters && cfg.hyphenPenalty < kPenInf)
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
            if (k - (cuts.empty() ? 0 : cuts.back()) >= kUrlMinPiece) cuts.push_back(k + 1);
        }
        if (!cuts.empty()) {
          u32 from = 0;
          for (u32 cut : cuts) {
            word(w.substr(from, cut - from), n, u, st, (float)cfg.urlBreakPenalty, ctx);
            from = cut;
          }
          word(w.substr(from), n, u, st, kPenInf, ctx);
          return;
        }
      }
      word(w, n, u, st, kPenInf, ctx);
      return;
    }
    u32 prev = 0;  // within core
    for (size_t k = 0; k <= pts.size(); k++) {
      u32 end = (k < pts.size()) ? pts[k] : e - a;
      std::string seg;
      if (k == 0) seg += w.substr(0, a);  // lead
      seg += w.substr(a + prev, end - prev);
      if (k == pts.size()) seg += w.substr(e);  // trail
      word(seg, n, u, st, kPenInf, ctx);
      if (k < pts.size()) hyphenPoint(n, u, st, ctx);
      prev = end;
    }
  }

  void emitText(const ContentNode* n, Flow& u, ICtx ctx) {
    StyleId st = E.compose(n->style, ctx.add, ctx.mul);
    StyleId stCjk = E.compose(st, E.cjk, 1.0f);
    std::string_view s = strs.get(n->str);
    const double halfPx = kPunctHalfEm * E.fontPx(stCjk);
    const Su glueSu = suRoundPx(cfg.cjkGlueEm * E.fontPx(stCjk));

    enum class Prev : u8 { None, Latin, Cjk, Punct };  // Punct: CJK punctuation glyph
    Prev prev = Prev::None;
    std::string wordBuf;
    u32 i = 0;

    auto flushWord = [&] {
      if (!wordBuf.empty()) {
        emitWord(wordBuf, n, u, st, ctx);
        wordBuf.clear();
      }
    };
    auto boundary = [&] { autospace(u, st, ctx, n->span); };
    {  // formula → CJK boundary: the previous inline item was a formula
      if (count(u) > 0 && isObject(count(u) - 1) && objectOf(count(u) - 1).kind == ObjKind::Math && !s.empty()) {
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
      u32 b = push(u, IK::Box, firstCc(chars), 0, key(stCjk, ctx, rc), sp, n->span,
                   (float)cfg.cjkJustifyK, 0.0f);
      B.cold[B.items[b].cold].capSu = glueSu;  // stretch capacity for the cost fn (App C)
      if (definedEm > 0) {
        double px = definedEm * E.fontPx(stCjk);
        fixWidth(u, b, px, suRoundPx(px), glueSu);
      }
    };
    // the run's text.punct (plan P3-02), else the document's cjk.punctCompress
    const u8 runPunct = styles.get(st).punct;
    auto pushPunct = [&](std::string_view ch, bool open) {
      const PunctCompress mode = runPunct ? (PunctCompress)(runPunct - 1) : cfg.punctCompress;
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
        if (count(u) > 0 && (isCjkChar(count(u) - 1) || isObject(count(u) - 1))) forbidLast();
        if (lastIsCloseSp()) {
          // closing + closing: solid; None keeps the half but rigid (a break
          // would put the second closer at a line start — 禁则)
          if (mode == PunctCompress::None) forbidLast();
          else pop(u);
        }
      }
      AdvanceSpec sp;
      sp.k = AdvanceSpec::MeasuredMinusBlanks;
      sp.str = strs.intern(ch);
      push(u, IK::Box, firstCc(ch), 0, key(stCjk, ctx, RealizeClass::BlankBearing), sp,
           n->span, 0.0f, kPenInf);
      if (!open) blank(u, stCjk, ctx, n->span, halfPx, false, 0.0f);
    };

    while (i < s.size()) {
      u32 start = i;
      u32 cp = utf8Next(s, i);
      if (cp == ' ' || cp == '\t') {
        flushWord();
        // a text that starts with a space right after a space (something
        // between two texts rendered nothing: an undefined splice, a counter
        // event) adds none — the browser collapses it, as TeX's input does
        const size_t c = count(u);
        if (start == 0 && c > 0 && B.items[c - 1].k == IK::Glue && B.items[c - 1].cls == (u8)GC::Word &&
            (B.items[c - 1].attrs & IA_SourceSpace)) {
          prev = Prev::None;
          continue;
        }
        AdvanceSpec sp;
        sp.str = E.spaceRef;
        push(u, IK::Glue, (u8)GC::Word, IA_SourceSpace, key(st, ctx, RealizeClass::Plain),
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

  void indent(Flow& u, StyleId st, Span span, double px, double em) override {
    AdvanceSpec sp;
    sp.k = AdvanceSpec::Fixed;
    sp.em = em;
    sp.str = E.spaceRef;
    RunRec rk = key(st, ICtx{}, RealizeClass::Pinned);
    rk.syn = SynKind::Indent;
    u32 i = push(u, IK::Box, 0, 0, rk, sp, span, 0.0f, kPenInf);
    fixWidth(u, i, px, suRoundPx(px), suRoundPx(0.0));
  }
  void finish(Flow& u) override;
  void toCell(Flow& tmp, Flow& tc) override {
    tc.hl = std::move(tmp.hl);
    tc.anchor = tmp.anchor;  // a label inside the cell, kept (plan P1-17)
  }
  void done(std::vector<TopBlock>&) override {}
};

// ---- the leaves (shared by the HList and the legacy inline sinks) ----------
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
  Flow cellOf(const std::vector<ContentNode*>& kids, ICtx ctx) {
    Flow tmp, tc;
    for (const ContentNode* k : kids) sink.walk(k, tmp, ctx);
    sink.finish(tmp);
    sink.toCell(tmp, tc);
    return tc;
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
        ctx.noHyphen = !tr.hyphenate;
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
          for (const ContentNode* k : n->kids) sink.walk(k, u, ctx);  // the block's content
        }
        sink.finish(u);
        return;
      }
      case LayouterId::Grid: {
        GridData& g = u.data.emplace<GridData>();
        g.codeStyle = n->style;  // mono at its size: the cascade's (plan P3-01)
        g.features = styles.get(n->style).features;
        g.chRef = strs.intern(kGridProbeLatin);  // the grid's probes (code/grid.h)
        g.cjkChRef = strs.intern(kGridProbeCjk);
        if (StrRef lang = attrStr(n, ArgK::lang)) g.lang = lang;
        g.wrap = attrBool(n, ArgK::wrap, g.wrap);
        g.lineNo = attrInt(n, ArgK::lineNo, g.lineNo);
        if (StrRef hl = attrStr(n, ArgK::hl))  // "3,5-7": validated by the reader
          parseRangeSet(strs.get(hl), g.hlLines, kRailRangeLines, kRailRangeNumber);
        // sidecar rows (verbatim-design §5): one inline stream per logical
        // line — the whole body pipeline (KP, math, links) applies inside each
        if (ls.sidecar) {
          g.sidecar = true;
          for (const ContentNode* lineNode : ls.rows) {
            u.cells.push_back(cellOf(lineNode->kids, {}));
            u.cells.back().span = lineNode->span;
          }
        }
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
                                   ? E.rt->tokens(lang, bodyKids[0]->str)
                                   : nullptr;
        if (tok && tok->st == ResState::Ready) {
          std::vector<std::vector<TokenRun>> lines;
          tokenLines(strs.get(bodyKids[0]->str), bodyKids[0]->style, E.cascade, bodyKids[0]->env, tok->toks.data(),
                     tok->toks.size(), strs, styles, lines);
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
              if (a.key == ArgK::h && a.tag == ArgTag::Num) r.hPx = a.num;
            }
            if (r.hPx <= 0) r.hPx = cfg.lineHeight * cfg.baseSizePx;
            return;
          }
          case Painter::Image: {
            // figure-design.md §3: the size spec (intrinsic dims, declared or
            // pull-provided, and a scale); layout resolves the display box. An
            // unsafe scheme (reported by the ingest scan, plan P1-16) or a
            // failed load paints a placeholder
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
            if (safe) E.imageDims(srcRef, iw, ih);
            if (safe && iw > 0 && ih > 0) im.src = srcRef;
            im.size.iw = iw;
            im.size.ih = ih;
            im.size.scale = scale;
            im.size.placeholder = !im.src;
            // a float's caption rows break to its width
            ICtx cctx;
            cctx.noHyphen = true;
            for (const ContentNode* k : ls.rows) u.cells.push_back(cellOf(k->kids, cctx));
            return;
          }
          case Painter::MathRow: {
            // prepared here, laid out in Measure (plan P1-25): see math()
            MathData& m = u.data.emplace<MathData>();
            for (const ArgVal& a : n->args)
              if (a.key == ArgK::name && a.tag == ArgTag::Str) m.tag = a.ref;
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
            if (mathText) {  // the legacy oracle lays out at emit (MIGRATION, until P4-02)
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
  env.bulletRef = env.strs.intern("\xE2\x80\xA2");
}
static void shapeTop(const BoxTree& bt, size_t t, Emitter& e, TopBlock& tb) {
  const TopTree& tt = bt.tops[t];
  tb.pid = tt.pid;
  tb.tree = &tt;
  tb.units.assign(tt.leaves.size(), {});
  for (size_t k = 0; k < tt.leaves.size(); k++) e.leaf(tt.blocks[tt.leaves[k]], bt.sources[t][k], tb.units[k]);
}

std::vector<TopBlock> emitWith(const BoxTree& bt, EmitEnv& env, InlineSink& sink) {
  std::vector<TopBlock> tops(bt.tops.size());
  prepareEnv(env);
  Emitter e(env, sink);
  for (size_t t = 0; t < bt.tops.size(); t++) shapeTop(bt, t, e, tops[t]);
  sink.done(tops);
  return tops;
}

struct EmitPass::State {
  std::vector<MeasureItem> missing;
  MeasureNeeds needs;
  EmitEnv env;
  HlInline sink;
  Emitter e;  // (reads env.mathText at construction)
  State(EmitEnv en, const MetricStore* metrics)
      : needs{metrics, &en.styles, &en.strs, en.cfg.baseSizePx, &missing}, env(en), sink(env), e(prepared(), sink) {
  }
  EmitEnv& prepared() {
    prepareEnv(env);
    if (needs.metrics) env.mathText = &needs;
    return env;
  }
};
EmitPass::EmitPass(const BoxTree& bt, Arena& arena, Interner& strs, StyleTable& styles, const EmitSettings& cfg,
                   DiagSink& diags, const MetricStore* metrics, const ResourceTable* rt)
    : bt_(bt),
      st_(std::make_unique<State>(EmitEnv{arena, diags, strs, styles, cfg, nullptr, rt, bt.math, bt.cascade}, metrics)) {}
EmitPass::~EmitPass() = default;
bool EmitPass::top(size_t t, TopBlock& out, std::vector<MeasureItem>& missing) {
  st_->missing.clear();
  shapeTop(bt_, t, st_->e, out);
  missing = st_->missing;
  return missing.empty();
}

std::vector<TopBlock> emitDoc(const BoxTree& bt, Arena& arena, Interner& strs, StyleTable& styles,
                              const EmitSettings& cfg, DiagSink& diags, const MeasureNeeds* mathText,
                              const ResourceTable* rt) {
  EmitEnv env{arena, diags, strs, styles, cfg, mathText, rt, bt.math, bt.cascade};
  HlInline sink(env);
  return emitWith(bt, env, sink);
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
  dst.parts.assign(h.parts.begin(), h.parts.end());
  dst.hasDeferred = h.hasDeferred;
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
    MeasureNeeds mt{&store, &env.styles, &env.strs, env.docBasePx, &missing};
    DiagSink scratch;
    const MathScope scope{env.math, ob.epoch, ob.style};
    std::vector<MathSeg> segs =
        layoutMathSegments(env.strs.get(ob.formula), /*display=*/false, emPx(cfg, env.styles.get(ob.style)),
                           env.arena, env.strs, scratch, Span{h.cold[ph.cold].srcStart, h.cold[ph.cold].srcEnd},
                           &mt, /*parseDiags=*/false, &scope);
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
        pen.x = (float)mathPenalty(cfg, segs[k].brkBefore);
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

// The pending-object hook (plan P1-25; design T8 S6, T5 owns it later): a
// pending object of a kind with a finalizer is finalized by it in Measure;
// resolveWidths never names a kind.
using ObjectFinalizer = bool (*)(HList& h, size_t& at, MetricStore& store, const EmitSettings& cfg, ObjectEnv& env,
                                 std::vector<MeasureItem>& need);
static constexpr ObjectFinalizer kFinalizers[] = {
    /*Math*/ finalizeFormula, /*Image*/ nullptr, /*Raw*/ nullptr, /*Error*/ nullptr};
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
  (void)cfg;
  std::vector<MeasureItem> missing;
  MeasureNeeds mt{&store, &env.styles, &env.strs, env.docBasePx, &missing};
  DiagSink scratch;
  const MathScope scope{env.math, m.epoch, m.style};
  MathBox* box = layoutMathFormula(env.strs.get(m.formula), /*display=*/true, m.sizePx, env.arena, env.strs, scratch,
                                   m.span, &mt, /*parseDiags=*/false, &scope);
  if (!missing.empty()) {
    need.insert(need.end(), missing.begin(), missing.end());
    return;
  }
  keepLayoutDiags(scratch, env);
  m.box = box;
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
      if (sp.k == AdvanceSpec::Object && h.objs[h.parts[sp.obj].obj].deferred) continue;  // the finalizer's
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
    for (FlowUnit& u : tb.units) {
      if (MathData* m = std::get_if<MathData>(&u.data); m && !m->box && objects)
        finalizeDisplay(*m, store, cfg, *objects, need);
      if (const GridData* g = std::get_if<GridData>(&u.data)) {
        needStyle(g->codeStyle);
        if (g->wrap) {
          for (StrRef probe : {g->chRef, g->cjkChRef}) {
            if (!probe || store.hasWord(probe, g->codeStyle)) continue;
            ask(probe, g->codeStyle);
          }
        }
      }
      resolveItems(u.hl);
      for (TableCell& c : u.cells) resolveItems(c.hl);
    }
  }
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
//   Glue Fill                  fil glue (fill, plan P2-16)    BF_SPACE|BF_FIL
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
      b.linkUrl = r.link.ref;
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
            if constexpr (kFull) b.math = h.parts[sp.obj].math;
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
          case GC::Fill:
            b.flags = (u16)(BF_SPACE | BF_FIL | ref);
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

static void unitHeader(std::string& out, const LayoutBlock& b, const FlowUnit& u, const Interner& strs) {
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
    // the size spec layout resolves (plan P1-16)
    if (im->size.iw > 0 || im->size.ih > 0) appendf(out, " intrinsic=%gx%gpx", im->size.iw, im->size.ih);
    if (im->size.scale > 0) appendf(out, " scale=%g", im->size.scale);
    if (im->size.placeholder) out += " placeholder";
    if (b.floatSide) out += b.floatSide == 1 ? " float=left" : " float=right";
  }
  if (b.tr.align == BlockTraits::Align::Center) out += " centered";
  if (b.tr.singleCenter) out += " single-center";
  out += "\n";
}

static const LayoutBlock& leafOf(const TopBlock& tb, size_t k) { return tb.tree->blocks[tb.tree->leaves[k]]; }

std::string dumpBlocks(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles) {
  std::string out;
  for (const TopBlock& tb : tops) {
    appendf(out, "top pid=%u units=%zu\n", tb.pid, tb.units.size());
    for (size_t ui = 0; ui < tb.units.size(); ui++) {
      const FlowUnit& u = tb.units[ui];
      unitHeader(out, leafOf(tb, ui), u, strs);
      auto dumpBlock = [&](const LinebreakBlock& b) {
        out += "  ";
        if (b.math) {
          out += "math \"";
          appendEscaped(out, strs.get(b.text));
          appendf(out, "\" w=%dsu asc=%dsu desc=%dsu", b.width, b.math->asc,
                  b.math->desc);
        }
        else if (b.flags & BF_INDENT) appendf(out, "indent w=%dsu", b.width);
        else if (b.flags & BF_FIL) out += "fill";
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
        if (st.weight == 700) out += " BOLD";
        if (st.italic) out += " EM";
        if (st.fontRole == FONTROLE_MONO) out += " CODE";
        if (b.linkUrl) out += " LINK";
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
    for (size_t ui = 0; ui < tb.units.size(); ui++) {
      const FlowUnit& u = tb.units[ui];
      unitHeader(out, leafOf(tb, ui), u, strs);
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
        out += dumpMathBox(m->box, strs);
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
