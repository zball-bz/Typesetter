#pragma once
#include "../support/support.h"

namespace tsr {

struct SourceText {
  std::string text;
  std::vector<u32> lineStarts;  // byte offset of each line start

  void init(std::string src) {
    text = std::move(src);
    lineStarts.clear();
    lineStarts.push_back(0);
    for (u32 i = 0; i < text.size(); i++)
      if (text[i] == '\n') lineStarts.push_back(i + 1);
  }
  u32 size() const { return (u32)text.size(); }
  std::string_view view() const { return text; }
  std::string_view slice(Span s) const {
    return std::string_view(text).substr(s.start, s.end - s.start);
  }
  u32 lineCount() const { return (u32)lineStarts.size(); }
  u32 lineStart(u32 ln) const { return lineStarts[ln]; }
  u32 lineEnd(u32 ln) const {  // exclusive of the line terminator (\n or \r\n)
    u32 e = (ln + 1 < lineStarts.size()) ? lineStarts[ln + 1] : size();
    if (e > lineStart(ln) && e <= size() && e > 0 && text[e - 1] == '\n') e--;
    if (e > lineStart(ln) && text[e - 1] == '\r') e--;
    return e;
  }
};

// Cooked text of a raw multi-line slice: CRLF line terminators read as LF,
// so a CRLF source produces the same AST as its LF twin (spans stay raw).
inline std::string crlfToLf(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
    out += s[i];
  }
  return out;
}

}  // namespace tsr
