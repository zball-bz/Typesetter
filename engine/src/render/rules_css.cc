#include "rules_css.h"

#include <algorithm>

namespace tsr {

namespace {

// a value that may stand in a declaration as written: no character that
// could end it, its rule or the <style> element
bool cssSafe(std::string_view v) {
  if (v.empty()) return false;
  for (char c : v)
    if (c == '{' || c == '}' || c == ';' || c == '<' || c == '>' || c == '\\' || c == '\n' || c == '\r' ||
        c == '@' || c == '!')
      return false;
  return true;
}
bool tokenSafe(std::string_view v) {  // a language tag, a level
  if (v.empty()) return false;
  for (char c : v)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
  return true;
}

struct Writer {
  const Interner& strs;
  DiagSink* diags;

  void noCss(const char* what) const {
    if (diags) diags->add(Sev::Warning, "rule-no-css", {}, std::string("a rule's ") + what + " has no CSS form: the semantic page leaves the rule out");
  }

  // the selector as compound selectors over the elements the page writes;
  // false: none (a role or class waits for its hook; others are diagnosed)
  bool selector(const StyleSelector& s, std::vector<std::string>& out) const {
    out.clear();
    if (s.role || s.cls) return false;  // P3-18 / P3-23 hooks
    if (s.depth) {
      noCss("depth selector");
      return false;
    }
    std::string level;
    int ordered = 0;  // 1 ordered, 2 not
    for (const auto& [k, v] : s.where) {
      const std::string_view t = strs.get(v);
      if (k == ArgK::level && s.kind == (u16)Kind::heading && tokenSafe(t)) level = t;
      else if (k == ArgK::ordered && s.kind == (u16)Kind::list) ordered = !t.empty() && t[0] == 't' ? 1 : 2;  // "true" | "false"
      else {
        noCss("attribute selector");
        return false;
      }
    }
    switch (s.kind) {
      case 0xFFFF: out = {"*"}; break;
      case (u16)Kind::para: out = {"p:not(.tsr-mathblock)"}; break;
      case (u16)Kind::heading:
        if (!level.empty()) out = {"h" + level};
        else out = {"h1", "h2", "h3", "h4", "h5", "h6"};
        break;
      case (u16)Kind::list:
        if (ordered == 1) out = {"ol"};
        else if (ordered == 2) out = {"ul"};
        else out = {"ul", "ol"};
        break;
      case (u16)Kind::item: out = {"li"}; break;
      case (u16)Kind::quote: out = {"blockquote"}; break;
      case (u16)Kind::codeblock: out = {"pre"}; break;
      case (u16)Kind::code: out = {":not(pre) > code"}; break;
      case (u16)Kind::rule: out = {"hr"}; break;
      case (u16)Kind::table: out = {"table"}; break;
      case (u16)Kind::trow: out = {"tr"}; break;
      case (u16)Kind::tcell: out = {"td"}; break;
      case (u16)Kind::image: out = {"img"}; break;
      case (u16)Kind::link:
      case (u16)Kind::ref: out = {"a"}; break;
      case (u16)Kind::mathblock: out = {"p.tsr-mathblock"}; break;
      case (u16)Kind::mathinline: out = {"code.tsr-mathsrc"}; break;
      case (u16)Kind::error: out = {".tsr-err"}; break;
      default:
        noCss("kind (no element of its own)");
        return false;
    }
    if (s.lang) {
      const std::string_view l = strs.get(s.lang);
      if (!tokenSafe(l)) return false;
      for (std::string& c : out) c += ":lang(" + std::string(l) + ")";
    }
    return true;
  }

  // the patch as declarations ("" when none has a CSS form): its values
  // read through the same appliers the fold uses
  std::string decls(const StyleRule& r) const {
    Styling st;
    NodeProps np;
    auto intern = [](u32 x) { return x; };  // rule strings are document StrRefs
    auto view = [&](u32 x) { return strs.get(x); };
    for (const ArgVal& a : r.patch) {
      if ((u16)a.key >= ARGK_COUNT) continue;
      if (isNodeArg(a.key)) applyNodeArg(np, a, intern, view);
      else applyStyleArg(st, a, intern, view);
    }
    std::string d;
    const char* imp = r.force ? " !important" : "";
    auto decl = [&](const char* prop, std::string_view v) {
      if (!cssSafe(v)) return;
      d += prop;
      d += ':';
      d += v;
      d += imp;
      d += ';';
    };
    auto num = [](const char* fmt, double x) {
      std::string t;
      appendf(t, fmt, x);
      return t;
    };
    auto len = [&](const Len& l) {
      return l.v == 0 ? std::string("0") : num(l.unit == 2 ? "%gpx" : "%gem", l.v);
    };
    bool sized = false;
    for (const ArgVal& a : r.patch) {
      switch (a.key) {
        case ArgK::weight:
          if (st.weight) decl("font-weight", num("%g", st.weight));
          break;
        case ArgK::italic: decl("font-style", st.italic ? "italic" : "normal"); break;
        case ArgK::decoration: {
          std::string t;
          if (st.decoration & DECORATION_UNDER) t += "underline ";
          if (st.decoration & DECORATION_OVER) t += "overline ";
          if (st.decoration & DECORATION_STRIKE) t += "line-through ";
          if (!t.empty()) {
            t.pop_back();
            decl("text-decoration-line", t);
          }
          break;
        }
        case ArgK::fontRole:
          if (st.fontRole == FONTROLE_MONO) decl("font-family", "var(--tsr-font-mono, monospace)");
          else if (st.fontRole == FONTROLE_BODY) decl("font-family", "var(--tsr-font-body, serif)");
          break;
        case ArgK::baseline:
          if (st.baseline) decl("vertical-align", st.baseline == BASELINE_SUPER ? "super" : "sub");
          break;
        case ArgK::size:
        case ArgK::sizePx:
          if (!sized) {  // one slot: the fold's last value
            sized = true;
            if (st.sizePx > 0) decl("font-size", num("%gpx", st.sizePx));
            else if (st.sizeMul != 1.0f) decl("font-size", num("%gem", st.sizeMul));
          }
          break;
        case ArgK::font:
          if (st.fontFamily) decl("font-family", strs.get(st.fontFamily));
          break;
        case ArgK::color:
          if (st.color) decl("color", strs.get(st.color));
          break;
        case ArgK::parIndent: decl("text-indent", len(np.parIndent)); break;
        case ArgK::parAlign:
          decl("text-align", np.parAlign == PARALIGN_START    ? "start"
                             : np.parAlign == PARALIGN_CENTER ? "center"
                             : np.parAlign == PARALIGN_END    ? "end"
                                                              : "justify");
          break;
        case ArgK::parHyphenate: decl("hyphens", np.parHyphenate == PARHYPHENATE_FALSE ? "manual" : "auto"); break;
        case ArgK::parSingleLine:  // (plan P3-09) a shrink-to-fit box, centred: one line centres, more fill the measure
          if (np.parSingleLine == PARSINGLELINE_CENTER) {
            decl("display", "table");
            decl("margin-inline", "auto");
          }
          break;
        case ArgK::blockIndent: decl("padding-inline-start", len(np.blockIndent)); break;
        case ArgK::keepWithNext: decl("break-after", np.keepWithNext ? "avoid" : "auto"); break;
        case ArgK::listMarker:
          if (np.listMarker) decl("list-style-type", strs.get(np.listMarker));
          break;
        // (plan P3-14) the block trait group's CSS forms
        case ArgK::keep:
          if (np.keep == KEEP_TOGETHER || np.keep == KEEP_BOTH) decl("break-inside", "avoid");
          if (np.keep == KEEP_WITH_NEXT || np.keep == KEEP_BOTH) decl("break-after", "avoid");
          break;
        case ArgK::spaceBefore: decl("margin-block-start", len(np.spaceBefore)); break;
        case ArgK::spaceAfter: decl("margin-block-end", len(np.spaceAfter)); break;
        case ArgK::breakBefore: decl("break-before", np.breakBefore == BREAKBEFORE_PAGE ? "page" : "auto"); break;
        case ArgK::breakAfter: decl("break-after", np.breakAfter == BREAKAFTER_PAGE ? "page" : "auto"); break;
        case ArgK::parHang:  // (a first line hanging out: the CSS form of hangAfter 1)
          decl("padding-inline-start", len(np.parHang));
          decl("text-indent", np.parHang.v == 0 ? std::string("0") : "-" + len(np.parHang));
          break;
        case ArgK::boxPadding:
          if (np.boxPadding) decl("padding", strs.get(np.boxPadding));
          break;
        case ArgK::boxBorder:
          if (np.boxBorder) {
            decl("border-style", "solid");
            decl("border-width", strs.get(np.boxBorder));
          }
          break;
        case ArgK::boxBorderColor:
          if (np.boxBorderColor) decl("border-color", strs.get(np.boxBorderColor));
          break;
        case ArgK::boxBackground:
          if (np.boxBackground) decl("background", strs.get(np.boxBackground));
          break;
        case ArgK::beside:
          if (np.beside == BESIDE_SHRINK) decl("display", "flow-root");
          break;
        case ArgK::placeFloat:
          if (np.placeFloat == PLACEFLOAT_LEFT || np.placeFloat == PLACEFLOAT_RIGHT)
            decl("float", np.placeFloat == PLACEFLOAT_LEFT ? "inline-start" : "inline-end");
          if (np.placeFloat == PLACEFLOAT_INLINE) {  // (plan P3-15) side by side
            decl("display", "inline-block");
            decl("vertical-align", "top");
          }
          break;
        case ArgK::placeWidth:
          if (np.placeWidth) decl("width", strs.get(np.placeWidth));
          break;
        default: break;  // no CSS form on this page (gap, lang, media, breaker)
      }
    }
    if (!d.empty()) d.pop_back();
    return d;
  }

  void rule(std::string& out, const StyleRule& r, const std::string& env) const {
    std::vector<std::string> sels;
    if (!selector(r.sel, sels)) return;
    const std::string d = decls(r);
    if (d.empty()) return;
    out += ":where(";
    bool first = true;
    for (const std::string& c : sels) {
      auto one = [&](const std::string& x) {
        if (!first) out += ',';
        first = false;
        out += x;
      };
      if (env.empty()) {
        one(c);
      } else {  // the element where the env begins, and its descendants
        const std::string attr = "[data-tsr-env=\"" + env + "\"]";
        one(c + attr);
        one(attr + " " + c);
      }
    }
    out += "){";
    out += d;
    out += "}\n";
  }
};

u64 envHash(const Cascade& cascade, RuleEnvId env, const Interner& strs) {
  u64 h = 1469598103934665603ull;
  auto mix = [&](const void* p, size_t n) {
    const u8* b = (const u8*)p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
  };
  auto mixStr = [&](StrRef r) {
    const std::string_view v = strs.get(r);
    const u32 n = (u32)v.size();
    mix(&n, 4);
    mix(v.data(), v.size());
  };
  std::vector<const StyleRule*> rules;
  cascade.chain(env, rules);
  for (const StyleRule* r : rules) {
    mix(&r->sel.kind, 2);
    mixStr(r->sel.role);
    mixStr(r->sel.cls);
    mixStr(r->sel.lang);
    mix(&r->sel.depth, 1);
    for (const auto& [k, v] : r->sel.where) {
      mix(&k, sizeof k);
      mixStr(v);
    }
    for (const ArgVal& a : r->patch) {
      mix(&a.key, sizeof a.key);
      mix(&a.tag, sizeof a.tag);
      if (a.tag == ArgTag::Str) mixStr(a.ref);
      else mix(&a.num, sizeof a.num);
    }
    const u8 f = r->force;
    mix(&f, 1);
    const u8 end = 0xFF;  // a rule boundary
    mix(&end, 1);
  }
  return h;
}

}  // namespace

bool startsEnv(const ContentNode* n) {
  if (n->kind != Kind::styled) return false;
  for (const ArgVal& a : n->args)
    if (a.key == ArgK::matchKind || a.key == ArgK::matchRole || a.key == ArgK::matchClass ||
        a.key == ArgK::matchLang || a.key == ArgK::matchDepth || a.key == ArgK::matchWhere)
      return true;
  return false;
}

std::string envAttr(const Cascade& cascade, RuleEnvId env, const Interner& strs) {
  if (!env) return {};
  std::string out;
  appendf(out, "%016llx", (unsigned long long)envHash(cascade, env, strs));
  return out;
}

std::string rulesToCss(const Cascade& cascade, const ContentTree& tree, const Interner& strs, DiagSink* diags) {
  Writer w{strs, diags};
  std::string out;
  for (const StyleRule& r : cascade.baseRules()) w.rule(out, r, {});
  // the document envs the page marks, in the order they first appear
  std::vector<RuleEnvId> envs;
  auto note = [&](RuleEnvId e) {
    if (e && std::find(envs.begin(), envs.end(), e) == envs.end()) envs.push_back(e);
  };
  if (tree.root) {
    std::vector<const ContentNode*> work;
    for (size_t k = tree.root->kids.size(); k-- > 0;) {
      const ContentNode* top = tree.root->kids[k];
      work.push_back(top);
    }
    for (const ContentNode* top : tree.root->kids)
      if (!startsEnv(top)) note(top->env);
    while (!work.empty()) {
      const ContentNode* n = work.back();
      work.pop_back();
      if (startsEnv(n)) note(n->env);
      for (size_t k = n->kids.size(); k-- > 0;) work.push_back(n->kids[k]);
    }
  }
  std::vector<const StyleRule*> rules;
  for (RuleEnvId e : envs) {
    const std::string attr = envAttr(cascade, e, strs);
    cascade.chain(e, rules);
    for (const StyleRule* r : rules) w.rule(out, *r, attr);
  }
  return out;
}

}  // namespace tsr
