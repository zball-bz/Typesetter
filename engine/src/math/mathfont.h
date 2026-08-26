// Accessors over the precompiled MATH artifact (math-design.md §3).
// The artifact is font design units; conversion to su happens here, once,
// at use time: su = units * sizePx * 64 / upem.
#pragma once
#include "../../gen/euler_math.h"
#include "../support/support.h"

namespace tsr {

inline int mathConst(mathfont::C c) {
  return mathfont::kConstants[(int)c];
}

// Percent-valued constants (ScriptPercentScaleDown, RadicalDegreeBottomRaise…)
// are plain numbers, never run through mathSu.
inline Su mathSu(double units, double sizePx) {
  return (Su)std::llround(units * sizePx * 64.0 / (double)mathfont::kUpem);
}

inline const mathfont::GlyphRec* mathGlyph(u32 cp) {
  int lo = 0, hi = mathfont::kGlyphCount;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (mathfont::kGlyphs[mid].cp < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo < mathfont::kGlyphCount && mathfont::kGlyphs[lo].cp == cp)
    return &mathfont::kGlyphs[lo];
  return nullptr;
}

inline const mathfont::VarChain* mathChain(u32 cp, bool vertical) {
  const mathfont::VarChain* arr = vertical ? mathfont::kVertChains : mathfont::kHorizChains;
  int n = vertical ? mathfont::kVertChainCount : mathfont::kHorizChainCount;
  int lo = 0, hi = n;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (arr[mid].baseCp < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo < n && arr[lo].baseCp == cp) return &arr[lo];
  return nullptr;
}

// Dictionary names are ASCII and sorted bytewise (Python sort == strcmp).
inline const mathfont::OpEntry* mathOp(std::string_view name) {
  int lo = 0, hi = mathfont::kOpCount;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (std::string_view(mathfont::kOps[mid].name) < name) lo = mid + 1;
    else hi = mid;
  }
  if (lo < mathfont::kOpCount && std::string_view(mathfont::kOps[lo].name) == name)
    return &mathfont::kOps[lo];
  return nullptr;
}

}  // namespace tsr
