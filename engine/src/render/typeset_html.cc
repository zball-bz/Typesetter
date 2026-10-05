#include "typeset_html.h"

#include <algorithm>
#include <cstring>

#include "../math/mathfont.h"
#include "html_writer.h"
#include "style_css.gen.h"
#include "../shape/textrules.h"

namespace tsr {

// class list of a run ("tsr-r tsr-b …"), built without allocating
struct RunClasses {
  char buf[96];
  size_t n = 0;
  explicit RunClasses(const Styling& st, const char* before = nullptr, const char* after = nullptr) {
    if (before) add(before);
    add("tsr-r");
    if (st.bits & CLS_BOLD) add("tsr-b");
    if (st.bits & CLS_EM) add("tsr-i");
    if (st.bits & CLS_CJK) add("tsr-cjk");
    if (st.bits & CLS_CODE) add("tsr-code");
    if (st.bits & CLS_SUP) add("tsr-sup");
    if (after && *after) add(after);
  }
  void add(std::string_view s) {
    if (n) buf[n++] = ' ';
    std::memcpy(buf + n, s.data(), s.size());
    n += s.size();
  }
  std::string_view sv() const { return {buf, n}; }
};

// data-s/data-e of a source span, relative to the paragraph base
static void spanAttrs(Tag& t, Span sp, u32 base) {
  t.num("data-s", sp.start - base);
  t.num("data-e", sp.end - base);
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
      t.px("left", suToPx(x)).px("top", suToPx(base) - fA).px("font-size", px).px("line-height", fH);
      t.open();
      escapeHtml(out, strs.get(b->text));
      out += "</span>";
      return;
    }
    case MathKind::Rule: {
      Tag t(out, "span");
      t.attrSafe("class", "tsr-mr");
      t.px("left", suToPx(x)).px("top", suToPx(base - b->asc));
      t.px("width", suToPx(b->w)).px("height", suToPx(b->asc + b->desc));
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
                     double displayTop = 0, u32 srcBase = 0) {
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
  if (!span.empty()) spanAttrs(t, span, srcBase);
  t.px("width", suToPx(mb->w)).px("height", suToPx(mb->asc + mb->desc));
  if (display) t.decl("position", "absolute").decl("left", "0").px("top", displayTop);
  else t.px("vertical-align", -suToPx(mb->desc));
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
  auto pos3 = [&](Tag& t) -> Tag& {
    return t.px("top", suToPx(ly)).px("left", suToPx(l.left)).px("width", suToPx(l.width));
  };
  auto anchorOf = [&](const FlowUnit& u, Tag& t) {  // first line of an anchored unit
    if (u.anchor && l.unitIdx != lastAnchored) {
      lastAnchored = l.unitIdx;
      t.id(strs.get(u.anchor));
    }
  };
  auto lineSpan = [&](Tag& t) {
    if (!l.srcSpan.empty()) spanAttrs(t, l.srcSpan, srcBase);
  };
  if (l.special == 3) {  // raw passthrough (trusted, handler-declared)
    const FlowUnit& ru = tb.units[l.unitIdx];
    Tag t(out, "div");
    t.attrSafe("class", "tsr-raw");
    pos3(t).px("height", ru.rawHpx);
    t.open();
    out += strs.get(ru.rawHtml);  // the ONE unescaped path (§9)
    out += "</div>\n";
    return;
  }
  if (l.special == 1) {
    Tag t(out, "div");
    t.attrSafe("class", "tsr-rule");
    pos3(t);
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
      pos3(t).px("height", suToPx(advH));
      t.open();
    }
    if (mu.eqTag) {
      // right-margin equation number, at the measure's right edge
      Su lineRight = l.left + l.width;
      Su measureR = suFloorPx(cfg.widthPx);
      Tag t(out, "span");
      t.attrSafe("class", "tsr-eqno");
      t.attrSafe("data-syn", "eqno");
      t.px("right", -suToPx(measureR - lineRight));
      t.open();
      escapeHtml(out, strs.get(mu.eqTag));
      out += "</span>";
    }
    StrRef srcRef = mu.src ? attrStr(mu.src, ArgK::src) : 0;
    Su boxH = mu.mathBox->asc + mu.mathBox->desc;
    Su adv = suRoundPx(cfg.lineHeight * cfg.baseSizePx);
    mathSpan(out, mu.mathBox, srcRef, /*display=*/true, strs, {},
             boxH < adv ? suToPx(adv - boxH) / 2.0 : 0.0);
    out += "</div>\n";
    return;
  }
  if (l.special == 5) {  // figure image / placeholder (figure-design §5)
    const FlowUnit& iu = tb.units[l.unitIdx];
    if (iu.imgSrc) {
      Tag t(out, "img");
      t.attrSafe("class", "tsr-img");
      t.attrSafe("draggable", "false");
      t.attrSafe("data-syn", "image");
      anchorOf(iu, t);
      lineSpan(t);
      t.attr("src", strs.get(iu.imgSrc));
      t.attr("alt", iu.imgAlt ? strs.get(iu.imgAlt) : std::string_view{});
      pos3(t).px("height", suToPx(l.height));
      t.open();
      out += "\n";
    } else {
      Tag t(out, "div");
      t.attrSafe("class", "tsr-imgph");
      t.attrSafe("data-syn", "image");
      anchorOf(iu, t);
      lineSpan(t);
      pos3(t).px("height", suToPx(l.height));
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
      if (li + 1 < fr.lines.size() && fr.lines[li + 1].special == 2 && fr.lines[li + 1].codeCont) {
        t.attrSafe("data-join", "none");
        join = nullptr;
      }
    }
    anchorOf(tb.units[l.unitIdx], t);  // label anchor: first line of the unit
    lineSpan(t);
    if (join) t.attrSafe("data-join", join);
    if (!ragged && (tb.units[l.unitIdx].ragged || l.noGlue)) t.attrSafe("data-ragged", "1");
    if (l.cellIdx >= 0) t.attrSafe("data-cell", "1");
    if (l.overfull) t.attrSafe("data-overfull", "1");  // deliberate overflow (audit)
    pos3(t);
    if (l.special == 2) {
      const std::string* feats = &cfg.codeFontFeatures;
      if (tb.units[l.unitIdx].codeLang) {
        auto it = cfg.codeFontFeaturesByLang.find(
            std::string(strs.get(tb.units[l.unitIdx].codeLang)));
        if (it != cfg.codeFontFeaturesByLang.end()) feats = &it->second;
      }
      if (!feats->empty()) t.declEsc("font-feature-settings", *feats);
      if (l.height > 0) {
        // baseline sits centred in the row (the hl background made the
        // top-stuck default line box visible); hl rows also paint height
        t.px("line-height", suToPx(l.height));
        if (l.codeHl) t.px("height", suToPx(l.height));
      }
    }
    if (l.special == 0 && l.wordDeltaPx != 0) t.px("word-spacing", l.wordDeltaPx);
    t.open();
  }

  if (l.marker) {
    const Styling& mst = styles.get(l.markerStyle);
    Tag t(out, "span");
    t.attrSafe("class", RunClasses(mst, "tsr-marker").sv());
    t.attrSafe("data-syn", "marker");
    runCss(t, mst, cfg, strs);
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
      t.attrSafe("class", RunClasses(cst).sv());
      t.attrSafe("data-syn", "cont");
      runCss(t, cst, cfg, strs);
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
          cjk = isWide(utf8Next(seg, probe));
          while (s1 < seg.size()) {
            u32 nx = s1;
            if (isWide(utf8Next(seg, nx)) != cjk) break;
            s1 = nx;
          }
        } else {
          s1 = (u32)seg.size();
        }
        const Styling& cst = styles.get(r.style);
        Tag t(out, "span");
        t.attrSafe("class", RunClasses(cst).sv());
        runCss(t, cst, cfg, strs);
        double d = snap ? (cjk ? l.snapCjkPx : l.snapLatinPx) : 0;
        if (d > 0) {
          // ONE style attribute: the letter-spacing joins the run's own
          // declarations (defect #21: a second style="" was dropped)
          t.attrSafe("data-snap", "1");
          t.px("letter-spacing", d);
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
  const HList& h = l.cellIdx >= 0 ? u.cells[(size_t)l.cellIdx].hl : u.hl;
  const std::vector<HItem>& v = h.items;
  // paint reads the items and their run instances (plan P1-12): a DOM run
  // opens where the run changes; a punctuation glyph, a pinned box and a
  // formula are runs of their own; spacer glue and the indent paint as
  // spacers, blanks fold into their glyph's squeeze
  auto run = [&](const HItem& it) -> const RunRec& { return h.runs[it.run]; };
  auto cold = [&](const HItem& it) -> const ColdRec& { return h.cold[it.cold]; };
  // the carrier next to i on this line (penalties skipped), or -1
  auto nextOnLine = [&](u32 i) -> i64 {
    for (u32 k = i + 1; k < l.itemEnd; k++)
      if (v[k].k != IK::Penalty) return k;
    return -1;
  };
  auto prevOnLine = [&](u32 i) -> i64 {
    for (u32 k = i; k-- > l.itemBegin;)
      if (v[k].k != IK::Penalty) return k;
    return -1;
  };
  auto isGap = [&](i64 k) { return k >= 0 && v[k].k == IK::Glue && v[k].cls == (u8)GC::InterChar; };
  // opens a run at its first carrier; `extraStyle(t)` adds declarations
  // after the run's own, and `syn` (e.g. "hyphen") follows the style;
  // returns whether it is a link
  auto openRun = [&](const HItem& first, const char* extraCls, const char* syn, auto&& extraStyle) {
    const RunRec& r = run(first);
    const Styling& sty = styles.get(r.face);
    const bool isLink = r.link != 0;
    Tag t(out, isLink ? "a" : "span");
    t.attrSafe("class", RunClasses(sty, nullptr, extraCls).sv());
    if (isLink) t.attr("href", strs.get(r.link));
    if (first.attrs & IA_Anchor) t.id(strs.get(cold(first).anchor));  // inline anchor (footnote marker)
    if (r.syn == SynKind::Ref) {  // §9.3: copy skips (a ref's hyphen is "hyphen")
      if (!syn) t.attrSafe("data-syn", "ref");
    } else if (cold(first).srcEnd > cold(first).srcStart) {
      t.num("data-s", cold(first).srcStart - srcBase);
    }
    runCss(t, sty, cfg, strs);
    extraStyle(t);
    if (syn) t.attrSafe("data-syn", syn);
    t.open();
    return isLink;
  };
  auto noStyle = [](Tag&) {};
  u32 i = l.itemBegin;
  while (i < l.itemEnd) {
    const HItem& it = v[i];
    if (it.k == IK::Penalty) {
      i++;
      continue;
    }
    const RunRec& r = run(it);
    if (it.k == IK::Box && r.rc == RealizeClass::Object) {  // inline formula: one box, baseline via vertical-align
      const AdvanceSpec& sp = h.specs[it.aux];
      mathSpan(out, h.objs[sp.obj].math, sp.str, /*display=*/false, strs,
               Span{cold(it).srcStart, cold(it).srcEnd}, 0, srcBase);
      i++;
      continue;
    }
    // final hyphen glyph: inside its word's link (it used to close the link
    // and add an unlinked '-')
    if (it.k == IK::Disc) {
      if (i == l.itemEnd - 1 && l.endsWithHyphen) {
        bool link = openRun(it, nullptr, "hyphen", noStyle);
        out += "-";
        out += link ? "</a>" : "</span>";
      }
      i++;
      continue;
    }
    const bool indentBox = it.k == IK::Box && r.syn == SynKind::Indent;
    const bool spacer = it.k == IK::Glue && (it.cls == (u8)GC::Autospace || it.cls == (u8)GC::ObjectSpace);
    if (indentBox || spacer) {
      double w = cold(it).rawPx;
      if (spacer) w += l.wordDeltaPx * (double)it.x;
      Tag t(out, "span");
      t.attrSafe("class", "tsr-sp");
      t.attrSafe("data-syn", indentBox ? "indent" : "boundary");
      t.px("width", w);
      t.open();
      out += "</span>";
      i++;
      continue;
    }
    if (it.k == IK::Glue && (it.cls == (u8)GC::Blank || it.cls == (u8)GC::InterChar)) {
      i++;  // a blank: absorbed by its glyph's advance; a gap: the run's letter-spacing
      continue;
    }
    if (it.k == IK::Box && r.rc == RealizeClass::BlankBearing) {
      // the glyph's own blank (its half em) on this line, else squeezed away
      const bool open = kCCFlags[it.cls] & kCC_open;
      const i64 k = open ? prevOnLine(i) : nextOnLine(i);
      const bool halfPresent = k >= 0 && v[k].k == IK::Glue && v[k].cls == (u8)GC::Blank &&
                               ((v[k].attrs & IA_OwnedByNext) != 0) == open;
      const char* squeeze = halfPresent ? nullptr : (open ? "tsr-sqL" : "tsr-sqR");
      bool link = openRun(it, squeeze, nullptr, noStyle);
      escapeHtml(out, strs.get(h.specs[it.aux].str));
      out += link ? "</a>" : "</span>";
      i++;
      continue;
    }
    // —/…… defined-width box (1em single, 2em pair — App C): the engine
    // ASSUMES the defined advance instead of measuring (canvas cannot
    // predict DOM's full-width-ization of these), and the DOM — under
    // text-spacing-trim — shapes them to exactly that advance with
    // connected ink. Own span so no letter-spacing splits the pair; its
    // stretch gap becomes a margin.
    if (it.k == IK::Box && r.rc == RealizeClass::Pinned) {
      // the assumed advance is ENFORCED as an inline-block width: Blink
      // full-width-izes CJK dashes to exactly this budget, Gecko does not
      // (Noto's em dash is ~0.89em) and would leave every line with a ——
      // visibly short of the measure. No overflow:hidden — that would move
      // the baseline to the box bottom.
      const bool keep = l.cjkDeltaPx != 0 && isGap(nextOnLine(i));
      const double wPx = cold(it).rawPx;
      bool link = openRun(it, nullptr, nullptr, [&](Tag& t) {
        t.decl("display", "inline-block").decl("text-align", "center").px("width", wPx);
        if (keep) t.px("margin-right", l.cjkDeltaPx);
      });
      escapeHtml(out, strs.get(h.specs[it.aux].str));
      out += link ? "</a>" : "</span>";
      i++;
      continue;
    }
    // the rest of this run on this line (a line-final hyphen point ends it:
    // the hyphen glyph is a run of its own)
    u32 j = i;
    while (j < l.itemEnd && v[j].run == it.run && !(v[j].k == IK::Disc && j + 1 == l.itemEnd)) j++;
    if (r.rc == RealizeClass::LetterSpaced) {
      // a CJK run: letter-spacing realizes its InterChar gaps; the last one
      // is real only when the run's last char has a gap after it on this
      // line (the next char, or a closing glyph)
      i64 last = -1;
      for (u32 k = i; k < j; k++)
        if (v[k].k != IK::Penalty) last = k;
      const bool gapFollows = isGap(last);
      bool link = openRun(it, nullptr, nullptr, [&](Tag& t) {
        if (l.cjkDeltaPx == 0) return;
        t.px("letter-spacing", l.cjkDeltaPx);
        // negate via the value, never by prepending '-': a negative delta
        // would otherwise render "--Npx" (invalid, dropped)
        if (!gapFollows) t.px("margin-right", -l.cjkDeltaPx);
      });
      for (u32 k = i; k < j; k++)
        if (v[k].k == IK::Box) escapeHtml(out, strs.get(h.specs[v[k].aux].str));
      out += link ? "</a>" : "</span>";
      i = j;
      continue;
    }
    // a Latin run: words and spaces. Mid-line hyphen points paint nothing,
    // so the pieces sit in ONE text node and the browser kerns across the
    // junction — the Disc's unbroken width modelled it
    bool link = openRun(it, nullptr, nullptr, noStyle);
    for (u32 k = i; k < j; k++) {
      const HItem& x = v[k];
      if (x.k == IK::Box) escapeHtml(out, strs.get(h.specs[x.aux].str));
      else if (x.k == IK::Glue) out += ' ';
    }
    out += link ? "</a>" : "</span>";
    i = j;
  }
  out += "</div>\n";
}

// The root render contract (plan P1-04): the document root carries its
// language, base size and every font role resolved engine-side — the same
// families the faces measured — so measurement and paint never diverge and
// the HTML needs no host CSS for its fonts.
static void rootTag(std::string& out, const char* cls, const Config& cfg) {
  Tag t(out, "div");
  t.attrSafe("class", cls);
  t.attr("lang", cfg.lang);
  t.declEsc("--tsr-font-body", familyFor(cfg, false, Script::Latin));
  t.declEsc("--tsr-font-cjk", familyFor(cfg, false, Script::Cjk));
  t.declEsc("--tsr-font-mono", familyFor(cfg, true, Script::Latin));
  t.declEsc("--tsr-font-mono-cjk", familyFor(cfg, true, Script::Cjk));
  t.px("font-size", cfg.baseSizePx);
  t.open();
  out += "\n";
}

std::string renderTypeset(const std::vector<TopBlock>& tops, const LayoutResult& lr,
                          const StyleTable& styles, const Interner& strs,
                          const Config& cfg) {
  std::string out;
  rootTag(out, "tsr-doc", cfg);
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
      t.decl("position", "relative").px("height", suToPx(fr.h));
      if (p + 1 < lr.paras.size()) t.px("margin-bottom", cfg.paraSpacingEm * cfg.baseSizePx);
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
  rootTag(out, "tsr-doc tsr-paged", cfg);
  for (size_t pg = 0; pg < starts.size(); pg++) {
    size_t lo = starts[pg];
    size_t hi = pg + 1 < starts.size() ? starts[pg + 1] : bands.size();
    {
      Tag t(out, "div");
      t.attrSafe("class", "tsr-sheet");
      t.decl("position", "relative").decl("overflow", "hidden").px("height", suToPx(H));
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
