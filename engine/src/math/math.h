// Math layout (math-design.md §4–§7): source → MathBox tree, measurement-free.
// The entire box tree is a pure function of (source, sizePx, display) over the
// precompiled MATH artifact — natively golden-testable (--stage=mathbox).
#pragma once
#include <array>
#include <vector>
#include "../measure/measure.h"
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
  u8 cls = 0;                 // TeX atom class (atom.h: kOrd..kInner)
  u8 firstCls = 0, lastCls = 0;  // effective edge classes (glue vs neighbours)
  Su w = 0, asc = 0, desc = 0;   // extents relative to the box baseline
  Su italic = 0;              // italic correction (glyph/base boxes)
  Su topAccent = 0;           // top-accent attachment x (default w/2 at build)
  StrRef text = 0;            // Glyph: UTF-8 character(s) to paint
  float px = 0;               // Glyph: font-size for emission (style-scaled)
  u16 font = 0;               // Glyph: its MathFont id (math/font.h), or kTextFont:
                              //   the document's text font (names, operators)
  std::vector<MathKid> kids;  // HBox children
};

// Text-font runs inside formulas (names, \text{…}) are set in the document's
// body font — the Concrete-Mathematics contract (Euler variables, roman
// names). Their widths are textWidth resources (plan P1-20): the layouter
// reads them through a MeasureNeeds; what is missing is a need of the pass
// that laid the formula out, which then defers (an inline formula: its
// object, resolved in Measure; a display formula: its top-level block).

// Parses + lays out one formula (parseDiags false: the parse diagnostics
// were reported when it was prepared, plan P1-25). Errors/diags are non-fatal: the returned box
// degrades to an upright text rendering of the source. `display` selects
// display style (mathblock); inline formulas use text style.
struct MathScope;  // env.h: what names bind to (plan P2-15)
MathBox* layoutMathFormula(std::string_view src, bool display, double sizePx,
                           Arena& arena, Interner& strs, DiagSink& diags,
                           Span span, const MeasureNeeds* text = nullptr, bool parseDiags = true,
                           const MathScope* scope = nullptr);

// Inline-formula line breaking (math-design.md §9; plan P3-25): the formula
// splits into unbreakable segments where its top-level atoms allow a break
// (MathBreaks: a penalty after and before each atom class; a relation by
// default may head the continuation line — the CJK convention — and TeX
// breaks after it and after a binary operator). Display formulas and
// degraded parses stay one segment. glueBefore is the inter-atom glue the
// break consumes, penalty its cost.
struct MathBreaks {
  std::array<double, 8> after{}, before{};  // by atom class (ord … inner); < 0: no break there
};
struct MathSeg {
  MathBox* box;
  Su glueBefore = 0;
  float penalty = 0;
};
std::vector<MathSeg> layoutMathSegments(std::string_view src, bool display,
                                        double sizePx, Arena& arena,
                                        Interner& strs, DiagSink& diags,
                                        Span span, const MathBreaks& breaks, const MeasureNeeds* text = nullptr,
                                        bool parseDiags = true, const MathScope* scope = nullptr);

// (plan P3-29; design T8 S9, D-S11) a display formula's rows: split at its
// top-level row breaks (`\` ending a line), a row's cells at its top-level
// alignment points (`&`) — TeX's align: columns alternate right and left, a
// pair joined (its right cell opening as after an Ord), pairs an em apart.
// A formula of one row without `&` is one cell, its box the formula's.
struct MathCell {
  MathBox* box = nullptr;
  Su lead = 0;  // the room before it (a pair's right cell: the Ord glue)
};
struct MathRows {
  std::vector<std::vector<MathCell>> rows;
  Su em = 0;    // its size's em (the column gap)
  bool aligned() const;  // more than one row, or a row of more than one cell
};
MathRows layoutMathRows(std::string_view src, double sizePx, Arena& arena, Interner& strs, DiagSink& diags,
                        Span span, const MeasureNeeds* text = nullptr, bool parseDiags = true,
                        const MathScope* scope = nullptr);
// the rows of a group (an equations block's formulas, or one formula) with
// their columns shared: each row a box as wide as the group's columns —
// rows set one under the other align at their `&`. out[i]: member i's rows.
void alignMathRows(const std::vector<const MathRows*>& group, Arena& arena,
                   std::vector<std::vector<MathBox*>>& out);

std::string dumpMathBox(const MathBox* box, const Interner& strs);

}  // namespace tsr
