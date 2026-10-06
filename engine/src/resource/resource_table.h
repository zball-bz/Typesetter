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
#include "box.h"
#include "codec.h"

namespace tsr {

enum class ResState : u8 { Pending, Ready, Failed };

// A code body's tokens. With overlays (plan P3-22; code/overlay.h) the text
// the provider tokenizes, `sent`, is the body with the overlay spans
// blanked; `toks` is its answer (as the Session keeps it), `runs()` that
// answer with the spans set over it.
struct TokenNeed {
  StrRef lang = 0, body = 0;
  u32 overlays = 0;               // the overlay mask
  StrRef sent = 0;                // the provider's text: body, or body masked
  std::vector<CodeToken> spans;   // the overlay spans (tokens of their class)
  ResState st = ResState::Pending;
  std::vector<CodeToken> toks;    // Ready: sorted, disjoint, on UTF-8 boundaries
  std::vector<CodeToken> merged;  // Ready, with spans: toks with the spans set
  const std::vector<CodeToken>& runs() const { return spans.empty() ? toks : merged; }
};
// A replaced box's size (resources.def boxInfo; plan P3-28, design T6 S14 /
// T9 M11): its kind, its payload (an image's src, the markup of an svg or
// html box) and the width it is measured at — 0 for an image's intrinsic
// size, else the box's width (a block's available width at layout, an
// inline box's declared one), so a width-dependent box is one need per
// width. The answer: CSS px; baseline from the box's top.
struct BoxNeed {
  BoxKind kind = BoxKind::Image;
  StrRef src = 0;
  double availPx = 0;
  Span span;  // the first requesting node's (diagnostics)
  ResState st = ResState::Pending;
  double w = 0, h = 0, baseline = 0;  // Ready: CSS px
  bool emit = false;  // an Emit consumer waits on it (an image, an inline box): its answer re-runs Emit
};

class ResourceTable {
 public:
  // one need per distinct key (language, body, overlays); returns its
  // index — a new one's `sent` and `spans` are the caller's to fill
  u32 needTokens(StrRef lang, StrRef body, u32 overlays, bool* fresh = nullptr) {
    const TokenKey k{lang, body, overlays};
    auto it = tokenIndex_.find(k);
    if (fresh) *fresh = it == tokenIndex_.end();
    if (it != tokenIndex_.end()) return it->second;
    TokenNeed t;
    t.lang = lang;
    t.body = body;
    t.overlays = overlays;
    t.sent = body;
    tokenNeeds.push_back(std::move(t));
    return tokenIndex_[k] = (u32)tokenNeeds.size() - 1;
  }
  u32 needBox(BoxKind kind, StrRef src, double availPx, Span span, bool* fresh = nullptr) {
    const BoxKey k{kind, src, suRoundPx(availPx)};
    auto it = boxIndex_.find(k);
    if (fresh) *fresh = it == boxIndex_.end();
    if (it != boxIndex_.end()) return it->second;
    BoxNeed b;
    b.kind = kind;
    b.src = src;
    b.availPx = availPx;
    b.span = span;
    boxNeeds.push_back(b);
    return boxIndex_[k] = (u32)boxNeeds.size() - 1;
  }
  const TokenNeed* tokens(StrRef lang, StrRef body, u32 overlays) const {
    auto it = tokenIndex_.find(TokenKey{lang, body, overlays});
    return it == tokenIndex_.end() ? nullptr : &tokenNeeds[it->second];
  }
  const BoxNeed* box(BoxKind kind, StrRef src, double availPx = 0) const {
    auto it = boxIndex_.find(BoxKey{kind, src, suRoundPx(availPx)});
    return it == boxIndex_.end() ? nullptr : &boxNeeds[it->second];
  }
  // a token or box need still pending (the blocks that wait on one are
  // Doc::topWaits'; plan P1-20)
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
  struct TokenKey {
    StrRef lang, body;
    u32 overlays;
    bool operator==(const TokenKey& o) const { return lang == o.lang && body == o.body && overlays == o.overlays; }
  };
  struct TokenKeyHash {
    size_t operator()(const TokenKey& k) const {
      return std::hash<u64>()(((u64)k.lang << 32) | k.body) ^ (std::hash<u32>()(k.overlays) * 0x9E3779B97F4A7C15ull);
    }
  };
  struct BoxKey {
    BoxKind kind;
    StrRef src;
    Su avail;  // the width, quantized (one need per su)
    bool operator==(const BoxKey& o) const { return kind == o.kind && src == o.src && avail == o.avail; }
  };
  struct BoxKeyHash {
    size_t operator()(const BoxKey& k) const {
      return std::hash<u64>()(((u64)k.src << 32) | (u32)k.avail) ^ ((size_t)k.kind * 0x9E3779B97F4A7C15ull);
    }
  };
  std::unordered_map<TokenKey, u32, TokenKeyHash> tokenIndex_;
  std::unordered_map<BoxKey, u32, BoxKeyHash> boxIndex_;
};

// a width the host failed to give (design T9 A1): a per-code-point em bound
// × 1.2 — 1em by default, the multi-em dashes and ligatures their own, emoji
// 1.3em; best effort, the line's nowrap confines any residue
double failedWidthPx(std::string_view text, double sizePx);

}  // namespace tsr
