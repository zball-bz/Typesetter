// One formula as HTML (math-design.md §8; plans P3-26, P3-27): the box both
// pages write — the typeset page for its laid-out formulas, the plain page
// for its estimated ones (render.math boxes). Stateless, like the writers.
#pragma once
#include <string>
#include <string_view>

#include "../math/math.h"

namespace tsr {

struct MathSpanOpts {
  bool display = false;    // a display formula (its copy is "$ src $")
  bool placed = false;     // absolutely placed in its row at displayTop (the typeset page)
  double displayTop = 0;   // its top, px
  Span span{};             // its source span (data-s/e, relative to srcBase); data-copy-group
  u32 srcBase = 0;
  StrRef color = 0;        // its paint style's colour
  bool label = false;      // (plan P3-27, D-R04) role=math, its source as aria-label
};

// <span class="tsr-math" data-syn="math" data-copy="$src$" …> with its
// glyph runs and rules absolutely placed inside (srcAsWritten: the source as
// the author wrote it, without the dollars)
void writeMathSpan(std::string& out, const MathBox* mb, std::string_view srcAsWritten, const Interner& strs,
                   const MathSpanOpts& o);

}  // namespace tsr
