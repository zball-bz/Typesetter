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
  void children(std::span<AstNode* const> kids, bool leadingComma) {
    for (size_t i = 0; i < kids.size(); i++) {
      if (leadingComma || i) out += ", ";
      value(kids[i]);
    }
  }
  // a content body: its one value, or a sequence
  void body(const AstNode* n) {
    if (n->nkids == 1) value(n->kids()[0]);
    else {
      out += "__sq(";
      children(n->kids(), false);
      out += ")";
    }
  }

  void value(const AstNode* n) {
    switch (n->kind) {
      case AstKind::Text:
        out += "__a(__t(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Comment:
        out += "__a(__cm(";
        strLit(n->str);
        close(n);
        break;
      case AstKind::Call:
        call(n);
        break;
      case AstKind::Splice: {
        const SpliceP& sp = side<SpliceP>(n);
        std::string_view expr = strs.get(sp.expr);
        out += "__v(";
        if (n->nkids == 0) {
          out += "(";
          out += expr;
          out += ")";
        } else if (sp.lastCall > 0) {
          // trailing content args desugar into the final call: f(a)[c] → f(a, c)
          out += expr.substr(0, sp.lastCall);
          out += "(";
          std::string_view inner = expr.substr(sp.lastCall + 1, expr.size() - 1 - (sp.lastCall + 1));
          bool innerEmpty = true;
          for (char c : inner)
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { innerEmpty = false; break; }
          out += inner;
          children(n->kids(), !innerEmpty);
          out += ")";
        } else {
          out += "(";
          out += expr;
          out += ")(";
          children(n->kids(), false);
          out += ")";
        }
        out += ")";
        break;
      }
      case AstKind::Error:
        out += "__a(__er(";
        strLit(n->str);
        out += ", ";
        strLit(side<ErrorP>(n).message);
        close(n);
        break;
      case AstKind::Stmt:
        // unreachable: nested statements become Error nodes in the AST
        // builder (plan P0-05); top-level ones are units of the program
        out += "__t(\"\")";
        break;
      case AstKind::Doc:
        // structural: the program
        out += "__t(\"\")";
        break;
    }
  }

  // Built-in sugar: one adapter per slot, printing today's constructor call
  // (the lowering contract of T2 replaces these in plan P2-xx).
  void call(const AstNode* n) {
    switch (n->sugar) {
      case SugarId::strong:
      case SugarId::em:
        appendf(out, "__a(%s(", n->sugar == SugarId::strong ? "__b" : "__i");
        children(n->kids(), false);
        close(n);
        break;
      case SugarId::code:
        out += "__a(__cd(";
        strLit(n->str);
        close(n);
        break;
      case SugarId::note:
        out += "__a(__nt(";
        children(n->kids(), false);
        close(n);
        break;
      case SugarId::link:
        out += "__a(__ln(";
        strLit(side<LinkP>(n).url);
        children(n->kids(), true);
        close(n);
        break;
      case SugarId::arg:
        body(n);
        break;
      case SugarId::para: {
        // a paragraph that is exactly one display formula IS the mathblock
        // (interim L1 rule until T2's normalization; no promotion in the AST)
        const AstNode* only = n->nkids == 1 ? n->kids()[0] : nullptr;
        if (only && only->isCall(SugarId::math) && side<MathP>(only).display) {
          out += "__a(__mb(";
          strLit(only->str);
          if (side<MathP>(only).label) {
            out += ", ";
            strLit(side<MathP>(only).label);
          }
          close(only);
          break;
        }
        out += "__a(__p(";
        children(n->kids(), false);
        close(n);
        break;
      }
      case SugarId::math:
        // mid-paragraph display degrades to inline (deterministic; documented)
        out += "__a(__mi(";
        strLit(n->str);
        close(n);
        break;
      case SugarId::heading: {
        const HeadingP& h = side<HeadingP>(n);
        appendf(out, "__a(__hd(%d, ", h.level);
        if (h.label) strLit(h.label);
        else out += "null";
        children(n->kids(), true);
        close(n);
        break;
      }
      case SugarId::ref:
        out += "__a(__rf(";
        strLit(n->str);
        close(n);
        break;
      case SugarId::list: {
        const ListP& l = side<ListP>(n);
        appendf(out, "__a(__l(%s, %d", l.ordered ? "true" : "false", l.start);
        children(n->kids(), true);
        close(n);
        break;
      }
      case SugarId::item:
        out += "__a(__it(";
        children(n->kids(), false);
        close(n);
        break;
      case SugarId::quote:
        out += "__a(__qt(";
        children(n->kids(), false);
        close(n);
        break;
      case SugarId::fence: {
        // dispatcher call: unknown tags fall back to a plain code block at
        // runtime; handlers may be async (document fn already is)
        const FenceP& f = side<FenceP>(n);
        out += "__a(__v(await __fence(";
        strLit(f.lang);
        out += ", ({";
        if (!f.args.empty()) out += src.slice(f.args);
        out += "}), ";
        strLit(n->str);
        appendf(out, ", %d", (int)f.bodyOffset);  // body source offset
        if (f.lines) {                              // per-line offsets (contained)
          out += ", ";
          out += strs.get(f.lines);
        }
        out += ")";  // close() ends val
        close(n);
        break;
      }
      case SugarId::region: {
        const RegionP& r = side<RegionP>(n);
        out += "__a(__region(";
        strLit(n->str);
        out += ", ({";
        if (!r.args.empty()) out += src.slice(r.args);
        out += "}), [";
        std::span<AstNode* const> kids = n->kids();
        for (size_t i = 0; i < kids.size(); i++) {
          if (i) out += ", ";
          const AstNode* k = kids[i];
          if (k->isCall(SugarId::para) && k->nkids && k->kids()[0]->isCall(SugarId::row)) {
            out += "[";  // one source paragraph: array of rows
            for (size_t r = 0; r < k->nkids; r++) {
              if (r) out += ", ";
              const AstNode* row = k->kids()[r];
              out += "[";  // one row: array of cell values
              for (size_t c = 0; c < row->nkids; c++) {
                if (c) out += ", ";
                body(row->kids()[c]);
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
      case SugarId::rule:
        out += "__a(__hr(";
        close(n);
        break;
      case SugarId::row:
      case SugarId::cell:
        // structural: consumed by region
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
  if (n->isCall(SugarId::fence) && !side<FenceP>(n).args.empty()) return true;
  if (n->isCall(SugarId::region) && !side<RegionP>(n).args.empty()) return true;
  for (const AstNode* k : n->kids())
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
  for (const AstNode* n : doc->kids()) {
    if (n->kind != AstKind::Stmt || !side<StmtP>(n).let) continue;
    std::string_view inner = src.slice(side<StmtP>(n).js);
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

  for (const AstNode* n : doc->kids()) {
    u32 i = (u32)units.size();
    u32 flags = 0;
    if (n->kind == AstKind::Stmt) {
      flags = kStmt;
      std::string_view inner = src.slice(side<StmtP>(n).js);
      open(i);
      if (side<StmtP>(n).let) {
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
