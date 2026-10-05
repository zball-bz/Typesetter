#include "resolve.h"

#include "../semantic/materialize.h"

namespace tsr {

void resolveDoc(ContentTree& tree, Arena& arena, Interner& strs, StyleTable& styles,
                const Config& cfg, DiagSink& diags, const Registry& reg, Index& index) {
  index = Index{};
  if (!tree.root) return;
  Counters counters(reg);
  Terms terms(cfg);
  locate(tree.root, reg, counters, strs, index, diags);
  bindCites(tree.root, reg, counters, strs, index);
  MaterializeEnv env{arena, strs, styles, diags, reg, terms, counters, index};
  tree.root = materialize(tree.root, env);
  // what tools read after resolve: ordinals (spans, not nodes, outlive it)
  for (Row& r : index.rows)
    for (const CollectorDef& c : reg.collectors)
      if (c.citeable && c.table == r.table) r.ordinal = counters.keyedIfSeen(c.rowCounter, r.key);
  for (Instance& in : index.instances) in.node = nullptr;
  for (Row& r : index.rows) r.node = nullptr;
  index.instOf.clear();
  index.refused.clear();
}

}  // namespace tsr
