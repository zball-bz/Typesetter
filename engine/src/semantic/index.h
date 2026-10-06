// The Index and the read-only phases of resolve (plan P1-10; design T3 "Index
// + phased resolver"). LOCATE walks the instantiated tree once, in document
// order, without changing it: every classed node becomes an instance with its
// number, level, title and anchor; every label registers uniformly (user
// labels validated, aliases minted); keyed rows and flow items are recorded.
// BIND then assigns citation ordinals in document order (a note's body at its
// marker). MATERIALIZE (materialize.h) builds the output from these facts.
// The Index persists on the document for `tsrc --stage=index` and tools.
#pragma once
#include <unordered_map>
#include <unordered_set>

#include "numbering.h"

namespace tsr {

constexpr u32 kNoInst = ~0u;

struct Instance {
  ClassId cls = 0;
  const ContentNode* node = nullptr;  // the input node (valid during resolve)
  Span span;
  int level = 1;
  std::string number;  // formatted; "" when unnumbered
  std::string label;   // its anchor: the accepted user label, else the alias
  bool aliased = false;  // the anchor is the alias (written as the node's label)
  std::string markerAlias;  // a flow item's marker anchor
  std::string title;
  // (plan P3-03; D-S03) the node whose content is its title (Title::Text:
  // the node itself): a collector clones it (cloneTitle); valid during
  // resolve
  const ContentNode* titleNode = nullptr;
  Supplement supplement;  // its class's, or an event's for its counter (plan P2-07)
};

struct LabelTarget {
  enum class K : u8 { Instance, Marker, Plain } k = K::Plain;
  u32 inst = kNoInst;
  Span span;
};

struct Row {
  std::string table, key;
  const ContentNode* node = nullptr;  // the row's own node, or the instance's (during resolve)
  Span span;
  u32 inst = kNoInst;
  std::string title, bodyText;
  int ordinal = 0;  // a citeable row's ordinal once cited
};

struct Index {
  std::vector<Instance> instances;
  std::unordered_map<std::string, LabelTarget> labels;
  std::vector<std::string> labelOrder;  // registration order
  std::vector<Row> rows;
  std::unordered_map<std::string, u32> rowOf;  // table '\0' key → first row
  std::vector<std::pair<std::string, std::vector<u32>>> flows;  // flow → items, document order
  std::unordered_map<const ContentNode*, u32> instOf;
  std::unordered_set<const ContentNode*> refused;  // user labels dropped from their node

  const Row* row(std::string_view table, std::string_view key) const;
  std::vector<u32>& flow(const std::string& name);
  const std::vector<u32>* flowItems(std::string_view name) const;
};

// target "a, b" → its keys, trimmed (a group reference)
std::vector<std::string> splitKeys(std::string_view target);
// the text of a subtree, comments skipped
void excerptInto(const ContentNode* n, const Interner& strs, std::string& out);

void locate(const ContentNode* root, const Registry& reg, Counters& counters, const Interner& strs,
            Index& ix, DiagSink& diags);
void bindCites(const ContentNode* root, const Registry& reg, Counters& counters, const Interner& strs,
               const Index& ix);

// tsrc --stage=index
std::string dumpIndex(const Index& ix, const Registry& reg);

}  // namespace tsr
