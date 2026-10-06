// Measurement faces (plan P1-04; design T4 M4, v2 §6): the tuple a width is
// measured in — resolved family list, px size, weight, italic — is the only
// metric key, and the only source of metric-bearing paint. Styles that
// differ in paint only (color, link, decoration, lang) share a face and
// therefore share their metrics.
#pragma once
#include <optional>
#include <unordered_map>
#include <vector>

#include "../api/config.h"
#include "../model/style.h"

namespace tsr {

using FaceId = u32;
enum class Script : u8 { Latin, Cjk };  // classification moves to TextRules (P1-11)

// The complete measurement tuple (plan P1-19; design T9 A1 MetricKey, D-T04):
// a width measured under one key is never applied under another — any
// change of font, face status, size, weight, style, features, language or
// device pixel ratio is a different key, so no invalidation channel exists.
struct FaceKey {
  StrRef family = 0;    // the resolved family stack
  u64 faceDigest = 0;   // the loaded declared faces in the stack (none yet: 0)
  double sizePx = 0;
  u16 weight = 400;
  u8 italic = 0;
  u8 caps = 0;          // reserved (synthetic small caps, T5)
  StrRef features = 0;  // font-feature-settings (code runs: code.fontFeatures)
  StrRef lang = 0;      // the shaping language (the run's, else the document's)
  double dppx = 1;      // the device pixel ratio the host measures at
  bool operator==(const FaceKey&) const = default;
};
struct FaceKeyHash {
  size_t operator()(const FaceKey& k) const {
    u64 b, d;
    std::memcpy(&b, &k.sizePx, 8);
    std::memcpy(&d, &k.dppx, 8);
    u64 h = 1469598103934665603ull;
    for (u64 v : {(u64)k.family, k.faceDigest, b, (u64)k.weight, (u64)k.italic, (u64)k.caps, (u64)k.features,
                  (u64)k.lang, d})
      h = (h ^ v) * 1099511628211ull;
    return (size_t)h;
  }
};

// The one em of a style (plan P0-08): an absolute sizePx replaces the base,
// sizeMul composes on top. Measurement, CSS and emit all use this formula.
template <class S>  // a stage view with doc.baseSize
inline double emPx(const S& cfg, const Styling& s) { return emPx(cfg.baseSizePx, s); }

// Family resolution (plan P1-04): an explicit text.font wins; otherwise the
// font role (text.fontRole, plan P2-08: body or mono) and the script
// decide:
//   Latin:  role.latin
//   Cjk:    role.cjk → body.cjk → role.latin   (CJK-class glyphs never fall
//           into a Latin face; no mono special case: a host with a CJK-capable
//           mono font sets fonts.monoCjk)
template <class S>  // a stage view with the fonts.* rows (Measure, Paint)
inline std::string_view familyFor(const S& cfg, bool mono, Script script) {
  if (script == Script::Latin) return mono ? cfg.monoFont : cfg.bodyFont;
  if (mono && !cfg.monoCjkFont.empty()) return cfg.monoCjkFont;
  if (!cfg.cjkFont.empty()) return cfg.cjkFont;
  return mono ? cfg.monoFont : cfg.bodyFont;
}

class FaceTable {
 public:
  FaceTable() = default;
  FaceTable(const FaceTable&) = default;
  // (a view holds references: a copy keeps the faces, the next bind() the
  // settings)
  FaceTable& operator=(const FaceTable& o) {
    if (this == &o) return *this;
    cfg_.reset();
    if (o.cfg_) cfg_.emplace(*o.cfg_);
    styles_ = o.styles_;
    strs_ = o.strs_;
    keys_ = o.keys_;
    index_ = o.index_;
    memo_ = o.memo_;
    return *this;
  }
  void bind(const MeasureSettings& cfg, const StyleTable* styles, Interner* strs) {
    cfg_.emplace(cfg);
    styles_ = styles;
    strs_ = strs;
  }
  // memoised per style; the script is the one T5's classifier gave the run
  // (engine.script, plan P2-08)
  FaceId faceOf(StyleId st) {
    if (st < memo_.size() && memo_[st] != kNone) return memo_[st];
    const Styling& s = styles_->get(st);
    const Script script = s.script == SCRIPT_CJK ? Script::Cjk : Script::Latin;
    const bool mono = s.fontRole == FONTROLE_MONO;
    FaceKey k;
    k.family = s.fontFamily ? s.fontFamily : strs_->intern(familyFor(*cfg_, mono, script));
    k.sizePx = emPx(*cfg_, s);
    k.weight = s.weight ? s.weight : 400;
    // CJK italic is painted upright with emphasis marks (.tsr-cjk.tsr-i):
    // it is measured upright too (v2 §14)
    k.italic = s.italic && script == Script::Latin ? 1 : 0;
    // the phase-1 projection (design T9 M4): features for code runs, the
    // run's language else the document's, the host's dppx
    if (mono && !cfg_->codeFontFeatures.empty()) k.features = strs_->intern(cfg_->codeFontFeatures);
    k.lang = s.lang ? s.lang : strs_->intern(cfg_->lang);
    k.dppx = cfg_->dppx;
    k.faceDigest = loadedDigest(strs_->get(k.family));
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
  // the faces the host has loaded that the stack names (host.loadedFaces:
  // one "family|weight|style|src" per line): a font landing changes the
  // key, so nothing measured against its fallback is ever reused
  u64 loadedDigest(std::string_view stack) const {
    u64 h = 0;
    std::string_view all = cfg_->loadedFaces;
    while (!all.empty()) {
      const size_t nl = all.find('\n');
      const std::string_view line = all.substr(0, nl);
      all = nl == std::string_view::npos ? std::string_view{} : all.substr(nl + 1);
      const std::string_view family = line.substr(0, line.find('|'));
      if (family.empty() || stack.find(family) == std::string_view::npos) continue;
      u64 x = 1469598103934665603ull;
      for (char c : line) x = (x ^ (u8)c) * 1099511628211ull;
      h ^= x;
    }
    return h;
  }
  std::string_view family(FaceId f) const { return strs_->get(keys_[f].family); }
  size_t count() const { return keys_.size(); }

 private:
  static constexpr FaceId kNone = 0xFFFFFFFFu;
  std::optional<MeasureSettings> cfg_;
  const StyleTable* styles_ = nullptr;
  Interner* strs_ = nullptr;
  std::vector<FaceKey> keys_;
  std::unordered_map<FaceKey, FaceId, FaceKeyHash> index_;
  std::vector<FaceId> memo_;
};

}  // namespace tsr
