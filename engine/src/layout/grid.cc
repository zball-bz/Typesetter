#include "grid.h"

#include <algorithm>

#include "../shape/textrules.h"

namespace tsr {

std::vector<GridRow> wrapGridLine(std::string_view joined, const std::vector<std::pair<u32, u32>>& hang, i32 cols,
                                  i32 latinAtoms, i32 cjkCols, const GridParams& gp) {
  std::vector<GridRow> rows;
  if (cols <= 0 || joined.empty()) {
    rows.push_back({0, (u32)joined.size(), 0});
    return rows;
  }
  const i32 minCols = gp.minCols;
  // (plan P5-02) its break characters, by code point (any script's)
  std::vector<u32> breakCps;
  for (u32 i = 0; i < gp.breakAfter.size();) breakCps.push_back(utf8Next(gp.breakAfter, i));
  auto breakAfter = [&](u32 cp) { return std::find(breakCps.begin(), breakCps.end(), cp) != breakCps.end(); };
  // hanging base: the logical line's own leading whitespace columns
  i32 leadChars = 0;
  while ((size_t)leadChars < joined.size() && (joined[leadChars] == ' ' || joined[leadChars] == '\t')) leadChars++;
  const i32 leadCols = leadChars * latinAtoms;  // in atom units
  auto contColsAt = [&](u32 breakByte) -> u16 {
    i32 cc = leadCols / latinAtoms + gp.contIndent;
    // comment-aware (verbatim-design §4): a break inside a comment run
    // aligns the continuation to the comment's CONTENT column
    for (auto [cs, ce] : hang) {
      if (!gp.commentAware) break;
      if (breakByte <= cs || breakByte > ce) continue;
      i32 col = 0;  // the column of the comment start
      u32 pb = 0;
      while (pb < cs) {
        u32 cp2 = utf8Next(joined, pb);
        col += eawWide(cp2) ? cjkCols : latinAtoms;
      }
      // lead-in: its opening marks (// # -- ；…: neither a word's characters
      // nor a space, any script's) and one space, in columns
      u32 q2 = cs;
      i32 lead = 0;
      while (q2 < ce) {
        u32 nx = q2;
        const u32 cp2 = utf8Next(joined, nx);
        if (cp2 == ' ' || isWordChar(cp2) || isIdeo(cp2)) break;
        q2 = nx;
        lead += eawWide(cp2) ? cjkCols / latinAtoms : 1;
      }
      if (q2 < ce && joined[q2] == ' ') lead++;
      cc = col / latinAtoms + lead;
      break;
    }
    const i32 colCap = cols / latinAtoms;
    if (cc > colCap - minCols) cc = colCap > minCols ? colCap - minCols : 0;
    if (cc < 0) cc = 0;
    return (u16)cc;
  };
  u32 lo = 0;
  u16 nextCont = 0;
  while (lo < joined.size()) {
    i32 avail = rows.empty() ? cols : cols - (i32)nextCont * latinAtoms;
    if (avail < minCols * latinAtoms) avail = minCols * latinAtoms;
    u32 p = lo;
    i32 col = 0;
    u32 lastBrk = 0;
    while (p < joined.size()) {
      u32 q = p;
      u32 cp = utf8Next(joined, q);
      i32 w = eawWide(cp) ? cjkCols : latinAtoms;  // (plan P4-05) UAX #11: wide and fullwidth take two columns
      if (col + w > avail) break;
      col += w;
      p = q;
      if (breakAfter(cp)) {
        lastBrk = p;  // break AFTER the boundary
      } else if (isWide(cp) && !isOpenPunct(cp)) {
        // CJK wraps between any two characters (clreq), except before a
        // closing punct / after an opening one (禁则)
        u32 r = q;
        u32 nx = q < joined.size() ? utf8Next(joined, r) : 0;
        if (!(nx && (isClosePunct(nx) || noStart(nx)))) lastBrk = p;  // (plan P4-05: the nostart column)
      }
    }
    if (p >= joined.size()) {
      rows.push_back({lo, (u32)joined.size(), nextCont});
      break;
    }
    u32 cut = lastBrk > lo ? lastBrk : p;
    if (cut <= lo) {  // guarantee progress on pathological input
      u32 q = lo;
      utf8Next(joined, q);
      cut = q;
    }
    // trailing spaces stay in the ROW (not swallowed between slices): the
    // copy rebuild must be byte-lossless, and pre whitespace at a ragged
    // row's end is invisible anyway
    u32 ext = cut;
    while (ext < joined.size() && joined[ext] == ' ') ext++;
    rows.push_back({lo, ext, nextCont});
    nextCont = contColsAt(cut);  // the NEXT row's indent
    lo = ext;
  }
  if (rows.empty()) rows.push_back({0, 0, 0});
  return rows;
}

}  // namespace tsr
