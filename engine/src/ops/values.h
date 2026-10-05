// Value-domain validators for the ops reader (plan P0-06; schema.json domains).
// Paint values reach the HTML style attribute verbatim, so a value that is
// not exactly a color / font list / language tag is rejected at decode:
// this is where CSS injection ('red;letter-spacing:5px') is closed (I2).
#pragma once
#include <string_view>

namespace tsr::values {

inline bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isAlnum(char c) { return isAlpha(c) || isDigit(c); }
inline bool hasControl(std::string_view s) {
  for (unsigned char c : s)
    if (c < 0x20 || c == 0x7F) return true;
  return false;
}

// [A-Za-z_][A-Za-z0-9_-]*, at most 64 bytes (roles, error codes)
inline bool isIdent(std::string_view s) {
  if (s.empty() || s.size() > 64) return false;
  if (!isAlpha(s[0]) && s[0] != '_') return false;
  for (char c : s)
    if (!isAlnum(c) && c != '_' && c != '-') return false;
  return true;
}

// labels: non-empty, at most 512 bytes, no control characters
inline bool isLabel(std::string_view s) { return !s.empty() && s.size() <= 512 && !hasControl(s); }

// BCP-47 shape: alpha{2,8} ( '-' alnum{1,8} )*
inline bool isLang(std::string_view s) {
  if (s.empty() || s.size() > 64) return false;
  size_t i = 0, n = 0;
  while (i < s.size() && isAlpha(s[i])) { i++; n++; }
  if (n < 2 || n > 8) return false;
  while (i < s.size()) {
    if (s[i] != '-') return false;
    i++;
    n = 0;
    while (i < s.size() && isAlnum(s[i])) { i++; n++; }
    if (n < 1 || n > 8) return false;
  }
  return true;
}

// "3", "3,5-7": positive line numbers and ranges, blanks allowed around ',' '-'
inline bool isRangeSet(std::string_view s) {
  size_t i = 0;
  auto blanks = [&] { while (i < s.size() && s[i] == ' ') i++; };
  auto num = [&] {
    size_t a = i;
    while (i < s.size() && isDigit(s[i])) i++;
    return i > a && i - a <= 7;
  };
  blanks();
  for (;;) {
    if (!num()) return false;
    blanks();
    if (i < s.size() && s[i] == '-') {
      i++;
      blanks();
      if (!num()) return false;
      blanks();
    }
    if (i == s.size()) return true;
    if (s[i] != ',') return false;
    i++;
    blanks();
  }
}

// CSS color: #rgb #rgba #rrggbb #rrggbbaa, a named color word, var(--name),
// or rgb()/rgba()/hsl()/hsla() over numbers, percentages, '.', ',', '/', blanks.
inline bool isColor(std::string_view s) {
  if (s.empty() || s.size() > 64) return false;
  if (s[0] == '#') {
    size_t n = s.size() - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    for (size_t i = 1; i < s.size(); i++) {
      char c = s[i];
      if (!isDigit(c) && !((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
  }
  if (s.substr(0, 6) == "var(--") {
    if (s.back() != ')' || s.size() < 8) return false;
    for (size_t i = 6; i + 1 < s.size(); i++)
      if (!isAlnum(s[i]) && s[i] != '-' && s[i] != '_') return false;
    return true;
  }
  for (std::string_view f : {"rgb(", "rgba(", "hsl(", "hsla("}) {
    if (s.substr(0, f.size()) == f) {
      if (s.back() != ')') return false;
      for (size_t i = f.size(); i + 1 < s.size(); i++) {
        char c = s[i];
        if (!isDigit(c) && c != '.' && c != ',' && c != '/' && c != ' ' && c != '%' &&
            c != '-' && c != 'd' && c != 'e' && c != 'g')  // 120deg
          return false;
      }
      return true;
    }
  }
  for (char c : s)  // a named color: letters only (red, currentColor, transparent)
    if (!isAlpha(c)) return false;
  return true;
}

// font family list: comma-separated names; a name is a "quoted string" (no
// quotes or backslashes inside) or a run of letters, digits, blanks, '-' and
// non-ASCII (CJK family names). No ; { } < > \ ( ) or control characters.
inline bool isFontList(std::string_view s) {
  if (s.empty() || s.size() > 512 || hasControl(s)) return false;
  size_t i = 0;
  for (;;) {
    while (i < s.size() && s[i] == ' ') i++;
    if (i >= s.size()) return false;
    if (s[i] == '"' || s[i] == '\'') {
      char q = s[i++];
      size_t a = i;
      while (i < s.size() && s[i] != q) {
        if (s[i] == '\\' || s[i] == '"' || s[i] == '\'') return false;
        i++;
      }
      if (i >= s.size() || i == a) return false;
      i++;
    } else {
      size_t a = i;
      while (i < s.size() && s[i] != ',') {
        unsigned char c = (unsigned char)s[i];
        if (!(isAlnum((char)c) || c == ' ' || c == '-' || c == '_' || c >= 0x80)) return false;
        i++;
      }
      if (i == a) return false;
    }
    while (i < s.size() && s[i] == ' ') i++;
    if (i == s.size()) return true;
    if (s[i] != ',') return false;
    i++;
  }
}

}  // namespace tsr::values
