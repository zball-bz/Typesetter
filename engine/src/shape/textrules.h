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
// the range that holds the CJK Unified Ideographs' first codepoint, found
// in the generated table at compile time: the second common case
constexpr size_t hanRange() {
  size_t k = 0;
  for (size_t i = 0; i < sizeof(kCCRanges) / sizeof(kCCRanges[0]); i++)
    if (kCCRanges[i].start <= 0x4E00) k = i;
  return k;
}
constexpr size_t kHanRange = hanRange();
constexpr u32 kHanEnd = kHanRange + 1 < sizeof(kCCRanges) / sizeof(kCCRanges[0]) ? kCCRanges[kHanRange + 1].start
                                                                                 : 0x110000;
inline u8 packed(u32 cp) {
  if (cp < kCCRanges[1].start) return kCCRanges[0].v;  // the common case: below U+2000
  if (cp >= kCCRanges[kHanRange].start && cp < kHanEnd) return kCCRanges[kHanRange].v;
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

// (plan P4-02) a letter or digit of an alphabetic script — what an
// apostrophe sits between (UAX #29 MidLetter). Compat has no Alpha class
// yet (P4-05): ASCII alphanumerics and the letters below U+2000 outside the
// Latin-1 punctuation and symbols.
inline bool isWordChar(u32 cp) {
  if (cp < 0x80) return (cp >= '0' && cp <= '9') || ((cp | 32) >= 'a' && (cp | 32) <= 'z');
  return cp >= 0xC0 && cp < 0x2000 && cp != 0xD7 && cp != 0xF7;
}

// (plan P4-02; design T5) a soft break between two characters reads as
// nothing iff both join seamlessly: the joining classes (ideographs, wide
// punctuation, the em dash and the ellipsis — D-L03), and an ambiguous quote
// its context sets wide; else as a space
inline bool joinsWithoutSpace(u32 prev, bool prevWide, u32 next, bool nextWide) {
  auto joins = [](u32 cp, bool wide) { return joinsWide(cp) || (wide && isAmbQuote(cp)); };
  return joins(prev, prevWide) && joins(next, nextWide);
}

// (plan P4-02) the end of the extended grapheme cluster starting at byte i
// (UAX #29 GB3–GB13; the Indic conjunct rule GB9c is not applied). A space
// or a tab is a cluster of its own whatever follows it: it is a glue, not a
// glyph.
namespace textrules_detail {
// a codepoint no grapheme rule joins to its neighbour: below U+0300 (no
// marks, no Prepend, no ZWJ, no regional indicator) other than CR, and the
// CJK Unified Ideographs (GCB Other, not pictographic) — the common case,
// decided without the tables
inline bool clusterSimple(u32 cp) { return (cp < 0x300 && cp != '\r') || (cp >= 0x4E00 && cp < 0xA000); }
[[gnu::noinline]] inline u32 clusterEndSlow(std::string_view s, u32 i) {
  u32 j = i;
  const u32 first = utf8Next(s, j);
  if (first == ' ' || first == '\t' || j >= s.size()) return j;
  if (clusterSimple(first)) {
    u32 k = j;
    if (clusterSimple(utf8Next(s, k))) return j;
  }
  CpInfo a = cpInfo(first);
  bool pict = a.extPict;  // ExtPict Extend* so far (GB11)
  bool zwj = false;       // … followed by a ZWJ
  u32 ri = a.gcb == GCB::Regional_Indicator ? 1 : 0;
  while (j < s.size()) {
    u32 k = j;
    const u32 cp = utf8Next(s, k);
    const CpInfo b = cpInfo(cp);
    const GCB x = a.gcb, y = b.gcb;
    bool join;
    if (x == GCB::CR && y == GCB::LF) join = true;                                            // GB3
    else if (x == GCB::Control || x == GCB::CR || x == GCB::LF) join = false;                 // GB4
    else if (y == GCB::Control || y == GCB::CR || y == GCB::LF) join = false;                 // GB5
    else if (x == GCB::L && (y == GCB::L || y == GCB::V || y == GCB::LV || y == GCB::LVT)) join = true;  // GB6
    else if ((x == GCB::LV || x == GCB::V) && (y == GCB::V || y == GCB::T)) join = true;      // GB7
    else if ((x == GCB::LVT || x == GCB::T) && y == GCB::T) join = true;                      // GB8
    else if (y == GCB::Extend || y == GCB::ZWJ || y == GCB::SpacingMark) join = true;          // GB9, GB9a
    else if (x == GCB::Prepend) join = true;                                                  // GB9b
    else if (zwj && b.extPict) join = true;                                                   // GB11
    else if (x == GCB::Regional_Indicator && y == GCB::Regional_Indicator) join = ri % 2 == 1;  // GB12, GB13
    else join = false;
    if (!join) break;
    zwj = y == GCB::ZWJ && pict;
    pict = b.extPict || (pict && y == GCB::Extend);
    ri = y == GCB::Regional_Indicator ? ri + 1 : 0;
    a = b;
    j = k;
  }
  return j;
}
}  // namespace textrules_detail
inline u32 clusterEnd(std::string_view s, u32 i) {
  const u8 b = (u8)s[i];
  // ASCII followed by ASCII (not CR LF): one byte, decided inline
  if (b < 0x80 && (i + 1 >= s.size() || ((u8)s[i + 1] < 0x80 && b != '\r'))) return i + 1;
  // a character of U+4000–U+9FFF (lead bytes E4–E9: GCB Other, never
  // pictographic) followed by ASCII or another of them: three bytes
  if (b >= 0xE4 && b <= 0xE9 && i + 3 <= s.size() && ((u8)s[i + 1] & 0xC0) == 0x80 && ((u8)s[i + 2] & 0xC0) == 0x80) {
    if (i + 3 == s.size()) return i + 3;
    const u8 n = (u8)s[i + 3];
    if (n < 0x80 || (n >= 0xE4 && n <= 0xE9)) return i + 3;
  }
  return textrules_detail::clusterEndSlow(s, i);
}

}  // namespace tsr
