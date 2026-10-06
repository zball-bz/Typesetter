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

// A block's traits (design T6 BlockTraits, the subset today's layout uses):
// a view compiled from its node's block properties (plan P3-01: NodeProps;
// the default stylesheet, engine/data/defaults.json, gives today's values).
struct BlockTraits {
  // the gap between this block's children, in paragraph gaps (num/den,
  // integer division) or a length (gapSu); 0/0 and 0 inherit the parent's
  u8 gapNum = 0, gapDen = 0;
  Su gapSu = 0;
  enum class Align : u8 { Justify, Ragged, Center, End } align = Align::Justify;
  bool hyphenate = true;
  bool keepWithNext = false;  // paged: never the last block on a sheet
};
// what a block is (its layouter's case; the dumps' name)
enum class TraitsId : u8 {
  Root, Para, Caption, Heading, List, Item, Quote, Group, Figure, Code, Table, Image, Float, Math, Raw,
  Rule, Error, Marker, N
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
  bool leaf() const { return unit != ~0u; }
};

// one top-level block: the swap unit of the editing loop (pid)
struct TopTree {
  u32 pid = 0;
  std::vector<LayoutBlock> blocks;  // pre-order
  std::vector<u32> leaves;          // unit → block
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
