// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
#pragma once
#include <cstdint>
#include <string_view>

namespace tsr {

// textual attribute domains (schema "domains"); whole-string match on bytes
enum class TextDomain : std::uint8_t { Ident, Label, Lang, Size, Intlist, RangeSet, Color, Font, Copy, Classlist, Names, Extname, Len, Gap, Tracks, Lens, Where, Features };
bool matchDomain(TextDomain d, std::string_view s);

}  // namespace tsr
