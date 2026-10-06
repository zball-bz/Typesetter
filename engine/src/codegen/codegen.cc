#include "codegen.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "../inline/jslex.h"
#include "../linepass/linepass.h"
#include "../support/json.h"
#include "stdnames.gen.h"

namespace tsr {

namespace {


// `#let name = expr` that becomes a hoisted name and an assignment hole
bool hoistable(std::string_view inner, JsSimpleLet& sl) {
  sl = jsSimpleLet(inner);
  return sl.ok && !jsReservedWord(inner.substr(sl.identStart, sl.identEnd - sl.identStart));
}

// an else branch has no head (an `if` head is never at offset 0)
bool isElse(Span head) { return head.start == 0 && head.end == 0; }

void appendJs(std::string& out, std::string_view s) { appendUtf8Sanitized(out, s); }

// AST → program ops. Each value-writing method returns whether the value
// awaits (a fence, an async hole), which sets the async bit of its op and
// of every op above it. Holes are numbered in preorder, so a frame's holes
// are one contiguous range. A scope (a keyword body declaring names, a loop
// body: plan P2-12) has a hole table of its own, numbered from 0: its holes
// are functions made inside the scope's JS, so they close over its names.
struct Gen {
  const SourceText& src;
  const Interner& strs;
  LowerWriter& w;
  std::vector<std::string> holes;  // the current hole table (top level: the module's __h)
  // a fragment (plan P2-13): its spans moved by `base`, or all `clamp` (a
  // text with no place in the source); its holes are descriptors, never
  // JavaScript
  bool frag = false;
  bool inMath = false;  // lowering a formula's hole (plan P2-15)
  u32 base = 0;
  const Span* clamp = nullptr;
  DiagSink* fdiags = nullptr;

  u32 newHole() {
    holes.emplace_back();
    return (u32)holes.size() - 1;
  }
  Span at(Span s) const { return !frag ? s : clamp ? *clamp : Span{s.start + base, s.end + base}; }
  // a fragment's splice is a hole only as a bare value head (#x, #a.b — the
  // scope's, D-L07 — with or without content arguments) or an
  // interpolation (#(__mK);): anything else would need eval
  static bool barePath(std::string_view e) {
    if (e.empty() || !isIdentStart(e[0]) || e.back() == '.') return false;
    for (size_t i = 0; i < e.size(); i++) {
      if (e[i] == '.') {
        if (i + 1 >= e.size() || !isIdentStart(e[i + 1])) return false;
      } else if (!isIdentCont(e[i])) {
        return false;
      }
    }
    return true;
  }
  static bool interpolation(std::string_view e, u32& k) {
    if (e.size() < 6 || e.substr(0, 4) != "(__m" || e.back() != ')') return false;
    k = 0;
    for (char c : e.substr(4, e.size() - 5)) {
      if (c < '0' || c > '9') return false;
      k = k * 10 + (u32)(c - '0');
    }
    return true;
  }
  bool fragHole(const AstNode* n) const {
    const SpliceP& sp = side<SpliceP>(n);
    std::string_view e = strs.get(sp.expr);
    u32 k;
    return (interpolation(e, k) && !n->nkids) || (barePath(e) && !sp.named);
  }

  // The holes of a subtree in the current table — splices, statements,
  // keyword conditions, scopes and loops, fence and region argument lists:
  // the user code a block runs itself. A block without any cannot throw, so
  // it gets no frame (the handlers of fences and regions are contained where
  // they are invoked).
  u32 holeCount(const AstNode* n) const {
    u32 c = 0;
    switch (n->kind) {
      case AstKind::Splice:
        if (frag && !fragHole(n)) return 0;  // text
        c = 1;
        break;
      case AstKind::Stmt:  // its hole (a content literal's body follows)
        if (frag) return 0;
        c = 1;
        break;
      case AstKind::Keyword: {
        if (frag) return 0;
        if (!n->nkids) return 0;
        if (strs.get(n->str) != "if") return 1;  // a loop: one hole, its body a table of its own
        for (const AstNode* br : n->kids())
          c += (isElse(side<BranchP>(br).head) ? 0 : 1) + (declares(br) ? 1 : kidHoles(br));
        return c;
      }
      case AstKind::Call:
        if (frag) break;  // argument lists are dropped
        if (n->isCall(SugarId::fence) && !side<FenceP>(n).args.empty()) c = 1;
        else if (n->isCall(SugarId::region) && !side<RegionP>(n).args.empty()) c = 1;
        break;
      case AstKind::Doc:
      case AstKind::Text:
      case AstKind::Comment:
      case AstKind::Error:
      case AstKind::Branch:
        break;
    }
    return c + kidHoles(n);
  }
  u32 kidHoles(const AstNode* n) const {
    u32 c = 0;
    for (const AstNode* k : n->kids()) c += holeCount(k);
    return c;
  }
  // The names a scope declares (plan P2-12; D-L02: a repeated #let is a
  // reassignment): its `#let x = e` and `#let x = [ … ]` statements wherever
  // they stand — a list item, a quote, a region, a content literal —, but
  // not in a keyword body, which is a scope of its own. The document is one
  // scope.
  void scopeNames(const AstNode* n, std::vector<std::string>& out) const {
    for (const AstNode* k : n->kids()) {
      if (k->kind == AstKind::Keyword) continue;
      if (k->kind == AstKind::Stmt && side<StmtP>(k).let) {
        std::string_view name = bound(k);
        if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
      }
      scopeNames(k, out);
    }
  }
  // the name a #let binds in its scope: `#let x = e`, `#let x = [ … ]` (a
  // pattern, a reserved word: none — the statement keeps it)
  std::string_view bound(const AstNode* st) const {
    std::string_view js = strs.get(side<StmtP>(st).js);
    if (side<StmtP>(st).content) return jsReservedWord(js) ? std::string_view{} : js;
    JsSimpleLet sl;
    return hoistable(js, sl) ? js.substr(sl.identStart, sl.identEnd - sl.identStart) : std::string_view{};
  }
  bool declares(const AstNode* br) const {
    std::vector<std::string> names;
    scopeNames(br, names);
    return !names.empty();
  }
  void span(Span s) {
    s = at(s);
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
    w.u((u32)holes.size());
    w.u((u32)holes.size() + hc);
    return done(at, value(k));
  }
  // a keyword body or a content literal's: its one value, or a seq of its
  // values — each in a frame when it runs user code, so an error stays in
  // the block (the iteration) it happens in
  bool blockBody(const AstNode* n) { return blockList(n->kids()); }
  bool blockList(std::span<AstNode* const> ks) {
    if (ks.size() == 1) return block(ks[0]);
    size_t at = callHead("seq", nullptr, 0);
    return done(at, blockKids(ks));
  }
  // a content body: its one value, or a seq of its values
  bool body(const AstNode* n) {
    if (n->nkids == 1) return value(n->kids()[0]);
    size_t at = callHead("seq", nullptr, 0);
    return done(at, kids(n->kids()));
  }
  void emptyText(std::string_view s = "") {
    callHead("text", nullptr, 1);
    key("text");
    w.constStr(s);
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
        if (clamp) pairs.clear();  // a clamped fragment's text has no place to map to
        w.u((u32)pairs.size() / 2);
        for (u32 x : pairs) w.u(x);
        // its cell cuts (plan P2-11): "o,…" → nSep o*
        std::vector<u32> seps;
        v = 0;
        digit = false;
        for (char ch : std::string_view(strs.get(side<TextP>(n).seps))) {
          if (ch >= '0' && ch <= '9') {
            v = v * 10 + (u32)(ch - '0');
            digit = true;
          } else if (digit) {
            seps.push_back(v);
            v = 0;
            digit = false;
          }
        }
        if (digit) seps.push_back(v);
        w.u((u32)seps.size());
        // offsets in the string as written (sanitized to UTF-8: an invalid
        // byte before a bar widens to U+FFFD)
        const std::string_view cooked = strs.get(n->str);
        for (u32 x : seps) {
          std::string pre;
          appendUtf8Sanitized(pre, cooked.substr(0, x));
          w.u((u32)pre.size());
        }
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
      case AstKind::Stmt:
        if (frag) return literal(n);
        return stmt(n);
      case AstKind::Keyword:
        if (frag) return literal(n);
        return keyword(n);
      case AstKind::Branch:  // only inside its Keyword
      case AstKind::Doc:
        emptyText();
        return false;
    }
    return false;
  }

  // (a fragment) code that would need eval: its source as text, and why
  bool literal(const AstNode* n) {
    if (fdiags)
      fdiags->add(Sev::Info, "fragment-splice", n->span,
                  "a fragment runs no JavaScript: only #name, #a.b and m`…` interpolations are values");
    w.op(Lop::TEXT);
    w.u(w.str(src.slice(n->span)));
    span(n->span);
    w.u(0);
    w.u(0);
    return false;
  }

  // A statement's hole: `#let x = e` assigns the hoisted x (D-L02), any other
  // #let and a #{…} run as written — at top level a declaring one is
  // verbatim instead (codegen below), anywhere else its bindings stay inside
  // it (statement-local).
  std::string stmtHole(std::string_view inner, bool let, bool aw) const {
    JsSimpleLet sl;
    std::string hole;
    if (let && hoistable(inner, sl)) {
      hole = aw ? "async () => { " : "() => { ";
      appendJs(hole, inner.substr(sl.identStart, sl.identEnd - sl.identStart));
      hole += " = (\n";
      appendJs(hole, inner.substr(sl.exprStart));
      hole += "\n); }";
      return hole;
    }
    hole = aw ? "async () => {\n" : "() => {\n";
    if (let) hole += "let";
    appendJs(hole, inner);
    if (let) hole += ";";
    hole += "\n}";
    return hole;
  }

  // A statement anywhere (plan P2-12): STMT, which runs its hole and is no
  // content; `#let x = [ … ]` (a content literal) is LET: its body is built
  // where it stands and bound to x.
  bool stmt(const AstNode* n) {
    const StmtP& st = side<StmtP>(n);
    std::string_view inner = strs.get(st.js);
    if (st.content) {
      const u32 h = newHole();
      size_t at = w.op(Lop::LET);
      w.u(h);
      std::string t = bound(n).empty() ? "(__v) => { let " : "(__v) => { ";
      appendJs(t, inner);
      t += " = __v; }";
      holes[h] = std::move(t);
      return done(at, blockBody(n));
    }
    const bool aw = jsMentions(inner, "await");
    const u32 h = newHole();
    size_t at = w.op(Lop::STMT);
    w.u(h);
    holes[h] = stmtHole(inner, st.let, aw);
    return done(at, aw);
  }

  // A keyword form (plan P2-12). #if: IF, each branch its condition (a hole,
  // evaluated in order until one holds) and its body; a body that declares
  // names is a SCOPE. #for / #while: LOOP — the loop is JS, its body a scope
  // evaluated per iteration.
  bool keyword(const AstNode* n) {
    std::string_view kw = strs.get(n->str);
    if (!n->nkids) {  // (the parser makes none: an unclosed body is an error)
      emptyText();
      return false;
    }
    if (kw != "if") return ownTable(n->kids()[0], n, kw);
    size_t at = w.op(Lop::IF);
    w.u(n->nkids);
    bool a = false;
    for (const AstNode* br : n->kids()) {
      const Span head = side<BranchP>(br).head;
      if (isElse(head)) {
        w.u(0);
      } else {
        std::string_view c = src.slice(head);
        const bool aw = jsMentions(c, "await");
        const u32 h = newHole();
        std::string t = aw ? "async () => (\n" : "() => (\n";
        appendJs(t, c);
        t += "\n)";
        holes[h] = std::move(t);
        w.u(h + 1);
        a |= aw;
      }
      a |= declares(br) ? ownTable(br, n, "") : blockBody(br);
    }
    return done(at, a);
  }

  // A body with a hole table of its own: SCOPE (kw empty) or LOOP. Its hole
  // is the scope in JS — the loop, the names it declares — and hands its
  // table to __b, which evaluates the body against it.
  bool ownTable(const AstNode* br, const AstNode* form, std::string_view kw) {
    const u32 h = newHole();
    size_t at = w.op(kw.empty() ? Lop::SCOPE : Lop::LOOP);
    w.u(h);
    const u32 nh = kidHoles(br);
    w.u(nh);
    if (!kw.empty()) span(form->span);
    std::vector<std::string> outer = std::move(holes);
    holes.clear();
    bool a = blockBody(br);
    std::vector<std::string> inner = std::move(holes);
    holes = std::move(outer);
    std::string_view head = kw.empty() ? std::string_view{} : src.slice(side<BranchP>(br).head);
    a |= jsMentions(head, "await");
    // the scope's names start as the names they shadow (`#let n = n + 1`
    // reads the outer n, as before its #let): read outside the block that
    // declares them
    std::vector<std::string> names;
    scopeNames(br, names);
    std::string t = a ? "async (__b) => {\n" : "(__b) => {\n";
    if (!kw.empty()) {
      t += "const __r = [];\n";
      t += kw;
      t += " (\n";
      appendJs(t, head);
      t += "\n) {\n";
    }
    for (size_t i = 0; i < names.size(); i++)
      appendf(t, "const __s%zu = typeof %s === \"undefined\" ? undefined : %s;\n", i, names[i].c_str(),
              names[i].c_str());
    t += "{\n";
    if (!names.empty()) {
      t += "let ";
      for (size_t i = 0; i < names.size(); i++) appendf(t, "%s%s = __s%zu", i ? ", " : "", names[i].c_str(), i);
      t += ";\n";
    }
    t += kw.empty() ? "return " : "__r.push(";
    t += a ? "await __b([\n" : "__b([\n";
    for (const std::string& x : inner) {
      t += x;
      t += ",\n";
    }
    t += kw.empty() ? "]);\n}\n}" : "]));\n}\n}\nreturn __r;\n}";
    holes[h] = std::move(t);
    (void)nh;
    return done(at, a);
  }

  // #expr, #f(args)[content]…: a hole. Content arguments go in structurally:
  // the hole gets __k, and __k() evaluates them where the call's argument
  // list ends (plan P2-02) — f(...[args], ...__k()), so an empty list, a
  // trailing comma or a comment needs no text surgery.
  bool splice(const AstNode* n) {
    if (frag) return fragSplice(n);
    const SpliceP& sp = side<SpliceP>(n);
    std::string_view expr = strs.get(sp.expr);
    const u32 h = newHole();
    size_t at = w.op(Lop::HOLE);
    w.u(h);
    span(n->span);
    const bool kidsAwait = kids(n->kids());
    // (plan P2-14) a splice that names a loading constructor awaits it
    bool loads = false;
    for (const char* n : kStdAsync) loads = loads || jsMentions(expr, n);
    const bool async = kidsAwait || loads || jsMentions(expr, "await");
    std::string t = async ? "async " : "";
    // a named argument list (plan P2-06) is one options object
    auto args = [&](std::string& o) {
      std::string_view a = expr.substr(sp.lastCall + 1, expr.size() - 1 - (sp.lastCall + 1));
      o += sp.named ? "(({" : "(...[";
      appendJs(o, a);
      o += sp.named ? "})" : "]";
    };
    if (n->nkids == 0 && sp.named) {
      t += "() => (";
      appendJs(t, expr.substr(0, sp.lastCall));
      args(t);
      t += "))";
    } else if (n->nkids == 0 && inMath) {
      // a formula's hole (plan P2-15): a number is a math value, a string
      // text, content itself
      t += "() => (__rt.std.mathHole(";
      appendJs(t, expr);
      t += "))";
    } else if (n->nkids == 0) {
      t += "() => (";
      appendJs(t, expr);
      t += ")";
    } else {
      const char* k = kidsAwait ? "...(await __k())" : "...__k()";
      t += "(__k) => (";
      if (sp.lastCall > 0) {
        appendJs(t, expr.substr(0, sp.lastCall));
        args(t);
        t += ", ";
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

  // a fragment's splice (plan P2-13): its hole's descriptor — {"v":k} the
  // k-th interpolation, {"p":"a.b"} a value head ("k":1 with content
  // arguments, "a":1 when they await) — else its source as text
  bool fragSplice(const AstNode* n) {
    if (!fragHole(n)) return literal(n);
    std::string_view expr = strs.get(side<SpliceP>(n).expr);
    const u32 h = newHole();
    size_t at = w.op(Lop::HOLE);
    w.u(h);
    span(n->span);
    const bool a = kids(n->kids());
    u32 k;
    if (interpolation(expr, k)) {
      holes[h] = "{\"v\":" + std::to_string(k) + "}";
    } else {
      holes[h] = "{\"p\":\"" + std::string(expr) + "\"";
      if (n->nkids) holes[h] += ",\"k\":1";
      if (a) holes[h] += ",\"a\":1";
      holes[h] += "}";
    }
    return done(at, a);
  }

  // a fence or region argument list: a hole making the opts object; the
  // operand is 0 (none) or (hole + 1) << 1 | awaits
  u32 argsHole(Span args, bool& awaits) {
    awaits = false;
    if (args.empty()) return 0;
    if (frag) {  // a fragment evaluates no JavaScript
      if (fdiags)
        fdiags->add(Sev::Info, "fragment-splice", args,
                    "a fragment runs no JavaScript: this argument list is dropped");
      return 0;
    }
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
        size_t at = callHead("para", &n->span, 0);
        return done(at, kids(n->kids()));
      }
      case SugarId::math: {
        // one formula (plan P2-15): math{display, label} of its per-line
        // fragments (mathsrc, each at its source) and holes, each in a
        // frame — a failing hole is an error inside the formula. Display
        // math is a block wherever it is (plan P2-11): the normal form places
        // it (alone in its paragraph that block, inside one inline: N1)
        const bool display = side<MathP>(n).display;
        const StrRef label = side<MathP>(n).label;
        size_t at = callHead("math", &n->span, (display ? 1 : 0) + (label ? 1 : 0));
        if (display) {
          key("display");
          w.constBool(true);
        }
        if (label) {
          key("label");
          w.constStr(strs.get(label));
        }
        w.u(n->nkids);
        bool a = false;
        for (const AstNode* k : n->kids()) {
          if (k->kind == AstKind::Text) {
            callHead("mathsrc", &k->span, 1);
            key("src");
            w.constStr(strs.get(k->str));
            w.u(0);
            continue;
          }
          inMath = true;
          a |= block(k);
          inMath = false;
        }
        return done(at, a);
      }
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
      case SugarId::ref: {
        // a structured reference (plan P2-09; design T3 S3): @[a, b] is a
        // parent ref with one child ref per id (the parent keeps the whole
        // target), and @x[…] / @[x][…] carry their bracket as the `extra`
        // child — a seq in slot "extra" the element row's template reads
        // (a supplement word, or a citation's locator; D-L01)
        std::string_view target = strs.get(n->str);
        std::vector<std::string_view> ids;
        if (target.find(',') != std::string_view::npos) {
          for (size_t at = 0; at <= target.size();) {
            size_t comma = target.find(',', at);
            if (comma == std::string_view::npos) comma = target.size();
            std::string_view id = target.substr(at, comma - at);
            while (!id.empty() && (id.front() == ' ' || id.front() == '\t')) id.remove_prefix(1);
            while (!id.empty() && (id.back() == ' ' || id.back() == '\t')) id.remove_suffix(1);
            if (!id.empty()) ids.push_back(id);
            at = comma + 1;
          }
        }
        if (!n->nkids && ids.empty()) return strCall("ref", n, "target", n->str);
        size_t at = callHead("ref", &n->span, 1);
        key("target");
        w.constStr(target);
        w.u((u32)ids.size() + (n->nkids ? 1 : 0));
        for (std::string_view id : ids) {
          callHead("ref", nullptr, 1);
          key("target");
          w.constStr(id);
          w.u(0);
        }
        bool a = false;
        if (n->nkids) {
          callHead("seq", nullptr, 1);
          key("slot");
          w.constStr("extra");
          a = kids(n->kids());
        }
        return done(at, a);
      }
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
        w.u(f.label ? w.str(strs.get(f.label)) + 1 : 0);  // ` <id>` (plan P2-06): 0 = none
        w.u(f.info ? w.str(strs.get(f.info)) + 1 : 0);    // the info words
        w.u(w.str(strs.get(n->str)));
        const Span body = this->at({f.bodyOffset, f.bodyEnd});
        w.u(body.start);
        w.u(body.end);
        if (f.lines && !clamp) {  // per-line offsets (contained): "[o1,o2,…]"
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
          for (u32 o : offs) w.constUint(this->at({o, o}).start);
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
        w.u(r.label ? w.str(strs.get(r.label)) + 1 : 0);  // ` <id>` (plan P2-06): 0 = none
        span(n->span);
        std::span<AstNode* const> ks = n->kids();
        w.u((u32)ks.size());
        for (const AstNode* k : ks) a |= block(k);  // its interior: blocks like any (plan P2-11)
        (void)a;  // a region always awaits: its handler may be async (P2-03)
        return done(at, true);
      }
      case SugarId::rule:
        callHead("rule", &n->span, 0);
        w.u(0);
        return false;
    }
    return false;
  }
};

}  // namespace

Lowered codegen(const AstNode* doc, const SourceText& src, const Interner& strs) {
  Lowered L;
  LowerWriter w;
  Gen g{src, strs, w, {}};

  // hoisted #let names, deduplicated: a repeated #let is a reassignment (D-L02)
  std::vector<std::string> hoisted;
  g.scopeNames(doc, hoisted);

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
    b.holeLo = (u32)g.holes.size();
    if (n->kind == AstKind::Stmt && !side<StmtP>(n).content) {
      std::string_view inner = strs.get(side<StmtP>(n).js);
      const bool aw = jsMentions(inner, "await");
      std::string verb;
      JsSimpleLet sl;
      // #let x = e → x = (e) and a declaration-free #{…}: framed holes
      bool isHole = true;
      if (side<StmtP>(n).let) {
        if (!hoistable(inner, sl)) {
          isHole = false;
          verb = "let";
          appendJs(verb, inner);
          verb += ";";
        }
      } else if (jsDeclares(inner)) {
        isHole = false;
        appendJs(verb, inner);
      }
      if (isHole) {
        b.kind = LBlock::Stmt;
        b.flags = kBlockUser | kBlockFramed | (aw ? kBlockAsync : 0);
        const u32 h = g.newHole();
        g.holes[h] = g.stmtHole(inner, side<StmtP>(n).let, aw);
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
    } else {  // a content literal is a content block of no content
      b.kind = LBlock::Content;
      b.flags = g.holeCount(n) ? kBlockUser | kBlockFramed : 0;
      if (g.value(n)) b.flags |= kBlockAsync;
    }
    b.holeHi = (u32)g.holes.size();
    w.blocks.push_back(b);
  }
  w.holes = (u32)g.holes.size();

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

// ---- fragments (plan P2-13) ---------------------------------------------------

Lowered codegenFragments(const std::vector<FragmentText>& texts, const Span* clamp) {
  Lowered L;
  LowerWriter w;
  Arena arena;
  Interner strs(arena);
  std::vector<std::string> holes;
  std::string diags;
  u32 docEnd = clamp ? clamp->end : 0;
  for (const FragmentText& ft : texts) {
    SourceText src;
    src.init(ft.text);
    DiagSink d;
    const Skeleton sk = linepass(src, arena, d);
    const AstNode* doc = parseDoc(src, sk, arena, strs, d);
    Gen g{src, strs, w, std::move(holes)};
    g.frag = true;
    g.base = ft.base;
    g.clamp = clamp;
    g.fdiags = &d;
    // its content (as a content body's, syntax-design §7): one paragraph —
    // statements aside — is its inline content
    std::vector<AstNode*> kids(doc->kids().begin(), doc->kids().end());
    size_t paras = 0, other = 0;
    for (const AstNode* k : kids) {
      if (k->isCall(SugarId::para)) paras++;
      else if (k->kind != AstKind::Stmt) other++;
    }
    if (paras == 1 && other == 0) {
      std::vector<AstNode*> inl;
      for (AstNode* k : kids) {
        if (k->kind == AstKind::Stmt) inl.push_back(k);
        else inl.insert(inl.end(), k->kids().begin(), k->kids().end());
      }
      kids = std::move(inl);
    }
    LBlockRow b;
    const Span sp = g.at({0, src.size()});
    b.s = sp.start;
    b.e = sp.end;
    b.pc = (u32)w.body.size();
    b.holeLo = (u32)g.holes.size();
    b.kind = LBlock::Content;
    u32 hc = 0;
    for (const AstNode* k : kids) hc += g.holeCount(k);
    b.flags = hc ? kBlockUser | kBlockFramed : 0;
    if (g.blockList(kids)) b.flags |= kBlockAsync;
    b.holeHi = (u32)g.holes.size();
    w.blocks.push_back(b);
    holes = std::move(g.holes);
    for (const Diag& x : d.items) {
      const Span ds = g.at(x.span);
      if (!diags.empty()) diags += ",";
      appendf(diags, "{\"sev\":%d,\"code\":", x.sev == Sev::Error ? 2 : x.sev == Sev::Warning ? 1 : 0);
      jsonString(diags, x.code);
      diags += ",\"msg\":";
      jsonString(diags, x.msg);
      appendf(diags, ",\"s\":%u,\"e\":%u}", ds.start, ds.end);
    }
    if (!clamp) docEnd = std::max(docEnd, ft.base + src.size());
  }
  w.holes = (u32)holes.size();
  L.program = w.finish(0, false, docEnd);
  L.js = "{\"holes\":[";
  for (size_t i = 0; i < holes.size(); i++) {
    if (i) L.js += ",";
    L.js += holes[i];
  }
  L.js += "],\"diags\":[" + diags + "]}";
  return L;
}

namespace {
bool getU32(std::string_view b, size_t& p, u32& v) {
  if (p + 4 > b.size()) return false;
  v = (u32)(u8)b[p] | (u32)(u8)b[p + 1] << 8 | (u32)(u8)b[p + 2] << 16 | (u32)(u8)b[p + 3] << 24;
  p += 4;
  return true;
}
void putU32(std::string& out, u32 v) {
  for (int i = 0; i < 4; i++) out += (char)((v >> (8 * i)) & 0xff);
}
}  // namespace

bool decodeFragmentRequest(std::string_view b, FragmentRequest& r) {
  r = FragmentRequest{};
  size_t p = 0;
  u32 n, s, e;
  if (!getU32(b, p, n) || p >= b.size()) return false;
  r.clamped = b[p++] != 0;
  if (!getU32(b, p, s) || !getU32(b, p, e) || s > e) return false;
  r.clamp = {s, e};
  if (n > (b.size() - p) / 8) return false;
  for (u32 i = 0; i < n; i++) {
    FragmentText t;
    u32 len;
    if (!getU32(b, p, t.base) || !getU32(b, p, len) || len > b.size() - p) return false;
    if ((u64)t.base + len > 0xffffffffu) return false;
    t.text.assign(b.substr(p, len));
    p += len;
    r.texts.push_back(std::move(t));
  }
  return p == b.size();
}

std::string runFragmentRequest(std::string_view request) {
  FragmentRequest r;
  Lowered L;
  if (decodeFragmentRequest(request, r)) L = codegenFragments(r.texts, r.clamped ? &r.clamp : nullptr);
  else L.js = "{\"error\":\"bad fragment request\"}";
  std::string out;
  putU32(out, (u32)L.program.size());
  out += L.program;
  putU32(out, (u32)L.js.size());
  out += L.js;
  return out;
}

}  // namespace tsr
