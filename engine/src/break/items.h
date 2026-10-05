// The breaker's item projection (plan P0-12; design T6 S1, TeX semantics).
//
// The legacy block (BreakBlock: fuseLegacy's lowering of an HList, plan
// P1-12) fuses box width, glue capacity and penalty under BF_* flags; every
// layer used to re-derive what a break discards. The breaker reads TeX items
// instead:
//   Box      w                       (a word, glyph, formula, indent)
//   Glue     w, stretch, shrink      (discarded at a break and at a line start)
//   Penalty  pen | Forbidden | Forced (a break here keeps what precedes it)
//   Disc     w (not broken), pre     (a hyphenation point)
// Legal breaks: a Glue preceded by a Box or Disc; a Penalty that is not
// Forbidden; a Disc whose penalty is not Forbidden. A Forced penalty must
// break, wherever it appears; the paragraph end is an implicit Forced break.
//
// Until the item-native breaker reads the HList (P4-08), blocksToItems is
// the adapter:
//   space / boundary / punct half   -> [Penalty(pen) if pen != 0] Glue(w, cap, cap)
//   CJK char                        -> Box, Penalty(Forbidden), Glue(0, cap, cap), Penalty(pen)
//   hyphen point                    -> Disc{w = junction kern, pre = hyphen width}
//   anything else                   -> Box [, Penalty(pen) when breakable]
// "cap" is the block's spaceWidth (today's stretch = shrink capacity). Each
// item records its source block: a break at an item consumes the blocks up to
// and including `block` (breakpoints count blocks, as layout reads them).
#pragma once
#include <cmath>
#include <type_traits>

#include "../emit/emit.h"

namespace tsr {

enum class ItemKind : u8 { Box, Glue, Penalty, Disc };
enum class PenTag : u8 { Normal, Forbidden, Forced };

struct BItem {
  ItemKind k = ItemKind::Box;
  u8 order = 0;  // stretch order: 0 finite, 1 fil
  PenTag tag = PenTag::Normal;
  u8 reserved0 = 0;
  Su w = 0;                // Box/Glue width; Disc: width when NOT broken here
  Su stretch = 0, shrink = 0;
  i32 pen = 0;             // Penalty/Disc: thousandths
  Su pre = 0;              // Disc: width that ends the line when broken here
  u32 block = 0;           // source block; a break here consumes blocks [.., block]
};
// integral fields, no padding: the bytes are an exact cache-key domain
static_assert(std::has_unique_object_representations_v<BItem>);

// The one conversion from the emit-side float penalty (BREAK_INF = never).
inline bool penForbidden(float p) { return !(p < BREAK_INF); }
inline i32 penThousandths(float p) { return (i32)std::lround((double)p * 1000.0); }

inline void blocksToItems(const std::vector<BreakBlock>& blocks, std::vector<BItem>& out) {
  out.clear();
  out.reserve(blocks.size() * 2);
  auto penalty = [&](u32 bi, float p) {
    BItem it;
    it.k = ItemKind::Penalty;
    it.block = bi;
    if (penForbidden(p)) it.tag = PenTag::Forbidden;
    else it.pen = penThousandths(p);
    out.push_back(it);
  };
  for (u32 bi = 0; bi < (u32)blocks.size(); bi++) {
    const BreakBlock& b = blocks[bi];
    if (b.isHyphen()) {
      BItem it;
      it.k = ItemKind::Disc;
      it.block = bi;
      it.w = b.width;  // the junction kern: the pieces shape as one run
      it.pre = b.breakWidth;
      if (penForbidden(b.breakPenalty)) it.tag = PenTag::Forbidden;
      else it.pen = penThousandths(b.breakPenalty);
      out.push_back(it);
      continue;
    }
    if (b.isSpace()) {
      // a glue break carries its penalty on a Penalty in front of it (the
      // glue then goes to the next line start and is discarded there); a
      // Forbidden one keeps the glue from being a legal break at all
      if (penForbidden(b.breakPenalty) || b.breakPenalty != 0) penalty(bi, b.breakPenalty);
      BItem g;
      g.k = ItemKind::Glue;
      g.block = bi;
      g.w = b.width;
      g.stretch = g.shrink = b.spaceWidth;
      out.push_back(g);
      continue;
    }
    BItem box;
    box.k = ItemKind::Box;
    box.block = bi;
    box.w = b.width;
    out.push_back(box);
    if (b.spaceWidth != 0) {  // CJK char: its attached gap capacity
      penalty(bi, BREAK_INF);
      BItem g;
      g.k = ItemKind::Glue;
      g.block = bi;
      g.stretch = g.shrink = b.spaceWidth;
      out.push_back(g);
      penalty(bi, b.breakPenalty);
    } else if (!penForbidden(b.breakPenalty)) {
      penalty(bi, b.breakPenalty);
    }
  }
}

}  // namespace tsr
