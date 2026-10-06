// MIGRATION ONLY (plan P1-12; deleted with the paragraph shaper, P4-02): the
// pre-HList inline emitter, verbatim, kept as the oracle of fuseLegacy. The
// block walk is shared (emitWith); this sink writes the old LinebreakBlocks,
// resolveWidthsLegacy measures them, and fuseCheck compares them field by
// field with fuseLegacy(HList) — the CI equivalence check of the golden
// runner and `tsrc --fuse-check`.
#include "legacy.h"

#include "emit_internal.h"
#include "../shape/textrules.h"

#include "../hyphen/hyphen.h"

namespace tsr {

namespace {

struct LegacyInline final : InlineSink {
  EmitEnv& E;
  Arena& arena;
  DiagSink& diags;
  Interner& strs;
  StyleTable& styles;
  EmitSettings cfg;
  const MeasureNeeds* mathText;
  StrRef& spaceRef;  // interned by emitWith
  StrRef& hyphenRef;
  explicit LegacyInline(EmitEnv& e)
      : E(e), arena(e.arena), diags(e.diags), strs(e.strs), styles(e.styles), cfg(e.cfg),
        mathText(e.mathText), spaceRef(e.spaceRef), hyphenRef(e.hyphenRef) {}
  // the copy policy (plan P3-07) is no field the oracle compares
  void copyPolicy(const ContentNode*, Flow&, ICtx&) override {}
  StyleId compose(StyleId base, const StyleDelta& d, float mul) { return E.compose(base, d, mul); }

  void walk(const ContentNode* n, Flow& u, ICtx ctx) override { inlineWalk(n, u, ctx); }
  void indent(Flow& u, StyleId st, Span span, double px, double em) override {
    (void)em;
    pushSynthetic(u, st, 0, span, px, BF_INDENT, 0.0f, BREAK_INF, 0.0);
  }
  void finish(Flow&) override {}
  void toCell(Flow& tmp, Flow& tc) override { tc.legacy = std::move(tmp.legacy); }
  void done(std::vector<TopBlock>& tops) override;

  // inline nodes other than text and its containers walked so far, and
  // their count when the last source space was set: a space collapses only
  // into a space no object (which this oracle does not model) separates
  u64 atoms = 0, atomsAtSpace = ~0ull;
  // attach (plan P2-08), as emit.cc reads it
  void inlineWalk(const ContentNode* n, Flow& u, ICtx ctx) {
    const ArgVal* at = attr(n, ArgK::attach);
    if (!at || at->tag != ArgTag::Str) return walkOne(n, u, ctx);
    const size_t before = u.legacy.size();
    walkOne(n, u, ctx);
    if (u.legacy.size() == before) return;
    const std::string_view a = strs.get(at->ref);
    if (a != "next" && before > 0) u.legacy[before - 1].breakPenalty = BREAK_INF;
    if (a != "prev") u.legacy.back().breakPenalty = BREAK_INF;
  }
  void walkOne(const ContentNode* n, Flow& u, ICtx ctx) {
    if (n->kind != Kind::text && n->kind != Kind::seq && n->kind != Kind::styled && n->kind != Kind::link &&
        n->kind != Kind::comment)
      atoms++;
    switch (n->kind) {
      case Kind::text:
        emitText(n, u, ctx);
        return;
      case Kind::link: {
        ICtx c2 = ctx;
        if (n->anchorTo) c2.url = {n->anchorTo, true, n->anchorDoc};  // (plan P3-04)
        else
          for (const ArgVal& a : n->args)
            if (a.key == ArgK::url && a.tag == ArgTag::Str) c2.url = {a.ref, false};
        for (const ContentNode* k : n->kids) inlineWalk(k, u, c2);
        return;
      }
      case Kind::code: {
        // inline code: single unbreakable block, mono style
        if (!n->kids.empty() && n->kids[0]->kind == Kind::text) {
          LinebreakBlock b;
          b.breakPenalty = BREAK_INF;
          b.flags = ctx.addFlags;
          b.style = compose(n->style, ctx.add, ctx.mul);  // mono and its size: rules (plan P3-01)
          b.text = n->kids[0]->str;
          b.linkUrl = ctx.url.ref;
          b.span = n->span;
          u.legacy.push_back(b);
        }
        return;
      }
      case Kind::ref: {
        // resolver output: kids = display text, url arg = "#tsr-<label>"
        ICtx c2 = ctx;
        if (n->anchorTo) c2.url = {n->anchorTo, true, n->anchorDoc};  // its target's anchor (plan P3-04)
        c2.addFlags |= BF_REF;
        const size_t before = u.legacy.size();
        for (const ContentNode* k : n->kids) inlineWalk(k, u, c2);
        if (u.legacy.size() > before) {
          // labelled ref = inline anchor (footnote marker, notes-design.md
          // §1); the marker glues to what precedes it through its attach
          // (inlineWalk), never a line start, like a closing punct
          for (const ArgVal& a : n->args)
            if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref)
              u.legacy[before].anchorId = a.ref;
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
        emitText(&tmp, u, ctx);  // in its style: mono by the default rule (plan P3-01)
        return;
      }
      case Kind::mathinline: {
        const MathSource ms = mathSource(n, strs);  // (plan P2-15)
        const StrRef srcRef = strs.intern(ms.copy);
        StyleId st = compose(n->style, ctx.add, ctx.mul);
        const MathScope scope{E.math, n->declEpoch, st};
        // CJK–formula boundary glue (App C: formulas are Latin-class)
        if (!u.legacy.empty() && u.legacy.back().isCjkChar()) {
          double px = kCjkBoundaryEm * fontPx(st);
          pushSynthetic(u, st, ctx.url.ref, n->span, px,
                        (u16)(BF_SPACE | BF_BOUND | ctx.addFlags), 1.0f, 0.0f, px);
        }
        std::vector<MathSeg> segs = layoutMathSegments(
            ms.text, /*display=*/false, fontPx(st), arena, strs,
            diags, n->span, MathBreaks{cfg.mathBreakAfter, cfg.mathBreakBefore}, mathText, true, &scope);
        for (size_t k = 0; k < segs.size(); k++) {
          if (k) {
            // the break-point glue: discardable at a break (BF_SPACE trims
            // at line edges), rigid otherwise; synthetic for copy (§9.3)
            const double pen = segs[k].penalty;
            LinebreakBlock g;
            g.flags = (u16)(BF_SPACE | BF_SYNTH | ctx.addFlags);
            g.breakPenalty = (float)pen;
            g.style = st;
            g.text = spaceRef;
            g.linkUrl = ctx.url.ref;
            g.span = n->span;
            g.width = segs[k].glueBefore;
            g.spaceWidth = 0;
            g.rawPx = suToPx(segs[k].glueBefore);
            g.widthResolved = true;
            // the previous segment itself is unbreakable-after
            u.legacy.back().breakPenalty = BREAK_INF;
            u.legacy.push_back(g);
          }
          LinebreakBlock b;
          b.breakPenalty = 0;  // CJK-context break after a formula is legal
          b.style = st;
          b.text = k == 0 ? srcRef : 0;  // copy: source rides the first segment
          b.linkUrl = ctx.url.ref;
          b.flags = ctx.addFlags;
          b.span = n->span;
          b.obj = true;
          b.objKind = ObjKind::Math;
          b.objAsc = segs[k].box->asc;
          b.objDesc = segs[k].box->desc;
          b.objPayload = segs[k].box;
          b.width = segs[k].box->w;
          b.rawPx = suToPx(segs[k].box->w);
          b.widthResolved = true;
          u.legacy.push_back(b);
        }
        return;
      }
      case Kind::comment:
        return;
      case Kind::entry:  // (plan P3-13) an anchored entry (an index entry): an empty anchor
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref) {
            LinebreakBlock b;
            b.breakPenalty = BREAK_INF;
            b.style = compose(n->style, ctx.add, ctx.mul);
            b.text = strs.intern("");
            b.linkUrl = ctx.url.ref;
            b.flags = ctx.addFlags;
            b.span = n->span;
            b.anchorId = a.ref;
            b.widthResolved = true;
            u.legacy.push_back(b);
          }
        return;
      case Kind::fill:  // fil glue (plan P2-16), as emit lowers it
        pushSynthetic(u, compose(n->style, ctx.add, ctx.mul), ctx.url.ref, n->span, 0.0,
                      (u16)(BF_SPACE | BF_FIL | ctx.addFlags), 0.0f, 0.0f, 0.0);
        return;
      case Kind::group: {
        // inline-embedded labeled group (e.g. a term spliced mid-paragraph):
        // the containing unit carries the anchor so refs still land
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref && !u.anchor)
            u.anchor = a.ref;
        for (const ContentNode* k : n->kids) inlineWalk(k, u, ctx);
        return;
      }
      default:
        for (const ContentNode* k : n->kids) inlineWalk(k, u, ctx);
        return;
    }
  }

  void pushWordBlock(std::string_view w, const ContentNode* n, Flow& u, StyleId st,
                     StrRef url, float penalty, u16 extraFlags = 0) {
    LinebreakBlock b;
    b.breakPenalty = penalty;
    b.style = st;
    b.text = strs.intern(w);
    b.linkUrl = url;
    b.flags = extraFlags;
    b.span = n->span;
    u.legacy.push_back(b);
  }

  void emitWord(std::string_view w, const ContentNode* n, Flow& u, StyleId st, StrRef url,
                bool noHyphen, u16 extraFlags) {
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
    if (!noHyphen && coreLetters && e - a >= 5 && cfg.hyphenPenalty < BREAK_INF)
      pts = hyphenPoints(w.substr(a, e - a));
    if (pts.empty()) {
      // long unhyphenatable tokens (URLs, paths, identifiers): break
      // opportunities after separators, glyph-free — the browser's own
      // "break after slash" convention, under KP control (no hyphen glyph,
      // penalty urlBreakPenalty). Pieces stay one shaped run when unbroken.
      if (!noHyphen && w.size() >= cfg.urlBreakMinLen && cfg.urlBreakPenalty < BREAK_INF) {
        std::vector<u32> cuts;
        for (u32 k = 1; k + 1 < w.size(); k++) {
          char c = w[k];
          if (c == '/' || c == '?' || c == '&' || c == '=' || c == '.' || c == '-' || c == '_')
            if (k - (cuts.empty() ? 0 : cuts.back()) >= 3) cuts.push_back(k + 1);
        }
        if (!cuts.empty()) {
          u32 from = 0;
          for (u32 cut : cuts) {
            pushWordBlock(w.substr(from, cut - from), n, u, st, url,
                          (float)cfg.urlBreakPenalty, extraFlags);
            from = cut;
          }
          pushWordBlock(w.substr(from), n, u, st, url, BREAK_INF, extraFlags);
          return;
        }
      }
      pushWordBlock(w, n, u, st, url, BREAK_INF, extraFlags);
      return;
    }
    u32 prev = 0;  // within core
    for (size_t k = 0; k <= pts.size(); k++) {
      u32 end = (k < pts.size()) ? pts[k] : e - a;
      std::string seg;
      if (k == 0) seg += w.substr(0, a);  // lead
      seg += w.substr(a + prev, end - prev);
      if (k == pts.size()) seg += w.substr(e);  // trail
      pushWordBlock(seg, n, u, st, url, BREAK_INF, extraFlags);
      if (k < pts.size()) {
        LinebreakBlock hy;
        hy.breakPenalty = (float)cfg.hyphenPenalty;
        hy.style = st;
        hy.text = hyphenRef;
        hy.linkUrl = url;
        hy.flags = (u16)(BF_HYPHEN | extraFlags);
        hy.span = n->span;
        u.legacy.push_back(hy);
      }
      prev = end;
    }
  }

  // the style's em (sizePx honoured): one formula with measurement (P0-08)
  double fontPx(StyleId st) { return emPx(cfg, styles.get(st)); }

  void pushSynthetic(Flow& u, StyleId st, StrRef url, Span span, double px, u16 flags,
                     float weight, float penalty, double capacityPx) {
    LinebreakBlock b;
    b.flags = flags;
    b.breakPenalty = penalty;
    b.stretchWeight = weight;
    b.style = st;
    b.text = spaceRef;
    b.linkUrl = url;
    b.span = span;
    b.width = suRoundPx(px);
    b.spaceWidth = suRoundPx(capacityPx);
    b.rawPx = px;
    b.widthResolved = true;
    u.legacy.push_back(b);
  }

  void emitText(const ContentNode* n, Flow& u, ICtx ctx) {
    StyleId st = compose(n->style, ctx.add, ctx.mul);
    StyleId stCjk = compose(st, E.cjk, 1.0f);
    std::string_view s = strs.get(n->str);
    const double halfPx = kPunctHalfEm * fontPx(stCjk);
    const Su glueSu = suRoundPx(cfg.cjkGlueEm * fontPx(stCjk));

    enum class Prev : u8 { None, Latin, Cjk, Punct };  // Punct: CJK punctuation glyph
    Prev prev = Prev::None;
    std::string word;
    u32 i = 0;
    using Marks = MarkClass;  // (plan P3-30: as the HList emitter)
    const Marks marks = markClassOf(E.styles.get(st).lang, strs);

    auto flushWord = [&] {
      if (!word.empty()) {
        emitWord(word, n, u, st, ctx.url.ref, ctx.noHyphen, ctx.addFlags);
        word.clear();
      }
    };
    auto boundary = [&] {
      double px = kCjkBoundaryEm * fontPx(st);
      pushSynthetic(u, st, ctx.url.ref, n->span, px, (u16)(BF_SPACE | BF_BOUND | ctx.addFlags),
                    1.0f, 0.0f, px);
    };
    {  // formula → CJK boundary: the previous inline block was math
      if (!u.legacy.empty() && u.legacy.back().obj && !s.empty()) {
        u32 j0 = 0;
        u32 first = utf8Next(s, j0);
        if (isIdeo(first)) boundary();
      }
    }
    auto lastIsCloseSp = [&] {  // a closing/dot punct's trailing half
      return !u.legacy.empty() && (u.legacy.back().flags & BF_PUNCT_SP) &&
             !(u.legacy.back().flags & BF_PUNCT_OPEN);
    };
    auto lastIsOpenGlyph = [&] {
      return !u.legacy.empty() && (u.legacy.back().flags & BF_PUNCT_GLYPH) &&
             (u.legacy.back().flags & BF_PUNCT_OPEN);
    };
    // definedEm > 0: the block's width is DEFINED, never measured, and the
    // renderer pins its box to exactly that advance (BF_PAIR). Used for
    // U+2014/U+2026 (1em single, 2em pairs — App C): canvas and DOM disagree
    // on their advance (full-width-ization, cluster shaping), so measurement
    // cannot predict rendering for them.
    auto pushCjkChar = [&](std::string_view chars, double definedEm = 0) {
      LinebreakBlock b;
      b.flags = (u16)((definedEm > 0 ? (BF_CJK | BF_PAIR) : BF_CJK) | ctx.addFlags);
      b.breakPenalty = 0;
      b.stretchWeight = (float)cfg.cjkJustifyK;
      b.spaceWidth = glueSu;  // stretch capacity for the cost fn (App C)
      b.style = stCjk;
      b.text = strs.intern(chars);
      b.linkUrl = ctx.url.ref;
      b.span = n->span;
      if (definedEm > 0) {
        double px = definedEm * fontPx(stCjk);
        b.width = suRoundPx(px);
        b.rawPx = px;
        b.widthResolved = true;
      }
      u.legacy.push_back(b);
    };
    const u8 runPunct = styles.get(st).punct;  // text.punct (plan P3-02)
    auto pushPunct = [&](std::string_view ch, bool open) {
      const PunctCompress mode = runPunct ? (PunctCompress)(runPunct - 1) : cfg.punctCompress;
      // BF_PUNCT_OPEN on a half-space marks it as an OPENING punct's leading
      // half — the renderer squeezes a glyph only when its OWN half is absent.
      const u16 openSpFlags = (u16)(BF_SPACE | BF_PUNCT_SP | BF_PUNCT_OPEN | ctx.addFlags);
      if (open) {
        if (lastIsCloseSp()) {
          // closing/dot + opening
          if (mode == PunctCompress::Full) u.legacy.pop_back();  // set solid
          else if (mode == PunctCompress::None)
            pushSynthetic(u, stCjk, ctx.url.ref, n->span, halfPx, openSpFlags, 0.0f, 0.0f, 0.0);
          // Book: the closer's breakable half stays as the breathing space
        } else if (lastIsOpenGlyph()) {
          // opening + opening: solid (a breakable gap here would let the
          // first opener dangle at a line end — 禁则); None keeps a RIGID half
          if (mode == PunctCompress::None)
            pushSynthetic(u, stCjk, ctx.url.ref, n->span, halfPx, openSpFlags, 0.0f, BREAK_INF, 0.0);
        } else {
          pushSynthetic(u, stCjk, ctx.url.ref, n->span, halfPx, openSpFlags,
                        0.0f, 0.0f, 0.0);  // leading half — breakable, NOT stretchable
        }
      } else {
        // 禁则: no break before a closing punct (inline formulas included)
        if (!u.legacy.empty() && (u.legacy.back().isCjkChar() || u.legacy.back().obj))
          u.legacy.back().breakPenalty = BREAK_INF;
        if (lastIsCloseSp()) {
          // closing + closing: solid; None keeps the half but rigid (a break
          // would put the second closer at a line start — 禁则)
          if (mode == PunctCompress::None) u.legacy.back().breakPenalty = BREAK_INF;
          else u.legacy.pop_back();
        }
      }
      LinebreakBlock g;
      g.flags = (u16)(BF_CJK | BF_PUNCT_GLYPH | (open ? BF_PUNCT_OPEN : 0) | ctx.addFlags);
      g.breakPenalty = BREAK_INF;
      g.style = stCjk;
      g.text = strs.intern(ch);
      g.linkUrl = ctx.url.ref;
      g.span = n->span;
      u.legacy.push_back(g);
      if (!open)
        pushSynthetic(u, stCjk, ctx.url.ref, n->span, halfPx,
                      (u16)(BF_SPACE | BF_PUNCT_SP | ctx.addFlags), 0.0f, 0.0f, 0.0);
    };

    while (i < s.size()) {
      u32 start = i;
      u32 cp = utf8Next(s, i);
      if (cp == ' ' || cp == '\t') {
        flushWord();
        // a space right after a source space collapses (emit.cc does the same)
        if (start == 0 && atomsAtSpace == atoms && !u.legacy.empty() && (u.legacy.back().flags & BF_SPACE) &&
            !(u.legacy.back().flags & (BF_BOUND | BF_PUNCT_SP)) && u.legacy.back().text == spaceRef) {
          prev = Prev::None;
          continue;
        }
        atomsAtSpace = atoms;
        LinebreakBlock b;
        b.flags = (u16)(BF_SPACE | ctx.addFlags);
        b.breakPenalty = 0;
        b.stretchWeight = 1;
        b.style = st;
        b.text = spaceRef;
        b.linkUrl = ctx.url.ref;
        b.span = n->span;
        u.legacy.push_back(b);
        prev = Prev::None;
        continue;
      }
      // the em dash and ellipsis (ambiguous classes) sit outside the wide
      // ranges but are CJK-class here (em-dash/ellipsis pairs, App C) —
      // without this they would take the Latin path and grow spurious
      // boundary glue on both sides.
      if (isIdeo(cp) || isAmbDashOrEllipsis(cp)) {
        // em-dash / ellipsis: defined-width pinned blocks — 2em as a pair,
        // 1em alone (App C; advance is unmeasurable, see pushCjkChar).
        // BUT an English em dash / ellipsis — single, with no CJK on either
        // side — is ordinary text: it measures in the Latin face, where the
        // 1em convention would over-budget it (blog EN pages showed ~2px).
        if (isAmbDashOrEllipsis(cp)) {
          u32 j = i;
          u32 cp2 = (i < s.size()) ? utf8Next(s, j) : 0;
          const bool pair = cp2 == cp;
          const bool cjkAfter = cp2 != 0 && (isWide(cp2) || isAmbDashOrEllipsis(cp2));
          if (marks == Marks::Latin || (marks == Marks::Neighbours && !pair && prev != Prev::Cjk && !cjkAfter)) {
            word.append(s.data() + start, i - start);
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
        if (isAmbQuote(cp) && marks != Marks::Cjk && (marks == Marks::Latin || prev != Prev::Cjk)) {
          u32 j = i;
          u32 cp2 = (i < s.size()) ? utf8Next(s, j) : 0;
          if (marks == Marks::Latin || cp2 == 0 || !(isWide(cp2) || isOpenPunct(cp2) || isClosePunct(cp2))) {
            word.append(s.data() + start, i - start);
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
      word.append(s.data() + start, i - start);
      prev = Prev::Latin;
    }
    flushWord();
  }
};

// Word spaces absorb cross-space kerning (e.g. Georgia "s. A"): sum-of-words
// measurement misses it, leaving every justified line systematically short.
// Tag each plain space with its neighbouring codepoints; resolveWidths turns
// that into gap = m(prev+' '+next) - m(prev) - m(next).
static void fillSpaceContexts(std::vector<TopBlock>& tops, Interner& strs) {
  auto lastCp = [&](const LinebreakBlock& b) -> std::string {
    std::string_view t = strs.get(b.text);
    if (t.empty()) return {};
    u32 cp = utf8PrevCp(t, (u32)t.size());
    if (!kernEligible(cp)) return {};  // no cross-space kern vs CJK or symbols
    u32 i = (u32)t.size();
    while (i > 0 && ((u8)t[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0) i--;
    return std::string(t.substr(i));
  };
  auto firstCp = [&](const LinebreakBlock& b) -> std::string {
    std::string_view t = strs.get(b.text);
    if (t.empty()) return {};
    u32 i = 0;
    u32 cp = utf8Next(t, i);
    if (!kernEligible(cp)) return {};
    return std::string(t.substr(0, i));
  };
  auto isWord = [](const LinebreakBlock& b) {
    return !b.isSpace() && !b.isHyphen() && !b.isSynthetic() && !b.isCjkChar() &&
           !b.isPunctGlyph() && !b.obj && b.text != 0;
  };
  auto tag = [&](std::vector<LinebreakBlock>& blocks) {
    for (size_t i = 0; i < blocks.size(); i++) {
      LinebreakBlock& b = blocks[i];
      const bool hyph = b.isHyphen();
      if (!hyph && (!b.isSpace() || (b.flags & (BF_PUNCT_SP | BF_BOUND | BF_SYNTH))))
        continue;
      if (i == 0 || i + 1 >= blocks.size()) continue;
      if (!isWord(blocks[i - 1]) || !isWord(blocks[i + 1])) continue;
      // the browser only kerns INSIDE one shaped run: a style or link
      // boundary (italic title → roman period, real-world-report.md) splits
      // the run, so no cross-space kern exists there to budget for
      if (blocks[i - 1].style != blocks[i + 1].style || blocks[i - 1].style != b.style ||
          blocks[i - 1].linkUrl != blocks[i + 1].linkUrl)
        continue;
      std::string prev = lastCp(blocks[i - 1]);
      std::string next = firstCp(blocks[i + 1]);
      if (prev.empty() || next.empty()) continue;
      b.ctxPrev = strs.intern(prev);
      b.ctxNext = strs.intern(next);
      // hyphen point: JUNCTION bigram — the pieces shape as one run when the
      // break is not taken, and the browser kerns across the boundary
      b.ctxTrigram = strs.intern(hyph ? prev + next : prev + " " + next);
    }
  };
  for (TopBlock& tb : tops) {
    for (FlowUnit& u : tb.units) {
      tag(u.legacy);
      for (TableCell& c : u.cells) tag(c.legacy);
    }
  }
}

void LegacyInline::done(std::vector<TopBlock>& tops) { fillSpaceContexts(tops, strs); }

}  // namespace

MeasureRequest resolveWidthsLegacy(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const EmitSettings& cfg) {
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
  auto resolveBlocks = [&](std::vector<LinebreakBlock>& blocks) {
      for (LinebreakBlock& b : blocks) {
        needStyle(b.style);
        if (b.widthResolved) continue;
        bool ctxReady = true;
        if (b.ctxTrigram) {
          for (StrRef r : {b.ctxTrigram, b.ctxPrev, b.ctxNext}) {
            if (!store.hasWord(r, b.style)) {
              ctxReady = false;
              ask(r, b.style);
            }
          }
        }
        if (store.hasWord(b.text, b.style) && ctxReady) {
          const WordMet& w = store.word(b.text, b.style);
          if (b.isHyphen()) {
            b.breakWidth = w.su;
            b.rawPx = w.px;  // only added to a line when it ends at this block
            if (b.ctxTrigram && store.hasWord(b.ctxTrigram, b.style) &&
                store.hasWord(b.ctxPrev, b.style) &&
                store.hasWord(b.ctxNext, b.style)) {
              // junction kern when NOT broken here: pieces shape as one run
              double k = store.word(b.ctxTrigram, b.style).px -
                         store.word(b.ctxPrev, b.style).px -
                         store.word(b.ctxNext, b.style).px;
              b.kernPx = (float)k;
              b.width = suRoundPx(k);  // feeds KP's in-line width sum
            }
          } else if (b.isPunctGlyph()) {
            // glyph advance minus its compressible half (App C): the half
            // lives in the adjacent BF_PUNCT_SP block (or was compressed away)
            double halfPx = kPunctHalfEm * emPx(cfg, styles.get(b.style));
            double gpx = w.px - halfPx;
            if (gpx < 0) gpx = 0;
            b.width = suCeilPx(gpx);
            b.rawPx = gpx;
          } else if (b.isSpace()) {
            double px = w.px;
            if (b.ctxTrigram && store.hasWord(b.ctxTrigram, b.style) &&
                store.hasWord(b.ctxPrev, b.style) && store.hasWord(b.ctxNext, b.style)) {
              // cross-space kerning correction: gap = m(tri) - m(prev) - m(next)
              px = store.word(b.ctxTrigram, b.style).px -
                   store.word(b.ctxPrev, b.style).px -
                   store.word(b.ctxNext, b.style).px;
              if (px < 0) px = 0;
            }
            Su su = suCeilPx(px) + (Su)cfg.epsilonPerWordSu;
            b.spaceWidth = su;
            b.width = su;
            b.rawPx = px;
          } else {
            b.width = w.su;
            b.rawPx = w.px;
          }
          b.widthResolved = true;
        } else {
          ask(b.text, b.style);
        }
      }
  };
  for (TopBlock& tb : tops) {
    for (FlowUnit& u : tb.units) {
      if (const GridData* g = std::get_if<GridData>(&u.data)) {
        needStyle(g->codeStyle);
        if (g->wrap) {
          for (StrRef probe : {g->chRef, g->cjkChRef}) {
            if (!probe || store.hasWord(probe, g->codeStyle)) continue;
            ask(probe, g->codeStyle);
          }
        }
      }
      resolveBlocks(u.legacy);
      for (TableCell& c : u.cells) resolveBlocks(c.legacy);
    }
  }
  return req;
}

std::vector<TopBlock> emitDocLegacy(const BoxTree& bt, Arena& arena, Interner& strs,
                                    StyleTable& styles, const EmitSettings& cfg, DiagSink& diags,
                                    const MeasureNeeds* mathText) {
  EmitEnv env{arena, diags, strs, styles, cfg, mathText, nullptr, bt.math};
  LegacyInline sink(env);
  return emitWith(bt, env, sink);
}

namespace {

void cmpBlocks(std::string& out, int& budget, const std::string& where,
               const std::vector<LinebreakBlock>& got, const std::vector<LinebreakBlock>& want,
               const Interner& strs) {
  if (budget <= 0) return;
  if (got.size() != want.size()) {
    appendf(out, "%s: %zu blocks, legacy %zu\n", where.c_str(), got.size(), want.size());
    budget--;
  }
  for (size_t i = 0; i < got.size() && i < want.size() && budget > 0; i++) {
    const LinebreakBlock& a = got[i];
    const LinebreakBlock& b = want[i];
    std::string d;
    auto f = [&](const char* name, bool same) {
      if (!same) d += std::string(d.empty() ? "" : ",") + name;
    };
    f("width", a.width == b.width);
    f("breakWidth", a.breakWidth == b.breakWidth);
    f("spaceWidth", a.spaceWidth == b.spaceWidth);
    f("rawPx", a.rawPx == b.rawPx);
    f("breakPenalty", a.breakPenalty == b.breakPenalty);
    f("stretchWeight", a.stretchWeight == b.stretchWeight);
    f("style", a.style == b.style);
    f("flags", a.flags == b.flags);
    f("text", a.text == b.text);
    f("linkUrl", a.linkUrl == b.linkUrl);
    f("anchorId", a.anchorId == b.anchorId);
    f("ctxTrigram", a.ctxTrigram == b.ctxTrigram);
    f("ctxPrev", a.ctxPrev == b.ctxPrev);
    f("ctxNext", a.ctxNext == b.ctxNext);
    f("kernPx", a.kernPx == b.kernPx);
    f("widthResolved", a.widthResolved == b.widthResolved);
    auto box = [](const LinebreakBlock& x) { return static_cast<const MathBox*>(x.objPayload); };
    f("obj", a.obj == b.obj && a.objKind == b.objKind && a.objAsc == b.objAsc && a.objDesc == b.objDesc &&
                 (!a.obj || a.objKind != ObjKind::Math ||
                  (box(a) && box(b) && box(a)->w == box(b)->w && dumpMathBox(box(a), strs) == dumpMathBox(box(b), strs))));
    f("span", a.span.start == b.span.start && a.span.end == b.span.end);
    if (d.empty()) continue;
    appendf(out, "%s block %zu \"", where.c_str(), i);
    appendEscaped(out, strs.get(a.text));
    appendf(out, "\": %s (pen %g/%g flags %u/%u w %d/%d)\n", d.c_str(), (double)a.breakPenalty,
            (double)b.breakPenalty, a.flags, b.flags, a.width, b.width);
    budget--;
  }
}

}  // namespace

std::string fuseCheck(const std::vector<TopBlock>& tops, const BoxTree& bt, Arena& arena,
                      Interner& strs, StyleTable& styles, const EmitSettings& cfg, MetricStore& metrics,
                      double baseSizePx) {
  DiagSink scratch;
  std::vector<MeasureItem> missing;
  MeasureNeeds mt{&metrics, &styles, &strs, baseSizePx, &missing};
  std::vector<TopBlock> old = emitDocLegacy(bt, arena, strs, styles, cfg, scratch, &mt);
  MeasureRequest req = resolveWidthsLegacy(old, metrics, styles, cfg);
  std::string out;
  if (!req.empty()) out += "legacy path: widths unresolved\n";
  int budget = 12;
  if (old.size() != tops.size()) appendf(out, "%zu tops, legacy %zu\n", tops.size(), old.size());
  for (size_t t = 0; t < tops.size() && t < old.size(); t++) {
    const TopBlock& a = tops[t];
    const TopBlock& b = old[t];
    if (a.units.size() != b.units.size()) {
      appendf(out, "top %zu: %zu units, legacy %zu\n", t, a.units.size(), b.units.size());
      continue;
    }
    for (size_t k = 0; k < a.units.size(); k++) {
      const FlowUnit& ua = a.units[k];
      const FlowUnit& ub = b.units[k];
      std::string where = "top " + std::to_string(t) + " unit " + std::to_string(k);
      // the full lowering against the legacy blocks; the breaker's form
      // (what production keeps) against the full lowering
      auto check = [&](const std::string& at, const HList& h, const std::vector<BreakBlock>& slim,
                       const std::vector<LinebreakBlock>& want) {
        // what the legacy emitter never produced (plan P1-13: inline image,
        // raw and error objects, hard line breaks) has no oracle
        for (const InlineObject& o : h.objs)
          if (o.kind != ObjKind::Math) return;
        for (const HItem& it : h.items)
          if (it.k == IK::Penalty && it.x <= -kPenInf) return;
        std::vector<LinebreakBlock> full;
        std::vector<u32> start;
        fuseLegacy(h, full, start);
        cmpBlocks(out, budget, at, full, want, strs);
        bool same = slim.size() == full.size();
        for (size_t i = 0; same && i < slim.size(); i++)
          same = slim[i].width == full[i].width && slim[i].breakWidth == full[i].breakWidth &&
                 slim[i].spaceWidth == full[i].spaceWidth && slim[i].breakPenalty == full[i].breakPenalty &&
                 slim[i].flags == full[i].flags;
        if (!same) appendf(out, "%s: the breaker's blocks differ from the full lowering\n", at.c_str());
      };
      check(where, ua.hl, ua.blocks, ub.legacy);
      if (ua.cells.size() != ub.cells.size()) {
        appendf(out, "%s: %zu cells, legacy %zu\n", where.c_str(), ua.cells.size(), ub.cells.size());
        continue;
      }
      for (size_t c = 0; c < ua.cells.size(); c++)
        check(where + " cell " + std::to_string(c), ua.cells[c].hl, ua.cells[c].blocks, ub.cells[c].legacy);
    }
  }
  return out;
}

}  // namespace tsr
