#include "break.h"

#include "items.h"

#include <algorithm>
#include <cstring>
#include <list>
#include <unordered_map>

namespace tsr {

// Breaking semantics (plan P0-12; design T6 S1) over the TeX item
// projection of the block stream (items.h):
// - legal breaks: a Glue after a Box/Disc, a non-Forbidden Penalty or Disc;
//   a Forced penalty must break; the paragraph end is a Forced break;
// - discard: a line ending at a Glue excludes it; after any break (and at
//   the paragraph start, matching layout's trim) Glue and Penalty items are
//   dropped up to the first Box or Disc; a final Glue is dropped (\unskip);
// - a Disc adds its `pre` when broken, its own width otherwise;
// - the last line (Forced end) has fil stretch and normal shrink;
// - cost: x = slack / stretch (or / shrink when tight); Overfull below
//   -shrinkThreshold, a class of its own; cost = min(mapped(x)^exponent, cap)
//   with the power by multiplication; demerits = sum(cost) + sum(pen) / 1000;
// - ties: lower demerits, then fewer lines, then the LATER parent;
// - the final pass rescues: when every node's line to a legal break is
//   Overfull, the latest node breaks there anyway (no demerits added) and
//   the line is reported Overfull — one unbreakable run per such line,
//   never a whole paragraph collapsed onto one line.
// The search window (±range legal breaks around the best parent) and the
// ±1 line-count pruning are today's; P1-14 replaces them with an active list.

namespace {

struct LineFit {
  double cost = 0;
  bool overfull = false;
};

double powi(double x, u8 e) {
  double r = 1;
  for (u8 k = 0; k < e; k++) r *= x;
  return r;
}

LineFit fitLine(i64 natural, i64 stretch, i64 shrink, bool fil, i64 width, const CostParams& p) {
  const i64 slack = width - natural;
  double x;
  if (slack >= 0) {
    if (fil || slack == 0) x = 0;
    else if (stretch > 0) x = (double)slack / (double)stretch;
    else return {p.cap, false};  // underfull without stretch: TeX's inf_bad
  } else {
    if (shrink <= 0) return {0, true};
    x = (double)slack / (double)shrink;
  }
  if (x < -p.shrinkThreshold) return {0, true};
  const double mapped = x < 0 ? p.shrinkCoeff * (1.0 / (1.0 + x) - 1.0) : x;
  const double c = powi(mapped, p.exponent < 1 ? 1 : p.exponent > 4 ? 4 : p.exponent);
  return {c < p.cap ? c : p.cap, false};
}

struct Para {
  const std::vector<BItem>& it;
  std::vector<i64> w, st, sh;  // prefix sums over items (i64 su)
  std::vector<u32> nextBox;    // first Box/Disc at or after k (it.size() if none)
  explicit Para(const std::vector<BItem>& items) : it(items) {
    const u32 n = (u32)it.size();
    w.assign(n + 1, 0);
    st.assign(n + 1, 0);
    sh.assign(n + 1, 0);
    for (u32 k = 0; k < n; k++) {
      const BItem& x = it[k];
      const bool sized = x.k != ItemKind::Penalty;
      w[k + 1] = w[k] + (sized ? x.w : 0);
      st[k + 1] = st[k] + (x.k == ItemKind::Glue ? x.stretch : 0);
      sh[k + 1] = sh[k] + (x.k == ItemKind::Glue ? x.shrink : 0);
    }
    nextBox.assign(n + 1, n);
    for (u32 k = n; k-- > 0;)
      nextBox[k] = (it[k].k == ItemKind::Box || it[k].k == ItemKind::Disc) ? k : nextBox[k + 1];
  }
  // the line after break `from` (item index, -1 = paragraph start) up to the
  // break at item `to` (it.size() = the paragraph end)
  LineFit fit(i64 from, u32 to, i64 width, const CostParams& p) const {
    const u32 n = (u32)it.size();
    u32 s = nextBox[(u32)(from + 1)];
    u32 e = to;
    i64 extra = 0;
    if (to == n) {
      while (e > s && it[e - 1].k == ItemKind::Glue) e--;  // \unskip
    } else if (it[to].k == ItemKind::Disc) {
      extra = it[to].pre;
    }
    if (e < s) e = s;
    return fitLine(w[e] - w[s] + extra, st[e] - st[s], sh[e] - sh[s], to == n, width, p);
  }
};

}  // namespace

BreakResult breakItems(const std::vector<BItem>& it, u32 nBlocks, LineWidths widths,
                       const CostParams& params, u32 cursorSearchRange, bool finalPass) {
  BreakResult res;
  const u32 n = (u32)it.size();
  if (n == 0) return res;
  const Para para(it);

  // legal breaks; bk[0] = the paragraph start (item -1)
  std::vector<i64> bk{-1};
  for (u32 k = 0; k < n; k++) {
    const BItem& x = it[k];
    bool legal = false;
    if (x.k == ItemKind::Glue) legal = k > 0 && (it[k - 1].k == ItemKind::Box || it[k - 1].k == ItemKind::Disc);
    else if (x.k == ItemKind::Penalty || x.k == ItemKind::Disc) legal = x.tag != PenTag::Forbidden;
    if (legal) bk.push_back(k);
  }
  const u32 B = (u32)bk.size();
  auto penOf = [&](u32 bi) -> double {
    const BItem& x = it[(u32)bk[bi]];
    return (x.k == ItemKind::Glue || x.tag == PenTag::Forced) ? 0.0 : x.pen / 1000.0;
  };

  struct Entry {
    u32 line;     // lines before this break
    double val;   // demerits so far
    u32 parent;   // index into bk
    bool overfull;
  };
  // the total order: demerits, then fewer lines, then the later parent
  auto better = [](const Entry& a, const Entry& b) {
    if (a.val != b.val) return a.val < b.val;
    if (a.line != b.line) return a.line < b.line;
    return a.parent > b.parent;
  };
  std::vector<std::vector<Entry>> dp(B);
  dp[0].push_back({0, 0, 0, false});

  u32 cursorB = 0, minParentB = 0;  // a Forced break bounds every later line
  // the final-pass rescue at legal break `bi` (bk.size() = the paragraph
  // end): the best entry, by the total order, among the nodes whose line to
  // the previous legal break was not Overfull (TeX's active list there)
  u32 rescuedParent = 0;
  auto rescueFrom = [&](u32 bi) -> const Entry* {
    const Entry* best = nullptr;
    const u32 prev = bi - 1;
    for (u32 bj = minParentB; bj <= prev; bj++) {
      for (const Entry& e : dp[bj]) {
        if (bj != prev && para.fit(bk[bj], (u32)bk[prev], widths.at(e.line), params).overfull)
          continue;
        // the total order with the node itself as the parent of the new line
        const bool take = !best || e.val < best->val ||
                          (e.val == best->val && (e.line < best->line ||
                                                  (e.line == best->line && bj > rescuedParent)));
        if (take) {
          best = &e;
          rescuedParent = bj;
        }
      }
    }
    return best;
  };
  std::vector<Entry> cand;
  for (u32 bi = 1; bi < B; bi++) {
    const u32 i = (u32)bk[bi];
    u32 loB = cursorB > cursorSearchRange ? cursorB - cursorSearchRange : 0;
    if (loB < minParentB) loB = minParentB;
    const u32 hiB = std::min(bi - 1, cursorB + cursorSearchRange);
    cand.clear();  // best entry per line count (small: at most a few)
    for (u32 bj = loB; bj <= hiB; bj++) {
      for (const Entry& e : dp[bj]) {
        const LineFit f = para.fit(bk[bj], i, widths.at(e.line), params);
        if (f.overfull) continue;
        const Entry next{e.line + 1, e.val + f.cost + penOf(bi), bj, false};
        auto slot = std::find_if(cand.begin(), cand.end(),
                                 [&](const Entry& c) { return c.line == next.line; });
        if (slot == cand.end()) cand.push_back(next);
        else if (better(next, *slot)) *slot = next;
      }
    }
    if (cand.empty() && finalPass) {
      // rescue: every node's line to this break is Overfull. Of the nodes
      // still active at the previous legal break, the best by the total
      // order breaks here (no demerits added); the line is set Overfull.
      if (const Entry* r = rescueFrom(bi)) cand.push_back({r->line + 1, r->val, rescuedParent, true});
    }
    if (!cand.empty()) {
      const Entry* min = &cand[0];
      for (const Entry& c : cand)
        if (better(c, *min)) min = &c;
      for (const Entry& c : cand)
        if ((i64)c.line >= (i64)min->line - 1 && c.line <= min->line + 1) dp[bi].push_back(c);
      std::sort(dp[bi].begin(), dp[bi].end(),
                [](const Entry& a, const Entry& b) { return a.line < b.line; });
      cursorB = min->parent;
    }
    if (it[i].tag == PenTag::Forced && it[i].k == ItemKind::Penalty) {
      minParentB = bi;  // no line spans a Forced break
      if (cursorB < bi) cursorB = bi;
    }
  }

  // the paragraph end: a Forced break with a fil last line
  bool found = false;
  Entry best{};
  u32 endParent = 0;
  for (u32 bj = minParentB; bj < B; bj++) {
    for (const Entry& e : dp[bj]) {
      const LineFit f = para.fit(bk[bj], n, widths.at(e.line), params);
      if (f.overfull) continue;
      const Entry fin{e.line + 1, e.val + f.cost, bj, false};
      if (!found || better(fin, best)) {
        best = fin;
        endParent = bj;
        found = true;
      }
    }
  }
  if (!found && finalPass) {
    if (const Entry* r = rescueFrom(B)) {
      best = {r->line + 1, r->val, rescuedParent, true};
      endParent = rescuedParent;
      found = true;
    }
  }
  if (!found) {
    res.feasible = false;
    res.breakpoints = {nBlocks};
    return res;
  }

  // walk back: (break index, line count) → the entry that reached it
  std::vector<u32> chosen;  // bk indices of the interior breaks, last first
  std::vector<bool> over{best.overfull};
  u32 curB = endParent, curLine = best.line - 1;
  while (curB != 0) {
    const Entry* e = nullptr;
    for (const Entry& x : dp[curB])
      if (x.line == curLine) { e = &x; break; }
    if (!e) break;  // unreachable: every kept entry has its parent's
    chosen.push_back(curB);
    over.push_back(e->overfull);
    curB = e->parent;
    curLine--;
  }
  std::reverse(chosen.begin(), chosen.end());
  std::reverse(over.begin(), over.end());
  // a break's index is the first block of the next line after discard, so
  // breaks giving identical lines report the same (latest) position and the
  // dump indices match layout's trimmed ranges
  for (u32 bi : chosen) {
    const u32 nb = para.nextBox[(u32)bk[bi] + 1];
    const u32 at = nb < n ? it[nb].block : nBlocks;
    if (at < nBlocks && (res.breakpoints.empty() || at > res.breakpoints.back()))
      res.breakpoints.push_back(at);
  }
  res.breakpoints.push_back(nBlocks);
  for (u32 l = 0; l < (u32)over.size(); l++)
    if (over[l]) res.overfullLines.push_back(l);
  res.cost = best.val;
  return res;
}

BreakResult breakLines(const std::vector<LinebreakBlock>& blocks, LineWidths widths,
                       const CostParams& params, u32 cursorSearchRange, bool finalPass) {
  std::vector<BItem> items;
  blocksToItems(blocks, items);
  return breakItems(items, (u32)blocks.size(), widths, params, cursorSearchRange, finalPass);
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
  k.reserve(12 + blocks.size() * 5);
  auto d = [&](double v) {
    u64 b;
    std::memcpy(&b, &v, 8);
    k.push_back((u32)b);
    k.push_back((u32)(b >> 32));
  };
  k.push_back(params.exponent);
  d(params.shrinkThreshold);
  d(params.shrinkCoeff);
  d(params.cap);
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
    k.push_back(b.flags);  // the item adapter reads the BF_ kind bits
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
  std::vector<BItem> items;
  blocksToItems(blocks, items);
  const u32 nb = (u32)blocks.size();
  BreakResult r = breakItems(items, nb, widths, params, 5, false);
  if (!r.feasible) {
    // retry ladder: a narrow measure can starve the ±5 window of feasible
    // transitions; widen, and let the unbounded final pass rescue
    for (u32 range : {10u, 20u, 50u}) {
      r = breakItems(items, nb, widths, params, range, false);
      if (r.feasible) break;
    }
    if (!r.feasible) r = breakItems(items, nb, widths, params, 0xFFFFFFFFu, true);
  }
  memo.put(h, key, r);
  return r;
}

}  // namespace tsr
