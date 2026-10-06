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
//   (N1, the display-math fallback to inline, is gone: plan P3-17 — display
//                  math splits a paragraph as any block does, D-I02)
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
//   N5 split:      (plan P3-17, D-I02) a paragraph at a Blocks position
//                  holding a block splits around it: [para, block,
//                  para{cont}] — (plan P3-29, D-S11: display lines one
//                  after the other, two or more, are one equations block)
//                  the continuation without a first-line
//                  indent or the space before a paragraph; a block in any
//                  other Inline position (a heading, a link, an inline
//                  group) is an error{block-in-inline} around it
//   N6 models:     a list's kids are items, a table's rows, a row's cells,
//                  an equations block's display formulas (plan P2-16): any
//                  other child is an error{content-model} around it
// A child in a slot its parent takes (plan P2-16; schema "slots": a code
// block's margin, a reference's extra, a block's tag) is a part, not
// content: no rule moves, wraps or checks it, and its own kids sit at the
// slot's model.
#include "model.h"

namespace tsr {

namespace {
enum class Pos : u8 { Blocks, Inline, Opaque };

// nothing in it: empty text, or a sequence of nothing (a splice that
// rendered nothing — #use(…), plan P3-31)
bool emptyContent(const ContentNode* k, const Interner& strs) {
  if (k->kind == Kind::text) return strs.get(k->str).empty();
  if (k->kind != Kind::seq) return false;
  for (const ContentNode* c : k->kids)
    if (!emptyContent(c, strs)) return false;
  return true;
}
// blanks only (spaces, tabs, soft breaks)
bool blankText(std::string_view s) {
  return s.find_first_not_of(" \t\n") == std::string_view::npos;
}
bool emptyPara(const ContentNode* p, const Interner& strs) {
  if (p->kind != Kind::para) return false;
  for (const ContentNode* k : p->kids)
    if (!emptyContent(k, strs)) return false;
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
// the position a body model gives (Position: the node's own)
Pos posOf(Body b, Pos own) {
  switch (b) {
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

Pos kidsPos(const ContentNode* n, Pos own) {
  if (n->kind == Kind::note || n->kind == Kind::tcell) {
    for (const ContentNode* k : n->kids)
      if (effLevel(k) == Level::Block) return Pos::Blocks;
    return Pos::Inline;
  }
  return posOf((u16)n->kind < KIND_COUNT ? kKinds[(u16)n->kind].body : Body::None, own);
}

struct Norm {
  Arena& arena;
  Interner& strs;
  DiagSink& diags;

  size_t made = 0;
  // a wrapper it makes takes the cascade at its place (settleMade, plan P3-01)
  ContentNode* mk(Kind k, Span sp) {
    ContentNode* n = arena.make<ContentNode>();
    n->kind = k;
    n->span = sp;
    n->props = ~0u;  // kPropsUnset (model/cascade.h)
    n->env = ~0u;    // kEnvUnset: its parent's
    made++;
    return n;
  }
  static bool checked(Kind k) {
    return k == Kind::list || k == Kind::terms || k == Kind::table || k == Kind::trow || k == Kind::equations;
  }
  // N6: what a checked model admits
  static bool admits(Kind parent, Kind kid) {
    switch (parent) {
      case Kind::list:
      case Kind::terms: return kid == Kind::item;  // (plan P3-34)
      case Kind::table: return kid == Kind::trow;
      case Kind::trow: return kid == Kind::tcell;
      case Kind::equations: return kid == Kind::mathblock;
      case Kind::doc: case Kind::para: case Kind::heading: case Kind::item: case Kind::quote:
      case Kind::codeblock: case Kind::rule: case Kind::group: case Kind::tcell: case Kind::term:
      case Kind::collect: case Kind::mathblock: case Kind::error: case Kind::comment: case Kind::text:
      case Kind::styled: case Kind::link: case Kind::code: case Kind::ref: case Kind::mathinline:
      case Kind::raw: case Kind::hardbreak: case Kind::seq: case Kind::image: case Kind::note:
      case Kind::field: case Kind::event: case Kind::entry: case Kind::slot: case Kind::when:
      case Kind::each: case Kind::math: case Kind::mathsrc: case Kind::fill:
        return true;
    }
  }

  // N5 (plan P3-17, D-I02): a paragraph holding blocks among its inline
  // kids, as its runs and the blocks in order: the first run is the
  // paragraph itself (its label, its props), the runs after a block its
  // continuations (cont: no indent, no space before); a run of only empty
  // text is no paragraph. false: no block in it
  bool splitPara(ContentNode* p, std::vector<ContentNode*>& out) {
    bool any = false;
    for (const ContentNode* k : p->kids) any = any || (effLevel(k) == Level::Block && !part(k, p));
    if (!any) return false;
    std::vector<ContentNode*> run;
    bool first = true;
    auto flush = [&] {
      bool content = false;
      for (const ContentNode* k : run)
        content = content || (levelOf(k->kind) != Level::Trivia &&
                              (k->kind != Kind::text || strs.get(k->str).find_first_not_of(" \t\n") != std::string_view::npos));
      if (content) {
        ContentNode* q = p;
        if (!first) {  // the same paragraph, continued: its style, rules and properties
          q = mk(Kind::para, {run.front()->span.start, run.back()->span.end});
          q->style = p->style;
          q->scope = p->scope;
          q->env = p->env;
          q->props = p->props;
          q->declEpoch = p->declEpoch;
          q->args.push_back({ArgK::cont, ArgTag::Bool, 1, 0});
        }
        q->kids = run;
        out.push_back(q);
        first = false;
      } else {
        for (ContentNode* k : run)  // (trivia keep their place)
          if (levelOf(k->kind) == Level::Trivia) out.push_back(k);
      }
      run.clear();
    };
    std::vector<ContentNode*> kids = p->kids;
    const size_t from = out.size();
    for (ContentNode* k : kids) {
      if (effLevel(k) == Level::Block && !part(k, p)) {
        flush();
        out.push_back(k);
        first = false;  // (what follows continues the paragraph)
      } else {
        run.push_back(k);
      }
    }
    flush();
    equations(p, out, from);
    return true;
  }
  // (plan P3-29, D-S11) display lines one after the other in a paragraph —
  // two or more, nothing but blanks between them — are one equations
  // block: its rows share their alignment, each its own equation (its label,
  // its number)
  void equations(const ContentNode* p, std::vector<ContentNode*>& out, size_t from) {
    for (size_t i = from; i < out.size(); i++) {
      size_t j = i;
      while (j < out.size() && out[j]->kind == Kind::mathblock) j++;
      if (j - i >= 2) {
        ContentNode* e = mk(Kind::equations, {out[i]->span.start, out[j - 1]->span.end});
        e->style = p->style;
        e->scope = p->scope;
        e->env = p->env;
        e->declEpoch = p->declEpoch;
        e->kids.assign(out.begin() + (long)i, out.begin() + (long)j);
        out.erase(out.begin() + (long)i, out.begin() + (long)j);
        out.insert(out.begin() + (long)i, e);
      }
    }
  }

  // a part of its parent: a child in a slot the parent takes
  bool part(const ContentNode* k, const ContentNode* parent) const { return slotOn(slotOf(k, strs), parent->kind); }

  void kids(ContentNode* cur, Pos pos, std::vector<std::pair<ContentNode*, Pos>>& work) {
    const Pos kp = kidsPos(cur, pos);
    if (kp == Pos::Opaque) return;
    std::vector<ContentNode*> kids;
    kids.reserve(cur->kids.size());
    for (ContentNode* k : cur->kids) {
      if (part(k, cur)) {
        kids.push_back(k);
        continue;
      }
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
      if (kp == Pos::Blocks && k->kind == Kind::para && splitPara(k, kids)) continue;  // N5
      kids.push_back(k);
    }
    // (plan P3-34) between a checked model's parts, blanks are no text:
    // #list[#item[a] #item[b]]
    if (checked(cur->kind))
      std::erase_if(kids, [&](const ContentNode* k) { return k->kind == Kind::text && blankText(strs.get(k->str)); });
    if (kp == Pos::Inline) {
      for (ContentNode*& k : kids) {
        if (part(k, cur) || effLevel(k) != Level::Block) continue;
        // N5: no paragraph to split here — an error around the block
        const std::string msg = std::string("a ") + kindName(k->kind) + " cannot stand inside a " +
                                kindName(cur->kind) + " (only a paragraph splits around a block)";
        diags.add(Sev::Warning, "block-in-inline", k->span, msg);
        ContentNode* e = mk(Kind::error, k->span);
        e->style = k->style;
        e->args.push_back({ArgK::message, ArgTag::Str, 0, strs.intern(msg)});
        e->args.push_back({ArgK::code, ArgTag::Str, 0, strs.intern("block-in-inline")});
        e->kids.push_back(k);
        k = e;
      }
    } else if (!checked(cur->kind)) {
      // N4: a run of inline-level kids at a Blocks position is a paragraph
      std::vector<ContentNode*> out;
      out.reserve(kids.size());
      for (size_t i = 0; i < kids.size();) {
        const Level l = effLevel(kids[i]);
        if ((l != Level::Inline && l != Level::Trivia) || part(kids[i], cur)) {
          out.push_back(kids[i++]);
          continue;
        }
        size_t j = i;
        bool inl = false;
        while (j < kids.size()) {
          const Level lj = effLevel(kids[j]);
          if ((lj != Level::Inline && lj != Level::Trivia) || part(kids[j], cur)) break;
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
    if (checked(cur->kind)) {  // N6
      for (ContentNode*& k : kids)
        if (levelOf(k->kind) != Level::Trivia && !admits(cur->kind, k->kind) && !part(k, cur)) {
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
    for (ContentNode* k : cur->kids) {
      if (part(k, cur)) {  // a part's kids sit at its slot's model
        const Pos sp = posOf(kKinds[(u16)k->kind].body == Body::Position ? kSlots[(u8)slotOf(k, strs)].model
                                                                         : kKinds[(u16)k->kind].body, kp);
        if (sp != Pos::Opaque) work.push_back({k, sp == Pos::Blocks ? Pos::Blocks : Pos::Inline});
        continue;
      }
      work.push_back({k, kp});
    }
  }
};
}  // namespace

size_t normalize(ContentNode* n, Arena& arena, Interner& strs, DiagSink& diags) {
  Norm N{arena, strs, diags};
  std::vector<std::pair<ContentNode*, Pos>> work{{n, Pos::Blocks}};
  while (!work.empty()) {
    auto [cur, pos] = work.back();
    work.pop_back();
    N.kids(cur, pos, work);
  }
  return N.made;
}

}  // namespace tsr
