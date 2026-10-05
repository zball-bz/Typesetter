// Metric store + request building (document-model §6.3/§7). Widths are
// quantized on ingestion: ceil to su + epsilon (the §7 ε policy as arithmetic).
#pragma once
#include <algorithm>
#include <cmath>
#include "../api/config.h"
#include "../model/model.h"
#include "face.h"

namespace tsr {

struct VMet {
  Su ascent = 0, descent = 0;
  bool have = false;
};

struct WordMet {
  Su su;      // quantized: ceil + ε — feeds the breaker (overflow safety)
  double px;  // raw — feeds justification arithmetic (document-model §6.1)
};

// Answers keyed by (string, face): a style query resolves its face first
// (plan P1-04), so paint-only variants of a style never re-request widths.
class MetricStore {
 public:
  void bind(FaceTable* faces) { faces_ = faces; }
  // 32 bits each: no aliasing below 4G strings / faces
  static u64 key(StrRef s, FaceId f) { return ((u64)s << 32) | f; }
  FaceId faceOf(StyleId st) const { return faces_->faceOf(st); }
  bool hasFaceWord(StrRef s, FaceId f) const { return words_.count(key(s, f)) != 0; }
  bool hasWord(StrRef s, StyleId st) const { return hasFaceWord(s, faceOf(st)); }
  const WordMet& word(StrRef s, StyleId st) const { return words_.at(key(s, faceOf(st))); }
  // host metrics are clamped to a finite, representable range (plan P0-11):
  // NaN or 1e300 would overflow the su conversion
  static double hostPx(double px) { return std::isfinite(px) ? std::clamp(px, 0.0, 1e6) : 0.0; }
  void provideWord(StrRef s, FaceId f, double px, const Config& cfg) {
    px = hostPx(px);
    words_[key(s, f)] = {suCeilPx(px) + (Su)cfg.epsilonPerWordSu, px};
  }
  bool hasFaceVmet(FaceId f) const { return f < vmets_.size() && vmets_[f].have; }
  bool hasVmet(StyleId st) const { return hasFaceVmet(faceOf(st)); }
  const VMet& vmet(StyleId st) const { return vmets_[faceOf(st)]; }
  void provideVmet(FaceId f, double ascentPx, double descentPx) {
    if (vmets_.size() <= f) vmets_.resize(f + 1);
    vmets_[f] = {suRoundPx(hostPx(ascentPx)), suRoundPx(hostPx(descentPx)), true};
  }
  void invalidate() {
    words_.clear();
    vmets_.clear();
  }

 private:
  FaceTable* faces_ = nullptr;
  std::unordered_map<u64, WordMet> words_;
  std::vector<VMet> vmets_;
};

struct MeasureItem {
  StrRef str;
  FaceId face;
};
struct MeasureRequest {
  std::vector<FaceId> vmetFaces;
  std::vector<MeasureItem> words;
  bool empty() const { return vmetFaces.empty() && words.empty(); }
};

// The measurement description of a face (for the JS measurer and the mock).
struct StyleDesc {
  std::string family;
  double sizePx;
  int weight;
  bool italic;
};
inline StyleDesc describeFace(const FaceTable& faces, FaceId f) {
  const FaceKey& k = faces.get(f);
  return {std::string(faces.family(f)), k.sizePx, k.weight, k.italic != 0};
}

}  // namespace tsr
