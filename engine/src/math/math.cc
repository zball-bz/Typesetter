// Math parser + box layout (math-design.md §5–§7).
// Lineage: OpenType MATH constants drive every construct (Typst crosswalk);
// the TeXbook supplies what the font cannot: the inter-atom spacing matrix,
// bin→ord demotion (Rules 5–6), and the style-transition algebra.
#include "math.h"

#include "dict.h"
#include "env.h"
#include "font.h"
#include "ir.h"

namespace tsr {

namespace {

using mathfont::kNoTopAccent;

std::string cpToUtf8(u32 cp) {
  std::string s;
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) {
    s += (char)(0xC0 | (cp >> 6));
    s += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += (char)(0xE0 | (cp >> 12));
    s += (char)(0x80 | ((cp >> 6) & 0x3F));
    s += (char)(0x80 | (cp & 0x3F));
  } else {
    s += (char)(0xF0 | (cp >> 18));
    s += (char)(0x80 | ((cp >> 12) & 0x3F));
    s += (char)(0x80 | ((cp >> 6) & 0x3F));
    s += (char)(0x80 | (cp & 0x3F));
  }
  return s;
}

// ---- style algebra (TeXbook; KaTeX Style.ts encoding) ----------------------
enum : u8 { D = 0, Dc, T, Tc, S, Sc, SS, SSc };
constexpr u8 kSupStyle[8] = {S, Sc, S, Sc, SS, SSc, SS, SSc};
constexpr u8 kSubStyle[8] = {Sc, Sc, Sc, Sc, SSc, SSc, SSc, SSc};
constexpr u8 kNumStyle[8] = {T, Tc, S, Sc, SS, SSc, SS, SSc};
constexpr u8 kDenStyle[8] = {Tc, Tc, Sc, Sc, SSc, SSc, SSc, SSc};
inline bool isCramped(u8 st) { return st & 1; }
inline bool isScriptStyle(u8 st) { return st >= S; }
inline bool isDisplay(u8 st) { return st <= Dc; }
inline std::string hexCp(u32 cp) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04X", cp);
  return buf;
}

// the primary math font (plan P1-23: a runtime object; it supplies every
// MATH constant)
inline const MathFont& primaryFont() { return MathFontRegistry::get().primary(); }
inline double styleScale(u8 st) {
  if (st >= SS) return primaryFont().constant(C::ScriptScriptPercentScaleDown) / 100.0;
  if (st >= S) return primaryFont().constant(C::ScriptPercentScaleDown) / 100.0;
  return 1.0;
}

// ---- inter-atom glue (TeXbook p.170; KaTeX spacingData) --------------------
// value&3: 0 none / 1 thin(3mu) / 2 med(4mu) / 3 thick(5mu);
// value&4: suppressed in script/scriptscript styles (the parenthesized set).
constexpr u8 kSpaceTab[8][8] = {
    //         Ord Op Bin Rel Open Close Punct Inner
    /*Ord*/   {0, 1, 6, 7, 0, 0, 0, 5},
    /*Op*/    {1, 1, 0, 7, 0, 0, 0, 5},
    /*Bin*/   {6, 6, 0, 0, 6, 0, 0, 6},
    /*Rel*/   {7, 7, 0, 0, 7, 0, 0, 7},
    /*Open*/  {0, 0, 0, 0, 0, 0, 0, 0},
    /*Close*/ {0, 1, 6, 7, 0, 0, 0, 5},
    /*Punct*/ {5, 5, 0, 5, 5, 5, 5, 5},
    /*Inner*/ {5, 1, 6, 7, 5, 0, 5, 5},
};
constexpr int kMuOf[4] = {0, 3, 4, 5};

// ---- layout ----------------------------------------------------------------
struct Layouter {
  Arena& arena;
  Interner& strs;
  DiagSink& diags;
  Span span;
  double basePx;
  const MeasureNeeds* text = nullptr;  // text-font runs (nullptr = Euler only)
  StyleId textBase = 0;                // the formula's style (MathScope)
  std::vector<u32> uncovered;          // the code points warned about
  const MathFont& F = primaryFont();

  MathBox* mkBox(MathKind k) {
    MathBox* b = arena.make<MathBox>();
    b->kind = k;
    return b;
  }

  Su toSu(double units, u8 st) { return F.su(units, basePx * styleScale(st)); }
  Su constSu(C c, u8 st) { return toSu(F.constant(c), st); }

  // one warning per uncovered code point of the formula (at most 8)
  const GlyphRec* rec(u32 cp) {
    const GlyphRec* r = F.glyph(cp);
    if (!r && std::find(uncovered.begin(), uncovered.end(), cp) == uncovered.end() && uncovered.size() < 8) {
      uncovered.push_back(cp);
      diags.add(Sev::Warning, "math-coverage", span, "symbol U+" + hexCp(cp) + " not in math font");
    }
    return r;
  }

  MathBox* glyphBox(u32 cp, u8 cls, u8 st) {
    // a code point the math font does not cover is a measured text leaf (plan
    // P1-25; design T8 coverage chain): never a stand-in box another font paints
    if (!F.glyph(cp) && text) {
      rec(cp);  // the coverage warning, once per code point
      MathBox* t = textBox(cpToUtf8(cp), cls, st, /*textFont=*/true);
      t->cls = t->firstCls = t->lastCls = cls;
      return t;
    }
    MathBox* b = mkBox(MathKind::Glyph);
    b->cls = b->firstCls = b->lastCls = cls;
    b->text = strs.intern(cpToUtf8(cp));
    b->px = (float)(basePx * styleScale(st));
    if (const GlyphRec* r = rec(cp)) {
      b->w = toSu(r->adv, st);
      b->asc = toSu(r->asc, st);
      b->desc = toSu(r->desc, st);
      b->italic = toSu(r->italic, st);
      b->topAccent = r->topAccent != kNoTopAccent ? toSu(r->topAccent, st)
                                                  : (b->w + b->italic) / 2;
    } else {
      b->w = toSu(kMathPolicy.missingAdvU, st);
      b->asc = toSu(kMathPolicy.missingAscU, st);
      b->topAccent = b->w / 2;
    }
    return b;
  }

  // text-font run: measured by the host in the body font at the style's
  // size; missing metrics are recorded (the doc re-emits after the pull)
  // and the Euler box stands in meanwhile
  bool textFontBox(MathBox* b, std::string_view txt, u8 st) {
    if (!text || !text->metrics || !text->styles || !text->strs || text->docBasePx <= 0)
      return false;
    // made under the formula (plan P3-01): its style at the math size, the
    // formula positioning it
    Styling sty = text->styles->get(textBase);
    sty.baseline = 0;
    sty.sizePx = 0;
    sty.sizeMul = (float)(basePx * styleScale(st) / text->docBasePx);
    StyleId sid = text->styles->idOf(sty);
    StrRef ref = text->strs->intern(txt);
    if (!text->metrics->hasWord(ref, sid) || !text->metrics->hasVmet(sid)) {
      if (text->missing) text->missing->push_back({ref, text->metrics->faceOf(sid)});
      return false;
    }
    const WordMet& wm = text->metrics->word(ref, sid);
    const VMet& vm = text->metrics->vmet(sid);
    b->font = kTextFont;
    b->w = suCeilPx(wm.px);
    b->asc = vm.ascent;
    b->desc = vm.descent;
    b->italic = 0;
    b->topAccent = b->w / 2;
    return true;
  }

  // literal glyph run (digits, text operators): one box, summed advances
  MathBox* textBox(std::string_view txt, u8 cls, u8 st, bool textFont = false) {
    MathBox* b = mkBox(MathKind::Glyph);
    b->cls = b->firstCls = b->lastCls = cls;
    b->text = strs.intern(txt);
    b->px = (float)(basePx * styleScale(st));
    if (textFont && textFontBox(b, txt, st)) return b;
    double advU = 0;
    int ascU = 0, descU = 0, italU = 0;
    u32 i = 0;
    while (i < txt.size()) {
      u32 cp = utf8Next(txt, i);
      // a text-font run measured on a later pass: the Euler stand-in must
      // not raise coverage warnings for glyphs it will never paint
      if (const GlyphRec* r = textFont ? F.glyph(cp) : rec(cp)) {
        advU += r->adv;
        if (r->asc > ascU) ascU = r->asc;
        if (r->desc > descU) descU = r->desc;
        italU = r->italic;
      } else advU += kMathPolicy.missingAdvU;
    }
    b->w = toSu(advU, st);
    b->asc = toSu(ascU, st);
    b->desc = toSu(descU, st);
    b->italic = toSu(italU, st);
    b->topAccent = b->w / 2;
    return b;
  }

  MathBox* spacer(Su w) {
    MathBox* b = mkBox(MathKind::Spacer);
    b->w = w;
    return b;
  }

  // vertical glyph stretching (Typst fragment/glyph.rs::stretch): walk the
  // variant chain for the first glyph tall enough, else build the assembly
  // with extender repetition and uniform connector overlaps.
  MathBox* stretchVert(u32 cp, u8 cls, u8 st, Su target) {
    MathBox* natural = glyphBox(cp, cls, st);
    if (natural->asc + natural->desc >= target) return natural;
    const VarChain* ch = F.chain(cp);
    if (!ch) return natural;
    MathBox* best = natural;
    for (int k = 0; k < ch->n; k++) {
      u32 vcp = F.variantCps[ch->off + k];
      MathBox* vb = glyphBox(vcp, cls, st);
      best = vb;
      if (vb->asc + vb->desc >= target) return vb;
    }
    if (ch->asmN == 0) return best;
    // assembly, font units first (bottom-to-top part order per OpenType)
    const AsmPart* parts = &F.parts[ch->asmOff];
    const int minOv = F.minConnectorOverlap;
    double targetU = (double)target * F.upem /
                     (64.0 * basePx * styleScale(st));  // su → design units
    std::vector<const AsmPart*> list;
    for (int r = 1; r <= kMathPolicy.maxAssemblyRepeats; r++) {
      list.clear();
      for (int k = 0; k < ch->asmN; k++) {
        int copies = parts[k].isExtender ? r : 1;
        for (int c = 0; c < copies; c++) list.push_back(&parts[k]);
      }
      if (list.size() < 2) continue;
      double full = 0;
      for (const AsmPart* pp : list) full += pp->fullAdv;
      double maxH = full - (double)minOv * (double)(list.size() - 1);
      if (maxH >= targetU || r == 64) {
        // uniform overlap, clamped to every joint's connector capacity
        int maxOv = INT32_MAX;
        for (size_t k = 0; k + 1 < list.size(); k++) {
          int cap = list[k]->endOverlap < list[k + 1]->startOverlap
                        ? list[k]->endOverlap
                        : list[k + 1]->startOverlap;
          if (cap < maxOv) maxOv = cap;
        }
        if (maxOv < minOv) maxOv = minOv;
        double o = (full - targetU) / (double)(list.size() - 1);
        if (o < minOv) o = minOv;
        if (o > maxOv) o = maxOv;
        double H = full - o * (double)(list.size() - 1);
        MathBox* out = mkBox(MathKind::HBox);
        out->cls = out->firstCls = out->lastCls = cls;
        out->asc = toSu(H, st);
        out->desc = 0;
        double cursor = 0;  // ink height consumed, from the bottom
        for (const AsmPart* pp : list) {
          MathBox* g = glyphBox(pp->cp, cls, st);
          // part baseline so its ink bottom sits at `cursor` above box bottom
          Su dy = toSu(cursor, st) + g->desc;
          out->kids.push_back({0, dy, g});
          if (g->w > out->w) out->w = g->w;
          cursor += pp->fullAdv - o;
        }
        return out;
      }
    }
    return best;
  }

  // (plan P3-29) horizontal stretching (MathHorizGlyphConstruction): the
  // widest variant no wider than `target` when `fit` (an accent, TeX's rule:
  // a variant too wide keeps the one before, the natural glyph first), else
  // the first at least as wide; when every variant falls short, the
  // assembly at the target width (braces, arrows) — left to right, uniform
  // overlaps
  MathBox* stretchHoriz(u32 cp, u8 cls, u8 st, Su target, bool fit) {
    MathBox* natural = glyphBox(cp, cls, st);
    const VarChain* ch = F.hchain(cp);
    if (!ch || natural->w >= target) return natural;
    MathBox* best = natural;
    for (int k = 0; k < ch->n; k++) {
      MathBox* vb = glyphBox(F.variantCps[ch->off + k], cls, st);
      if (fit && vb->w > target) return best;
      best = vb;
      if (vb->w >= target) return vb;
    }
    if (ch->asmN == 0) return best;
    const AsmPart* parts = &F.parts[ch->asmOff];
    const int minOv = F.minConnectorOverlap;
    const double targetU = (double)target * F.upem / (64.0 * basePx * styleScale(st));
    std::vector<const AsmPart*> list;
    for (int r = 1; r <= kMathPolicy.maxAssemblyRepeats; r++) {
      list.clear();
      for (int k = 0; k < ch->asmN; k++)
        for (int c = 0, copies = parts[k].isExtender ? r : 1; c < copies; c++) list.push_back(&parts[k]);
      if (list.size() < 2) continue;
      double full = 0;
      for (const AsmPart* pp : list) full += pp->fullAdv;
      if (full - (double)minOv * (double)(list.size() - 1) < targetU && r < kMathPolicy.maxAssemblyRepeats) continue;
      int maxOv = INT32_MAX;
      for (size_t k = 0; k + 1 < list.size(); k++)
        maxOv = std::min(maxOv, std::min<int>(list[k]->endOverlap, list[k + 1]->startOverlap));
      if (maxOv < minOv) maxOv = minOv;
      double o = std::clamp((full - targetU) / (double)(list.size() - 1), (double)minOv, (double)maxOv);
      MathBox* out = mkBox(MathKind::HBox);
      out->cls = out->firstCls = out->lastCls = cls;
      double cursor = 0;
      for (const AsmPart* pp : list) {
        MathBox* g = glyphBox(pp->cp, cls, st);
        // its ink extents (a brace's lies above the baseline: a negative depth)
        out->asc = out->kids.empty() ? g->asc : std::max(out->asc, g->asc);
        out->desc = out->kids.empty() ? g->desc : std::max(out->desc, g->desc);
        out->kids.push_back({toSu(cursor, st), 0, g});
        cursor += pp->fullAdv - o;
      }
      out->w = toSu(cursor + o, st);
      out->topAccent = out->w / 2;
      return out;
    }
    return best;
  }
  // a spacing accent's combining form, which carries the wide variants
  static u32 combiningAccent(u32 cp) {
    switch (cp) {
      case 0x02C6: return 0x0302;  // ˆ hat
      case 0x02DC: return 0x0303;  // ˜ tilde
      case 0x02C7: return 0x030C;  // ˇ check
      case 0x02D8: return 0x0306;  // ˘ breve
      default: return cp;
    }
  }

  // wrap a stretched glyph so its box is centred on the math axis
  MathBox* centerOnAxis(MathBox* b, u8 cls, u8 st) {
    Su axis = constSu(C::AxisHeight, st);
    Su h = b->asc + b->desc;
    MathBox* o = mkBox(MathKind::HBox);
    o->cls = o->firstCls = o->lastCls = cls;
    o->w = b->w;
    o->italic = b->italic;
    o->asc = h / 2 + axis;
    o->desc = h - o->asc;
    o->kids.push_back({0, o->asc - b->asc, b});
    return o;
  }

  Su pairGlue(u8 l, u8 r, u8 st) {
    if (l > kInner || r > kInner) return 0;
    u8 v = kSpaceTab[l][r];
    if ((v & 4) && isScriptStyle(st)) return 0;
    int mu = kMuOf[v & 3];
    if (mu == 0) return 0;
    return toSu(mu * (double)F.upem / 18.0, st);
  }

  MathBox* layout(MNode* n, u8 st) {
    switch (n->k) {
      case MNode::Run: return layoutRun(n, st);
      case MNode::Sym:
        if (n->mid) return glyphBox(n->cp, n->cls, st);  // (a middle outside a group: its natural glyph)
        if (n->flags & kFlagLarge) return bigOpGlyph(n->cp, st);
        return glyphBox(n->cp, n->cls, st);
      case MNode::Num: return textBox(n->txt, n->cls, st, false);
      case MNode::Text: return textBox(n->txt, n->cls, st, n->textFont);
      case MNode::Attach: return layoutScript(n, st);
      case MNode::Frac: return layoutFrac(n, st);
      case MNode::Group: return layoutGroup(n, st);
      case MNode::Call: return layoutCall(n, st);
      case MNode::Error:  // its source slice, set in the text font (measured like names)
        return textBox(n->txt, kOrd, st, /*textFont=*/true);
      case MNode::Rows: return layoutGrid(n, "c", st);  // (rows outside a grid: centred cells)
      case MNode::Align:  // (an alignment point no grid or display takes: nothing; runs skip it)
      case MNode::Param: break;
    }
    return mkBox(MathKind::HBox);
  }

  MathBox* layoutRun(MNode* n, u8 st) {
    return layoutSlice(n->kids, 0, n->kids.size(), st, true, true);
  }

  MathBox* layoutSlice(const std::vector<MNode*>& kids, size_t lo, size_t hi,
                       u8 st, bool startEdge, bool endEdge) {
    std::vector<MathBox*> boxes;
    boxes.reserve(hi - lo);
    for (size_t k = lo; k < hi; k++)
      if (kids[k]->k != MNode::Align) boxes.push_back(layout(kids[k], st));
    return assemble(boxes, st, startEdge, endEdge);
  }

  // (plan P3-29; design T8 Grid) rows of cells. A column is as wide as its
  // widest cell, each cell set at its column's alignment — the align word's
  // letters (l, c, r) cycled over the columns; an `rl` pair (aligned) joins
  // without a gap and its right cell opens as after an Ord (TeX's `&={}`: a
  // relation keeps its space). Columns stand 1em apart (TeX's 2×\arraycolsep,
  // cases' \quad); rows are at least a strut tall (TeX's 8.5pt + 3.5pt at
  // 10pt: 0.85em + 0.35em), with a jot (0.3em) between them when the grid
  // has pairs (aligned). The grid is centred on the axis (TeX \vcenter).
  MathBox* layoutGrid(const MNode* rows, std::string_view align, u8 st) {
    if (align.empty()) align = "c";
    const Su em = toSu(F.upem, st);
    auto alignOf = [&](size_t c) { return align[c % align.size()]; };
    auto pairRight = [&](size_t c) { return c > 0 && alignOf(c - 1) == 'r' && alignOf(c) == 'l'; };
    bool pairs = false;
    for (size_t c = 1; c < 2 * align.size() && !pairs; c++) pairs = pairRight(c);
    struct Cell {
      MathBox* box;
      Su lead;
    };
    std::vector<std::vector<Cell>> grid;
    std::vector<Su> colW;
    for (const MNode* row : rows ? rows->kids : std::vector<MNode*>{}) {
      std::vector<Cell>& r = grid.emplace_back();
      for (size_t c = 0; c < row->kids.size(); c++) {
        MathBox* b = layout(row->kids[c], st);
        const Su lead = pairRight(c) && !b->kids.empty() ? pairGlue(kOrd, b->firstCls, st) : 0;
        r.push_back({b, lead});
        if (colW.size() <= c) colW.resize(c + 1, 0);
        colW[c] = std::max(colW[c], lead + b->w);
      }
    }
    std::vector<Su> colX(colW.size(), 0);
    Su x = 0;
    for (size_t c = 0; c < colW.size(); c++) {
      if (c) x += pairRight(c) ? 0 : em;
      colX[c] = x;
      x += colW[c];
    }
    const Su strutAsc = em * 85 / 100, strutDesc = em * 35 / 100, jot = pairs ? em * 3 / 10 : 0;
    std::vector<Su> asc(grid.size()), desc(grid.size());
    Su h = 0;
    for (size_t i = 0; i < grid.size(); i++) {
      asc[i] = strutAsc;
      desc[i] = strutDesc;
      for (const Cell& c : grid[i]) {
        asc[i] = std::max(asc[i], c.box->asc);
        desc[i] = std::max(desc[i], c.box->desc);
      }
      h += asc[i] + desc[i] + (i ? jot : 0);
    }
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    out->w = x;
    out->topAccent = x / 2;
    const Su axis = constSu(C::AxisHeight, st);
    out->asc = h / 2 + axis;
    out->desc = h - out->asc;
    Su top = 0;  // from the grid's top down to the row's top
    for (size_t i = 0; i < grid.size(); i++) {
      if (i) top += jot;
      const Su dy = out->asc - (top + asc[i]);
      for (size_t c = 0; c < grid[i].size(); c++) {
        const Cell& cell = grid[i][c];
        const Su w = cell.lead + cell.box->w, room = colW[c] - w;
        const char a = alignOf(c);
        const Su dx = colX[c] + cell.lead + (a == 'l' ? 0 : a == 'r' ? room : room / 2);
        out->kids.push_back({dx, dy, cell.box});
      }
      top += asc[i] + desc[i];
    }
    return out;
  }

  // Rules 5–6 (TeXbook): a Bin with no operand on its left — at the start,
  // after Bin, Op, Rel, Open or Punct — or before Rel, Close, Punct or the
  // end is an Ord. It runs ONCE over a list of laid-out atoms (plan P3-25):
  // a formula's top level for its segments, a run's own atoms inside it.
  // startEdge/endEdge false: the list is a slice of a larger one.
  void demote(std::vector<MathBox*>& boxes, bool startEdge, bool endEdge) {
    for (size_t i = 0; i < boxes.size(); i++) {
      u8 c = boxes[i]->cls;
      if (c == kBin) {
        u8 prev = i ? boxes[i - 1]->lastCls : 0xFF;
        if ((i == 0 && startEdge) || (i && (prev == kBin || prev == kOp ||
            prev == kRel || prev == kOpen || prev == kPunct)))
          boxes[i]->cls = boxes[i]->firstCls = boxes[i]->lastCls = kOrd;
      }
      if (c == kRel || c == kClose || c == kPunct) {
        if (i && boxes[i - 1]->cls == kBin)
          boxes[i - 1]->cls = boxes[i - 1]->firstCls = boxes[i - 1]->lastCls = kOrd;
      }
    }
    if (endEdge && !boxes.empty() && boxes.back()->cls == kBin)
      boxes.back()->cls = boxes.back()->firstCls = boxes.back()->lastCls = kOrd;
  }

  // atoms [lo, hi) side by side with their pair glue (their classes as
  // demoted); a lone atom's accent attachment is the list's (plan P3-25)
  MathBox* pack(const std::vector<MathBox*>& boxes, size_t lo, size_t hi, u8 st) {
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = kOrd;
    Su x = 0;
    for (size_t i = lo; i < hi; i++) {
      if (i > lo) {
        Su g = pairGlue(boxes[i - 1]->lastCls, boxes[i]->firstCls, st);
        if (g) {
          out->kids.push_back({x, 0, spacer(g)});
          x += g;
        }
      }
      out->kids.push_back({x, 0, boxes[i]});
      x += boxes[i]->w;
      if (boxes[i]->asc > out->asc) out->asc = boxes[i]->asc;
      if (boxes[i]->desc > out->desc) out->desc = boxes[i]->desc;
    }
    out->w = x;
    out->topAccent = hi - lo == 1 ? boxes[lo]->topAccent : x / 2;
    if (hi > lo) {
      out->firstCls = boxes[lo]->firstCls;
      out->lastCls = boxes[hi - 1]->lastCls;
      out->italic = boxes[hi - 1]->italic;
    }
    return out;
  }
  MathBox* assemble(std::vector<MathBox*>& boxes, u8 st, bool startEdge = true, bool endEdge = true) {
    demote(boxes, startEdge, endEdge);
    return pack(boxes, 0, boxes.size(), st);
  }

  // scripts: MATH constants with the TeX 18a character-base refinement and
  // Typst's joint collision resolution (scripts.rs::compute_script_shifts)
  // (plan P3-25) one attach for every base: an operator's limits mode — a
  // large operator's (sum), a text operator's (lim), limits() — sets its
  // scripts above and below; else they are scripts
  // (plan P3-29) an attach node's limits flag (attach(…, t, b, tr, br): t
  // and b above and below) forces them too; its pre-scripts (tl, bl) stand
  // before the base, shifted as its scripts are
  MathBox* layoutScript(MNode* n, u8 st) {
    const u8 fl = n->a->flags;
    const bool isChar = n->a->k == MNode::Sym && !(fl & kFlagLarge);
    MathBox* base;
    MathBox* out;
    if (n->limits || (fl & kFlagLimitsAlways) || ((fl & kFlagLimits) && isDisplay(st))) {
      base = n->a->k == MNode::Text ? textBox(n->a->txt, kOp, st, n->a->textFont) : layout(n->a, st);
      out = attachLimits(base, n->sub, n->sup, st);
    } else {
      base = layout(n->a, st);
      out = attachScripts(base, n->sub, n->sup, st, isChar);
    }
    return n->tl || n->bl ? attachPre(out, base, n->tl, n->bl, st, isChar) : out;
  }

  // a script pair's shifts against a base (MATH constants, the TeX 18a
  // character-base refinement and Typst's joint collision resolution)
  void scriptShifts(const MathBox* base, const MathBox* sup, const MathBox* sub, u8 st, bool isChar, Su& shiftUp,
                    Su& shiftDown) {
    shiftUp = shiftDown = 0;
    if (sup) {
      Su u0 = isChar ? 0 : base->asc - constSu(C::SuperscriptBaselineDropMax, st);
      Su su1 = constSu(isCramped(st) ? C::SuperscriptShiftUpCramped
                                     : C::SuperscriptShiftUp, st);
      Su su2 = sup->desc + constSu(C::SuperscriptBottomMin, st);
      shiftUp = u0 > su1 ? u0 : su1;
      if (su2 > shiftUp) shiftUp = su2;
    }
    if (sub) {
      Su v0 = isChar ? 0 : base->desc + constSu(C::SubscriptBaselineDropMin, st);
      Su sd1 = constSu(C::SubscriptShiftDown, st);
      shiftDown = v0 > sd1 ? v0 : sd1;
      if (!sup) {
        Su top = sub->asc - constSu(C::SubscriptTopMax, st);
        if (top > shiftDown) shiftDown = top;
      }
    }
    if (sup && sub) {
      Su gap = (shiftUp - sup->desc) - (sub->asc - shiftDown);
      Su gapMin = constSu(C::SubSuperscriptGapMin, st);
      if (gap < gapMin) {
        Su deficit = gapMin - gap;
        Su maxUp = constSu(C::SuperscriptBottomMaxWithSubscript, st) -
                   (shiftUp - sup->desc);
        Su up = deficit < maxUp ? deficit : (maxUp > 0 ? maxUp : 0);
        shiftUp += up;
        shiftDown += deficit - up;
      }
    }
  }

  MathBox* attachScripts(MathBox* base, MNode* subN, MNode* supN, u8 st,
                         bool isChar) {
    if (!subN && !supN) return base;
    MathBox* sup = supN ? layout(supN, kSupStyle[st]) : nullptr;
    MathBox* sub = subN ? layout(subN, kSubStyle[st]) : nullptr;
    Su shiftUp = 0, shiftDown = 0;
    scriptShifts(base, sup, sub, st, isChar, shiftUp, shiftDown);

    MathBox* out = mkBox(MathKind::HBox);
    // (plan P3-25) its edges are its base's: (a+b)^2 opens with an Open
    out->cls = base->cls;
    out->firstCls = base->firstCls;
    out->w = base->w;
    out->asc = base->asc;
    out->desc = base->desc;
    out->kids.push_back({0, 0, base});
    Su right = base->w;
    if (sup) {
      Su dx = base->w;
      out->kids.push_back({dx, shiftUp, sup});
      if (dx + sup->w > right) right = dx + sup->w;
      if (shiftUp + sup->asc > out->asc) out->asc = shiftUp + sup->asc;
      if (sup->desc - shiftUp > out->desc) out->desc = sup->desc - shiftUp;
    }
    if (sub) {
      // OpenType convention: the subscript hangs back by the italic
      // correction (the ∫ slant tuck); the advance includes the full ink
      Su dx = base->w - base->italic;
      if (dx < 0) dx = 0;
      out->kids.push_back({dx, -shiftDown, sub});
      if (dx + sub->w > right) right = dx + sub->w;
      if (shiftDown + sub->desc > out->desc) out->desc = shiftDown + sub->desc;
      if (sub->asc - shiftDown > out->asc) out->asc = sub->asc - shiftDown;
    }
    out->w = right + constSu(C::SpaceAfterScript, st);
    out->lastCls = base->lastCls;
    return out;
  }

  // (plan P3-29) pre-scripts (attach's tl, bl): shifted as scripts of the
  // base are, right-aligned against the body (the base with its own
  // attachments), which follows them
  MathBox* attachPre(MathBox* body, const MathBox* base, MNode* tlN, MNode* blN, u8 st, bool isChar) {
    MathBox* sup = tlN ? layout(tlN, kSupStyle[st]) : nullptr;
    MathBox* sub = blN ? layout(blN, kSubStyle[st]) : nullptr;
    Su shiftUp = 0, shiftDown = 0;
    scriptShifts(base, sup, sub, st, isChar, shiftUp, shiftDown);
    const Su preW = std::max(sup ? sup->w : 0, sub ? sub->w : 0);
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = body->cls;
    out->firstCls = body->firstCls;
    out->lastCls = body->lastCls;
    out->italic = body->italic;
    out->w = preW + body->w;
    out->asc = body->asc;
    out->desc = body->desc;
    if (sup) {
      out->kids.push_back({preW - sup->w, shiftUp, sup});
      out->asc = std::max(out->asc, shiftUp + sup->asc);
      out->desc = std::max(out->desc, sup->desc - shiftUp);
    }
    if (sub) {
      out->kids.push_back({preW - sub->w, -shiftDown, sub});
      out->desc = std::max(out->desc, shiftDown + sub->desc);
      out->asc = std::max(out->asc, sub->asc - shiftDown);
    }
    out->kids.push_back({preW, 0, body});
    return out;
  }

  // fractions: Typst fraction.rs verbatim (MATH constants, axis-centred bar)
  MathBox* layoutFrac(MNode* n, u8 st) {
    bool disp = isDisplay(st);
    MathBox* num = layout(n->a, kNumStyle[st]);
    MathBox* den = layout(n->b, kDenStyle[st]);
    Su axis = constSu(C::AxisHeight, st);
    Su thick = constSu(C::FractionRuleThickness, st);
    Su numGapMin = constSu(disp ? C::FractionNumDisplayStyleGapMin
                                : C::FractionNumeratorGapMin, st);
    Su denGapMin = constSu(disp ? C::FractionDenomDisplayStyleGapMin
                                : C::FractionDenominatorGapMin, st);
    Su numUp = constSu(disp ? C::FractionNumeratorDisplayStyleShiftUp
                            : C::FractionNumeratorShiftUp, st);
    Su denDown = constSu(disp ? C::FractionDenominatorDisplayStyleShiftDown
                              : C::FractionDenominatorShiftDown, st);
    Su numFloor = axis + thick / 2 + numGapMin + num->desc;
    if (numFloor > numUp) numUp = numFloor;
    Su denFloor = -axis + thick / 2 + denGapMin + den->asc;
    if (denFloor > denDown) denDown = denFloor;

    Su w = num->w > den->w ? num->w : den->w;
    const Su pad = toSu((double)F.upem * kMathPolicy.fracPadNum / kMathPolicy.fracPadDen, st);
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    out->w = w + 2 * pad;
    out->asc = numUp + num->asc;
    out->desc = denDown + den->desc;
    MathBox* bar = mkBox(MathKind::Rule);
    bar->w = w;
    bar->asc = thick;
    out->kids.push_back({pad + (w - num->w) / 2, numUp, num});
    out->kids.push_back({pad, axis - thick / 2, bar});
    out->kids.push_back({pad + (w - den->w) / 2, -denDown, den});
    return out;
  }

  // fenced content: delimiters stretch to the content when it outgrows the
  // natural glyph — target 2·max(asc−axis, desc+axis), 10% shortfall
  // tolerated (Typst short_fall); centred on the axis when stretched.
  MathBox* fencedRun(const std::vector<MNode*>& kidsN, u32 openCp, u32 closeCp,
                     u8 st) {
    std::vector<MathBox*> inner;
    inner.reserve(kidsN.size());
    Su iAsc = 0, iDesc = 0;
    for (MNode* k : kidsN) {
      if (k->k == MNode::Sym && k->mid) {  // (plan P3-24) its middle: stretched below, with the delimiters
        inner.push_back(nullptr);
        continue;
      }
      MathBox* b = layout(k, st);
      if (b->asc > iAsc) iAsc = b->asc;
      if (b->desc > iDesc) iDesc = b->desc;
      inner.push_back(b);
    }
    Su axis = constSu(C::AxisHeight, st);
    Su over = iAsc - axis, under = iDesc + axis;
    Su target = 2 * (over > under ? over : under);
    target = kMathPolicy.shortfall(target);  // short_fall
    auto delim = [&](u32 cp, u8 cls) {
      // (plan P3-29) `.`: no delimiter (TeX's \right.), its null space
      // (\nulldelimiterspace: 1.2pt at 10pt)
      if (cp == '.') {
        MathBox* sp = spacer(toSu(0.12 * F.upem, st));
        sp->cls = sp->firstCls = sp->lastCls = cls;
        return sp;
      }
      MathBox* g = glyphBox(cp, cls, st);
      if (g->asc + g->desc >= target) return g;  // natural glyph suffices
      MathBox* sg = stretchVert(cp, cls, st, target);
      return centerOnAxis(sg, cls, st);
    };
    for (size_t i = 0; i < inner.size(); i++)
      if (!inner[i]) inner[i] = delim(kidsN[i]->cp, kRel);
    std::vector<MathBox*> boxes;
    boxes.reserve(inner.size() + 2);
    boxes.push_back(delim(openCp, kOpen));
    for (MathBox* b : inner) boxes.push_back(b);
    boxes.push_back(delim(closeCp, kClose));
    MathBox* out = assemble(boxes, st);
    out->cls = kOrd;  // firstCls/lastCls stay Open/Close for neighbour glue
    return out;
  }

  MathBox* layoutGroup(MNode* n, u8 st) {
    return fencedRun(n->a->kids, n->openCp, n->closeCp, st);
  }

  // limits above/below a display operator (MATH constants; K assembleSupSub,
  // T compute_limit_shifts). Horizontal centres slide by ±italic/2.
  MathBox* attachLimits(MathBox* base, MNode* subN, MNode* supN, u8 st) {
    MathBox* sup = supN ? layout(supN, kSupStyle[st]) : nullptr;
    MathBox* sub = subN ? layout(subN, kSubStyle[st]) : nullptr;
    if (!sup && !sub) return base;
    Su w = base->w;
    if (sup && sup->w > w) w = sup->w;
    if (sub && sub->w > w) w = sub->w;
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = base->cls;
    out->w = w;
    out->asc = base->asc;
    out->desc = base->desc;
    Su cx = w / 2;
    out->kids.push_back({cx - base->w / 2, 0, base});
    if (sup) {
      Su rise = constSu(C::UpperLimitGapMin, st) + sup->desc;
      Su rise2 = constSu(C::UpperLimitBaselineRiseMin, st);
      Su dy = base->asc + (rise > rise2 ? rise : rise2);
      out->kids.push_back({cx + base->italic / 2 - sup->w / 2, dy, sup});
      out->asc = dy + sup->asc;
    }
    if (sub) {
      Su drop = constSu(C::LowerLimitGapMin, st) + sub->asc;
      Su drop2 = constSu(C::LowerLimitBaselineDropMin, st);
      Su dy = base->desc + (drop > drop2 ? drop : drop2);
      out->kids.push_back({cx - base->italic / 2 - sub->w / 2, -dy, sub});
      out->desc = dy + sub->desc;
    }
    return out;
  }

  // (plan P3-25) a large operator's glyph: in display style grown to
  // DisplayOperatorMinHeight and centred on the axis (T glyph stretch; K
  // makeLargeOp Size2 swap). Its scripts are attach's; what follows it is
  // the formula's own atoms.
  MathBox* bigOpGlyph(u32 cp, u8 st) {
    MathBox* op = glyphBox(cp, kOp, st);
    if (isDisplay(st)) {
      Su minH = constSu(C::DisplayOperatorMinHeight, st);
      if (op->asc + op->desc < minH) op = centerOnAxis(stretchVert(cp, kOp, st, minH), kOp, st);
    }
    return op;
  }

  // radicals: MATH constants per Typst radical.rs; surd stretched to the
  // radicand + gap + rule, leftover split half above / half below (TeXbook
  // p.443 item 11); degree raised by RadicalDegreeBottomRaisePercent.
  MathBox* layoutRadical(MNode* degN, MNode* radN, u8 st) {
    MathBox* rad = layout(radN, (u8)(st | 1));  // cramped
    bool disp = isDisplay(st);
    Su gap = constSu(disp ? C::RadicalDisplayStyleVerticalGap
                          : C::RadicalVerticalGap, st);
    Su thick = constSu(C::RadicalRuleThickness, st);
    Su extra = constSu(C::RadicalExtraAscender, st);
    Su target = rad->asc + rad->desc + gap + thick;
    MathBox* surd = stretchVert(0x221A, kOrd, st, target);
    Su surdH = surd->asc + surd->desc;
    Su excess = surdH - target;
    Su below = rad->desc + (excess > 0 ? excess / 2 : 0);
    Su top = surdH - below;         // surd ink top, above baseline
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    Su x = 0;
    if (degN && !degN->kids.empty()) {
      MathBox* deg = layoutRun(degN, SS);  // degree in scriptscript
      Su kb = constSu(C::RadicalKernBeforeDegree, st);
      Su ka = constSu(C::RadicalKernAfterDegree, st);  // typically negative
      Su raise = (Su)((i64)surdH *
                      F.constant(C::RadicalDegreeBottomRaisePercent) / 100);
      Su degDy = -below + raise + deg->desc;
      out->kids.push_back({kb, degDy, deg});
      x = kb + deg->w + ka;
      if (x < 0) x = 0;
      if (degDy + deg->asc > top + extra && degDy + deg->asc > out->asc)
        out->asc = degDy + deg->asc;
    }
    out->kids.push_back({x, surd->desc - below, surd});
    x += surd->w;
    MathBox* bar = mkBox(MathKind::Rule);
    bar->w = rad->w;
    bar->asc = thick;
    out->kids.push_back({x, top - thick, bar});
    out->kids.push_back({x, 0, rad});
    out->w = x + rad->w;
    if (top + extra > out->asc) out->asc = top + extra;
    if (rad->asc > out->asc) out->asc = rad->asc;
    out->desc = below > rad->desc ? below : rad->desc;
    return out;
  }

  // accents: TopAccentAttachment alignment (T fragment/glyph.rs), cramped
  // base; the accent rides at max(0, base.asc − AccentBaseHeight).
  MathBox* layoutAccent(u32 accCp, MNode* baseN, u8 st) {
    MathBox* base = layoutRun(baseN, (u8)(st | 1));
    // a single-glyph base carries its glyph's TopAccentAttachment (pack
    // passes a lone atom's on, plan P3-25)
    const Su baseAttach = base->topAccent;
    MathBox* acc = glyphBox(accCp, kOrd, st);
    // (plan P3-29) a base wider than the accent takes a wide one — the
    // widest variant that fits (hat, tilde), or an assembly as wide as the
    // base (an arrow) — centred over it
    bool wide = false;
    if (base->w > acc->w)
      if (const u32 wc = F.hchain(accCp) ? accCp : combiningAccent(accCp); F.hchain(wc)) {
        MathBox* w = stretchHoriz(wc, kOrd, st, base->w, /*fit=*/true);
        if (w->w > acc->w) {
          acc = w;
          wide = true;
        }
      }
    Su dy = base->asc - constSu(C::AccentBaseHeight, st);
    if (dy < 0) dy = 0;
    Su x = wide ? (base->w - acc->w) / 2 : baseAttach - acc->topAccent;
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    out->w = base->w;
    out->italic = base->italic;
    out->topAccent = base->topAccent;
    out->kids.push_back({0, 0, base});
    out->kids.push_back({x, dy, acc});
    out->asc = base->asc;
    if (dy + acc->asc > out->asc) out->asc = dy + acc->asc;
    out->desc = base->desc;
    return out;
  }

  // binomial: barless stack (Stack* MATH constants) fenced in parens
  // stack(top, bottom): two rows about the axis, no rule (MATH Stack*
  // constants; binom = lr((, stack(n, k), )) in stdlib.tsv)
  MathBox* layoutStack(MNode* topN, MNode* botN, u8 st) {
    bool disp = isDisplay(st);
    MathBox* top = layout(topN, kNumStyle[st]);
    MathBox* bot = layout(botN, kDenStyle[st]);
    Su upMin = constSu(disp ? C::StackTopDisplayStyleShiftUp
                            : C::StackTopShiftUp, st);
    Su downMin = constSu(disp ? C::StackBottomDisplayStyleShiftDown
                              : C::StackBottomShiftDown, st);
    Su gapMin = constSu(disp ? C::StackDisplayStyleGapMin : C::StackGapMin, st);
    Su up = upMin, down = downMin;
    Su gap = (up - top->desc) - (bot->asc - down);
    if (gap < gapMin) {  // split the deficit both ways (Typst stack leftover)
      Su d = gapMin - gap;
      up += d / 2;
      down += d - d / 2;
    }
    Su w = top->w > bot->w ? top->w : bot->w;
    MathBox* stack = mkBox(MathKind::HBox);
    stack->cls = stack->firstCls = stack->lastCls = kOrd;
    stack->w = w;
    stack->topAccent = w / 2;
    stack->asc = up + top->asc;
    stack->desc = down + bot->desc;
    stack->kids.push_back({(w - top->w) / 2, up, top});
    stack->kids.push_back({(w - bot->w) / 2, -down, bot});
    return stack;
  }

  // over/underline: MATH Overbar*/Underbar* rule constructs. `bar` routes
  // here too (user-visible contract): a Rule box renders as a plain div —
  // identical in every browser, unlike accent glyph shaping.
  MathBox* layoutHRule(MNode* baseN, u8 st, bool over) {
    MathBox* base = layoutRun(baseN, (u8)(st | 1));  // cramped
    Su gap = constSu(over ? C::OverbarVerticalGap : C::UnderbarVerticalGap, st);
    Su thick = constSu(over ? C::OverbarRuleThickness : C::UnderbarRuleThickness, st);
    Su extra = constSu(over ? C::OverbarExtraAscender : C::UnderbarExtraDescender, st);
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    out->w = base->w;
    out->italic = base->italic;
    out->topAccent = base->topAccent;
    MathBox* bar = mkBox(MathKind::Rule);
    bar->w = base->w;
    bar->asc = thick;
    out->kids.push_back({0, 0, base});
    if (over) {
      out->kids.push_back({0, base->asc + gap, bar});
      out->asc = base->asc + gap + thick + extra;
      out->desc = base->desc;
    } else {
      out->kids.push_back({0, -(base->desc + gap + thick), bar});
      out->asc = base->asc;
      out->desc = base->desc + gap + thick + extra;
    }
    return out;
  }

  // (plan P3-29; design T8 HStretch) a brace, bracket, paren or arrow
  // stretched to the base's width, over or under it: a stretch stack (its
  // gap to the base StretchStackGapBelowMin over, …AboveMin under); its
  // annotations are attach's, above and below (hstretch's limits)
  MathBox* layoutHStretch(MNode* baseN, u32 cp, bool over, u8 st) {
    MathBox* base = layout(baseN, st);
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    out->kids.push_back({0, 0, base});
    out->w = base->w;
    out->asc = base->asc;
    out->desc = base->desc;
    if (!cp) return out;
    MathBox* g = stretchHoriz(cp, kOrd, st, base->w, /*fit=*/false);
    const Su dx = (base->w - g->w) / 2;
    if (dx < 0) {  // a glyph wider than its base: the base centred under it
      out->kids[0].dx = -dx;
      out->w = g->w;
    }
    if (over) {
      const Su dy = base->asc + constSu(C::StretchStackGapBelowMin, st) + g->desc;
      out->kids.push_back({std::max<Su>(dx, 0), dy, g});
      out->asc = dy + g->asc;
    } else {
      const Su dy = base->desc + constSu(C::StretchStackGapAboveMin, st) + g->asc;
      out->kids.push_back({std::max<Su>(dx, 0), -dy, g});
      out->desc = dy + g->desc;
    }
    out->topAccent = out->w / 2;
    return out;
  }

  // a primitive call (plan P1-24): the closed set; template rows were
  // expanded at bind, so nothing here knows a family name
  static std::string_view identText(const MNode* n) {
    if (n->k == MNode::Text) return n->txt;
    if (n->k == MNode::Run && n->kids.size() == 1 && n->kids[0]->k == MNode::Text) return n->kids[0]->txt;
    return {};
  }
  MathBox* layoutCall(MNode* n, u8 st) {
    auto arg = [&](size_t i) -> MNode* { return i < n->kids.size() ? n->kids[i] : nullptr; };
    auto run = [&](MNode* a) -> std::vector<MNode*> {
      if (!a) return {};
      if (a->k == MNode::Run) return a->kids;
      return {a};
    };
    auto content = [&](size_t i) -> MNode* {
      if (MNode* a = arg(i)) return a;
      static MNode empty;
      empty.k = MNode::Run;
      return &empty;
    };
    switch (n->prim) {
      case Prim::Frac: {
        MNode fr;
        fr.k = MNode::Frac;
        fr.a = content(0);
        fr.b = content(1);
        return layoutFrac(&fr, st);
      }
      case Prim::Stack: return layoutStack(content(0), content(1), st);
      case Prim::Radical: return layoutRadical(arg(1), content(0), st);
      case Prim::Lr: {
        const u32 open = arg(0) && arg(0)->k == MNode::Sym ? arg(0)->cp : 0;
        const u32 close = arg(2) && arg(2)->k == MNode::Sym ? arg(2)->cp : 0;
        return fencedRun(run(arg(1)), open, close, st);
      }
      case Prim::Accent:
        return layoutAccent(arg(1) && arg(1)->k == MNode::Sym ? arg(1)->cp : 0, content(0), st);
      case Prim::Rule:
        return layoutHRule(content(0), st, /*over=*/!(arg(1) && arg(1)->txt == "under"));
      // (plan P3-24) a fixed space of mu/18 em; a style for its body; a class
      // for its box (the rewrites were bound at parse: what reaches here is
      // their content)
      case Prim::Space: {
        const MNode* m = arg(0);
        if (m && m->k == MNode::Run && m->kids.size() == 1) m = m->kids[0];
        const double mu = m && m->k == MNode::Num ? std::strtod(m->txt.c_str(), nullptr) : 0;
        MathBox* b = spacer(toSu(mu * F.upem / 18.0, st));
        b->cls = b->firstCls = b->lastCls = kOrd;
        return b;
      }
      case Prim::Style: {
        const std::string_view s = arg(1) ? identText(arg(1)) : std::string_view{};
        const u8 base = s == "display" ? D : s == "script" ? S : s == "sscript" ? SS : s == "text" ? T : (u8)(st & ~1);
        return layout(content(0), (u8)(base | (st & 1)));
      }
      case Prim::Class: {
        MathBox* b = layout(content(1), st);
        b->cls = b->firstCls = b->lastCls = n->cls;
        return b;
      }
      case Prim::Limits:
      case Prim::Variant:
      case Prim::Attach:  // (bound to an Attach node)
        return layout(content(0), st);
      case Prim::Grid: {
        const MNode* rows = arg(1);
        return layoutGrid(rows && rows->k == MNode::Rows ? rows : nullptr, arg(0) ? identText(arg(0)) : "c", st);
      }
      case Prim::HStretch:
        return layoutHStretch(content(0), arg(1) && arg(1)->k == MNode::Sym ? arg(1)->cp : 0,
                              !(arg(2) && identText(arg(2)) == "under"), st);
      case Prim::Delim: {
        const MNode* m = arg(1);
        if (m && m->k == MNode::Run && m->kids.size() == 1) m = m->kids[0];
        const double em = m && m->k == MNode::Num ? std::strtod(m->txt.c_str(), nullptr) : 0;
        const MNode* d = arg(0);
        if (!d || d->k != MNode::Sym) {
          std::vector<MathBox*> none;
          return assemble(none, st);
        }
        // the delimiter at least `em` tall, on the axis (TeX's \big: amsmath's
        // 1.2em steps); its class the symbol's
        MathBox* g = stretchVert(d->cp, d->cls, st, toSu(em * F.upem, st));
        return centerOnAxis(g, d->cls, st);
      }
      case Prim::Phantom: {
        const std::string_view m = arg(1) ? identText(arg(1)) : std::string_view{"full"};
        MathBox* body = layout(content(0), st);
        MathBox* out = mkBox(MathKind::HBox);
        out->cls = body->cls;
        out->firstCls = body->firstCls;
        out->lastCls = body->lastCls;
        if (m == "smash") {  // its ink, without height or depth
          out->w = body->w;
          out->kids.push_back({0, 0, body});
          return out;
        }
        // its room, without ink: full, width only (h), height only (v)
        out->w = m == "v" ? 0 : body->w;
        out->asc = m == "h" ? 0 : body->asc;
        out->desc = m == "h" ? 0 : body->desc;
        return out;
      }
      case Prim::None: break;
    }
    std::vector<MathBox*> none;
    return assemble(none, st);
  }
};

}  // namespace

MathBox* layoutMathFormula(std::string_view src, bool display, double sizePx,
                           Arena& arena, Interner& strs, DiagSink& diags,
                           Span span, const MeasureNeeds* text, bool parseDiags, const MathScope* scope) {
  // errors are local (plan P1-24): an Error leaf lays out in place
  MathIR ir = parseMath(src, arena, scope);
  if (parseDiags) reportMathDiags(ir, src, span, diags);
  Layouter L{arena, strs, diags, span, sizePx, text, scope ? scope->style : 0};
  return L.layout(ir.root, display ? D : T);
}

// (plan P3-25; design T8 Lazy MathLayout: one math-list → item conversion)
// the formula's top-level atoms are laid out once and demoted once; a break
// between two of them costs min(after its left's class, before its right's)
// from the class tables (math.breakAfter / math.breakBefore), a class with
// none giving none; a segment is a maximal run without a break inside, and
// the glue a break consumes is the pair's
std::vector<MathSeg> layoutMathSegments(std::string_view src, bool display,
                                        double sizePx, Arena& arena,
                                        Interner& strs, DiagSink& diags,
                                        Span span, const MathBreaks& breaks, const MeasureNeeds* text,
                                        bool parseDiags, const MathScope* scope) {
  std::vector<MathSeg> out;
  MathIR ir = parseMath(src, arena, scope);
  if (parseDiags) reportMathDiags(ir, src, span, diags);
  Layouter L{arena, strs, diags, span, sizePx, text, scope ? scope->style : 0};
  const u8 st = display ? D : T;
  const std::vector<MNode*>& kids = ir.root->kids;
  if (display || kids.empty()) {
    out.push_back({L.layout(ir.root, st), 0, 0});
    return out;
  }
  std::vector<MathBox*> boxes;
  boxes.reserve(kids.size());
  for (MNode* k : kids)
    if (k->k != MNode::Align) boxes.push_back(L.layout(k, st));
  L.demote(boxes, /*startEdge=*/true, /*endEdge=*/true);
  std::vector<size_t> cuts{0};
  std::vector<float> pens{0};
  for (size_t i = 0; i + 1 < boxes.size(); i++) {
    const u8 l = boxes[i]->lastCls, r = boxes[i + 1]->firstCls;
    const double a = l < 8 ? breaks.after[l] : -1, b = r < 8 ? breaks.before[r] : -1;
    const double p = a >= 0 && b >= 0 ? std::min(a, b) : a >= 0 ? a : b;
    if (p < 0) continue;
    cuts.push_back(i + 1);
    pens.push_back((float)p);
  }
  cuts.push_back(boxes.size());
  for (size_t c = 0; c + 1 < cuts.size(); c++) {
    MathBox* b = L.pack(boxes, cuts[c], cuts[c + 1], st);
    const Su glue = c ? L.pairGlue(boxes[cuts[c] - 1]->lastCls, boxes[cuts[c]]->firstCls, st) : 0;
    out.push_back({b, glue, pens[c]});
  }
  return out;
}

static void dumpBox(std::string& out, const MathBox* b, const Interner& strs,
                    int depth, Su dx, Su dy) {
  for (int i = 0; i < depth; i++) out += "  ";
  static const char* kClsName[] = {"Ord",  "Op",    "Bin",   "Rel",
                                   "Open", "Close", "Punct", "Inner"};
  const char* cls = b->cls <= 7 ? kClsName[b->cls] : "?";
  switch (b->kind) {
    case MathKind::Glyph:
      out += "glyph \"";
      appendEscaped(out, strs.get(b->text));
      appendf(out, "\" %s w=%d asc=%d desc=%d", cls, b->w, b->asc, b->desc);
      if (b->italic) appendf(out, " it=%d", b->italic);
      appendf(out, " px=%g", (double)b->px);
      break;
    case MathKind::Rule:
      appendf(out, "rule w=%d h=%d", b->w, b->asc + b->desc);
      break;
    case MathKind::Spacer:
      appendf(out, "glue w=%d", b->w);
      break;
    case MathKind::HBox:
      appendf(out, "hbox %s w=%d asc=%d desc=%d", cls, b->w, b->asc, b->desc);
      break;
  }
  if (dx || dy) appendf(out, " @(%d,%d)", dx, dy);
  out += "\n";
  for (const MathKid& k : b->kids) dumpBox(out, k.box, strs, depth + 1, k.dx, k.dy);
}

std::string dumpMathBox(const MathBox* box, const Interner& strs) {
  std::string out;
  if (box) dumpBox(out, box, strs, 0, 0, 0);
  return out;
}

}  // namespace tsr
