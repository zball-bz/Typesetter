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
  // the trie under construction: per node its (codepoint, child) edges
  std::vector<std::vector<std::pair<u32, u32>>> kids(1);
  std::vector<i32> row(1, -1);
  std::vector<u32> letters;
  std::vector<u8> lv;
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
      if (cp != '.') out.alphabet_.push_back(cp);
      u32 next = ~0u;
      for (const auto& [c, k] : kids[node])
        if (c == cp) next = k;
      if (next == ~0u) {
        next = (u32)kids.size();
        kids[node].push_back({cp, next});
        kids.emplace_back();
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
  // rows by the order they were written; nodes keep their ids
  out.nodeRow_ = row;
  out.edgeStart_.reserve(kids.size() + 1);
  for (auto& k : kids) {
    std::sort(k.begin(), k.end());
    out.edgeStart_.push_back((u32)out.edgeCp_.size());
    for (const auto& [c, n] : k) {
      out.edgeCp_.push_back(c);
      out.edgeChild_.push_back(n);
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
    out.alphabet_.insert(out.alphabet_.end(), word.begin(), word.end());
    out.exceptions_.push_back({std::move(word), std::move(pts)});
  }
  std::sort(out.exceptions_.begin(), out.exceptions_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  std::sort(out.alphabet_.begin(), out.alphabet_.end());
  out.alphabet_.erase(std::unique(out.alphabet_.begin(), out.alphabet_.end()), out.alphabet_.end());
  for (u32 cp : out.alphabet_)
    if (cp < 128) out.asciiAlpha_[cp >> 6] |= 1ull << (cp & 63);
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
