#include "model.h"

#include "../elements/registry.h"
#include "softbreak.h"

#include <algorithm>

namespace tsr {

namespace {
struct Inst {
  const RawOps& raw;
  Arena& arena;
  Interner& strs;
  StyleTable& styles;
  DiagSink& diags;
  const Registry& reg;

  // fold one style attribute onto an effective style
  void applyPatch(Styling& st, const ArgVal& a) {
    applyStyleArg(st, a, [&](u32 ref) { return strs.intern(raw.strings[ref]); },
                  [&](u32 ref) { return raw.strings[ref]; });
  }
  // fold a delta node (plan P2-08: a childless styled node — a STYLE_PUSH's,
  // a node's own `style`) onto an effective style
  void applyDelta(Styling& st, u32 id) {
    for (const ArgVal& a : raw.nodes[id].args) applyPatch(st, a);
  }

  // InstLimits (plan P0-07, D-I03): a DAG value emitted many times is copied
  // per occurrence, so a few bytes of ops can describe an exponential tree.
  // The copy runs on an explicit stack and stops at a node budget and a depth
  // bound; the cut-off subtree becomes an error{inst-limit} node.
  size_t budget = 0;
  bool limitReported = false;
  static constexpr u32 kMaxDepth = 256;

  ContentNode* limitNode(Span sp, const char* what) {
    if (!limitReported) {
      diags.add(Sev::Error, "inst-limit", sp, std::string("document exceeds the ") + what);
      limitReported = true;
    }
    ContentNode* e = arena.make<ContentNode>();
    e->kind = Kind::error;
    e->span = sp;
    e->args.push_back({ArgK::message, ArgTag::Str, 0, strs.intern(std::string("content cut: ") + what)});
    e->args.push_back({ArgK::code, ArgTag::Str, 0, strs.intern("inst-limit")});
    return e;
  }

  ContentNode* copy(u32 rootId, const Styling& inherited) {
    // verb: inside a code or verbatim body, whose newlines are its lines
    struct Pending { ContentNode* parent; u32 id; Styling inh; u32 depth; u16 inside; bool verb; };
    std::vector<Pending> work;
    work.push_back({nullptr, rootId, inherited, 0, 0, false});
    ContentNode* result = nullptr;
    while (!work.empty()) {
      Pending p = std::move(work.back());
      work.pop_back();
      // an AT alias (plan P2-04) is its target at the alias's span
      const RawNode& an = raw.nodes[p.id];
      const RawNode& rn = an.alias != kNoAlias ? raw.nodes[an.alias] : an;
      // span containment (design T2 S7, document-model §1): a node whose span
      // is empty or outside its parent's takes the parent's, so a value made
      // or defined elsewhere stays inside the paragraph that splices it
      Span sp = an.span;
      if (p.parent && !p.parent->span.empty() &&
          (sp.empty() || sp.start < p.parent->span.start || sp.end > p.parent->span.end))
        sp = p.parent->span;
      ContentNode* n;
      bool descend = false;
      Styling ownStyle = p.inh;
      if (budget == 0) {
        n = limitNode(sp, "instantiation node budget");
      } else if (p.depth > kMaxDepth) {
        n = limitNode(sp, "maximum nesting depth");
      } else {
        budget--;
        // a styled node's attributes are its delta; any node's `style` is
        // its own (plan P2-08), applied after
        if (rn.kind == Kind::styled)
          for (const ArgVal& a : rn.args) applyPatch(ownStyle, a);
        for (const ArgVal& a : rn.args)
          if (a.key == ArgK::style && a.tag == ArgTag::Node) applyDelta(ownStyle, a.ref);
        n = arena.make<ContentNode>();
        n->kind = rn.kind;
        n->span = sp;
        n->style = styles.idOf(ownStyle);
        const bool own = an.alias == kNoAlias && sp.start == rn.span.start && sp.end == rn.span.end;
        const std::vector<u32>* map = own && !rn.rawmap.empty() ? &rn.rawmap : nullptr;
        std::vector<u32> resolved;
        if (rn.isText) {
          std::string_view str = raw.strings[rn.str];
          if (!p.verb && str.find('\n') != std::string_view::npos) {  // soft breaks (plan P2-10)
            std::string s(str);
            const u32 rawLen = rn.span.end - rn.span.start;
            const bool mapped = own && !rn.span.empty() && (map || s.size() == rawLen);
            if (map) resolved = *map;
            resolveSoftBreaks(s, resolved, rawLen, mapped);
            n->str = strs.intern(s);
            map = mapped && !resolved.empty() ? &resolved : nullptr;
          } else {
            n->str = strs.intern(str);
          }
        }
        if (map) {
          u32* m = arena.allocArray<u32>(map->size());
          std::copy(map->begin(), map->end(), m);
          n->rawmap = m;
          n->nrawmap = (u32)map->size();
        }
        for (const ArgVal& a : rn.args) {
          if (a.key == ArgK::style) continue;  // folded into n->style
          ArgVal v = a;
          if (a.tag == ArgTag::Str) v.ref = strs.intern(raw.strings[a.ref]);
          if (a.key == ArgK::ext) v.name = strs.intern(raw.strings[a.name]);
          n->args.push_back(v);
        }
        n->kids.reserve(rn.children.size());
        n->cls = reg.classify(n, p.inside, strs);  // membership, once (plan P1-10)
        descend = true;
      }
      if (p.parent) p.parent->kids.push_back(n);
      else result = n;
      if (descend)  // reversed, so children pop (and append) in order
        for (size_t c = rn.children.size(); c-- > 0;)
          work.push_back({n, rn.children[c], ownStyle, p.depth + 1, n->cls ? n->cls : p.inside,
                          p.verb || kKinds[(u16)n->kind].body == Body::Code || kKinds[(u16)n->kind].body == Body::Text});
    }
    return result;
  }
};
}  // namespace

ContentTree instantiate(const RawOps& raw, Arena& arena, Interner& strs,
                        StyleTable& styles, DiagSink& diags, const Registry& reg) {
  ContentTree t;
  ContentNode* root = arena.make<ContentNode>();
  root->kind = Kind::doc;
  t.root = root;
  if (!raw.ok) return t;

  Inst inst{raw, arena, strs, styles, diags, reg};
  inst.budget = std::max<size_t>(kInstMinBudget, kInstPerRawNode * raw.nodes.size());
  std::vector<const SchedItem*> stack;  // schedule deltas (delta nodes)
  auto refold = [&] {
    Styling st{};
    for (const SchedItem* d : stack) inst.applyDelta(st, d->a);
    return st;
  };
  Styling cur{};
  for (const SchedItem& s : raw.sched) {
    switch (s.op) {
      case Op::STYLE_PUSH:
        stack.push_back(&s);
        inst.applyDelta(cur, s.a);
        break;
      case Op::STYLE_POP_TO: {
        u32 h = s.a;
        if (h > stack.size()) {
          diags.add(Sev::Warning, "style-underflow", {}, "STYLE_POP_TO above height");
          h = (u32)stack.size();
        }
        stack.resize(h);
        cur = refold();
        break;
      }
      case Op::EMIT:
        root->kids.push_back(inst.copy(s.a, cur));
        break;
      default:
        break;
    }
  }
  if (root->kids.empty() && !raw.nodes.empty() && raw.decls.empty())
    diags.add(Sev::Warning, "ops-invalid", {}, "buffer has nodes but no EMIT");
  // declarations (plan P2-05; D-I07: read after the whole buffer decoded,
  // in any order): templates instantiate style-neutral (their styles bind
  // where they are used); a hoisted name's last declaration wins
  std::vector<std::pair<std::pair<u16, StrRef>, size_t>> last;
  for (const RawDecl& rd : raw.decls) {
    Decl d;
    d.type = rd.type;
    d.flowIndex = rd.flowIndex;
    d.span = rd.span;
    for (const ArgVal& a : rd.args) {
      ArgVal v = a;
      if (a.tag == ArgTag::Str) v.ref = strs.intern(raw.strings[a.ref]);
      if (a.key == ArgK::name) {
        d.name = v.ref;
        continue;
      }
      if (a.key == ArgK::ext) v.name = strs.intern(raw.strings[a.name]);
      d.args.push_back(v);
    }
    for (u32 id : rd.templates) d.templates.push_back(inst.copy(id, Styling{}));
    if (kDecls[d.type].hoisted) {
      auto it = std::find_if(last.begin(), last.end(),
                             [&](const auto& e) { return e.first.first == d.type && e.first.second == d.name; });
      if (it != last.end()) {
        Decl& prev = t.decls[it->second];
        prev.superseded = true;
        diags.add(Sev::Info, "decl-redeclared", d.span,
                  std::string(kDecls[d.type].name) + " '" + std::string(strs.get(d.name)) +
                      "' declared again: the later declaration wins");
        it->second = t.decls.size();
      } else {
        last.push_back({{d.type, d.name}, t.decls.size()});
      }
    }
    t.decls.push_back(std::move(d));
  }
  root->span = root->kids.empty()
                   ? Span{}
                   : Span{root->kids.front()->span.start, root->kids.back()->span.end};
  normalize(root, strs);  // after the root span: an unwrapped block keeps its span
  return t;
}

static void styleStr(std::string& out, const Styling& s, const Interner& strs) {
  // the flag tokens keep the spellings and order of the class bits they
  // replaced (plan P2-08): script, italic, weight, mono role, decorations,
  // baseline
  out += "[";
  bool first = true;
  auto f = [&](bool on, const char* n) {
    if (on) {
      if (!first) out += "+";
      out += n;
      first = false;
    }
  };
  f(s.script == SCRIPT_CJK, "CJK");
  f(s.italic, "EM");
  f(s.weight == 700, "BOLD");
  if (s.weight && s.weight != 700) {
    if (!first) out += "+";
    appendf(out, "W%u", (unsigned)s.weight);
    first = false;
  }
  f(s.fontRole == FONTROLE_MONO, "CODE");
  f(s.decoration & DECORATION_UNDER, "U");
  f(s.decoration & DECORATION_OVER, "O");
  f(s.decoration & DECORATION_STRIKE, "S");
  f(s.baseline == BASELINE_SUPER, "SUP");
  f(s.baseline == BASELINE_SUB, "SUB");
  if (first) out += "base";
  if (s.sizeMul != 1.0f) appendf(out, "x%.2f", (double)s.sizeMul);
  if (s.fontRole == FONTROLE_BODY) out += " role=body";
  appendStyleFields(out, s, strs);
  out += "]";
}

// a styled node's attributes the style part of its line already shows (the
// rows that were class bits, plan P2-08, and the size multiplier)
static bool hiddenStyleArg(ArgK k) {
  return k == ArgK::weight || k == ArgK::italic || k == ArgK::decoration || k == ArgK::fontRole ||
         k == ArgK::baseline || k == ArgK::size || k == ArgK::hang;
}

static void dumpNode(std::string& out, const ContentNode* n, const Interner& strs,
                     const StyleTable& styles, int depth) {
  for (int i = 0; i < depth; i++) out += "  ";
  appendf(out, "%s @[%u,%u) ", kindName(n->kind), n->span.start, n->span.end);
  styleStr(out, styles.get(n->style), strs);
  if (n->kind == Kind::text) {
    out += " str=\"";
    appendEscaped(out, strs.get(n->str));
    out += "\"";
    for (u32 k = 0; k < n->nrawmap; k += 2)  // the cooked→raw map (plan P2-04)
      appendf(out, "%s%u:%u", k ? "," : " raw=", n->rawmap[k], n->rawmap[k + 1]);
  }
  for (const ArgVal& a : n->args) {
    if (n->kind == Kind::styled && hiddenStyleArg(a.key)) continue;  // shown via style
    if (a.key == ArgK::ext) appendf(out, " ext.%s=", std::string(strs.get(a.name)).c_str());
    else appendf(out, " %s=", argName(a.key));
    switch (a.tag) {
      case ArgTag::Null: out += "null"; break;
      case ArgTag::Bool: out += a.num ? "true" : "false"; break;
      case ArgTag::Num: appendf(out, "%g", a.num); break;
      case ArgTag::Str:
        out += "\"";
        appendEscaped(out, strs.get(a.ref));
        out += "\"";
        break;
      case ArgTag::Node: appendf(out, "%%%u", a.ref); break;
    }
  }
  out += "\n";
  for (const ContentNode* k : n->kids) dumpNode(out, k, strs, styles, depth + 1);
}

std::string dumpTree(const ContentTree& t, const Interner& strs, const StyleTable& styles) {
  std::string out;
  if (t.root) dumpNode(out, t.root, strs, styles, 0);
  for (const Decl& d : t.decls) {  // declarations (plan P2-05), after the tree
    appendf(out, "decl %s name=\"", kDecls[d.type].name);
    appendEscaped(out, strs.get(d.name));
    appendf(out, "\" flow=%u @[%u,%u)%s", d.flowIndex, d.span.start, d.span.end, d.superseded ? " superseded" : "");
    for (const ArgVal& a : d.args) {
      appendf(out, " ext.%s=", std::string(strs.get(a.name)).c_str());
      if (a.tag == ArgTag::Str) {
        out += "\"";
        appendEscaped(out, strs.get(a.ref));
        out += "\"";
      } else if (a.tag == ArgTag::Bool) out += a.num ? "true" : "false";
      else appendf(out, "%g", a.num);
    }
    out += "\n";
    for (const ContentNode* k : d.templates) dumpNode(out, k, strs, styles, 1);
  }
  return out;
}

}  // namespace tsr
