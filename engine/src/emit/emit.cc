#include "emit.h"

#include <functional>

#include "../hyphen/hyphen.h"

namespace tsr {

namespace {

struct Emitter {
  Arena& arena;
  DiagSink& diags;
  Interner& strs;
  StyleTable& styles;
  const Config& cfg;
  StrRef spaceRef, hyphenRef, bulletRef;
  StrRef pendingAnchor = 0;  // labeled container (group): first unit anchors
  StrRef takeAnchor() {
    StrRef a = pendingAnchor;
    pendingAnchor = 0;
    return a;
  }

  StyleId compose(StyleId base, u64 addBits, float mul) {
    if (addBits == 0 && mul == 1.0f) return base;
    Styling s = styles.get(base);
    s.bits |= addBits;
    s.sizeMul *= mul;
    return styles.idOf(s);
  }

  // ---- inline walk --------------------------------------------------------
  struct ICtx {
    u64 addBits = 0;
    u16 addFlags = 0;  // e.g. BF_REF for resolver-synthesized content
    float mul = 1.0f;
    StrRef url = 0;
    bool noHyphen = false;  // display context (headings)
  };

  void inlineWalk(const ContentNode* n, FlowUnit& u, ICtx ctx) {
    switch (n->kind) {
      case Kind::text:
        emitText(n, u, ctx);
        return;
      case Kind::link: {
        ICtx c2 = ctx;
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::url && a.tag == ArgTag::Str) c2.url = a.ref;
        c2.addBits |= CLS_LINK;
        for (const ContentNode* k : n->kids) inlineWalk(k, u, c2);
        return;
      }
      case Kind::code: {
        // inline code: single unbreakable block, mono style
        if (!n->kids.empty() && n->kids[0]->kind == Kind::text) {
          LinebreakBlock b;
          b.breakPenalty = BREAK_INF;
          b.flags = ctx.addFlags;
          b.style = compose(n->style, ctx.addBits | CLS_CODE, ctx.mul);
          b.text = n->kids[0]->str;
          b.linkUrl = ctx.url;
          b.span = n->span;
          u.blocks.push_back(b);
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
        for (const ContentNode* k : n->kids) inlineWalk(k, u, c2);
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
        StyleId st = compose(n->style, ctx.addBits, ctx.mul);
        // CJK–formula boundary glue (App C: formulas are Latin-class)
        if (!u.blocks.empty() && u.blocks.back().isCjkChar()) {
          double px = kCjkBoundaryEm * fontPx(st);
          pushSynthetic(u, st, ctx.url, n->span, px,
                        (u16)(BF_SPACE | BF_BOUND | ctx.addFlags), 1.0f, 0.0f, px);
        }
        std::vector<MathSeg> segs = layoutMathSegments(
            strs.get(srcRef), /*display=*/false, fontPx(st), arena, strs,
            diags, n->span);
        for (size_t k = 0; k < segs.size(); k++) {
          if (k) {
            // the break-point glue: discardable at a break (BF_SPACE trims
            // at line edges), rigid otherwise; synthetic for copy (§9.3)
            double pen = segs[k].brkBefore == 1 ? cfg.mathRelAfterPenalty
                         : segs[k].brkBefore == 2 ? cfg.mathRelBeforePenalty
                                                  : cfg.mathBinAfterPenalty;
            LinebreakBlock g;
            g.flags = (u16)(BF_SPACE | BF_BOUND | ctx.addFlags);
            g.breakPenalty = (float)pen;
            g.style = st;
            g.text = spaceRef;
            g.linkUrl = ctx.url;
            g.span = n->span;
            g.width = segs[k].glueBefore;
            g.spaceWidth = 0;
            g.rawPx = suToPx(segs[k].glueBefore);
            g.widthResolved = true;
            // the previous segment itself is unbreakable-after
            u.blocks.back().breakPenalty = BREAK_INF;
            u.blocks.push_back(g);
          }
          LinebreakBlock b;
          b.breakPenalty = 0;  // CJK-context break after a formula is legal
          b.style = st;
          b.text = k == 0 ? srcRef : 0;  // copy: source rides the first segment
          b.linkUrl = ctx.url;
          b.flags = ctx.addFlags;
          b.span = n->span;
          b.math = segs[k].box;
          b.width = segs[k].box->w;
          b.rawPx = suToPx(segs[k].box->w);
          b.widthResolved = true;
          u.blocks.push_back(b);
        }
        return;
      }
      case Kind::comment:
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

  void pushWordBlock(std::string_view w, const ContentNode* n, FlowUnit& u, StyleId st,
                     StrRef url, float penalty, u16 extraFlags = 0) {
    LinebreakBlock b;
    b.breakPenalty = penalty;
    b.style = st;
    b.text = strs.intern(w);
    b.linkUrl = url;
    b.flags = extraFlags;
    b.span = n->span;
    u.blocks.push_back(b);
  }

  void emitWord(std::string_view w, const ContentNode* n, FlowUnit& u, StyleId st, StrRef url,
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
        u.blocks.push_back(hy);
      }
      prev = end;
    }
  }

  double fontPx(StyleId st) { return cfg.baseSizePx * (double)styles.get(st).sizeMul; }

  void pushSynthetic(FlowUnit& u, StyleId st, StrRef url, Span span, double px, u16 flags,
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
    u.blocks.push_back(b);
  }

  void emitText(const ContentNode* n, FlowUnit& u, ICtx ctx) {
    StyleId st = compose(n->style, ctx.addBits, ctx.mul);
    StyleId stCjk = compose(st, CLS_CJK, 1.0f);
    std::string_view s = strs.get(n->str);
    const double halfPx = kPunctHalfEm * fontPx(stCjk);
    const Su glueSu = suRoundPx(cfg.cjkGlueEm * fontPx(stCjk));

    enum class Prev : u8 { None, Latin, Cjk };
    Prev prev = Prev::None;
    std::string word;
    u32 i = 0;

    auto flushWord = [&] {
      if (!word.empty()) {
        emitWord(word, n, u, st, ctx.url, ctx.noHyphen, ctx.addFlags);
        word.clear();
      }
    };
    auto boundary = [&] {
      double px = kCjkBoundaryEm * fontPx(st);
      pushSynthetic(u, st, ctx.url, n->span, px, (u16)(BF_SPACE | BF_BOUND | ctx.addFlags),
                    1.0f, 0.0f, px);
    };
    {  // formula → CJK boundary: the previous inline block was math
      if (!u.blocks.empty() && u.blocks.back().math && !s.empty()) {
        u32 j0 = 0;
        u32 first = utf8Next(s, j0);
        if (isCjkIdeo(first)) boundary();
      }
    }
    auto lastIsCloseSp = [&] {  // a closing/dot punct's trailing half
      return !u.blocks.empty() && (u.blocks.back().flags & BF_PUNCT_SP) &&
             !(u.blocks.back().flags & BF_PUNCT_OPEN);
    };
    auto lastIsOpenGlyph = [&] {
      return !u.blocks.empty() && (u.blocks.back().flags & BF_PUNCT_GLYPH) &&
             (u.blocks.back().flags & BF_PUNCT_OPEN);
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
      b.linkUrl = ctx.url;
      b.span = n->span;
      if (definedEm > 0) {
        double px = definedEm * fontPx(stCjk);
        b.width = suRoundPx(px);
        b.rawPx = px;
        b.widthResolved = true;
      }
      u.blocks.push_back(b);
    };
    auto pushPunct = [&](std::string_view ch, bool open) {
      const PunctCompress mode = cfg.punctCompress;
      // BF_PUNCT_OPEN on a half-space marks it as an OPENING punct's leading
      // half — the renderer squeezes a glyph only when its OWN half is absent.
      const u16 openSpFlags = (u16)(BF_SPACE | BF_PUNCT_SP | BF_PUNCT_OPEN | ctx.addFlags);
      if (open) {
        if (lastIsCloseSp()) {
          // closing/dot + opening
          if (mode == PunctCompress::Full) u.blocks.pop_back();  // set solid
          else if (mode == PunctCompress::None)
            pushSynthetic(u, stCjk, ctx.url, n->span, halfPx, openSpFlags, 0.0f, 0.0f, 0.0);
          // Book: the closer's breakable half stays as the breathing space
        } else if (lastIsOpenGlyph()) {
          // opening + opening: solid (a breakable gap here would let the
          // first opener dangle at a line end — 禁则); None keeps a RIGID half
          if (mode == PunctCompress::None)
            pushSynthetic(u, stCjk, ctx.url, n->span, halfPx, openSpFlags, 0.0f, BREAK_INF, 0.0);
        } else {
          pushSynthetic(u, stCjk, ctx.url, n->span, halfPx, openSpFlags,
                        0.0f, 0.0f, 0.0);  // leading half — breakable, NOT stretchable
        }
      } else {
        // 禁则: no break before a closing punct (inline formulas included)
        if (!u.blocks.empty() && (u.blocks.back().isCjkChar() || u.blocks.back().math))
          u.blocks.back().breakPenalty = BREAK_INF;
        if (lastIsCloseSp()) {
          // closing + closing: solid; None keeps the half but rigid (a break
          // would put the second closer at a line start — 禁则)
          if (mode == PunctCompress::None) u.blocks.back().breakPenalty = BREAK_INF;
          else u.blocks.pop_back();
        }
      }
      LinebreakBlock g;
      g.flags = (u16)(BF_CJK | BF_PUNCT_GLYPH | (open ? BF_PUNCT_OPEN : 0) | ctx.addFlags);
      g.breakPenalty = BREAK_INF;
      g.style = stCjk;
      g.text = strs.intern(ch);
      g.linkUrl = ctx.url;
      g.span = n->span;
      u.blocks.push_back(g);
      if (!open)
        pushSynthetic(u, stCjk, ctx.url, n->span, halfPx,
                      (u16)(BF_SPACE | BF_PUNCT_SP | ctx.addFlags), 0.0f, 0.0f, 0.0);
    };

    while (i < s.size()) {
      u32 start = i;
      u32 cp = utf8Next(s, i);
      if (cp == ' ' || cp == '\t') {
        flushWord();
        LinebreakBlock b;
        b.flags = (u16)(BF_SPACE | ctx.addFlags);
        b.breakPenalty = 0;
        b.stretchWeight = 1;
        b.style = st;
        b.text = spaceRef;
        b.linkUrl = ctx.url;
        b.span = n->span;
        u.blocks.push_back(b);
        prev = Prev::None;
        continue;
      }
      // U+2014/U+2026 sit outside the CJK ranges but are CJK-class here
      // (em-dash/ellipsis pairs, App C) — without this they would take the
      // Latin path and grow spurious boundary glue on both sides.
      if (isCjkIdeo(cp) || cp == 0x2014 || cp == 0x2026) {
        flushWord();
        if (prev == Prev::Latin) boundary();
        // em-dash / ellipsis: defined-width pinned blocks — 2em as a pair,
        // 1em alone (App C; advance is unmeasurable, see pushCjkChar)
        if (cp == 0x2014 || cp == 0x2026) {
          u32 j = i;
          u32 cp2 = (i < s.size()) ? utf8Next(s, j) : 0;
          if (cp2 == cp) {
            pushCjkChar(s.substr(start, j - start), /*definedEm=*/2.0);
            i = j;
          } else {
            pushCjkChar(s.substr(start, i - start), /*definedEm=*/1.0);
          }
          prev = Prev::Cjk;
          continue;
        }
        pushCjkChar(s.substr(start, i - start));
        prev = Prev::Cjk;
        continue;
      }
      if (isPunctOpen(cp) || isPunctClose(cp)) {
        flushWord();
        if (prev == Prev::Latin) boundary();
        pushPunct(s.substr(start, i - start), isPunctOpen(cp));
        prev = Prev::Cjk;
        continue;
      }
      if (prev == Prev::Cjk) boundary();
      word.append(s.data() + start, i - start);
      prev = Prev::Latin;
    }
    flushWord();
  }

  // ---- block walk ---------------------------------------------------------
  void blockWalk(const ContentNode* n, Su indent, StrRef marker, TopBlock& tb) {
    switch (n->kind) {
      case Kind::para: {
        FlowUnit u;
        u.src = n;
        u.indent = indent;
        u.marker = marker;
        u.markerStyle = compose(n->style, 0, 1.0f);
        u.anchor = takeAnchor();
        if (cfg.paraIndentEm > 0 && marker == 0) {  // 首行缩进 (App C: blocks, not CSS)
          double px = cfg.paraIndentEm * cfg.baseSizePx;
          pushSynthetic(u, n->style, 0, n->span, px, BF_INDENT, 0.0f, BREAK_INF, 0.0);
        }
        inlineWalk(n, u, {});
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::heading: {
        int level = 1;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::level && a.tag == ArgTag::Num) level = (int)a.num;
        }
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
        inlineWalk(n, u, ctx);
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::list: {
        bool ordered = false;
        int num = 1;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::ordered && a.tag == ArgTag::Bool) ordered = a.num != 0;
          if (a.key == ArgK::start && a.tag == ArgTag::Num) num = (int)a.num;
        }
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
        u.codeStyle = compose(n->style, CLS_CODE, 1.0f);
        u.markerStyle = u.codeStyle;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::wrap && a.tag == ArgTag::Bool) u.codeWrap = a.num != 0;
          if (a.key == ArgK::lineNo && a.tag == ArgTag::Num) u.codeLineNo = (i32)a.num;
          if (a.key == ArgK::lineNo && a.tag == ArgTag::Bool && a.num != 0) u.codeLineNo = 1;
          if (a.key == ArgK::hl && a.tag == ArgTag::Str) {
            // "3,5-7" → 1-based line set
            std::string_view h = strs.get(a.ref);
            size_t p = 0;
            while (p < h.size()) {
              size_t c = h.find(',', p);
              if (c == std::string_view::npos) c = h.size();
              std::string_view part = h.substr(p, c - p);
              size_t dash = part.find('-');
              int lo = atoi(std::string(part.substr(0, dash)).c_str());
              int hi = dash == std::string_view::npos
                           ? lo
                           : atoi(std::string(part.substr(dash + 1)).c_str());
              for (int k = lo; k <= hi && k - lo < 10000; k++)
                if (k > 0) u.hlLines.push_back((u32)k);
              p = c + 1;
            }
          }
        }
        // Two body forms (CH1): a single text child = plain lines split on
        // \n; otherwise each child is one line (seq of styled runs — the
        // leaves' styles were already folded at instantiation).
        if (n->kids.size() == 1 && n->kids[0]->kind == Kind::text) {
          std::string_view body = strs.get(n->kids[0]->str);
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
          std::function<void(const ContentNode*, std::vector<FlowUnit::CodeRun>&)>
              collect = [&](const ContentNode* k, std::vector<FlowUnit::CodeRun>& out) {
                if (k->kind == Kind::text) {
                  out.push_back({k->str, compose(k->style, CLS_CODE, 1.0f)});
                  return;
                }
                if (k->kind == Kind::comment) return;
                for (const ContentNode* c : k->kids) collect(c, out);
              };
          for (const ContentNode* lineNode : n->kids) {
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
        int cols = 1;
        StrRef alignRef = 0;
        for (const ArgVal& a : n->args) {
          if (a.key == ArgK::cols && a.tag == ArgTag::Num) cols = (int)a.num;
          if (a.key == ArgK::align && a.tag == ArgTag::Str) alignRef = a.ref;
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref) u.anchor = a.ref;
        }
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
            for (const ContentNode* k : cell->kids) inlineWalk(k, tmp, {});
            tc.blocks = std::move(tmp.blocks);
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
                                      fontPx(n->style), arena, strs, diags, n->span);
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
        inlineWalk(n, u, ctx);  // error case renders ⚠ + message
        tb.units.push_back(std::move(u));
        return;
      }
      case Kind::comment:
        return;
      case Kind::group: {
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::label && a.tag == ArgTag::Str && a.ref)
            pendingAnchor = a.ref;
        for (const ContentNode* k : n->kids) blockWalk(k, indent, 0, tb);
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

// Word spaces absorb cross-space kerning (e.g. Georgia "s. A"): sum-of-words
// measurement misses it, leaving every justified line systematically short.
// Tag each plain space with its neighbouring codepoints; resolveWidths turns
// that into gap = m(prev+' '+next) - m(prev) - m(next).
static void fillSpaceContexts(std::vector<TopBlock>& tops, Interner& strs) {
  auto lastCp = [&](const LinebreakBlock& b) -> std::string {
    std::string_view t = strs.get(b.text);
    if (t.empty()) return {};
    u32 cp = utf8PrevCp(t, (u32)t.size());
    if (isCjk(cp) || cp >= 0x2000) return {};  // no cross-space kern vs CJK
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
    if (isCjk(cp) || cp >= 0x2000) return {};
    return std::string(t.substr(0, i));
  };
  auto isWord = [](const LinebreakBlock& b) {
    return !b.isSpace() && !b.isHyphen() && !b.isSynthetic() && !b.isCjkChar() &&
           !b.isPunctGlyph() && !b.math && b.text != 0;
  };
  auto tag = [&](std::vector<LinebreakBlock>& blocks) {
    for (size_t i = 0; i < blocks.size(); i++) {
      LinebreakBlock& b = blocks[i];
      if (!b.isSpace() || (b.flags & (BF_PUNCT_SP | BF_BOUND))) continue;
      if (i == 0 || i + 1 >= blocks.size()) continue;
      if (!isWord(blocks[i - 1]) || !isWord(blocks[i + 1])) continue;
      std::string prev = lastCp(blocks[i - 1]);
      std::string next = firstCp(blocks[i + 1]);
      if (prev.empty() || next.empty()) continue;
      b.ctxPrev = strs.intern(prev);
      b.ctxNext = strs.intern(next);
      b.ctxTrigram = strs.intern(prev + " " + next);
    }
  };
  for (TopBlock& tb : tops) {
    for (FlowUnit& u : tb.units) {
      tag(u.blocks);
      for (TableCell& c : u.cells) tag(c.blocks);
    }
  }
}

std::vector<TopBlock> emitDoc(const ContentTree& tree, Arena& arena,
                              Interner& strs, StyleTable& styles,
                              const Config& cfg, DiagSink& diags) {
  std::vector<TopBlock> tops;
  if (!tree.root) return tops;
  Emitter e{arena, diags, strs, styles, cfg,
            strs.intern(" "), strs.intern("-"), strs.intern("\xE2\x80\xA2")};
  u32 pid = 0;
  for (const ContentNode* child : tree.root->kids) {
    TopBlock tb;
    tb.pid = pid++;
    tb.node = child;
    e.blockWalk(child, 0, 0, tb);
    if (!tb.units.empty()) tops.push_back(std::move(tb));
  }
  fillSpaceContexts(tops, strs);
  return tops;
}

MeasureRequest resolveWidths(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const Config& cfg) {
  MeasureRequest req;
  std::unordered_map<u64, bool> seenWord;
  std::vector<bool> seenStyle(styles.count(), false);
  auto needStyle = [&](StyleId st) {
    if (st < seenStyle.size() && !seenStyle[st]) {
      seenStyle[st] = true;
      if (!store.hasVmet(st)) req.vmetStyles.push_back(st);
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
              u64 k = MetricStore::key(r, b.style);
              if (!seenWord.count(k)) {
                seenWord[k] = true;
                req.words.push_back({r, b.style});
              }
            }
          }
        }
        if (store.hasWord(b.text, b.style) && ctxReady) {
          const WordMet& w = store.word(b.text, b.style);
          if (b.isHyphen()) {
            b.breakWidth = w.su;
            b.rawPx = w.px;  // only added to a line when it ends at this block
          } else if (b.isPunctGlyph()) {
            // glyph advance minus its compressible half (App C): the half
            // lives in the adjacent BF_PUNCT_SP block (or was compressed away)
            double halfPx = kPunctHalfEm * cfg.baseSizePx * (double)styles.get(b.style).sizeMul;
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
          u64 k = MetricStore::key(b.text, b.style);
          if (!seenWord.count(k)) {
            seenWord[k] = true;
            req.words.push_back({b.text, b.style});
          }
        }
      }
  };
  for (TopBlock& tb : tops) {
    for (FlowUnit& u : tb.units) {
      if (u.kind == FlowUnit::K::Code) needStyle(u.codeStyle);
      resolveBlocks(u.blocks);
      for (TableCell& c : u.cells) resolveBlocks(c.blocks);
    }
  }
  return req;
}

std::string dumpBlocks(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles) {
  std::string out;
  for (const TopBlock& tb : tops) {
    appendf(out, "top pid=%u units=%zu\n", tb.pid, tb.units.size());
    for (const FlowUnit& u : tb.units) {
      const char* k = u.kind == FlowUnit::K::Text ? "text"
                      : u.kind == FlowUnit::K::Code ? "code"
                      : u.kind == FlowUnit::K::Raw ? "raw"
                      : u.kind == FlowUnit::K::Table ? "table"
                      : u.kind == FlowUnit::K::Math ? "math" : "rule";
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
      out += "\n";
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
        if (st.fontFamily) {
          out += " font=\"";
          appendEscaped(out, strs.get(st.fontFamily));
          out += "\"";
        }
        if (st.lang) {
          out += " lang=";
          out += strs.get(st.lang);
        }
        if (st.color) {
          out += " color=";
          out += strs.get(st.color);
        }
        if (st.sizePx > 0) appendf(out, " size=%gpx", (double)st.sizePx);
        appendf(out, " @[%u,%u)\n", b.span.start, b.span.end);
      };
      for (const LinebreakBlock& b : u.blocks) dumpBlock(b);
      for (size_t ci = 0; ci < u.cells.size(); ci++) {
        appendf(out, "  cell %zu\n", ci);
        for (const LinebreakBlock& b : u.cells[ci].blocks) dumpBlock(b);
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
      for (const LinebreakBlock& b : u.blocks) {
        if (!b.math) continue;
        appendf(out, "inline pid=%u \"", tb.pid);
        appendEscaped(out, strs.get(b.text));
        out += "\"\n";
        out += dumpMathBox(b.math, strs);
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
