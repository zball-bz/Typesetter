// The math vocabulary API (plan P1-22; design T8 MathDict): every spelling
// resolves to one symbol of a font-independent table generated from
// engine/data/math/symbols.tsv (tools/mathdict.py). Font coverage is not a
// vocabulary concern: the font artifact (mathfont.h) only holds metrics.
#pragma once
#include <string_view>

#include "../../gen/math_dict.h"
#include "../support/support.h"
#include "atom.h"

namespace tsr {

using SymbolInfo = mathdict::Symbol;

struct MathDict {
  // a name, an operator key, AA..ZZ (binary search, strcmp order)
  static const SymbolInfo* byName(std::string_view name) {
    int lo = 0, hi = mathdict::kSymbolCount;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (std::string_view(mathdict::kSymbols[mid].name) < name) lo = mid + 1;
      else hi = mid;
    }
    if (lo < mathdict::kSymbolCount && std::string_view(mathdict::kSymbols[lo].name) == name)
      return &mathdict::kSymbols[lo];
    return nullptr;
  }
  // a bare code point's entry: its default row, or (a negated code point
  // without a row of its own) the class of what it negates
  static const mathdict::CpClass* cpEntry(u32 cp) {
    int lo = 0, hi = mathdict::kCpClassCount;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (mathdict::kCpClasses[mid].cp < cp) lo = mid + 1;
      else hi = mid;
    }
    return lo < mathdict::kCpClassCount && mathdict::kCpClasses[lo].cp == cp ? &mathdict::kCpClasses[lo] : nullptr;
  }
  // the class a bare code point takes; Ord when unknown
  static u8 classOfCp(u32 cp) {
    const mathdict::CpClass* e = cpEntry(cp);
    return e ? e->cls : (u8)kOrd;
  }
  // (plan P3-24; design T8: predicates read only SymbolInfo) a typed code
  // point's symbol — the same as its name's (∑ is sum: large, limits)
  static const SymbolInfo* byCp(u32 cp) {
    const mathdict::CpClass* e = cpEntry(cp);
    return e && e->sym >= 0 ? &mathdict::kSymbols[e->sym] : nullptr;
  }
  // a character some operator key uses (the lexer's maximal munch)
  static bool isOpChar(char c) {
    for (const char* k = mathdict::kOpChars; *k; k++)
      if (*k == c) return true;
    return false;
  }
  // (plan P3-24) a letter or digit in a math alphabet (bb, cal, frak, bold,
  // italic, sans, mono), else the code point itself
  static int alphabet(std::string_view name) {
    for (int a = 0; a < mathdict::kAlphabetCount; a++)
      if (name == mathdict::kAlphabets[a].name) return a;
    return -1;
  }
  static u32 variant(int alphabet, u32 cp) {
    if (alphabet < 0 || alphabet >= mathdict::kAlphabetCount) return cp;
    const int i = cp >= 'A' && cp <= 'Z' ? (int)(cp - 'A') : cp >= 'a' && cp <= 'z' ? 26 + (int)(cp - 'a')
                  : cp >= '0' && cp <= '9' ? 52 + (int)(cp - '0') : -1;
    const u32 v = i >= 0 ? mathdict::kAlphabets[alphabet].cps[i] : 0;
    return v ? v : cp;
  }
  // the longest operator key at `pos` (the lexer's maximal munch over
  // operator characters); its length in `len`, nullptr when none
  static const SymbolInfo* matchOp(std::string_view s, u32 pos, u32& len) {
    const SymbolInfo* best = nullptr;
    len = 0;
    u32 node = 0;
    for (u32 i = pos; i < s.size(); i++) {
      const mathdict::TrieNode& n = mathdict::kOpTrie[node];
      u32 next = 0;
      for (u32 k = n.kid; k < (u32)n.kid + n.nKids; k++)
        if (mathdict::kOpTrie[k].c == s[i]) {
          next = k;
          break;
        }
      if (!next) break;
      node = next;
      if (mathdict::kOpTrie[node].sym >= 0) {
        best = &mathdict::kSymbols[mathdict::kOpTrie[node].sym];
        len = i - pos + 1;
      }
    }
    return best;
  }
  // the precomposed negation of a code point (UCD: cp + U+0338), 0 = none
  static u32 negate(u32 cp) {
    int lo = 0, hi = mathdict::kNegationCount;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (mathdict::kNegations[mid].cp < cp) lo = mid + 1;
      else hi = mid;
    }
    return lo < mathdict::kNegationCount && mathdict::kNegations[lo].cp == cp ? mathdict::kNegations[lo].neg : 0;
  }
};

}  // namespace tsr
