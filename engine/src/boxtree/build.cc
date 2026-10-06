#include "build.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <utility>

#include "../code/overlay.h"
#include "../elements/registry.h"
#include "../model/cascade.h"
#include "../semantic/locale.h"

namespace tsr {

namespace {

using Align = BlockTraits::Align;
// what a block is: its name in the dumps (its traits are its node's block
// properties since plan P3-01: engine/data/defaults.json gives a list's
// child gap of a third, display blocks set ragged and unhyphenated, a
// caption centred, a heading and a block image keeping with what follows)
constexpr const char* kTraitNames[] = {"root",  "para",  "caption", "heading", "list",  "item",
                                       "quote", "group", "figure",  "code",    "table", "image",
                                       "float", "math",  "raw",     "rule",    "error", "marker", "cell",
                                       "equations"};
static_assert(sizeof kTraitNames / sizeof kTraitNames[0] == (size_t)TraitsId::N, "one name per TraitsId");

// What a group means to the box tree (finding emitter/figure-role-string-
// dispatch): its element class's box trait (plan P2-05) — no role is read
// by name; a code block's sidecar lines are its margin slot (plan P2-13).
struct RoleInfo {
  TraitsId traits = TraitsId::Group;
  bool captions = false;  // its paragraphs are captions; an image with a side floats
};
// a figure-class group (its class's box trait, plan P2-05)
constexpr RoleInfo kFigureRole{TraitsId::Figure, true};
// image sides (figure-design.md §4): 1 left, 2 right
constexpr const char* kSides[] = {"left", "right"};

class Builder {
 public:
  Builder(Interner& s, StyleTable& st, const NodePropsTable& np, const BoxTreeSettings& c, const Registry& r)
      : strs(s), styles(st), cfg(c), reg(r), props(np) {
    for (const char* side : kSides) sideRefs.push_back(s.find(side));
  }

  void top(const ContentNode* child, TopTree& tt, std::vector<LeafSource>& src) {
    t = &tt;
    leaves = &src;
    u32 root = open(LayouterId::Stack, Painter::None, TraitsId::Root, child, ~0u, 0);
    walk(child, root, 0, 0);
    close(root);
    // (plan P3-14) the boxes: a framed block's descendants stand inside its
    // padding and border, at both edges
    std::vector<Su> inL(tt.blocks.size(), 0);  // the start insets around each block
    for (u32 i = 0; i < (u32)tt.blocks.size(); i++) {
      LayoutBlock& b = tt.blocks[i];
      if (b.parent == ~0u) continue;
      const LayoutBlock& p = tt.blocks[b.parent];
      if (p.layouter == LayouterId::Table) continue;  // a cell: a flow root of its own (layout places it)
      inL[i] = inL[b.parent] + p.box.inset(3);
      b.x += inL[i];
      b.xr = p.xr + p.box.inset(1);
    }
    // anchors (finding emitter/anchor-opt-in-per-kind): an anchored block's
    // label rides the first leaf of its subtree whatever its kind (a marker
    // alone carries none); children come before parents in reverse
    // pre-order, so a deeper label is the leaf's id and the enclosing one
    // rides along as its second
    for (u32 i = (u32)tt.blocks.size(); i-- > 0;) {
      const LayoutBlock& b = tt.blocks[i];
      if (!b.anchor) continue;
      for (u32 j = i; j < b.end; j++) {
        LayoutBlock& l = tt.blocks[j];
        if (!l.leaf() || l.traits == TraitsId::Marker) continue;
        if (!l.carry) l.carry = b.anchor;
        else if (!l.carry2) l.carry2 = b.anchor;
        else tt.lostAnchors++;
        break;
      }
    }
    // (plan P3-07; design T6 Sep) what follows each leaf in content text:
    // what native copy of the semantic page gives — a paragraph, heading,
    // code block, table or formula is set apart (Para), the one paragraph
    // of a tight item is inlined in its <li> (Newline: the next item starts
    // a line); the top's last leaf ends the block
    for (size_t k = 0; k < tt.leaves.size(); k++) {
      const u32 i = tt.leaves[k];
      LayoutBlock& l = tt.blocks[i];
      if (k + 1 == tt.leaves.size()) {
        l.sepAfter = Sep::Newline;
        continue;
      }
      const LayoutBlock* p = l.parent != ~0u ? &tt.blocks[l.parent] : nullptr;
      const bool tightItem = p && p->traits == TraitsId::Item && l.parent + 1 == i && p->end == i + 1 &&
                             (l.traits == TraitsId::Para || l.traits == TraitsId::Marker);
      l.sepAfter = tightItem ? Sep::Newline : Sep::Para;
    }
  }

 private:
  Interner& strs;
  StyleTable& styles;
  BoxTreeSettings cfg;
  const Registry& reg;
  const NodePropsTable& props;
  std::vector<StrRef> sideRefs;
  TopTree* t = nullptr;
  std::vector<LeafSource>* leaves = nullptr;
  int figDepth = 0;  // inside a captions role: paragraphs are captions
  // (plan P3-34) the next paragraph's run-in term and its hanging indent
  const ContentNode* runIn_ = nullptr;
  Su runInHang_ = 0;
  bool pendingBreak = false;  // (plan P3-14) for the next block opened

  // a block length (plan P3-01): em of the block's own size
  Su lenSu(const Len& l, const ContentNode* n) const {
    if (!l.unit) return 0;
    return suRoundPx(l.unit == 2 ? (double)l.v : (double)l.v * emPx(cfg.baseSizePx, styles.get(n->style)));
  }

  const RoleInfo* roleOf(const ContentNode* n) const {
    if (n->cls && reg.cls(n->cls).box == ElementClass::Box::Figure) return &kFigureRole;
    return nullptr;
  }

  // an image's float side: its place.float (plan P3-14; its `side`
  // argument is that trait's declared alias), 1 left, 2 right
  u8 sideOf(const ContentNode* n) const {
    const u8 pf = props.get(n->props).placeFloat;
    if (pf == PLACEFLOAT_LEFT || pf == PLACEFLOAT_RIGHT) return pf == PLACEFLOAT_LEFT ? 1 : 2;
    if (pf == PLACEFLOAT_NONE) return 0;
    StrRef r = attrStr(n, ArgK::side);
    for (size_t i = 0; r && i < sideRefs.size(); i++)
      if (sideRefs[i] == r) return (u8)(i + 1);
    return 0;
  }
  static StrRef labelOf(const ContentNode* n) { return attrStr(n, ArgK::label); }

  u32 open(LayouterId l, Painter p, TraitsId tr, const ContentNode* n, u32 parent, Su x) {
    LayoutBlock b;
    b.layouter = l;
    b.painter = p;
    b.traits = tr;
    b.parent = parent;
    b.x = x;
    if (n) {
      b.span = n->span;
      b.insertAt = n->insertAt;
      // a label is universal (plan P2-05): any block a node opens carries it
      // — not the top-level wrapper, which only borrows its child's span
      if (tr != TraitsId::Root) b.anchor = labelOf(n);
    }
    // its traits: its node's block properties (plan P3-01); the top-level
    // wrapper's gap is one paragraph gap
    if (tr == TraitsId::Root) {
      b.tr.gapNum = b.tr.gapDen = 1;
    } else if (n) {
      traitsOf(n, b.tr);
      boxOf(n, b.box);
    }
    // (plan P3-14) a page break an empty block asked for (#pagebreak())
    // lands on the next block
    if (pendingBreak) {
      b.tr.breakBefore = true;
      pendingBreak = false;
    }
    t->blocks.push_back(b);
    return (u32)t->blocks.size() - 1;
  }
  // (plan P3-30) whether a language's words hyphenate (its locale pack),
  // once per language: a run's own (text.lang), else the document's
  std::unordered_map<StrRef, bool> hyphenates_;
  bool hyphenates(StrRef lang) {
    auto it = hyphenates_.find(lang);
    if (it != hyphenates_.end()) return it->second;
    return hyphenates_[lang] = localeHyphenates(lang ? strs.get(lang) : std::string_view(cfg.lang));
  }
  // a node's block properties (plan P3-01) as the traits layout reads
  void traitsOf(const ContentNode* n, BlockTraits& tr) {
    const NodeProps& np = props.get(n->props);
    tr.gapNum = np.blockGap.num;
    tr.gapDen = np.blockGap.den;
    if (np.blockGap.len.unit) tr.gapSu = lenSu(np.blockGap.len, n);
    tr.align = np.parAlign == PARALIGN_START    ? Align::Ragged
               : np.parAlign == PARALIGN_CENTER ? Align::Center
               : np.parAlign == PARALIGN_END    ? Align::End
                                                : Align::Justify;
    tr.singleCenter = np.parSingleLine == PARSINGLELINE_CENTER;
    // (plan P3-30) auto: whether its language's words hyphenate (its pack)
    tr.hyphenate = np.parHyphenate == PARHYPHENATE_TRUE ||
                   (np.parHyphenate != PARHYPHENATE_FALSE && hyphenates(styles.get(n->style).lang));
    tr.keepWithNext = np.keepWithNext || np.keep == KEEP_WITH_NEXT || np.keep == KEEP_BOTH;
    tr.snapKerning = np.snapKerning;
    if (np.sidecarFrac > 0) tr.sidecarFrac = np.sidecarFrac;
    tr.contIndent = (i32)np.contIndent;
    if (np.codeOverlays) tr.codeOverlays = overlayMask(strs.get(np.codeOverlays));
    // (plan P3-14) the block trait group
    tr.keepTogether = np.keep == KEEP_TOGETHER || np.keep == KEEP_BOTH;
    tr.spaceBefore = lenSu(np.spaceBefore, n);
    tr.spaceAfter = lenSu(np.spaceAfter, n);
    tr.breakBefore = np.breakBefore == BREAKBEFORE_PAGE;
    tr.breakAfter = np.breakAfter == BREAKAFTER_PAGE;
    tr.hang = lenSu(np.parHang, n);
    if (np.parHangAfter > 0) tr.hangAfter = (u16)np.parHangAfter;
    tr.media = np.media == MEDIA_SCREEN ? 1 : np.media == MEDIA_PAGED ? 2 : 0;
    tr.shrink = np.beside == BESIDE_SHRINK;
    if (np.breakerTolerance > 0) tr.tolerance = np.breakerTolerance;
    tr.emergencyStretch = lenSu(np.breakerStretch, n);
    // (plan P3-15) its placement
    using P = BlockTraits::Place;
    tr.place = np.placeFloat == PLACEFLOAT_LEFT     ? P::Start
               : np.placeFloat == PLACEFLOAT_RIGHT  ? P::End
               : np.placeFloat == PLACEFLOAT_TOP    ? P::Top
               : np.placeFloat == PLACEFLOAT_BOTTOM ? P::Bottom
               : np.placeFloat == PLACEFLOAT_PAGE   ? P::Page
               : np.placeFloat == PLACEFLOAT_INLINE ? P::Inline
                                                    : P::Flow;
    if (np.placeWidth) {  // domain "size": a length, or a percent of the container
      const std::string_view w = strs.get(np.placeWidth);
      if (!w.empty() && w.back() == '%') tr.placeFrac = std::strtof(std::string(w.substr(0, w.size() - 1)).c_str(), nullptr) / 100.0f;
      else tr.placeW = lenSu(parseLen(w), n);
    }
    if (np.placeGap.unit) tr.placeGap = lenSu(np.placeGap, n);
  }
  // (plan P3-14) a node's box: its padding and border widths (CSS shorthand,
  // in its own em), their colours
  void boxOf(const ContentNode* n, BoxModel& box) const {
    if (const HtmlShape* sh = reg.shapeOf(n, strs); sh && sh->frame) {
      box.themed = true;
      box.role = attrStr(n, ArgK::role);
    }
    const NodeProps& np = props.get(n->props);
    auto lens = [&](StrRef v, Su out[4]) {
      if (!v) return;
      Su l[4];
      int k = 0;
      const std::string_view sv = strs.get(v);
      for (size_t at = 0; at < sv.size() && k < 4;) {
        size_t sp = sv.find(' ', at);
        if (sp == std::string_view::npos) sp = sv.size();
        l[k++] = lenSu(parseLen(sv.substr(at, sp - at)), n);
        at = sp + 1;
      }
      if (k == 0) return;
      out[0] = l[0];
      out[1] = k > 1 ? l[1] : l[0];
      out[2] = k > 2 ? l[2] : l[0];
      out[3] = k > 3 ? l[3] : out[1];
    };
    lens(np.boxPadding, box.pad);
    lens(np.boxBorder, box.border);
    box.borderColor = np.boxBorderColor;
    box.background = np.boxBackground;
  }
  // a container without leaves is dropped: it takes no gap (a page break
  // it asks for goes to the next block)
  void close(u32 i) {
    if (i + 1 == t->blocks.size() && !t->blocks[i].leaf() && i > 0) {
      pendingBreak = pendingBreak || t->blocks[i].tr.breakBefore || t->blocks[i].tr.breakAfter;
      t->blocks.pop_back();
      return;
    }
    t->blocks[i].end = (u32)t->blocks.size();
  }
  LayoutBlock& leaf(LayouterId l, Painter p, TraitsId tr, const ContentNode* n, u32 parent, Su x,
                    LeafSource s) {
    u32 i = open(l, p, tr, n, parent, x);
    t->blocks[i].unit = (u32)t->leaves.size();
    t->blocks[i].end = i + 1;
    t->leaves.push_back(i);
    leaves->push_back(std::move(s));
    return t->blocks[i];
  }

  // (plan P3-11; design T6 S10) a code block with sidecar notes: a table of
  // its logical lines — each row a one-line grid leaf beside its note's
  // paragraph (an empty cell when it has none); the block's label rides
  // its first line, a list marker too
  void codeTable(const ContentNode* n, const ContentNode* side, u32 parent, Su x, StrRef marker) {
    const u32 tb = open(LayouterId::Table, Painter::None, TraitsId::Code, n, parent, x);
    TableSpec spec;
    spec.cols = {ColSpec{0, 'l', false}, ColSpec{t->blocks[tb].tr.sidecarFrac, 'l', true}};
    spec.gapCodeEm = 1;
    spec.framed = false;
    spec.lines = true;
    spec.rules = TableSpec::Rules::None;
    const u32 tableIdx = (u32)t->tables.size();
    t->blocks[tb].spec = tableIdx;
    t->tables.push_back(std::move(spec));
    // its logical lines, as emit splits them: one text body by its line
    // breaks, else one kid per line
    u32 lines = 0;
    std::vector<const ContentNode*> body;
    for (const ContentNode* k : n->kids)
      if (k != side) body.push_back(k);
    if (body.size() == 1 && body[0]->kind == Kind::text) {
      const std::string_view text = strs.get(body[0]->str);
      lines = 1 + (u32)std::count(text.begin(), text.end(), '\n');
    } else {
      lines = (u32)body.size();
    }
    for (u32 i = 0; i < lines; i++) {
      const u32 c0 = open(LayouterId::Stack, Painter::None, TraitsId::Cell, nullptr, tb, 0);
      LeafSource s;
      s.node = n;
      s.sidecar = side;  // (not body)
      s.lineLo = i;
      s.lineHi = i + 1;
      LayoutBlock& lb = leaf(LayouterId::Grid, Painter::None, TraitsId::Code, n, c0, 0, std::move(s));
      lb.anchor = 0;  // the block's label is the table's
      if (i == 0) {
        lb.marker = marker;
        lb.markerStyle = n->style;
      }
      t->blocks[c0].end = (u32)t->blocks.size();
      const ContentNode* note = i < side->kids.size() ? side->kids[i] : nullptr;
      const u32 c1 = open(LayouterId::Stack, Painter::None, TraitsId::Cell, nullptr, tb, 0);
      if (note && !note->kids.empty()) {
        LeafSource ns;
        ns.node = note;
        leaf(LayouterId::Paragraph, Painter::None, TraitsId::Para, note, c1, 0, std::move(ns));
      }
      t->blocks[c1].end = (u32)t->blocks.size();
      CellPlace p0, p1;  // (its row: the line and its note)
      p0.row = p1.row = i;
      p1.col = 1;
      t->tables[tableIdx].place.push_back(p0);
      t->tables[tableIdx].place.push_back(p1);
    }
    t->tables[tableIdx].rows = lines;
    t->blocks[tb].end = (u32)t->blocks.size();
  }
  // (plan P3-14) a table's tracks: width[:align] by commas (model.h)
  void tracksOf(std::string_view v, const ContentNode* n, std::vector<ColSpec>& out) const {
    using K = TrackDecl::K;
    using W = ColSpec::W;
    for (const TrackDecl& t : parseTracks(v)) {
      ColSpec c;
      c.align = t.align;
      switch (t.k) {
        case K::Fr: c.fr = t.v; break;
        case K::Percent: c.percent = t.v; break;
        case K::Fixed:
          c.w = W::Fixed;
          c.fixed = lenSu(t.len, n);
          break;
        case K::Auto: c.w = W::Auto; break;
        case K::Min: c.w = W::Min; break;
        case K::Max: c.w = W::Max; break;
      }
      out.push_back(c);
    }
    if (out.empty()) out.push_back(ColSpec{});
  }

  // a table cell (plan P3-10): kept even when empty — it holds its grid
  // position; its x is the cell's own (layout places the cell)
  void cellBlock(const ContentNode* cell, u32 table) {
    const u32 cb = open(LayouterId::Stack, Painter::None, TraitsId::Cell, cell, table, 0);
    if (cell) {
      // all blocks: laid out as blocks; any inline content (a term the
      // resolver made a group of, beside text) keeps the cell one paragraph
      bool blocks = !cell->kids.empty();
      for (const ContentNode* k : cell->kids) blocks = blocks && !isInlineLevel(k->kind);
      if (blocks) {
        for (const ContentNode* k : cell->kids) walk(k, cb, 0, 0);
      } else if (!cell->kids.empty()) {
        LeafSource s;
        s.node = cell;
        leaf(LayouterId::Paragraph, Painter::None, TraitsId::Para, cell, cb, 0, std::move(s));
      }
    }
    t->blocks[cb].end = (u32)t->blocks.size();
  }

  void walk(const ContentNode* n, u32 parent, Su x, StrRef marker) {
    LeafSource s;
    s.node = n;
    switch (n->kind) {
      case Kind::para: {
        const bool caption = figDepth > 0;
        if (caption) s.role = LeafSource::Role::Caption;
        // (plan P3-34) a description item's term runs in here
        const ContentNode* runIn = std::exchange(runIn_, nullptr);
        s.runIn = runIn;
        // 首行缩进 (App C): its par.indent (plan P3-01), never on a marker's
        // line, a run-in term's, nor a paragraph's continuation after a
        // block (plan P3-17)
        const Len ind = props.get(n->props).parIndent;
        const bool cont = attrBool(n, ArgK::cont, false);
        if (ind.unit && ind.v > 0 && marker == 0 && !runIn && !cont) s.paraIndent = ind;
        LayoutBlock& b = leaf(LayouterId::Paragraph, Painter::None, caption ? TraitsId::Caption : TraitsId::Para,
                              n, parent, x, std::move(s));
        b.marker = marker;
        b.markerStyle = n->style;
        b.tr.cont = cont;
        if (runIn && !b.tr.hang) b.tr.hang = runInHang_;  // its lines after the first hang in
        return;
      }
      case Kind::heading: {
        LayoutBlock& b = leaf(LayouterId::Paragraph, Painter::None, TraitsId::Heading, n, parent, x, std::move(s));
        b.marker = marker;
        b.markerStyle = n->style;
        return;
      }
      case Kind::list: {
        const bool ordered = attrBool(n, ArgK::ordered, false);
        int num = attrInt(n, ArgK::start, 1);
        u32 list = open(LayouterId::Stack, Painter::None, TraitsId::List, n, parent, x);
        const Su pad = lenSu(props.get(n->props).blockIndent, n);  // its block.indent (plan P3-01)
        t->blocks[list].pad = pad;
        // an item's marker: the number the resolver gave it (plan P3-03:
        // counter-backed, SemInfo.number), else its list.marker, else its
        // place in the list
        const StrRef bullet = props.get(n->props).listMarker;
        for (const ContentNode* item : n->kids) {
          StrRef mref = item->number;
          if (!mref) mref = !ordered && bullet ? bullet : strs.intern(ordered ? std::to_string(num) + "." : "\xE2\x80\xA2");  // •
          num++;
          u32 it = open(LayouterId::Stack, Painter::None, TraitsId::Item, item, list, x + pad);
          bool first = true;
          for (const ContentNode* k : item->kids) {
            walk(k, it, x + pad, first ? mref : 0);  // the marker rides the item's first block
            first = false;
          }
          if (item->kids.empty()) {  // an empty item still shows its marker
            LeafSource ms;
            ms.role = LeafSource::Role::MarkerOnly;
            ms.node = item;
            LayoutBlock& b =
                leaf(LayouterId::Paragraph, Painter::None, TraitsId::Marker, item, it, x + pad, std::move(ms));
            b.marker = mref;
            b.markerStyle = item->style;
          }
          close(it);
        }
        close(list);
        return;
      }
      case Kind::terms: {
        // (plan P3-34; D-L08) a description list: an item's term (its part in
        // slot term; bold by the slot's default rule) runs in at the start of
        // its first paragraph, whose lines after the first hang block.indent
        // in (2em: the kind's default rule), as its later blocks stand; a term
        // with no paragraph first is a paragraph of its own
        const u32 list = open(LayouterId::Stack, Painter::None, TraitsId::List, n, parent, x);
        const Su hang = lenSu(props.get(n->props).blockIndent, n);
        for (const ContentNode* item : n->kids) {
          const u32 it = open(LayouterId::Stack, Painter::None, TraitsId::Item, item, list, x);
          const ContentNode* term = nullptr;
          for (const ContentNode* k : item->kids)
            if (slotOf(k, strs) == SlotId::Term) {
              term = k;
              break;
            }
          auto termAlone = [&] {
            LeafSource ts;
            ts.node = term;
            LayoutBlock& b = leaf(LayouterId::Paragraph, Painter::None, TraitsId::Para, term, it, x, std::move(ts));
            if (!b.tr.hang) b.tr.hang = hang;
          };
          bool first = true;
          for (const ContentNode* k : item->kids) {
            if (k == term) continue;
            if (first && term) {
              if (k->kind == Kind::para) {
                runIn_ = term;
                runInHang_ = hang;
                walk(k, it, x, 0);
                runIn_ = nullptr;
              } else {
                termAlone();
                walk(k, it, x + hang, 0);
              }
            } else {
              walk(k, it, x + hang, 0);
            }
            first = false;
          }
          if (first && term) termAlone();  // a term without a description
          close(it);
        }
        close(list);
        return;
      }
      case Kind::equations: {  // (plan P3-29, D-S11) display rows a jot apart, aligned at their `&`
        const u32 e = open(LayouterId::Stack, Painter::None, TraitsId::Equations, n, parent, x);
        for (const ContentNode* k : n->kids) walk(k, e, x, 0);
        close(e);
        return;
      }
      case Kind::quote: {
        u32 q = open(LayouterId::Stack, Painter::None, TraitsId::Quote, n, parent, x);
        const Su pad = lenSu(props.get(n->props).blockIndent, n);  // its block.indent (plan P3-01)
        t->blocks[q].pad = pad;
        for (const ContentNode* k : n->kids) walk(k, q, x + pad, 0);
        close(q);
        return;
      }
      case Kind::codeblock: {
        // sidecar notes (verbatim-design §5): the lines of its margin slot
        // (plan P2-13: group{slot: margin}, made by the default fence) make
        // the block a two-track table (plan P3-11)
        for (const ContentNode* k : n->kids)
          if (k->kind == Kind::group && slotOf(k, strs) == SlotId::Margin) s.sidecar = k;
        if (s.sidecar && !s.sidecar->kids.empty()) {
          codeTable(n, s.sidecar, parent, x, marker);
          return;
        }
        LayoutBlock& b = leaf(LayouterId::Grid, Painter::None, TraitsId::Code, n, parent, x, std::move(s));
        b.marker = marker;
        b.markerStyle = n->style;  // mono at code.scale: the cascade's (plan P3-01)
        return;
      }
      case Kind::rule:
        leaf(LayouterId::Replaced, Painter::Rule, TraitsId::Rule, n, parent, x, std::move(s));
        return;
      case Kind::table: {
        // (plan P3-10; design T6 S9) a grid of flow roots: one Cell block per
        // position, its content laid out by the ordinary layouters at the
        // column's width (inline content is one paragraph; block content its
        // blocks) — no longer flattened into one inline stream
        const u32 tb = open(LayouterId::Table, Painter::None, TraitsId::Table, n, parent, x);
        TableSpec spec;
        // its columns (plan P3-14): the tracks attribute, else v1's `cols`
        // equal ones aligned by the `align` letters
        if (StrRef tr = attrStr(n, ArgK::tracks)) {
          tracksOf(strs.get(tr), n, spec.cols);
        } else {
          const int cols = attrInt(n, ArgK::cols, 1);
          const StrRef al = attrStr(n, ArgK::align);
          const std::string_view a = al ? strs.get(al) : std::string_view{};
          for (u32 c = 0; c < (u32)(cols < 1 ? 1 : cols); c++)
            spec.cols.push_back(ColSpec{0, c < a.size() ? (u8)a[c] : (u8)'l', false});
        }
        const int rules = attrEnum(n, ArgK::rules, strs);  // grid | booktabs | none
        spec.rules = rules == 2 ? TableSpec::Rules::Booktabs : rules == 3 ? TableSpec::Rules::None : TableSpec::Rules::Grid;
        // (HTML's table model) each cell at its row's first free position,
        // covering its spans; a row longer than the columns widens the table
        struct At {
          const ContentNode* cell;
          CellPlace p;
        };
        std::vector<At> at;
        std::vector<std::vector<char>> used;
        u32 r = 0;
        for (const ContentNode* row : n->kids) {
          if (row->kind != Kind::trow) continue;
          if (used.size() <= r) used.resize(r + 1);
          u32 c = 0;
          for (const ContentNode* cell : row->kids) {
            if (cell->kind != Kind::tcell) continue;
            while (c < used[r].size() && used[r][c]) c++;
            CellPlace p;
            p.row = r;
            p.col = c;
            p.colspan = (u32)std::max(1, attrInt(cell, ArgK::colspan, 1));
            p.rowspan = (u32)std::max(1, attrInt(cell, ArgK::rowspan, 1));
            const std::string_view al = strs.get(attrStr(cell, ArgK::align));
            p.halign = al.size() == 1 ? (u8)al[0] : 0;
            const int va = attrEnum(cell, ArgK::valign, strs);  // top | middle | bottom
            p.valign = va == 2 ? CellPlace::V::Middle : va == 3 ? CellPlace::V::Bottom : CellPlace::V::Top;
            for (u32 rr = r; rr < r + p.rowspan; rr++) {
              if (used.size() <= rr) used.resize(rr + 1);
              if (used[rr].size() < c + p.colspan) used[rr].resize(c + p.colspan, 0);
              for (u32 cc = c; cc < c + p.colspan; cc++) used[rr][cc] = 1;
            }
            at.push_back({cell, p});
            c += p.colspan;
          }
          r++;
        }
        // rows: as written (a span past the last is cut to it)
        const u32 nrows = r;
        u32 ncols = (u32)spec.cols.size();
        for (At& a : at) {
          a.p.rowspan = std::min(a.p.rowspan, nrows - a.p.row);
          ncols = std::max(ncols, a.p.col + a.p.colspan);
        }
        while (spec.cols.size() < ncols) spec.cols.push_back(ColSpec{0, 'l', false});
        // a position no cell covers holds an empty one
        for (u32 rr = 0; rr < nrows; rr++)
          for (u32 cc = 0; cc < ncols; cc++)
            if (rr >= used.size() || cc >= used[rr].size() || !used[rr][cc]) {
              CellPlace p;
              p.row = rr;
              p.col = cc;
              at.push_back({nullptr, p});
            }
        std::stable_sort(at.begin(), at.end(), [](const At& a, const At& b) {
          return a.p.row != b.p.row ? a.p.row < b.p.row : a.p.col < b.p.col;
        });
        spec.rows = nrows;
        spec.header = (u32)std::clamp(attrInt(n, ArgK::header, 0), 0, (int)nrows);
        for (const At& a : at) spec.place.push_back(a.p);
        t->blocks[tb].spec = (u32)t->tables.size();
        t->tables.push_back(std::move(spec));
        for (const At& a : at) cellBlock(a.cell, tb);
        t->blocks[tb].end = (u32)t->blocks.size();
        return;
      }
      case Kind::raw:
        leaf(LayouterId::Replaced, Painter::Raw, TraitsId::Raw, n, parent, x, std::move(s));
        return;
      case Kind::image: {
        const u8 side = sideOf(n);
        LayoutBlock& b = leaf(LayouterId::Replaced, Painter::Image, side ? TraitsId::Float : TraitsId::Image, n,
                              parent, x, std::move(s));
        b.floatSide = side;
        return;
      }
      case Kind::mathblock: {
        // (plan P3-26) its tag part (an equation number): its second track
        for (const ContentNode* k : n->kids)
          if (slotOf(k, strs) == SlotId::Tag) s.rows.push_back(k);
        leaf(LayouterId::Replaced, Painter::MathRow, TraitsId::Math, n, parent, x, std::move(s));
        return;
      }
      case Kind::error:
        leaf(LayouterId::Paragraph, Painter::None, TraitsId::Error, n, parent, x, std::move(s));
        return;
      case Kind::comment:
        return;
      case Kind::group: {
        const RoleInfo* role = roleOf(n);
        u32 g = open(LayouterId::Stack, Painter::None, role ? role->traits : TraitsId::Group, n, parent, x);
        if (role && role->captions) {
          // float form (figure-design.md §4): the caption paragraphs ride the
          // image as rows broken to its width — the float box is image +
          // caption rows, placed out of flow by layout
          const ContentNode* img = nullptr;
          for (const ContentNode* k : n->kids)
            if (k->kind == Kind::image) {
              img = k;
              break;
            }
          if (img && sideOf(img)) {
            LeafSource fs;
            fs.node = img;
            for (const ContentNode* k : n->kids)
              if (k->kind == Kind::para) fs.rows.push_back(k);
            const ContentNode* row0 = fs.rows.empty() ? nullptr : fs.rows[0];
            LayoutBlock& fb = leaf(LayouterId::Replaced, Painter::Image, TraitsId::Float, img, g, x, std::move(fs));
            fb.floatSide = sideOf(img);
            if (row0) traitsOf(row0, fb.rowTr);  // its caption rows' (plan P3-09, D-Y05)
            close(g);
            return;
          }
          figDepth++;
        }
        for (const ContentNode* k : n->kids) walk(k, g, x, 0);
        if (role && role->captions) figDepth--;
        close(g);
        return;
      }
      default:  // transparent: its children stand in its place
        for (const ContentNode* k : n->kids) walk(k, parent, x, 0);
        return;
    }
  }
};

}  // namespace

const char* traitsName(TraitsId t) { return kTraitNames[(size_t)t]; }

BoxTree buildBoxTree(const ContentTree& tree, Interner& strs, StyleTable& styles, const NodePropsTable& props,
                     const BoxTreeSettings& cfg, const Registry& reg) {
  BoxTree bt;
  if (!tree.root) return bt;
  Builder b(strs, styles, props, cfg, reg);
  u32 pid = 0;
  for (const ContentNode* child : tree.root->kids) {
    TopTree t;
    t.pid = pid++;
    if (const StrRef role = attrStr(child, ArgK::role))
      if (const HtmlShape* sh = reg.shapeOf(child, strs); sh && sh->dataRole) t.role = role;
    std::vector<LeafSource> src;
    b.top(child, t, src);
    if (t.leaves.empty()) continue;  // nothing to lay out: no frame
    bt.tops.push_back(std::move(t));
    bt.sources.push_back(std::move(src));
  }
  return bt;
}

std::string dumpBlockTree(const std::vector<TopTree>& tops, const Interner& strs) {
  static const char* const kLayouter[] = {"paragraph", "stack", "replaced", "grid", "table"};
  static const char* const kPainter[] = {"", " rule", " image", " raw", " math"};
  std::string out;
  for (const TopTree& t : tops) {
    appendf(out, "top pid=%u leaves=%zu\n", t.pid, t.leaves.size());
    for (u32 i = 0; i < t.blocks.size(); i++) {
      const LayoutBlock& b = t.blocks[i];
      u32 depth = 0;
      for (u32 p = b.parent; p != ~0u; p = t.blocks[p].parent) depth++;
      out.append(2 + 2 * depth, ' ');
      const BlockTraits& tr = b.tr;
      appendf(out, "%s%s %s", kLayouter[(size_t)b.layouter], kPainter[(size_t)b.painter], traitsName(b.traits));
      if (b.leaf()) appendf(out, " unit=%u", b.unit);
      if (b.x) appendf(out, " x=%dsu", b.x);
      if (b.pad) appendf(out, " pad=%dsu", b.pad);
      if (tr.gapDen) appendf(out, " gap=%u/%u", tr.gapNum, tr.gapDen);
      if (tr.align == BlockTraits::Align::Ragged) out += " ragged";
      if (tr.align == BlockTraits::Align::Center) out += " centered";
      if (tr.singleCenter) out += " single-center";
      if (tr.align == BlockTraits::Align::End) out += " end";
      if (tr.gapSu) appendf(out, " gap=%dsu", tr.gapSu);
      if (!tr.hyphenate) out += " nohyphen";
      if (tr.keepWithNext) out += " keep-with-next";
      if (b.floatSide) out += b.floatSide == 1 ? " float=left" : " float=right";
      // (plan P3-14) the trait group, where set
      if (b.xr) appendf(out, " xr=%dsu", b.xr);
      if (tr.keepTogether) out += " keep-together";
      if (tr.cont) out += " cont";
      if (tr.spaceBefore || tr.spaceAfter) appendf(out, " space=%dsu/%dsu", tr.spaceBefore, tr.spaceAfter);
      if (tr.breakBefore) out += " break-before";
      if (tr.breakAfter) out += " break-after";
      if (tr.hang) appendf(out, " hang=%dsu/%u", tr.hang, tr.hangAfter);
      if (tr.media) out += tr.media == 1 ? " media=screen" : " media=paged";
      if (tr.shrink) out += " shrink";
      if (tr.tolerance >= 0) appendf(out, " tolerance=%g", tr.tolerance);
      if (tr.emergencyStretch) appendf(out, " emergency=%dsu", tr.emergencyStretch);
      if (tr.place != BlockTraits::Place::Flow && !b.floatSide) {  // (an image float: float= above)
        static const char* const kPlace[] = {"flow", "start", "end", "top", "bottom", "page", "inline"};
        appendf(out, " place=%s", kPlace[(size_t)tr.place]);
        if (tr.placeW) appendf(out, "/%dsu", tr.placeW);
        if (tr.placeFrac) appendf(out, "/%g%%", tr.placeFrac * 100);
      }
      if (b.box.framed())
        appendf(out, " box=%d,%d,%d,%d/%d,%d,%d,%d", b.box.pad[0], b.box.pad[1], b.box.pad[2], b.box.pad[3],
                b.box.border[0], b.box.border[1], b.box.border[2], b.box.border[3]);
      if (b.anchor) {
        out += " anchor=\"";
        appendEscaped(out, strs.get(b.anchor));
        out += "\"";
      }
      if (b.carry && b.carry != b.anchor) {
        out += " carry=\"";
        appendEscaped(out, strs.get(b.carry));
        out += "\"";
      }
      if (b.marker) {
        out += " marker=\"";
        appendEscaped(out, strs.get(b.marker));
        out += "\"";
      }
      appendf(out, " @[%u,%u)\n", b.span.start, b.span.end);
    }
  }
  return out;
}

}  // namespace tsr
