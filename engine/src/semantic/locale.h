// Locale packs and the document's language (plan P3-30; design T4 M9,
// D-T06). A BCP-47 tag reads its packs through a chain: its likely subtags
// (lang-Script-REGION, CLDR likelySubtags), then its truncations unless a
// parent locale says otherwise (CLDR parentLocales: zh-Hant's parent is the
// root, not zh), ending at the root — the en pack. The data is
// engine/data/locale (locale.json: the tables; one json per pack).
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../support/support.h"

namespace tsr {

struct RawOps;

struct LocalePack {
  std::string lang;
  std::unordered_map<std::string, std::string> terms;
  int hyphenate = -1;  // par.hyphenate: auto (plan P3-30): its words hyphenate (1), or not (0); -1: its parent's
};

// the built-in packs (engine/data/locale), parsed once
const std::vector<LocalePack>& builtinLocalePacks();
// a tag's likely subtags: "zh-TW" → "zh-Hant-TW"; an unknown tag as written
// (canonical case, '_' as '-')
std::string maximizeLocale(std::string_view tag);
// the locale names a tag reads, most specific first, ending "root"
std::vector<std::string> localeChain(std::string_view tag);
// the built-in pack a tag reads first (its chain's first that has one; the
// root's: "en")
std::string localePackFor(std::string_view lang);
// whether a language's words hyphenate (par.hyphenate: auto)
bool localeHyphenates(std::string_view lang);

// (D-T06) the language of a document's text, deterministically: kana make
// it ja, hangul ko, Han zh-Hans or zh-Hant by the characters only one of the
// two scripts uses, Latin letters en, nothing und. CJK counts against Latin
// letters (one CJK character weighs four letters); code is not text.
std::string detectDocumentLang(const RawOps& raw);

}  // namespace tsr
