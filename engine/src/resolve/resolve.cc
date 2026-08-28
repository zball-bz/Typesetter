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
    case Kind::hardbreak: case Kind::raw: case Kind::note:
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

  // footnotes (notes-design.md §1): document-order counter; bodies are
  // lifted into the `notes` collector (implicit at document end)
  int noteNo = 0;
  std::vector<ContentNode*> notes;
  bool notesPlaced = false;

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
          // caption prefix (figure-design.md §1): 图 n： bolded into the
          // first paragraph child; captionless figures keep just the number
          for (ContentNode* k : n->kids) {
            if (k->kind != Kind::para) continue;
            ContentNode* t =
                mkText(cfg.supFigure + std::to_string(figNo) + "\xEF\xBC\x9A",
                       k->span, styles.idOf(Styling{CLS_BOLD, 1.0f}));
            k->kids.insert(k->kids.begin(), t);
            break;
          }
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
      case Kind::note: {
        noteNo++;
        std::string num = std::to_string(noteNo);
        setArgStr(n, ArgK::name, num);
        // fn-n: the note body (list item, back-link target of the marker);
        // fnref-n: the marker itself (inline anchor, target of the ↩)
        addLabel("fn-" + num, {Kind::note, num, ""}, n->span);
        addLabel("fnref-" + num, {Kind::ref, num, "\xE2\x86\xA9"}, n->span);
        notes.push_back(n);
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
      case Kind::note: disp = e.number; break;  // bare digit (marker / @fn-n)
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

  // ---- footnotes (notes-design.md §1) ------------------------------------
  // the marker: a superscript ref to the note's list item, itself anchored
  // (fnref-n) so the note's ↩ can return. Style composes on the note's own
  // style so color/font scopes carry into the marker.
  ContentNode* buildMarker(ContentNode* note) {
    std::string num(strs.get(argStr(note, ArgK::name)));
    Styling s = styles.get(note->style);
    s.bits |= CLS_SUP;
    s.sizeMul *= 0.7f;
    ContentNode* r = mkNode(Kind::ref, note->span, styles.idOf(s));
    setArgStr(r, ArgK::target, "fn-" + num);
    setArgStr(r, ArgK::label, "fnref-" + num);
    return r;
  }
  void rescale(ContentNode* n, float f) {
    Styling s = styles.get(n->style);
    s.sizeMul *= f;
    n->style = styles.idOf(s);
    for (ContentNode* k : n->kids) rescale(k, f);
  }
  // the notes section: rule + ordered list, one item per note in document
  // order (the list marker IS the number, matching the superscripts);
  // bodies at 0.85× with a ↩ back-link. Built once: explicitly at
  // #notes(), else appended to the document by resolveDoc.
  ContentNode* buildNotes(Span span) {
    notesPlaced = true;
    ContentNode* g = mkNode(Kind::group, span);
    setArgStr(g, ArgK::role, "notes");
    if (notes.empty()) return g;
    g->kids.push_back(mkNode(Kind::rule, span));
    ContentNode* list = mkNode(Kind::list, span);
    setArgBool(list, ArgK::ordered, true);
    for (size_t i = 0; i < notes.size(); i++) {
      ContentNode* nd = notes[i];
      rewrite(nd);  // refs inside the body resolve like anywhere else
      std::string num = std::to_string(i + 1);
      ContentNode* item = mkNode(Kind::item, nd->span);
      ContentNode* para = mkNode(Kind::para, nd->span);
      setArgStr(para, ArgK::label, "fn-" + num);
      for (ContentNode* k : nd->kids) {
        rescale(k, 0.85f);
        para->kids.push_back(k);
      }
      Styling small = styles.get(nd->style);
      small.sizeMul *= 0.85f;
      StyleId smallId = styles.idOf(small);
      para->kids.push_back(mkText(" ", nd->span, smallId));
      ContentNode* back = mkNode(Kind::ref, nd->span, smallId);
      setArgStr(back, ArgK::target, "fnref-" + num);
      resolveRef(back);
      para->kids.push_back(back);
      item->kids.push_back(para);
      list->kids.push_back(item);
    }
    g->kids.push_back(list);
    return g;
  }

  ContentNode* buildCollect(ContentNode* c) {
    std::string what(strs.get(argStr(c, ArgK::what)));
    if (what == "toc") return buildToc(c);
    if (what == "glossary") return buildGlossary(c);
    if (what == "notes") return buildNotes(c->span);
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
      // a paragraph that is nothing but one block-level splice (#term,
      // #toc, #codeblock(...), a handler-built table, …) IS that construct
      // at block level — unwrap before dispatching (CH1: generalized from
      // the term/collect special case to every non-inline kind)
      if (k->kind == Kind::para && k->kids.size() == 1 &&
          !isInlineKind(k->kids[0]->kind) && k->kids[0]->kind != Kind::para)
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
      if (k->kind == Kind::note) {
        // the body leaves the paragraph (buildNotes lifts it); the marker
        // stays — a resolved superscript reference
        n->kids[i] = buildMarker(k);
        resolveRef(n->kids[i]);
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
  // implicit notes section (notes-design.md §1): at document end unless
  // the author placed #notes() themselves
  if (!r.notes.empty() && !r.notesPlaced)
    tree.root->kids.push_back(r.buildNotes(tree.root->span));
}

}  // namespace tsr
