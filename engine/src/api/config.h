#pragma once
#include <map>

#include "../support/support.h"
#include "settings.gen.h"

namespace tsr {

// Config (every host setting), CostParams and PunctCompress are generated
// from the schema's "settings" rows (plan P1-03): settings.gen.h.

// App C constants (em): punct compressible half, CJK–Latin boundary glue.
constexpr double kPunctHalfEm = 0.5;
constexpr double kCjkBoundaryEm = 0.25;
// Table geometry (em): horizontal cell padding, vertical row padding.
constexpr double kTableCellPadEm = 0.4;
constexpr double kTableRowPadEm = 0.3;

inline double headingSizeMul(int level) {
  return level == 1 ? 1.6 : level == 2 ? 1.35 : level == 3 ? 1.15 : 1.0;
}

// Document language → supplement words (real-world-report.md): CJK
// documents keep 图/表/式 and the full-width colon; everything else gets
// English supplements. Heading refs stay "§" in both.
inline void applyLang(Config& c, std::string_view lang) {
  std::string_view p = lang.substr(0, 2);
  if (p == "zh" || p == "ja") {
    c.supTable = "\xE8\xA1\xA8 ";
    c.supFigure = "\xE5\x9B\xBE ";
    c.supEquation = "\xE5\xBC\x8F ";
    c.capSep = "\xEF\xBC\x9A";
  } else {
    c.supTable = "Table ";
    c.supFigure = "Figure ";
    c.supEquation = "Eq. ";
    c.capSep = ": ";
  }
}

}  // namespace tsr
