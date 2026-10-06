// Cross-document labels (plan P3-31; design T3 LabelManifest over T9 A7,
// INTEGRATION: T9's transport, T3's semantics). A document's labels product
// is what the other documents of its project may refer to — its labels, each
// with its class, its number as raw start-independent components by counter,
// its title and anchor — and its counters' totals; a document reads the
// others' through the declared input `labels` (inputs.def), given before
// Ingest and never shown to a script. The importer formats an external
// label with its own class templates, terms and patterns, after the
// producer's start (project.starts).
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "index.h"

namespace tsr {

struct ExternalLabel {
  std::string doc;   // its document's key
  std::string cls;   // its class ("": a plain label)
  int level = 1;
  std::vector<std::pair<std::string, std::vector<int>>> number;  // (counter, values), outermost first
  std::string title;
  std::string anchor;  // its anchor key (the importer's page spells it)
};
struct ExternalLabels {
  std::unordered_map<std::string, ExternalLabel> byLabel;
};

// the input `labels` (LabelManifestList): a JSON array of labels products;
// `self`'s own manifest is skipped, an earlier manifest's label wins over a
// later one's. false: malformed (err) — nothing is imported
bool decodeLabelManifests(std::string_view json, std::string_view self, ExternalLabels& out, std::string& err);

// project.starts: {doc: {counter: n}}; false: malformed
using ProjectStarts = std::unordered_map<std::string, std::unordered_map<std::string, int>>;
bool parseProjectStarts(std::string_view json, ProjectStarts& out);

// the labels product: {"v":1,"doc":…,"totals":{…},"labels":[…]}, labels in
// label order, totals in counter order, one label per line
std::string labelsProduct(const Index& ix, const Registry& reg, std::string_view doc);

}  // namespace tsr
