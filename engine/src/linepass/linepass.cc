#include "linepass.h"

#include "../inline/jslex.h"

namespace tsr {

namespace {

struct OpenC {
  SkelNode* node;      // Quote / Item (List is implicit parent of Item)
  u32 contentCol = 0;  // Item: required continuation column
};

struct LinePass {
  const SourceText& src;
  Arena& arena;
  DiagSink& diags;
  std::string_view all;

  SkelNode* root;
  std::vector<OpenC> open;   // container stack (Quote/Item entries)
  SkelNode* leaf = nullptr;  // open paragraph

  SkelNode* mk(SkelKind k) {
    SkelNode* n = arena.make<SkelNode>();
    n->kind = k;
    return n;
  }
  SkelNode* parent() {
    for (auto it = open.rbegin(); it != open.rend(); ++it)
      return it->node;
    return root;
  }
  void closeLeaf() { leaf = nullptr; }
  void closeTo(size_t depth) {
    closeLeaf();
    while (open.size() > depth) open.pop_back();
  }

  static bool isBlank(std::string_view t) {
    for (char c : t)
      if (c != ' ' && c != '\t' && c != '\r') return false;
    return true;
  }

  // --- per-line prefix matching -------------------------------------------
  // pos/col walk the line; returns count of open containers matched.
  size_t matchPrefixes(u32 ls, u32 le, u32& pos, u32& col, bool blank) {
    size_t matched = 0;
    for (const OpenC& c : open) {
      if (c.node->kind == SkelKind::Region) { matched++; continue; }
      if (c.node->kind == SkelKind::Quote) {
        u32 p = pos, cl = col;
        while (p < le && all[p] == ' ') { p++; cl++; }
        if (p < le && all[p] == '>') {
          p++; cl++;
          if (p < le && all[p] == ' ') { p++; cl++; }
          pos = p; col = cl; matched++;
          continue;
        }
        break;  // quote prefix absent
      }
      // Item: blank lines stay inside; content must reach contentCol
      if (blank) { matched++; continue; }
      u32 p = pos, cl = col;
      while (p < le && all[p] == ' ' && cl < c.contentCol) { p++; cl++; }
      if (cl >= c.contentCol) { pos = p; col = cl; matched++; continue; }
      break;
    }
    (void)ls;
    return matched;
  }

  // --- new container starters ---------------------------------------------
  // Returns true if a starter was consumed (and containers opened).
  bool tryStarters(u32 le, u32& pos, u32& col) {
    bool any = false;
    for (;;) {
      u32 p = pos, cl = col;
      while (p < le && all[p] == ' ') { p++; cl++; }
      if (p >= le) break;
      char c = all[p];
      if (c == '>') {
        SkelNode* q = mk(SkelKind::Quote);
        q->span = {p, le};
        parent()->kids.push_back(q);
        open.push_back({q, 0});
        p++; cl++;
        if (p < le && all[p] == ' ') { p++; cl++; }
        pos = p; col = cl;
        closeLeaf();
        any = true;
        continue;
      }
      bool ordered = false;
      u32 markerLen = 0;
      int startNum = 1;
      if ((c == '-' || c == '+') && p + 1 < le && all[p + 1] == ' ') {
        ordered = (c == '+');
        markerLen = 1;
      } else if (c >= '0' && c <= '9') {
        u32 q = p;
        int num = 0;
        while (q < le && all[q] >= '0' && all[q] <= '9' && q - p < 9) {
          num = num * 10 + (all[q] - '0');
          q++;
        }
        if (q < le && all[q] == '.' && q + 1 < le && all[q + 1] == ' ') {
          ordered = true;
          markerLen = (q + 1) - p;
          startNum = num;
        }
      }
      if (markerLen == 0) break;
      // thematic break `---` shadows a `- ` start? "- " requires space, "---" has none — fine.
      u32 contentCol = cl + markerLen + 1;
      // same-type list at same column continues; else open a new List
      SkelNode* list = nullptr;
      SkelNode* par = parent();
      if (!par->kids.empty() && par->kids.back()->kind == SkelKind::List &&
          par->kids.back()->ordered == ordered && par->kids.back()->level == (u8)cl)
        list = par->kids.back();
      if (!list) {
        list = mk(SkelKind::List);
        list->ordered = ordered;
        list->start = startNum;
        list->level = (u8)cl;  // marker column (repurposed)
        list->span = {p, le};
        par->kids.push_back(list);
      }
      SkelNode* item = mk(SkelKind::Item);
      item->span = {p, le};
      list->kids.push_back(item);
      open.push_back({item, contentCol});
      pos = p + markerLen;
      col = cl + markerLen;
      if (pos < le && all[pos] == ' ') { pos++; col++; }
      closeLeaf();
      any = true;
    }
    return any;
  }

  // --- leaves --------------------------------------------------------------
  void addParaLine(u32 pos, u32 le) {
    // strip trailing ws
    u32 e = le;
    while (e > pos && (all[e - 1] == ' ' || all[e - 1] == '\t' || all[e - 1] == '\r')) e--;
    if (e <= pos) return;
    if (!leaf) {
      leaf = mk(SkelKind::Para);
      leaf->span = {pos, e};
      parent()->kids.push_back(leaf);
    }
    leaf->span.end = e;
    leaf->lineSpans.push_back({pos, e});
  }

  // consume a fence starting at line ln; returns last consumed line.
  // CommonMark-style: N>=3 backticks, closer needs >= N; the opener's
  // indentation is stripped from content lines (dedent); the closer may be
  // indented. `openCol` = column of the first backtick within the content.
  u32 fence(u32 ln, u32 pos, u32 openCol, u32 nlines) {
    u32 le = src.lineEnd(ln);
    u32 nTicks = 0;
    while (pos + nTicks < le && all[pos + nTicks] == '`') nTicks++;
    SkelNode* f = mk(SkelKind::Fence);
    f->span = {pos, le};
    u32 lang0 = pos + nTicks, lang1 = le;
    while (lang0 < lang1 && all[lang0] == ' ') lang0++;
    f->langSpan = {lang0, lang1};
    parent()->kids.push_back(f);
    closeLeaf();
    u32 l = ln + 1;
    for (; l < nlines; l++) {
      u32 ls2 = src.lineStart(l), le2 = src.lineEnd(l);
      u32 p2 = ls2, c2 = 0;
      matchPrefixes(ls2, le2, p2, c2, isBlank(all.substr(ls2, le2 - ls2)));
      // closer: optional indentation, then >= nTicks backticks, only trailing ws
      {
        u32 q = p2;
        while (q < le2 && all[q] == ' ') q++;
        u32 t = 0;
        while (q + t < le2 && all[q + t] == '`') t++;
        u32 after = q + t;
        while (after < le2 && (all[after] == ' ' || all[after] == '\r')) after++;
        if (t >= nTicks && after == le2) {
          f->span.end = le2;
          return l;
        }
      }
      // content line: dedent up to the opener's column
      u32 strip = 0;
      while (strip < openCol && p2 + strip < le2 && all[p2 + strip] == ' ') strip++;
      f->lineSpans.push_back({p2 + strip, le2});
      f->span.end = le2;
    }
    diags.add(Sev::Error, "parse-block", f->span, "unterminated fence");
    return l - 1;
  }

  // block comment starting at pos; returns end offset (after --%)
  u32 blockComment(u32 pos) {
    u32 i = pos + 3;  // after %--
    int depth = 1;
    while (i < all.size()) {
      if (i + 2 < all.size() && all[i] == '%' && all[i + 1] == '-' && all[i + 2] == '-') { depth++; i += 3; continue; }
      if (i + 2 < all.size() && all[i] == '-' && all[i + 1] == '-' && all[i + 2] == '%') {
        depth--;
        i += 3;
        if (depth == 0) {
          SkelNode* cm = mk(SkelKind::Comment);
          cm->span = {pos, i};
          cm->inner = {pos + 3, i - 3};
          parent()->kids.push_back(cm);
          closeLeaf();
          return i;
        }
        continue;
      }
      i++;
    }
    diags.add(Sev::Error, "parse-block", {pos, (u32)all.size()}, "unterminated comment");
    SkelNode* cm = mk(SkelKind::Comment);
    cm->span = {pos, (u32)all.size()};
    cm->inner = {pos + 3, (u32)all.size()};
    parent()->kids.push_back(cm);
    return (u32)all.size();
  }

  void run() {
    root = mk(SkelKind::Doc);
    root->span = {0, src.size()};
    const u32 nlines = src.lineCount();
    for (u32 ln = 0; ln < nlines; ln++) {
      u32 ls = src.lineStart(ln), le = src.lineEnd(ln);
      bool blank = isBlank(all.substr(ls, le - ls));
      u32 pos = ls, col = 0;
      size_t matched = matchPrefixes(ls, le, pos, col, blank);

      if (blank) {
        closeLeaf();  // containers stay open; items close on failed indent later
        continue;
      }
      if (matched < open.size()) closeTo(matched);
      tryStarters(le, pos, col);

      // skip leading spaces of leaf content
      while (pos < le && all[pos] == ' ') { pos++; col++; }
      if (pos >= le) { closeLeaf(); continue; }
      std::string_view rest = all.substr(pos, le - pos);

      if (rest.size() >= 3 && rest.substr(0, 3) == "```") {
        ln = fence(ln, pos, col, nlines);
        continue;
      }
      if (rest.size() >= 3 && rest.substr(0, 3) == "%--") {
        u32 end = blockComment(pos);
        while (ln + 1 < nlines && src.lineStart(ln + 1) <= end) ln++;
        continue;
      }
      if (rest[0] == '=') {
        u32 n = 0;
        while (n < rest.size() && rest[n] == '=') n++;
        if (n <= 6 && n < rest.size() && rest[n] == ' ') {
          closeLeaf();
          SkelNode* h = mk(SkelKind::Heading);
          h->level = (u8)n;
          u32 cs = pos + n + 1;
          u32 e = le;
          while (e > cs && (all[e - 1] == ' ' || all[e - 1] == '\r')) e--;
          // trailing "<id>" label (v2 §11.1): space + <…> at line end
          if (e > cs + 2 && all[e - 1] == '>') {
            u32 lb = e - 1;
            while (lb > cs && all[lb - 1] != '<' && all[lb - 1] != '>' &&
                   all[lb - 1] != ' ')
              lb--;
            if (lb >= cs + 2 && all[lb - 1] == '<' && lb < e - 1 && all[lb - 2] == ' ') {
              h->labelSpan = {lb, e - 1};
              e = lb - 2;
              while (e > cs && all[e - 1] == ' ') e--;
            }
          }
          h->span = {pos, e};
          h->lineSpans.push_back({cs, e});
          parent()->kids.push_back(h);
          continue;
        }
      }
      {  // thematic break: 3+ dashes alone
        u32 n = 0;
        while (n < rest.size() && rest[n] == '-') n++;
        u32 t = n;
        while (t < rest.size() && (rest[t] == ' ' || rest[t] == '\r')) t++;
        if (n >= 3 && t == rest.size()) {
          closeLeaf();
          SkelNode* r = mk(SkelKind::Rule);
          r->span = {pos, le};
          parent()->kids.push_back(r);
          continue;
        }
      }
      // region opener: #!name(args)? alone on its line (v2 §4.1)
      if (rest.size() >= 3 && rest[0] == '#' && rest[1] == '!' && isIdentStart(rest[2])) {
        u32 np = pos + 2;
        while (np < le && isIdentCont(all[np])) np++;
        Span nameSpan{pos + 2, np};
        Span argsSpan{np, np};
        u32 after = np;
        bool ok = true;
        if (after < le && all[after] == '(') {
          JsScan js = scanJs(all.substr(0, le), after, true);
          if (js.ok) {
            argsSpan = {after + 1, js.end - 1};
            after = js.end;
          } else ok = false;
        }
        u32 t = after;
        while (t < le && (all[t] == ' ' || all[t] == '\r')) t++;
        if (ok && t == le) {
          SkelNode* rg = mk(SkelKind::Region);
          rg->span = {pos, le};
          rg->langSpan = nameSpan;
          rg->inner = argsSpan;
          parent()->kids.push_back(rg);
          open.push_back({rg, 0});
          closeLeaf();
          continue;
        }
        // fall through: not a region opener, plain paragraph text
      }
      // region closer: #name! alone on its line
      if (rest.size() >= 3 && rest[0] == '#' && isIdentStart(rest[1])) {
        u32 np = pos + 1;
        while (np < le && isIdentCont(all[np])) np++;
        u32 t = np + 1;
        while (t < le && (all[t] == ' ' || all[t] == '\r')) t++;
        if (np < le && all[np] == '!' && t == le) {
          size_t ri = open.size();
          while (ri > 0 && open[ri - 1].node->kind != SkelKind::Region) ri--;
          if (ri > 0) {
            SkelNode* rg = open[ri - 1].node;
            std::string_view want = src.slice(rg->langSpan);
            std::string_view got = all.substr(pos + 1, np - (pos + 1));
            if (want != got)
              diags.add(Sev::Error, "region-mismatch", {pos, le},
                        "closer '#" + std::string(got) +
                            "!' does not match open region '#!" + std::string(want) + "'");
            rg->span.end = le;
            closeTo(ri);       // pop containers opened inside the region
            open.pop_back();   // pop the region itself
            closeLeaf();
            continue;
          }
          // no open region: plain paragraph text
        }
      }
      if (rest.size() >= 4 && rest.substr(0, 4) == "#let" &&
          (rest.size() == 4 || rest[4] == ' ' || rest[4] == '\t')) {
        closeLeaf();
        u32 innerStart = pos + 4;
        // multi-line statements only at top level (container prefixes would
        // corrupt the JS text) — inside containers, scan stops at EOL.
        JsScan s = scanJs(open.empty() ? all : all.substr(0, le), innerStart, false);
        u32 end = s.end;
        if (!s.ok) diags.add(Sev::Error, "splice-js", {pos, end}, "unterminated #let");
        SkelNode* c = mk(SkelKind::CodeLet);
        c->span = {pos, end};
        c->inner = {innerStart, s.hitSemicolon ? end - 1 : end};
        parent()->kids.push_back(c);
        while (ln + 1 < nlines && src.lineStart(ln + 1) <= end) ln++;
        continue;
      }
      if (rest.size() >= 2 && rest.substr(0, 2) == "#{") {
        closeLeaf();
        JsScan s = scanJs(open.empty() ? all : all.substr(0, le), pos + 1, true);
        u32 end = s.ok ? s.end : le;
        if (!s.ok) diags.add(Sev::Error, "splice-js", {pos, end}, "unterminated #{ block");
        SkelNode* c = mk(SkelKind::CodeBlock);
        c->span = {pos, end};
        c->inner = {pos + 2, s.ok ? end - 1 : end};
        parent()->kids.push_back(c);
        while (ln + 1 < nlines && src.lineStart(ln + 1) <= end) ln++;
        continue;
      }
      addParaLine(pos, le);
    }
    for (const OpenC& c : open)
      if (c.node->kind == SkelKind::Region)
        diags.add(Sev::Error, "region-unclosed", c.node->span,
                  "region '#!" + std::string(src.slice(c.node->langSpan)) +
                      "' has no matching closer");
  }
};

}  // namespace

Skeleton linepass(const SourceText& src, Arena& arena, DiagSink& diags) {
  LinePass lp{src, arena, diags, src.view()};
  lp.run();
  return {lp.root};
}

static void dumpNode(std::string& out, const SkelNode* n, const SourceText& src, int depth) {
  for (int i = 0; i < depth; i++) out += "  ";
  switch (n->kind) {
    case SkelKind::Doc: appendf(out, "doc @[%u,%u)\n", n->span.start, n->span.end); break;
    case SkelKind::Para:
      appendf(out, "para @[%u,%u) lines=%zu\n", n->span.start, n->span.end, n->lineSpans.size());
      break;
    case SkelKind::Heading:
      appendf(out, "heading%d @[%u,%u)", n->level, n->span.start, n->span.end);
      if (!n->labelSpan.empty()) {
        out += " label=\"";
        appendEscaped(out, src.slice(n->labelSpan));
        out += "\"";
      }
      out += "\n";
      break;
    case SkelKind::List:
      appendf(out, "list %s start=%d @[%u,%u)\n", n->ordered ? "ordered" : "bullet", n->start,
              n->span.start, n->span.end);
      break;
    case SkelKind::Item: appendf(out, "item @[%u,%u)\n", n->span.start, n->span.end); break;
    case SkelKind::Quote: appendf(out, "quote @[%u,%u)\n", n->span.start, n->span.end); break;
    case SkelKind::Fence: {
      appendf(out, "fence @[%u,%u) lang=\"", n->span.start, n->span.end);
      appendEscaped(out, src.slice(n->langSpan));
      appendf(out, "\" lines=%zu\n", n->lineSpans.size());
      break;
    }
    case SkelKind::Rule: appendf(out, "rule @[%u,%u)\n", n->span.start, n->span.end); break;
    case SkelKind::CodeLet:
    case SkelKind::CodeBlock:
      appendf(out, "%s @[%u,%u) js=\"", n->kind == SkelKind::CodeLet ? "code-let" : "code-block",
              n->span.start, n->span.end);
      appendEscaped(out, src.slice(n->inner));
      out += "\"\n";
      break;
    case SkelKind::Comment:
      appendf(out, "comment @[%u,%u)\n", n->span.start, n->span.end);
      break;
    case SkelKind::Region:
      appendf(out, "region @[%u,%u) name=\"", n->span.start, n->span.end);
      appendEscaped(out, src.slice(n->langSpan));
      out += "\"\n";
      break;
  }
  for (const SkelNode* k : n->kids) dumpNode(out, k, src, depth + 1);
}

std::string dumpSkeleton(const Skeleton& sk, const SourceText& src) {
  std::string out;
  if (sk.root) dumpNode(out, sk.root, src, 0);
  return out;
}

}  // namespace tsr
