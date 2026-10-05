// Knuth–Plass line breaking with TeX item semantics (plans P0-12, P1-14;
// the rules are at the top of break.cc, the item projection in items.h). su
// inputs, bounded double costs, an exact active-list search.
#pragma once
#include <list>
#include <unordered_map>

#include "../emit/emit.h"

namespace tsr {

struct BItem;

struct BreakResult {
  std::vector<u32> breakpoints;    // counts of blocks consumed per line, ascending
  double cost = 0;                 // demerits
  std::vector<u32> overfullLines;  // rescued lines (final pass): content wider than the line
  bool feasible = true;            // false: the final pass had to rescue
  u8 pass = 3;                     // the pass that found it (1 tolerance, 2 emergency, 3 final)
};

// Prefix form (figure-design.md §4, TeX parshape-in-lines): the first
// `narrowK` lines run beside a float at the narrowed width.
struct LineWidths {
  Su constant;
  Su narrow = 0;
  u32 narrowK = 0;
  Su at(u32 i) const { return (i < narrowK && narrow > 0) ? narrow : constant; }
};

// What the search reads besides the items (design T6 S2), defaults = today:
// no tolerance pass and no emergency pass, only the final pass with TeX's
// rescue. Pass 1 accepts only lines of cost <= tolerance; pass 2 adds
// emergencyStretch to every line's stretch.
constexpr double kNoTolerance = -1;
struct BreakParams {
  CostParams cost;
  double tolerance = kNoTolerance;
  Su emergencyStretch = 0;
};

BreakResult breakItems(const std::vector<BItem>& items, u32 nBlocks, LineWidths widths,
                       const BreakParams& params);
BreakResult breakLines(const std::vector<BreakBlock>& blocks, LineWidths widths,
                       const BreakParams& params);

// The cached form (editor-design.md §2): KP reads only the items, the line
// widths and the params, so a result is keyed by exactly those — the
// complete input's bytes, compared on a hit — and kept in the Session's
// memo slot (plan P1-21): an editing session re-breaks only the paragraphs
// a keystroke changed. No memo: break uncached.
class BreakMemo {
 public:
  static constexpr size_t kBudgetBytes = size_t(16) << 20;
  const BreakResult* find(u64 hash, std::string_view key);
  void put(u64 hash, std::string_view key, const BreakResult& r);
  void setBudget(size_t bytes) { budget_ = bytes ? bytes : kBudgetBytes; }
  size_t bytes() const { return bytes_; }

 private:
  struct Entry {
    std::string key;
    BreakResult result;
    std::list<u64>::iterator lru;
  };
  static size_t cost(const Entry& e);
  void erase(std::unordered_map<u64, Entry>::iterator it);
  std::unordered_map<u64, Entry> map_;
  std::list<u64> lru_;
  size_t bytes_ = 0;
  size_t budget_ = kBudgetBytes;
};
BreakResult breakLinesCached(const std::vector<BreakBlock>& blocks, LineWidths widths, const BreakParams& params,
                             BreakMemo* memo);

}  // namespace tsr
