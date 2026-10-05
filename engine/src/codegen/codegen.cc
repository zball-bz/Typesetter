#include "codegen.h"
#include "../inline/jslex.h"
#include <iterator>

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
        out += "__a(__t(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Styled:
        appendf(out, "__a(%s(", n->tag == (u8)'*' ? "__b" : "__i");
        children(n->kids, false);
        close(n);
        break;
      case AstKind::Code:
        out += "__a(__cd(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Comment:
        out += "__a(__cm(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Note:
        out += "__a(__nt(";
        children(n->kids, false);
        close(n);
        break;
      case AstKind::Link:
        out += "__a(__ln(";
        strLit(n->aux);
        children(n->kids, true);
        close(n);
        break;
      case AstKind::SpliceArg:
        if (n->kids.size() == 1) value(n->kids[0]);
        else {
          out += "__sq(";
          children(n->kids, false);
          out += ")";
        }
        break;
      case AstKind::Splice: {
        out += "__v(";
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
        // a paragraph that is exactly one display formula IS the mathblock
        if (n->kids.size() == 1 && n->kids[0]->kind == AstKind::Math &&
            n->kids[0]->tag == 1) {
          out += "__a(__mb(";
          strLit(n->kids[0]->str);
          if (n->kids[0]->aux) {
            out += ", ";
            strLit(n->kids[0]->aux);
          }
          close(n->kids[0]);
          break;
        }
        out += "__a(__p(";
        children(n->kids, false);
        close(n);
        break;
      case AstKind::Math:
        // mid-paragraph display degrades to inline (deterministic; documented)
        out += "__a(__mi(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Heading:
        appendf(out, "__a(__hd(%d, ", n->tag);
        if (n->aux) strLit(n->aux);
        else out += "null";
        children(n->kids, true);
        close(n);
        break;
      case AstKind::Ref:
        out += "__a(__rf(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::ListB:
        appendf(out, "__a(__l(%s, %d", n->ordered ? "true" : "false", n->num);
        children(n->kids, true);
        close(n);
        break;
      case AstKind::Item:
        out += "__a(__it(";
        children(n->kids, false);
        close(n);
        break;
      case AstKind::Quote:
        out += "__a(__qt(";
        children(n->kids, false);
        close(n);
        break;
      case AstKind::CodeBlockB:
        // dispatcher call: unknown tags fall back to a plain code block at
        // runtime; handlers may be async (document fn already is)
        out += "__a(__v(await __fence(";
        strLit(n->aux);
        out += ", ({";
        if (!n->expr.empty()) out += src.slice(n->expr);
        out += "}), ";
        strLit(n->str);
        appendf(out, ", %d)", n->num);  // body source offset (close() ends val)
        close(n);
        break;
      case AstKind::Region: {
        out += "__a(__region(";
        strLit(n->str);
        out += ", ({";
        if (!n->expr.empty()) out += src.slice(n->expr);
        out += "}), [";
        for (size_t i = 0; i < n->kids.size(); i++) {
          if (i) out += ", ";
          const AstNode* k = n->kids[i];
          if (k->kind == AstKind::Para && !k->kids.empty() &&
              k->kids[0]->kind == AstKind::Row) {
            out += "[";  // one source paragraph: array of rows
            for (size_t r = 0; r < k->kids.size(); r++) {
              if (r) out += ", ";
              const AstNode* row = k->kids[r];
              out += "[";  // one row: array of cell values
              for (size_t c = 0; c < row->kids.size(); c++) {
                if (c) out += ", ";
                const AstNode* cell = row->kids[c];
                if (cell->kids.size() == 1) value(cell->kids[0]);
                else {
                  out += "__sq(";
                  children(cell->kids, false);
                  out += ")";
                }
              }
              out += "]";
            }
            out += "]";
          } else {
            value(k);
          }
        }
        out += "]";
        close(n);
        break;
      }
      case AstKind::Rule:
        out += "__a(__hr(";
        close(n);
        break;
      case AstKind::Error:
        out += "__a(__er(";
        strLit(n->str);
        out += ", ";
        strLit(n->aux);
        close(n);
        break;
      case AstKind::CodeStmt:
        // unreachable: nested statements become Error nodes in the AST
        // builder (plan P0-05); top-level ones are units of the program
        out += "__t(\"\")";
        break;
      case AstKind::Doc:
      case AstKind::Row:
      case AstKind::Cell:
        // structural: Doc is the program, Row/Cell are consumed by Region
        out += "__t(\"\")";
        break;
    }
  }
};
}  // namespace

// Does a block evaluate user JavaScript itself — a splice expression, or a
// fence / region argument list? Only such blocks are framed (plan P0-05).
// Fence and region *handlers* are contained by __fence / __region at the
// call, so a plain ```js block needs no frame (frames cost ~0.2 ms per 67
// blocks on the 87K bench: P0-05 perf gate).
static bool hasUserCode(const AstNode* n) {
  if (n->kind == AstKind::Splice) return true;
  if ((n->kind == AstKind::CodeBlockB || n->kind == AstKind::Region) && !n->expr.empty())
    return true;
  for (const AstNode* k : n->kids)
    if (hasUserCode(k)) return true;
  return false;
}

// Constructors the generated code calls, always through short __-aliases
// (D-L15: user bindings may not start with __, so they cannot shadow them;
// short names keep the module as small as before, plan P0-05 perf gate).
static const char* kGenCtors[][2] = {
    {"text", "__t"},  {"para", "__p"},  {"em", "__i"},       {"strong", "__b"},
    {"val", "__v"},   {"heading", "__hd"}, {"list", "__l"},  {"item", "__it"},
    {"quote", "__qt"}, {"rule", "__hr"}, {"comment", "__cm"}, {"link", "__ln"},
    {"code", "__cd"}, {"seq", "__sq"},  {"ref", "__rf"},     {"note", "__nt"},
    {"mathinline", "__mi"}, {"mathblock", "__mb"}, {"error", "__er"}};
// Names user code sees (shadowable by #let: they live in the outer scope).
static const char* kUserCtors[] = {"para", "text", "em", "strong", "val", "m", "heading",
                                   "list", "item", "quote", "codeblock", "rule", "comment",
                                   "link", "code", "seq", "ref", "term", "toc", "glossary",
                                   "notes", "note", "bibliography", "style", "mathinline",
                                   "mathblock", "image"};

// The per-compile nonce in unit markers: FNV-1a over the source length and
// up to 64 sampled bytes. Deterministic (I1) and cheap; a user's own text
// cannot accidentally contain a marker.
static u64 sampleHash(std::string_view s) {
  u64 h = 1469598103934665603ull ^ (u64)s.size();
  size_t step = s.size() / 64 + 1;
  for (size_t i = 0; i < s.size(); i += step) {
    h ^= (unsigned char)s[i];
    h *= 1099511628211ull;
  }
  return h;
}

JsProgram codegen(const AstNode* doc, const SourceText& src, const Interner& strs) {
  JsProgram p;
  std::string& out = p.text;
  // Module shape (plan P0-05; the hygienic printed-JS form of MD-04):
  //   outer function: the user-visible names (para …), shadowable by #let
  //   inner function: its parameters are the generated __-aliases (fast
  //   locals), its body holds user statements and the program's units
  out += "export default async (__s, $) => {\nconst {";
  for (size_t i = 0; i < std::size(kUserCtors); i++) {
    if (i) out += ", ";
    out += kUserCtors[i];
  }
  out += "} = __s;\nreturn (async ({__emit, __at: __a, __region, __fence, __fail, __height, "
         "__cur, __syntax";
  for (auto& n : kGenCtors) appendf(out, ", %s: %s", n[0], n[1]);
  out += "}) => {\n";

  // hoisted simple #let names (deduped: a repeated #let is a reassignment, D-L02)
  std::vector<std::string> hoisted;
  for (const AstNode* n : doc->kids) {
    if (n->kind != AstKind::CodeStmt || n->tag != 0) continue;
    std::string_view inner = src.slice(n->expr);
    JsSimpleLet sl = jsSimpleLet(inner);
    if (!sl.ok) continue;
    std::string name(inner.substr(sl.identStart, sl.identEnd - sl.identStart));
    bool seen = false;
    for (const std::string& h : hoisted) seen = seen || h == name;
    if (!seen) hoisted.push_back(name);
  }
  if (!hoisted.empty()) {
    out += "let ";
    for (size_t i = 0; i < hoisted.size(); i++) {
      if (i) out += ", ";
      out += hoisted[i];
    }
    out += ";\n";
  }

  // Only units that run user code are listed (they alone can fail); their
  // index in this table is the id passed to __fail / __cur / __syntax. Each
  // listed unit is bracketed by /*<nonce>[i*/ … /*<nonce>]i*/ markers, which
  // the executor searches only on the SyntaxError path (D-I11).
  struct Unit { Span src; u32 flags; };
  std::vector<Unit> units;
  enum : u32 { kFramed = 2, kStmt = 4 };
  char nonce[24];
  snprintf(nonce, sizeof nonce, "%016llx", (unsigned long long)sampleHash(src.view()));
  Gen g{src, strs, out};
  auto open = [&](u32 i) { appendf(out, "/*%s[%u*/", nonce, i); };
  auto closeU = [&](u32 i) { appendf(out, "/*%s]%u*/\n", nonce, i); };
  auto framedOpen = [&]() { out += "{ const __h = __height(); try {\n"; };
  auto framedClose = [&](u32 i) { appendf(out, "\n} catch (__e) { __fail(%u, __e, __h); } }", i); };

  for (const AstNode* n : doc->kids) {
    u32 i = (u32)units.size();
    u32 flags = 0;
    if (n->kind == AstKind::CodeStmt) {
      flags = kStmt;
      std::string_view inner = src.slice(n->expr);
      open(i);
      if (n->tag == 0) {
        JsSimpleLet sl = jsSimpleLet(inner);
        if (sl.ok) {  // #let x = e  →  framed assignment to the hoisted binding
          flags |= kFramed;
          framedOpen();
          out += inner.substr(sl.identStart, sl.identEnd - sl.identStart);
          out += " = (\n";
          out += inner.substr(sl.exprStart);
          out += "\n);";
          framedClose(i);
        } else {  // pattern / several declarators: verbatim, unframed (D-I10)
          appendf(out, "__cur(%u);\nlet", i);
          out += inner;
          out += ";\n";
        }
      } else if (!jsDeclares(inner)) {  // declaration-free #{…}: framed
        flags |= kFramed;
        framedOpen();
        out += inner;
        framedClose(i);
      } else {  // declaring #{…}: bindings must stay visible (D-I10)
        appendf(out, "__cur(%u);\n", i);
        out += inner;
        out += "\n";
      }
      closeU(i);
    } else if (hasUserCode(n)) {
      flags = kFramed;
      open(i);
      framedOpen();
      out += "__emit(";
      g.value(n);
      out += ");";
      framedClose(i);
      closeU(i);
    } else {  // pure markup cannot throw: no frame, no table entry
      out += "__emit(";
      g.value(n);
      out += ");\n";
      continue;
    }
    units.push_back({n->span, flags});
  }
  out += "})(__s);\n};\n//# sourceURL=tsm:doc\n";
  // unit table, out of band on the last line (D-I11):
  //   //# tsm-units=<nonce>;<doc end>;[[s, e, flags], …]
  appendf(out, "//# tsm-units=%s;%u;[", nonce, doc->span.end);
  for (size_t k = 0; k < units.size(); k++) {
    if (k) out += ",";
    appendf(out, "[%u,%u,%u]", units[k].src.start, units[k].src.end, units[k].flags);
  }
  out += "]\n";
  return p;
}

}  // namespace tsr
