// Math layout (math-design.md §4–§7): source → MathBox tree, measurement-free.
// The entire box tree is a pure function of (source, sizePx, display) over the
// precompiled MATH artifact — natively golden-testable (--stage=mathbox).
#pragma once
#include "../support/support.h"

namespace tsr {

// Box kinds (math-design.md §4). Everything composite is an HBox of
// positioned children; vertical stacking is a layout procedure, not a kind.
enum class MathKind : u8 { Glyph, Rule, HBox, Spacer };

struct MathBox;
struct MathKid {
  Su dx = 0;   // child origin x within the parent box
  Su dy = 0;   // child BASELINE relative to parent baseline; positive = UP
  MathBox* box = nullptr;
};

struct MathBox {
  MathKind kind = MathKind::HBox;
  u8 cls = 0;                 // TeX atom class (mathfont::kOrd..kInner)
  u8 firstCls = 0, lastCls = 0;  // effective edge classes (glue vs neighbours)
  Su w = 0, asc = 0, desc = 0;   // extents relative to the box baseline
  Su italic = 0;              // italic correction (glyph/base boxes)
  StrRef text = 0;            // Glyph: UTF-8 character(s) to paint
  float px = 0;               // Glyph: font-size for emission (style-scaled)
  std::vector<MathKid> kids;  // HBox children
};

// Parses + lays out one formula. Errors/diags are non-fatal: the returned box
// degrades to an upright text rendering of the source. `display` selects
// display style (mathblock); inline formulas use text style.
MathBox* layoutMathFormula(std::string_view src, bool display, double sizePx,
                           Arena& arena, Interner& strs, DiagSink& diags,
                           Span span);

std::string dumpMathBox(const MathBox* box, const Interner& strs);

}  // namespace tsr
