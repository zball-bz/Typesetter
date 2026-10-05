#include "index.h"

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
  std::string alias(const AliasRule& a, const std::string& number, const std::string& key) {
    return a.prefix + (a.body == AliasRule::Body::Number ? number : key);
  }

  void instance(const ContentNode* n, ClassId c) {
    const ElementClass& C = reg.cls(c);
    Instance in;
    in.cls = c;
    in.node = n;
    in.span = n->span;
    u32 id = (u32)ix.instances.size();
    if (C.counter != kNoIndex) in.level = counters.levelOf(C.counter, n);
    if (C.numbering == ElementClass::Numbering::Always) in.number = counters.step(C.counter, in.level);
    if (C.title == ElementClass::Title::Text) excerptInto(n, strs, in.title);
    if (C.title == ElementClass::Title::Arg) in.title = strs.get(attrStr(n, C.titleArg));
    LabelTarget self{LabelTarget::K::Instance, id, n->span};
    switch (C.labels) {
      case ElementClass::Labels::User: {
        std::string l(strs.get(attrStr(n, ArgK::label)));
        if (!l.empty() && userLabel(n, l, self)) {
          in.label = l;
          if (C.numbering == ElementClass::Numbering::Labelled)
            in.number = counters.step(C.counter, in.level);
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
      in.label = alias(C.alias, in.number, in.label);
      in.aliased = true;
      addLabel(in.label, self, n->span);
    }
    if (C.flow) {
      in.markerAlias = alias(C.flow->markerAlias, in.number, in.label);
      addLabel(in.markerAlias, {LabelTarget::K::Marker, id, n->span}, n->span);
      ix.flow(C.flow->name).push_back(id);
    }
    if (!C.table.empty() && C.labels == ElementClass::Labels::FromArg && !in.label.empty()) {
      Row r;
      r.table = C.table;
      r.key = in.label;
      r.node = n;
      r.span = n->span;
      r.inst = id;
      r.title = in.title;
      for (const ContentNode* k : n->kids) excerptInto(k, strs, r.bodyText);
      ix.rows.push_back(std::move(r));
    }
    ix.instOf[n] = id;
    ix.instances.push_back(std::move(in));
  }

  // a collector whose rows are its own children (a bibliography's entries)
  void rows(const ContentNode* n) {
    const CollectorDef* c = reg.collector(strs.get(attrStr(n, ArgK::what)));
    if (!c || !c->rowsFromKids) return;
    for (const ContentNode* k : n->kids) {
      std::string key(strs.get(attrStr(k, c->rowKey)));
      if (key.empty() || ix.row(c->table, key)) continue;  // the first entry wins
      Row r;
      r.table = c->table;
      r.key = key;
      r.node = k;
      r.span = k->span;
      ix.rowOf[c->table + '\0' + key] = (u32)ix.rows.size();
      ix.rows.push_back(std::move(r));
    }
  }

  void visit(const ContentNode* n) {
    if (n->cls) {
      instance(n, n->cls);
    } else if (n->kind == Kind::collect) {
      rows(n);  // a collector node holds no labels of its own
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

void bindCites(const ContentNode* root, const Registry& reg, Counters& counters, const Interner& strs,
               const Index& ix) {
  if (!root) return;
  if (root->kind == Kind::ref) {
    std::string target(strs.get(attrStr(root, ArgK::target)));
    if (!ix.labels.count(target))
      for (const std::string& k : splitKeys(target))
        for (const CollectorDef& c : reg.collectors)
          if (c.citeable && ix.row(c.table, k)) {
            counters.keyed(c.rowCounter, k);
            break;
          }
  }
  if (root->kind == Kind::collect) return;  // a collector's rows are not citations
  for (const ContentNode* k : root->kids) bindCites(k, reg, counters, strs, ix);
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
