// Inline parser (plan P1-06; design T1 SurfaceLexer, phase 2): text,
// strict-pair emphasis, escapes, splices with content arguments, links, code
// spans, math islands, inline comments, notes and references over a leaf's
// joined text (syntax/cursor.h). Dispatch is the INLINE rows' opener table
// (syntax.gen.h); atoms and bracket bodies come from the shared lexer
// primitives (syntax/lexer.h), so no scan leaves its leaf or its body.
#include "../ast/ast.h"

#include <algorithm>

#include "../syntax/cursor.h"
#include "../syntax/lexer.h"
#include "jslex.h"
#include "../syntax/labels.h"

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
  // (plan P2-11) a region interior's paragraph: its top-level unescaped '|'
  // are cell cuts (D-L05), recorded on their Text nodes (TextP seps)
  bool cells = false;
};

// A content body re-enters the line pass (Blocks mode): defined below.
std::vector<AstNode*> parseBlocks(const SourceText& src, const std::vector<Span>& lines, Arena& arena,
                                  Interner& strs, DiagSink& diags, u32 depth);

// A content body's lines (the first starts after its '['): the common
// indentation of the lines after the first stripped (App B rule 4), parsed
// as blocks; a body that is one paragraph unwraps to its inline content
// (plan P1-08; content literals, plan P2-12), `depth` levels deep
std::vector<AstNode*> parseContentLines(const SourceText& src, std::vector<Span> lines, Arena& arena,
                                        Interner& strs, DiagSink& diags, u32 depth) {
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
  std::vector<AstNode*> kids = parseBlocks(src, lines, arena, strs, diags, depth);
  // one paragraph — statements aside, which are no content (plan P2-12) —
  // is its inline content, the statements where they stand
  size_t paras = 0, other = 0;
  for (const AstNode* k : kids) {
    if (k->isCall(SugarId::para)) paras++;
    else if (k->kind != AstKind::Stmt) other++;
  }
  if (paras == 1 && other == 0) {
    std::vector<AstNode*> out;
    for (AstNode* k : kids) {
      if (k->kind == AstKind::Stmt) {
        out.push_back(k);
        continue;
      }
      std::span<AstNode* const> inl = k->kids();
      out.insert(out.end(), inl.begin(), inl.end());
    }
    return out;
  }
  return kids;
}

// One parse of the range [from, to) of a leaf's text. Content bodies (link
// text, content arguments, notes) are sub-parses bounded by their closer.
struct InlineParser {
  const SourceText& src;
  const LeafText& L;
  const LeafHints hints;
  const u32 from, to;
  const u32 depth;  // the levels above its items (kMaxNesting: linepass.h)
  Arena& arena;
  Interner& strs;
  DiagSink& diags;
  const std::string_view t;  // the leaf's text up to `to`: every scan stops there
  BracketMatcher brackets;
  AstAlloc A;

  std::vector<Frame> stack;
  std::string buf;
  u32 bufStart = 0, bufEnd = 0;  // view offsets
  // the text's cooked→raw map (plan P2-04; design T1 TextRaw): breakpoints
  // (cooked offset, raw offset), identity between them — escapes, collapsed
  // blanks, line joins and container prefixes break it; empty when the
  // text is its own raw slice
  std::vector<u32> rmap;
  u32 rmC = 0, rmR = 0;  // the last breakpoint
  std::vector<u32> seps;  // the cell cuts in buf (cooked offsets; hints.cells)
  bool pendingSpace = false;
  // a line join is a soft break (plan P2-10): U+000A in the cooked text,
  // absorbing the blanks around it; the engine decides how it joins (a
  // space, or nothing between two wide characters) — the parser no longer
  bool pendingBreak = false;
  bool prevGlyph = false;
  bool inLink = false;  // (plan P3-33) a link's text: a URL in it is text, not a link of its own
  u32 i = 0;

  InlineParser(const SourceText& sr, const LeafText& leaf, LeafHints h, u32 a, u32 b, u32 d, Arena& ar,
               Interner& st, DiagSink& dg)
      : src(sr), L(leaf), hints(h), from(a), to(b), depth(d), arena(ar), strs(st), diags(dg),
        t(leaf.text().substr(0, b)), brackets(t), A{ar} {}

  Span span(u32 a, u32 b) const { return L.rawSpan(a, b); }

  // the next cooked byte (at buf.size()) comes from raw offset `raw`
  void mark(u32 raw) {
    const u32 c = (u32)buf.size();
    if (rmap.empty() || raw != rmR + (c - rmC)) {
      rmap.push_back(c);
      rmap.push_back(raw);
      rmC = c;
      rmR = raw;
    }
  }

  void put(char c, u32 pos, u32 len = 1) {
    if (buf.empty()) bufStart = pos;
    if (pendingSpace || pendingBreak) {
      // a space for the blanks before pos, a soft break for a line join: it
      // maps to where they begin (the previous byte's end), or to nothing
      // before the text's first byte
      if (!buf.empty() || !stack.back().items.empty()) {
        mark(buf.empty() ? L.raw(pos) : L.raw(bufEnd));
        buf += pendingBreak ? '\n' : ' ';
      }
      pendingSpace = pendingBreak = false;
    }
    mark(L.raw(pos));
    buf += c;
    bufEnd = pos + len;
    prevGlyph = true;
  }

  void flushText() {
    if (buf.empty()) return;
    const Span sp = span(bufStart, bufEnd);
    AstNode* n = A.node<TextP>(AstKind::Text, sp);
    n->str = strs.intern(buf);
    const u32 c = (u32)buf.size();
    if (rmR + (c - rmC) != sp.end) {  // the end, unless identity reaches it
      rmap.push_back(c);
      rmap.push_back(sp.end);
    }
    if (!(rmap.size() == 2 && rmap[1] == sp.start && c == sp.end - sp.start)) {
      std::string m;  // "cooked:raw" pairs, raw relative to the span start
      for (size_t k = 0; k < rmap.size(); k += 2)
        appendf(m, "%s%u:%u", k ? "," : "", rmap[k], rmap[k + 1] - sp.start);
      side<TextP>(n).rawmap = strs.intern(m);
    }
    if (!seps.empty()) {  // "o,o,…": the cooked offsets of its cell cuts
      std::string m;
      for (size_t k = 0; k < seps.size(); k++) {
        if (k) m += ',';
        m += std::to_string(seps[k]);
      }
      side<TextP>(n).seps = strs.intern(m);
    }
    stack.back().items.push_back(n);
    buf.clear();
    rmap.clear();
    seps.clear();
  }

  void spaceBeforeItem() {
    if (pendingSpace || pendingBreak) {
      if (!buf.empty() || !stack.back().items.empty()) {
        if (buf.empty()) bufStart = bufEnd;
        mark(L.raw(bufEnd));
        buf += pendingBreak ? '\n' : ' ';
      }
      pendingSpace = pendingBreak = false;
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

  // the levels above a body opened here: its call's, inside the open pairs
  u32 nest() const { return depth + (u32)stack.size(); }
  bool tooDeep() const { return nest() >= kMaxNesting; }
  bool nestSaid = false;
  // a body past kMaxNesting: one error node in its place, its text unread
  std::vector<AstNode*> nestCut(u32 a, u32 b) {
    const std::string msg = "content nests more than " + std::to_string(kMaxNesting) + " deep: this body is cut";
    diags.add(Sev::Error, "nest-limit", span(a, b), msg);
    AstNode* e = A.node<ErrorP>(AstKind::Error, span(a, b));
    e->str = strs.intern("nest-limit");
    side<ErrorP>(e).message = strs.intern(msg);
    return {e};
  }

  std::vector<AstNode*> parseSub(u32 a, u32 b, bool linkText = false) {
    if (tooDeep()) return nestCut(a, b);
    InlineParser p(src, L, hints, a, b, nest(), arena, strs, diags);
    p.inLink = inLink || linkText;
    p.run();
    return std::move(p.stack.back().items);
  }

  // an opener the line pass reverted to literal text
  bool reverted(u32 at) const {
    if (!hints.literal || hints.literal->empty()) return false;
    return std::binary_search(hints.literal->begin(), hints.literal->end(), L.raw(at));  // ascending
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
    if (tooDeep()) return nestCut(a, b);
    std::vector<Span> lines;
    for (u32 s0 = a; s0 <= b;) {
      u32 e = s0;
      while (e < b && t[e] != '\n') e++;
      lines.push_back({L.raw(s0), L.raw(e)});
      s0 = e + 1;
    }
    return parseContentLines(src, std::move(lines), arena, strs, diags, nest());
  }

  // A line join: a soft break (plan P2-10; U+000A in the text, resolved at
  // instantiation — model/softbreak.h)
  void join() {
    pendingBreak = true;
    prevGlyph = false;
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
    AstNode* cm = A.node<CommentP>(AstKind::Comment, span(i, end));
    cm->str = strs.intern(t.substr(i + 3, end - 3 - (i + 3)));
    stack.back().items.push_back(cm);  // does not set prevGlyph
    i = end;
  }

  // A formula's pieces (plan P2-15; design T8 MathValue; D-L13): one
  // fragment per line of [a, b) — its text as written (the math lexer
  // decodes \$ and \#), at its own source span — and its holes: #ident
  // (letters then letters or digits; a `.`, `(`, `[` or `;` after it is
  // formula text) and #(expr), each a splice. A `#` that starts neither is
  // itself (info math-hash); one in quoted text is text.
  std::vector<AstNode*> mathPieces(u32 a, u32 b) {
    auto isWs = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
    while (a < b && isWs(t[a])) a++;
    while (b > a && isWs(t[b - 1])) b--;
    std::vector<AstNode*> out;
    auto fragment = [&](u32 x, u32 y) {
      if (y <= x) return;
      AstNode* f = A.node<TextP>(AstKind::Text, span(x, y));
      f->str = strs.intern(t.substr(x, y - x));
      out.push_back(f);
    };
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    u32 fs = a;
    for (u32 p = a; p < b;) {
      const char c = t[p];
      if (c == '\n') {
        fragment(fs, p);
        fs = ++p;
        continue;
      }
      if (c == '\\' && !(p + 1 < b && t[p + 1] == '\n')) {  // (a `\` ending its line is a row break, plan P3-29)
        p += 2;
        continue;
      }
      if (c == '"') {  // quoted text
        p++;
        while (p < b && t[p] != '"' && t[p] != '\n') p++;
        if (p < b && t[p] == '"') p++;
        continue;
      }
      if (c != '#') {
        p++;
        continue;
      }
      u32 e = p + 1;
      std::string expr;
      if (e < b && letter(t[e])) {
        while (e < b && (letter(t[e]) || (t[e] >= '0' && t[e] <= '9'))) e++;
        expr = std::string(t.substr(p + 1, e - (p + 1)));
      } else if (e < b && t[e] == '(') {
        const JsScan js = scanJs(t.substr(0, b), e, true);
        if (js.ok) {
          e = js.end;
          expr = std::string(t.substr(p + 1, e - (p + 1)));
        }
      }
      if (expr.empty()) {
        diags.add(Sev::Info, "math-hash", span(p, p + 1),
                  "a formula's # starts a hole (#name, #(expr)): this one is a literal (write \\#)");
        p++;
        continue;
      }
      fragment(fs, p);
      AstNode* h = A.node<SpliceP>(AstKind::Splice, span(p, e));
      side<SpliceP>(h).expr = strs.intern(expr);
      out.push_back(h);
      p = fs = e;
    }
    fragment(fs, b);
    return out;
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
    A.setKids(mn, mathPieces(i + 1, close));
    pushItem(mn);
    // a label: ` <id>` directly after the closing $ (the one label grammar,
    // plan P2-06); a labelled display formula is numbered, an inline one has
    // no anchor yet (label-orphan)
    u32 after = close + 1;
    if (after + 1 < to && t[after] == ' ') {
      if (u32 le = lexLabel(t, after + 1, to)) {
        if (display) side<MathP>(mn).label = strs.intern(t.substr(after + 2, le - 1 - (after + 2)));
        else
          diags.add(Sev::Info, "label-orphan", span(after + 1, le),
                    "an inline formula takes no label yet: it is dropped (display formulas, $ … $ on its own, do)");
        mn->span.end = L.raw(le);
        i = le;
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
    A.setKids(n, parseSub(i + 1, (u32)close, /*linkText=*/true));
    side<LinkP>(n).url = strs.intern(t.substr(close + 2, pclose - (close + 2)));
    pushItem(n);
    i = pclose + 1;
  }

  // strict pairs: an opener needs a glyph after it, a closer a glyph before
  // it; (plan P3-33; syntax.def Intraword) between two ASCII letters or
  // digits a marker is text — snake_case, 2*3*4 (CJK neighbours never guard)
  void pair(char c) {
    auto alnum = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); };
    if (i > 0 && i + 1 < to && alnum(t[i - 1]) && alnum(t[i + 1])) {
      put(c, i);
      i++;
      return;
    }
    bool canClose = prevGlyph && !pendingSpace && !pendingBreak && stack.size() > 1 && stack.back().marker == (u8)c;
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
    if (nextGlyph && tooDeep() && !nestSaid) {
      nestSaid = true;
      diags.add(Sev::Error, "nest-limit", span(i, i + 1),
                "content nests more than " + std::to_string(kMaxNesting) + " deep: this marker is text");
    }
    if (nextGlyph && !tooDeep()) {
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

  // a keyword form (plan P2-12): a Keyword node of Branch nodes — each its
  // head JS (a condition, a for/while head; none: else) and its body, parsed
  // as a content body; an elseChain keyword takes `else [ … ]` and
  // `else if (c) [ … ]` after a body
  void keyword(u32 hash, KwHead kh) {
    std::vector<AstNode*> branches;
    u32 open = kh.bodyOpen, hs = kh.headStart, he = kh.headEnd, from = hash, end = kh.bodyOpen;
    bool hasHead = true;
    for (;;) {
      const i32 close = bodyClose(open);
      if (close < 0) {
        diags.add(Sev::Error, "parse-inline", span(open, to), "unclosed keyword body");
        break;
      }
      AstNode* br = A.node<BranchP>(AstKind::Branch, span(from, (u32)close + 1));
      side<BranchP>(br).head = hasHead ? span(hs, he) : Span{};
      std::vector<AstNode*> body = parseBody(open + 1, (u32)close);
      // a body that is inline content keeps its edge whitespace as text does
      // — a space, or a line break (a soft break) — so iterations do not run
      // together (#for (const x of xs) [#x, ]); emit collapses it against a
      // space beside it, and a line drops it at its edges
      bool inl = false;  // content (not only statements), none of it blocks
      for (const AstNode* k : body) inl = inl || k->kind != AstKind::Stmt;
      for (const AstNode* k : body)
        for (SugarId b : {SugarId::para, SugarId::heading, SugarId::list, SugarId::item, SugarId::terms,
                          SugarId::quote, SugarId::rule, SugarId::fence, SugarId::region})
          if (k->isCall(b)) inl = false;
      if (inl) {
        auto ws = [&](u32 at) { return t[at] == ' ' || t[at] == '\t' || t[at] == '\n'; };
        auto edge = [&](u32 at) {
          AstNode* n = A.node<TextP>(AstKind::Text, span(at, at + 1));
          n->str = strs.intern(t.substr(at, 1));
          return n;
        };
        if (ws(open + 1)) body.insert(body.begin(), edge(open + 1));
        if ((u32)close > open + 1 && ws((u32)close - 1)) body.push_back(edge((u32)close - 1));
      }
      A.setKids(br, body);
      branches.push_back(br);
      end = (u32)close + 1;
      KwElse ke;  // (an else without a condition ends the chain: fuzz finding)
      if (!kKeywords[kh.kw].elseChain || !hasHead || !lexElse(t, end, ke)) break;
      from = end;
      while (from < to && (t[from] == ' ' || t[from] == '\t')) from++;
      open = ke.bodyOpen;
      hasHead = ke.cond;
      hs = ke.headStart;
      he = ke.headEnd;
    }
    if (branches.empty()) {  // its first body never closes (fuzz finding): an error, not a form
      AstNode* e = A.node<ErrorP>(AstKind::Error, span(hash, kh.bodyOpen + 1));
      e->str = strs.intern("keyword-form");
      side<ErrorP>(e).message = strs.intern("#" + std::string(kKeywords[kh.kw].name) + "'s body is not closed");
      pushItem(e);
      i = kh.bodyOpen + 1;
      return;
    }
    AstNode* k = A.node(AstKind::Keyword, span(hash, end));
    k->str = strs.intern(kKeywords[kh.kw].name);
    A.setKids(k, branches);
    pushItem(k);
    i = end;
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
    // (plan P3-33; syntax.def PrevIdent) a bare value head — no call, no
    // content argument — right after an identifier character is text
    // (word#todo, C#); a call or a body stays a splice (H#sub[2]O)
    if (!s.paren && hash > 0 && isSpliceCont(t[hash - 1]) &&
        t.substr(exprStart, exprEnd - exprStart).find('(') == std::string_view::npos &&
        !(exprEnd < to && t[exprEnd] == '[')) {
      put('#', hash);
      i = hash + 1;
      return;
    }
    spaceBeforeItem();
    flushText();
    // a keyword form (plan P2-12): #if (c) [A] else [B], #for (h) [B], #while (c) [B]
    KwHead kh;
    if (!s.paren && lexKeywordHead(t, hash, kh)) {
      keyword(hash, kh);
      return;
    }
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
        const bool form = keywordIndex(head) >= 0;
        std::string msg = form ? "#" + std::string(head) + " needs (…) and a body: #" + std::string(head) +
                                     (head == "for" ? " (const x of xs) [ … ]" : " (condition) [ … ]")
                          : head == "else" ? std::string("#else follows a body: #if (c) [ … ] else [ … ]")
                          : head == "let"  ? std::string("#let starts a line: #let name = value")
                          : "'" + std::string(head) + "' is a reserved word and cannot start a splice";
        if (form) code = "keyword-form";
        diags.add(Sev::Error, code, span(hash, after), msg);
        AstNode* e = A.node<ErrorP>(AstKind::Error, span(hash, after));
        e->str = strs.intern(code);
        side<ErrorP>(e).message = strs.intern(msg);
        pushItem(e);
        i = after;
        return;
      }
    }
    // the final call's argument list (plan P2-06): a named list is one
    // options object; named next to positional is an error with a fix-it
    bool named = false;
    if (s.lastCall && exprEnd > s.lastCall + 1 && t[exprEnd - 1] == ')') {
      std::string_view args = t.substr(s.lastCall + 1, exprEnd - 1 - (s.lastCall + 1));
      const JsArgList al = jsArgList(args);
      if (al.form == JsArgList::Mixed) {
        u32 after = exprEnd;
        while (after < to && t[after] == '[') {
          i32 close = bodyClose(after);
          if (close < 0) break;
          after = (u32)close + 1;
        }
        std::string msg = "named and positional arguments mixed: name every one, " +
                          std::string(t.substr(exprStart, s.lastCall - exprStart)) + jsNamedFixit(args, al);
        diags.add(Sev::Error, "mixed-args", span(hash, after), msg);
        AstNode* e = A.node<ErrorP>(AstKind::Error, span(hash, after));
        e->str = strs.intern("mixed-args");
        side<ErrorP>(e).message = strs.intern(msg);
        pushItem(e);
        i = after;
        return;
      }
      named = al.form == JsArgList::Named;
    }
    AstNode* spl = A.node<SpliceP>(AstKind::Splice, span(hash, exprEnd));
    side<SpliceP>(spl).expr = strs.intern(t.substr(exprStart, exprEnd - exprStart));
    side<SpliceP>(spl).lastCall = s.lastCall ? s.lastCall - exprStart : 0;
    side<SpliceP>(spl).named = named;
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
      } else if (u32 p = lexBareId(t, i + 1, to); p > i + 1) {  // IdStart IdCont* (IdJoin IdCont+)*
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
    // a supplement (D-L01): `[…]` right after the id is the reference's
    // extra content — parsed now, read by the reference template from P2-09
    if (end < to && t[end] == '[' && !reverted(end)) {
      i32 close = bodyClose(end);
      if (close >= 0) {
        A.setKids(n, parseSub(end + 1, (u32)close));
        end = (u32)close + 1;
        n->span.end = L.raw(end);
      }
    }
    pushItem(n);
    i = end;
  }

  // (plan P3-33; syntax.def brk) `\` at the end of a line: a hard line
  // break (the linebreak slot); the blanks before it and the next line's
  // indentation are no text
  void lineBreak() {
    pendingSpace = pendingBreak = false;
    flushText();
    pushItem(A.call(SugarId::linebreak, span(i, i + 2)));  // (the backslash and its line's end)
    prevGlyph = false;
    i += 2;
    while (i < to && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r')) i++;
  }

  // (plan P3-33; syntax.def url, an Island) a bare http(s) URL is a link to
  // itself, verbatim — markup inside it (a_b_c) is the URL's. Its scheme
  // is already this text's tail: it leaves the text for the link. In a
  // link's own text a URL is text.
  void autolink() {
    u32 s0 = 0, e = 0;
    const bool ok = !inLink && lexUrl(t, i, s0, e) && s0 >= from && bufEnd == i && buf.size() >= i - s0 &&
              std::string_view(buf).substr(buf.size() - (i - s0)) == t.substr(s0, i - s0);
    if (!ok) {
      put(':', i);
      i++;
      return;
    }
    buf.resize(buf.size() - (i - s0));
    bufEnd = s0;
    while (rmap.size() >= 2 && rmap[rmap.size() - 2] >= buf.size()) rmap.resize(rmap.size() - 2);
    if (!rmap.empty()) {
      rmC = rmap[rmap.size() - 2];
      rmR = rmap[rmap.size() - 1];
    }
    flushText();
    AstNode* n0 = A.call<LinkP>(SugarId::link, span(s0, e));
    const std::string_view url = t.substr(s0, e - s0);
    side<LinkP>(n0).url = strs.intern(url);
    AstNode* text = A.node<TextP>(AstKind::Text, span(s0, e));
    text->str = strs.intern(url);
    A.setKids(n0, std::vector<AstNode*>{text});
    pushItem(n0);
    i = e;
  }

  // (plan P3-33) adjacent text nodes — an unclosed marker's text between
  // its neighbours — are one: their strings, cooked→raw maps and cell cuts
  // joined
  void coalesce(std::vector<AstNode*>& items) {
    std::vector<AstNode*> out;
    for (AstNode* n : items) {
      if (n->kind != AstKind::Text || out.empty() || out.back()->kind != AstKind::Text) {
        out.push_back(n);
        continue;
      }
      out.back() = joinText(out.back(), n);
    }
    items.swap(out);
  }
  // pairs (cooked, raw relative to its span start) of a text's map; an
  // identity text: (0, 0)
  std::vector<u32> mapOf(const AstNode* n) const {
    std::vector<u32> m;
    const StrRef r = side<TextP>(n).rawmap;
    if (!r) return {0, 0};
    std::string_view s = strs.get(r);
    while (!s.empty()) {
      const size_t comma = s.find(','), colon = s.find(':');
      m.push_back((u32)std::stoul(std::string(s.substr(0, colon))));
      m.push_back((u32)std::stoul(std::string(s.substr(colon + 1, comma == std::string_view::npos ? std::string_view::npos
                                                                                             : comma - colon - 1))));
      s = comma == std::string_view::npos ? std::string_view{} : s.substr(comma + 1);
    }
    return m;
  }
  AstNode* joinText(const AstNode* a, const AstNode* b) {
    const std::string_view as = strs.get(a->str), bs = strs.get(b->str);
    const u32 ac = (u32)as.size(), shift = b->span.start - a->span.start;
    std::vector<u32> m = mapOf(a);
    if (m.size() >= 4 && m[m.size() - 2] == ac) m.resize(m.size() - 2);  // a's end: b's start says it
    const std::vector<u32> mb = mapOf(b);
    for (size_t k = 0; k < mb.size(); k += 2) {
      const u32 c = mb[k] + ac, r = mb[k + 1] + shift;
      if (m.size() >= 2 && r == m[m.size() - 1] + (c - m[m.size() - 2])) continue;  // the identity holds
      if (m.size() >= 2 && m[m.size() - 2] == c) m.resize(m.size() - 2);
      m.push_back(c);
      m.push_back(r);
    }
    const Span sp{a->span.start, b->span.end};
    const u32 total = ac + (u32)bs.size();
    if (m[m.size() - 1] + (total - m[m.size() - 2]) != sp.end - sp.start) {
      m.push_back(total);
      m.push_back(sp.end - sp.start);
    }
    AstNode* n = A.node<TextP>(AstKind::Text, sp);
    n->str = strs.intern(std::string(as) + std::string(bs));
    if (!(m.size() == 2 && m[1] == 0 && total == sp.end - sp.start)) {
      std::string s;
      for (size_t k = 0; k < m.size(); k += 2) appendf(s, "%s%u:%u", k ? "," : "", m[k], m[k + 1]);
      side<TextP>(n).rawmap = strs.intern(s);
    }
    std::string seps;
    for (const AstNode* x : {a, b})
      if (const StrRef sr = side<TextP>(x).seps) {
        std::string_view v = strs.get(sr);
        while (!v.empty()) {
          const size_t comma = v.find(',');
          u32 o = (u32)std::stoul(std::string(v.substr(0, comma)));
          if (x == b) o += ac;
          if (!seps.empty()) seps += ',';
          seps += std::to_string(o);
          v = comma == std::string_view::npos ? std::string_view{} : v.substr(comma + 1);
        }
      }
    if (!seps.empty()) side<TextP>(n).seps = strs.intern(seps);
    return n;
  }

  void run() {
    stack.push_back({0});
    i = from;
    // bytes the loop must look at: rule openers, blanks, joins, escapes —
    // and in a region paragraph the cell bar (plan P2-11)
    const bool cells = hints.cells;
    auto special = [cells](char ch) {
      return kInlineOpenerByte[(u8)ch] || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
             ch == '\\' || (cells && ch == '|');
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
      if (c == '\\') {
        // (plan P3-33; syntax.def Escapable) `\` and ASCII punctuation: that
        // character; `\` at a line's end: a hard break; else a backslash
        if (i + 1 < to && isEscapable(t[i + 1])) {
          put(t[i + 1], i, 2);
          i += 2;
        } else if (i + 1 < to && t[i + 1] == '\n') {
          lineBreak();
        } else {
          put('\\', i);
          i++;
        }
        continue;
      }
      const InlineRule rule = inlineOpener(t, i);
      switch (rule) {
        case InlineRule::url: autolink(); continue;
        case InlineRule::brk: break;  // (handled above)
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
      if (c == '|' && hints.cells && stack.size() == 1)  // a cell cut (D-L05: top level only)
        seps.push_back((u32)buf.size() - 1);
      i++;
    }
    flushText();
    while (stack.size() > 1) {
      Frame f = std::move(stack.back());
      stack.pop_back();
      AstNode* lit = A.node<TextP>(AstKind::Text, span(f.markerPos, f.markerPos + 1));
      char m = (char)f.marker;
      lit->str = strs.intern(std::string_view(&m, 1));
      auto& parent = stack.back().items;
      parent.push_back(lit);
      for (AstNode* it : f.items) parent.push_back(it);
    }
    coalesce(stack.back().items);
  }
};

std::vector<AstNode*> parseLeaf(const SourceText& src, const std::vector<Span>& spans, Arena& arena,
                                Interner& strs, DiagSink& diags, LeafHints hints = {}, u32 depth = 0) {
  LeafText L(src.view(), spans);
  InlineParser p(src, L, hints, 0, L.size(), depth, arena, strs, diags);
  p.run();
  return std::move(p.stack.back().items);
}


struct AstBuilder {
  const SourceText& src;
  Arena& arena;
  Interner& strs;
  DiagSink& diags;

  AstAlloc A{arena};
  u32 depth = 0;  // the levels above the node being built

  // a verbatim body: its line slices (container prefixes stripped) joined
  std::string joinLines(const std::vector<Span>& lines) const {
    std::string body;
    for (size_t k = 0; k < lines.size(); k++) {
      if (k) body += '\n';
      body += src.slice(lines[k]);
    }
    return body;
  }

  bool cellsNext = false;  // the next paragraph is a region's: it records cell cuts
  // a leaf's inline content, under its call
  std::vector<AstNode*> inlineParse(const std::vector<Span>& spans, LeafHints hints = {}) {
    return parseLeaf(src, spans, arena, strs, diags, hints, depth + 1);
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
    depth += s->kind != SkelKind::Doc;
    for (const SkelNode* k : s->kids) kids.push_back(build(k, top));
    depth -= s->kind != SkelKind::Doc;
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
        LeafHints h{&s->bodies, &s->literalAt};
        h.cells = cellsNext;
        cellsNext = false;
        A.setKids(p, inlineParse(s->lineSpans, h));
        // a ` <id>` closing a paragraph as literal text (not a formula's
        // label): say so (plan P2-06)
        if (!s->lineSpans.empty() && p->nkids) {
          const Span last = s->lineSpans.back();
          const LabelSuffix ls = trailingLabel(src.view(), last.start, last.end);
          const AstNode* tail = p->kids()[p->nkids - 1];
          if (ls.ok && tail->kind == AstKind::Text && tail->span.end >= ls.labelEnd + 1)
            diags.add(Sev::Info, "label-like-text", {ls.labelStart - 1, ls.labelEnd + 1},
                      "<" + std::string(src.slice({ls.labelStart, ls.labelEnd})) +
                          "> at the end of a paragraph is text: a paragraph takes a label through "
                          "#para({label: …})[…]; write \\< to keep it as text quietly");
        }
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
        if (s->marker == '/') {  // (plan P3-34; D-L08) a description list
          AstNode* d = A.call(SugarId::terms, s->span);
          A.setKids(d, buildKids(s));
          return d;
        }
        AstNode* l = A.call<ListP>(SugarId::list, s->span);
        side<ListP>(l) = {s->ordered, s->start};
        A.setKids(l, buildKids(s));
        return l;
      }
      case SkelKind::Item: {
        AstNode* it = A.call(SugarId::item, s->span);
        std::vector<AstNode*> kids = buildKids(s);
        if (!s->termSpan.empty()) {  // (plan P3-34) its term part, inline, first
          AstNode* term = A.call(SugarId::termpart, s->termSpan);
          depth++;
          A.setKids(term, inlineParse({s->termSpan}));
          depth--;
          kids.insert(kids.begin(), term);
        }
        A.setKids(it, kids);
        return it;
      }
      case SkelKind::Quote: {
        AstNode* q = A.call(SugarId::quote, s->span);
        A.setKids(q, buildKids(s));
        return q;
      }
      case SkelKind::Fence: {
        // the info string, Markdown-style (plan P2-06): a tag, its argument
        // list (one grammar with splices and region headers: named entries,
        // the handler's options), then free info words, then an optional
        // ` <id>` label
        const Span info = s->langSpan;
        std::string_view all = src.view();
        u32 p = info.start;
        while (p < info.end && (all[p] == ' ' || all[p] == '\t')) p++;
        u32 te = p;
        while (te < info.end && all[te] != ' ' && all[te] != '\t' && all[te] != '(' && all[te] != '\r') te++;
        Span args;
        u32 after = te;
        if (after < info.end && all[after] == '(') {
          JsScan js = scanJs(all.substr(0, info.end), after, true);
          if (js.ok) {
            args = {after + 1, js.end - 1};
            after = js.end;
          }
        }
        if (!args.empty()) {
          const JsArgList al = jsArgList(src.slice(args));
          if (al.form == JsArgList::Positional || al.form == JsArgList::Mixed)
            return errorNode(s->span, "header-positional",
                             "fence arguments must be named (key: value): write " +
                                 std::string(src.slice({p, te})) + jsNamedFixit(src.slice(args), al));
        }
        const LabelSuffix ls = trailingLabel(all, after, info.end);
        u32 ie = ls.ok ? ls.textEnd : info.end, ib = after;
        while (ib < ie && (all[ib] == ' ' || all[ib] == '\t')) ib++;
        while (ie > ib && (all[ie - 1] == ' ' || all[ie - 1] == '\t' || all[ie - 1] == '\r')) ie--;
        AstNode* f = A.call<FenceP>(SugarId::fence, s->span);
        FenceP& fp = side<FenceP>(f);
        fp.lang = strs.intern(src.slice({p, te}));
        fp.args = args;
        if (ie > ib) fp.info = strs.intern(src.slice({ib, ie}));
        if (ls.ok) fp.label = strs.intern(src.slice({ls.labelStart, ls.labelEnd}));
        fp.bodyOffset = s->lineSpans.empty() ? s->span.end : s->lineSpans[0].start;
        // the body's raw end: its source span is [bodyOffset, bodyEnd) (plan P2-04)
        fp.bodyEnd = s->lineSpans.empty() ? fp.bodyOffset : s->lineSpans.back().end;
        // inside a quote or list item (or with CRLF line ends) the body
        // lines are not contiguous in the source: each line's offset goes
        // to the handler (and to a sidecar's notes, plan P2-13)
        bool contiguous = true;
        for (size_t k = 1; k < s->lineSpans.size(); k++)
          contiguous = contiguous && s->lineSpans[k].start == s->lineSpans[k - 1].end + 1;
        if (s->contained || !contiguous) {
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
        AstNode* c = A.node<CommentP>(AstKind::Comment, s->span);
        c->str = strs.intern(joinLines(s->lineSpans));
        side<CommentP>(c).front = s->front;
        return c;
      }
      case SkelKind::Region: {
        // the header: one argument grammar (plan P2-06) — named entries are
        // the options; anything else is an error with a fix-it
        bool labelArg = false;
        if (!s->inner.empty()) {
          const JsArgList al = jsArgList(src.slice(s->inner));
          if (al.form == JsArgList::Positional || al.form == JsArgList::Mixed)
            return errorNode(s->span, "header-positional",
                             "region arguments must be named (key: value): write #!" +
                                 std::string(src.slice(s->langSpan)) + jsNamedFixit(src.slice(s->inner), al));
          for (const JsArgItem& it : al.items) {
            std::string_view t = src.slice({s->inner.start + it.start, s->inner.start + it.end});
            labelArg = labelArg || (it.k == JsArgItem::Named && (t.substr(0, 5) == "label" &&
                                                                 (t.size() == 5 || !isIdentCont(t[5]))));
          }
        }
        AstNode* r = A.call<RegionP>(SugarId::region, s->span);
        r->str = strs.intern(src.slice(s->langSpan));
        side<RegionP>(r).args = s->inner;  // opener args (inside parens; empty span = none)
        if (!s->labelSpan.empty()) {  // ` <id>` (plan P2-06): the options' label; an explicit label: wins
          side<RegionP>(r).label = strs.intern(src.slice(s->labelSpan));
          if (labelArg)
            diags.add(Sev::Warning, "label-conflict", s->labelSpan,
                      "the region has a label: argument and a <" + std::string(src.slice(s->labelSpan)) +
                          "> suffix: the label: argument wins");
        }
        // its interior lowers like any blocks (plan P2-11); a paragraph
        // records its cell cuts for body.rows()
        std::vector<AstNode*> kids;
        depth++;
        for (const SkelNode* k : s->kids) {
          cellsNext = k->kind == SkelKind::Para;
          kids.push_back(build(k));
        }
        depth--;
        A.setKids(r, kids);
        return r;
      }
      case SkelKind::CodeLet:
      case SkelKind::CodeBlock: {
        const bool let = s->kind == SkelKind::CodeLet;
        // its JS: its lines joined, container prefixes stripped (a content
        // literal: its name)
        std::string js;
        if (s->content) {
          js = src.slice(s->inner);
        } else {
          for (size_t l = 0; l < s->lineSpans.size(); l++) {
            if (l) js += '\n';
            js += src.slice(s->lineSpans[l]);
          }
        }
        std::string_view reserved = jsReservedBinding(js, let);
        if (!reserved.empty())
          return errorNode(s->span, "reserved-name",
                           "'" + std::string(reserved) +
                               "': names starting with __ are reserved for the engine");
        AstNode* c = A.node<StmtP>(AstKind::Stmt, s->span);
        side<StmtP>(c) = {let, strs.intern(js), s->content};
        // `#let x = [ … ]` (plan P2-12): its body is content
        if (s->content) A.setKids(c, parseContentLines(src, s->lineSpans, arena, strs, diags, depth + 1));
        // a statement anywhere (plan P2-12) runs where it stands; one nested
        // in a block that declares (a #{…} with let/const/function, a #let of
        // a pattern) keeps those bindings to itself
        if (!top && !s->content) {
          const std::string_view inner = js;
          bool local;
          if (let) {
            const JsSimpleLet sl = jsSimpleLet(inner);
            local = !sl.ok || jsReservedWord(inner.substr(sl.identStart, sl.identEnd - sl.identStart));
          } else {
            local = jsDeclares(inner);
          }
          if (local)
            diags.add(Sev::Info, "statement-local", s->span,
                      "the bindings of this nested statement stay inside it: bind a name with #let name = value");
        }
        return c;
      }
    }
    return A.node(AstKind::Doc, s->span);
  }
};

std::vector<AstNode*> parseBlocks(const SourceText& src, const std::vector<Span>& lines, Arena& arena,
                                  Interner& strs, DiagSink& diags, u32 depth) {
  Skeleton sk = linepassLines(src, lines, arena, diags, depth);
  AstBuilder b{src, arena, strs, diags};
  b.depth = depth;
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
