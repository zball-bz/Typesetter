#include "model.h"

namespace tsr {

namespace {
struct Inst {
  const RawOps& raw;
  Arena& arena;
  Interner& strs;
  StyleTable& styles;
  DiagSink& diags;

  ContentNode* copy(u32 id, u64 bits) {
    const RawNode& rn = raw.nodes[id];
    u64 own = bits;
    if (rn.kind == Kind::styled) {
      for (const ArgVal& a : rn.args)
        if (a.key == ArgK::bits && a.tag == ArgTag::Num) own |= (u64)a.num;
    }
    ContentNode* n = arena.make<ContentNode>();
    n->kind = rn.kind;
    n->span = rn.span;
    n->style = styles.idOf({own});
    if (rn.isText) n->str = strs.intern(raw.strings[rn.str]);
    for (const ArgVal& a : rn.args) {
      ArgVal v = a;
      if (a.tag == ArgTag::Str) v.ref = strs.intern(raw.strings[a.ref]);
      n->args.push_back(v);
    }
    n->kids.reserve(rn.children.size());
    for (u32 c : rn.children) n->kids.push_back(copy(c, own));
    return n;
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
  std::vector<u64> stack;  // schedule style stack (bits deltas)
  u64 cur = 0;
  for (const SchedItem& s : raw.sched) {
    switch (s.op) {
      case Op::STYLE_PUSH:
        stack.push_back(s.bits);
        cur |= s.bits;
        break;
      case Op::STYLE_POP_TO: {
        u32 h = s.a;
        if (h > stack.size()) {
          diags.add(Sev::Warning, "style-underflow", {}, "STYLE_POP_TO above height");
          h = (u32)stack.size();
        }
        stack.resize(h);
        cur = 0;
        for (u64 b : stack) cur |= b;
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
  return t;
}

static void styleStr(std::string& out, const Styling& s) {
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
  if (first) out += "base";
  if (s.sizeMul != 1.0f) appendf(out, "x%.2f", (double)s.sizeMul);
  out += "]";
}

static void dumpNode(std::string& out, const ContentNode* n, const Interner& strs,
                     const StyleTable& styles, int depth) {
  for (int i = 0; i < depth; i++) out += "  ";
  appendf(out, "%s @[%u,%u) ", kindName(n->kind), n->span.start, n->span.end);
  styleStr(out, styles.get(n->style));
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
