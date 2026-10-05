// The semantic resolver (v2 §11.1, document-model §5; plan P1-10, design T3):
// the phase driver. PHASE 0 is the registry (built before instantiate, which
// stamps each node's class); then LOCATE (the Index: instances, numbers,
// labels, rows, flows — read-only), BIND (citation ordinals) and MATERIALIZE
// (the output tree: references, sites, collectors, flows). Every feature is
// a registry row; this file knows none. The input tree is not changed
// (D-S13): its output replaces tree.root; the Index persists.
#pragma once
#include "../api/config.h"
#include "../semantic/index.h"

namespace tsr {

void resolveDoc(ContentTree& tree, Arena& arena, Interner& strs, StyleTable& styles,
                const Config& cfg, DiagSink& diags, const Registry& reg, Index& index);

}  // namespace tsr
