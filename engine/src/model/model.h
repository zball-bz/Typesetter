// Content tree + instantiation (document-model §2–§4); the style table is
// style.h.
#pragma once
#include <cstdlib>

#include "style.h"

namespace tsr {

struct ContentNode {
  Kind kind;
  Span span;
  StyleId style = 0;
  u16 cls = 0;     // SemInfo: its element class (elements/registry.h), set at
                   // instantiate; 0 = none. Not an argument; dumps omit it.
  StrRef str = 0;  // text: interned string
  // text: its cooked→raw map (plan P2-04), nrawmap/2 pairs (cooked offset,
  // raw offset − span.start), identity between breakpoints; none when the
  // text is its own source slice, or not at its own source (an occurrence
  // alias, a contained span)
  const u32* rawmap = nullptr;
  u32 nrawmap = 0;
  std::vector<ArgVal> args;         // Str args re-pointed to doc interner
  std::vector<ContentNode*> kids;
  // (plan P2-15; design T8 MathEnv) the positional declarations in force
  // where it was emitted: those whose flow index is at most its EMIT's —
  // a moved or cloned node keeps it, so a formula binds names as of its
  // place in the flow
  u32 declEpoch = 0;
  // (plan P3-01; design T4 Cascade) its block properties (a NodePropsTable
  // id: model/cascade.h) and the rules in force for its children (a RuleEnvId)
  u32 props = 0;
  u32 env = 0;
  // (plan P3-01; T4 CascadeState) its scope: the rule-free projection of
  // its style — the style deltas around it and its own, no rule — which the
  // semantic page writes (the rules reach it as CSS) and lifting carries.
  // Everything else reads `style`.
  StyleId scope = 0;
  // (plan P3-03; design T3 SemInfo.synthetic) made by a site: a title's
  // clone (a TOC entry) skips it
  bool synthetic = false;
  // (plan P3-03; design T3 SemInfo.number) its marker as MATERIALIZE wrote
  // it — an ordered list item's counter-backed number, a notes entry's —
  // which the box tree draws; 0: none (dumps omit it)
  StrRef number = 0;
  // (plan P3-04; design T3 SemInfo.targetAnchor) the label whose anchor a
  // resolved reference or an internal link points at — the serializers
  // spell its href (AnchorNamer); 0: none (unresolved, or an external URL)
  StrRef anchorTo = 0;
  // (plan P3-13; design T3 Placement::Deferred) a deferred flow's entry: an
  // insert of the page its marker (at this source position) stands on;
  // kInsertArea: the rest of the flow's wrap (the inserts' separator)
  u32 insertAt = kNotInsert;
};

// Typed attribute accessors (plan P0-06). Values that came through the ops
// reader are already inside their schema domain; nodes the engine builds are
// trusted. attrInt still clamps to i32, so no consumer ever converts an
// unchecked double.
inline const ArgVal* attr(const ContentNode* n, ArgK k) {
  for (const ArgVal& a : n->args)
    if (a.key == k) return &a;
  return nullptr;
}
inline i32 attrInt(const ContentNode* n, ArgK k, i32 dflt) {
  const ArgVal* a = attr(n, k);
  if (!a) return dflt;
  if (a->tag == ArgTag::Bool) return a->num != 0 ? 1 : 0;
  if (a->tag != ArgTag::Num || !(a->num == a->num)) return dflt;
  if (a->num <= -2147483648.0) return INT32_MIN;
  if (a->num >= 2147483647.0) return INT32_MAX;
  return (i32)a->num;
}
inline double attrNum(const ContentNode* n, ArgK k, double dflt) {
  const ArgVal* a = attr(n, k);
  return (a && a->tag == ArgTag::Num) ? a->num : dflt;
}
inline bool attrBool(const ContentNode* n, ArgK k, bool dflt) {
  const ArgVal* a = attr(n, k);
  return (a && a->tag == ArgTag::Bool) ? a->num != 0 : dflt;
}
inline StrRef attrStr(const ContentNode* n, ArgK k) {
  const ArgVal* a = attr(n, k);
  return (a && a->tag == ArgTag::Str) ? a->ref : 0;
}

// EXT data (plan P2-05): the value of a node's EXT attribute `name`, or null
inline const ArgVal* extAttr(const ContentNode* n, StrRef name) {
  for (const ArgVal& a : n->args)
    if (a.key == ArgK::ext && a.name == name) return &a;
  return nullptr;
}

// (plan P3-14) an enum attribute's value as its place in the kind's
// declared domain (schema "enum:a|b|c": 1, 2, 3); 0 when absent or not one
// of them — layers read enum arguments by place, never by spelling
inline int attrEnum(const ContentNode* n, ArgK k, const Interner& strs) {
  const StrRef v = attrStr(n, k);
  if (!v) return 0;
  const KindInfo& ki = kKinds[(u16)n->kind];
  for (u8 i = 0; i < ki.nAttrs; i++) {
    const AttrSpec& a = ki.attrs[i];
    if (a.key != (u16)k || a.dom != Dom::Enum) continue;
    const std::string_view s = strs.get(v);
    for (u8 m = 0; m < a.nMembers; m++)
      if (s == a.members[m]) return m + 1;
  }
  return 0;
}

// (plan P3-14; design T6 SizeSpec) a table's tracks attribute (domain
// "tracks": width[:align] by commas) as its columns' declarations
struct TrackDecl {
  enum class K : u8 { Fr, Fixed, Percent, Auto, Min, Max } k = K::Fr;
  float v = 1;  // Fr: its share; Percent: its fraction of the measure
  Len len;      // Fixed
  u8 align = 'l';
};
inline std::vector<TrackDecl> parseTracks(std::string_view v) {
  std::vector<TrackDecl> out;
  for (size_t at = 0; at <= v.size();) {
    size_t comma = v.find(',', at);
    if (comma == std::string_view::npos) comma = v.size();
    std::string_view e = v.substr(at, comma - at);
    at = comma + 1;
    if (e.empty()) continue;
    TrackDecl t;
    if (const size_t colon = e.find(':'); colon != std::string_view::npos) {
      if (colon + 1 < e.size()) t.align = (u8)e[colon + 1];
      e = e.substr(0, colon);
    }
    auto num = [&](size_t suffix) { return std::strtof(std::string(e.substr(0, e.size() - suffix)).c_str(), nullptr); };
    constexpr std::string_view kAuto = "auto", kMin = "min", kMax = "max", kFr = "fr";
    if (e == kAuto) t.k = TrackDecl::K::Auto;
    else if (e == kMin) t.k = TrackDecl::K::Min;
    else if (e == kMax) t.k = TrackDecl::K::Max;
    else if (e.size() > 2 && e.substr(e.size() - 2) == kFr) t.v = std::max(0.001f, num(2));
    else if (e.back() == '%') {
      t.k = TrackDecl::K::Percent;
      t.v = std::max(0.0001f, num(1) / 100.0f);
    } else {
      t.k = TrackDecl::K::Fixed;
      t.len = parseLen(e);
    }
    out.push_back(t);
  }
  return out;
}

// (plan P3-07; design T7 CopyPolicy) what copy takes of a node, from its
// universal `copy` / `syn` attributes (domain "copy": text | omit |
// replace:<text>); marked: it has either. A `syn` alone is synthetic text:
// omitted. syn: the data-syn kind (its `syn`, else its kind's name)
struct CopyAttr {
  enum class Mode : u8 { Text, Omit, Replace } mode = Mode::Text;
  bool marked = false;
  std::string_view replace;
  std::string_view syn;
};
inline CopyAttr copyAttr(const ContentNode* n, const Interner& strs) {
  CopyAttr c;
  const StrRef cp = attrStr(n, ArgK::copy), sy = attrStr(n, ArgK::syn);
  if (!cp && !sy) return c;
  c.marked = true;
  const std::string_view v = cp ? strs.get(cp) : std::string_view{};
  constexpr std::string_view kReplace = "replace:", kText = "text";
  if (v == kText) return c;
  c.syn = sy ? strs.get(sy) : std::string_view(kKinds[(u16)n->kind].name);
  if (v.substr(0, kReplace.size()) == kReplace) {
    c.mode = CopyAttr::Mode::Replace;
    c.replace = v.substr(kReplace.size());
  } else {
    c.mode = CopyAttr::Mode::Omit;
  }
  return c;
}

// The slot a node fills in its parent (plan P2-16; schema "slots"): None
// when its slot attribute names none; slotOn: whether a kind takes it
inline SlotId slotOf(const ContentNode* n, const Interner& strs) {
  const StrRef r = attrStr(n, ArgK::slot);
  if (!r) return SlotId::None;
  const std::string_view v = strs.get(r);
  for (u8 i = 1; i < SLOT_COUNT; i++)
    if (v == kSlots[i].name) return (SlotId)i;
  return SlotId::None;
}
inline bool slotOn(SlotId s, Kind parent) {
  if (s == SlotId::None) return false;
  const SlotInfo& si = kSlots[(u8)s];
  if (si.anyBlock) return (u16)parent < KIND_COUNT && kKinds[(u16)parent].level == Level::Block;
  for (u8 i = 0; i < si.nKinds; i++)
    if (si.kinds[i] == (u16)parent) return true;
  return false;
}

// A declaration (plan P2-05; design T2 S9): its type (kDecls), name, EXT
// data and style-neutral templates, at its flow position (the EMITs before
// it). Hoisted types: the last declaration of a name wins and the earlier
// ones are superseded (decl-redeclared); positional ones apply in order.
struct Decl {
  u16 type = 0;
  StrRef name = 0;
  std::vector<ArgVal> args;  // EXT data (names and strings interned)
  std::vector<ContentNode*> templates;
  u32 flowIndex = 0;
  Span span;
  bool superseded = false;
};

struct ContentTree {
  ContentNode* root = nullptr;  // kind doc; children = pid-bearing blocks
  std::vector<Decl> decls;      // in document order
};

// InstLimits (plan P0-07): the instantiated tree holds at most
// max(kInstMinBudget, kInstPerRawNode × raw nodes) nodes, nested at most 256
// deep. HostOnly configuration once the settings ABI exists (P1-03).
constexpr size_t kInstMinBudget = 262144;
constexpr size_t kInstPerRawNode = 64;

// EMIT walk with emission-time style resolution; DAG values are copied per
// emission (document-model §3).
class Registry;
class Cascade;
class NodePropsTable;
// `reg` decides each node's class (its nearest classed ancestor included);
// `cascade` (plan P3-01) folds the rules in force into each node's style and
// block properties (`props`).
ContentTree instantiate(const RawOps& raw, Arena& arena, Interner& strs, StyleTable& styles, NodePropsTable& props,
                        Cascade& cascade, DiagSink& diags, const Registry& reg);

std::string dumpTree(const ContentTree& t, const Interner& strs, const StyleTable& styles);

// Level class of a kind (schema.json "level"): what a node may stand next to.
inline Level levelOf(Kind k) {
  return (u16)k < KIND_COUNT ? kKinds[(u16)k].level : Level::Block;
}
// Inline content: inline, transparent (styled, seq) and trivia (comment).
inline bool isInlineLevel(Kind k) {
  Level l = levelOf(k);
  return l == Level::Inline || l == Level::Transparent || l == Level::Trivia;
}

// Normal form (plans P0-07, P2-11; normalize.cc has the rules N1–N6): run on
// the instantiated tree — positions, fallbacks, anonymous paragraphs, model
// checks; diagnoses block-in-inline without splitting it. Returns how many
// nodes it made (unfolded: model/cascade.h settleMade)
size_t normalize(ContentNode* n, Arena& arena, Interner& strs, DiagSink& diags);

}  // namespace tsr
