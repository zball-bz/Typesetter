// Counters (plan P1-10; design T3 Counters + NumberingPattern): one automaton
// for every counter — flat or by level (depth from a node argument, a
// skipped level counting 0 or 1), or keyed (one step per distinct key, the
// citation ordinals). Numbers format as decimal components joined by '.'
// (the NumberingPattern mini-language arrives with declared counters, P2-07).
#pragma once
#include <unordered_map>

#include "../elements/registry.h"

namespace tsr {

class Counters {
 public:
  explicit Counters(const Registry& reg) : reg_(reg), v_(reg.counters.size()), keyed_(reg.counters.size()) {}
  // steps counter c (at `level` for a by-level counter); the formatted number
  std::string step(u16 c, int level);
  // the level a node steps a by-level counter at (clamped into 1..depth)
  int levelOf(u16 c, const ContentNode* n) const;
  // a keyed counter's ordinal for `key`, stepping on its first use
  int keyed(u16 c, const std::string& key);
  int keyedIfSeen(u16 c, const std::string& key) const;
  const std::vector<std::string>& keyOrder(u16 c) const { return keyed_[c].order; }
  u16 counterNamed(std::string_view name) const;

 private:
  struct Keyed {
    std::unordered_map<std::string, int> ord;
    std::vector<std::string> order;
  };
  const Registry& reg_;
  std::vector<std::vector<int>> v_;
  std::vector<Keyed> keyed_;
};

}  // namespace tsr
