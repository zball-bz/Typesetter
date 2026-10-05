#include "typeset_html.h"

#include <algorithm>

#include "../math/mathfont.h"
#include "html_writer.h"

namespace tsr {

static std::string runClasses(const Styling& st) {
  std::string out = "tsr-r";
  if (st.bits & CLS_BOLD) out += " tsr-b";
  if (st.bits & CLS_EM) out += " tsr-i";
  if (st.bits & CLS_CJK) out += " tsr-cjk";
  if (st.bits & CLS_CODE) out += " tsr-code";
  if (st.bits & CLS_SUP) out += " tsr-sup";
  return out;
}

// Inline-style overrides a run carries beyond its classes (document-model
// §3): explicit font-family wins over the .tsr-cjk var rule by specificity.
// Values were validated at decode (P0-06); text values are escaped here.
static std::string runStyle(const Styling& st, const Config& cfg, const Interner& strs) {
  std::string style;
  if (st.sizeMul != 1.0f || st.sizePx > 0) {
    style += "font-size:";
    fmtPx(style, emPx(cfg, st));
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
  return style;
}

// lang + style of a run, in that order (today's attribute order)
static void runAttrs(Tag& t, const Styling& st, const Config& cfg, const Interner& strs) {
  if (st.lang) t.attr("lang", strs.get(st.lang));
  t.style(runStyle(st, cfg, strs));
}

static std::string spanAttr(Span sp, u32 base) { return std::to_string(sp.start - base); }

// Positioned glyph runs inside one formula (math-design.md §8): flatten the
// MathBox tree to absolute (x, baseline) leaves. A glyph span pins its text
// baseline by explicit line-height == the font's hhea height: the baseline
// then sits exactly kAscender·px/upem below the span top.
static void mathLeaves(std::string& out, const MathBox* b, const Interner& strs,
                       Su x, Su base) {
  switch (b->kind) {
    case MathKind::Glyph: {
      const double px = (double)b->px;
      // text-font run (names/operators): the box carries the body font's
      // ascent/descent from the host measurer; the span's line box equals
      // the content area so the baseline lands exactly at `base`
      const double fA = b->textFont ? suToPx(b->asc)
                                    : (double)mathfont::kAscender * px / mathfont::kUpem;
      const double fH = b->textFont
          ? suToPx(b->asc + b->desc)
          : (double)(mathfont::kAscender + mathfont::kDescender) * px / mathfont::kUpem;
      Tag t(out, "span");
      t.attrSafe("class", b->textFont ? "tsr-mg tsr-mt" : "tsr-mg");
      t.style("left:" + pxStr(suToPx(x)) + ";top:" + pxStr(suToPx(base) - fA) +
              ";font-size:" + pxStr(px) + ";line-height:" + pxStr(fH));
      t.open();
      escapeHtml(out, strs.get(b->text));
      out += "</span>";
      return;
    }
    case MathKind::Rule: {
      Tag t(out, "span");
      t.attrSafe("class", "tsr-mr");
      t.style("left:" + pxStr(suToPx(x)) + ";top:" + pxStr(suToPx(base - b->asc)) +
              ";width:" + pxStr(suToPx(b->w)) + ";height:" + pxStr(suToPx(b->asc + b->desc)));
      t.open();
      out += "</span>";
      return;
    }
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
                     const std::string& posStyle, u32 srcBase = 0) {
  Tag t(out, "span");
  t.attrSafe("class", "tsr-math");
  t.attrSafe("data-syn", "math");
  std::string src;
  if (srcRef) {  // later segments of a split formula contribute nothing
    src = display ? "$ " : "$";
    src += strs.get(srcRef);
    src += display ? " $" : "$";
  }
  t.attr("data-src", src);
  if (!span.empty()) {
    t.attrSafe("data-s", spanAttr(span, srcBase));
    t.attrSafe("data-e", std::to_string(span.end - srcBase));
  }
  std::string style = "width:" + pxStr(suToPx(mb->w)) + ";height:" + pxStr(suToPx(mb->asc + mb->desc));
  if (!posStyle.empty()) style += ";" + posStyle;
  else style += ";vertical-align:" + pxStr(-suToPx(mb->desc));
  t.style(style);
  t.open();
  mathLeaves(out, mb, strs, 0, mb->asc);
  out += "</span>";
}

// One line box (any kind) at an optional vertical rebase — shared by the
// flowing serializer (yShift 0, lines inside their para frame) and the
// paged serializer (yShift rebases into the sheet).
static void renderLineBox(std::string& out, const TopBlock& tb, const ParaFrame& fr,
                          size_t li, const StyleTable& styles, const Interner& strs,
                          const Config& cfg, u32& lastAnchored, Su yShift,
                          u32 srcBase = 0) {
  const LineBox& l = fr.lines[li];
  const Su ly = (Su)(l.y + yShift);
  const std::string pos3 = "top:" + pxStr(suToPx(ly)) + ";left:" + pxStr(suToPx(l.left)) +
                           ";width:" + pxStr(suToPx(l.width));
  auto anchorOf = [&](const FlowUnit& u, Tag& t) {  // first line of an anchored unit
    if (u.anchor && l.unitIdx != lastAnchored) {
      lastAnchored = l.unitIdx;
      t.id(strs.get(u.anchor));
    }
  };
  auto lineSpan = [&](Tag& t) {
    if (!l.srcSpan.empty()) {
      t.attrSafe("data-s", spanAttr(l.srcSpan, srcBase));
      t.attrSafe("data-e", std::to_string(l.srcSpan.end - srcBase));
    }
  };
      if (l.special == 3) {  // raw passthrough (trusted, handler-declared)
        const FlowUnit& ru = tb.units[l.unitIdx];
        Tag t(out, "div");
        t.attrSafe("class", "tsr-raw");
        t.style(pos3 + ";height:" + pxStr(ru.rawHpx));
        t.open();
        out += strs.get(ru.rawHtml);  // the ONE unescaped path (§9)
        out += "</div>\n";
        return;
      }
      if (l.special == 1) {
        Tag t(out, "div");
        t.attrSafe("class", "tsr-rule");
        t.style(pos3);
        t.open();
        out += "</div>\n";
        return;
      }
      if (l.special == 4) {  // display math (§8): centred block formula
        const FlowUnit& mu = tb.units[l.unitIdx];
        Su boxHh = mu.mathBox->asc + mu.mathBox->desc;
        Su advH = suRoundPx(cfg.lineHeight * cfg.baseSizePx);
        if (boxHh > advH) advH = boxHh;
        {
          Tag t(out, "div");
          t.attrSafe("class", "tsr-line");
          anchorOf(mu, t);
          lineSpan(t);
          t.attrSafe("data-ragged", "1");
          t.style(pos3 + ";height:" + pxStr(suToPx(advH)));
          t.open();
        }
        if (mu.eqTag) {
          // right-margin equation number, at the measure's right edge
          Su lineRight = l.left + l.width;
          Su measureR = suFloorPx(cfg.widthPx);
          Tag t(out, "span");
          t.attrSafe("class", "tsr-eqno");
          t.attrSafe("data-syn", "eqno");
          t.style("right:" + pxStr(-suToPx(measureR - lineRight)));
          t.open();
          escapeHtml(out, strs.get(mu.eqTag));
          out += "</span>";
        }
        StrRef srcRef = mu.src ? attrStr(mu.src, ArgK::src) : 0;
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
        std::string pos = pos3 + ";height:" + pxStr(suToPx(l.height));
        if (iu.imgSrc) {
          Tag t(out, "img");
          t.attrSafe("class", "tsr-img");
          t.attrSafe("draggable", "false");
          t.attrSafe("data-syn", "image");
          anchorOf(iu, t);
          lineSpan(t);
          t.attr("src", strs.get(iu.imgSrc));
          t.attr("alt", iu.imgAlt ? strs.get(iu.imgAlt) : std::string_view{});
          t.style(pos);
          t.open();
          out += "\n";
        } else {
          Tag t(out, "div");
          t.attrSafe("class", "tsr-imgph");
          t.attrSafe("data-syn", "image");
          anchorOf(iu, t);
          lineSpan(t);
          t.style(pos);
          t.open();
          if (iu.imgAlt) escapeHtml(out, strs.get(iu.imgAlt));
          out += "</div>\n";
        }
        return;
      }
      {
        Tag t(out, "div");
        t.attrSafe("class", l.special == 2 && l.codeHl ? "tsr-line tsr-hlline" : "tsr-line");
        bool ragged = false;
        const char* join = l.join == 1 ? "space" : l.join == 2 ? "none" : nullptr;
        if (l.special == 2) {
          // code rows are ragged by nature (the audit's justify checks do not
          // apply); a wrapped row additionally rejoins its continuation (§9.3)
          t.attrSafe("data-ragged", "1");
          ragged = true;
          size_t self = li;
          if (self + 1 < fr.lines.size() && fr.lines[self + 1].special == 2 &&
              fr.lines[self + 1].codeCont) {
            t.attrSafe("data-join", "none");
            join = nullptr;
          }
        }
        anchorOf(tb.units[l.unitIdx], t);  // label anchor: first line of the unit
        lineSpan(t);
        if (join) t.attrSafe("data-join", join);
        if (!ragged && (tb.units[l.unitIdx].ragged || l.noGlue)) t.attrSafe("data-ragged", "1");
        if (l.cellIdx >= 0) t.attrSafe("data-cell", "1");
        std::string style = pos3;
        if (l.special == 2) {
          const std::string* feats = &cfg.codeFontFeatures;
          if (tb.units[l.unitIdx].codeLang) {
            auto it = cfg.codeFontFeaturesByLang.find(
                std::string(strs.get(tb.units[l.unitIdx].codeLang)));
            if (it != cfg.codeFontFeaturesByLang.end()) feats = &it->second;
          }
          if (!feats->empty()) {
            style += ";font-feature-settings:";
            escapeHtml(style, *feats);
          }
        }
        if (l.special == 2 && l.height > 0) {
          // baseline sits centred in the row (the hl background made the
          // top-stuck default line box visible); hl rows also paint height
          style += ";line-height:" + pxStr(suToPx(l.height));
          if (l.codeHl) style += ";height:" + pxStr(suToPx(l.height));
        }
        if (l.special == 0 && l.wordDeltaPx != 0) style += ";word-spacing:" + pxStr(l.wordDeltaPx);
        t.style(style);
        t.open();
      }

      if (l.marker) {
        const Styling& mst = styles.get(l.markerStyle);
        Tag t(out, "span");
        t.attrSafe("class", "tsr-marker " + runClasses(mst));
        t.attrSafe("data-syn", "marker");
        runAttrs(t, mst, cfg, strs);
        t.open();
        escapeHtml(out, strs.get(l.marker));
        out += "</span>";
      }

      if (l.special == 2) {
        const FlowUnit& u = tb.units[l.unitIdx];
        if (l.codeCont && l.contCols > 0) {
          // grid-exact continuation indent: REAL spaces in the same mono
          // flow (exact ch in ANY font), synthetic for both copy paths
          const Styling& cst = styles.get(u.codeStyle);
          Tag t(out, "span");
          t.attrSafe("class", runClasses(cst));
          t.attrSafe("data-syn", "cont");
          runAttrs(t, cst, cfg, strs);
          t.open();
          out.append(l.contCols, ' ');
          out += "</span>";
        }
        const bool snap = l.snapLatinPx > 0 || l.snapCjkPx > 0;
        u32 off = 0;
        for (const FlowUnit::CodeRun& r : u.codeRuns[l.codeLine]) {
          std::string_view t0 = strs.get(r.text);
          u32 rLo = off, rHi = off + (u32)t0.size();
          off = rHi;
          u32 lo = l.cbLo > rLo ? l.cbLo : rLo;
          u32 hi = l.cbHi < rHi ? l.cbHi : rHi;
          if (lo >= hi) continue;
          std::string_view seg = t0.substr(lo - rLo, hi - lo);
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
            Tag t(out, "span");
            t.attrSafe("class", runClasses(cst));
            runAttrs(t, cst, cfg, strs);
            double d = snap ? (cjk ? l.snapCjkPx : l.snapLatinPx) : 0;
            if (d > 0) {
              // ONE style attribute: the letter-spacing joins the run's
              // own declarations (defect #21: a second style="" was dropped)
              t.attrSafe("data-snap", "1");
              t.style("letter-spacing:" + pxStr(d));
            }
            t.open();
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
      // opens a run; `syn` (e.g. "hyphen") is added after the style, as
      // today; returns whether the run is a link
      auto openRun = [&](const Styling& sty, StrRef url, const LinebreakBlock& first,
                         const std::string& extraStyle, const char* extraCls,
                         const char* syn = nullptr) {
        const bool isLink = url != 0;
        Tag t(out, isLink ? "a" : "span");
        std::string cls = runClasses(sty);
        if (extraCls && *extraCls) { cls += " "; cls += extraCls; }
        t.attrSafe("class", cls);
        if (isLink) t.attr("href", strs.get(url));
        if (first.anchorId) t.id(strs.get(first.anchorId));  // inline anchor (footnote marker)
        if (first.flags & BF_REF) t.attrSafe("data-syn", "ref");  // §9.3: copy skips
        else if (!first.span.empty()) t.attrSafe("data-s", spanAttr(first.span, srcBase));
        runAttrs(t, sty, cfg, strs);
        t.style(extraStyle);
        if (syn) t.attrSafe("data-syn", syn);
        t.open();
        return isLink;
      };
      auto sameRun = [&](const LinebreakBlock& a, const LinebreakBlock& b) {
        return a.style == b.style && a.linkUrl == b.linkUrl;
      };
      while (i < l.blockEnd) {
        const LinebreakBlock& b = bl[i];
        if (b.math) {  // inline formula: one box, baseline via vertical-align
          mathSpan(out, b.math, b.text, /*display=*/false, strs, b.span,
                   std::string(), srcBase);
          i++;
          continue;
        }
        if (b.isHyphen()) {
          if (i == l.blockEnd - 1 && l.endsWithHyphen) {
            bool link = openRun(styles.get(b.style), 0, b, "", nullptr, "hyphen");
            out += "-";
            out += link ? "</a>" : "</span>";
          }
          i++;
          continue;
        }
        if (b.flags & (BF_INDENT | BF_BOUND)) {
          double w = b.rawPx;
          if (b.flags & BF_BOUND) w += l.wordDeltaPx * (double)b.stretchWeight;
          Tag t(out, "span");
          t.attrSafe("class", "tsr-sp");
          t.attrSafe("data-syn", (b.flags & BF_INDENT) ? "indent" : "boundary");
          t.style("width:" + pxStr(w));
          t.open();
          out += "</span>";
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
          u32 j = i;
          while (j < l.blockEnd && bl[j].isCjkChar() && !(bl[j].flags & BF_PAIR) &&
                 sameRun(bl[j], b))
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
          bool link = openRun(styles.get(b.style), b.linkUrl, b, extra, nullptr);
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
        u32 j = i;
        while (j < l.blockEnd && sameRun(bl[j], b) &&
               (!bl[j].isHyphen() || j + 1 < l.blockEnd) &&
               !bl[j].isCjkChar() && !bl[j].math &&
               !bl[j].isPunctGlyph() && !(bl[j].flags & (BF_INDENT | BF_BOUND)) &&
               !(bl[j].flags & BF_PUNCT_SP))
          j++;
        bool link = openRun(styles.get(b.style), b.linkUrl, b, "", nullptr);
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
    // Source anchors are PARA-RELATIVE (base in data-s0 on the container):
    // an edit then leaves every untouched paragraph byte-identical in
    // serialized form, so the editing loop patches exactly the damaged
    // paragraphs (shell.mjs patchIn). Paged output keeps absolute offsets.
    u32 srcBase = 0xFFFFFFFFu;
    for (const LineBox& l : fr.lines)
      if (!l.srcSpan.empty() && l.srcSpan.start < srcBase) srcBase = l.srcSpan.start;
    if (srcBase == 0xFFFFFFFFu) srcBase = 0;
    {
      Tag t(out, "div");
      t.attrSafe("class", "tsr-para");
      t.num("data-pid", fr.pid);
      t.num("data-s0", srcBase);
      std::string style = "position:relative;height:" + pxStr(suToPx(fr.h));
      if (p + 1 < lr.paras.size()) style += ";margin-bottom:" + pxStr(cfg.paraSpacingEm * cfg.baseSizePx);
      t.style(style);
      t.open();
      out += "\n";
    }
    for (size_t li = 0; li < fr.lines.size(); li++)
      renderLineBox(out, tb, fr, li, styles, strs, cfg, lastAnchored, 0, srcBase);
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
    {
      Tag t(out, "div");
      t.attrSafe("class", "tsr-sheet");
      t.style("position:relative;overflow:hidden;height:" + pxStr(suToPx(H)));
      t.open();
      out += "\n";
    }
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
