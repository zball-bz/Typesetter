// Content tree + instantiation (document-model §2–§4); the style table is
// style.h.
#pragma once
#include "style.h"

namespace tsr {

struct ContentNode {
  Kind kind;
  Span span;
  StyleId style = 0;
  u16 cls = 0;     // SemInfo: its element class (elements/registry.h), set at
                   // instantiate; 0 = none. Not an argument; dumps omit it.
  StrRef str = 0;  // text: interned string
  // text: its cooked→raw map (plan P2-04), nrawmap/2 pairs (cooked offset,
  // raw offset − span.start), identity between breakpoints; none when the
  // text is its own source slice, or not at its own source (an occurrence
  // alias, a contained span)
  const u32* rawmap = nullptr;
  u32 nrawmap = 0;
  std::vector<ArgVal> args;         // Str args re-pointed to doc interner
  std::vector<ContentNode*> kids;
};

// Typed attribute accessors (plan P0-06). Values that came through the ops
// reader are already inside their schema domain; nodes the engine builds are
// trusted. attrInt still clamps to i32, so no consumer ever converts an
// unchecked double.
inline const ArgVal* attr(const ContentNode* n, ArgK k) {
  for (const ArgVal& a : n->args)
    if (a.key == k) return &a;
  return nullptr;
}
inline i32 attrInt(const ContentNode* n, ArgK k, i32 dflt) {
  const ArgVal* a = attr(n, k);
  if (!a) return dflt;
  if (a->tag == ArgTag::Bool) return a->num != 0 ? 1 : 0;
  if (a->tag != ArgTag::Num || !(a->num == a->num)) return dflt;
  if (a->num <= -2147483648.0) return INT32_MIN;
  if (a->num >= 2147483647.0) return INT32_MAX;
  return (i32)a->num;
}
inline double attrNum(const ContentNode* n, ArgK k, double dflt) {
  const ArgVal* a = attr(n, k);
  return (a && a->tag == ArgTag::Num) ? a->num : dflt;
}
inline bool attrBool(const ContentNode* n, ArgK k, bool dflt) {
  const ArgVal* a = attr(n, k);
  return (a && a->tag == ArgTag::Bool) ? a->num != 0 : dflt;
}
inline StrRef attrStr(const ContentNode* n, ArgK k) {
  const ArgVal* a = attr(n, k);
  return (a && a->tag == ArgTag::Str) ? a->ref : 0;
}

struct ContentTree {
  ContentNode* root = nullptr;  // kind doc; children = pid-bearing blocks
};

// InstLimits (plan P0-07): the instantiated tree holds at most
// max(kInstMinBudget, kInstPerRawNode × raw nodes) nodes, nested at most 256
// deep. HostOnly configuration once the settings ABI exists (P1-03).
constexpr size_t kInstMinBudget = 262144;
constexpr size_t kInstPerRawNode = 64;

// EMIT walk with emission-time style resolution; DAG values are copied per
// emission (document-model §3).
class Registry;
// `reg` decides each node's class (its nearest classed ancestor included).
ContentTree instantiate(const RawOps& raw, Arena& arena, Interner& strs,
                        StyleTable& styles, DiagSink& diags, const Registry& reg);

std::string dumpTree(const ContentTree& t, const Interner& strs, const StyleTable& styles);

// Level class of a kind (schema.json "level"): what a node may stand next to.
inline Level levelOf(Kind k) {
  return (u16)k < KIND_COUNT ? kKinds[(u16)k].level : Level::Block;
}
// Inline content: inline, transparent (styled, seq) and trivia (comment).
inline bool isInlineLevel(Kind k) {
  Level l = levelOf(k);
  return l == Level::Inline || l == Level::Transparent || l == Level::Trivia;
}

// Normal form (plan P0-07, normalize.cc): run on the instantiated tree, and
// on every subtree the resolver builds.
//   N1 empty-para: a paragraph of only empty text vanishes (placeholder splices)
//   N2 unwrap:     a paragraph whose only child is block/adaptive-level IS that
//                  block (a #term / #toc / #codeblock(…) splice alone on a line)
//   N3 blocks:     a seq holding a block (a multi-block content body, plan
//                  P1-08) takes its place among its siblings; a paragraph that
//                  is only such a seq is those blocks
void normalize(ContentNode* n, const Interner& strs);

}  // namespace tsr
