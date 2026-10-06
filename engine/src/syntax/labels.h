// The one label and id grammar (plan P2-06; design T1 SurfaceLexer: lexLabel,
// lexAttrSuffix, lexBareId). A label is '<' LabelChar+ '>' — any character
// but whitespace (ASCII and Unicode) and the label syntax's own `<>[]@,;\`;
// CJK is welcome. A trailing ` <id>` attaches a label where the tail of the
// line is not prose: headings, region and fence openers, math islands. A bare
// reference id is IdStart IdCont* (IdJoin IdCont+)*, so `@sec:intro` names
// sec:intro and `@x.` keeps its period in prose.
#pragma once
#include <string_view>

#include "syntax.gen.h"

namespace tsr {

// Unicode White_Space beyond ASCII
inline bool isUnicodeWhite(u32 cp) {
  return cp == 0x85 || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 ||
         cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

// the byte length of the label character at s[i], 0 when it is none
inline u32 labelCharAt(std::string_view s, u32 i) {
  const unsigned char c = (unsigned char)s[i];
  if (c < 0x80) return isLabelChar((char)c) ? 1 : 0;
  u32 j = i;
  const u32 cp = utf8Next(s, j);
  return isUnicodeWhite(cp) ? 0 : j - i;
}

// '<' LabelChar+ '>' at s[i] (within [i, to)): one past the '>', or 0
inline u32 lexLabel(std::string_view s, u32 i, u32 to) {
  if (i >= to || s[i] != '<') return 0;
  u32 p = i + 1;
  while (p < to) {
    const u32 n = labelCharAt(s, p);
    if (!n) break;
    p += n;
  }
  return p > i + 1 && p < to && s[p] == '>' ? p + 1 : 0;
}

// a ` <id>` closing the text [start, end) (trailing blanks ignored): the
// label's inner span and where the text before it ends; none when the '<'
// is escaped (`\<`) or nothing precedes it
struct LabelSuffix {
  bool ok = false;
  u32 labelStart = 0, labelEnd = 0;  // inside the brackets
  u32 textEnd = 0;                   // the text before the suffix (blanks trimmed)
};
inline LabelSuffix trailingLabel(std::string_view s, u32 start, u32 end) {
  LabelSuffix r;
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) end--;
  if (end < start + 3 || s[end - 1] != '>') return r;
  u32 lt = end - 1;
  while (lt > start && s[lt] != '<') lt--;
  if (s[lt] != '<' || lexLabel(s, lt, end) != end) return r;
  if (lt == start || (s[lt - 1] != ' ' && s[lt - 1] != '\t')) return r;  // ` <id>`: a blank before it
  u32 te = lt - 1;
  while (te > start && (s[te - 1] == ' ' || s[te - 1] == '\t')) te--;
  if (te > start && s[te - 1] == '\\') return r;  // (an escaped blank is not a separator)
  r.ok = true;
  r.labelStart = lt + 1;
  r.labelEnd = end - 1;
  r.textEnd = te;
  return r;
}

// IdStart IdCont* (IdJoin IdCont+)* at s[i]: one past it, or i when none
inline u32 lexBareId(std::string_view s, u32 i, u32 to) {
  if (i >= to || !isIdStart(s[i])) return i;
  u32 p = i + 1;
  while (p < to && isIdCont(s[p])) p++;
  while (p + 1 < to && isIdJoin(s[p]) && isIdCont(s[p + 1])) {
    p++;
    while (p < to && isIdCont(s[p])) p++;
  }
  return p;
}

}  // namespace tsr
