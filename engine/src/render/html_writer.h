// The one place HTML start tags are assembled (plan P0-10; design T7 S2).
//
// - Attribute names are an allowlist checked at COMPILE time (AttrName is
//   consteval): an unlisted name does not build.
// - Attributes stream into the output in call order; nothing is buffered.
// - Style declarations accumulate into ONE style attribute, placed where the
//   first declaration was added (a second style="" used to be appended by
//   hand: snap-kerning's letter-spacing was dropped by every browser).
// - A repeated attribute is a serializer defect: debug builds assert; release
//   keeps the first value and counts it, and the Doc turns the count into a
//   "render-attr" diagnostic.
// - Attribute values are escaped here (attr), except values the caller marks
//   safe (attrSafe: numbers and fixed literals). Style values were validated
//   at decode (P0-06); declEsc escapes the ones that carry text.
// - Element ids are spelled by AnchorNamer only.
#pragma once
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

namespace tsr {

inline void escapeHtml(std::string& out, std::string_view s) {
  size_t from = 0;
  for (size_t i = 0; i < s.size(); i++) {
    const char* rep;
    switch (s[i]) {
      case '&': rep = "&amp;"; break;
      case '<': rep = "&lt;"; break;
      case '>': rep = "&gt;"; break;
      case '"': rep = "&quot;"; break;
      case '\'': rep = "&#39;"; break;
      default: continue;
    }
    out.append(s.data() + from, i - from);
    out += rep;
    from = i + 1;
  }
  out.append(s.data() + from, s.size() - from);
}

// Stable px formatting: up to 3 decimals, trailing zeros trimmed — exactly
// printf's "%.3f" (correctly rounded, ties to even; "-0" for a negative that
// rounds to zero), computed in integers: px = m·2^e, so px·1000 = (m·1000)·2^e
// with m·1000 < 2^63. The paint path formats every position and width; the
// printf machinery was ~1.5 ms of an 87K document update in WASM.
inline size_t fmtPxBuf(char (&buf)[48], double px) {
  u64 bits;
  std::memcpy(&bits, &px, 8);
  const bool neg = bits >> 63;
  const int bexp = (int)((bits >> 52) & 0x7FF);
  const u64 mant = bits & ((1ull << 52) - 1);
  size_t len = 0;
  if (bexp == 0x7FF || bexp >= 1075) {  // inf, nan, or |px| >= 2^52: printf's own
    int n = std::snprintf(buf, sizeof buf - 2, "%.3f", px);
    len = n > 0 ? (size_t)n : 0;
    if (len > sizeof buf - 3) len = sizeof buf - 3;
    while (len > 0 && buf[len - 1] == '0') len--;
    if (len > 0 && buf[len - 1] == '.') len--;
  } else {
    const u64 m = bexp ? (mant | (1ull << 52)) : mant;
    const int shift = 1075 - (bexp ? bexp : 1);  // px = m / 2^shift
    const u64 n = m * 1000;
    u64 q = 0;
    if (shift < 64) {
      q = n >> shift;
      const u64 r = n & ((1ull << shift) - 1), half = 1ull << (shift - 1);
      if (r > half || (r == half && (q & 1))) q++;
    }
    if (neg) buf[len++] = '-';
    char digits[24];
    int nd = 0;
    u64 ip = q / 1000;
    do {
      digits[nd++] = (char)('0' + ip % 10);
      ip /= 10;
    } while (ip);
    while (nd) buf[len++] = digits[--nd];
    u32 frac = (u32)(q % 1000);
    if (frac) {
      buf[len++] = '.';
      buf[len++] = (char)('0' + frac / 100);
      frac %= 100;
      if (frac) {
        buf[len++] = (char)('0' + frac / 10);
        frac %= 10;
        if (frac) buf[len++] = (char)('0' + frac);
      }
    }
  }
  buf[len++] = 'p';
  buf[len++] = 'x';
  return len;
}
inline void fmtPx(std::string& out, double px) {
  char buf[48];
  out.append(buf, fmtPxBuf(buf, px));
}
inline std::string pxStr(double px) {
  std::string s;
  fmtPx(s, px);
  return s;
}

// AnchorNamer (D-S06): prefix + label, escaped for an attribute.
// (plan P3-06) the prefix is the document's setting render.idPrefix: a
// render sets it for its duration (AnchorScope; per thread, one render at a
// time, as WriterDefects) — a print root or a second article uses its own;
// `suppress`: no ids at all (a preview fragment)
struct AnchorNamer {
  static constexpr std::string_view kPrefix = kAnchorPrefix;
  struct Current {
    std::string prefix{kAnchorPrefix};
    bool suppress = false;
  };
  static Current& current() {
    static thread_local Current c;
    return c;
  }
  static void id(std::string& out, std::string_view label) {
    out += current().prefix;
    escapeHtml(out, label);
  }
  // (plan P3-04) a reference to a label's anchor: "#" + its id, unescaped
  // (the attribute writer escapes)
  static std::string href(std::string_view label) {
    std::string h = "#";
    h += current().prefix;
    h += label;
    return h;
  }
};
// a render's anchor spelling, restored when it ends
struct AnchorScope {
  AnchorNamer::Current saved;
  explicit AnchorScope(std::string_view prefix, bool suppress = false) : saved(AnchorNamer::current()) {
    AnchorNamer::current().prefix = std::string(prefix);
    AnchorNamer::current().suppress = suppress;
  }
  ~AnchorScope() { AnchorNamer::current() = saved; }
  AnchorScope(const AnchorScope&) = delete;
  AnchorScope& operator=(const AnchorScope&) = delete;
};

// The attribute allowlist — the whole DOM vocabulary both serializers emit
// besides style (document-model §9).
inline constexpr std::string_view kHtmlAttrs[] = {
    "alt",       "class",     "draggable", "href",    "id",       "lang",      "src",
    "start",     "title",     "data-s",    "data-e",  "data-syn", "data-join", "data-ragged",
    "data-cell", "data-snap", "data-pid",  "data-s0", "data-src", "data-role",
    "data-overfull", "data-tsr-env",
    // (plan P3-07) the copy contract: replaced text and its group, a line's
    // track (data-cell retired), a paged band's block
    "data-copy", "data-copy-group", "data-track", "data-b",
    // (plan P3-14) a table cell's spans
    "colspan", "rowspan",
    // (plan P3-23) a presentation row's ARIA role; (plan P3-27) a formula's
    // accessible name (its source); (plan P3-29) a formula's later rows,
    // hidden (its first is named)
    "role", "aria-label", "aria-hidden",
};
static_assert(std::size(kHtmlAttrs) <= 32);
constexpr int htmlAttrIndex(std::string_view name) {
  for (size_t i = 0; i < std::size(kHtmlAttrs); i++)
    if (kHtmlAttrs[i] == name) return (int)i;
  return -1;
}

// not constexpr: reaching it during constant evaluation is a compile error
void attributeNotInAllowlist();

struct AttrName {
  std::string_view name;
  uint32_t bit;
  consteval AttrName(const char* s) : name(s), bit(0) {
    int i = htmlAttrIndex(name);
    if (i < 0) attributeNotInAllowlist();
    bit = 1u << i;
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
  Tag(std::string& out, std::string_view name) : out_(out), name_(name) {
    out_ += '<';
    out_ += name;
  }
  Tag(const Tag&) = delete;
  Tag& operator=(const Tag&) = delete;

  // a text value: escaped
  Tag& attr(AttrName n, std::string_view value) {
    if (begin(n)) {
      escapeHtml(out_, value);
      out_ += '"';
    }
    return *this;
  }
  // a value that cannot contain markup (numbers, fixed literals)
  Tag& attrSafe(AttrName n, std::string_view value) {
    if (begin(n)) {
      out_ += value;
      out_ += '"';
    }
    return *this;
  }
  Tag& num(AttrName n, unsigned long long v) {
    if (begin(n)) {
      char buf[24];
      auto r = std::to_chars(buf, buf + sizeof buf, v);
      out_.append(buf, (size_t)(r.ptr - buf));
      out_ += '"';
    }
    return *this;
  }
  // an element id, spelled by AnchorNamer (none in a preview fragment)
  Tag& id(std::string_view label) {
    if (AnchorNamer::current().suppress) return *this;
    if (begin("id")) {
      AnchorNamer::id(out_, label);
      out_ += '"';
    }
    return *this;
  }

  // style declarations "prop:value;prop:value" (validated literals)
  Tag& style(std::string_view decls) {
    if (decls.empty()) return *this;
    char* p = styleAt(decls.size());
    std::memcpy(p, decls.data(), decls.size());
    return *this;
  }
  // one declaration with a px value
  Tag& px(std::string_view prop, double v) {
    char buf[48];
    size_t n = fmtPxBuf(buf, v);
    return decl(prop, std::string_view(buf, n));
  }
  // one declaration, value appended as is (validated at decode)
  Tag& decl(std::string_view prop, std::string_view value) {
    char* p = styleAt(prop.size() + 1 + value.size());
    std::memcpy(p, prop.data(), prop.size());
    p[prop.size()] = ':';
    std::memcpy(p + prop.size() + 1, value.data(), value.size());
    return *this;
  }
  // one declaration whose value carries text (font names): attribute-escaped
  Tag& declEsc(std::string_view prop, std::string_view value) {
    if (value.find_first_of("&<>\"'") == std::string_view::npos) return decl(prop, value);
    std::string v;
    escapeHtml(v, value);
    return decl(prop, v);
  }

  // writes the '>' that ends the start tag
  void open() {
    closeStyle();
    out_ += '>';
  }

 private:
  // writes ` name="`; false (and nothing written) on a repeated attribute
  bool begin(AttrName n) {
    if (seen_ & n.bit) {
      assert(false && "repeated attribute");
      WriterDefects& d = writerDefects();
      if (d.count++ == 0) d.first = "<" + std::string(name_) + "> " + std::string(n.name);
      return false;  // first wins
    }
    seen_ |= n.bit;
    closeStyle();
    out_ += ' ';
    out_ += n.name;
    out_ += "=\"";
    return true;
  }
  void closeStyle() {
    if (styleOpen_) {
      out_ += '"';
      styleOpen_ = false;
    }
  }
  // room for one more declaration (with its ';' separator) inside the single
  // style attribute; returns where to write it
  char* styleAt(size_t len) {
    if (styleEnd_ == std::string::npos) {  // first declaration: open style="
      closeStyle();
      out_ += " style=\"";
      styleOpen_ = true;
      size_t at = out_.size();
      out_.resize(at + len);
      styleEnd_ = out_.size();
      return &out_[at];
    }
    if (styleOpen_) {  // style is still the last attribute: append
      size_t at = out_.size();
      out_.resize(at + 1 + len);
      out_[at] = ';';
      styleEnd_ = out_.size();
      return &out_[at + 1];
    }
    // attributes followed the style: insert before its closing quote
    size_t at = styleEnd_;
    out_.insert(at, 1 + len, ';');
    styleEnd_ = at + 1 + len;
    return &out_[at + 1];
  }

  std::string& out_;
  std::string_view name_;
  uint32_t seen_ = 0;
  size_t styleEnd_ = std::string::npos;  // end of the style value in out_
  bool styleOpen_ = false;               // style value written last, quote not closed yet
};

}  // namespace tsr
