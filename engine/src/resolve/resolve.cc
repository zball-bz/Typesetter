#include "resolve.h"

namespace tsr {

namespace {

const ArgVal* findArg(const ContentNode* n, ArgK k) {
  for (const ArgVal& a : n->args)
    if (a.key == k) return &a;
  return nullptr;
}
StrRef argStr(const ContentNode* n, ArgK k) {
  const ArgVal* a = findArg(n, k);
  return (a && a->tag == ArgTag::Str) ? a->ref : 0;
}

void excerptInto(const ContentNode* n, const Interner& strs, std::string& out) {
  if (n->kind == Kind::comment) return;
  if (n->kind == Kind::text) {
    out += strs.get(n->str);
    return;
  }
  for (const ContentNode* k : n->kids) excerptInto(k, strs, out);
}

bool isInlineKind(Kind k) {
  switch (k) {
    case Kind::text: case Kind::styled: case Kind::link: case Kind::code:
    case Kind::ref: case Kind::seq: case Kind::mathinline: case Kind::comment:
    case Kind::hardbreak: case Kind::raw:
      return true;
    default:
      return false;
  }
}

struct Entry {
  Kind kind;
  std::string number;   // heading "2.1"; table/figure ordinal
  std::string excerpt;  // heading text / term name
};

struct Resolver {
  Arena& arena;
  Interner& strs;
  StyleTable& styles;
  const Config& cfg;
  DiagSink& diags;

  std::unordered_map<std::string, Entry> labels;
  struct TocItem {
    int level;
    std::string number, text, anchor;
  };
  std::vector<TocItem> toc;
  struct GlossItem {
    std::string name, desc;
  };
  std::vector<GlossItem> gloss;

  std::vector<int> secc;  // section counter stack (derived heading tree)
  int tableNo = 0, figNo = 0, eqNo = 0;

  // ---- node fabrication ---------------------------------------------------
  ContentNode* mkNode(Kind k, Span span, StyleId style = 0) {
    ContentNode* n = arena.make<ContentNode>();
    n->kind = k;
    n->span = span;
    n->style = style;
    return n;
  }
  ContentNode* mkText(std::string_view s, Span span, StyleId style = 0) {
    ContentNode* n = mkNode(Kind::text, span, style);
    n->str = strs.intern(s);
    return n;
  }
  void setArgStr(ContentNode* n, ArgK k, std::string_view v) {
    for (ArgVal& a : n->args)
      if (a.key == k) {
        a.tag = ArgTag::Str;
        a.ref = strs.intern(v);
        return;
      }
    ArgVal a;
    a.key = k;
    a.tag = ArgTag::Str;
    a.ref = strs.intern(v);
    n->args.push_back(a);
  }
  void setArgBool(ContentNode* n, ArgK k, bool v) {
    ArgVal a;
    a.key = k;
    a.tag = ArgTag::Bool;
    a.num = v ? 1 : 0;
    n->args.push_back(a);
  }
  ContentNode* mkLink(std::string_view anchor, std::string_view text, Span span) {
    ContentNode* l = mkNode(Kind::link, span);
    setArgStr(l, ArgK::url, "#tsr-" + std::string(anchor));
    l->kids.push_back(mkText(text, span));
    return l;
  }

  // ---- pass 1: counters + label table -------------------------------------
  bool addLabel(const std::string& label, Entry e, Span span) {
    auto it = labels.find(label);
    if (it != labels.end()) {
      diags.add(Sev::Warning, "label-duplicate", span,
                "label '" + label + "' declared twice (first wins)");
      return false;
    }
    labels.emplace(label, std::move(e));
    return true;
  }

  void scan(ContentNode* n) {
    switch (n->kind) {
      case Kind::heading: {
        int level = 1;
        const ArgVal* la = findArg(n, ArgK::level);
        if (la && la->tag == ArgTag::Num) level = (int)la->num;
        if (level < 1) level = 1;
        if (level > 6) level = 6;
        if ((int)secc.size() < level) secc.resize((size_t)level, 0);
        secc.resize((size_t)level);
        secc[(size_t)level - 1]++;
        std::string num;
        for (size_t i = 0; i < secc.size(); i++) {
          if (i) num += '.';
          num += std::to_string(secc[i]);
        }
        std::string text;
        excerptInto(n, strs, text);
        std::string label(strs.get(argStr(n, ArgK::label)));
        std::string autoLabel = "h-" + num;  // every heading is a TOC anchor
        if (label.empty() || !addLabel(label, {Kind::heading, num, text}, n->span)) {
          label = autoLabel;
          setArgStr(n, ArgK::label, label);
          addLabel(label, {Kind::heading, num, text}, n->span);
        }
        toc.push_back({level, num, text, label});
        break;
      }
      case Kind::table: {
        tableNo++;
        std::string label(strs.get(argStr(n, ArgK::label)));
        if (!label.empty())
          addLabel(label, {Kind::table, std::to_string(tableNo), ""}, n->span);
        break;
      }
      case Kind::group: {
        if (std::string_view(strs.get(argStr(n, ArgK::role))) == "figure") {
          figNo++;
          std::string label(strs.get(argStr(n, ArgK::label)));
          if (!label.empty())
            addLabel(label, {Kind::group, std::to_string(figNo), ""}, n->span);
        }
        break;
      }
      case Kind::mathblock: {
        // labelled display formulas number sequentially; the tag renders at
        // the right margin (emit reads ArgK::name)
        StrRef label = argStr(n, ArgK::label);
        if (label) {
          eqNo++;
          std::string num = std::to_string(eqNo);
          addLabel(std::string(strs.get(label)), {Kind::mathblock, num, ""},
                   n->span);
          setArgStr(n, ArgK::name, "(" + num + ")");
        }
        break;
      }
      case Kind::term: {
        std::string name(strs.get(argStr(n, ArgK::name)));
        if (!name.empty()) {
          setArgStr(n, ArgK::label, name);  // auto-label = the term name
          std::string desc;
          for (const ContentNode* k : n->kids) excerptInto(k, strs, desc);
          addLabel(name, {Kind::term, "", name}, n->span);
          gloss.push_back({name, desc});
        }
        break;
      }
      default:
        break;
    }
    for (ContentNode* k : n->kids) scan(k);
  }

  // ---- pass 2: REF rewriting + collector/term expansion -------------------
  void resolveRef(ContentNode* r) {
    std::string target(strs.get(argStr(r, ArgK::target)));
    r->kids.clear();
    auto it = labels.find(target);
    if (it == labels.end()) {
      diags.add(Sev::Warning, "ref-unresolved", r->span,
                "reference '" + target + "' has no label");
      r->kids.push_back(mkText("??", r->span, r->style));
      return;
    }
    const Entry& e = it->second;
    std::string disp;
    switch (e.kind) {
      case Kind::heading: disp = cfg.supHeading + e.number; break;
      case Kind::table: disp = cfg.supTable + e.number; break;
      case Kind::group: disp = cfg.supFigure + e.number; break;
      case Kind::mathblock: disp = cfg.supEquation + "(" + e.number + ")"; break;
      default: disp = e.excerpt.empty() ? target : e.excerpt; break;
    }
    setArgStr(r, ArgK::url, "#tsr-" + target);
    r->kids.push_back(mkText(disp, r->span, r->style));
  }

  ContentNode* buildToc(const ContentNode* c) {
    ContentNode* rootList = mkNode(Kind::list, c->span);
    setArgBool(rootList, ArgK::ordered, false);
    std::vector<ContentNode*> stack{rootList};
    int minLevel = 7;
    for (const TocItem& t : toc)
      if (t.level < minLevel) minLevel = t.level;
    for (const TocItem& t : toc) {
      size_t depth = (size_t)(t.level - minLevel);
      while (stack.size() > depth + 1) stack.pop_back();
      while (stack.size() < depth + 1) {
        ContentNode* parentList = stack.back();
        if (parentList->kids.empty())
          parentList->kids.push_back(mkNode(Kind::item, c->span));
        ContentNode* sub = mkNode(Kind::list, c->span);
        setArgBool(sub, ArgK::ordered, false);
        parentList->kids.back()->kids.push_back(sub);
        stack.push_back(sub);
      }
      ContentNode* item = mkNode(Kind::item, c->span);
      ContentNode* para = mkNode(Kind::para, c->span);
      para->kids.push_back(mkLink(t.anchor, t.number + " " + t.text, c->span));
      item->kids.push_back(para);
      stack.back()->kids.push_back(item);
    }
    return rootList;
  }

  ContentNode* buildGlossary(const ContentNode* c) {
    ContentNode* list = mkNode(Kind::list, c->span);
    setArgBool(list, ArgK::ordered, false);
    for (const GlossItem& g : gloss) {
      ContentNode* item = mkNode(Kind::item, c->span);
      ContentNode* para = mkNode(Kind::para, c->span);
      para->kids.push_back(mkLink(g.name, g.name, c->span));
      if (!g.desc.empty()) para->kids.push_back(mkText(" \xE2\x80\x94 " + g.desc, c->span));
      item->kids.push_back(para);
      list->kids.push_back(item);
    }
    return list;
  }

  ContentNode* buildCollect(ContentNode* c) {
    std::string what(strs.get(argStr(c, ArgK::what)));
    if (what == "toc") return buildToc(c);
    if (what == "glossary") return buildGlossary(c);
    diags.add(Sev::Warning, "collect-unknown", c->span,
              "unknown collector '" + what + "'");
    return mkNode(Kind::group, c->span);
  }

  // term → group{role:"term"}: bold name line (dash-joined inline desc),
  // then any block-level description children.
  ContentNode* buildTerm(ContentNode* t) {
    std::string name(strs.get(argStr(t, ArgK::name)));
    ContentNode* g = mkNode(Kind::group, t->span, t->style);
    setArgStr(g, ArgK::role, "term");
    StrRef label = argStr(t, ArgK::label);
    if (label) setArgStr(g, ArgK::label, strs.get(label));
    ContentNode* namePara = mkNode(Kind::para, t->span, t->style);
    namePara->kids.push_back(
        mkText(name, t->span, styles.idOf(Styling{CLS_BOLD, 1.0f})));
    bool sep = false;
    for (ContentNode* k : t->kids) {
      if (isInlineKind(k->kind)) {
        if (!sep) {
          namePara->kids.push_back(mkText(" \xE2\x80\x94 ", t->span, t->style));
          sep = true;
        }
        namePara->kids.push_back(k);
      }
    }
    g->kids.push_back(namePara);
    for (ContentNode* k : t->kids)
      if (!isInlineKind(k->kind)) g->kids.push_back(k);
    return g;
  }

  void rewrite(ContentNode* n) {
    for (size_t i = 0; i < n->kids.size(); i++) {
      ContentNode* k = n->kids[i];
      // a paragraph that is nothing but one splice (#term / #toc / #glossary)
      // is that construct at block level — unwrap before dispatching
      if (k->kind == Kind::para && k->kids.size() == 1 &&
          (k->kids[0]->kind == Kind::term || k->kids[0]->kind == Kind::collect))
        k = n->kids[i] = k->kids[0];
      if (k->kind == Kind::collect) {
        n->kids[i] = buildCollect(k);
        continue;  // built subtrees contain no refs/collects
      }
      if (k->kind == Kind::term) {
        n->kids[i] = buildTerm(k);
        rewrite(n->kids[i]);
        continue;
      }
      if (k->kind == Kind::ref) {
        resolveRef(k);
        continue;
      }
      rewrite(k);
    }
  }
};

}  // namespace

void resolveDoc(ContentTree& tree, Arena& arena, Interner& strs,
                StyleTable& styles, const Config& cfg, DiagSink& diags) {
  if (!tree.root) return;
  Resolver r{arena, strs, styles, cfg, diags, {}, {}, {}, {}, 0, 0};
  r.scan(tree.root);
  r.rewrite(tree.root);
}

}  // namespace tsr
