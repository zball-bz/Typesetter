// The box tree (plan P1-18; design T6 "Box tree"): the one contract between
// the content tree and layout. Built after resolve (stage BoxTree), it fixes
// per document state the block structure, the layouter of every block
// (chosen by content model), its traits (TraitTable) and its anchor; emit
// shapes the leaves' content and layout walks the tree with its layouters.
// Nothing here depends on the measure, and nothing downstream compares role
// strings or kinds. Layout reads this header; it must not see model.h
// (architecture lint).
#pragma once
#include <string>
#include <vector>

#include "../model/style.h"
#include "../support/support.h"

namespace tsr {

// chosen by content model: inline content → Paragraph; verbatim → Grid
// (with its sidecar track); table rows → Table; an image, raw markup, a rule
// or a display formula → Replaced; block children → Stack
enum class LayouterId : u8 { Paragraph, Stack, Replaced, Grid, Table };
// what a Replaced block paints
enum class Painter : u8 { None, Rule, Image, Raw, MathRow };

// (plan P3-07; design T6/T7 Sep) what joins a line to the next in content
// text — copy's separator: a break's (Space: a consumed source space; None:
// a hyphen, a CJK break, no space) inside a stream; at a stream's end its
// track's or unit's (Tab after a table cell, Row after a row's last cell,
// Para after a unit the semantic page sets apart); Newline: a real line
// boundary — a forced break, a code line's end, a block's last line (a
// change of block implies a paragraph). Layout produces it, once per line.
enum class Sep : u8 { Newline, Space, None, Tab, Row, Para };
// its data-join spelling (Newline: no attribute)
inline const char* sepName(Sep s) {
  switch (s) {
    case Sep::Newline: return nullptr;
    case Sep::Space: return "space";
    case Sep::None: return "none";
    case Sep::Tab: return "tab";
    case Sep::Row: return "row";
    case Sep::Para: return "para";
  }
  return nullptr;
}

// A block's traits (design T6 BlockTraits, the subset today's layout uses):
// a view compiled from its node's block properties (plan P3-01: NodeProps;
// the default stylesheet, engine/data/defaults.json, gives today's values).
struct BlockTraits {
  // the gap between this block's children, in paragraph gaps (num/den,
  // integer division) or a length (gapSu); 0/0 and 0 inherit the parent's
  u8 gapNum = 0, gapDen = 0;
  Su gapSu = 0;
  enum class Align : u8 { Justify, Ragged, Center, End } align = Align::Justify;  // its LineEnds preset (P3-09)
  bool singleCenter = false;  // par.singleLine center: a one-line paragraph is centred (D-Y05)
  bool hyphenate = true;
  bool keepWithNext = false;  // paged: never the last block on a sheet
  // a code block's grid (plan P3-02: codeblock.* rows)
  bool snapKerning = false;
  float sidecarFrac = 0.4f;
  i32 contIndent = 2;
  // (plan P3-14; design T6 S12) the block trait group: a page cut inside
  // it avoided (keep together; with-next is keepWithNext), the space above
  // and below it (collapsing with its neighbours' and the stack's gap), a
  // page break before / after it, its paragraphs' hanging indent (lines
  // after the first hangAfter start `hang` in), where it shows (media:
  // 0 all, 1 screen, 2 paged), whether it narrows beside a float instead of
  // clearing it, its breaker's tolerance and emergency stretch
  bool keepTogether = false;
  Su spaceBefore = 0, spaceAfter = 0;
  bool breakBefore = false, breakAfter = false;
  Su hang = 0;
  u16 hangAfter = 1;
  u8 media = 0;
  bool shrink = false;
  double tolerance = -1;  // kNoTolerance
  Su emergencyStretch = 0;
};
// (plan P3-14; design T6 BoxModel) a framed block: its padding and border
// (top, right, bottom, left), their colours; its content box is inset by
// both, and a frame fragment paints them under its content
struct BoxModel {
  Su pad[4] = {0, 0, 0, 0}, border[4] = {0, 0, 0, 0};
  StrRef borderColor = 0, background = 0;
  bool framed() const {
    for (int i = 0; i < 4; i++)
      if (pad[i] || border[i]) return true;
    return background != 0;
  }
  Su inset(int side) const { return pad[side] + border[side]; }
};
// what a block is (its layouter's case; the dumps' name)
enum class TraitsId : u8 {
  Root, Para, Caption, Heading, List, Item, Quote, Group, Figure, Code, Table, Image, Float, Math, Raw,
  Rule, Error, Marker, Cell, N
};

// (plan P3-10; design T6 TableSpec) a table's tracks: Fr(1) columns, or a
// Percent share of the available width, each with its halign ('l', 'c',
// 'r'); its children are one Cell block per cell, row-major by its first
// position (a position no cell covers holds an empty one; plan P3-14:
// cells may span rows and columns). A v1 table (`cols`, `align`) is
// framed: cell padding (table.cellPad / rowPad) and full-width rules. A
// code block with sidecar notes is a two-track table of its lines (plan
// P3-11): its code beside its notes a code em apart, unframed, a row per
// logical line — a row ends a line (no tab or row separator), and a page
// may break between rows.
struct ColSpec {
  float percent = 0;     // > 0: a Percent track; else Fr(1)
  u8 align = 'l';
  bool sidecar = false;  // a code block's notes (data-track="sidecar")
  // (plan P3-14; design T6 SizeSpec) its width: a share of the room left
  // (Fr: fr), a fixed length, a percent (above), or content-fitted (Auto
  // between the cells' min- and max-content, Min, Max)
  enum class W : u8 { Fr, Fixed, Auto, Min, Max } w = W::Fr;
  float fr = 1;
  Su fixed = 0;
};
// (plan P3-14) a cell's place: its first row and column, its spans, its
// own alignment (0: the column's) and vertical alignment in its rows
struct CellPlace {
  u32 row = 0, col = 0, rowspan = 1, colspan = 1;
  u8 halign = 0;
  enum class V : u8 { Top, Middle, Bottom } valign = V::Top;
};
struct TableSpec {
  std::vector<ColSpec> cols;
  float gapCodeEm = 0;  // between tracks, in code em
  bool framed = true;   // the cell padding (table.cellPad / rowPad)
  bool lines = false;
  // (plan P3-14) its rules: a full grid (v1: above, between and below the
  // rows), booktabs (above, under the header rows, below) or none; its
  // header rows (repeated atop a continuation sheet); its cells' places, in
  // the order of its cell blocks (row-major by first position), its rows
  enum class Rules : u8 { Grid, Booktabs, None } rules = Rules::Grid;
  u32 header = 0;
  std::vector<CellPlace> place;
  u32 rows = 0;
};
const char* traitsName(TraitsId t);

// One block, in pre-order: blocks[0] is the top's root; a block's subtree is
// [its index, end), its kids are i+1, then each kid's end.
struct LayoutBlock {
  LayouterId layouter = LayouterId::Stack;
  Painter painter = Painter::None;
  TraitsId traits = TraitsId::Group;
  BlockTraits tr;  // its node's block properties (plan P3-01)
  u8 floatSide = 0;  // Replaced image: 1 left, 2 right (F2 float)
  u32 parent = ~0u;
  u32 end = 0;       // one past the subtree's last block
  u32 unit = ~0u;    // a leaf: its unit (emit's FlowUnit index)
  Su x = 0;          // the start edge (the indents of its ancestors)
  Su pad = 0;        // added to its children's start edge (a list, a quote)
  StrRef anchor = 0;  // its label: exactly one fragment of the block carries it
  // a leaf: the anchor its first fragment carries — its own label or an
  // enclosing block's whose first leaf it is (the deepest wins a shared leaf)
  StrRef carry = 0;
  StrRef carry2 = 0;  // a second enclosing label sharing that leaf
  // a list item's marker, painted in the gutter of the leaf's first line
  StrRef marker = 0;
  StyleId markerStyle = 0;
  Span span;
  // (plan P3-09) a float leaf: its caption rows' own block properties
  BlockTraits rowTr;
  u32 spec = ~0u;  // (plan P3-10) a table: its TopTree::tables entry
  // (plan P3-07) a leaf: the separator after its last line in content text
  // — Para where the semantic page sets it apart, Newline in a tight list
  // item and at the top's end (a new block is a new paragraph anyway)
  Sep sepAfter = Sep::Newline;
  // (plan P3-13) a deferred flow's entry: its marker's source position (its
  // fragments are the paged sheets' inserts); kInsertArea: their separator
  u32 insertAt = kNotInsert;
  // (plan P3-14) its end edge's inset (the right paddings and borders of its
  // ancestors, as x is the start's) and its own box
  Su xr = 0;
  BoxModel box;
  bool leaf() const { return unit != ~0u; }
};

// one top-level block: the swap unit of the editing loop (pid)
struct TopTree {
  u32 pid = 0;
  std::vector<LayoutBlock> blocks;  // pre-order
  std::vector<u32> leaves;          // unit → block
  std::vector<TableSpec> tables;    // the tables' tracks (plan P3-10)
  u32 lostAnchors = 0;              // labels a third on one leaf (none carries them)
};

// the effective gap between a stack's children, in paragraph gaps
// (a length gap: num = 0, den = 0, su set)
inline void gapOf(const TopTree& t, u32 b, u8& num, u8& den, Su& su) {
  su = 0;
  for (u32 i = b; i != ~0u; i = t.blocks[i].parent) {
    const BlockTraits& tr = t.blocks[i].tr;
    if (tr.gapDen || tr.gapSu) {
      num = tr.gapNum;
      den = tr.gapDen;
      su = tr.gapSu;
      return;
    }
  }
  num = den = 1;  // the root's
}

// tsrc --stage=blocktree
std::string dumpBlockTree(const std::vector<TopTree>& tops, const Interner& strs);

}  // namespace tsr
