// Splice lexer: bracket-balanced scanning over standard JS (v2 Appendix A).
// Pure state machine — strings, templates (${} nesting), comments, brackets.
// Regex literals are forbidden by the language spec, so '/' is an ordinary char.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "../support/support.h"

namespace tsr {

struct JsScan {
  bool ok = false;
  u32 end = 0;             // one past the last consumed byte
  bool hitSemicolon = false;  // ToEol mode: terminated by ';' (consumed)
  const char* err = nullptr;
};

namespace jslex_detail {
enum Frame : u8 { Paren, Bracket, Brace, TmplHole, TmplLit };
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
    if (!st.empty() && st.back() == TmplLit) {
      if (c == '\\') { i += 2; continue; }
      if (c == '`') { st.pop_back(); i++; goto after; }
      if (c == '$' && i + 1 < src.size() && src[i + 1] == '{') { st.push_back(TmplHole); i += 2; continue; }
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
    if (c == '`') { st.push_back(TmplLit); i++; continue; }
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
      else if (!st.empty() && st.back() == TmplHole && c == '}') st.pop_back();  // back into template
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
  // an escape at the very end skips past it: the scan ends at the text's end
  // (fuzz_inline, plan P2-02: a statement's span ran one byte past the source)
  if (i > src.size()) i = (u32)src.size();
  if (!balancedMode && st.empty()) { r.ok = true; r.end = i; return r; }
  r.err = "unterminated";
  r.end = i;
  return r;
}

inline bool isIdentStart(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}
inline bool isIdentCont(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }

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

// Every identifier token of s — outside strings and comments, inside
// template ${…} holes too — as fn(name); fn returns false to stop early.
// Property names (x.await) are included: callers over-approximate.
template <class Fn>
inline void jsIdentsDeep(std::string_view s, Fn&& fn) {
  std::vector<int> holes;  // brace depth at each open template ${ hole
  int depth = 0;
  bool lit = false;  // inside template literal text
  const u32 n = (u32)s.size();
  u32 i = 0;
  while (i < n) {
    char c = s[i];
    if (lit) {
      if (c == '\\') i += 2;
      else if (c == '`') { lit = false; i++; }
      else if (c == '$' && i + 1 < n && s[i + 1] == '{') { holes.push_back(depth++); lit = false; i += 2; }
      else i++;
      continue;
    }
    if (c == '\'' || c == '"') {
      i++;
      while (i < n && s[i] != c && s[i] != '\n') i += s[i] == '\\' ? 2 : 1;
      i++;
      continue;
    }
    if (c == '`') { lit = true; i++; continue; }
    if (c == '/' && i + 1 < n && s[i + 1] == '/') {
      while (i < n && s[i] != '\n') i++;
      continue;
    }
    if (c == '/' && i + 1 < n && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
      i += 2;
      continue;
    }
    if (c == '{') { depth++; i++; continue; }
    if (c == '}') {
      depth--;
      if (!holes.empty() && depth == holes.back()) { holes.pop_back(); lit = true; }
      i++;
      continue;
    }
    if (isIdentStart(c)) {
      u32 a = i;
      while (i < n && isIdentCont(s[i])) i++;
      if (!fn(s.substr(a, i - a))) return;
      continue;
    }
    if (c >= '0' && c <= '9') {
      while (i < n && (isIdentCont(s[i]) || s[i] == '.')) i++;
      continue;
    }
    i++;
  }
}

// Does `word` occur as an identifier anywhere in s (plan P2-02: a hole whose
// code mentions `await` is an async function; over-approximation only makes
// a hole async)?
inline bool jsMentions(std::string_view s, std::string_view word) {
  bool found = false;
  jsIdentsDeep(s, [&](std::string_view id) {
    found = id == word;
    return !found;
  });
  return found;
}

// An ECMAScript reserved word (strict mode and module code): never a
// binding name, so a #let of one is not hoisted (plan P2-02).
inline bool jsReservedWord(std::string_view w) {
  static constexpr std::string_view kWords[] = {
      "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default",
      "delete", "do", "else", "enum", "export", "extends", "false", "finally", "for",
      "function", "if", "implements", "import", "in", "instanceof", "interface", "let", "new",
      "null", "package", "private", "protected", "public", "return", "static", "super",
      "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with",
      "yield", "arguments", "eval"};
  for (std::string_view k : kWords)
    if (k == w) return true;
  return false;
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

// The one argument-list grammar (plan P2-06; design T1 ArgList), for splice
// calls, region headers and fence info alike: the depth-0 items of a list
// (between its parentheses) and its form. An item is
//   Named      PropertyName ':' expr (PropertyName: identifier, string,
//              number or [computed])
//   Spread     '...' expr
//   Shorthand  a lone identifier
//   Positional anything else
// The list is Empty (no item but comments), Named (named, shorthand and
// spread items with at least one named), Mixed (named next to positional),
// else Positional. A named item is never a valid JS argument, so treating a
// named list as one options object keeps every valid JS call's meaning.
struct JsArgItem {
  enum K : u8 { Positional, Named, Shorthand, Spread } k = Positional;
  u32 start = 0, end = 0;  // the item, blanks and comments trimmed
};
struct JsArgList {
  enum Form : u8 { Empty, Positional, Named, Mixed } form = Empty;
  std::vector<JsArgItem> items;
};
inline JsArgList jsArgList(std::string_view s) {
  JsArgList r;
  std::vector<std::pair<u32, u32>> parts;
  u32 start = 0;
  jsTokens(s, [&](char k, u32 a, u32, int depth) {
    if (k == 'p' && depth == 0 && s[a] == ',') {
      parts.push_back({start, a});
      start = a + 1;
    }
  });
  parts.push_back({start, (u32)s.size()});
  // past blanks and comments
  auto skip = [&](u32 p, u32 e) {
    while (p < e) {
      if (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r') p++;
      else if (p + 1 < e && s[p] == '/' && s[p + 1] == '*') {
        p += 2;
        while (p + 1 < e && !(s[p] == '*' && s[p + 1] == '/')) p++;
        p = std::min(e, p + 2);
      } else if (p + 1 < e && s[p] == '/' && s[p + 1] == '/') {
        while (p < e && s[p] != '\n') p++;
      } else break;
    }
    return p;
  };
  bool named = false, positional = false;
  for (auto [a, b] : parts) {
    u32 p = skip(a, b), e = b;
    if (p >= e) continue;  // an empty item: a trailing comma, a comment
    while (e > p && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) e--;
    JsArgItem it;
    it.start = p;
    it.end = e;
    // the property name, if the item opens with one
    u32 q = p;
    if (s.substr(p, 3) == "...") {
      it.k = JsArgItem::Spread;
    } else {
      if (isIdentStart(s[q])) {
        while (q < e && isIdentCont(s[q])) q++;
      } else if (s[q] == '"' || s[q] == '\'') {
        const char quote = s[q++];
        while (q < e && s[q] != quote) q += s[q] == '\\' ? 2 : 1;
        q = std::min(e, q + 1);
      } else if (s[q] >= '0' && s[q] <= '9') {
        while (q < e && (isIdentCont(s[q]) || s[q] == '.')) q++;
      } else if (s[q] == '[') {
        JsScan c = scanJs(s.substr(0, e), q, true);
        q = c.ok ? c.end : q;
      }
      u32 c = q;
      while (c < e && (s[c] == ' ' || s[c] == '\t')) c++;
      if (q > p && c < e && s[c] == ':' && !(c + 1 < e && s[c + 1] == ':')) it.k = JsArgItem::Named;
      else if (q == e && isIdentStart(s[p])) it.k = JsArgItem::Shorthand;
      else it.k = JsArgItem::Positional;
    }
    named = named || it.k == JsArgItem::Named;
    positional = positional || it.k == JsArgItem::Positional;
    r.items.push_back(it);
  }
  r.form = r.items.empty() ? JsArgList::Empty
           : named && positional ? JsArgList::Mixed
           : named ? JsArgList::Named
                   : JsArgList::Positional;
  return r;
}

// a header that is not a named list: the fix-it — every item named (a
// shorthand x as x: x, a positional value under a name to choose)
inline std::string jsNamedFixit(std::string_view s, const JsArgList& l) {
  std::string out = "(";
  for (size_t k = 0; k < l.items.size(); k++) {
    const JsArgItem& it = l.items[k];
    std::string_view t = s.substr(it.start, it.end - it.start);
    if (k) out += ", ";
    if (it.k == JsArgItem::Shorthand) out += std::string(t) + ": " + std::string(t);
    else if (it.k == JsArgItem::Positional) out += "name: " + std::string(t);
    else out += t;
  }
  return out + ")";
}

}  // namespace tsr
