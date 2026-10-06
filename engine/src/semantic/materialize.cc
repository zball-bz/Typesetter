#include "materialize.h"

#include <functional>

namespace tsr {

namespace {

// A template's site: the span and style its nodes take, plus the style
// deltas of the `styled` items around the current one.
struct Ctx {
  Span span;
  StyleId style = 0;
  StyleDelta d;
  float size = 1.0f;
};

// The slots one template instantiation reads.
struct Slots {
  std::vector<std::pair<std::string_view, std::string>> text;
  std::vector<std::pair<std::string_view, std::vector<ContentNode*>>> nodes;
  // `each`: the items (or keys) at a placeholder; `paras`: a body placement
  std::function<void(const TItem&, const Ctx&, ContentNode*, std::vector<ContentNode*>&)> each;
  std::function<void(const TItem&, const Ctx&, std::vector<ContentNode*>&)> paras;

  void set(std::string_view k, std::string v) { text.push_back({k, std::move(v)}); }
  void put(std::string_view k, std::string v) {  // set, replacing a value already set
    for (auto& [n, x] : text)
      if (n == k) {
        x = std::move(v);
        return;
      }
    set(k, std::move(v));
  }
  const std::string* textOf(std::string_view k) const {
    for (const auto& [n, v] : text)
      if (n == k) return &v;
    return nullptr;
  }
  const std::vector<ContentNode*>* nodesOf(std::string_view k) const {
    for (const auto& [n, v] : nodes)
      if (n == k) return &v;
    return nullptr;
  }
  bool isSet(std::string_view k) const {
    if (const std::string* t = textOf(k)) return !t->empty();
    if (const std::vector<ContentNode*>* v = nodesOf(k)) return !v->empty();
    return false;
  }
};

struct Mat {
  MaterializeEnv& e;
  int collecting = 0;
  std::vector<std::string> flowPlaced, tablesRendered;
  std::vector<const Instance*> scope;  // the instances the walk is inside (field)

  static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v)
      if (x == s) return true;
    return false;
  }

  // --- nodes ------------------------------------------------------------------
  ContentNode* mk(Kind k, Span span, StyleId style = 0) {
    ContentNode* n = e.arena.make<ContentNode>();
    n->kind = k;
    n->span = span;
    n->style = style;
    return n;
  }
  ContentNode* clone1(const ContentNode* n) {
    ContentNode* c = e.arena.make<ContentNode>();
    *c = *n;
    return c;
  }
  ContentNode* deepClone(const ContentNode* n) {
    ContentNode* c = clone1(n);
    for (ContentNode*& k : c->kids) k = deepClone(k);
    return c;
  }
  void setArg(ContentNode* n, ArgK k, std::string_view v) {
    for (ArgVal& a : n->args)
      if (a.key == k) {
        a.tag = ArgTag::Str;
        a.ref = e.strs.intern(v);
        return;
      }
    n->args.push_back({k, ArgTag::Str, 0, e.strs.intern(v)});
  }
  void dropArg(ContentNode* n, ArgK k) {
    for (size_t i = 0; i < n->args.size(); i++)
      if (n->args[i].key == k) {
        n->args.erase(n->args.begin() + (long)i);
        return;
      }
  }
  StyleId styleOf(const Ctx& c) { return compose(e.styles, c.style, c.d, c.size); }
  // a delta over inserted content: every node of the subtree, once
  ContentNode* delta(ContentNode* n, const Ctx& c) {
    if (c.d.empty() && c.size == 1.0f) return n;
    ContentNode* d = clone1(n);
    d->style = compose(e.styles, n->style, c.d, c.size);
    for (ContentNode*& k : d->kids) k = delta(k, c);
    return d;
  }

  // --- templates ----------------------------------------------------------------
  // Strings, terms and text slots that follow each other make one text node.
  void inst(const Template& t, const Ctx& c, const Slots& s, ContentNode* container,
            std::vector<ContentNode*>& out) {
    Run run{c, out};
    items(t, c, s, container, out, run);
    run.flush(*this);
  }
  // the text run a template is building (one text node per run of strings,
  // terms and text slots, `when` branches included: § + 1 stays one node)
  struct Run {
    const Ctx& c;
    std::vector<ContentNode*>& out;
    std::string text;
    bool open = false;
    void add(std::string_view s) {
      text += s;
      open = true;
    }
    void flush(Mat& m) {
      if (!open) return;
      ContentNode* tx = m.mk(Kind::text, c.span, m.styleOf(c));
      tx->str = m.e.strs.intern(text);
      out.push_back(tx);
      text.clear();
      open = false;
    }
  };
  void items(const Template& t, const Ctx& c, const Slots& s, ContentNode* container,
             std::vector<ContentNode*>& out, Run& run) {
    for (const TItem& it : t) {
      switch (it.k) {
        case TItem::K::Text:
          run.add(it.name);
          break;
        case TItem::K::Term:
          run.add(e.terms.get(it.name));
          break;
        case TItem::K::Slot:
          if (const std::string* v = s.textOf(it.name)) {
            run.add(*v);
          } else if (const std::vector<ContentNode*>* ns = s.nodesOf(it.name)) {
            run.flush(*this);
            for (ContentNode* n : *ns) out.push_back(delta(n, c));
          }
          break;
        case TItem::K::Node:
          run.flush(*this);
          out.push_back(node(it, c, s));
          break;
        case TItem::K::Styled: {
          run.flush(*this);
          Ctx d = c;
          d.d += it.delta;
          d.size *= it.size;
          inst(it.kids, d, s, container, out);
          break;
        }
        case TItem::K::When:
          items(s.isSet(it.name) ? it.kids : it.orElse, c, s, container, out, run);
          break;
        case TItem::K::Each:
          run.flush(*this);
          if (s.each) s.each(it, c, container, out);
          break;
        case TItem::K::Paras:
          run.flush(*this);
          if (s.paras) s.paras(it, c, out);
          break;
      }
    }
  }
  std::string argValue(const TArg& a, const Slots& s) {
    const std::string* v = s.textOf(a.s);
    if (a.k == TArg::K::Text) return a.s;
    if (!v || v->empty()) return {};
    return a.k == TArg::K::Anchor ? "#tsr-" + *v : *v;
  }
  ContentNode* node(const TItem& it, const Ctx& c, const Slots& s) {
    bool inline_ = isInlineLevel(it.kind);
    ContentNode* n = mk(it.kind, c.span, inline_ || it.siteStyle ? styleOf(c) : 0);
    for (const auto& [k, v] : it.args) {
      if (v.k == TArg::K::Bool || v.k == TArg::K::Num) {
        n->args.push_back({k, v.k == TArg::K::Bool ? ArgTag::Bool : ArgTag::Num, v.k == TArg::K::Bool ? (v.b ? 1.0 : 0.0) : v.num, 0});
        continue;
      }
      std::string val = argValue(v, s);
      if (!val.empty() || v.k == TArg::K::Text) setArg(n, k, val);  // an empty slot: no argument
    }
    inst(it.kids, c, s, n, n->kids);
    if (n->kind == Kind::ref) resolveRef(n);  // a generated reference reads like any other
    return n;
  }
  std::string textOf(const Template& t, const Slots& s) {
    std::vector<ContentNode*> tmp;
    inst(t, Ctx{}, s, nullptr, tmp);
    std::string out;
    for (const ContentNode* n : tmp) out += e.strs.get(n->str);
    return out;
  }

  // a supplement's word (plan P2-07): a locale term, a literal, or the
  // literal for the document's language (its pack, then its language, then
  // en, then the first)
  std::string supplementText(const Supplement& s) {
    if (!s.term.empty()) return std::string(e.terms.get(s.term));
    if (s.literal) return s.text;
    if (s.byLang.empty()) return {};
    const std::string& pack = e.terms.pack();
    const std::string lang = pack.substr(0, pack.find('-'));
    for (const auto& [l, t] : s.byLang)
      if (l == pack) return t;
    for (const auto& [l, t] : s.byLang)
      if (l == lang) return t;
    for (const auto& [l, t] : s.byLang)
      if (l == "en") return t;
    return s.byLang.front().second;
  }

  // --- instances ------------------------------------------------------------------
  const Instance* instanceOf(const ContentNode* n) const {
    auto it = e.ix.instOf.find(n);
    return it == e.ix.instOf.end() ? nullptr : &e.ix.instances[it->second];
  }
  void instanceSlots(const Instance& in, Slots& s) {
    s.set("number", in.number);
    s.set("supplement", supplementText(in.supplement));
    s.set("title", in.title);
    s.set("alias", in.label);
    s.set("marker-alias", in.markerAlias);
  }

  // B1: a reference, in place on a new ref node. Structured (plan P2-09;
  // design T3 S3): its child refs are a group (@[a, b]), its child in slot
  // "extra" the bracket of @x[…], which the template reads — a reference's
  // supplement word, a citation's locator
  ContentNode* resolveRef(ContentNode* r) {
    std::vector<std::string> members;
    std::vector<ContentNode*> extra;
    const StrRef extraSlot = e.strs.find("extra");
    for (ContentNode* k : r->kids) {
      if (k->kind == Kind::ref) members.emplace_back(e.strs.get(attrStr(k, ArgK::target)));
      else if (extraSlot && attrStr(k, ArgK::slot) == extraSlot) kidsOf(k, extra);
    }
    r->kids.clear();
    std::string target(e.strs.get(attrStr(r, ArgK::target)));
    Ctx c{r->span, r->style};
    if (!members.empty()) return group(r, members, extra, c);
    auto it = e.ix.labels.find(target);
    if (it == e.ix.labels.end()) {
      // not a label: a citation (a group of one; a JS target "a, b" still
      // splits into its keys)
      std::vector<std::string> keys = splitKeys(target);
      if (const CollectorDef* T = citeTable(keys)) {
        cite(r, *T, keys, extra, c);
        return r;
      }
      e.diags.add(Sev::Warning, "ref-unresolved", r->span, "reference '" + target + "' has no label");
      Slots s;
      s.set("label", target);
      inst(e.reg.unresolved, c, s, r, r->kids);
      return r;
    }
    for (const CollectorDef& t : e.reg.collectors)
      if (t.citeable && e.ix.row(t.table, target)) {
        e.diags.add(Sev::Warning, "ref-shadowed", r->span,
                    "'" + target + "' is both a label and a " + t.name + " key (the label wins)");
        break;
      }
    const LabelTarget& lt = it->second;
    const Template* form = nullptr;
    Slots s;
    s.set("label", target);
    s.nodes.push_back({"extra", extra});
    if (lt.k != LabelTarget::K::Plain) {
      const Instance& in = e.ix.instances[lt.inst];
      const ElementClass& C = e.reg.cls(in.cls);
      instanceSlots(in, s);
      // ref(target, {supplement}) replaces the supplement word (plan P2-07)
      if (StrRef sup = attrStr(r, ArgK::supplement)) s.put("supplement", std::string(e.strs.get(sup)));
      if (lt.k == LabelTarget::K::Marker && C.flow && C.flow->markerAlias.hasRef) form = &C.flow->markerAlias.ref;
      else if (C.hasRef) form = &C.ref;
      if (StrRef f = attrStr(r, ArgK::form)) form = namedForm(C, e.strs.get(f), form, r);
    }
    if (!form) {
      e.diags.add(Sev::Info, "ref-unnumbered", r->span,
                  "'" + target + "' names an unnumbered element; its label text is shown");
      form = &e.reg.unnumbered;
    }
    setArg(r, ArgK::url, "#tsr-" + target);
    inst(*form, c, s, r, r->kids);
    return r;
  }

  // a group reference (@[a, b]): citations read through their table's cite
  // template (one bracket, ranges compressed); labels each as their own
  // reference, the ref-sep word between (the parent links nowhere itself)
  ContentNode* group(ContentNode* r, const std::vector<std::string>& members, const std::vector<ContentNode*>& extra,
                     const Ctx& c) {
    if (const CollectorDef* T = citeTable(members)) {
      cite(r, *T, members, extra, c);
      return r;
    }
    for (size_t i = 0; i < members.size(); i++) {
      if (i) {
        ContentNode* tx = mk(Kind::text, c.span, styleOf(c));
        tx->str = e.strs.intern(e.terms.get("ref-sep"));
        r->kids.push_back(tx);
      }
      ContentNode* m = mk(Kind::ref, r->span, r->style);
      setArg(m, ArgK::target, members[i]);
      for (ArgK k : {ArgK::form, ArgK::supplement})
        if (StrRef v = attrStr(r, k)) setArg(m, k, e.strs.get(v));
      r->kids.push_back(resolveRef(m));
    }
    if (!extra.empty())
      e.diags.add(Sev::Info, "ref-extra", r->span, "the bracket after a group of labels is not read (it is a citation's locator)");
    return r;
  }

  // ref(target, {form}) (plan P2-07): the class's named form, else a
  // built-in one — number, title (name), supplement, full (the class's)
  const Template* namedForm(const ElementClass& C, std::string_view name, const Template* dflt, const ContentNode* r) {
    for (const auto& [n, t] : C.forms)
      if (n == name) return &t;
    static const Template kNumber{TItem{TItem::K::Slot, "number"}};
    static const Template kTitle{TItem{TItem::K::Slot, "title"}};
    static const Template kSupplement{TItem{TItem::K::Slot, "supplement"}};
    if (name == "number") return &kNumber;
    if (name == "title" || name == "name") return &kTitle;
    if (name == "supplement") return &kSupplement;
    if (name != "full")
      e.diags.add(Sev::Warning, "ref-form", r->span,
                  "a " + C.name + " has no reference form '" + std::string(name) + "' (the full form is used)");
    return dflt;
  }

  // the citeable table any of `keys` is a row of
  const CollectorDef* citeTable(const std::vector<std::string>& keys) const {
    for (const CollectorDef& t : e.reg.collectors) {
      if (!t.citeable) continue;
      for (const std::string& k : keys)
        if (e.ix.row(t.table, k)) return &t;
    }
    return nullptr;
  }

  // citations: each key links to its row with its ordinal (an unknown key is
  // diagnosed in its own slot); with the table's `compress`, three or more
  // consecutive ordinals read first–last (range-sep); `extra` is the locator
  void cite(ContentNode* r, const CollectorDef& T, const std::vector<std::string>& keys,
            const std::vector<ContentNode*>& extra, const Ctx& c) {
    std::vector<int> ord(keys.size(), 0);
    for (size_t i = 0; i < keys.size(); i++)
      if (e.ix.row(T.table, keys[i])) ord[i] = e.counters.keyed(T.rowCounter, keys[i]);
    Slots s;
    s.nodes.push_back({"extra", extra});
    s.each = [&](const TItem& it, const Ctx& ec, ContentNode* container, std::vector<ContentNode*>& out) {
      auto item = [&](size_t i) {
        Slots ks;
        if (ord[i]) {
          ks.set("row", "1");
          ks.set("anchor", T.rowAnchor.prefix + keys[i]);
          ks.set("ordinal", std::to_string(ord[i]));
        } else {
          e.diags.add(Sev::Warning, "ref-unresolved", r->span,
                      "citation key '" + keys[i] + "' has no " + T.name + " entry");
        }
        inst(it.kids, ec, ks, container, out);
      };
      for (size_t i = 0; i < keys.size(); i++) {
        if (i) inst(it.sep, ec, Slots{}, container, out);
        size_t j = i;
        if (T.compress && ord[i])
          while (j + 1 < keys.size() && ord[j + 1] == ord[j] + 1) j++;
        if (j - i >= 2) {  // a run of three or more
          item(i);
          ContentNode* tx = mk(Kind::text, ec.span, styleOf(ec));
          tx->str = e.strs.intern(e.terms.get("range-sep"));
          out.push_back(tx);
          item(j);
          i = j;
        } else {
          item(i);
        }
      }
    };
    inst(T.cite, c, s, r, r->kids);
  }

  // --- the walk (B1–B3 in document order) ----------------------------------------
  // the output of an ordinary node: its kids replaced as their kinds and
  // classes say, then its own class's changes
  ContentNode* walk(const ContentNode* n) {
    std::vector<ContentNode*> kids;
    const Instance* in = n->cls ? instanceOf(n) : nullptr;
    if (in) scope.push_back(in);
    bool changed = kidsOf(n, kids);
    if (in) scope.pop_back();
    ContentNode* o = const_cast<ContentNode*>(n);
    if (changed) {
      o = clone1(n);
      o->kids = std::move(kids);
    }
    return own(n, o);
  }
  bool kidsOf(const ContentNode* n, std::vector<ContentNode*>& out) {
    bool changed = false;
    out.reserve(n->kids.size());
    for (const ContentNode* k : n->kids) {
      size_t before = out.size();
      replace(k, out);
      changed = changed || out.size() != before + 1 || out.back() != k;
    }
    return changed;
  }
  // a paragraph of only events, entries and empty text vanishes with them
  // (plan P2-07; the generalized empty paragraph): #appendix() on its own line
  static bool positional(Kind k) { return k == Kind::event || k == Kind::entry; }
  bool vacuous(const ContentNode* p) const {
    if (p->kind != Kind::para) return false;
    bool any = false;
    for (const ContentNode* k : p->kids) {
      if (positional(k->kind)) any = true;
      else if (k->kind != Kind::text || !e.strs.get(k->str).empty()) return false;
    }
    return any;
  }
  void replace(const ContentNode* k, std::vector<ContentNode*>& out) {
    // B2: events (applied in LOCATE) and entries (rows of their table)
    // leave nothing where they stand
    if (positional(k->kind) || vacuous(k)) return;
    if (k->kind == Kind::slot || k->kind == Kind::when || k->kind == Kind::each) {
      std::string msg = std::string(kindName(k->kind)) + " belongs in a declaration's template";
      e.diags.add(Sev::Warning, "template-only", k->span, msg);
      ContentNode* x = mk(Kind::error, k->span, k->style);
      x->args.push_back({ArgK::message, ArgTag::Str, 0, e.strs.intern(msg)});
      x->args.push_back({ArgK::code, ArgTag::Str, 0, e.strs.intern("template-only")});
      out.push_back(x);
      return;
    }
    if (k->kind == Kind::collect) {
      collect(k, out);
      return;
    }
    if (k->kind == Kind::ref) {
      out.push_back(resolveRef(clone1(k)));
      return;
    }
    if (k->kind == Kind::field) {  // (plan P2-05, P2-07) a slot of the enclosing instance, or of `of`'s
      std::string name(e.strs.get(attrStr(k, ArgK::name)));
      const Instance* in = scope.empty() ? nullptr : scope.back();
      if (StrRef of = attrStr(k, ArgK::of)) {
        auto lt = e.ix.labels.find(std::string(e.strs.get(of)));
        in = lt != e.ix.labels.end() && lt->second.inst != kNoInst ? &e.ix.instances[lt->second.inst] : nullptr;
      }
      if (in) {
        Slots s;
        instanceSlots(*in, s);
        s.set("label", in->label);
        if (const std::string* v = s.textOf(name)) {
          ContentNode* tx = mk(Kind::text, k->span, k->style);
          tx->str = e.strs.intern(*v);
          out.push_back(tx);
          return;
        }
      }
      e.diags.add(Sev::Warning, "field-unresolved", k->span, "field '" + name + "' has no value");
      Slots s;
      s.set("label", name);
      inst(e.reg.unresolved, Ctx{k->span, k->style}, s, nullptr, out);
      return;
    }
    if (k->cls) {
      const ElementClass& C = e.reg.cls(k->cls);
      if (C.replaced()) {  // B2: a replace site, then its content like any other
        std::vector<ContentNode*> built;
        replaceSite(k, C, built);
        for (ContentNode* b : built) out.push_back(walk(b));
        return;
      }
      if (C.flow) {  // B2: a flow item leaves its marker; its body goes to the flow
        const Instance* in = instanceOf(k);
        Slots s;
        instanceSlots(*in, s);
        inst(C.flow->marker, Ctx{k->span, k->style}, s, nullptr, out);
        return;
      }
    }
    out.push_back(walk(k));
  }

  void replaceSite(const ContentNode* k, const ElementClass& C, std::vector<ContentNode*>& out) {
    const Instance* in = instanceOf(k);
    Slots s;
    instanceSlots(*in, s);
    // the label it carries: the class's label argument, else its own
    std::string label(in->label.empty() ? e.strs.get(attrStr(k, ArgK::label)) : std::string_view(in->label));
    s.set("label", label);
    std::vector<ContentNode*> inl, blk;
    for (ContentNode* x : k->kids) (isInlineLevel(x->kind) ? inl : blk).push_back(x);
    s.nodes.push_back({"inline-body", inl});
    s.nodes.push_back({"block-body", blk});
    for (const SiteDef& site : C.sites)
      if (site.where == SiteDef::Where::Replace) inst(site.tmpl, Ctx{k->span, k->style}, s, nullptr, out);
  }

  // B2: a classed node's own changes — its refused label dropped, its alias
  // written as its anchor, its sites attached
  ContentNode* own(const ContentNode* n, ContentNode* o) {
    bool refused = e.ix.refused.count(n) != 0;
    const Instance* in = n->cls ? instanceOf(n) : nullptr;
    const ElementClass* C = in ? &e.reg.cls(in->cls) : nullptr;
    bool anchor = C && in->aliased && !C->flow && !C->replaced();
    bool sites = false;
    if (C && !in->number.empty())
      for (const SiteDef& s : C->sites) sites = sites || s.where != SiteDef::Where::Replace;
    if (!refused && !anchor && !sites) return o;
    if (o == n) o = clone1(n);
    if (refused) dropArg(o, ArgK::label);
    if (anchor) setArg(o, ArgK::label, in->label);
    if (!sites) return o;
    Slots s;
    instanceSlots(*in, s);
    for (const SiteDef& site : C->sites) {
      switch (site.where) {
        case SiteDef::Where::Arg:
          setArg(o, site.arg, textOf(site.tmpl, s));
          break;
        case SiteDef::Where::Prepend: {
          ContentNode* at = o;
          if (site.at == SiteDef::At::FirstPara) {
            at = nullptr;
            for (ContentNode*& k : o->kids)
              if (k->kind == Kind::para) {
                k = clone1(k);
                at = k;
                break;
              }
          }
          if (!at) break;  // nothing to attach to (a captionless figure)
          std::vector<ContentNode*> gen;
          inst(site.tmpl, Ctx{at->span, at->style}, s, at, gen);
          at->kids.insert(at->kids.begin(), gen.begin(), gen.end());
          break;
        }
        case SiteDef::Where::Replace:
          break;
      }
    }
    return o;
  }

  // --- collectors (B3) ---------------------------------------------------------------
  void collect(const ContentNode* k, std::vector<ContentNode*>& out) {
    if (collecting) {  // built content holds no collector (it would build itself again)
      e.diags.add(Sev::Warning, "collect-nested", k->span, "a collector cannot appear inside collected content");
      out.push_back(mk(Kind::group, k->span));
      return;
    }
    struct Guard {
      int& d;
      explicit Guard(int& x) : d(x) { d++; }
      ~Guard() { d--; }
    } guard(collecting);
    std::string what(e.strs.get(attrStr(k, ArgK::what)));
    const CollectorDef* C = e.reg.collector(what);
    if (!C) {
      e.diags.add(Sev::Warning, "collect-unknown", k->span, "unknown collector '" + what + "'");
      out.push_back(mk(Kind::group, k->span));
      return;
    }
    Ctx cc{k->span, k->style};
    switch (C->src) {
      case CollectorDef::Src::Outline: outline(*C, cc, out); return;
      case CollectorDef::Src::Table: table(*C, k, cc, out); return;
      case CollectorDef::Src::Flow: flow(*C, cc, out); return;
    }
  }

  // the outline instances; nested by depth, each deeper level a new
  // instance of the wrap inside the last entry
  void outline(const CollectorDef& C, const Ctx& cc, std::vector<ContentNode*>& out) {
    std::vector<const Instance*> items;
    int minLevel = 1 << 30;
    for (const Instance& in : e.ix.instances)
      if (e.reg.cls(in.cls).outline) {
        items.push_back(&in);
        minLevel = std::min(minLevel, in.level);
      }
    Kind filler = !C.entry.empty() && C.entry[0].k == TItem::K::Node ? C.entry[0].kind : Kind::item;
    auto entry = [&](const Instance& in, ContentNode* container) {
      Slots s;
      s.set("number", in.number);
      s.set("title", in.title);
      s.set("anchor", in.label);
      inst(C.entry, cc, s, container, container->kids);
    };
    Slots ws;
    ws.each = [&](const TItem&, const Ctx& c, ContentNode* container, std::vector<ContentNode*>& o) {
      if (!container) {
        for (const Instance* in : items) {
          Slots s;
          s.set("number", in->number);
          s.set("title", in->title);
          s.set("anchor", in->label);
          inst(C.entry, cc, s, nullptr, o);
        }
        return;
      }
      std::vector<ContentNode*> stack{container};
      for (const Instance* in : items) {
        size_t depth = C.nestByDepth ? (size_t)(in->level - minLevel) : 0;
        while (stack.size() > depth + 1) stack.pop_back();
        while (stack.size() < depth + 1) {
          ContentNode* parent = stack.back();
          if (parent->kids.empty()) parent->kids.push_back(mk(filler, c.span));
          ContentNode* sub = nullptr;
          Slots ss;
          ss.each = [&](const TItem&, const Ctx&, ContentNode* cont, std::vector<ContentNode*>&) { sub = cont; };
          std::vector<ContentNode*> subNodes;
          inst(C.wrap, cc, ss, nullptr, subNodes);
          for (ContentNode* x : subNodes) parent->kids.back()->kids.push_back(x);
          if (!sub) break;
          stack.push_back(sub);
        }
        entry(*in, stack.back());
      }
    };
    inst(C.wrap, cc, ws, nullptr, out);
  }

  void table(const CollectorDef& C, const ContentNode* k, const Ctx& cc, std::vector<ContentNode*>& out) {
    Slots ws;
    if (!C.keyedRows) {  // rows of instances (a glossary)
      ws.each = [&](const TItem&, const Ctx&, ContentNode* container, std::vector<ContentNode*>& o) {
        for (const Row& r : e.ix.rows) {
          if (r.table != C.table) continue;
          Slots s;
          s.set("title", r.title);
          s.set("anchor", r.key);
          s.set("body-text", r.bodyText);
          inst(C.entry, cc, s, container, o);
        }
      };
      inst(C.wrap, cc, ws, nullptr, out);
      return;
    }
    // keyed rows: the cited ones in citation order, then (cited-then-all)
    // every other row in document order
    std::vector<std::string> keys = e.counters.keyOrder(C.rowCounter);
    std::string_view cited = e.strs.get(attrStr(k, ArgK::cited));
    if (cited.empty() && e.strs.get(attrStr(k, ArgK::form)) == "all") cited = "cited-then-all";  // (v6–9 buffers)
    if (cited == "cited-then-all" || (cited.empty() && C.cited == CollectorDef::Cited::CitedThenAll))
      for (const Row& r : e.ix.rows)
        if (r.table == C.table && !e.counters.keyedIfSeen(C.rowCounter, r.key)) {
          e.counters.keyed(C.rowCounter, r.key);
          keys.push_back(r.key);
        }
    if (keys.empty()) {
      inst(C.empty, cc, Slots{}, nullptr, out);
      return;
    }
    // the first rendering of a table owns its rows' anchors; a later one
    // clones the rows and carries none (one id per row)
    bool first = !contains(tablesRendered, C.table);
    if (first) tablesRendered.push_back(C.table);
    ws.each = [&](const TItem&, const Ctx&, ContentNode* container, std::vector<ContentNode*>& o) {
      for (const std::string& key : keys) {
        const Row* r = e.ix.row(C.table, key);
        if (!r || !r->node) continue;  // a cited key without a row
        Slots s;
        s.set("ordinal", std::to_string(e.counters.keyedIfSeen(C.rowCounter, key)));
        s.set("anchor", first ? C.rowAnchor.prefix + key : std::string());
        std::vector<ContentNode*> body;
        for (ContentNode* x : r->node->kids) body.push_back(first ? x : deepClone(x));
        s.nodes.push_back({"body", body});
        std::vector<ContentNode*> entry;
        inst(C.entry, Ctx{r->node->span, r->node->style}, s, container, entry);
        for (ContentNode* x : entry) o.push_back(walk(x));  // rows may carry references
      }
    };
    inst(C.wrap, cc, ws, nullptr, out);
  }

  // a flow's items, placed once: each item's body (materialized here, where
  // it is placed) as the entry's paragraphs
  void flow(const CollectorDef& C, const Ctx& cc, std::vector<ContentNode*>& out) {
    if (contains(flowPlaced, C.flow)) {
      e.diags.add(Sev::Info, "flow-already-placed", cc.span, C.flow + " were already placed");
      inst(C.empty, cc, Slots{}, nullptr, out);
      return;
    }
    flowPlaced.push_back(C.flow);
    const std::vector<u32>* items = e.ix.flowItems(C.flow);
    if (!items || items->empty()) {
      inst(C.empty, cc, Slots{}, nullptr, out);
      return;
    }
    Slots ws;
    ws.each = [&](const TItem&, const Ctx&, ContentNode* container, std::vector<ContentNode*>& o) {
      for (u32 id : *items) {
        const Instance& in = e.ix.instances[id];
        std::vector<ContentNode*> body;
        kidsOf(in.node, body);
        Slots s;
        instanceSlots(in, s);
        s.nodes.push_back({"body", body});
        s.paras = [&](const TItem& it, const Ctx& c, std::vector<ContentNode*>& po) { paras(it, c, body, s, po); };
        inst(C.entry, Ctx{in.node->span, in.node->style}, s, container, o);
      }
    };
    inst(C.wrap, cc, ws, nullptr, out);
  }

  // a body as paragraphs: an inline body is one paragraph, a block body keeps
  // its blocks; the anchor goes on the first paragraph and the tail ends the
  // last (both made when the body does not start or end with one)
  void paras(const TItem& it, const Ctx& c, const std::vector<ContentNode*>& body, const Slots& s,
             std::vector<ContentNode*>& out) {
    Ctx scaled = c;
    scaled.d = StyleDelta{};
    scaled.size = it.size;
    bool blocks = false;
    for (const ContentNode* k : body) blocks = blocks || !isInlineLevel(k->kind);
    std::vector<ContentNode*> res;
    ContentNode* para = nullptr;
    for (ContentNode* k : body) {
      ContentNode* d = delta(k, scaled);
      if (blocks) {
        res.push_back(d);
        continue;
      }
      if (!para) para = mk(Kind::para, c.span);
      para->kids.push_back(d);
    }
    if (!blocks && !para) para = mk(Kind::para, c.span);
    if (para) res.push_back(para);
    ContentNode* first = nullptr;
    ContentNode* last = nullptr;
    for (ContentNode*& k : res)
      if (k->kind == Kind::para) {
        k = clone1(k);
        if (!first) first = k;
        last = k;
      }
    if (!first || res.front() != first) {
      first = mk(Kind::para, c.span);
      res.insert(res.begin(), first);
      if (!last) last = first;
    }
    std::string anchor = argValue(it.anchor, s);
    if (!anchor.empty()) setArg(first, ArgK::label, anchor);
    if (res.back() != last) {
      last = mk(Kind::para, c.span);
      res.push_back(last);
    }
    inst(it.tail, c, s, last, last->kids);
    out.insert(out.end(), res.begin(), res.end());
  }

  ContentNode* run(const ContentNode* root) {
    ContentNode* o = walk(root);
    if (o == root) o = clone1(root);
    // a flow whose items no collector placed goes where its class says
    std::vector<std::string> seen;
    for (const ElementClass& C : e.reg.classes) {
      if (!C.flow || !C.flow->placeAtEnd || contains(seen, C.flow->name)) continue;
      seen.push_back(C.flow->name);
      const std::vector<u32>* items = e.ix.flowItems(C.flow->name);
      const CollectorDef* fc = e.reg.flowCollector(C.flow->name);
      if (items && !items->empty() && fc && !contains(flowPlaced, C.flow->name))
        flow(*fc, Ctx{root->span, root->style}, o->kids);
    }
    return o;
  }
};

}  // namespace

ContentNode* materialize(const ContentNode* root, MaterializeEnv& env) {
  if (!root) return nullptr;
  Mat m{env};
  return m.run(root);
}

}  // namespace tsr
