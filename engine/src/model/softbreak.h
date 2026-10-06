// Soft breaks (plan P2-10; design T1 S13 as decided by the integration: on
// the wire a line join inside a paragraph is U+000A in its inline-model text;
// code and verbatim bodies keep their newlines). A soft break reads as a
// space — or as nothing between two characters that join seamlessly
// (TextRules joinsWide: the wide CJK classes, the predicate the parser used
// to apply, unchanged); at a text's edge (markup on the other side, which is
// never wide) it is a space. Resolved once, at instantiation, so every
// consumer — the tree, the semantic page, titles, emit — reads the same text;
// P4-02 moves the decision into the paragraph shaper, with context across
// node edges (TextRules joinsWithoutSpace).
#pragma once
#include <string>
#include <vector>

#include "../shape/textrules.h"

namespace tsr {

// Resolves the soft breaks of `s` in place. `map` is its cooked→raw map
// (pairs: cooked offset, raw offset − span start; empty = the identity over a
// raw slice of `rawLen` bytes) and is rebuilt as the parser builds one: a
// breakpoint wherever the identity breaks, the end when the identity does
// not reach it, nothing when the whole text is its own raw slice. Returns
// whether anything changed.
inline bool resolveSoftBreaks(std::string& s, std::vector<u32>& map, u32 rawLen, bool mapped) {
  if (s.find('\n') == std::string::npos) return false;
  const u32 n = (u32)s.size();
  // each cooked byte's raw offset, and the end
  std::vector<u32> raw(n);
  u32 end = rawLen;
  if (mapped) {
    if (map.empty()) {
      for (u32 k = 0; k < n; k++) raw[k] = k;
    } else {
      size_t b = 0;
      for (u32 k = 0; k < n; k++) {
        while (b + 2 < map.size() && map[b + 2] <= k) b += 2;
        raw[k] = map[b + 1] + (k - map[b]);
      }
      end = map[map.size() - 2] == n ? map.back() : raw[n - 1] + 1;
    }
  }
  std::string out;
  std::vector<u32> outRaw;
  out.reserve(n);
  outRaw.reserve(n);
  for (u32 k = 0; k < n; k++) {
    if (s[k] != '\n') {
      out += s[k];
      if (mapped) outRaw.push_back(raw[k]);
      continue;
    }
    bool seamless = false;
    if (k > 0 && k + 1 < n) {
      u32 p = k, q = k + 1;
      const u32 prev = utf8PrevCp(s, p);
      const u32 next = utf8Next(s, q);
      seamless = joinsWide(prev) && joinsWide(next);
    }
    if (!seamless) {
      out += ' ';
      if (mapped) outRaw.push_back(raw[k]);
    }
  }
  s = std::move(out);
  if (!mapped) return true;
  map.clear();
  const u32 m = (u32)s.size();
  for (u32 k = 0; k < m; k++)
    if (k == 0 || outRaw[k] != outRaw[k - 1] + 1) {
      map.push_back(k);
      map.push_back(outRaw[k]);
    }
  if (m == 0 || outRaw[m - 1] + 1 != end) {
    map.push_back(m);
    map.push_back(end);
  }
  if (map.size() == 2 && map[1] == 0 && m == rawLen) map.clear();  // its own raw slice
  return true;
}

}  // namespace tsr
