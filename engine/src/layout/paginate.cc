#include "paginate.h"

#include <algorithm>

namespace tsr {

PageResult paginate(const LayoutResult& lr, const std::vector<TopBlock>& tops, double pageHeightPx) {
  struct Band {
    u32 para;
    u32 lo, hi;               // [lo, hi) into fr.lines
    i64 top, bot;             // absolute su
    bool stickAfter = false;  // keep with what follows (a heading, a block image)
    u32 ordinal = 0, count = 0;  // a paragraph line's position (widow/orphan)
  };
  PageResult pr;
  const Su H = suRoundPx(pageHeightPx);
  pr.height = H;
  std::vector<Band> bands;

  for (u32 p = 0; p < (u32)lr.paras.size(); p++) {
    const ParaFrame& fr = lr.paras[p];
    const TopTree& tree = *tops[p].tree;
    auto leafOf = [&](const Fragment& l) -> const LayoutBlock& { return tree.blocks[tree.leaves[l.unitIdx]]; };
    auto paraLine = [&](const Fragment& l) {
      return l.kind == FragKind::Line && l.cellIdx < 0 && l.table == ~0u && leafOf(l).layouter == LayouterId::Paragraph;
    };
    // per-leaf paragraph line counts (widow/orphan bookkeeping)
    std::vector<u32> unitLines(tree.leaves.size(), 0), unitSeen(tree.leaves.size(), 0);
    for (const Fragment& l : fr.lines)
      if (paraLine(l)) unitLines[l.unitIdx]++;
    size_t i = 0;
    while (i < fr.lines.size()) {
      const Fragment& l = fr.lines[i];
      const LayoutBlock& b = leafOf(l);
      Band band;
      band.para = p;
      band.lo = (u32)i;
      auto lineTop = [&](const Fragment& x) -> i64 { return (i64)fr.y + x.y; };
      band.top = lineTop(l);
      band.bot = band.top + l.height;
      size_t j = i + 1;
      auto sameUnit = [&](size_t k) { return k < fr.lines.size() && fr.lines[k].unitIdx == l.unitIdx; };
      if (l.table != ~0u) {
        // a table with its rules and cells is atomic (plan P3-10: its group);
        // a code block's table cuts between its rows (logical lines, plan
        // P3-11): a row is its code rows and its note
        const TableSpec& ts = tree.tables[tree.blocks[l.table].spec];
        const size_t nc = ts.cols.size() ? ts.cols.size() : 1;
        auto rowOf = [&](const Fragment& f) { return f.gridCell < 0 ? -1 : (i64)((size_t)f.gridCell / nc); };
        const i64 row = rowOf(l);
        while (j < fr.lines.size() && fr.lines[j].table == l.table && (!ts.lines || rowOf(fr.lines[j]) == row)) j++;
      } else if (b.floatSide != 0) {
        // the whole leaf is atomic (a float with its caption)
        while (sameUnit(j)) j++;
      } else if (b.layouter == LayouterId::Grid) {
        // one logical code line: its wrapped rows + zipped sidecar rows
        u32 row = l.kind == FragKind::CodeRow ? l.codeLine : (u32)l.cellIdx;
        while (sameUnit(j)) {
          const Fragment& n = fr.lines[j];
          u32 nrow = n.kind == FragKind::CodeRow ? n.codeLine : (u32)n.cellIdx;
          if (nrow != row) break;
          j++;
        }
      }
      for (size_t k = i; k < j; k++) {
        i64 t = lineTop(fr.lines[k]);
        i64 bo = t + fr.lines[k].height;
        if (t < band.top) band.top = t;
        if (bo > band.bot) band.bot = bo;
      }
      band.hi = (u32)j;
      // a keep-with-next leaf's last band sticks to what follows
      if (b.tr.keepWithNext && !sameUnit(j)) band.stickAfter = true;
      if (paraLine(l)) {
        band.count = unitLines[l.unitIdx];
        band.ordinal = unitSeen[l.unitIdx]++;
      }
      bands.push_back(band);
      i = j;
    }
  }
  std::stable_sort(bands.begin(), bands.end(), [](const Band& a, const Band& b) { return a.top < b.top; });

  // greedy cuts with keep-rules; a violated cut backs up, an impossible one
  // falls back to the greedy position (mirrors KP's hard-cut fallback)
  std::vector<size_t> starts{0};
  std::vector<i64> tops0{0};
  size_t pageFirst = 0;
  i64 S = 0;
  auto violates = [&](size_t j) -> bool {
    if (j == 0 || j <= pageFirst) return false;
    if (bands[j - 1].stickAfter) return true;
    const Band& b = bands[j];
    if (b.count > 0 && b.ordinal > 0 && (b.ordinal < 2 || b.count - b.ordinal < 2)) return true;  // orphan / widow
    return false;
  };
  for (size_t i = 0; i < bands.size(); i++) {
    if (i == pageFirst) continue;
    if (bands[i].bot - S <= (i64)H) continue;
    size_t j = i;
    while (j > pageFirst && violates(j)) j--;
    if (j == pageFirst) j = i;  // oversized atom: overflow this sheet
    starts.push_back(j);
    tops0.push_back(bands[j].top);
    pageFirst = j;
    S = bands[j].top;
  }
  for (size_t pg = 0; pg < starts.size(); pg++) {
    Page page;
    page.top = tops0[pg];
    const size_t lo = starts[pg], hi = pg + 1 < starts.size() ? starts[pg + 1] : bands.size();
    for (size_t bi = lo; bi < hi; bi++) page.bands.push_back({bands[bi].para, bands[bi].lo, bands[bi].hi});
    pr.pages.push_back(std::move(page));
  }
  return pr;
}

}  // namespace tsr
