// Flow units → breaks → frames with vertical metrics (document-model §8).
#pragma once
#include "../break/break.h"

namespace tsr {

struct LineBox {
  Su y = 0, left = 0, width = 0;
  u32 unitIdx = 0;
  i32 cellIdx = -1;                  // >=0: index into the unit's table cells
  u32 blockBegin = 0, blockEnd = 0;  // trimmed range into the unit's blocks
                                     //   (or the cell's blocks, cellIdx >= 0)
  u32 itemBegin = 0, itemEnd = 0;    // the same line in the unit's (cell's)
                                     //   HList items: what layout and paint read
  double wordDeltaPx = 0;            // raw-px justification value (render uses this)
  double cjkDeltaPx = 0;             // k × wordDeltaPx (v2 §8), letter-spacing value
  i32 wordDeltaSu = 0;               // rounded, for dumps
  i32 cjkDeltaSu = 0;
  u8 join = 0;                       // 0 last (no attr), 1 space, 2 none (hyphen)
  bool endsWithHyphen = false;
  bool noGlue = false;               // no stretchable glue (URL-only line): set ragged
  bool overfull = false;             // holds a run wider than the line (breaker's
                                     // rescue, plan P0-12): set at the shrink limit
  u8 special = 0;                    // 0 text, 1 rule, 2 code, 3 raw, 4 math
  u32 codeLine = 0;                  // special==2: index into unit codeRuns
  u32 cbLo = 0, cbHi = 0;            // special==2: byte slice of the joined line
  bool codeCont = false;             // wrap continuation row (indented, unnumbered)
  u16 contCols = 0;                  // continuation indent, in ch columns
  float snapLatinPx = 0;             // snap-kerning letter-spacing (render)
  float snapCjkPx = 0;
  bool codeHl = false;               // hl-range line (background)
  Su height = 0;                     // row advance (hl background needs it)
  StrRef marker = 0;                 // list marker on the unit's first line
  StrRef anchor = 0;                 // a cell's / caption's anchor, on its first line
  StyleId markerStyle = 0;
  Span srcSpan;
};

struct ParaFrame {
  u32 pid = 0;
  Su x = 0, y = 0, w = 0, h = 0;
  std::vector<LineBox> lines;
};

// the breaks layout made for a stream (tsrc --stage=breaks)
struct UnitBreaks {
  u32 pid = 0, unit = 0;
  i32 cell = -1;  // >= 0: the unit's cell / caption / sidecar row stream
  BreakResult r;
};

struct LayoutResult {
  i64 docHeightSu = 0;
  std::vector<ParaFrame> paras;
  std::vector<UnitBreaks> breaks;  // every stream, in order
};

// Breaks and lays out (plan P1-15: breaking is layout's; the float
// exclusions live at its cursor); overfull streams are reported to diags.
LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics,
                       Interner& strs, const Config& cfg, DiagSink& diags);

std::string dumpBreaks(const LayoutResult& lr);
std::string dumpLayout(const LayoutResult& lr);

}  // namespace tsr
