// The native token provider (tree-sitter, json grammar). Native builds only. ('tsm'
// blocks are tokenized by the engine itself since plan P1-09; the tsm
// tree-sitter grammar stays linked for the conformance check, nativeTokens.)
#pragma once
#include "../api/doc.h"

namespace tsr {

// the highlight tokens of `body` in `lang` (json, tsm), sorted, non-overlapping
std::vector<CodeToken> nativeTokens(std::string_view lang, std::string_view body);

}  // namespace tsr
