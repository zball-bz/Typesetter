// Measurement faces (plan P1-04; design T4 M4, v2 §6): the tuple a width is
// measured in — resolved family list, px size, weight, italic — is the only
// metric key, and the only source of metric-bearing paint. Styles that
// differ in paint only (color, link, decoration, lang) share a face and
// therefore share their metrics.
#pragma once
#include <unordered_map>
#include <vector>

#include "../api/config.h"
#include "../model/style.h"

namespace tsr {

using FaceId = u32;
enum class Script : u8 { Latin, Cjk };  // classification moves to TextRules (P1-11)

struct FaceKey {
  StrRef family = 0;  // the resolved family list
  double sizePx = 0;
  u16 weight = 400;
  u8 italic = 0;
  u8 caps = 0;  // reserved (synthetic small caps, T5)
  bool operator==(const FaceKey&) const = default;
};
struct FaceKeyHash {
  size_t operator()(const FaceKey& k) const {
    u64 b;
    std::memcpy(&b, &k.sizePx, 8);
    u64 h = 1469598103934665603ull;
    for (u64 v : {(u64)k.family, b, (u64)k.weight, (u64)k.italic, (u64)k.caps}) h = (h ^ v) * 1099511628211ull;
    return (size_t)h;
  }
};

// The one em of a style (plan P0-08): an absolute sizePx replaces the base,
// sizeMul composes on top. Measurement, CSS and emit all use this formula.
inline double emPx(const Config& cfg, const Styling& s) { return emPx(cfg.baseSizePx, s); }

// Family resolution (plan P1-04): an explicit text.font wins; otherwise the
// role (mono for CODE runs, body otherwise — T5 run roles select it from
// P2-08) and the script decide:
//   Latin:  role.latin
//   Cjk:    role.cjk → body.cjk → role.latin   (CJK-class glyphs never fall
//           into a Latin face; no mono special case: a host with a CJK-capable
//           mono font sets fonts.monoCjk)
inline std::string_view familyFor(const Config& cfg, bool mono, Script script) {
  if (script == Script::Latin) return mono ? cfg.monoFont : cfg.bodyFont;
  if (mono && !cfg.monoCjkFont.empty()) return cfg.monoCjkFont;
  if (!cfg.cjkFont.empty()) return cfg.cjkFont;
  return mono ? cfg.monoFont : cfg.bodyFont;
}

class FaceTable {
 public:
  void bind(const Config* cfg, const StyleTable* styles, Interner* strs) {
    cfg_ = cfg;
    styles_ = styles;
    strs_ = strs;
  }
  // memoised per style; the script is the style's CJK bit until TextRules
  FaceId faceOf(StyleId st) {
    if (st < memo_.size() && memo_[st] != kNone) return memo_[st];
    const Styling& s = styles_->get(st);
    const Script script = (s.bits & CLS_CJK) ? Script::Cjk : Script::Latin;
    FaceKey k;
    k.family = s.fontFamily ? s.fontFamily
                            : strs_->intern(familyFor(*cfg_, (s.bits & CLS_CODE) != 0, script));
    k.sizePx = emPx(*cfg_, s);
    k.weight = (s.bits & CLS_BOLD) ? 700 : 400;
    // CJK italic is painted upright with emphasis marks (.tsr-cjk.tsr-i):
    // it is measured upright too (v2 §14)
    k.italic = (s.bits & CLS_EM) && script == Script::Latin ? 1 : 0;
    FaceId f;
    auto it = index_.find(k);
    if (it != index_.end()) {
      f = it->second;
    } else {
      f = (FaceId)keys_.size();
      keys_.push_back(k);
      index_.emplace(k, f);
    }
    if (memo_.size() <= st) memo_.resize(st + 1, kNone);
    memo_[st] = f;
    return f;
  }
  const FaceKey& get(FaceId f) const { return keys_[f]; }
  std::string_view family(FaceId f) const { return strs_->get(keys_[f].family); }
  size_t count() const { return keys_.size(); }

 private:
  static constexpr FaceId kNone = 0xFFFFFFFFu;
  const Config* cfg_ = nullptr;
  const StyleTable* styles_ = nullptr;
  Interner* strs_ = nullptr;
  std::vector<FaceKey> keys_;
  std::unordered_map<FaceKey, FaceId, FaceKeyHash> index_;
  std::vector<FaceId> memo_;
};

}  // namespace tsr
