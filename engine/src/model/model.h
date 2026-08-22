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
};

struct Styling {
  u64 bits = 0;
  float sizeMul = 1.0f;  // heading scale etc.; InlineStyle patches arrive later
};

using StyleId = u32;
class StyleTable {
 public:
  StyleTable() { idOf(Styling{}); }  // id 0 = base
  StyleId idOf(Styling s) {
    u32 mulBits;
    std::memcpy(&mulBits, &s.sizeMul, 4);
    auto key = std::make_pair(s.bits, mulBits);
    auto it = map_.find(key);
    if (it != map_.end()) return it->second;
    StyleId id = (StyleId)styles_.size();
    styles_.push_back(s);
    map_.emplace(key, id);
    return id;
  }
  const Styling& get(StyleId id) const { return styles_[id]; }
  size_t count() const { return styles_.size(); }

 private:
  struct PairHash {
    size_t operator()(const std::pair<u64, u32>& p) const {
      return std::hash<u64>()(p.first ^ ((u64)p.second << 32));
    }
  };
  std::vector<Styling> styles_;
  std::unordered_map<std::pair<u64, u32>, StyleId, PairHash> map_;
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
