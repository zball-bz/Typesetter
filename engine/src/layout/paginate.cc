#include "paginate.h"

#include <algorithm>

namespace tsr {

namespace {

// A box of the vertical list: fragments a page never cuts between (joined
// by Structural tiers), in one frame
struct Box {
  u32 para = 0;
  u32 lo = 0, hi = 0;    // [lo, hi) into its frame's fragments
  i64 top = 0, bot = 0;  // absolute su
  i64 extTop = 0, extBot = 0;  // (plan P3-14) with the frames it opens / closes (their padding)
  PenTier tier = PenTier::Normal;  // a cut just before it
  u8 paged = 0;
  u64 table = 0;  // its table, (frame << 32 | block) + 1; 0: none
  bool tableStart = false;
  Span span;            // the source it holds (an insert's reference)
  u32 insertAt = ~0u;   // an insert: its reference's source position
  std::vector<u32> inserts;  // the inserts it references
  i64 h() const { return bot - top; }
};

}  // namespace

PageResult paginate(const LayoutResult& lr, const PageSpec& spec, DiagSink* diags) {
  PageResult pr;
  const i64 H = spec.h;
  pr.height = spec.h;
  std::vector<Box> flow, ins, sep;  // sep: the inserts' separator (plan P3-13)
  std::vector<Box> frames;          // (plan P3-14) framed blocks' frames: drawn, not cut
  std::vector<u64> tablesSeen;
  for (u32 p = 0; p < (u32)lr.paras.size(); p++) {
    const ParaFrame& fr = lr.paras[p];
    std::vector<Box>* last = nullptr;
    for (u32 i = 0; i < (u32)fr.lines.size(); i++) {
      const Fragment& l = fr.lines[i];
      const i64 t = (i64)fr.y + l.y, bo = t + l.height;
      if (l.paged & kPagedFrame) {
        Box f;
        f.para = p;
        f.lo = i;
        f.hi = i + 1;
        f.top = t;
        f.bot = bo;
        frames.push_back(f);
        continue;
      }
      std::vector<Box>& list = !(l.paged & kPagedInsert) ? flow : (l.paged & kPagedHeader) ? sep : ins;
      const u64 table = l.table != ~0u ? (((u64)p << 32) | l.table) + 1 : 0;
      if (l.brk == PenTier::Structural && last == &list && !list.empty() && list.back().para == p &&
          list.back().paged == l.paged) {
        Box& b = list.back();
        b.hi = i + 1;
        b.top = std::min(b.top, t);
        b.bot = std::max(b.bot, bo);
        if (!l.srcSpan.empty()) {
          if (b.span.empty()) b.span = l.srcSpan;
          b.span.start = std::min(b.span.start, l.srcSpan.start);
          b.span.end = std::max(b.span.end, l.srcSpan.end);
        }
        continue;
      }
      Box b;
      b.para = p;
      b.lo = i;
      b.hi = i + 1;
      b.top = t;
      b.bot = bo;
      b.tier = l.brk;
      b.paged = l.paged;
      b.table = table;
      if (table && std::find(tablesSeen.begin(), tablesSeen.end(), table) == tablesSeen.end()) {
        b.tableStart = true;
        tablesSeen.push_back(table);
      }
      b.span = l.srcSpan;
      b.insertAt = l.insertAt;
      list.push_back(std::move(b));
      last = &list;
    }
  }
  std::stable_sort(flow.begin(), flow.end(), [](const Box& a, const Box& b) { return a.top < b.top; });
  // (plan P3-14) a frame's padding and border go with the box it opens
  // (above it) and the one it closes (below): a sheet starts at its frame
  for (Box& b : flow) {
    b.extTop = b.top;
    b.extBot = b.bot;
  }
  for (const Box& f : frames) {
    size_t a = ~size_t(0), z = ~size_t(0);
    for (size_t x = 0; x < flow.size(); x++)
      if (flow[x].para == f.para && flow[x].top >= f.top && flow[x].bot <= f.bot) {
        if (a == ~size_t(0)) a = x;
        z = x;
      }
    if (a == ~size_t(0)) continue;
    flow[a].extTop = std::min(flow[a].extTop, f.top);
    flow[z].extBot = std::max(flow[z].extBot, f.bot);
  }
  // the separator above a sheet's inserts (its first: a rule), repeated on
  // every sheet that has any
  if (sep.size() > 1) sep.resize(1);
  const i64 sepH = sep.empty() ? 0 : sep[0].h();
  // above the inserts: the separator's band (its rule at its midline), else
  // the footnote skip
  const i64 insSkip = sep.empty() ? spec.footnoteSkip : sepH;
  // each insert to the box that references it (its source position), else
  // to the last sheet
  std::vector<u32> unreferenced;
  for (u32 x = 0; x < (u32)ins.size(); x++) {
    bool placed = false;
    for (Box& b : flow)
      if (!(b.paged & kPagedMovable) && ins[x].insertAt != ~0u && b.span.start <= ins[x].insertAt &&
          ins[x].insertAt < std::max(b.span.end, b.span.start + 1)) {
        b.inserts.push_back(x);
        placed = true;
        break;
      }
    if (!placed) unreferenced.push_back(x);
  }
  auto band = [](const Box& b, i64 yShift, bool repeat) { return PageBand{b.para, b.lo, b.hi, yShift, repeat}; };
  auto report = [&](const char* code, Span sp, std::string msg) {
    if (diags) diags->add(Sev::Warning, code, sp, std::move(msg));
  };

  const size_t n = flow.size();
  // (plan P3-15) the room a movable box (a page float) leaves in the flow —
  // from its top to the next box's: the flow closes over it on its sheet
  std::vector<i64> hole(n, 0);
  for (size_t x = 0; x < n; x++)
    if (flow[x].paged & kPagedMovable) {
      i64 next = flow[x].bot;
      for (size_t y = x + 1; y < n; y++)
        if (!(flow[y].paged & kPagedMovable)) {
          next = flow[y].extTop;
          break;
        }
      hole[x] = std::max<i64>(0, next - flow[x].extTop);
    }
  size_t s = 0;
  std::vector<size_t> carried, carriedBottom;  // page floats deferred to the next sheet's top / bottom
  std::vector<size_t> floatSheet;              // (plan P3-15) page floats for a sheet of floats
  while (s < n || !carried.empty() || !carriedBottom.empty() || !floatSheet.empty()) {
    // a sheet of floats: the page floats met on the sheet before, stacked
    // from its top (as many as fit; one taller than it overflows it)
    if (!floatSheet.empty()) {
      Page pg;
      pg.top = flow[floatSheet[0]].top;
      i64 cursor = 0;
      size_t q = 0;
      for (; q < floatSheet.size(); q++) {
        const Box& b = flow[floatSheet[q]];
        if (q > 0 && cursor + b.h() > H) break;
        pg.bands.push_back(band(b, cursor + pg.top - b.top, false));
        cursor += b.h();
      }
      if (cursor > H) pg.overflow = cursor - H;
      floatSheet.erase(floatSheet.begin(), floatSheet.begin() + (long)q);
      pr.pages.push_back(std::move(pg));
      continue;
    }
    // (plan P5-02) carried page floats taller than a sheet together: a sheet
    // of their own, overflowing visibly (as a sheet of floats does) — the
    // flow starts on the next one, never forced below them
    {
      i64 carriedH = 0;
      for (size_t c : carried) carriedH += flow[c].h();
      for (size_t c : carriedBottom) carriedH += flow[c].h();
      if (carriedH > H) {
        Page fp;
        const size_t first = !carried.empty() ? carried[0] : carriedBottom[0];
        fp.top = flow[first].top;
        i64 cur = 0;
        for (const std::vector<size_t>* list : {&carried, &carriedBottom})
          for (size_t c : *list) {
            fp.bands.push_back(band(flow[c], cur + fp.top - flow[c].top, false));
            cur += flow[c].h();
          }
        fp.overflow = cur - H;
        report("page-overflow", flow[first].span, "a page float taller than the page overflows it");
        carried.clear();
        carriedBottom.clear();
        pr.pages.push_back(std::move(fp));
        continue;
      }
    }
    Page pg;
    const i64 S = s < n ? flow[s].extTop : flow[!carried.empty() ? carried[0] : carriedBottom[0]].top;
    pg.top = S;
    // a table continued from an earlier sheet repeats its header rows
    std::vector<size_t> header;
    if (s < n && flow[s].table && !flow[s].tableStart)
      for (size_t x = 0; x < n; x++)
        if (flow[x].table == flow[s].table && (flow[x].paged & kPagedHeader)) header.push_back(x);
    // (plan P3-14) the header's room: from its top to its table's first
    // row after it (the padding under its rule)
    i64 headerH = 0;
    if (!header.empty()) {
      const Box& h0 = flow[header.front()];
      i64 end = flow[header.back()].bot;
      for (size_t x = header.back() + 1; x < n; x++)
        if (flow[x].table == h0.table) {
          end = flow[x].top;
          break;
        }
      headerH = end - h0.top;
    }
    i64 lift = 0;  // what sits above the flow: carried floats, a repeated header, lifted floats
    for (size_t c : carried) lift += flow[c].h();
    lift += headerH;
    i64 sink = 0;  // (plan P3-15) what sits below it: bottom floats
    for (size_t c : carriedBottom) sink += flow[c].h();
    std::vector<size_t> lifted, nextCarried, sunk, nextBottom, pageFloats;
    i64 insH = 0;
    bool anyIns = false;
    i64 flowEnd = 0;  // the flow's height so far (the floats' holes closed)
    i64 removed = 0;  // the holes of the floats met so far
    size_t k = s;
    for (; k < n; k++) {
      const Box& b = flow[k];
      if (k > s && b.tier == PenTier::Forced) break;
      if (b.paged & kPagedMovable) {
        // a page float: to a sheet of floats; to the bottom or the top of
        // this sheet if it fits there, else of the next
        if (b.paged & kPagedPage) pageFloats.push_back(k);
        else if (b.paged & kPagedBottom) {
          if (lift + sink + b.h() + flowEnd + insH <= H) {
            sunk.push_back(k);
            sink += b.h();
          } else {
            nextBottom.push_back(k);
          }
        } else if (lift + b.h() + flowEnd + insH + sink <= H) {
          lifted.push_back(k);
          lift += b.h();
        } else {
          nextCarried.push_back(k);
        }
        removed += hole[k];
        continue;
      }
      i64 need = 0;
      for (u32 x : b.inserts) need += ins[x].h();
      const i64 skip = !anyIns && need > 0 ? insSkip : 0;
      const i64 end = std::max(flowEnd, b.extBot - S - removed);
      const i64 bottom = end + lift + sink + insH + need + skip;
      if (bottom <= H || k == s) {
        flowEnd = end;
        insH += need + skip;
        anyIns = anyIns || need > 0;
        if (bottom > H) {  // an atom taller than a sheet: set alone, overflowing visibly
          pg.overflow = bottom - H;
          report("page-overflow", b.span, "a block taller than the page overflows it");
          k++;
          break;
        }
        continue;
      }
      break;
    }
    // the cut: the latest legal one before the first box past the sheet,
    // relaxing the keeps in their order (D-Y04). A relaxed cut must move
    // what it keeps together onto the next sheet — the boxes from the cut
    // through the one that did not fit — else it buys nothing and the next
    // keep is relaxed; the last resort is the greedy cut
    size_t j = k;
    if (k < n && k > s + 1 && flow[k].tier != PenTier::Forced && pg.overflow == 0) {
      static constexpr PenTier kLevels[] = {PenTier::Normal, PenTier::KeepTogether, PenTier::WidowOrphan,
                                            PenTier::KeepWithNext, PenTier::Structural};
      bool found = false;
      for (PenTier lvl : kLevels) {
        for (size_t jj = k; jj > s; jj--) {
          if (flow[jj].paged & kPagedMovable) continue;
          if (flow[jj].tier <= lvl && (lvl == PenTier::Normal || flow[k].extBot - flow[jj].extTop <= H)) {
            j = jj;
            found = true;
            break;
          }
        }
        if (found) {
          if (lvl != PenTier::Normal)
            report("keep-violated", flow[j].span,
                   lvl == PenTier::KeepTogether   ? "a block kept together is cut by a page"
                   : lvl == PenTier::WidowOrphan  ? "a page cut leaves a widow or orphan line"
                   : lvl == PenTier::KeepWithNext ? "a page cut separates a heading from what follows"
                                                  : "a page cut splits an atom");
          break;
        }
      }
      if (!found && flow[k].tier > PenTier::Normal) report("keep-violated", flow[k].span, "a page cut violates a keep");
    }
    // the flow's end at the cut (its holes closed)
    i64 flowH = 0;
    {
      i64 rem = 0;
      for (size_t x = s; x < j; x++) {
        if (flow[x].paged & kPagedMovable) rem += hole[x];
        else flowH = std::max(flowH, flow[x].extBot - S - rem);
      }
    }
    // the sheet: carried, repeated and lifted boxes at its top, the flow
    // [s, j) below them, its bottom floats and inserts at its bottom
    i64 cursor = 0;
    auto place = [&](const Box& b, bool repeat) {
      pg.bands.push_back(band(b, cursor + S - b.top, repeat));
      cursor += b.h();
    };
    for (size_t c : carried) place(flow[c], false);
    if (!header.empty()) {
      const i64 at = cursor, top0 = flow[header.front()].top;
      for (size_t x : header) pg.bands.push_back(band(flow[x], at + (flow[x].top - top0) + S - flow[x].top, true));
      cursor = at + headerH;
    }
    for (size_t x : lifted)
      if (x < j) place(flow[x], false);
    const i64 flowShift = cursor;
    // (plan P3-14) the frames of the blocks this sheet's flow meets, under
    // it: a block cut by the sheet shows its frame's part here (the sheet
    // clips), a continued one written without ids
    {
      i64 lo = INT64_MAX, hi = INT64_MIN;
      for (size_t x = s; x < j; x++)
        if (!(flow[x].paged & kPagedMovable)) {
          lo = std::min(lo, flow[x].extTop);
          hi = std::max(hi, flow[x].extBot);
        }
      for (const Box& f : frames)
        if (f.top < hi && f.bot > lo) pg.bands.push_back(band(f, flowShift, f.top < lo));
    }
    std::vector<u32> pageInserts;
    {
      i64 rem = 0;
      for (size_t x = s; x < j; x++) {
        if (flow[x].paged & kPagedMovable) {
          rem += hole[x];
          continue;
        }
        pg.bands.push_back(band(flow[x], flowShift - rem, false));
        for (u32 r : flow[x].inserts) pageInserts.push_back(r);
      }
    }
    if (j >= n) pageInserts.insert(pageInserts.end(), unreferenced.begin(), unreferenced.end());
    i64 total = 0;  // the inserts' room
    if (!pageInserts.empty()) {
      total = insSkip;
      for (u32 r : pageInserts) total += ins[r].h();
    }
    // (plan P3-15) bottom floats: above the inserts, below the flow
    std::vector<size_t> bottoms(carriedBottom);
    for (size_t x : sunk)
      if (x < j) bottoms.push_back(x);
    i64 bottomsH = 0;
    for (size_t x : bottoms) bottomsH += flow[x].h();
    i64 floorY = H - total;  // the inserts' top
    i64 used = cursor + flowH;  // (plan P5-02) the sheet's lowest edge so far
    if (!bottoms.empty()) {
      i64 y = std::max(H - total - bottomsH, cursor + flowH);
      for (size_t x : bottoms) {
        pg.bands.push_back(band(flow[x], y + S - flow[x].top, false));
        y += flow[x].h();
      }
      floorY = y;
      used = std::max(used, y);
    }
    if (!pageInserts.empty()) {
      i64 y = std::max(H - total, std::max(cursor + flowH, bottoms.empty() ? cursor + flowH : floorY)) +
              (sep.empty() ? spec.footnoteSkip : 0);
      for (const Box& b : sep) {
        pg.bands.push_back(band(b, y + S - b.top, true));
        y += b.h();
      }
      for (u32 r : pageInserts) {
        pg.bands.push_back(band(ins[r], y + S - ins[r].top, false));
        y += ins[r].h();
      }
      used = std::max(used, y);
    }
    // (plan P5-02) a sheet whose content runs past its bottom shows it (the
    // writer drops its clip) — whatever put it there
    if (used > H && pg.overflow == 0) pg.overflow = used - H;
    // floats that did not fit wait for the next sheet; floats placed past
    // the cut are met again in the flow; page floats make the next sheet
    carried.clear();
    for (size_t x : nextCarried)
      if (x < j) carried.push_back(x);
    carriedBottom.clear();
    for (size_t x : nextBottom)
      if (x < j) carriedBottom.push_back(x);
    for (size_t x : pageFloats)
      if (x < j) floatSheet.push_back(x);
    pr.pages.push_back(std::move(pg));
    s = j;
  }
  return pr;
}

}  // namespace tsr
