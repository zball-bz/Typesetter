#include "layout.h"
#include "../shape/objects.h"
#include "../shape/textrules.h"

#include <algorithm>
#include <unordered_set>

#include "../code/grid.h"

namespace tsr {

namespace {

// A line between block breakpoints [prevBp, bp), read as items (plan
// P1-12): leading and trailing glue and penalties dropped — TeX's discard,
// as the legacy blocks' space trim — plus its block range for the dumps.
struct LineItems {
  u32 ilo = 0, ihi = 0;  // items
  u32 lo = 0, hi = 0;    // blocks
};
u32 blockOf(const std::vector<u32>& bs, u32 item) {
  return (u32)(std::upper_bound(bs.begin(), bs.end(), item) - bs.begin()) - 1;
}
bool lineItems(const HList& h, const std::vector<u32>& bs, u32 prevBp, u32 bp, LineItems& r) {
  u32 a = bs[prevBp], b = bs[bp];
  while (a < b && !isBoxOrDisc(h.items[a])) a++;
  while (b > a && !isBoxOrDisc(h.items[b - 1])) b--;
  if (a >= b) return false;
  r.ilo = a;
  r.ihi = b;
  r.lo = blockOf(bs, a);
  r.hi = blockOf(bs, b - 1) + 1;
  return true;
}
// what a line holds: its natural width (a hyphen point adds its junction
// kern mid-line, its hyphen at the line end), the stretch the renderer will
// realize (glue weights; a CJK char's InterChar gap), the vertical extents
// and the source span
struct LineFill {
  double naturalPx = 0;
  double totalWeight = 0;  // stretch positions the renderer will realize
  double capacityPx = 0;   // their glue capacity (the shrink limit's base)
  bool anyCjkGap = false;
  bool endsHyphen = false;
  Su maxAsc = 0, maxDesc = 0;
  Span span{};
};
LineFill fillLine(const HList& h, const LineItems& r, const MetricStore& metrics) {
  LineFill f;
  bool spanSet = false;
  for (u32 i = r.ilo; i < r.ihi; i++) {
    const HItem& it = h.items[i];
    if (it.k == IK::Penalty) continue;
    const ColdRec& c = h.cold[it.cold];
    if (it.k == IK::Disc) {
      if (i != r.ihi - 1) f.naturalPx += c.rawPx;  // junction kern
    } else if (!(it.k == IK::Glue && it.cls == (u8)GC::InterChar)) {
      f.naturalPx += c.rawPx;
    }
    if (it.k == IK::Glue && (it.x > 0 || it.cls == (u8)GC::InterChar)) {
      f.totalWeight += it.x;
      f.capacityPx += suToPx(c.capSu);
      if (it.cls == (u8)GC::InterChar) f.anyCjkGap = true;
    }
    const StyleId st = h.runs[it.run].face;
    if (metrics.hasVmet(st)) {
      const VMet& v = metrics.vmet(st);
      if (v.ascent > f.maxAsc) f.maxAsc = v.ascent;
      if (v.descent > f.maxDesc) f.maxDesc = v.descent;
    }
    if (const ObjPart* pt = objectPart(h, it)) {  // an inline object's part
      if (pt->asc > f.maxAsc) f.maxAsc = pt->asc;
      if (pt->desc > f.maxDesc) f.maxDesc = pt->desc;
    }
    if (c.srcEnd > c.srcStart) {
      if (!spanSet) {
        f.span = Span{c.srcStart, c.srcEnd};
        spanSet = true;
      } else {
        if (c.srcStart < f.span.start) f.span.start = c.srcStart;
        if (c.srcEnd > f.span.end) f.span.end = c.srcEnd;
      }
    }
  }
  f.endsHyphen = h.items[r.ihi - 1].k == IK::Disc;
  if (f.endsHyphen) {
    const HItem& d = h.items[r.ihi - 1];
    f.naturalPx += h.cold[h.side[h.discs[d.aux].pre].cold].rawPx;
  }
  return f;
}
// does the line end at a forced break (a hard line break, plan P1-13)? It
// sits after the line's last box, inside the blocks the break consumed
bool endsForced(const HList& h, u32 ihi, u32 consumedEnd) {
  for (u32 k = ihi; k < consumedEnd; k++)
    if (h.items[k].k == IK::Penalty && h.items[k].x <= -kPenInf) return true;
  return false;
}
// did the break after item ihi consume a real source space? (copy contract
// §9.3 — synthetic glue: autospace, punctuation blanks, indents don't count)
bool joinsSpace(const HList& h, u32 ihi) {
  for (u32 k = ihi; k < (u32)h.items.size() && !isBoxOrDisc(h.items[k]); k++)
    if (h.items[k].k == IK::Glue && (h.items[k].attrs & IA_SourceSpace)) return true;
  return false;
}

// The gap before a unit: one rule for the cursor and the exclusions.
Su gapBefore(u32 ui, const FlowUnit& u, bool firstBlock, Su paraGap) {
  return ui > 0 ? (u.tightAbove ? paraGap / 3 : paraGap) : (firstBlock ? 0 : paraGap);
}

// The float exclusions of the flow (plan P1-15; design T6 "ExclusionMap"):
// the F2 tracker (figure-design.md §4) that Doc::typeset used to run ahead
// of layout and replay through five fields on the units, now at layout's
// own cursor. It reproduces today's prefix ParShape exactly — occlusion
// counted from the float's top in baseLeading lines, a same-side float
// stacked below (the widest of the stack occludes), an opposite-side float
// and every non-text unit clearing — so breaks and lines are unchanged;
// conservative bands over real line heights come with ParShape (T6).
class ExclusionMap {
 public:
  ExclusionMap(Su baseLeading, Su paraGap, Su emGap) : lead_(baseLeading), paraGap_(paraGap), emGap_(emGap) {}
  // the cursor moves down by a gap
  void advance(Su gap) {
    if (remain_ > 0) {
      remain_ -= gap;
      if (remain_ < 0) remain_ = 0;
    }
  }
  // a float arriving while one is active: same side → stacked below the
  // active one (`shift`); the other side → the active one is cleared first
  // (`clear`) — real-world-report.md: Wikipedia opens with two thumbnails
  void arrive(u8 side, Su& shift, Su& clear) {
    shift = clear = 0;
    if (remain_ > 0 && side_ == side) {
      shift = (Su)remain_;
    } else if (remain_ > 0) {
      clear = (Su)remain_;
      remain_ = 0;
      occl_ = 0;
    }
  }
  // the float placed: image + caption + one gap of clearance, beside the
  // measure's edge
  void add(u8 side, Su shift, Su imgW, Su imgH, i64 captionH) {
    remain_ = shift + (i64)imgH + captionH + paraGap_;
    Su occl = imgW + emGap_;
    occl_ = shift > 0 && occl_ > occl ? occl_ : occl;
    side_ = side;
  }
  // a non-text unit clears the float: the advance that does it
  Su clear() {
    if (remain_ <= 0) return 0;
    Su c = (Su)remain_;
    remain_ = 0;
    occl_ = 0;
    return c;
  }
  // the line widths of a paragraph starting here: the prefix beside the
  // float, then the measure
  LineWidths widths(Su lineWidth, bool& fromLeft) const {
    LineWidths lw{lineWidth};
    fromLeft = false;
    if (remain_ > 0 && occl_ > 0 && occl_ < lw.constant - 64) {
      lw.narrow = lw.constant - occl_;
      lw.narrowK = (u32)((remain_ + lead_ - 1) / lead_);
      fromLeft = side_ == 1;
    }
    return lw;
  }
  // the paragraph's lines, counted in baseLeading
  void consume(size_t lines) {
    if (remain_ > 0) {
      remain_ -= (i64)lines * lead_;
      if (remain_ < 0) remain_ = 0;
    }
  }

 private:
  Su lead_, paraGap_, emGap_;
  i64 remain_ = 0;  // occlusion height left, measured from the cursor
  Su occl_ = 0;     // the occluded width
  u8 side_ = 0;
};

}  // namespace

LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics,
                       Interner& strs, const Config& cfg, DiagSink& diags) {
  LayoutResult lr;
  const Su measure = suFloorPx(cfg.widthPx);
  const Su baseLeading = suRoundPx(cfg.lineHeight * cfg.baseSizePx);
  const Su paraGap = suRoundPx(cfg.paraSpacingEm * cfg.baseSizePx);
  i64 y = 0;
  i64 floatBottomAbs = 0;  // doc-height watermark for a trailing float (F2)
  ExclusionMap excl(baseLeading, paraGap, suRoundPx(cfg.baseSizePx));
  bool firstBlock = true;
  // Layout breaks its paragraphs (plan P1-15) with the cached KP (break.cc:
  // keyed by exactly the DP inputs, shared across documents — the editing
  // loop's fast path). A run wider than the line is set Overfull on a line
  // of its own (the final-pass rescue) and reported once per stream.
  BreakParams bparams;
  bparams.cost = cfg.cost;
  auto breakStream = [&](const std::vector<BreakBlock>& blocks, const HList& h, LineWidths lw) {
    BreakResult r = breakLinesCached(blocks, lw, bparams);
    if (!r.overfullLines.empty()) {
      Span sp{};
      for (const ColdRec& c : h.cold)
        if (c.srcEnd > c.srcStart) {
          if (sp.empty()) sp = Span{c.srcStart, c.srcEnd};
          sp.start = std::min(sp.start, c.srcStart);
          sp.end = std::max(sp.end, c.srcEnd);
        }
      diags.add(Sev::Warning, "overfull-line", sp,
                std::to_string(r.overfullLines.size()) + " line(s) hold a run wider than the measure");
    }
    return r;
  };
  std::vector<BreakResult> cellBreaks;  // the current unit's cells

  for (size_t p = 0; p < tops.size(); p++) {
    const TopBlock& tb = tops[p];
    ParaFrame fr;
    fr.pid = tb.pid;
    fr.y = (Su)y;
    fr.w = measure;

    i64 py = 0;
    for (u32 ui = 0; ui < tb.units.size(); ui++) {
      const FlowUnit& u = tb.units[ui];
      const Su gap = gapBefore(ui, u, firstBlock, paraGap);
      if (ui > 0) py += gap;
      excl.advance(gap);
      Su floatShift = 0, clearSu = 0;
      if (u.kind == FlowUnit::K::Image && u.floatSide != 0) excl.arrive(u.floatSide, floatShift, clearSu);
      else if (u.kind != FlowUnit::K::Text) clearSu = excl.clear();  // every non-text unit clears the float
      if (clearSu > 0) py += clearSu;
      const Su lineWidth = measure - u.indent;
      cellBreaks.clear();
      // width-dependent sizes are layout's (plan P1-16): an image's display
      // box at the measure, a code block's sidecar column
      Su imgW = 0, imgH = 0;
      if (u.kind == FlowUnit::K::Image) resolveImageSize(u.img, cfg.widthPx - suToPx(u.indent), imgW, imgH);
      const Su sidebarW = u.sidecar ? suRoundPx(cfg.sidebarFrac * (cfg.widthPx - suToPx(u.indent))) : 0;

      if (u.kind == FlowUnit::K::Image && u.floatSide != 0) {
        // float box (figure-design.md §4): out of flow — zero advance; the
        // image at the measure's edge, caption rows beneath at the float
        // width; the units that flow beside it narrow by the exclusion
        i64 captionH = 0;
        for (const TableCell& c : u.cells) {  // the caption breaks to the float width
          cellBreaks.push_back(breakStream(c.blocks, c.hl, LineWidths{imgW}));
          captionH += (i64)cellBreaks.back().breakpoints.size() * baseLeading;
        }
        excl.add(u.floatSide, floatShift, imgW, imgH, captionH);
        const Su boxLeft = u.floatSide == 1 ? u.indent
                                            : u.indent + lineWidth - imgW;
        LineBox line;
        line.unitIdx = ui;
        line.special = 5;
        line.left = boxLeft;
        line.width = imgW;
        line.height = imgH;
        if (u.src && !u.src->span.empty()) line.srcSpan = u.src->span;
        line.y = (Su)(py + floatShift);  // stacked below an active float
        fr.lines.push_back(line);
        i64 cy = py + floatShift + imgH;
        for (u32 ci = 0; ci < (u32)u.cells.size(); ci++) {
          const TableCell& cell = u.cells[ci];
          u32 prevBp = 0;
          for (u32 bp : cellBreaks[ci].breakpoints) {
            LineItems r;
            const bool any = lineItems(cell.hl, cell.blockStart, prevBp, bp, r);
            prevBp = bp;
            if (!any) continue;
            LineBox cl;
            cl.unitIdx = ui;
            cl.cellIdx = (i32)ci;
            cl.blockBegin = r.lo;
            cl.blockEnd = r.hi;
            cl.itemBegin = r.ilo;
            cl.itemEnd = r.ihi;
            cl.left = boxLeft;
            cl.width = imgW;
            cl.y = (Su)cy;
            // §9.3: wrapped caption rows rejoin on copy (unlike table cells,
            // whose row boundaries are content)
            if (bp != (u32)cell.blocks.size()) cl.join = joinsSpace(cell.hl, r.ihi) ? 1 : 2;
            cl.height = baseLeading;
            cy += baseLeading;
            fr.lines.push_back(cl);
          }
        }
        if ((i64)fr.y + cy > floatBottomAbs) floatBottomAbs = (i64)fr.y + cy;
        continue;  // no py advance: the float is out of flow
      }

      if (u.kind == FlowUnit::K::Rule) {
        LineBox line;
        line.unitIdx = ui;
        line.special = 1;
        line.left = u.indent;
        line.width = lineWidth;
        line.height = baseLeading;  // band extent (pagination); y is midline
        line.y = (Su)(py + baseLeading / 2);
        py += baseLeading;
        fr.lines.push_back(line);
        continue;
      }
      if (u.kind == FlowUnit::K::Raw) {
        LineBox line;
        line.unitIdx = ui;
        line.special = 3;
        line.left = u.indent;
        line.width = lineWidth;
        line.height = suRoundPx(u.rawHpx);
        line.y = (Su)py;
        py += suRoundPx(u.rawHpx);
        fr.lines.push_back(line);
        continue;
      }
      if (u.kind == FlowUnit::K::Code) {
        Su adv = baseLeading;
        if (metrics.hasVmet(u.codeStyle)) {
          const VMet& v = metrics.vmet(u.codeStyle);
          if (v.ascent + v.descent > adv) adv = v.ascent + v.descent;
        }
        // three-box partition (verbatim §5): the code measure stops before
        // the sidecar column; the gutter stays out-of-flow (markers)
        const bool hasSidecar = sidebarW > 0 && !u.cells.empty();
        Su gapSu = 0;
        Su lineWidthFull = lineWidth;
        Su lineWidthCode = lineWidth;
        if (hasSidecar) {
          gapSu = suRoundPx(cfg.baseSizePx * cfg.codeScale);
          lineWidthCode = lineWidth - sidebarW - gapSu;
          if (lineWidthCode < 64) lineWidthCode = 64;
          for (const TableCell& c : u.cells)  // sidecar rows break to the sidebar
            cellBreaks.push_back(breakStream(c.blocks, c.hl, LineWidths{sidebarW}));
        }
        (void)lineWidthFull;
        // ch grid (CH4, code-design.md §4): monospace is a metric contract —
        // 1ch per char, 2ch for CJK; wrap is a COLUMN computation, greedy
        // with a token-boundary preference, continuation rows indent 2ch.
        Su chSu = 0;
        if (u.codeWrap && u.chRef && metrics.hasWord(u.chRef, u.codeStyle))
          chSu = metrics.word(u.chRef, u.codeStyle).su;
        // measured CJK width (verbatim-design §2): budget columns from the
        // real ratio, conservatively ceiled — no assumed 2:1
        i32 cjkCols = 2;
        if (chSu > 0 && u.cjkChRef && metrics.hasWord(u.cjkChRef, u.codeStyle)) {
          Su c = metrics.word(u.cjkChRef, u.codeStyle).su;
          cjkCols = (i32)((c + chSu - 1) / chSu);
          if (cjkCols < 1) cjkCols = 1;
        }
        i32 cols = chSu > 0 ? (i32)(lineWidthCode / chSu) : 0;
        if (cols > 0 && cols < 8) cols = 8;
        // snap-kerning (verbatim §3): solve the rational grid from RAW
        // measurements; column budget switches to atom units — Latin = q,
        // CJK = p atoms — with letter-spacing pulling advances onto it
        GridSpec grid;
        i32 latinAtoms = 1;
        if (cfg.verbatimSnapKerning && chSu > 0 && u.cjkChRef &&
            metrics.hasWord(u.chRef, u.codeStyle) &&
            metrics.hasWord(u.cjkChRef, u.codeStyle)) {
          double chLpx = metrics.word(u.chRef, u.codeStyle).px;
          double chCpx = metrics.word(u.cjkChRef, u.codeStyle).px;
          grid = solveGrid(chLpx, chCpx, cols);
          if (grid.atomPx > 0 && grid.dLatinPx <= 0.1 * chLpx &&
              grid.dCjkPx <= 0.1 * chCpx) {
            Su atomSu = suCeilPx(grid.atomPx);
            latinAtoms = grid.q;
            cjkCols = grid.p;              // in atom units now
            cols = (i32)(lineWidthCode / atomSu);  // the code column, not the measure
            if (cols > 0 && cols < 8 * grid.q) cols = 8 * grid.q;
          } else {
            grid = GridSpec{};             // budget-only fallback
          }
        }
        auto isBreakable = [](u32 cp) {
          return cp == ' ' || cp == '\t' || cp == ',' || cp == ';' ||
                 cp == ')' || cp == '}' || cp == ']' || cp == '>';
        };
        std::unordered_set<u32> hlSet(u.hlLines.begin(), u.hlLines.end());
        bool first = true;
        for (u32 li = 0; li < (u32)u.codeRuns.size(); li++) {
          std::string joined;
          std::vector<std::pair<u32, u32>> commentSpans;  // byte ranges
          for (const FlowUnit::CodeRun& r : u.codeRuns[li]) {
            u32 b0 = (u32)joined.size();
            joined.append(strs.get(r.text));
            if (r.isComment) commentSpans.push_back({b0, (u32)joined.size()});
          }
          // hanging base: the logical line's own leading whitespace columns
          i32 leadChars = 0;
          while ((size_t)leadChars < joined.size() &&
                 (joined[leadChars] == ' ' || joined[leadChars] == '\t'))
            leadChars++;
          i32 leadCols = leadChars * latinAtoms;  // in atom units
          auto contColsAt = [&](u32 breakByte) -> u16 {
            i32 cc = leadCols / latinAtoms + cfg.verbatimContIndent;
            // comment-aware (verbatim-design §4): a break inside a comment
            // run aligns the continuation to the comment's CONTENT column
            for (auto [cs, ce] : commentSpans) {
              if (breakByte <= cs || breakByte > ce) continue;
              // column of the comment start
              i32 col = 0;
              u32 pb = 0;
              while (pb < cs) {
                u32 cp2 = utf8Next(joined, pb);
                col += isWide(cp2) ? cjkCols : latinAtoms;
              }
              // lead-in: opening punctuation streak + one space
              u32 q2 = cs;
              i32 lead = 0;
              while (q2 < ce && joined[q2] != ' ' &&
                     !((joined[q2] >= 'a' && joined[q2] <= 'z') ||
                       (joined[q2] >= 'A' && joined[q2] <= 'Z') ||
                       (joined[q2] >= '0' && joined[q2] <= '9')) &&
                     (u8)joined[q2] < 0x80) {
                q2++;
                lead++;
              }
              if (q2 < ce && joined[q2] == ' ') lead++;
              cc = col / latinAtoms + lead;
              break;
            }
            i32 colCap = cols / latinAtoms;
            if (cc > colCap - 8) cc = colCap > 8 ? colCap - 8 : 0;
            if (cc < 0) cc = 0;
            return (u16)cc;
          };
          struct Row { u32 lo, hi; };
          std::vector<Row> rows;
          std::vector<u16> rowContOut;
          if (cols <= 0 || joined.empty()) {
            rows.push_back({0, (u32)joined.size()});
          } else {
            u32 lo = 0;
            u16 nextCont = 0;
            std::vector<u16> rowCont;
            while (lo < joined.size()) {
              i32 avail = rows.empty() ? cols : cols - (i32)nextCont * latinAtoms;
              if (avail < 8 * latinAtoms) avail = 8 * latinAtoms;
              u32 p = lo;
              i32 col = 0;
              u32 lastBrk = 0;
              while (p < joined.size()) {
                u32 q = p;
                u32 cp = utf8Next(joined, q);
                i32 w = isWide(cp) ? cjkCols : latinAtoms;
                if (col + w > avail) break;
                col += w;
                p = q;
                if (isBreakable(cp)) {
                  lastBrk = p;  // break AFTER the boundary
                } else if (isWide(cp) && !isOpenPunct(cp)) {
                  // CJK wraps between any two characters (clreq), except
                  // before a closing punct / after an opening one (禁则)
                  u32 r = q;
                  u32 nx = q < joined.size() ? utf8Next(joined, r) : 0;
                  if (!(nx && isClosePunct(nx))) lastBrk = p;
                }
              }
              if (p >= joined.size()) {
                rows.push_back({lo, (u32)joined.size()});
                rowCont.push_back(nextCont);
                break;
              }
              u32 cut = lastBrk > lo ? lastBrk : p;
              if (cut <= lo) {  // guarantee progress on pathological input
                u32 q = lo;
                utf8Next(joined, q);
                cut = q;
              }
              // trailing spaces stay in the ROW (not swallowed between
              // slices): the copy rebuild must be byte-lossless, and pre
              // whitespace at a ragged row's end is invisible anyway
              u32 ext = cut;
              while (ext < joined.size() && joined[ext] == ' ') ext++;
              rows.push_back({lo, ext});
              rowCont.push_back(nextCont);
              nextCont = contColsAt(cut);  // the NEXT row's indent
              lo = ext;
            }
            if (rows.empty()) {
              rows.push_back({0, 0});
              rowCont.push_back(0);
            }
            rowContOut = std::move(rowCont);
          }
          bool hl = hlSet.count(li + 1) != 0;
          const i64 rowTop = py;
          for (size_t ri = 0; ri < rows.size(); ri++) {
            LineBox line;
            line.unitIdx = ui;
            line.special = 2;
            line.codeLine = li;
            line.cbLo = rows[ri].lo;
            line.cbHi = rows[ri].hi;
            line.codeCont = ri > 0;
            line.contCols = ri < rowContOut.size() ? rowContOut[ri] : 0;
            line.snapLatinPx = (float)grid.dLatinPx;
            line.snapCjkPx = (float)grid.dCjkPx;
            line.codeHl = hl;
            line.height = adv;
            line.left = u.indent;
            line.width = lineWidthCode;
            line.y = (Su)py;
            if (ri == 0 && u.codeLineNo > 0) {
              line.marker = strs.intern(std::to_string(u.codeLineNo + (i32)li));
              line.markerStyle = u.codeStyle;
            } else if (first && u.marker) {
              line.marker = u.marker;
              line.markerStyle = u.markerStyle;
            }
            first = false;
            py += adv;
            fr.lines.push_back(line);
          }
          // sidecar rows for this logical line (equal-height zip, §5):
          // ordinary inline lines broken to the sidebar measure — math,
          // links and refs land through the generic cell render path
          if (hasSidecar && li < u.cells.size()) {
            const TableCell& cell = u.cells[li];
            i64 cy = rowTop;
            u32 prevBp = 0;
            for (u32 bp : cellBreaks[li].breakpoints) {
              LineItems r;
              const bool any = lineItems(cell.hl, cell.blockStart, prevBp, bp, r);
              prevBp = bp;
              if (!any) continue;
              const LineFill f = fillLine(cell.hl, r, metrics);
              LineBox sl;
              sl.unitIdx = ui;
              sl.cellIdx = (i32)li;
              sl.blockBegin = r.lo;
              sl.blockEnd = r.hi;
              sl.itemBegin = r.ilo;
              sl.itemEnd = r.ihi;
              sl.left = (Su)(u.indent + lineWidthCode + gapSu);
              sl.width = sidebarW;
              sl.srcSpan = f.span;
              sl.y = (Su)cy;
              Su sadv = baseLeading;
              if (f.maxAsc + f.maxDesc > sadv) sadv = f.maxAsc + f.maxDesc;
              sl.height = sadv;
              cy += sadv;
              fr.lines.push_back(sl);
            }
            if (cy > py) py = cy;  // the equal-height constraint
          }
        }
        continue;
      }

      if (u.kind == FlowUnit::K::Image) {
        // block figure image (figure-design.md §3): centred on the measure,
        // advance = display height (float placement is F2)
        LineBox line;
        line.unitIdx = ui;
        line.special = 5;
        Su shift = (lineWidth - imgW) / 2;
        if (shift < 0) shift = 0;
        line.left = u.indent + shift;
        line.width = imgW;
        line.height = imgH;
        if (u.src && !u.src->span.empty()) line.srcSpan = u.src->span;
        line.y = (Su)py;
        py += imgH;
        fr.lines.push_back(line);
        continue;
      }
      if (u.kind == FlowUnit::K::Math && u.mathBox) {
        // display formula: centred on the measure, advance = box extents
        const MathBox* mb = u.mathBox;
        LineBox line;
        line.unitIdx = ui;
        line.special = 4;
        Su shift = (lineWidth - mb->w) / 2;
        if (shift < 0) shift = 0;
        line.left = u.indent + shift;
        line.width = mb->w;
        if (u.src && !u.src->span.empty()) line.srcSpan = u.src->span;
        line.y = (Su)py;
        Su adv = mb->asc + mb->desc;
        if (adv < baseLeading) adv = baseLeading;
        line.height = adv;
        py += adv;
        fr.lines.push_back(line);
        continue;
      }
      if (u.kind == FlowUnit::K::Table && u.tCols > 0) {
        // three-line-flavoured grid: full-width rules above, between, and
        // below rows; equal columns; ragged cells aligned per column
        const Su colW = lineWidth / (Su)u.tCols;
        const Su padX = suRoundPx(kTableCellPadEm * cfg.baseSizePx);
        const Su padY = suRoundPx(kTableRowPadEm * cfg.baseSizePx);
        Su cellW = colW - 2 * padX;
        if (cellW < 64) cellW = 64;
        for (const TableCell& c : u.cells)  // each cell breaks to its content width
          cellBreaks.push_back(breakStream(c.blocks, c.hl, LineWidths{cellW}));
        const size_t nRows = u.cells.size() / u.tCols;
        auto addRule = [&](i64 yy) {
          LineBox rl;
          rl.unitIdx = ui;
          rl.special = 1;
          rl.left = u.indent;
          rl.width = lineWidth;
          rl.y = (Su)yy;
          fr.lines.push_back(rl);
        };
        addRule(py);
        for (size_t r = 0; r < nRows; r++) {
          i64 rowTop = py + padY;
          i64 rowBottom = rowTop + baseLeading;
          for (u32 c = 0; c < u.tCols; c++) {
            const TableCell& cell = u.cells[r * u.tCols + c];
            const BreakResult& cb = cellBreaks[r * u.tCols + c];
            i64 cy = rowTop;
            u32 prevBp = 0;
            for (size_t cli = 0; cli < cb.breakpoints.size(); cli++) {
              const u32 bp = cb.breakpoints[cli];
              LineItems lr;
              const bool any = lineItems(cell.hl, cell.blockStart, prevBp, bp, lr);
              prevBp = bp;
              if (!any) continue;
              const LineFill f = fillLine(cell.hl, lr, metrics);
              Su shift = 0;
              Su slack = cellW - suCeilPx(f.naturalPx);
              if (slack > 0) {
                u8 al = u.tAligns[c];
                if (al == 'c') shift = slack / 2;
                else if (al == 'r') shift = slack;
              }
              LineBox line;
              line.unitIdx = ui;
              line.cellIdx = (i32)(r * u.tCols + c);
              line.overfull = std::binary_search(cb.overfullLines.begin(),
                                                 cb.overfullLines.end(), (u32)cli);
              line.blockBegin = lr.lo;
              line.blockEnd = lr.hi;
              line.itemBegin = lr.ilo;
              line.itemEnd = lr.ihi;
              line.left = (Su)(u.indent + (Su)c * colW + padX + shift);
              line.width = cellW - shift;  // right edge stays at the column
                                           // content edge (audit: no overflow)
              line.srcSpan = f.span;
              line.endsWithHyphen = f.endsHyphen;
              line.y = (Su)cy;
              Su advance = baseLeading;
              if (f.maxAsc + f.maxDesc > advance) advance = f.maxAsc + f.maxDesc;
              line.height = advance;
              cy += advance;
              fr.lines.push_back(line);
            }
            if (cy > rowBottom) rowBottom = cy;
          }
          py = rowBottom + padY;
          addRule(py);
        }
        continue;
      }
      // Text unit
      const HList& h = u.hl;
      u32 prev = 0;
      bool firstLine = true;
      bool narrowLeft = false;
      const LineWidths lw = excl.widths(lineWidth, narrowLeft);
      lr.breaks.push_back({tb.pid, ui, breakStream(u.blocks, u.hl, lw)});
      const BreakResult& br = lr.breaks.back().r;
      excl.consume(br.breakpoints.size());
      for (size_t li = 0; li < br.breakpoints.size(); li++) {
        u32 bp = br.breakpoints[li];
        LineItems r;
        const bool any = lineItems(h, u.blockStart, prev, bp, r);
        prev = bp;
        if (!any) continue;
        const LineFill f = fillLine(h, r, metrics);
        const double naturalPx = f.naturalPx;
        const double totalWeight = f.totalWeight;
        const double capacityPx = f.capacityPx;
        const bool endsHyphen = f.endsHyphen;
        const bool joinSpace = joinsSpace(h, r.ihi);

        LineBox line;
        line.unitIdx = ui;
        line.blockBegin = r.lo;
        line.blockEnd = r.hi;
        line.itemBegin = r.ilo;
        line.itemEnd = r.ihi;
        line.left = u.indent;
        line.width = lineWidth;
        // F2 parshape replay: the first narrowK lines run beside the float
        const bool narrowed = li < (size_t)lw.narrowK && lw.narrow > 0;
        if (narrowed) {
          line.width = lw.narrow;
          if (narrowLeft) line.left += lineWidth - lw.narrow;
        }
        line.srcSpan = f.span;
        line.endsWithHyphen = endsHyphen;
        if (firstLine && u.marker) { line.marker = u.marker; line.markerStyle = u.markerStyle; }
        firstLine = false;

        // a hard line break ends a line like the paragraph end: ragged, a
        // real line boundary for copy
        const bool isLast = (bp == u.blocks.size()) || u.ragged || endsForced(h, r.ihi, u.blockStart[bp]);
        const bool overfull =
            std::binary_search(br.overfullLines.begin(), br.overfullLines.end(), (u32)li);
        line.overfull = overfull;
        double slackPx = narrowed
                             ? suToPx(lw.narrow) - naturalPx
                             : (cfg.widthPx - suToPx(u.indent)) - naturalPx;
        // a line without stretchable glue (all URL pieces / one unbreakable
        // token) cannot be justified — TeX's underfull box; it sets ragged
        // rather than pretending (real-world-report.md)
        if (totalWeight <= 0 && !isLast && slackPx != 0) line.noGlue = true;
        if (totalWeight > 0) {
          double d = slackPx / totalWeight;  // per unit weight (v2 §8)
          if (isLast && slackPx > 0) d = 0;
          // an Overfull line (a run wider than the measure, plan P0-12) is
          // set at the shrink limit and overflows; it never spreads
          // unbounded negative spacing over its glue
          if (overfull && slackPx < 0) {
            const double minD = -cfg.cost.shrinkThreshold * capacityPx / totalWeight;
            if (d < minD) d = minD;
          }
          line.wordDeltaPx = d;
          line.wordDeltaSu = (i32)std::llround(d * 64.0);
          if (f.anyCjkGap) {
            line.cjkDeltaPx = d * cfg.cjkJustifyK;
            line.cjkDeltaSu = (i32)std::llround(line.cjkDeltaPx * 64.0);
          }
        }
        line.join = isLast ? 0 : (endsHyphen || !joinSpace) ? 2 : 1;
        if (u.centered && slackPx > 0) {
          // caption centring: slack splits both sides; the right edge stays
          // inside the measure (width shrinks by the shift)
          Su cs = suRoundPx(slackPx / 2);
          line.left += cs;
          line.width -= cs;
        }

        Su advance = baseLeading;
        if (f.maxAsc + f.maxDesc > advance) advance = f.maxAsc + f.maxDesc;
        line.height = advance;
        line.y = (Su)py;
        py += advance;
        fr.lines.push_back(line);
      }
    }
    fr.h = (Su)py;
    y += py;
    if (p + 1 < tops.size()) y += paraGap;
    lr.paras.push_back(std::move(fr));
    firstBlock = false;
  }
  if (floatBottomAbs > y) y = floatBottomAbs;  // a trailing float still shows
  lr.docHeightSu = y;
  return lr;
}

std::string dumpBreaks(const LayoutResult& lr) {
  std::string out;
  for (const UnitBreaks& b : lr.breaks) {
    appendf(out, "top pid=%u unit=%u lines=%zu cost=%.4f breakpoints=[", b.pid, b.unit, b.r.breakpoints.size(),
            b.r.cost);
    for (size_t k = 0; k < b.r.breakpoints.size(); k++) appendf(out, "%s%u", k ? "," : "", b.r.breakpoints[k]);
    out += "]\n";
  }
  return out;
}

std::string dumpLayout(const LayoutResult& lr) {
  std::string out;
  appendf(out, "doc h=%lldsu\n", (long long)lr.docHeightSu);
  for (const ParaFrame& fr : lr.paras) {
    appendf(out, "para pid=%u y=%dsu w=%dsu h=%dsu\n", fr.pid, fr.y, fr.w, fr.h);
    for (size_t i = 0; i < fr.lines.size(); i++) {
      const LineBox& l = fr.lines[i];
      if (l.special == 1) {
        appendf(out, "  L%zu rule y=%dsu left=%dsu w=%dsu\n", i, l.y, l.left, l.width);
        continue;
      }
      if (l.special == 2) {
        appendf(out, "  L%zu code y=%dsu left=%dsu line=%u [%u,%u)%s%s%s\n", i,
                l.y, l.left, l.codeLine, l.cbLo, l.cbHi,
                l.codeCont ? " cont" : "", l.codeHl ? " hl" : "",
                l.marker ? " marker" : "");
        if (l.contCols) out.insert(out.size() - 1,
                                   " cc=" + std::to_string(l.contCols));
        continue;
      }
      if (l.special == 3) {
        appendf(out, "  L%zu raw y=%dsu left=%dsu w=%dsu\n", i, l.y, l.left, l.width);
        continue;
      }
      if (l.special == 4) {
        appendf(out, "  L%zu math y=%dsu left=%dsu w=%dsu\n", i, l.y, l.left, l.width);
        continue;
      }
      if (l.special == 5) {
        appendf(out, "  L%zu img y=%dsu left=%dsu w=%dsu h=%dsu\n", i, l.y,
                l.left, l.width, l.height);
        continue;
      }
      if (l.cellIdx >= 0) {
        appendf(out, "  L%zu cell=%d y=%dsu left=%dsu w=%dsu blocks=[%u,%u)%s\n",
                i, l.cellIdx, l.y, l.left, l.width, l.blockBegin, l.blockEnd,
                l.overfull ? " overfull" : "");
        continue;
      }
      appendf(out, "  L%zu y=%dsu left=%dsu w=%dsu dw=%dsu dc=%dsu join=%s%s%s%s blocks=[%u,%u) @[%u,%u)\n",
              i, l.y, l.left, l.width, l.wordDeltaSu, l.cjkDeltaSu,
              l.join == 0 ? "last" : l.join == 1 ? "space" : "none",
              l.endsWithHyphen ? " hyphen" : "", l.marker ? " marker" : "",
              l.overfull ? " overfull" : "",
              l.blockBegin, l.blockEnd, l.srcSpan.start, l.srcSpan.end);
    }
  }
  return out;
}

}  // namespace tsr
