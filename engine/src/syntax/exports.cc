#include "exports.h"
#include "labels.h"

#include <algorithm>

#include "../support/json.h"

namespace tsr {

namespace {

constexpr u8 kNoTag = 0xFF;
constexpr u8 tagOf(std::string_view name) {
  for (int k = 0; k < kTokenTagCount; k++)
    if (name == kTokenTags[k]) return (u8)k;
  return kNoTag;
}
constexpr u8 kKeyword = tagOf("keyword"), kString = tagOf("string"), kComment = tagOf("comment"),
             kFunction = tagOf("function"), kType = tagOf("type"), kConstant = tagOf("constant"),
             kOperator = tagOf("operator"), kPunct = tagOf("punctuation"),
             kProperty = tagOf("property"), kAttribute = tagOf("attribute"), kLabel = tagOf("label"),
             kEmbedded = tagOf("embedded");
static_assert(kEmbedded != kNoTag && kKeyword != kNoTag);

// A scratch parse of a source (the stateless entry points).
struct Scratch {
  SourceText src;
  Arena arena;
  Interner strs{arena};
  DiagSink diags;
  AstNode* root = nullptr;
  explicit Scratch(std::string_view text) {
    src.init(std::string(text));
    Skeleton sk = linepass(src, arena, diags);
    root = parseDoc(src, sk, arena, strs, diags);
  }
};

struct Lines {
  std::string_view all;
  u32 endAt(u32 p) const {  // end of p's line, terminator excluded
    u32 s = p;
    while (p < all.size() && all[p] != '\n') p++;
    if (p > s && all[p - 1] == '\r') p--;
    return p;
  }
  u32 startAt(u32 p) const {
    while (p > 0 && all[p - 1] != '\n') p--;
    return p;
  }
  bool blank(char c) const { return c == ' ' || c == '\t'; }
};

// --- tokens ------------------------------------------------------------------
// The markers and atoms of every construct, from the AST and the source:
// markers keyword, splice heads and statements function, code string, math
// type, references constant, labels label, emphasis and notes attribute,
// links property, cell bars operator, rules punctuation, fence bodies
// embedded, comments comment.
struct TokenWalk : Lines {
  const Interner& strs;
  std::vector<CodeToken> out;

  TokenWalk(std::string_view a, const Interner& s) : Lines{a}, strs(s) {}

  void tok(u32 s, u32 e, u8 tag) {
    if (tag != kNoTag && s < e && e <= all.size()) out.push_back({s, e, tag});
  }
  void kids(const AstNode* n, u8 textTag, u32 qd) {
    for (const AstNode* k : n->kids()) walk(k, textTag, qd);
  }

  void walk(const AstNode* n, u8 textTag, u32 qd) {
    const Span sp = n->span;
    switch (n->kind) {
      case AstKind::Doc:
        kids(n, kNoTag, qd);
        return;
      case AstKind::Text:
        tok(sp.start, sp.end, textTag);
        return;
      case AstKind::Comment:
        tok(sp.start, sp.end, kComment);
        return;
      case AstKind::Stmt:
        tok(sp.start, sp.end, kFunction);
        return;
      case AstKind::Error:
        return;
      case AstKind::Splice:
        splice(n, qd);
        return;
      case AstKind::Call:
        call(n, textTag, qd);
        return;
    }
  }

  // '#' and its identifier chain: function; call arguments: embedded;
  // content arguments' brackets: punctuation
  void splice(const AstNode* n, u32 qd) {
    const u32 s = n->span.start;
    u32 headEnd = n->nkids ? n->kids()[0]->span.start - 1 : n->span.end;
    if (!n->nkids && headEnd > s + 1 && all[headEnd - 1] == ';') headEnd--;
    u32 p = s + 1;
    while (p < headEnd && (isSpliceCont(all[p]) || all[p] == '.')) p++;
    tok(s, p, kFunction);
    tok(p, headEnd, kEmbedded);
    for (const AstNode* a : n->kids()) {
      tok(a->span.start - 1, a->span.start, kPunct);
      kids(a, kNoTag, qd);
      tok(a->span.end, a->span.end + 1, kPunct);
    }
  }

  // the '>' of a quote on each of its lines (after the prefixes of the
  // quotes around it and any indentation)
  void quoteMarks(const AstNode* n, u32 qd) {
    tok(n->span.start, n->span.start + 1, kKeyword);
    u32 ls = n->span.start;
    for (;;) {
      while (ls < all.size() && all[ls] != '\n') ls++;
      if (++ls >= n->span.end) break;
      u32 p = ls;
      for (u32 k = 0; k <= qd; k++) {
        while (p < n->span.end && blank(all[p])) p++;
        if (p >= n->span.end || all[p] != '>') break;
        if (k == qd) tok(p, p + 1, kKeyword);
        p++;
      }
    }
  }

  void fence(const AstNode* n) {
    const FenceP& f = side<FenceP>(n);
    const Span sp = n->span;
    opener(sp.start, kKeyword);
    // the closer: the last line of the span when it is a backtick run
    u32 bodyEnd = sp.end;
    u32 ls = startAt(sp.end);
    if (ls > sp.start) {
      u32 p = ls;
      while (p < sp.end && (blank(all[p]) || all[p] == '>')) p++;
      u32 q = p;
      while (q < sp.end && all[q] == '`') q++;
      u32 r = q;
      while (r < sp.end && (blank(all[r]) || all[r] == '\r')) r++;
      if (q - p >= 3 && r == sp.end) {
        tok(p, q, kKeyword);
        bodyEnd = ls > 0 ? ls - 1 : ls;
      }
    }
    std::string_view lines = f.lines ? strs.get(f.lines) : std::string_view{};
    if (!lines.empty()) {  // a contained fence: one token per body line
      u32 v = 0;
      for (char c : lines) {
        if (c >= '0' && c <= '9') {
          v = v * 10 + (u32)(c - '0');
        } else if (c == ',' || c == ']') {
          tok(v, std::min(endAt(v), bodyEnd), kEmbedded);
          v = 0;
        }
      }
    } else if (!strs.get(n->str).empty()) {
      tok(f.bodyOffset, bodyEnd, kEmbedded);
    }
  }

  // an opener line: its token, and its ` <id>` suffix as a label (plan P2-06)
  void opener(u32 s, u8 tag) {
    const u32 e = endAt(s);
    const LabelSuffix ls = trailingLabel(all, s, e);
    if (!ls.ok) {
      tok(s, e, tag);
      return;
    }
    tok(s, ls.textEnd, tag);
    tok(ls.labelStart - 1, ls.labelEnd + 1, kLabel);
  }

  void region(const AstNode* n, u32 qd) {
    const Span sp = n->span;
    opener(sp.start, kFunction);
    u32 ls = startAt(sp.end);
    if (ls > sp.start) {
      u32 p = ls;
      while (p < sp.end && (blank(all[p]) || all[p] == '>')) p++;
      if (p + 1 < sp.end && all[p] == '#' && all[p + 1] != '!') {
        u32 q = p + 1;
        while (q < sp.end && isSpliceCont(all[q])) q++;
        if (q < sp.end && all[q] == '!') tok(p, q + 1, kFunction);
      }
    }
    for (const AstNode* k : n->kids()) {
      if (!k->isCall(SugarId::para)) {
        walk(k, kNoTag, qd);
        continue;
      }
      // a paragraph's cell cuts (plan P2-11: its Text nodes' seps) are bars
      for (const AstNode* x : k->kids()) {
        if (x->kind == AstKind::Text)
          for (u32 c : numbers(strs.get(side<TextP>(x).seps))) {
            const u32 r = x->span.start + rawOf(strs.get(side<TextP>(x).rawmap), c);
            tok(r, r + 1, kOperator);
          }
        walk(x, kNoTag, qd);
      }
    }
  }
  // "a,b,…" / "c:r,…" → the numbers
  static std::vector<u32> numbers(std::string_view m) {
    std::vector<u32> out;
    u32 v = 0;
    bool digit = false;
    for (char ch : m) {
      if (ch >= '0' && ch <= '9') {
        v = v * 10 + (u32)(ch - '0');
        digit = true;
      } else if (digit) {
        out.push_back(v);
        v = 0;
        digit = false;
      }
    }
    if (digit) out.push_back(v);
    return out;
  }
  // a cooked offset's raw offset (relative to the span start) through a
  // Text's cooked→raw map (empty: the identity)
  static u32 rawOf(std::string_view map, u32 c) {
    const std::vector<u32> m = numbers(map);
    u32 r = c;
    for (size_t k = 0; k + 1 < m.size() && m[k] <= c; k += 2) r = m[k + 1] + (c - m[k]);
    return r;
  }

  void call(const AstNode* n, u8 textTag, u32 qd) {
    const Span sp = n->span;
    switch (n->sugar) {
      case SugarId::para:
      case SugarId::arg:
        kids(n, textTag, qd);
        return;
      case SugarId::heading: {
        const HeadingP& h = side<HeadingP>(n);
        tok(sp.start, sp.start + h.level, kKeyword);
        kids(n, kFunction, qd);
        if (h.label) {  // the " <label>" after the heading text
          u32 p = sp.end;
          while (p < all.size() && blank(all[p])) p++;
          if (p < all.size() && all[p] == '<')
            tok(p, p + (u32)strs.get(h.label).size() + 2, kLabel);
        }
        return;
      }
      case SugarId::list:
        kids(n, kNoTag, qd);
        return;
      case SugarId::item: {
        u32 m = sp.start;
        if (all[m] == '-' || all[m] == '+') {
          m++;
        } else {
          while (m < sp.end && all[m] >= '0' && all[m] <= '9') m++;
          if (m < sp.end && all[m] == '.') m++;
        }
        tok(sp.start, m, kKeyword);
        kids(n, kNoTag, qd);
        return;
      }
      case SugarId::quote:
        quoteMarks(n, qd);
        kids(n, kNoTag, qd + 1);
        return;
      case SugarId::rule:
        tok(sp.start, sp.end, kPunct);
        return;
      case SugarId::fence:
        fence(n);
        return;
      case SugarId::region:
        region(n, qd);
        return;
      case SugarId::strong:
      case SugarId::em:
        tok(sp.start, sp.start + 1, kAttribute);
        kids(n, kAttribute, qd);
        tok(sp.end - 1, sp.end, kAttribute);
        return;
      case SugarId::code:
        tok(sp.start, sp.end, kString);
        return;
      case SugarId::link: {
        u32 urlLen = (u32)strs.get(side<LinkP>(n).url).size();
        tok(sp.start, sp.start + 1, kProperty);
        kids(n, kProperty, qd);
        tok(sp.end - urlLen - 3, sp.end, kProperty);
        return;
      }
      case SugarId::note:
        tok(sp.start, sp.start + 2, kAttribute);
        kids(n, kAttribute, qd);
        tok(sp.end - 1, sp.end, kAttribute);
        return;
      case SugarId::ref:
        tok(sp.start, sp.end, kConstant);
        return;
      case SugarId::math: {
        const MathP& m = side<MathP>(n);
        if (!m.label) {
          // an inline formula's ` <id>` is still a label token (dropped with
          // label-orphan, plan P2-06)
          const LabelSuffix ls = all[sp.end - 1] == '>' ? trailingLabel(all, sp.start, sp.end) : LabelSuffix{};
          if (!ls.ok) {
            tok(sp.start, sp.end, kType);
            return;
          }
          tok(sp.start, ls.textEnd, kType);
          tok(ls.labelStart - 1, ls.labelEnd + 1, kLabel);
          return;
        }
        u32 ls = sp.end - (u32)strs.get(m.label).size() - 2;  // " <label>"
        tok(sp.start, ls - 1, kType);
        tok(ls, sp.end, kLabel);
        return;
      }
    }
  }

  std::vector<CodeToken> finish() {
    std::stable_sort(out.begin(), out.end(),
                     [](const CodeToken& a, const CodeToken& b) { return a.start < b.start; });
    std::vector<CodeToken> res;
    u32 covered = 0;
    for (const CodeToken& t : out)
      if (t.start >= covered) {
        res.push_back(t);
        covered = t.end;
      }
    return res;
  }
};

// --- outline -------------------------------------------------------------------
struct OutlineWalk : Lines {
  const Interner& strs;
  std::string headings, regions, fences, labels;

  OutlineWalk(std::string_view a, const Interner& s) : Lines{a}, strs(s) {}

  static void sep(std::string& out) {
    if (!out.empty()) out += ',';
  }
  static void spanJson(std::string& out, u32 s, u32 e) { appendf(out, "\"span\":[%u,%u]", s, e); }
  void label(std::string_view id, const char* rule, u32 s, u32 e) {
    sep(labels);
    labels += "{\"id\":";
    jsonString(labels, id);
    labels += ",\"rule\":\"";
    labels += rule;
    labels += "\",";
    spanJson(labels, s, e);
    labels += "}";
  }
  // the visible text of inline content
  void text(const AstNode* n, std::string& out) const {
    switch (n->kind) {
      case AstKind::Text:
        out += strs.get(n->str);
        return;
      case AstKind::Call:
        if (n->isCall(SugarId::code) || n->isCall(SugarId::math)) {
          out += strs.get(n->str);
          return;
        }
        if (n->isCall(SugarId::ref)) {
          out += '@';
          out += strs.get(n->str);
          return;
        }
        break;
      case AstKind::Doc:
      case AstKind::Comment:
      case AstKind::Splice:
      case AstKind::Stmt:
      case AstKind::Error:
        break;
    }
    if (n->kind == AstKind::Splice) return;
    for (const AstNode* k : n->kids()) text(k, out);
  }
  // `label: "id"` among a region's arguments
  static std::string argLabel(std::string_view args) {
    size_t p = 0;
    while ((p = args.find("label", p)) != std::string_view::npos) {
      size_t q = p + 5;
      while (q < args.size() && (args[q] == ' ' || args[q] == '\t')) q++;
      if (q < args.size() && args[q] == ':') {
        q++;
        while (q < args.size() && (args[q] == ' ' || args[q] == '\t')) q++;
        if (q < args.size() && (args[q] == '"' || args[q] == '\'')) {
          size_t e = args.find(args[q], q + 1);
          if (e != std::string_view::npos) return std::string(args.substr(q + 1, e - q - 1));
        }
      }
      p += 5;
    }
    return {};
  }

  void walk(const AstNode* n, const SourceText& src) {
    const Span sp = n->span;
    if (n->isCall(SugarId::heading)) {
      const HeadingP& h = side<HeadingP>(n);
      std::string title;
      for (const AstNode* k : n->kids()) text(k, title);
      sep(headings);
      appendf(headings, "{\"level\":%d,\"title\":", h.level);
      jsonString(headings, title);
      headings += ",\"label\":";
      if (h.label) jsonString(headings, strs.get(h.label));
      else headings += "null";
      headings += ",";
      spanJson(headings, sp.start, sp.end);
      headings += "}";
      if (h.label) {
        u32 p = sp.end;
        while (p < all.size() && blank(all[p])) p++;
        label(strs.get(h.label), "heading", p, p + (u32)strs.get(h.label).size() + 2);
      }
    } else if (n->isCall(SugarId::region)) {
      // a label: argument wins over the opener's ` <id>` (plan P2-06)
      std::string lbl = argLabel(src.slice(side<RegionP>(n).args));
      if (lbl.empty() && side<RegionP>(n).label) lbl = strs.get(side<RegionP>(n).label);
      sep(regions);
      regions += "{\"name\":";
      jsonString(regions, strs.get(n->str));
      regions += ",\"label\":";
      if (!lbl.empty()) jsonString(regions, lbl);
      else regions += "null";
      regions += ",";
      spanJson(regions, sp.start, sp.end);
      regions += "}";
      if (!lbl.empty()) label(lbl, "region", sp.start, endAt(sp.start));
    } else if (n->isCall(SugarId::fence)) {
      sep(fences);
      fences += "{\"lang\":";
      jsonString(fences, strs.get(side<FenceP>(n).lang));
      if (side<FenceP>(n).label) {  // ` <id>` (plan P2-06)
        fences += ",\"label\":";
        jsonString(fences, strs.get(side<FenceP>(n).label));
      }
      fences += ",";
      spanJson(fences, sp.start, sp.end);
      fences += "}";
      if (side<FenceP>(n).label) label(strs.get(side<FenceP>(n).label), "fence", sp.start, endAt(sp.start));
    } else if (n->isCall(SugarId::math) && side<MathP>(n).label) {
      std::string_view id = strs.get(side<MathP>(n).label);
      label(id, "math", sp.end - (u32)id.size() - 2, sp.end);
    }
    for (const AstNode* k : n->kids()) walk(k, src);
  }
};

void astJsonRec(std::string& out, const AstNode* n, const SourceText& src, const Interner& strs) {
  out += '{';
  jsonAstNode(out, n, src, strs);
  if (n->nkids) {
    out += ",\"kids\":[";
    bool first = true;
    for (const AstNode* k : n->kids()) {
      if (!first) out += ',';
      first = false;
      astJsonRec(out, k, src, strs);
    }
    out += ']';
  }
  out += '}';
}

}  // namespace

std::vector<CodeToken> syntaxTokens(const AstNode* doc, const SourceText& src, const Interner& strs) {
  TokenWalk w(src.view(), strs);
  if (doc) w.walk(doc, kNoTag, 0);
  return w.finish();
}
std::vector<CodeToken> syntaxTokens(std::string_view source) {
  Scratch s(source);
  return syntaxTokens(s.root, s.src, s.strs);
}

std::string outlineJson(const AstNode* doc, const SourceText& src, const Interner& strs,
                        const DiagSink& diags) {
  OutlineWalk w(src.view(), strs);
  if (doc) w.walk(doc, src);
  std::string out = "{\"headings\":[" + w.headings + "],\"regions\":[" + w.regions +
                    "],\"fences\":[" + w.fences + "],\"labels\":[" + w.labels + "],\"diagnostics\":[";
  bool first = true;
  for (const Diag& d : diags.items) {
    if (!first) out += ',';
    first = false;
    out += "{\"sev\":\"";
    out += d.sev == Sev::Error ? "error" : d.sev == Sev::Warning ? "warning" : "info";
    out += "\",\"code\":";
    jsonString(out, d.code);
    appendf(out, ",\"span\":[%u,%u],\"message\":", d.span.start, d.span.end);
    jsonString(out, d.msg);
    out += '}';
  }
  out += "]}";
  return out;
}
std::string outlineJson(std::string_view source) {
  Scratch s(source);
  return outlineJson(s.root, s.src, s.strs, s.diags);
}

std::string astJson(const AstNode* doc, const SourceText& src, const Interner& strs) {
  std::string out;
  if (doc) astJsonRec(out, doc, src, strs);
  return out;
}
std::string astJson(std::string_view source) {
  Scratch s(source);
  return astJson(s.root, s.src, s.strs);
}

std::string dumpTokens(const std::vector<CodeToken>& toks, const SourceText& src) {
  std::string out;
  for (const CodeToken& t : toks) {
    appendf(out, "[%u,%u) %s \"", t.start, t.end, kTokenTags[t.tag]);
    appendEscaped(out, src.slice({t.start, t.end}));
    out += "\"\n";
  }
  return out;
}

std::string tokensJson(const std::vector<CodeToken>& toks) {
  std::string out = "[";
  for (size_t i = 0; i < toks.size(); i++)
    appendf(out, "%s[%u,%u,%u]", i ? "," : "", toks[i].start, toks[i].end, (unsigned)toks[i].tag);
  out += "]";
  return out;
}

}  // namespace tsr
