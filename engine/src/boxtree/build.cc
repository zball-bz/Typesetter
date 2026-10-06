#include "build.h"

#include <algorithm>

#include "../elements/registry.h"
#include "../model/cascade.h"

namespace tsr {

namespace {

using Align = BlockTraits::Align;
// what a block is: its name in the dumps (its traits are its node's block
// properties since plan P3-01: engine/data/defaults.json gives a list's
// child gap of a third, display blocks set ragged and unhyphenated, a
// caption centred, a heading and a block image keeping with what follows)
constexpr const char* kTraitNames[] = {"root",  "para",  "caption", "heading", "list",  "item",
                                       "quote", "group", "figure",  "code",    "table", "image",
                                       "float", "math",  "raw",     "rule",    "error", "marker", "cell"};
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

  // a block length (plan P3-01): em of the block's own size
  Su lenSu(const Len& l, const ContentNode* n) const {
    if (!l.unit) return 0;
    return suRoundPx(l.unit == 2 ? (double)l.v : (double)l.v * emPx(cfg.baseSizePx, styles.get(n->style)));
  }

  const RoleInfo* roleOf(const ContentNode* n) const {
    if (n->cls && reg.cls(n->cls).box == ElementClass::Box::Figure) return &kFigureRole;
    return nullptr;
  }

  u8 sideOf(const ContentNode* n) const {
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
    }
    t->blocks.push_back(b);
    return (u32)t->blocks.size() - 1;
  }
  // a node's block properties (plan P3-01) as the traits layout reads
  void traitsOf(const ContentNode* n, BlockTraits& tr) const {
    const NodeProps& np = props.get(n->props);
    tr.gapNum = np.blockGap.num;
    tr.gapDen = np.blockGap.den;
    if (np.blockGap.len.unit) tr.gapSu = lenSu(np.blockGap.len, n);
    tr.align = np.parAlign == PARALIGN_START    ? Align::Ragged
               : np.parAlign == PARALIGN_CENTER ? Align::Center
               : np.parAlign == PARALIGN_END    ? Align::End
                                                : Align::Justify;
    tr.singleCenter = np.parSingleLine == PARSINGLELINE_CENTER;
    tr.hyphenate = np.parHyphenate != PARHYPHENATE_FALSE;
    tr.keepWithNext = np.keepWithNext;
    tr.snapKerning = np.snapKerning;
    if (np.sidecarFrac > 0) tr.sidecarFrac = np.sidecarFrac;
    tr.contIndent = (i32)np.contIndent;
  }
  // a container without leaves is dropped: it takes no gap
  void close(u32 i) {
    if (i + 1 == t->blocks.size() && !t->blocks[i].leaf() && i > 0) {
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
    t->blocks[tb].spec = (u32)t->tables.size();
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
    }
    t->blocks[tb].end = (u32)t->blocks.size();
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
        // 首行缩进 (App C): its par.indent (plan P3-01), never on a marker's line
        const Len ind = props.get(n->props).parIndent;
        if (ind.unit && ind.v > 0 && marker == 0) s.paraIndent = ind;
        LayoutBlock& b = leaf(LayouterId::Paragraph, Painter::None, caption ? TraitsId::Caption : TraitsId::Para,
                              n, parent, x, std::move(s));
        b.marker = marker;
        b.markerStyle = n->style;
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
        const int cols = attrInt(n, ArgK::cols, 1);
        const StrRef al = attrStr(n, ArgK::align);
        const std::string_view a = al ? strs.get(al) : std::string_view{};
        for (u32 c = 0; c < (u32)(cols < 1 ? 1 : cols); c++)
          spec.cols.push_back(ColSpec{0, c < a.size() ? (u8)a[c] : (u8)'l', false});
        const u32 ncols = (u32)spec.cols.size();
        t->blocks[tb].spec = (u32)t->tables.size();
        t->tables.push_back(std::move(spec));
        for (const ContentNode* row : n->kids) {
          if (row->kind != Kind::trow) continue;
          u32 c = 0;
          for (const ContentNode* cell : row->kids) {
            if (cell->kind != Kind::tcell || c >= ncols) continue;
            cellBlock(cell, tb);
            c++;
          }
          for (; c < ncols; c++) cellBlock(nullptr, tb);
        }
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
