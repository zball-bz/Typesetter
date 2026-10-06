#include "manifest.h"

#include <algorithm>

#include "../support/json.h"

namespace tsr {

namespace {
constexpr size_t kMaxManifests = 4096, kMaxLabels = 1u << 20, kMaxGroups = 16, kMaxValues = 16;

bool str(const JsonValue* v, std::string& out, size_t max = 4096) {
  if (!v || v->t != JsonValue::T::Str || v->str.size() > max) return false;
  out = v->str;
  return true;
}
bool num(const JsonValue* v, int& out) {
  if (!v || v->t != JsonValue::T::Num || !(v->num >= -1e9 && v->num <= 1e9) || v->num != (double)(int)v->num) return false;
  out = (int)v->num;
  return true;
}
}  // namespace

bool decodeLabelManifests(std::string_view json, std::string_view self, ExternalLabels& out, std::string& err) {
  out = {};
  JsonValue root;
  JsonReader rd;
  if (!rd.parse(json, root)) {
    err = std::string("not JSON: ") + (rd.error() ? rd.error() : "?");
    return false;
  }
  if (root.t != JsonValue::T::Arr || root.arr.size() > kMaxManifests) {
    err = "expected an array of manifests";
    return false;
  }
  ExternalLabels got;
  for (const JsonValue& m : root.arr) {
    int v = 0;
    std::string doc;
    const JsonValue* labels = m.t == JsonValue::T::Obj ? m.get("labels") : nullptr;
    if (!num(m.get("v"), v) || v != 1 || !str(m.get("doc"), doc) || !labels || labels->t != JsonValue::T::Arr ||
        labels->arr.size() > kMaxLabels) {
      err = "a manifest is {v: 1, doc, totals, labels: [...]}";
      return false;
    }
    if (doc == self) continue;  // (its own: the document's own labels)
    for (const JsonValue& l : labels->arr) {
      ExternalLabel x;
      std::string label;
      x.doc = doc;
      const JsonValue* number = l.t == JsonValue::T::Obj ? l.get("number") : nullptr;
      if (!str(l.get("label"), label, 256) || label.empty() || !str(l.get("class"), x.cls, 256) ||
          !num(l.get("level"), x.level) || !str(l.get("title"), x.title) || !str(l.get("anchor"), x.anchor, 256) ||
          !number || number->t != JsonValue::T::Arr || number->arr.size() > kMaxGroups) {
        err = "a label is {label, class, level, number: [[counter, [n…]]…], title, anchor}";
        return false;
      }
      for (const JsonValue& g : number->arr) {
        std::string counter;
        if (g.t != JsonValue::T::Arr || g.arr.size() != 2 || !str(&g.arr[0], counter, 256) ||
            g.arr[1].t != JsonValue::T::Arr || g.arr[1].arr.size() > kMaxValues) {
          err = "a number group is [counter, [n…]]";
          return false;
        }
        std::vector<int> vals;
        for (const JsonValue& n : g.arr[1].arr) {
          int k = 0;
          if (!num(&n, k)) {
            err = "a number component is an integer";
            return false;
          }
          vals.push_back(k);
        }
        x.number.push_back({std::move(counter), std::move(vals)});
      }
      got.byLabel.emplace(std::move(label), std::move(x));  // (the first wins)
    }
  }
  out = std::move(got);
  return true;
}

bool parseProjectStarts(std::string_view json, ProjectStarts& out) {
  out.clear();
  if (json.empty()) return true;
  JsonValue root;
  JsonReader rd;
  if (!rd.parse(json, root) || root.t != JsonValue::T::Obj) return false;
  for (size_t i = 0; i < root.keys.size(); i++) {
    const JsonValue& d = root.vals[i];
    if (d.t != JsonValue::T::Obj) return false;
    auto& row = out[root.keys[i]];
    for (size_t k = 0; k < d.keys.size(); k++) {
      int n = 0;
      if (!num(&d.vals[k], n)) return false;
      row[d.keys[k]] = n;
    }
  }
  return true;
}

std::string labelsProduct(const Index& ix, const Registry& reg, std::string_view doc) {
  std::string out = "{\"v\":1,\"doc\":";
  jsonString(out, doc);
  out += ",\"totals\":{";
  std::vector<std::pair<std::string, int>> totals = ix.totals;
  std::sort(totals.begin(), totals.end());
  for (size_t i = 0; i < totals.size(); i++) {
    if (i) out += ',';
    jsonString(out, totals[i].first);
    appendf(out, ":%d", totals[i].second);
  }
  out += "},\"labels\":[";
  std::vector<std::string> labels;
  for (const auto& [l, t] : ix.labels) labels.push_back(l);
  std::sort(labels.begin(), labels.end());
  bool first = true;
  for (const std::string& l : labels) {
    const LabelTarget& t = ix.labels.at(l);
    out += first ? "\n" : ",\n";
    first = false;
    out += "{\"label\":";
    jsonString(out, l);
    if (t.k == LabelTarget::K::Plain || t.inst >= ix.instances.size()) {
      out += ",\"class\":\"\",\"level\":1,\"number\":[],\"title\":\"\",\"anchor\":";
      jsonString(out, l);
      out += '}';
      continue;
    }
    const Instance& in = ix.instances[t.inst];
    out += ",\"class\":";
    jsonString(out, reg.cls(in.cls).name);
    appendf(out, ",\"level\":%d,\"number\":[", in.level);
    // start-independent: its own start taken off each group's first value
    for (size_t g = 0; g < in.groups.size(); g++) {
      const std::string& counter = reg.counters[in.groups[g].first].name;
      std::vector<int> vals = in.groups[g].second;
      if (auto it = ix.starts.find(counter); it != ix.starts.end() && !vals.empty()) vals[0] -= it->second;
      out += g ? ",[" : "[";
      jsonString(out, counter);
      out += ",[";
      for (size_t k = 0; k < vals.size(); k++) appendf(out, "%s%d", k ? "," : "", vals[k]);
      out += "]]";
    }
    out += "],\"title\":";
    jsonString(out, in.title);
    out += ",\"anchor\":";  // (a reference links to its label's id, as a local one does)
    jsonString(out, l);
    out += '}';
  }
  out += "\n]}\n";
  return out;
}

}  // namespace tsr
