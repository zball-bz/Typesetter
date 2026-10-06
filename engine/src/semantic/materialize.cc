#include "materialize.h"

#include <functional>

namespace tsr {

namespace {

// A template's site: the span and style its nodes take (computed and
// scope: model.h), plus the style deltas of the `styled` items around
// the current one.
struct Ctx {
  Span span;
  StyleId style = 0;
  StyleDelta d;
  float size = 1.0f;
  StyleId scope = 0;
  RuleEnvId env = 0;  // the rules in force there
};
// the site at a node: under it
Ctx siteAt(const ContentNode* k) {
  Ctx c;
  c.span = k->span;
  c.style = k->style;
  c.scope = k->scope;
  c.env = k->env;
  return c;
}

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
  std::vector<std::string> tablesRendered, anchorsRendered;
  std::vector<const Instance*> scope;  // the instances the walk is inside (field)
  // (plan P3-13) the flow items placed: each goes to one place — the first
  // collector whose scope holds it, a section's end, the document's end
  std::vector<char> placed = std::vector<char>(e.ix.instances.size(), 0);
  const ContentNode* root_ = nullptr;
  std::vector<const FlowDef*> sectionFlows;  // flows placed at their sections' ends

  static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v)
      if (x == s) return true;
    return false;
  }

  // --- nodes ------------------------------------------------------------------
  ContentNode* mk(Kind k, Span span) {
    ContentNode* n = e.arena.make<ContentNode>();
    n->kind = k;
    n->span = span;
    n->props = ~0u;  // kPropsUnset: settled at its place after materialize
    n->env = kEnvUnset;  // (its parent's, unless it is made at a site: make())
    e.made++;
    return n;
  }
  // at a site (Cascade.make, plan P3-01): its parent's style there, the
  // rules that match it, the site's deltas over them
  void make(ContentNode* n, const Ctx& c) {
    Styling st = e.styles.get(c.style), sc = e.styles.get(c.scope);
    Cascade::NodeView v{n->kind};
    v.args = &n->args;
    v.role = attrStr(n, ArgK::role);
    v.cls = attrStr(n, ArgK::class_);
    v.lang = st.lang;
    e.cascade.make(st, sc, v, c.env, c.d, c.size);
    n->style = e.styles.idOf(st);
    n->scope = e.styles.idOf(sc);
    n->env = c.env;
  }
  ContentNode* mk(Kind k, Span span, const Ctx& c) {
    ContentNode* n = mk(k, span);
    make(n, c);
    return n;
  }
  // under a made node: its style, no delta pending
  static Ctx under(const ContentNode* n, Span span) {
    Ctx c = siteAt(n);
    c.span = span;
    return c;
  }
  // made in a node's state (its replacement, its generated content)
  ContentNode* mk(Kind k, Span span, const ContentNode* like) { return mk(k, span, siteAt(like)); }
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
  // a delta over inserted content: every node of the subtree, once
  ContentNode* delta(ContentNode* n, const Ctx& c) {
    if (c.d.empty() && c.size == 1.0f) return n;
    ContentNode* d = clone1(n);
    d->style = compose(e.styles, n->style, c.d, c.size);
    d->scope = compose(e.styles, n->scope, c.d, c.size);
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
      ContentNode* tx = m.mk(Kind::text, c.span, c);
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
    return *v;  // (an anchor: a label, which node() makes the link's target)
  }
  ContentNode* node(const TItem& it, const Ctx& c, const Slots& s) {
    bool inline_ = isInlineLevel(it.kind);
    ContentNode* n = mk(it.kind, c.span);
    if (!it.marker.empty()) n->number = e.strs.intern(textOf(it.marker, s));  // its marker (SemInfo.number)
    for (const auto& [k, v] : it.args) {
      if (v.k == TArg::K::Bool || v.k == TArg::K::Num) {
        n->args.push_back({k, v.k == TArg::K::Bool ? ArgTag::Bool : ArgTag::Num, v.k == TArg::K::Bool ? (v.b ? 1.0 : 0.0) : v.num, 0});
        continue;
      }
      std::string val = argValue(v, s);
      if (v.k == TArg::K::Anchor) {  // (plan P3-04) an internal link: its target label, spelled by the serializers
        if (!val.empty()) {
          setArg(n, ArgK::target, val);
          n->anchorTo = e.strs.intern(val);
        }
        continue;
      }
      if (!val.empty() || v.k == TArg::K::Text) setArg(n, k, val);  // an empty slot: no argument
    }
    // an inline node (or a block taking the site's style) is made at the
    // site, its role and attributes selecting rules, and its content under it
    if (inline_ || it.siteStyle) {
      make(n, c);
      inst(it.kids, under(n, c.span), s, n, n->kids);
    } else {
      inst(it.kids, c, s, n, n->kids);
    }
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
  u32 idOf(const Instance* in) const { return (u32)(in - e.ix.instances.data()); }
  // (plan P3-13) a keyed row's anchor in a scope: in a refsection, the
  // section's place among its class's first (bib-2-kp81)
  std::string rowAnchorIn(const AliasRule& a, const std::string& key, u32 scope) const {
    if (scope == kDocScope) return a.prefix + key;
    u32 n = 0;
    for (u32 i = 0; i <= scope; i++) n += e.ix.instances[i].cls == e.ix.instances[scope].cls;
    return a.prefix + std::to_string(n) + "-" + key;
  }
  // (plan P3-13) the scope a keyed counter counts in here (a refsection)
  u32 keyedHere(u16 c) const {
    std::vector<u32> encl;
    for (const Instance* in : scope) encl.push_back(idOf(in));
    return keyedScope(e.reg, e.ix, c, encl);
  }

  // B1: a reference, in place on a new ref node. Structured (plan P2-09;
  // design T3 S3): its child refs are a group (@[a, b]), its child in slot
  // "extra" the bracket of @x[…], which the template reads — a reference's
  // supplement word, a citation's locator
  ContentNode* resolveRef(ContentNode* r) {
    std::vector<std::string> members;
    std::vector<ContentNode*> extra;

    for (ContentNode* k : r->kids) {
      if (k->kind == Kind::ref) members.emplace_back(e.strs.get(attrStr(k, ArgK::target)));
      else if (slotOf(k, e.strs) == SlotId::Extra) kidsOf(k, extra);
    }
    r->kids.clear();
    std::string target(e.strs.get(attrStr(r, ArgK::target)));
    Ctx c = siteAt(r);
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
    r->anchorTo = e.strs.intern(target);  // its target's anchor (plan P3-04; the serializers spell it)
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
        ContentNode* tx = mk(Kind::text, c.span, c);
        tx->str = e.strs.intern(e.terms.get("ref-sep"));
        r->kids.push_back(tx);
      }
      ContentNode* m = mk(Kind::ref, r->span, r);
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
      if (e.ix.row(T.table, keys[i])) ord[i] = e.counters.keyed(T.rowCounter, keys[i], keyedHere(T.rowCounter));
    Slots s;
    s.nodes.push_back({"extra", extra});
    s.each = [&](const TItem& it, const Ctx& ec, ContentNode* container, std::vector<ContentNode*>& out) {
      auto item = [&](size_t i) {
        Slots ks;
        if (ord[i]) {
          ks.set("row", "1");
          ks.set("anchor", rowAnchorIn(T.rowAnchor, keys[i], keyedHere(T.rowCounter)));
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
          ContentNode* tx = mk(Kind::text, ec.span, ec);
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
      // (plan P3-13) a section ends where the next of its level or above
      // starts: the items of section-end flows before it go there
      if (n == root_ && !sectionFlows.empty() && k->cls && e.reg.cls(k->cls).outline)
        if (const Instance* h = instanceOf(k))
          for (const FlowDef* F : sectionFlows)
            if (h->level <= F->depth) placeRest(*F, siteAt(root_), out, idOf(h));
      if (out.size() != before) {
        changed = true;
        before = out.size();
      }
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
    // leave nothing where they stand — an anchored entry (an index entry,
    // plan P3-13) its empty anchor, the target of its collector's back-link
    if (k->kind == Kind::entry && k->cls)
      if (const Instance* in = instanceOf(k); in && in->aliased && !in->label.empty()) {
        ContentNode* a = clone1(k);
        a->kids.clear();
        setArg(a, ArgK::label, in->label);
        out.push_back(a);
        return;
      }
    if (positional(k->kind) || vacuous(k)) return;
    if (k->kind == Kind::slot || k->kind == Kind::when || k->kind == Kind::each) {
      std::string msg = std::string(kindName(k->kind)) + " belongs in a declaration's template";
      e.diags.add(Sev::Warning, "template-only", k->span, msg);
      ContentNode* x = mk(Kind::error, k->span, k);
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
      // (plan P3-13) another marker of a flow item (a named note): its
      // marker, anchored as this occurrence
      if (auto oc = e.ix.occurrenceOf.find(k); oc != e.ix.occurrenceOf.end()) {
        const Instance& in = e.ix.instances[oc->second.first];
        Slots s;
        instanceSlots(in, s);
        s.put("marker-alias", in.occurrences[oc->second.second - 2]);
        inst(e.reg.cls(in.cls).flow->marker, siteAt(k), s, nullptr, out);
        return;
      }
      out.push_back(resolveRef(clone1(k)));
      return;
    }
    if (k->kind == Kind::link && attrStr(k, ArgK::target)) {  // a link to a label (plan P3-04)
      std::string target(e.strs.get(attrStr(k, ArgK::target)));
      ContentNode* l = clone1(k);
      if (e.ix.labels.count(target)) l->anchorTo = e.strs.intern(target);
      else e.diags.add(Sev::Warning, "ref-unresolved", k->span, "link target '" + target + "' has no label");
      l->kids.clear();  // its content, walked like any other
      kidsOf(k, l->kids);
      out.push_back(own(k, l));
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
          ContentNode* tx = mk(Kind::text, k->span, k);
          tx->str = e.strs.intern(*v);
          out.push_back(tx);
          return;
        }
      }
      e.diags.add(Sev::Warning, "field-unresolved", k->span, "field '" + name + "' has no value");
      Slots s;
      s.set("label", name);
      inst(e.reg.unresolved, siteAt(k), s, nullptr, out);
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
        inst(C.flow->marker, siteAt(k), s, nullptr, out);
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
      if (site.where == SiteDef::Where::Replace) inst(site.tmpl, siteAt(k), s, nullptr, out);
  }

  // B2: a classed node's own changes — its refused label dropped, its alias
  // written as its anchor, its sites attached
  ContentNode* own(const ContentNode* n, ContentNode* o) {
    bool refused = e.ix.refused.count(n) != 0;
    const Instance* in = n->cls ? instanceOf(n) : nullptr;
    const ElementClass* C = in ? &e.reg.cls(in->cls) : nullptr;
    // its anchor where the node does not carry it: an alias, or a label from
    // an argument (a dterm's name, plan P3-03)
    bool anchor = C && (in->aliased || (C->labels == ElementClass::Labels::FromArg && !in->label.empty())) &&
                  !C->flow && !C->replaced();
    if (C && C->marker && !in->number.empty()) {  // its number is its marker (SemInfo.number)
      if (o == n) o = clone1(n);
      o->number = e.strs.intern(in->number);
    }
    bool sites = false;
    // (a numbered instance, or one of a class that never numbers: a proof's ∎)
    if (C && (!in->number.empty() || C->numbering == ElementClass::Numbering::Never) && C->display)
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
        case SiteDef::Where::Prepend:
        case SiteDef::Where::Append: {
          ContentNode* at = siteTarget(o, site);
          if (!at) break;  // nothing to attach to (a captionless figure)
          std::vector<ContentNode*> gen;
          inst(site.tmpl, siteAt(at), s, at, gen);
          for (ContentNode* g : gen) g->synthetic = true;  // a title's clone skips it (B3)
          if (site.where == SiteDef::Where::Prepend) at->kids.insert(at->kids.begin(), gen.begin(), gen.end());
          else at->kids.insert(at->kids.end(), gen.begin(), gen.end());
          break;
        }
        case SiteDef::Where::Tag: {  // its tag part (plan P3-03): content in slot "tag"
          ContentNode* tag = mk(Kind::seq, o->span, siteAt(o));
          setArg(tag, ArgK::slot, kSlots[(u8)SlotId::Tag].name);
          tag->synthetic = true;
          inst(site.tmpl, siteAt(o), s, tag, tag->kids);
          o->kids.push_back(tag);
          break;
        }
        case SiteDef::Where::Replace:
          break;
      }
    }
    return o;
  }
  // where a prepend/append site attaches in `o` (cloned when it is a kid):
  // itself, its first or last paragraph, or its first part in a slot (else
  // its first paragraph)
  ContentNode* siteTarget(ContentNode* o, const SiteDef& site) {
    if (site.at == SiteDef::At::Self) return o;
    ContentNode** hit = nullptr;
    if (site.at == SiteDef::At::Part)
      for (ContentNode*& k : o->kids)
        if (slotOf(k, e.strs) == site.part) {
          hit = &k;
          break;
        }
    if (!hit && site.at != SiteDef::At::LastPara)
      for (ContentNode*& k : o->kids)
        if (k->kind == Kind::para) {
          hit = &k;
          break;
        }
    if (!hit && site.at == SiteDef::At::LastPara)
      for (size_t i = o->kids.size(); i-- > 0;)
        if (o->kids[i]->kind == Kind::para) {
          hit = &o->kids[i];
          break;
        }
    if (!hit) return nullptr;
    *hit = clone1(*hit);
    return *hit;
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
    Ctx cc = siteAt(k);
    // (plan P3-23) its output stands in a group of its role, the
    // collector's name — the presentation map's hook (a table of contents
    // is a nav, the notes a section) and a rule's — unless its wrap is that
    // group already (notes, bibliography)
    const bool own = C->wrap.size() == 1 && C->wrap[0].k == TItem::K::Node && C->wrap[0].kind == Kind::group &&
                     std::any_of(C->wrap[0].args.begin(), C->wrap[0].args.end(), [&](const auto& a) {
                       return a.first == ArgK::role && a.second.k == TArg::K::Text && a.second.s == what;
                     });
    std::vector<ContentNode*>* dst = &out;
    if (!own) {
      ContentNode* g = mk(Kind::group, k->span);
      g->args.push_back({ArgK::role, ArgTag::Str, 0, e.strs.intern(what)});
      make(g, cc);
      out.push_back(g);
      cc = under(g, cc.span);
      dst = &g->kids;
    }
    if (C->hasHead) inst(C->head, cc, Slots{}, nullptr, *dst);  // (plan P3-13) its static head
    switch (C->src) {
      case CollectorDef::Src::Outline: outline(*C, cc, *dst); return;
      case CollectorDef::Src::Table: table(*C, k, cc, *dst); return;
      case CollectorDef::Src::Flow: {
        u32 lo = 0, hi = kNoInst;
        if (C->scope == CollectorDef::Scope::Section)
          if (auto at = e.ix.collectAt.find(k); at != e.ix.collectAt.end()) sectionOf(at->second, C->scopeDepth, lo, hi);
        flow(*C, cc, *dst, lo, hi);
        return;
      }
      case CollectorDef::Src::Classes: classes(*C, cc, *dst); return;
    }
  }
  // (plan P3-13) the section around a place (the instances before it): from
  // the last outline instance of level ≤ depth before it to the next
  void sectionOf(u32 at, int depth, u32& lo, u32& hi) const {
    lo = 0;
    hi = kNoInst;
    for (u32 i = 0; i < (u32)e.ix.instances.size(); i++) {
      const Instance& in = e.ix.instances[i];
      if (!e.reg.cls(in.cls).outline || in.level > depth) continue;
      if (i < at) lo = i;
      else {
        hi = i;
        break;
      }
    }
  }

  // (plan P3-13; design T3 Query Classes) the instances of the collector's
  // classes, in document order: a list of figures, of tables, of theorems
  void classes(const CollectorDef& C, const Ctx& cc, std::vector<ContentNode*>& out) {
    std::vector<const Instance*> items;
    for (const Instance& in : e.ix.instances)
      if (std::find(C.classes.begin(), C.classes.end(), in.cls) != C.classes.end()) items.push_back(&in);
    if (items.empty() && C.hasEmpty) {
      inst(C.empty, cc, Slots{}, nullptr, out);
      return;
    }
    Slots ws;
    ws.each = [&](const TItem&, const Ctx&, ContentNode* container, std::vector<ContentNode*>& o) {
      for (const Instance* in : items) {
        Slots s;  // (its title content, not its excerpt: titleSlot)
        s.set("number", in->number);
        s.set("supplement", supplementText(in->supplement));
        titleSlot(*in, cc, s);
        s.set("anchor", in->label);
        // its parts' content, by slot name (a figure's caption), cloned as
        // a title is
        if (in->node)
          for (const ContentNode* k : in->node->kids)
            if (const SlotId part = slotOf(k, e.strs); part != SlotId::None) {
              std::vector<ContentNode*> nodes;
              for (const ContentNode* x : k->kids) cloneTitle(x, cc, k->scope, k->env, nodes);
              s.nodes.push_back({kSlots[(u8)part].name, nodes});
            }
        inst(C.entry, cc, s, container, o);
      }
    };
    inst(C.wrap, cc, ws, nullptr, out);
  }

  // (plan P3-03; D-S03) an entry's title: the instance's title content
  // cloned (cloneTitle), else its title text
  void titleSlot(const Instance& in, const Ctx& dest, Slots& s) {
    if (!in.titleNode) {
      s.set("title", in.title);
      return;
    }
    std::vector<ContentNode*> nodes;
    for (const ContentNode* k : in.titleNode->kids)
      cloneTitle(k, dest, in.titleNode->scope, in.titleNode->env, nodes);
    s.nodes.push_back({"title", nodes});
  }
  // cloneTitle (design T3 B3): a title's content copied into a collector —
  // what a site generated, flows (a footnote: its marker), anchors,
  // collectors, events and entries left out, a reference as its resolved
  // text; the author's own styling kept and the destination's context
  // taken (Cascade.lift); the copy belongs to the collector (its span, no
  // raw map) and is never walked again
  void cloneTitle(const ContentNode* k, const Ctx& dest, StyleId oldParentScope, RuleEnvId env,
                  std::vector<ContentNode*>& out) {
    if (k->synthetic || k->kind == Kind::comment || k->kind == Kind::collect || k->kind == Kind::event ||
        k->kind == Kind::entry)
      return;
    if (k->cls && e.reg.cls(k->cls).flow) return;  // a flow item (a footnote) stays where it is
    if (k->kind == Kind::ref) {  // its resolved text, unlinked
      ContentNode* r = resolveRef(clone1(k));
      for (const ContentNode* x : r->kids) cloneTitle(x, dest, k->scope, k->env, out);
      return;
    }
    ContentNode* d = clone1(k);
    Styling st = e.styles.get(dest.style);
    Cascade::NodeView v{k->kind};
    v.args = &k->args;
    v.role = attrStr(k, ArgK::role);
    v.cls = attrStr(k, ArgK::class_);
    v.lang = e.styles.get(k->scope).lang;
    e.cascade.reenter(st, v, env, e.styles.get(k->scope), e.styles.get(oldParentScope));
    d->style = e.styles.idOf(st);
    d->props = ~0u;  // kPropsUnset: the destination's
    d->env = kEnvUnset;
    e.made++;
    d->span = dest.span;
    d->rawmap = nullptr;
    d->nrawmap = 0;
    dropArg(d, ArgK::label);
    d->kids.clear();
    Ctx under = dest;
    under.style = d->style;
    for (const ContentNode* x : k->kids) cloneTitle(x, under, k->scope, k->env, d->kids);
    out.push_back(d);
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
      titleSlot(in, cc, s);
      s.set("anchor", in.label);
      inst(C.entry, cc, s, container, container->kids);
    };
    Slots ws;
    ws.each = [&](const TItem&, const Ctx& c, ContentNode* container, std::vector<ContentNode*>& o) {
      if (!container) {
        for (const Instance* in : items) {
          Slots s;
          s.set("number", in->number);
          titleSlot(*in, cc, s);
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
    if (!C.keyedRows) {  // rows of instances (a glossary, an index)
      // (plan P3-13) in sort-key order, grouped by key (an index: a key and
      // its occurrences), else each row by itself in document order
      std::vector<const Row*> rows;
      for (const Row& r : e.ix.rows)
        if (r.table == C.table) rows.push_back(&r);
      auto order = [](const Row* r) -> const std::string& { return r->sortKey.empty() ? r->key : r->sortKey; };
      if (C.bySortKey) std::stable_sort(rows.begin(), rows.end(), [&](const Row* a, const Row* b) { return order(a) < order(b); });
      std::vector<std::vector<const Row*>> groups;
      for (const Row* r : rows) {
        if (C.groupByKey)
          if (auto g = std::find_if(groups.begin(), groups.end(), [&](const auto& x) { return x[0]->key == r->key; });
              g != groups.end()) {
            g->push_back(r);
            continue;
          }
        groups.push_back({r});
      }
      if (groups.empty() && C.hasEmpty) {
        inst(C.empty, cc, Slots{}, nullptr, out);
        return;
      }
      // a row's anchor: its instance's (an index entry's alias), else its key
      auto anchorOf = [&](const Row* r) {
        return r->inst != kNoInst && !e.ix.instances[r->inst].label.empty() ? e.ix.instances[r->inst].label : r->key;
      };
      ws.each = [&](const TItem&, const Ctx&, ContentNode* container, std::vector<ContentNode*>& o) {
        for (const std::vector<const Row*>& g : groups) {
          const Row& r = *g[0];
          Slots s;
          s.set("title", r.title);
          s.set("key", r.key);
          s.set("anchor", anchorOf(&r));
          s.set("body-text", r.bodyText);
          if (r.node && !r.node->kids.empty()) {  // its content, as a title is cloned
            std::vector<ContentNode*> body;
            for (const ContentNode* x : r.node->kids) cloneTitle(x, cc, r.node->scope, r.node->env, body);
            s.nodes.push_back({"body", body});
          }
          // each{of: occurrences}: every row of the key, its back-link
          s.each = [&](const TItem& it, const Ctx& ec, ContentNode* cont, std::vector<ContentNode*>& eo) {
            for (size_t k = 0; k < g.size(); k++) {
              if (k) inst(it.sep, ec, Slots{}, cont, eo);
              Slots os;
              os.set("anchor", anchorOf(g[k]));
              os.set("ordinal", std::to_string(k + 1));
              inst(it.kids, ec, os, cont, eo);
            }
          };
          inst(C.entry, cc, s, container, o);
        }
      };
      inst(C.wrap, cc, ws, nullptr, out);
      return;
    }
    // keyed rows: the cited ones in citation order, then (cited-then-all)
    // every other row in document order
    // (plan P3-13) in its scope: a refsection's bibliography lists its citations
    const u32 ks = keyedHere(C.rowCounter);
    std::vector<std::string> keys = e.counters.keyOrder(C.rowCounter, ks);
    std::string_view cited = e.strs.get(attrStr(k, ArgK::cited));
    if (cited.empty() && e.strs.get(attrStr(k, ArgK::form)) == "all") cited = "cited-then-all";  // (v6–9 buffers)
    if (cited == "cited-then-all" || (cited.empty() && C.cited == CollectorDef::Cited::CitedThenAll))
      for (const Row& r : e.ix.rows)
        if (r.table == C.table && !e.counters.keyedIfSeen(C.rowCounter, r.key, ks)) {
          e.counters.keyed(C.rowCounter, r.key, ks);
          keys.push_back(r.key);
        }
    if (keys.empty()) {
      inst(C.empty, cc, Slots{}, nullptr, out);
      return;
    }
    // the first rendering of a table owns its rows' nodes, the first in a
    // scope their anchors there; a later one clones the rows and carries
    // none (one id per row)
    const bool first = !contains(tablesRendered, C.table);
    if (first) tablesRendered.push_back(C.table);
    const std::string scoped = C.table + '\0' + std::to_string(ks);
    const bool anchors = !contains(anchorsRendered, scoped);
    if (anchors) anchorsRendered.push_back(scoped);
    ws.each = [&](const TItem&, const Ctx&, ContentNode* container, std::vector<ContentNode*>& o) {
      for (const std::string& key : keys) {
        const Row* r = e.ix.row(C.table, key);
        if (!r || !r->node) continue;  // a cited key without a row
        Slots s;
        s.set("ordinal", std::to_string(e.counters.keyedIfSeen(C.rowCounter, key, ks)));
        s.set("anchor", anchors ? rowAnchorIn(C.rowAnchor, key, ks) : std::string());
        std::vector<ContentNode*> body;
        for (ContentNode* x : r->node->kids) body.push_back(first ? x : deepClone(x));
        s.nodes.push_back({"body", body});
        std::vector<ContentNode*> entry;
        inst(C.entry, siteAt(r->node), s, container, entry);
        for (ContentNode* x : entry) o.push_back(walk(x));  // rows may carry references
      }
    };
    inst(C.wrap, cc, ws, nullptr, out);
  }

  // a flow's items not yet placed, of those in [lo, hi) (a scoped
  // collector's section): each item's body (materialized here, where it is
  // placed) as the entry's paragraphs. A deferred item's entry is an insert
  // of its marker's page and the rest of its wrap the inserts' separator
  // (plan P3-13: the paged sheets move them, design T6)
  void flow(const CollectorDef& C, const Ctx& cc, std::vector<ContentNode*>& out, u32 lo = 0, u32 hi = kNoInst) {
    std::vector<u32> items;
    bool any = false;
    if (const std::vector<u32>* all = e.ix.flowItems(C.flow))
      for (u32 id : *all)
        if (id >= lo && id < hi) {
          any = true;
          if (!placed[id]) items.push_back(id);
        }
    if (items.empty()) {
      if (any) e.diags.add(Sev::Info, "flow-already-placed", cc.span, C.flow + " were already placed");
      inst(C.empty, cc, Slots{}, nullptr, out);
      return;
    }
    bool deferred = false;
    for (u32 id : items) {
      placed[id] = 1;
      deferred = deferred || e.reg.cls(e.ix.instances[id].cls).flow->placement == FlowDef::Placement::Deferred;
    }
    Slots ws;
    ws.each = [&](const TItem&, const Ctx&, ContentNode* container, std::vector<ContentNode*>& o) {
      for (u32 id : items) {
        const Instance& in = e.ix.instances[id];
        // its body, walked as where it stands (its instances around it)
        std::vector<const Instance*> outer = std::move(scope);
        scope.clear();
        for (u32 a = id; a != kNoInst; a = e.ix.instances[a].parent) scope.insert(scope.begin(), &e.ix.instances[a]);
        std::vector<ContentNode*> body;
        kidsOf(in.node, body);
        scope = std::move(outer);
        Slots s;
        instanceSlots(in, s);
        s.nodes.push_back({"body", body});
        s.paras = [&](const TItem& it, const Ctx& c, std::vector<ContentNode*>& po) { paras(it, c, body, s, po); };
        // each{of: occurrences}: its markers' back-links (a named note's
        // several: a b c)
        std::vector<std::string> marks{in.markerAlias};
        marks.insert(marks.end(), in.occurrences.begin(), in.occurrences.end());
        if (marks.size() > 1) s.set("occurrences", std::to_string(marks.size()));
        s.each = [&](const TItem& it, const Ctx& ec, ContentNode* cont, std::vector<ContentNode*>& eo) {
          for (size_t m = 0; m < marks.size(); m++) {
            if (m) inst(it.sep, ec, Slots{}, cont, eo);
            Slots os;
            os.set("anchor", marks[m]);
            os.set("ordinal", std::to_string(m + 1));
            os.set("letter", formatNumber("a", {(int)m + 1}, e.reg));
            inst(it.kids, ec, os, cont, eo);
          }
        };
        const size_t at = o.size();
        inst(C.entry, siteAt(in.node), s, container, o);
        if (e.reg.cls(in.cls).flow->placement == FlowDef::Placement::Deferred)
          for (size_t x = at; x < o.size(); x++) o[x]->insertAt = in.node->span.start;
      }
    };
    const size_t at = out.size();
    inst(C.wrap, cc, ws, nullptr, out);
    if (deferred)
      for (size_t x = at; x < out.size(); x++) out[x]->insertAt = kInsertArea;
  }
  // (plan P3-13) a flow's items before instance `hi` that no collector
  // placed, where its placement puts them (a section's end, the document's)
  void placeRest(const FlowDef& F, const Ctx& cc, std::vector<ContentNode*>& out, u32 hi = kNoInst) {
    const std::vector<u32>* items = e.ix.flowItems(F.name);
    const CollectorDef* fc = e.reg.flowCollector(F.name);
    if (!items || !fc) return;
    for (u32 id : *items)
      if (id < hi && !placed[id]) {
        flow(*fc, cc, out, 0, hi);
        return;
      }
  }

  // Cascade.lift (plan P3-01; T4, D-S09): a moved node and its subtree
  // re-entered under a new parent (`style`): each keeps its own delta and
  // its scope, the rules of its old place fold in again; block properties
  // are the destination's (settled after materialize)
  ContentNode* lift(const ContentNode* k, StyleId style, StyleId oldParentScope, RuleEnvId env) {
    ContentNode* d = clone1(k);
    Styling st = e.styles.get(style);
    Cascade::NodeView v{k->kind};
    v.args = &k->args;
    v.role = attrStr(k, ArgK::role);
    v.cls = attrStr(k, ArgK::class_);
    v.lang = e.styles.get(k->scope).lang;
    e.cascade.reenter(st, v, env, e.styles.get(k->scope), e.styles.get(oldParentScope));
    d->style = e.styles.idOf(st);
    d->props = ~0u;  // kPropsUnset
    d->env = kEnvUnset;  // (block properties: the destination's)
    e.made++;
    for (ContentNode*& kid : d->kids) kid = lift(kid, d->style, k->scope, k->env);
    return d;
  }

  // a body as paragraphs, lifted from its site `c` (a note's): an inline
  // body is one paragraph whose content is entered under an inline wrapper
  // of the item's role (note-body: the rules size it), a block body keeps its
  // blocks, entered as under that wrapper; the anchor goes on the first
  // paragraph and the tail ends the last (both made when the body does not
  // start or end with one)
  void paras(const TItem& it, const Ctx& c, const std::vector<ContentNode*>& body, const Slots& s,
             std::vector<ContentNode*>& out) {
    // the wrapper: made at the site's scope, in its rule env (run
    // properties come from the site, not from rules that matched there)
    Ctx site = c;
    site.style = c.scope;
    site.d = StyleDelta{};
    site.size = 1.0f;
    ContentNode* wrap = mk(Kind::styled, c.span);
    if (!it.name.empty()) setArg(wrap, ArgK::role, it.name);
    make(wrap, site);
    bool blocks = false;
    for (const ContentNode* k : body) blocks = blocks || !isInlineLevel(k->kind);
    std::vector<ContentNode*> res;
    if (blocks) {
      for (const ContentNode* k : body) res.push_back(lift(k, wrap->style, c.scope, c.env));
    } else {
      for (const ContentNode* k : body) wrap->kids.push_back(lift(k, wrap->style, c.scope, c.env));
      ContentNode* para = mk(Kind::para, c.span);
      para->kids.push_back(wrap);
      res.push_back(para);
    }
    ContentNode* first = nullptr;
    ContentNode* last = nullptr;
    for (ContentNode* k : res)
      if (k->kind == Kind::para) {
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
    // the tail: inside the wrapper (an inline body), else as under it
    const bool inWrap = !blocks && last->kids.size() == 1 && last->kids[0] == wrap;
    inst(it.tail, under(wrap, c.span), s, inWrap ? wrap : last, inWrap ? wrap->kids : last->kids);
    out.insert(out.end(), res.begin(), res.end());
  }

  ContentNode* run(const ContentNode* root) {
    root_ = root;
    std::vector<std::string> seen;
    for (const ElementClass& C : e.reg.classes)
      if (C.flow && C.flow->placement == FlowDef::Placement::SectionEnd && !contains(seen, C.flow->name)) {
        seen.push_back(C.flow->name);
        sectionFlows.push_back(&*C.flow);
      }
    ContentNode* o = walk(root);
    if (o == root) o = clone1(root);
    // a flow's items no collector placed go where its class says: the
    // document's end (the last section's, a deferred flow's on screen)
    seen.clear();
    for (const ElementClass& C : e.reg.classes) {
      if (!C.flow || !C.flow->atEnd() || contains(seen, C.flow->name)) continue;
      seen.push_back(C.flow->name);
      placeRest(*C.flow, siteAt(root), o->kids);
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
