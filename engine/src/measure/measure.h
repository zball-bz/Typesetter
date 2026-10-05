// Metric store + request building (document-model §6.3/§7). Widths are
// quantized on ingestion: ceil to su + epsilon (the §7 ε policy as arithmetic).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "../api/config.h"
#include "../model/style.h"
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
  bool hasFaceWord(StrRef s, FaceId f) const { return find(s, f) != nullptr; }
  bool hasWord(StrRef s, StyleId st) const { return hasFaceWord(s, faceOf(st)); }
  // the store keeps raw px (plan P1-19: answers are the host's, quantization
  // is Measure's): ceil to su + the per-word ε of the Measure stage
  WordMet word(StrRef s, StyleId st) const {
    const double* px = find(s, faceOf(st));
    if (!px) std::abort();  // precondition: hasWord
    return {suCeilPx(*px) + epsilon_, *px};
  }
  void setEpsilon(Su eps) { epsilon_ = eps; }
  // host metrics are clamped to a finite, representable range (plan P0-11):
  // NaN or 1e300 would overflow the su conversion
  static double hostPx(double px) { return std::isfinite(px) ? std::clamp(px, 0.0, 1e6) : 0.0; }
  void provideWord(StrRef s, FaceId f, double px) {
    px = hostPx(px);
    if (double* w = const_cast<double*>(find(s, f))) {
      *w = px;
      return;
    }
    if (s >= head_.size()) head_.resize((size_t)s + 1, 0);
    slots_.push_back({f, head_[s], px});
    head_[s] = (u32)slots_.size();
  }
  bool hasFaceVmet(FaceId f) const { return f < vmets_.size() && vmets_[f].have; }
  bool hasVmet(StyleId st) const { return hasFaceVmet(faceOf(st)); }
  const VMet& vmet(StyleId st) const { return vmets_[faceOf(st)]; }
  void provideVmet(FaceId f, double ascentPx, double descentPx) {
    if (vmets_.size() <= f) vmets_.resize(f + 1);
    vmets_[f] = {suRoundPx(hostPx(ascentPx)), suRoundPx(hostPx(descentPx)), true};
  }
  void invalidate() {
    head_.clear();
    slots_.clear();
    vmets_.clear();
  }

 private:
  // words by string: StrRefs are dense interner ids (host answers are
  // interned engine-side), and a string is measured in few faces — a slot
  // chain per string replaces a hash lookup on the measure loop's hot path
  struct Slot {
    FaceId face;
    u32 next;  // 1 + the next slot of this string, 0 = none
    double px;
  };
  const double* find(StrRef s, FaceId f) const {
    if (s >= head_.size()) return nullptr;
    for (u32 i = head_[s]; i; i = slots_[i - 1].next)
      if (slots_[i - 1].face == f) return &slots_[i - 1].px;
    return nullptr;
  }
  FaceTable* faces_ = nullptr;
  std::vector<u32> head_;  // per string: 1 + its first slot, 0 = none
  std::vector<Slot> slots_;
  std::vector<VMet> vmets_;
  Su epsilon_ = 1;
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

// What a pass that needs text metrics before Measure reads them through
// (plan P1-20; design T9 A1 need()): the store, and where to record what is
// still missing — the pass defers on it, nothing re-runs the document.
struct MeasureNeeds {
  const MetricStore* metrics = nullptr;
  StyleTable* styles = nullptr;
  Interner* strs = nullptr;
  double docBasePx = 0;                 // Config::baseSizePx (style ids scale on it)
  std::vector<MeasureItem>* missing = nullptr;
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
