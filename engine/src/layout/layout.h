// Flow units + breaks + vertical metrics → frames (document-model §8).
#pragma once
#include "../break/break.h"

namespace tsr {

struct LineBox {
  Su y = 0, left = 0, width = 0;
  u32 unitIdx = 0;
  i32 cellIdx = -1;                  // >=0: index into the unit's table cells
  u32 blockBegin = 0, blockEnd = 0;  // trimmed range into the unit's blocks
                                     //   (or the cell's blocks, cellIdx >= 0)
  double wordDeltaPx = 0;            // raw-px justification value (render uses this)
  double cjkDeltaPx = 0;             // k × wordDeltaPx (v2 §8), letter-spacing value
  i32 wordDeltaSu = 0;               // rounded, for dumps
  i32 cjkDeltaSu = 0;
  u8 join = 0;                       // 0 last (no attr), 1 space, 2 none (hyphen)
  bool endsWithHyphen = false;
  u8 special = 0;                    // 0 text, 1 rule, 2 code, 3 raw, 4 math
  u32 codeLine = 0;                  // special==2: index into unit codeRuns
  u32 cbLo = 0, cbHi = 0;            // special==2: byte slice of the joined line
  bool codeCont = false;             // wrap continuation row (indented, unnumbered)
  u16 contCols = 0;                  // continuation indent, in ch columns
  bool codeHl = false;               // hl-range line (background)
  Su height = 0;                     // row advance (hl background needs it)
  StrRef marker = 0;                 // list marker on the unit's first line
  StyleId markerStyle = 0;
  Span srcSpan;
};

struct ParaFrame {
  u32 pid = 0;
  Su x = 0, y = 0, w = 0, h = 0;
  std::vector<LineBox> lines;
};

struct LayoutResult {
  i64 docHeightSu = 0;
  std::vector<ParaFrame> paras;
};

LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics,
                       Interner& strs, const Config& cfg);

std::string dumpLayout(const LayoutResult& lr);

}  // namespace tsr
