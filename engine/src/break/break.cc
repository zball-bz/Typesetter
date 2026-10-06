#include "break.h"

#include "items.h"

#include <algorithm>
#include <cstring>
#include <list>
#include <unordered_map>

namespace tsr {

// Breaking semantics (plans P0-12, P1-14; design T6 S1/S2) over the TeX item
// projection of the block stream (items.h):
// - legal breaks: a Glue after a Box/Disc, a non-Forbidden Penalty or Disc;
//   a Forced penalty must break; the paragraph end is a Forced break;
// - discard: a line ending at a Glue excludes it; after any break (and at
//   the paragraph start, matching layout's trim) Glue and Penalty items are
//   dropped up to the first Box or Disc; a final Glue is dropped (\unskip);
// - a Disc adds its `pre` when broken, its own width otherwise;
// - the last line (Forced end) has fil stretch and normal shrink, as has
//   every line ending at a Forced penalty (a hard line break) and every line
//   holding fil glue (a fill, plan P2-16);
// - cost: x = slack / stretch (or / shrink when tight); Overfull below
//   -shrinkThreshold, a class of its own; cost = min(mapped(x)^exponent, cap)
//   with the power by multiplication; demerits = sum(cost) + sum(pen) / 1000;
// - ties: lower demerits, then fewer lines, then the LATER parent;
// - search: an active list in position order. A node leaves it as soon as
//   its line becomes Overfull (exact while a line's width grows with its
//   end, i.e. every item's shrink is at most its width — a Disc with a wide
//   pre may make it inexact, as in TeX), and a Forced break deactivates
//   every earlier node. Nodes at one break are kept per line count only
//   while the line widths still depend on it (the parshape prefix); beyond
//   it every line count has the same future, so one node per break holds
//   the best by the total order. No window, no line-count pruning.
// - passes: (1) lines of cost <= tolerance (only with a tolerance), (2) the
//   same with emergencyStretch added to every line (only with both), then
//   the final pass: every non-Overfull line is feasible and TeX's rescue
//   applies — when every active node's line to a legal break is Overfull,
//   the best of them by the total order breaks there anyway (no demerits
//   added) and the line is reported Overfull: one unbreakable run per such
//   line, never a whole paragraph collapsed onto one line.

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
  std::vector<u32> fl;         // prefix counts of fil glue (plan P2-16; empty: none)
  std::vector<u32> nextBox;    // first Box/Disc at or after k (it.size() if none)
  explicit Para(const std::vector<BItem>& items) : it(items) {
    const u32 n = (u32)it.size();
    w.assign(n + 1, 0);
    st.assign(n + 1, 0);
    sh.assign(n + 1, 0);
    for (u32 k = 0; k < n; k++) {
      const BItem& x = it[k];
      const bool sized = x.k != ItemKind::Penalty;
      if (x.k == ItemKind::Glue && x.order > 0 && fl.empty()) fl.assign(n + 1, 0);
      if (!fl.empty()) fl[k + 1] = fl[k] + (x.k == ItemKind::Glue && x.order > 0 ? 1 : 0);
      w[k + 1] = w[k] + (sized ? x.w : 0);
      st[k + 1] = st[k] + (x.k == ItemKind::Glue ? x.stretch : 0);
      sh[k + 1] = sh[k] + (x.k == ItemKind::Glue ? x.shrink : 0);
    }
    nextBox.assign(n + 1, n);
    for (u32 k = n; k-- > 0;)
      nextBox[k] = (it[k].k == ItemKind::Box || it[k].k == ItemKind::Disc) ? k : nextBox[k + 1];
  }
  // the line after break `from` (item index, -1 = paragraph start) up to the
  // break at item `to` (it.size() = the paragraph end); `extraStretch` is
  // pass 2's emergency stretch
  LineFit fit(i64 from, u32 to, i64 width, const CostParams& p, Su extraStretch) const {
    const u32 n = (u32)it.size();
    u32 s = nextBox[(u32)(from + 1)];
    u32 e = to;
    i64 extra = 0;
    // the paragraph end and a forced break (a hard line break, plan P1-13:
    // TeX's \hfil\break) end a ragged line: fil stretch
    const bool fil = to == n || (it[to].k == ItemKind::Penalty && it[to].tag == PenTag::Forced);
    if (to == n) {
      while (e > s && it[e - 1].k == ItemKind::Glue) e--;  // \unskip
    } else if (it[to].k == ItemKind::Disc) {
      extra = it[to].pre;
    }
    if (e < s) e = s;
    return fitLine(w[e] - w[s] + extra, st[e] - st[s] + extraStretch, sh[e] - sh[s], fil || (!fl.empty() && fl[e] > fl[s]), width, p);
  }
};

struct Node {
  u32 bi;         // the legal break it sits at (index into bk; bk.size() = the end)
  u32 line;       // lines before it
  double val;     // demerits so far
  u32 parent;     // node index (0 = the start)
  bool overfull;  // the line ending here was rescued
};

// the total order: demerits, then fewer lines, then the later parent (node
// indices grow with the position)
bool better(double va, u32 la, u32 pa, double vb, u32 lb, u32 pb) {
  if (va != vb) return va < vb;
  if (la != lb) return la < lb;
  return pa > pb;
}

struct Search {
  const std::vector<BItem>& it;
  const Para& para;
  const std::vector<i64>& bk;  // legal breaks; bk[0] = -1 (the start)
  LineWidths widths;
  const CostParams& cp;
  std::vector<Node> pool;

  // one pass: the end node of the best path, or ~0u when there is none
  u32 run(double tolerance, Su extraStretch, bool rescue) {
    const u32 n = (u32)it.size();
    const u32 B = (u32)bk.size();
    pool.clear();
    pool.push_back({0, 0, 0, 0, false});
    std::vector<u32> active{0}, next;
    // line classes: below `merged` the next line's width depends on the line
    // count; from `merged` on it does not
    const u32 merged = widths.narrow > 0 ? widths.narrowK : 0;
    struct Slot {
      bool used = false;
      u32 line = 0;
      double val = 0;
      u32 parent = 0;
      bool overfull = false;
    };
    std::vector<Slot> slots(merged + 1);
    for (u32 bi = 1; bi <= B; bi++) {
      const bool end = bi == B;
      const u32 to = end ? n : (u32)bk[bi];
      const BItem* at = end ? nullptr : &it[to];
      const bool forced = end || (at->k == ItemKind::Penalty && at->tag == PenTag::Forced);
      const double pen =
          (end || at->k == ItemKind::Glue || at->tag == PenTag::Forced) ? 0.0 : at->pen / 1000.0;
      for (Slot& s : slots) s.used = false;
      bool any = false;
      u32 rescueFrom = ~0u;  // the best node by the total order among those Overfull here
      next.clear();
      for (u32 a : active) {
        const Node& nd = pool[a];
        const LineFit f = para.fit(bk[nd.bi], to, widths.at(nd.line), cp, extraStretch);
        if (f.overfull) {
          if (rescueFrom == ~0u ||
              better(nd.val, nd.line, a, pool[rescueFrom].val, pool[rescueFrom].line, rescueFrom))
            rescueFrom = a;
          continue;  // deactivated
        }
        next.push_back(a);
        if (tolerance >= 0 && f.cost > tolerance) continue;  // not feasible in this pass
        const u32 line = nd.line + 1;
        const double val = nd.val + f.cost + pen;
        Slot& s = slots[line < merged ? line : merged];
        if (!s.used || better(val, line, a, s.val, s.line, s.parent)) {
          s = {true, line, val, a, false};
          any = true;
        }
      }
      if (!any && rescue && rescueFrom != ~0u) {
        // TeX's rescue: every active node is Overfull here, none feasible
        const Node& nd = pool[rescueFrom];
        const u32 line = nd.line + 1;
        slots[line < merged ? line : merged] = {true, line, nd.val, rescueFrom, true};
      }
      if (end) {
        u32 best = ~0u;
        for (const Slot& s : slots) {
          if (!s.used) continue;
          pool.push_back({bi, s.line, s.val, s.parent, s.overfull});
          const u32 k = (u32)pool.size() - 1;
          if (best == ~0u || better(s.val, s.line, s.parent, pool[best].val, pool[best].line, pool[best].parent))
            best = k;
        }
        return best;
      }
      if (forced) next.clear();  // no line spans a Forced break
      for (const Slot& s : slots)
        if (s.used) {
          pool.push_back({bi, s.line, s.val, s.parent, s.overfull});
          next.push_back((u32)pool.size() - 1);
        }
      active.swap(next);
      if (active.empty()) return ~0u;  // nothing reaches further
    }
    return ~0u;
  }
};

}  // namespace

BreakResult breakItems(const std::vector<BItem>& it, u32 nBlocks, LineWidths widths,
                       const BreakParams& params) {
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

  Search search{it, para, bk, widths, params.cost, {}};
  u32 best = ~0u;
  const bool tol = params.tolerance >= 0;
  if (tol) {
    best = search.run(params.tolerance, 0, false);
    res.pass = 1;
  }
  if (best == ~0u && tol && params.emergencyStretch > 0) {
    best = search.run(params.tolerance, params.emergencyStretch, false);
    res.pass = 2;
  }
  if (best == ~0u) {
    best = search.run(kNoTolerance, params.emergencyStretch, true);
    res.pass = 3;
  }
  if (best == ~0u) {  // only an empty active list: nothing to break
    res.feasible = false;
    res.breakpoints = {nBlocks};
    return res;
  }

  // walk back: the interior breaks (bk indices) and each line's rescue flag
  std::vector<u32> chosen;
  std::vector<bool> over;
  for (u32 k = best; k != 0; k = search.pool[k].parent) {
    if (search.pool[k].bi != (u32)bk.size()) chosen.push_back(search.pool[k].bi);
    over.push_back(search.pool[k].overfull);
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
  res.feasible = res.overfullLines.empty();
  res.cost = search.pool[best].val;
  return res;
}

BreakResult breakLines(const std::vector<BreakBlock>& blocks, LineWidths widths,
                       const BreakParams& params) {
  std::vector<BItem> items;
  blocksToItems(blocks, items);
  return breakItems(items, (u32)blocks.size(), widths, params);
}

// The process-wide KP memo (plans P0-11, P1-14; shared across documents —
// the editing loop's fast path). The key is a 128-bit hash of exactly what
// the DP reads — the items' bytes, the block count, the line widths and the
// params — and a hit is validated by the item count, so a collision would
// need both 64-bit halves to agree. Least-recently-used entries go once the
// stored words exceed the budget.
namespace {

struct Key128 {
  u64 lo = 0, hi = 0;
};

inline u64 rotl(u64 x, int r) { return (x << r) | (x >> (64 - r)); }
inline u64 fmix(u64 k) {  // MurmurHash3's finalizer
  k ^= k >> 33;
  k *= 0xFF51AFD7ED558CCDull;
  k ^= k >> 33;
  k *= 0xC4CEB9FE1A85EC53ull;
  k ^= k >> 33;
  return k;
}

struct Hasher {  // MurmurHash3 x64_128's block step over 8-byte words
  u64 h1 = 0x9E3779B97F4A7C15ull, h2 = 0xC2B2AE3D27D4EB4Full;
  u64 len = 0;
  void word(u64 k) {
    u64 k1 = k * 0x87C37B91114253D5ull;
    k1 = rotl(k1, 31) * 0x4CF5AD432745937Full;
    h1 ^= k1;
    h1 = rotl(h1, 27) + h2;
    h1 = h1 * 5 + 0x52DCE729;
    u64 k2 = (k ^ 0x5851F42D4C957F2Dull) * 0x4CF5AD432745937Full;
    k2 = rotl(k2, 33) * 0x87C37B91114253D5ull;
    h2 ^= k2;
    h2 = rotl(h2, 31) + h1;
    h2 = h2 * 5 + 0x38495AB5;
    len += 8;
  }
  void bytes(const void* p, size_t nb) {
    const unsigned char* c = (const unsigned char*)p;
    while (nb >= 8) {
      u64 k;
      std::memcpy(&k, c, 8);
      word(k);
      c += 8;
      nb -= 8;
    }
    if (nb) {
      u64 k = 0;
      std::memcpy(&k, c, nb);
      word(k ^ ((u64)nb << 56));
    }
  }
  void dbl(double v) {
    u64 b;
    std::memcpy(&b, &v, 8);
    word(b);
  }
  Key128 done() {
    h1 ^= len;
    h2 ^= len;
    h1 += h2;
    h2 += h1;
    h1 = fmix(h1);
    h2 = fmix(h2);
    h1 += h2;
    h2 += h1;
    return {h1, h2};
  }
};

Key128 breakKey(const std::vector<BItem>& items, u32 nBlocks, LineWidths widths, const BreakParams& params) {
  Hasher h;
  h.bytes(items.data(), items.size() * sizeof(BItem));  // no padding (items.h static_assert)
  h.word(((u64)nBlocks << 32) | (u32)items.size());
  h.word(((u64)(u32)widths.constant << 32) | (u32)widths.narrow);
  h.word(widths.narrowK);
  h.word(params.cost.exponent);
  h.dbl(params.cost.shrinkThreshold);
  h.dbl(params.cost.shrinkCoeff);
  h.dbl(params.cost.cap);
  h.dbl(params.tolerance);
  h.word((u32)params.emergencyStretch);
  return h.done();
}

}  // namespace

// The KP memo (editor-design.md §2; plan P1-21: a Session's memo slot, no
// process-global state): keyed by the complete break input — the items'
// bytes, the line widths and the params — whose bytes are stored and
// compared on a hit, so a hash collision can never return another
// paragraph's breaks. LRU within a byte budget.
const BreakResult* BreakMemo::find(u64 hash, std::string_view key) {
  auto it = map_.find(hash);
  if (it == map_.end() || it->second.key != key) return nullptr;
  lru_.splice(lru_.begin(), lru_, it->second.lru);  // most recent first
  return &it->second.result;
}
void BreakMemo::put(u64 hash, std::string_view key, const BreakResult& r) {
  auto it = map_.find(hash);
  if (it != map_.end()) erase(it);  // a colliding key replaces the old one
  lru_.push_front(hash);
  Entry& e = map_[hash];
  e.key.assign(key);
  e.result = r;
  e.lru = lru_.begin();
  bytes_ += cost(e);
  while (bytes_ > budget_ && lru_.size() > 1) erase(map_.find(lru_.back()));
}
size_t BreakMemo::cost(const Entry& e) {
  return e.key.size() + 4 * (e.result.breakpoints.size() + e.result.overfullLines.size()) + 64;
}
void BreakMemo::erase(std::unordered_map<u64, Entry>::iterator it) {
  bytes_ -= cost(it->second);
  lru_.erase(it->second.lru);
  map_.erase(it);
}

BreakResult breakLinesCached(const std::vector<BreakBlock>& blocks, LineWidths widths, const BreakParams& params,
                             BreakMemo* memo) {
  static std::vector<BItem> items;  // scratch: rebuilt per call
  blocksToItems(blocks, items);
  if (!memo) return breakItems(items, (u32)blocks.size(), widths, params);
  // the complete input, serialized: the key bytes compared on a hit
  static std::string key;
  key.assign((const char*)items.data(), items.size() * sizeof(BItem));  // no padding (items.h static_assert)
  auto put = [&](const void* p, size_t n) { key.append((const char*)p, n); };
  const u32 nBlocks = (u32)blocks.size();
  put(&nBlocks, 4);
  put(&widths.constant, sizeof widths.constant);
  put(&widths.narrow, sizeof widths.narrow);
  put(&widths.narrowK, sizeof widths.narrowK);
  put(&params.cost.exponent, sizeof params.cost.exponent);
  put(&params.cost.shrinkThreshold, sizeof params.cost.shrinkThreshold);
  put(&params.cost.shrinkCoeff, sizeof params.cost.shrinkCoeff);
  put(&params.cost.cap, sizeof params.cost.cap);
  put(&params.tolerance, sizeof params.tolerance);
  put(&params.emergencyStretch, sizeof params.emergencyStretch);
  const Key128 k = breakKey(items, nBlocks, widths, params);
  if (const BreakResult* hit = memo->find(k.lo ^ k.hi, key)) return *hit;
  BreakResult r = breakItems(items, nBlocks, widths, params);
  memo->put(k.lo ^ k.hi, key, r);
  return r;
}

}  // namespace tsr
