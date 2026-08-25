#include "layout.h"

namespace tsr {

LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics,
                       const Config& cfg) {
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
      if (u.kind == FlowUnit::K::Code) {
        Su adv = baseLeading;
        if (metrics.hasVmet(u.codeStyle)) {
          const VMet& v = metrics.vmet(u.codeStyle);
          if (v.ascent + v.descent > adv) adv = v.ascent + v.descent;
        }
        bool first = true;
        for (StrRef lineRef : u.codeLines) {
          LineBox line;
          line.unitIdx = ui;
          line.special = 2;
          line.codeText = lineRef;
          line.left = u.indent;
          line.width = lineWidth;
          line.y = (Su)py;
          if (first && u.marker) { line.marker = u.marker; line.markerStyle = u.markerStyle; }
          first = false;
          py += adv;
          fr.lines.push_back(line);
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
        line.join = isLast ? 0 : (endsHyphen ? 2 : 1);

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
        appendf(out, "  L%zu code y=%dsu left=%dsu\n", i, l.y, l.left);
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
