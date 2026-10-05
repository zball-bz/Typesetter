// Sidecar extraction (verbatim-design §5): a model transform of the resolve
// stage (plan P1-03 moved it out of the API handle).
#pragma once
#include "../model/model.h"

namespace tsr {

// split each code line of a sidecar-marked codeblock at its marker: the code
// part re-forms the body (what the tokenizer sees), the comment part parses
// as an inline fragment into a trailing group{role:"sidecar-lines"} child —
// one seq per logical line
void extractSidecars(ContentNode* n, Arena& arena, Interner& strs, StyleTable& styles,
                     DiagSink& diags);

}  // namespace tsr
