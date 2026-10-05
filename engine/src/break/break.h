// Knuth–Plass line breaking with TeX item semantics (plans P0-12, P1-14;
// the rules are at the top of break.cc, the item projection in items.h). su
// inputs, bounded double costs, an exact active-list search.
#pragma once
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
// widths and the params, so a result is keyed by a 128-bit hash of exactly
// those, validated on a hit, and shared process-wide across documents — an
// editing session re-breaks only the paragraphs a keystroke changed.
BreakResult breakLinesCached(const std::vector<BreakBlock>& blocks, LineWidths widths,
                             const BreakParams& params);
// tests: the memo's budget in result words (0 = the default)
void breakMemoBudget(size_t words);

}  // namespace tsr
