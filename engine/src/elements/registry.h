// The element registry (plan P1-10; design T3 "Element registry"): one row
// per class of node — what it is (selectors), how it is numbered, labelled,
// titled and referenced, what is attached to it (sites), whether it is a
// flow item or a table row — plus the counters, the collectors and the
// templates all generated presentation is made of. The built-in rows are
// data (engine/data/elements.json); a registry builds from any document of
// that form, so the built-ins have no privilege over declared rows (P2-07).
// Membership is decided once, at instantiate (ContentNode::cls); no stage
// compares role strings to find out what a node is.
#pragma once
#include <memory>
#include <optional>

#include "../model/model.h"
#include "../support/json.h"

namespace tsr {

using ClassId = u16;  // 0: no class
constexpr u16 kNoIndex = 0xFFFF;

// --- templates: generated, style-neutral content ------------------------------
struct TArg {  // a template node's argument value
  enum class K : u8 { Text, Bool, Slot, Anchor } k = K::Text;
  std::string s;  // Text: the value; Slot / Anchor: a slot name
  bool b = false;
};
struct TItem {
  // Text: a literal; Slot / Term: a value (adjacent ones make one text);
  // Node: a node; Styled: a delta over its kids; When: kids if a slot is set,
  // else `orElse`; Each: the items (or keys) placeholder, `sep` between;
  // Paras: a flow item's body as paragraphs (anchor on the first, `tail` at
  // the end of the last, scaled by `scale`)
  enum class K : u8 { Text, Slot, Term, Node, Styled, When, Each, Paras } k = K::Text;
  std::string name;
  Kind kind = Kind::text;
  bool siteStyle = false;  // a block node taking the site's style (else 0)
  std::vector<std::pair<ArgK, TArg>> args;
  u64 bits = 0;
  float size = 1.0f;
  std::vector<TItem> kids, sep, orElse, tail;
  TArg anchor;
};
using Template = std::vector<TItem>;

struct Selector {
  bool anyKind = true;
  Kind kind = Kind::doc;
  std::vector<std::pair<ArgK, std::string>> preds;  // arg == value (text form)
  ClassId inside = 0;                               // nearest classed ancestor
  int specificity() const { return (anyKind ? 0 : 1) + (int)preds.size() + (inside ? 1 : 0); }
};

struct CounterDef {
  std::string name;
  bool byLevel = false;
  ArgK levelArg = ArgK::level;
  int depth = 1;        // by-level: levels clamp into 1..depth
  bool gapOne = false;  // a skipped level counts 1 (else 0: "1.0.1")
  bool keyed = false;   // steps once per distinct key
};

struct AliasRule {  // generated labels: prefix + number | key
  std::string prefix;
  enum class Body : u8 { None, Number, Key } body = Body::None;
  Template ref;  // how a reference to this alias reads (empty: the class's)
  bool hasRef = false;
};

struct SiteDef {
  enum class Where : u8 { Prepend, Arg, Replace } where = Where::Prepend;
  enum class At : u8 { Self, FirstPara } at = At::Self;
  ArgK arg = ArgK::label;
  Template tmpl;
};

struct FlowDef {
  std::string name;
  bool placeAtEnd = false;
  AliasRule markerAlias;
  Template marker;
};

struct ElementClass {
  std::string name;
  std::vector<Selector> select;
  u16 counter = kNoIndex;
  enum class Numbering : u8 { Never, Always, Labelled } numbering = Numbering::Never;
  std::string supplement;  // a term key
  enum class Labels : u8 { User, None, FromArg } labels = Labels::User;
  ArgK labelArg = ArgK::label;
  enum class Title : u8 { None, Text, Arg } title = Title::None;
  ArgK titleArg = ArgK::label;
  bool outline = false;
  AliasRule alias;
  std::vector<SiteDef> sites;
  Template ref;
  bool hasRef = false;
  std::optional<FlowDef> flow;
  std::string table;  // instances are rows of this keyed table (key = label)
  // presentation traits (plan P2-05, finding role-string-dispatch: what a
  // class means to the box tree and the semantic page, read through
  // ContentNode::cls, never through its role string)
  enum class Box : u8 { Plain, Figure } box = Box::Plain;   // Figure: captions, floats
  enum class Html : u8 { Plain, Figure } html = Html::Plain;  // Figure: <figure>/<figcaption>
  bool replaced() const {
    for (const SiteDef& s : sites)
      if (s.where == SiteDef::Where::Replace) return true;
    return false;
  }
};

struct CollectorDef {
  std::string name;
  enum class Src : u8 { Outline, Table, Flow } src = Src::Outline;
  std::string table, flow;
  bool nestByDepth = false;
  bool cited = false;  // the rows cited, in citation order …
  ArgK allArg = ArgK::label;
  std::string allValue;  // … every row when the collector's allArg says so
  enum class Ctx : u8 { Collector, Instance, Row } ctx = Ctx::Collector;
  Template wrap, entry, empty;
  bool hasEmpty = false;
  // a table whose rows are the collector node's own children
  bool rowsFromKids = false;
  ArgK rowKey = ArgK::name;
  u16 rowCounter = kNoIndex;  // a keyed counter: the rows' ordinals
  AliasRule rowAnchor;
  Template cite;  // a reference to keys of this table
  bool citeable = false;
};

class Registry {
 public:
  // the built-in rows, parsed once
  static const Registry& builtin();
  // a registry from a document of elements.json's form; nullptr and `error`
  // when it is malformed
  static std::unique_ptr<Registry> fromJson(std::string_view json, std::string& error);

  // the class of a node, given the class of its nearest classed ancestor
  ClassId classify(const ContentNode* n, ClassId inside, const Interner& strs) const;
  const ElementClass& cls(ClassId c) const { return classes[c]; }
  const CollectorDef* collector(std::string_view name) const;
  const CollectorDef* tableOwner(std::string_view table) const;
  const CollectorDef* flowCollector(std::string_view flow) const;
  // a label of the shape some alias rule mints (it would collide)
  bool reservedShape(std::string_view label) const;

  std::vector<ElementClass> classes;  // [0]: no class
  std::vector<CounterDef> counters;
  std::vector<CollectorDef> collectors;
  Template unresolved, unnumbered;

 private:
  std::vector<std::pair<ClassId, const Selector*>> byKind_[KIND_COUNT + 1];  // [KIND_COUNT]: any kind
  void index();
};

}  // namespace tsr
