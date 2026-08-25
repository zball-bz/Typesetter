// The normative mock measurer (testing.md §2). Implemented identically in
// runtime/src/shared/mockmeasure.mjs — drift between the two fails CI.
#pragma once
#include "measure.h"

namespace tsr {

inline double mockCpWidthEm(u32 cp) {
  if (cp == ' ') return 0.25;
  if (cp >= 0x21 && cp <= 0x7E) return 0.5;
  if (isCjk(cp)) return 1.0;
  return 0.6;
}

inline double mockWordWidthPx(std::string_view word, double sizePx) {
  double em = 0;
  u32 i = 0;
  while (i < word.size()) em += mockCpWidthEm(utf8Next(word, i));
  return em * sizePx;
}

inline void mockProvide(const MeasureRequest& req, MetricStore& store,
                        const Interner& strs, const StyleTable& styles,
                        const Config& cfg) {
  for (StyleId st : req.vmetStyles) {
    StyleDesc d = describeStyle(cfg, styles.get(st), strs);
    store.provideVmet(st, 0.8 * d.sizePx, 0.2 * d.sizePx);
  }
  for (const MeasureItem& it : req.words) {
    StyleDesc d = describeStyle(cfg, styles.get(it.style), strs);
    store.provideWord(it.str, it.style, mockWordWidthPx(strs.get(it.str), d.sizePx), cfg);
  }
}

}  // namespace tsr
