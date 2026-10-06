#include "resolve.h"

#include "../model/cascade.h"
#include "../semantic/manifest.h"
#include "../semantic/materialize.h"

namespace tsr {

void resolveDoc(ContentTree& tree, Arena& arena, Interner& strs, StyleTable& styles, NodePropsTable& props,
                const Cascade& cascade, const ResolveSettings& cfg, DiagSink& diags, const Registry& reg, Index& index,
                std::string_view labels) {
  index = Index{};
  if (!tree.root) return;
  Counters counters(reg);
  Terms terms(cfg, &tree.decls, &strs);  // (plan P3-30: the document's locale packs too)
  // (plan P3-31) its project: where its counters start, the other documents'
  // labels and starts
  ProjectStarts starts;
  if (!parseProjectStarts(cfg.projectStarts, starts))
    diags.add(Sev::Warning, "project-starts", {}, "project.starts is {doc: {counter: n}}: ignored");
  if (auto own = starts.find(cfg.projectDoc); own != starts.end())
    for (const auto& [name, n] : own->second) {
      const u16 c = counters.counterNamed(name);
      if (c == kNoIndex) continue;
      const CounterDef& d = reg.counters[c];
      if (d.keyed || d.within != kNoIndex || d.scope) continue;  // (restarted ones start where they start)
      counters.offset(c, n);
      index.starts[name] = n;
    }
  ExternalLabels external;
  if (!labels.empty()) {
    std::string err;
    if (!decodeLabelManifests(labels, cfg.projectDoc, external, err))
      diags.add(Sev::Warning, "input-labels", {}, "the labels input is malformed (" + err + "): nothing imported");
  }
  locate(tree.root, reg, counters, strs, index, diags);
  bind(tree.root, reg, counters, strs, index, diags);
  MaterializeEnv env{arena, strs, styles, diags, reg, terms, counters, index, cascade, 0, &external, &starts};
  tree.root = materialize(tree.root, env);
  // (plan P3-31) its counters' totals: the steps of a document-wide
  // counter's first component, its own start not counted
  for (u16 c = 0; c < (u16)reg.counters.size(); c++) {
    const CounterDef& d = reg.counters[c];
    if (d.keyed || d.within != kNoIndex || d.scope) continue;
    const auto own = index.starts.find(d.name);
    const int n = counters.top(c) - (own == index.starts.end() ? 0 : own->second);
    if (n > 0) index.totals.push_back({d.name, n});
  }
  if (env.made) settleMade(tree.root, cascade, props, styles);
  // what tools read after resolve: ordinals (spans, not nodes, outlive it)
  for (Row& r : index.rows)
    for (const CollectorDef& c : reg.collectors)
      if (c.citeable && c.table == r.table) r.ordinal = counters.keyedIfSeen(c.rowCounter, r.key);
  for (Instance& in : index.instances) in.node = nullptr;
  for (Row& r : index.rows) r.node = nullptr;
  index.instOf.clear();
  index.refused.clear();
  index.collectAt.clear();
  index.occurrenceOf.clear();
}

}  // namespace tsr
