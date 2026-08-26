#include "semantic_html.h"

namespace tsr {

namespace {

void esc(std::string& out, std::string_view s) {
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
}

struct Sem {
  const Interner& strs;
  std::string& out;

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
  void attrs(const ContentNode* n, int pid) {
    if (pid >= 0) appendf(out, " data-pid=\"%d\"", pid);
    if (!n->span.empty())
      appendf(out, " data-s=\"%u\" data-e=\"%u\"", n->span.start, n->span.end);
    std::string_view label = argS(n, ArgK::label);
    if (!label.empty()) {
      out += " id=\"tsr-";
      esc(out, label);
      out += "\"";
    }
  }

  void inlineKids(const ContentNode* n) {
    for (const ContentNode* k : n->kids) inl(k);
  }

  void inl(const ContentNode* n) {
    switch (n->kind) {
      case Kind::text:
        esc(out, strs.get(n->str));
        return;
      case Kind::styled: {
        u64 bits = (u64)argN(n, ArgK::bits, 0);
        const char* tag = (bits & CLS_BOLD) ? "strong" : (bits & CLS_EM) ? "em" : "span";
        out += "<";
        out += tag;
        std::string_view lang = argS(n, ArgK::lang);
        if (!lang.empty()) {
          out += " lang=\"";
          esc(out, lang);
          out += "\"";
        }
        std::string style;
        std::string_view font = argS(n, ArgK::font);
        if (!font.empty()) {
          style += "font-family:";
          style += font;
          style += ";";
        }
        std::string_view color = argS(n, ArgK::color);
        if (!color.empty()) {
          style += "color:";
          style += color;
          style += ";";
        }
        double sizePx = argN(n, ArgK::sizePx, 0);
        if (sizePx > 0) {
          char buf[32];
          std::snprintf(buf, sizeof buf, "font-size:%gpx;", sizePx);
          style += buf;
        }
        if (bits & (CLS_UNDER | CLS_OVER | CLS_STRIKE)) {
          style += "text-decoration:";
          if (bits & CLS_UNDER) style += "underline ";
          if (bits & CLS_OVER) style += "overline ";
          if (bits & CLS_STRIKE) style += "line-through ";
          style.pop_back();
          style += ";";
        }
        if (!style.empty()) {
          style.pop_back();
          out += " style=\"";
          esc(out, style);
          out += "\"";
        }
        out += ">";
        inlineKids(n);
        out += "</";
        out += tag;
        out += ">";
        return;
      }
      case Kind::link:
      case Kind::ref: {
        out += "<a href=\"";
        esc(out, argS(n, ArgK::url));
        out += "\">";
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
        out += "<code class=\"tsr-mathsrc\">$";
        esc(out, argS(n, ArgK::src));
        out += "$</code>";
        return;
      case Kind::error:
        out += "<span class=\"tsr-err\" title=\"";
        esc(out, argS(n, ArgK::message));
        out += "\">&#9888; ";
        esc(out, argS(n, ArgK::message));
        out += "</span>";
        return;
      case Kind::comment:
        return;  // document-model nodes, excluded from output
      default:
        inlineKids(n);
        return;
    }
  }

  void block(const ContentNode* n, int pid) {
    switch (n->kind) {
      case Kind::para:
        out += "<p";
        attrs(n, pid);
        out += ">";
        inlineKids(n);
        out += "</p>\n";
        return;
      case Kind::heading: {
        int level = (int)argN(n, ArgK::level, 1);
        if (level < 1) level = 1;
        if (level > 6) level = 6;
        appendf(out, "<h%d", level);
        attrs(n, pid);
        out += ">";
        inlineKids(n);
        appendf(out, "</h%d>\n", level);
        return;
      }
      case Kind::list: {
        const ArgVal* ord = arg(n, ArgK::ordered);
        bool ordered = ord && ord->num != 0;
        int start = (int)argN(n, ArgK::start, 1);
        if (ordered && start != 1) appendf(out, "<ol start=\"%d\"", start);
        else out += ordered ? "<ol" : "<ul";
        attrs(n, pid);
        out += ">\n";
        for (const ContentNode* k : n->kids) {
          out += "<li>";
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
        out += "<blockquote";
        attrs(n, pid);
        out += ">\n";
        for (const ContentNode* k : n->kids) block(k, -1);
        out += "</blockquote>\n";
        return;
      case Kind::codeblock: {
        out += "<pre";
        attrs(n, pid);
        out += "><code";
        std::string_view lang = argS(n, ArgK::lang);
        if (!lang.empty()) {
          out += " class=\"language-";
          esc(out, lang);
          out += "\"";
        }
        out += ">";
        if (n->kids.size() == 1 && n->kids[0]->kind == Kind::text) {
          esc(out, strs.get(n->kids[0]->str));
        } else {
          // structured lines: seq of styled runs per child (CH1); the
          // trailing sidecar group is display-layer only (verbatim §5)
          bool firstLine = true;
          for (size_t li = 0; li < n->kids.size(); li++) {
            if (n->kids[li]->kind == Kind::group) continue;
            if (!firstLine) out += "\n";
            firstLine = false;
            inl(n->kids[li]);
          }
        }
        out += "</code></pre>\n";
        return;
      }
      case Kind::rule:
        out += "<hr";
        attrs(n, pid);
        out += ">\n";
        return;
      case Kind::mathblock:
        out += "<p class=\"tsr-mathblock\"";
        attrs(n, pid);
        out += "><code class=\"tsr-mathsrc\">$ ";
        esc(out, argS(n, ArgK::src));
        out += " $</code></p>\n";
        return;
      case Kind::group: {
        out += "<div";
        std::string_view role = argS(n, ArgK::role);
        if (!role.empty()) {
          out += " data-role=\"";
          esc(out, role);
          out += "\"";
        }
        attrs(n, pid);
        out += ">\n";
        for (const ContentNode* k : n->kids) block(k, -1);
        out += "</div>\n";
        return;
      }
      case Kind::table: {
        out += "<table";
        attrs(n, pid);
        out += ">\n";
        std::string_view align = argS(n, ArgK::align);
        for (const ContentNode* row : n->kids) {
          if (row->kind != Kind::trow) continue;
          out += "<tr>";
          size_t c = 0;
          for (const ContentNode* cell : row->kids) {
            if (cell->kind != Kind::tcell) continue;
            char al = c < align.size() ? align[c] : 'l';
            if (al == 'c') out += "<td style=\"text-align:center\">";
            else if (al == 'r') out += "<td style=\"text-align:right\">";
            else out += "<td>";
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
        out += "<div class=\"tsr-err\"";
        attrs(n, pid);
        out += " title=\"";
        esc(out, argS(n, ArgK::message));
        out += "\">&#9888; ";
        esc(out, argS(n, ArgK::message));
        out += "</div>\n";
        return;
      case Kind::comment:
        return;
      default:
        // inline content at block level (defensive): wrap in a paragraph
        out += "<p";
        attrs(n, pid);
        out += ">";
        inl(n);
        out += "</p>\n";
        return;
    }
  }
};

}  // namespace

std::string renderSemantic(const ContentTree& tree, const Interner& strs) {
  std::string out;
  out += "<div class=\"tsr-flow\">\n";
  if (tree.root) {
    int pid = 0;
    for (const ContentNode* k : tree.root->kids) {
      Sem s{strs, out};
      s.block(k, pid);  // pid mirrors emitDoc's per-root-child numbering
      pid++;
    }
  }
  out += "</div>\n";
  return out;
}

}  // namespace tsr
