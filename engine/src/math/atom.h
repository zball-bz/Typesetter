// GENERATED from the class and flag columns of symbols.tsv by tools/mathdict.py — do not edit.
// TeX atom classes and symbol flags (plan P1-22: the vocabulary is the
// dictionary's, not the font compiler's).
#pragma once
#include <cstdint>

namespace tsr {

enum AtomClass : uint8_t { kOrd, kOp, kBin, kRel, kOpen, kClose, kPunct, kInner };
enum SymFlag : uint8_t {
  kFlagLarge = 1,    // a large operator (display size)
  kFlagLimitsAlways = 2,  // limits above/below in every style (limits(), plan P3-24)
  kFlagLimits = 4,   // limits above/below in display style
  kFlagTextOp = 8,   // a multi-letter operator set upright in text
  kFlagFence = 16,   // a symmetric delimiter: alone in a group, its middle (plan P3-24)
};

}  // namespace tsr
