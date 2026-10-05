#include "numbering.h"

namespace tsr {

int Counters::levelOf(u16 c, const ContentNode* n) const {
  const CounterDef& d = reg_.counters[c];
  if (!d.byLevel) return 1;
  int level = attrInt(n, d.levelArg, 1);
  return level < 1 ? 1 : level > d.depth ? d.depth : level;
}

std::string Counters::step(u16 c, int level) {
  const CounterDef& d = reg_.counters[c];
  std::vector<int>& v = v_[c];
  size_t depth = d.byLevel ? (size_t)level : 1;
  // a skipped level counts 0 (1.0.1) or 1 (1.1.1); the new level starts at 0
  if (v.size() < depth) {
    v.resize(depth - 1, d.gapOne ? 1 : 0);
    v.push_back(0);
  }
  v.resize(depth);
  v[depth - 1]++;
  std::string out;
  for (size_t i = 0; i < v.size(); i++) {
    if (i) out += '.';
    out += std::to_string(v[i]);
  }
  return out;
}

int Counters::keyed(u16 c, const std::string& key) {
  Keyed& k = keyed_[c];
  auto it = k.ord.find(key);
  if (it != k.ord.end()) return it->second;
  int n = (int)k.order.size() + 1;
  k.ord.emplace(key, n);
  k.order.push_back(key);
  return n;
}

int Counters::keyedIfSeen(u16 c, const std::string& key) const {
  auto it = keyed_[c].ord.find(key);
  return it == keyed_[c].ord.end() ? 0 : it->second;
}

u16 Counters::counterNamed(std::string_view name) const {
  for (size_t k = 0; k < reg_.counters.size(); k++)
    if (reg_.counters[k].name == name) return (u16)k;
  return kNoIndex;
}

}  // namespace tsr
