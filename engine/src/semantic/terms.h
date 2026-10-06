// Locale terms (plan P1-10; design T3 LocaleTerms over T4's LocalePack): the
// words the semantic layer generates — supplements, separators, the back
// reference, the unresolved mark — are data chosen by the document's terms
// language through a fallback chain: exact → script (zh-TW/HK/MO → zh-Hant;
// zh, zh-CN, zh-SG → zh-Hans) → language → root (en). The terms.* settings
// override single words.
#pragma once
#include <unordered_map>

#include "../api/settings.gen.h"

namespace tsr {

struct LocalePack {
  std::string lang;
  std::unordered_map<std::string, std::string> terms;
};

// the built-in packs (engine/data/locale), parsed once
const std::vector<LocalePack>& builtinLocalePacks();
// the pack a language reads: its name, after the fallback chain
std::string localePackFor(std::string_view lang);

class Terms {
 public:
  explicit Terms(const ResolveSettings& cfg);
  // the word for `key` ("" when no pack has it)
  std::string_view get(std::string_view key) const;
  const std::string& pack() const { return pack_; }

 private:
  std::string pack_;
  std::vector<const LocalePack*> chain_;
  std::unordered_map<std::string, std::string> overrides_;
};

}  // namespace tsr
