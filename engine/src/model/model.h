// Content tree + style table + instantiation (document-model §2–§4).
#pragma once
#include "../ops/ops.h"

namespace tsr {

// Frozen base class bits (document-model §3).
enum : u64 {
  CLS_LATIN = 1ull << 0,
  CLS_CJK = 1ull << 1,
  CLS_EM = 1ull << 2,
  CLS_BOLD = 1ull << 3,
  CLS_CODE = 1ull << 6,
  CLS_LINK = 1ull << 13,
  // decorations (CH1, code-design.md §3): rendered as text-decoration,
  // metric-neutral by construction
  CLS_UNDER = 1ull << 16,
  CLS_OVER = 1ull << 17,
  CLS_STRIKE = 1ull << 18,
  // superscript (notes-design.md §1): footnote markers — size rides
  // sizeMul (measured), the raise is paint-only (position: relative)
  CLS_SUP = 1ull << 19,
};

// Effective style: class bits + relative size + InlineStyle overrides
// (document-model §3; implemented subset: fontFamily, lang, color, sizePx —
// weight/italic ride the bits, letterSpacing is engine-owned justification).
// StrRef 0 / 0.0 = "not set" (inherit the class-based default).
struct Styling {
  u64 bits = 0;
  float sizeMul = 1.0f;
  StrRef fontFamily = 0;  // CSS font-family list (overrides body/cjk/mono)
  StrRef lang = 0;        // BCP-47 tag → per-run lang attr ('locl' forms)
  StrRef color = 0;       // CSS color
  float sizePx = 0;       // absolute base size (sizeMul still composes on top)
  bool operator==(const Styling& o) const {
    return bits == o.bits && sizeMul == o.sizeMul && fontFamily == o.fontFamily &&
           lang == o.lang && color == o.color && sizePx == o.sizePx;
  }
};

using StyleId = u32;
class StyleTable {
 public:
  StyleTable() { idOf(Styling{}); }  // id 0 = base
  StyleId idOf(Styling s) {
    // canonical floats (plan P0-08): -0 → +0 and NaN → the default, so equal
    // styles hash equally (the hash reads the bit patterns)
    if (!(s.sizeMul == s.sizeMul)) s.sizeMul = 1.0f;
    if (!(s.sizePx == s.sizePx) || s.sizePx < 0) s.sizePx = 0;
    if (s.sizeMul == 0) s.sizeMul = 0.0f;
    if (s.sizePx == 0) s.sizePx = 0.0f;
    auto it = map_.find(s);
    if (it != map_.end()) return it->second;
    StyleId id = (StyleId)styles_.size();
    styles_.push_back(s);
    map_.emplace(s, id);
    return id;
  }
  const Styling& get(StyleId id) const { return styles_[id]; }
  size_t count() const { return styles_.size(); }

 private:
  struct Hash {
    size_t operator()(const Styling& s) const {
      u32 mulBits, pxBits;
      std::memcpy(&mulBits, &s.sizeMul, 4);
      std::memcpy(&pxBits, &s.sizePx, 4);
      u64 h = s.bits;
      h = h * 1099511628211ull ^ mulBits;
      h = h * 1099511628211ull ^ s.fontFamily;
      h = h * 1099511628211ull ^ s.lang;
      h = h * 1099511628211ull ^ s.color;
      h = h * 1099511628211ull ^ pxBits;
      return (size_t)h;
    }
  };
  std::vector<Styling> styles_;
  std::unordered_map<Styling, StyleId, Hash> map_;
};

struct ContentNode {
  Kind kind;
  Span span;
  StyleId style = 0;
  StrRef str = 0;  // text: interned string
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
ContentTree instantiate(const RawOps& raw, Arena& arena, Interner& strs,
                        StyleTable& styles, DiagSink& diags);

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
void normalize(ContentNode* n, const Interner& strs);

}  // namespace tsr
