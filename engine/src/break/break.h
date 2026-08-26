// Knuth–Plass DP — faithful port of the PoC (src/linebreak.ts) plus the
// zero-break endpoint fix; operates on su-quantized blocks, cost math in
// double px as in the PoC.
#pragma once
#include "../emit/emit.h"

namespace tsr {

struct BreakResult {
  std::vector<u32> breakpoints;  // counts of blocks consumed per line, ascending
  double cost = 0;
};

// Prefix form (figure-design.md §4, TeX parshape-in-lines): the first
// `narrowK` lines run beside a float at the narrowed width.
struct LineWidths {
  Su constant;
  Su narrow = 0;
  u32 narrowK = 0;
  Su at(u32 i) const { return (i < narrowK && narrow > 0) ? narrow : constant; }
};

BreakResult breakLines(const std::vector<LinebreakBlock>& blocks, LineWidths widths,
                       const CostParams& params, u32 cursorSearchRange = 5);

}  // namespace tsr
