#include "typeset_html.h"

#include <cstring>

#include "../math/font.h"
#include "../math/math.h"
#include "html_writer.h"
#include "style_css.gen.h"

namespace tsr {

// class list of a run ("tsr-r tsr-b …"), built without allocating
struct RunClasses {
  char buf[96];
  size_t n = 0;
  // the classes keep their names from the class bits (plan P2-08): bold for
  // weight 700, italic, the CJK script, the mono role, a superscript
  RunClasses(const Styling& st, const Interner& strs, const char* before = nullptr, const char* after = nullptr) {
    if (before) add(before);
    add("tsr-r");
    if (st.weight == 700) add("tsr-b");
    if (st.italic) add("tsr-i");
    if (st.script == SCRIPT_CJK) add("tsr-cjk");
    if (st.fontRole == FONTROLE_MONO) add("tsr-code");
    if (st.baseline == BASELINE_SUPER) add("tsr-sup");
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
// a link run's href (plan P3-04): an anchor through the AnchorNamer, else
// the URL as written
static std::string hrefOf(const LinkTarget& l, const Interner& strs) {
  return l.anchor ? AnchorNamer::href(strs.get(l.ref)) : std::string(strs.get(l.ref));
}

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
      // the content area so the baseline lands exactly at `base`. A math
      // font's glyph is pinned by its hhea line box (the registry's)
      const bool text = b->font == kTextFont;
      const MathFont* mf = text ? nullptr : MathFontRegistry::get().byId(b->font);
      if (!text && !mf) mf = &MathFontRegistry::get().primary();
      const double fA = text ? suToPx(b->asc) : (double)mf->hheaAsc * px / mf->upem;
      const double fH = text ? suToPx(b->asc + b->desc) : (double)(mf->hheaAsc + mf->hheaDesc) * px / mf->upem;
      Tag t(out, "span");
      t.attrSafe("class", text ? "tsr-mg tsr-mt" : "tsr-mg");
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
                     double displayTop = 0, u32 srcBase = 0, StrRef color = 0) {
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
  if (color) t.declEsc("color", strs.get(color));  // the formula's paint style (plan P1-25)
  t.open();
  mathLeaves(out, mb, strs, 0, mb->asc);
  out += "</span>";
}

// One painted node at an optional vertical rebase — shared by the flowing
// writer (yShift 0, nodes inside their block's container, sources relative
// to the block) and the paged writer (yShift rebases into the sheet,
// sources absolute). Stateless: every attribute is the node's.
static void writeNode(std::string& out, const DLBlock& blk, const DLNode& n, Su yShift, u32 srcBase,
                      const StyleTable& styles, const Interner& strs, double basePx) {
  const Su ly = (Su)(n.y + yShift);
  auto pos3 = [&](Tag& t) -> Tag& {
    return t.px("top", suToPx(ly)).px("left", suToPx(n.left)).px("width", suToPx(n.width));
  };
  auto anchor = [&](Tag& t) {
    if (n.anchor) t.id(strs.get(n.anchor));
  };
  auto lineSpan = [&](Tag& t) {
    if (!n.span.empty() || n.spanned) spanAttrs(t, n.span, srcBase);
  };
  // an enclosing block's label that shares this node: an empty target
  auto anchor2 = [&] {
    if (!n.anchor2) return;
    Tag t(out, "span");
    t.id(strs.get(n.anchor2));
    t.attrSafe("data-syn", "anchor");
    t.open();
    out += "</span>";
  };
  switch (n.kind) {
    case FragKind::Raw: {  // raw passthrough (trusted, handler-declared)
      Tag t(out, "div");
      t.attrSafe("class", "tsr-raw");
      anchor(t);
      pos3(t).px("height", n.heightPx);
      t.open();
      anchor2();
      out += strs.get(n.src);  // the ONE unescaped path (§9)
      out += "</div>\n";
      return;
    }
    case FragKind::Rule: {
      Tag t(out, "div");
      t.attrSafe("class", "tsr-rule");
      anchor(t);
      pos3(t);
      t.open();
      anchor2();
      out += "</div>\n";
      return;
    }
    case FragKind::Frame: {  // (plan P3-14) a framed block's border and background, under it
      Tag t(out, "div");
      t.attrSafe("class", "tsr-frame");
      pos3(t).px("height", n.heightPx);
      if (n.box) {
        const BoxModel& b = *n.box;
        if (b.border[0] || b.border[1] || b.border[2] || b.border[3]) {
          std::string w;
          for (int side = 0; side < 4; side++) {
            char buf[48];
            if (side) w += ' ';
            w.append(buf, fmtPxBuf(buf, suToPx(b.border[side])));
          }
          t.decl("border-width", w);
          if (b.borderColor) t.decl("border-color", strs.get(b.borderColor));  // (a validated colour)
        }
        if (b.background) t.decl("background", strs.get(b.background));
      }
      t.attrSafe("data-syn", "frame");
      t.open();
      out += "</div>\n";
      return;
    }
    case FragKind::Math: {  // display math (§8): the formula placed in its row
      {
        Tag t(out, "div");
        t.attrSafe("class", "tsr-line");
        anchor(t);
        lineSpan(t);
        t.attrSafe("data-ragged", "1");
        pos3(t).px("height", n.heightPx);
        t.open();
      }
      anchor2();
      if (n.eqTag) {  // right-margin equation number, at the measure's right edge
        Tag t(out, "span");
        t.attrSafe("class", "tsr-eqno");
        t.attrSafe("data-syn", "eqno");
        t.px("right", n.eqRightPx);
        t.open();
        escapeHtml(out, strs.get(n.eqTag));
        out += "</span>";
      }
      mathSpan(out, n.math, n.mathSrc, /*display=*/true, strs, {}, n.mathTopPx, 0,
               n.markerStyle ? styles.get(n.markerStyle).color : 0);
      out += "</div>\n";
      return;
    }
    case FragKind::Image: {  // figure image / placeholder (figure-design §5)
      if (n.src) {
        anchor2();  // (an img holds no children)
        Tag t(out, "img");
        t.attrSafe("class", "tsr-img");
        t.attrSafe("draggable", "false");
        t.attrSafe("data-syn", "image");
        anchor(t);
        lineSpan(t);
        t.attr("src", strs.get(n.src));
        t.attr("alt", n.alt ? strs.get(n.alt) : std::string_view{});
        pos3(t).px("height", n.heightPx);
        t.open();
        out += "\n";
      } else {
        Tag t(out, "div");
        t.attrSafe("class", "tsr-imgph");
        t.attrSafe("data-syn", "image");
        anchor(t);
        lineSpan(t);
        pos3(t).px("height", n.heightPx);
        t.open();
        anchor2();
        if (n.alt) escapeHtml(out, strs.get(n.alt));
        out += "</div>\n";
      }
      return;
    }
    case FragKind::Line:
    case FragKind::CodeRow:
      break;
  }
  const bool code = n.kind == FragKind::CodeRow;
  {
    Tag t(out, "div");
    t.attrSafe("class", code && n.hl ? "tsr-line tsr-hlline" : "tsr-line");
    anchor(t);
    lineSpan(t);
    if (n.join) t.attrSafe("data-join", n.join);
    if (n.ragged) t.attrSafe("data-ragged", "1");  // (code rows are ragged by nature)
    if (n.track) t.attrSafe("data-track", n.track);
    if (n.overfull) t.attrSafe("data-overfull", "1");  // deliberate overflow (audit)
    pos3(t);
    if (code) {
      if (!n.features.empty()) t.declEsc("font-feature-settings", n.features);
      if (n.lineHeightPx > 0) t.px("line-height", n.lineHeightPx);
      if (n.heightPx > 0) t.px("height", n.heightPx);
    }
    if (!code && n.wordSpacingPx != 0) t.px("word-spacing", n.wordSpacingPx);
    t.open();
  }
  anchor2();
  if (n.marker) {
    const Styling& mst = styles.get(n.markerStyle);
    Tag t(out, "span");
    t.attrSafe("class", RunClasses(mst, strs, "tsr-marker").sv());
    t.attrSafe("data-syn", "marker");
    runCss(t, mst, basePx, strs);
    t.open();
    escapeHtml(out, strs.get(n.marker));
    out += "</span>";
  }
  for (u32 ri = n.runBegin; ri < n.runEnd; ri++) {
    const DLRun& d = blk.runs[ri];
    switch (d.k) {
      case DLRun::K::Math: {  // one box, baseline via vertical-align; in its run's link
        if (d.link) {
          Tag t(out, "a");
          t.attrSafe("class", "tsr-r");
          t.attr("href", hrefOf(d.link, strs));
          t.open();
        }
        mathSpan(out, d.math, d.src, /*display=*/false, strs, d.span, 0, srcBase, styles.get(d.face).color);
        if (d.link) out += "</a>";
        continue;
      }
      case DLRun::K::Image: {  // on the baseline; a dashed placeholder when unsized
        Tag t(out, d.src ? "img" : "span");
        t.attrSafe("class", d.src ? "tsr-iimg" : "tsr-iimg tsr-iimgph");
        t.attrSafe("data-syn", "image");
        if (!d.span.empty()) t.num("data-s", d.span.start - srcBase);
        if (d.src) {
          t.attrSafe("draggable", "false");
          t.attr("src", strs.get(d.src));
          t.attr("alt", d.alt ? strs.get(d.alt) : std::string_view{});
        }
        t.px("width", suToPx(d.w)).px("height", suToPx(d.h));
        t.open();
        if (!d.src) out += "</span>";
        continue;
      }
      case DLRun::K::Raw: {  // handler-declared markup in a box of its size
        Tag t(out, "span");
        t.attrSafe("class", "tsr-iraw");
        t.attrSafe("data-syn", "raw");
        if (!d.span.empty()) t.num("data-s", d.span.start - srcBase);
        t.px("width", suToPx(d.w)).px("height", suToPx(d.h));
        t.open();
        out += strs.get(d.src);  // trusted passthrough, as the block form (§9)
        out += "</span>";
        continue;
      }
      case DLRun::K::Spacer: {
        Tag t(out, "span");
        t.attrSafe("class", "tsr-sp");
        t.attrSafe("data-syn", d.syn);
        t.px("width", d.widthPx);
        t.open();
        out += "</span>";
        continue;
      }
      case DLRun::K::CodeCont:
      case DLRun::K::CodeText: {
        const Styling& cst = styles.get(d.face);
        Tag t(out, "span");
        t.attrSafe("class", RunClasses(cst, strs).sv());
        if (d.syn) t.attrSafe("data-syn", d.syn);
        runCss(t, cst, basePx, strs);
        if (d.fit == DLRun::Fit::LetterSpacing) {
          // ONE style attribute: the letter-spacing joins the run's own
          // declarations (defect #21: a second style="" was dropped)
          t.attrSafe("data-snap", "1");
          t.px("letter-spacing", d.letterPx);
        }
        t.open();
        if (d.k == DLRun::K::CodeCont) out.append(d.n, ' ');
        else escapeHtml(out, d.seg);
        out += "</span>";
        continue;
      }
      case DLRun::K::Words:
      case DLRun::K::Chars:
      case DLRun::K::Glyph:
      case DLRun::K::Hyphen:
        break;
    }
    // a text run: style, link, inline anchor, source, its own spacing
    const Styling& sty = styles.get(d.face);
    const bool isLink = (bool)d.link;
    {
      Tag t(out, isLink ? "a" : "span");
      t.attrSafe("class", RunClasses(sty, strs, nullptr, d.error ? "tsr-err" : d.cls).sv());
      if (isLink) t.attr("href", hrefOf(d.link, strs));
      if (d.error) t.attr("title", strs.get(d.error));  // (plan P3-16, document-model §9.1)
      if (d.id) t.id(strs.get(d.id));
      // (plan P3-07, §9.3) what copy takes: an omitted or replaced run says
      // its kind (a hyphen glyph says "hyphen" below), a replaced one its
      // text, once per group; a content run its source
      if (d.copy != CopyMode::Text) {
        if (!d.syn) t.attr("data-syn", strs.get(d.synName));
        if (d.copy == CopyMode::Replace) {
          t.attr("data-copy", strs.get(d.copyText));
          t.num("data-copy-group", d.copyGroup);
        }
      } else if (d.dataS != ~0u) {
        t.num("data-s", d.dataS - srcBase);
      }
      runCss(t, sty, basePx, strs);
      if (d.fit == DLRun::Fit::Pinned) {
        t.decl("display", "inline-block").decl("text-align", "center").px("width", d.widthPx);
        if (d.marginRight) t.px("margin-right", d.marginRightPx);
      } else if (d.fit == DLRun::Fit::LetterSpacing) {
        t.px("letter-spacing", d.letterPx);
        if (d.marginRight) t.px("margin-right", d.marginRightPx);
      }
      if (d.syn) t.attrSafe("data-syn", d.syn);
      t.open();
    }
    if (d.k == DLRun::K::Hyphen) {
      out += "-";
    } else if (d.k == DLRun::K::Glyph) {
      escapeHtml(out, strs.get(d.text));
    } else {
      const HList& h = *n.h;
      for (u32 k = d.i; k < d.j; k++) {
        const HItem& x = h.items[k];
        if (x.k == IK::Box) escapeHtml(out, strs.get(h.specs[x.aux].str));
        else if (x.k == IK::Glue && d.k == DLRun::K::Words) out += ' ';
      }
    }
    out += isLink ? "</a>" : "</span>";
  }
  out += "</div>\n";
}

// The root render contract (plan P1-04): the document root carries its
// language, base size and every font role resolved engine-side — the same
// families the faces measured — so measurement and paint never diverge and
// the HTML needs no host CSS for its fonts.
void writeRoot(std::string& out, const char* cls, const DLRoot& r) {
  Tag t(out, "div");
  t.attrSafe("class", cls);
  t.attr("lang", r.lang);
  t.declEsc("--tsr-font-body", r.fontBody);
  t.declEsc("--tsr-font-cjk", r.fontCjk);
  t.declEsc("--tsr-font-mono", r.fontMono);
  t.declEsc("--tsr-font-mono-cjk", r.fontMonoCjk);
  t.px("font-size", r.basePx);
  if (r.minHeightPx > 0) t.px("min-height", r.minHeightPx);
  t.open();
  out += "\n";
}

// the flowing block, with or without its positional attributes
void writeBlockOpen(std::string& out, const DLBlock& b, bool positional) {
  Tag t(out, "div");
  t.attrSafe("class", "tsr-para");
  if (positional) {
    t.num("data-pid", b.pid);
    t.num("data-s0", b.srcBase);
  }
  t.decl("position", "relative").px("height", suToPx(b.h));
  if (positional && b.gapAfterPx >= 0) t.px("margin-bottom", b.gapAfterPx);
  if (b.scrollX) t.decl("overflow-x", "auto");  // (D-Y09) a table wider than the measure
  t.open();
  out += "\n";
}
void writeBlockNodes(std::string& out, const DLBlock& b, const StyleTable& styles, const Interner& strs, double basePx) {
  for (const DLNode& n : b.nodes) writeNode(out, b, n, 0, b.srcBase, styles, strs, basePx);
  out += "</div>\n";
}
static void writeFlowBlock(std::string& out, const DLBlock& b, const StyleTable& styles, const Interner& strs,
                           double basePx, bool positional) {
  writeBlockOpen(out, b, positional);
  writeBlockNodes(out, b, styles, strs, basePx);
}
void writeBlock(std::string& out, const DLBlock& b, const StyleTable& styles, const Interner& strs, double basePx) {
  writeFlowBlock(out, b, styles, strs, basePx, true);
}
void writeBlockBody(std::string& out, const DLBlock& b, const StyleTable& styles, const Interner& strs, double basePx) {
  writeFlowBlock(out, b, styles, strs, basePx, false);
}

void writeNodes(std::string& out, const DLBlock& b, u32 lo, u32 hi, Su yShift, const StyleTable& styles,
                const Interner& strs, double basePx) {
  for (u32 i = lo; i < hi; i++) writeNode(out, b, b.nodes[i], yShift, 0, styles, strs, basePx);
}

}  // namespace tsr
