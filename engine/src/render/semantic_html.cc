#include "semantic_html.h"

#include "html_writer.h"
#include "../resource/resource_table.h"
#include "../elements/registry.h"
#include "../math/env.h"

namespace tsr {

namespace {

constexpr auto esc = escapeHtml;

struct Sem {
  Interner& strs;
  StyleTable& styles;
  std::string& out;
  const ResourceTable* rt;  // answered code tokens (plan P1-19)
  const Registry* reg;     // the classes' semantic elements (plan P2-05)

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

  // shared attributes: span anchoring + optional pid + label anchor
  void attrs(Tag& t, const ContentNode* n, int pid) {
    if (pid >= 0) t.num("data-pid", (unsigned)pid);
    if (!n->span.empty()) {
      t.num("data-s", n->span.start);
      t.num("data-e", n->span.end);
    }
    std::string_view label = argS(n, ArgK::label);
    if (!label.empty()) t.id(label);
  }
  // <name …shared attributes…>
  void open(std::string_view name, const ContentNode* n, int pid, const char* cls = nullptr) {
    Tag t(out, name);
    if (cls) t.attrSafe("class", cls);
    attrs(t, n, pid);
    t.open();
  }

  // a text leaf in its effective style
  void textRun(StyleId sid, std::string_view text) {
    // leaf styles are the effective styles (instantiation folds styled
    // deltas onto leaves — document-model §3); render from them so
    // token colors, resolver-fabricated bold, and patch styles all
    // reach the no-JS page. Kind::styled is transparent below.
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
    if (st.sizePx > 0) {
      style += "font-size:";
      fmtPx(style, st.sizePx);
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
    const bool wrap = tag || !style.empty() || st.lang;
    if (wrap) {
      Tag t(out, tag ? tag : "span");
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

  void inl(const ContentNode* n) {
    switch (n->kind) {
      case Kind::text:
        textRun(n->style, strs.get(n->str));
        return;
      case Kind::styled:
        // transparent: the leaves carry the folded styles (above)
        inlineKids(n);
        return;
      case Kind::link:
      case Kind::ref: {
        std::string_view url = argS(n, ArgK::url);
        if (url.empty()) {  // unresolved ref / grouped citation container
          inlineKids(n);
          return;
        }
        {
          Tag t(out, "a");
          t.attr("href", url);
          std::string_view id = argS(n, ArgK::label);  // inline anchor (marker)
          if (!id.empty()) t.id(id);
          t.open();
        }
        inlineKids(n);
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
        // §9.2: source-text fallback until the semantic phase learns boxes
        // (its source as written: fragments and holes, plan P2-15)
        out += "<code class=\"tsr-mathsrc\">$";
        esc(out, mathSource(n, strs).copy);
        out += "$</code>";
        return;
      case Kind::error:
        {
          Tag t(out, "span");
          t.attrSafe("class", "tsr-err");
          t.attr("title", argS(n, ArgK::message));
          t.open();
        }
        out += "&#9888; ";
        esc(out, argS(n, ArgK::message));
        out += "</span>";
        return;
      case Kind::comment:
        return;  // document-model nodes, excluded from output
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

  void block(const ContentNode* n, int pid) {
    switch (n->kind) {
      case Kind::para:
        open("p", n, pid);
        inlineKids(n);
        out += "</p>\n";
        return;
      case Kind::heading: {
        int level = attrInt(n, ArgK::level, 1);
        if (level < 1) level = 1;
        if (level > 6) level = 6;
        const char hn[3] = {'h', (char)('0' + level), 0};
        open(hn, n, pid);
        inlineKids(n);
        appendf(out, "</h%d>\n", level);
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
      case Kind::quote:
        open("blockquote", n, pid);
        out += "\n";
        for (const ContentNode* k : n->kids) block(k, -1);
        out += "</blockquote>\n";
        return;
      case Kind::codeblock: {
        open("pre", n, pid);
        {
          Tag t(out, "code");
          std::string_view lang = argS(n, ArgK::lang);
          if (!lang.empty()) t.attr("class", "language-" + std::string(lang));
          t.open();
        }
        const ContentNode* body = !n->kids.empty() && n->kids[0]->kind == Kind::text ? n->kids[0] : nullptr;
        const TokenNeed* tok = body && rt ? rt->tokens(attrStr(n, ArgK::lang), body->str) : nullptr;
        if (tok && tok->st == ResState::Ready) {
          // its code tokens, folded here (the tree is never rewritten)
          std::vector<std::vector<TokenRun>> lines;
          tokenLines(strs.get(body->str), body->style, tok->toks.data(), tok->toks.size(), strs, styles, lines);
          for (size_t li = 0; li < lines.size(); li++) {
            if (li) out += "\n";
            for (const TokenRun& r : lines[li]) textRun(r.style, r.text);
          }
        } else if (n->kids.size() == 1 && body) {
          esc(out, strs.get(body->str));
        } else {
          // structured lines: seq of styled runs per child (CH1); the
          // margin slot follows the code (below)
          bool firstLine = true;
          for (size_t li = 0; li < n->kids.size(); li++) {
            if (n->kids[li]->kind == Kind::group) continue;
            if (!firstLine) out += "\n";
            firstLine = false;
            inl(n->kids[li]);
          }
        }
        out += "</code></pre>\n";
        // its margin slot (plan P2-13: sidecar notes are content — a
        // footnote marker, a reference): an aside after the code, one
        // paragraph per annotated line (data-line counts from 1)
        const StrRef margin = strs.find("margin");
        for (const ContentNode* k : n->kids) {
          if (k->kind != Kind::group || !margin || attrStr(k, ArgK::slot) != margin) continue;
          bool any = false;
          for (size_t li = 0; li < k->kids.size(); li++) {
            if (k->kids[li]->kids.empty()) continue;
            if (!any) out += "<aside class=\"tsr-margin\">\n";
            any = true;
            appendf(out, "<p data-line=\"%zu\">", li + 1);
            inl(k->kids[li]);
            out += "</p>\n";
          }
          if (any) out += "</aside>\n";
        }
        return;
      }
      case Kind::rule:
        open("hr", n, pid);
        out += "\n";
        return;
      case Kind::mathblock:
        open("p", n, pid, "tsr-mathblock");
        out += "<code class=\"tsr-mathsrc\">$ ";
        esc(out, mathSource(n, strs).copy);
        out += " $</code>";
        if (std::string_view tag = argS(n, ArgK::name); !tag.empty()) {
          // the equation number on the no-JS page too (P0-09 k)
          out += " <span class=\"tsr-eqno\">";
          esc(out, tag);
          out += "</span>";
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
        if (n->cls && reg && reg->cls(n->cls).html == ElementClass::Html::Figure) {
          // real HTML for the no-JS page (figure-design.md §5)
          open("figure", n, pid);
          out += "\n";
          bool capOpen = false;
          for (const ContentNode* k : n->kids) {
            if (k->kind == Kind::para) {
              if (!capOpen) {
                out += "<figcaption>";
                capOpen = true;
              }
              inlineKids(k);
            } else {
              block(k, -1);
            }
          }
          if (capOpen) out += "</figcaption>\n";
          out += "</figure>\n";
          return;
        }
        {
          Tag t(out, "div");
          std::string_view role = argS(n, ArgK::role);  // the role is data for the page
          if (!role.empty()) t.attr("data-role", role);
          attrs(t, n, pid);
          t.open();
          out += "\n";
        }
        for (const ContentNode* k : n->kids) block(k, -1);
        out += "</div>\n";
        return;
      }
      case Kind::table: {
        open("table", n, pid);
        out += "\n";
        std::string_view align = argS(n, ArgK::align);
        for (const ContentNode* row : n->kids) {
          if (row->kind != Kind::trow) continue;
          out += "<tr>";
          size_t c = 0;
          for (const ContentNode* cell : row->kids) {
            if (cell->kind != Kind::tcell) continue;
            char al = c < align.size() ? align[c] : 'l';
            {
              Tag t(out, "td");
              if (al == 'c') t.style("text-align:center");
              else if (al == 'r') t.style("text-align:right");
              t.open();
            }
            for (const ContentNode* k : cell->kids) inl(k);
            out += "</td>";
            c++;
          }
          out += "</tr>\n";
        }
        out += "</table>\n";
        return;
      }
      case Kind::raw:
        // trusted, handler-declared passthrough — the ONE unescaped path (§9)
        out += argS(n, ArgK::html);
        out += "\n";
        return;
      case Kind::error:
        {
          Tag t(out, "div");
          t.attrSafe("class", "tsr-err");
          attrs(t, n, pid);
          t.attr("title", argS(n, ArgK::message));
          t.open();
        }
        out += "&#9888; ";
        esc(out, argS(n, ArgK::message));
        out += "</div>\n";
        return;
      case Kind::comment:
        return;
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
                           const ResourceTable* rt, const Registry* reg) {
  std::string out;
  out += "<div class=\"tsr-flow\">\n";
  if (tree.root) {
    int pid = 0;
    for (const ContentNode* k : tree.root->kids) {
      Sem s{strs, styles, out, rt, reg};
      s.block(k, pid);  // pid mirrors emitDoc's per-root-child numbering
      pid++;
    }
  }
  out += "</div>\n";
  return out;
}

}  // namespace tsr
