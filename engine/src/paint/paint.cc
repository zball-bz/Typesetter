#include "displaylist.h"

#include "../measure/face.h"
#include "../shape/objects.h"
#include "../shape/textrules.h"

namespace tsr {

namespace {

// A line's runs (plan P1-12 run instances): a DOM run opens where the run
// changes; a punctuation glyph, a pinned box and an inline object are runs
// of their own; spacer glue and the indent paint as spacers; blanks fold
// into their glyph's squeeze and InterChar gaps into letter-spacing.
void lineRuns(const Fragment& l, const HList& h, std::vector<DLRun>& out) {
  const std::vector<HItem>& v = h.items;
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
  // a run opened at its first carrier: style, link, inline anchor, source
  auto run = [&](const HItem& first, DLRun::K k) {
    const RunRec& r = h.runs[first.run];
    const ColdRec& c = h.cold[first.cold];
    DLRun d;
    d.k = k;
    d.face = r.face;
    d.link = r.link;
    if (first.attrs & IA_Anchor) d.id = c.anchor;  // inline anchor (footnote marker)
    d.synRef = r.syn == SynKind::Ref;
    d.copy = r.copy;  // (plan P3-07) its copy policy, decided at emit
    d.synName = r.synName;
    d.copyText = r.copyText;
    d.copyGroup = r.copyGroup;
    d.error = r.error;
    if (c.srcEnd > c.srcStart) d.dataS = c.srcStart;
    return d;
  };
  u32 i = l.itemBegin;
  while (i < l.itemEnd) {
    const HItem& it = v[i];
    if (it.k == IK::Penalty) {
      i++;
      continue;
    }
    const RunRec& r = h.runs[it.run];
    if (it.k == IK::Box && r.rc == RealizeClass::Object) {  // an inline object part: its painter
      const AdvanceSpec& sp = h.specs[it.aux];
      const ObjPart& pt = h.parts[sp.obj];
      const InlineObject& ob = h.objs[pt.obj];
      const ColdRec& c = h.cold[it.cold];
      DLRun d;
      d.span = Span{c.srcStart, c.srcEnd};
      switch (ob.kind) {
        case ObjKind::Math:  // one box, baseline via vertical-align
          d.k = DLRun::K::Math;
          d.math = pt.math;
          d.src = ob.src;  // (plan P3-26) every part: its formula's source, copied once per group
          d.face = h.runs[it.run].face;  // its paint style: colour, link (plan P1-25)
          d.link = h.runs[it.run].link;
          break;
        case ObjKind::Image:  // on the baseline; a dashed placeholder when unsized
          d.k = DLRun::K::Image;
          d.src = ob.src;
          d.alt = ob.alt;
          d.w = pt.w;
          d.h = pt.asc;
          break;
        case ObjKind::Raw:  // handler-declared markup in a box of its size
          d.k = DLRun::K::Raw;
          d.src = ob.src;
          d.w = pt.w;
          d.h = pt.asc + pt.desc;
          d.desc = pt.desc;  // (plan P3-28) a host box's measured baseline
          break;
        case ObjKind::Error:  // its text, in its run's style
          d = run(it, DLRun::K::Glyph);
          d.text = sp.str;
          break;
      }
      out.push_back(d);
      i++;
      continue;
    }
    // final hyphen glyph: inside its word's link
    if (it.k == IK::Disc) {
      if (i == l.itemEnd - 1 && l.endsWithHyphen) {
        DLRun d = run(it, DLRun::K::Hyphen);
        d.syn = "hyphen";
        out.push_back(d);
      }
      i++;
      continue;
    }
    const bool indentBox = it.k == IK::Box && r.syn == SynKind::Indent;
    const bool fill = it.k == IK::Glue && it.cls == (u8)GC::Fill;  // (plan P2-16)
    const bool spacer = it.k == IK::Glue && (it.cls == (u8)GC::Autospace || it.cls == (u8)GC::ObjectSpace || fill);
    if (indentBox || spacer) {
      DLRun d;
      d.k = DLRun::K::Spacer;
      d.syn = indentBox ? "indent" : fill ? "fill" : "boundary";
      d.widthPx = h.cold[it.cold].rawPx;
      if (fill) d.widthPx += l.fillPx;
      else if (spacer) d.widthPx += l.wordDeltaPx * (double)it.x;
      out.push_back(d);
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
      DLRun d = run(it, DLRun::K::Glyph);
      d.cls = halfPresent ? nullptr : (open ? "tsr-sqL" : "tsr-sqR");
      d.text = h.specs[it.aux].str;
      out.push_back(d);
      i++;
      continue;
    }
    // —/…… defined-width box (1em single, 2em pair — App C): the engine
    // ASSUMES the defined advance instead of measuring, ENFORCED as an
    // inline-block width (Gecko does not full-width-ize CJK dashes); its
    // stretch gap becomes a margin
    if (it.k == IK::Box && r.rc == RealizeClass::Pinned) {
      DLRun d = run(it, DLRun::K::Glyph);
      d.fit = DLRun::Fit::Pinned;
      d.widthPx = h.cold[it.cold].rawPx;
      d.marginRight = l.cjkDeltaPx != 0 && isGap(nextOnLine(i));
      d.marginRightPx = l.cjkDeltaPx;
      d.text = h.specs[it.aux].str;
      out.push_back(d);
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
      DLRun d = run(it, DLRun::K::Chars);
      d.i = i;
      d.j = j;
      if (l.cjkDeltaPx != 0) {
        d.fit = DLRun::Fit::LetterSpacing;
        d.letterPx = l.cjkDeltaPx;
        // negate via the value, never by prepending '-'
        d.marginRight = !isGap(last);
        d.marginRightPx = -l.cjkDeltaPx;
      }
      out.push_back(d);
      i = j;
      continue;
    }
    // a Latin run: words and spaces. Mid-line hyphen points paint nothing,
    // so the pieces sit in ONE text node and the browser kerns across the
    // junction — the Disc's unbroken width modelled it
    DLRun d = run(it, DLRun::K::Words);
    d.i = i;
    d.j = j;
    out.push_back(d);
    i = j;
  }
}

// a code row's segments: each run's slice of the joined line, split by
// script for snap-kerning (verbatim §3) with its letter-spacing
void codeRuns(const Fragment& l, const GridData& g, const Interner& strs, std::vector<DLRun>& out) {
  if (l.codeCont && l.contCols > 0) {
    // grid-exact continuation indent: REAL spaces in the same mono flow
    // (exact ch in ANY font), synthetic for both copy paths
    DLRun d;
    d.k = DLRun::K::CodeCont;
    d.face = g.codeStyle;
    d.syn = "cont";
    d.n = l.contCols;
    out.push_back(d);
  }
  const bool snap = l.snapLatinPx > 0 || l.snapCjkPx > 0;
  u32 off = 0;
  // (a two-track table's row holds its slice of the block's lines: P3-11)
  if (l.codeLine < g.firstLine || l.codeLine - g.firstLine >= g.lines.size()) return;
  for (const CodeRun& r : g.lines[l.codeLine - g.firstLine]) {
    std::string_view t0 = strs.get(r.text);
    u32 rLo = off, rHi = off + (u32)t0.size();
    off = rHi;
    u32 lo = l.cbLo > rLo ? l.cbLo : rLo;
    u32 hi = l.cbHi < rHi ? l.cbHi : rHi;
    if (lo >= hi) continue;
    std::string_view seg = t0.substr(lo - rLo, hi - lo);
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
      DLRun d;
      d.k = DLRun::K::CodeText;
      d.face = r.style;
      d.seg = seg.substr(s0, s1 - s0);
      const double px = snap ? (cjk ? l.snapCjkPx : l.snapLatinPx) : 0;
      if (px > 0) {
        d.fit = DLRun::Fit::LetterSpacing;
        d.letterPx = px;
      }
      out.push_back(d);
      s0 = s1;
    }
  }
}

}  // namespace

DLRoot paintRoot(const PaintSettings& cfg, const LayoutResult* lr) {
  DLRoot r;
  // (plan P3-16) the document's extent past its last block (a trailing
  // float): its min-height
  if (lr && !lr->paras.empty()) {
    const ParaFrame& last = lr->paras.back();
    if (lr->docHeightSu > (i64)last.y + last.h) r.minHeightPx = suToPx((Su)lr->docHeightSu);
  }
  r.lang = cfg.lang;
  r.fontBody = familyFor(cfg, false, Script::Latin);
  r.fontCjk = familyFor(cfg, false, Script::Cjk);
  r.fontMono = familyFor(cfg, true, Script::Latin);
  r.fontMonoCjk = familyFor(cfg, true, Script::Cjk);
  r.basePx = cfg.baseSizePx;
  return r;
}

void paintBlock(const LayoutResult& lr, size_t p, const std::vector<TopBlock>& tops, const Interner& strs,
                const PaintSettings& cfg, DLBlock& out, const StyleTable* styles, const MetricStore* metrics) {
  const ParaFrame& fr = lr.paras[p];
  const TopBlock& tb = tops[p];
  const TopTree& tree = *tb.tree;
  out.pid = fr.pid;
  out.h = fr.h;
  // (plan P3-16; D-Y08) the gap to the next block: layout's, in su — the one
  // vertical authority (a float's block takes none)
  out.gapAfterPx = p + 1 < lr.paras.size() ? suToPx((Su)((i64)lr.paras[p + 1].y - ((i64)fr.y + fr.h))) : -1;
  out.nodes.clear();
  out.runs.clear();
  // Source anchors are PARA-RELATIVE (base in data-s0 on the container): an
  // edit then leaves every untouched paragraph byte-identical in serialized
  // form, so the editing loop patches exactly the damaged paragraphs
  // (shell.mjs patchIn). Paged output keeps absolute offsets.
  u32 srcBase = 0xFFFFFFFFu;
  for (const Fragment& l : fr.lines)
    if ((!l.srcSpan.empty() || l.spanned) && l.srcSpan.start < srcBase) srcBase = l.srcSpan.start;
  out.srcBase = srcBase == 0xFFFFFFFFu ? 0 : srcBase;
  out.scrollX = fr.overflowR > fr.w;
  out.role = tb.tree && tb.tree->role ? strs.get(tb.tree->role) : std::string_view{};
  for (size_t li = 0; li < fr.lines.size(); li++) {
    const Fragment& l = fr.lines[li];
    const LayoutBlock& b = tree.blocks[tree.leaves[l.unitIdx]];
    const FlowUnit& u = tb.units[l.unitIdx];
    DLNode n;
    n.kind = l.kind;
    n.y = l.y;
    n.left = l.left;
    n.width = l.width;
    n.anchor = l.anchor;
    n.anchor2 = l.anchor2;
    n.baseline = l.baseline;
    n.span = l.srcSpan;
    n.spanned = l.spanned;
    n.runBegin = n.runEnd = (u32)out.runs.size();
    switch (l.kind) {
      case FragKind::Rule:
        n.y = l.y + l.height / 2;  // the rule at its band's middle
        break;
      case FragKind::Frame:  // (plan P3-14) its block's box, under its content
        n.heightPx = suToPx(l.height);
        n.box = l.boxBlock < tree.blocks.size() ? &tree.blocks[l.boxBlock].box : nullptr;
        break;
      case FragKind::Raw: {
        const RawData& r = std::get<RawData>(u.data);
        n.heightPx = l.hostBox ? suToPx(l.height) : r.size.h;  // (plan P3-28) measured: layout's
        n.src = r.html;
        break;
      }
      case FragKind::Math: {
        // display math (§8): the row's advance; the formula on the row's
        // baseline, which layout set (plan P3-26: its number is a line of
        // the leaf's tag track, measured and placed by layout)
        const MathData& m = std::get<MathData>(u.data);
        n.math = l.mathRow < m.rows.size() ? m.rows[l.mathRow] : m.box;  // (plan P3-29) its row
        n.mathSrc = m.src;
        n.markerStyle = m.style;  // its paint style (colour)
        n.heightPx = suToPx(l.height);
        n.mathTopPx = suToPx(l.baseline - n.math->asc);
        // (plan P3-27, D-R04) role=math, aria-label — on its first row; its
        // later rows (plan P3-29) hidden, so it is read once
        n.mathLabel = cfg.a11yMathLabel && l.mathRow == 0;
        n.mathHidden = cfg.a11yMathLabel && l.mathRow > 0;
        n.ragged = true;
        break;
      }
      case FragKind::Image: {
        const ImageData& im = std::get<ImageData>(u.data);
        n.src = im.src;
        n.alt = im.alt;
        n.heightPx = suToPx(l.height);
        break;
      }
      case FragKind::CodeRow: {
        const GridData& g = std::get<GridData>(u.data);
        n.hl = l.codeHl;
        // code rows are ragged by nature; a wrapped row rejoins its
        // continuation (layout's separator, §9.3)
        n.ragged = true;
        n.join = sepName(l.sep);
        // its text.features (plan P3-02: code.fontFeatures and the per-language
        // map are rules on code blocks)
        n.features = g.features ? strs.get(g.features) : std::string_view{};  // interned: lives with the doc
        if (l.height > 0) {
          // the baseline centred in the row; hl rows also paint height
          n.lineHeightPx = suToPx(l.height);
          if (l.codeHl) n.heightPx = suToPx(l.height);
        }
        n.rowStyle = g.codeStyle;  // (plan P3-19) its strut: the code face
        n.marker = l.marker;
        n.markerRole = l.markerRole;
        n.markerStyle = l.markerStyle;
        codeRuns(l, g, strs, out.runs);
        break;
      }
      case FragKind::Line: {
        n.join = sepName(l.sep);
        n.ragged = l.ragged || l.noGlue;
        // (plan P3-07) a second track's line: a table cell, a code block's
        // sidecar row, a float's caption row (data-cell retired)
        if (l.gridCell >= 0 && l.table != ~0u) {  // a table cell — a code block's notes say so (plan P3-11)
          const TableSpec& ts = tree.tables[tree.blocks[l.table].spec];
          const bool side = !ts.cols.empty() && ts.cols[(size_t)l.gridCell % ts.cols.size()].sidecar;
          n.track = side ? "sidecar" : "cell";
        } else if (l.cellIdx >= 0) {
          // a float's caption row; (plan P3-26) a display formula's number
          n.track = b.painter == Painter::MathRow ? "tag" : "caption";
        }
        n.overfull = l.overfull;
        n.wordSpacingPx = l.wordDeltaPx;
        n.marker = l.marker;
        n.markerRole = l.markerRole;
        n.markerStyle = l.markerStyle;
        n.h = l.cellIdx >= 0 ? &u.cells[(size_t)l.cellIdx].hl : &u.hl;
        lineRuns(l, *n.h, out.runs);
        // (plan P3-27, D-R04) its formulas' accessible names
        if (cfg.a11yMathLabel)
          for (size_t k = n.runBegin; k < out.runs.size(); k++)
            if (out.runs[k].k == DLRun::K::Math) out.runs[k].mathLabel = true;
        // (plan P3-19; design T7 S11) a run in a user font family: its
        // face's content height as its line-height (the contract gives the
        // roles theirs), so its line box is its content area
        if (styles && metrics)
          for (size_t r = n.runBegin; r < out.runs.size(); r++) {
            DLRun& d = out.runs[r];
            const Styling& st = styles->get(d.face);
            if (!st.fontFamily || !metrics->hasVmet(d.face)) continue;
            const VMet& v = metrics->vmet(d.face);
            const double em = emPx(cfg.baseSizePx, st);
            if (em > 0) d.lh = (float)(suToPx(v.ascent + v.descent) / em);
          }
        break;
      }
    }
    (void)b;
    n.runEnd = (u32)out.runs.size();
    out.nodes.push_back(n);
  }
}

std::string dumpDisplayList(const LayoutResult& lr, const std::vector<TopBlock>& tops, const StyleTable& styles,
                            const Interner& strs, const PaintSettings& cfg) {
  static const char* const kKind[] = {"line", "rule", "code", "raw", "math", "image"};
  static const char* const kRun[] = {"words", "chars", "glyph", "hyphen", "spacer",
                                     "math",  "image", "raw",   "code",   "cont"};
  (void)styles;
  std::string out;
  DLBlock b;
  for (size_t p = 0; p < lr.paras.size(); p++) {
    paintBlock(lr, p, tops, strs, cfg, b);
    appendf(out, "block pid=%u s0=%u h=%dsu\n", b.pid, b.srcBase, b.h);
    for (const DLNode& n : b.nodes) {
      appendf(out, "  %s y=%dsu left=%dsu w=%dsu base=%dsu", kKind[(size_t)n.kind], n.y, n.left, n.width, n.baseline);
      if (n.anchor) {
        out += " id=\"";
        appendEscaped(out, strs.get(n.anchor));
        out += "\"";
      }
      if (n.join) appendf(out, " join=%s", n.join);
      if (n.ragged) out += " ragged";
      if (n.track) appendf(out, " track=%s", n.track);
      if (n.marker) {
        out += " marker=\"";
        appendEscaped(out, strs.get(n.marker));
        out += "\"";
      }
      out += "\n";
      for (u32 r = n.runBegin; r < n.runEnd; r++) {
        const DLRun& d = b.runs[r];
        appendf(out, "    %s", kRun[(size_t)d.k]);
        std::string text;
        if (d.k == DLRun::K::Words || d.k == DLRun::K::Chars)
          for (u32 k = d.i; k < d.j; k++) {
            const HItem& it = n.h->items[k];
            if (it.k == IK::Box) text += strs.get(n.h->specs[it.aux].str);
            else if (it.k == IK::Glue && d.k == DLRun::K::Words) text += ' ';
          }
        else if (d.k == DLRun::K::Glyph) text = strs.get(d.text);
        else if (d.k == DLRun::K::CodeText) text = d.seg;
        if (!text.empty()) {
          out += " \"";
          appendEscaped(out, text);
          out += "\"";
        }
        if (d.link) out += " link";
        if (d.id) out += " id";
        if (d.synRef) out += " ref";
        if (d.syn) appendf(out, " syn=%s", d.syn);
        if (d.fit == DLRun::Fit::LetterSpacing) out += " letter-spaced";
        if (d.fit == DLRun::Fit::Pinned) out += " pinned";
        out += "\n";
      }
    }
  }
  return out;
}

}  // namespace tsr
