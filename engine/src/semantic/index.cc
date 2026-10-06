#include "index.h"
#include "../math/env.h"

namespace tsr {

const Row* Index::row(std::string_view table, std::string_view key) const {
  std::string k(table);
  k += '\0';
  k += key;
  auto it = rowOf.find(k);
  return it == rowOf.end() ? nullptr : &rows[it->second];
}

std::vector<u32>& Index::flow(const std::string& name) {
  for (auto& f : flows)
    if (f.first == name) return f.second;
  flows.push_back({name, {}});
  return flows.back().second;
}

const std::vector<u32>* Index::flowItems(std::string_view name) const {
  for (const auto& f : flows)
    if (f.first == name) return &f.second;
  return nullptr;
}

std::vector<std::string> splitKeys(std::string_view target) {
  std::vector<std::string> keys;
  size_t at = 0;
  while (at <= target.size()) {
    size_t comma = target.find(',', at);
    if (comma == std::string_view::npos) comma = target.size();
    std::string k(target.substr(at, comma - at));
    while (!k.empty() && k.front() == ' ') k.erase(k.begin());
    while (!k.empty() && k.back() == ' ') k.pop_back();
    if (!k.empty()) keys.push_back(k);
    at = comma + 1;
  }
  return keys;
}

void excerptInto(const ContentNode* n, const Interner& strs, std::string& out) {
  if (n->kind == Kind::comment) return;
  // a formula is its source (plan P2-15, interim: TOC and reference text
  // keep it; structured excerpts that clone it are T3's)
  if (n->kind == Kind::mathinline || n->kind == Kind::mathblock) {
    out += mathSource(n, strs).copy;
    return;
  }
  if (n->kind == Kind::text) {
    out += strs.get(n->str);
    return;
  }
  for (const ContentNode* k : n->kids) excerptInto(k, strs, out);
}

namespace {

struct Locator {
  const Registry& reg;
  Counters& counters;
  const Interner& strs;
  Index& ix;
  DiagSink& diags;
  std::vector<u32> encl;  // the instances around the node visited, innermost last
  std::vector<u32> ordinals = std::vector<u32>(reg.classes.size(), 0);  // per class (Body::Ordinal)

  bool addLabel(const std::string& label, LabelTarget t, Span span) {
    if (ix.labels.count(label)) {
      diags.add(Sev::Warning, "label-duplicate", span, "label '" + label + "' declared twice (first wins)");
      return false;
    }
    ix.labels.emplace(label, t);
    ix.labelOrder.push_back(label);
    return true;
  }
  // a user label: a shape some alias rule mints, or a second declaration,
  // is refused (and dropped from its node, so no id is emitted twice)
  bool userLabel(const ContentNode* n, const std::string& label, LabelTarget t) {
    if (reg.reservedShape(label)) {
      diags.add(Sev::Warning, "label-reserved", n->span,
                "label '" + label + "' has the shape of a generated anchor and is ignored");
      ix.refused.insert(n);
      return false;
    }
    if (!addLabel(label, t, n->span)) {
      ix.refused.insert(n);
      return false;
    }
    return true;
  }
  std::string alias(const AliasRule& a, const std::string& number, const std::string& key, u32 ordinal) {
    return a.prefix + (a.body == AliasRule::Body::Number    ? number
                       : a.body == AliasRule::Body::Ordinal ? std::to_string(ordinal)
                                                            : key);
  }

  void instance(const ContentNode* n, ClassId c) {
    const ElementClass& C = reg.cls(c);
    Instance in;
    in.cls = c;
    in.node = n;
    in.span = n->span;
    u32 id = (u32)ix.instances.size();
    const u32 ordinal = ++ordinals[c];
    in.parent = encl.empty() ? kNoInst : encl.back();
    if (C.counter != kNoIndex) in.level = counters.levelOf(C.counter, n);
    // (plan P3-13) a per-level outline class (a part, a chapter: a counter
    // of its own) stands at its node's level
    if (C.outline && (C.counter == kNoIndex || !reg.counters[C.counter].byLevel)) in.level = attrInt(n, ArgK::level, 1);
    in.supplement = C.supplement;
    if (C.counter != kNoIndex)
      if (const Supplement* s = counters.supplementOf(C.counter)) in.supplement = *s;
    const bool counted = C.counter != kNoIndex;  // (the loader refuses a numbered class without one)
    if (C.numbering == ElementClass::Numbering::Always && counted) {
      in.number = counters.step(C.counter, in.level);
      in.groups = counters.groups(C.counter);  // (plan P3-31)
    }
    if (C.title == ElementClass::Title::Text) {
      excerptInto(n, strs, in.title);
      in.titleNode = n;
    }
    if (C.title == ElementClass::Title::Arg) in.title = strs.get(attrStr(n, C.titleArg));
    if (C.title == ElementClass::Title::Part)  // (plan P3-13) its part's content: a figure's caption
      for (const ContentNode* k : n->kids)
        if (slotOf(k, strs) == C.titlePart) {
          excerptInto(k, strs, in.title);
          in.titleNode = k;
          break;
        }
    if (C.title == ElementClass::Title::Ext)
      for (const ArgVal& a : n->args)
        if (a.key == ArgK::ext && a.tag == ArgTag::Str && strs.get(a.name) == C.titleExt) in.title = strs.get(a.ref);
    LabelTarget self{LabelTarget::K::Instance, id, n->span};
    // (plan P3-03) a class that refers to its enclosing instance: its label
    // names that one (a table in a figure: @tab reads the figure's number)
    if (C.refersTo == ElementClass::RefersTo::Enclosing && !encl.empty()) self.inst = encl.back();
    switch (C.labels) {
      case ElementClass::Labels::User: {
        std::string l(strs.get(attrStr(n, ArgK::label)));
        if (!l.empty() && userLabel(n, l, self)) {
          in.label = l;
          if (C.numbering == ElementClass::Numbering::Labelled && counted) {
            in.number = counters.step(C.counter, in.level);
            in.groups = counters.groups(C.counter);  // (plan P3-31)
          }
        }
        break;
      }
      case ElementClass::Labels::FromArg: {
        std::string l(strs.get(attrStr(n, C.labelArg)));
        if (!l.empty()) {
          in.label = l;
          addLabel(l, self, n->span);
        }
        break;
      }
      case ElementClass::Labels::None:
        break;
    }
    if (in.label.empty() && C.alias.body != AliasRule::Body::None) {
      in.label = alias(C.alias, in.number, in.label, ordinal);
      in.aliased = true;
      addLabel(in.label, self, n->span);
    }
    if (C.flow) {
      in.markerAlias = alias(C.flow->markerAlias, in.number, in.label, ordinal);
      addLabel(in.markerAlias, {LabelTarget::K::Marker, id, n->span}, n->span);
      ix.flow(C.flow->name).push_back(id);
    }
    // a table row: keyed by its row-key argument (a citation key: the first
    // row of a key wins), else by its label
    std::string key = C.rowKeyed ? std::string(strs.get(attrStr(n, C.rowKey)))
                      : C.labels == ElementClass::Labels::FromArg ? in.label : std::string();
    if (!C.table.empty() && !key.empty() && !(C.rowKeyed && !C.multi && ix.row(C.table, key))) {
      Row r;
      r.table = C.table;
      r.key = key;
      r.sortKey = strs.get(attrStr(n, ArgK::sortKey));
      r.node = n;
      r.span = n->span;
      r.inst = id;
      r.title = in.title;
      for (const ContentNode* k : n->kids) excerptInto(k, strs, r.bodyText);
      ix.rowOf.emplace(C.table + '\0' + key, (u32)ix.rows.size());  // (a multi table: its first)
      ix.rows.push_back(std::move(r));
    }
    ix.instOf[n] = id;
    ix.instances.push_back(std::move(in));
  }

  // a counter event (plan P2-07), applied where it stands in pre-order
  void event(const ContentNode* n) {
    std::string name(strs.get(attrStr(n, ArgK::counter)));
    u16 c = counters.counterNamed(name);
    if (c == kNoIndex) {
      diags.add(Sev::Warning, "event-counter", n->span, "counterUpdate: no counter '" + name + "'");
      return;
    }
    Counters::Event ev;
    ev.set = strs.get(attrStr(n, ArgK::set));
    ev.step = attrInt(n, ArgK::step, 0);
    ev.add = attrInt(n, ArgK::add, 0);
    ev.pattern = strs.get(attrStr(n, ArgK::numbering));
    if (StrRef sup = attrStr(n, ArgK::supplement)) {
      JsonValue v;
      JsonReader rd;
      if (!rd.parse(strs.get(sup), v) || !parseSupplement(v, ev.supplement))
        diags.add(Sev::Warning, "event-supplement", n->span, "counterUpdate: a supplement is a term, a text or texts by language");
    }
    counters.apply(c, ev);
  }

  void visit(const ContentNode* n) {
    if (n->cls) {
      instance(n, n->cls);
      encl.push_back((u32)ix.instances.size() - 1);
      counters.enterScope(n->cls, n, strs);  // its scoped counters (an ordered list's items)
      for (const ContentNode* k : n->kids) visit(k);
      counters.leaveScope(n->cls);
      encl.pop_back();
      return;
    } else if (n->kind == Kind::event) {
      event(n);
      return;
    } else if (n->kind == Kind::entry && !n->cls) {
      diags.add(Sev::Warning, "entry-unclassed", n->span,
                "an entry of no element class (role '" + std::string(strs.get(attrStr(n, ArgK::role))) +
                    "') is collected nowhere");
    } else if (n->kind == Kind::collect) {
      // a collector node holds no labels of its own; its place (a scoped
      // collector's section)
      ix.collectAt[n] = (u32)ix.instances.size();
    } else {
      std::string l(strs.get(attrStr(n, ArgK::label)));
      if (!l.empty()) userLabel(n, l, {LabelTarget::K::Plain, kNoInst, n->span});
    }
    for (const ContentNode* k : n->kids) visit(k);
  }
};

}  // namespace

void locate(const ContentNode* root, const Registry& reg, Counters& counters, const Interner& strs,
            Index& ix, DiagSink& diags) {
  if (!root) return;
  Locator l{reg, counters, strs, ix, diags};
  l.visit(root);
}

u32 keyedScope(const Registry& reg, const Index& ix, u16 c, const std::vector<u32>& encl) {
  const ClassId sc = reg.counters[c].scope;
  if (sc)
    for (size_t i = encl.size(); i-- > 0;)
      if (ix.instances[encl[i]].cls == sc) return encl[i];
  return kDocScope;
}

namespace {

struct Binder {
  const Registry& reg;
  Counters& counters;
  const Interner& strs;
  Index& ix;
  DiagSink& diags;
  std::vector<u32> encl;

  // another marker of a flow item (a named note's): an occurrence, anchored
  // by the item's marker alias and its place among them
  void occurrence(const ContentNode* n, const std::string& target) {
    auto it = ix.labels.find(target);
    if (it == ix.labels.end() || it->second.k != LabelTarget::K::Instance) return;
    Instance& in = ix.instances[it->second.inst];
    if (!reg.cls(in.cls).flow || in.markerAlias.empty()) return;
    const u32 k = (u32)in.occurrences.size() + 2;
    std::string label = in.markerAlias + "." + std::to_string(k);
    if (ix.labels.count(label)) {
      diags.add(Sev::Warning, "label-duplicate", n->span, "label '" + label + "' declared twice (first wins)");
      return;
    }
    ix.labels.emplace(label, LabelTarget{LabelTarget::K::Marker, it->second.inst, n->span});
    ix.labelOrder.push_back(label);
    in.occurrences.push_back(std::move(label));
    ix.occurrenceOf[n] = {it->second.inst, k};
  }

  void visit(const ContentNode* n) {
    if (n->kind == Kind::ref) {
      std::string target(strs.get(attrStr(n, ArgK::target)));
      if (!ix.labels.count(target)) {
        for (const std::string& k : splitKeys(target))
          for (const CollectorDef& c : reg.collectors)
            if (c.citeable && ix.row(c.table, k)) {
              counters.keyed(c.rowCounter, k, keyedScope(reg, ix, c.rowCounter, encl));
              break;
            }
      } else if (strs.get(attrStr(n, ArgK::form)) == "marker") {
        occurrence(n, target);
      }
    }
    if (n->kind == Kind::collect) return;  // a collector's rows are not citations
    auto in = n->cls ? ix.instOf.find(n) : ix.instOf.end();
    if (in != ix.instOf.end()) encl.push_back(in->second);
    for (const ContentNode* k : n->kids) visit(k);
    if (in != ix.instOf.end()) encl.pop_back();
  }
};

}  // namespace

void bind(const ContentNode* root, const Registry& reg, Counters& counters, const Interner& strs, Index& ix,
          DiagSink& diags) {
  if (!root) return;
  Binder b{reg, counters, strs, ix, diags, {}};
  b.visit(root);
}

std::string dumpIndex(const Index& ix, const Registry& reg) {
  std::string out;
  for (const Instance& in : ix.instances) {
    const ElementClass& C = reg.cls(in.cls);
    appendf(out, "instance %s", C.name.c_str());
    if (!in.number.empty()) out += " " + in.number;
    if (C.counter != kNoIndex && reg.counters[C.counter].byLevel) appendf(out, " level=%d", in.level);
    if (!in.label.empty()) out += " label=" + in.label;
    if (!in.markerAlias.empty()) out += " marker=" + in.markerAlias;
    for (const std::string& o : in.occurrences) out += " occurrence=" + o;
    if (!in.title.empty()) {
      out += " title=\"";
      appendEscaped(out, in.title);
      out += "\"";
    }
    appendf(out, " @[%u,%u)\n", in.span.start, in.span.end);
  }
  for (const std::string& l : ix.labelOrder) {
    const LabelTarget& t = ix.labels.at(l);
    out += "label " + l + " -> ";
    switch (t.k) {
      case LabelTarget::K::Instance:
      case LabelTarget::K::Marker: {
        const Instance& in = ix.instances[t.inst];
        out += reg.cls(in.cls).name;
        if (!in.number.empty()) out += " " + in.number;
        if (t.k == LabelTarget::K::Marker) out += " (marker)";
        break;
      }
      case LabelTarget::K::Plain:
        out += "(unnumbered)";
        break;
    }
    appendf(out, " @[%u,%u)\n", t.span.start, t.span.end);
  }
  for (const Row& r : ix.rows) {
    out += "row " + r.table + " " + r.key;
    if (r.ordinal) appendf(out, " ordinal=%d", r.ordinal);
    appendf(out, " @[%u,%u)\n", r.span.start, r.span.end);
  }
  for (const auto& [name, items] : ix.flows) {
    out += "flow " + name + ":";
    for (u32 i : items) out += " " + ix.instances[i].label;
    out += "\n";
  }
  return out;
}

}  // namespace tsr
