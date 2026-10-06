// GENERATED from engine/schema/languages.json by tools/gen-languages.mjs — do not edit.
// The code-highlight manifest's engine tables (plan P3-22; design T9 A6).
#pragma once
#include <cstdint>
#include <string_view>

namespace tsr {

// a capture name's first segment → its token tag (kTokenTags), beyond the tags themselves
struct CaptureAlias {
  std::string_view name;
  int tag;
};
inline constexpr CaptureAlias kCaptureAliases[] = {
    {"tag", 5},  // → type
    {"conditional", 0},  // → keyword
    {"repeat", 0},  // → keyword
    {"include", 0},  // → keyword
    {"boolean", 6},  // → constant
    {"constructor", 6},  // → constant
    {"method", 4},  // → function
    {"field", 10},  // → property
    {"parameter", 10},  // → property
};

// a fence tag → its built-in language (names, aliases and profiles)
struct LangAlias {
  std::string_view tag, lang;
};
inline constexpr LangAlias kLangAliases[] = {
    {"json", "json"},
    {"javascript", "javascript"},
    {"js", "javascript"},
    {"mjs", "javascript"},
    {"typescript", "typescript"},
    {"ts", "typescript"},
    {"python", "python"},
    {"py", "python"},
    {"cpp", "cpp"},
    {"c++", "cpp"},
    {"cc", "cpp"},
    {"c", "cpp"},
    {"rust", "rust"},
    {"rs", "rust"},
    {"tsm", "tsm"},
    {"cpp-literate", "cpp"},
};

// an overlay (code/overlay.h): open, a name of no forbid character, close,
// then at most one of the suffixes (longest listed first), one token of tag
struct OverlaySpec {
  std::string_view name, open, close, forbid;
  std::string_view suffix[4];
  std::uint8_t nSuffix;
  int tag;
};
inline constexpr OverlaySpec kOverlays[] = {
    {"noweb", "<<", ">>", "<>\n", {"+=", "=", "", ""}, 2, 12},
};
inline constexpr std::uint32_t kOverlayCount = 1;

// a fence tag that means a language with overlays (bit k: kOverlays[k])
struct LangProfile {
  std::string_view tag, lang;
  std::uint32_t overlays;
};
inline constexpr LangProfile kProfiles[] = {
    {"cpp-literate", "cpp", 1u},
};

}  // namespace tsr
