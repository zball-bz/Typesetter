#include "locale.h"

#include <algorithm>

#include "../ops/ops.h"
#include "../support/json.h"
#include "semantic_data.gen.h"

namespace tsr {

namespace {

struct LocaleTable {
  std::unordered_map<std::string, std::string> likely, parents;  // keys and values canonical
  std::vector<u32> hans, hant;                                    // the detection characters, sorted
};

std::string canonical(std::string_view tag) {
  // lang lower, Script title, REGION upper, separated by '-'
  std::string out;
  size_t part = 0;
  for (size_t i = 0; i <= tag.size(); part++) {
    size_t j = i;
    while (j < tag.size() && tag[j] != '-' && tag[j] != '_') j++;
    std::string p(tag.substr(i, j - i));
    for (size_t k = 0; k < p.size(); k++) {
      char& c = p[k];
      const bool upper = part > 0 && (p.size() == 2 || (p.size() == 4 && k == 0));
      if (upper && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
      if (!upper && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    if (!out.empty()) out += '-';
    out += p;
    i = j + 1;
    if (j >= tag.size()) break;
  }
  return out;
}

std::vector<u32> codepoints(std::string_view s) {
  std::vector<u32> out;
  for (u32 i = 0; i < s.size();) out.push_back(utf8Next(s, i));
  std::sort(out.begin(), out.end());
  return out;
}

const LocaleTable& table() {
  static const LocaleTable t = [] {
    LocaleTable out;
    JsonValue v;
    JsonReader rd;
    if (!rd.parse(kLocaleJson, v)) __builtin_trap();  // checked by the native tests
    if (const JsonValue* l = v.get("likely"))
      for (size_t k = 0; k < l->keys.size(); k++) out.likely[canonical(l->keys[k])] = canonical(l->vals[k].str);
    if (const JsonValue* p = v.get("parents"))
      for (size_t k = 0; k < p->keys.size(); k++) out.parents[canonical(p->keys[k])] = p->vals[k].str;
    if (const JsonValue* d = v.get("detect")) {
      if (const JsonValue* s = d->get("hans")) out.hans = codepoints(s->str);
      if (const JsonValue* s = d->get("hant")) out.hant = codepoints(s->str);
    }
    return out;
  }();
  return t;
}

struct Subtags {
  std::string lang, script, region;
};
Subtags split(const std::string& tag) {
  Subtags s;
  size_t i = 0, part = 0;
  while (i <= tag.size()) {
    size_t j = tag.find('-', i);
    if (j == std::string::npos) j = tag.size();
    const std::string p = tag.substr(i, j - i);
    if (part == 0) s.lang = p;
    else if (p.size() == 4 && s.script.empty() && s.region.empty()) s.script = p;
    else if ((p.size() == 2 || p.size() == 3) && s.region.empty()) s.region = p;
    part++;
    i = j + 1;
  }
  return s;
}

}  // namespace

const std::vector<LocalePack>& builtinLocalePacks() {
  static const std::vector<LocalePack> packs = [] {
    std::vector<LocalePack> out;
    for (const LocaleSource& src : kLocalePacks) {
      JsonValue v;
      JsonReader rd;
      if (!rd.parse(src.json, v)) __builtin_trap();  // checked by the native tests
      LocalePack p;
      p.lang = std::string(src.lang);
      if (const JsonValue* t = v.get("terms"))
        for (size_t k = 0; k < t->keys.size(); k++) p.terms[t->keys[k]] = t->vals[k].str;
      if (const JsonValue* h = v.get("hyphenate"); h && h->t == JsonValue::T::Bool) p.hyphenate = h->b ? 1 : 0;
      out.push_back(std::move(p));
    }
    return out;
  }();
  return packs;
}

std::string maximizeLocale(std::string_view tagIn) {
  const LocaleTable& t = table();
  const std::string tag = canonical(tagIn);
  if (auto it = t.likely.find(tag); it != t.likely.end()) return it->second;
  Subtags s = split(tag);
  // CLDR's lookup order: lang-REGION, lang-Script, lang, und-Script
  auto fill = [&](const std::string& key) {
    auto it = t.likely.find(key);
    if (it == t.likely.end()) return false;
    const Subtags m = split(it->second);
    if (s.lang.empty() || s.lang == "und") s.lang = m.lang;
    if (s.script.empty()) s.script = m.script;
    if (s.region.empty()) s.region = m.region;
    return true;
  };
  (!s.region.empty() && fill(s.lang + "-" + s.region)) || (!s.script.empty() && fill(s.lang + "-" + s.script)) ||
      fill(s.lang) || (!s.script.empty() && fill("und-" + s.script));
  std::string out = s.lang;
  if (!s.script.empty()) out += "-" + s.script;
  if (!s.region.empty()) out += "-" + s.region;
  return out;
}

std::vector<std::string> localeChain(std::string_view tag) {
  const LocaleTable& t = table();
  std::vector<std::string> out;
  std::string cur = maximizeLocale(tag);
  for (int guard = 0; guard < 8 && !cur.empty() && cur != "root"; guard++) {
    out.push_back(cur);
    if (auto it = t.parents.find(cur); it != t.parents.end()) {
      cur = it->second;
      continue;
    }
    const Subtags s = split(cur);  // lang-Script-REGION → lang-Script → lang
    if (!s.region.empty()) cur = s.script.empty() ? s.lang : s.lang + "-" + s.script;
    else if (!s.script.empty()) cur = s.lang;
    else cur = "root";
  }
  out.push_back("root");
  return out;
}

std::string localePackFor(std::string_view lang) {
  for (const std::string& name : localeChain(lang))
    for (const LocalePack& p : builtinLocalePacks())
      if (p.lang == name) return p.lang;
  return "en";  // the root
}

bool localeHyphenates(std::string_view lang) {
  for (const std::string& name : localeChain(lang))
    for (const LocalePack& p : builtinLocalePacks())
      if (p.lang == (name == "root" ? "en" : name) && p.hyphenate >= 0) return p.hyphenate == 1;
  return true;
}

std::string detectDocumentLang(const RawOps& raw) {
  const LocaleTable& t = table();
  u64 kana = 0, hangul = 0, han = 0, latin = 0, hans = 0, hant = 0;
  // the first 16Ki code points of its text decide (a bounded cost per edit;
  // deterministic: document order)
  u32 budget = 1u << 14;
  // a code body is not text: its text nodes are skipped
  std::vector<bool> code(raw.nodes.size(), false);
  for (size_t n = 0; n < raw.nodes.size(); n++)
    if (raw.nodes[n].kind == Kind::codeblock || raw.nodes[n].kind == Kind::code)
      for (u32 k : raw.nodes[n].children)
        if (k < code.size()) code[k] = true;
  for (size_t n = 0; n < raw.nodes.size() && budget; n++) {
    const RawNode& node = raw.nodes[n];
    if (!node.isText || code[n] || node.str >= raw.strings.size()) continue;
    const std::string_view s = raw.strings[node.str];
    for (u32 i = 0; i < s.size() && budget; budget--) {
      const u32 cp = utf8Next(s, i);
      if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0xFF66 && cp <= 0xFF9D)) kana++;
      else if ((cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)) hangul++;
      else if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x20000 && cp <= 0x2FA1F) ||
               (cp >= 0xF900 && cp <= 0xFAFF)) {
        han++;
        if (std::binary_search(t.hans.begin(), t.hans.end(), cp)) hans++;
        else if (std::binary_search(t.hant.begin(), t.hant.end(), cp)) hant++;
      } else if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0x24F)) {
        latin++;
      }
    }
  }
  const u64 cjk = kana + hangul + han;
  if (cjk && cjk * 4 >= latin) {
    if (kana * 20 >= cjk) return "ja";  // kana: at least 5% of the CJK text
    if (hangul >= han) return "ko";
    return hant > hans ? "zh-Hant" : "zh-Hans";
  }
  return latin ? "en" : "und";
}

}  // namespace tsr
