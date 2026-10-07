// The character grid's line wrapping (plan P3-11; design T6 "Grid"): one
// logical line of a code block, wrapped to a column budget — a pure function
// of the line's text and the grid's parameters (GridParams: data, not code
// paths), shared by every grid. Measurement (the ch probes, snap-kerning's
// atoms) and placement stay with the layouter.
#pragma once
#include <string_view>
#include <utility>
#include <vector>

#include "../support/support.h"

namespace tsr {

struct GridParams {
  i32 minCols = 8;           // code.minCols: no row budget narrower, in columns
  i32 contIndent = 2;        // codeblock.contIndent: a continuation row's extra indent
  bool commentAware = true;  // a break inside a hanging run (a comment) aligns to its content
  // break opportunities after these characters, by code point (code.breakAfter;
  // CJK: between any two, by kinsoku)
  std::string_view breakAfter = " \t,;)}]>";
};

struct GridRow {
  u32 lo = 0, hi = 0;  // the row's byte slice of the line
  u16 cont = 0;        // its continuation indent, in columns
};

// `hang`: the byte ranges of the line's hanging runs (code.hang content: a
// comment); `cols`: the row budget in atoms (0: the line does not wrap); a
// Latin char takes `latinAtoms` atoms, a wide one `cjkCols` (snap-kerning's
// rational grid, or the measured CJK:Latin ratio). Greedy: a row ends after
// its last break opportunity (else mid-token), keeping its trailing spaces
// (copy stays byte-lossless); a continuation row indents by the line's own
// leading whitespace plus contIndent — or, broken inside a comment, to the
// comment's content column — never so far that fewer than minCols remain.
std::vector<GridRow> wrapGridLine(std::string_view text, const std::vector<std::pair<u32, u32>>& hang, i32 cols,
                                  i32 latinAtoms, i32 cjkCols, const GridParams& gp);

}  // namespace tsr
