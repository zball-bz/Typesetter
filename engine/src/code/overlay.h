// Code overlays (plan P3-22; design T9 A6): spans the engine finds in a code
// body itself — noweb's <<fragment>> names — and sets as one token of the
// overlay's class whatever the provider says. engine/schema/languages.json
// declares them (code/languages.gen.h); a code block takes them from its
// codeblock.overlays property (a fence argument, a rule, or a fence
// profile's default rule: cpp-literate).
//
// The transform is byte-preserving: every byte of every span becomes an
// ASCII space in the text the provider tokenizes, so every offset outside
// the spans is unchanged, and the provider only ever sees plain code (a
// fragment name is not valid C++). Its runs are then merged with the spans;
// a span wins any overlap.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "tokens.h"

namespace tsr {

// the overlays named in `names` (space-separated) as a mask (bit k:
// kOverlays[k]); a name no overlay has goes to `unknown`
u32 overlayMask(std::string_view names, std::vector<std::string_view>* unknown = nullptr);

// the mask's overlay names, space-separated
std::string overlayNames(u32 mask);

// a fence tag's built-in language (its name, an alias, a profile), else ""
std::string_view languageOfTag(std::string_view tag);

// the spans of the mask's overlays in `body`, ascending and disjoint (an
// earlier overlay wins a tie), as tokens of their class
std::vector<CodeToken> overlaySpans(std::string_view body, u32 mask);

// `body` with the spans' bytes blanked (same length)
std::string maskSpans(std::string_view body, const std::vector<CodeToken>& spans);

// a provider's runs over the masked text, with the spans set over them: a
// run that overlaps a span keeps only its parts outside it
std::vector<CodeToken> mergeSpans(const std::vector<CodeToken>& runs, const std::vector<CodeToken>& spans);

}  // namespace tsr
