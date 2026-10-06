// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// A typeset run's attributes and style declarations (schema "props"; plan
// P1-02). Values were validated at decode; text values are attribute-escaped.
#pragma once
#include <cstdio>
#include <cstring>

#include "../model/style.h"
#include "html_writer.h"

namespace tsr {

inline void runCss(Tag& t, const Styling& st, double basePx, const Interner& strs) {
  if (st.lang) t.attr("lang", strs.get(st.lang));
  if (st.sizeMul != 1.0f || st.sizePx > 0) t.px("font-size", emPx(basePx, st));
  if (st.fontFamily) t.declEsc("font-family", strs.get(st.fontFamily));
  if (st.color) t.declEsc("color", strs.get(st.color));
  if (st.decoration) {
    char buf[64];
    size_t n = 0;
    auto add = [&](const char* w) {
      if (n) buf[n++] = ' ';
      std::memcpy(buf + n, w, std::strlen(w));
      n += std::strlen(w);
    };
    if (st.decoration & DECORATION_UNDER) add("underline");
    if (st.decoration & DECORATION_OVER) add("overline");
    if (st.decoration & DECORATION_STRIKE) add("line-through");
    t.decl("text-decoration", std::string_view(buf, n));
  }
  if (st.weight && st.weight != 400 && st.weight != 700) {
    char w[8];
    t.decl("font-weight", std::string_view(w, (size_t)std::snprintf(w, sizeof w, "%u", (unsigned)st.weight)));
  }
}

// the contract classes a run's style gives it (schema "contract"; plan P3-18)
template <class Add>
inline void contractClasses(const Styling& st, Add add) {
  if (st.weight == 700) add("tsr-b");
  if (st.italic) add("tsr-i");
  if (st.script == SCRIPT_CJK) add("tsr-cjk");
  if (st.fontRole == FONTROLE_MONO) add("tsr-code");
  if (st.space == SPACE_PRE) add("tsr-pre");
  if (st.baseline == BASELINE_SUPER) add("tsr-sup");
}

}  // namespace tsr
