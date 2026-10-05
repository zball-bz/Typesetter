// The style table (document-model §3): class bits, StyleId, the effective
// run style. Split from model.h (plan P1-18) so layout, paint and the typeset
// backend read styles without seeing the content tree.
#pragma once
#include <unordered_map>
#include <vector>

#include "../ops/ops.h"
#include "props.gen.h"

namespace tsr {

// Frozen base class bits (document-model §3).
enum : u64 {
  CLS_LATIN = 1ull << 0,
  CLS_CJK = 1ull << 1,
  CLS_EM = 1ull << 2,
  CLS_BOLD = 1ull << 3,
  CLS_CODE = 1ull << 6,
  CLS_LINK = 1ull << 13,
  // decorations (CH1, code-design.md §3): rendered as text-decoration,
  // metric-neutral by construction
  CLS_UNDER = 1ull << 16,
  CLS_OVER = 1ull << 17,
  CLS_STRIKE = 1ull << 18,
  // superscript (notes-design.md §1): footnote markers — size rides
  // sizeMul (measured), the raise is paint-only (position: relative)
  CLS_SUP = 1ull << 19,
};

// Effective style (document-model §3): `Styling` is generated from the run
// properties of the schema's "props" section (props.gen.h) — class bits,
// relative size, font family, lang, color, absolute size. Weight/italic ride
// the bits; letterSpacing is engine-owned justification, never a property.

using StyleId = u32;
class StyleTable {
 public:
  StyleTable() { idOf(Styling{}); }  // id 0 = base
  StyleId idOf(Styling s) {
    canonicalize(s);  // equal styles hash equally (the hash reads the bit patterns)
    auto it = map_.find(s);
    if (it != map_.end()) return it->second;
    StyleId id = (StyleId)styles_.size();
    styles_.push_back(s);
    map_.emplace(s, id);
    return id;
  }
  const Styling& get(StyleId id) const { return styles_[id]; }
  size_t count() const { return styles_.size(); }

 private:
  std::vector<Styling> styles_;
  std::unordered_map<Styling, StyleId, StylingHash> map_;
};

// A style derived from `base` (plan P0-09, T3 S0 f): generated text — caption
// prefixes, term names, links — composes on its site's style instead of an
// absolute Styling, so font, language and color scopes carry into it.
inline StyleId compose(StyleTable& styles, StyleId base, u64 addBits, float mul = 1.0f) {
  if (addBits == 0 && mul == 1.0f) return base;
  Styling s = styles.get(base);
  s.bits |= addBits;
  s.sizeMul *= mul;
  return styles.idOf(s);
}

// The one em of a style (plan P0-08): an absolute sizePx replaces the base,
// sizeMul composes on top. Measurement, CSS and emit all use this formula.
inline double emPx(double basePx, const Styling& s) {
  double base = s.sizePx > 0 ? (double)s.sizePx : basePx;
  return base * (double)s.sizeMul;
}

}  // namespace tsr
