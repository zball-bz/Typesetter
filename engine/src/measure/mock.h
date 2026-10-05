// The normative mock measurer (testing.md §2): the golden metrics. Its
// widths depend on nothing but this file — the wide ranges below are a
// frozen literal copy (plan P1-11), so changing the engine's character
// classes (shape/textrules.h) never changes a mock width; engine/test pins
// them (unitTextRules).
#pragma once
#include "measure.h"

namespace tsr {

inline bool mockIsWide(u32 cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
         (cp >= 0x20000 && cp <= 0x2FA1F);
}

inline double mockCpWidthEm(u32 cp) {
  if (cp == ' ') return 0.25;
  if (cp >= 0x21 && cp <= 0x7E) return 0.5;
  if (mockIsWide(cp)) return 1.0;
  return 0.6;
}

inline double mockWordWidthPx(std::string_view word, double sizePx) {
  double em = 0;
  u32 i = 0;
  while (i < word.size()) em += mockCpWidthEm(utf8Next(word, i));
  return em * sizePx;
}

// widths and vertical metrics depend only on the face's px size
inline void mockProvide(const MeasureRequest& req, MetricStore& store, const Interner& strs,
                        const FaceTable& faces) {
  for (FaceId f : req.vmetFaces) {
    double px = faces.get(f).sizePx;
    store.provideVmet(f, 0.8 * px, 0.2 * px);
  }
  for (const MeasureItem& it : req.words)
    store.provideWord(it.str, it.face, mockWordWidthPx(strs.get(it.str), faces.get(it.face).sizePx));
}

}  // namespace tsr
