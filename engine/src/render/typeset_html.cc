#include "typeset_html.h"

namespace tsr {

static void escapeHtml(std::string& out, std::string_view s) {
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
}

// Stable px formatting: up to 3 decimals, trailing zeros trimmed.
static void fmtPx(std::string& out, double px) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3f", px);
  size_t len = std::strlen(buf);
  while (len > 0 && buf[len - 1] == '0') len--;
  if (len > 0 && buf[len - 1] == '.') len--;
  out.append(buf, len);
  out += "px";
}

static void runClasses(std::string& out, const Styling& st) {
  out += "tsr-r";
  if (st.bits & CLS_BOLD) out += " tsr-b";
  if (st.bits & CLS_EM) out += " tsr-i";
  if (st.bits & CLS_CJK) out += " tsr-cjk";
  if (st.bits & CLS_CODE) out += " tsr-code";
}

static void runStyleAttr(std::string& out, const Styling& st, const Config& cfg) {
  if (st.sizeMul != 1.0f) {
    out += " style=\"font-size:";
    fmtPx(out, cfg.baseSizePx * (double)st.sizeMul);
    out += "\"";
  }
}

std::string renderTypeset(const std::vector<TopBlock>& tops, const LayoutResult& lr,
                          const StyleTable& styles, const Interner& strs,
                          const Config& cfg) {
  std::string out;
  out += "<div class=\"tsr-doc\">\n";
  for (size_t p = 0; p < lr.paras.size(); p++) {
    const ParaFrame& fr = lr.paras[p];
    const TopBlock& tb = tops[p];
    appendf(out, "<div class=\"tsr-para\" data-pid=\"%u\" style=\"position:relative;height:", fr.pid);
    fmtPx(out, suToPx(fr.h));
    if (p + 1 < lr.paras.size()) {
      out += ";margin-bottom:";
      fmtPx(out, cfg.paraSpacingEm * cfg.baseSizePx);
    }
    out += "\">\n";
    for (const LineBox& l : fr.lines) {
      if (l.special == 1) {
        out += "<div class=\"tsr-rule\" style=\"top:";
        fmtPx(out, suToPx(l.y));
        out += ";left:";
        fmtPx(out, suToPx(l.left));
        out += ";width:";
        fmtPx(out, suToPx(l.width));
        out += "\"></div>\n";
        continue;
      }
      out += "<div class=\"tsr-line\"";
      if (!l.srcSpan.empty())
        appendf(out, " data-s=\"%u\" data-e=\"%u\"", l.srcSpan.start, l.srcSpan.end);
      if (l.join == 1) out += " data-join=\"space\"";
      else if (l.join == 2) out += " data-join=\"none\"";
      if (tb.units[l.unitIdx].ragged) out += " data-ragged=\"1\"";
      out += " style=\"top:";
      fmtPx(out, suToPx(l.y));
      out += ";left:";
      fmtPx(out, suToPx(l.left));
      out += ";width:";
      fmtPx(out, suToPx(l.width));
      if (l.special == 0 && l.wordDeltaPx != 0) {
        out += ";word-spacing:";
        fmtPx(out, l.wordDeltaPx);
      }
      out += "\">";

      if (l.marker) {
        const Styling& mst = styles.get(l.markerStyle);
        out += "<span class=\"tsr-marker ";
        runClasses(out, mst);
        out += "\" data-syn=\"marker\"";
        runStyleAttr(out, mst, cfg);
        out += ">";
        escapeHtml(out, strs.get(l.marker));
        out += "</span>";
      }

      if (l.special == 2) {
        const FlowUnit& u = tb.units[l.unitIdx];
        const Styling& cst = styles.get(u.codeStyle);
        out += "<span class=\"";
        runClasses(out, cst);
        out += "\"";
        runStyleAttr(out, cst, cfg);
        out += ">";
        escapeHtml(out, strs.get(l.codeText));
        out += "</span></div>\n";
        continue;
      }

      const FlowUnit& u = tb.units[l.unitIdx];
      u32 i = l.blockBegin;
      auto openRun = [&](const Styling& sty, StrRef url, const LinebreakBlock& first,
                         const std::string& extraStyle, const char* extraCls) {
        const bool isLink = url != 0;
        out += isLink ? "<a class=\"" : "<span class=\"";
        runClasses(out, sty);
        if (extraCls && *extraCls) { out += " "; out += extraCls; }
        out += "\"";
        if (isLink) {
          out += " href=\"";
          escapeHtml(out, strs.get(url));
          out += "\"";
        }
        if (!first.span.empty()) appendf(out, " data-s=\"%u\"", first.span.start);
        std::string style;
        if (sty.sizeMul != 1.0f) {
          style += "font-size:";
          fmtPx(style, cfg.baseSizePx * (double)sty.sizeMul);
        }
        if (!extraStyle.empty()) {
          if (!style.empty()) style += ";";
          style += extraStyle;
        }
        if (!style.empty()) {
          out += " style=\"";
          out += style;
          out += "\"";
        }
        out += ">";
        return isLink;
      };
      while (i < l.blockEnd) {
        const LinebreakBlock& b = u.blocks[i];
        // final hyphen glyph
        if (b.isHyphen()) {
          if (i == l.blockEnd - 1 && l.endsWithHyphen) {
            bool link = openRun(styles.get(b.style), 0, b, "", nullptr);
            out.insert(out.size() - 1, " data-syn=\"hyphen\"");  // before '>'
            out += "-";
            out += link ? "</a>" : "</span>";
          }
          i++;
          continue;
        }
        if (b.flags & (BF_INDENT | BF_BOUND)) {
          double w = b.rawPx;
          if (b.flags & BF_BOUND) w += l.wordDeltaPx * (double)b.stretchWeight;
          out += "<span class=\"tsr-sp\" data-syn=\"";
          out += (b.flags & BF_INDENT) ? "indent" : "boundary";
          out += "\" style=\"width:";
          fmtPx(out, w);
          out += "\"></span>";
          i++;
          continue;
        }
        if (b.flags & BF_PUNCT_SP) { i++; continue; }  // absorbed by glyph advance
        if (b.isPunctGlyph()) {
          const bool open = b.flags & BF_PUNCT_OPEN;
          const bool halfPresent =
              open ? (i > l.blockBegin && (u.blocks[i - 1].flags & BF_PUNCT_SP) &&
                      (u.blocks[i - 1].flags & BF_PUNCT_OPEN))
                   : (i + 1 < l.blockEnd && (u.blocks[i + 1].flags & BF_PUNCT_SP) &&
                      !(u.blocks[i + 1].flags & BF_PUNCT_OPEN));
          const char* squeeze = halfPresent ? "" : (open ? "tsr-sqL" : "tsr-sqR");
          bool link = openRun(styles.get(b.style), b.linkUrl, b, "", squeeze);
          escapeHtml(out, strs.get(b.text));
          out += link ? "</a>" : "</span>";
          i++;
          continue;
        }
        // ——/…… pair: own span, no internal letter-spacing (the two glyphs
        // must join seamlessly); its stretch gap becomes a positive margin.
        if (b.flags & BF_PAIR) {
          std::string extra;
          if (l.cjkDeltaPx != 0) {
            const LinebreakBlock* nx = (i + 1 < l.blockEnd) ? &u.blocks[i + 1] : nullptr;
            bool keep = nx && (nx->isCjkChar() ||
                               (nx->isPunctGlyph() && !(nx->flags & BF_PUNCT_OPEN)));
            if (keep) {
              extra += "margin-right:";
              fmtPx(extra, l.cjkDeltaPx);
            }
          }
          bool link = openRun(styles.get(b.style), b.linkUrl, b, extra, nullptr);
          escapeHtml(out, strs.get(b.text));
          out += link ? "</a>" : "</span>";
          i++;
          continue;
        }
        if (b.isCjkChar()) {
          StyleId st = b.style;
          StrRef url = b.linkUrl;
          u32 j = i;
          while (j < l.blockEnd && u.blocks[j].isCjkChar() && !(u.blocks[j].flags & BF_PAIR) &&
                 u.blocks[j].style == st && u.blocks[j].linkUrl == url)
            j++;
          std::string extra;
          if (l.cjkDeltaPx != 0) {
            extra += "letter-spacing:";
            fmtPx(extra, l.cjkDeltaPx);
            // trailing letter-space is real only when the next rendered gap is
            // CJK (char run of another style, or a closing punct glyph)
            const LinebreakBlock* nx = (j < l.blockEnd) ? &u.blocks[j] : nullptr;
            bool keep = nx && (nx->isCjkChar() ||
                               (nx->isPunctGlyph() && !(nx->flags & BF_PUNCT_OPEN)));
            if (!keep) {
              // negate via the value, never by prepending '-': a negative
              // delta would otherwise render "--Npx" (invalid, dropped)
              extra += ";margin-right:";
              fmtPx(extra, -l.cjkDeltaPx);
            }
          }
          bool link = openRun(styles.get(st), url, b, extra, nullptr);
          std::string runText;
          for (u32 k = i; k < j; k++) runText += strs.get(u.blocks[k].text);
          escapeHtml(out, runText);
          out += link ? "</a>" : "</span>";
          i = j;
          continue;
        }
        // Latin run: words and spaces
        StyleId st = b.style;
        StrRef url = b.linkUrl;
        u32 j = i;
        while (j < l.blockEnd && u.blocks[j].style == st && u.blocks[j].linkUrl == url &&
               !u.blocks[j].isHyphen() && !u.blocks[j].isCjkChar() &&
               !u.blocks[j].isPunctGlyph() && !(u.blocks[j].flags & (BF_INDENT | BF_BOUND)) &&
               !(u.blocks[j].flags & BF_PUNCT_SP))
          j++;
        bool link = openRun(styles.get(st), url, b, "", nullptr);
        std::string runText;
        for (u32 k = i; k < j; k++)
          runText += u.blocks[k].isSpace() ? std::string(" ")
                                           : std::string(strs.get(u.blocks[k].text));
        escapeHtml(out, runText);
        out += link ? "</a>" : "</span>";
        i = j;
      }
      out += "</div>\n";
    }
    out += "</div>\n";
  }
  out += "</div>\n";
  return out;
}

}  // namespace tsr
