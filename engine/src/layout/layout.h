// The box tree's leaves → breaks → frames of fragments with vertical metrics
// (document-model §8; plan P1-18: a layouter per LayouterId, the box tree's
// traits and gaps). Layout never sees the content tree (architecture lint).
#pragma once
#include "../boxtree/block.h"
#include "../break/break.h"

namespace tsr {

// What layout produces (plan P1-18; design T6 Fragment, T7 LaidOutLine /
// LaidOutBox): one record per materialized line or box, in paint order. Its
// kind is what it is geometrically — paint reads the leaf's payload by the
// leaf's painter, never a magic number.
enum class FragKind : u8 {
  Line,     // a line of an inline stream (a paragraph, a cell, a caption row, a sidecar row)
  Rule,     // a horizontal rule (a rule block, a table's rules)
  CodeRow,  // a row of a code block's grid
  Raw,      // handler-declared markup of a declared height
  Math,     // a display formula's row
  Image,    // an image (or its placeholder)
};
struct Fragment {
  FragKind kind = FragKind::Line;
  Su y = 0, left = 0, width = 0;     // y: the top (a rule's too)
  Su height = 0;                     // the advance (a rule's band extent)
  Su baseline = 0;                   // the baseline below y: the CSS inline formula
                                     //   (half the leading above the extents)
  u32 unitIdx = 0;                   // the leaf (TopTree::leaves)
  i32 cellIdx = -1;                  // >=0: the leaf's other track (a table cell,
                                     //   a caption row, a sidecar row)
  u32 blockBegin = 0, blockEnd = 0;  // trimmed range into the stream's blocks
  u32 itemBegin = 0, itemEnd = 0;    // the same line in the stream's HList
                                     //   items: what layout and paint read
  double wordDeltaPx = 0;            // raw-px justification value (render uses this)
  double cjkDeltaPx = 0;             // k × wordDeltaPx (v2 §8), letter-spacing value
  i32 wordDeltaSu = 0;               // rounded, for dumps
  i32 cjkDeltaSu = 0;
  Sep sep = Sep::Newline;            // what joins it to the next line in content text
  bool spanned = false;              // srcSpan holds even when empty (a blank code row)
  bool endsWithHyphen = false;
  bool ragged = false;               // a line that is not justified (its stream's alignment)
  bool noGlue = false;               // no stretchable glue (URL-only line): set ragged
  double fillPx = 0;                 // each fill's share of the slack (plan P2-16)
  bool overfull = false;             // holds a run wider than the line (breaker's
                                     // rescue, plan P0-12): set at the shrink limit
  u32 codeLine = 0;                  // CodeRow: the logical line (GridData::lines)
  u32 cbLo = 0, cbHi = 0;            // CodeRow: byte slice of the joined line
  bool codeCont = false;             // wrap continuation row (indented, unnumbered)
  u16 contCols = 0;                  // continuation indent, in ch columns
  float snapLatinPx = 0;             // snap-kerning letter-spacing (render)
  float snapCjkPx = 0;
  bool codeHl = false;               // hl-range line (background)
  StrRef marker = 0;                 // a list marker / line number in the gutter
  StyleId markerStyle = 0;
  StrRef anchor = 0;                 // the id it carries: exactly one fragment per
                                     //   anchored block (stateless, plan P1-18)
  StrRef anchor2 = 0;                // an enclosing block's label sharing it
  Span srcSpan;
};

// the vertical list of a top (tsrc --stage=vlist): each leaf's box, the gap
// before it (its stacks' gap rule) and what clearing a float added
struct VEntry {
  u32 unit = 0;
  Su gap = 0, clear = 0;
  Su y = 0, h = 0;
  bool out = false;  // a float: out of flow, no advance
};

struct ParaFrame {
  u32 pid = 0;
  Su x = 0, y = 0, w = 0, h = 0;
  std::vector<Fragment> lines;
  std::vector<VEntry> vlist;
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
// memo: the Session's KP memo (plan P1-21), or none (uncached)
LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics,
                       Interner& strs, const LayoutSettings& cfg, DiagSink& diags, BreakMemo* memo = nullptr);

std::string dumpBreaks(const LayoutResult& lr);
std::string dumpLayout(const LayoutResult& lr);
std::string dumpVList(const LayoutResult& lr, const std::vector<TopBlock>& tops);

}  // namespace tsr
