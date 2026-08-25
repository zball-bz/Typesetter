#include "emit.h"

#include "../hyphen/hyphen.h"

namespace tsr {

namespace {

struct Emitter {
  Interner& strs;
  StyleTable& styles;
  const Config& cfg;
  StrRef spaceRef, hyphenRef, bulletRef;

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
          b.style = compose(n->style, ctx.addBits | CLS_CODE, ctx.mul);
          b.text = n->kids[0]->str;
          b.linkUrl = ctx.url;
          b.span = n->span;
          u.blocks.push_back(b);
        }
        return;
      }
      case Kind::comment:
        return;
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
                bool noHyphen) {
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
      pushWordBlock(w, n, u, st, url, BREAK_INF);
      return;
    }
    u32 prev = 0;  // within core
    for (size_t k = 0; k <= pts.size(); k++) {
      u32 end = (k < pts.size()) ? pts[k] : e - a;
      std::string seg;
      if (k == 0) seg += w.substr(0, a);  // lead
      seg += w.substr(a + prev, end - prev);
      if (k == pts.size()) seg += w.substr(e);  // trail
      pushWordBlock(seg, n, u, st, url, BREAK_INF);
      if (k < pts.size()) {
        LinebreakBlock hy;
        hy.breakPenalty = (float)cfg.hyphenPenalty;
        hy.style = st;
        hy.text = hyphenRef;
        hy.linkUrl = url;
        hy.flags = BF_HYPHEN;
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
        emitWord(word, n, u, st, ctx.url, ctx.noHyphen);
        word.clear();
      }
    };
    auto boundary = [&] {
      double px = kCjkBoundaryEm * fontPx(st);
      pushSynthetic(u, st, ctx.url, n->span, px, BF_SPACE | BF_BOUND, 1.0f, 0.0f, px);
    };
    auto lastIsCloseSp = [&] {  // a closing/dot punct's trailing half
      return !u.blocks.empty() && (u.blocks.back().flags & BF_PUNCT_SP) &&
             !(u.blocks.back().flags & BF_PUNCT_OPEN);
    };
    auto lastIsOpenGlyph = [&] {
      return !u.blocks.empty() && (u.blocks.back().flags & BF_PUNCT_GLYPH) &&
             (u.blocks.back().flags & BF_PUNCT_OPEN);
    };
    auto pushCjkChar = [&](std::string_view chars, bool pair = false) {
      LinebreakBlock b;
      b.flags = pair ? (BF_CJK | BF_PAIR) : BF_CJK;
      b.breakPenalty = 0;
      b.stretchWeight = (float)cfg.cjkJustifyK;
      b.spaceWidth = glueSu;  // stretch capacity for the cost fn (App C)
      b.style = stCjk;
      b.text = strs.intern(chars);
      b.linkUrl = ctx.url;
      b.span = n->span;
      u.blocks.push_back(b);
    };
    auto pushPunct = [&](std::string_view ch, bool open) {
      const PunctCompress mode = cfg.punctCompress;
      // BF_PUNCT_OPEN on a half-space marks it as an OPENING punct's leading
      // half — the renderer squeezes a glyph only when its OWN half is absent.
      const u16 openSpFlags = BF_SPACE | BF_PUNCT_SP | BF_PUNCT_OPEN;
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
        // 禁则: no break before a closing punct
        if (!u.blocks.empty() && u.blocks.back().isCjkChar())
          u.blocks.back().breakPenalty = BREAK_INF;
        if (lastIsCloseSp()) {
          // closing + closing: solid; None keeps the half but rigid (a break
          // would put the second closer at a line start — 禁则)
          if (mode == PunctCompress::None) u.blocks.back().breakPenalty = BREAK_INF;
          else u.blocks.pop_back();
        }
      }
      LinebreakBlock g;
      g.flags = (u16)(BF_CJK | BF_PUNCT_GLYPH | (open ? BF_PUNCT_OPEN : 0));
      g.breakPenalty = BREAK_INF;
      g.style = stCjk;
      g.text = strs.intern(ch);
      g.linkUrl = ctx.url;
      g.span = n->span;
      u.blocks.push_back(g);
      if (!open)
        pushSynthetic(u, stCjk, ctx.url, n->span, halfPx, BF_SPACE | BF_PUNCT_SP, 0.0f,
                      0.0f, 0.0);
    };

    while (i < s.size()) {
      u32 start = i;
      u32 cp = utf8Next(s, i);
      if (cp == ' ' || cp == '\t') {
        flushWord();
        LinebreakBlock b;
        b.flags = BF_SPACE;
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
        // em-dash / ellipsis pairs: one unbreakable 2em block (App C)
        if ((cp == 0x2014 || cp == 0x2026) && i < s.size()) {
          u32 j = i;
          u32 cp2 = utf8Next(s, j);
          if (cp2 == cp) {
            pushCjkChar(s.substr(start, j - start), /*pair=*/true);
            i = j;
            prev = Prev::Cjk;
            continue;
          }
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
        for (const ArgVal& a : n->args)
          if (a.key == ArgK::level && a.tag == ArgTag::Num) level = (int)a.num;
        FlowUnit u;
        u.src = n;
        u.indent = indent;
        u.marker = marker;
        u.markerStyle = n->style;
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
        if (!n->kids.empty() && n->kids[0]->kind == Kind::text) {
          std::string_view body = strs.get(n->kids[0]->str);
          size_t pos = 0;
          while (pos <= body.size()) {
            size_t eol = body.find('\n', pos);
            if (eol == std::string_view::npos) eol = body.size();
            u.codeLines.push_back(strs.intern(body.substr(pos, eol - pos)));
            if (eol == body.size()) break;
            pos = eol + 1;
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
      case Kind::comment:
        return;
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
           !b.isPunctGlyph() && b.text != 0;
  };
  for (TopBlock& tb : tops) {
    for (FlowUnit& u : tb.units) {
      for (size_t i = 0; i < u.blocks.size(); i++) {
        LinebreakBlock& b = u.blocks[i];
        if (!b.isSpace() || (b.flags & (BF_PUNCT_SP | BF_BOUND))) continue;
        if (i == 0 || i + 1 >= u.blocks.size()) continue;
        if (!isWord(u.blocks[i - 1]) || !isWord(u.blocks[i + 1])) continue;
        std::string prev = lastCp(u.blocks[i - 1]);
        std::string next = firstCp(u.blocks[i + 1]);
        if (prev.empty() || next.empty()) continue;
        b.ctxPrev = strs.intern(prev);
        b.ctxNext = strs.intern(next);
        b.ctxTrigram = strs.intern(prev + " " + next);
      }
    }
  }
}

std::vector<TopBlock> emitDoc(const ContentTree& tree, Interner& strs,
                              StyleTable& styles, const Config& cfg) {
  std::vector<TopBlock> tops;
  if (!tree.root) return tops;
  Emitter e{strs, styles, cfg, strs.intern(" "), strs.intern("-"), strs.intern("\xE2\x80\xA2")};
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
  for (TopBlock& tb : tops) {
    for (FlowUnit& u : tb.units) {
      if (u.kind == FlowUnit::K::Code) needStyle(u.codeStyle);
      for (LinebreakBlock& b : u.blocks) {
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
                      : u.kind == FlowUnit::K::Code ? "code" : "rule";
      appendf(out, " unit %s indent=%dsu", k, u.indent);
      if (u.marker) {
        out += " marker=\"";
        appendEscaped(out, strs.get(u.marker));
        out += "\"";
      }
      if (u.kind == FlowUnit::K::Code) appendf(out, " lines=%zu", u.codeLines.size());
      out += "\n";
      for (const LinebreakBlock& b : u.blocks) {
        out += "  ";
        if (b.flags & BF_INDENT) appendf(out, "indent w=%dsu", b.width);
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
        if (st.sizeMul != 1.0f) appendf(out, " x%.2f", (double)st.sizeMul);
        appendf(out, " @[%u,%u)\n", b.span.start, b.span.end);
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
