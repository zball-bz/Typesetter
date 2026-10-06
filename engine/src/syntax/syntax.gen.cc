// GENERATED from engine/src/syntax/syntax.def by tools/gen-syntax.mjs — do not edit.
#include "../ast/ast.h"
#include "../support/json.h"

namespace tsr {

// one node's line: "<name> @[s,e)<fields>" — the kind (or the sugar of a
// Call) picks the row whose format prints it
void dumpAstNode(std::string& out, const AstNode* n, const SourceText& src, const Interner& strs) {
  auto spanOut = [&] { appendf(out, " @[%u,%u)", n->span.start, n->span.end); };
  switch (n->kind) {
    case AstKind::Doc: {
      out += "doc";
      spanOut();
      break;
    }
    case AstKind::Text: {
      out += "text";
      spanOut();
      out += " str=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    }
    case AstKind::Comment: {
      out += "comment";
      spanOut();
      out += " body=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\"";
      break;
    }
    case AstKind::Call:
      switch (n->sugar) {
        case SugarId::para: {
          out += "para";
          spanOut();
          break;
        }
        case SugarId::heading: {
          out += "heading";
          spanOut();
          out += " level=";
          appendf(out, "%d", (int)side<HeadingP>(n).level);
          if (side<HeadingP>(n).label != 0) {
            out += " label=\"";
            appendEscaped(out, strs.get(side<HeadingP>(n).label));
            out += "\"";
          }
          break;
        }
        case SugarId::list: {
          out += "list";
          spanOut();
          out += " ";
          out += side<ListP>(n).ordered ? "ordered" : "bullet";
          out += " start=";
          appendf(out, "%d", (int)side<ListP>(n).start);
          break;
        }
        case SugarId::item: {
          out += "item";
          spanOut();
          break;
        }
        case SugarId::quote: {
          out += "quote";
          spanOut();
          break;
        }
        case SugarId::rule: {
          out += "rule";
          spanOut();
          break;
        }
        case SugarId::fence: {
          out += "codeblock";
          spanOut();
          out += " lang=\"";
          appendEscaped(out, strs.get(side<FenceP>(n).lang));
          out += "\" body=\"";
          appendEscaped(out, strs.get(n->str));
          out += "\"";
          break;
        }
        case SugarId::region: {
          out += "region";
          spanOut();
          out += " name=\"";
          appendEscaped(out, strs.get(n->str));
          out += "\"";
          break;
        }
        case SugarId::strong: {
          out += "styled";
          spanOut();
          out += " marker=*";
          break;
        }
        case SugarId::em: {
          out += "styled";
          spanOut();
          out += " marker=_";
          break;
        }
        case SugarId::code: {
          out += "code";
          spanOut();
          out += " str=\"";
          appendEscaped(out, strs.get(n->str));
          out += "\"";
          break;
        }
        case SugarId::link: {
          out += "link";
          spanOut();
          out += " url=\"";
          appendEscaped(out, strs.get(side<LinkP>(n).url));
          out += "\"";
          break;
        }
        case SugarId::note: {
          out += "note";
          spanOut();
          break;
        }
        case SugarId::ref: {
          out += "ref";
          spanOut();
          out += " target=\"";
          appendEscaped(out, strs.get(n->str));
          out += "\"";
          break;
        }
        case SugarId::math: {
          out += "math";
          spanOut();
          out += " display=";
          appendf(out, "%d", (int)side<MathP>(n).display);
          out += " src=\"";
          appendEscaped(out, strs.get(n->str));
          out += "\"";
          if (side<MathP>(n).label != 0) {
            out += " label=\"";
            appendEscaped(out, strs.get(side<MathP>(n).label));
            out += "\"";
          }
          break;
        }
        case SugarId::arg: {
          out += "arg";
          spanOut();
          break;
        }
        case SugarId::row: {
          out += "row";
          spanOut();
          break;
        }
        case SugarId::cell: {
          out += "cell";
          spanOut();
          break;
        }
      }
      break;
    case AstKind::Splice: {
      out += "splice";
      spanOut();
      out += " expr=\"";
      appendEscaped(out, strs.get(side<SpliceP>(n).expr));
      out += "\"";
      break;
    }
    case AstKind::Stmt: {
      out += side<StmtP>(n).let ? "code-let" : "code-block";
      spanOut();
      out += " js=\"";
      appendEscaped(out, src.slice(side<StmtP>(n).js));
      out += "\"";
      break;
    }
    case AstKind::Error: {
      out += "error";
      spanOut();
      out += " code=\"";
      appendEscaped(out, strs.get(n->str));
      out += "\" msg=\"";
      appendEscaped(out, strs.get(side<ErrorP>(n).message));
      out += "\"";
      break;
    }
  }
}

// one node's JSON members (no braces, no kids): kind, sugar, span, str and
// its payload fields
void jsonAstNode(std::string& out, const AstNode* n, const SourceText& src, const Interner& strs) {
  static constexpr const char* kKind[] = {"doc", "text", "comment", "call", "splice", "stmt", "error"};
  out += "\"kind\":\"";
  out += kKind[(int)n->kind];
  out += "\"";
  if (n->kind == AstKind::Call) {
    out += ",\"sugar\":\"";
    out += kSugarName[(int)n->sugar];
    out += "\"";
  }
  appendf(out, ",\"span\":[%u,%u]", n->span.start, n->span.end);
  if (n->str) {
    out += ",\"str\":";
    jsonString(out, strs.get(n->str));
  }
  switch (n->kind) {
    case AstKind::Doc:
      break;
    case AstKind::Text: {
      const TextP& p = side<TextP>(n);
      if (p.rawmap) {
      out += ",\"rawmap\":";
      jsonString(out, strs.get(p.rawmap));
      }
      break;
    }
    case AstKind::Comment:
      break;
    case AstKind::Call:
      switch (n->sugar) {
        case SugarId::para:
          break;
        case SugarId::heading: {
          const HeadingP& p = side<HeadingP>(n);
          out += ",\"level\":";
          appendf(out, "%d", (int)p.level);
          out += ",\"label\":";
          jsonString(out, strs.get(p.label));
          break;
        }
        case SugarId::list: {
          const ListP& p = side<ListP>(n);
          out += ",\"ordered\":";
          out += p.ordered ? "true" : "false";
          out += ",\"start\":";
          appendf(out, "%d", (int)p.start);
          break;
        }
        case SugarId::item:
          break;
        case SugarId::quote:
          break;
        case SugarId::rule:
          break;
        case SugarId::fence: {
          const FenceP& p = side<FenceP>(n);
          out += ",\"lang\":";
          jsonString(out, strs.get(p.lang));
          out += ",\"args\":";
          jsonString(out, src.slice(p.args));
          out += ",\"bodyOffset\":";
          appendf(out, "%u", (unsigned)p.bodyOffset);
          out += ",\"bodyEnd\":";
          appendf(out, "%u", (unsigned)p.bodyEnd);
          out += ",\"lines\":";
          jsonString(out, strs.get(p.lines));
          break;
        }
        case SugarId::region: {
          const RegionP& p = side<RegionP>(n);
          out += ",\"args\":";
          jsonString(out, src.slice(p.args));
          break;
        }
        case SugarId::strong:
          break;
        case SugarId::em:
          break;
        case SugarId::code:
          break;
        case SugarId::link: {
          const LinkP& p = side<LinkP>(n);
          out += ",\"url\":";
          jsonString(out, strs.get(p.url));
          break;
        }
        case SugarId::note:
          break;
        case SugarId::ref:
          break;
        case SugarId::math: {
          const MathP& p = side<MathP>(n);
          out += ",\"display\":";
          out += p.display ? "true" : "false";
          out += ",\"label\":";
          jsonString(out, strs.get(p.label));
          break;
        }
        case SugarId::arg:
          break;
        case SugarId::row:
          break;
        case SugarId::cell:
          break;
      }
      break;
    case AstKind::Splice: {
      const SpliceP& p = side<SpliceP>(n);
      out += ",\"expr\":";
      jsonString(out, strs.get(p.expr));
      out += ",\"lastCall\":";
      appendf(out, "%u", (unsigned)p.lastCall);
      break;
    }
    case AstKind::Stmt: {
      const StmtP& p = side<StmtP>(n);
      out += ",\"let\":";
      out += p.let ? "true" : "false";
      out += ",\"js\":";
      jsonString(out, src.slice(p.js));
      break;
    }
    case AstKind::Error: {
      const ErrorP& p = side<ErrorP>(n);
      out += ",\"message\":";
      jsonString(out, strs.get(p.message));
      break;
    }
  }
}

}  // namespace tsr
