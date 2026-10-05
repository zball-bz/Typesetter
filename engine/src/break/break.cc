#include "break.h"

#include <algorithm>
#include <cstring>
#include <list>
#include <unordered_map>

namespace tsr {

static constexpr double INF = 1e18;

static double costFn(const CostParams& p, double totalWidth, double lineWidth,
                     double totalSpace) {
  double slack = lineWidth - totalWidth;
  double space = totalSpace > 0.001 ? totalSpace : 0.001;
  double x = slack / space;  // positive = stretch, negative = shrink
  if (x < -p.shrinkThreshold) return INF;
  double mapped = x < 0 ? p.shrinkCoeff * (1.0 / (1.0 + x) - 1.0) : x;
  return std::pow(mapped, p.exponent);
}

BreakResult breakLines(const std::vector<LinebreakBlock>& blocks, LineWidths widths,
                       const CostParams& params, u32 cursorSearchRange) {
  const u32 n = (u32)blocks.size();
  if (n == 0) return {{}, 0};

  // Virtual start block at index 0; allBlocks[i] == blocks[i-1].
  const u32 N = n + 1;
  auto blk = [&](u32 i) -> const LinebreakBlock* { return i == 0 ? nullptr : &blocks[i - 1]; };
  auto penalty = [&](u32 i) -> double {
    return i == 0 ? 0.0 : (double)blocks[i - 1].breakPenalty;
  };

  std::vector<double> widthPsum(N + 1, 0), swidthPsum(N + 1, 0);
  for (u32 i = 0; i < N; i++) {
    const LinebreakBlock* b = blk(i);
    widthPsum[i + 1] = widthPsum[i] + (b ? suToPx(b->width) : 0);
    swidthPsum[i + 1] = swidthPsum[i] + (b ? suToPx(b->spaceWidth) : 0);
  }

  struct DpEntry { u32 line; double val; };
  struct ResEntry { u32 line; u32 parent; };
  std::vector<std::vector<DpEntry>> dp(N);
  std::vector<std::vector<ResEntry>> dpRes(N);
  dp[0].push_back({0, 0});

  std::vector<u32> bkIdx;
  for (u32 i = 0; i < N; i++)
    if (penalty(i) < INF) bkIdx.push_back(i);
  const u32 B = (u32)bkIdx.size();

  u32 cursorB = 0;
  for (u32 bi = 1; bi < B; bi++) {
    const u32 i = bkIdx[bi];
    const u32 loB = cursorB > cursorSearchRange ? cursorB - cursorSearchRange : 0;
    const u32 hiB = std::min(bi - 1, cursorB + cursorSearchRange);

    std::unordered_map<u32, double> dps;
    std::unordered_map<u32, u32> dpp;
    double dpmin = INF;
    i32 dpminLine = -1;
    u32 dpminParentB = cursorB;

    for (u32 bj = loB; bj <= hiB; bj++) {
      const u32 j = bkIdx[bj];
      if (dp[j].empty()) continue;
      for (const DpEntry& e : dp[j]) {
        double contentW = widthPsum[i + 1] - widthPsum[j + 1] +
                          (blk(i) ? suToPx(blk(i)->breakWidth) : 0);
        double L = suToPx(widths.at(e.line));
        double totalSpace = swidthPsum[i + 1] - swidthPsum[j + 1];
        double lineCost = costFn(params, contentW, L, totalSpace);
        if (lineCost >= INF) continue;
        double dpNext = e.val + lineCost + penalty(i);
        u32 nextLine = e.line + 1;
        auto it = dps.find(nextLine);
        if (it == dps.end() || it->second > dpNext) {
          dps[nextLine] = dpNext;
          dpp[nextLine] = j;
          if (dpNext < dpmin) {
            dpmin = dpNext;
            dpminLine = (i32)nextLine;
            dpminParentB = bj;
          }
        }
      }
    }

    if (dpminLine >= 0) {
      for (const auto& [k, val] : dps) {
        if ((i32)k >= dpminLine - 1 && (i32)k <= dpminLine + 1) {
          dp[i].push_back({k, val});
          dpRes[i].push_back({k, dpp[k]});
        }
      }
      cursorB = dpminParentB;
    }
  }

  // Endpoint scan. Unlike the PoC this includes i == 0 (the virtual block):
  // the zero-break solution — whole paragraph as the ragged last line — was
  // unreachable in the PoC and forced a spurious break in every paragraph
  // that ends with an unbreakable word.
  double bestVal = INF;
  i32 bestIdx = -1;
  u32 bestLine = 0;
  for (i32 i = (i32)N - 1; i >= 0; i--) {
    if (penalty((u32)i) >= INF) continue;
    if (dp[(u32)i].empty()) continue;
    for (const DpEntry& e : dp[(u32)i]) {
      double remainW = widthPsum[N] - widthPsum[i + 1];
      double L = suToPx(widths.at(e.line));
      if ((u32)i == N - 1 || remainW <= L) {
        if (e.val < bestVal) {
          bestVal = e.val;
          bestIdx = i;
          bestLine = e.line;
        }
      }
    }
  }

  if (bestIdx < 0) return {{n}, INF};

  std::vector<u32> breaks;
  if ((u32)bestIdx < N - 1) breaks.push_back(n);
  breaks.push_back((u32)bestIdx);

  u32 curIdx = (u32)bestIdx;
  u32 curLine = bestLine;
  while (curLine > 0) {
    const ResEntry* entry = nullptr;
    for (const ResEntry& e : dpRes[curIdx])
      if (e.line == curLine) { entry = &e; break; }
    if (!entry || entry->parent == 0) break;
    curIdx = entry->parent;
    curLine--;
    breaks.push_back(curIdx);
  }

  std::sort(breaks.begin(), breaks.end());
  breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
  while (!breaks.empty() && breaks.front() == 0) breaks.erase(breaks.begin());
  if (breaks.empty() || breaks.back() != n) breaks.push_back(n);
  return {breaks, bestVal};
}

// The process-wide KP memo (plan P0-11; shared across documents — the
// editing loop's fast path). The key is exactly the inputs breakLines
// reads, packed field by field into 32-bit words; any new field the DP
// starts reading MUST be added to breakKey. A hit compares the stored key
// words, so a hash collision is a miss, never another paragraph's
// breakpoints. Least-recently-used entries go once the stored words exceed
// the budget (it used to wipe itself whole at 16384 entries).
namespace {

void breakKey(std::vector<u32>& k, const std::vector<LinebreakBlock>& blocks, LineWidths widths,
              const CostParams& params) {
  k.clear();
  k.reserve(10 + blocks.size() * 4);
  auto d = [&](double v) {
    u64 b;
    std::memcpy(&b, &v, 8);
    k.push_back((u32)b);
    k.push_back((u32)(b >> 32));
  };
  d(params.exponent);
  d(params.shrinkThreshold);
  d(params.shrinkCoeff);
  k.push_back((u32)widths.constant);
  k.push_back((u32)widths.narrow);
  k.push_back(widths.narrowK);
  k.push_back((u32)blocks.size());
  for (const LinebreakBlock& b : blocks) {
    k.push_back((u32)b.width);
    k.push_back((u32)b.spaceWidth);
    k.push_back((u32)b.breakWidth);
    u32 pen;
    std::memcpy(&pen, &b.breakPenalty, 4);
    k.push_back(pen);
  }
}

u64 hashWords(const std::vector<u32>& k) {
  u64 h = 0x9E3779B97F4A7C15ull ^ k.size();
  for (u32 w : k) {  // one multiply-xorshift round per field
    h = (h ^ w) * 0xFF51AFD7ED558CCDull;
    h ^= h >> 32;
  }
  return h;
}

class BreakMemo {
 public:
  static constexpr size_t kBudgetWords = size_t(4) << 20;  // 16 MB of key + result words

  const BreakResult* find(u64 h, const std::vector<u32>& key) {
    auto it = map_.find(h);
    if (it == map_.end() || it->second.key != key) return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second.lru);  // most recent first
    return &it->second.result;
  }
  void put(u64 h, const std::vector<u32>& key, const BreakResult& r) {
    auto it = map_.find(h);
    if (it != map_.end()) erase(it);  // a colliding key replaces the old one
    lru_.push_front(h);
    Entry& e = map_[h];
    e.key = key;
    e.result = r;
    e.lru = lru_.begin();
    words_ += cost(e);
    while (words_ > kBudgetWords && lru_.size() > 1) erase(map_.find(lru_.back()));
  }
  size_t size() const { return map_.size(); }

 private:
  struct Entry {
    std::vector<u32> key;
    BreakResult result;
    std::list<u64>::iterator lru;
  };
  static size_t cost(const Entry& e) { return e.key.size() + e.result.breakpoints.size() + 16; }
  void erase(std::unordered_map<u64, Entry>::iterator it) {
    words_ -= cost(it->second);
    lru_.erase(it->second.lru);
    map_.erase(it);
  }
  std::unordered_map<u64, Entry> map_;
  std::list<u64> lru_;
  size_t words_ = 0;
};

}  // namespace

BreakResult breakLinesRetry(const std::vector<LinebreakBlock>& blocks, LineWidths widths,
                            const CostParams& params) {
  static BreakMemo memo;
  static std::vector<u32> key;  // scratch: rebuilt per call
  breakKey(key, blocks, widths, params);
  const u64 h = hashWords(key);
  if (const BreakResult* hit = memo.find(h, key)) return *hit;
  BreakResult r = breakLines(blocks, widths, params);
  if (r.cost >= 1e17) {
    // retry ladder: a narrow measure can starve the ±5 cursor window of
    // feasible transitions; widen until a finite solution appears
    for (u32 range : {10u, 20u, 50u, 0xFFFFFFFFu}) {
      r = breakLines(blocks, widths, params, range);
      if (r.cost < 1e17) break;
    }
  }
  memo.put(h, key, r);
  return r;
}

}  // namespace tsr
