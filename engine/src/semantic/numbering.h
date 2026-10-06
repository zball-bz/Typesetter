// Counters (plan P1-10, P2-07; design T3 Counters + NumberingPattern): one
// automaton for every counter — flat or by level (depth from a node argument,
// a skipped level counting 0 or 1), keyed (one step per distinct key, the
// citation ordinals), numbered within another counter (its components first,
// restarted by its steps), formatted by a NumberingPattern — and positional
// events (counterUpdate: set, step, add, a new pattern, a supplement).
//
// NumberingPattern, the engine's one parsed mini-language (Typst-compatible):
// counting symbols 1 a A i I ① 一 * and {name} (a declared counter system);
// everything before the first symbol is a prefix, between symbols a
// separator, after the last a suffix. A number with more components than the
// pattern has symbols repeats the last symbol and separator: "1.1" formats
// [2, 10] as 2.10, "A.1" [1, 3] as A.3, "(1a)" [1, 2] as (1b), "1." [3] as 3.
#pragma once
#include <unordered_map>

#include "../elements/registry.h"

namespace tsr {

// a formatted number in a pattern ("" = 1.1)
std::string formatNumber(std::string_view pattern, const std::vector<int>& comps, const Registry& reg);

class Counters {
 public:
  explicit Counters(const Registry& reg);
  // steps counter c (at `level` for a by-level counter); the formatted number
  std::string step(u16 c, int level);
  // the level a node steps a by-level counter at (clamped into 1..depth)
  int levelOf(u16 c, const ContentNode* n) const;
  // a keyed counter's ordinal for `key`, stepping on its first use
  int keyed(u16 c, const std::string& key);
  int keyedIfSeen(u16 c, const std::string& key) const;
  const std::vector<std::string>& keyOrder(u16 c) const { return keyed_[c].order; }
  u16 counterNamed(std::string_view name) const;

  // a positional event (plan P2-07): set the values (a dot or comma list),
  // step at a level, add to the last component, change the pattern, change
  // the supplement the counter's instances read (empty fields: unchanged)
  struct Event {
    std::string set, pattern;
    int step = 0, add = 0;
    Supplement supplement;
  };
  void apply(u16 c, const Event& ev);
  // the supplement an event set for counter c (none: the class's)
  const Supplement* supplementOf(u16 c) const { return sup_[c].set() ? &sup_[c] : nullptr; }

 private:
  struct Keyed {
    std::unordered_map<std::string, int> ord;
    std::vector<std::string> order;
  };
  std::string format(u16 c) const;
  std::string formatComps(u16 c, const std::vector<int>& comps) const;
  void restartWithin(u16 c, int level);
  const Registry& reg_;
  std::vector<std::vector<int>> v_;
  std::vector<std::string> pattern_;  // the current pattern of each counter
  std::vector<Supplement> sup_;
  std::vector<Keyed> keyed_;
};

}  // namespace tsr
