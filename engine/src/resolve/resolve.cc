#include "resolve.h"

namespace tsr {

namespace {

const ArgVal* findArg(const ContentNode* n, ArgK k) { return attr(n, k); }
StrRef argStr(const ContentNode* n, ArgK k) { return attrStr(n, k); }

void excerptInto(const ContentNode* n, const Interner& strs, std::string& out) {
  if (n->kind == Kind::comment) return;
  if (n->kind == Kind::text) {
    out += strs.get(n->str);
    return;
  }
  for (const ContentNode* k : n->kids) excerptInto(k, strs, out);
}

struct Entry {
  Kind kind;
  std::string number;   // heading "2.1"; table/figure ordinal
  std::string excerpt;  // heading text / term name
  bool numbered = true; // false: a classless target (displays its label)
};

// Label shapes the engine mints (plan P0-09 e): h-<digits(.digits)*>,
// fn-<digits>, fnref-<digits>, bib-<anything>. A user label of such a shape
// would collide with a generated anchor, so it is reserved. h-index is legal.
bool reservedShape(std::string_view l) {
  auto digitsDots = [](std::string_view t, bool dots) {
    if (t.empty()) return false;
    bool prevDot = true;
    for (char c : t) {
      if (c >= '0' && c <= '9') { prevDot = false; continue; }
      if (dots && c == '.' && !prevDot) { prevDot = true; continue; }
      return false;
    }
    return !prevDot;
  };
  // the alias rules the resolver mints: prefix, and the shape of the rest
  // (0 = anything, 1 = digits, 2 = dotted digits); data until P3-04's
  // AliasRule rows generate them
  static const struct { std::string_view prefix; int shape; } kShapes[] = {
      {"bib-", 0}, {"h-", 2}, {"fn-", 1}, {"fnref-", 1}};
  for (const auto& sh : kShapes)
    if (l.substr(0, sh.prefix.size()) == sh.prefix)
      return sh.shape == 0 || digitsDots(l.substr(sh.prefix.size()), sh.shape == 2);
  return false;
}

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

  // citations (notes-design.md §2): entries come from the executor
  // (collect{what:bibliography} kids = group{role:bibentry, name:key});
  // @key resolves against labels first, then this table; ordinals are
  // assigned in first-citation order
  std::unordered_map<std::string, ContentNode*> bib;
  std::unordered_map<std::string, int> citeNo;
  std::vector<std::string> citeOrder;
  int bibBuilt = 0;  // bibliography collectors built so far (rows clone after the first)
  int collecting = 0;  // collectors being built (no collector nests in built content)

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
  void dropArg(ContentNode* n, ArgK k) {
    for (size_t i = 0; i < n->args.size(); i++)
      if (n->args[i].key == k) {
        n->args.erase(n->args.begin() + (long)i);
        return;
      }
  }
  // generated links take their site's style (no absolute styling, P0-09 f)
  ContentNode* mkLink(std::string_view anchor, std::string_view text, Span span,
                      StyleId style = 0) {
    ContentNode* l = mkNode(Kind::link, span, style);
    setArgStr(l, ArgK::url, "#tsr-" + std::string(anchor));
    l->kids.push_back(mkText(text, span, style));
    return l;
  }
  ContentNode* clone(const ContentNode* n) {
    ContentNode* c = arena.make<ContentNode>();
    *c = *n;
    for (ContentNode*& k : c->kids) k = clone(k);
    return c;
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
  // A user label on node n (plan P0-09 c/d/e): reserved shapes and losing
  // duplicates are dropped from the node, so no DOM id is emitted twice.
  // Returns the registered label, or "" when none.
  std::string registerUserLabel(ContentNode* n, Entry e) {
    std::string label(strs.get(argStr(n, ArgK::label)));
    if (label.empty()) return "";
    if (reservedShape(label)) {
      diags.add(Sev::Warning, "label-reserved", n->span,
                "label '" + label + "' has the shape of a generated anchor and is ignored");
      dropArg(n, ArgK::label);
      return "";
    }
    if (!addLabel(label, std::move(e), n->span)) {
      dropArg(n, ArgK::label);
      return "";
    }
    return label;
  }

  void scan(ContentNode* n) {
    switch (n->kind) {
      case Kind::heading: {
        int level = 1;
        level = attrInt(n, ArgK::level, level);
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
        std::string label = registerUserLabel(n, {Kind::heading, num, text});
        if (label.empty()) {  // every heading is a TOC anchor: h-<number>
          label = "h-" + num;
          setArgStr(n, ArgK::label, label);
          addLabel(label, {Kind::heading, num, text}, n->span);
        }
        toc.push_back({level, num, text, label});
        break;
      }
      case Kind::table: {
        tableNo++;
        registerUserLabel(n, {Kind::table, std::to_string(tableNo), ""});
        break;
      }
      case Kind::group: {
        if (std::string_view(strs.get(argStr(n, ArgK::role))) == "figure") {
          figNo++;
          registerUserLabel(n, {Kind::group, std::to_string(figNo), ""});
          // caption prefix (figure-design.md §1): 图 n： bolded into the
          // first paragraph child; captionless figures keep just the number
          for (ContentNode* k : n->kids) {
            if (k->kind != Kind::para) continue;
            ContentNode* t = mkText(cfg.supFigure + std::to_string(figNo) + cfg.capSep,
                                    k->span, compose(styles, k->style, CLS_BOLD));
            k->kids.insert(k->kids.begin(), t);
            break;
          }
        } else {
          registerUserLabel(n, {Kind::group, "", "", /*numbered=*/false});
        }
        break;
      }
      case Kind::mathblock: {
        // labelled display formulas number sequentially; the tag renders at
        // the right margin (emit reads ArgK::name)
        if (!registerUserLabel(n, {Kind::mathblock, std::to_string(eqNo + 1), ""}).empty()) {
          eqNo++;
          setArgStr(n, ArgK::name, "(" + std::to_string(eqNo) + ")");
        }
        break;
      }
      case Kind::collect: {
        if (std::string_view(strs.get(argStr(n, ArgK::what))) == "bibliography")
          for (ContentNode* k : n->kids) {
            std::string key(strs.get(argStr(k, ArgK::name)));
            if (!key.empty() && !bib.count(key)) bib.emplace(key, k);
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
      case Kind::doc: case Kind::para: case Kind::list: case Kind::item: case Kind::quote:
      case Kind::codeblock: case Kind::rule: case Kind::trow: case Kind::tcell:
      case Kind::error: case Kind::comment: case Kind::text: case Kind::styled:
      case Kind::link: case Kind::code: case Kind::ref: case Kind::mathinline:
      case Kind::raw: case Kind::hardbreak: case Kind::seq: case Kind::image:
        // every labelled node registers (P0-09 d): a classless target
        // displays its label text (ref-unnumbered)
        registerUserLabel(n, {n->kind, "", "", /*numbered=*/false});
        break;
    }
    for (ContentNode* k : n->kids) scan(k);
  }

  // Citation ordinals in document order, note bodies counted at their
  // markers (P0-09 a): a walk over the tree before rewrite lifts any note.
  void orderCites(const ContentNode* n) {
    if (n->kind == Kind::ref) {
      std::string target(strs.get(argStr(n, ArgK::target)));
      if (!labels.count(target))
        for (const std::string& k : splitKeys(target))
          if (bib.count(k)) citeOrdinal(k);
    }
    if (n->kind == Kind::collect) return;  // entries are not citations
    for (const ContentNode* k : n->kids) orderCites(k);
  }
  static std::vector<std::string> splitKeys(const std::string& target) {
    std::vector<std::string> keys;
    size_t at = 0;
    while (at <= target.size()) {
      size_t comma = target.find(',', at);
      if (comma == std::string::npos) comma = target.size();
      std::string k = target.substr(at, comma - at);
      while (!k.empty() && k.front() == ' ') k.erase(k.begin());
      while (!k.empty() && k.back() == ' ') k.pop_back();
      if (!k.empty()) keys.push_back(k);
      at = comma + 1;
    }
    return keys;
  }

  // ---- pass 2: REF rewriting + collector/term expansion -------------------
  int citeOrdinal(const std::string& key) {
    auto it = citeNo.find(key);
    if (it != citeNo.end()) return it->second;
    int n = (int)citeOrder.size() + 1;
    citeNo.emplace(key, n);
    citeOrder.push_back(key);
    return n;
  }
  // @key / @[k1, k2] against the bibliography: numeric style "[1]" /
  // "[1, 2]", each number linking to its entry (bib-<key>)
  // grouped citations resolve per key (P0-09 b): an unknown key shows ?? in
  // its own slot and is diagnosed by name; the others still link
  bool resolveCite(ContentNode* r, const std::string& target) {
    std::vector<std::string> keys = splitKeys(target);
    bool any = false;
    for (const std::string& k : keys) any = any || bib.count(k);
    if (!any) return false;
    r->kids.push_back(mkText("[", r->span, r->style));
    for (size_t i = 0; i < keys.size(); i++) {
      if (i) r->kids.push_back(mkText(", ", r->span, r->style));
      if (!bib.count(keys[i])) {
        diags.add(Sev::Warning, "ref-unresolved", r->span,
                  "citation key '" + keys[i] + "' has no bibliography entry");
        r->kids.push_back(mkText("??", r->span, r->style));
        continue;
      }
      ContentNode* l = mkNode(Kind::link, r->span, r->style);
      setArgStr(l, ArgK::url, "#tsr-bib-" + keys[i]);
      l->kids.push_back(mkText(std::to_string(citeOrdinal(keys[i])), r->span, r->style));
      r->kids.push_back(l);
    }
    r->kids.push_back(mkText("]", r->span, r->style));
    return true;
  }

  void resolveRef(ContentNode* r) {
    std::string target(strs.get(argStr(r, ArgK::target)));
    r->kids.clear();
    auto it = labels.find(target);
    if (it == labels.end()) {
      if (resolveCite(r, target)) return;
      diags.add(Sev::Warning, "ref-unresolved", r->span,
                "reference '" + target + "' has no label");
      r->kids.push_back(mkText("??", r->span, r->style));
      return;
    }
    const Entry& e = it->second;
    if (bib.count(target))
      diags.add(Sev::Warning, "ref-shadowed", r->span,
                "'" + target + "' is both a label and a bibliography key (the label wins)");
    if (!e.numbered)
      diags.add(Sev::Info, "ref-unnumbered", r->span,
                "'" + target + "' names an unnumbered element; its label text is shown");
    std::string disp;
    switch (e.kind) {
      case Kind::heading: disp = cfg.supHeading + e.number; break;
      case Kind::table: disp = cfg.supTable + e.number; break;
      // a figure; any other labelled group is unnumbered: its label text
      case Kind::group: disp = e.numbered ? cfg.supFigure + e.number : target; break;
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
      para->kids.push_back(mkLink(t.anchor, t.number + " " + t.text, c->span, c->style));
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
      para->kids.push_back(mkLink(g.name, g.name, c->span, c->style));
      if (!g.desc.empty())
        para->kids.push_back(mkText(" \xE2\x80\x94 " + g.desc, c->span, c->style));
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
    ContentNode* g = mkNode(Kind::group, span);
    setArgStr(g, ArgK::role, "notes");
    if (notesPlaced) {  // P0-09 g: the flow is placed once; aliasing the
                        // same note nodes twice duplicated ids and content
      diags.add(Sev::Info, "flow-already-placed", span, "notes were already placed");
      return g;
    }
    notesPlaced = true;
    if (notes.empty()) return g;
    g->kids.push_back(mkNode(Kind::rule, span));
    ContentNode* list = mkNode(Kind::list, span);
    setArgBool(list, ArgK::ordered, true);
    for (size_t i = 0; i < notes.size(); i++) {
      ContentNode* nd = notes[i];
      rewrite(nd);  // refs inside the body resolve like anywhere else
      std::string num = std::to_string(i + 1);
      ContentNode* item = mkNode(Kind::item, nd->span);
      // an inline body is one paragraph; a body of blocks (a multi-paragraph
      // ^[…], plan P1-08) keeps them: the label goes on the first paragraph,
      // the back link ends the last one
      bool blocks = false;
      for (const ContentNode* k : nd->kids) blocks = blocks || !isInlineLevel(k->kind);
      ContentNode* para = nullptr;
      for (ContentNode* k : nd->kids) {
        rescale(k, 0.85f);
        if (blocks) {
          item->kids.push_back(k);
          continue;
        }
        if (!para) para = mkNode(Kind::para, nd->span);
        para->kids.push_back(k);
      }
      if (!blocks && !para) para = mkNode(Kind::para, nd->span);
      if (para) item->kids.push_back(para);
      ContentNode* first = nullptr;
      ContentNode* last = nullptr;
      for (ContentNode* k : item->kids)
        if (k->kind == Kind::para) {
          if (!first) first = k;
          last = k;
        }
      if (!first || item->kids.front() != first) {  // a body opening with a list, …
        first = mkNode(Kind::para, nd->span);
        item->kids.insert(item->kids.begin(), first);
        if (!last) last = first;
      }
      setArgStr(first, ArgK::label, "fn-" + num);
      if (item->kids.back() != last) {
        last = mkNode(Kind::para, nd->span);
        item->kids.push_back(last);
      }
      Styling small = styles.get(nd->style);
      small.sizeMul *= 0.85f;
      StyleId smallId = styles.idOf(small);
      last->kids.push_back(mkText(" ", nd->span, smallId));
      ContentNode* back = mkNode(Kind::ref, nd->span, smallId);
      setArgStr(back, ArgK::target, "fnref-" + num);
      resolveRef(back);
      last->kids.push_back(back);
      list->kids.push_back(item);
    }
    g->kids.push_back(list);
    return g;
  }

  // the bibliography section: cited entries in citation order (all
  // entries, cited-first, with all: true), each "[n] formatted entry" as
  // an anchored paragraph. The executor emits the collector at document
  // end, after every citation has been seen by the rewrite pass.
  ContentNode* buildBibliography(ContentNode* c) {
    ContentNode* g = mkNode(Kind::group, c->span, c->style);
    setArgStr(g, ArgK::role, "bibliography");
    bool all = false;
    for (const ArgVal& a : c->args)
      if (a.key == ArgK::form && a.tag == ArgTag::Str &&
          std::string_view(strs.get(a.ref)) == "all") all = true;
    std::vector<std::string> keys = citeOrder;
    if (all)
      for (ContentNode* k : c->kids) {
        std::string key(strs.get(argStr(k, ArgK::name)));
        if (!key.empty() && !citeNo.count(key)) {
          citeOrdinal(key);
          keys.push_back(key);
        }
      }
    if (keys.empty()) return g;
    bool first = bibBuilt++ == 0;
    g->kids.push_back(mkNode(Kind::rule, c->span));
    for (const std::string& key : keys) {
      auto it = bib.find(key);
      if (it == bib.end() || !it->second) continue;  // cited key without entry (fuzz: bib-missing-entry)
      ContentNode* e = it->second;
      ContentNode* para = mkNode(Kind::para, e->span, e->style);
      // P0-09 g: a second bibliography clones its rows and carries no
      // anchors (one id per entry)
      if (first) setArgStr(para, ArgK::label, "bib-" + key);
      para->kids.push_back(
          mkText("[" + std::to_string(citeNo[key]) + "] ", e->span, e->style));
      for (ContentNode* k : e->kids) para->kids.push_back(first ? k : clone(k));
      rewrite(para);  // entries may carry links/refs of their own
      g->kids.push_back(para);
    }
    return g;
  }

  ContentNode* buildCollect(ContentNode* c) {
    // a collector inside built content (a #bibliography in a bib entry,
    // which buildBibliography rewrites) would rebuild itself forever
    if (collecting) {
      diags.add(Sev::Warning, "collect-nested", c->span, "a collector cannot appear inside collected content");
      return mkNode(Kind::group, c->span);
    }
    struct Scope {
      int& d;
      explicit Scope(int& d_) : d(d_) { d++; }
      ~Scope() { d--; }
    } scope(collecting);
    std::string what(strs.get(argStr(c, ArgK::what)));
    if (what == "toc") return buildToc(c);
    if (what == "glossary") return buildGlossary(c);
    if (what == "notes") return buildNotes(c->span);
    if (what == "bibliography") return buildBibliography(c);
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
        mkText(name, t->span, compose(styles, t->style, CLS_BOLD)));
    bool sep = false;
    for (ContentNode* k : t->kids) {
      if (isInlineLevel(k->kind)) {
        if (!sep) {
          namePara->kids.push_back(mkText(" \xE2\x80\x94 ", t->span, t->style));
          sep = true;
        }
        namePara->kids.push_back(k);
      }
    }
    g->kids.push_back(namePara);
    for (ContentNode* k : t->kids)
      if (!isInlineLevel(k->kind)) g->kids.push_back(k);
    return g;
  }

  void rewrite(ContentNode* n) {
    for (size_t i = 0; i < n->kids.size(); i++) {
      ContentNode* k = n->kids[i];
      // (empty-para removal and block unwrapping now run in normalize(),
      // right after instantiation — plan P0-07)
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
  Resolver r{.arena = arena, .strs = strs, .styles = styles, .cfg = cfg, .diags = diags};
  r.scan(tree.root);
  r.orderCites(tree.root);
  r.rewrite(tree.root);
  // implicit notes section (notes-design.md §1): at document end unless
  // the author placed #notes() themselves
  if (!r.notes.empty() && !r.notesPlaced)
    tree.root->kids.push_back(r.buildNotes(tree.root->span));
}

}  // namespace tsr
