#pragma once
#include <map>

#include "../shape/textrules.h"
#include "../support/support.h"
#include "settings.gen.h"

namespace tsr {

// Config (every host setting), CostParams and PunctCompress are generated
// from the schema's "settings" rows (plan P1-03): settings.gen.h.

// App C constants (em): punct compressible half, CJK–Latin boundary glue —
// the rules' constants (engine/rules/locale/compat.def, plan P1-11).
constexpr double kPunctHalfEm = kRule_punctHalfEm;
constexpr double kCjkBoundaryEm = kRule_cjkBoundaryEm;
// Table geometry (em): horizontal cell padding, vertical row padding.
constexpr double kTableCellPadEm = 0.4;
constexpr double kTableRowPadEm = 0.3;

inline double headingSizeMul(int level) {
  return level == 1 ? 1.6 : level == 2 ? 1.35 : level == 3 ? 1.15 : 1.0;
}

// (Supplement words are locale terms since plan P1-10: semantic/terms.h,
// engine/data/locale; doc.lang is the host default document language.)

}  // namespace tsr
