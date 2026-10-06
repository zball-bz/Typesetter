// The breaker's items (plan P0-12; design T6 S1/S16, TeX semantics), read
// straight off the HList (plan P4-08: the item-native breaker; finding
// break-layout-pages/glue-semantics-split):
//   Box      w                        (a word, glyph, formula part, indent)
//   Glue     w, stretch, shrink       (discarded at a break and at a line start)
//   Penalty  pen | Forbidden | Forced (a break here keeps what precedes it)
//   Disc     w (not broken), pre      (a break inside a word: its pre ends the line)
// Legal breaks: a Glue preceded by a Box or Disc; a Penalty that is not
// Forbidden; a Disc whose penalty is not Forbidden. A Forced penalty must
// break, wherever it appears; the paragraph end is an implicit Forced break.
//
// The stretch model is v2 §8's (D-X01; finding emitter/kp-ignores-stretch-
// weight): a glue's stretch and shrink are its weight times the stream's
// justification unit, `HList::juSu` (a space of its base style) — 1 for a
// word space and the CJK–Latin boundary, k for a realized CJK gap, 0 for a
// punctuation blank or an object's glue (rigid) —, so the breaker's
// capacity is (n_latin + k·n_cjk)·juSu and the renderer's per-gap
// adjustment, slack / Σweights times each weight, agree by construction. A
// fill (plan P2-16) is fil glue. Each item records its HList index.
#pragma once
#include <cmath>
#include <type_traits>

#include "../shape/hlist.h"

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
  u32 src = 0;             // its HList item
};
// integral fields, no padding: the bytes are an exact cache-key domain
static_assert(std::has_unique_object_representations_v<BItem>);

// The one conversion from the emit-side float penalty (kPenInf = never,
// -kPenInf = a forced break: a hard line break, plan P1-13).
inline bool penForbidden(float p) { return !(p < kPenInf); }
inline bool penForced(float p) { return p <= -kPenInf; }
inline i32 penThousandths(float p) { return (i32)std::lround((double)p * 1000.0); }

inline void hlistToItems(const HList& h, std::vector<BItem>& out) {
  out.clear();
  out.reserve(h.items.size());
  for (u32 i = 0; i < (u32)h.items.size(); i++) {
    const HItem& x = h.items[i];
    BItem it;
    it.src = i;
    switch (x.k) {
      case IK::Box:
        it.k = ItemKind::Box;
        it.w = x.w;
        break;
      case IK::Glue:
        it.k = ItemKind::Glue;
        it.w = x.w;
        if (x.cls == (u8)GC::Fill) it.order = 1;
        else if (x.x > 0) it.stretch = it.shrink = (Su)std::lround((double)x.x * h.juSu);
        break;
      case IK::Penalty:
      case IK::Disc:
        it.k = x.k == IK::Penalty ? ItemKind::Penalty : ItemKind::Disc;
        if (penForbidden(x.x)) it.tag = PenTag::Forbidden;
        else if (penForced(x.x) && x.k == IK::Penalty) it.tag = PenTag::Forced;
        else it.pen = penThousandths(x.x);
        if (x.k == IK::Disc) {
          it.w = x.w;  // the junction kern: the pieces shape as one run
          const DiscRec& d = h.discs[x.aux];
          for (u32 s = d.pre; s < d.pre + d.preN; s++) it.pre += h.side[s].w;
        }
        break;
    }
    out.push_back(it);
  }
}

}  // namespace tsr
