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
    Su advance = baseLeading;
    if (f.maxAsc + f.maxDesc > advance) advance = f.maxAsc + f.maxDesc;
    line.height = advance;
    line.baseline = (advance - (f.maxAsc + f.maxDesc)) / 2 + f.maxAsc;
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
    bool start;  // on the start side: pushes text from the start
  };
  explicit ExclusionMap(Su gap) : gap_(gap) {}
  bool empty() const { return v_.empty(); }
  void add(const Box& b) { v_.push_back(b); }
  // [x0, x1) narrowed by the floats meeting [y0, y1) (`push`: more room a
  // start float leaves — a list marker's, beside it)
  void available(i64 y0, i64 y1, Su& x0, Su& x1, Su push = 0) const {
    for (const Box& f : v_) {
      if (f.y1 <= y0 || f.y0 >= y1) continue;
      if (f.start) x0 = std::max(x0, f.x1 + gap_ + push);
      else x1 = std::min(x1, f.x0 - gap_);
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
  DocLayout(const MetricStore& m, Interner& s, const LayoutSettings& c, DiagSink& d, LayoutResult& r, BreakMemo* memo)
      : metrics(m), strs(s), cfg(c), diags(d), lr(r), memo_(memo), measure(suFloorPx(c.widthPx)),
        baseLeading(suRoundPx(c.lineHeight * c.baseSizePx)), paraGap(suRoundPx(c.paraSpacingEm * c.baseSizePx)),
        minWrap(suRoundPx(c.minWrapWidthEm * c.baseSizePx)), em(suRoundPx(c.baseSizePx)), excl(suRoundPx(c.baseSizePx)) {
    bparams.cost = c.cost;
    ctx = Ctx{0, measure, c.widthPx, &excl, nullptr, false};
  }

  void run(const std::vector<TopBlock>& tops) {
    i64 y = 0;
    bool gap = false;  // a gap before the next top: an in-flow one came before
    for (size_t p = 0; p < tops.size(); p++) {
      tb = &tops[p];
      tree = tb->tree;
      ParaFrame frame;
      frame.pid = tb->pid;
      frame.w = measure;
      // the document's stack: a paragraph gap between tops — none after a
      // top that is only out of flow (a float: zero advance, plan P3-08)
      const Su topGap = gap ? paraGap : 0;
      gapBefore = topGap;
      y += topGap;
      frame.y = (Su)y;
      fr = &frame;
      py = 0;
      block(0);
      frame.h = (Su)py;
      y += py;
      bool outOnly = !frame.vlist.empty();
      for (const VEntry& e : frame.vlist) outOnly = outOnly && e.out;
      if (!outOnly) gap = true;
      else y -= topGap;  // the float took no room: the next top stands where it would have
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
  Su left(const LayoutBlock& b) const { return ctx.x0 + b.x; }
  Su width(const LayoutBlock& b) const { return ctx.width - b.x; }
  double widthPx(const LayoutBlock& b) const { return ctx.widthPx - suToPx(b.x); }
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
  void block(u32 b) { (this->*kLayouters[(size_t)tree->blocks[b].layouter])(tree->blocks[b]); }

  // Layout breaks its paragraphs (plan P1-15) with the cached KP (break.cc:
  // keyed by exactly the DP inputs, shared across documents — the editing
  // loop's fast path). A run wider than the line is set Overfull on a line
  // of its own (the final-pass rescue) and reported once per stream.
  // (plan P3-09) a stream breaks with the line-end glue it will be set with
  BreakResult breakStream(const std::vector<BreakBlock>& blocks, const HList& h, const ParShape& shape,
                          const LineEnds& ends) {
    BreakParams params = bparams;
    params.ends = ends;
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
    if (clears) l.clear = clearance(b);
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
  }

  void stack(const LayoutBlock& b) {
    u8 num, den;
    Su su;
    gapOf(*tree, (u32)(&b - tree->blocks.data()), num, den, su);
    const Su gap = den ? (Su)((i64)paraGap * num / den) : su;
    const u32 self = (u32)(&b - tree->blocks.data());
    bool prevOut = false;  // the child before was only out of flow (a float)
    for (u32 k = self + 1; k < b.end; k = tree->blocks[k].end) {
      if (k > self + 1 && !prevOut) {
        py += gap;
        gapBefore = gap;
      }
      const size_t v0 = fr->vlist.size();
      block(k);
      prevOut = fr->vlist.size() > v0;
      for (size_t e = v0; e < fr->vlist.size(); e++) prevOut = prevOut && fr->vlist[e].out;
      if (prevOut) gapBefore = k > self + 1 ? gap : gapBefore;  // what follows takes the float's gap
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
    LinePolicy pol;
    pol.ends = ctx.halign ? *ctx.halign : endsOf(b.tr);  // (in a table cell: its column's halign)
    pol.singleCenter = b.tr.singleCenter;
    lr.breaks.push_back({tb->pid, b.unit, -1, breakStream(u.blocks, u.hl, shape, pol.ends)});
    pol.endSep = b.sepAfter;
    pol.marker = b.marker;
    pol.markerStyle = b.markerStyle;
    pol.anchor = b.carry ? b.carry : u.anchor;  // a block's label, else an inline one
    py = materializeLines({u.hl, u.blockStart, (u32)u.blocks.size(), lr.breaks.back().r, shape, left(b), b.unit, -1},
                          pol, metrics, cfg, baseLeading, py, fr->lines);
    leave(b, l);
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
      case Painter::Raw:
        f.kind = FragKind::Raw;
        f.height = suRoundPx(std::get<RawData>(u.data).hPx);
        break;
      case Painter::Image: {
        // block figure image (figure-design.md §3): centred on the measure,
        // advance = display height (float placement is F2)
        Su imgW = 0, imgH = 0;
        resolveImageSize(std::get<ImageData>(u.data).size, widthPx(b), imgW, imgH);
        f.kind = FragKind::Image;
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
        const MathBox* mb = std::get<MathData>(u.data).box;
        if (!mb) {
          leave(b, l);
          return;
        }
        f.kind = FragKind::Math;
        Su shift = (lineWidth - mb->w) / 2;
        if (shift < 0) shift = 0;
        f.left = left(b) + shift;
        f.width = mb->w;
        f.srcSpan = b.span;
        f.height = std::max(mb->asc + mb->desc, baseLeading);
        f.baseline = (f.height - (mb->asc + mb->desc)) / 2 + mb->asc;
        f.sep = b.sepAfter;  // a formula is copied (its source) like a paragraph
        break;
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
    resolveImageSize(std::get<ImageData>(u.data).size, widthPx(b), imgW, imgH);
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
    // the sidecar column is layout's (plan P1-16: code.sidecarFrac)
    const Su sidebarW = g.sidecar ? suRoundPx(b.tr.sidecarFrac * (widthPx(b))) : 0;
    Su adv = baseLeading;
    Su rowBase = adv / 2;  // a row's baseline, centred (its line-height is the row)
    if (metrics.hasVmet(g.codeStyle)) {
      const VMet& v = metrics.vmet(g.codeStyle);
      if (v.ascent + v.descent > adv) adv = v.ascent + v.descent;
      rowBase = (adv - (v.ascent + v.descent)) / 2 + v.ascent;
    }
    // three-box partition (verbatim §5): the code measure stops before
    // the sidecar column; the gutter stays out-of-flow (markers)
    const bool hasSidecar = sidebarW > 0 && !u.cells.empty();
    Su gapSu = 0;
    Su lineWidthFull = lineWidth;
    Su lineWidthCode = lineWidth;
    if (hasSidecar) {
      gapSu = suRoundPx(cfg.baseSizePx * cfg.codeScale);
      lineWidthCode = lineWidth - sidebarW - gapSu;
      if (lineWidthCode < kRailMinLineSu) lineWidthCode = kRailMinLineSu;
      for (const TableCell& c : u.cells) {  // sidecar rows break to the sidebar
        cellBreaks.push_back(breakStream(c.blocks, c.hl, ParShape(sidebarW), leftEnds()));
        lr.breaks.push_back({tb->pid, b.unit, (i32)cellBreaks.size() - 1, cellBreaks.back()});
      }
    }
    (void)lineWidthFull;
    // ch grid (CH4, code-design.md §4): monospace is a metric contract —
    // 1ch per char, 2ch for CJK; wrap is a COLUMN computation, greedy
    // with a token-boundary preference, continuation rows indent 2ch.
    Su chSu = 0;
    if (g.wrap && g.chRef && metrics.hasWord(g.chRef, g.codeStyle))
      chSu = metrics.word(g.chRef, g.codeStyle).su;
    // measured CJK width (verbatim-design §2): budget columns from the
    // real ratio, conservatively ceiled — no assumed 2:1
    i32 cjkCols = 2;
    if (chSu > 0 && g.cjkChRef && metrics.hasWord(g.cjkChRef, g.codeStyle)) {
      Su c = metrics.word(g.cjkChRef, g.codeStyle).su;
      cjkCols = (i32)((c + chSu - 1) / chSu);
      if (cjkCols < 1) cjkCols = 1;
    }
    i32 cols = chSu > 0 ? (i32)(lineWidthCode / chSu) : 0;
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
        cols = (i32)(lineWidthCode / atomSu);  // the code column, not the measure
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
      bool hl = hlSet.count(li + 1) != 0;
      const i64 rowTop = py;
      // (plan P3-07) its source: a row is its slice of an exact line, else
      // the line as a whole
      const Span ls = li < g.lineSpans.size() ? g.lineSpans[li] : Span{};
      const bool exact = li < g.lineSpans.size() && ls.end - ls.start == joined.size();
      for (size_t ri = 0; ri < rows.size(); ri++) {
        Fragment line;
        line.unitIdx = b.unit;
        line.kind = FragKind::CodeRow;
        line.codeLine = li;
        line.cbLo = rows[ri].lo;
        line.cbHi = rows[ri].hi;
        line.codeCont = ri > 0;
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
          line.marker = strs.intern(std::to_string(g.lineNo + (i32)li));
          line.markerStyle = g.codeStyle;
        } else if (first && b.marker) {
          line.marker = b.marker;
          line.markerStyle = b.markerStyle;
        }
        first = false;
        py += adv;
        lastRow = fr->lines.size();
        fr->lines.push_back(line);
      }
      // sidecar rows for this logical line (equal-height zip, §5):
      // ordinary inline lines broken to the sidebar measure — math,
      // links and refs land through the generic cell render path
      if (hasSidecar && li < u.cells.size()) {
        const TableCell& cell = u.cells[li];
        LinePolicy pol;  // a row's note: one stream, ending a line (D-R03)
        pol.ends = leftEnds();
        pol.anchor = cell.anchor;
        const ParShape shape(sidebarW);
        const i64 cy = materializeLines({cell.hl, cell.blockStart, (u32)cell.blocks.size(), cellBreaks[li], shape,
                                         (Su)(left(b) + lineWidthCode + gapSu), b.unit, (i32)li},
                                        pol, metrics, cfg, baseLeading, rowTop, fr->lines);
        if (cy > py) py = cy;  // the equal-height constraint
      }
    }
    if (lastRow != ~size_t(0)) fr->lines[lastRow].sep = b.sepAfter;
    leave(b, l);
  }

  // (plan P3-10; design T6 TableSpec, S9) a grid of flow roots: v1 tracks
  // (`cols` equal columns, the cell padding inside), full-width rules above,
  // between and below the rows; each cell's content is laid out by the
  // ordinary layouters at its column's width and halign, then the row
  // takes its tallest cell. The table is one box of the vertical list and
  // one atomic group on paged sheets.
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
    if (spec.cols > 0 && !cells.empty()) {
      const Su colW = lineWidth / (Su)spec.cols;
      const Su padX = suRoundPx(cfg.tableCellPadEm * cfg.baseSizePx);  // table.cellPad
      const Su padY = suRoundPx(cfg.tableRowPadEm * cfg.baseSizePx);   // table.rowPad
      Su cellW = colW - 2 * padX;
      if (cellW < kRailMinLineSu) cellW = kRailMinLineSu;
      const size_t nRows = cells.size() / spec.cols;
      auto addRule = [&](i64 yy) {
        Fragment rl;
        rl.unitIdx = unit0;
        rl.kind = FragKind::Rule;
        rl.left = left(b);
        rl.width = lineWidth;
        rl.y = (Su)yy;
        fr->lines.push_back(rl);
      };
      addRule(py);
      for (size_t r = 0; r < nRows; r++) {
        const i64 rowTop = py + padY;
        i64 rowBottom = rowTop + baseLeading;
        for (u32 c = 0; c < spec.cols; c++) {
          const u32 k = (u32)(r * spec.cols + c);
          const u8 a = spec.aligns[c];
          const LineEnds halign = LineEnds::preset(a == 'c'   ? LineEnds::Preset::Center
                                                   : a == 'r' ? LineEnds::Preset::Right
                                                              : LineEnds::Preset::Left,
                                                   em);
          // (plan P3-07) a cell ends with a tab, a row with a row; the table
          // with what follows its last leaf
          const Sep cellSep = c + 1 < spec.cols ? Sep::Tab : r + 1 < nRows ? Sep::Row : endSep;
          const Su cellX = (Su)(left(b) + (Su)c * colW + padX);
          const size_t before = fr->lines.size();
          // the cell: a flow root at its content box
          ExclusionMap cellFloats(em);
          const Ctx saved = ctx;
          const i64 savedPy = py;
          const Su savedGap = gapBefore;
          ctx = Ctx{cellX, cellW, suToPx(cellW), &cellFloats, &halign, true};
          py = rowTop;
          gapBefore = 0;
          block(cells[k]);
          const i64 cy = py;
          ctx = saved;
          py = savedPy;
          gapBefore = savedGap;
          if (fr->lines.size() == before) {
            // an empty cell still holds its place in content text: an empty
            // line carrying its separator (no items, no height of its own)
            Fragment e;
            e.unitIdx = unit0;
            e.y = (Su)rowTop;
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
          if (cy > rowBottom) rowBottom = cy;
        }
        py = rowBottom + padY;
        addRule(py);
      }
    }
    for (size_t q = first; q < fr->lines.size(); q++) fr->lines[q].table = self;
    if (!ctx.cell) fr->vlist.push_back({unit0, gapBefore, l.clear, (Su)l.top, (Su)(py - l.top), false, self});
    gapBefore = 0;
  }
};
const DocLayout::Fn DocLayout::kLayouters[] = {&DocLayout::paragraph, &DocLayout::stack, &DocLayout::replaced,
                                               &DocLayout::grid, &DocLayout::table};

}  // namespace

LayoutResult layoutDoc(const std::vector<TopBlock>& tops, const MetricStore& metrics, Interner& strs,
                       const LayoutSettings& cfg, DiagSink& diags, BreakMemo* memo) {
  LayoutResult lr;
  DocLayout(metrics, strs, cfg, diags, lr, memo).run(tops);
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
      if (l.kind == FragKind::Rule) {  // printed at its midline
        appendf(out, "  L%zu rule y=%dsu left=%dsu w=%dsu\n", i, l.y + l.height / 2, l.left, l.width);
        continue;
      }
      if (l.kind == FragKind::CodeRow) {
        appendf(out, "  L%zu code y=%dsu left=%dsu line=%u [%u,%u)%s%s%s\n", i,
                l.y, l.left, l.codeLine, l.cbLo, l.cbHi,
                l.codeCont ? " cont" : "", l.codeHl ? " hl" : "",
                l.marker ? " marker" : "");
        if (l.contCols) out.insert(out.size() - 1,
                                   " cc=" + std::to_string(l.contCols));
        continue;
      }
      if (l.kind == FragKind::Raw) {
        appendf(out, "  L%zu raw y=%dsu left=%dsu w=%dsu\n", i, l.y, l.left, l.width);
        continue;
      }
      if (l.kind == FragKind::Math) {
        appendf(out, "  L%zu math y=%dsu left=%dsu w=%dsu\n", i, l.y, l.left, l.width);
        continue;
      }
      if (l.kind == FragKind::Image) {
        appendf(out, "  L%zu img y=%dsu left=%dsu w=%dsu h=%dsu\n", i, l.y,
                l.left, l.width, l.height);
        continue;
      }
      if (l.cellIdx >= 0 || l.gridCell >= 0) {
        appendf(out, "  L%zu cell=%d y=%dsu left=%dsu w=%dsu blocks=[%u,%u)%s\n",
                i, l.gridCell >= 0 ? l.gridCell : l.cellIdx, l.y, l.left, l.width, l.blockBegin, l.blockEnd,
                l.overfull ? " overfull" : "");
        continue;
      }
      appendf(out, "  L%zu y=%dsu left=%dsu w=%dsu dw=%dsu dc=%dsu join=%s%s%s%s blocks=[%u,%u) @[%u,%u)\n",
              i, l.y, l.left, l.width, l.wordDeltaSu, l.cjkDeltaSu,
              l.sep == Sep::Newline ? "last" : sepName(l.sep),
              l.endsWithHyphen ? " hyphen" : "", l.marker ? " marker" : "",
              l.overfull ? " overfull" : "",
              l.blockBegin, l.blockEnd, l.srcSpan.start, l.srcSpan.end);
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
