#include "typeset_html.h"

#include <algorithm>

#include "../math/mathfont.h"

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

// Inline-style overrides a run carries beyond its classes (document-model
// §3): explicit font-family wins over the .tsr-cjk var rule by specificity.
static void styleInto(std::string& style, const Styling& st, const Config& cfg,
                      const Interner& strs) {
  double base = st.sizePx > 0 ? (double)st.sizePx : cfg.baseSizePx;
  if (st.sizeMul != 1.0f || st.sizePx > 0) {
    style += "font-size:";
    fmtPx(style, base * (double)st.sizeMul);
    style += ";";
  }
  if (st.fontFamily) {
    style += "font-family:";
    escapeHtml(style, strs.get(st.fontFamily));
    style += ";";
  }
  if (st.color) {
    style += "color:";
    escapeHtml(style, strs.get(st.color));
    style += ";";
  }
  if (st.bits & (CLS_UNDER | CLS_OVER | CLS_STRIKE)) {
    style += "text-decoration:";
    if (st.bits & CLS_UNDER) style += "underline ";
    if (st.bits & CLS_OVER) style += "overline ";
    if (st.bits & CLS_STRIKE) style += "line-through ";
    style.pop_back();
    style += ";";
  }
  if (!style.empty() && style.back() == ';') style.pop_back();
}

static void langAttr(std::string& out, const Styling& st, const Interner& strs) {
  if (!st.lang) return;
  out += " lang=\"";
  escapeHtml(out, strs.get(st.lang));
  out += "\"";
}

static void runStyleAttr(std::string& out, const Styling& st, const Config& cfg,
                         const Interner& strs) {
  std::string style;
  styleInto(style, st, cfg, strs);
  langAttr(out, st, strs);
  if (!style.empty()) {
    out += " style=\"";
    out += style;
    out += "\"";
  }
}

// Positioned glyph runs inside one formula (math-design.md §8): flatten the
// MathBox tree to absolute (x, baseline) leaves. A glyph span pins its text
// baseline by explicit line-height == the font's hhea height: the baseline
// then sits exactly kAscender·px/upem below the span top.
static void mathLeaves(std::string& out, const MathBox* b, const Interner& strs,
                       Su x, Su base) {
  switch (b->kind) {
    case MathKind::Glyph: {
      const double px = (double)b->px;
      const double fA = (double)mathfont::kAscender * px / mathfont::kUpem;
      const double fH =
          (double)(mathfont::kAscender + mathfont::kDescender) * px / mathfont::kUpem;
      out += "<span class=\"tsr-mg\" style=\"left:";
      fmtPx(out, suToPx(x));
      out += ";top:";
      fmtPx(out, suToPx(base) - fA);
      out += ";font-size:";
      fmtPx(out, px);
      out += ";line-height:";
      fmtPx(out, fH);
      out += "\">";
      escapeHtml(out, strs.get(b->text));
      out += "</span>";
      return;
    }
    case MathKind::Rule:
      out += "<span class=\"tsr-mr\" style=\"left:";
      fmtPx(out, suToPx(x));
      out += ";top:";
      fmtPx(out, suToPx(base - b->asc));
      out += ";width:";
      fmtPx(out, suToPx(b->w));
      out += ";height:";
      fmtPx(out, suToPx(b->asc + b->desc));
      out += "\"></span>";
      return;
    case MathKind::Spacer:
      return;
    case MathKind::HBox:
      for (const MathKid& k : b->kids)
        mathLeaves(out, k.box, strs, x + k.dx, base - k.dy);
      return;
  }
}

// One formula as an inline box (§8): width/height from the box, the baseline
// pinned with vertical-align (inline) or an explicit top offset (display).
// data-syn="math" + data-src carry the copy contract (§9.3: source text).
static void mathSpan(std::string& out, const MathBox* mb, StrRef srcRef,
                     bool display, const Interner& strs, Span span,
                     const std::string& posStyle) {
  out += "<span class=\"tsr-math\" data-syn=\"math\" data-src=\"";
  if (srcRef) {  // later segments of a split formula contribute nothing
    out += display ? "$ " : "$";
    escapeHtml(out, strs.get(srcRef));
    out += display ? " $" : "$";
  }
  out += "\"";
  if (!span.empty()) appendf(out, " data-s=\"%u\" data-e=\"%u\"", span.start, span.end);
  out += " style=\"width:";
  fmtPx(out, suToPx(mb->w));
  out += ";height:";
  fmtPx(out, suToPx(mb->asc + mb->desc));
  if (!posStyle.empty()) {
    out += ";";
    out += posStyle;
  } else {
    out += ";vertical-align:";
    fmtPx(out, -suToPx(mb->desc));
  }
  out += "\">";
  mathLeaves(out, mb, strs, 0, mb->asc);
  out += "</span>";
}

// One line box (any kind) at an optional vertical rebase — shared by the
// flowing serializer (yShift 0, lines inside their para frame) and the
// paged serializer (yShift rebases into the sheet).
static void renderLineBox(std::string& out, const TopBlock& tb, const ParaFrame& fr,
                          size_t li, const StyleTable& styles, const Interner& strs,
                          const Config& cfg, u32& lastAnchored, Su yShift) {
  const LineBox& l = fr.lines[li];
  const Su ly = (Su)(l.y + yShift);
      if (l.special == 3) {  // raw passthrough (trusted, handler-declared)
        const FlowUnit& ru = tb.units[l.unitIdx];
        out += "<div class=\"tsr-raw\" style=\"top:";
        fmtPx(out, suToPx(ly));
        out += ";left:";
        fmtPx(out, suToPx(l.left));
        out += ";width:";
        fmtPx(out, suToPx(l.width));
        out += ";height:";
        fmtPx(out, ru.rawHpx);
        out += "\">";
        out += strs.get(ru.rawHtml);  // the ONE unescaped path (§9)
        out += "</div>\n";
        return;
      }
      if (l.special == 1) {
        out += "<div class=\"tsr-rule\" style=\"top:";
        fmtPx(out, suToPx(ly));
        out += ";left:";
        fmtPx(out, suToPx(l.left));
        out += ";width:";
        fmtPx(out, suToPx(l.width));
        out += "\"></div>\n";
        return;
      }
      if (l.special == 4) {  // display math (§8): centred block formula
        const FlowUnit& mu = tb.units[l.unitIdx];
        out += "<div class=\"tsr-line\"";
        if (mu.anchor && l.unitIdx != lastAnchored) {
          lastAnchored = l.unitIdx;
          out += " id=\"tsr-";
          escapeHtml(out, strs.get(mu.anchor));
          out += "\"";
        }
        if (!l.srcSpan.empty())
          appendf(out, " data-s=\"%u\" data-e=\"%u\"", l.srcSpan.start, l.srcSpan.end);
        out += " data-ragged=\"1\" style=\"top:";
        fmtPx(out, suToPx(ly));
        out += ";left:";
        fmtPx(out, suToPx(l.left));
        out += ";width:";
        fmtPx(out, suToPx(l.width));
        Su boxHh = mu.mathBox->asc + mu.mathBox->desc;
        Su advH = suRoundPx(cfg.lineHeight * cfg.baseSizePx);
        if (boxHh > advH) advH = boxHh;
        out += ";height:";
        fmtPx(out, suToPx(advH));
        out += "\">";
        if (mu.eqTag) {
          // right-margin equation number, at the measure's right edge
          Su lineRight = l.left + l.width;
          Su measureR = suFloorPx(cfg.widthPx);
          out += "<span class=\"tsr-eqno\" data-syn=\"eqno\" style=\"right:";
          fmtPx(out, -suToPx(measureR - lineRight));
          out += "\">";
          escapeHtml(out, strs.get(mu.eqTag));
          out += "</span>";
        }
        StrRef srcRef = 0;
        if (mu.src)
          for (const ArgVal& a : mu.src->args)
            if (a.key == ArgK::src && a.tag == ArgTag::Str) srcRef = a.ref;
        Su boxH = mu.mathBox->asc + mu.mathBox->desc;
        Su adv = suRoundPx(cfg.lineHeight * cfg.baseSizePx);
        std::string pos = "position:absolute;left:0;top:";
        fmtPx(pos, boxH < adv ? suToPx(adv - boxH) / 2.0 : 0.0);
        mathSpan(out, mu.mathBox, srcRef, /*display=*/true, strs, {}, pos);
        out += "</div>\n";
        return;
      }
      if (l.special == 5) {  // figure image / placeholder (figure-design §5)
        const FlowUnit& iu = tb.units[l.unitIdx];
        std::string pos = "top:";
        fmtPx(pos, suToPx(ly));
        pos += ";left:";
        fmtPx(pos, suToPx(l.left));
        pos += ";width:";
        fmtPx(pos, suToPx(l.width));
        pos += ";height:";
        fmtPx(pos, suToPx(l.height));
        std::string id;
        if (iu.anchor && l.unitIdx != lastAnchored) {
          lastAnchored = l.unitIdx;
          id = " id=\"tsr-";
          escapeHtml(id, strs.get(iu.anchor));
          id += "\"";
        }
        std::string span;
        if (!l.srcSpan.empty())
          appendf(span, " data-s=\"%u\" data-e=\"%u\"", l.srcSpan.start,
                  l.srcSpan.end);
        if (iu.imgSrc) {
          out += "<img class=\"tsr-img\" draggable=\"false\" data-syn=\"image\"";
          out += id;
          out += span;
          out += " src=\"";
          escapeHtml(out, strs.get(iu.imgSrc));
          out += "\" alt=\"";
          if (iu.imgAlt) escapeHtml(out, strs.get(iu.imgAlt));
          out += "\" style=\"";
          out += pos;
          out += "\">\n";
        } else {
          out += "<div class=\"tsr-imgph\" data-syn=\"image\"";
          out += id;
          out += span;
          out += " style=\"";
          out += pos;
          out += "\">";
          if (iu.imgAlt) escapeHtml(out, strs.get(iu.imgAlt));
          out += "</div>\n";
        }
        return;
      }
      out += "<div class=\"tsr-line";
      if (l.special == 2 && l.codeHl) out += " tsr-hlline";
      out += "\"";
      if (l.special == 2) {
        // code rows are ragged by nature (the audit's justify checks do not
        // apply); a wrapped row additionally rejoins its continuation (§9.3)
        out += " data-ragged=\"1\"";
        size_t self = li;
        if (self + 1 < fr.lines.size() && fr.lines[self + 1].special == 2 &&
            fr.lines[self + 1].codeCont)
          out += " data-join=\"none\"";
      }
      {  // label anchor: first line of an anchored unit gets the id
        const FlowUnit& au = tb.units[l.unitIdx];
        if (au.anchor && l.unitIdx != lastAnchored) {
          lastAnchored = l.unitIdx;
          out += " id=\"tsr-";
          escapeHtml(out, strs.get(au.anchor));
          out += "\"";
        }
      }
      if (!l.srcSpan.empty())
        appendf(out, " data-s=\"%u\" data-e=\"%u\"", l.srcSpan.start, l.srcSpan.end);
      if (l.join == 1) out += " data-join=\"space\"";
      else if (l.join == 2) out += " data-join=\"none\"";
      if (tb.units[l.unitIdx].ragged) out += " data-ragged=\"1\"";
      if (l.cellIdx >= 0) out += " data-cell=\"1\"";
      out += " style=\"top:";
      fmtPx(out, suToPx(ly));
      out += ";left:";
      fmtPx(out, suToPx(l.left));
      out += ";width:";
      fmtPx(out, suToPx(l.width));
      if (l.special == 2) {
        const std::string* feats = &cfg.codeFontFeatures;
        if (tb.units[l.unitIdx].codeLang) {
          auto it = cfg.codeFontFeaturesByLang.find(
              std::string(strs.get(tb.units[l.unitIdx].codeLang)));
          if (it != cfg.codeFontFeaturesByLang.end()) feats = &it->second;
        }
        if (!feats->empty()) {
          out += ";font-feature-settings:";
          escapeHtml(out, *feats);
        }
      }
      if (l.special == 2 && l.height > 0) {
        // baseline sits centred in the row (the hl background made the
        // top-stuck default line box visible); hl rows also paint height
        out += ";line-height:";
        fmtPx(out, suToPx(l.height));
        if (l.codeHl) {
          out += ";height:";
          fmtPx(out, suToPx(l.height));
        }
      }
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
        runStyleAttr(out, mst, cfg, strs);
        out += ">";
        escapeHtml(out, strs.get(l.marker));
        out += "</span>";
      }

      if (l.special == 2) {
        const FlowUnit& u = tb.units[l.unitIdx];
        if (l.codeCont && l.contCols > 0) {
          // grid-exact continuation indent: REAL spaces in the same mono
          // flow (exact ch in ANY font), synthetic for both copy paths
          const Styling& cst = styles.get(u.codeStyle);
          out += "<span class=\"";
          runClasses(out, cst);
          out += "\" data-syn=\"cont\"";
          runStyleAttr(out, cst, cfg, strs);
          out += ">";
          out.append(l.contCols, ' ');
          out += "</span>";
        }
        const bool snap = l.snapLatinPx > 0 || l.snapCjkPx > 0;
        u32 off = 0;
        for (const FlowUnit::CodeRun& r : u.codeRuns[l.codeLine]) {
          std::string_view t = strs.get(r.text);
          u32 rLo = off, rHi = off + (u32)t.size();
          off = rHi;
          u32 lo = l.cbLo > rLo ? l.cbLo : rLo;
          u32 hi = l.cbHi < rHi ? l.cbHi : rHi;
          if (lo >= hi) continue;
          std::string_view seg = t.substr(lo - rLo, hi - lo);
          // snap-kerning: split the run by script, letter-spacing per side
          u32 s0 = 0;
          while (s0 < seg.size()) {
            u32 s1 = s0;
            bool cjk = false;
            if (snap) {
              u32 probe = s0;
              cjk = isCjk(utf8Next(seg, probe));
              s1 = s0;
              while (s1 < seg.size()) {
                u32 nx = s1;
                if (isCjk(utf8Next(seg, nx)) != cjk) break;
                s1 = nx;
              }
            } else {
              s1 = (u32)seg.size();
            }
            const Styling& cst = styles.get(r.style);
            out += "<span class=\"";
            runClasses(out, cst);
            out += "\"";
            runStyleAttr(out, cst, cfg, strs);
            double d = snap ? (cjk ? l.snapCjkPx : l.snapLatinPx) : 0;
            if (d > 0) {
              out += " data-snap=\"1\" style=\"letter-spacing:";
              fmtPx(out, d);
              out += "\"";
            }
            out += ">";
            escapeHtml(out, seg.substr(s0, s1 - s0));
            out += "</span>";
            s0 = s1;
          }
        }
        out += "</div>\n";
        return;
      }

      const FlowUnit& u = tb.units[l.unitIdx];
      const std::vector<LinebreakBlock>& bl =
          l.cellIdx >= 0 ? u.cells[(size_t)l.cellIdx].blocks : u.blocks;
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
        if (first.flags & BF_REF) out += " data-syn=\"ref\"";  // §9.3: copy skips
        else if (!first.span.empty()) appendf(out, " data-s=\"%u\"", first.span.start);
        langAttr(out, sty, strs);
        std::string style;
        styleInto(style, sty, cfg, strs);
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
        const LinebreakBlock& b = bl[i];
        if (b.math) {  // inline formula: one box, baseline via vertical-align
          mathSpan(out, b.math, b.text, /*display=*/false, strs, b.span,
                   std::string());
          i++;
          continue;
        }
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
              open ? (i > l.blockBegin && (bl[i - 1].flags & BF_PUNCT_SP) &&
                      (bl[i - 1].flags & BF_PUNCT_OPEN))
                   : (i + 1 < l.blockEnd && (bl[i + 1].flags & BF_PUNCT_SP) &&
                      !(bl[i + 1].flags & BF_PUNCT_OPEN));
          const char* squeeze = halfPresent ? "" : (open ? "tsr-sqL" : "tsr-sqR");
          bool link = openRun(styles.get(b.style), b.linkUrl, b, "", squeeze);
          escapeHtml(out, strs.get(b.text));
          out += link ? "</a>" : "</span>";
          i++;
          continue;
        }
        // —/…… defined-width block (1em single, 2em pair — App C): the
        // engine ASSUMES the defined advance instead of measuring (canvas
        // cannot predict DOM's full-width-ization of these), and the DOM —
        // under text-spacing-trim — shapes them to exactly that advance
        // with connected ink. Own span so no letter-spacing splits the
        // pair; its stretch gap becomes a margin.
        if (b.flags & BF_PAIR) {
          // the assumed advance is ENFORCED as an inline-block width: Blink
          // full-width-izes CJK dashes to exactly this budget, Gecko does
          // not (Noto's em dash is ~0.89em) and would leave every line with
          // a —— visibly short of the measure. No overflow:hidden — that
          // would move the baseline to the box bottom.
          std::string extra = "display:inline-block;text-align:center;width:";
          fmtPx(extra, b.rawPx);
          if (l.cjkDeltaPx != 0) {
            const LinebreakBlock* nx = (i + 1 < l.blockEnd) ? &bl[i + 1] : nullptr;
            bool keep = nx && (nx->isCjkChar() ||
                               (nx->isPunctGlyph() && !(nx->flags & BF_PUNCT_OPEN)));
            if (keep) {
              extra += ";margin-right:";
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
          while (j < l.blockEnd && bl[j].isCjkChar() && !(bl[j].flags & BF_PAIR) &&
                 bl[j].style == st && bl[j].linkUrl == url)
            j++;
          std::string extra;
          if (l.cjkDeltaPx != 0) {
            extra += "letter-spacing:";
            fmtPx(extra, l.cjkDeltaPx);
            // trailing letter-space is real only when the next rendered gap is
            // CJK (char run of another style, or a closing punct glyph)
            const LinebreakBlock* nx = (j < l.blockEnd) ? &bl[j] : nullptr;
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
          for (u32 k = i; k < j; k++) runText += strs.get(bl[k].text);
          escapeHtml(out, runText);
          out += link ? "</a>" : "</span>";
          i = j;
          continue;
        }
        // Latin run: words and spaces. Mid-line hyphen points are absorbed
        // (they render nothing) so the pieces sit in ONE text node and the
        // browser kerns across the junction — the widths modelled it
        // (LinebreakBlock::kernPx); a line-final hyphen still terminates
        // the run to get its glyph.
        StyleId st = b.style;
        StrRef url = b.linkUrl;
        u32 j = i;
        while (j < l.blockEnd && bl[j].style == st && bl[j].linkUrl == url &&
               (!bl[j].isHyphen() || j + 1 < l.blockEnd) &&
               !bl[j].isCjkChar() && !bl[j].math &&
               !bl[j].isPunctGlyph() && !(bl[j].flags & (BF_INDENT | BF_BOUND)) &&
               !(bl[j].flags & BF_PUNCT_SP))
          j++;
        bool link = openRun(styles.get(st), url, b, "", nullptr);
        std::string runText;
        for (u32 k = i; k < j; k++)
          runText += bl[k].isHyphen() ? std::string()
                     : bl[k].isSpace() ? std::string(" ")
                                       : std::string(strs.get(bl[k].text));
        escapeHtml(out, runText);
        out += link ? "</a>" : "</span>";
        i = j;
      }
      out += "</div>\n";
}

std::string renderTypeset(const std::vector<TopBlock>& tops, const LayoutResult& lr,
                          const StyleTable& styles, const Interner& strs,
                          const Config& cfg) {
  std::string out;
  out += "<div class=\"tsr-doc\">\n";
  for (size_t p = 0; p < lr.paras.size(); p++) {
    const ParaFrame& fr = lr.paras[p];
    const TopBlock& tb = tops[p];
    u32 lastAnchored = 0xFFFFFFFFu;
    appendf(out, "<div class=\"tsr-para\" data-pid=\"%u\" style=\"position:relative;height:", fr.pid);
    fmtPx(out, suToPx(fr.h));
    if (p + 1 < lr.paras.size()) {
      out += ";margin-bottom:";
      fmtPx(out, cfg.paraSpacingEm * cfg.baseSizePx);
    }
    out += "\">\n";
    for (size_t li = 0; li < fr.lines.size(); li++)
      renderLineBox(out, tb, fr, li, styles, strs, cfg, lastAnchored, 0);
    out += "</div>\n";
  }
  out += "</div>\n";
  return out;
}

// Paged serializer (pages-design.md §2): a post-pass over the finished
// layout — no re-break, no new layout mode. Lines are grouped into atomic
// BANDS, bands are cut greedily into sheets with keep-rules, and each band
// renders via renderLineBox rebased into its sheet.
std::string renderPages(const std::vector<TopBlock>& tops, const LayoutResult& lr,
                        const StyleTable& styles, const Interner& strs,
                        const Config& cfg, double pageHeightPx) {
  struct Band {
    size_t para;
    u32 lo, hi;          // [lo, hi) into fr.lines
    i64 top, bot;        // absolute su
    bool stickAfter = false;  // heading / image: keep with what follows
    u32 ordinal = 0, count = 0;  // text-unit line position (widow/orphan)
  };
  const Su H = suRoundPx(pageHeightPx);
  std::vector<Band> bands;

  for (size_t p = 0; p < lr.paras.size(); p++) {
    const ParaFrame& fr = lr.paras[p];
    const TopBlock& tb = tops[p];
    // per-unit text line counts (widow/orphan bookkeeping)
    std::vector<u32> unitLines(tb.units.size(), 0), unitSeen(tb.units.size(), 0);
    for (const LineBox& l : fr.lines)
      if (l.special == 0 && l.cellIdx < 0 &&
          tb.units[l.unitIdx].kind == FlowUnit::K::Text)
        unitLines[l.unitIdx]++;
    size_t i = 0;
    while (i < fr.lines.size()) {
      const LineBox& l = fr.lines[i];
      const FlowUnit& u = tb.units[l.unitIdx];
      Band b;
      b.para = p;
      b.lo = (u32)i;
      auto lineTop = [&](const LineBox& x) -> i64 {
        // rules store y at their midline
        return (i64)fr.y + x.y - (x.special == 1 ? x.height / 2 : 0);
      };
      b.top = lineTop(l);
      b.bot = b.top + l.height;
      size_t j = i + 1;
      auto sameUnit = [&](size_t k) {
        return k < fr.lines.size() && fr.lines[k].unitIdx == l.unitIdx;
      };
      if (u.kind == FlowUnit::K::Table ||
          (u.kind == FlowUnit::K::Image && u.floatSide != 0)) {
        // whole unit atomic (table incl. rules; float box incl. caption)
        while (sameUnit(j)) j++;
      } else if (u.kind == FlowUnit::K::Code) {
        // one logical code line: its wrapped rows + zipped sidecar rows
        u32 row = l.special == 2 ? l.codeLine : (u32)l.cellIdx;
        while (sameUnit(j)) {
          const LineBox& n = fr.lines[j];
          u32 nrow = n.special == 2 ? n.codeLine : (u32)n.cellIdx;
          if (nrow != row) break;
          j++;
        }
      }
      for (size_t k = i; k < j; k++) {
        i64 t = lineTop(fr.lines[k]);
        i64 bo = t + fr.lines[k].height;
        if (t < b.top) b.top = t;
        if (bo > b.bot) b.bot = bo;
      }
      b.hi = (u32)j;
      if (u.kind == FlowUnit::K::Image && u.floatSide == 0)
        b.stickAfter = true;  // block image keeps its caption
      if (tb.node && tb.node->kind == Kind::heading && j >= fr.lines.size())
        b.stickAfter = true;  // heading sticks to the next block
      if (u.kind == FlowUnit::K::Text && l.special == 0 && l.cellIdx < 0) {
        b.count = unitLines[l.unitIdx];
        b.ordinal = unitSeen[l.unitIdx]++;
      }
      bands.push_back(b);
      i = j;
    }
  }
  std::stable_sort(bands.begin(), bands.end(),
                   [](const Band& a, const Band& b) { return a.top < b.top; });

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
    if (b.count > 0 && b.ordinal > 0 &&
        (b.ordinal < 2 || b.count - b.ordinal < 2))
      return true;  // orphan / widow
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

  std::string out;
  out += "<div class=\"tsr-doc tsr-paged\">\n";
  for (size_t pg = 0; pg < starts.size(); pg++) {
    size_t lo = starts[pg];
    size_t hi = pg + 1 < starts.size() ? starts[pg + 1] : bands.size();
    out += "<div class=\"tsr-sheet\" style=\"position:relative;overflow:hidden;height:";
    fmtPx(out, suToPx(H));
    out += "\">\n";
    u32 lastAnchored = 0xFFFFFFFFu;
    for (size_t bi = lo; bi < hi; bi++) {
      const Band& b = bands[bi];
      const ParaFrame& fr = lr.paras[b.para];
      const TopBlock& tb = tops[b.para];
      Su shift = (Su)((i64)fr.y - tops0[pg]);
      for (u32 li = b.lo; li < b.hi; li++)
        renderLineBox(out, tb, fr, li, styles, strs, cfg, lastAnchored, shift);
    }
    out += "</div>\n";
  }
  out += "</div>\n";
  return out;
}

}  // namespace tsr
