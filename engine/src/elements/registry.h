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
  enum class K : u8 { Text, Bool, Slot, Anchor, Num } k = K::Text;
  std::string s;  // Text: the value; Slot / Anchor: a slot name
  bool b = false;
  double num = 0;
};
struct TItem {
  // Text: a literal; Slot / Term: a value (adjacent ones make one text);
  // Node: a node; Styled: a delta over its kids; When: kids if a slot is set,
  // else `orElse`; Each: the items (or keys) placeholder, `sep` between;
  // Paras: a flow item's body as paragraphs, lifted (plan P3-01: anchor on
  // the first, `tail` at the end of the last; `name`: the role of the
  // inline wrapper its content is entered under, note-body)
  enum class K : u8 { Text, Slot, Term, Node, Styled, When, Each, Paras } k = K::Text;
  std::string name;
  Kind kind = Kind::text;
  bool siteStyle = false;  // a block node taking the site's style (else 0)
  std::vector<std::pair<ArgK, TArg>> args;
  StyleDelta delta;  // Styled: what it puts on its content
  float size = 1.0f;
  std::vector<TItem> kids, sep, orElse, tail;
  TArg anchor;
  // Node: its list marker (plan P3-03: a notes entry's, from the note's
  // number), as text
  std::vector<TItem> marker;
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
  // (plan P2-07) numbered within another counter: its first withinDepth
  // components prefix this one's (joined by withinSep), and a step of that
  // counter at a level ≤ withinDepth restarts this one
  u16 within = kNoIndex;
  int withinDepth = 1;
  std::string withinSep = ".";
  bool withinPrefix = true;  // (plan P3-13) false: restarted, not prefixed (footnotes per chapter)
  // (plan P3-03; design T3 enum {scope: olist}) a counter scoped to each
  // instance of a class: it starts over in one (from its `start` argument,
  // in its `numbering` pattern) and the outer count resumes after it
  ClassId scope = 0;
  std::string scopeName;
  std::string pattern;     // NumberingPattern ("" = 1.1: decimal, '.'-joined)
  std::vector<int> start;  // the values before the first step (default: none)
};

// A counter system (plan P2-07; design T3 NumberingPattern): `{name}` in a
// pattern formats with these symbols
struct CounterSystem {
  std::string name;
  std::vector<std::string> symbols;
  // (plan P3-13; CSS additive-symbols) Additive: a number is the greedy sum
  // of the symbols' weights, largest first (roman numerals, Greek or Hebrew
  // numbering)
  enum class Mode : u8 { Numeric, Alphabetic, Cyclic, Fixed, Additive } mode = Mode::Numeric;
  std::vector<int> weights;  // Additive: one per symbol, descending
};

// A class's (or an event's) supplement word: a locale term, a literal, or
// literals per language (plan P2-07)
struct Supplement {
  std::string term;
  std::string text;
  bool literal = false;
  std::vector<std::pair<std::string, std::string>> byLang;  // (lang prefix, text)
  bool set() const { return !term.empty() || literal || !byLang.empty(); }
};

struct AliasRule {  // generated labels: prefix + number | key | ordinal
  std::string prefix;
  // Ordinal (plan P3-13): the instance's place among its class's, from 1 —
  // unique when the number is not (a footnote counter reset per chapter)
  enum class Body : u8 { None, Number, Key, Ordinal } body = Body::None;
  Template ref;  // how a reference to this alias reads (empty: the class's)
  bool hasRef = false;
};

// A site (design T3 Site; plan P3-03): where an instance's generated content
// goes — before or after the content of the node, its first or last
// paragraph, or a part (a slot: a figure's caption); into an argument (the
// compat equation name); in place of the node; or as its tag part (the
// margin tag: an equation's number)
struct SiteDef {
  enum class Where : u8 { Prepend, Append, Arg, Replace, Tag } where = Where::Prepend;
  enum class At : u8 { Self, FirstPara, LastPara, Part } at = At::Self;
  SlotId part = SlotId::None;  // At::Part (no such part: the first paragraph)
  ArgK arg = ArgK::label;
  Template tmpl;
};

// (plan P3-13; design T3 Placement) where a flow's items go that no
// collector placed: nowhere (CollectorOnly), the document's end (End), the
// end of each section of outline level ≤ depth (SectionEnd: endnotes per
// chapter), or the bottom of their marker's page (Deferred: placed at the
// end like End, its entries are the paged sheets' inserts, design T6)
struct FlowDef {
  std::string name;
  enum class Placement : u8 { CollectorOnly, End, SectionEnd, Deferred } placement = Placement::CollectorOnly;
  int depth = 1;  // SectionEnd: the sections' outline level
  AliasRule markerAlias;
  Template marker;
  bool atEnd() const { return placement != Placement::CollectorOnly; }
};

struct ElementClass {
  std::string name;
  std::vector<Selector> select;
  u16 counter = kNoIndex;
  enum class Numbering : u8 { Never, Always, Labelled } numbering = Numbering::Never;
  Supplement supplement;
  enum class Labels : u8 { User, None, FromArg } labels = Labels::User;
  ArgK labelArg = ArgK::label;
  // (plan P3-13) Part: a part's content (a figure's caption)
  enum class Title : u8 { None, Text, Arg, Ext, Part } title = Title::None;
  SlotId titlePart = SlotId::None;
  ArgK titleArg = ArgK::label;
  std::string titleExt;  // Title::Ext: the EXT name (plan P2-07)
  bool outline = false;
  AliasRule alias;
  std::vector<SiteDef> sites;
  // (plan P3-03; D-S05) whether its sites show its number where it stands
  // (a site's output; a reference and a collector show it regardless): the
  // built-in heading row says false, a document that numbers a class true
  bool display = true;
  // (plan P3-03; design T3 SemInfo.number) its number is its list marker
  // (an ordered list's item): the box tree draws it
  bool marker = false;
  // (plan P3-03; design T3 refersTo) Enclosing: a label here names the
  // nearest classed ancestor (a table in a figure: @tab is the figure)
  enum class RefersTo : u8 { Self, Enclosing } refersTo = RefersTo::Self;
  // (plan P3-06; design T7 refPreview) a reference to an instance may show
  // its content in place (a footnote's body on hover): the RenderResult's
  // anchors table carries it, the shell's refPreview behaviour reads it
  enum class Preview : u8 { None, Block } preview = Preview::None;
  Template ref;
  bool hasRef = false;
  std::optional<FlowDef> flow;
  std::string table;  // instances are rows of this keyed table (key = label)
  // (plan P2-07) the rows' key is this argument, not a label (citation keys
  // are no labels: `@key` cites the row)
  bool rowKeyed = false;
  ArgK rowKey = ArgK::key;
  // (plan P3-13; design T3 multi tables) every instance is a row, a key may
  // hold several (an index's entries); else the first row of a key wins
  bool multi = false;
  // named reference forms (ref(target, {form})) beyond the built-in number,
  // title, supplement and full
  std::vector<std::pair<std::string, Template>> forms;
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
  // (plan P3-13) Classes: the instances of these classes in document order
  // (a list of figures, of tables, of theorems)
  enum class Src : u8 { Outline, Table, Flow, Classes } src = Src::Outline;
  // (plan P3-13; design T3 Query.scope) a flow's items in the whole
  // document, or in the section the collector stands in (outline level ≤
  // scopeDepth): a chapter's endnotes placed at its end
  enum class Scope : u8 { Doc, Section } scope = Scope::Doc;
  int scopeDepth = 1;
  std::vector<ClassId> classes;
  std::string table, flow;
  Template head;  // (plan P3-13) a static head, before its wrap (empty or not)
  bool hasHead = false;
  bool nestByDepth = false;
  // a keyed table's rows: the cited ones in citation order, then (by
  // default or by the collect node's `cited`) every other in document order
  enum class Cited : u8 { Cited, CitedThenAll } cited = Cited::Cited;
  enum class Ctx : u8 { Collector, Instance, Row } ctx = Ctx::Collector;
  Template wrap, entry, empty;
  bool hasEmpty = false;
  // (plan P3-13) a table's rows grouped by key (an index: a key, then
  // its occurrences), in sort-key order (else document order)
  bool groupByKey = false, bySortKey = false;
  bool keyedRows = false;     // (rows) a keyed table: ordinals and anchors per row
  u16 rowCounter = kNoIndex;  // a keyed counter: the rows' ordinals
  AliasRule rowAnchor;
  Template cite;  // a reference to keys of this table
  bool citeable = false;
  bool compress = false;  // (plan P2-09) three or more consecutive ordinals read first–last
};

// a supplement in its JSON form ("key": a term; {term}, {text}, {lang: text, …})
bool parseSupplement(const JsonValue& v, Supplement& out);

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
  ClassId classNamed(std::string_view name) const;

  std::vector<ElementClass> classes;  // [0]: no class
  std::vector<CounterDef> counters;
  std::vector<CounterSystem> systems;
  std::vector<CollectorDef> collectors;
  Template unresolved, unnumbered;
  // (plan P3-01; T7's minimal role map) the element a generated role reads
  // as on the semantic page — its presentation is the rules' (defaults.json)
  // — and what that element says of a run (a page writing a run's own
  // superscript, bold or italic needs no second element for it)
  struct RoleHtml {
    std::string role, tag;
    enum class Says : u8 { Nothing, Super, Bold, Italic } says = Says::Nothing;
  };
  std::vector<RoleHtml> roleHtml;
  const RoleHtml* roleElement(std::string_view role) const {
    for (const RoleHtml& r : roleHtml)
      if (r.role == role) return &r;
    return nullptr;
  }

 private:
  std::vector<std::pair<ClassId, const Selector*>> byKind_[KIND_COUNT + 1];  // [KIND_COUNT]: any kind
  void index();
};

}  // namespace tsr
