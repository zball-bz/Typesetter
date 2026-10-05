// Pagination (pages-design.md §2; plan P1-18 moved the cutter here from the
// paged serializer): a post-pass over the finished layout — no re-break, no
// new layout mode. Fragments are grouped into atomic BANDS, bands are cut
// greedily into sheets with keep-rules; the paged backend writes each band
// rebased into its sheet, statelessly.
#pragma once
#include "layout.h"

namespace tsr {

struct PageBand {
  u32 para = 0;      // the frame (LayoutResult::paras)
  u32 lo = 0, hi = 0;  // its fragments [lo, hi)
};
struct Page {
  i64 top = 0;  // absolute su: the sheet's origin
  std::vector<PageBand> bands;
};
struct PageResult {
  Su height = 0;  // the sheet height
  std::vector<Page> pages;
};

PageResult paginate(const LayoutResult& lr, const std::vector<TopBlock>& tops, double pageHeightPx);

}  // namespace tsr
