#include "cascade.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "../support/json.h"
#include "semantic_data.gen.h"

namespace tsr {

void Cascade::setBase(std::vector<StyleRule> defaults, std::vector<StyleRule> host) {
  base_ = std::move(defaults);
  for (StyleRule& r : host) base_.push_back(std::move(r));
  byKind_.assign(KIND_COUNT + 1, {});
  for (u32 i = 0; i < (u32)base_.size(); i++) {
    const u16 k = base_[i].sel.kind;
    byKind_[k < KIND_COUNT ? k : KIND_COUNT].push_back(i);
    depthRules_ = depthRules_ || base_[i].sel.depth;
  }
}

RuleEnvId Cascade::extend(RuleEnvId env, StyleRule r) {
  depthRules_ = depthRules_ || r.sel.depth;
  ext_.push_back({env, std::move(r)});
  return (RuleEnvId)ext_.size();
}

bool Cascade::matches(const StyleSelector& s, const NodeView& n) const {
  if (s.kind != 0xFFFF && s.kind != (u16)n.kind) return false;
  if (s.role && s.role != n.role) return false;
  if (s.depth && s.depth != n.depth) return false;
  if (s.cls) {  // one of its class tokens
    if (!n.cls) return false;
    const std::string_view want = strs_.get(s.cls), have = strs_.get(n.cls);
    bool hit = false;
    for (size_t at = 0; at <= have.size() && !hit;) {
      size_t sp = have.find(' ', at);
      if (sp == std::string_view::npos) sp = have.size();
      hit = have.substr(at, sp - at) == want;
      at = sp + 1;
    }
    if (!hit) return false;
  }
  if (s.lang) {  // a BCP-47 prefix: zh matches zh-Hans
    if (!n.lang) return false;
    const std::string_view want = strs_.get(s.lang), have = strs_.get(n.lang);
    if (have.size() < want.size()) return false;
    for (size_t i = 0; i < want.size(); i++)
      if (std::tolower((unsigned char)have[i]) != std::tolower((unsigned char)want[i])) return false;
    if (have.size() > want.size() && have[want.size()] != '-') return false;
  }
  for (const auto& [key, val] : s.where) {
    const ArgVal* a = nullptr;
    if (n.args)
      for (const ArgVal& x : *n.args)
        if (x.key == key) a = &x;
    if (!a) return false;
    const std::string_view want = strs_.get(val);
    std::string have;
    if (a->tag == ArgTag::Str) have = strs_.get(a->ref);
    else if (a->tag == ArgTag::Bool) have = a->num != 0 ? "true" : "false";
    else if (a->tag == ArgTag::Num) appendf(have, "%g", a->num);
    else return false;
    if (have != want) return false;
  }
  return true;
}

void Cascade::fold(Styling& st, NodeProps& props, const NodeView& n, RuleEnvId env,
                   const std::vector<ArgVal>& own, bool base) const {
  // the patches that apply, lowest first
  const std::vector<ArgVal>* buf[32];
  std::vector<const std::vector<ArgVal>*> more;
  size_t nb = 0;
  auto push = [&](const std::vector<ArgVal>* p) {
    if (nb < 32) buf[nb++] = p;
    else more.push_back(p);
  };
  std::vector<const std::vector<ArgVal>*> forced;
  if (base && !base_.empty()) {
    const std::vector<u32>& a = byKind_[(u16)n.kind < KIND_COUNT ? (u16)n.kind : KIND_COUNT];
    const std::vector<u32>& b = byKind_[KIND_COUNT];
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
      const u32 r = j >= b.size() || (i < a.size() && a[i] < b[j]) ? a[i++] : b[j++];
      if (!matches(base_[r].sel, n)) continue;
      if (base_[r].force) forced.push_back(&base_[r].patch);
      else push(&base_[r].patch);
    }
  }
  if (env) {  // the env's rules, outer (pushed first) to inner
    u32 chain[16];
    std::vector<u32> longer;
    size_t nc = 0;
    for (RuleEnvId e = env; e; e = ext_[e - 1].parent) {
      if (nc < 16) chain[nc++] = e;
      else longer.push_back(e);
    }
    for (size_t k = longer.size(); k-- > 0;)
      if (matches(ext_[longer[k] - 1].rule.sel, n)) push(&ext_[longer[k] - 1].rule.patch);
    for (size_t k = nc; k-- > 0;)
      if (matches(ext_[chain[k] - 1].rule.sel, n)) push(&ext_[chain[k] - 1].rule.patch);
  }
  if (!own.empty()) push(&own);
  if (nb == 0 && forced.empty()) return;
  // per property the last value wins (a size: one slot for size and
  // sizePx); decorations OR
  std::array<const ArgVal*, ARGK_COUNT> last{};
  u8 deco = 0;
  bool decoSet = false;
  auto take = [&](const std::vector<ArgVal>* p) {
    for (const ArgVal& a : *p) {
      if ((u16)a.key >= ARGK_COUNT) continue;
      if (a.key == ArgK::decoration && a.tag == ArgTag::Num) {
        deco |= (u8)(u64)a.num;
        decoSet = true;
        continue;
      }
      last[a.key == ArgK::sizePx ? (u16)ArgK::size : (u16)a.key] = &a;
    }
  };
  for (size_t k = 0; k < nb; k++) take(buf[k]);
  for (const auto* p : more) take(p);
  for (const auto* p : forced) take(p);
  auto intern = [](u32 r) { return r; };  // rule strings are document StrRefs already
  auto view = [&](u32 r) { return strs_.get(r); };
  for (u16 k = 0; k < ARGK_COUNT; k++) {
    const ArgVal* a = last[k];
    if (!a) continue;
    if (isNodeArg(a->key)) applyNodeArg(props, *a, intern, view);
    else applyStyleArg(st, *a, intern, view);
  }
  if (decoSet) st.decoration |= deco;
}

size_t settleMade(ContentNode* root, const Cascade& cascade, NodePropsTable& props, const StyleTable& styles) {
  if (!root) return 0;
  size_t settled = 0;
  // (node, its parent) in preorder; `up`: the ancestors' kinds when a
  // depth selector needs them
  struct At {
    ContentNode* n;
    const ContentNode* parent;
    u32 depth;  // its index in `up`
  };
  const bool depth = cascade.usesDepth();
  std::vector<u16> up;
  std::vector<At> work{{root, nullptr, 0}};
  while (!work.empty()) {
    At a = work.back();
    work.pop_back();
    ContentNode* n = a.n;
    if (depth) up.resize(a.depth);
    if (n->props == kPropsUnset) {
      const NodeProps& from = a.parent && a.parent->props != kPropsUnset ? props.get(a.parent->props) : props.get(0);
      NodeProps np = inheritProps(from);
      Styling st = styles.get(n->style);  // the maker's, kept
      Cascade::NodeView view{n->kind};
      view.args = &n->args;
      view.role = attrStr(n, ArgK::role);
      view.cls = attrStr(n, ArgK::class_);
      view.lang = st.lang;
      if (depth)
        for (u16 k : up) view.depth += k == (u16)n->kind;
      const RuleEnvId env = a.parent ? a.parent->env : 0;
      cascade.fold(st, np, view, env, {});
      n->props = props.idOf(np);
      n->env = env;
      settled++;
    }
    if (depth) up.push_back((u16)n->kind);
    for (size_t k = n->kids.size(); k-- > 0;) work.push_back({n->kids[k], n, a.depth + 1});
  }
  return settled;
}

// ---- rules in JSON ------------------------------------------------------------

std::string_view defaultRulesJson() { return kDefaultsJson; }

namespace {
ArgK styleKey(std::string_view k, bool& ok) {
  for (const StyleKeyRow& r : kStyleKeys)
    if (k == r.key) {
      ok = true;
      return r.attr;
    }
  ok = false;
  return ArgK::weight;
}
ArgK argKeyNamed(std::string_view k, bool& ok) {
  for (u16 i = 0; i < ARGK_COUNT; i++)
    if (k == argName((ArgK)i)) {
      ok = true;
      return (ArgK)i;
    }
  ok = false;
  return ArgK::weight;
}
u16 kindNamed(std::string_view k) {
  for (u16 i = 0; i < KIND_COUNT; i++)
    if (k == kKinds[i].name) return i;
  return 0xFFFF;
}
}  // namespace

std::vector<StyleRule> parseRules(std::string_view json, Interner& strs, DiagSink& diags, const char* origin,
                             const SettingText& setting) {
  std::vector<StyleRule> out;
  JsonValue doc;
  JsonReader rd;
  auto bad = [&](const std::string& why) { diags.add(Sev::Warning, "style-rule", {}, std::string(origin) + ": " + why); };
  if (!rd.parse(json, doc)) {
    bad(std::string("not JSON (") + rd.error() + ")");
    return out;
  }
  if (doc.t != JsonValue::T::Arr) {
    bad("rules are an array");
    return out;
  }
  for (const JsonValue& item : doc.arr) {
    const JsonValue *sel = nullptr, *patch = nullptr, *force = nullptr;
    if (item.t == JsonValue::T::Arr && item.arr.size() == 2) {
      sel = &item.arr[0];
      patch = &item.arr[1];
    } else if (item.t == JsonValue::T::Obj) {
      sel = item.get("select");
      patch = item.get("patch");
      force = item.get("force");
    }
    if (!sel || !patch || patch->t != JsonValue::T::Obj) {
      bad("a rule is [selector, patch] or {select, patch, force}");
      continue;
    }
    StyleRule r;
    r.force = force && force->t == JsonValue::T::Bool && force->b;
    bool ok = true;
    auto selKind = [&](std::string_view k) {
      r.sel.kind = kindNamed(k);
      if (r.sel.kind == 0xFFFF) {
        bad("no kind '" + std::string(k) + "'");
        ok = false;
      }
    };
    if (sel->t == JsonValue::T::Str) {
      selKind(sel->str);
    } else if (sel->t == JsonValue::T::Obj) {
      for (size_t i = 0; i < sel->keys.size() && ok; i++) {
        const std::string& k = sel->keys[i];
        const JsonValue& v = sel->vals[i];
        auto text = [&]() -> std::string {
          if (v.t == JsonValue::T::Str) return v.str;
          if (v.t == JsonValue::T::Bool) return v.b ? "true" : "false";
          if (v.t == JsonValue::T::Num) {
            std::string t;
            appendf(t, "%g", v.num);
            return t;
          }
          return "";
        };
        if (k == "kind") selKind(text());
        else if (k == "role") r.sel.role = strs.intern(text());
        else if (k == "class") r.sel.cls = strs.intern(text());
        else if (k == "lang" || k == "textLang") r.sel.lang = strs.intern(text());
        else if (k == "depth") r.sel.depth = (u8)std::clamp(v.num, 1.0, 16.0);
        else {  // where: one of its own attributes
          bool known;
          const ArgK key = argKeyNamed(k, known);
          if (!known) {
            bad("a selector key '" + k + "' names no attribute");
            ok = false;
          } else {
            r.sel.where.push_back({key, strs.intern(text())});
          }
        }
      }
    } else {
      bad("a selector is a kind name or an object");
      ok = false;
    }
    if (!ok) continue;
    // the patch: style keys (as $.style.push takes them) or attribute names;
    // nested objects are key paths (par: {indent} = par.indent)
    std::function<void(const JsonValue&, const std::string&)> keys = [&](const JsonValue& o, const std::string& pre) {
      for (size_t i = 0; i < o.keys.size(); i++) {
        const std::string path = pre.empty() ? o.keys[i] : pre + "." + o.keys[i];
        const JsonValue& v = o.vals[i];
        if (v.t == JsonValue::T::Obj && !v.get("setting")) {
          keys(v, path);
          continue;
        }
        bool known;
        ArgK key = styleKey(path, known);
        if (!known) key = argKeyNamed(path, known);
        if (!known) {
          bad("a patch key '" + path + "' is no style property");
          continue;
        }
        ArgVal a{key, ArgTag::Null, 0, 0};
        std::string text;
        if (v.t == JsonValue::T::Obj) {  // {"setting": "code.scale", "unit": "em"}
          const JsonValue* s = v.get("setting");
          const JsonValue* u = v.get("unit");
          text = setting && s && s->t == JsonValue::T::Str ? setting(s->str) : std::string();
          if (text.empty()) {
            bad("a patch names an unknown setting");
            continue;
          }
          if (u && u->t == JsonValue::T::Str) text += u->str;
          a.tag = ArgTag::Str;
        } else if (v.t == JsonValue::T::Str) {
          a.tag = ArgTag::Str;
          text = v.str;
        } else if (v.t == JsonValue::T::Num) {
          a.tag = ArgTag::Num;
          a.num = v.num;
        } else if (v.t == JsonValue::T::Bool) {
          a.tag = ArgTag::Bool;
          a.num = v.b ? 1 : 0;
        }
        std::string why;
        if (!checkStyledValue(a, text, why)) {
          bad("'" + path + "': " + why);
          continue;
        }
        if (a.tag == ArgTag::Str) a.ref = strs.intern(text);
        r.patch.push_back(a);
      }
    };
    keys(*patch, "");
    out.push_back(std::move(r));
  }
  return out;
}

}  // namespace tsr
