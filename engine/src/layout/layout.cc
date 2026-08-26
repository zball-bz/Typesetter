#include "layout.h"

#include <unordered_set>

namespace tsr {

LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics,
                       Interner& strs, const Config& cfg) {
  LayoutResult lr;
  const Su measure = suFloorPx(cfg.widthPx);
  const Su baseLeading = suRoundPx(cfg.lineHeight * cfg.baseSizePx);
  const Su paraGap = suRoundPx(cfg.paraSpacingEm * cfg.baseSizePx);
  i64 y = 0;

  for (size_t p = 0; p < tops.size(); p++) {
    const TopBlock& tb = tops[p];
    ParaFrame fr;
    fr.pid = tb.pid;
    fr.y = (Su)y;
    fr.w = measure;

    i64 py = 0;
    for (u32 ui = 0; ui < tb.units.size(); ui++) {
      const FlowUnit& u = tb.units[ui];
      if (ui > 0) py += u.tightAbove ? paraGap / 3 : paraGap;
      const Su lineWidth = measure - u.indent;

      if (u.kind == FlowUnit::K::Rule) {
        LineBox line;
        line.unitIdx = ui;
        line.special = 1;
        line.left = u.indent;
        line.width = lineWidth;
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
        // ch grid (CH4, code-design.md §4): monospace is a metric contract —
        // 1ch per char, 2ch for CJK; wrap is a COLUMN computation, greedy
        // with a token-boundary preference, continuation rows indent 2ch.
        Su chSu = 0;
        if (u.codeWrap && u.chRef && metrics.hasWord(u.chRef, u.codeStyle))
          chSu = metrics.word(u.chRef, u.codeStyle).su;
        i32 cols = chSu > 0 ? (i32)(lineWidth / chSu) : 0;
        if (cols > 0 && cols < 8) cols = 8;
        auto isBreakable = [](u32 cp) {
          return cp == ' ' || cp == '\t' || cp == ',' || cp == ';' ||
                 cp == ')' || cp == '}' || cp == ']' || cp == '>';
        };
        std::unordered_set<u32> hlSet(u.hlLines.begin(), u.hlLines.end());
        bool first = true;
        for (u32 li = 0; li < (u32)u.codeRuns.size(); li++) {
          std::string joined;
          for (const FlowUnit::CodeRun& r : u.codeRuns[li])
            joined.append(strs.get(r.text));
          struct Row { u32 lo, hi; };
          std::vector<Row> rows;
          if (cols <= 0 || joined.empty()) {
            rows.push_back({0, (u32)joined.size()});
          } else {
            u32 lo = 0;
            while (lo < joined.size()) {
              i32 avail = rows.empty() ? cols : cols - 2;
              if (avail < 8) avail = 8;
              u32 p = lo;
              i32 col = 0;
              u32 lastBrk = 0;
              while (p < joined.size()) {
                u32 q = p;
                u32 cp = utf8Next(joined, q);
                i32 w = isCjk(cp) ? 2 : 1;
                if (col + w > avail) break;
                col += w;
                p = q;
                if (isBreakable(cp)) {
                  lastBrk = p;  // break AFTER the boundary
                } else if (isCjk(cp) && !isPunctOpen(cp)) {
                  // CJK wraps between any two characters (clreq), except
                  // before a closing punct / after an opening one (禁则)
                  u32 r = q;
                  u32 nx = q < joined.size() ? utf8Next(joined, r) : 0;
                  if (!(nx && isPunctClose(nx))) lastBrk = p;
                }
              }
              if (p >= joined.size()) {
                rows.push_back({lo, (u32)joined.size()});
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
              lo = ext;
            }
            if (rows.empty()) rows.push_back({0, 0});
          }
          bool hl = hlSet.count(li + 1) != 0;
          for (size_t ri = 0; ri < rows.size(); ri++) {
            LineBox line;
            line.unitIdx = ui;
            line.special = 2;
            line.codeLine = li;
            line.cbLo = rows[ri].lo;
            line.cbHi = rows[ri].hi;
            line.codeCont = ri > 0;
            line.codeHl = hl;
            line.height = adv;
            line.left = u.indent + (ri ? 2 * chSu : 0);
            line.width = lineWidth - (ri ? 2 * chSu : 0);
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
        }
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
            i64 cy = rowTop;
            u32 prevBp = 0;
            for (u32 bp : cell.breakpoints) {
              u32 lo = prevBp, hi = bp;
              while (lo < hi && cell.blocks[lo].isSpace()) lo++;
              while (hi > lo && cell.blocks[hi - 1].isSpace()) hi--;
              prevBp = bp;
              if (lo >= hi) continue;
              double naturalPx = 0;
              Su maxAsc = 0, maxDesc = 0;
              Span span{};
              bool spanSet = false;
              for (u32 i2 = lo; i2 < hi; i2++) {
                const LinebreakBlock& b = cell.blocks[i2];
                if (!b.isHyphen()) naturalPx += b.rawPx;
                if (metrics.hasVmet(b.style)) {
                  const VMet& v = metrics.vmet(b.style);
                  if (v.ascent > maxAsc) maxAsc = v.ascent;
                  if (v.descent > maxDesc) maxDesc = v.descent;
                }
                if (b.math) {
                  if (b.math->asc > maxAsc) maxAsc = b.math->asc;
                  if (b.math->desc > maxDesc) maxDesc = b.math->desc;
                }
                if (!b.span.empty()) {
                  if (!spanSet) { span = b.span; spanSet = true; }
                  else {
                    if (b.span.start < span.start) span.start = b.span.start;
                    if (b.span.end > span.end) span.end = b.span.end;
                  }
                }
              }
              const bool endsHyphen = cell.blocks[hi - 1].isHyphen();
              if (endsHyphen) naturalPx += cell.blocks[hi - 1].rawPx;
              Su shift = 0;
              Su slack = cellW - suCeilPx(naturalPx);
              if (slack > 0) {
                u8 al = u.tAligns[c];
                if (al == 'c') shift = slack / 2;
                else if (al == 'r') shift = slack;
              }
              LineBox line;
              line.unitIdx = ui;
              line.cellIdx = (i32)(r * u.tCols + c);
              line.blockBegin = lo;
              line.blockEnd = hi;
              line.left = (Su)(u.indent + (Su)c * colW + padX + shift);
              line.width = cellW - shift;  // right edge stays at the column
                                           // content edge (audit: no overflow)
              line.srcSpan = span;
              line.endsWithHyphen = endsHyphen;
              line.y = (Su)cy;
              Su advance = baseLeading;
              if (maxAsc + maxDesc > advance) advance = maxAsc + maxDesc;
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
      const std::vector<LinebreakBlock>& bl = u.blocks;
      u32 prev = 0;
      bool firstLine = true;
      for (size_t li = 0; li < u.breakpoints.size(); li++) {
        u32 bp = u.breakpoints[li];
        u32 lo = prev, hi = bp;
        while (lo < hi && bl[lo].isSpace()) lo++;
        while (hi > lo && bl[hi - 1].isSpace()) hi--;
        prev = bp;
        if (lo >= hi) continue;

        double naturalPx = 0;
        double totalWeight = 0;  // stretch positions the renderer will realize
        bool anyCjkGap = false;
        Su maxAsc = 0, maxDesc = 0;
        Span span{};
        bool spanSet = false;
        for (u32 i = lo; i < hi; i++) {
          const LinebreakBlock& b = bl[i];
          if (!b.isHyphen()) naturalPx += b.rawPx;
          if (b.isSpace() && b.stretchWeight > 0) totalWeight += b.stretchWeight;
          if (b.isCjkChar() && i + 1 < hi) {
            // a CJK char stretches (letter-spacing) only when the rendered gap
            // after it is CJK: next char, or a closing punct glyph
            const LinebreakBlock& nx = bl[i + 1];
            if (nx.isCjkChar() || (nx.isPunctGlyph() && !(nx.flags & BF_PUNCT_OPEN))) {
              totalWeight += b.stretchWeight;
              anyCjkGap = true;
            }
          }
          if (metrics.hasVmet(b.style)) {
            const VMet& v = metrics.vmet(b.style);
            if (v.ascent > maxAsc) maxAsc = v.ascent;
            if (v.descent > maxDesc) maxDesc = v.descent;
          }
          if (b.math) {
            if (b.math->asc > maxAsc) maxAsc = b.math->asc;
            if (b.math->desc > maxDesc) maxDesc = b.math->desc;
          }
          if (!b.span.empty()) {
            if (!spanSet) { span = b.span; spanSet = true; }
            else {
              if (b.span.start < span.start) span.start = b.span.start;
              if (b.span.end > span.end) span.end = b.span.end;
            }
          }
        }
        const bool endsHyphen = bl[hi - 1].isHyphen();
        if (endsHyphen) naturalPx += bl[hi - 1].rawPx;
        // did this break consume a real source space? (copy contract §9.3 —
        // synthetic glue: CJK boundary, punct halves, indents don't count)
        bool joinSpace = false;
        for (u32 j = hi; j < (u32)bl.size() && bl[j].isSpace(); j++) {
          if (!(bl[j].flags & (BF_BOUND | BF_PUNCT_SP | BF_INDENT))) {
            joinSpace = true;
            break;
          }
        }

        LineBox line;
        line.unitIdx = ui;
        line.blockBegin = lo;
        line.blockEnd = hi;
        line.left = u.indent;
        line.width = lineWidth;
        line.srcSpan = span;
        line.endsWithHyphen = endsHyphen;
        if (firstLine && u.marker) { line.marker = u.marker; line.markerStyle = u.markerStyle; }
        firstLine = false;

        const bool isLast = (bp == bl.size()) || u.ragged;
        double slackPx = (cfg.widthPx - suToPx(u.indent)) - naturalPx;
        if (totalWeight > 0) {
          double d = slackPx / totalWeight;  // per unit weight (v2 §8)
          if (isLast && slackPx > 0) d = 0;
          line.wordDeltaPx = d;
          line.wordDeltaSu = (i32)std::llround(d * 64.0);
          if (anyCjkGap) {
            line.cjkDeltaPx = d * cfg.cjkJustifyK;
            line.cjkDeltaSu = (i32)std::llround(line.cjkDeltaPx * 64.0);
          }
        }
        line.join = isLast ? 0 : (endsHyphen || !joinSpace) ? 2 : 1;

        Su advance = baseLeading;
        if (maxAsc + maxDesc > advance) advance = maxAsc + maxDesc;
        line.y = (Su)py;
        py += advance;
        fr.lines.push_back(line);
      }
    }
    fr.h = (Su)py;
    y += py;
    if (p + 1 < tops.size()) y += paraGap;
    lr.paras.push_back(std::move(fr));
  }
  lr.docHeightSu = y;
  return lr;
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
      if (l.cellIdx >= 0) {
        appendf(out, "  L%zu cell=%d y=%dsu left=%dsu w=%dsu blocks=[%u,%u)\n",
                i, l.cellIdx, l.y, l.left, l.width, l.blockBegin, l.blockEnd);
        continue;
      }
      appendf(out, "  L%zu y=%dsu left=%dsu w=%dsu dw=%dsu dc=%dsu join=%s%s%s blocks=[%u,%u) @[%u,%u)\n",
              i, l.y, l.left, l.width, l.wordDeltaSu, l.cjkDeltaSu,
              l.join == 0 ? "last" : l.join == 1 ? "space" : "none",
              l.endsWithHyphen ? " hyphen" : "", l.marker ? " marker" : "",
              l.blockBegin, l.blockEnd, l.srcSpan.start, l.srcSpan.end);
    }
  }
  return out;
}

}  // namespace tsr
