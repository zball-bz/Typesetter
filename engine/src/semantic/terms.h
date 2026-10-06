// Locale terms (plan P1-10; design T3 LocaleTerms over T4's LocalePack): the
// words the semantic layer generates — supplements, separators, the back
// reference, the unresolved mark — are data chosen by the document's
// language through its locale chain (locale.h, plan P3-30: likely subtags,
// parent locales, the root en); a document's own packs ($.locale) come
// before the built-in ones of each name. The terms.* settings override
// single words.
#pragma once
#include <unordered_map>

#include "../api/settings.gen.h"
#include "locale.h"

namespace tsr {

struct Decl;
class Interner;

class Terms {
 public:
  // decls: the document's ($.locale packs, plan P3-30), or none
  Terms(const ResolveSettings& cfg, const std::vector<Decl>* decls = nullptr, const Interner* strs = nullptr);
  // the word for `key` ("" when no pack has it)
  std::string_view get(std::string_view key) const;
  const std::string& pack() const { return pack_; }

 private:
  std::string pack_;
  std::vector<LocalePack> own_;  // the document's packs
  std::vector<const LocalePack*> chain_;
  std::unordered_map<std::string, std::string> overrides_;
};

}  // namespace tsr
