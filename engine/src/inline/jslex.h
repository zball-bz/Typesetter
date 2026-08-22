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

}  // namespace tsr
