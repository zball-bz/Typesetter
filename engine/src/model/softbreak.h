// Soft breaks (plan P2-10; design T1 S13 as decided by the integration: on
// the wire a line join inside a paragraph is U+000A in its inline-model text;
// code and verbatim bodies keep their newlines). A soft break reads as
// nothing between two characters that join seamlessly, else as a space —
// TextRules joinsWithoutSpace (plan P4-02), asked of the characters on
// either side in the paragraph's reading order, across node edges
// (`这是*强调*⏎中文` joins: findings parser-owned-cjk-line-join,
// markup-language/cjk-softbreak-classifier), with the ambiguous quotes as
// the paragraph context resolves them (shape/context.h: `他说“好”⏎然后`
// joins). Resolved once, right after the normal form, so every consumer —
// the tree, the semantic page, titles, emit — reads the same text.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../shape/textrules.h"

namespace tsr {

struct ContentNode;
class Interner;
class StyleTable;
class Arena;

// Rewrites the soft breaks of `s`: the k-th one (in order) becomes nothing
// when join(k), else a space. `map` is its cooked→raw map (pairs: cooked
// offset, raw offset − span start; empty = the identity over a raw slice of
// `rawLen` bytes) and is rebuilt as the parser builds one: a breakpoint
// wherever the identity breaks, the end when the identity does not reach
// it, nothing when the whole text is its own raw slice.
inline void rewriteSoftBreaks(std::string& s, std::vector<u32>& map, u32 rawLen, bool mapped,
                              const std::function<bool(u32)>& join) {
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
  u32 nth = 0;
  for (u32 k = 0; k < n; k++) {
    if (s[k] != '\n') {
      out += s[k];
      if (mapped) outRaw.push_back(raw[k]);
      continue;
    }
    if (!join(nth++)) {
      out += ' ';
      if (mapped) outRaw.push_back(raw[k]);
    }
  }
  s = std::move(out);
  if (!mapped) return;
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
}

// The pass: every inline-model text of the tree (instantiation leaves its
// soft breaks in place, a mapped text with an explicit map), resolved with
// its paragraph's context.
void resolveSoftBreaks(ContentNode* root, Arena& arena, Interner& strs, const StyleTable& styles);

}  // namespace tsr
