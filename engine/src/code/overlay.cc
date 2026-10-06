#include "overlay.h"

#include "languages.gen.h"

namespace tsr {

u32 overlayMask(std::string_view names, std::vector<std::string_view>* unknown) {
  u32 mask = 0;
  size_t i = 0;
  while (i < names.size()) {
    while (i < names.size() && names[i] == ' ') i++;
    size_t j = i;
    while (j < names.size() && names[j] != ' ') j++;
    if (j > i) {
      const std::string_view name = names.substr(i, j - i);
      bool found = false;
      for (u32 k = 0; k < kOverlayCount; k++)
        if (kOverlays[k].name == name) {
          mask |= 1u << k;
          found = true;
        }
      if (!found && unknown) unknown->push_back(name);
    }
    i = j;
  }
  return mask;
}

std::string overlayNames(u32 mask) {
  std::string out;
  for (u32 k = 0; k < kOverlayCount; k++)
    if (mask & (1u << k)) {
      if (!out.empty()) out += ' ';
      out += kOverlays[k].name;
    }
  return out;
}

std::string_view languageOfTag(std::string_view tag) {
  for (const LangAlias& a : kLangAliases)
    if (a.tag == tag) return a.lang;
  return {};
}

namespace {
// the end of an overlay's span that starts at `at`, else 0
size_t spanAt(const OverlaySpec& o, std::string_view body, size_t at) {
  if (body.substr(at, o.open.size()) != o.open) return 0;
  size_t p = at + o.open.size();
  const size_t name = p;
  while (p < body.size() && body.substr(p, o.close.size()) != o.close) {
    if (o.forbid.find(body[p]) != std::string_view::npos) return 0;
    p++;
  }
  if (p == name || p >= body.size()) return 0;  // an empty name, or no close
  p += o.close.size();
  for (u8 s = 0; s < o.nSuffix; s++)
    if (body.substr(p, o.suffix[s].size()) == o.suffix[s]) return p + o.suffix[s].size();
  return p;
}
}  // namespace

std::vector<CodeToken> overlaySpans(std::string_view body, u32 mask) {
  std::vector<CodeToken> out;
  if (!mask) return out;
  size_t at = 0;
  while (at < body.size()) {
    size_t end = 0;
    int tag = -1;
    for (u32 k = 0; k < kOverlayCount && !end; k++)
      if (mask & (1u << k)) {
        end = spanAt(kOverlays[k], body, at);
        tag = kOverlays[k].tag;
      }
    if (end) {
      out.push_back({(u32)at, (u32)end, (u8)tag});
      at = end;
    } else {
      at++;
    }
  }
  return out;
}

std::string maskSpans(std::string_view body, const std::vector<CodeToken>& spans) {
  std::string out(body);
  for (const CodeToken& s : spans)
    for (u32 i = s.start; i < s.end; i++) out[i] = ' ';
  return out;
}

std::vector<CodeToken> mergeSpans(const std::vector<CodeToken>& runs, const std::vector<CodeToken>& spans) {
  if (spans.empty()) return runs;
  std::vector<CodeToken> out;
  out.reserve(runs.size() + spans.size());
  size_t si = 0;
  for (const CodeToken& r : runs) {
    u32 cur = r.start;
    // the spans before this run's end: each cuts it and goes in its place
    while (cur < r.end) {
      while (si < spans.size() && spans[si].end <= cur) {
        out.push_back(spans[si]);
        si++;
      }
      if (si == spans.size() || spans[si].start >= r.end) {
        out.push_back({cur, r.end, r.tag});
        break;
      }
      if (spans[si].start > cur) out.push_back({cur, spans[si].start, r.tag});
      cur = spans[si].end;  // (the span itself is pushed by the loop above)
    }
  }
  for (; si < spans.size(); si++) out.push_back(spans[si]);
  return out;
}

}  // namespace tsr
