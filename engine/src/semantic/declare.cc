#include "declare.h"

#include <algorithm>
#include <mutex>

#include "semantic_data.gen.h"

namespace tsr {

namespace {

// --- JSON rows -------------------------------------------------------------------
JsonValue* member(JsonValue& o, std::string_view k) {
  for (size_t i = o.keys.size(); i-- > 0;)
    if (o.keys[i] == k) return &o.vals[i];
  return nullptr;
}
JsonValue& memberOrAdd(JsonValue& o, std::string_view k, JsonValue::T t) {
  if (JsonValue* v = member(o, k)) return *v;
  o.keys.emplace_back(k);
  o.vals.emplace_back();
  o.vals.back().t = t;
  return o.vals.back();
}
// a row patch: each field replaces the row's (a new name appends the row);
// `like` naming the row itself means the previous layer's row, which an
// overlay already is
void overlay(JsonValue& section, const std::string& name, const JsonValue& row) {
  JsonValue* have = member(section, name);
  if (!have) {
    section.keys.push_back(name);
    section.vals.push_back(row);
    return;
  }
  for (size_t i = 0; i < row.keys.size(); i++) {
    if (row.keys[i] == "like" && row.vals[i].t == JsonValue::T::Str && row.vals[i].str == name) continue;
    if (JsonValue* f = member(*have, row.keys[i])) *f = row.vals[i];
    else {
      have->keys.push_back(row.keys[i]);
      have->vals.push_back(row.vals[i]);
    }
  }
}
int depthOf(const JsonValue& v) {
  int d = 0;
  for (const JsonValue& x : v.arr) d = std::max(d, depthOf(x));
  for (const JsonValue& x : v.vals) d = std::max(d, depthOf(x));
  return d + 1;
}

// the registry section a declaration type patches
const char* sectionOf(std::string_view type) {
  if (type == "element") return "classes";
  if (type == "counter") return "counters";
  if (type == "collector") return "collectors";
  if (type == "counter-system") return "systems";
  return nullptr;
}

// the fields a row of each section may have (others: decl-field, ignored)
bool knownField(std::string_view section, std::string_view f) {
  static constexpr std::string_view kClasses[] = {"select", "like",  "counter", "numbering", "supplement", "labels",
                                                  "title",  "outline", "alias", "sites",     "ref",        "forms",
                                                  "flow",   "table", "row-key", "box",       "html"};
  static constexpr std::string_view kCounters[] = {"shape", "level-arg", "depth", "gap", "keyed", "within", "pattern", "start"};
  static constexpr std::string_view kCollectors[] = {"query", "context", "wrap", "entry", "empty", "rows", "cite"};
  static constexpr std::string_view kSystems[] = {"symbols", "mode"};
  auto in = [&](const auto& xs) {
    for (std::string_view x : xs)
      if (x == f) return true;
    return false;
  };
  if (!f.empty() && f[0] == '$') return true;  // $comment
  if (section == "classes") return in(kClasses);
  if (section == "counters") return in(kCounters);
  if (section == "collectors") return in(kCollectors);
  return in(kSystems);
}

// --- templates: raw nodes → elements.json's template items ------------------------
constexpr size_t kTemplateNodes = 512;  // per template (design T3: decl-invalid beyond)
constexpr int kRowDepth = 8;            // a row's nesting, templates aside

struct Conv {
  const RawOps& raw;
  std::string err;
  size_t count = 0;

  const RawNode* node(u32 id) {
    for (int hops = 0; id < raw.nodes.size(); hops++) {
      const RawNode& n = raw.nodes[id];
      if (n.alias == kNoAlias || hops > 64) return &n;
      id = n.alias;
    }
    return nullptr;
  }
  std::string_view str(const ArgVal& a) const { return a.tag == ArgTag::Str ? raw.strings[a.ref] : std::string_view(); }
  const ArgVal* arg(const RawNode& n, ArgK k) const {
    for (const ArgVal& a : n.args)
      if (a.key == k) return &a;
    return nullptr;
  }
  static JsonValue text(std::string_view s) {
    JsonValue v;
    v.t = JsonValue::T::Str;
    v.str = s;
    return v;
  }
  static void put(JsonValue& o, std::string_view k, JsonValue v) {
    o.keys.emplace_back(k);
    o.vals.push_back(std::move(v));
  }

  bool kids(const RawNode& n, JsonValue& out, int depth) {
    out.t = JsonValue::T::Arr;
    for (u32 k : n.children)
      if (!items(k, out, depth + 1)) return false;
    return true;
  }
  // the items node `id` stands for, appended to the array `out`
  bool items(u32 id, JsonValue& out, int depth) {
    const RawNode* np = node(id);
    if (!np) return err = "a template node is missing", false;
    if (++count > kTemplateNodes) return err = "a template has more than 512 nodes", false;
    if (depth > 64) return err = "a template is nested too deeply", false;
    const RawNode& n = *np;
    if (n.isText || n.kind == Kind::text) {
      out.arr.push_back(text(raw.strings[n.str]));
      return true;
    }
    JsonValue it;
    it.t = JsonValue::T::Obj;
    switch (n.kind) {
      case Kind::seq:
        for (u32 k : n.children)
          if (!items(k, out, depth + 1)) return false;
        return true;
      case Kind::comment:
        return true;
      case Kind::slot: {  // slot('term:key') reads a locale word
        std::string_view name = arg(n, ArgK::name) ? str(*arg(n, ArgK::name)) : std::string_view();
        if (name.substr(0, 5) == "term:") put(it, "term", text(name.substr(5)));
        else put(it, "slot", text(name));
        if (const ArgVal* o = arg(n, ArgK::or_)) put(it, "or", text(str(*o)));
        break;
      }
      case Kind::when:
      case Kind::each: {
        const ArgVal* of = arg(n, ArgK::of);
        put(it, n.kind == Kind::when ? "when" : "each", text(of ? str(*of) : std::string_view()));
        JsonValue k;
        if (!kids(n, k, depth)) return false;
        if (!k.arr.empty()) put(it, "kids", std::move(k));
        if (const ArgVal* sep = arg(n, ArgK::sep)) {
          JsonValue s;
          s.t = JsonValue::T::Arr;
          s.arr.push_back(text(str(*sep)));
          put(it, "sep", std::move(s));
        }
        break;
      }
      case Kind::styled: {  // a delta carrier (plan P2-08): its relative rows
        JsonValue st;
        st.t = JsonValue::T::Obj;
        for (const ArgVal& x : n.args) {
          JsonValue v;
          if (x.key == ArgK::weight && x.tag == ArgTag::Num) v.t = JsonValue::T::Num, v.num = x.num;
          else if (x.key == ArgK::italic && x.tag == ArgTag::Bool) v.t = JsonValue::T::Bool, v.b = x.num != 0;
          else if (x.key == ArgK::decoration && x.tag == ArgTag::Num) {
            v.t = JsonValue::T::Arr;
            const u64 f = (u64)x.num;
            if (f & DECORATION_UNDER) v.arr.push_back(text("under"));
            if (f & DECORATION_OVER) v.arr.push_back(text("over"));
            if (f & DECORATION_STRIKE) v.arr.push_back(text("strike"));
          } else if (x.key == ArgK::fontRole && x.tag == ArgTag::Str) v = text(str(x));
          else if (x.key == ArgK::baseline && x.tag == ArgTag::Str) v = text(str(x));
          else if (x.key == ArgK::size && x.tag == ArgTag::Str) {  // em and % only: a template is relative
            Styling s;
            applyStyleArg(s, x, [](u32) { return StrRef(0); }, [&](u32 r) { return raw.strings[r]; });
            if (s.sizePx > 0 || s.sizeMul == 1.0f) continue;
            v.t = JsonValue::T::Num, v.num = s.sizeMul;
          } else {
            continue;  // a template's styled carries relative rows only (absolute ones: P3-01 rules)
          }
          put(st, x.key == ArgK::fontRole ? "role" : argName(x.key), std::move(v));
        }
        put(it, "styled", std::move(st));
        JsonValue k;
        if (!kids(n, k, depth)) return false;
        put(it, "kids", std::move(k));
        break;
      }
      case Kind::event:
      case Kind::entry:
      case Kind::collect:
      case Kind::note:
        // design T3 Templates rule 2: nothing counted, collected or flowing
        return err = std::string("a template may not contain ") + kindName(n.kind), false;
      default: {
        put(it, "node", text(kindName(n.kind)));
        JsonValue args;
        args.t = JsonValue::T::Obj;
        for (const ArgVal& a : n.args) {
          if (a.key == ArgK::ext || a.key == ArgK::label) continue;  // no data, no anchors (rule 2)
          JsonValue v;
          switch (a.tag) {
            case ArgTag::Str: v = text(raw.strings[a.ref]); break;
            case ArgTag::Num: v.t = JsonValue::T::Num, v.num = a.num; break;
            case ArgTag::Bool: v.t = JsonValue::T::Bool, v.b = a.num != 0; break;
            default: continue;
          }
          put(args, argName(a.key), std::move(v));
        }
        if (!args.keys.empty()) put(it, "args", std::move(args));
        JsonValue k;
        if (!kids(n, k, depth)) return false;
        if (!k.arr.empty()) put(it, "kids", std::move(k));
        break;
      }
    }
    out.arr.push_back(std::move(it));
    return true;
  }

  // {"$t": k} anywhere in a row → its k-th template
  bool substitute(JsonValue& v, const RawDecl& d) {
    if (v.t == JsonValue::T::Obj && v.keys.size() == 1 && v.keys[0] == "$t") {
      const JsonValue& k = v.vals[0];
      if (k.t != JsonValue::T::Num || k.num < 0 || k.num >= (double)d.templates.size())
        return err = "a template reference is out of range", false;
      JsonValue t;
      t.t = JsonValue::T::Arr;
      count = 0;
      if (!items(d.templates[(size_t)k.num], t, 0)) return false;
      v = std::move(t);
      return true;
    }
    for (JsonValue& x : v.arr)
      if (!substitute(x, d)) return false;
    for (JsonValue& x : v.vals)
      if (!substitute(x, d)) return false;
    return true;
  }
};

// one patch of a layer: a row for a section's name
struct Patch {
  const char* section;
  std::string name;
  JsonValue row;
  Span span;
  bool host = false;
  const char* what = "";  // the declaration type (diagnostics)
};

// a declaration's row (false: decl-invalid, `err` says why)
bool declRow(const RawOps& raw, const RawDecl& d, Patch& p, std::string& err) {
  JsonValue row;
  row.t = JsonValue::T::Obj;
  for (const ArgVal& a : d.args) {
    if (a.key == ArgK::name && a.tag == ArgTag::Str) p.name = raw.strings[a.ref];
    if (a.key != ArgK::ext) continue;
    std::string_view n = raw.strings[a.name];
    if (n == "row" && a.tag == ArgTag::Str) {
      JsonReader rd;
      JsonValue v;
      if (!rd.parse(raw.strings[a.ref], v) || v.t != JsonValue::T::Obj)
        return err = std::string("its row is not a JSON object: ") + (rd.error() ? rd.error() : "expected an object"),
               false;
      for (size_t i = 0; i < v.keys.size(); i++) {  // explicit scalars beside it win
        if (!member(row, v.keys[i])) Conv::put(row, v.keys[i], std::move(v.vals[i]));
      }
      continue;
    }
    JsonValue v;
    switch (a.tag) {
      case ArgTag::Str: v = Conv::text(raw.strings[a.ref]); break;
      case ArgTag::Num: v.t = JsonValue::T::Num, v.num = a.num; break;
      case ArgTag::Bool: v.t = JsonValue::T::Bool, v.b = a.num != 0; break;
      default: continue;
    }
    if (JsonValue* have = member(row, n)) *have = std::move(v);
    else Conv::put(row, n, std::move(v));
  }
  if (p.name.empty()) return err = "a declaration needs a name", false;
  if (depthOf(row) > kRowDepth) return err = "its row is nested more than 8 deep", false;
  Conv c{raw, {}};
  if (!c.substitute(row, d)) return err = c.err, false;
  p.row = std::move(row);
  return true;
}

// --- the cache -----------------------------------------------------------------
struct Built {
  std::shared_ptr<const Registry> reg;
  std::vector<std::pair<size_t, std::string>> refused;  // patch index, why
};
std::mutex gCacheMu;
std::vector<std::pair<std::string, Built>> gCache;  // most recent first
constexpr size_t kCacheSize = 8;

const JsonValue& builtinRows() {
  static const JsonValue v = [] {
    JsonValue x;
    JsonReader rd;
    rd.parse(kElementsJson, x);
    return x;
  }();
  return v;
}

bool load(const JsonValue& rows, std::unique_ptr<Registry>& out, std::string& err) {
  std::string text;
  jsonDump(text, rows);
  out = Registry::fromJson(text, err);
  return out != nullptr;
}

Built build(std::string_view base, const std::vector<Patch>& patches) {
  JsonValue rows;
  if (base.empty()) {
    rows = builtinRows();
  } else {
    JsonReader rd;
    rd.parse(base, rows);
  }
  auto apply = [&](JsonValue& r, const Patch& p) {
    overlay(memberOrAdd(r, p.section, JsonValue::T::Obj), p.name, p.row);
  };
  Built b;
  JsonValue all = rows;
  for (const Patch& p : patches) apply(all, p);
  std::unique_ptr<Registry> reg;
  std::string err;
  if (load(all, reg, err)) {
    b.reg = std::move(reg);
    return b;
  }
  // some row is refused: find which, one patch at a time
  for (size_t i = 0; i < patches.size(); i++) {
    JsonValue next = rows;
    apply(next, patches[i]);
    std::unique_ptr<Registry> r;
    if (load(next, r, err)) rows = std::move(next);
    else b.refused.push_back({i, err});
  }
  if (!load(rows, reg, err)) {  // cannot happen: the base loaded
    b.reg = std::shared_ptr<const Registry>(&Registry::builtin(), [](const Registry*) {});
    return b;
  }
  b.reg = std::move(reg);
  return b;
}

}  // namespace

bool templateJson(const RawOps& raw, u32 id, JsonValue& out, std::string& error) {
  Conv c{raw, {}};
  out = JsonValue{};
  out.t = JsonValue::T::Arr;
  if (!c.items(id, out, 0)) return error = c.err, false;
  return true;
}

std::shared_ptr<const Registry> declaredRegistry(const RawOps& raw, const IngestSettings& cfg, std::string_view base,
                                                 DiagSink& diags) {
  std::vector<Patch> patches;
  // host rows: semantics.{elements,counters,collectors,systems}
  const std::pair<const std::string*, const char*> host[] = {{&cfg.semSystems, "systems"},
                                                             {&cfg.semCounters, "counters"},
                                                             {&cfg.semElements, "classes"},
                                                             {&cfg.semCollectors, "collectors"}};
  for (const auto& [text, section] : host) {
    if (text->empty()) continue;
    JsonValue v;
    JsonReader rd;
    if (!rd.parse(*text, v) || v.t != JsonValue::T::Obj) continue;  // the settings row checked it
    for (size_t i = 0; i < v.keys.size(); i++) {
      if (v.vals[i].t != JsonValue::T::Obj) {
        diags.add(Sev::Warning, "semantics-invalid", {},
                  std::string("semantics: ") + section + " '" + v.keys[i] + "' is not an object");
        continue;
      }
      for (const std::string& f : v.vals[i].keys)
        if (!knownField(section, f))
          diags.add(Sev::Warning, "semantics-invalid", {},
                    std::string("semantics: ") + section + " '" + v.keys[i] + "': unknown field '" + f + "' (ignored)");
      patches.push_back({section, v.keys[i], std::move(v.vals[i]), {}, true});
    }
  }
  // the document's declarations, in order; a hoisted name's last
  // declaration supersedes the earlier ones whole (instantiate says so)
  auto nameOf = [&](const RawDecl& d) {
    for (const ArgVal& a : d.args)
      if (a.key == ArgK::name && a.tag == ArgTag::Str) return raw.strings[a.ref];
    return std::string_view();
  };
  for (size_t i = 0; i < raw.decls.size(); i++) {
    const RawDecl& d = raw.decls[i];
    const char* section = d.type < DECL_COUNT ? sectionOf(kDecls[d.type].name) : nullptr;
    if (!section) continue;
    bool superseded = false;
    for (size_t j = i + 1; j < raw.decls.size() && !superseded; j++)
      superseded = raw.decls[j].type == d.type && nameOf(raw.decls[j]) == nameOf(d);
    if (superseded) continue;
    Patch p{section, {}, {}, d.span, false, kDecls[d.type].name};
    std::string err;
    if (!declRow(raw, d, p, err)) {
      diags.add(Sev::Warning, "decl-invalid", d.span,
                std::string(p.what) + (p.name.empty() ? "" : " '" + p.name + "'") + ": " + err);
      continue;
    }
    for (const std::string& f : p.row.keys)
      if (!knownField(section, f))
        diags.add(Sev::Warning, "decl-field", d.span,
                  std::string(kDecls[d.type].name) + " '" + p.name + "': unknown field '" + f + "' (ignored)");
    patches.push_back(std::move(p));
  }
  if (patches.empty() && base.empty())
    return std::shared_ptr<const Registry>(&Registry::builtin(), [](const Registry*) {});

  std::string key(base);
  key += '\0';
  for (const Patch& p : patches) {
    key += p.section;
    key += '\0';
    key += p.name;
    key += '\0';
    jsonDump(key, p.row);
    key += '\0';
  }
  Built b;
  {
    std::lock_guard<std::mutex> lock(gCacheMu);
    for (size_t i = 0; i < gCache.size(); i++)
      if (gCache[i].first == key) {
        b = gCache[i].second;
        std::rotate(gCache.begin(), gCache.begin() + (long)i, gCache.begin() + (long)i + 1);
        break;
      }
  }
  if (!b.reg) {
    b = build(base, patches);
    std::lock_guard<std::mutex> lock(gCacheMu);
    gCache.insert(gCache.begin(), {key, b});
    if (gCache.size() > kCacheSize) gCache.pop_back();
  }
  for (const auto& [i, why] : b.refused) {
    const Patch& p = patches[i];
    if (p.host)
      diags.add(Sev::Warning, "semantics-invalid", {}, std::string("semantics: ") + p.section + " '" + p.name + "': " + why);
    else
      diags.add(Sev::Warning, "decl-invalid", p.span, std::string(p.what) + " '" + p.name + "': " + why);
  }
  return b.reg;
}

void checkDeclarations(const RawOps& raw, const ContentTree& tree, const Registry& reg, const Interner& strs,
                       DiagSink& diags) {
  // decl-after-use: the first instance of a declared class before its declaration
  std::vector<std::pair<ClassId, Span>> declared;
  for (const RawDecl& d : raw.decls) {
    if (d.type >= DECL_COUNT || std::string_view(kDecls[d.type].name) != "element" || d.span.empty()) continue;
    for (const ArgVal& a : d.args)
      if (a.key == ArgK::name && a.tag == ArgTag::Str)
        if (ClassId c = reg.classNamed(raw.strings[a.ref])) declared.push_back({c, d.span});
  }
  if (!declared.empty() && tree.root) {
    std::vector<const ContentNode*> work{tree.root};
    std::vector<bool> said(declared.size());
    while (!work.empty()) {
      const ContentNode* n = work.back();
      work.pop_back();
      if (n->cls && !n->span.empty())
        for (size_t i = 0; i < declared.size(); i++)
          if (!said[i] && declared[i].first == n->cls && n->span.end <= declared[i].second.start) {
            said[i] = true;
            diags.add(Sev::Info, "decl-after-use", n->span,
                      "a " + reg.cls(n->cls).name + " before its declaration (declarations are hoisted)");
          }
      for (const ContentNode* k : n->kids) work.push_back(k);
    }
  }
  (void)strs;
  // event-unplaced: an event no EMIT root (or template) reaches
  bool any = false;
  for (const RawNode& n : raw.nodes) any = any || n.kind == Kind::event;
  if (!any) return;
  std::vector<u8> seen(raw.nodes.size());
  std::vector<u32> work;
  auto push = [&](u32 id) {
    if (id < seen.size() && !seen[id]) {
      seen[id] = 1;
      work.push_back(id);
    }
  };
  for (const SchedItem& s : raw.sched)
    if (s.op == Op::EMIT) push(s.a);
  for (const RawDecl& d : raw.decls)
    for (u32 t : d.templates) push(t);
  while (!work.empty()) {
    const RawNode& n = raw.nodes[work.back()];
    work.pop_back();
    for (u32 k : n.children) push(k);
    if (n.alias != kNoAlias) push(n.alias);
    for (const ArgVal& a : n.args)
      if (a.tag == ArgTag::Node) push(a.ref);
  }
  for (size_t i = 0; i < raw.nodes.size(); i++)
    if (raw.nodes[i].kind == Kind::event && !seen[i])
      diags.add(Sev::Warning, "event-unplaced", raw.nodes[i].span,
                "a counterUpdate placed nowhere does nothing: splice its value (#…) where it applies");
}

}  // namespace tsr
