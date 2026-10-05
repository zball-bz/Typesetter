// Output contract checks for the golden runner (remediation plan P0-01;
// document-model §9). Every html / semantic / paged output is scanned:
//
//   attr-dup        no element repeats an attribute name
//   id-unique       every id value occurs once per output
//   anchor-closure  every internal href="#x" resolves to exactly one id="x"
//   allowlist       only allowlisted elements appear (trusted raw content,
//                   <div class="tsr-raw">…</div> and the inline form
//                   <span class="tsr-iraw">…</span>, is skipped)
//   line-spans      every non-synthetic .tsr-line carries data-s and data-e
//                   (typeset/paged outputs only)
//
// Known failures are listed in test/golden/XFAIL as `<fixture>:<output>:<check>`.
// A listed check that passes is an XPASS and fails the run, so the list can
// only shrink.
#pragma once
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace contract {

struct Tag {
  std::string name;  // lower-case; empty for text
  bool closing = false;
  bool selfClose = false;
  std::vector<std::pair<std::string, std::string>> attrs;
};

inline bool isNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == ':' || c == '.';
}

// Tokenizes engine output (well-formed by construction; this is a checker,
// not a general HTML parser). Text and comments are skipped.
inline std::vector<Tag> tags(std::string_view h) {
  std::vector<Tag> out;
  size_t i = 0;
  while (i < h.size()) {
    if (h[i] != '<') { i++; continue; }
    if (h.compare(i, 4, "<!--") == 0) {
      size_t e = h.find("-->", i + 4);
      i = e == std::string_view::npos ? h.size() : e + 3;
      continue;
    }
    Tag t;
    size_t j = i + 1;
    if (j < h.size() && h[j] == '/') { t.closing = true; j++; }
    size_t ns = j;
    while (j < h.size() && isNameChar(h[j])) j++;
    if (j == ns) { i++; continue; }  // a literal '<' in text
    for (size_t k = ns; k < j; k++) t.name += (char)std::tolower((unsigned char)h[k]);
    for (;;) {
      while (j < h.size() && (h[j] == ' ' || h[j] == '\n' || h[j] == '\t')) j++;
      if (j >= h.size()) break;
      if (h[j] == '>') { j++; break; }
      if (h[j] == '/' && j + 1 < h.size() && h[j + 1] == '>') { t.selfClose = true; j += 2; break; }
      size_t as = j;
      while (j < h.size() && h[j] != '=' && h[j] != '>' && h[j] != ' ' && h[j] != '/') j++;
      std::string an(h.substr(as, j - as));
      std::string av;
      if (j < h.size() && h[j] == '=') {
        j++;
        if (j < h.size() && (h[j] == '"' || h[j] == '\'')) {
          char q = h[j++];
          size_t vs = j;
          while (j < h.size() && h[j] != q) j++;
          av = std::string(h.substr(vs, j - vs));
          if (j < h.size()) j++;
        } else {
          size_t vs = j;
          while (j < h.size() && h[j] != ' ' && h[j] != '>') j++;
          av = std::string(h.substr(vs, j - vs));
        }
      }
      if (an.empty()) { j++; continue; }
      t.attrs.emplace_back(an, av);
    }
    out.push_back(std::move(t));
    i = j;
  }
  return out;
}

inline const std::string* attr(const Tag& t, std::string_view n) {
  for (auto& [k, v] : t.attrs)
    if (k == n) return &v;
  return nullptr;
}

inline bool hasClass(const Tag& t, std::string_view c) {
  const std::string* cl = attr(t, "class");
  if (!cl) return false;
  size_t p = 0;
  while (p <= cl->size()) {
    size_t e = cl->find(' ', p);
    if (e == std::string::npos) e = cl->size();
    if (std::string_view(*cl).substr(p, e - p) == c) return true;
    p = e + 1;
  }
  return false;
}

// Elements the engine may emit outside trusted raw content.
inline const std::set<std::string>& allowlist() {
  static const std::set<std::string> a = {
      "div", "span", "a", "p", "br", "hr", "img", "sup", "sub", "strong", "em", "b", "i",
      "u", "s", "del", "ins", "mark", "small", "code", "kbd", "pre", "blockquote", "q",
      "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "dl", "dt", "dd", "table",
      "thead", "tbody", "tfoot", "tr", "td", "th", "caption", "col", "colgroup", "figure",
      "figcaption", "nav", "section", "aside", "header", "footer", "details", "summary",
      "math", "svg"};
  return a;
}

// Returns the names of the failing checks, each with a short reason.
inline std::map<std::string, std::string> check(std::string_view html, bool typeset) {
  std::map<std::string, std::string> fail;
  auto note = [&](const char* c, const std::string& why) {
    if (!fail.count(c)) fail[c] = why;
  };
  std::vector<Tag> ts = tags(html);
  std::map<std::string, int> ids;
  std::vector<std::string> hrefs;
  int rawDepth = 0;  // >0 while inside trusted raw content
  std::string rawTag;  // its container: div.tsr-raw or span.tsr-iraw
  for (const Tag& t : ts) {
    if (rawDepth > 0) {
      if (t.name == rawTag) rawDepth += t.closing ? -1 : (t.selfClose ? 0 : 1);
      continue;
    }
    if (t.closing) continue;
    if (((t.name == "div" && hasClass(t, "tsr-raw")) || (t.name == "span" && hasClass(t, "tsr-iraw"))) &&
        !t.selfClose) {
      rawDepth = 1;
      rawTag = t.name;
    }
    std::set<std::string> seen;
    for (auto& [k, v] : t.attrs)
      if (!seen.insert(k).second) note("attr-dup", "<" + t.name + "> repeats " + k);
    if (const std::string* id = attr(t, "id")) ids[*id]++;
    if (const std::string* h = attr(t, "href"))
      if (!h->empty() && (*h)[0] == '#') hrefs.push_back(h->substr(1));
    if (!allowlist().count(t.name)) note("allowlist", "<" + t.name + ">");
    if (typeset && t.name == "div" && hasClass(t, "tsr-line") && !attr(t, "data-syn") &&
        (!attr(t, "data-s") || !attr(t, "data-e")))
      note("line-spans", "tsr-line without data-s/data-e");
  }
  for (auto& [id, n] : ids)
    if (n > 1) note("id-unique", "id \"" + id + "\" x" + std::to_string(n));
  for (const std::string& h : hrefs) {
    auto it = ids.find(h);
    if (it == ids.end()) note("anchor-closure", "href #" + h + " has no target");
  }
  return fail;
}

}  // namespace contract
