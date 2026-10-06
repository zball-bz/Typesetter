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
  u8 hang = 0;  // code.hang
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
           hang == o.hang &&
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
constexpr u8 HANG_INDENT = 1;
constexpr u8 HANG_CONTENT = 2;

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
    mix((u64)s.hang);
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

// folds one styled attribute — a delta node's: a styled node's own, a node's
// `style`, a STYLE_PUSH's (plan P2-08) — onto a style (values were validated
// at decode); intern(ref) maps a buffer string to a StrRef, view(ref) reads
// it. Sizes (D-T01): px is absolute and replaces (the multiplier resets), em
// and % multiply
template <class Intern, class View>
inline void applyStyleArg(Styling& st, const ArgVal& a, Intern intern, View view) {
  if (a.key == ArgK::weight && a.tag == ArgTag::Num) st.weight = (u16)a.num;
  if (a.key == ArgK::italic && a.tag == ArgTag::Bool) st.italic = a.num != 0;
  if (a.key == ArgK::decoration && a.tag == ArgTag::Num) st.decoration |= (u8)(u64)a.num;  // ORed in
  if (a.key == ArgK::fontRole && a.tag == ArgTag::Str) {
    const std::string_view v = view(a.ref);
    if (v == "body") st.fontRole = 1;
    if (v == "mono") st.fontRole = 2;
  }
  if (a.key == ArgK::baseline && a.tag == ArgTag::Str) {
    const std::string_view v = view(a.ref);
    if (v == "super") st.baseline = 1;
    if (v == "sub") st.baseline = 2;
  }
  if (a.key == ArgK::hang && a.tag == ArgTag::Str) {
    const std::string_view v = view(a.ref);
    if (v == "indent") st.hang = 1;
    if (v == "content") st.hang = 2;
  }
  if (a.key == ArgK::size && a.tag == ArgTag::Str) {  // "0.7em" | "70%" | "22px"
    const std::string_view v = view(a.ref);
    double x = 0;
    size_t i = 0;
    for (; i < v.size() && v[i] >= '0' && v[i] <= '9'; i++) x = x * 10 + (v[i] - '0');
    if (i < v.size() && v[i] == '.')
      for (double f = 0.1; ++i < v.size() && v[i] >= '0' && v[i] <= '9'; f /= 10) x += (v[i] - '0') * f;
    const std::string_view unit = v.substr(i);
    if (unit == "px") {
      if (x > 0) st.sizePx = (float)x;
      st.sizeMul = 1.0f;
    } else {
      st.sizeMul *= (float)(unit == "%" ? x / 100 : x);
    }
  }
  if (a.key == ArgK::font && a.tag == ArgTag::Str) st.fontFamily = intern(a.ref);
  if (a.key == ArgK::lang && a.tag == ArgTag::Str) st.lang = intern(a.ref);
  if (a.key == ArgK::color && a.tag == ArgTag::Str) st.color = intern(a.ref);
  if (a.key == ArgK::sizePx && a.tag == ArgTag::Num) {
    st.sizePx = (float)a.num;
    st.sizeMul = 1.0f;
  }
}

// the value part of the tree and block dumps, in row order (each dump keeps
// its own flag tokens and size-multiplier spelling)
inline void appendStyleFields(std::string& out, const Styling& s, const Interner& strs) {
  if (s.hang) {
    static const char* const kV[] = {"indent", "content"};
    out += " hang=";
    out += kV[s.hang - 1];
  }
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
