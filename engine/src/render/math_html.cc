#include "math_html.h"

#include "../math/font.h"
#include "html_writer.h"

namespace tsr {

namespace {
// Positioned glyph runs inside one formula (math-design.md §8): flatten the
// MathBox tree to absolute (x, baseline) leaves. A glyph span pins its text
// baseline by explicit line-height == the font's hhea height: the baseline
// then sits exactly kAscender·px/upem below the span top.
void mathLeaves(std::string& out, const MathBox* b, const Interner& strs,
                       Su x, Su base) {
  switch (b->kind) {
    case MathKind::Glyph: {
      const double px = (double)b->px;
      // text-font run (names/operators): the box carries the body font's
      // ascent/descent from the host measurer; the span's line box equals
      // the content area so the baseline lands exactly at `base` — its line
      // metrics, also when its extents are its reference ink (plan P5-01).
      // A math font's glyph is pinned by its hhea line box (the registry's)
      const bool text = b->font == kTextFont;
      const MathFont* mf = text ? nullptr : MathFontRegistry::get().byId(b->font);
      if (!text && !mf) mf = &MathFontRegistry::get().primary();
      const bool ink = text && (b->lineAsc || b->lineDesc);
      const Su tA = ink ? b->lineAsc : b->asc, tD = ink ? b->lineDesc : b->desc;
      const double fA = text ? suToPx(tA) : (double)mf->hheaAsc * px / mf->upem;
      const double fH = text ? suToPx(tA + tD) : (double)(mf->hheaAsc + mf->hheaDesc) * px / mf->upem;
      Tag t(out, "span");
      t.attrSafe("class", text ? "tsr-mg tsr-mt" : "tsr-mg");
      t.px("left", suToPx(x)).px("top", suToPx(base) - fA).px("font-size", px).px("line-height", fH);
      // (plan P5-01) a host's math font names its family (the contract's
      // .tsr-mg is the embedded one's); the registry vetted the name
      if (mf && mf->id != 0) {
        std::string fam = "\"";
        fam += mf->family;
        fam += '"';
        t.declEsc("font-family", fam);
      }
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

}  // namespace

// One formula as an inline box (§8): width/height from the box, the baseline
// pinned with vertical-align (inline) or an explicit top offset (display).
// (plan P3-26) the copy contract as any replaced run's: data-syn="math", its
// source as data-copy, every part of one formula in one data-copy-group
// (its source start, high bit set — never a replaced node's small number),
// so copy takes the source once whichever parts the selection holds. (plan
// P3-27, D-R04) its accessible name: role=math, the source as aria-label.
// what makes a span the formula: what copy takes of it (once per group), its
// accessible name, its source span
static void formulaAttrs(Tag& t, std::string_view srcAsWritten, const MathSpanOpts& o) {
  t.attrSafe("data-syn", "math");
  std::string src = o.display ? "$ " : "$";
  src += srcAsWritten;
  src += o.display ? " $" : "$";
  t.attr("data-copy", src);
  t.num("data-copy-group", 0x80000000u | (o.span.empty() ? o.group : o.span.start));
  if (o.label) {
    t.attrSafe("role", "math");
    t.attr("aria-label", srcAsWritten);
  }
  if (o.hidden) t.attrSafe("aria-hidden", "true");
  if (!o.span.empty()) {
    t.num("data-s", o.span.start - o.srcBase);
    t.num("data-e", o.span.end - o.srcBase);
  }
}

void writeMathRows(std::string& out, const std::vector<const MathBox*>& rows, std::string_view srcAsWritten,
                   const Interner& strs, const MathSpanOpts& o) {
  Tag t(out, "span");
  t.attrSafe("class", "tsr-math tsr-mathrows");
  formulaAttrs(t, srcAsWritten, o);
  t.open();
  MathSpanOpts row = o;
  row.bare = true;
  for (size_t i = 0; i < rows.size(); i++) {
    if (i) out += "<br>";
    writeMathSpan(out, rows[i], srcAsWritten, strs, row);
  }
  out += "</span>";
}

void writeMathSpan(std::string& out, const MathBox* mb, std::string_view srcAsWritten, const Interner& strs,
                   const MathSpanOpts& o) {
  Tag t(out, "span");
  t.attrSafe("class", "tsr-math");
  if (!o.bare) formulaAttrs(t, srcAsWritten, o);
  t.px("width", suToPx(mb->w)).px("height", suToPx(mb->asc + mb->desc));
  if (o.placed) t.decl("position", "absolute").decl("left", "0").px("top", o.displayTop);
  else t.px("vertical-align", -suToPx(mb->desc));
  if (o.color) t.declEsc("color", strs.get(o.color));  // the formula's paint style (plan P1-25)
  t.open();
  mathLeaves(out, mb, strs, 0, mb->asc);
  out += "</span>";
}

}  // namespace tsr
