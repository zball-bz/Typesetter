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

void foldTokens(ContentNode* cb, const CodeToken* toks, size_t n,
                Arena& arena, Interner& strs, StyleTable& styles) {
  if (cb->kids.empty() || cb->kids[0]->kind != Kind::text) return;
  // a trailing sidecar group (and anything after the body text) survives
  std::vector<ContentNode*> tail(cb->kids.begin() + 1, cb->kids.end());
  const ContentNode* bodyNode = cb->kids[0];
  std::string_view body = strs.get(bodyNode->str);
  StyleId baseStyle = bodyNode->style;
  Span span = bodyNode->span;

  auto mkText = [&](std::string_view s, StyleId st) {
    ContentNode* t = arena.make<ContentNode>();
    t->kind = Kind::text;
    t->span = span;
    t->style = st;
    t->str = strs.intern(s);
    return t;
  };
  // one interned style per tag, created lazily
  StyleId tagStyle[kTokenTagCount];
  bool tagStyleMade[kTokenTagCount] = {false};
  auto styleFor = [&](u8 tag) {
    if (!tagStyleMade[tag]) {
      Styling s = styles.get(baseStyle);
      std::string var = std::string("var(--tsr-tok-") + kTokenTags[tag] + ")";
      s.color = strs.intern(var);
      if (tag == 3) s.bits |= CLS_EM;  // comment: italic (duplex contract)
      tagStyle[tag] = styles.idOf(s);
      tagStyleMade[tag] = true;
    }
    return tagStyle[tag];
  };

  std::vector<ContentNode*> lines;
  size_t ti = 0;
  size_t pos = 0;
  while (pos <= body.size()) {
    size_t eol = body.find('\n', pos);
    if (eol == std::string_view::npos) eol = body.size();
    ContentNode* line = arena.make<ContentNode>();
    line->kind = Kind::seq;
    line->span = span;
    line->style = cb->style;
    while (ti < n && toks[ti].end <= pos) ti++;
    size_t scan = ti;
    size_t cur = pos;
    while (cur < eol) {
      if (scan < n && toks[scan].start < eol && toks[scan].end > cur) {
        size_t ts = toks[scan].start > cur ? toks[scan].start : cur;
        size_t te = toks[scan].end < eol ? toks[scan].end : eol;
        if (ts > cur) line->kids.push_back(mkText(body.substr(cur, ts - cur), baseStyle));
        line->kids.push_back(mkText(body.substr(ts, te - ts), styleFor(toks[scan].tag)));
        cur = te;
        if (toks[scan].end <= eol) scan++;
        continue;
      }
      // no token covering cur on this line: emit plain up to the next one
      size_t stop = eol;
      if (scan < n && toks[scan].start < eol && toks[scan].start > cur)
        stop = toks[scan].start;
      line->kids.push_back(mkText(body.substr(cur, stop - cur), baseStyle));
      cur = stop;
    }
    lines.push_back(line);
    if (eol == body.size()) break;
    pos = eol + 1;
  }
  cb->kids.assign(lines.begin(), lines.end());
  cb->kids.insert(cb->kids.end(), tail.begin(), tail.end());
}

}  // namespace tsr
