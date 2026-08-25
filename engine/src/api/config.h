#pragma once
#include "../support/support.h"

namespace tsr {

struct CostParams {
  double exponent = 3;
  double shrinkThreshold = 0.37;
  double shrinkCoeff = 0.6;
};

// Adjacent-punctuation compression style (clreq; v2 App C).
//   Full: every adjacent gap compressed (newspaper-tight)
//   Book: close+close and open+open set solid, but a breakable half-width
//         breathing space is kept between a closing/dot and an opening punct
//   None: full-width style — all punctuation spaces kept (rigid where 禁则
//         forbids a break)
enum class PunctCompress : u8 { Full = 0, Book = 1, None = 2 };

struct Config {
  std::string bodyFont = "\"Crimson Text\", Georgia, serif";
  std::string monoFont = "monospace";
  double baseSizePx = 18;
  double lineHeight = 1.5;
  double paraSpacingEm = 1.2;
  double widthPx = 300;
  double epsilonPerWordSu = 1;  // document-model §6.1
  double cjkJustifyK = 0.6;
  double cjkGlueEm = 0.1;     // per-char stretch capacity (App C)
  double paraIndentEm = 0;    // CJK paragraph indent (2 for 首行缩进); 0 = off
  double hyphenPenalty = 0.7;
  PunctCompress punctCompress = PunctCompress::Book;
  double listIndentEm = 1.5;
  double quoteIndentEm = 1.0;
  CostParams cost;
};

// App C constants (em): punct compressible half, CJK–Latin boundary glue.
constexpr double kPunctHalfEm = 0.5;
constexpr double kCjkBoundaryEm = 0.25;

inline double headingSizeMul(int level) {
  return level == 1 ? 1.6 : level == 2 ? 1.35 : level == 3 ? 1.15 : 1.0;
}

}  // namespace tsr
