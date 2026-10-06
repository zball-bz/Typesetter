#include "semantic_html.h"

#include <algorithm>
#include <unordered_map>

#include "html_writer.h"
#include "../resource/resource_table.h"
#include "../code/overlay.h"
#include "../model/cascade.h"
#include "../elements/registry.h"
#include "../math/env.h"
#include "../math/math.h"
#include "math_html.h"
#include "rules_css.h"

namespace tsr {

namespace {

constexpr auto esc = escapeHtml;

struct Sem {
  Interner& strs;
  StyleTable& styles;
  std::string& out;
  const ResourceTable* rt;  // answered code tokens (plan P1-19)
  const Registry* reg;     // the classes' semantic elements (plan P2-05)
  const Cascade* cascade;  // document envs (plan P3-01)
  const NodePropsTable* props;  // a code block's overlays (plan P3-22)
  const SemanticMath& math;     // (plan P3-27) formulas as boxes or source
  std::string topEnv;      // the top-level block's env mark ("" = none)
  // a preview (renderSemanticFragment): references it leaves out
  const std::function<bool(StrRef)>* backlink = nullptr;

  const ArgVal* arg(const ContentNode* n, ArgK k) {
    for (const ArgVal& a : n->args)
      if (a.key == k) return &a;
    return nullptr;
  }
  std::string_view argS(const ContentNode* n, ArgK k) {
    const ArgVal* a = arg(n, k);
    return (a && a->tag == ArgTag::Str) ? strs.get(a->ref) : std::string_view{};
  }
  double argN(const ContentNode* n, ArgK k, double dflt) {
    const ArgVal* a = arg(n, k);
    return (a && a->tag == ArgTag::Num) ? a->num : dflt;
  }

  // (plan P3-27; design T7 S13, D-R07) a formula as its box: laid out from
  // the math font's metrics alone (its text runs estimated in the math font
  // before anything is measured), at its style's size, written as the
  // typeset page writes it — role=math and its source as aria-label when the
  // page labels formulas (D-R04)
  void formulaBox(const ContentNode* n, bool display) {
    const MathSource ms = mathSource(n, strs);
    MathSpanOpts o;
    o.display = display;
    o.span = n->span;
    o.label = math.label;
    if (!display) {
      const MathScope scope{math.env, n->declEpoch, n->style};
      DiagSink scratch;  // (what the typeset layout reports, not this estimate)
      const double sizePx = emPx(math.basePx, styles.get(n->style));
      const MathBox* box = layoutMathFormula(ms.text, false, sizePx, *math.arena, strs, scratch, n->span,
                                             /*text=*/nullptr, /*parseDiags=*/false, &scope);
      writeMathSpan(out, box, ms.copy, strs, o);
      return;
    }
    // (plan P3-29, D-S11) a display formula's rows — aligned with its
    // equations block's (aligned below), else on their own — one under the
    // other in one span: the formula
    std::vector<const MathBox*> rows;
    if (auto it = alignedRows.find(n); it != alignedRows.end()) {
      rows = it->second;
    } else {
      const MathRows r = displayRows(n);
      if (r.aligned()) {
        std::vector<std::vector<MathBox*>> out1;
        alignMathRows({&r}, *math.arena, out1);
        rows.assign(out1[0].begin(), out1[0].end());
      } else {
        rows.push_back(r.rows[0][0].box);
      }
    }
    if (rows.size() == 1) writeMathSpan(out, rows[0], ms.copy, strs, o);
    else writeMathRows(out, rows, ms.copy, strs, o);
  }
  // a display formula's rows of cells (its text runs estimated)
  MathRows displayRows(const ContentNode* n) {
    const MathSource ms = mathSource(n, strs);
    const MathScope scope{math.env, n->declEpoch, n->style};
    DiagSink scratch;
    return layoutMathRows(ms.text, emPx(math.basePx, styles.get(n->style)), *math.arena, strs, scratch, n->span,
                          /*text=*/nullptr, /*parseDiags=*/false, &scope);
  }
  // (plan P3-29) an equations block's formulas, aligned together: their rows
  std::unordered_map<const ContentNode*, std::vector<const MathBox*>> alignedRows;

  // (plan P3-07, D-R06) what copy takes of a node, as the typeset view
  // says it: an omitted or replaced node's kind (its `syn`, else its kind)
  // and replacement — so both phases share one copy contract
  bool copyMarked(const ContentNode* n) const { return copyAttr(n, strs).marked; }
  void copyAttrs(Tag& t, const ContentNode* n) {
    const CopyAttr c = copyAttr(n, strs);
    if (c.mode == CopyAttr::Mode::Text) return;
    t.attr("data-syn", c.syn);
    if (c.mode == CopyAttr::Mode::Replace) t.attr("data-copy", c.replace);
  }

  // shared attributes: span anchoring + optional pid + label anchor
  void attrs(Tag& t, const ContentNode* n, int pid) {
    if (pid >= 0) t.num("data-pid", (unsigned)pid);
    if (pid >= 0 && !topEnv.empty()) t.attr("data-tsr-env", topEnv);
    if (!n->span.empty()) {
      t.num("data-s", n->span.start);
      t.num("data-e", n->span.end);
    }
    std::string_view label = argS(n, ArgK::label);
    if (!label.empty()) t.id(label);
    copyAttrs(t, n);
  }
  // <name …shared attributes…> (plan P3-23: its presentation row's ARIA role)
  void open(std::string_view name, const ContentNode* n, int pid, const char* cls = nullptr) {
    Tag t(out, name);
    if (cls) t.attrSafe("class", cls);
    if (reg)
      if (const HtmlShape* sh = reg->shapeOf(n, strs); sh && !sh->aria.empty()) t.attr("role", sh->aria);
    attrs(t, n, pid);
    t.open();
  }

  // (plan P3-18) a class list as tsr-c-* tokens
  static std::string classTokens(std::string_view cl) {
    std::string out;
    for (size_t at = 0; at < cl.size();) {
      size_t sp = cl.find(' ', at);
      if (sp == std::string_view::npos) sp = cl.size();
      if (sp > at) {
        if (!out.empty()) out += ' ';
        out += "tsr-c-";
        out += cl.substr(at, sp - at);
      }
      at = sp + 1;
    }
    return out;
  }
  // a text leaf in its scope (its rule-free style); `classes`: the style
  // classes in force on it (plan P3-18: its nodes', a code token's)
  void textRun(StyleId sid, std::string_view text, StrRef classes = 0) {
    // a leaf's scope is its rule-free style (instantiation folds styled
    // deltas onto leaves — document-model §3, plan P3-01): render it, so
    // token colours and patch styles reach the no-JS page; what rules add
    // is the page's CSS, and roles map to elements (roleTag). Kind::styled
    // is transparent below.
    const Styling& st = styles.get(sid);
    // a superscript nests its emphasis (sup > strong|em): it used to
    // drop the bold/italic of a marker inside emphasis (plan P1-02)
    const char* outer = st.baseline == BASELINE_SUPER ? "sup" : st.baseline == BASELINE_SUB ? "sub" : nullptr;
    const bool bold = st.weight >= 600;
    const char* inner = bold ? "strong" : st.italic ? "em" : nullptr;
    const char* tag = inner ? inner : outer;
    if (inner && outer) {
      out += "<";
      out += outer;
      out += ">";
    }
    std::string style;
    if (st.fontFamily) {
      style += "font-family:";
      esc(style, strs.get(st.fontFamily));
      style += ";";
    }
    if (st.color) {
      style += "color:";
      esc(style, strs.get(st.color));
      style += ";";
    }
    // its size as the typeset page sets it (plan P3-23): an absolute size
    // times its multiplier, else the multiplier, relative to its element
    if (st.sizePx > 0) {
      style += "font-size:";
      fmtPx(style, st.sizePx * st.sizeMul);
      style += ";";
    } else if (st.sizeMul != 1.0f) {
      char buf[32];
      style += "font-size:";
      style.append(buf, (size_t)std::snprintf(buf, sizeof buf, "%gem", (double)st.sizeMul));
      style += ";";
    }
    if (st.weight && st.weight != 400 && st.weight != 700) {
      style += "font-weight:";
      style += std::to_string(st.weight);
      style += ";";
    }
    if (st.decoration) {
      style += "text-decoration:";
      if (st.decoration & DECORATION_UNDER) style += "underline ";
      if (st.decoration & DECORATION_OVER) style += "overline ";
      if (st.decoration & DECORATION_STRIKE) style += "line-through ";
      style.pop_back();
      style += ";";
    }
    if (!style.empty()) style.pop_back();
    if (!classes) classes = st.classes;
    const bool wrap = tag || !style.empty() || st.lang || classes;
    if (wrap) {
      Tag t(out, tag ? tag : "span");
      if (classes) t.attrSafe("class", classTokens(strs.get(classes)));
      if (st.lang) t.attr("lang", strs.get(st.lang));
      t.style(style);
      // bold+italic: strong tag + italic style
      if (st.italic && tag && bold) t.style("font-style:italic");
      t.open();
    }
    esc(out, text);
    if (wrap) {
      out += "</";
      out += tag ? tag : "span";
      out += ">";
    }
    if (inner && outer) {
      out += "</";
      out += outer;
      out += ">";
    }
  }

  void inlineKids(const ContentNode* n) {
    for (const ContentNode* k : n->kids) inl(k);
  }
  // its content, inside the element its role reads as on this page (its
  // inline presentation row, plan P3-23: the rules give it in the typeset
  // view) — unless its own style says it already (a marker in raised text
  // is raised once, as the typeset view sets it)
  void roleKids(const ContentNode* n) {
    const StrRef r = attrStr(n, ArgK::role);
    const HtmlShape* re = r && reg ? reg->htmlRow(strs.get(r)) : nullptr;
    if (re && !re->inlineLevel) re = nullptr;
    std::string_view tag = re ? std::string_view(re->element) : std::string_view{};
    if (re) {
      using Says = HtmlShape::Says;
      const Styling& sc = styles.get(n->scope);
      if ((re->says == Says::Super && sc.baseline == BASELINE_SUPER) || (re->says == Says::Bold && sc.weight >= 600) ||
          (re->says == Says::Italic && sc.italic))
        tag = {};
    }
    if (!tag.empty()) {
      out += "<";
      out += tag;
      out += ">";
    }
    inlineKids(n);
    if (!tag.empty()) {
      out += "</";
      out += tag;
      out += ">";
    }
  }

  void inl(const ContentNode* n) {
    // a marked node without an element of its own says it on a span
    if (copyMarked(n) && n->kind != Kind::link && n->kind != Kind::ref && n->kind != Kind::error) {
      {
        Tag t(out, "span");
        copyAttrs(t, n);
        t.open();
      }
      inlNode(n);
      out += "</span>";
      return;
    }
    inlNode(n);
  }
  void inlNode(const ContentNode* n) {
    switch (n->kind) {
      case Kind::text:
        textRun(n->scope, strs.get(n->str), styles.get(n->style).classes);
        return;
      case Kind::styled:
        // transparent: the leaves carry the folded styles (above), a role
        // its element; a style.where marks where its env begins
        if (startsEnv(n) && cascade) {
          const std::string env = envAttr(*cascade, n->env, strs);
          {
            Tag t(out, "span");
            t.attr("data-tsr-env", env);
            t.open();
          }
          roleKids(n);
          out += "</span>";
          return;
        }
        roleKids(n);
        return;
      case Kind::link:
      case Kind::ref: {
        if (backlink && n->anchorTo && (*backlink)(n->anchorTo)) return;
        // its href: a resolved target's anchor (AnchorNamer, plan P3-04),
        // else a link's URL
        const std::string href = n->anchorTo ? AnchorNamer::href(strs.get(n->anchorTo)) : std::string(argS(n, ArgK::url));
        if (href.empty()) {  // unresolved ref / grouped citation container
          roleKids(n);
          return;
        }
        {
          Tag t(out, "a");
          t.attr("href", href);
          std::string_view id = argS(n, ArgK::label);  // inline anchor (marker)
          if (!id.empty()) t.id(id);
          copyAttrs(t, n);
          t.open();
        }
        roleKids(n);
        out += "</a>";
        return;
      }
      case Kind::code:
        out += "<code>";
        if (!n->kids.empty() && n->kids[0]->kind == Kind::text)
          esc(out, strs.get(n->kids[0]->str));
        out += "</code>";
        return;
      case Kind::hardbreak:
        out += "<br>";
        return;
      case Kind::mathinline:
        // (plan P3-27, D-R07) its box, estimated before measurement; else
        // its source as written (fragments and holes, plan P2-15)
        if (math.boxes) {
          formulaBox(n, /*display=*/false);
          return;
        }
        out += "<code class=\"tsr-mathsrc\">$";
        esc(out, mathSource(n, strs).copy);
        out += "$</code>";
        return;
      case Kind::error:
        {
          Tag t(out, "span");
          t.attrSafe("class", "tsr-err");
          t.attr("title", argS(n, ArgK::message));
          t.attrSafe("data-syn", "error");  // copy omits it (D-R01)
          t.open();
        }
        out += "&#9888; ";
        esc(out, argS(n, ArgK::message));
        out += "</span>";
        return;
      case Kind::comment:
        return;  // document-model nodes, excluded from output
      case Kind::entry:  // (plan P3-13) an anchored entry marks its place
        if (std::string_view id = argS(n, ArgK::label); !id.empty()) {
          {
            Tag t(out, "span");
            t.id(id);
            t.open();
          }
          out += "</span>";
        }
        return;
      case Kind::group: {
        // an inline labelled group (a term used inline, also inside a table
        // cell — plan P1-17) is a link target: its anchor wraps its content
        std::string_view id = argS(n, ArgK::label);
        if (id.empty()) {
          inlineKids(n);
          return;
        }
        {
          Tag t(out, "span");
          t.id(id);
          t.open();
        }
        inlineKids(n);
        out += "</span>";
        return;
      }
      default:
        // an inline object of the shaper's flatten table (plan P1-13) is
        // painted, never dropped; other kinds keep their content (block
        // kinds in inline position are the placement rules', P2-11/P3-17)
        if (kKinds[(u16)n->kind].inl == InlineShape::Object) inlineObject(n);
        else inlineKids(n);
        return;
    }
  }

  // an inline image or raw mark (a formula has its own case above)
  void inlineObject(const ContentNode* n) {
    if (n->kind == Kind::raw) {
      out += "<span class=\"tsr-iraw\">";
      out += argS(n, ArgK::html);  // trusted, handler-declared passthrough (§9)
      out += "</span>";
      return;
    }
    std::string_view src = argS(n, ArgK::src);
    if (n->kind != Kind::image || !safeImageSrc(src)) {
      out += "<span class=\"tsr-imgph\">";
      esc(out, argS(n, ArgK::alt));
      out += "</span>";
      return;
    }
    Tag t(out, "img");
    t.attr("src", src);
    t.attr("alt", argS(n, ArgK::alt));
    std::string style;
    for (const ArgVal& a : n->args)
      if (a.tag == ArgTag::Num && (a.key == ArgK::w || a.key == ArgK::h) && a.num > 0) {
        style += a.key == ArgK::w ? "width:" : "height:";
        fmtPx(style, a.num);
        style += ";";
      }
    if (!style.empty()) {
      style.pop_back();
      t.style(style);
    }
    t.open();
  }

  // (plan P3-23) a node's presentation row, and the element it names
  const HtmlShape* shape(const ContentNode* n) const { return reg ? reg->shapeOf(n, strs) : nullptr; }
  std::string elementOf(const ContentNode* n, const HtmlShape* sh, std::string_view dflt) {
    if (!sh || sh->element.empty()) return std::string(dflt);
    if (!sh->levelSuffix) return sh->element;
    int level = attrInt(n, ArgK::level, 1);
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    return sh->element + (char)('0' + level);
  }
  void close(std::string_view el) {
    out += "</";
    out += el;
    out += ">\n";
  }

  // (plan P3-23; document-model §9.2) a defined term as a description
  // list: its name (role term-name) the term, its description (role
  // term-def, and the blocks after its first paragraph) the definition
  void term(const ContentNode* n, const HtmlShape* sh, int pid) {
    const ContentNode* head = !n->kids.empty() && n->kids[0]->kind == Kind::para ? n->kids[0] : nullptr;
    auto roleIs = [&](const ContentNode* k, std::string_view r) {
      return k->kind == Kind::styled && argS(k, ArgK::role) == r;
    };
    const std::string el = elementOf(n, sh, "dl");
    {
      Tag t(out, el);
      std::string_view role = argS(n, ArgK::role);
      if (!role.empty() && sh->dataRole) t.attr("data-role", role);
      if (!sh->aria.empty()) t.attr("role", sh->aria);
      attrs(t, n, pid);
      t.open();
      out += "\n";
    }
    out += "<dt>";
    if (head)
      for (const ContentNode* k : head->kids)
        if (roleIs(k, "term-name")) inlineKids(k);
    out += "</dt>\n";
    bool def = false;
    if (head)
      for (const ContentNode* k : head->kids) def = def || roleIs(k, "term-def");
    const bool blocks = n->kids.size() > (head ? 1u : 0u);
    if (def || blocks) {
      out += "<dd>";
      if (head)
        for (const ContentNode* k : head->kids)
          if (roleIs(k, "term-def")) inlineKids(k);
      if (blocks) {
        out += "\n";
        for (size_t i = head ? 1 : 0; i < n->kids.size(); i++) block(n->kids[i], -1);
      }
      out += "</dd>\n";
    }
    close(el);
  }

  void block(const ContentNode* n, int pid) {
    const HtmlShape* sh = shape(n);
    switch (n->kind) {
      case Kind::para: {
        const std::string el = elementOf(n, sh, "p");
        if (attrBool(n, ArgK::cont, false)) {  // (plan P3-17) a continuation: no indent, no space above
          Tag t(out, el);
          attrs(t, n, pid);
          t.decl("margin-top", "0").decl("text-indent", "0");
          t.open();
        } else {
          open(el, n, pid);
        }
        inlineKids(n);
        close(el);
        return;
      }
      case Kind::heading: {
        const std::string el = elementOf(n, sh, "h1");
        open(el, n, pid);
        inlineKids(n);
        close(el);
        return;
      }
      case Kind::list: {
        const ArgVal* ord = arg(n, ArgK::ordered);
        bool ordered = ord && ord->num != 0;
        int start = attrInt(n, ArgK::start, 1);
        {
          Tag t(out, ordered ? "ol" : "ul");
          if (ordered && start != 1) t.attrSafe("start", std::to_string(start));
          attrs(t, n, pid);
          t.open();
          out += "\n";
        }
        for (const ContentNode* k : n->kids) {
          // a tight single-paragraph item inlines the paragraph: its anchor
          // moves onto the <li> (P0-09 i; footnote ids used to dangle)
          bool tight = k->kids.size() == 1 && k->kids[0]->kind == Kind::para;
          std::string_view lid = tight ? argS(k->kids[0], ArgK::label) : std::string_view{};
          {
            Tag t(out, "li");
            if (!lid.empty()) t.id(lid);
            t.open();
          }
          // an item's blocks flow inside the li
          bool sub = false;
          for (const ContentNode* b : k->kids) {
            if (b->kind == Kind::para && !sub && k->kids.size() == 1) {
              inlineKids(b);  // tight single-para item: no inner <p>
            } else {
              block(b, -1);
            }
            sub = true;
          }
          out += "</li>\n";
        }
        out += ordered ? "</ol>\n" : "</ul>\n";
        return;
      }
      case Kind::equations: {
        // (plan P3-29, D-S11) display rows aligned at their `&`: each its own
        // formula (its number, its label), their columns shared
        open(elementOf(n, sh, "div"), n, pid, "tsr-equations");
        out += "\n";
        if (math.boxes) {
          std::vector<const ContentNode*> members;
          std::vector<MathRows> rows;
          for (const ContentNode* k : n->kids)
            if (k->kind == Kind::mathblock) {
              members.push_back(k);
              rows.push_back(displayRows(k));
            }
          bool aligned = false;
          for (const MathRows& r : rows) aligned = aligned || r.aligned();
          if (aligned) {
            std::vector<const MathRows*> group;
            for (const MathRows& r : rows) group.push_back(&r);
            std::vector<std::vector<MathBox*>> out1;
            alignMathRows(group, *math.arena, out1);
            for (size_t i = 0; i < members.size(); i++) alignedRows[members[i]].assign(out1[i].begin(), out1[i].end());
          }
        }
        for (const ContentNode* k : n->kids) block(k, -1);
        close(elementOf(n, sh, "div"));
        return;
      }
      case Kind::quote: {
        const std::string el = elementOf(n, sh, "blockquote");
        open(el, n, pid);
        out += "\n";
        for (const ContentNode* k : n->kids) block(k, -1);
        close(el);
        return;
      }
      case Kind::codeblock: {
        open("pre", n, pid);
        {
          Tag t(out, "code");
          std::string_view lang = argS(n, ArgK::lang);
          if (!lang.empty()) t.attr("class", "language-" + std::string(lang));
          t.open();
        }
        const ContentNode* body = !n->kids.empty() && n->kids[0]->kind == Kind::text ? n->kids[0] : nullptr;
        // (plan P3-23) its margin slot (plan P2-13: sidecar notes are
        // content — a footnote marker, a reference) projected inline: after
        // its line, behind the fence's declared marker, as copy omits it
        // (D-R03: the code is what a code block copies)
        const ContentNode* margin = nullptr;
        for (const ContentNode* k : n->kids)
          if (k->kind == Kind::group && slotOf(k, strs) == SlotId::Margin) margin = k;
        const std::string_view marker = argS(n, ArgK::sidecar);
        auto note = [&](size_t li) {
          if (!margin || li >= margin->kids.size() || margin->kids[li]->kids.empty()) return;
          out += " <span class=\"tsr-margin\" data-syn=\"sidecar\">";
          if (!marker.empty()) {
            esc(out, marker);
            out += " ";
          }
          inl(margin->kids[li]);
          out += "</span>";
        };
        const u32 overlays = props ? overlayMask(strs.get(props->get(n->props).codeOverlays)) : 0;
        const TokenNeed* tok = body && rt ? rt->tokens(attrStr(n, ArgK::lang), body->str, overlays) : nullptr;
        if (tok && tok->st == ResState::Ready) {
          // its code tokens, folded here (the tree is never rewritten)
          std::vector<std::vector<TokenRun>> lines;
          tokenLines(strs.get(body->str), body->scope, nullptr, 0, tok->runs().data(), tok->runs().size(), strs,
                     styles, lines);
          for (size_t li = 0; li < lines.size(); li++) {
            if (li) out += "\n";
            for (const TokenRun& r : lines[li]) textRun(r.style, r.text);
            note(li);
          }
        } else if (body) {
          const std::string_view text = strs.get(body->str);
          size_t li = 0;
          for (size_t at = 0; at <= text.size(); li++) {
            size_t eol = text.find('\n', at);
            if (eol == std::string_view::npos) eol = text.size();
            if (li) out += "\n";
            esc(out, text.substr(at, eol - at));
            note(li);
            at = eol + 1;
          }
        } else {
          // structured lines: seq of styled runs per child (CH1)
          size_t li = 0;
          for (const ContentNode* k : n->kids) {
            if (k->kind == Kind::group) continue;
            if (li) out += "\n";
            inl(k);
            note(li++);
          }
        }
        out += "</code></pre>\n";
        return;
      }
      case Kind::rule:
        open(elementOf(n, sh, "hr"), n, pid);
        out += "\n";
        return;
      case Kind::mathblock:
        open("p", n, pid, "tsr-mathblock");
        if (math.boxes) {
          formulaBox(n, /*display=*/true);
        } else {
          out += "<code class=\"tsr-mathsrc\">$ ";
          esc(out, mathSource(n, strs).copy);
          out += " $</code>";
        }
        {
          // the equation number on the no-JS page too (P0-09 k): its tag
          // part (plan P3-03; the compat name is gone, P3-26), which copy
          // leaves out as on the typeset page
          for (const ContentNode* k : n->kids)
            if (slotOf(k, strs) == SlotId::Tag) {
              out += " <span class=\"tsr-eqno\" data-syn=\"eqno\">";
              inlineKids(k);
              out += "</span>";
            }
        }
        out += "</p>\n";
        return;
      case Kind::image: {
        std::string_view src = argS(n, ArgK::src);
        if (!safeImageSrc(src)) {
          out += "<div class=\"tsr-imgph\">";
          esc(out, argS(n, ArgK::alt));
          out += "</div>\n";
          return;
        }
        {
          Tag t(out, "img");
          attrs(t, n, pid);
          t.attr("src", src);
          t.attr("alt", argS(n, ArgK::alt));
          t.style("max-width:100%");
          t.open();
          out += "\n";
        }
        return;
      }
      case Kind::group: {
        // (plan P3-23) its presentation row: its element (a div by default),
        // its role as data-role where the row hooks it (D-R02), its ARIA
        // role, and its parts in their slots' elements (a figure's caption:
        // one figcaption, where its first part stands) — a part of one
        // paragraph holds its text, of several their paragraphs; nothing is
        // guessed from where a paragraph stands (finding
        // real-world-evidence/missed:5)
        if (sh && sh->projection == HtmlShape::Projection::Term) {
          term(n, sh, pid);
          return;
        }
        const std::string el = elementOf(n, sh, "div");
        {
          Tag t(out, el);
          std::string_view role = argS(n, ArgK::role);
          if (!role.empty() && (!sh || sh->dataRole)) t.attr("data-role", role);
          if (sh && !sh->aria.empty()) t.attr("role", sh->aria);
          attrs(t, n, pid);
          t.open();
          out += "\n";
        }
        std::vector<SlotId> written;
        for (const ContentNode* k : n->kids) {
          const SlotId slot = slotOf(k, strs);
          const std::string_view sel = sh && slot != SlotId::None ? sh->slotElement(slot) : std::string_view{};
          if (sel.empty()) {
            block(k, -1);
            continue;
          }
          if (std::find(written.begin(), written.end(), slot) != written.end()) continue;
          written.push_back(slot);
          std::vector<const ContentNode*> part;
          for (const ContentNode* x : n->kids)
            if (slotOf(x, strs) == slot) part.push_back(x);
          out += "<";
          out += sel;
          out += ">";
          if (part.size() == 1 && part[0]->kind == Kind::para) {
            inlineKids(part[0]);
          } else {
            out += "\n";
            for (const ContentNode* x : part) block(x, -1);
          }
          out += "</";
          out += sel;
          out += ">\n";
        }
        close(el);
        return;
      }
      case Kind::table: {
        open("table", n, pid);
        out += "\n";
        // its columns' alignment: the align letters (v1), else its tracks'
        std::string align(argS(n, ArgK::align));
        if (std::string_view tr = argS(n, ArgK::tracks); !tr.empty()) {
          align.clear();
          for (size_t at = 0; at <= tr.size();) {
            size_t comma = tr.find(',', at);
            if (comma == std::string_view::npos) comma = tr.size();
            const std::string_view e = tr.substr(at, comma - at);
            const size_t colon = e.find(':');
            align += colon != std::string_view::npos && colon + 1 < e.size() ? e[colon + 1] : 'l';
            at = comma + 1;
          }
        }
        // (plan P3-14) header rows are th cells; spans as written
        const int header = attrInt(n, ArgK::header, 0);
        int r = 0;
        std::vector<int> above;  // per column: the rows a cell above still spans
        for (const ContentNode* row : n->kids) {
          if (row->kind != Kind::trow) continue;
          out += "<tr>";
          size_t c = 0;
          for (const ContentNode* cell : row->kids) {
            if (cell->kind != Kind::tcell) continue;
            while (c < above.size() && above[c] > 0) c++;  // (its column: past the cells above spanning into it)
            const std::string_view own = argS(cell, ArgK::align);
            const char al = own.size() == 1 ? own[0] : c < align.size() ? align[c] : 'l';
            const int cs = attrInt(cell, ArgK::colspan, 1), rs = attrInt(cell, ArgK::rowspan, 1);
            const char* tag = r < header ? "th" : "td";
            {
              Tag t(out, tag);
              if (cs > 1) t.num("colspan", (unsigned long long)cs);
              if (rs > 1) t.num("rowspan", (unsigned long long)rs);
              if (al == 'c') t.style("text-align:center");
              else if (al == 'r') t.style("text-align:right");
              const int va = attrEnum(cell, ArgK::valign, strs);  // top | middle | bottom
              if (va == 2) t.decl("vertical-align", "middle");
              if (va == 3) t.decl("vertical-align", "bottom");
              t.open();
            }
            for (const ContentNode* k : cell->kids) inl(k);
            out += "</";
            out += tag;
            out += ">";
            const size_t span = (size_t)(cs > 1 ? cs : 1);
            if (above.size() < c + span) above.resize(c + span, 0);
            for (size_t k = c; k < c + span; k++) above[k] = rs > 1 ? rs : 1;
            c += span;
          }
          for (int& a : above) a = a > 0 ? a - 1 : 0;
          out += "</tr>\n";
          r++;
        }
        out += "</table>\n";
        return;
      }
      case Kind::raw:
        // trusted, handler-declared passthrough — the ONE unescaped path (§9);
        // a labelled one sits in a division that carries its anchor (plan
        // P3-04, S9b)
        if (!argS(n, ArgK::label).empty()) {
          open("div", n, pid);
          out += argS(n, ArgK::html);
          out += "</div>\n";
          return;
        }
        out += argS(n, ArgK::html);
        out += "\n";
        return;
      case Kind::error:
        {
          Tag t(out, "div");
          t.attrSafe("class", "tsr-err");
          attrs(t, n, pid);
          t.attr("title", argS(n, ArgK::message));
          if (!copyMarked(n)) t.attrSafe("data-syn", "error");  // copy omits it (D-R01)
          t.open();
        }
        out += "&#9888; ";
        esc(out, argS(n, ArgK::message));
        out += "</div>\n";
        return;
      case Kind::comment:
        return;
      case Kind::styled: {
        // a style.where around blocks (a nested $.set, plan P3-01): a
        // division where its env begins
        bool blocks = false;
        for (const ContentNode* k : n->kids) blocks = blocks || !isInlineLevel(k->kind);
        if (blocks && startsEnv(n) && cascade) {
          {
            Tag t(out, "div");
            attrs(t, n, pid);
            t.attr("data-tsr-env", envAttr(*cascade, n->env, strs));
            t.open();
          }
          out += "\n";
          for (const ContentNode* k : n->kids) block(k, -1);
          out += "</div>\n";
          return;
        }
        open("p", n, pid);
        inl(n);
        out += "</p>\n";
        return;
      }
      default:
        // inline content at block level (defensive): wrap in a paragraph
        open("p", n, pid);
        inl(n);
        out += "</p>\n";
        return;
    }
  }
};

}  // namespace

std::string renderSemantic(const ContentTree& tree, Interner& strs, StyleTable& styles,
                           const ResourceTable* rt, const Registry* reg, const Cascade* cascade,
                           const NodePropsTable* props, const SemanticMath& math) {
  std::string out;
  out += "<div class=\"tsr-flow\">\n";
  if (tree.root) {
    int pid = 0;
    for (const ContentNode* k : tree.root->kids) {
      Sem s{strs, styles, out, rt, reg, cascade, props, math,
            cascade && !startsEnv(k) ? envAttr(*cascade, k->env, strs) : std::string()};
      s.block(k, pid);  // pid mirrors emitDoc's per-root-child numbering
      pid++;
    }
  }
  out += "</div>\n";
  return out;
}

std::string renderSemanticFragment(const ContentTree& tree, Interner& strs, StyleTable& styles,
                                   const ResourceTable* rt, const Registry* reg, const Cascade* cascade,
                                   const NodePropsTable* props, std::string_view label,
                                   const std::function<bool(StrRef)>& backlink, const SemanticMath& math) {
  std::string out;
  if (!tree.root || label.empty()) return out;
  Sem s{strs, styles, out, rt, reg, cascade, props, math, std::string()};
  s.backlink = &backlink;
  // the labelled node, and the item it begins (document order, first wins)
  const ContentNode* hit = nullptr;
  const ContentNode* item = nullptr;
  auto find = [&](auto&& self, const ContentNode* n, const ContentNode* parent) -> bool {
    if (s.argS(n, ArgK::label) == label) {
      hit = n;
      if (parent && parent->kind == Kind::item && !parent->kids.empty() && parent->kids[0] == n) item = parent;
      return true;
    }
    for (const ContentNode* k : n->kids)
      if (self(self, k, n)) return true;
    return false;
  };
  find(find, tree.root, nullptr);
  if (!hit) return out;
  if (item)
    for (const ContentNode* b : item->kids) s.block(b, -1);
  else
    s.block(hit, -1);
  return out;
}

}  // namespace tsr
