// Pagination (pages-design.md §2; plan P1-18 moved the cutter here from the
// paged serializer; plan P3-12, design T6 "PageBuilder"): a post-pass over
// the finished layout — no re-break, no new layout mode, no kind tests. The
// layouters declared a page-break tier before every fragment (layout.h
// PenTier); fragments joined by Structural tiers form BOXES (a line, a code
// line, a table row, a float box), cut greedily into sheets of the page's
// content height. When no clean cut exists the keeps relax in a declared
// order (D-Y04: keep-together, then widows/orphans, then keep-with-next),
// each relaxation reported; an atom taller than a sheet is set alone and
// overflows it, visibly. Page floats lift to the top of their sheet (or the
// next), footnote inserts go to the bottom of their reference's sheet, a
// table's header rows repeat atop its continuation sheets. The paged
// backend writes each band rebased into its sheet, statelessly.
#pragma once
#include "layout.h"

namespace tsr {

struct DiagSink;

// the page's geometry (design T6 PageSpec; the shell's print derives the
// sheet from the same settings)
struct PageSpec {
  Su h = 0;             // the content height (page.height)
  Su footnoteSkip = 0;  // above a sheet's inserts
};

struct PageBand {
  u32 para = 0;        // the frame (LayoutResult::paras)
  u32 lo = 0, hi = 0;  // its fragments [lo, hi)
  i64 yShift = 0;      // moved on its sheet: a lifted page float, an insert, flow below them
  bool repeat = false; // a repeated table header: written without ids
};
struct Page {
  i64 top = 0;  // absolute su: the sheet's origin
  std::vector<PageBand> bands;
  i64 overflow = 0;  // > 0: content past the sheet's bottom (an atom taller than a sheet)
};
struct PageResult {
  Su height = 0;  // the sheet height
  std::vector<Page> pages;
};

PageResult paginate(const LayoutResult& lr, const PageSpec& spec, DiagSink* diags = nullptr);

}  // namespace tsr
