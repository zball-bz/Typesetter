#include "layout.h"

#include "../support/rails.h"
#include "../shape/objects.h"
#include "../shape/textrules.h"

#include <algorithm>
#include <unordered_set>

#include "../code/grid.h"

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
  // Justify: the measure is filled; Ragged: it is not (tight lines still
  // shrink); Center: ragged, the slack split both sides; Cell: ragged, set
  // left, centre or right within the cell's content width
  enum class Align : u8 { Justify, Ragged, Center, Cell } align = Align::Justify;
  u8 cellAlign = 'l';
  double widthPx = 0;   // the measure in px (justification and centring slack)
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
  LineWidths widths;   // the prefix beside a float, then the content width
  bool narrowLeft;     // the float is on the left: narrowed lines shift right
  Su left, width;      // the content box
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
    // not justified: the lines say so (paint: data-ragged; the audit's right
    // edge skips them)
    line.ragged = pol.align != LinePolicy::Align::Justify;
    line.cellIdx = s.cellIdx;
    line.blockBegin = r.lo;
    line.blockEnd = r.hi;
    line.itemBegin = r.ilo;
    line.itemEnd = r.ihi;
    line.left = s.left;
    line.width = s.width;
    // the prefix beside a float
    const bool narrowed = li < (size_t)s.widths.narrowK && s.widths.narrow > 0;
    if (narrowed) {
      line.width = s.widths.narrow;
      if (s.narrowLeft) line.left += s.width - s.widths.narrow;
    }
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
    // the spacing: justified lines fill the measure, every other line only
    // shrinks when tight (the breaker counted on it) — one rule for every
    // stream
    const bool rigid = last || pol.align != LinePolicy::Align::Justify;
    const double slackPx = (narrowed ? suToPx(s.widths.narrow) : pol.widthPx) - f.naturalPx;
    // a line without stretchable glue (all URL pieces / one unbreakable
    // token) cannot be justified — TeX's underfull box; it sets ragged
    // rather than pretending (real-world-report.md)
    // fil glue (plan P2-16: a fill) takes a slack line's whole slack, on any
    // line; the finite glue keeps its width
    const bool fil = f.fills > 0 && slackPx > 0;
    if (fil) line.fillPx = slackPx / f.fills;
    if (f.totalWeight <= 0 && !rigid && slackPx != 0 && !fil) line.noGlue = true;
    if (f.totalWeight > 0 && !fil) {
      double d = slackPx / f.totalWeight;  // per unit weight (v2 §8)
      if (rigid && slackPx > 0) d = 0;
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
    if (pol.align == LinePolicy::Align::Center && slackPx > 0) {
      // caption centring: slack splits both sides; the right edge stays
      // inside the measure (width shrinks by the shift)
      Su cs = suRoundPx(slackPx / 2);
      line.left += cs;
      line.width -= cs;
    } else if (pol.align == LinePolicy::Align::Cell) {
      Su slack = s.width - suCeilPx(f.naturalPx);
      Su shift = 0;
      if (slack > 0) {
        if (pol.cellAlign == 'c') shift = slack / 2;
        else if (pol.cellAlign == 'r') shift = slack;
      }
      line.left += shift;
      line.width -= shift;  // the right edge stays at the content edge (audit: no overflow)
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

// The float exclusions of the flow (plan P1-15; design T6 "ExclusionMap"):
// the F2 tracker (figure-design.md §4) that Doc::typeset used to run ahead
// of layout and replay through five fields on the units, now at layout's
// own cursor. It reproduces today's prefix ParShape exactly — occlusion
// counted from the float's top in baseLeading lines, a same-side float
// stacked below (the widest of the stack occludes), an opposite-side float
// and every non-text unit clearing — so breaks and lines are unchanged;
// conservative bands over real line heights come with ParShape (T6).
class ExclusionMap {
 public:
  ExclusionMap(Su baseLeading, Su paraGap, Su emGap) : lead_(baseLeading), paraGap_(paraGap), emGap_(emGap) {}
  // the cursor moves down by a gap
  void advance(Su gap) {
    if (remain_ > 0) {
      remain_ -= gap;
      if (remain_ < 0) remain_ = 0;
    }
  }
  // a float arriving while one is active: same side → stacked below the
  // active one (`shift`); the other side → the active one is cleared first
  // (`clear`) — real-world-report.md: Wikipedia opens with two thumbnails
  void arrive(u8 side, Su& shift, Su& clear) {
    shift = clear = 0;
    if (remain_ > 0 && side_ == side) {
      shift = (Su)remain_;
    } else if (remain_ > 0) {
      clear = (Su)remain_;
      remain_ = 0;
      occl_ = 0;
    }
  }
  // the float placed: image + caption + one gap of clearance, beside the
  // measure's edge
  void add(u8 side, Su shift, Su imgW, Su imgH, i64 captionH) {
    remain_ = shift + (i64)imgH + captionH + paraGap_;
    Su occl = imgW + emGap_;
    occl_ = shift > 0 && occl_ > occl ? occl_ : occl;
    side_ = side;
  }
  // a non-text unit clears the float: the advance that does it
  Su clear() {
    if (remain_ <= 0) return 0;
    Su c = (Su)remain_;
    remain_ = 0;
    occl_ = 0;
    return c;
  }
  // the line widths of a paragraph starting here: the prefix beside the
  // float, then the measure
  LineWidths widths(Su lineWidth, bool& fromLeft) const {
    LineWidths lw{lineWidth};
    fromLeft = false;
    if (remain_ > 0 && occl_ > 0 && occl_ < lw.constant - kRailMinLineSu) {
      lw.narrow = lw.constant - occl_;
      lw.narrowK = (u32)((remain_ + lead_ - 1) / lead_);
      fromLeft = side_ == 1;
    }
    return lw;
  }
  // the paragraph's lines, counted in baseLeading
  void consume(size_t lines) {
    if (remain_ > 0) {
      remain_ -= (i64)lines * lead_;
      if (remain_ < 0) remain_ = 0;
    }
  }

 private:
  Su lead_, paraGap_, emGap_;
  i64 remain_ = 0;  // occlusion height left, measured from the cursor
  Su occl_ = 0;     // the occluded width
  u8 side_ = 0;
};

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
        excl(baseLeading, paraGap, suRoundPx(c.baseSizePx)) {
    bparams.cost = c.cost;
  }

  void run(const std::vector<TopBlock>& tops) {
    i64 y = 0;
    for (size_t p = 0; p < tops.size(); p++) {
      tb = &tops[p];
      tree = tb->tree;
      ParaFrame frame;
      frame.pid = tb->pid;
      frame.y = (Su)y;
      frame.w = measure;
      fr = &frame;
      py = 0;
      // the document's stack: a paragraph gap between tops
      gapBefore = p > 0 ? paraGap : 0;
      excl.advance(gapBefore);
      block(0);
      frame.h = (Su)py;
      y += py;
      if (p + 1 < tops.size()) y += paraGap;
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
  BreakResult breakStream(const std::vector<BreakBlock>& blocks, const HList& h, LineWidths lw) {
    BreakResult r = breakLinesCached(blocks, lw, bparams, memo_);
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
  // a leaf starts: an in-flow box that is not a paragraph clears the float
  // beside it
  struct Leaf {
    size_t from;  // its first fragment
    Su clear;
    i64 top;
  };
  Leaf enter(bool clears) {
    Leaf l{fr->lines.size(), 0, 0};
    if (clears) l.clear = excl.clear();
    py += l.clear;
    l.top = py;
    cellBreaks.clear();
    return l;
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
    fr->vlist.push_back({b.unit, gapBefore, l.clear, (Su)l.top, (Su)(py - l.top), out});
    gapBefore = 0;
  }

  void stack(const LayoutBlock& b) {
    u8 num, den;
    Su su;
    gapOf(*tree, (u32)(&b - tree->blocks.data()), num, den, su);
    const Su gap = den ? (Su)((i64)paraGap * num / den) : su;
    const u32 self = (u32)(&b - tree->blocks.data());
    for (u32 k = self + 1; k < b.end; k = tree->blocks[k].end) {
      if (k > self + 1) {
        py += gap;
        excl.advance(gap);
        gapBefore = gap;
      }
      block(k);
    }
  }

  void paragraph(const LayoutBlock& b) {
    const FlowUnit& u = tb->units[b.unit];
    Leaf l = enter(false);
    const Su lineWidth = measure - b.x;
    bool narrowLeft = false;
    const LineWidths lw = excl.widths(lineWidth, narrowLeft);
    lr.breaks.push_back({tb->pid, b.unit, -1, breakStream(u.blocks, u.hl, lw)});
    excl.consume(lr.breaks.back().r.breakpoints.size());
    const BlockTraits::Align a = b.tr.align;
    LinePolicy pol;
    pol.align = a == BlockTraits::Align::Center   ? LinePolicy::Align::Center
                : a == BlockTraits::Align::Ragged ? LinePolicy::Align::Ragged
                : a == BlockTraits::Align::End    ? LinePolicy::Align::Cell
                                                  : LinePolicy::Align::Justify;
    if (a == BlockTraits::Align::End) pol.cellAlign = 'r';  // set at the end (plan P3-01: par.align end)
    pol.endSep = b.sepAfter;
    pol.widthPx = cfg.widthPx - suToPx(b.x);
    pol.marker = b.marker;
    pol.markerStyle = b.markerStyle;
    pol.anchor = b.carry ? b.carry : u.anchor;  // a block's label, else an inline one
    py = materializeLines({u.hl, u.blockStart, (u32)u.blocks.size(), lr.breaks.back().r, lw, narrowLeft, b.x,
                           lineWidth, b.unit, -1},
                          pol, metrics, cfg, baseLeading, py, fr->lines);
    leave(b, l);
  }

  void replaced(const LayoutBlock& b) {
    const FlowUnit& u = tb->units[b.unit];
    if (b.painter == Painter::Image && b.floatSide) {
      floatBox(b, u);
      return;
    }
    Leaf l = enter(true);
    const Su lineWidth = measure - b.x;
    Fragment f;
    f.unitIdx = b.unit;
    f.y = (Su)py;
    f.left = b.x;
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
        resolveImageSize(std::get<ImageData>(u.data).size, cfg.widthPx - suToPx(b.x), imgW, imgH);
        f.kind = FragKind::Image;
        Su shift = (lineWidth - imgW) / 2;
        if (shift < 0) shift = 0;
        f.left = b.x + shift;
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
        f.left = b.x + shift;
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
    Leaf l = enter(false);
    Su floatShift = 0, clearSu = 0;
    excl.arrive(b.floatSide, floatShift, clearSu);
    py += clearSu;
    l.clear = clearSu;
    l.top = py;
    const Su lineWidth = measure - b.x;
    Su imgW = 0, imgH = 0;
    resolveImageSize(std::get<ImageData>(u.data).size, cfg.widthPx - suToPx(b.x), imgW, imgH);
    i64 captionH = 0;
    for (const Flow& c : u.cells) {  // the caption breaks to the float width
      cellBreaks.push_back(breakStream(c.blocks, c.hl, LineWidths{imgW}));
      lr.breaks.push_back({tb->pid, b.unit, (i32)cellBreaks.size() - 1, cellBreaks.back()});
      captionH += (i64)cellBreaks.back().breakpoints.size() * baseLeading;
    }
    excl.add(b.floatSide, floatShift, imgW, imgH, captionH);
    const Su boxLeft = b.floatSide == 1 ? b.x : b.x + lineWidth - imgW;
    Fragment f;
    f.unitIdx = b.unit;
    f.kind = FragKind::Image;
    f.left = boxLeft;
    f.width = imgW;
    f.height = imgH;
    f.baseline = imgH;
    f.srcSpan = b.span;
    f.y = (Su)(py + floatShift);  // stacked below an active float
    fr->lines.push_back(f);
    i64 cy = py + floatShift + imgH;
    for (u32 ci = 0; ci < (u32)u.cells.size(); ci++) {
      // caption rows: left-aligned at the float width; wrapped rows rejoin
      // on copy (§9.3), each caption paragraph ends a line, the last the unit
      const Flow& cell = u.cells[ci];
      LinePolicy pol;
      pol.align = LinePolicy::Align::Ragged;
      pol.endSep = ci + 1 < (u32)u.cells.size() ? Sep::Newline : b.sepAfter;
      pol.widthPx = suToPx(imgW);
      pol.anchor = cell.anchor;
      cy = materializeLines({cell.hl, cell.blockStart, (u32)cell.blocks.size(), cellBreaks[ci], LineWidths{imgW},
                             false, boxLeft, imgW, b.unit, (i32)ci},
                            pol, metrics, cfg, baseLeading, cy, fr->lines);
    }
    if ((i64)fr->y + cy > floatBottomAbs) floatBottomAbs = (i64)fr->y + cy;
    leave(b, l, /*out=*/true);  // no advance: the float is out of flow
  }

  void grid(const LayoutBlock& b) {
    const FlowUnit& u = tb->units[b.unit];
    const GridData& g = std::get<GridData>(u.data);
    Leaf l = enter(true);
    const Su lineWidth = measure - b.x;
    // the sidecar column is layout's (plan P1-16: code.sidecarFrac)
    const Su sidebarW = g.sidecar ? suRoundPx(b.tr.sidecarFrac * (cfg.widthPx - suToPx(b.x))) : 0;
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
        cellBreaks.push_back(breakStream(c.blocks, c.hl, LineWidths{sidebarW}));
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
    auto isBreakable = [](u32 cp) {
      return cp == ' ' || cp == '\t' || cp == ',' || cp == ';' ||
             cp == ')' || cp == '}' || cp == ']' || cp == '>';
    };
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
      // hanging base: the logical line's own leading whitespace columns
      i32 leadChars = 0;
      while ((size_t)leadChars < joined.size() &&
             (joined[leadChars] == ' ' || joined[leadChars] == '\t'))
        leadChars++;
      i32 leadCols = leadChars * latinAtoms;  // in atom units
      auto contColsAt = [&](u32 breakByte) -> u16 {
        i32 cc = leadCols / latinAtoms + b.tr.contIndent;
        // comment-aware (verbatim-design §4): a break inside a comment
        // run aligns the continuation to the comment's CONTENT column
        for (auto [cs, ce] : commentSpans) {
          if (breakByte <= cs || breakByte > ce) continue;
          // column of the comment start
          i32 col = 0;
          u32 pb = 0;
          while (pb < cs) {
            u32 cp2 = utf8Next(joined, pb);
            col += isWide(cp2) ? cjkCols : latinAtoms;
          }
          // lead-in: opening punctuation streak + one space
          u32 q2 = cs;
          i32 lead = 0;
          while (q2 < ce && joined[q2] != ' ' &&
                 !((joined[q2] >= 'a' && joined[q2] <= 'z') ||
                   (joined[q2] >= 'A' && joined[q2] <= 'Z') ||
                   (joined[q2] >= '0' && joined[q2] <= '9')) &&
                 (u8)joined[q2] < 0x80) {
            q2++;
            lead++;
          }
          if (q2 < ce && joined[q2] == ' ') lead++;
          cc = col / latinAtoms + lead;
          break;
        }
        i32 colCap = cols / latinAtoms;
        if (cc > colCap - minCols) cc = colCap > minCols ? colCap - minCols : 0;
        if (cc < 0) cc = 0;
        return (u16)cc;
      };
      struct Row { u32 lo, hi; };
      std::vector<Row> rows;
      std::vector<u16> rowContOut;
      if (cols <= 0 || joined.empty()) {
        rows.push_back({0, (u32)joined.size()});
      } else {
        u32 lo = 0;
        u16 nextCont = 0;
        std::vector<u16> rowCont;
        while (lo < joined.size()) {
          i32 avail = rows.empty() ? cols : cols - (i32)nextCont * latinAtoms;
          if (avail < 8 * latinAtoms) avail = 8 * latinAtoms;
          u32 p = lo;
          i32 col = 0;
          u32 lastBrk = 0;
          while (p < joined.size()) {
            u32 q = p;
            u32 cp = utf8Next(joined, q);
            i32 w = isWide(cp) ? cjkCols : latinAtoms;
            if (col + w > avail) break;
            col += w;
            p = q;
            if (isBreakable(cp)) {
              lastBrk = p;  // break AFTER the boundary
            } else if (isWide(cp) && !isOpenPunct(cp)) {
              // CJK wraps between any two characters (clreq), except
              // before a closing punct / after an opening one (禁则)
              u32 r = q;
              u32 nx = q < joined.size() ? utf8Next(joined, r) : 0;
              if (!(nx && isClosePunct(nx))) lastBrk = p;
            }
          }
          if (p >= joined.size()) {
            rows.push_back({lo, (u32)joined.size()});
            rowCont.push_back(nextCont);
            break;
          }
          u32 cut = lastBrk > lo ? lastBrk : p;
          if (cut <= lo) {  // guarantee progress on pathological input
            u32 q = lo;
            utf8Next(joined, q);
            cut = q;
          }
          // trailing spaces stay in the ROW (not swallowed between
          // slices): the copy rebuild must be byte-lossless, and pre
          // whitespace at a ragged row's end is invisible anyway
          u32 ext = cut;
          while (ext < joined.size() && joined[ext] == ' ') ext++;
          rows.push_back({lo, ext});
          rowCont.push_back(nextCont);
          nextCont = contColsAt(cut);  // the NEXT row's indent
          lo = ext;
        }
        if (rows.empty()) {
          rows.push_back({0, 0});
          rowCont.push_back(0);
        }
        rowContOut = std::move(rowCont);
      }
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
        line.contCols = ri < rowContOut.size() ? rowContOut[ri] : 0;
        line.snapLatinPx = (float)grid.dLatinPx;
        line.snapCjkPx = (float)grid.dCjkPx;
        line.codeHl = hl;
        line.height = adv;
        line.baseline = rowBase;
        line.left = b.x;
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
        pol.align = LinePolicy::Align::Ragged;
        pol.widthPx = suToPx(sidebarW);
        pol.anchor = cell.anchor;
        const i64 cy = materializeLines({cell.hl, cell.blockStart, (u32)cell.blocks.size(), cellBreaks[li],
                                         LineWidths{sidebarW}, false, (Su)(b.x + lineWidthCode + gapSu),
                                         sidebarW, b.unit, (i32)li},
                                        pol, metrics, cfg, baseLeading, rowTop, fr->lines);
        if (cy > py) py = cy;  // the equal-height constraint
      }
    }
    if (lastRow != ~size_t(0)) fr->lines[lastRow].sep = b.sepAfter;
    leave(b, l);
  }

  void table(const LayoutBlock& b) {
    const FlowUnit& u = tb->units[b.unit];
    const TableData& td = std::get<TableData>(u.data);
    Leaf l = enter(true);
    const Su lineWidth = measure - b.x;
    if (td.cols == 0) {
      leave(b, l);
      return;
    }
    // three-line-flavoured grid: full-width rules above, between, and
    // below rows; equal columns; ragged cells aligned per column
    const Su colW = lineWidth / (Su)td.cols;
    const Su padX = suRoundPx(cfg.tableCellPadEm * cfg.baseSizePx);  // table.cellPad
    const Su padY = suRoundPx(cfg.tableRowPadEm * cfg.baseSizePx);   // table.rowPad
    Su cellW = colW - 2 * padX;
    if (cellW < kRailMinLineSu) cellW = kRailMinLineSu;
    for (const Flow& c : u.cells) {  // each cell breaks to its content width
      cellBreaks.push_back(breakStream(c.blocks, c.hl, LineWidths{cellW}));
      lr.breaks.push_back({tb->pid, b.unit, (i32)cellBreaks.size() - 1, cellBreaks.back()});
    }
    const size_t nRows = u.cells.size() / td.cols;
    auto addRule = [&](i64 yy) {
      Fragment rl;
      rl.unitIdx = b.unit;
      rl.kind = FragKind::Rule;
      rl.left = b.x;
      rl.width = lineWidth;
      rl.y = (Su)yy;
      fr->lines.push_back(rl);
    };
    addRule(py);
    for (size_t r = 0; r < nRows; r++) {
      i64 rowTop = py + padY;
      i64 rowBottom = rowTop + baseLeading;
      for (u32 c = 0; c < td.cols; c++) {
        const Flow& cell = u.cells[r * td.cols + c];
        LinePolicy pol;
        pol.align = LinePolicy::Align::Cell;
        pol.cellAlign = td.aligns[c];
        pol.widthPx = suToPx(cellW);
        pol.anchor = cell.anchor;
        // (plan P3-07) a cell ends with a tab, a row with a row; the table
        // with its unit's separator
        pol.endSep = c + 1 < td.cols ? Sep::Tab : r + 1 < nRows ? Sep::Row : b.sepAfter;
        const Su cellX = (Su)(b.x + (Su)c * colW + padX);
        const size_t before = fr->lines.size();
        const i64 cy = materializeLines({cell.hl, cell.blockStart, (u32)cell.blocks.size(),
                                         cellBreaks[r * td.cols + c], LineWidths{cellW}, false, cellX, cellW,
                                         b.unit, (i32)(r * td.cols + c)},
                                        pol, metrics, cfg, baseLeading, rowTop, fr->lines);
        if (fr->lines.size() == before) {
          // an empty cell still holds its place in content text: an empty
          // line carrying its separator (no items, no height of its own)
          Fragment e;
          e.unitIdx = b.unit;
          e.cellIdx = (i32)(r * td.cols + c);
          e.y = (Su)rowTop;
          e.left = cellX;
          e.width = cellW;
          e.height = baseLeading;
          e.baseline = baseLeading / 2;
          e.ragged = true;
          e.sep = pol.endSep;
          e.anchor = cell.anchor;
          e.srcSpan = cell.span.empty() ? Span{b.span.start, b.span.start} : cell.span;
          e.spanned = true;
          fr->lines.push_back(e);
        }
        if (cy > rowBottom) rowBottom = cy;
      }
      py = rowBottom + padY;
      addRule(py);
    }
    leave(b, l);
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
      if (l.cellIdx >= 0) {
        appendf(out, "  L%zu cell=%d y=%dsu left=%dsu w=%dsu blocks=[%u,%u)%s\n",
                i, l.cellIdx, l.y, l.left, l.width, l.blockBegin, l.blockEnd,
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
      const LayoutBlock& b = t.blocks[t.leaves[v.unit]];
      appendf(out, "  box unit=%u %s y=%dsu h=%dsu%s\n", v.unit, traitsName(b.traits), v.y, v.h,
              v.out ? " out-of-flow" : "");
    }
  }
  return out;
}

}  // namespace tsr
