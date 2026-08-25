// Resolver pass (v2 §11.1, document-model §5): counter automata, label
// table, REF rewriting, collector expansion. Runs between instantiation and
// emission and mutates the tree in place — the post-resolve tree IS the
// documented ContentTree, so fallback HTML would already carry final numbers.
#pragma once
#include "../api/config.h"
#include "../model/model.h"

namespace tsr {

void resolveDoc(ContentTree& tree, Arena& arena, Interner& strs,
                StyleTable& styles, const Config& cfg, DiagSink& diags);

}  // namespace tsr
