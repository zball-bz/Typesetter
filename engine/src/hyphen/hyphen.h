// Liang hyphenation over compiled TeX patterns (v2 §15; en-US first).
#pragma once
#include "../support/support.h"

namespace tsr {

// Break positions for an ASCII word (break allowed before word[i] for each
// returned i). Empty for short (<5), non-ASCII-letter, or unmatched words.
// Mirrors the `hyphen` npm package semantics (leftmin/rightmin = 2).
// the shortest word hyphenated (plan P3-02: a named parameter; TeX's
// \lefthyphenmin + \righthyphenmin + 1 for the en patterns)
constexpr u32 kHyphenMinLetters = 5;
std::vector<u32> hyphenPoints(std::string_view word);

}  // namespace tsr
