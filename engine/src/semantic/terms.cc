#include "terms.h"

#include "../support/json.h"
#include "semantic_data.gen.h"

namespace tsr {

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
      out.push_back(std::move(p));
    }
    return out;
  }();
  return packs;
}

std::string localePackFor(std::string_view lang) {
  auto has = [](std::string_view name) {
    for (const LocalePack& p : builtinLocalePacks())
      if (p.lang == name) return true;
    return false;
  };
  std::string l(lang);
  for (char& c : l)
    if (c == '_') c = '-';
  if (has(l)) return l;
  auto lower = [](std::string s) {
    for (char& c : s)
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
  };
  std::string ll = lower(l);
  std::string prim = ll.substr(0, ll.find('-'));
  if (prim == "zh") {
    bool hant = ll.find("hant") != std::string::npos || ll == "zh-tw" || ll == "zh-hk" ||
                ll == "zh-mo" || ll.rfind("zh-tw-", 0) == 0 || ll.rfind("zh-hk-", 0) == 0 ||
                ll.rfind("zh-mo-", 0) == 0;
    return hant ? "zh-Hant" : "zh-Hans";
  }
  if (has(prim)) return prim;
  return "en";
}

Terms::Terms(const Config& cfg) : pack_(localePackFor(cfg.lang)) {
  for (const LocalePack& p : builtinLocalePacks())
    if (p.lang == pack_) chain_.push_back(&p);
  for (const LocalePack& p : builtinLocalePacks())
    if (p.lang == "en" && pack_ != "en") chain_.push_back(&p);
  const std::pair<const char*, const std::string*> kOverrides[] = {
      {"section", &cfg.supHeading}, {"table", &cfg.supTable},     {"figure", &cfg.supFigure},
      {"equation", &cfg.supEquation}, {"caption-sep", &cfg.capSep}};
  for (const auto& [key, v] : kOverrides)
    if (!v->empty()) overrides_[key] = *v;
}

std::string_view Terms::get(std::string_view key) const {
  if (auto it = overrides_.find(std::string(key)); it != overrides_.end()) return it->second;
  for (const LocalePack* p : chain_)
    if (auto it = p->terms.find(std::string(key)); it != p->terms.end()) return it->second;
  return {};
}

}  // namespace tsr
