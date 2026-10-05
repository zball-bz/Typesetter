// Splice lexer: bracket-balanced scanning over standard JS (v2 Appendix A).
// Pure state machine — strings, templates (${} nesting), comments, brackets.
// Regex literals are forbidden by the language spec, so '/' is an ordinary char.
#pragma once
#include "../support/support.h"

namespace tsr {

struct JsScan {
  bool ok = false;
  u32 end = 0;             // one past the last consumed byte
  bool hitSemicolon = false;  // ToEol mode: terminated by ';' (consumed)
  const char* err = nullptr;
};

namespace jslex_detail {
enum Frame : u8 { Paren, Bracket, Brace, TemplateExpr, Template };
}

// mode Balanced: src[pos] must be one of ( [ { — scans to the matching closer.
// mode ToEol: scans until '\n' or ';' at depth 0 (v2 §3 rule 4).
inline JsScan scanJs(std::string_view src, u32 pos, bool balancedMode) {
  using namespace jslex_detail;
  JsScan r;
  std::vector<u8> st;
  u32 i = pos;
  auto isOpen = [](char c) { return c == '(' || c == '[' || c == '{'; };
  if (balancedMode) {
    if (i >= src.size() || !isOpen(src[i])) { r.err = "expected bracket"; return r; }
  }
  while (i < src.size()) {
    char c = src[i];
    // template-literal scanning state
    if (!st.empty() && st.back() == Template) {
      if (c == '\\') { i += 2; continue; }
      if (c == '`') { st.pop_back(); i++; goto after; }
      if (c == '$' && i + 1 < src.size() && src[i + 1] == '{') { st.push_back(TemplateExpr); i += 2; continue; }
      i++;
      continue;
    }
    if (c == '\'' || c == '"') {
      char q = c;
      i++;
      while (i < src.size()) {
        if (src[i] == '\\') { i += 2; continue; }
        if (src[i] == q) { i++; break; }
        if (src[i] == '\n') break;  // unterminated string; be forgiving
        i++;
      }
      goto after;
    }
    if (c == '`') { st.push_back(Template); i++; continue; }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      while (i < src.size() && src[i] != '\n') i++;
      continue;  // newline handled below
    }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) i++;
      i = (i + 1 < src.size()) ? i + 2 : (u32)src.size();
      continue;
    }
    if (c == '(') { st.push_back(Paren); i++; continue; }
    if (c == '[') { st.push_back(Bracket); i++; continue; }
    if (c == '{') { st.push_back(Brace); i++; continue; }
    if (c == ')' || c == ']' || c == '}') {
      u8 want = c == ')' ? Paren : c == ']' ? Bracket : Brace;
      if (!st.empty() && st.back() == want) st.pop_back();
      else if (!st.empty() && st.back() == TemplateExpr && c == '}') st.pop_back();  // back into template
      else { r.err = "unbalanced bracket"; r.end = i; return r; }
      i++;
      goto after;
    }
    if (!balancedMode && st.empty()) {
      if (c == '\n') { r.ok = true; r.end = i; return r; }
      if (c == ';') { r.ok = true; r.end = i + 1; r.hitSemicolon = true; return r; }
    }
    i++;
    continue;
  after:
    if (balancedMode && st.empty()) { r.ok = true; r.end = i; return r; }
    continue;
  }
  if (!balancedMode && st.empty()) { r.ok = true; r.end = i; return r; }
  r.err = "unterminated";
  r.end = i;
  return r;
}

inline bool isIdentStart(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}
inline bool isIdentCont(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }

// Scans a bare-splice head chain starting after '#': ident (.ident | (...))*
// (content args [] arrive in M2). Returns end, or start if not a valid head.
inline u32 scanSpliceHead(std::string_view src, u32 pos) {
  u32 i = pos;
  if (i >= src.size() || !isIdentStart(src[i])) return pos;
  while (i < src.size() && isIdentCont(src[i])) i++;
  for (;;) {
    if (i + 1 < src.size() && src[i] == '.' && isIdentStart(src[i + 1])) {
      i += 2;
      while (i < src.size() && isIdentCont(src[i])) i++;
      continue;
    }
    if (i < src.size() && src[i] == '(') {
      JsScan s = scanJs(src, i, /*balanced=*/true);
      if (!s.ok) return i;  // truncate at the broken call — caller reports
      i = s.end;
      continue;
    }
    break;
  }
  return i;
}

// --- top-level token helpers (plan P0-05: statement framing and hygiene) ---
// Walks `src`, skipping strings, templates and comments, and calls
// fn(kind, start, end, depth) for every identifier ('i') and every ',' / '='
// ('p') token. depth counts open ( [ { brackets.
template <class Fn>
inline void jsTokens(std::string_view src, Fn&& fn) {
  int depth = 0;
  u32 i = 0;
  const u32 n = (u32)src.size();
  while (i < n) {
    char c = src[i];
    if (c == '\'' || c == '"') {
      char q = c;
      i++;
      while (i < n && src[i] != q) i += src[i] == '\\' ? 2 : 1;
      i++;
      continue;
    }
    if (c == '`') {  // template: skip text, recurse into ${…} by depth counting
      i++;
      int inner = 0;
      while (i < n) {
        if (src[i] == '\\') { i += 2; continue; }
        if (inner == 0 && src[i] == '`') { i++; break; }
        if (src[i] == '$' && i + 1 < n && src[i + 1] == '{') { inner++; i += 2; continue; }
        if (inner > 0 && src[i] == '}') inner--;
        i++;
      }
      continue;
    }
    if (c == '/' && i + 1 < n && src[i + 1] == '/') {
      while (i < n && src[i] != '\n') i++;
      continue;
    }
    if (c == '/' && i + 1 < n && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) i++;
      i += 2;
      continue;
    }
    if (c == '(' || c == '[' || c == '{') { depth++; i++; continue; }
    if (c == ')' || c == ']' || c == '}') { depth--; i++; continue; }
    if (isIdentStart(c)) {
      u32 s = i;
      while (i < n && isIdentCont(src[i])) i++;
      fn('i', s, i, depth);
      continue;
    }
    if (c >= '0' && c <= '9') {
      while (i < n && (isIdentCont(src[i]) || src[i] == '.')) i++;
      continue;
    }
    if (c == ',' || c == '=') fn('p', i, i + 1, depth);
    i++;
  }
}

// `#let name = expr` with a single identifier and no top-level comma: returns
// the identifier span and the offset where expr starts; ok=false otherwise
// (patterns and multiple declarators stay verbatim, plan D-I10).
struct JsSimpleLet {
  bool ok = false;
  u32 identStart = 0, identEnd = 0, exprStart = 0;
};
inline JsSimpleLet jsSimpleLet(std::string_view s) {
  JsSimpleLet r;
  u32 i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i >= s.size() || !isIdentStart(s[i])) return r;
  r.identStart = i;
  while (i < s.size() && isIdentCont(s[i])) i++;
  r.identEnd = i;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i >= s.size() || s[i] != '=') return r;
  if (i + 1 < s.size() && (s[i + 1] == '=' || s[i + 1] == '>')) return r;
  r.exprStart = i + 1;
  bool comma = false;
  jsTokens(s.substr(r.exprStart), [&](char k, u32 a, u32, int depth) {
    if (k == 'p' && depth == 0 && s[r.exprStart + a] == ',') comma = true;
  });
  r.ok = !comma;
  return r;
}

// Does a statement block declare bindings at its top level (let/const/var/
// function/class)? Declaring blocks run unframed (plan D-I10).
inline bool jsDeclares(std::string_view s) {
  bool decl = false;
  jsTokens(s, [&](char k, u32 a, u32 b, int depth) {
    if (k != 'i' || depth != 0) return;
    std::string_view w = s.substr(a, b - a);
    if (w == "let" || w == "const" || w == "var" || w == "function" || w == "class") decl = true;
  });
  return decl;
}

// First identifier starting with "__" in a binding position: the pattern of a
// #let (before its first top-level '='), or the names that follow
// let/const/var/function/class at the top level of a block. Empty = none.
inline std::string_view jsReservedBinding(std::string_view s, bool isLet) {
  std::string_view found;
  bool binding = isLet;  // a #let starts in binding position
  int bindDepth = 0;
  jsTokens(s, [&](char k, u32 a, u32 b, int depth) {
    if (!found.empty()) return;
    std::string_view w = s.substr(a, b - a);
    if (k == 'p') {
      if (w == "=" && depth == bindDepth) binding = false;
      return;
    }
    if (binding && w.size() >= 2 && w[0] == '_' && w[1] == '_') { found = w; return; }
    if (depth == 0 && (w == "let" || w == "const" || w == "var" || w == "function" || w == "class")) {
      binding = true;
      bindDepth = 0;
    }
  });
  return found;
}

// A region header / fence info argument list must be named entries only:
// `key: value`, "key": value, shorthand `key`, or ...spread (plan P0-05).
// Returns false for positional entries such as `3` or `a b`.
inline bool jsNamedArgList(std::string_view s) {
  // split at top-level commas
  std::vector<std::pair<u32, u32>> parts;
  u32 start = 0;
  jsTokens(s, [&](char k, u32 a, u32, int depth) {
    if (k == 'p' && depth == 0 && s[a] == ',') { parts.push_back({start, a}); start = a + 1; }
  });
  parts.push_back({start, (u32)s.size()});
  for (auto [a, b] : parts) {
    std::string_view e = s.substr(a, b - a);
    size_t x = 0, y = e.size();
    while (x < y && (e[x] == ' ' || e[x] == '\t' || e[x] == '\n' || e[x] == '\r')) x++;
    while (y > x && (e[y - 1] == ' ' || e[y - 1] == '\t' || e[y - 1] == '\n' || e[y - 1] == '\r')) y--;
    e = e.substr(x, y - x);
    if (e.empty()) continue;  // trailing comma / empty list
    if (e.substr(0, 3) == "...") continue;
    size_t p = 0;
    if (e[0] == '"' || e[0] == '\'') {
      char q = e[0];
      p = 1;
      while (p < e.size() && e[p] != q) p += e[p] == '\\' ? 2 : 1;
      p++;
    } else if (isIdentStart(e[0])) {
      while (p < e.size() && isIdentCont(e[p])) p++;
    } else {
      return false;
    }
    while (p < e.size() && (e[p] == ' ' || e[p] == '\t')) p++;
    if (p == e.size()) {
      if (e[0] == '"' || e[0] == '\'') return false;  // "x" alone is positional
      continue;                                     // shorthand
    }
    if (e[p] != ':') return false;
  }
  return true;
}

}  // namespace tsr
