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

  AstNode* mk(AstKind k, Span s) {
    AstNode* n = arena.make<AstNode>();
    n->kind = k;
    n->span = s;
    return n;
  }

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

  // True if scanning from `from` to `to` crosses only plain newlines (no
  // container prefixes stripped in between) — cross-line raw scans are only
  // sound then.
  bool contiguous(u32 to) const {
    for (size_t k = sp; k + 1 < spans.size() && spans[k].end < to; k++)
      if (spans[k + 1].start != spans[k].end + 1) return false;
    return true;
  }
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
    AstNode* t = mk(AstKind::Text, {bufStart, bufEnd});
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

  AstNode* parseSub(Span s, AstKind kind) {
    InlineParser p{src, arena, strs, diags, all};
    p.spans = {s};
    p.run();
    AstNode* n = mk(kind, s);
    n->kids = std::move(p.stack.back().items);
    return n;
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
    AstNode* spl = mk(AstKind::Splice, {hashPos, exprEnd});
    spl->expr = {exprStart, exprEnd};
    spl->lastCallStart = lastCall;

    // content arguments: directly adjacent [ ... ], repeatable (single span)
    u32 after = exprEnd;
    while (after < lim && all[after] == '[') {
      i32 close = matchBracket(after, lim);
      if (close < 0) {
        diags.add(Sev::Error, "parse-inline", {after, lim}, "unclosed content argument");
        break;
      }
      spl->kids.push_back(parseSub({after + 1, (u32)close}, AstKind::SpliceArg));
      after = (u32)close + 1;
    }
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
        u32 hardEnd = (u32)all.size();
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
        if (depth != 0)
          diags.add(Sev::Error, "parse-inline", {i, p}, "unterminated comment");
        flushText();
        AstNode* cm = mk(AstKind::Comment, {i, p});
        std::string body(all.substr(i + 3, (depth == 0 ? p - 3 : p) - (i + 3)));
        cm->str = strs.intern(body);
        stack.back().items.push_back(cm);  // does not set prevGlyph
        seekTo(p);
        continue;
      }
      if (c == '`') {
        u32 close = i + 1;
        while (close < lim && all[close] != '`') close++;
        if (close < lim) {
          spaceBeforeItem();
          flushText();
          AstNode* code = mk(AstKind::Code, {i, close + 1});
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
            AstNode* link = parseSub({i + 1, (u32)close}, AstKind::Link);
            link->span = {i, (u32)pclose + 1};
            std::string url(all.substr((u32)close + 2, (u32)pclose - ((u32)close + 2)));
            link->aux = strs.intern(url);
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
          AstNode* s = mk(AstKind::Styled, {f.markerPos, i + 1});
          s->tag = (u8)c;
          s->kids = std::move(f.items);
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
          AstNode* r = mk(AstKind::Ref, {i, end});
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
      AstNode* lit = mk(AstKind::Text, {f.markerPos, f.markerPos + 1});
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
    if (c == '`') {
      u32 q = p + 1;
      while (q < line.end && all[q] != '`') q++;
      p = (q < line.end) ? q + 1 : q + 1;
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

  AstNode* mk(AstKind k, Span s) {
    AstNode* n = arena.make<AstNode>();
    n->kind = k;
    n->span = s;
    return n;
  }

  std::vector<AstNode*> inlineParse(const std::vector<Span>& spans) {
    InlineParser p{src, arena, strs, diags, src.view()};
    p.spans = spans;
    p.run();
    return std::move(p.stack.back().items);
  }

  AstNode* build(const SkelNode* s) {
    switch (s->kind) {
      case SkelKind::Doc: {
        AstNode* d = mk(AstKind::Doc, s->span);
        for (const SkelNode* k : s->kids) d->kids.push_back(build(k));
        return d;
      }
      case SkelKind::Para: {
        AstNode* p = mk(AstKind::Para, s->span);
        p->kids = inlineParse(s->lineSpans);
        return p;
      }
      case SkelKind::Heading: {
        AstNode* h = mk(AstKind::Heading, s->span);
        h->tag = s->level;
        if (!s->labelSpan.empty()) h->aux = strs.intern(src.slice(s->labelSpan));
        h->kids = inlineParse(s->lineSpans);
        return h;
      }
      case SkelKind::List: {
        AstNode* l = mk(AstKind::ListB, s->span);
        l->ordered = s->ordered;
        l->num = s->start;
        for (const SkelNode* k : s->kids) l->kids.push_back(build(k));
        return l;
      }
      case SkelKind::Item: {
        AstNode* it = mk(AstKind::Item, s->span);
        for (const SkelNode* k : s->kids) it->kids.push_back(build(k));
        return it;
      }
      case SkelKind::Quote: {
        AstNode* q = mk(AstKind::Quote, s->span);
        for (const SkelNode* k : s->kids) q->kids.push_back(build(k));
        return q;
      }
      case SkelKind::Fence: {
        AstNode* f = mk(AstKind::CodeBlockB, s->span);
        // info string "tag(args)": args reuse the splice argument lexer and
        // compile to a JS object literal for the fence dispatcher (v2 §4.1)
        Span tagSpan = s->langSpan;
        u32 lp = tagSpan.start;
        std::string_view all = src.view();
        while (lp < tagSpan.end && all[lp] != '(') lp++;
        if (lp < tagSpan.end) {
          JsScan js = scanJs(all.substr(0, tagSpan.end), lp, true);
          if (js.ok) {
            f->expr = {lp + 1, js.end - 1};
            tagSpan.end = lp;
          }
        }
        std::string lang(src.slice(tagSpan));
        while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\r')) lang.pop_back();
        f->aux = strs.intern(lang);
        f->num = (int)(s->lineSpans.empty() ? s->span.end : s->lineSpans[0].start);
        std::string body;
        for (size_t k = 0; k < s->lineSpans.size(); k++) {
          if (k) body += '\n';
          body += src.slice(s->lineSpans[k]);
        }
        f->str = strs.intern(body);
        return f;
      }
      case SkelKind::Rule:
        return mk(AstKind::Rule, s->span);
      case SkelKind::Comment: {
        AstNode* c = mk(AstKind::Comment, s->span);
        std::string body(src.slice(s->inner));
        c->str = strs.intern(body);
        return c;
      }
      case SkelKind::Region: {
        AstNode* r = mk(AstKind::Region, s->span);
        r->str = strs.intern(src.slice(s->langSpan));
        r->expr = s->inner;  // opener args (inside parens; empty span = none)
        for (const SkelNode* k : s->kids) {
          if (k->kind == SkelKind::Para) {
            // line provenance (v2 §4.1): each source line is a Row whose
            // Cells are the top-level '|' segmentation, inline-parsed
            AstNode* p = mk(AstKind::Para, k->span);
            for (const Span& line : k->lineSpans) {
              AstNode* row = mk(AstKind::Row, line);
              std::vector<Span> cells;
              splitCells(src.view(), line, cells);
              for (const Span& c : cells) {
                AstNode* cell = mk(AstKind::Cell, c);
                cell->kids = inlineParse({c});
                row->kids.push_back(cell);
              }
              p->kids.push_back(row);
            }
            r->kids.push_back(p);
          } else {
            r->kids.push_back(build(k));
          }
        }
        return r;
      }
      case SkelKind::CodeLet:
      case SkelKind::CodeBlock: {
        AstNode* c = mk(AstKind::CodeStmt, s->span);
        c->expr = s->inner;
        c->tag = s->kind == SkelKind::CodeLet ? 0 : 1;
        return c;
      }
    }
    return mk(AstKind::Doc, s->span);
  }
};

}  // namespace

AstNode* parseDoc(const SourceText& src, const Skeleton& sk, Arena& arena,
                  Interner& strs, DiagSink& diags) {
  AstBuilder b{src, arena, strs, diags};
  return b.build(sk.root);
}

static void dumpNode(std::string& out, const AstNode* n, const SourceText& src,
                     const Interner& strs, int depth) {
  for (int i = 0; i < depth; i++) out += "  ";
  auto hdr = [&](const char* name) {
    appendf(out, "%s @[%u,%u)", name, n->span.start, n->span.end);
  };
  switch (n->kind) {
    case AstKind::Doc: hdr("doc"); break;
    case AstKind::Para: hdr("para"); break;
    case AstKind::Heading:
      hdr("heading");
      appendf(out, " level=%d", n->tag);
      if (n->aux) {
        out += " label=\"";
        appendEscaped(out, strs.get(n->aux));
        out += "\"";
      }
      break;
    case AstKind::ListB:
      hdr("list");
      appendf(out, " %s start=%d", n->ordered ? "ordered" : "bullet", n->num);
      break;
    case AstKind::Item: hdr("item"); break;
    case AstKind::Quote: hdr("quote"); break;
    case AstKind::CodeBlockB:
      hdr("codeblock");
      out += " lang=\"";
      appendEscaped(out, strs.get(n->aux));
      out += "\" body=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    case AstKind::Rule: hdr("rule"); break;
    case AstKind::Comment:
      hdr("comment");
      out += " body=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    case AstKind::CodeStmt:
      hdr(n->tag == 0 ? "code-let" : "code-block");
      out += " js=\"";
      appendEscaped(out, src.slice(n->expr));
      out += "\"";
      break;
    case AstKind::Text:
      hdr("text");
      out += " str=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    case AstKind::Styled: hdr("styled"); appendf(out, " marker=%c", (char)n->tag); break;
    case AstKind::Splice:
      hdr("splice");
      out += " expr=\"";
      appendEscaped(out, src.slice(n->expr));
      out += "\"";
      break;
    case AstKind::SpliceArg: hdr("arg"); break;
    case AstKind::Link:
      hdr("link");
      out += " url=\"";
      appendEscaped(out, strs.get(n->aux));
      out += "\"";
      break;
    case AstKind::Code:
      hdr("code");
      out += " str=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    case AstKind::Ref:
      hdr("ref");
      out += " target=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    case AstKind::Region:
      hdr("region");
      out += " name=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    case AstKind::Row: hdr("row"); break;
    case AstKind::Cell: hdr("cell"); break;
  }
  out += "\n";
  for (const AstNode* k : n->kids) dumpNode(out, k, src, strs, depth + 1);
}

std::string dumpAst(const AstNode* doc, const SourceText& src, const Interner& strs) {
  std::string out;
  dumpNode(out, doc, src, strs, 0);
  return out;
}

}  // namespace tsr
