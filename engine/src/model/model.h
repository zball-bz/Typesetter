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

struct ContentTree {
  ContentNode* root = nullptr;  // kind doc; children = pid-bearing blocks
};

// EMIT walk with emission-time style resolution; DAG values are copied per
// emission (document-model §3).
ContentTree instantiate(const RawOps& raw, Arena& arena, Interner& strs,
                        StyleTable& styles, DiagSink& diags);

std::string dumpTree(const ContentTree& t, const Interner& strs, const StyleTable& styles);

}  // namespace tsr
