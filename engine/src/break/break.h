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

struct LineWidths {
  Su constant;
  Su at(u32) const { return constant; }
};

BreakResult breakLines(const std::vector<LinebreakBlock>& blocks, LineWidths widths,
                       const CostParams& params, u32 cursorSearchRange = 5);

}  // namespace tsr
