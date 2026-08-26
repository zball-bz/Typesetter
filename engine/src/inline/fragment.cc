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

  // bits accumulate down the styled path and fold into leaf styles — the
  // same emission-time semantics instantiate() gives ops-borne content
  void conv(const AstNode* a, u64 bits, StyleId base,
            std::vector<ContentNode*>& out) {
    Styling st = styles.get(base);
    st.bits |= bits;
    StyleId eff = styles.idOf(st);
    switch (a->kind) {
      case AstKind::Text: {
        ContentNode* t = mk(Kind::text, eff);
        t->str = a->str;
        out.push_back(t);
        return;
      }
      case AstKind::Styled: {
        u64 add = a->tag == (u8)'*' ? CLS_BOLD : CLS_EM;
        for (const AstNode* k : a->kids) conv(k, bits | add, base, out);
        return;
      }
      case AstKind::Code: {
        ContentNode* c = mk(Kind::code, eff);
        ContentNode* t = mk(Kind::text, eff);
        t->str = a->str;
        c->kids.push_back(t);
        out.push_back(c);
        return;
      }
      case AstKind::Math: {
        ContentNode* m = mk(Kind::mathinline, eff);
        setStr(m, ArgK::src, strs.get(a->str));
        out.push_back(m);
        return;
      }
      case AstKind::Ref: {
        ContentNode* r = mk(Kind::ref, eff);
        setStr(r, ArgK::target, strs.get(a->str));
        out.push_back(r);
        return;
      }
      case AstKind::Link: {
        ContentNode* l = mk(Kind::link, eff);
        setStr(l, ArgK::url, strs.get(a->aux));
        for (const AstNode* k : a->kids) conv(k, bits, base, l->kids);
        out.push_back(l);
        return;
      }
      case AstKind::Comment:
        return;
      case AstKind::Splice: {
        // no executor in fragment context: splices stay literal
        diags.add(Sev::Info, "fragment-splice", outer,
                  "splices are not evaluated in inline fragments");
        ContentNode* t = mk(Kind::text, eff);
        t->str = strs.intern(src.slice(a->span));
        out.push_back(t);
        return;
      }
      default:
        for (const AstNode* k : a->kids) conv(k, bits, base, out);
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
  for (const AstNode* a : ast) c.conv(a, 0, baseStyle, out);
  return out;
}

}  // namespace tsr
