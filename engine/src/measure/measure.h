// Metric store + request building (document-model §6.3/§7). Widths are
// quantized on ingestion: ceil to su + epsilon (the §7 ε policy as arithmetic).
#pragma once
#include "../api/config.h"
#include "../model/model.h"

namespace tsr {

struct VMet {
  Su ascent = 0, descent = 0;
  bool have = false;
};

struct WordMet {
  Su su;      // quantized: ceil + ε — feeds the breaker (overflow safety)
  double px;  // raw — feeds justification arithmetic (document-model §6.1)
};

class MetricStore {
 public:
  static u64 key(StrRef s, StyleId st) { return ((u64)s << 24) | st; }
  bool hasWord(StrRef s, StyleId st) const { return words_.count(key(s, st)) != 0; }
  const WordMet& word(StrRef s, StyleId st) const { return words_.at(key(s, st)); }
  void provideWord(StrRef s, StyleId st, double px, const Config& cfg) {
    words_[key(s, st)] = {suCeilPx(px) + (Su)cfg.epsilonPerWordSu, px};
  }
  bool hasVmet(StyleId st) const { return st < vmets_.size() && vmets_[st].have; }
  const VMet& vmet(StyleId st) const { return vmets_[st]; }
  void provideVmet(StyleId st, double ascentPx, double descentPx) {
    if (vmets_.size() <= st) vmets_.resize(st + 1);
    vmets_[st] = {suRoundPx(ascentPx), suRoundPx(descentPx), true};
  }
  void invalidate() {
    words_.clear();
    vmets_.clear();
  }

 private:
  std::unordered_map<u64, WordMet> words_;
  std::vector<VMet> vmets_;
};

struct MeasureItem {
  StrRef str;
  StyleId style;
};
struct MeasureRequest {
  std::vector<StyleId> vmetStyles;
  std::vector<MeasureItem> words;
  bool empty() const { return vmetStyles.empty() && words.empty(); }
};

// CSS-facing description of a style (for the JS measurer and the renderer).
struct StyleDesc {
  std::string family;
  double sizePx;
  int weight;
  bool italic;
};
inline StyleDesc describeStyle(const Config& cfg, const Styling& s) {
  StyleDesc d;
  d.family = (s.bits & CLS_CODE) ? cfg.monoFont
             : (s.bits & CLS_CJK) ? cfg.cjkFont
                                  : cfg.bodyFont;
  d.sizePx = cfg.baseSizePx * (double)s.sizeMul;
  d.weight = (s.bits & CLS_BOLD) ? 700 : 400;
  d.italic = (s.bits & CLS_EM) != 0;
  return d;
}

}  // namespace tsr
