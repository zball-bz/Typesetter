// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// Run style properties (schema "props"; plans P1-02, P2-08; design T4 M2/M5).
#pragma once
#include <cstring>

#include "../ops/ops.h"

namespace tsr {

// The effective run style: one field per property row, in row order. 0 /
// false / StrRef 0 = not set.
struct Styling {
  u16 weight = 0;  // text.weight
  bool italic = false;  // text.italic
  u8 decoration = 0;  // text.decoration
  u8 fontRole = 0;  // text.fontRole
  u8 baseline = 0;  // text.baseline
  float sizeMul = 1.0f;  // text.sizeMul
  StrRef fontFamily = 0;  // text.font
  StrRef lang = 0;  // text.lang
  StrRef color = 0;  // text.color
  float sizePx = 0;  // text.size
  u8 script = 0;  // engine.script
  bool operator==(const Styling& o) const {
    return weight == o.weight &&
           italic == o.italic &&
           decoration == o.decoration &&
           fontRole == o.fontRole &&
           baseline == o.baseline &&
           sizeMul == o.sizeMul &&
           fontFamily == o.fontFamily &&
           lang == o.lang &&
           color == o.color &&
           sizePx == o.sizePx &&
           script == o.script;
  }
};

constexpr u8 DECORATION_UNDER = 1;
constexpr u8 DECORATION_OVER = 2;
constexpr u8 DECORATION_STRIKE = 4;
constexpr u8 FONTROLE_BODY = 1;
constexpr u8 FONTROLE_MONO = 2;
constexpr u8 BASELINE_SUPER = 1;
constexpr u8 BASELINE_SUB = 2;

// a hash over the canonical bits of every field
struct StylingHash {
  size_t operator()(const Styling& s) const {
    u64 h = 1469598103934665603ull;
    auto mix = [&](u64 v) { h = (h ^ v) * 1099511628211ull; };
    mix((u64)s.weight);
    mix((u64)s.italic);
    mix((u64)s.decoration);
    mix((u64)s.fontRole);
    mix((u64)s.baseline);
    {
      u32 b;
      std::memcpy(&b, &s.sizeMul, 4);
      mix(b);
    }
    mix((u64)s.fontFamily);
    mix((u64)s.lang);
    mix((u64)s.color);
    {
      u32 b;
      std::memcpy(&b, &s.sizePx, 4);
      mix(b);
    }
    mix((u64)s.script);
    return (size_t)h;
  }
};

// canonical floats (plan P0-08): -0 → +0, NaN (and a negative px) → the initial value
inline void canonicalize(Styling& s) {
  if (!(s.sizeMul == s.sizeMul)) s.sizeMul = 1.0f;
  if (s.sizeMul == 0) s.sizeMul = 0.0f;
  if (!(s.sizePx == s.sizePx) || s.sizePx < 0) s.sizePx = 0;
  if (s.sizePx == 0) s.sizePx = 0.0f;
}

// the v6-10 styled `bits` flags, folded onto the rows that replace them
inline void applyLegacyBits(Styling& st, u64 b) {
  if (b & (1ull << 3)) st.weight = 700;
  if (b & (1ull << 2)) st.italic = true;
  if (b & (1ull << 16)) st.decoration |= DECORATION_UNDER;
  if (b & (1ull << 17)) st.decoration |= DECORATION_OVER;
  if (b & (1ull << 18)) st.decoration |= DECORATION_STRIKE;
}

// folds one styled attribute or STYLE_PUSH patch value onto a style (values
// were validated at decode); intern(ref) maps a buffer string to a StrRef
template <class Intern>
inline void applyStyleArg(Styling& st, const ArgVal& a, Intern intern) {
  if (a.key == ArgK::bits && a.tag == ArgTag::Num && a.num >= 0) applyLegacyBits(st, (u64)a.num);
  if (a.key == ArgK::font && a.tag == ArgTag::Str) st.fontFamily = intern(a.ref);
  if (a.key == ArgK::lang && a.tag == ArgTag::Str) st.lang = intern(a.ref);
  if (a.key == ArgK::color && a.tag == ArgTag::Str) st.color = intern(a.ref);
  if (a.key == ArgK::sizePx && a.tag == ArgTag::Num) st.sizePx = (float)a.num;
}

// the value part of the tree and block dumps, in row order (each dump keeps
// its own flag tokens and size-multiplier spelling)
inline void appendStyleFields(std::string& out, const Styling& s, const Interner& strs) {
  if (s.fontFamily) {
    out += " font=\"";
    appendEscaped(out, strs.get(s.fontFamily));
    out += "\"";
  }
  if (s.lang) {
    out += " lang=";
    out += strs.get(s.lang);
  }
  if (s.color) {
    out += " color=";
    out += strs.get(s.color);
  }
  if (s.sizePx > 0) appendf(out, " size=%gpx", (double)s.sizePx);
}

}  // namespace tsr
