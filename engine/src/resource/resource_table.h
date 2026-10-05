// The ResourceTable (plan P1-19; design T9 A1): every datum the engine can
// only obtain after Ingest, in one place — content-keyed needs, one wait
// state, the open request batch, validated answers and local degradation.
// textWidth and fontVmet rows live in the MetricStore (raw px; Measure
// quantizes), code tokens and box sizes here. Answers never touch the
// authored tree (finding api-measure-code/image-dims-in-author-args).
#pragma once
#include <unordered_map>
#include <vector>

#include "../code/tokens.h"
#include "../measure/measure.h"
#include "codec.h"

namespace tsr {

enum class ResState : u8 { Pending, Ready, Failed };

struct TokenNeed {
  StrRef lang = 0, body = 0;
  ResState st = ResState::Pending;
  std::vector<CodeToken> toks;  // Ready: sorted, disjoint, on UTF-8 boundaries
};
struct BoxNeed {
  StrRef src = 0;
  Span span;  // the first requesting node's (diagnostics)
  ResState st = ResState::Pending;
  double w = 0, h = 0;  // Ready: intrinsic CSS px
};

class ResourceTable {
 public:
  // one need per distinct key; returns its index
  u32 needTokens(StrRef lang, StrRef body) {
    const u64 k = ((u64)lang << 32) | body;
    auto it = tokenIndex_.find(k);
    if (it != tokenIndex_.end()) return it->second;
    tokenNeeds.push_back({lang, body});
    return tokenIndex_[k] = (u32)tokenNeeds.size() - 1;
  }
  u32 needBox(StrRef src, Span span) {
    auto it = boxIndex_.find(src);
    if (it != boxIndex_.end()) return it->second;
    BoxNeed b;
    b.src = src;
    b.span = span;
    boxNeeds.push_back(b);
    return boxIndex_[src] = (u32)boxNeeds.size() - 1;
  }
  const TokenNeed* tokens(StrRef lang, StrRef body) const {
    auto it = tokenIndex_.find(((u64)lang << 32) | body);
    return it == tokenIndex_.end() ? nullptr : &tokenNeeds[it->second];
  }
  const BoxNeed* box(StrRef src) const {
    auto it = boxIndex_.find(src);
    return it == boxIndex_.end() ? nullptr : &boxNeeds[it->second];
  }
  // a token or box need still pending: Emit waits (the whole-document
  // barrier of design T9 M4; per-pid deferral is P1-20)
  bool barrierPending() const {
    for (const TokenNeed& t : tokenNeeds)
      if (t.st == ResState::Pending) return true;
    for (const BoxNeed& b : boxNeeds)
      if (b.st == ResState::Pending) return true;
    return false;
  }
  void clear() {
    tokenNeeds.clear();
    boxNeeds.clear();
    tokenIndex_.clear();
    boxIndex_.clear();
    batch = {};
  }

  std::vector<TokenNeed> tokenNeeds;
  std::vector<BoxNeed> boxNeeds;
  // the open request batch: what each kind's resIds stand for
  struct Batch {
    u32 id = 0;
    bool open = false;
    std::vector<MeasureItem> words;  // textWidth
    std::vector<FaceId> vmets;       // fontVmet
    std::vector<u32> tokens, boxes;  // codeTokens, boxInfo: need indices
  } batch;
  u32 nextBatch = 1;

 private:
  std::unordered_map<u64, u32> tokenIndex_;
  std::unordered_map<StrRef, u32> boxIndex_;
};

// a width the host failed to give (design T9 A1): a per-code-point em bound
// × 1.2 — 1em by default, the multi-em dashes and ligatures their own, emoji
// 1.3em; best effort, the line's nowrap confines any residue
double failedWidthPx(std::string_view text, double sizePx);

}  // namespace tsr
