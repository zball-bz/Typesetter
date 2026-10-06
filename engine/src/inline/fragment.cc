#include "../model/softbreak.h"
#include "fragment.h"

#include "../ast/ast.h"

namespace tsr {

namespace {

struct Conv {
  const SourceText& src;
  Arena& arena;
  Interner& strs;
  StyleTable& styles;
  DiagSink& diags;
  Span outer;  // span stamped on every produced node

  ContentNode* mk(Kind k, StyleId st) {
    ContentNode* n = arena.make<ContentNode>();
    n->kind = k;
    n->span = outer;
    n->style = st;
    return n;
  }
  void setStr(ContentNode* n, ArgK k, std::string_view v) {
    ArgVal a;
    a.key = k;
    a.tag = ArgTag::Str;
    a.ref = strs.intern(v);
    n->args.push_back(a);
  }

  // deltas accumulate down the styled path and fold into leaf styles — the
  // same emission-time semantics instantiate() gives ops-borne content
  void conv(const AstNode* a, const StyleDelta& bits, StyleId base,
            std::vector<ContentNode*>& out) {
    StyleId eff = compose(styles, base, bits);
    switch (a->kind) {
      case AstKind::Text: {
        ContentNode* t = mk(Kind::text, eff);
        t->str = a->str;
        std::string s(strs.get(a->str));
        std::vector<u32> none;
        if (resolveSoftBreaks(s, none, 0, false)) t->str = strs.intern(s);  // (plan P2-10)
        out.push_back(t);
        return;
      }
      case AstKind::Comment:
        return;
      case AstKind::Splice:
      case AstKind::Keyword:
      case AstKind::Branch: {
        // no executor in fragment context: splices (and keyword forms) stay literal
        diags.add(Sev::Info, "fragment-splice", outer,
                  "splices are not evaluated in inline fragments");
        ContentNode* t = mk(Kind::text, eff);
        t->str = strs.intern(src.slice(a->span));
        out.push_back(t);
        return;
      }
      case AstKind::Error: {
        ContentNode* e = mk(Kind::error, eff);
        setStr(e, ArgK::message, strs.get(side<ErrorP>(a).message));
        setStr(e, ArgK::code, strs.get(a->str));
        out.push_back(e);
        return;
      }
      case AstKind::Call:
        call(a, bits, base, eff, out);
        return;
      case AstKind::Doc:
      case AstKind::Stmt:
        // block structure cannot come out of an inline parse; flatten
        for (const AstNode* k : a->kids()) conv(k, bits, base, out);
        return;
    }
  }

  void call(const AstNode* a, const StyleDelta& bits, StyleId base, StyleId eff,
            std::vector<ContentNode*>& out) {
    switch (a->sugar) {
      case SugarId::strong:
      case SugarId::em: {
        StyleDelta add;
        if (a->sugar == SugarId::strong) add.weight = 700;
        else add.italic = true;
        for (const AstNode* k : a->kids()) conv(k, bits + add, base, out);
        return;
      }
      case SugarId::code: {
        ContentNode* c = mk(Kind::code, eff);
        ContentNode* t = mk(Kind::text, eff);
        t->str = a->str;
        c->kids.push_back(t);
        out.push_back(c);
        return;
      }
      case SugarId::math: {
        ContentNode* m = mk(Kind::mathinline, eff);
        setStr(m, ArgK::src, strs.get(a->str));
        out.push_back(m);
        return;
      }
      case SugarId::ref: {
        ContentNode* r = mk(Kind::ref, eff);
        setStr(r, ArgK::target, strs.get(a->str));
        out.push_back(r);
        return;
      }
      case SugarId::link: {
        ContentNode* l = mk(Kind::link, eff);
        setStr(l, ArgK::url, strs.get(side<LinkP>(a).url));
        for (const AstNode* k : a->kids()) conv(k, bits, base, l->kids);
        out.push_back(l);
        return;
      }
      case SugarId::note:
        // notes need the resolver's flow: flattened in fragments until the
        // single fragment lowering (plan P2-13)
        diags.add(Sev::Info, "fragment-note", outer,
                  "notes in inline fragments are flattened");
        for (const AstNode* k : a->kids()) conv(k, bits, base, out);
        return;
      case SugarId::para:
      case SugarId::heading:
      case SugarId::list:
      case SugarId::item:
      case SugarId::quote:
      case SugarId::rule:
      case SugarId::fence:
      case SugarId::region:
      case SugarId::arg:
        // block structure cannot come out of an inline parse; flatten
        for (const AstNode* k : a->kids()) conv(k, bits, base, out);
        return;
    }
  }
};

}  // namespace

std::vector<ContentNode*> parseInlineFragment(std::string_view text,
                                              StyleId baseStyle, Span span,
                                              Arena& arena, Interner& strs,
                                              StyleTable& styles,
                                              DiagSink& diags) {
  SourceText frag;
  frag.init(std::string(text));
  std::vector<Span> spans{{0, (u32)text.size()}};
  std::vector<AstNode*> ast = parseInlineSpans(frag, spans, arena, strs, diags);
  Conv c{frag, arena, strs, styles, diags, span};
  std::vector<ContentNode*> out;
  for (const AstNode* a : ast) c.conv(a, StyleDelta{}, baseStyle, out);
  return out;
}

}  // namespace tsr
