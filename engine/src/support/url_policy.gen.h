// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// The URL policy (engine/schema/url_policy.def; plan P3-20): which references a
// document may make, by use. A relative reference is allowed for every use.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace tsr {

enum class UrlUse : std::uint8_t { Image = 1, Link = 2, Load = 4 };

inline bool urlAllowed(std::string_view src, UrlUse use) {
  const size_t colon = src.find(':');
  if (colon == std::string_view::npos) return true;
  const size_t stop = src.find_first_of("/?#");
  if (stop != std::string_view::npos && stop < colon) return true;
  std::string low(src.substr(0, colon));
  for (char& c : low) c = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
  const std::string_view rest = src.substr(colon + 1);
  const std::uint8_t u = (std::uint8_t)use;
  if (low == "http") return (u & 7) && true;
  if (low == "https") return (u & 7) && true;
  if (low == "mailto") return (u & 2) && true;
  if (low == "data") return (u & 1) && rest.rfind("image/", 0) == 0;
  (void)rest;
  return false;
}

}  // namespace tsr
