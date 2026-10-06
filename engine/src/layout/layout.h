// The box tree's leaves → breaks → frames of fragments with vertical metrics
// (document-model §8; plan P1-18: a layouter per LayouterId, the box tree's
// traits and gaps). Layout never sees the content tree (architecture lint).
#pragma once
#include "../boxtree/block.h"
#include "../break/break.h"
#include "../resource/box.h"

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
  Frame,    // (plan P3-14) a framed block's padding, border and background, under its content
};
// (plan P3-12; design T6 VList penalties, D-Y04) the tier of a page break
// just before a fragment — the layouters declare it, pagination relaxes it in
// this order when a page cannot be filled otherwise: KeepTogether, then
// WidowOrphan, then KeepWithNext; a Structural joint (inside a wrapped code
// line, a table row, a float box) is never broken — an atom taller than the
// page overflows it, visibly; Forced ends the page
enum class PenTier : u8 { Normal, KeepTogether, WidowOrphan, KeepWithNext, Structural, Forced };
// a fragment's paged role (pagination's mechanisms; their producers come
// with page floats, table headers and footnote inserts)
constexpr u8 kPagedMovable = 1;  // a page float: to the top of its page, or the next
constexpr u8 kPagedInsert = 2;   // a footnote insert: to the bottom of its reference's page
constexpr u8 kPagedHeader = 4;   // a table header row: repeated atop a continuation page
constexpr u8 kPagedFrame = 8;    // (plan P3-14) a frame: drawn, clipped, on every sheet its block meets
constexpr u8 kPagedBottom = 16;  // (plan P3-15) a movable box: to the bottom of its page, not the top
constexpr u8 kPagedPage = 32;    // (plan P3-15) a movable box: to a sheet of floats after its page
// widows and orphans: lines a paragraph keeps together at a page cut
constexpr u32 kOrphans = 2, kWidows = 2;

struct Fragment {
  FragKind kind = FragKind::Line;
  Su y = 0, left = 0, width = 0;     // y: the top (a rule's too)
  Su height = 0;                     // the advance (a rule's band extent)
  Su baseline = 0;                   // the baseline below y: the CSS inline formula
                                     //   (half the leading above the extents)
  u32 unitIdx = 0;                   // the leaf (TopTree::leaves)
  i32 cellIdx = -1;                  // >=0: the leaf's other track (a float's caption
                                     //   row, a code block's sidecar row)
  i32 gridCell = -1;                 // (plan P3-10) >=0: the table cell it sits in
  u32 table = ~0u;                   // the table block it belongs to (its rules too):
                                     //   one atomic group on paged sheets
  u32 blockBegin = 0, blockEnd = 0;  // trimmed range into the stream's blocks
  u32 itemBegin = 0, itemEnd = 0;    // the same line in the stream's HList
                                     //   items: what layout and paint read
  double wordDeltaPx = 0;            // raw-px justification value (render uses this)
  double cjkDeltaPx = 0;             // k × wordDeltaPx (v2 §8), letter-spacing value
  i32 wordDeltaSu = 0;               // rounded, for dumps
  i32 cjkDeltaSu = 0;
  Sep sep = Sep::Newline;            // what joins it to the next line in content text
  PenTier brk = PenTier::Normal;     // a page break just before it (plan P3-12)
  u8 paged = 0;                      // its paged role (kPaged*)
  u32 insertAt = ~0u;                // an insert: the source position of its reference
  u32 boxBlock = ~0u;                // (plan P3-14) a frame: its block (the top's blocks)
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
  bool hostBox = false;              // (plan P3-28) a raw box the host measures at its width
  bool placeholder = false;          // (plan P3-32) an image without its size (failed): paint writes its alt box
  u16 mathRow = 0;                   // (plan P3-29) Math: which row of its formula
  StrRef marker = 0;                 // a list marker / line number in the gutter
  StyleId markerStyle = 0;
  // (plan P3-16; design T7 Placement) what the marker is, and where it
  // stands: a transitional encoding — its end edge at the line's start (CSS
  // right:100%, unmeasured; P3-26 measures it)
  enum class Marker : u8 { List, LineNumber } markerRole = Marker::List;
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
  u32 block = ~0u;   // a container's box (a table, plan P3-10): its block
};

struct ParaFrame {
  u32 pid = 0;
  Su x = 0, y = 0, w = 0, h = 0;
  Su overflowR = 0;  // (plan P3-14) > w: content past the measure (a table wider than it, D-Y09)
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
  Su gutterSu = 0;  // (plan P3-16) how far the line numbers stand left of the measure (paged sheets show it)
  std::vector<ParaFrame> paras;
  std::vector<UnitBreaks> breaks;  // every stream, in order
};

// Breaks and lays out (plan P1-15: breaking is layout's; the float
// exclusions live at its cursor); overfull streams are reported to diags.
// memo: the Session's KP memo (plan P1-21), or none (uncached). paged
// (plan P3-14): the medium — blocks of the other one (media) are left out.
// boxes (plan P3-28): where a host box's height at its width comes from
// (none: its declared height)
LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics,
                       Interner& strs, const LayoutSettings& cfg, DiagSink& diags, BreakMemo* memo = nullptr,
                       bool paged = false, BoxAsker* boxes = nullptr);

std::string dumpBreaks(const LayoutResult& lr);
std::string dumpLayout(const LayoutResult& lr);
std::string dumpVList(const LayoutResult& lr, const std::vector<TopBlock>& tops);

}  // namespace tsr
