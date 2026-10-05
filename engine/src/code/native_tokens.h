// The native token provider (tree-sitter, json + tsm grammars): answers
// every pending NEED_TOKENS request of a document. Native builds only.
#pragma once
#include "../api/doc.h"

namespace tsr {

void provideNativeTokens(Doc& doc);

}  // namespace tsr
