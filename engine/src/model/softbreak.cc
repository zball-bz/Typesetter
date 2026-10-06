// The soft-break pass (plan P4-02; model/softbreak.h): the tree's inline
// streams in reading order — a block's inline content through its
// containers (styled, seq, link, ref, an inline group) — each one's soft
// breaks resolved with the paragraph context of shape/context.h.
#include "softbreak.h"

#include "../shape/context.h"
#include "../shape/objects.h"
#include "model.h"

namespace tsr {

namespace {

// A stream's pieces in reading order; its character entries are built only
// when one of its texts has a soft break (most streams have none)
struct Stream {
  struct Piece {
    ContentNode* text;  // null: an evidence entry of kind k
    CtxEntry::K k;
  };
  std::vector<Piece> pieces;
  bool soft = false;
  std::vector<CtxEntry> v;
  bool ambiguous = false;
  struct Text {
    ContentNode* node;
    std::vector<u32> breaks;  // its soft breaks' entries
  };
  std::vector<Text> texts;
  void evidence(CtxEntry::K k) { pieces.push_back({nullptr, k}); }
};

struct Pass {
  Arena& arena;
  Interner& strs;
  const StyleTable& styles;

  // a code or verbatim body: its newlines are its lines
  static bool verbatim(Kind k) {
    const Body b = kKinds[(u16)k].body;
    return b == Body::Code || b == Body::Text;
  }

  void flush(Stream& s) {
    if (!s.soft) {
      s.pieces.clear();
      return;
    }
    for (const Stream::Piece& p : s.pieces) {
      if (p.text) text(p.text, s);
      else s.v.push_back({p.k});
    }
    s.pieces.clear();
    s.soft = false;
    resolveContext(s.v, s.ambiguous);
    for (Stream::Text& t : s.texts) {
      ContentNode* n = t.node;
      auto join = [&](u32 k) {
        const u32 e = t.breaks[k];
        if (e == 0 || e + 1 >= s.v.size()) return false;
        const CtxEntry& a = s.v[e - 1];
        const CtxEntry& b = s.v[e + 1];
        return a.k == CtxEntry::Char && b.k == CtxEntry::Char && joinsWithoutSpace(a.cp, a.wide, b.cp, b.wide);
      };
      std::string str(strs.get(n->str));
      std::vector<u32> map(n->rawmap, n->rawmap + n->nrawmap);
      const bool mapped = n->srcExact;  // (instantiation gave a mapped text an explicit map)
      rewriteSoftBreaks(str, map, n->span.end - n->span.start, mapped, join);
      n->str = strs.intern(str);
      if (mapped && !map.empty()) {
        u32* m = arena.allocArray<u32>(map.size());
        std::copy(map.begin(), map.end(), m);
        n->rawmap = m;
        n->nrawmap = (u32)map.size();
      } else {
        n->rawmap = nullptr;
        n->nrawmap = 0;
      }
    }
    s.v.clear();
    s.texts.clear();
    s.ambiguous = false;
  }

  // kids that are a flow of their own (a note's body, an error's content,
  // a block's), each inline run of them a stream
  void apart(const ContentNode* n) {
    Stream inner;
    for (ContentNode* k : n->kids) walk(k, inner);
    flush(inner);
  }

  void text(ContentNode* n, Stream& s) {
    const std::string_view str = strs.get(n->str);
    const Styling& st = styles.get(n->style);
    const MarkClass marks = markClassOf(st.lang ? strs.get(st.lang) : std::string_view{});
    Stream::Text t{n, {}};
    for (u32 i = 0; i < str.size();) {
      const u32 start = i;
      i = clusterEnd(str, i);
      u32 j = start;
      const u32 cp = utf8Next(str, j);
      if (cp == '\n' || cp == ' ' || cp == '\t') {
        if (cp == '\n') t.breaks.push_back((u32)s.v.size());
        s.v.push_back({CtxEntry::Blank});
      } else {
        s.v.push_back(ctxChar(cp, marks, s.ambiguous));
      }
    }
    if (!t.breaks.empty()) s.texts.push_back(std::move(t));
  }

  void walk(ContentNode* n, Stream& s) {
    if (verbatim(n->kind)) {  // inline code is Latin-class evidence; a code block, a comment end nothing
      if (kKinds[(u16)n->kind].inl == InlineShape::Code) s.evidence(CtxEntry::Narrow);
      return;
    }
    if (levelOf(n->kind) == Level::Block) {  // a block ends the stream; its inline content is one
      flush(s);
      apart(n);
      return;
    }
    switch (kKinds[(u16)n->kind].inl) {
      case InlineShape::Text:
        s.pieces.push_back({n, CtxEntry::Char});
        s.soft = s.soft || strs.get(n->str).find('\n') != std::string_view::npos;
        return;
      case InlineShape::Container:
        for (ContentNode* k : n->kids) walk(k, s);
        return;
      case InlineShape::Code:
        s.evidence(CtxEntry::Narrow);
        return;
      case InlineShape::Object:
        s.evidence(objectKind(objectKindOf(n->kind)).lastCC == CC::Alpha ? CtxEntry::Narrow : CtxEntry::Opaque);
        return;
      case InlineShape::Break:
      case InlineShape::Fill:
        s.evidence(CtxEntry::Opaque);
        return;
      case InlineShape::Error:  // its box (⚠ message) in the stream, what it wraps apart
        s.evidence(CtxEntry::Narrow);
        apart(n);
        return;
      case InlineShape::Skip:  // nothing in the line; an index entry's text is apart
        apart(n);
        return;
      case InlineShape::Unsupported:  // a footnote: its marker in the line, its body apart
        s.evidence(CtxEntry::Narrow);
        apart(n);
        return;
    }
  }
};

}  // namespace

void resolveSoftBreaks(ContentNode* root, Arena& arena, Interner& strs, const StyleTable& styles) {
  Pass p{arena, strs, styles};
  Stream s;
  p.walk(root, s);
  p.flush(s);
}

}  // namespace tsr
