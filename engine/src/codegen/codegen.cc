#include "codegen.h"

namespace tsr {

namespace {
struct Gen {
  const SourceText& src;
  const Interner& strs;
  std::string& out;

  void strLit(StrRef r) {
    out += "\"";
    appendEscaped(out, strs.get(r));
    out += "\"";
  }
  void close(const AstNode* n) {
    appendf(out, "),%u,%u)", n->span.start, n->span.end);
  }
  void children(const std::vector<AstNode*>& kids, bool leadingComma) {
    for (size_t i = 0; i < kids.size(); i++) {
      if (leadingComma || i) out += ", ";
      value(kids[i]);
    }
  }

  void value(const AstNode* n) {
    switch (n->kind) {
      case AstKind::Text:
        out += "__at(text(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Styled:
        appendf(out, "__at(%s(", n->tag == (u8)'*' ? "strong" : "em");
        children(n->kids, false);
        close(n);
        break;
      case AstKind::Code:
        out += "__at(code(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Comment:
        out += "__at(comment(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Link:
        out += "__at(link(";
        strLit(n->aux);
        children(n->kids, true);
        close(n);
        break;
      case AstKind::SpliceArg:
        if (n->kids.size() == 1) value(n->kids[0]);
        else {
          out += "seq(";
          children(n->kids, false);
          out += ")";
        }
        break;
      case AstKind::Splice: {
        out += "val(";
        if (n->kids.empty()) {
          out += "(";
          out += src.slice(n->expr);
          out += ")";
        } else if (n->lastCallStart > 0) {
          // trailing content args desugar into the final call: f(a)[c] → f(a, c)
          out += src.view().substr(n->expr.start, n->lastCallStart - n->expr.start);
          out += "(";
          std::string_view inner =
              src.view().substr(n->lastCallStart + 1, n->expr.end - 1 - (n->lastCallStart + 1));
          bool innerEmpty = true;
          for (char c : inner)
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { innerEmpty = false; break; }
          out += inner;
          children(n->kids, !innerEmpty);
          out += ")";
        } else {
          out += "(";
          out += src.slice(n->expr);
          out += ")(";
          children(n->kids, false);
          out += ")";
        }
        out += ")";
        break;
      }
      case AstKind::Para:
        out += "__at(para(";
        children(n->kids, false);
        close(n);
        break;
      case AstKind::Heading:
        appendf(out, "__at(heading(%d, ", n->tag);
        if (n->aux) strLit(n->aux);
        else out += "null";
        children(n->kids, true);
        close(n);
        break;
      case AstKind::Ref:
        out += "__at(ref(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::ListB:
        appendf(out, "__at(list(%s, %d", n->ordered ? "true" : "false", n->num);
        children(n->kids, true);
        close(n);
        break;
      case AstKind::Item:
        out += "__at(item(";
        children(n->kids, false);
        close(n);
        break;
      case AstKind::Quote:
        out += "__at(quote(";
        children(n->kids, false);
        close(n);
        break;
      case AstKind::CodeBlockB:
        out += "__at(codeblock(";
        strLit(n->aux);
        out += ", ";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Rule:
        out += "__at(rule(";
        close(n);
        break;
      default:
        out += "text(\"\")";
        break;
    }
  }
};
}  // namespace

JsProgram codegen(const AstNode* doc, const SourceText& src, const Interner& strs) {
  JsProgram p;
  std::string& out = p.text;
  out += "export default async ({__emit, __at, para, text, em, strong, val, m, "
         "heading, list, item, quote, codeblock, rule, comment, link, code, seq, "
         "ref, term, toc, glossary}, $) => {\n";
  Gen g{src, strs, out};
  for (const AstNode* n : doc->kids) {
    if (n->kind == AstKind::CodeStmt) {
      if (n->tag == 0) {
        out += "let";
        out += src.slice(n->expr);
        out += ";\n";
      } else {
        out += src.slice(n->expr);
        out += "\n";
      }
      continue;
    }
    out += "__emit(";
    g.value(n);
    out += ");\n";
  }
  out += "};\n//# sourceURL=tsm:doc\n";
  return p;
}

}  // namespace tsr
