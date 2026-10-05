// CallAST (plan P1-05; design T1 §CallAST): built-in sugar is a Call whose
// meaning is its slot (SugarId); a user call is a Splice (its callee is JS
// text); statements are Stmt, malformed input is Error. The node is small and
// uniform; a sugar's or kind's typed payload (syntax.gen.h: HeadingP, FenceP,
// SpliceP, …) is a side record allocated right behind the node, so nodes
// without one pay nothing. Kids are an arena slice.
#pragma once
#include <span>

#include "../linepass/linepass.h"
#include "../syntax/syntax.gen.h"

namespace tsr {

enum class AstKind : u8 { Doc, Text, Comment, Call, Splice, Stmt, Error };

struct AstNode {
  AstNode** kidv = nullptr;  // arena slice (kids())
  u32 nkids = 0;
  AstKind kind = AstKind::Doc;
  u8 flags = 0;
  SugarId sugar = SugarId::para;  // Call only
  Span span;
  StrRef str = 0;  // Text, Comment: text; code, ref, math, fence body, region
                   // name: their string; Error: the code
  u32 side = 0;    // bytes of the trailing side record, 0 = none

  std::span<AstNode* const> kids() const { return {kidv, nkids}; }
  bool isCall(SugarId s) const { return kind == AstKind::Call && sugar == s; }
};
static_assert(sizeof(AstNode) <= 32, "CallAST nodes stay within 32 bytes");

// The node's side record (its payload struct from syntax.gen.h).
template <class P>
P& side(AstNode* n) {
  return *reinterpret_cast<P*>(reinterpret_cast<char*>(n) + sizeof(AstNode));
}
template <class P>
const P& side(const AstNode* n) {
  return *reinterpret_cast<const P*>(reinterpret_cast<const char*>(n) + sizeof(AstNode));
}

// Node construction in the document arena.
struct AstAlloc {
  Arena& arena;
  AstNode* node(AstKind k, Span s) {
    AstNode* n = new (arena.alloc(sizeof(AstNode), alignof(AstNode))) AstNode();
    n->kind = k;
    n->span = s;
    return n;
  }
  // a node with a payload P trailing it (one allocation, so side<P> holds)
  template <class P>
  AstNode* node(AstKind k, Span s) {
    static_assert(alignof(P) <= alignof(AstNode) && sizeof(AstNode) % alignof(P) == 0);
    void* mem = arena.alloc(sizeof(AstNode) + sizeof(P), alignof(AstNode));
    AstNode* n = new (mem) AstNode();
    new ((char*)mem + sizeof(AstNode)) P();
    n->kind = k;
    n->span = s;
    n->side = sizeof(P);
    return n;
  }
  AstNode* call(SugarId sg, Span s) {
    AstNode* n = node(AstKind::Call, s);
    n->sugar = sg;
    return n;
  }
  template <class P>
  AstNode* call(SugarId sg, Span s) {
    AstNode* n = node<P>(AstKind::Call, s);
    n->sugar = sg;
    return n;
  }
  void setKids(AstNode* n, const std::vector<AstNode*>& kids) {
    n->nkids = (u32)kids.size();
    n->kidv = kids.empty() ? nullptr : arena.allocArray<AstNode*>(kids.size());
    for (size_t i = 0; i < kids.size(); i++) n->kidv[i] = kids[i];
  }
};

AstNode* parseDoc(const SourceText& src, const Skeleton& sk, Arena& arena,
                  Interner& strs, DiagSink& diags);

// Inline-parse a bare span list (fragment re-entry: sidecars, m.parse).
std::vector<AstNode*> parseInlineSpans(const SourceText& src,
                                       const std::vector<Span>& spans,
                                       Arena& arena, Interner& strs,
                                       DiagSink& diags);

// One node's dump line without indentation or newline (generated from the
// syntax.def dump formats: syntax.gen.cc); dumpAst prints the tree.
void dumpAstNode(std::string& out, const AstNode* n, const SourceText& src, const Interner& strs);
// One node's JSON members (kind, sugar, span, str, payload; generated):
// syntax/exports.h writes the tree.
void jsonAstNode(std::string& out, const AstNode* n, const SourceText& src, const Interner& strs);
std::string dumpAst(const AstNode* doc, const SourceText& src, const Interner& strs);

}  // namespace tsr
