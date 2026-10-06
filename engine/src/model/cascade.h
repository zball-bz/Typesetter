// The cascade (plan P3-01; design T4 "ScopeDelta, Rules", "Cascade"; D-T03,
// D-T08): one fold that computes, at emission time, a node's run style and
// block properties from its parent's state, the rules in force and its own
// delta.
//
// A rule is a selector and a patch (styled attributes). Rules come, lowest
// first, from the engine's defaults (engine/data/defaults.json, env 0), the
// host (settings style.rules, env 1), `$.set` pushes (the schedule stack, in
// push order) and `style.where` nodes (outer to inner); a node's own delta —
// a styled node's attributes, its `style` — comes after them, and a host rule
// marked `force` after that. Per property the last value wins (computed-value
// semantics): a relative size applies once, against the parent's computed
// value; decorations OR; inheriting rows start from the parent's, the others
// from their initial value.
//
// Selectors read the node as it was emitted (kind, role, class tokens, its
// own attributes, its language, its depth among ancestors of its kind) —
// never what a rule set, so there are no cycles.
#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "model.h"

namespace tsr {

using PropsId = u32;
class NodePropsTable {
 public:
  NodePropsTable() { idOf(NodeProps{}); }  // id 0 = initial
  PropsId idOf(const NodeProps& p) {
    auto it = map_.find(p);
    if (it != map_.end()) return it->second;
    const PropsId id = (PropsId)props_.size();
    props_.push_back(p);
    map_.emplace(p, id);
    return id;
  }
  const NodeProps& get(PropsId id) const { return props_[id]; }

 private:
  std::vector<NodeProps> props_;
  std::unordered_map<NodeProps, PropsId, NodePropsHash> map_;
};

struct StyleSelector {
  u16 kind = 0xFFFF;  // any
  StrRef role = 0, cls = 0, lang = 0;  // lang: a BCP-47 prefix of the node's language
  u8 depth = 0;                        // 0 = any; else ancestors of its kind + 1
  std::vector<std::pair<ArgK, StrRef>> where;  // its own attributes, as text
};
struct StyleRule {
  StyleSelector sel;
  std::vector<ArgVal> patch;  // styled attributes; strings are document StrRefs
  bool force = false;         // a host rule over the node's own delta (D-T03)
};

using RuleEnvId = u32;  // 0: the base rules (defaults, host)

class Cascade {
 public:
  explicit Cascade(Interner& strs) : strs_(strs) {}

  // the base env (env 0 then env 1); the defaults read `settings` values
  // where a patch names one ({"setting": "code.scale", "unit": "em"})
  void setBase(std::vector<StyleRule> defaults, std::vector<StyleRule> host);
  // an env with one more rule after `env`'s (a $.set, a style.where)
  RuleEnvId extend(RuleEnvId env, StyleRule r);
  bool any() const { return !base_.empty() || !ext_.empty(); }
  bool usesDepth() const { return depthRules_; }

  // what the fold reads of a node
  struct NodeView {
    Kind kind;
    StrRef role = 0, cls = 0;
    const std::vector<ArgVal>* args = nullptr;  // its own attributes (document StrRefs)
    StrRef lang = 0;  // its language (inherited, or its own delta's)
    u8 depth = 1;
  };
  // a node's computed run style and block properties: from its parent's
  // (`style`, `props`), the rules of `env` that match it (and the base
  // rules unless !base: a declaration's style-neutral template), its own
  // delta (`own`: styled attributes, document StrRefs), then the forced host
  // rules
  void fold(Styling& st, NodeProps& props, const NodeView& n, RuleEnvId env, const std::vector<ArgVal>& own,
            bool base = true) const;

 private:
  bool matches(const StyleSelector& s, const NodeView& n) const;
  Interner& strs_;
  std::vector<StyleRule> base_;                  // env 0 then env 1
  std::vector<std::vector<u32>> byKind_;    // base rules by selector kind (+ one list for any kind, last)
  struct Ext {
    RuleEnvId parent;
    StyleRule rule;
  };
  std::vector<Ext> ext_;  // env id k + 1 = ext_[k]
  bool depthRules_ = false;
};

// a host setting's value as patch text ("" = unknown): the defaults name
// some ({"setting": "code.scale", "unit": "em"} → "0.85em")
using SettingText = std::function<std::string(std::string_view)>;
// rules from their JSON: an array of [selector, patch] or {"select",
// "patch", "force"}; a selector is a kind name or {kind, role, class, lang,
// depth, …where}; a patch is styled attributes by name. A malformed rule is
// a diagnostic (style-rule) and is dropped
std::vector<StyleRule> parseRules(std::string_view json, Interner& strs, DiagSink& diags, const char* origin,
                             const SettingText& setting = {});
// the engine's defaults (engine/data/defaults.json)
std::string_view defaultRulesJson();

}  // namespace tsr
