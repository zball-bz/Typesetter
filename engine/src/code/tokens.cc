#include "tokens.h"

namespace tsr {

int tokenTagFromCapture(std::string_view name) {
  size_t dot = name.find('.');
  std::string_view head = dot == std::string_view::npos ? name : name.substr(0, dot);
  for (int i = 0; i < kTokenTagCount; i++)
    if (head == kTokenTags[i]) return i;
  // common aliases seen across grammar queries
  if (head == "tag") return 5;                                   // type-ish
  if (head == "conditional" || head == "repeat" || head == "include")
    return 0;                                                    // keyword
  if (head == "boolean" || head == "constructor") return 6;      // constant
  if (head == "method") return 4;                                // function
  if (head == "field" || head == "parameter") return 10;         // property
  return -1;
}

bool validTokens(std::string_view body, const CodeToken* toks, size_t n) {
  auto boundary = [&](u32 at) { return at == body.size() || (at < body.size() && ((u8)body[at] & 0xC0) != 0x80); };
  u32 covered = 0;
  for (size_t i = 0; i < n; i++) {
    const CodeToken& t = toks[i];
    if (t.tag >= kTokenTagCount || t.start >= t.end || t.end > body.size() || t.start < covered ||
        !boundary(t.start) || !boundary(t.end))
      return false;
    covered = t.end;
  }
  return true;
}

void tokenLines(std::string_view body, StyleId base, const CodeToken* toks, size_t n, Interner& strs,
                StyleTable& styles, std::vector<std::vector<TokenRun>>& lines) {
  lines.clear();
  // one interned style per tag, created lazily
  StyleId tagStyle[kTokenTagCount];
  bool tagStyleMade[kTokenTagCount] = {false};
  auto styleFor = [&](u8 tag) {
    if (!tagStyleMade[tag]) {
      Styling s = styles.get(base);
      std::string var = std::string("var(--tsr-tok-") + kTokenTags[tag] + ")";
      s.color = strs.intern(var);
      if (tag == kTokenTagComment) {  // comment: italic (duplex contract), hanging at its content
        s.italic = true;
        s.hang = HANG_CONTENT;
      }
      tagStyle[tag] = styles.idOf(s);
      tagStyleMade[tag] = true;
    }
    return tagStyle[tag];
  };
  size_t ti = 0;
  size_t pos = 0;
  while (pos <= body.size()) {
    size_t eol = body.find('\n', pos);
    if (eol == std::string_view::npos) eol = body.size();
    std::vector<TokenRun>& line = lines.emplace_back();
    while (ti < n && toks[ti].end <= pos) ti++;
    size_t scan = ti;
    size_t cur = pos;
    while (cur < eol) {
      if (scan < n && toks[scan].start < eol && toks[scan].end > cur) {
        size_t ts = toks[scan].start > cur ? toks[scan].start : cur;
        size_t te = toks[scan].end < eol ? toks[scan].end : eol;
        if (ts > cur) line.push_back({body.substr(cur, ts - cur), base});
        line.push_back({body.substr(ts, te - ts), styleFor(toks[scan].tag), toks[scan].tag});
        cur = te;
        if (toks[scan].end <= eol) scan++;
        continue;
      }
      // no token covering cur on this line: plain up to the next one
      size_t stop = eol;
      if (scan < n && toks[scan].start < eol && toks[scan].start > cur) stop = toks[scan].start;
      line.push_back({body.substr(cur, stop - cur), base});
      cur = stop;
    }
    if (eol == body.size()) break;
    pos = eol + 1;
  }
}

}  // namespace tsr
