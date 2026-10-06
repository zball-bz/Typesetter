#include "box.h"

#include <cmath>
#include <cstdlib>

namespace tsr {

namespace {

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

// the root element's start tag, after leading blanks: "<svg …>" (its
// attribute text), or false
bool svgStartTag(std::string_view s, std::string_view& attrs) {
  size_t i = 0;
  while (i < s.size() && space(s[i])) i++;
  if (s.size() - i < 5 || s[i] != '<' || lower(s[i + 1]) != 's' || lower(s[i + 2]) != 'v' ||
      lower(s[i + 3]) != 'g' || !(space(s[i + 4]) || s[i + 4] == '>' || s[i + 4] == '/'))
    return false;
  const size_t a = i + 4;
  char q = 0;
  for (size_t k = a; k < s.size(); k++) {
    if (q) {
      if (s[k] == q) q = 0;
    } else if (s[k] == '"' || s[k] == '\'') {
      q = s[k];
    } else if (s[k] == '>') {
      attrs = s.substr(a, k - a);
      return true;
    }
  }
  return false;
}

// an attribute's value in a start tag's attribute text (names compare
// case-insensitively; viewBox is case-sensitive in SVG, but HTML parsers
// fold it back)
bool attrOf(std::string_view attrs, std::string_view name, std::string_view& value) {
  size_t i = 0;
  while (i < attrs.size()) {
    while (i < attrs.size() && (space(attrs[i]) || attrs[i] == '/')) i++;
    const size_t n0 = i;
    while (i < attrs.size() && !space(attrs[i]) && attrs[i] != '=' && attrs[i] != '/') i++;
    const std::string_view n = attrs.substr(n0, i - n0);
    while (i < attrs.size() && space(attrs[i])) i++;
    std::string_view v;
    if (i < attrs.size() && attrs[i] == '=') {
      i++;
      while (i < attrs.size() && space(attrs[i])) i++;
      if (i < attrs.size() && (attrs[i] == '"' || attrs[i] == '\'')) {
        const char q = attrs[i++];
        const size_t v0 = i;
        while (i < attrs.size() && attrs[i] != q) i++;
        v = attrs.substr(v0, i - v0);
        if (i < attrs.size()) i++;
      } else {
        const size_t v0 = i;
        while (i < attrs.size() && !space(attrs[i])) i++;
        v = attrs.substr(v0, i - v0);
      }
    }
    if (n.size() == name.size()) {
      bool eq = true;
      for (size_t k = 0; k < n.size() && eq; k++) eq = lower(n[k]) == lower(name[k]);
      if (eq) {
        value = v;
        return true;
      }
    }
    if (n.empty()) i++;
  }
  return false;
}

// a number at the front of s (strtod on a bounded copy); its end in `rest`
bool number(std::string_view s, double& v, std::string_view& rest) {
  size_t i = 0;
  while (i < s.size() && (space(s[i]) || s[i] == ',')) i++;
  char buf[64];
  size_t n = 0;
  while (i + n < s.size() && n < sizeof buf - 1 &&
         (std::string_view("0123456789+-.eE").find(s[i + n]) != std::string_view::npos))
    n++;
  if (!n) return false;
  for (size_t k = 0; k < n; k++) buf[k] = s[i + k];
  buf[n] = 0;
  char* end = nullptr;
  v = std::strtod(buf, &end);
  if (end == buf || !std::isfinite(v)) return false;
  rest = s.substr(i + (size_t)(end - buf));
  return true;
}

// a length in px (bare or "px"), or a percentage of `of`; other units: false
bool length(std::string_view s, double of, double& px) {
  std::string_view rest;
  if (!number(s, px, rest)) return false;
  while (!rest.empty() && space(rest.back())) rest.remove_suffix(1);
  if (rest.empty() || rest == "px") return px >= 0;
  if (rest == "%" && of > 0) {
    px = px * of / 100;
    return px >= 0;
  }
  return false;
}

}  // namespace

BoxKind boxKindOf(std::string_view markup) {
  std::string_view attrs;
  return svgStartTag(markup, attrs) ? BoxKind::Svg : BoxKind::Html;
}

bool svgBoxPx(std::string_view markup, double widthPx, double& h, double& baseline) {
  std::string_view attrs, v;
  if (!svgStartTag(markup, attrs)) return false;
  if (attrOf(attrs, "height", v) && length(v, 0, h) && h > 0) {
    baseline = h;
    return true;
  }
  double vb[4];
  std::string_view rest;
  if (!attrOf(attrs, "viewbox", v)) return false;
  for (double& x : vb) {
    if (!number(v, x, rest)) return false;
    v = rest;
  }
  if (!(vb[2] > 0 && vb[3] > 0)) return false;
  double w = widthPx;
  if (attrOf(attrs, "width", v) && !length(v, widthPx, w)) return false;  // em, cm, …: the host's
  if (!(w > 0)) return false;
  h = baseline = w * vb[3] / vb[2];
  return true;
}

}  // namespace tsr
