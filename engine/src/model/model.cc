#include "model.h"

#include <algorithm>

namespace tsr {

namespace {
struct Inst {
  const RawOps& raw;
  Arena& arena;
  Interner& strs;
  StyleTable& styles;
  DiagSink& diags;

  // fold one delta (bits + InlineStyle patch args) onto an effective style
  void applyPatch(Styling& st, const ArgVal& a) {
    switch (a.key) {
      case ArgK::bits:
        // a bit set is a non-negative integer (fuzz: negative-bits)
        if (a.tag == ArgTag::Num && a.num >= 0) st.bits |= (u64)a.num;
        break;
      case ArgK::font:
        if (a.tag == ArgTag::Str) st.fontFamily = strs.intern(raw.strings[a.ref]);
        break;
      case ArgK::lang:
        if (a.tag == ArgTag::Str) st.lang = strs.intern(raw.strings[a.ref]);
        break;
      case ArgK::color:
        if (a.tag == ArgTag::Str) st.color = strs.intern(raw.strings[a.ref]);
        break;
      case ArgK::sizePx:
        if (a.tag == ArgTag::Num) st.sizePx = (float)a.num;
        break;
      default:
        break;
    }
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
    struct Pending { ContentNode* parent; u32 id; Styling inh; u32 depth; };
    std::vector<Pending> work;
    work.push_back({nullptr, rootId, inherited, 0});
    ContentNode* result = nullptr;
    while (!work.empty()) {
      Pending p = std::move(work.back());
      work.pop_back();
      const RawNode& rn = raw.nodes[p.id];
      ContentNode* n;
      bool descend = false;
      Styling own = p.inh;
      if (budget == 0) {
        n = limitNode(rn.span, "instantiation node budget");
      } else if (p.depth > kMaxDepth) {
        n = limitNode(rn.span, "maximum nesting depth");
      } else {
        budget--;
        if (rn.kind == Kind::styled)
          for (const ArgVal& a : rn.args) applyPatch(own, a);
        n = arena.make<ContentNode>();
        n->kind = rn.kind;
        n->span = rn.span;
        n->style = styles.idOf(own);
        if (rn.isText) n->str = strs.intern(raw.strings[rn.str]);
        for (const ArgVal& a : rn.args) {
          ArgVal v = a;
          if (a.tag == ArgTag::Str) v.ref = strs.intern(raw.strings[a.ref]);
          n->args.push_back(v);
        }
        n->kids.reserve(rn.children.size());
        descend = true;
      }
      if (p.parent) p.parent->kids.push_back(n);
      else result = n;
      if (descend)  // reversed, so children pop (and append) in order
        for (size_t c = rn.children.size(); c-- > 0;)
          work.push_back({n, rn.children[c], own, p.depth + 1});
    }
    return result;
  }
};
}  // namespace

ContentTree instantiate(const RawOps& raw, Arena& arena, Interner& strs,
                        StyleTable& styles, DiagSink& diags) {
  ContentTree t;
  ContentNode* root = arena.make<ContentNode>();
  root->kind = Kind::doc;
  t.root = root;
  if (!raw.ok) return t;

  Inst inst{raw, arena, strs, styles, diags};
  inst.budget = std::max<size_t>(kInstMinBudget, kInstPerRawNode * raw.nodes.size());
  std::vector<const SchedItem*> stack;  // schedule deltas (bits + patches)
  auto refold = [&] {
    Styling st{};
    for (const SchedItem* d : stack) {
      st.bits |= d->bits;
      for (const ArgVal& a : d->patch) inst.applyPatch(st, a);
    }
    return st;
  };
  Styling cur{};
  for (const SchedItem& s : raw.sched) {
    switch (s.op) {
      case Op::STYLE_PUSH:
        stack.push_back(&s);
        cur.bits |= s.bits;
        for (const ArgVal& a : s.patch) inst.applyPatch(cur, a);
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
  if (root->kids.empty() && !raw.nodes.empty())
    diags.add(Sev::Warning, "ops-invalid", {}, "buffer has nodes but no EMIT");
  root->span = root->kids.empty()
                   ? Span{}
                   : Span{root->kids.front()->span.start, root->kids.back()->span.end};
  normalize(root, strs);  // after the root span: an unwrapped block keeps its span
  return t;
}

static void styleStr(std::string& out, const Styling& s, const Interner& strs) {
  out += "[";
  bool first = true;
  auto f = [&](u64 bit, const char* n) {
    if (s.bits & bit) {
      if (!first) out += "+";
      out += n;
      first = false;
    }
  };
  f(CLS_LATIN, "LATIN");
  f(CLS_CJK, "CJK");
  f(CLS_EM, "EM");
  f(CLS_BOLD, "BOLD");
  f(CLS_CODE, "CODE");
  f(CLS_LINK, "LINK");
  f(CLS_UNDER, "U");
  f(CLS_OVER, "O");
  f(CLS_STRIKE, "S");
  if (first) out += "base";
  if (s.sizeMul != 1.0f) appendf(out, "x%.2f", (double)s.sizeMul);
  if (s.fontFamily) {
    out += " font=\"";
    appendEscaped(out, strs.get(s.fontFamily));
    out += "\"";
  }
  if (s.lang) {
    out += " lang=";
    out += strs.get(s.lang);
  }
  if (s.color) {
    out += " color=";
    out += strs.get(s.color);
  }
  if (s.sizePx > 0) appendf(out, " size=%gpx", (double)s.sizePx);
  out += "]";
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
  }
  for (const ArgVal& a : n->args) {
    if (n->kind == Kind::styled && a.key == ArgK::bits) continue;  // shown via style
    appendf(out, " %s=", argName(a.key));
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
  return out;
}

}  // namespace tsr
