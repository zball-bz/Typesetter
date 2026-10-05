// Knuth–Plass line breaking with TeX item semantics (plan P0-12; the rules
// are at the top of break.cc, the item projection in items.h). su inputs,
// bounded double costs.
#pragma once
#include "../emit/emit.h"

namespace tsr {

struct BItem;

struct BreakResult {
  std::vector<u32> breakpoints;    // counts of blocks consumed per line, ascending
  double cost = 0;                 // demerits
  std::vector<u32> overfullLines;  // rescued lines (final pass): content wider than the line
  bool feasible = true;            // false: no path without the final-pass rescue
};

// Prefix form (figure-design.md §4, TeX parshape-in-lines): the first
// `narrowK` lines run beside a float at the narrowed width.
struct LineWidths {
  Su constant;
  Su narrow = 0;
  u32 narrowK = 0;
  Su at(u32 i) const { return (i < narrowK && narrow > 0) ? narrow : constant; }
};

BreakResult breakLines(const std::vector<BreakBlock>& blocks, LineWidths widths,
                       const CostParams& params, u32 cursorSearchRange = 5,
                       bool finalPass = false);
BreakResult breakItems(const std::vector<BItem>& items, u32 nBlocks, LineWidths widths,
                       const CostParams& params, u32 cursorSearchRange, bool finalPass);

// Cached form with the retry ladder folded in (editor-design.md §2): ±5,
// then wider windows while no feasible path exists, then the unbounded
// final pass with the Overfull rescue. KP reads ONLY block geometry (width,
// spaceWidth, breakWidth, breakPenalty, kind flags) plus the line widths
// and cost params, so results are keyed by that and shared process-wide
// across documents — an editing session re-breaks only the paragraphs a
// keystroke actually changed.
BreakResult breakLinesRetry(const std::vector<BreakBlock>& blocks, LineWidths widths,
                            const CostParams& params);

}  // namespace tsr
