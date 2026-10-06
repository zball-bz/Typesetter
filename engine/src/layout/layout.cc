#include "layout.h"

#include "../support/rails.h"
#include "../shape/objects.h"
#include "../shape/textrules.h"

#include <algorithm>
#include <unordered_set>

#include "../code/grid.h"
#include "grid.h"

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
  u32 fills = 0;  // fil glue (plan P2-16)
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
    if (it.k == IK::Glue && it.cls == (u8)GC::Fill) f.fills++;
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

// How a stream's lines sit and join (plan P1-17; design T6 LinePolicy).
struct LinePolicy {
  // (plan P3-07) the stream's last line's separator: its unit's (a
  // paragraph's sepAfter) or its track's (a cell's tab or row); inside the
  // stream every line takes its break's — whatever the alignment
  Sep endSep = Sep::Newline;
  // (plan P3-09; design T6 LineEnds) the glue at the lines' ends — the
  // breaker optimized with it, materializeLines realizes it: one preset
  // per alignment, a table cell's halign included
  LineEnds ends;
  bool singleCenter = false;  // par.singleLine center: a one-line stream is centred (D-Y05)
  StrRef marker = 0;    // on the first line
  StyleId markerStyle = 0;
  StrRef anchor = 0;    // the stream's anchor (a cell's, a caption's), on its first line
};
// a broken stream and where its lines go
struct LineStream {
  const HList& h;
  const std::vector<u32>& blockStart;
  u32 nBlocks;
  const BreakResult& br;
  const ParShape& shape;  // each line's slot in the content box
  Su left;                // the content box's start
  u32 unitIdx;
  i32 cellIdx;
};

// One function turns every broken stream — a paragraph, a table cell, a
// float caption, a sidecar row — into lines (plan P1-17; it replaces four
// loops and P1-13's height shim): the item range after discard; the natural
// width and stretch; heights from vmet and object parts; the hyphen from the
// Disc; the join from the discarded run at the break (a source space joins
// with a space, a Disc, synthetic glue or nothing with none, the paragraph
// end or a forced break is a real boundary); Overfull lines set at the
// shrink limit. Returns the cursor after the last line.
i64 materializeLines(const LineStream& s, const LinePolicy& pol, const MetricStore& metrics, const LayoutSettings& cfg,
                     Su baseLeading, i64 y, std::vector<Fragment>& out) {
  u32 prev = 0;
  bool first = true;
  const BreakResult& br = s.br;
  const u32 nLines = (u32)br.breakpoints.size();
  u32 made = 0;  // lines made so far
  for (size_t li = 0; li < br.breakpoints.size(); li++) {
    const u32 bp = br.breakpoints[li];
    LineItems r;
    const bool any = lineItems(s.h, s.blockStart, prev, bp, r);
    prev = bp;
    if (!any) continue;
    const LineFill f = fillLine(s.h, r, metrics);
    Fragment line;
    line.unitIdx = s.unitIdx;

    line.cellIdx = s.cellIdx;
    line.blockBegin = r.lo;
    line.blockEnd = r.hi;
    line.itemBegin = r.ilo;
    line.itemEnd = r.ihi;
    // its slot (plan P3-08: the paragraph's shape)
    const LineSlot& slot = s.shape.at((u32)li);
    line.left = s.left + slot.left;
    line.width = slot.width;

    line.srcSpan = f.span;
    line.endsWithHyphen = f.endsHyphen;
    if (first) {
      line.marker = pol.marker;
      line.markerStyle = pol.markerStyle;
      line.anchor = pol.anchor;
      first = false;
    }
    // a hard line break ends a line like the paragraph end: unjustified, a
    // real line boundary for copy
    const bool last = bp == s.nBlocks || endsForced(s.h, r.ihi, s.blockStart[bp]);
    line.overfull = std::binary_search(br.overfullLines.begin(), br.overfullLines.end(), (u32)li);
    // (plan P3-09) its end glue: the last-line pair after a Forced break; a
    // stream of one line under par.singleLine center is centred
    static constexpr EndGlue kFil{0, 0, 1};
    const bool single = pol.singleCenter && br.breakpoints.size() == 1;
    const EndGlue& ea = single ? kFil : last ? pol.ends.lastStart : pol.ends.start;
    const EndGlue& eb = single ? kFil : last ? pol.ends.lastEnd : pol.ends.end;
    const bool endFil = ea.order > 0 || eb.order > 0;
    // not justified: the lines say so (paint: data-ragged; the audit's right
    // edge skips them)
    line.ragged = pol.ends.rigidInterior || single;
    // (plan P3-08) the slot width is the one definition of the measure:
    // justification and centring slack come from it
    const double slackPx = suToPx(slot.width) - f.naturalPx;
    // the slack goes to the highest order of glue the line holds: a fill
    // (plan P2-16) first, then the ends' fil (the last line's \parfillskip,
    // a centred last line), then the interior glue — unless the preset keeps
    // it rigid — or the ends' finite stretch; a tight line shrinks its
    // interior glue whatever the preset (the breaker counted on it)
    const bool fil = f.fills > 0 && slackPx > 0;
    if (fil) line.fillPx = slackPx / f.fills;
    const bool stretchesInterior = !fil && !endFil && !pol.ends.rigidInterior;
    // a line without stretchable glue (all URL pieces / one unbreakable
    // token) cannot be justified — TeX's underfull box; it says ragged
    // rather than pretending (real-world-report.md)
    if (f.totalWeight <= 0 && stretchesInterior && slackPx != 0) line.noGlue = true;
    if (f.totalWeight > 0 && !fil) {
      double d = slackPx / f.totalWeight;  // per unit weight (v2 §8)
      if (!stretchesInterior && slackPx > 0) d = 0;
      // an Overfull line (a run wider than the measure, plan P0-12) is set
      // at the shrink limit and overflows; it never spreads unbounded
      // negative spacing over its glue
      if (line.overfull && slackPx < 0) {
        const double minD = -cfg.cost.shrinkThreshold * f.capacityPx / f.totalWeight;
        if (d < minD) d = minD;
      }
      line.wordDeltaPx = d;
      line.wordDeltaSu = (i32)std::llround(d * 64.0);
      if (f.anyCjkGap) {
        line.cjkDeltaPx = d * cfg.cjkJustifyK;
        line.cjkDeltaSu = (i32)std::llround(line.cjkDeltaPx * 64.0);
      }
    }
    // the start glue's share of the slack, in su of the natural width (the
    // right edge stays at the slot's end: the width shrinks by the shift)
    if (!fil && !stretchesInterior && slackPx > 0) {
      const Su slack = slot.width - suCeilPx(f.naturalPx);
      Su shift = 0;
      if (slack > 0) {
        if (endFil) shift = ea.order && eb.order ? slack / 2 : ea.order ? slack : 0;
        else if (ea.stretch + eb.stretch > 0) shift = (Su)((i64)slack * ea.stretch / (ea.stretch + eb.stretch));
      }
      line.left += shift;
      line.width -= shift;
    }
    // its separator (plan P3-07): the break's inside the stream, a forced
    // break's newline, the stream's own at its end
    if (!last)
      line.sep = (f.endsHyphen || !joinsSpace(s.h, r.ihi)) ? Sep::None : Sep::Space;
    else if (bp == s.nBlocks)
      line.sep = pol.endSep;
    // (plan P3-12) a page cut before it: its stream's own (the block's
    // boundary, set by the caller) for the first line, widows and orphans
    // inside
    if (made > 0 && (made < kOrphans || nLines - made < kWidows)) line.brk = PenTier::WidowOrphan;
    made++;
    Su advance = baseLeading;
    if (f.maxAsc + f.maxDesc > advance) advance = f.maxAsc + f.maxDesc;
    line.height = advance;
    // (plan P3-19; design T7 S11) the baseline the contract pins: a run's
    // line box is its content area (its face's content-height line-height),
    // so the line's baseline is its top plus its tallest ascent
    line.baseline = f.maxAsc;
    line.y = (Su)y;
    y += advance;
    out.push_back(line);
  }
  return y;
}

// The floats of the document flow (plan P3-08; design T6 "ParShape +
// ExclusionMap"): side-tagged boxes in flow-root coordinates — y from the
// document's top, x from the measure's start — each pushing text from its
// own side (a float in a list pushes from its indent). A paragraph's line i
// is shaped by every float its conservative band [yTop + i·minAdv,
// yTop + (i+1)·maxAdv) meets: whatever advances the lines realize, line i
// lies inside its band, so no line overlaps a float it was not shaped for —
// one pass, no re-break. Floats on both sides coexist and stacks may have
// any widths.
class ExclusionMap {
 public:
  struct Box {
    i64 y0, y1;
    Su x0, x1;
    bool start;    // on the start side: pushes text from the start
    Su gap = -1;   // (plan P3-15) its own gap to the text beside it (unset: the map's)
  };
  explicit ExclusionMap(Su gap) : gap_(gap) {}
  bool empty() const { return v_.empty(); }
  void add(const Box& b) { v_.push_back(b); }
  // [x0, x1) narrowed by the floats meeting [y0, y1) (`push`: more room a
  // start float leaves — a list marker's, beside it)
  void available(i64 y0, i64 y1, Su& x0, Su& x1, Su push = 0) const {
    for (const Box& f : v_) {
      if (f.y1 <= y0 || f.y0 >= y1) continue;
      const Su g = f.gap >= 0 ? f.gap : gap_;
      if (f.start) x0 = std::max(x0, f.x1 + g + push);
      else x1 = std::min(x1, f.x0 - g);
    }
  }
  // the first y at or below `y` clear of the floats that meet [x0, x1)
  // horizontally (the whole box, D-Y02)
  i64 clearY(i64 y, Su x0, Su x1) const {
    i64 c = y;
    for (const Box& f : v_)
      if (f.y1 > y && f.x1 > x0 && f.x0 < x1) c = std::max(c, f.y1);
    return c;
  }
  // the lowest float bottom below y (y: none)
  i64 bottom(i64 y) const {
    i64 c = y;
    for (const Box& f : v_) c = std::max(c, f.y1);
    return c;
  }
  // a paragraph's shape in the content box [x0, x1) from yTop
  ParShape shape(i64 yTop, Su x0, Su x1, Su minAdv, Su maxAdv, Su push) const {
    ParShape ps(x1 - x0);
    const i64 last = bottom(yTop);
    for (i64 i = 0; yTop + i * minAdv < last; i++) {
      Su a = x0, b = x1;
      available(yTop + i * minAdv, yTop + (i + 1) * maxAdv, a, b, push);
      ps.lines.push_back(LineSlot{a - x0, b > a ? b - a : 0});
    }
    while (!ps.lines.empty() && ps.lines.back().left == 0 && ps.lines.back().width == ps.rest.width)
      ps.lines.pop_back();
    return ps;
  }
  // where a float box of height h, spanning [x0, x1), may stand from y: below
  // a float of its side it would overlap (a stack) and below one of the
  // other side when the column left between them is narrower than minWrap
  i64 place(i64 y, i64 h, bool start, Su x0, Su x1, Su minWrap, Su vgap) const {
    for (bool moved = true; moved;) {
      moved = false;
      for (const Box& f : v_) {
        if (f.y1 <= y || f.y0 >= y + h) continue;
        const bool blocks = f.start == start ? (f.x1 > x0 && f.x0 < x1)
                                             : (start ? f.x0 - gap_ - (x1 + gap_) : x0 - gap_ - (f.x1 + gap_)) < minWrap;
        if (blocks) {
          y = f.y1 + vgap;
          moved = true;
        }
      }
    }
    return y;
  }

 private:
  Su gap_;  // between a float and the text beside it
  std::vector<Box> v_;
};

// the gutter's separator, in the marker's em (the CSS .tsr-marker padding)
constexpr double kMarkerSepEm = 0.55;

// the tallest line a stream can make: the conservative band's maxAdv
Su streamAdvance(const HList& h, const MetricStore& metrics, Su baseLeading) {
  Su asc = 0, desc = 0;
  for (const HItem& it : h.items) {
    if (it.k == IK::Penalty) continue;
    const StyleId st = h.runs[it.run].face;
    if (metrics.hasVmet(st)) {
      const VMet& v = metrics.vmet(st);
      asc = std::max(asc, v.ascent);
      desc = std::max(desc, v.descent);
    }
    if (const ObjPart* pt = objectPart(h, it)) {
      asc = std::max(asc, pt->asc);
      desc = std::max(desc, pt->desc);
    }
  }
  return std::max(baseLeading, (Su)(asc + desc));
}

// The layouters (plan P1-18; design T6 "layouter registry"): one per
// LayouterId, chosen by the box tree by content model. A Stack lays its
// children out one below the other with its gap between them (the
// effective gap of the deepest stack holding both: a list packs everything
// inside it a third of a paragraph gap apart); the leaves' layouters turn
// their content into fragments at the cursor, clearing or narrowing beside
// the floats of the ExclusionMap.
class DocLayout {
 public:
  DocLayout(const MetricStore& m, Interner& s, const LayoutSettings& c, DiagSink& d, LayoutResult& r, BreakMemo* memo,
            bool paged, BoxAsker* boxes)
      : metrics(m), strs(s), cfg(c), diags(d), lr(r), memo_(memo), boxes_(boxes), paged_(paged),
        measure(suFloorPx(c.widthPx)),
        baseLeading(suRoundPx(c.lineHeight * c.baseSizePx)), paraGap(suRoundPx(c.paraSpacingEm * c.baseSizePx)),
        minWrap(suRoundPx(c.minWrapWidthEm * c.baseSizePx)), em(suRoundPx(c.baseSizePx)), excl(suRoundPx(c.baseSizePx)) {
    bparams.cost = c.cost;
    ctx = Ctx{0, measure, c.widthPx, &excl, nullptr, false};
  }

  void run(const std::vector<TopBlock>& tops) {
    i64 y = 0;
    bool gap = false;  // a gap before the next top: an in-flow one came before
    Su prevAfter = 0;  // the space the top before asks below it
    for (size_t p = 0; p < tops.size(); p++) {
      tb = &tops[p];
      tree = tb->tree;
      ParaFrame frame;
      frame.pid = tb->pid;
      frame.w = measure;
      // the document's stack: a paragraph gap between tops — none after a
      // top that is only out of flow (a float: zero advance, plan P3-08) —
      // or the space either side asks for (plan P3-14)
      Su topGap = gap ? std::max({paraGap, prevAfter, spaceBefore(0)}) : 0;
      if (tree->blocks.size() > 1 && tree->blocks[1].tr.cont) topGap = 0;  // (plan P3-17) a continuation
      if (!shows(tree->blocks[0]) || (tree->blocks.size() > 1 && !shows(tree->blocks[1]))) topGap = 0;
      gapBefore = topGap;
      y += topGap;
      frame.y = (Su)y;
      fr = &frame;
      py = 0;
      block(0);
      frame.h = (Su)py;
      y += py;
      bool outOnly = !frame.vlist.empty() || frame.lines.empty();
      for (const VEntry& e : frame.vlist) outOnly = outOnly && e.out;
      if (!outOnly) {
        gap = true;
        prevAfter = spaceAfter(0);
      } else {
        y -= topGap;  // the float took no room: the next top stands where it would have
      }
      lr.paras.push_back(std::move(frame));
    }
    if (floatBottomAbs > y) y = floatBottomAbs;  // a trailing float still shows
    lr.docHeightSu = y;
  }

 private:
  const MetricStore& metrics;
  Interner& strs;
  LayoutSettings cfg;
  DiagSink& diags;
  LayoutResult& lr;
  BreakMemo* memo_;  // the Session's KP memo (plan P1-21), or none
  BoxAsker* boxes_;  // (plan P3-28) host boxes at their widths, or none (declared sizes)
  const bool paged_;  // (plan P3-14) laying out for paged sheets (media)
  const Su measure, baseLeading, paraGap;
  const Su minWrap;  // layout.minWrapWidth: narrower beside floats, text clears them
  // (plan P3-10; design T6 flow roots) the container the layouters fill:
  // the document's measure, or a table cell's content box — a flow root of
  // its own (the document's floats stay outside), whose leaves keep no
  // vertical-list entries and whose paragraphs take the column's halign
  struct Ctx {
    Su x0 = 0, width = 0;
    double widthPx = 0;  // its width in px (image sizing)
    ExclusionMap* excl = nullptr;
    const LineEnds* halign = nullptr;
    bool cell = false;
  };
  Ctx ctx;
  // a block's content box: inside its ancestors' indents and boxes (x at the
  // start, xr at the end) and its own box (plan P3-14)
  Su left(const LayoutBlock& b) const { return ctx.x0 + b.x + b.box.inset(3); }
  Su width(const LayoutBlock& b) const { return ctx.width - b.x - b.xr - b.box.inset(3) - b.box.inset(1); }
  double widthPx(const LayoutBlock& b) const {
    return ctx.widthPx - suToPx(b.x + b.xr + b.box.inset(3) + b.box.inset(1));
  }
  const Su em;       // the ragged presets' end stretch unit (D-Y01)
  // a block's line-end preset (plan P3-09): its par.align
  LineEnds endsOf(const BlockTraits& tr) const {
    using A = BlockTraits::Align;
    using P = LineEnds::Preset;
    return LineEnds::preset(tr.align == A::Ragged   ? P::Left
                            : tr.align == A::Center ? P::Center
                            : tr.align == A::End    ? P::Right
                                                    : P::Justify,
                            em);
  }
  LineEnds leftEnds() const { return LineEnds::preset(LineEnds::Preset::Left, em); }
  ExclusionMap excl;
  BreakParams bparams;
  i64 floatBottomAbs = 0;  // doc-height watermark for a trailing float (F2)
  // the top being laid out
  const TopBlock* tb = nullptr;
  const TopTree* tree = nullptr;
  ParaFrame* fr = nullptr;
  i64 py = 0;                           // the cursor, from the frame's top
  Su gapBefore = 0;                     // the gap before the next leaf (vlist)
  std::vector<BreakResult> cellBreaks;  // the current leaf's other tracks

  using Fn = void (DocLayout::*)(const LayoutBlock&);
  static const Fn kLayouters[];  // the registry: one per LayouterId
  // a block by its layouter, with the trait group around it (plan P3-14):
  // the medium it shows in, a page break before / after it, its frame,
  // narrowing beside a float, keeping its fragments together, keeping a
  // container with what follows
  bool forceNext = false;  // a page break before the next fragment laid out
  void block(u32 i) {
    const LayoutBlock& b = tree->blocks[i];
    if (b.tr.media && b.tr.media != (paged_ ? 2 : 1)) return;  // not in this medium
    // (plan P3-15) a block floated to a side (not an image float: its own
    // layouter's) goes out of flow, laid out detached
    using P = BlockTraits::Place;
    if ((b.tr.place == P::Start || b.tr.place == P::End) && !b.floatSide && i != detaching_) {
      sideFloat(i);
      return;
    }
    if ((b.tr.place == P::Top || b.tr.place == P::Bottom || b.tr.place == P::Page) && i != detaching_ && !ctx.cell) {
      pageFloat(i);
      return;
    }
    const bool hooks = forceNext || b.tr.breakBefore || b.tr.breakAfter || b.tr.keepTogether || b.tr.shrink ||
                       b.box.framed() || (b.tr.keepWithNext && !b.leaf());
    if (!hooks) {
      (this->*kLayouters[(size_t)b.layouter])(b);
      return;
    }
    const bool force = forceNext || b.tr.breakBefore;
    forceNext = false;
    const size_t from = fr->lines.size();
    // a framed block: a flow root clear of the floats its box meets, its
    // frame under its content, its content inside its padding and border
    size_t frameAt = ~size_t(0);
    i64 frameTop = 0;
    if (b.box.framed()) {
      if (!ctx.excl->empty() && !ctx.cell) {
        const i64 at = (i64)fr->y + py;
        const i64 c = ctx.excl->clearY(at, ctx.x0 + b.x, ctx.x0 + ctx.width - b.xr);
        if (c > at) py += c - at + gapBefore;
      }
      frameTop = py;
      Fragment f;
      f.kind = FragKind::Frame;
      f.unitIdx = firstUnit(i);
      f.boxBlock = i;
      f.y = (Su)py;
      f.left = ctx.x0 + b.x;
      f.width = ctx.width - b.x - b.xr;
      f.paged = kPagedFrame;
      f.srcSpan = b.span;
      frameAt = fr->lines.size();
      fr->lines.push_back(f);
      py += b.box.inset(0);
    }
    // beside a float, narrowed to the room its top line leaves (D-Y02:
    // only when it says so; a paragraph narrows anyway)
    const Ctx saved = ctx;
    if (b.tr.shrink && !ctx.excl->empty() && b.layouter != LayouterId::Paragraph) {
      Su a = ctx.x0 + b.x, c = ctx.x0 + ctx.width - b.xr;
      const i64 at = (i64)fr->y + py;
      ctx.excl->available(at, at + baseLeading, a, c);
      if (c - a >= std::min(minWrap, ctx.width)) {
        ctx.width -= (a - (ctx.x0 + b.x)) + ((ctx.x0 + ctx.width - b.xr) - c);
        ctx.x0 = a - b.x;
        ctx.widthPx = suToPx(ctx.width);
        shrinking = true;
      }
    }
    (this->*kLayouters[(size_t)b.layouter])(b);
    shrinking = false;
    ctx = saved;
    if (frameAt != ~size_t(0)) {
      py += b.box.inset(2);
      fr->lines[frameAt].height = (Su)(py - frameTop);
    }
    // its first fragment of content (not its frame)
    size_t first = from;
    while (first < fr->lines.size() && fr->lines[first].kind == FragKind::Frame) first++;
    if (force) {
      if (first < fr->lines.size()) fr->lines[first].brk = PenTier::Forced;
      else forceNext = true;  // nothing laid out: the break waits for what follows
    }
    if (b.tr.keepTogether)
      for (size_t q = first + 1; q < fr->lines.size(); q++)
        if (fr->lines[q].brk < PenTier::KeepTogether) fr->lines[q].brk = PenTier::KeepTogether;
    if (b.tr.keepWithNext && !b.leaf()) keepNext = true;
    if (b.tr.breakAfter) forceNext = true;
  }
  // (plan P3-15; design T6 layoutDetached) a block laid out on its own: a
  // flow root at width `w` (no floats from outside, none leaking out, no
  // vertical-list entries), its fragments taken out of the frame relative
  // to its box's top-left; its height
  u32 detaching_ = ~0u;  // the block laid out detached (its placement not applied again)
  struct Detached {
    std::vector<Fragment> frags;
    Su w = 0;
    i64 h = 0;
  };
  Detached layoutDetached(u32 i, Su w) {
    const LayoutBlock& b = tree->blocks[i];
    Detached d;
    d.w = w;
    const size_t from = fr->lines.size();
    ExclusionMap own(em);
    const Ctx saved = ctx;
    const i64 savedPy = py;
    const Su savedGap = gapBefore;
    const bool savedKeep = keepNext, savedForce = forceNext;
    const u32 savedDetaching = detaching_;
    // its box at x 0: its own indent (b.x, b.xr) cancelled
    ctx = Ctx{-b.x, w + b.x + b.xr, suToPx(w + b.x + b.xr), &own, nullptr, true};
    py = 0;
    gapBefore = 0;
    keepNext = false;
    forceNext = false;
    detaching_ = i;
    block(i);
    d.h = py;
    detaching_ = savedDetaching;
    ctx = saved;
    py = savedPy;
    gapBefore = savedGap;
    keepNext = savedKeep;
    forceNext = savedForce;
    d.frags.assign(fr->lines.begin() + (long)from, fr->lines.end());
    fr->lines.resize(from);
    return d;
  }
  // a detached box placed at (x, frame-relative y): one atom on paged sheets
  void place(Detached& d, Su x, i64 y) {
    for (size_t q = 0; q < d.frags.size(); q++) {
      Fragment f = d.frags[q];
      f.left += x;
      f.y += (Su)y;
      if (q > 0) f.brk = PenTier::Structural;
      f.paged &= (u8)~kPagedFrame;  // (an atom: its frames go with it)
      fr->lines.push_back(f);
    }
  }
  // (plan P3-15) a placed block's width: its declared length or fraction of
  // the room, else its content's (FitBody: its non-caption content's
  // max-content) — never more than the room
  Su placedWidth(u32 i, Su room) const {
    const BlockTraits& tr = tree->blocks[i].tr;
    Su w = tr.placeW ? tr.placeW : tr.placeFrac > 0 ? (Su)((double)room * tr.placeFrac) : 0;
    if (!w) {
      Su mn = 0, mx = 0;
      intrinsic(i, mn, mx, /*captions=*/false);
      w = std::max(mx, mn);
      const LayoutBlock& b = tree->blocks[i];
      w += b.box.inset(1) + b.box.inset(3);
    }
    return std::clamp<Su>(w, std::min<Su>(kRailMinLineSu, room), room);
  }
  // a block floated to the start or end side: laid out detached, placed at
  // the cursor beside the floats already there (ExclusionMap::place, as an
  // image float), the text beside it narrowed by its exclusion
  void sideFloat(u32 i) {
    const LayoutBlock& b = tree->blocks[i];
    Leaf l = enter(false, b);
    const Su room = ctx.width - b.x - b.xr;
    const Su w = placedWidth(i, room);
    Detached d = layoutDetached(i, w);
    const bool start = b.tr.place == BlockTraits::Place::Start;
    const Su boxLeft = start ? ctx.x0 + b.x : ctx.x0 + ctx.width - b.xr - w;
    const Su gap = b.tr.placeGap >= 0 ? b.tr.placeGap : em;
    const i64 top = ctx.excl->place((i64)fr->y + py, d.h, start, boxLeft, boxLeft + w, minWrap, paraGap);
    place(d, boxLeft, top - fr->y);
    ctx.excl->add({top, top + d.h, boxLeft, boxLeft + w, start, gap});
    if (top + d.h > floatBottomAbs) floatBottomAbs = top + d.h;
    // its box in the vertical list, out of flow
    if (!ctx.cell) fr->vlist.push_back({b.leaf() ? b.unit : firstUnit(i), gapBefore, l.clear, (Su)(top - fr->y), (Su)d.h, true});
    gapBefore = 0;
    if (l.from < fr->lines.size()) fr->lines[l.from].brk = PenTier::Normal;
  }
  // a page float: in place on screen; on paged sheets one movable box to
  // the top or bottom of its sheet, or to a sheet of floats
  void pageFloat(u32 i) {
    const LayoutBlock& b = tree->blocks[i];
    const size_t from = fr->lines.size();
    const u32 saved = detaching_;
    detaching_ = i;
    block(i);
    detaching_ = saved;
    const u8 role = kPagedMovable | (b.tr.place == BlockTraits::Place::Bottom ? kPagedBottom
                                     : b.tr.place == BlockTraits::Place::Page ? kPagedPage
                                                                               : 0);
    for (size_t q = from; q < fr->lines.size(); q++) {
      fr->lines[q].paged = (u8)((fr->lines[q].paged & ~kPagedFrame) | role);  // (its frames move with it)
      if (q > from) fr->lines[q].brk = PenTier::Structural;
    }
  }
  // (plan P3-15) consecutive inline blocks of a stack: each laid out
  // detached at its width, set side by side a gap apart, a line full when
  // the next does not fit; lines centred (or as the stack's par.align says),
  // their boxes top-aligned, the stack's gap between lines; returns past the
  // run
  u32 inlineRun(const LayoutBlock& parent, u32 k, Su gapV) {
    const u32 self = (u32)(&parent - tree->blocks.data());
    std::vector<u32> run;
    for (u32 c = k; c < parent.end && tree->blocks[c].tr.place == BlockTraits::Place::Inline; c = tree->blocks[c].end)
      if (shows(tree->blocks[c])) run.push_back(c);
    u32 next = k;
    for (u32 c = k; c < parent.end && tree->blocks[c].tr.place == BlockTraits::Place::Inline; c = tree->blocks[c].end)
      next = tree->blocks[c].end;
    if (run.empty()) return next;
    const Su x0 = left(parent), room = width(parent);
    std::vector<Detached> boxes;
    std::vector<Su> gaps;
    for (u32 c : run) {
      boxes.push_back(layoutDetached(c, placedWidth(c, room)));
      gaps.push_back(tree->blocks[c].tr.placeGap >= 0 ? tree->blocks[c].tr.placeGap : em);
    }
    const BlockTraits::Align al = parent.tr.align;
    size_t a = 0;
    bool firstLine = true;
    while (a < boxes.size()) {
      size_t z = a + 1;
      Su used = boxes[a].w;
      for (; z < boxes.size() && used + gaps[z - 1] + boxes[z].w <= room; z++) used += gaps[z - 1] + boxes[z].w;
      if (!firstLine) py += gapV;
      firstLine = false;
      Su x = al == BlockTraits::Align::Ragged ? 0 : al == BlockTraits::Align::End ? room - used : (room - used) / 2;
      if (x < 0) x = 0;
      i64 h = 0;
      const size_t lineFrom = fr->lines.size();
      for (size_t q = a; q < z; q++) {
        place(boxes[q], x0 + x, py);
        x += boxes[q].w + gaps[q];
        h = std::max(h, boxes[q].h);
      }
      for (size_t q = lineFrom + 1; q < fr->lines.size(); q++) fr->lines[q].brk = PenTier::Structural;  // a line: one atom
      if (!ctx.cell) fr->vlist.push_back({firstUnit(run[a]), gapBefore, 0, (Su)py, (Su)h, false, self});
      gapBefore = 0;
      py += h;
      a = z;
    }
    return next;
  }
  // a valid unit for a container's own fragments: its first leaf's
  u32 firstUnit(u32 i) const {
    for (u32 k = i; k < tree->blocks[i].end; k++)
      if (tree->blocks[k].leaf()) return tree->blocks[k].unit;
    return 0;
  }
  bool shrinking = false;  // (a block narrowed beside a float lays out there: no clearing)

  // Layout breaks its paragraphs (plan P1-15) with the cached KP (break.cc:
  // keyed by exactly the DP inputs, shared across documents — the editing
  // loop's fast path). A run wider than the line is set Overfull on a line
  // of its own (the final-pass rescue) and reported once per stream.
  // (plan P3-09) a stream breaks with the line-end glue it will be set with
  BreakResult breakStream(const std::vector<BreakBlock>& blocks, const HList& h, const ParShape& shape,
                          const LineEnds& ends, const BlockTraits* tr = nullptr) {
    BreakParams params = bparams;
    params.ends = ends;
    if (tr) {  // (plan P3-14) its breaker: a tolerance pass, an emergency stretch
      params.tolerance = tr->tolerance;
      params.emergencyStretch = tr->emergencyStretch;
    }
    BreakResult r = breakLinesCached(blocks, shape, params, memo_);
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
  }
  // a leaf starts: an in-flow box that is not a paragraph clears the floats
  // its whole box meets (D-Y02), standing its gap below them
  struct Leaf {
    size_t from;  // its first fragment
    Su clear;
    i64 top;
  };
  Leaf enter(bool clears, const LayoutBlock& b) {
    Leaf l{fr->lines.size(), 0, 0};
    if (clears && !shrinking) l.clear = clearance(b);
    py += l.clear;
    l.top = py;
    cellBreaks.clear();
    return l;
  }
  // how far down a block at the cursor moves to clear the floats its box
  // meets: to their bottom, plus the gap it stands below what precedes it
  Su clearance(const LayoutBlock& b) const {
    const i64 at = (i64)fr->y + py;
    const i64 c = ctx.excl->clearY(at, left(b), ctx.x0 + ctx.width);
    return c > at ? (Su)(c + gapBefore - at) : 0;
  }
  // a leaf ends: its anchor on its first fragment (a table's on its first
  // cell line, not its rule), and its box in the vertical list
  void leave(const LayoutBlock& b, const Leaf& l, bool out = false) {
    if (b.carry && l.from < fr->lines.size()) {
      size_t at = l.from;
      for (size_t k = l.from; k < fr->lines.size(); k++)
        if (fr->lines[k].kind != FragKind::Rule) {
          at = k;
          break;
        }
      fr->lines[at].anchor = b.carry;  // (a paragraph's first line has it already)
      fr->lines[at].anchor2 = b.carry2;
    }
    if (!ctx.cell) fr->vlist.push_back({b.unit, gapBefore, l.clear, (Su)l.top, (Su)(py - l.top), out});
    gapBefore = 0;
    boundary(l.from, b.tr.keepWithNext, out);
    insertRole(b, l.from);
  }
  // (plan P3-13) a deferred flow's entry (its nearest block that says so):
  // its fragments are inserts of its marker's sheet; the rest of the flow's
  // wrap (a rule) the separator above a sheet's inserts
  void insertRole(const LayoutBlock& b, size_t from) {
    u32 at = kNotInsert;
    for (u32 k = (u32)(&b - tree->blocks.data()); k != ~0u && at == kNotInsert; k = tree->blocks[k].parent)
      at = tree->blocks[k].insertAt;
    if (at == kNotInsert) return;
    for (size_t q = from; q < fr->lines.size(); q++) {
      fr->lines[q].paged |= at == kInsertArea ? kPagedInsert | kPagedHeader : kPagedInsert;
      if (at != kInsertArea) fr->lines[q].insertAt = at;
    }
  }
  // (plan P3-12) a block's fragments [from, end) in the vertical list: a
  // page cut before the first is its boundary's — kept with a block before
  // it that keeps with what follows (a heading); a float box is one atom
  // that leaves the flow's keeps alone; inside a table cell the table
  // decides
  bool keepNext = false;
  void boundary(size_t from, bool keepsWithNext, bool atom) {
    if (ctx.cell || from >= fr->lines.size()) return;
    if (atom) {
      for (size_t q = from + 1; q < fr->lines.size(); q++) fr->lines[q].brk = PenTier::Structural;
      return;
    }
    if (keepNext && fr->lines[from].brk < PenTier::KeepWithNext) fr->lines[from].brk = PenTier::KeepWithNext;
    keepNext = keepsWithNext;
  }

  // (plan P3-14; design T6 vertical algebra) the space a block asks above /
  // below it, collapsing through an unframed container's first / last child
  Su spaceBefore(u32 k) const {
    const LayoutBlock& b = tree->blocks[k];
    Su s = b.tr.spaceBefore;
    if (!b.leaf() && !b.box.framed() && k + 1 < b.end) s = std::max(s, spaceBefore(k + 1));
    return s;
  }
  Su spaceAfter(u32 k) const {
    const LayoutBlock& b = tree->blocks[k];
    Su s = b.tr.spaceAfter;
    if (!b.leaf() && !b.box.framed() && k + 1 < b.end) {
      u32 last = k + 1;
      for (u32 c = k + 1; c < b.end; c = tree->blocks[c].end) last = c;
      s = std::max(s, spaceAfter(last));
    }
    return s;
  }
  bool shows(const LayoutBlock& b) const { return !b.tr.media || b.tr.media == (paged_ ? 2 : 1); }

  void stack(const LayoutBlock& b) {
    u8 num, den;
    Su su;
    gapOf(*tree, (u32)(&b - tree->blocks.data()), num, den, su);
    const Su gap = den ? (Su)((i64)paraGap * num / den) : su;
    const u32 self = (u32)(&b - tree->blocks.data());
    bool prevOut = false;  // the child before was only out of flow (a float)
    u32 prev = ~0u;        // the child before, in flow
    for (u32 k = self + 1; k < b.end;) {
      const LayoutBlock& kb = tree->blocks[k];
      if (!shows(kb)) {  // (plan P3-14) not in this medium: no gap
        k = kb.end;
        continue;
      }
      if (prev != ~0u && !prevOut) {
        // the gap, or more when either side asks for more space; none
        // before a paragraph's continuation (plan P3-17)
        const Su g = kb.tr.cont ? 0 : std::max({gap, spaceAfter(prev), spaceBefore(k)});
        py += g;
        gapBefore = g;
      }
      const size_t v0 = fr->vlist.size();
      u32 next = kb.end;
      if (kb.tr.place == BlockTraits::Place::Inline) next = inlineRun(b, k, gap);  // (plan P3-15) a line of boxes
      else block(k);
      prevOut = fr->vlist.size() > v0;
      for (size_t e = v0; e < fr->vlist.size(); e++) prevOut = prevOut && fr->vlist[e].out;
      if (prevOut) gapBefore = prev != ~0u ? gap : gapBefore;  // what follows takes the float's gap
      if (!prevOut) prev = k;
      k = next;
    }
  }

  void paragraph(const LayoutBlock& b) {
    const FlowUnit& u = tb->units[b.unit];
    Leaf l = enter(false, b);
    const Su lineWidth = width(b);
    // (plan P3-08) its shape beside the floats its lines' bands meet; a
    // list marker's line keeps its marker's room beside a start float; a
    // column narrower than minWrapWidth: the paragraph clears the floats
    ParShape shape(lineWidth);
    if (!ctx.excl->empty()) {
      const Su adv = streamAdvance(u.hl, metrics, baseLeading);
      const Su push = b.marker && b.parent != ~0u && tree->blocks[b.parent].parent != ~0u
                          ? tree->blocks[tree->blocks[b.parent].parent].pad
                          : 0;
      for (int tries = 0; tries < 64; tries++) {
        shape = ctx.excl->shape((i64)fr->y + py, left(b), ctx.x0 + ctx.width, baseLeading, adv, push);
        bool narrow = false;
        for (const LineSlot& s : shape.lines) narrow = narrow || s.width < std::min(minWrap, lineWidth);
        if (!narrow) break;
        const Su c = clearance(b);
        if (c <= 0) break;
        py += c;
        l.clear += c;
        l.top = py;
      }
    }
    // (plan P3-14) a hanging indent: the lines after the first hangAfter
    // start `hang` in (beside a float as anywhere: the same slots)
    if (b.tr.hang > 0) {
      const u32 n = std::max<u32>((u32)shape.lines.size(), b.tr.hangAfter);
      for (u32 i = 0; i < n; i++) {
        const LineSlot s0 = shape.at(i);
        if (i >= shape.lines.size()) shape.lines.push_back(s0);
        if (i >= b.tr.hangAfter) {
          const Su h = std::min(b.tr.hang, shape.lines[i].width);
          shape.lines[i].left += h;
          shape.lines[i].width -= h;
        }
      }
      const Su h = std::min(b.tr.hang, shape.rest.width);
      shape.rest.left += h;
      shape.rest.width -= h;
    }
    LinePolicy pol;
    pol.ends = ctx.halign ? *ctx.halign : endsOf(b.tr);  // (in a table cell: its column's halign)
    pol.singleCenter = b.tr.singleCenter;
    lr.breaks.push_back({tb->pid, b.unit, -1, breakStream(u.blocks, u.hl, shape, pol.ends, &b.tr)});
    pol.endSep = b.sepAfter;
    pol.marker = b.marker;
    pol.markerStyle = b.markerStyle;
    pol.anchor = b.carry ? b.carry : u.anchor;  // a block's label, else an inline one
    py = materializeLines({u.hl, u.blockStart, (u32)u.blocks.size(), lr.breaks.back().r, shape, left(b), b.unit, -1},
                          pol, metrics, cfg, baseLeading, py, fr->lines);
    leave(b, l);
  }

  // (plan P3-32; design T9 M12) an image's size spec at this layout: a
  // Provided one takes the host's intrinsic size (boxInfo at width 0; the
  // author's one dim keeps the aspect ratio). Unanswered — pending (filed:
  // this layout is provisional) or failed — it is the placeholder
  IntrinsicSize imageSize(const ImageData& im, Span span) const {
    IntrinsicSize s = im.size;
    if (s.source != SizeSource::Provided) return s;
    const BoxAnswer a = boxes_ && im.src ? boxes_->ask(BoxKind::Image, im.src, 0, span) : BoxAnswer{};
    if (a.ready) intrinsicDims(s.w, s.h, a.w, a.h);
    else s.source = SizeSource::Placeholder;
    return s;
  }

  void replaced(const LayoutBlock& b) {
    const FlowUnit& u = tb->units[b.unit];
    if (b.painter == Painter::Image && b.floatSide) {
      floatBox(b, u);
      return;
    }
    Leaf l = enter(true, b);
    const Su lineWidth = width(b);
    Fragment f;
    f.unitIdx = b.unit;
    f.y = (Su)py;
    f.left = left(b);
    f.width = lineWidth;
    switch (b.painter) {
      case Painter::Rule:
        f.kind = FragKind::Rule;
        f.height = baseLeading;  // its band: the rule at its middle
        break;
      case Painter::Raw: {
        // (plan P3-28; design T6 S14) a host box: its height the host's at
        // the box's width (unanswered: the declared one, and this layout
        // is provisional)
        const RawData& r = std::get<RawData>(u.data);
        f.kind = FragKind::Raw;
        f.hostBox = r.size.source == SizeSource::Host;
        double h = r.size.h;
        if (f.hostBox && boxes_ && r.html)
          if (const BoxAnswer a = boxes_->ask(r.kind, r.html, widthPx(b), b.span); a.ready) h = a.h;
        f.height = suRoundPx(h);
        break;
      }
      case Painter::Image: {
        // block figure image (figure-design.md §3): centred on the measure,
        // advance = display height (float placement is F2)
        Su imgW = 0, imgH = 0;
        const IntrinsicSize size = imageSize(std::get<ImageData>(u.data), b.span);
        resolveImageSize(size, widthPx(b), imgW, imgH);
        f.kind = FragKind::Image;
        f.placeholder = size.placeholder();
        Su shift = (lineWidth - imgW) / 2;
        if (shift < 0) shift = 0;
        f.left = left(b) + shift;
        f.width = imgW;
        f.height = imgH;
        f.srcSpan = b.span;
        break;
      }
      case Painter::MathRow: {
        // display formula: centred on the measure, advance = box extents
        const MathData& md = std::get<MathData>(u.data);
        const MathBox* mb = md.box;
        if (!mb) {
          leave(b, l);
          return;
        }
        f.kind = FragKind::Math;
        f.srcSpan = b.span;
        auto row = [&](Fragment& rf, const MathBox* rb) {
          Su shift = (lineWidth - rb->w) / 2;
          if (shift < 0) shift = 0;
          rf.left = left(b) + shift;
          rf.width = rb->w;
          rf.height = std::max(rb->asc + rb->desc, baseLeading);
          rf.baseline = (rf.height - (rb->asc + rb->desc)) / 2 + rb->asc;
        };
        // (plan P3-29; design T6 S15) its rows, a jot (0.3em) apart, one
        // fragment each — a page may break between them; its number on the
        // last
        for (size_t r = 0; r + 1 < md.rows.size(); r++) {
          Fragment rf = f;
          row(rf, md.rows[r]);
          rf.mathRow = (u16)r;
          rf.sep = Sep::Newline;
          fr->lines.push_back(rf);
          py += rf.height + suRoundPx(0.3 * md.sizePx);
        }
        if (!md.rows.empty()) {
          f.y = (Su)py;
          f.mathRow = (u16)(md.rows.size() - 1);
          mb = md.rows.back();
        }
        row(f, mb);
        f.sep = b.sepAfter;  // a formula is copied (its source) like a paragraph
        if (u.cells.empty()) break;
        // (plan P3-26; design T6 display rows) its number, measured: a line
        // at the measure's end on the formula's baseline — or, when the two
        // would come closer than an em, below it, still at the end (TeX's
        // \eqno on a line of its own); never parted from it by a page cut
        {
          BlockTraits tagTr;
          tagTr.align = BlockTraits::Align::End;
          tagTr.hyphenate = false;
          const LineEnds ends = endsOf(tagTr);
          const ParShape shape(lineWidth);
          const Flow& cell = u.cells[0];
          cellBreaks.push_back(breakStream(cell.blocks, cell.hl, shape, ends));
          lr.breaks.push_back({tb->pid, b.unit, (i32)cellBreaks.size() - 1, cellBreaks.back()});
          LinePolicy pol;
          pol.ends = ends;
          std::vector<Fragment> tag;
          const i64 tagH = materializeLines({cell.hl, cell.blockStart, (u32)cell.blocks.size(), cellBreaks.back(), shape,
                                             left(b), b.unit, 0},
                                            pol, metrics, cfg, baseLeading, 0, tag);
          fr->lines.push_back(f);
          if (tag.empty()) {
            py += f.height;
            leave(b, l);
            return;
          }
          const bool beside = (i64)f.left + f.width + em <= (i64)tag[0].left;
          const i64 top = beside ? (i64)f.y + f.baseline - tag[0].baseline : (i64)f.y + f.height;
          for (Fragment& t : tag) {
            t.y = (Su)(top + t.y);
            t.brk = PenTier::Structural;
            fr->lines.push_back(t);
          }
          // (beside it, the number's line overhangs the row with its
          // leading: only a number below the formula advances the cursor)
          py = beside ? (i64)f.y + f.height : top + tagH;
          leave(b, l);
          return;
        }
      }
      case Painter::None:
        leave(b, l);
        return;
    }
    if (f.kind == FragKind::Image || f.kind == FragKind::Raw) f.baseline = f.height;  // on its bottom
    if (f.kind == FragKind::Rule) f.baseline = f.height / 2;
    py += f.height;
    fr->lines.push_back(f);
    leave(b, l);
  }

  // float box (figure-design.md §4): out of flow — zero advance; the image
  // at the measure's edge, caption rows beneath at the float width; the
  // blocks that flow beside it narrow by the exclusion
  void floatBox(const LayoutBlock& b, const FlowUnit& u) {
    Leaf l = enter(false, b);
    const Su lineWidth = width(b);
    Su imgW = 0, imgH = 0;
    const IntrinsicSize size = imageSize(std::get<ImageData>(u.data), b.span);
    resolveImageSize(size, widthPx(b), imgW, imgH);
    i64 captionH = 0;
    // its caption rows: aligned as caption paragraphs say (D-Y05: as a
    // block figure's)
    const LineEnds capEnds = endsOf(b.rowTr);
    for (const Flow& c : u.cells) {  // the caption breaks to the float width
      cellBreaks.push_back(breakStream(c.blocks, c.hl, ParShape(imgW), capEnds));
      lr.breaks.push_back({tb->pid, b.unit, (i32)cellBreaks.size() - 1, cellBreaks.back()});
      captionH += (i64)cellBreaks.back().breakpoints.size() * baseLeading;
    }
    // (plan P3-08) at the cursor, beside the floats already there: below one
    // of its side it would overlap, below one of the other side when the
    // column between them would be narrower than minWrapWidth
    const bool start = b.floatSide == 1;
    const Su boxLeft = start ? left(b) : left(b) + lineWidth - imgW;
    const i64 top = ctx.excl->place((i64)fr->y + py, (i64)imgH + captionH, start, boxLeft, boxLeft + imgW, minWrap,
                               paraGap);
    Fragment f;
    f.unitIdx = b.unit;
    f.kind = FragKind::Image;
    f.placeholder = size.placeholder();
    f.left = boxLeft;
    f.width = imgW;
    f.height = imgH;
    f.baseline = imgH;
    f.srcSpan = b.span;
    f.y = (Su)(top - fr->y);
    fr->lines.push_back(f);
    i64 cy = top - fr->y + imgH;
    for (u32 ci = 0; ci < (u32)u.cells.size(); ci++) {
      // caption rows: left-aligned at the float width; wrapped rows rejoin
      // on copy (§9.3), each caption paragraph ends a line, the last the unit
      const Flow& cell = u.cells[ci];
      LinePolicy pol;
      pol.ends = capEnds;
      pol.singleCenter = b.rowTr.singleCenter;
      pol.endSep = ci + 1 < (u32)u.cells.size() ? Sep::Newline : b.sepAfter;
      pol.anchor = cell.anchor;
      const ParShape shape(imgW);
      cy = materializeLines({cell.hl, cell.blockStart, (u32)cell.blocks.size(), cellBreaks[ci], shape, boxLeft,
                             b.unit, (i32)ci},
                            pol, metrics, cfg, baseLeading, cy, fr->lines);
    }
    ctx.excl->add({top, (i64)fr->y + cy, boxLeft, boxLeft + imgW, start});
    if ((i64)fr->y + cy > floatBottomAbs) floatBottomAbs = (i64)fr->y + cy;
    leave(b, l, /*out=*/true);  // no advance: the float is out of flow
  }

  void grid(const LayoutBlock& b) {
    const FlowUnit& u = tb->units[b.unit];
    const GridData& g = std::get<GridData>(u.data);
    Leaf l = enter(true, b);
    const Su lineWidth = width(b);
    Su adv = baseLeading;
    Su rowBase = adv / 2;  // a row's baseline, centred (its line-height is the row)
    if (metrics.hasVmet(g.codeStyle)) {
      const VMet& v = metrics.vmet(g.codeStyle);
      if (v.ascent + v.descent > adv) adv = v.ascent + v.descent;
      rowBase = (adv - (v.ascent + v.descent)) / 2 + v.ascent;
    }
    // the code track: the whole measure (its sidecar notes, if any, are a
    // second track of the table it sits in: plan P3-11); the gutter stays
    // out of flow (markers)
    const Su lineWidthCode = lineWidth;
    // ch grid (CH4, code-design.md §4): monospace is a metric contract —
    // 1ch per char, 2ch for CJK; wrap is a COLUMN computation, greedy with
    // a break-character preference, continuation rows indent 2ch. The
    // alignment (snap-kerning, budget) does not depend on wrapping (plan
    // P3-11: wrap:false kept snap-kerning off).
    Su chSu = 0;
    if (g.chRef && metrics.hasWord(g.chRef, g.codeStyle)) chSu = metrics.word(g.chRef, g.codeStyle).su;
    // measured CJK width (verbatim-design §2): budget columns from the
    // real ratio, conservatively ceiled — no assumed 2:1
    i32 cjkCols = 2;
    if (chSu > 0 && g.cjkChRef && metrics.hasWord(g.cjkChRef, g.codeStyle)) {
      Su c = metrics.word(g.cjkChRef, g.codeStyle).su;
      cjkCols = (i32)((c + chSu - 1) / chSu);
      if (cjkCols < 1) cjkCols = 1;
    }
    i32 cols = g.wrap && chSu > 0 ? (i32)(lineWidthCode / chSu) : 0;
    const i32 minCols = cfg.verbatimMinCols;  // code.minCols
    if (cols > 0 && cols < minCols) cols = minCols;
    // snap-kerning (verbatim §3): solve the rational grid from RAW
    // measurements; column budget switches to atom units — Latin = q,
    // CJK = p atoms — with letter-spacing pulling advances onto it
    GridSpec grid;
    i32 latinAtoms = 1;
    if (b.tr.snapKerning && chSu > 0 && g.cjkChRef &&
        metrics.hasWord(g.chRef, g.codeStyle) &&
        metrics.hasWord(g.cjkChRef, g.codeStyle)) {
      double chLpx = metrics.word(g.chRef, g.codeStyle).px;
      double chCpx = metrics.word(g.cjkChRef, g.codeStyle).px;
      grid = solveGrid(chLpx, chCpx, cols, cfg.verbatimSnapMaxQ);
      const double tol = cfg.verbatimSnapTolerance;  // code.snapTolerance
      if (grid.atomPx > 0 && grid.dLatinPx <= tol * chLpx && grid.dCjkPx <= tol * chCpx) {
        Su atomSu = suCeilPx(grid.atomPx);
        latinAtoms = grid.q;
        cjkCols = grid.p;              // in atom units now
        cols = g.wrap ? (i32)(lineWidthCode / atomSu) : 0;  // the code column, not the measure
        if (cols > 0 && cols < minCols * grid.q) cols = minCols * grid.q;
      } else {
        grid = GridSpec{};             // budget-only fallback
      }
    }
    // (plan P3-11) its wrapping parameters: data (layout/grid.h)
    GridParams gp;
    gp.minCols = minCols;
    gp.contIndent = b.tr.contIndent;
    std::unordered_set<u32> hlSet(g.hlLines.begin(), g.hlLines.end());
    bool first = true;
    size_t lastRow = ~size_t(0);  // the block's last code row: its unit's separator
    for (u32 li = 0; li < (u32)g.lines.size(); li++) {
      std::string joined;
      std::vector<std::pair<u32, u32>> commentSpans;  // byte ranges
      for (const CodeRun& r : g.lines[li]) {
        u32 b0 = (u32)joined.size();
        joined.append(strs.get(r.text));
        if (r.hang) commentSpans.push_back({b0, (u32)joined.size()});
      }
      const std::vector<GridRow> rows = wrapGridLine(joined, commentSpans, cols, latinAtoms, cjkCols, gp);
      const u32 at = g.firstLine + li;  // the block's line (a table row shows one: plan P3-11)
      bool hl = hlSet.count(at + 1) != 0;
      // (plan P3-07) its source: a row is its slice of an exact line, else
      // the line as a whole
      const Span ls = li < g.lineSpans.size() ? g.lineSpans[li] : Span{};
      const bool exact = li < g.lineSpans.size() && ls.end - ls.start == joined.size();
      for (size_t ri = 0; ri < rows.size(); ri++) {
        Fragment line;
        line.unitIdx = b.unit;
        line.kind = FragKind::CodeRow;
        line.codeLine = at;
        line.cbLo = rows[ri].lo;
        line.cbHi = rows[ri].hi;
        line.codeCont = ri > 0;
        // (plan P3-12) a page never cuts a logical line; between lines it
        // keeps widows and orphans, counted in logical lines
        if (ri > 0) line.brk = PenTier::Structural;
        else if (li > 0 && (li < kOrphans || (u32)g.lines.size() - li < kWidows)) line.brk = PenTier::WidowOrphan;
        // a wrapped row rejoins its continuation (§9.3); a code line ends
        // with a newline, the block with its unit's separator (below)
        if (ri + 1 < rows.size()) line.sep = Sep::None;
        if (li < g.lineSpans.size()) {
          line.srcSpan = exact ? Span{ls.start + rows[ri].lo, ls.start + rows[ri].hi} : ls;
          line.spanned = true;
        }
        line.contCols = rows[ri].cont;
        line.snapLatinPx = (float)grid.dLatinPx;
        line.snapCjkPx = (float)grid.dCjkPx;
        line.codeHl = hl;
        line.height = adv;
        line.baseline = rowBase;
        line.left = left(b);
        line.width = lineWidthCode;
        line.y = (Su)py;
        if (ri == 0 && g.lineNo > 0) {
          const std::string num = std::to_string(g.lineNo + (i32)at);
          line.marker = strs.intern(num);
          line.markerStyle = g.codeStyle;
          line.markerRole = Fragment::Marker::LineNumber;
          // (plan P3-16) its extent left of the line: its digits and the
          // gutter's separator (0.55 code em, the marker's CSS padding)
          if (chSu > 0) {
            const Su codeEm = suRoundPx(cfg.baseSizePx * cfg.codeScale);
            const Su need = (Su)num.size() * chSu + (Su)(codeEm * kMarkerSepEm) - line.left;
            if (need > lr.gutterSu) lr.gutterSu = need;
          }
        } else if (first && b.marker) {
          line.marker = b.marker;
          line.markerStyle = b.markerStyle;
        }
        first = false;
        py += adv;
        lastRow = fr->lines.size();
        fr->lines.push_back(line);
      }
    }
    if (lastRow != ~size_t(0)) fr->lines[lastRow].sep = b.sepAfter;
    leave(b, l);
  }

  // (plan P3-14) the intrinsic widths of a block's content (CSS min- and
  // max-content): the widest run no legal break divides, the widest line
  // unbroken; a leaf's indent within the block is added
  void intrinsic(u32 k, Su& mn, Su& mx, bool captions = true) const {
    const LayoutBlock& top = tree->blocks[k];
    for (u32 i = k; i < top.end; i++) {
      const LayoutBlock& b = tree->blocks[i];
      if (b.layouter == LayouterId::Table && i != k) {  // (plan P3-15) a table: its columns side by side
        Su tmn = 0, tmx = 0;
        tableIntrinsic(i, tmn, tmx);
        const Su ind = (b.x - top.x) + (b.xr - top.xr);
        mn = std::max(mn, tmn + ind);
        mx = std::max(mx, tmx + ind);
        i = b.end - 1;
        continue;
      }
      if (!b.leaf()) continue;
      if (!captions && b.traits == TraitsId::Caption) continue;  // (FitBody: a figure's body, not its caption)
      const FlowUnit& u = tb->units[b.unit];
      Su lmn = 0, lmx = 0;
      switch (b.layouter) {
        case LayouterId::Paragraph: {
          Su run = 0, line = 0;
          IK prev = IK::Penalty;
          for (const HItem& it : u.hl.items) {
            const bool brk = (it.k == IK::Glue && (prev == IK::Box || prev == IK::Disc)) ||
                             (it.k == IK::Penalty && it.x < kPenInf);
            if (it.k == IK::Penalty && it.x <= -kPenInf) {  // a forced break: a new line
              lmx = std::max(lmx, line);
              line = 0;
            }
            if (brk) {
              lmn = std::max(lmn, run);
              run = 0;
            } else if (it.k != IK::Penalty) {
              run += it.w;
            }
            if (it.k != IK::Penalty) line += it.w;
            prev = it.k;
          }
          lmn = std::max(lmn, run);
          lmx = std::max(lmx, line);
          break;
        }
        case LayouterId::Replaced:
          if (b.painter == Painter::Image) {
            Su w = 0, h = 0;
            resolveImageSize(imageSize(std::get<ImageData>(u.data), b.span), 1e6, w, h);
            lmn = lmx = w;
          } else if (b.painter == Painter::Raw) {
            const RawData& r = std::get<RawData>(u.data);
            lmx = suRoundPx(std::max(r.size.w, r.size.minW));
            lmn = suRoundPx(r.size.minW > 0 ? r.size.minW : r.size.w);
          } else if (b.painter == Painter::MathRow) {
            if (const MathBox* mb = std::get<MathData>(u.data).box) lmn = lmx = mb->w;
          }
          break;
        default:  // a code block: as wide as the room (it wraps)
          lmn = kRailMinLineSu;
          break;
      }
      const Su ind = (b.x - top.x) + (b.xr - top.xr) + b.box.inset(1) + b.box.inset(3);
      mn = std::max(mn, lmn + ind);
      mx = std::max(mx, lmx + ind);
    }
  }

  // (plan P3-15) a table's intrinsic widths: its columns' (each its cells'
  // widest, a spanning cell's excess shared) side by side, with its gaps
  void tableIntrinsic(u32 t, Su& mn, Su& mx) const {
    const LayoutBlock& b = tree->blocks[t];
    const TableSpec& spec = tree->tables[b.spec];
    const u32 ncols = (u32)spec.cols.size();
    const Su padX = spec.framed ? suRoundPx(cfg.tableCellPadEm * cfg.baseSizePx) : 0;
    const Su gap = suRoundPx(spec.gapCodeEm * cfg.baseSizePx * cfg.codeScale);
    std::vector<Su> cmn(ncols, 2 * padX), cmx(ncols, 2 * padX);
    u32 i = 0;
    for (u32 c = t + 1; c < b.end && i < spec.place.size(); c = tree->blocks[c].end, i++) {
      const CellPlace& p = spec.place[i];
      Su a = 0, z = 0;
      intrinsic(c, a, z);
      a += 2 * padX;
      z += 2 * padX;
      Su have = gap * (Su)(p.colspan - 1), haveX = have;
      for (u32 q = p.col; q < p.col + p.colspan && q < ncols; q++) {
        have += cmn[q];
        haveX += cmx[q];
      }
      for (u32 q = p.col; q < p.col + p.colspan && q < ncols; q++) {
        if (a > have) cmn[q] += (a - have) / (Su)p.colspan;
        if (z > haveX) cmx[q] += (z - haveX) / (Su)p.colspan;
      }
    }
    mn = mx = gap * (Su)(ncols ? ncols - 1 : 0);
    for (u32 q = 0; q < ncols; q++) {
      mn += cmn[q];
      mx += std::max(cmx[q], cmn[q]);
    }
  }

  // (plan P3-14; design T6 resolveTracks) the columns' widths: fixed and
  // percent first, content-fitted ones between their cells' min- and
  // max-content (sharing what is left in proportion), fr ones sharing the
  // rest; v1 tables (equal fr columns) and code tables as before
  std::vector<Su> tracks(const LayoutBlock& b, const TableSpec& spec, const std::vector<u32>& cells, Su lineWidth,
                         Su gap, Su padX) const {
    using W = ColSpec::W;
    const u32 ncols = (u32)spec.cols.size();
    std::vector<Su> colW(ncols, 0);
    bool content = false;
    for (const ColSpec& c : spec.cols) content = content || (c.w != W::Fr && c.percent <= 0);
    if (!content) {  // fr and percent only: the v1 arithmetic
      Su fixed = gap * (Su)(ncols - 1);
      float frs = 0;
      for (u32 c = 0; c < ncols; c++) {
        if (spec.cols[c].percent > 0) fixed += colW[c] = suRoundPx(spec.cols[c].percent * widthPx(b));
        else frs += spec.cols[c].fr;
      }
      const u32 nFr = (u32)std::count_if(spec.cols.begin(), spec.cols.end(), [](const ColSpec& c) { return c.percent <= 0; });
      bool equal = true;
      for (const ColSpec& c : spec.cols) equal = equal && (c.percent > 0 || c.fr == 1);
      for (u32 c = 0; c < ncols; c++) {
        if (spec.cols[c].percent > 0) continue;
        const Su frW = !nFr ? 0
                       : equal ? (lineWidth - fixed) / (Su)nFr
                               : (Su)((double)(lineWidth - fixed) * spec.cols[c].fr / frs);
        colW[c] = spec.framed ? frW : std::max(frW, kRailMinLineSu);
      }
      return colW;
    }
    // the cells' intrinsic widths, a single column's at once, a spanning
    // cell's excess shared by its columns
    std::vector<Su> mn(ncols, 2 * padX + kRailMinLineSu), mx(ncols, 2 * padX);
    for (int pass = 0; pass < 2; pass++)
      for (size_t i = 0; i < cells.size() && i < spec.place.size(); i++) {
        const CellPlace& p = spec.place[i];
        if ((p.colspan > 1) != (pass == 1)) continue;
        Su cmn = 0, cmx = 0;
        intrinsic(cells[i], cmn, cmx);
        cmn += 2 * padX;
        cmx += 2 * padX;
        Su have = gap * (Su)(p.colspan - 1), haveX = have;
        for (u32 c = p.col; c < p.col + p.colspan; c++) {
          have += mn[c];
          haveX += mx[c];
        }
        for (u32 c = p.col; c < p.col + p.colspan; c++) {
          if (cmn > have) mn[c] += (cmn - have) / (Su)p.colspan;
          if (cmx > haveX) mx[c] += (cmx - haveX) / (Su)p.colspan;
        }
      }
    for (u32 c = 0; c < ncols; c++) mx[c] = std::max(mx[c], mn[c]);
    Su room = lineWidth - gap * (Su)(ncols - 1);
    float frs = 0;
    Su flexMin = 0, flexMax = 0;  // the content-fitted columns'
    for (u32 c = 0; c < ncols; c++) {
      const ColSpec& cs = spec.cols[c];
      if (cs.percent > 0) room -= colW[c] = std::max(mn[c], suRoundPx(cs.percent * widthPx(b)));
      else if (cs.w == W::Fixed) room -= colW[c] = std::max(mn[c], cs.fixed);
      else if (cs.w == W::Min) room -= colW[c] = mn[c];
      else if (cs.w == W::Max) room -= colW[c] = mx[c];
      else if (cs.w == W::Auto) {
        flexMin += mn[c];
        flexMax += mx[c];
      } else {
        frs += cs.fr;
        room -= mn[c];  // (an fr column's floor)
      }
    }
    // auto columns: their max-content when it fits, else between their
    // min and max in proportion to what each would take
    const Su autoRoom = std::max<Su>(room, flexMin);
    for (u32 c = 0; c < ncols; c++) {
      if (spec.cols[c].w != W::Auto || spec.cols[c].percent > 0) continue;
      colW[c] = autoRoom >= flexMax  ? mx[c]
                : flexMax > flexMin ? mn[c] + (Su)((double)(autoRoom - flexMin) * (mx[c] - mn[c]) / (flexMax - flexMin))
                                    : mn[c];
      room -= colW[c];
    }
    // fr columns: the rest, above their floor
    const Su frRoom = std::max<Su>(0, room);
    for (u32 c = 0; c < ncols; c++)
      if (spec.cols[c].w == W::Fr && spec.cols[c].percent <= 0)
        colW[c] = mn[c] + (frs > 0 ? (Su)((double)frRoom * spec.cols[c].fr / frs) : 0);
    return colW;
  }

  // (plan P3-10; design T6 TableSpec, S9; plan P3-14) a grid of flow roots:
  // its tracks (equal v1 columns, or fixed / percent / content-fitted / fr
  // ones), the cell padding inside; its rules (a full grid, booktabs, none);
  // each cell's content laid out by the ordinary layouters at its columns'
  // width and halign, a row taking its tallest single-row cell, a cell
  // spanning rows the rows it spans (valign placing it in them); header
  // rows repeat atop a continuation sheet. A table wider than the measure
  // overflows it (table-overflow, D-Y09).
  void table(const LayoutBlock& b) {
    const u32 self = (u32)(&b - tree->blocks.data());
    const TableSpec& spec = tree->tables[b.spec];
    Leaf l = enter(true, b);
    const Su lineWidth = width(b);
    std::vector<u32> cells;
    for (u32 k = self + 1; k < b.end; k = tree->blocks[k].end) cells.push_back(k);
    // a valid unit for its own fragments (rules, empty cells): its first
    // leaf's, else the top's first
    u32 unit0 = 0, lastLeaf = ~0u;
    for (u32 k = self + 1; k < b.end; k++)
      if (tree->blocks[k].leaf()) {
        if (lastLeaf == ~0u) unit0 = tree->blocks[k].unit;
        lastLeaf = k;
      }
    const Sep endSep = lastLeaf != ~0u ? tree->blocks[lastLeaf].sepAfter : Sep::Newline;
    const size_t first = fr->lines.size();
    std::vector<size_t> rowStarts;  // each row's first fragment
    std::vector<char> joined;       // a row a cell above spans into
    size_t headerEnd = first;       // past the header rows (and their rule)
    const u32 ncols = (u32)spec.cols.size();
    const u32 nRows = spec.rows;
    if (ncols > 0 && !cells.empty() && spec.place.size() == cells.size()) {
      const Su padX = spec.framed ? suRoundPx(cfg.tableCellPadEm * cfg.baseSizePx) : 0;  // table.cellPad
      const Su padY = spec.framed ? suRoundPx(cfg.tableRowPadEm * cfg.baseSizePx) : 0;   // table.rowPad
      const Su gap = suRoundPx(spec.gapCodeEm * cfg.baseSizePx * cfg.codeScale);
      const std::vector<Su> colW = tracks(b, spec, cells, lineWidth, gap, padX);
      std::vector<Su> colX(ncols);
      Su total = 0;
      for (u32 c = 0, x = 0; c < ncols; c++) {
        colX[c] = (Su)x;
        x += (u32)(colW[c] + gap);
        total = (Su)x - gap;
      }
      bool content = false;
      for (const ColSpec& c : spec.cols) content = content || (c.w != ColSpec::W::Fr && c.percent <= 0);
      // (D-Y09) wider than the measure even at min-content: it overflows
      if (total > lineWidth) {
        diags.add(Sev::Warning, "table-overflow", b.span,
                  "a table is wider than the measure at its columns' least widths");
        fr->overflowR = std::max(fr->overflowR, (Su)(left(b) + total));
      }
      const Su ruleW = content ? std::min(total, std::max(total, lineWidth)) : lineWidth;
      auto cellWidth = [&](const CellPlace& p) {
        Su w = gap * (Su)(p.colspan - 1);
        for (u32 c = p.col; c < p.col + p.colspan && c < ncols; c++) w += colW[c];
        w -= 2 * padX;
        return w < kRailMinLineSu ? kRailMinLineSu : w;
      };
      auto addRule = [&](i64 yy) {
        Fragment rl;
        rl.unitIdx = unit0;
        rl.kind = FragKind::Rule;
        rl.left = left(b);
        rl.width = ruleW;
        rl.y = (Su)yy;
        fr->lines.push_back(rl);
      };
      using R = TableSpec::Rules;
      if (spec.rules != R::None) addRule(py);
      // each row's last cell (by first position): its separator is a row's
      std::vector<size_t> lastInRow(nRows, ~size_t(0));
      for (size_t i = 0; i < cells.size(); i++) lastInRow[spec.place[i].row] = i;
      joined.assign(nRows, 0);
      for (const CellPlace& p : spec.place)
        for (u32 r = p.row + 1; r < p.row + p.rowspan && r < nRows; r++) joined[r] = 1;
      struct Laid {
        size_t lo = 0, hi = 0;
        i64 h = 0;
      };
      std::vector<Laid> laid(cells.size());
      std::vector<i64> rowTop(nRows, 0);
      size_t k = 0;  // the next cell, by first position
      for (u32 r = 0; r < nRows; r++) {
        const i64 top = py + padY;
        rowTop[r] = top;
        i64 rowBottom = top + baseLeading;
        rowStarts.push_back(fr->lines.size());
        for (; k < cells.size() && spec.place[k].row == r; k++) {
          const CellPlace& p = spec.place[k];
          const Su cellW = cellWidth(p);
          const u8 a = p.halign ? p.halign : spec.cols[p.col].align;
          const LineEnds halign = LineEnds::preset(a == 'c'   ? LineEnds::Preset::Center
                                                   : a == 'r' ? LineEnds::Preset::Right
                                                              : LineEnds::Preset::Left,
                                                   em);
          // (plan P3-07) a cell ends with a tab, a row with a row; the table
          // with what follows its last leaf. A code block's row ends a line
          // (plan P3-11): each of its cells ends with a newline, the last
          // row with what follows the block
          const Sep cellSep = spec.lines            ? (r + 1 < nRows ? Sep::Newline : endSep)
                              : lastInRow[r] != k   ? Sep::Tab
                              : r + 1 < nRows       ? Sep::Row
                                                    : endSep;
          const Su cellX = (Su)(left(b) + colX[p.col] + padX);
          const size_t before = fr->lines.size();
          // the cell: a flow root at its content box
          ExclusionMap cellFloats(em);
          const Ctx saved = ctx;
          const i64 savedPy = py;
          const Su savedGap = gapBefore;
          ctx = Ctx{cellX, cellW, suToPx(cellW), &cellFloats, &halign, true};
          py = top;
          gapBefore = 0;
          block(cells[k]);
          const i64 cy = py;
          ctx = saved;
          py = savedPy;
          gapBefore = savedGap;
          if (fr->lines.size() == before && !spec.lines) {
            // an empty cell still holds its place in content text: an empty
            // line carrying its separator (no items, no height of its own);
            // a code line without a note has none (its notes copy alone)
            Fragment e;
            e.unitIdx = unit0;
            e.y = (Su)top;
            e.left = cellX;
            e.width = cellW;
            e.height = baseLeading;
            e.baseline = baseLeading / 2;
            e.ragged = true;
            e.anchor = tree->blocks[cells[k]].anchor;
            const Span sp = tree->blocks[cells[k]].span;
            e.srcSpan = sp.empty() ? Span{b.span.start, b.span.start} : sp;
            e.spanned = true;
            fr->lines.push_back(e);
          }
          // its last line ends the cell
          for (size_t q = fr->lines.size(); q-- > before;)
            if (fr->lines[q].kind == FragKind::Line || fr->lines[q].kind == FragKind::CodeRow ||
                fr->lines[q].kind == FragKind::Math) {
              fr->lines[q].sep = cellSep;
              break;
            }
          for (size_t q = before; q < fr->lines.size(); q++) fr->lines[q].gridCell = (i32)k;
          laid[k] = {before, fr->lines.size(), cy - top};
          if (p.rowspan == 1 && cy > rowBottom) rowBottom = cy;
        }
        // the cells that end in this row: the rows they span hold them, and
        // each stands where its valign says
        for (size_t i = 0; i < k; i++) {
          const CellPlace& p = spec.place[i];
          if (p.row + p.rowspan - 1 != r) continue;
          if (p.rowspan > 1) rowBottom = std::max(rowBottom, rowTop[p.row] + laid[i].h);
          if (p.valign == CellPlace::V::Top) continue;
          const i64 room = rowBottom - rowTop[p.row] - laid[i].h;
          const i64 off = p.valign == CellPlace::V::Middle ? room / 2 : room;
          if (off > 0)
            for (size_t q = laid[i].lo; q < laid[i].hi; q++) fr->lines[q].y += (Su)off;
        }
        py = rowBottom + padY;
        const bool lastHeader = spec.header > 0 && r + 1 == spec.header;
        if (spec.rules == R::Grid || (spec.rules == R::Booktabs && (lastHeader || r + 1 == nRows))) addRule(py);
        if (lastHeader) headerEnd = fr->lines.size();
      }
    }
    for (size_t q = first; q < fr->lines.size(); q++) fr->lines[q].table = self;
    // (plan P3-12) a page cuts between rows only — a row (with the rule
    // under it) is one atom, rows a cell spans one; a code block's rows
    // keep widows and orphans; (plan P3-14) the header rows (and the rules
    // above and under them) repeat atop a continuation sheet and keep with
    // the first row after them
    for (size_t q = first; q < fr->lines.size(); q++) fr->lines[q].brk = PenTier::Structural;
    for (size_t r = 1; r < rowStarts.size(); r++) {
      if (rowStarts[r] >= fr->lines.size() || (r < joined.size() && joined[r])) continue;
      const bool wo = spec.lines && (r < kOrphans || rowStarts.size() - r < kWidows);
      fr->lines[rowStarts[r]].brk = wo                  ? PenTier::WidowOrphan
                                    : r == spec.header ? PenTier::KeepWithNext
                                                        : PenTier::Normal;
    }
    for (size_t q = first; q < headerEnd; q++) fr->lines[q].paged |= kPagedHeader;
    if (first < fr->lines.size()) fr->lines[first].brk = PenTier::Normal;
    if (!ctx.cell) fr->vlist.push_back({unit0, gapBefore, l.clear, (Su)l.top, (Su)(py - l.top), false, self});
    gapBefore = 0;
    boundary(first, b.tr.keepWithNext, false);
  }
};
const DocLayout::Fn DocLayout::kLayouters[] = {&DocLayout::paragraph, &DocLayout::stack, &DocLayout::replaced,
                                               &DocLayout::grid, &DocLayout::table};

}  // namespace

LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics, Interner& strs,
                       const LayoutSettings& cfg, DiagSink& diags, BreakMemo* memo, bool paged, BoxAsker* boxes) {
  LayoutResult lr;
  DocLayout(metrics, strs, cfg, diags, lr, memo, paged, boxes).run(tops);
  return lr;
}

std::string dumpBreaks(const LayoutResult& lr) {
  std::string out;
  for (const UnitBreaks& b : lr.breaks) {
    appendf(out, "top pid=%u unit=%u", b.pid, b.unit);
    if (b.cell >= 0) appendf(out, " cell=%d", b.cell);  // a cell's, caption's or sidecar row's stream
    appendf(out, " lines=%zu cost=%.4f breakpoints=[", b.r.breakpoints.size(), b.r.cost);
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
      const Fragment& l = fr.lines[i];
      [&] {
        if (l.kind == FragKind::Rule) {  // printed at its midline
          appendf(out, "  L%zu rule y=%dsu left=%dsu w=%dsu\n", i, l.y + l.height / 2, l.left, l.width);
          return;
        }
        if (l.kind == FragKind::Frame) {
          appendf(out, "  L%zu frame y=%dsu left=%dsu w=%dsu h=%dsu\n", i, l.y, l.left, l.width, l.height);
          return;
        }
        if (l.kind == FragKind::CodeRow) {
          appendf(out, "  L%zu code y=%dsu left=%dsu line=%u [%u,%u)%s%s%s\n", i,
                  l.y, l.left, l.codeLine, l.cbLo, l.cbHi,
                  l.codeCont ? " cont" : "", l.codeHl ? " hl" : "",
                  l.marker ? " marker" : "");
          if (l.contCols) out.insert(out.size() - 1,
                                     " cc=" + std::to_string(l.contCols));
          return;
        }
        if (l.kind == FragKind::Raw) {
          appendf(out, "  L%zu raw y=%dsu left=%dsu w=%dsu", i, l.y, l.left, l.width);
          if (l.hostBox) appendf(out, " host h=%dsu", l.height);  // (plan P3-28) measured at w
          out += '\n';
          return;
        }
        if (l.kind == FragKind::Math) {
          appendf(out, "  L%zu math y=%dsu left=%dsu w=%dsu\n", i, l.y, l.left, l.width);
          return;
        }
        if (l.kind == FragKind::Image) {
          appendf(out, "  L%zu img y=%dsu left=%dsu w=%dsu h=%dsu\n", i, l.y,
                  l.left, l.width, l.height);
          return;
        }
        if (l.cellIdx >= 0 || l.gridCell >= 0) {
          appendf(out, "  L%zu cell=%d y=%dsu left=%dsu w=%dsu blocks=[%u,%u)%s\n",
                  i, l.gridCell >= 0 ? l.gridCell : l.cellIdx, l.y, l.left, l.width, l.blockBegin, l.blockEnd,
                  l.overfull ? " overfull" : "");
          return;
        }
        appendf(out, "  L%zu y=%dsu left=%dsu w=%dsu dw=%dsu dc=%dsu join=%s%s%s%s blocks=[%u,%u) @[%u,%u)\n",
                i, l.y, l.left, l.width, l.wordDeltaSu, l.cjkDeltaSu,
                l.sep == Sep::Newline ? "last" : sepName(l.sep),
                l.endsWithHyphen ? " hyphen" : "", l.marker ? " marker" : "",
                l.overfull ? " overfull" : "",
                l.blockBegin, l.blockEnd, l.srcSpan.start, l.srcSpan.end);
      }();
      // (plan P3-13) its paged role: an insert (its reference's source
      // position) or the inserts' separator
      if (l.paged & kPagedInsert)
        out.insert(out.size() - 1, (l.paged & kPagedHeader) ? std::string(" insert-sep")
                                                            : " insert@" + std::to_string(l.insertAt));
    }
  }
  return out;
}

std::string dumpVList(const LayoutResult& lr, const std::vector<TopBlock>& tops) {
  std::string out;
  for (size_t p = 0; p < lr.paras.size() && p < tops.size(); p++) {
    const ParaFrame& fr = lr.paras[p];
    const TopTree& t = *tops[p].tree;
    appendf(out, "vlist pid=%u y=%dsu h=%dsu\n", fr.pid, fr.y, fr.h);
    for (const VEntry& v : fr.vlist) {
      if (v.gap) appendf(out, "  glue %dsu\n", v.gap);
      if (v.clear) appendf(out, "  clear %dsu\n", v.clear);
      const LayoutBlock& b = v.block != ~0u ? t.blocks[v.block] : t.blocks[t.leaves[v.unit]];
      appendf(out, "  box unit=%u %s y=%dsu h=%dsu%s\n", v.unit, traitsName(b.traits), v.y, v.h,
              v.out ? " out-of-flow" : "");
    }
  }
  return out;
}

}  // namespace tsr
