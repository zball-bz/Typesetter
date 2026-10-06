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

// A length (plan P3-01): em of the document's base size (rem-like) or px;
// unit 0 = unset
struct Len {
  float v = 0;
  u8 unit = 0;  // 1 em, 2 px
  bool operator==(const Len& o) const { return v == o.v && unit == o.unit; }
};
// a gap: a fraction of the paragraph gap (num/den, integer division) or a length
struct Gap {
  u8 num = 0, den = 0;
  Len len;
  bool operator==(const Gap& o) const { return num == o.num && den == o.den && len == o.len; }
};
// "1.5em" | "12px" | "0" (the len domain)
inline Len parseLen(std::string_view v) {
  Len l;
  double x = 0;
  size_t i = 0;
  for (; i < v.size() && v[i] >= '0' && v[i] <= '9'; i++) x = x * 10 + (v[i] - '0');
  if (i < v.size() && v[i] == '.')
    for (double f = 0.1; ++i < v.size() && v[i] >= '0' && v[i] <= '9'; f /= 10) x += (v[i] - '0') * f;
  l.v = (float)x;
  l.unit = v.substr(i) == "px" ? 2 : 1;
  return l;
}
// "1/3" or a length (the gap domain)
inline Gap parseGap(std::string_view v) {
  Gap g;
  const size_t slash = v.find('/');
  if (slash == std::string_view::npos) {
    g.len = parseLen(v);
    return g;
  }
  u32 a = 0, b = 0;
  for (char c : v.substr(0, slash)) a = a * 10 + (u32)(c - '0');
  for (char c : v.substr(slash + 1)) b = b * 10 + (u32)(c - '0');
  g.num = (u8)a;
  g.den = (u8)(b ? b : 1);
  return g;
}

// A node's block properties (schema "props", gran block): one field per row.
// 0 / false / unset = the initial value (the consumer's default)
struct NodeProps {
  Len parIndent = {};  // par.indent (inherits)
  u8 parAlign = 0;  // par.align (inherits)
  u8 parHyphenate = 0;  // par.hyphenate (inherits)
  Gap blockGap = {};  // block.gap
  Len blockIndent = {};  // block.indent
  bool keepWithNext = false;  // block.keepWithNext
  StrRef listMarker = 0;  // list.marker (inherits)
  bool operator==(const NodeProps& o) const {
    return parIndent == o.parIndent &&
           parAlign == o.parAlign &&
           parHyphenate == o.parHyphenate &&
           blockGap == o.blockGap &&
           blockIndent == o.blockIndent &&
           keepWithNext == o.keepWithNext &&
           listMarker == o.listMarker;
  }
};
constexpr u8 PARALIGN_JUSTIFY = 1;
constexpr u8 PARALIGN_START = 2;
constexpr u8 PARALIGN_CENTER = 3;
constexpr u8 PARALIGN_END = 4;
constexpr u8 PARHYPHENATE_AUTO = 1;
constexpr u8 PARHYPHENATE_TRUE = 2;
constexpr u8 PARHYPHENATE_FALSE = 3;
struct NodePropsHash {
  size_t operator()(const NodeProps& p) const {
    u64 h = 1469598103934665603ull;
    auto mix = [&](u64 v) { h = (h ^ v) * 1099511628211ull; };
    auto len = [&](const Len& l) {
      u32 b;
      std::memcpy(&b, &l.v, 4);
      mix(b);
      mix(l.unit);
    };
    len(p.parIndent);
    mix((u64)p.parAlign);
    mix((u64)p.parHyphenate);
    mix(p.blockGap.num);
    mix(p.blockGap.den);
    len(p.blockGap.len);
    len(p.blockIndent);
    mix((u64)p.keepWithNext);
    mix((u64)p.listMarker);
    return (size_t)h;
  }
};
// a child's starting props: the inheriting rows of its parent's, the rest initial
inline NodeProps inheritProps(const NodeProps& parent) {
  NodeProps p;
  p.parIndent = parent.parIndent;
  p.parAlign = parent.parAlign;
  p.parHyphenate = parent.parHyphenate;
  p.listMarker = parent.listMarker;
  return p;
}
// folds one styled attribute onto block properties (values validated at decode)
template <class Intern, class View>
inline void applyNodeArg(NodeProps& p, const ArgVal& a, Intern intern, View view) {
  if (a.key == ArgK::parIndent && a.tag == ArgTag::Str) p.parIndent = parseLen(view(a.ref));
  if (a.key == ArgK::parAlign && a.tag == ArgTag::Str) {
    const std::string_view v = view(a.ref);
    if (v == "justify") p.parAlign = 1;
    if (v == "start") p.parAlign = 2;
    if (v == "center") p.parAlign = 3;
    if (v == "end") p.parAlign = 4;
  }
  if (a.key == ArgK::parHyphenate && a.tag == ArgTag::Str) {
    const std::string_view v = view(a.ref);
    if (v == "auto") p.parHyphenate = 1;
    if (v == "true") p.parHyphenate = 2;
    if (v == "false") p.parHyphenate = 3;
  }
  if (a.key == ArgK::blockGap && a.tag == ArgTag::Str) p.blockGap = parseGap(view(a.ref));
  if (a.key == ArgK::blockIndent && a.tag == ArgTag::Str) p.blockIndent = parseLen(view(a.ref));
  if (a.key == ArgK::keepWithNext && a.tag == ArgTag::Bool) p.keepWithNext = a.num != 0;
  if (a.key == ArgK::listMarker && a.tag == ArgTag::Str) p.listMarker = intern(a.ref);
}
// the style keys by name (plan P3-01: rules in JSON read them as
// $.style.push does): each row's attribute name and its key path
struct StyleKeyRow {
  const char* key;
  ArgK attr;
};
inline constexpr StyleKeyRow kStyleKeys[] = {
    {"weight", ArgK::weight},
    {"italic", ArgK::italic},
    {"decoration", ArgK::decoration},
    {"fontRole", ArgK::fontRole},
    {"baseline", ArgK::baseline},
    {"hang", ArgK::hang},
    {"code.hang", ArgK::hang},
    {"size", ArgK::size},
    {"font", ArgK::font},
    {"lang", ArgK::lang},
    {"color", ArgK::color},
    {"sizePx", ArgK::sizePx},
    {"parIndent", ArgK::parIndent},
    {"par.indent", ArgK::parIndent},
    {"parAlign", ArgK::parAlign},
    {"par.align", ArgK::parAlign},
    {"parHyphenate", ArgK::parHyphenate},
    {"par.hyphenate", ArgK::parHyphenate},
    {"blockGap", ArgK::blockGap},
    {"block.gap", ArgK::blockGap},
    {"blockIndent", ArgK::blockIndent},
    {"block.indent", ArgK::blockIndent},
    {"keepWithNext", ArgK::keepWithNext},
    {"block.keepWithNext", ArgK::keepWithNext},
    {"listMarker", ArgK::listMarker},
    {"list.marker", ArgK::listMarker},
};
// whether an attribute patches a block property
inline bool isNodeArg(ArgK k) {
  return k == ArgK::parIndent || k == ArgK::parAlign || k == ArgK::parHyphenate || k == ArgK::blockGap || k == ArgK::blockIndent || k == ArgK::keepWithNext || k == ArgK::listMarker;
}

}  // namespace tsr
