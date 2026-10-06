// MATERIALIZE (plan P1-10; design T3 "Templates + Materializer", "Index +
// phased resolver" B1–B3): the output tree, built from the instantiated tree
// and the Index without changing the input (D-S13) — unchanged subtrees are
// shared, changed nodes are new. Every piece of generated presentation is a
// registry template instantiated at a site: reference forms, sites (caption
// prefixes, equation tags, term lines, flow markers), collector wraps and
// entries. Words come from the locale terms.
#pragma once
#include "../semantic/index.h"
#include "../semantic/terms.h"

namespace tsr {

struct MaterializeEnv {
  Arena& arena;
  Interner& strs;
  StyleTable& styles;
  DiagSink& diags;
  const Registry& reg;
  const Terms& terms;
  Counters& counters;
  const Index& ix;
  size_t made = 0;  // the nodes it made (unfolded: model/cascade.h settleMade)
};

// the output root for the input root
ContentNode* materialize(const ContentNode* root, MaterializeEnv& env);

}  // namespace tsr
