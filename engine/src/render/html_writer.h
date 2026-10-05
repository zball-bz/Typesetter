// The one place HTML start tags are assembled (plan P0-10; design T7 S2).
//
// - Attributes are buffered and written in insertion order.
// - Style declarations accumulate into ONE style attribute, placed where the
//   first declaration was added (a second style="" used to be appended by
//   hand: snap-kerning's letter-spacing was dropped by every browser).
// - Attribute names come from an allowlist (kAttrs + data-*). A repeated or
//   unlisted attribute is a serializer defect: debug builds assert; release
//   keeps the first value / drops the attribute and counts it, and the Doc
//   turns the count into a "render-attr" diagnostic.
// - Attribute values are escaped here (attr), except values the caller marks
//   safe (attrSafe: numbers and fixed literals) and style declarations, whose
//   values were validated at decode (P0-06) and escaped where they carry text.
// - Element ids are spelled by AnchorNamer only.
#pragma once
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../support/support.h"

namespace tsr {

inline void escapeHtml(std::string& out, std::string_view s) {
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
}

// Stable px formatting: up to 3 decimals, trailing zeros trimmed.
inline void fmtPx(std::string& out, double px) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3f", px);
  size_t len = std::strlen(buf);
  while (len > 0 && buf[len - 1] == '0') len--;
  if (len > 0 && buf[len - 1] == '.') len--;
  out.append(buf, len);
  out += "px";
}
inline std::string pxStr(double px) {
  std::string s;
  fmtPx(s, px);
  return s;
}

// AnchorNamer (D-S06): prefix + label, escaped for an attribute. The prefix is
// fixed until render.idPrefix (plan P3-06).
struct AnchorNamer {
  static constexpr std::string_view kPrefix = "tsr-";
  static void id(std::string& out, std::string_view label) {
    out += kPrefix;
    escapeHtml(out, label);
  }
};

// serializer defects since the last reset (per thread: one render at a time)
struct WriterDefects {
  unsigned count = 0;
  std::string first;  // "<tag> attr" of the first defect
};
inline WriterDefects& writerDefects() {
  static thread_local WriterDefects d;
  return d;
}

class Tag {
 public:
  // attribute names put() accepts; style is written by style() alone
  static bool allowed(std::string_view name) {
    static constexpr std::string_view kAttrs[] = {
        "alt", "class", "draggable", "href", "id", "lang", "src", "start", "title"};
    if (name.starts_with("data-")) return name.size() > 5;
    for (std::string_view a : kAttrs)
      if (a == name) return true;
    return false;
  }

  Tag(std::string& out, std::string_view name) : out_(out), name_(name) {}
  Tag(const Tag&) = delete;
  Tag& operator=(const Tag&) = delete;

  // a text value: escaped
  Tag& attr(std::string_view name, std::string_view value) {
    std::string v;
    escapeHtml(v, value);
    return put(name, std::move(v));
  }
  // a value that cannot contain markup (numbers, fixed literals)
  Tag& attrSafe(std::string_view name, std::string value) { return put(name, std::move(value)); }
  Tag& num(std::string_view name, unsigned long long v) { return put(name, std::to_string(v)); }
  // an element id, spelled by AnchorNamer
  Tag& id(std::string_view label) {
    std::string v;
    AnchorNamer::id(v, label);
    return put("id", std::move(v));
  }
  // style declarations "prop:value;prop:value" (validated, escaped by caller)
  Tag& style(std::string_view decls) {
    if (decls.empty()) return *this;
    if (styleAt_ < 0) {
      styleAt_ = (int)attrs_.size();
      attrs_.push_back({"style", std::string()});
    }
    std::string& s = attrs_[(size_t)styleAt_].second;
    if (!s.empty()) s += ';';
    s += decls;
    return *this;
  }
  bool has(std::string_view name) const {
    for (const auto& a : attrs_)
      if (a.first == name) return true;
    return false;
  }
  // writes "<name a=\"…\" …>"
  void open() {
    out_ += '<';
    out_ += name_;
    for (const auto& [n, v] : attrs_) {
      out_ += ' ';
      out_ += n;
      out_ += "=\"";
      out_ += v;
      out_ += '"';
    }
    out_ += '>';
  }

 private:
  Tag& put(std::string_view name, std::string value) {
    bool ok = allowed(name);  // style goes through style() only
    assert(ok && "attribute not in the writer allowlist");
    for (const auto& a : attrs_)
      if (a.first == name) ok = false;
    assert(ok && "repeated attribute");
    if (!ok) {  // first wins; an unlisted attribute is dropped
      WriterDefects& d = writerDefects();
      if (d.count++ == 0) {
        d.first = "<" + std::string(name_) + "> " + std::string(name);
      }
      return *this;
    }
    attrs_.push_back({std::string(name), std::move(value)});
    return *this;
  }

  std::string& out_;
  std::string_view name_;
  std::vector<std::pair<std::string, std::string>> attrs_;
  int styleAt_ = -1;
};

}  // namespace tsr
