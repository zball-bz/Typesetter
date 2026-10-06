#include "lexer.h"

#include "../inline/jslex.h"

namespace tsr {

bool lexCodeSpan(std::string_view t, u32 i, CodeSpanLex& out) {
  const u32 n = (u32)t.size();
  u32 run = 0;
  while (i + run < n && t[i + run] == '`') run++;
  out.run = run;
  for (u32 p = i + run; p < n;) {
    if (t[p] != '`') {
      p++;
      continue;
    }
    u32 r = 0;
    while (p + r < n && t[p + r] == '`') r++;
    if (r == run) {
      out.bodyStart = i + run;
      out.bodyEnd = p;
      out.end = p + r;
      return true;
    }
    p += r;
  }
  return false;
}

std::string codeSpanText(std::string_view body) {
  std::string s(body);
  for (char& c : s)
    if (c == '\n') c = ' ';
  bool allSpace = true;
  for (char c : s) allSpace = allSpace && c == ' ';
  if (s.size() >= 2 && s.front() == ' ' && s.back() == ' ' && !allSpace) s = s.substr(1, s.size() - 2);
  return s;
}

bool lexMath(std::string_view t, u32 i, u32& close) {
  const u32 n = (u32)t.size();
  u32 p = i + 1;
  while (p < n && t[p] != '$') {
    if (t[p] == '\\' && p + 1 < n) p++;
    p++;
  }
  if (p >= n || p == i + 1) return false;
  close = p;
  return true;
}

std::string mathText(std::string_view body) {
  std::string s;
  s.reserve(body.size());
  for (size_t k = 0; k < body.size(); k++) {
    if (body[k] == '\\' && k + 1 < body.size() && body[k + 1] == '$') {
      s += '$';
      k++;
      continue;
    }
    s += body[k];
  }
  return s;
}

bool lexComment(std::string_view t, u32 i, u32& end) {
  const u32 n = (u32)t.size();
  u32 p = i + 3;
  int depth = 1;
  while (p < n) {
    if (p + 2 < n && t[p] == '%' && t[p + 1] == '-' && t[p + 2] == '-') {
      depth++;
      p += 3;
      continue;
    }
    if (p + 2 < n && t[p] == '-' && t[p + 1] == '-' && t[p + 2] == '%') {
      p += 3;
      if (--depth == 0) {
        end = p;
        return true;
      }
      continue;
    }
    p++;
  }
  return false;
}

bool lexSplice(std::string_view t, u32 hash, SpliceLex& out) {
  const u32 n = (u32)t.size();
  u32 p = hash + 1;
  out = {};
  if (p < n && t[p] == '(') {
    JsScan s = scanJs(t, p, true);
    out.paren = true;
    out.jsEnd = std::min(s.end, n);
    if (!s.ok) {
      if (s.err && std::string_view(s.err) == "unterminated") out.openAt = p;
      return false;
    }
    out.end = s.end;
    return true;
  }
  if (p >= n || !isSpliceHead(t[p])) return false;
  while (p < n && isSpliceCont(t[p])) p++;
  for (;;) {
    if (p + 1 < n && t[p] == '.' && isSpliceHead(t[p + 1])) {
      p += 2;
      while (p < n && isSpliceCont(t[p])) p++;
      out.lastCall = 0;
      continue;
    }
    if (p < n && t[p] == '(') {
      JsScan s = scanJs(t, p, true);
      if (!s.ok) {  // the chain ends before a broken call
        if (s.err && std::string_view(s.err) == "unterminated") out.openAt = p;
        break;
      }
      out.lastCall = p;
      p = s.end;
      continue;
    }
    break;
  }
  out.end = p;
  return true;
}

namespace {
bool isBlank(char c) { return c == ' ' || c == '\t'; }
u32 skipBlank(std::string_view t, u32 p) {
  while (p < t.size() && isBlank(t[p])) p++;
  return p;
}
// a word at p followed by no identifier character: one past it, else 0
u32 word(std::string_view t, u32 p, std::string_view w) {
  if (t.substr(p, w.size()) != w) return 0;
  const u32 e = p + (u32)w.size();
  return e < t.size() && isSpliceCont(t[e]) ? 0 : e;
}
// '(' JS ')' Blank* '[' at p: the JS's span and the '['
bool parenThenBody(std::string_view t, u32 p, u32& hs, u32& he, u32& open, u32& openAt) {
  if (p >= t.size() || t[p] != '(') return false;
  JsScan s = scanJs(t, p, true);
  if (!s.ok) {
    if (s.err && std::string_view(s.err) == "unterminated") openAt = p;
    return false;
  }
  hs = p + 1;
  he = s.end - 1;
  const u32 q = skipBlank(t, s.end);
  if (q >= t.size() || t[q] != '[') return false;
  open = q;
  return true;
}
}  // namespace

bool lexKeywordHead(std::string_view t, u32 hash, KwHead& out) {
  out = {};
  u32 p = hash + 1, e = p;
  while (e < t.size() && isSpliceCont(t[e])) e++;
  const int kw = keywordIndex(t.substr(p, e - p));
  if (kw < 0) return false;
  out.kw = kw;
  return parenThenBody(t, skipBlank(t, e), out.headStart, out.headEnd, out.bodyOpen, out.openAt);
}

bool lexElse(std::string_view t, u32 p, KwElse& out) {
  out = {};
  p = skipBlank(t, p);
  const u32 e = word(t, p, "else");
  if (!e) return false;
  p = skipBlank(t, e);
  if (p < t.size() && t[p] == '[') {
    out.bodyOpen = p;
    return true;
  }
  const u32 f = word(t, p, "if");
  if (!f) return false;
  out.cond = true;
  return parenThenBody(t, skipBlank(t, f), out.headStart, out.headEnd, out.bodyOpen, out.openAt);
}

u32 atomEnd(std::string_view t, u32 i) {
  const u32 n = (u32)t.size();
  switch (inlineOpener(t, i)) {
    case InlineRule::code: {
      CodeSpanLex c;
      return lexCodeSpan(t, i, c) ? c.end : i;
    }
    case InlineRule::math: {
      u32 close;
      return lexMath(t, i, close) ? close + 1 : i;
    }
    case InlineRule::comment: {
      u32 end;
      return lexComment(t, i, end) ? end : i;
    }
    case InlineRule::splice: {
      SpliceLex s;
      return lexSplice(t, i, s) ? s.end : i;
    }
    case InlineRule::none:
      if (t[i] == '\\' && i + 1 < n && t[i + 1] != '\n') return i + 2;
      return i;
    case InlineRule::strong:
    case InlineRule::em:
    case InlineRule::link:
    case InlineRule::note:
    case InlineRule::ref:
    case InlineRule::refs:
      return i;
  }
  return i;
}

i32 BracketMatcher::match(u32 open, Mode m) {
  auto& memo = memo_[m];
  if (auto it = memo.find(open); it != memo.end()) return it->second;
  // one scan from `open`: every bracket pair closed on the way is recorded,
  // and so is every opener left unclosed at the end of the view
  const u32 n = (u32)t_.size();
  std::vector<u32> st{open};
  u32 p = open + 1;
  while (p < n && !st.empty()) {
    char c = t_[p];
    if (m == IslandAware) {
      u32 e = atomEnd(t_, p);
      if (e > p) {
        p = e;
        continue;
      }
      if (c == '`') {  // an unclosed backtick run is literal as a whole
        while (p < n && t_[p] == '`') p++;
        continue;
      }
    } else if (c == '\\' && p + 1 < n && t_[p + 1] != '\n') {
      p += 2;
      continue;
    }
    if (c == '[') {
      st.push_back(p);
    } else if (c == ']') {
      memo[st.back()] = (i32)p;
      st.pop_back();
    }
    p++;
  }
  for (u32 o : st) memo[o] = -1;
  return memo[open];
}

}  // namespace tsr
