#include "hyphen.h"

#include <algorithm>

#include "../../gen/hyphen_en_us.h"
#include "../shape/textrules.h"

namespace tsr {

namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
}  // namespace

bool HyphenDict::hasWide(u32 cp) const { return std::binary_search(alphabet_.begin(), alphabet_.end(), cp); }

i32 HyphenDict::child(u32 node, u32 cp) const {
  if (node == 0 && cp < 128) return rootAscii_[cp];
  const u32* a = edgeCp_.data() + edgeStart_[node];
  const u32* b = edgeCp_.data() + edgeStart_[node + 1];
  if (b - a <= 8) {
    for (const u32* e = a; e < b; e++)
      if (*e == cp) return (i32)edgeChild_[e - edgeCp_.data()];
    return -1;
  }
  const u32* e = std::lower_bound(a, b, cp);
  return e != b && *e == cp ? (i32)edgeChild_[e - edgeCp_.data()] : -1;
}

void HyphenDict::points(const u32* w, u32 n, std::vector<u32>& out) const {
  if (!exceptions_.empty()) {
    auto it = std::lower_bound(exceptions_.begin(), exceptions_.end(), 0, [&](const auto& e, int) {
      return std::lexicographical_compare(e.first.begin(), e.first.end(), w, w + n);
    });
    if (it != exceptions_.end() && std::equal(it->first.begin(), it->first.end(), w, w + n)) {
      out.insert(out.end(), it->second.begin(), it->second.end());
      return;
    }
  }
  // ".word.": a pattern's levels stand between its letters (levels[j]:
  // before s[j]); the odd maximum at a position is a break
  const u32 m = n + 2;
  u32 sBuf[64];
  u8 lvBuf[65];
  std::vector<u32> sBig;
  std::vector<u8> lvBig;
  u32* s = sBuf;
  u8* lv = lvBuf;
  if (m > 64) {
    sBig.resize(m);
    lvBig.resize(m + 1);
    s = sBig.data();
    lv = lvBig.data();
  }
  s[0] = s[m - 1] = '.';
  std::copy(w, w + n, s + 1);
  std::fill(lv, lv + m + 1, 0);
  for (u32 k = 0; k < m; k++) {
    i32 node = 0;
    for (u32 i = k; i < m; i++) {
      node = child((u32)node, s[i]);
      if (node < 0) break;
      const i32 r = nodeRow_[node];
      if (r < 0) continue;
      for (u32 t = rowStart_[r]; t < rowStart_[r + 1]; t++) {
        u8& l = lv[k + (t - rowStart_[r])];
        if (levels_[t] > l) l = levels_[t];
      }
    }
  }
  for (u32 i = leftmin; i + rightmin <= n; i++)
    if (lv[i + 1] & 1) out.push_back(i);
}

size_t HyphenDict::bytes() const {
  size_t b = sizeof(*this) + 4 * (edgeStart_.size() + edgeCp_.size() + edgeChild_.size() + nodeRow_.size() +
                                  rowStart_.size() + alphabet_.size()) +
             levels_.size();
  for (const auto& e : exceptions_) b += 4 * (e.first.size() + e.second.size()) + 48;
  return b;
}

bool HyphenDict::compile(std::string_view tag, std::string_view patterns, std::string_view exceptions, u8 leftmin,
                         u8 rightmin, std::string_view hyphenChar, HyphenDict& out, std::string& err) {
  out = HyphenDict{};
  out.tag = std::string(tag);
  out.leftmin = leftmin;
  out.rightmin = rightmin;
  if (!hyphenChar.empty()) out.hyphenChar = std::string(hyphenChar);
  // the trie under construction, flat (no allocation per node): a node's
  // edges are a list through `next` from first[node]; the alphabet below
  // U+0080 as bits, the rest a short list (plan P5-02: the resident
  // dictionary is compiled on the first typeset's critical path)
  struct Edge {
    u32 cp, child, next;
  };
  std::vector<Edge> edges;
  std::vector<u32> first(1, ~0u);
  std::vector<i32> row(1, -1);
  std::vector<u32> letters, wide;
  std::vector<u8> lv;
  edges.reserve(patterns.size() / 2);
  first.reserve(patterns.size() / 2);
  row.reserve(patterns.size() / 2);
  for (size_t i = 0; i < patterns.size();) {
    while (i < patterns.size() && space(patterns[i])) i++;
    if (i >= patterns.size()) break;
    letters.clear();
    lv.assign(1, 0);
    const size_t at = i;
    u32 j = (u32)i;
    while (j < patterns.size() && !space(patterns[j])) {
      const u32 cp = utf8Next(patterns, j);
      if (cp >= '0' && cp <= '9') {
        lv.back() = (u8)(cp - '0');
      } else {
        letters.push_back(cp == '.' ? cp : lowerOf(cp));
        lv.push_back(0);
      }
    }
    i = j;
    if (letters.empty()) {
      err = "pattern '" + std::string(patterns.substr(at, i - at)) + "' has no letters";
      return false;
    }
    u32 node = 0;
    for (u32 cp : letters) {
      if (cp < 128) {
        if (cp != '.') out.asciiAlpha_[cp >> 6] |= 1ull << (cp & 63);
      } else if (std::find(wide.begin(), wide.end(), cp) == wide.end()) {
        wide.push_back(cp);
      }
      u32 next = ~0u;
      for (u32 e = first[node]; e != ~0u; e = edges[e].next)
        if (edges[e].cp == cp) {
          next = edges[e].child;
          break;
        }
      if (next == ~0u) {
        next = (u32)first.size();
        edges.push_back({cp, next, first[node]});
        first[node] = (u32)edges.size() - 1;
        first.push_back(~0u);
        row.push_back(-1);
      }
      node = next;
    }
    // (a repeated pattern: the later one)
    row[node] = (i32)out.rowStart_.size();
    out.rowStart_.push_back((u32)out.levels_.size());
    out.levels_.insert(out.levels_.end(), lv.begin(), lv.end());
  }
  if (out.rowStart_.empty()) {
    err = "no patterns";
    return false;
  }
  out.rowStart_.push_back((u32)out.levels_.size());
  // rows by the order they were written; nodes keep their ids; a node's
  // edges by codepoint (few: an insertion sort)
  out.nodeRow_ = std::move(row);
  const u32 nodes = (u32)first.size();
  out.edgeStart_.reserve(nodes + 1);
  out.edgeCp_.reserve(edges.size());
  out.edgeChild_.reserve(edges.size());
  for (u32 k = 0; k < nodes; k++) {
    const u32 at = (u32)out.edgeCp_.size();
    out.edgeStart_.push_back(at);
    for (u32 e = first[k]; e != ~0u; e = edges[e].next) {
      u32 i = (u32)out.edgeCp_.size();
      out.edgeCp_.push_back(edges[e].cp);
      out.edgeChild_.push_back(edges[e].child);
      for (; i > at && out.edgeCp_[i - 1] > out.edgeCp_[i]; i--) {
        std::swap(out.edgeCp_[i - 1], out.edgeCp_[i]);
        std::swap(out.edgeChild_[i - 1], out.edgeChild_[i]);
      }
    }
  }
  out.edgeStart_.push_back((u32)out.edgeCp_.size());
  for (size_t i = 0; i < exceptions.size();) {
    while (i < exceptions.size() && space(exceptions[i])) i++;
    if (i >= exceptions.size()) break;
    std::vector<u32> word, pts;
    u32 j = (u32)i;
    while (j < exceptions.size() && !space(exceptions[j])) {
      const u32 cp = utf8Next(exceptions, j);
      if (cp == '-') pts.push_back((u32)word.size());
      else word.push_back(lowerOf(cp));
    }
    i = j;
    for (u32 cp : word)
      if (cp < 128) out.asciiAlpha_[cp >> 6] |= 1ull << (cp & 63);
      else if (std::find(wide.begin(), wide.end(), cp) == wide.end()) wide.push_back(cp);
    out.exceptions_.push_back({std::move(word), std::move(pts)});
  }
  std::sort(out.exceptions_.begin(), out.exceptions_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  // the alphabet: the ASCII bits, then the rest, sorted
  for (u32 cp = 0; cp < 128; cp++)
    if ((out.asciiAlpha_[cp >> 6] >> (cp & 63)) & 1) out.alphabet_.push_back(cp);
  std::sort(wide.begin(), wide.end());
  out.alphabet_.insert(out.alphabet_.end(), wide.begin(), wide.end());
  std::fill(std::begin(out.rootAscii_), std::end(out.rootAscii_), -1);
  for (u32 e = out.edgeStart_[0]; e < out.edgeStart_[1]; e++)
    if (out.edgeCp_[e] < 128) out.rootAscii_[out.edgeCp_[e]] = (i32)out.edgeChild_[e];
  return true;
}

const HyphenDict& residentHyphenDict() {
  static const HyphenDict d = [] {
    HyphenDict r;
    std::string err;
    HyphenDict::compile(hyphen_en_us::kTag, hyphen_en_us::kPatterns, hyphen_en_us::kExceptions,
                        hyphen_en_us::kLeftmin, hyphen_en_us::kRightmin, hyphen_en_us::kHyphenChar, r, err);
    return r;
  }();
  return d;
}

bool residentHyphenLang(std::string_view lang) {
  auto is = [&](std::string_view t) {
    if (lang.size() != t.size()) return false;
    for (size_t i = 0; i < t.size(); i++)
      if ((lang[i] | 0x20) != (t[i] | 0x20) && !(lang[i] == '_' && t[i] == '-')) return false;
    return true;
  };
  return lang.empty() || is("en") || is("en-US") || is("und") || is("root");
}

}  // namespace tsr
