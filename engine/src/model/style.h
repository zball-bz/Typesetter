// The style table (document-model §3): StyleId, the effective run style.
// Split from model.h (plan P1-18) so layout, paint and the typeset backend
// read styles without seeing the content tree.
#pragma once
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "../ops/ops.h"
#include "props.gen.h"

namespace tsr {

// Effective style (document-model §3): `Styling` is generated from the run
// properties of the schema's "props" section (props.gen.h) — weight, italic,
// decoration, font role, baseline, relative size, family, lang, color,
// absolute size (plan P2-08: they replace the class bits) — plus the script
// T5's classifier gives a run (engine.script: never on the wire), which
// selects its face. letterSpacing is engine-owned justification, never a
// property.
constexpr u8 SCRIPT_CJK = 1;

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

// An engine-made change of a run style (plan P2-08; it replaces the class
// bits engine code ORed in): what kind presentation (inline code is mono, a
// heading bold), classification (a CJK run) and generated content (a
// caption label, a footnote marker) put on top of a node's style. Unset
// fields keep the style's; decorations OR in; sizes multiply.
struct StyleDelta {
  u16 weight = 0;
  bool italic = false;
  u8 decoration = 0;
  u8 fontRole = 0;
  u8 baseline = 0;
  u8 script = 0;
  float mul = 1.0f;
  bool empty() const {
    return !weight && !italic && !decoration && !fontRole && !baseline && !script && mul == 1.0f;
  }
  // `o` on top of this one
  StyleDelta& operator+=(const StyleDelta& o) {
    if (o.weight) weight = o.weight;
    italic = italic || o.italic;
    decoration |= o.decoration;
    if (o.fontRole) fontRole = o.fontRole;
    if (o.baseline) baseline = o.baseline;
    if (o.script) script = o.script;
    mul *= o.mul;
    return *this;
  }
  StyleDelta operator+(const StyleDelta& o) const { return StyleDelta(*this) += o; }
};
inline void applyDelta(Styling& s, const StyleDelta& d) {
  if (d.weight) s.weight = d.weight;
  if (d.italic) s.italic = true;
  s.decoration |= d.decoration;
  if (d.fontRole) s.fontRole = d.fontRole;
  if (d.baseline) s.baseline = d.baseline;
  if (d.script) s.script = d.script;
  s.sizeMul *= d.mul;
}

// A style derived from `base` (plan P0-09, T3 S0 f): generated text — caption
// prefixes, term names, links — composes on its site's style instead of an
// absolute Styling, so font, language and color scopes carry into it.
inline StyleId compose(StyleTable& styles, StyleId base, const StyleDelta& d, float mul = 1.0f) {
  if (d.empty() && mul == 1.0f) return base;
  Styling s = styles.get(base);
  applyDelta(s, d);
  s.sizeMul *= mul;
  return styles.idOf(s);
}

// The one em of a style (plan P0-08): an absolute sizePx replaces the base,
// sizeMul composes on top. Measurement, CSS and emit all use this formula.
inline double emPx(double basePx, const Styling& s) {
  double base = s.sizePx > 0 ? (double)s.sizePx : basePx;
  return base * (double)s.sizeMul;
}

// (plan P3-18; design T4 M8) a class list with more classes: sorted,
// unique, space-separated, interned (Styling::classes; metric-neutral)
inline StrRef mergeClasses(Interner& strs, StrRef have, std::string_view add) {
  if (add.empty()) return have;
  std::vector<std::string_view> v;
  auto split = [&](std::string_view s) {
    for (size_t at = 0; at < s.size();) {
      size_t sp = s.find(' ', at);
      if (sp == std::string_view::npos) sp = s.size();
      if (sp > at) v.push_back(s.substr(at, sp - at));
      at = sp + 1;
    }
  };
  const std::string h(have ? strs.get(have) : std::string_view{});
  const std::string a(add);
  split(h);
  split(a);
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  std::string out;
  for (std::string_view x : v) {
    if (!out.empty()) out += ' ';
    out += x;
  }
  return strs.intern(out);
}

}  // namespace tsr
