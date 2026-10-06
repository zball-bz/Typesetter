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
  // (plan P2-15; design T8 MathEnv) the positional declarations in force
  // where it was emitted: those whose flow index is at most its EMIT's —
  // a moved or cloned node keeps it, so a formula binds names as of its
  // place in the flow
  u32 declEpoch = 0;
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

// EXT data (plan P2-05): the value of a node's EXT attribute `name`, or null
inline const ArgVal* extAttr(const ContentNode* n, StrRef name) {
  for (const ArgVal& a : n->args)
    if (a.key == ArgK::ext && a.name == name) return &a;
  return nullptr;
}

// A declaration (plan P2-05; design T2 S9): its type (kDecls), name, EXT
// data and style-neutral templates, at its flow position (the EMITs before
// it). Hoisted types: the last declaration of a name wins and the earlier
// ones are superseded (decl-redeclared); positional ones apply in order.
struct Decl {
  u16 type = 0;
  StrRef name = 0;
  std::vector<ArgVal> args;  // EXT data (names and strings interned)
  std::vector<ContentNode*> templates;
  u32 flowIndex = 0;
  Span span;
  bool superseded = false;
};

struct ContentTree {
  ContentNode* root = nullptr;  // kind doc; children = pid-bearing blocks
  std::vector<Decl> decls;      // in document order
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

// Normal form (plans P0-07, P2-11; normalize.cc has the rules N1–N6): run on
// the instantiated tree — positions, fallbacks, anonymous paragraphs, model
// checks; diagnoses block-in-inline without splitting it
void normalize(ContentNode* n, Arena& arena, Interner& strs, DiagSink& diags);

}  // namespace tsr
