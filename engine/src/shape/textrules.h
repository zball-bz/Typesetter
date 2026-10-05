// TextRules (plan P1-11; design T5 TextRules, D-X04): the one character
// classifier. Every script-, punctuation- or width-dependent decision of the
// engine asks it — emit's CJK/Latin/punctuation split, the line join, the
// grid columns, the snap runs, the kern cutoff — instead of carrying its own
// ranges. The table is generated (tools/ucdc.mjs → engine/gen/textrules.h)
// from the rules version's class rules (engine/rules/locale/compat.def:
// RULES_VERSION 0, today's classification bit for bit) and the pinned UCD
// 17.0.0 columns (UAX #29 grapheme break, Extended_Pictographic, UAX #11).
#pragma once
#include "../../gen/textrules.h"
#include "../support/support.h"

namespace tsr {

struct CpInfo {
  CC cc;
  GCB gcb;
  EAW eaw;
  bool extPict;
  bool kern;  // may kern across a space with its neighbour
};

namespace textrules_detail {
template <class R, size_t N>
inline const R& rangeOf(const R (&t)[N], u32 cp) {
  size_t lo = 0, hi = N;  // the last range starting at or before cp
  while (hi - lo > 1) {
    size_t mid = (lo + hi) / 2;
    if (t[mid].start <= cp) lo = mid;
    else hi = mid;
  }
  return t[lo];
}
inline u8 packed(u32 cp) {
  if (cp < kCCRanges[1].start) return kCCRanges[0].v;  // the common case: below U+2000
  return rangeOf(kCCRanges, cp).v;
}
}  // namespace textrules_detail

inline CC ccOf(u32 cp) { return (CC)(textrules_detail::packed(cp) >> 1); }
inline CpInfo cpInfo(u32 cp) {
  u8 c = textrules_detail::packed(cp);
  u8 u = textrules_detail::rangeOf(kUcdRanges, cp).v;
  return {(CC)(c >> 1), (GCB)(u & 15), (EAW)((u >> 4) & 7), (u & 128) != 0, (c & 1) != 0};
}
inline u8 ccFlags(u32 cp) { return kCCFlags[(u8)ccOf(cp)]; }

// the compat predicates (RULES_VERSION 0)
inline bool isWide(u32 cp) { return ccFlags(cp) & kCC_wide; }               // CJK class, set solid
inline bool isOpenPunct(u32 cp) { return ccFlags(cp) & kCC_open; }          // clreq opening punctuation
inline bool isClosePunct(u32 cp) { return ccFlags(cp) & kCC_close; }        // clreq closing punctuation
inline bool isIdeo(u32 cp) {                                                // wide, not punctuation
  u8 f = ccFlags(cp);
  return (f & kCC_wide) && !(f & (kCC_open | kCC_close));
}
inline bool joinsWide(u32 cp) { return ccFlags(cp) & kCC_joins; }           // a line join between two is seamless
inline bool kernEligible(u32 cp) { return textrules_detail::packed(cp) & 1; }
inline bool isAmbQuote(u32 cp) {
  CC c = ccOf(cp);
  return c == CC::AmbOpenQuote || c == CC::AmbCloseQuote;
}
inline bool isAmbDashOrEllipsis(u32 cp) {
  CC c = ccOf(cp);
  return c == CC::AmbDash || c == CC::AmbEllipsis;
}

}  // namespace tsr
