// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
#include "settings.gen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../ops/domains.gen.h"
#include "../support/json.h"
#include "config.h"

namespace tsr {
namespace {

struct Row {
  const char* path;
  u32 affects;  // stageBit set
  bool group;   // the value is an object (map rows)
};
const Row kRows[] = {
    {"host.width", stageBit(Stage::Layout) | stageBit(Stage::Paint), false},
    {"host.dppx", stageBit(Stage::Measure), false},
    {"host.epsilonSu", stageBit(Stage::Emit) | stageBit(Stage::Measure), false},
    {"doc.lang", stageBit(Stage::Resolve) | stageBit(Stage::Paint), false},
    {"doc.baseSize", stageBit(Stage::BoxTree) | stageBit(Stage::Emit) | stageBit(Stage::Measure) | stageBit(Stage::Layout) | stageBit(Stage::Paint), false},
    {"doc.leading", stageBit(Stage::Emit) | stageBit(Stage::Layout) | stageBit(Stage::Paint), false},
    {"doc.parGap", stageBit(Stage::Layout) | stageBit(Stage::Paint), false},
    {"doc.cjkJustify", stageBit(Stage::Emit) | stageBit(Stage::Layout), false},
    {"doc.cjkGlue", stageBit(Stage::Emit), false},
    {"fonts.body", stageBit(Stage::Measure) | stageBit(Stage::Paint), false},
    {"fonts.cjk", stageBit(Stage::Measure) | stageBit(Stage::Paint), false},
    {"fonts.mono", stageBit(Stage::Measure) | stageBit(Stage::Paint), false},
    {"fonts.monoCjk", stageBit(Stage::Measure) | stageBit(Stage::Paint), false},
    {"par.indent", stageBit(Stage::BoxTree) | stageBit(Stage::Emit), false},
    {"list.indent", stageBit(Stage::BoxTree), false},
    {"quote.indent", stageBit(Stage::BoxTree), false},
    {"cjk.punctCompress", stageBit(Stage::Emit), false},
    {"break.hyphenPenalty", stageBit(Stage::Emit), false},
    {"break.urlPenalty", stageBit(Stage::Emit), false},
    {"break.urlMinLen", stageBit(Stage::Emit), false},
    {"break.mathRelAfter", stageBit(Stage::Emit), false},
    {"break.mathRelBefore", stageBit(Stage::Emit), false},
    {"break.mathBinAfter", stageBit(Stage::Emit), false},
    {"cost.exponent", stageBit(Stage::Layout), false},
    {"cost.shrinkThreshold", stageBit(Stage::Layout), false},
    {"cost.shrinkCoeff", stageBit(Stage::Layout), false},
    {"cost.cap", stageBit(Stage::Layout), false},
    {"code.scale", stageBit(Stage::BoxTree) | stageBit(Stage::Emit) | stageBit(Stage::Layout), false},
    {"code.contIndent", stageBit(Stage::Layout), false},
    {"code.sidecarFrac", stageBit(Stage::Layout), false},
    {"code.snapKerning", stageBit(Stage::Layout) | stageBit(Stage::Paint), false},
    {"code.fontFeatures", stageBit(Stage::Measure) | stageBit(Stage::Paint), false},
    {"code.fontFeaturesByLang", stageBit(Stage::Paint), true},
    {"terms.heading", stageBit(Stage::Resolve), false},
    {"terms.table", stageBit(Stage::Resolve), false},
    {"terms.figure", stageBit(Stage::Resolve), false},
    {"terms.equation", stageBit(Stage::Resolve), false},
    {"terms.captionSep", stageBit(Stage::Resolve), false},
    {"page.height", stageBit(Stage::Paginate), false},
};
constexpr u32 kRowCount = sizeof kRows / sizeof kRows[0];

bool type(std::string& why, const char* want) {
  why = std::string("expected ") + want;
  return false;
}
bool num(const JsonValue& v, double lo, double hi, bool integral, double& x, std::string& why) {
  if (v.t != JsonValue::T::Num || !std::isfinite(v.num)) return type(why, integral ? "an integer" : "a number");
  if (integral && v.num != std::floor(v.num)) return type(why, "an integer");
  if (v.num < lo || v.num > hi) {
    char b[96];
    std::snprintf(b, sizeof b, "a value in [%g, %g]", lo, hi);
    why = std::string("expected ") + b;
    return false;
  }
  x = v.num;
  return true;
}
int member(const JsonValue& v, const char* const* ms, int n, std::string& why) {
  if (v.t == JsonValue::T::Str)
    for (int k = 0; k < n; k++)
      if (v.str == ms[k]) return k;
  why = "expected one of";
  for (int k = 0; k < n; k++) why += std::string(k ? "|" : " ") + ms[k];
  return -1;
}

bool applyRow(Config& c, u32 row, const JsonValue& v, std::string& why) {
  switch (row) {
    case 0: {  // host.width
      double x;
      if (!num(v, 1, 100000, false, x, why)) return false;
      c.widthPx = x;
      return true;
    }
    case 1: {  // host.dppx
      double x;
      if (!num(v, 0.25, 16, false, x, why)) return false;
      c.dppx = x;
      return true;
    }
    case 2: {  // host.epsilonSu
      double x;
      if (!num(v, 0, 64, false, x, why)) return false;
      c.epsilonPerWordSu = x;
      return true;
    }
    case 3: {  // doc.lang
      if (v.t != JsonValue::T::Str || (!matchDomain(TextDomain::Lang, v.str))) return type(why, "lang");
      c.lang = v.str;
      return true;
    }
    case 4: {  // doc.baseSize
      double x;
      if (!num(v, 4, 96, false, x, why)) return false;
      c.baseSizePx = x;
      return true;
    }
    case 5: {  // doc.leading
      double x;
      if (!num(v, 0.5, 4, false, x, why)) return false;
      c.lineHeight = x;
      return true;
    }
    case 6: {  // doc.parGap
      double x;
      if (!num(v, 0, 10, false, x, why)) return false;
      c.paraSpacingEm = x;
      return true;
    }
    case 7: {  // doc.cjkJustify
      double x;
      if (!num(v, 0, 4, false, x, why)) return false;
      c.cjkJustifyK = x;
      return true;
    }
    case 8: {  // doc.cjkGlue
      double x;
      if (!num(v, 0, 1, false, x, why)) return false;
      c.cjkGlueEm = x;
      return true;
    }
    case 9: {  // fonts.body
      if (v.t != JsonValue::T::Str || (!matchDomain(TextDomain::Font, v.str))) return type(why, "font");
      c.bodyFont = v.str;
      return true;
    }
    case 10: {  // fonts.cjk
      if (v.t != JsonValue::T::Str || (!matchDomain(TextDomain::Font, v.str))) return type(why, "font");
      c.cjkFont = v.str;
      return true;
    }
    case 11: {  // fonts.mono
      if (v.t != JsonValue::T::Str || (!matchDomain(TextDomain::Font, v.str))) return type(why, "font");
      c.monoFont = v.str;
      return true;
    }
    case 12: {  // fonts.monoCjk
      if (v.t != JsonValue::T::Str || (!v.str.empty() && !matchDomain(TextDomain::Font, v.str))) return type(why, "font or empty");
      c.monoCjkFont = v.str;
      return true;
    }
    case 13: {  // par.indent
      double x;
      if (!num(v, 0, 20, false, x, why)) return false;
      c.paraIndentEm = x;
      return true;
    }
    case 14: {  // list.indent
      double x;
      if (!num(v, 0, 20, false, x, why)) return false;
      c.listIndentEm = x;
      return true;
    }
    case 15: {  // quote.indent
      double x;
      if (!num(v, 0, 20, false, x, why)) return false;
      c.quoteIndentEm = x;
      return true;
    }
    case 16: {  // cjk.punctCompress
      static const char* const kM[] = {"full", "book", "none"};
      int m = member(v, kM, 3, why);
      if (m < 0) return false;
      c.punctCompress = (PunctCompress)m;
      return true;
    }
    case 17: {  // break.hyphenPenalty
      double x;
      if (!num(v, 0, 1e18, false, x, why)) return false;
      c.hyphenPenalty = x;
      return true;
    }
    case 18: {  // break.urlPenalty
      double x;
      if (!num(v, 0, 1e18, false, x, why)) return false;
      c.urlBreakPenalty = x;
      return true;
    }
    case 19: {  // break.urlMinLen
      double x;
      if (!num(v, 1, 100000, true, x, why)) return false;
      c.urlBreakMinLen = (u32)x;
      return true;
    }
    case 20: {  // break.mathRelAfter
      double x;
      if (!num(v, 0, 1e18, false, x, why)) return false;
      c.mathRelAfterPenalty = x;
      return true;
    }
    case 21: {  // break.mathRelBefore
      double x;
      if (!num(v, 0, 1e18, false, x, why)) return false;
      c.mathRelBeforePenalty = x;
      return true;
    }
    case 22: {  // break.mathBinAfter
      double x;
      if (!num(v, 0, 1e18, false, x, why)) return false;
      c.mathBinAfterPenalty = x;
      return true;
    }
    case 23: {  // cost.exponent
      double x;
      if (!num(v, 1, 4, true, x, why)) return false;
      c.cost.exponent = (u8)x;
      return true;
    }
    case 24: {  // cost.shrinkThreshold
      double x;
      if (!num(v, 0, 1, false, x, why)) return false;
      c.cost.shrinkThreshold = x;
      return true;
    }
    case 25: {  // cost.shrinkCoeff
      double x;
      if (!num(v, 0, 100, false, x, why)) return false;
      c.cost.shrinkCoeff = x;
      return true;
    }
    case 26: {  // cost.cap
      double x;
      if (!num(v, 1, 1e12, false, x, why)) return false;
      c.cost.cap = x;
      return true;
    }
    case 27: {  // code.scale
      double x;
      if (!num(v, 0.1, 4, false, x, why)) return false;
      c.codeScale = x;
      return true;
    }
    case 28: {  // code.contIndent
      double x;
      if (!num(v, 0, 40, true, x, why)) return false;
      c.verbatimContIndent = (int)x;
      return true;
    }
    case 29: {  // code.sidecarFrac
      double x;
      if (!num(v, 0.1, 0.9, false, x, why)) return false;
      c.sidebarFrac = x;
      return true;
    }
    case 30: {  // code.snapKerning
      if (v.t != JsonValue::T::Bool) return type(why, "true or false");
      c.verbatimSnapKerning = v.b;
      return true;
    }
    case 31: {  // code.fontFeatures
      if (v.t != JsonValue::T::Str || (!matchDomain(TextDomain::Features, v.str))) return type(why, "features");
      c.codeFontFeatures = v.str;
      return true;
    }
    case 32: {  // code.fontFeaturesByLang
      if (v.t != JsonValue::T::Obj) return type(why, "an object of strings");
      std::map<std::string, std::string> mm;
      for (size_t mi = 0; mi < v.keys.size(); mi++) {
        const JsonValue& mv = v.vals[mi];
        if (mv.t != JsonValue::T::Str || !matchDomain(TextDomain::Features, mv.str)) return type(why, "features values");
        mm[v.keys[mi]] = mv.str;
      }
      c.codeFontFeaturesByLang = std::move(mm);
      return true;
    }
    case 33: {  // terms.heading
      if (v.t != JsonValue::T::Str || v.str.size() > 4096) return type(why, "a string");
      c.supHeading = v.str;
      return true;
    }
    case 34: {  // terms.table
      if (v.t != JsonValue::T::Str || v.str.size() > 4096) return type(why, "a string");
      c.supTable = v.str;
      return true;
    }
    case 35: {  // terms.figure
      if (v.t != JsonValue::T::Str || v.str.size() > 4096) return type(why, "a string");
      c.supFigure = v.str;
      return true;
    }
    case 36: {  // terms.equation
      if (v.t != JsonValue::T::Str || v.str.size() > 4096) return type(why, "a string");
      c.supEquation = v.str;
      return true;
    }
    case 37: {  // terms.captionSep
      if (v.t != JsonValue::T::Str || v.str.size() > 4096) return type(why, "a string");
      c.capSep = v.str;
      return true;
    }
    case 38: {  // page.height
      double x;
      if (!num(v, 16, 100000, false, x, why)) return false;
      c.pageHeightPx = x;
      return true;
    }
    default:
      return false;
  }
}

int rowOf(std::string_view path) {
  for (u32 k = 0; k < kRowCount; k++)
    if (path == kRows[k].path) return (int)k;
  return -1;
}
bool isPrefix(std::string_view path) {
  for (u32 k = 0; k < kRowCount; k++) {
    std::string_view p = kRows[k].path;
    if (p.size() > path.size() && p.substr(0, path.size()) == path && p[path.size()] == '.') return true;
  }
  return false;
}

struct Hit {
  u32 row;
  const JsonValue* v;
};
void collect(const JsonValue& o, const std::string& prefix, std::vector<Hit>& hits, DiagSink& diags) {
  for (size_t i = 0; i < o.keys.size(); i++) {
    const std::string& k = o.keys[i];
    const JsonValue& v = o.vals[i];
    if (!k.empty() && k[0] == '$') continue;  // $comment, $vocab: annotations
    std::string path = prefix.empty() ? k : prefix + "." + k;
    int r = rowOf(path);
    if (r >= 0) hits.push_back({(u32)r, &v});
    else if (v.t == JsonValue::T::Obj && isPrefix(path)) collect(v, path, hits, diags);
    else diags.add(Sev::Warning, "setting-unknown", {}, "unknown setting '" + path + "'");
  }
}

void num(std::string& out, double v) {
  char b[40];
  std::snprintf(b, sizeof b, "%.15g", v);
  out += b;
}

}  // namespace

SettingsPatch applySettings(Config& c, std::string_view json, DiagSink& diags) {
  SettingsPatch p;
  JsonValue doc;
  JsonReader rd;
  if (!rd.parse(json, doc)) {
    diags.add(Sev::Error, "setting-json", {}, std::string("settings: ") + rd.error() + " at byte " + std::to_string(rd.offset()));
    p.ok = false;
    return p;
  }
  if (doc.t != JsonValue::T::Obj) {
    diags.add(Sev::Error, "setting-json", {}, "settings: expected an object");
    p.ok = false;
    return p;
  }
  std::vector<Hit> hits;
  collect(doc, "", hits, diags);
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.row < b.row; });  // row order
  for (const Hit& h : hits) {
    std::string why;
    if (applyRow(c, h.row, *h.v, why)) {
      p.applied++;
      p.affects |= kRows[h.row].affects;
    } else {
      diags.add(Sev::Warning, "setting-type", {}, std::string(kRows[h.row].path) + ": " + why);
    }
  }
  return p;
}

std::string settingsJson(const Config& c) {
  std::string out = "{";
  out += "\"host\": {\"width\": ";
  num(out, (double)c.widthPx);
  out += ", \"dppx\": ";
  num(out, (double)c.dppx);
  out += ", \"epsilonSu\": ";
  num(out, (double)c.epsilonPerWordSu);
  out += "}, \"doc\": {\"lang\": ";
  jsonString(out, c.lang);
  out += ", \"baseSize\": ";
  num(out, (double)c.baseSizePx);
  out += ", \"leading\": ";
  num(out, (double)c.lineHeight);
  out += ", \"parGap\": ";
  num(out, (double)c.paraSpacingEm);
  out += ", \"cjkJustify\": ";
  num(out, (double)c.cjkJustifyK);
  out += ", \"cjkGlue\": ";
  num(out, (double)c.cjkGlueEm);
  out += "}, \"fonts\": {\"body\": ";
  jsonString(out, c.bodyFont);
  out += ", \"cjk\": ";
  jsonString(out, c.cjkFont);
  out += ", \"mono\": ";
  jsonString(out, c.monoFont);
  out += ", \"monoCjk\": ";
  jsonString(out, c.monoCjkFont);
  out += "}, \"par\": {\"indent\": ";
  num(out, (double)c.paraIndentEm);
  out += "}, \"list\": {\"indent\": ";
  num(out, (double)c.listIndentEm);
  out += "}, \"quote\": {\"indent\": ";
  num(out, (double)c.quoteIndentEm);
  out += "}, \"cjk\": {\"punctCompress\": ";
  { static const char* const kM[] = {"full", "book", "none"}; jsonString(out, kM[(int)c.punctCompress]); }
  out += "}, \"break\": {\"hyphenPenalty\": ";
  num(out, (double)c.hyphenPenalty);
  out += ", \"urlPenalty\": ";
  num(out, (double)c.urlBreakPenalty);
  out += ", \"urlMinLen\": ";
  num(out, (double)c.urlBreakMinLen);
  out += ", \"mathRelAfter\": ";
  num(out, (double)c.mathRelAfterPenalty);
  out += ", \"mathRelBefore\": ";
  num(out, (double)c.mathRelBeforePenalty);
  out += ", \"mathBinAfter\": ";
  num(out, (double)c.mathBinAfterPenalty);
  out += "}, \"cost\": {\"exponent\": ";
  num(out, (double)c.cost.exponent);
  out += ", \"shrinkThreshold\": ";
  num(out, (double)c.cost.shrinkThreshold);
  out += ", \"shrinkCoeff\": ";
  num(out, (double)c.cost.shrinkCoeff);
  out += ", \"cap\": ";
  num(out, (double)c.cost.cap);
  out += "}, \"code\": {\"scale\": ";
  num(out, (double)c.codeScale);
  out += ", \"contIndent\": ";
  num(out, (double)c.verbatimContIndent);
  out += ", \"sidecarFrac\": ";
  num(out, (double)c.sidebarFrac);
  out += ", \"snapKerning\": ";
  out += c.verbatimSnapKerning ? "true" : "false";
  out += ", \"fontFeatures\": ";
  jsonString(out, c.codeFontFeatures);
  out += ", \"fontFeaturesByLang\": ";
  out += '{';
  { bool first = true; for (const auto& [mk, mv] : c.codeFontFeaturesByLang) { if (!first) out += ", "; first = false; jsonString(out, mk); out += ": "; jsonString(out, mv); } }
  out += '}';
  out += "}, \"terms\": {\"heading\": ";
  jsonString(out, c.supHeading);
  out += ", \"table\": ";
  jsonString(out, c.supTable);
  out += ", \"figure\": ";
  jsonString(out, c.supFigure);
  out += ", \"equation\": ";
  jsonString(out, c.supEquation);
  out += ", \"captionSep\": ";
  jsonString(out, c.capSep);
  out += "}, \"page\": {\"height\": ";
  num(out, (double)c.pageHeightPx);
  out += "}}";
  return out;
}

}  // namespace tsr
