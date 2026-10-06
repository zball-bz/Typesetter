// Hyphenation dictionaries (plan P4-06; design T5 HyphenDict, D-X09):
// Liang's algorithm over TeX patterns. One form, whoever supplies them: the
// resident en-US (engine/gen/hyphen_en_us.h, tools/hyphc.mjs) and a
// language's patterns a host answers through the hyphPatterns resource —
// both pattern text, compiled into the same trie.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "../support/support.h"

namespace tsr {

class HyphenDict {
 public:
  std::string tag;                // the patterns' language (BCP-47)
  std::string hyphenChar = "-";   // the glyph a break at a point adds
  u8 leftmin = 2, rightmin = 2;   // the fewest letters before / after a break
  // the shortest word hyphenated (plan P3-02's named minimum, per
  // dictionary: \lefthyphenmin + \righthyphenmin + 1)
  u32 minWord() const { return (u32)leftmin + rightmin + 1; }
  // whether a lower-cased letter is in the patterns' alphabet (a word with
  // one that is not is no word of this language: no points)
  bool has(u32 cp) const {
    return cp < 128 ? (asciiAlpha_[cp >> 6] >> (cp & 63)) & 1 : hasWide(cp);
  }
  // a lower-cased word's break positions (before letter i, leftmin ≤ i ≤
  // n − rightmin; an exception's as listed), appended to out
  void points(const u32* w, u32 n, std::vector<u32>& out) const;
  size_t bytes() const;  // its memory (the Session's budget)

  // TeX pattern text (".ach4 a1b …") and exceptions (hyphenated words,
  // "as-so-ciate …"); false (and err) when malformed
  static bool compile(std::string_view tag, std::string_view patterns, std::string_view exceptions, u8 leftmin,
                      u8 rightmin, std::string_view hyphenChar, HyphenDict& out, std::string& err);

 private:
  // the trie: node k's edges are [edgeStart_[k], edgeStart_[k + 1]), by codepoint
  std::vector<u32> edgeStart_, edgeCp_, edgeChild_;
  std::vector<i32> nodeRow_;    // the levels row of the pattern ending there, or −1
  std::vector<u32> rowStart_;   // row r: levels_[rowStart_[r], rowStart_[r + 1])
  std::vector<u8> levels_;
  std::vector<u32> alphabet_;   // sorted
  u64 asciiAlpha_[2] = {0, 0};  // the alphabet below U+0080, as bits
  i32 rootAscii_[128] = {};     // the root's child by an ASCII letter (−1: none)
  std::vector<std::pair<std::vector<u32>, std::vector<u32>>> exceptions_;  // (word, points), by word
  i32 child(u32 node, u32 cp) const;
  bool hasWide(u32 cp) const;
};

// the resident dictionary (en-US), compiled on first use
const HyphenDict& residentHyphenDict();
// whether a language is the resident dictionary's: en, en-US, und (none)
bool residentHyphenLang(std::string_view lang);

}  // namespace tsr
