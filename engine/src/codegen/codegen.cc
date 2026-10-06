#include "codegen.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "../inline/jslex.h"
#include "stdnames.gen.h"

namespace tsr {

namespace {


// The holes of a subtree — splices, fence and region argument lists: the
// user code a block runs itself. A block without any cannot throw, so it
// gets no frame (the handlers of fences and regions are contained where
// they are invoked).
u32 holeCount(const AstNode* n) {
  u32 c = 0;
  if (n->kind == AstKind::Splice) c = 1;
  else if (n->isCall(SugarId::fence) && !side<FenceP>(n).args.empty()) c = 1;
  else if (n->isCall(SugarId::region) && !side<RegionP>(n).args.empty()) c = 1;
  for (const AstNode* k : n->kids()) c += holeCount(k);
  return c;
}

void appendJs(std::string& out, std::string_view s) { appendUtf8Sanitized(out, s); }

// AST → program ops. Each value-writing method returns whether the value
// awaits (a fence, an async hole), which sets the async bit of its op and
// of every op above it. Holes are numbered in preorder, so a frame's holes
// are one contiguous range.
struct Gen {
  const SourceText& src;
  const Interner& strs;
  LowerWriter& w;
  std::vector<std::string> holes;  // the hole module's __h entries

  u32 newHole() {
    holes.emplace_back();
    return w.holes++;
  }
  void span(Span s) {
    w.u(s.start);
    w.u(s.end);
  }
  size_t callHead(std::string_view ctor, const Span* sp, u32 nAttrs) {
    size_t at = w.op(Lop::CALL);
    w.body += (char)(sp ? kCallSpanned : 0);
    w.u(w.ctor(ctor));
    if (sp) span(*sp);
    w.u(nAttrs);
    return at;
  }
  void key(std::string_view k) { w.u(w.str(k)); }
  void num(double v) {
    if (v >= 0 && v < 4294967296.0 && v == (double)(u32)v) {
      w.constUint((u32)v);
    } else {
      w.body += (char)(u8)LConst::F64;
      u64 bits;
      std::memcpy(&bits, &v, 8);
      for (int i = 0; i < 8; i++) w.body += (char)((bits >> (8 * i)) & 0xff);
    }
  }
  bool done(size_t at, bool async) {
    if (async) w.markAsync(at);
    return async;
  }
  bool kids(std::span<AstNode* const> ks) {
    w.u((u32)ks.size());
    bool a = false;
    for (const AstNode* k : ks) a |= value(k);
    return a;
  }
  // block-level children: each one that runs user code gets its own frame
  bool blockKids(std::span<AstNode* const> ks) {
    w.u((u32)ks.size());
    bool a = false;
    for (const AstNode* k : ks) a |= block(k);
    return a;
  }
  bool block(const AstNode* k) {
    const u32 hc = holeCount(k);
    if (!hc) return value(k);
    size_t at = w.op(Lop::FRAME);
    span(k->span);
    w.u(w.holes);
    w.u(w.holes + hc);
    return done(at, value(k));
  }
  // a content body: its one value, or a seq of its values
  bool body(const AstNode* n) {
    if (n->nkids == 1) return value(n->kids()[0]);
    size_t at = callHead("seq", nullptr, 0);
    return done(at, kids(n->kids()));
  }
  void emptyText() {
    callHead("text", nullptr, 1);
    key("text");
    w.constStr("");
    w.u(0);
  }
  bool strCall(std::string_view ctor, const AstNode* n, std::string_view k, StrRef v) {
    callHead(ctor, &n->span, 1);
    key(k);
    w.constStr(strs.get(v));
    w.u(0);
    return false;
  }

  bool value(const AstNode* n) {
    switch (n->kind) {
      case AstKind::Text: {
        w.op(Lop::TEXT);
        w.u(w.str(strs.get(n->str)));
        span(n->span);
        // the cooked→raw map (plan P2-04): "c:r,…" → nMap (c r)*
        std::string_view m = strs.get(side<TextP>(n).rawmap);
        std::vector<u32> pairs;
        u32 v = 0;
        bool digit = false;
        for (char ch : m) {
          if (ch >= '0' && ch <= '9') {
            v = v * 10 + (u32)(ch - '0');
            digit = true;
          } else if (digit) {
            pairs.push_back(v);
            v = 0;
            digit = false;
          }
        }
        if (digit) pairs.push_back(v);
        w.u((u32)pairs.size() / 2);
        for (u32 x : pairs) w.u(x);
        return false;
      }
      case AstKind::Comment:
        return strCall("comment", n, "text", n->str);
      case AstKind::Call:
        return call(n);
      case AstKind::Splice:
        return splice(n);
      case AstKind::Error:
        callHead("error", &n->span, 2);
        key("code");
        w.constStr(strs.get(n->str));
        key("message");
        w.constStr(strs.get(side<ErrorP>(n).message));
        w.u(0);
        return false;
      case AstKind::Stmt:  // nested statements are Error nodes (plan P0-05)
      case AstKind::Doc:
        emptyText();
        return false;
    }
    return false;
  }

  // #expr, #f(args)[content]…: a hole. Content arguments go in structurally:
  // the hole gets __k, and __k() evaluates them where the call's argument
  // list ends (plan P2-02) — f(...[args], ...__k()), so an empty list, a
  // trailing comma or a comment needs no text surgery.
  bool splice(const AstNode* n) {
    const SpliceP& sp = side<SpliceP>(n);
    std::string_view expr = strs.get(sp.expr);
    const u32 h = newHole();
    size_t at = w.op(Lop::HOLE);
    w.u(h);
    span(n->span);
    const bool kidsAwait = kids(n->kids());
    const bool async = kidsAwait || jsMentions(expr, "await");
    std::string t = async ? "async " : "";
    if (n->nkids == 0) {
      t += "() => (";
      appendJs(t, expr);
      t += ")";
    } else {
      const char* k = kidsAwait ? "...(await __k())" : "...__k()";
      t += "(__k) => (";
      if (sp.lastCall > 0) {
        appendJs(t, expr.substr(0, sp.lastCall));
        t += "(...[";
        appendJs(t, expr.substr(sp.lastCall + 1, expr.size() - 1 - (sp.lastCall + 1)));
        t += "], ";
      } else {
        t += "(";
        appendJs(t, expr);
        t += ")(";
      }
      t += k;
      t += "))";
    }
    holes[h] = std::move(t);
    return done(at, async);
  }

  // a fence or region argument list: a hole making the opts object; the
  // operand is 0 (none) or (hole + 1) << 1 | awaits
  u32 argsHole(Span args, bool& awaits) {
    awaits = false;
    if (args.empty()) return 0;
    const u32 h = newHole();
    std::string_view a = src.slice(args);
    awaits = jsMentions(a, "await");
    std::string t = awaits ? "async () => ({" : "() => ({";
    appendJs(t, a);
    t += "})";
    holes[h] = std::move(t);
    return (h + 1) << 1 | (awaits ? 1 : 0);
  }

  bool call(const AstNode* n) {
    switch (n->sugar) {
      case SugarId::strong:
      case SugarId::em: {
        size_t at = callHead(n->sugar == SugarId::strong ? "strong" : "em", &n->span, 0);
        return done(at, kids(n->kids()));
      }
      case SugarId::code:
        return strCall("code", n, "text", n->str);
      case SugarId::note: {
        size_t at = callHead("note", &n->span, 0);
        return done(at, kids(n->kids()));
      }
      case SugarId::link: {
        size_t at = callHead("link", &n->span, 1);
        key("url");
        w.constStr(strs.get(side<LinkP>(n).url));
        return done(at, kids(n->kids()));
      }
      case SugarId::arg:
        return body(n);
      case SugarId::para: {
        // a paragraph that is exactly one display formula IS the mathblock
        // (interim L1 rule until T2's normalization; no promotion in the AST)
        const AstNode* only = n->nkids == 1 ? n->kids()[0] : nullptr;
        if (only && only->isCall(SugarId::math) && side<MathP>(only).display) {
          const StrRef label = side<MathP>(only).label;
          callHead("mathblock", &only->span, label ? 2 : 1);
          key("src");
          w.constStr(strs.get(only->str));
          if (label) {
            key("label");
            w.constStr(strs.get(label));
          }
          w.u(0);
          return false;
        }
        size_t at = callHead("para", &n->span, 0);
        return done(at, kids(n->kids()));
      }
      case SugarId::math:
        // mid-paragraph display degrades to inline (deterministic; documented)
        return strCall("mathinline", n, "src", n->str);
      case SugarId::heading: {
        const HeadingP& h = side<HeadingP>(n);
        size_t at = callHead("heading", &n->span, 2);
        key("level");
        num(h.level);
        key("label");
        if (h.label) w.constStr(strs.get(h.label));
        else w.constNull();
        return done(at, kids(n->kids()));
      }
      case SugarId::ref:
        return strCall("ref", n, "target", n->str);
      case SugarId::list: {
        const ListP& l = side<ListP>(n);
        size_t at = callHead("list", &n->span, 2);
        key("ordered");
        w.constBool(l.ordered);
        key("start");
        num(l.start);
        return done(at, kids(n->kids()));
      }
      case SugarId::item:
      case SugarId::quote: {
        size_t at = callHead(n->sugar == SugarId::item ? "item" : "quote", &n->span, 0);
        return done(at, blockKids(n->kids()));
      }
      case SugarId::fence: {
        // the dispatcher: an unknown tag falls back to a plain code block;
        // a handler may be async, so a fence always awaits
        const FenceP& f = side<FenceP>(n);
        bool argsAwait;
        const u32 args = argsHole(f.args, argsAwait);
        size_t at = w.op(Lop::FENCE);
        w.u(w.str(strs.get(f.lang)));
        w.u(args);
        w.u(w.str(strs.get(n->str)));
        w.u(f.bodyOffset);
        w.u(f.bodyEnd);
        if (f.lines) {  // per-line offsets (contained): "[o1,o2,…]"
          std::string_view l = strs.get(f.lines);
          std::vector<u32> offs;
          u32 v = 0;
          bool digit = false;
          for (char c : l) {
            if (c >= '0' && c <= '9') {
              v = v * 10 + (u32)(c - '0');
              digit = true;
            } else if (digit) {
              offs.push_back(v);
              v = 0;
              digit = false;
            }
          }
          w.constArrayHead((u32)offs.size());
          for (u32 o : offs) w.constUint(o);
        } else {
          w.constNull();
        }
        span(n->span);
        return done(at, true);
      }
      case SugarId::region: {
        const RegionP& r = side<RegionP>(n);
        bool a;
        const u32 args = argsHole(r.args, a);
        size_t at = w.op(Lop::REGION);
        w.u(w.str(strs.get(n->str)));
        w.u(args);
        span(n->span);
        std::span<AstNode* const> ks = n->kids();
        w.u((u32)ks.size());
        for (const AstNode* k : ks) {
          if (k->isCall(SugarId::para) && k->nkids && k->kids()[0]->isCall(SugarId::row)) {
            // one source paragraph of table rows: rows of cell values,
            // framed like any block that runs user code
            const u32 hc = holeCount(k);
            size_t frame = 0;
            if (hc) {
              frame = w.op(Lop::FRAME);
              span(k->span);
              w.u(w.holes);
              w.u(w.holes + hc);
            }
            size_t at = w.op(Lop::ROWS);
            bool r = false;
            w.u(k->nkids);
            for (const AstNode* row : k->kids()) {
              w.u(row->nkids);
              for (const AstNode* cell : row->kids()) r |= body(cell);
            }
            done(at, r);
            if (hc) done(frame, r);
            a |= r;
          } else {
            a |= block(k);
          }
        }
        (void)a;  // a region always awaits: its handler may be async (P2-03)
        return done(at, true);
      }
      case SugarId::rule:
        callHead("rule", &n->span, 0);
        w.u(0);
        return false;
      case SugarId::row:
      case SugarId::cell:  // structural: consumed by region
        emptyText();
        return false;
    }
    return false;
  }
};

// `#let name = expr` that becomes a hoisted name and an assignment hole
bool hoistable(std::string_view inner, JsSimpleLet& sl) {
  sl = jsSimpleLet(inner);
  return sl.ok && !jsReservedWord(inner.substr(sl.identStart, sl.identEnd - sl.identStart));
}

}  // namespace

Lowered codegen(const AstNode* doc, const SourceText& src, const Interner& strs) {
  Lowered L;
  LowerWriter w;
  Gen g{src, strs, w, {}};

  // hoisted #let names, deduplicated: a repeated #let is a reassignment (D-L02)
  std::vector<std::string> hoisted;
  for (const AstNode* n : doc->kids()) {
    if (n->kind != AstKind::Stmt || !side<StmtP>(n).let) continue;
    std::string_view inner = src.slice(side<StmtP>(n).js);
    JsSimpleLet sl;
    if (!hoistable(inner, sl)) continue;
    std::string name(inner.substr(sl.identStart, sl.identEnd - sl.identStart));
    if (std::find(hoisted.begin(), hoisted.end(), name) == hoisted.end()) hoisted.push_back(name);
  }

  // a statement that declares at the top level (a #{…} with let/const/
  // function…, a #let of a pattern) stays verbatim at its place in the
  // module, unframed, so later code sees its bindings (D-I10)
  struct Verbatim {
    u32 block;
    std::string text;
  };
  std::vector<Verbatim> verbatims;

  for (const AstNode* n : doc->kids()) {
    LBlockRow b;
    b.s = n->span.start;
    b.e = n->span.end;
    b.pc = (u32)w.body.size();
    b.holeLo = w.holes;
    if (n->kind == AstKind::Stmt) {
      std::string_view inner = src.slice(side<StmtP>(n).js);
      const bool aw = jsMentions(inner, "await");
      std::string hole, verb;
      JsSimpleLet sl;
      bool isHole = true;
      if (side<StmtP>(n).let) {
        if (hoistable(inner, sl)) {  // #let x = e → x = (e), framed
          hole = aw ? "async () => { " : "() => { ";
          appendJs(hole, inner.substr(sl.identStart, sl.identEnd - sl.identStart));
          hole += " = (\n";
          appendJs(hole, inner.substr(sl.exprStart));
          hole += "\n); }";
        } else {
          isHole = false;
          verb = "let";
          appendJs(verb, inner);
          verb += ";";
        }
      } else if (!jsDeclares(inner)) {  // declaration-free #{…}: framed
        hole = aw ? "async () => {\n" : "() => {\n";
        appendJs(hole, inner);
        hole += "\n}";
      } else {
        isHole = false;
        appendJs(verb, inner);
      }
      if (isHole) {
        b.kind = LBlock::Stmt;
        b.flags = kBlockUser | kBlockFramed | (aw ? kBlockAsync : 0);
        const u32 h = g.newHole();
        g.holes[h] = std::move(hole);
        size_t at = w.op(Lop::STMT);
        if (aw) w.markAsync(at);
        w.u(h);
      } else {
        b.kind = LBlock::Verbatim;
        b.flags = kBlockUser;
        w.op(Lop::VERBATIM);
        w.u((u32)verbatims.size());
        verbatims.push_back({(u32)w.blocks.size(), std::move(verb)});
      }
    } else {
      b.kind = LBlock::Content;
      b.flags = holeCount(n) ? kBlockUser | kBlockFramed : 0;
      if (g.value(n)) b.flags |= kBlockAsync;
    }
    b.holeHi = w.holes;
    w.blocks.push_back(b);
  }

  // The hole module (only when there is user code):
  //   outer function: the user-visible names, shadowable by #let
  //   inner function: hoisted names, the holes, and the program run in
  //   segments around the verbatim statements
  const bool module = !g.holes.empty() || !verbatims.empty();
  if (module) {
    std::string& js = L.js;
    std::vector<size_t> marks;  // byte offsets of each piece's start and end
    // the std names the user code mentions (schema.json's constructors and
    // std functions: stdnames.gen.h) — a name it never mentions cannot be
    // referenced, so a new constructor changes no module
    std::vector<bool> used(std::size(kStdNames), false);
    auto mark = [&](std::string_view code) {
      jsIdentsDeep(code, [&](std::string_view id) {
        for (size_t i = 0; i < std::size(kStdNames); i++)
          if (id == kStdNames[i]) used[i] = true;
        return true;
      });
    };
    for (const std::string& h : g.holes) mark(h);
    for (const Verbatim& v : verbatims) mark(v.text);
    appendf(js, "export const abi = 0x%08x;\nexport default async (__rt, $) => {\n", PROGRAM_ABI);
    std::string names;
    for (size_t i = 0; i < std::size(kStdNames); i++) {
      if (!used[i]) continue;
      if (!names.empty()) names += ", ";
      names += kStdNames[i];
    }
    if (!names.empty()) js += "const {" + names + "} = __rt.std;\n";
    js += "return (async () => {\n";
    if (!hoisted.empty()) {
      js += "let ";
      for (size_t i = 0; i < hoisted.size(); i++) {
        if (i) js += ", ";
        js += hoisted[i];
      }
      js += ";\n";
    }
    js += "const __h = [\n";
    for (const std::string& h : g.holes) {
      marks.push_back(js.size());
      js += h;
      marks.push_back(js.size());
      js += ",\n";
    }
    js += "];\n";
    for (size_t k = 0; k < verbatims.size(); k++) {
      appendf(js, "await __rt.run(__h, %zu);\n", k);
      marks.push_back(js.size());
      js += verbatims[k].text;
      marks.push_back(js.size());
      js += "\n";
    }
    appendf(js, "await __rt.run(__h, %zu);\n", verbatims.size());
    js += "})();\n};\n//# sourceURL=tsm:doc\n";
    // the marks as UTF-16 offsets (the host slices the module as a JS string)
    std::vector<u32> u16(marks.size());
    u32 units = 0;
    size_t bi = 0;
    for (size_t m = 0; m < marks.size(); m++) {
      for (; bi < marks[m]; bi++) {
        const u8 c = (u8)js[bi];
        if ((c & 0xC0) != 0x80) units++;
        if (c >= 0xF0) units++;
      }
      u16[m] = units;
    }
    for (u32 i = 0; i < g.holes.size(); i++)
      w.pieces.push_back({LPiece::Hole, i, u16[2 * i], u16[2 * i + 1]});
    for (size_t k = 0; k < verbatims.size(); k++) {
      const size_t m = 2 * (g.holes.size() + k);
      w.pieces.push_back({LPiece::Verbatim, verbatims[k].block, u16[m], u16[m + 1]});
    }
  }
  L.program = w.finish(module ? fnv1a64(L.js) : 0, module, doc->span.end);
  return L;
}

}  // namespace tsr
