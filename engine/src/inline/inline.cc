// Inline parser (M2): text, strict-pair emphasis, escapes, splices (with
// content arguments), links, code spans, inline comments. Hand-rolled to the
// v2 §3/§5 spec; multi-span input (paragraph lines join with soft spaces).
#include "../ast/ast.h"
#include "jslex.h"

namespace tsr {

namespace {

struct Frame {
  u8 marker;  // 0 for root
  u32 markerPos = 0;
  std::vector<AstNode*> items;
};

struct InlineParser {
  const SourceText& src;
  Arena& arena;
  Interner& strs;
  DiagSink& diags;
  std::string_view all;

  std::vector<Span> spans;
  size_t sp = 0;  // current span index
  u32 i = 0;      // current byte position (within spans[sp])

  std::vector<Frame> stack;
  std::string buf;
  u32 bufStart = 0, bufEnd = 0;
  bool pendingSpace = false;
  bool prevGlyph = false;

  AstAlloc A{arena};

  u32 spanEnd() const { return spans[sp].end; }
  bool atEnd() const { return sp >= spans.size(); }
  void advanceSpanIfNeeded() {
    while (sp < spans.size() && i >= spans[sp].end) {
      u32 prevEnd = spans[sp].end;
      sp++;
      if (sp < spans.size()) {
        // Line join = soft space — EXCEPT between two CJK-class codepoints:
        // a source line break inside CJK prose joins seamlessly (clreq).
        auto cjkish = [](u32 c) { return isCjk(c) || c == 0x2014 || c == 0x2026; };
        bool cjkJoin = false;
        if (prevEnd > 0 && spans[sp].start < all.size()) {
          u32 tmp = spans[sp].start;
          cjkJoin = cjkish(utf8PrevCp(all, prevEnd)) && cjkish(utf8Next(all, tmp));
        }
        if (!cjkJoin) {
          pendingSpace = true;
          prevGlyph = false;
        }
        i = spans[sp].start;
      }
    }
  }

  // The bytes between two line spans of this leaf are a plain line break:
  // trailing/leading blanks and exactly one \n (\r\n included) — no container
  // prefix was stripped in between.
  bool plainGap(u32 a, u32 b) const {
    int nl = 0;
    for (u32 p = a; p < b; p++) {
      char c = all[p];
      if (c == '\n') {
        if (++nl > 1) return false;
      } else if (c != ' ' && c != '\t' && c != '\r') {
        return false;
      }
    }
    return nl == 1;
  }
  // True if a raw scan from the cursor to the exclusive end `to` stays inside
  // this leaf and crosses only plain line breaks — cross-line raw scans are
  // only sound then. A scan that would end past the leaf's last span never
  // is: islands and splices cannot escape their block (plan P0-04).
  bool contiguous(u32 to) const {
    if (spans.empty() || to > spans.back().end) return false;
    for (size_t k = sp; k + 1 < spans.size() && spans[k].end < to; k++)
      if (!plainGap(spans[k].end, spans[k + 1].start)) return false;
    return true;
  }
  // exclusive end of this leaf's text
  u32 leafEnd() const { return spans.empty() ? 0 : spans.back().end; }
  // Move the cursor to raw offset `to`.
  void seekTo(u32 to) {
    while (sp < spans.size() && spans[sp].end < to) sp++;
    i = to;
  }

  void put(char c, u32 pos, u32 len = 1) {
    if (buf.empty()) bufStart = pos;
    if (pendingSpace) {
      if (!buf.empty() || !stack.back().items.empty()) buf += ' ';
      pendingSpace = false;
    }
    buf += c;
    bufEnd = pos + len;
    prevGlyph = true;
  }

  void flushText() {
    if (buf.empty()) return;
    AstNode* t = A.node(AstKind::Text, {bufStart, bufEnd});
    t->str = strs.intern(buf);
    stack.back().items.push_back(t);
    buf.clear();
  }

  void spaceBeforeItem() {
    if (pendingSpace) {
      if (!buf.empty() || !stack.back().items.empty()) {
        if (buf.empty()) bufStart = bufEnd;
        buf += ' ';
      }
      pendingSpace = false;
    }
  }

  void pushItem(AstNode* n) {
    stack.back().items.push_back(n);
    prevGlyph = true;
  }

  // find matching ']' from position `from` (which is at '['), single span
  i32 matchBracket(u32 from, u32 lim) const {
    int depth = 0;
    for (u32 p = from; p < lim; p++) {
      char c = all[p];
      if (c == '\\') { p++; continue; }
      if (c == '[') depth++;
      else if (c == ']') {
        depth--;
        if (depth == 0) return (i32)p;
      }
    }
    return -1;
  }

  // matching ')' for a URL — plain depth scan, NOT the JS lexer ('//' in
  // https:// is not a comment here).
  i32 matchParen(u32 from, u32 lim) const {
    int depth = 0;
    for (u32 p = from; p < lim; p++) {
      char c = all[p];
      if (c == '\\') { p++; continue; }
      if (c == '(') depth++;
      else if (c == ')') {
        depth--;
        if (depth == 0) return (i32)p;
      }
    }
    return -1;
  }

  // a content body: an inline parse of one span
  std::vector<AstNode*> parseSub(Span s) {
    InlineParser p{src, arena, strs, diags, all};
    p.spans = {s};
    p.run();
    return std::move(p.stack.back().items);
  }

  void handleSplice(u32 hashPos) {
    const u32 lim = spanEnd();
    u32 exprStart = hashPos + 1;
    u32 exprEnd = exprStart;
    u32 lastCall = 0;
    if (exprStart < lim && all[exprStart] == '(') {
      JsScan s = scanJs(all, exprStart, true);
      bool ok = s.ok && (s.end <= lim || contiguous(s.end));
      if (!ok) {
        diags.add(Sev::Error, "splice-js", {hashPos, s.end}, "unbalanced #(...)");
        put('#', hashPos);
        i = hashPos + 1;
        return;
      }
      exprEnd = s.end;
    } else {
      // head chain; track trailing call for content-arg desugaring
      u32 p = exprStart;
      if (p >= lim || !isIdentStart(all[p])) {
        put('#', hashPos);
        i = hashPos + 1;
        return;
      }
      while (p < lim && isIdentCont(all[p])) p++;
      for (;;) {
        if (p + 1 < lim && all[p] == '.' && isIdentStart(all[p + 1])) {
          p += 2;
          while (p < lim && isIdentCont(all[p])) p++;
          lastCall = 0;
          continue;
        }
        if (p < lim && all[p] == '(') {
          JsScan s = scanJs(all, p, true);
          if (!s.ok || (s.end > lim && !contiguous(s.end))) break;
          lastCall = p;
          p = s.end;
          continue;
        }
        break;
      }
      exprEnd = p;
    }

    spaceBeforeItem();
    flushText();
    // a keyword as a bare head (#if, #for, #new …) would paste invalid JS
    // and fail the whole document: it becomes an error node (plan P0-05)
    if (exprStart < lim && all[exprStart] != '(') {
      u32 h = exprStart;
      while (h < exprEnd && isIdentCont(all[h])) h++;
      std::string_view head = all.substr(exprStart, h - exprStart);
      const char* code = reservedSpliceHead(head);
      if (code) {
        u32 after = exprEnd;
        while (after < lim && all[after] == '[') {
          i32 close = matchBracket(after, lim);
          if (close < 0) break;
          after = (u32)close + 1;
        }
        std::string msg = std::string(code) == "keyword-unsupported"
            ? "#" + std::string(head) + " is not supported yet (keyword forms: plan P2-12)"
            : "'" + std::string(head) + "' is a reserved word and cannot start a splice";
        diags.add(Sev::Error, code, {hashPos, after}, msg);
        AstNode* e = A.node<ErrorP>(AstKind::Error, {hashPos, after});
        e->str = strs.intern(code);
        side<ErrorP>(e).message = strs.intern(msg);
        pushItem(e);
        seekTo(after);
        return;
      }
    }
    AstNode* spl = A.node<SpliceP>(AstKind::Splice, {hashPos, exprEnd});
    side<SpliceP>(spl) = {{exprStart, exprEnd}, lastCall};

    // content arguments: directly adjacent [ ... ], repeatable (single span)
    std::vector<AstNode*> args;
    u32 after = exprEnd;
    while (after < lim && all[after] == '[') {
      i32 close = matchBracket(after, lim);
      if (close < 0) {
        diags.add(Sev::Error, "parse-inline", {after, lim}, "unclosed content argument");
        break;
      }
      AstNode* arg = A.call(SugarId::arg, {after + 1, (u32)close});
      A.setKids(arg, parseSub({after + 1, (u32)close}));
      args.push_back(arg);
      after = (u32)close + 1;
    }
    A.setKids(spl, args);
    if (after < lim && all[after] == ';') after++;  // hard terminator
    spl->span.end = after;
    pushItem(spl);
    seekTo(after);
  }

  void run() {
    stack.push_back({0});
    if (spans.empty()) return;
    i = spans[0].start;
    while (true) {
      advanceSpanIfNeeded();
      if (atEnd()) break;
      const u32 lim = spanEnd();
      char c = all[i];

      if (c == ' ' || c == '\t' || c == '\r') {
        pendingSpace = true;
        prevGlyph = false;
        i++;
        continue;
      }
      if (c == '\\' && i + 1 < lim) {
        put(all[i + 1], i, 2);
        i += 2;
        continue;
      }
      if (c == '%' && i + 2 < lim && all[i + 1] == '-' && all[i + 2] == '-') {
        // inline comment — lexically dumb, may span lines; spacing state is
        // unaffected (a comment is invisible to the text around it).
        u32 p = i + 3;
        int depth = 1;
        u32 hardEnd = leafEnd();  // a comment never escapes its block
        while (p < hardEnd) {
          if (p + 2 < hardEnd && all[p] == '%' && all[p + 1] == '-' && all[p + 2] == '-') { depth++; p += 3; continue; }
          if (p + 2 < hardEnd && all[p] == '-' && all[p + 1] == '-' && all[p + 2] == '%') {
            depth--;
            p += 3;
            if (depth == 0) break;
            continue;
          }
          p++;
        }
        if (depth != 0) {
          // unclosed inside its block: literal text, the error stays visible
          diags.add(Sev::Error, "parse-inline", {i, i + 3}, "unterminated comment");
          put('%', i);
          put('-', i + 1);
          put('-', i + 2);
          i += 3;
          continue;
        }
        flushText();
        AstNode* cm = A.node(AstKind::Comment, {i, p});
        std::string body = crlfToLf(all.substr(i + 3, p - 3 - (i + 3)));
        cm->str = strs.intern(body);
        stack.back().items.push_back(cm);  // does not set prevGlyph
        seekTo(p);
        continue;
      }
      if (c == '$') {
        // math island (v2 §5): verbatim to the closing '$'; may cross source
        // lines when only plain newlines intervene (same rule as splices)
        u32 hardEnd = leafEnd();  // an island never escapes its block
        u32 p = i + 1;
        while (p < hardEnd && all[p] != '$') {
          if (all[p] == '\\' && p + 1 < hardEnd) p++;
          p++;
        }
        // a '$' not closed within its block stays literal text (it used to
        // take a closer from a later block, duplicating the blocks between)
        bool ok = p < hardEnd && (p < lim || contiguous(p + 1));
        if (ok && p > i + 1) {
          spaceBeforeItem();
          flushText();
          std::string body = crlfToLf(all.substr(i + 1, p - (i + 1)));
          // $ x $ (whitespace inside both fences) is display math (Typst rule)
          auto isWs = [](char ch) {
            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
          };
          bool display = body.size() >= 2 && isWs(body.front()) && isWs(body.back());
          size_t b0 = 0, b1 = body.size();
          while (b0 < b1 && isWs(body[b0])) b0++;
          while (b1 > b0 && isWs(body[b1 - 1])) b1--;
          AstNode* mn = A.call<MathP>(SugarId::math, {i, p + 1});
          mn->str = strs.intern(body.substr(b0, b1 - b0));
          side<MathP>(mn).display = display;
          pushItem(mn);
          // equation label: ` <id>` directly after the closing $ (v2 §11.1
          // heading-label form); labelled display formulas get numbers
          u32 after = p + 1;
          u32 lim2 = spanEnd();
          if (after < lim2 && all[after] == ' ' && after + 1 < lim2 &&
              all[after + 1] == '<') {
            u32 lb = after + 2, le2 = lb;
            while (le2 < lim2 && all[le2] != '>' && all[le2] != '<') le2++;
            if (le2 < lim2 && all[le2] == '>' && le2 > lb) {
              side<MathP>(mn).label = strs.intern(all.substr(lb, le2 - lb));
              mn->span.end = le2 + 1;
              seekTo(le2 + 1);
              continue;
            }
          }
          seekTo(p + 1);
          continue;
        }
        put(c, i);
        i++;
        continue;
      }
      if (c == '`') {
        u32 close = i + 1;
        while (close < lim && all[close] != '`') close++;
        if (close < lim) {
          spaceBeforeItem();
          flushText();
          AstNode* code = A.call(SugarId::code, {i, close + 1});
          std::string body(all.substr(i + 1, close - (i + 1)));
          code->str = strs.intern(body);
          pushItem(code);
          i = close + 1;
          continue;
        }
        put(c, i);
        i++;
        continue;
      }
      if (c == '[') {
        i32 close = matchBracket(i, lim);
        if (close >= 0 && (u32)close + 1 < lim && all[close + 1] == '(') {
          i32 pclose = matchParen((u32)close + 1, lim);
          if (pclose >= 0) {
            spaceBeforeItem();
            flushText();
            AstNode* link = A.call<LinkP>(SugarId::link, {i, (u32)pclose + 1});
            A.setKids(link, parseSub({i + 1, (u32)close}));
            std::string url(all.substr((u32)close + 2, (u32)pclose - ((u32)close + 2)));
            side<LinkP>(link).url = strs.intern(url);
            pushItem(link);
            i = (u32)pclose + 1;
            continue;
          }
        }
        put(c, i);
        i++;
        continue;
      }
      if (c == '*' || c == '_') {
        bool canClose = prevGlyph && !pendingSpace && stack.size() > 1 &&
                        stack.back().marker == (u8)c;
        if (canClose) {
          flushText();
          Frame f = std::move(stack.back());
          stack.pop_back();
          AstNode* s = A.call(c == '*' ? SugarId::strong : SugarId::em, {f.markerPos, i + 1});
          A.setKids(s, f.items);
          stack.back().items.push_back(s);
          prevGlyph = true;
          i++;
          continue;
        }
        bool nextGlyph = (i + 1 < lim) && all[i + 1] != ' ' && all[i + 1] != '\t' &&
                         all[i + 1] != '\r';
        if (nextGlyph) {
          spaceBeforeItem();
          flushText();
          stack.push_back({(u8)c, i});
          prevGlyph = false;
          i++;
          continue;
        }
        put(c, i);
        i++;
        continue;
      }
      if (c == '#') {
        handleSplice(i);
        continue;
      }
      if (c == '^' && i + 1 < lim && all[i + 1] == '[') {
        // footnote sugar (notes-design.md §1): ^[inline body]; brackets
        // nest; an unclosed form stays literal text
        u32 depth = 0, close = i + 1;
        for (; close < lim; close++) {
          if (all[close] == '[') depth++;
          else if (all[close] == ']' && --depth == 0) break;
        }
        if (close < lim && all[close] == ']') {
          flushText();
          AstNode* nt = A.call(SugarId::note, {i, close + 1});
          A.setKids(nt, parseSub({i + 2, close}));
          pushItem(nt);
          i = close + 1;
          continue;
        }
        put(c, i);
        i++;
        continue;
      }
      if (c == '@') {
        // reference sugar (v2 §11.1): literal when preceded by an identifier
        // character (user@domain); bare form ASCII, CJK labels use @[…]
        bool prevIdent = i > 0 && isIdentCont(all[i - 1]);
        u32 tStart = 0, tEnd = 0, end = 0;
        if (!prevIdent) {
          if (i + 1 < lim && all[i + 1] == '[') {
            u32 close = i + 2;
            while (close < lim && all[close] != ']') close++;
            if (close < lim && close > i + 2) { tStart = i + 2; tEnd = close; end = close + 1; }
          } else if (i + 1 < lim && isIdentStart(all[i + 1])) {
            u32 p = i + 1;
            while (p < lim && (isIdentCont(all[p]) || all[p] == '-')) p++;
            tStart = i + 1; tEnd = p; end = p;
          }
        }
        if (tEnd > tStart) {
          spaceBeforeItem();
          flushText();
          AstNode* r = A.call(SugarId::ref, {i, end});
          r->str = strs.intern(all.substr(tStart, tEnd - tStart));
          pushItem(r);
          i = end;
          continue;
        }
        put(c, i);
        i++;
        continue;
      }
      put(c, i);
      i++;
    }
    flushText();
    while (stack.size() > 1) {
      Frame f = std::move(stack.back());
      stack.pop_back();
      AstNode* lit = A.node(AstKind::Text, {f.markerPos, f.markerPos + 1});
      char m = (char)f.marker;
      lit->str = strs.intern(std::string_view(&m, 1));
      auto& parent = stack.back().items;
      parent.push_back(lit);
      for (AstNode* it : f.items) parent.push_back(it);
    }
  }
};

// Top-level segmentation of a region line at unescaped '|' (v2 §4.1):
// code spans and splices (head chains, #(…), content args) are opaque, so
// `a|b` in a code span or #f("a|b") never splits. \| escapes; || is an
// empty cell; leading/trailing empty segments from |-framed lines drop.
static void splitCells(std::string_view all, Span line, std::vector<Span>& cells) {
  std::vector<u32> cuts;
  u32 p = line.start;
  std::string_view clipped = all.substr(0, line.end);
  while (p < line.end) {
    char c = all[p];
    if (c == '\\') { p += 2; continue; }
    if (c == '`') {  // code span is opaque; an unclosed backtick is literal
      u32 q = p + 1;
      while (q < line.end && all[q] != '`') q++;
      p = (q < line.end) ? q + 1 : p + 1;
      continue;
    }
    if (c == '$') {  // math island is opaque: `$|x|$` is one formula (plan P0-04)
      u32 q = p + 1;
      while (q < line.end && all[q] != '$') {
        if (all[q] == '\\') q++;
        q++;
      }
      p = (q < line.end) ? q + 1 : p + 1;
      continue;
    }
    if (c == '#') {
      u32 q = p + 1;
      if (q < line.end && all[q] == '(') {
        JsScan js = scanJs(clipped, q, true);
        p = js.ok ? js.end : line.end;
        continue;
      }
      u32 h = scanSpliceHead(clipped, q);
      if (h > q) {
        p = h;
        while (p < line.end && all[p] == '[') {  // content args
          int depth = 0;
          u32 r = p;
          for (; r < line.end; r++) {
            if (all[r] == '\\') { r++; continue; }
            if (all[r] == '[') depth++;
            else if (all[r] == ']' && --depth == 0) break;
          }
          p = (r < line.end) ? r + 1 : line.end;
        }
        continue;
      }
      p++;
      continue;
    }
    if (c == '|') cuts.push_back(p);
    p++;
  }
  u32 prev = line.start;
  for (u32 cut : cuts) {
    cells.push_back({prev, cut});
    prev = cut + 1;
  }
  cells.push_back({prev, line.end});
  auto blank = [&](Span sp) {
    for (u32 i = sp.start; i < sp.end; i++)
      if (all[i] != ' ' && all[i] != '\t' && all[i] != '\r') return false;
    return true;
  };
  if (cells.size() > 1 && blank(cells.front())) cells.erase(cells.begin());
  if (cells.size() > 1 && blank(cells.back())) cells.pop_back();
}

struct AstBuilder {
  const SourceText& src;
  Arena& arena;
  Interner& strs;
  DiagSink& diags;

  AstAlloc A{arena};

  std::vector<AstNode*> inlineParse(const std::vector<Span>& spans) {
    InlineParser p{src, arena, strs, diags, src.view()};
    p.spans = spans;
    p.run();
    return std::move(p.stack.back().items);
  }

  AstNode* errorNode(Span sp, const char* code, const std::string& msg, bool report = true) {
    if (report) diags.add(Sev::Error, code, sp, msg);
    AstNode* e = A.node<ErrorP>(AstKind::Error, sp);
    e->str = strs.intern(code);
    side<ErrorP>(e).message = strs.intern(msg);
    return e;
  }

  std::vector<AstNode*> buildKids(const SkelNode* s, bool top = false) {
    std::vector<AstNode*> kids;
    kids.reserve(s->kids.size());
    for (const SkelNode* k : s->kids) kids.push_back(build(k, top));
    return kids;
  }

  AstNode* build(const SkelNode* s, bool top = false) {
    switch (s->kind) {
      case SkelKind::Doc: {
        AstNode* d = A.node(AstKind::Doc, s->span);
        A.setKids(d, buildKids(s, /*top=*/true));
        return d;
      }
      case SkelKind::Error:  // reported by the line pass
        return errorNode(s->span, s->errCode, s->errMsg, /*report=*/false);
      case SkelKind::Para: {
        AstNode* p = A.call(SugarId::para, s->span);
        A.setKids(p, inlineParse(s->lineSpans));
        return p;
      }
      case SkelKind::Heading: {
        AstNode* h = A.call<HeadingP>(SugarId::heading, s->span);
        side<HeadingP>(h).level = s->level;
        if (!s->labelSpan.empty()) side<HeadingP>(h).label = strs.intern(src.slice(s->labelSpan));
        A.setKids(h, inlineParse(s->lineSpans));
        return h;
      }
      case SkelKind::List: {
        AstNode* l = A.call<ListP>(SugarId::list, s->span);
        side<ListP>(l) = {s->ordered, s->start};
        A.setKids(l, buildKids(s));
        return l;
      }
      case SkelKind::Item: {
        AstNode* it = A.call(SugarId::item, s->span);
        A.setKids(it, buildKids(s));
        return it;
      }
      case SkelKind::Quote: {
        AstNode* q = A.call(SugarId::quote, s->span);
        A.setKids(q, buildKids(s));
        return q;
      }
      case SkelKind::Fence: {
        // info string "tag(args)": args reuse the splice argument lexer and
        // compile to a JS object literal for the fence dispatcher (v2 §4.1)
        Span tagSpan = s->langSpan, args;
        u32 lp = tagSpan.start;
        std::string_view all = src.view();
        while (lp < tagSpan.end && all[lp] != '(') lp++;
        if (lp < tagSpan.end) {
          JsScan js = scanJs(all.substr(0, tagSpan.end), lp, true);
          if (js.ok) {
            args = {lp + 1, js.end - 1};
            tagSpan.end = lp;
          }
        }
        if (!args.empty() && !jsNamedArgList(src.slice(args)))
          return errorNode(s->span, "header-positional",
                           "fence arguments must be named (key: value)");
        AstNode* f = A.call<FenceP>(SugarId::fence, s->span);
        std::string lang(src.slice(tagSpan));
        while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\r')) lang.pop_back();
        FenceP& fp = side<FenceP>(f);
        fp.lang = strs.intern(lang);
        fp.args = args;
        fp.bodyOffset = s->lineSpans.empty() ? s->span.end : s->lineSpans[0].start;
        std::string body;
        for (size_t k = 0; k < s->lineSpans.size(); k++) {
          if (k) body += '\n';
          body += src.slice(s->lineSpans[k]);
        }
        f->str = strs.intern(body);
        return f;
      }
      case SkelKind::Rule:
        return A.call(SugarId::rule, s->span);
      case SkelKind::Comment: {
        AstNode* c = A.node(AstKind::Comment, s->span);
        std::string body = crlfToLf(src.slice(s->inner));
        c->str = strs.intern(body);
        return c;
      }
      case SkelKind::Region: {
        if (!s->inner.empty() && !jsNamedArgList(src.slice(s->inner)))
          return errorNode(s->span, "header-positional",
                           "region arguments must be named (key: value)");
        AstNode* r = A.call<RegionP>(SugarId::region, s->span);
        r->str = strs.intern(src.slice(s->langSpan));
        side<RegionP>(r).args = s->inner;  // opener args (inside parens; empty span = none)
        std::vector<AstNode*> kids;
        for (const SkelNode* k : s->kids) {
          if (k->kind == SkelKind::Para) {
            // line provenance (v2 §4.1): each source line is a row whose
            // cells are the top-level '|' segmentation, inline-parsed
            AstNode* p = A.call(SugarId::para, k->span);
            std::vector<AstNode*> rows;
            for (const Span& line : k->lineSpans) {
              AstNode* row = A.call(SugarId::row, line);
              std::vector<Span> cells;
              splitCells(src.view(), line, cells);
              std::vector<AstNode*> cellNodes;
              for (const Span& c : cells) {
                AstNode* cell = A.call(SugarId::cell, c);
                A.setKids(cell, inlineParse({c}));
                cellNodes.push_back(cell);
              }
              A.setKids(row, cellNodes);
              rows.push_back(row);
            }
            A.setKids(p, rows);
            kids.push_back(p);
          } else {
            kids.push_back(build(k));
          }
        }
        A.setKids(r, kids);
        return r;
      }
      case SkelKind::CodeLet:
      case SkelKind::CodeBlock: {
        if (!top)  // was silently dropped (codegen text("")); P2-12 runs them
          return errorNode(s->span, "statement-nested-unsupported",
                           "statements inside lists, quotes and regions are not "
                           "supported yet (plan P2-12)");
        std::string_view reserved =
            jsReservedBinding(src.slice(s->inner), s->kind == SkelKind::CodeLet);
        if (!reserved.empty())
          return errorNode(s->span, "reserved-name",
                           "'" + std::string(reserved) +
                               "': names starting with __ are reserved for the engine");
        AstNode* c = A.node<StmtP>(AstKind::Stmt, s->span);
        side<StmtP>(c) = {s->kind == SkelKind::CodeLet, s->inner};
        return c;
      }
    }
    return A.node(AstKind::Doc, s->span);
  }
};

}  // namespace

AstNode* parseDoc(const SourceText& src, const Skeleton& sk, Arena& arena,
                  Interner& strs, DiagSink& diags) {
  AstBuilder b{src, arena, strs, diags};
  return b.build(sk.root);
}

std::vector<AstNode*> parseInlineSpans(const SourceText& src,
                                       const std::vector<Span>& spans,
                                       Arena& arena, Interner& strs,
                                       DiagSink& diags) {
  InlineParser p{src, arena, strs, diags, src.view()};
  p.spans = spans;
  p.run();
  return std::move(p.stack.back().items);
}

static void dumpNode(std::string& out, const AstNode* n, const SourceText& src,
                     const Interner& strs, int depth) {
  for (int i = 0; i < depth; i++) out += "  ";
  dumpAstNode(out, n, src, strs);
  out += "\n";
  for (const AstNode* k : n->kids()) dumpNode(out, k, src, strs, depth + 1);
}

std::string dumpAst(const AstNode* doc, const SourceText& src, const Interner& strs) {
  std::string out;
  dumpNode(out, doc, src, strs, 0);
  return out;
}

}  // namespace tsr
