// Normal form of the content tree (plans P0-07, P2-11; design T2 S3/S8). It
// runs once, right after instantiation, driven by the schema's level classes
// and body models. A node's kids sit at a position its body model gives:
// Blocks (doc, item, quote; a block-level group), Inline (para, heading,
// link, term, entry; an inline group; an inline note or cell body — plan
// P2-16 made a cell's body blocks, mixed like a note's), or Opaque (code and
// verbatim text, data, none) — a `position` body (group, styled, seq,
// when, each) takes the node's own position. Effective levels: Transparent
// kinds (styled, seq) take their kids' (block when one of them is), Adaptive
// kinds (group, term, error, raw, image) fit either position, Trivia never
// counts.
//   N1 fallback:   a mathblock in an Inline position is a mathinline (its
//                  label dropped: label-dropped) — display math lowers to
//                  mathblock everywhere and lands here (plan P2-11)
//   N2 unwrap:     a paragraph whose only child is block/adaptive-level IS
//                  that block (a #toc / #codeblock(…) splice, a display
//                  formula alone)
//   N3 blocks:     a seq holding a block (a multi-block content body, plan
//                  P1-08) takes its place among its siblings; a paragraph
//                  that is only such a seq is those blocks; at a Blocks
//                  position a paragraph of only empty text vanishes
//   N4 anon para:  at a Blocks position a maximal run of inline-level kids
//                  is one paragraph (an inline group at block level, a
//                  splice of text among blocks)
//   N5 diagnose:   a block in an Inline position stays where it is and says
//                  so (block-in-inline; the split policy is P3-17)
//   N6 models:     a list's kids are items, a table's rows, a row's cells:
//                  any other child is an error{content-model} around it
#include "model.h"

namespace tsr {

namespace {
enum class Pos : u8 { Blocks, Inline, Opaque };

bool emptyPara(const ContentNode* p, const Interner& strs) {
  if (p->kind != Kind::para) return false;
  for (const ContentNode* k : p->kids)
    if (k->kind != Kind::text || !strs.get(k->str).empty()) return false;
  return true;
}

// a seq holding a block: a sequence of blocks (a multi-block content body)
bool blockSeq(const ContentNode* n) {
  if (n->kind != Kind::seq) return false;
  for (const ContentNode* k : n->kids)
    if (!isInlineLevel(k->kind)) return true;
  return false;
}
void spliceBlocks(ContentNode* seq, std::vector<ContentNode*>& out) {
  for (ContentNode* k : seq->kids) {
    if (blockSeq(k)) spliceBlocks(k, out);
    else out.push_back(k);
  }
}

// the level a node takes: Transparent kinds their kids'
Level effLevel(const ContentNode* n) {
  const Level l = levelOf(n->kind);
  if (l != Level::Transparent) return l;
  for (const ContentNode* k : n->kids)
    if (effLevel(k) == Level::Block) return Level::Block;
  return Level::Inline;
}

// the position a node's kids sit at. A note's body and a table cell's
// (plan P2-16: blocks) are mixed (design T2: Mixed): inline content — the
// flow that places it makes its paragraph, a cell is its line — or blocks
Pos kidsPos(const ContentNode* n, Pos own) {
  if (n->kind == Kind::note || n->kind == Kind::tcell) {
    for (const ContentNode* k : n->kids)
      if (effLevel(k) == Level::Block) return Pos::Blocks;
    return Pos::Inline;
  }
  switch ((u16)n->kind < KIND_COUNT ? kKinds[(u16)n->kind].body : Body::None) {
    case Body::Blocks:
    case Body::Items:
    case Body::Rows:
    case Body::Cells: return Pos::Blocks;
    case Body::Inline: return Pos::Inline;
    case Body::Position: return own;
    case Body::None:
    case Body::Code:
    case Body::Data:
    case Body::Text: return Pos::Opaque;
  }
  return Pos::Opaque;
}

struct Norm {
  Arena& arena;
  Interner& strs;
  DiagSink& diags;

  ContentNode* mk(Kind k, Span sp) {
    ContentNode* n = arena.make<ContentNode>();
    n->kind = k;
    n->span = sp;
    return n;
  }
  // N6: what a checked model admits
  static bool admits(Kind parent, Kind kid) {
    switch (parent) {
      case Kind::list: return kid == Kind::item;
      case Kind::table: return kid == Kind::trow;
      case Kind::trow: return kid == Kind::tcell;
      default: return true;
    }
  }

  void kids(ContentNode* cur, Pos pos, std::vector<std::pair<ContentNode*, Pos>>& work) {
    const Pos kp = kidsPos(cur, pos);
    if (kp == Pos::Opaque) return;
    std::vector<ContentNode*> kids;
    kids.reserve(cur->kids.size());
    for (ContentNode* k : cur->kids) {
      if (kp == Pos::Blocks && emptyPara(k, strs)) continue;  // N3
      // N3: a sequence of blocks takes its place among its siblings; a
      // paragraph that is only such a sequence is those blocks
      if (blockSeq(k)) {
        spliceBlocks(k, kids);
        continue;
      }
      if (k->kind == Kind::para && k->kids.size() == 1 && blockSeq(k->kids[0])) {
        spliceBlocks(k->kids[0], kids);
        continue;
      }
      if (k->kind == Kind::para && k->kids.size() == 1 && !isInlineLevel(k->kids[0]->kind) &&
          k->kids[0]->kind != Kind::para)
        k = k->kids[0];  // N2
      kids.push_back(k);
    }
    if (kp == Pos::Inline) {
      for (ContentNode* k : kids) {
        if (k->kind == Kind::mathblock) {  // N1
          if (attrStr(k, ArgK::label))
            diags.add(Sev::Info, "label-dropped", k->span,
                      "a display formula inside a paragraph is set inline: its label is dropped");
          k->kind = Kind::mathinline;
          std::vector<ArgVal> args;
          for (const ArgVal& a : k->args)
            if (a.key == ArgK::src) args.push_back(a);
          k->args = std::move(args);
        } else if (effLevel(k) == Level::Block) {  // N5
          diags.add(Sev::Warning, "block-in-inline", k->span,
                    std::string(kindName(k->kind)) + " inside a paragraph stays where it is");
        }
      }
    } else if (cur->kind != Kind::list && cur->kind != Kind::table && cur->kind != Kind::trow) {
      // N4: a run of inline-level kids at a Blocks position is a paragraph
      std::vector<ContentNode*> out;
      out.reserve(kids.size());
      for (size_t i = 0; i < kids.size();) {
        const Level l = effLevel(kids[i]);
        if (l != Level::Inline && l != Level::Trivia) {
          out.push_back(kids[i++]);
          continue;
        }
        size_t j = i;
        bool inl = false;
        while (j < kids.size()) {
          const Level lj = effLevel(kids[j]);
          if (lj != Level::Inline && lj != Level::Trivia) break;
          inl = inl || lj == Level::Inline;
          j++;
        }
        if (!inl) {  // trivia only (comments, events, entries): left as they are
          for (; i < j; i++) out.push_back(kids[i]);
          continue;
        }
        ContentNode* p = mk(Kind::para, {kids[i]->span.start, kids[j - 1]->span.end});
        p->style = kids[i]->style;
        p->kids.assign(kids.begin() + (long)i, kids.begin() + (long)j);
        if (p->span.end < p->span.start) p->span = kids[i]->span;
        out.push_back(p);
        i = j;
      }
      kids = std::move(out);
    }
    if (cur->kind == Kind::list || cur->kind == Kind::table || cur->kind == Kind::trow) {  // N6
      for (ContentNode*& k : kids)
        if (levelOf(k->kind) != Level::Trivia && !admits(cur->kind, k->kind)) {
          const std::string msg = std::string("a ") + kindName(cur->kind) + " holds no " + kindName(k->kind);
          diags.add(Sev::Warning, "content-model", k->span, msg);
          ContentNode* e = mk(Kind::error, k->span);
          e->style = k->style;
          e->args.push_back({ArgK::message, ArgTag::Str, 0, strs.intern(msg)});
          e->args.push_back({ArgK::code, ArgTag::Str, 0, strs.intern("content-model")});
          e->kids.push_back(k);
          k = e;
        }
    }
    cur->kids = std::move(kids);
    for (ContentNode* k : cur->kids) work.push_back({k, kp});
  }
};
}  // namespace

void normalize(ContentNode* n, Arena& arena, Interner& strs, DiagSink& diags) {
  Norm N{arena, strs, diags};
  std::vector<std::pair<ContentNode*, Pos>> work{{n, Pos::Blocks}};
  while (!work.empty()) {
    auto [cur, pos] = work.back();
    work.pop_back();
    N.kids(cur, pos, work);
  }
}

}  // namespace tsr
