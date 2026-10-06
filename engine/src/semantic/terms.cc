#include "terms.h"

#include "../model/model.h"
#include "../support/json.h"

namespace tsr {

namespace {
const ArgVal* extOf(const Decl& d, const Interner& strs, std::string_view name) {
  for (const ArgVal& a : d.args)
    if (a.key == ArgK::ext && strs.get(a.name) == name) return &a;
  return nullptr;
}
}  // namespace

Terms::Terms(const ResolveSettings& cfg, const std::vector<Decl>* decls, const Interner* strs)
    : pack_(localePackFor(cfg.lang)) {
  // (plan P3-30) the document's packs: $.locale(tag, {terms, hyphenate})
  if (decls && strs)
    for (const Decl& d : *decls) {
      if (d.superseded || d.type >= DECL_COUNT || std::string_view(kDecls[d.type].name) != "locale") continue;
      LocalePack p;
      p.lang = maximizeLocale(strs->get(d.name)) == maximizeLocale("en") ? "en" : std::string(strs->get(d.name));
      if (const ArgVal* t = extOf(d, *strs, "terms"); t && t->tag == ArgTag::Str) {
        JsonValue v;
        JsonReader rd;
        if (rd.parse(strs->get(t->ref), v))
          for (size_t k = 0; k < v.keys.size(); k++)
            if (v.vals[k].t == JsonValue::T::Str) p.terms[v.keys[k]] = v.vals[k].str;
      }
      own_.push_back(std::move(p));
    }
  // its chain: each name's own pack, then the built-in one; the root is en
  for (std::string name : localeChain(cfg.lang)) {
    if (name == "root") name = "en";
    for (const LocalePack& p : own_)
      if (p.lang == name || maximizeLocale(p.lang) == maximizeLocale(name)) chain_.push_back(&p);
    for (const LocalePack& p : builtinLocalePacks())
      if (p.lang == name) chain_.push_back(&p);
  }
  // the host's words for terms (settings rows naming a term: schema "term")
  forEachTermSetting(cfg, [&](const char* key, const std::string& v) {
    if (!v.empty()) overrides_[key] = v;
  });
}

std::string_view Terms::get(std::string_view key) const {
  if (auto it = overrides_.find(std::string(key)); it != overrides_.end()) return it->second;
  for (const LocalePack* p : chain_)
    if (auto it = p->terms.find(std::string(key)); it != p->terms.end()) return it->second;
  return {};
}

}  // namespace tsr
