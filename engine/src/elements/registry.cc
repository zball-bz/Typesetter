#include "registry.h"

#include <algorithm>

#include "semantic_data.gen.h"

namespace tsr {

namespace {

struct Loader {
  std::string err;
  Registry& r;

  bool fail(std::string m) {
    if (err.empty()) err = std::move(m);
    return false;
  }
  static const JsonValue* member(const JsonValue& v, std::string_view k) {
    return v.t == JsonValue::T::Obj ? v.get(k) : nullptr;
  }
  static std::string str(const JsonValue* v, std::string_view dflt = "") {
    return v && v->t == JsonValue::T::Str ? v->str : std::string(dflt);
  }

  bool kindOf(std::string_view name, Kind& out) {
    for (u16 k = 0; k < KIND_COUNT; k++)
      if (name == kindName((Kind)k)) {
        out = (Kind)k;
        return true;
      }
    return fail("unknown node kind '" + std::string(name) + "'");
  }
  bool argOf(std::string_view name, ArgK& out) {
    for (u16 k = 0; k < ARGK_COUNT; k++)
      if (name == argName((ArgK)k)) {
        out = (ArgK)k;
        return true;
      }
    return fail("unknown argument '" + std::string(name) + "'");
  }
  // a styled template item (plan P2-08): {weight, italic, decoration:
  // [under|over|strike], role: body|mono, baseline: super|sub, size: ×}
  bool styledOf(const JsonValue& s, TItem& it) {
    if (s.t != JsonValue::T::Obj) return fail("styled is an object of style rows");
    for (size_t k = 0; k < s.keys.size(); k++) {
      const std::string& key = s.keys[k];
      const JsonValue& v = s.vals[k];
      if (key == "weight" && v.t == JsonValue::T::Num && v.num >= 100 && v.num <= 900) it.delta.weight = (u16)v.num;
      else if (key == "italic" && v.t == JsonValue::T::Bool) it.delta.italic = v.b;
      else if (key == "decoration" && v.t == JsonValue::T::Arr) {
        for (const JsonValue& d : v.arr) {
          if (d.str == "under") it.delta.decoration |= DECORATION_UNDER;
          else if (d.str == "over") it.delta.decoration |= DECORATION_OVER;
          else if (d.str == "strike") it.delta.decoration |= DECORATION_STRIKE;
          else return fail("a decoration is under, over or strike");
        }
      } else if (key == "role" && (v.str == "body" || v.str == "mono"))
        it.delta.fontRole = v.str == "mono" ? FONTROLE_MONO : FONTROLE_BODY;
      else if (key == "baseline" && (v.str == "super" || v.str == "sub"))
        it.delta.baseline = v.str == "super" ? BASELINE_SUPER : BASELINE_SUB;
      else if (key == "size" && v.t == JsonValue::T::Num && v.num > 0) it.size = (float)v.num;
      else return fail("styled: unknown or invalid row '" + key + "'");
    }
    return true;
  }

  bool targ(const JsonValue& v, TArg& out) {
    switch (v.t) {
      case JsonValue::T::Str: out = {TArg::K::Text, v.str}; return true;
      case JsonValue::T::Bool: out = {TArg::K::Bool, "", v.b}; return true;
      case JsonValue::T::Num: out = {TArg::K::Num, "", false, v.num}; return true;
      case JsonValue::T::Obj:
        if (const JsonValue* s = v.get("slot")) return out = {TArg::K::Slot, str(s)}, true;
        if (const JsonValue* s = v.get("anchor")) return out = {TArg::K::Anchor, str(s)}, true;
        return fail("an argument value is a string, a number, a boolean, {slot} or {anchor}");
      case JsonValue::T::Null:
      case JsonValue::T::Arr:
        break;
    }
    return fail("an argument value is a string, a number, a boolean, {slot} or {anchor}");
  }

  bool tmpl(const JsonValue* v, Template& out) {
    if (!v) return true;
    if (v->t != JsonValue::T::Arr) return fail("a template is an array");
    for (const JsonValue& x : v->arr) {
      TItem it;
      if (x.t == JsonValue::T::Str) {
        it.name = x.str;
        out.push_back(std::move(it));
        continue;
      }
      if (x.t != JsonValue::T::Obj) return fail("a template item is a string or an object");
      if (const JsonValue* s = x.get("slot")) {
        it.k = TItem::K::Slot;
        it.name = str(s);
        if (const JsonValue* o = x.get("or")) {  // the slot, else the `or` slot (plan P2-07)
          TItem a = it, b = it;
          b.name = str(o);
          it.k = TItem::K::When;
          it.kids.push_back(std::move(a));
          it.orElse.push_back(std::move(b));
        }
      } else if (const JsonValue* s = x.get("term")) {
        it.k = TItem::K::Term;
        it.name = str(s);
      } else if (const JsonValue* s = x.get("node")) {
        it.k = TItem::K::Node;
        if (!kindOf(str(s), it.kind)) return false;
        it.siteStyle = str(x.get("style")) == "site";
        if (const JsonValue* a = x.get("args")) {
          for (size_t k = 0; k < a->keys.size(); k++) {
            std::pair<ArgK, TArg> p;
            if (!argOf(a->keys[k], p.first) || !targ(a->vals[k], p.second)) return false;
            it.args.push_back(std::move(p));
          }
        }
        if (!tmpl(x.get("kids"), it.kids)) return false;
      } else if (const JsonValue* s = x.get("styled")) {
        it.k = TItem::K::Styled;
        if (!styledOf(*s, it) || !tmpl(x.get("kids"), it.kids)) return false;
      } else if (const JsonValue* s = x.get("when")) {
        it.k = TItem::K::When;
        it.name = str(s);
        if (!tmpl(x.get("kids"), it.kids) || !tmpl(x.get("else"), it.orElse)) return false;
      } else if (const JsonValue* s = x.get("each")) {
        it.k = TItem::K::Each;
        it.name = str(s);
        if (!tmpl(x.get("kids"), it.kids) || !tmpl(x.get("sep"), it.sep)) return false;
      } else if (const JsonValue* s = x.get("paras")) {
        it.k = TItem::K::Paras;
        if (const JsonValue* a = member(*s, "anchor"); a && !targ(*a, it.anchor)) return false;
        if (const JsonValue* z = member(*s, "scale")) it.size = (float)z->num;
        if (!tmpl(member(*s, "tail"), it.tail)) return false;
      } else {
        return fail("unknown template item");
      }
      out.push_back(std::move(it));
    }
    return true;
  }

  bool alias(const JsonValue* v, AliasRule& out) {
    if (!v) return true;
    out.prefix = str(member(*v, "prefix"));
    std::string body = str(member(*v, "body"));
    out.body = body == "number" ? AliasRule::Body::Number : body == "key" ? AliasRule::Body::Key
                                                                         : AliasRule::Body::None;
    if (out.body == AliasRule::Body::None) return fail("an alias body is number or key");
    out.hasRef = member(*v, "ref") != nullptr;
    return tmpl(member(*v, "ref"), out.ref);
  }

  u16 counterIndex(std::string_view name) {
    for (size_t k = 0; k < r.counters.size(); k++)
      if (r.counters[k].name == name) return (u16)k;
    return kNoIndex;
  }
  ClassId classIndex(std::string_view name) {
    for (size_t k = 1; k < r.classes.size(); k++)
      if (r.classes[k].name == name) return (ClassId)k;
    return 0;
  }

  bool supplement(const JsonValue& v, Supplement& out) {
    return parseSupplement(v, out) || fail("a supplement is a term key, {term}, {text} or texts by language");
  }

  // counters: shape (by-level: level-arg, depth), gap, keyed, and (plan
  // P2-07) within {counter, depth, sep}, pattern, start; the within
  // counters resolve once every counter is known
  bool counters(const JsonValue* v) {
    if (!v) return true;
    std::vector<std::pair<size_t, std::string>> within;
    for (size_t k = 0; k < v->keys.size(); k++) {
      const JsonValue& c = v->vals[k];
      CounterDef d;
      d.name = v->keys[k];
      d.byLevel = str(member(c, "shape")) == "by-level";
      if (const JsonValue* a = member(c, "level-arg"); a && !argOf(a->str, d.levelArg)) return false;
      if (const JsonValue* a = member(c, "depth")) d.depth = std::max(1, (int)a->num);
      d.gapOne = str(member(c, "gap")) == "one";
      if (const JsonValue* a = member(c, "keyed")) d.keyed = a->b;
      if (const JsonValue* w = member(c, "within")) {
        within.push_back({r.counters.size(), str(member(*w, "counter"))});
        if (const JsonValue* a = member(*w, "depth")) d.withinDepth = std::max(1, (int)a->num);
        if (const JsonValue* a = member(*w, "sep")) d.withinSep = str(a);
      }
      d.pattern = str(member(c, "pattern"));
      if (const JsonValue* a = member(c, "start"))
        for (const JsonValue& x : a->arr) d.start.push_back((int)x.num);
      r.counters.push_back(std::move(d));
    }
    for (auto& [i, name] : within) {
      u16 w = counterIndex(name);
      if (w == kNoIndex || w == i) return fail("counter '" + r.counters[i].name + "' is within an undeclared counter");
      r.counters[i].within = w;
    }
    // no cycles through within (decl-cycle: the offending edge is dropped)
    for (CounterDef& d : r.counters) {
      u16 at = d.within;
      for (size_t steps = 0; at != kNoIndex && steps <= r.counters.size(); steps++) at = r.counters[at].within;
      if (at != kNoIndex) d.within = kNoIndex;
    }
    return true;
  }

  // counter systems (plan P2-07): {symbols: […], mode}
  bool systems(const JsonValue* v) {
    if (!v) return true;
    for (size_t k = 0; k < v->keys.size(); k++) {
      CounterSystem s;
      s.name = v->keys[k];
      if (const JsonValue* a = member(v->vals[k], "symbols"))
        for (const JsonValue& x : a->arr) s.symbols.push_back(str(&x));
      if (s.symbols.empty()) return fail("counter system '" + s.name + "' has no symbols");
      std::string m = str(member(v->vals[k], "mode"), "numeric");
      s.mode = m == "alphabetic" ? CounterSystem::Mode::Alphabetic
               : m == "cyclic"   ? CounterSystem::Mode::Cyclic
               : m == "fixed"    ? CounterSystem::Mode::Fixed
                                 : CounterSystem::Mode::Numeric;
      r.systems.push_back(std::move(s));
    }
    return true;
  }

  bool klass(const std::string& name, const JsonValue& v,
             std::vector<std::pair<ClassId, std::string>>& insides) {
    ElementClass c;
    if (const JsonValue* l = member(v, "like")) {
      ClassId base = classIndex(str(l));
      if (!base) return fail("class '" + name + "' is like an undeclared class");
      c = r.classes[base];
      c.select.clear();  // selectors never inherit
      if (c.flow) c.flow = FlowDef(*c.flow);
    }
    c.name = name;
    ClassId self = (ClassId)r.classes.size();
    if (const JsonValue* s = member(v, "select")) {
      for (const JsonValue& x : s->arr) {
        Selector sel;
        for (size_t k = 0; k < x.keys.size(); k++) {
          const std::string& key = x.keys[k];
          if (key == "node") {
            if (x.vals[k].str == "any") continue;
            sel.anyKind = false;
            if (!kindOf(x.vals[k].str, sel.kind)) return false;
          } else if (key == "inside") {
            insides.push_back({self, x.vals[k].str});  // resolved once every class is known
            sel.inside = kNoIndex;
          } else {
            std::pair<ArgK, std::string> p;
            if (!argOf(key, p.first)) return false;
            const JsonValue& val = x.vals[k];
            p.second = val.t == JsonValue::T::Bool ? (val.b ? "true" : "false")
                       : val.t == JsonValue::T::Num ? std::to_string((long long)val.num)
                                                     : val.str;
            sel.preds.push_back(std::move(p));
          }
        }
        c.select.push_back(std::move(sel));
      }
    } else {  // the default selector (plan P2-07): {role: name} on any kind
      Selector sel;
      sel.preds.push_back({ArgK::role, name});
      c.select.push_back(std::move(sel));
    }
    if (const JsonValue* x = member(v, "counter")) {
      c.counter = counterIndex(str(x));
      if (c.counter == kNoIndex) return fail("class '" + name + "' uses an undeclared counter");
    }
    if (const JsonValue* x = member(v, "numbering")) {
      std::string p = str(x);
      c.numbering = p == "always"     ? ElementClass::Numbering::Always
                    : p == "labelled" ? ElementClass::Numbering::Labelled
                                      : ElementClass::Numbering::Never;
    }
    if (const JsonValue* x = member(v, "supplement") ; x && !supplement(*x, c.supplement)) return false;
    if (const JsonValue* x = member(v, "labels")) {
      if (x->t == JsonValue::T::Obj) {
        c.labels = ElementClass::Labels::FromArg;
        if (!argOf(str(member(*x, "arg")), c.labelArg)) return false;
      } else {
        c.labels = str(x) == "none" ? ElementClass::Labels::None : ElementClass::Labels::User;
      }
    }
    if (const JsonValue* x = member(v, "title")) {
      if (x->t == JsonValue::T::Obj && member(*x, "ext")) {  // EXT data (plan P2-07)
        c.title = ElementClass::Title::Ext;
        c.titleExt = str(member(*x, "ext"));
      } else if (x->t == JsonValue::T::Obj) {
        c.title = ElementClass::Title::Arg;
        if (!argOf(str(member(*x, "arg")), c.titleArg)) return false;
      } else {
        c.title = str(x) == "text" ? ElementClass::Title::Text : ElementClass::Title::None;
      }
    }
    if (const JsonValue* x = member(v, "outline")) c.outline = x->b;
    if (!alias(member(v, "alias"), c.alias)) return false;
    if (const JsonValue* x = member(v, "sites")) {
      c.sites.clear();
      for (const JsonValue& s : x->arr) {
        SiteDef d;
        std::string where = str(member(s, "where"));
        d.where = where == "arg" ? SiteDef::Where::Arg : where == "replace" ? SiteDef::Where::Replace
                                                                            : SiteDef::Where::Prepend;
        d.at = str(member(s, "at")) == "first-para" ? SiteDef::At::FirstPara : SiteDef::At::Self;
        if (const JsonValue* a = member(s, "arg"); a && !argOf(a->str, d.arg)) return false;
        if (!tmpl(member(s, "template"), d.tmpl)) return false;
        c.sites.push_back(std::move(d));
      }
    }
    if (const JsonValue* x = member(v, "ref")) {
      c.ref.clear();
      c.hasRef = true;
      if (!tmpl(x, c.ref)) return false;
    }
    if (const JsonValue* x = member(v, "flow")) {
      FlowDef f;
      f.name = str(member(*x, "name"));
      f.placeAtEnd = str(member(*x, "placement")) == "end";
      if (!alias(member(*x, "marker-alias"), f.markerAlias) || !tmpl(member(*x, "marker"), f.marker))
        return false;
      c.flow = std::move(f);
    }
    if (const JsonValue* x = member(v, "table")) c.table = str(x);
    if (const JsonValue* x = member(v, "row-key")) {
      c.rowKeyed = true;
      if (!argOf(str(x), c.rowKey)) return false;
    }
    if (const JsonValue* x = member(v, "forms")) {
      if (x->t != JsonValue::T::Obj) return fail("class forms: an object of templates");
      for (size_t k = 0; k < x->keys.size(); k++) {
        auto it = std::find_if(c.forms.begin(), c.forms.end(), [&](const auto& f) { return f.first == x->keys[k]; });
        if (it == c.forms.end()) it = c.forms.insert(c.forms.end(), {x->keys[k], {}});
        it->second.clear();
        if (!tmpl(&x->vals[k], it->second)) return false;
      }
    }
    if (const JsonValue* x = member(v, "box")) {
      if (str(x) != "figure") return fail("class box: only 'figure'");
      c.box = ElementClass::Box::Figure;
    }
    if (const JsonValue* x = member(v, "html")) {
      if (str(x) != "figure") return fail("class html: only 'figure'");
      c.html = ElementClass::Html::Figure;
    }
    if (c.numbering != ElementClass::Numbering::Never && c.counter == kNoIndex)
      return fail("class '" + name + "' is numbered but has no counter");
    // a numbered class reads, by default, as its supplement and number
    if (!c.hasRef && c.counter != kNoIndex && c.numbering != ElementClass::Numbering::Never) {
      c.hasRef = true;
      c.ref = {TItem{TItem::K::Slot, "supplement"}, TItem{TItem::K::Slot, "number"}};
    }
    r.classes.push_back(std::move(c));
    return true;
  }

  bool collector(const std::string& name, const JsonValue& v) {
    CollectorDef d;
    d.name = name;
    const JsonValue* q = member(v, "query");
    if (!q) return fail("collector '" + name + "' has no query");
    if (const JsonValue* x = member(*q, "classes")) {
      d.src = CollectorDef::Src::Outline;
      if (str(x) != "outline") return fail("collector classes: only 'outline' (P3-13 adds lists)");
    } else if (const JsonValue* x = member(*q, "table")) {
      d.src = CollectorDef::Src::Table;
      d.table = str(x);
    } else if (const JsonValue* x = member(*q, "flow")) {
      d.src = CollectorDef::Src::Flow;
      d.flow = str(x);
    } else {
      return fail("collector '" + name + "': a query names classes, a table or a flow");
    }
    d.nestByDepth = str(member(*q, "nest")) == "depth";
    d.cited = str(member(*q, "cited")) == "cited-then-all" ? CollectorDef::Cited::CitedThenAll
                                                              : CollectorDef::Cited::Cited;
    std::string ctx = str(member(v, "context"), "collector");
    d.ctx = ctx == "instance" ? CollectorDef::Ctx::Instance
            : ctx == "row"    ? CollectorDef::Ctx::Row
                              : CollectorDef::Ctx::Collector;
    if (!tmpl(member(v, "wrap"), d.wrap) || !tmpl(member(v, "entry"), d.entry)) return false;
    d.hasEmpty = member(v, "empty") != nullptr;
    if (!tmpl(member(v, "empty"), d.empty)) return false;
    if (const JsonValue* rows = member(v, "rows")) {
      d.keyedRows = true;
      d.rowCounter = counterIndex(str(member(*rows, "counter")));
      if (d.rowCounter == kNoIndex || !r.counters[d.rowCounter].keyed)
        return fail("collector '" + name + "': rows need a keyed counter");
      if (!alias(member(*rows, "anchor"), d.rowAnchor)) return false;
    }
    if (const JsonValue* c = member(v, "cite")) {
      d.citeable = true;
      if (!tmpl(c, d.cite)) return false;
    }
    r.collectors.push_back(std::move(d));
    return true;
  }

  bool load(std::string_view json) {
    JsonValue v;
    JsonReader rd;
    if (!rd.parse(json, v) || v.t != JsonValue::T::Obj)
      return fail(rd.error() ? rd.error() : "expected an object");
    r.classes.emplace_back();  // ClassId 0: no class
    if (!systems(member(v, "systems")) || !counters(member(v, "counters"))) return false;
    if (!tmpl(member(v, "unresolved"), r.unresolved) || !tmpl(member(v, "unnumbered"), r.unnumbered))
      return false;
    std::vector<std::pair<ClassId, std::string>> insides;
    if (const JsonValue* cs = member(v, "classes"))
      for (size_t k = 0; k < cs->keys.size(); k++)
        if (!klass(cs->keys[k], cs->vals[k], insides)) return false;
    for (auto& [c, name] : insides) {
      ClassId in = classIndex(name);
      if (!in) return fail("selector inside an undeclared class '" + name + "'");
      for (Selector& s : r.classes[c].select)
        if (s.inside == kNoIndex) s.inside = in;
    }
    if (const JsonValue* cs = member(v, "collectors"))
      for (size_t k = 0; k < cs->keys.size(); k++)
        if (!collector(cs->keys[k], cs->vals[k])) return false;
    return true;
  }
};

bool digitsDots(std::string_view t, bool dots) {
  if (t.empty()) return false;
  bool prevDot = true;
  for (char c : t) {
    if (c >= '0' && c <= '9') {
      prevDot = false;
      continue;
    }
    if (dots && c == '.' && !prevDot) {
      prevDot = true;
      continue;
    }
    return false;
  }
  return !prevDot;
}

}  // namespace

// a supplement (plan P2-07): "key" or {"term": key} (a locale word),
// {"text": "…"}, or {"en": "…", "zh": "…"} (per language)
bool parseSupplement(const JsonValue& v, Supplement& out) {
  out = Supplement{};
  if (v.t == JsonValue::T::Str) {
    out.term = v.str;
    return true;
  }
  if (v.t != JsonValue::T::Obj) return false;
  auto text = [](const JsonValue* x) { return x && x->t == JsonValue::T::Str ? x->str : std::string(); };
  if (const JsonValue* t = v.get("term")) {
    out.term = text(t);
    return !out.term.empty();
  }
  if (const JsonValue* t = v.get("text")) {
    out.text = text(t);
    out.literal = true;
    return true;
  }
  for (size_t k = 0; k < v.keys.size(); k++) {
    if (v.vals[k].t != JsonValue::T::Str) return false;
    out.byLang.push_back({v.keys[k], v.vals[k].str});
  }
  return !out.byLang.empty();
}

ClassId Registry::classNamed(std::string_view name) const {
  for (size_t k = 1; k < classes.size(); k++)
    if (classes[k].name == name) return (ClassId)k;
  return 0;
}

std::unique_ptr<Registry> Registry::fromJson(std::string_view json, std::string& error) {
  auto r = std::make_unique<Registry>();
  Loader ld{{}, *r};
  if (!ld.load(json)) {
    error = ld.err;
    return nullptr;
  }
  r->index();
  return r;
}

const Registry& Registry::builtin() {
  static const std::unique_ptr<Registry> r = [] {
    std::string err;
    std::unique_ptr<Registry> b = fromJson(kElementsJson, err);
    if (!b) __builtin_trap();  // the built-in rows are checked by the native tests
    return b;
  }();
  return *r;
}

void Registry::index() {
  for (auto& v : byKind_) v.clear();
  for (size_t c = 1; c < classes.size(); c++)
    for (const Selector& s : classes[c].select)
      byKind_[s.anyKind ? KIND_COUNT : (u16)s.kind].push_back({(ClassId)c, &s});
}

ClassId Registry::classify(const ContentNode* n, ClassId inside, const Interner& strs) const {
  ClassId best = 0;
  int bestSpec = -1;
  auto test = [&](const std::vector<std::pair<ClassId, const Selector*>>& cands) {
    for (const auto& [c, s] : cands) {
      if (s->inside && s->inside != inside) continue;
      bool ok = true;
      for (const auto& [k, want] : s->preds) {
        const ArgVal* a = attr(n, k);
        if (!a) {
          ok = false;
          break;
        }
        std::string have = a->tag == ArgTag::Str    ? std::string(strs.get(a->ref))
                           : a->tag == ArgTag::Bool ? (a->num != 0 ? "true" : "false")
                                                    : std::to_string((long long)a->num);
        if (have != want) {
          ok = false;
          break;
        }
      }
      // the most specific selector wins; on a tie, the latest declaration
      if (ok && s->specificity() >= bestSpec && (s->specificity() > bestSpec || c > best)) {
        best = c;
        bestSpec = s->specificity();
      }
    }
  };
  if ((u16)n->kind < KIND_COUNT) test(byKind_[(u16)n->kind]);
  test(byKind_[KIND_COUNT]);
  return best;
}

const CollectorDef* Registry::collector(std::string_view name) const {
  for (const CollectorDef& c : collectors)
    if (c.name == name) return &c;
  return nullptr;
}
const CollectorDef* Registry::tableOwner(std::string_view table) const {
  for (const CollectorDef& c : collectors)
    if (c.keyedRows && c.table == table) return &c;
  return nullptr;
}
const CollectorDef* Registry::flowCollector(std::string_view flow) const {
  for (const CollectorDef& c : collectors)
    if (c.src == CollectorDef::Src::Flow && c.flow == flow) return &c;
  return nullptr;
}

bool Registry::reservedShape(std::string_view l) const {
  auto check = [&](const AliasRule& a, bool dotted) {
    if (a.body == AliasRule::Body::None || l.substr(0, a.prefix.size()) != a.prefix) return false;
    if (a.body == AliasRule::Body::Key) return true;
    return digitsDots(l.substr(a.prefix.size()), dotted);
  };
  for (const ElementClass& c : classes) {
    bool dotted = c.counter != kNoIndex && counters[c.counter].byLevel;
    if (check(c.alias, dotted)) return true;
    if (c.flow && check(c.flow->markerAlias, dotted)) return true;
  }
  for (const CollectorDef& c : collectors)
    if (check(c.rowAnchor, false)) return true;
  return false;
}

}  // namespace tsr
