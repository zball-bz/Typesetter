// TextRules (plan P1-11; design T5 TextRules, D-X04): the one character
// classifier. Every script-, punctuation- or width-dependent decision of the
// engine asks it — emit's CJK/Latin/punctuation split, the line join, the
// grid columns, the snap runs, the kern cutoff — instead of carrying its own
// ranges. The table is generated (tools/ucdc.mjs → engine/gen/textrules.h)
// from the rules (plan P4-05: engine/rules/locale/default.def, RULES_VERSION
// 1 — und's UCD-derived classes, the en and zh-Hans sections; compat.def,
// RULES_VERSION 0, stays as rules-diff's reference) and the pinned UCD 17.0.0
// columns (UAX #29 grapheme break, Extended_Pictographic, UAX #11).
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
// (plan P4-05) the class table: two loads (RULES_VERSION 1's classes are
// thousands of ranges — a search per character would cost the shaper)
inline u8 packed(u32 cp) {
  if (cp >= 0x110000) cp = 0xFFFD;
  return kCCBlocks[kCCIndex[cp >> 7]][cp & 127];
}
}  // namespace textrules_detail

inline CC ccOf(u32 cp) { return (CC)(textrules_detail::packed(cp) >> 1); }
inline CpInfo cpInfo(u32 cp) {
  u8 c = textrules_detail::packed(cp);
  u8 u = textrules_detail::rangeOf(kUcdRanges, cp).v;
  return {(CC)(c >> 1), (GCB)(u & 15), (EAW)((u >> 4) & 7), (u & 128) != 0, (c & 1) != 0};
}
inline u8 ccFlags(u32 cp) { return kCCFlags[(u8)ccOf(cp)]; }

// the class columns (RULES_VERSION 1: engine/rules/locale/default.def)
inline bool isWide(u32 cp) { return ccFlags(cp) & kCC_wide; }               // set solid as CJK
inline bool isPunctGlyph(u32 cp) { return ccFlags(cp) & kCC_punct; }        // a glyph with blanks
inline bool isOpenPunct(u32 cp) { return ccFlags(cp) & kCC_open; }          // clreq opening punctuation
inline bool isClosePunct(u32 cp) { return ccFlags(cp) & kCC_close; }        // clreq closing punctuation
inline bool noStart(u32 cp) { return ccFlags(cp) & kCC_nostart; }           // never at a line start (禁则)
inline bool takesAutospace(u32 cp) { return ccFlags(cp) & kCC_autospace; }  // CJK–Latin glue beside it
inline bool ambWide(u32 cp) { return ccFlags(cp) & kCC_ambwide; }           // sets an ambiguous neighbour CJK
inline bool isIdeo(u32 cp) { return isWide(cp) && !isPunctGlyph(cp); }      // a CJK letter (a box)
inline bool joinsWide(u32 cp) { return ccFlags(cp) & kCC_joins; }           // a line join between two is seamless
inline bool kernEligible(u32 cp) { return textrules_detail::packed(cp) & 1; }
// (plan P4-05) a code grid's column width: UAX #11 wide or fullwidth
inline bool eawWide(u32 cp) {
  const EAW w = cpInfo(cp).eaw;
  return w == EAW::W || w == EAW::F;
}
inline bool isAmbQuote(u32 cp) {
  CC c = ccOf(cp);
  return c == CC::AmbOpenQuote || c == CC::AmbCloseQuote;
}
inline bool isAmbDashOrEllipsis(u32 cp) {
  CC c = ccOf(cp);
  return c == CC::AmbDash || c == CC::AmbEllipsis;
}

// (plan P4-04) a punctuation glyph's blanks (em, its class's: the rules'
// BLANK rows): the compressible space its advance holds before and after
inline Blank blankOf(u32 cp) { return kBlanks[(u8)ccOf(cp)]; }

// (plan P4-04) the defined advance (the rules' ADVANCE rows) that starts at
// byte i of s (whose first codepoint is `first`), the longest: its entry
// and, in end, the byte after it; null when none does
inline const DefinedAdvance* definedAdvanceAt(std::string_view s, u32 i, u32 first, u32& end) {
  for (const DefinedAdvance& d : kDefinedAdvances) {
    if (d.seq[0] != first) continue;  // (the common case: no row starts with it)
    u32 j = i, k = 0;
    while (k < d.len && j < s.size() && utf8Next(s, j) == d.seq[k]) k++;
    if (k == d.len) {
      end = j;
      return &d;
    }
  }
  return nullptr;
}

// (plan P4-02) a letter or digit — what an apostrophe sits between (UAX
// #29 MidLetter): the Alpha and Digit classes (plan P4-05)
inline bool isWordChar(u32 cp) {
  const CC c = ccOf(cp);
  return c == CC::Alpha || c == CC::Digit;
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
