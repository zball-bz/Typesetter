#include "build.h"

namespace tsr {

namespace {

using Align = BlockTraits::Align;
// The TraitTable (design T6 BlockTraits): today's defaults — the root's gap
// is one paragraph gap, a list's a third of one (integer division, so
// everything inside a list after its first block packs tighter); display
// lines are ragged and unhyphenated; a caption is centred; a heading and a
// block image keep with what follows on a sheet.
constexpr BlockTraits kTraits[] = {
    {"root", 1, 1},
    {"para"},
    {"caption", 0, 0, Align::Center, false},
    {"heading", 0, 0, Align::Ragged, false, true},
    {"list", 1, 3},
    {"item"},
    {"quote"},
    {"group"},
    {"figure"},
    {"code", 0, 0, Align::Ragged, false},
    {"table"},
    {"image", 0, 0, Align::Ragged, true, true},
    {"float"},
    {"math", 0, 0, Align::Ragged},
    {"raw"},
    {"rule"},
    {"error", 0, 0, Align::Ragged},
    {"marker"},
};
static_assert(sizeof kTraits / sizeof kTraits[0] == (size_t)TraitsId::N, "one row per TraitsId");

// Roles are data (finding emitter/figure-role-string-dispatch): what a
// group{role} means to the box tree, looked up by interned name.
struct RoleInfo {
  TraitsId traits = TraitsId::Group;
  bool captions = false;  // its paragraphs are captions; an image with a side floats
  bool sidecar = false;   // a code block's sidecar lines
};
constexpr struct {
  const char* name;
  RoleInfo info;
} kRoles[] = {
    {"figure", {TraitsId::Figure, true, false}},
    {"sidecar-lines", {TraitsId::Group, false, true}},
};
// image sides (figure-design.md §4): 1 left, 2 right
constexpr const char* kSides[] = {"left", "right"};

class Builder {
 public:
  Builder(Interner& s, StyleTable& st, const Config& c) : strs(s), styles(st), cfg(c) {
    for (const auto& r : kRoles) roleRefs.push_back(s.find(r.name));
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
  }

 private:
  Interner& strs;
  StyleTable& styles;
  const Config& cfg;
  std::vector<StrRef> roleRefs, sideRefs;
  TopTree* t = nullptr;
  std::vector<LeafSource>* leaves = nullptr;
  int figDepth = 0;  // inside a captions role: paragraphs are captions

  const RoleInfo* roleOf(const ContentNode* n) const {
    StrRef r = attrStr(n, ArgK::role);
    for (size_t i = 0; r && i < roleRefs.size(); i++)
      if (roleRefs[i] == r) return &kRoles[i].info;
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
    if (n) b.span = n->span;
    t->blocks.push_back(b);
    return (u32)t->blocks.size() - 1;
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

  void walk(const ContentNode* n, u32 parent, Su x, StrRef marker) {
    LeafSource s;
    s.node = n;
    switch (n->kind) {
      case Kind::para: {
        const bool caption = figDepth > 0;
        if (caption) s.role = LeafSource::Role::Caption;
        else s.paraIndent = cfg.paraIndentEm > 0 && marker == 0;  // 首行缩进 (App C)
        LayoutBlock& b = leaf(LayouterId::Paragraph, Painter::None, caption ? TraitsId::Caption : TraitsId::Para,
                              n, parent, x, std::move(s));
        b.anchor = labelOf(n);  // a labelled paragraph (note bodies)
        b.marker = marker;
        b.markerStyle = n->style;
        return;
      }
      case Kind::heading: {
        LayoutBlock& b = leaf(LayouterId::Paragraph, Painter::None, TraitsId::Heading, n, parent, x, std::move(s));
        b.anchor = labelOf(n);
        b.marker = marker;
        b.markerStyle = n->style;
        return;
      }
      case Kind::list: {
        const bool ordered = attrBool(n, ArgK::ordered, false);
        int num = attrInt(n, ArgK::start, 1);
        u32 list = open(LayouterId::Stack, Painter::None, TraitsId::List, n, parent, x);
        const Su pad = suRoundPx(cfg.listIndentEm * cfg.baseSizePx);
        t->blocks[list].pad = pad;
        for (const ContentNode* item : n->kids) {
          std::string m = ordered ? std::to_string(num++) + "." : "\xE2\x80\xA2";  // •
          const StrRef mref = strs.intern(m);
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
        const Su pad = suRoundPx(cfg.quoteIndentEm * cfg.baseSizePx);
        t->blocks[q].pad = pad;
        for (const ContentNode* k : n->kids) walk(k, q, x + pad, 0);
        close(q);
        return;
      }
      case Kind::codeblock: {
        // sidecar rows (verbatim-design §5): the sidecar group's lines are
        // the block's second track
        for (const ContentNode* k : n->kids) {
          const RoleInfo* r = k->kind == Kind::group ? roleOf(k) : nullptr;
          if (r && r->sidecar) {
            s.sidecar = k;
            s.rows.assign(k->kids.begin(), k->kids.end());
          }
        }
        LayoutBlock& b = leaf(LayouterId::Grid, Painter::None, TraitsId::Code, n, parent, x, std::move(s));
        b.marker = marker;
        b.markerStyle = compose(styles, n->style, CLS_CODE, (float)cfg.codeScale);
        return;
      }
      case Kind::rule:
        leaf(LayouterId::Replaced, Painter::Rule, TraitsId::Rule, n, parent, x, std::move(s));
        return;
      case Kind::table: {
        LayoutBlock& b = leaf(LayouterId::Table, Painter::None, TraitsId::Table, n, parent, x, std::move(s));
        b.anchor = labelOf(n);
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
        LayoutBlock& b = leaf(LayouterId::Replaced, Painter::MathRow, TraitsId::Math, n, parent, x, std::move(s));
        b.anchor = labelOf(n);
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
        t->blocks[g].anchor = labelOf(n);
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
            leaf(LayouterId::Replaced, Painter::Image, TraitsId::Float, img, g, x, std::move(fs)).floatSide =
                sideOf(img);
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

const BlockTraits& traitsOf(TraitsId t) { return kTraits[(size_t)t]; }

BoxTree buildBoxTree(const ContentTree& tree, Interner& strs, StyleTable& styles, const Config& cfg) {
  BoxTree bt;
  if (!tree.root) return bt;
  Builder b(strs, styles, cfg);
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
      const BlockTraits& tr = traitsOf(b.traits);
      appendf(out, "%s%s %s", kLayouter[(size_t)b.layouter], kPainter[(size_t)b.painter], tr.name);
      if (b.leaf()) appendf(out, " unit=%u", b.unit);
      if (b.x) appendf(out, " x=%dsu", b.x);
      if (b.pad) appendf(out, " pad=%dsu", b.pad);
      if (tr.gapDen) appendf(out, " gap=%u/%u", tr.gapNum, tr.gapDen);
      if (tr.align == BlockTraits::Align::Ragged) out += " ragged";
      if (tr.align == BlockTraits::Align::Center) out += " centered";
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
