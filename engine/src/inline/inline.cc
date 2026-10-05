// Inline parser (plan P1-06; design T1 SurfaceLexer, phase 2): text,
// strict-pair emphasis, escapes, splices with content arguments, links, code
// spans, math islands, inline comments, notes and references over a leaf's
// joined text (syntax/cursor.h). Dispatch is the INLINE rows' opener table
// (syntax.gen.h); atoms and bracket bodies come from the shared lexer
// primitives (syntax/lexer.h), so no scan leaves its leaf or its body.
#include "../ast/ast.h"
#include "../syntax/cursor.h"
#include "../syntax/lexer.h"
#include "jslex.h"

namespace tsr {

namespace {

struct Frame {
  u8 marker;  // 0 for root
  u32 markerPos = 0;
  std::vector<AstNode*> items;
};

// What the line pass decided for a paragraph (plan P1-08): block-form content
// bodies close at their ']' line, and reverted openers are literal text.
struct LeafHints {
  const std::vector<Span>* bodies = nullptr;
  const std::vector<u32>* literal = nullptr;
};

// A content body re-enters the line pass (Blocks mode): defined below.
std::vector<AstNode*> parseBlocks(const SourceText& src, const std::vector<Span>& lines, Arena& arena,
                                  Interner& strs, DiagSink& diags);

// One parse of the range [from, to) of a leaf's text. Content bodies (link
// text, content arguments, notes) are sub-parses bounded by their closer.
struct InlineParser {
  const SourceText& src;
  const LeafText& L;
  const LeafHints hints;
  const u32 from, to;
  Arena& arena;
  Interner& strs;
  DiagSink& diags;
  const std::string_view t;  // the leaf's text up to `to`: every scan stops there
  BracketMatcher brackets;
  AstAlloc A;

  std::vector<Frame> stack;
  std::string buf;
  u32 bufStart = 0, bufEnd = 0;  // view offsets
  bool pendingSpace = false;
  bool prevGlyph = false;
  u32 i = 0;

  InlineParser(const SourceText& sr, const LeafText& leaf, LeafHints h, u32 a, u32 b, Arena& ar,
               Interner& st, DiagSink& dg)
      : src(sr), L(leaf), hints(h), from(a), to(b), arena(ar), strs(st), diags(dg),
        t(leaf.text().substr(0, b)), brackets(t), A{ar} {}

  Span span(u32 a, u32 b) const { return L.rawSpan(a, b); }

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
    AstNode* n = A.node(AstKind::Text, span(bufStart, bufEnd));
    n->str = strs.intern(buf);
    stack.back().items.push_back(n);
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

  // put(t[k], k) for every k in [a, b): a run of plain text
  void putRun(u32 a, u32 b) {
    put(t[a], a);
    buf.append(t.data() + a + 1, b - a - 1);
    bufEnd = b;
  }

  void pushItem(AstNode* n) {
    stack.back().items.push_back(n);
    prevGlyph = true;
  }

  std::vector<AstNode*> parseSub(u32 a, u32 b) {
    InlineParser p(src, L, hints, a, b, arena, strs, diags);
    p.run();
    return std::move(p.stack.back().items);
  }

  // an opener the line pass reverted to literal text
  bool reverted(u32 at) const {
    if (!hints.literal || hints.literal->empty()) return false;
    u32 r = L.raw(at);
    for (u32 x : *hints.literal)
      if (x == r) return true;
    return false;
  }
  // a content body's closer: the line pass's for a block-form body, else the
  // bracket counter's; -1 when there is none
  i32 bodyClose(u32 open) {
    if (reverted(open)) return -1;
    if (hints.bodies) {
      u32 r = L.raw(open);
      for (const Span& b : *hints.bodies)
        if (b.start == r) {
          u32 v = L.view(b.end);
          return v < to ? (i32)v : -1;
        }
    }
    return brackets.body(open);
  }
  // A content body (plan P1-08): its lines re-enter the line pass after the
  // common indentation is stripped (App B rule 4); a body that is one
  // paragraph unwraps to its inline content.
  std::vector<AstNode*> parseBody(u32 a, u32 b) {
    std::vector<Span> lines;
    for (u32 s0 = a; s0 <= b;) {
      u32 e = s0;
      while (e < b && t[e] != '\n') e++;
      lines.push_back({L.raw(s0), L.raw(e)});
      s0 = e + 1;
    }
    const std::string_view all = src.view();
    auto blankLine = [&](Span sp) {
      for (u32 k = sp.start; k < sp.end; k++)
        if (all[k] != ' ' && all[k] != '\t' && all[k] != '\r') return false;
      return true;
    };
    auto indentOf = [&](Span sp, u32 limit, u32* pos) {
      u32 col = 0, p = sp.start;
      while (p < sp.end && (all[p] == ' ' || all[p] == '\t') && col < limit) {
        col = all[p] == '\t' ? (col / 4 + 1) * 4 : col + 1;
        p++;
      }
      if (pos) *pos = p;
      return col;
    };
    u32 common = ~0u;
    for (size_t k = 1; k < lines.size(); k++)
      if (!blankLine(lines[k])) common = std::min(common, indentOf(lines[k], ~0u, nullptr));
    if (common != ~0u && common > 0)
      for (size_t k = 1; k < lines.size(); k++) {
        u32 p;
        indentOf(lines[k], common, &p);
        lines[k].start = p;
      }
    std::vector<AstNode*> kids = parseBlocks(src, lines, arena, strs, diags);
    if (kids.size() == 1 && kids[0]->isCall(SugarId::para)) {
      std::span<AstNode* const> inl = kids[0]->kids();
      return {inl.begin(), inl.end()};
    }
    return kids;
  }

  // A line join: a soft space — except between two CJK-class codepoints,
  // which join seamlessly (clreq).
  void join() {
    auto cjkish = [](u32 cp) { return isCjk(cp) || cp == 0x2014 || cp == 0x2026; };
    u32 next = i + 1;
    bool cjkJoin = next < t.size() && cjkish(utf8PrevCp(t, i)) && cjkish(utf8Next(t, next));
    if (!cjkJoin) {
      pendingSpace = true;
      prevGlyph = false;
    }
    i++;
  }

  // inline comment: invisible to the text around it (spacing state unchanged)
  void comment() {
    u32 end;
    if (reverted(i) || !lexComment(t, i, end)) {
      // unclosed in its leaf: literal text, the error stays visible
      diags.add(Sev::Error, "parse-inline", span(i, i + 3), "unterminated comment");
      put('%', i);
      put('-', i + 1);
      put('-', i + 2);
      i += 3;
      return;
    }
    flushText();
    AstNode* cm = A.node(AstKind::Comment, span(i, end));
    cm->str = strs.intern(t.substr(i + 3, end - 3 - (i + 3)));
    stack.back().items.push_back(cm);  // does not set prevGlyph
    i = end;
  }

  // math island (v2 §5): verbatim to the closing '$' within the leaf; '\$'
  // is the one escape it decodes
  void math() {
    u32 close;
    if (reverted(i) || !lexMath(t, i, close)) {
      put('$', i);
      i++;
      return;
    }
    spaceBeforeItem();
    flushText();
    std::string body = mathText(t.substr(i + 1, close - (i + 1)));
    // $ x $ (whitespace inside both fences) is display math (Typst rule)
    auto isWs = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
    bool display = body.size() >= 2 && isWs(body.front()) && isWs(body.back());
    size_t b0 = 0, b1 = body.size();
    while (b0 < b1 && isWs(body[b0])) b0++;
    while (b1 > b0 && isWs(body[b1 - 1])) b1--;
    AstNode* mn = A.call<MathP>(SugarId::math, span(i, close + 1));
    mn->str = strs.intern(std::string_view(body).substr(b0, b1 - b0));
    side<MathP>(mn).display = display;
    pushItem(mn);
    // equation label: ` <id>` directly after the closing $ (v2 §11.1
    // heading-label form); labelled display formulas get numbers
    u32 after = close + 1;
    if (after + 1 < to && t[after] == ' ' && t[after + 1] == '<') {
      u32 lb = after + 2, le = lb;
      while (le < to && t[le] != '>' && t[le] != '<' && t[le] != '\n') le++;
      if (le < to && t[le] == '>' && le > lb) {
        side<MathP>(mn).label = strs.intern(t.substr(lb, le - lb));
        mn->span.end = L.raw(le + 1);
        i = le + 1;
        return;
      }
    }
    i = after;
  }

  // code span: a run of N backticks to the next run of exactly N
  void code() {
    CodeSpanLex cs;
    if (!lexCodeSpan(t, i, cs) || reverted(i)) {  // an unclosed run is literal as a whole
      for (u32 k = 0; k < cs.run; k++) put('`', i + k);
      i += cs.run;
      return;
    }
    spaceBeforeItem();
    flushText();
    AstNode* n = A.call(SugarId::code, span(i, cs.end));
    n->str = strs.intern(codeSpanText(t.substr(cs.bodyStart, cs.bodyEnd - cs.bodyStart)));
    pushItem(n);
    i = cs.end;
  }

  // `(url)` right after a link's ']': a plain paren match on its line ('//'
  // in https:// is not a comment here)
  bool url(i32 close, u32& pclose) const {
    u32 p = (u32)close + 1;
    if (p >= to || t[p] != '(') return false;
    int depth = 0;
    for (; p < to && t[p] != '\n'; p++) {
      char c = t[p];
      if (c == '\\' && p + 1 < to && t[p + 1] != '\n') {
        p++;
        continue;
      }
      if (c == '(') depth++;
      else if (c == ')' && --depth == 0) {
        pclose = p;
        return true;
      }
    }
    return false;
  }

  // [text](url): the link text closes at its island-aware match, or — when
  // an island would swallow that closer — at its plain match, which then
  // bounds the islands inside it
  void link() {
    u32 pclose = 0;
    i32 close = brackets.match(i, BracketMatcher::IslandAware);
    bool ok = close >= 0 && url(close, pclose);
    if (!ok) {
      close = brackets.match(i, BracketMatcher::Plain);
      ok = close >= 0 && url(close, pclose);
    }
    if (!ok) {
      put('[', i);
      i++;
      return;
    }
    spaceBeforeItem();
    flushText();
    AstNode* n = A.call<LinkP>(SugarId::link, span(i, pclose + 1));
    A.setKids(n, parseSub(i + 1, (u32)close));
    side<LinkP>(n).url = strs.intern(t.substr(close + 2, pclose - (close + 2)));
    pushItem(n);
    i = pclose + 1;
  }

  // strict pairs: an opener needs a glyph after it, a closer a glyph before it
  void pair(char c) {
    bool canClose = prevGlyph && !pendingSpace && stack.size() > 1 && stack.back().marker == (u8)c;
    if (canClose) {
      flushText();
      Frame f = std::move(stack.back());
      stack.pop_back();
      AstNode* n = A.call(c == '*' ? SugarId::strong : SugarId::em, span(f.markerPos, i + 1));
      A.setKids(n, f.items);
      stack.back().items.push_back(n);
      prevGlyph = true;
      i++;
      return;
    }
    bool nextGlyph = i + 1 < to && t[i + 1] != ' ' && t[i + 1] != '\t' && t[i + 1] != '\r' &&
                     t[i + 1] != '\n';
    if (nextGlyph) {
      spaceBeforeItem();
      flushText();
      stack.push_back({(u8)c, i});
      prevGlyph = false;
      i++;
      return;
    }
    put(c, i);
    i++;
  }

  // #head.chain(args)[content]…; or #(expr)[content]…
  void splice() {
    const u32 hash = i;
    SpliceLex s;
    if (!lexSplice(t, hash, s) || (s.paren && reverted(hash + 1))) {
      if (s.paren) diags.add(Sev::Error, "splice-js", span(hash, s.jsEnd), "unbalanced #(...)");
      put('#', hash);
      i = hash + 1;
      return;
    }
    const u32 exprStart = hash + 1, exprEnd = s.end;
    spaceBeforeItem();
    flushText();
    // a keyword as a bare head (#if, #for, #new …) would paste invalid JS
    // and fail the whole document: it becomes an error node (plan P0-05)
    if (!s.paren) {
      u32 h = exprStart;
      while (h < exprEnd && isSpliceCont(t[h])) h++;
      std::string_view head = t.substr(exprStart, h - exprStart);
      if (const char* code = reservedSpliceHead(head)) {
        u32 after = exprEnd;
        while (after < to && t[after] == '[') {
          i32 close = bodyClose(after);
          if (close < 0) break;
          after = (u32)close + 1;
        }
        std::string msg = std::string(code) == "keyword-unsupported"
            ? "#" + std::string(head) + " is not supported yet (keyword forms: plan P2-12)"
            : "'" + std::string(head) + "' is a reserved word and cannot start a splice";
        diags.add(Sev::Error, code, span(hash, after), msg);
        AstNode* e = A.node<ErrorP>(AstKind::Error, span(hash, after));
        e->str = strs.intern(code);
        side<ErrorP>(e).message = strs.intern(msg);
        pushItem(e);
        i = after;
        return;
      }
    }
    AstNode* spl = A.node<SpliceP>(AstKind::Splice, span(hash, exprEnd));
    side<SpliceP>(spl).expr = strs.intern(t.substr(exprStart, exprEnd - exprStart));
    side<SpliceP>(spl).lastCall = s.lastCall ? s.lastCall - exprStart : 0;
    // content arguments: directly adjacent [ … ], repeatable
    std::vector<AstNode*> args;
    u32 after = exprEnd;
    while (after < to && t[after] == '[') {
      i32 close = bodyClose(after);
      if (close < 0) {
        diags.add(Sev::Error, "parse-inline", span(after, to), "unclosed content argument");
        break;
      }
      AstNode* arg = A.call(SugarId::arg, span(after + 1, (u32)close));
      A.setKids(arg, parseBody(after + 1, (u32)close));
      args.push_back(arg);
      after = (u32)close + 1;
    }
    A.setKids(spl, args);
    if (after < to && t[after] == ';') after++;  // hard terminator
    spl->span.end = L.raw(after);
    pushItem(spl);
    i = after;
  }

  // footnote sugar (notes-design.md §1): ^[inline body]; an unclosed form
  // stays literal text. A space before it moves after the note (until P4-07).
  void note() {
    i32 close = bodyClose(i + 1);
    if (close < 0) {
      put('^', i);
      i++;
      return;
    }
    flushText();
    AstNode* n = A.call(SugarId::note, span(i, (u32)close + 1));
    A.setKids(n, parseBody(i + 2, (u32)close));
    pushItem(n);
    i = (u32)close + 1;
  }

  // @[k1, k2] on one line: ids separated by ',' (a backslash escapes ']'),
  // trimmed; target = the ids joined by ", "
  bool idList(u32 p, std::string& target, u32& end) const {
    std::vector<std::string> ids(1);
    for (; p < to && t[p] != '\n'; p++) {
      char c = t[p];
      if (c == '\\' && p + 1 < to && t[p + 1] != '\n') {
        ids.back() += t[++p];
        continue;
      }
      if (c == ']') break;
      if (c == ',') ids.emplace_back();
      else ids.back() += c;
    }
    if (p >= to || t[p] != ']') return false;
    for (std::string& id : ids) {
      size_t a = 0, b = id.size();
      while (a < b && (id[a] == ' ' || id[a] == '\t')) a++;
      while (b > a && (id[b - 1] == ' ' || id[b - 1] == '\t')) b--;
      if (b == a) continue;
      if (!target.empty()) target += ", ";
      target.append(id, a, b - a);
    }
    end = p + 1;
    return !target.empty();
  }

  // reference sugar (v2 §11.1): literal when preceded by an identifier
  // character (user@domain); bare form ASCII, CJK labels use @[…]
  void ref(InlineRule rule) {
    bool prevIdent = i > 0 && isSpliceCont(t[i - 1]);
    std::string target;
    u32 end = 0;
    if (!prevIdent) {
      if (rule == InlineRule::refs) {
        if (!idList(i + 2, target, end)) target.clear();
      } else if (i + 1 < to && isSpliceHead(t[i + 1])) {
        u32 p = i + 1;
        while (p < to && (isSpliceCont(t[p]) || t[p] == '-')) p++;
        target = t.substr(i + 1, p - (i + 1));
        end = p;
      }
    }
    if (target.empty()) {
      put('@', i);
      i++;
      return;
    }
    spaceBeforeItem();
    flushText();
    AstNode* n = A.call(SugarId::ref, span(i, end));
    n->str = strs.intern(target);
    pushItem(n);
    i = end;
  }

  void run() {
    stack.push_back({0});
    i = from;
    // bytes the loop must look at: rule openers, blanks, joins, escapes
    auto special = [](char ch) {
      return kInlineOpenerByte[(u8)ch] || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
             ch == '\\';
    };
    while (i < to) {
      const char c = t[i];
      if (!special(c)) {
        u32 j = i + 1;
        while (j < to && !special(t[j])) j++;
        putRun(i, j);
        i = j;
        continue;
      }
      if (c == '\n') {
        join();
        continue;
      }
      if (c == ' ' || c == '\t' || c == '\r') {
        pendingSpace = true;
        prevGlyph = false;
        i++;
        continue;
      }
      if (c == '\\' && i + 1 < to && t[i + 1] != '\n') {
        put(t[i + 1], i, 2);
        i += 2;
        continue;
      }
      const InlineRule rule = inlineOpener(t, i);
      switch (rule) {
        case InlineRule::comment: comment(); continue;
        case InlineRule::math: math(); continue;
        case InlineRule::code: code(); continue;
        case InlineRule::link: link(); continue;
        case InlineRule::strong:
        case InlineRule::em: pair(c); continue;
        case InlineRule::splice: splice(); continue;
        case InlineRule::note: note(); continue;
        case InlineRule::ref:
        case InlineRule::refs: ref(rule); continue;
        case InlineRule::none: break;
      }
      put(c, i);
      i++;
    }
    flushText();
    while (stack.size() > 1) {
      Frame f = std::move(stack.back());
      stack.pop_back();
      AstNode* lit = A.node(AstKind::Text, span(f.markerPos, f.markerPos + 1));
      char m = (char)f.marker;
      lit->str = strs.intern(std::string_view(&m, 1));
      auto& parent = stack.back().items;
      parent.push_back(lit);
      for (AstNode* it : f.items) parent.push_back(it);
    }
  }
};

std::vector<AstNode*> parseLeaf(const SourceText& src, const std::vector<Span>& spans, Arena& arena,
                                Interner& strs, DiagSink& diags, LeafHints hints = {}) {
  LeafText L(src.view(), spans);
  InlineParser p(src, L, hints, 0, L.size(), arena, strs, diags);
  p.run();
  return std::move(p.stack.back().items);
}

// Top-level segmentation of a region line at unescaped '|' (v2 §4.1): atoms
// (escapes, code spans, math islands, comments, splices and their content
// arguments) are opaque, so `a|b` in a code span, `$|x|$` or #f("a|b")[c|d]
// never splits. '||' is an empty cell; leading and trailing empty segments
// of a '|'-framed line drop.
static void splitCells(std::string_view all, Span line, std::vector<Span>& cells) {
  LeafText L(all, {line});
  const std::string_view t = L.text();
  const u32 n = (u32)t.size();
  BracketMatcher brackets(t);
  std::vector<u32> cuts;
  for (u32 p = 0; p < n;) {
    u32 e = atomEnd(t, p);
    if (e > p) {
      bool spliced = t[p] == '#';
      p = e;
      while (spliced && p < n && t[p] == '[') {  // content arguments
        i32 close = brackets.body(p);
        if (close < 0) break;
        p = (u32)close + 1;
      }
      continue;
    }
    if (t[p] == '`') {  // an unclosed backtick run is literal
      while (p < n && t[p] == '`') p++;
      continue;
    }
    if (t[p] == '|') cuts.push_back(line.start + p);
    p++;
  }
  u32 prev = line.start;
  for (u32 cut : cuts) {
    cells.push_back({prev, cut});
    prev = cut + 1;
  }
  cells.push_back({prev, line.end});
  auto blank = [&](Span sp) {
    for (u32 k = sp.start; k < sp.end; k++)
      if (all[k] != ' ' && all[k] != '\t' && all[k] != '\r') return false;
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

  // a verbatim body: its line slices (container prefixes stripped) joined
  std::string joinLines(const std::vector<Span>& lines) const {
    std::string body;
    for (size_t k = 0; k < lines.size(); k++) {
      if (k) body += '\n';
      body += src.slice(lines[k]);
    }
    return body;
  }

  std::vector<AstNode*> inlineParse(const std::vector<Span>& spans, LeafHints hints = {}) {
    return parseLeaf(src, spans, arena, strs, diags, hints);
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
        A.setKids(p, inlineParse(s->lineSpans, {&s->bodies, &s->literalAt}));
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
        // inside a quote or list item the body lines are not contiguous in
        // the source: each line's offset goes to the handler
        if (s->contained) {
          std::string offs = "[";
          for (size_t k = 0; k < s->lineSpans.size(); k++)
            appendf(offs, "%s%u", k ? "," : "", s->lineSpans[k].start);
          fp.lines = strs.intern(offs + "]");
        }
        f->str = strs.intern(joinLines(s->lineSpans));
        return f;
      }
      case SkelKind::Rule:
        return A.call(SugarId::rule, s->span);
      case SkelKind::Comment: {
        AstNode* c = A.node(AstKind::Comment, s->span);
        c->str = strs.intern(joinLines(s->lineSpans));
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

std::vector<AstNode*> parseBlocks(const SourceText& src, const std::vector<Span>& lines, Arena& arena,
                                  Interner& strs, DiagSink& diags) {
  Skeleton sk = linepassLines(src, lines, arena, diags);
  AstBuilder b{src, arena, strs, diags};
  std::vector<AstNode*> kids;
  for (const SkelNode* k : sk.root->kids) kids.push_back(b.build(k));
  return kids;
}

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
  return parseLeaf(src, spans, arena, strs, diags);
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
