#include "ir.h"

#include <cstdio>
#include <functional>

#include "../../gen/math_rows.h"
#include "dict.h"

namespace tsr {

namespace {

// ---- tokenizer -------------------------------------------------------------
struct Tok {
  enum K : u8 { End, Num, Word, Op, Chr, Sup, Sub, Slash, Open, Close, Prime, Quote, Param } k = End;
  std::string text;                // Num/Word/Quote/Param
  const SymbolInfo* op = nullptr;  // Op (dictionary hit)
  u32 cp = 0;                      // Chr (direct char) / Open / Close
  u8 cls = kOrd;                   // Chr fallback class
  u32 pos = 0, end = 0;            // the token's bytes
  bool adjOpen = false;            // Word: '(' follows with no space (a call)
};

inline bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
// characters that participate in operator-sequence maximal munch
inline bool isOpChar(char c) {
  switch (c) {
    case '+': case '-': case '*': case '=': case '<': case '>': case '|':
    case '~': case ':': case ';': case '.': case ',': case '!': case '@':
    case '&': case '?': case '%':
      return true;
    default:
      return false;
  }
}

struct Lexer {
  std::string_view s;
  u32 i = 0;
  bool templ = false;  // a stdlib template: #name is a parameter

  Tok next() {
    Tok t = scan();
    t.end = i;
    return t;
  }
  Tok scan() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    Tok t;
    t.pos = i;
    if (i >= s.size()) return t;
    char c = s[i];
    if (isDigit(c)) {
      u32 j = i;
      while (j < s.size() && isDigit(s[j])) j++;
      if (j + 1 < s.size() && s[j] == '.' && isDigit(s[j + 1])) {
        j++;
        while (j < s.size() && isDigit(s[j])) j++;
      }
      t.k = Tok::Num;
      t.text = std::string(s.substr(i, j - i));
      i = j;
      return t;
    }
    if (isLetter(c)) {
      u32 j = i;
      while (j < s.size() && isLetter(s[j])) j++;
      t.k = Tok::Word;
      t.text = std::string(s.substr(i, j - i));
      t.adjOpen = j < s.size() && s[j] == '(';
      i = j;
      return t;
    }
    if (templ && c == '#' && i + 1 < s.size() && isLetter(s[i + 1])) {
      u32 j = i + 1;
      while (j < s.size() && isLetter(s[j])) j++;
      t.k = Tok::Param;
      t.text = std::string(s.substr(i + 1, j - i - 1));
      i = j;
      return t;
    }
    if (c == '"') {  // "quoted text": an upright text-font run (Ord)
      u32 j = i + 1;
      while (j < s.size() && s[j] != '"') j++;
      t.k = Tok::Quote;
      t.text = std::string(s.substr(i + 1, j - (i + 1)));
      i = j < s.size() ? j + 1 : j;
      return t;
    }
    switch (c) {
      case '^': i++; t.k = Tok::Sup; return t;
      case '/': i++; t.k = Tok::Slash; return t;
      case '\'': i++; t.k = Tok::Prime; return t;
      case '(': case '[': case '{':
        i++;
        t.k = Tok::Open;
        t.cp = (u8)c;
        return t;
      case ')': case ']': case '}':
        i++;
        t.k = Tok::Close;
        t.cp = (u8)c;
        return t;
      default:
        break;
    }
    if (c == '_') {
      // _|_ is ⊥ (a key outside the operator munch); otherwise a subscript
      if (i + 2 < s.size() && s[i + 1] == '|' && s[i + 2] == '_') {
        if (const SymbolInfo* e = MathDict::byName("_|_")) {
          i += 3;
          t.k = Tok::Op;
          t.op = e;
          return t;
        }
      }
      i++;
      t.k = Tok::Sub;
      return t;
    }
    if (c == '!' && i + 1 < s.size() && isLetter(s[i + 1])) {
      // negated name: !in, !exists, …
      u32 j = i + 1;
      while (j < s.size() && isLetter(s[j])) j++;
      if (const SymbolInfo* e = MathDict::byName(s.substr(i, j - i))) {
        i = j;
        t.k = Tok::Op;
        t.op = e;
        return t;
      }
      // unknown negation: '!' alone, the word lexes next round
      i++;
      t.k = Tok::Chr;
      t.cp = '!';
      return t;
    }
    if (isOpChar(c)) {
      // maximal munch over the operator-key trie (operator characters only)
      u32 len = 0;
      if (const SymbolInfo* e = MathDict::matchOp(s, i, len)) {
        i += len;
        t.k = Tok::Op;
        t.op = e;
        return t;
      }
      i++;
      t.k = Tok::Chr;
      t.cp = (u8)c;
      return t;
    }
    // a direct Unicode character: the class of its default dictionary row
    u32 cp = utf8Next(s, i);
    t.k = Tok::Chr;
    t.cp = cp;
    t.cls = MathDict::classOfCp(cp);
    return t;
  }
};

// ---- parser ----------------------------------------------------------------
struct Parser {
  Lexer lex;
  Arena& arena;
  Tok tok;
  std::vector<MathDiag>* diags;
  const std::vector<SlotSpec>* params = nullptr;  // a template's (Param leaves)
  const std::vector<MathRow>* rows = nullptr;     // the registry being built, else the registry

  const MathRow* rowOf(std::string_view name) const {
    if (!rows) return mathRow(name);
    for (const MathRow& r : *rows)
      if (r.name == name) return &r;
    return nullptr;
  }

  void advance() { tok = lex.next(); }
  void err(u32 lo, u32 hi, std::string msg, Sev sev = Sev::Error, const char* code = "math-parse") {
    if (diags) diags->push_back({sev, code, lo, hi, std::move(msg)});
  }
  MNode* mk(MNode::K k, u32 lo = 0, u32 hi = 0) {
    MNode* n = arena.make<MNode>();
    n->k = k;
    n->lo = lo;
    n->hi = hi;
    return n;
  }
  MNode* atom(u32 cp, u8 cls, u8 flags, u32 lo, u32 hi) {
    MNode* n = mk(MNode::Sym, lo, hi);
    n->cp = cp;
    n->cls = cls;
    n->flags = flags;
    return n;
  }
  // an Error leaf: the source slice [lo, hi), set as text
  MNode* error(u32 lo, u32 hi) {
    MNode* n = mk(MNode::Error, lo, hi);
    std::string_view v = lex.s.substr(lo, hi > lo ? hi - lo : 0);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n')) v.remove_suffix(1);
    n->txt = std::string(v);
    n->textFont = true;
    return n;
  }
  bool atRel() const { return tok.k == Tok::Op && tok.op->cls == kRel; }
  bool atComma() const { return tok.k == Tok::Op && tok.op->cls == kPunct && tok.op->cp == ','; }
  bool runEnds() const { return tok.k == Tok::End || tok.k == Tok::Close; }
  // error resynchronisation: skip to `,` `)` `;` a relation or the end
  u32 resync() {
    while (tok.k != Tok::End && tok.k != Tok::Close && !atComma() && !atRel() &&
           !(tok.k == Tok::Op && tok.op->cp == ';'))
      advance();
    return tok.k == Tok::End ? (u32)lex.s.size() : tok.pos;
  }

  // one full expression run; stops at End / Close (caller consumes).
  // Factors nest at most 256 deep: deeper source is one error leaf (the
  // fuzzers' stack bound)
  int depth = 0;
  MNode* parseRun(bool stopAtRel = false) {
    MNode* run = mk(MNode::Run, tok.pos);
    while (!runEnds()) {
      if (stopAtRel && atRel()) break;
      parseMolecule(run->kids);
    }
    run->hi = tok.pos;
    return run;
  }

  // factor + postfix scripts/primes + fraction chaining; visible unscripted
  // groups splice their atoms into the run (TeX: '(' is an Open atom)
  void parseMolecule(std::vector<MNode*>& items) {
    MNode* f = parseFactor(&items);
    if (!f) return;
    f = attachPostfix(f);
    while (tok.k == Tok::Slash) {
      const u32 slash = tok.pos;
      advance();
      MNode* rhs = parseFactor(nullptr);
      if (!rhs) {
        err(slash, slash + 1, "missing denominator");
        rhs = error(slash, slash + 1);
      }
      rhs = attachPostfix(rhs);
      MNode* fr = mk(MNode::Frac, f->lo, rhs->hi);
      fr->a = shed(f);
      fr->b = shed(rhs);
      fr->cls = kOrd;
      f = fr;
    }
    items.push_back(f);
  }

  // group consumed as a script/fraction argument sheds its parens
  MNode* shed(MNode* f) { return f->k == MNode::Group ? f->a : f; }

  MNode* attachPostfix(MNode* f) {
    for (;;) {
      if (tok.k == Tok::Sup || tok.k == Tok::Sub) {
        const bool isSup = tok.k == Tok::Sup;
        const u32 at = tok.pos;
        advance();
        MNode* arg = parseFactor(nullptr);
        if (!arg) {
          err(at, at + 1, "missing script argument");
          arg = error(at, at + 1);
        }
        arg = shed(arg);
        if (f->k != MNode::Attach && f->k != MNode::BigOp) {
          MNode* sc = mk(MNode::Attach, f->lo);
          sc->a = f;
          sc->cls = f->cls;
          f = sc;
        }
        MNode*& slot = isSup ? f->sup : f->sub;
        if (isSup && slot && f->primeSup) {
          // primes and an explicit superscript merge: f'^2 = f^{′2}
          slot->kids.push_back(arg);
          f->primeSup = false;
        } else if (slot) {
          // a second script of the same kind: an error leaf after the base
          err(at, arg->hi > at ? arg->hi : at + 1, "double script");
          MNode* r = mk(MNode::Run, f->lo);
          r->kids.push_back(f);
          r->kids.push_back(error(at, tok.k == Tok::End ? (u32)lex.s.size() : tok.pos));
          r->cls = f->cls;
          f = r;
          continue;
        } else {
          slot = arg;
          if (isSup) f->primeSup = false;
        }
        f->hi = tok.pos;
        continue;
      }
      if (tok.k == Tok::Chr && tok.cp == '!') {
        // postfix factorial: fold into the base so fractions/scripts see n!
        // as one atom ("!=", "!in" were already claimed by the lexer)
        MNode* bang = atom('!', kOrd, 0, tok.pos, tok.end);
        advance();
        MNode* r = mk(MNode::Run, f->lo, bang->hi);
        r->kids.push_back(f);
        r->kids.push_back(bang);
        r->cls = kOrd;
        f = r;
        continue;
      }
      if (tok.k == Tok::Prime) {
        MNode* prime = atom(0x2032, kOrd, 0, tok.pos, tok.end);
        advance();
        if (f->k != MNode::Attach && f->k != MNode::BigOp) {
          MNode* sc = mk(MNode::Attach, f->lo);
          sc->a = f;
          sc->cls = f->cls;
          f = sc;
        }
        if (!f->sup) {
          f->sup = mk(MNode::Run, prime->lo);
          f->primeSup = true;
        } else if (f->sup->k != MNode::Run) {
          MNode* r = mk(MNode::Run, f->sup->lo);
          r->kids.push_back(f->sup);
          f->sup = r;
        }
        f->sup->kids.push_back(prime);
        f->hi = prime->hi;
        continue;
      }
      break;
    }
    return f;
  }

  // items == nullptr: single-token context (script/fraction argument; no
  // splicing target — single-token semantics: x^ab = x^a · b)
  MNode* parseFactor(std::vector<MNode*>* items) {
    const u32 lo = tok.pos, hi = tok.end;
    // every recursion (groups, scripts, calls) passes here: the bound
    struct Depth {
      int& d;
      explicit Depth(int& x) : d(++x) {}
      ~Depth() { --d; }
    } guard(depth);
    if (depth > 256 && tok.k != Tok::End && tok.k != Tok::Close) {
      while (tok.k != Tok::End) advance();
      err(lo, (u32)lex.s.size(), "formula nested too deeply");
      return error(lo, (u32)lex.s.size());
    }
    switch (tok.k) {
      case Tok::Num: {
        MNode* n = mk(MNode::Num, lo, hi);
        n->txt = tok.text;
        n->cls = kOrd;
        advance();
        return n;
      }
      case Tok::Word:
        return parseWord(items);
      case Tok::Param: {
        MNode* n = mk(MNode::Param, lo, hi);
        n->txt = tok.text;
        advance();
        return n;
      }
      case Tok::Quote: {
        MNode* n = mk(MNode::Text, lo, hi);
        n->txt = tok.text;
        n->cls = kOrd;
        n->textFont = true;
        advance();
        return n;
      }
      case Tok::Op: {
        const SymbolInfo* e = tok.op;
        advance();
        if (e->flags & kFlagLarge) return parseBigOp(e, lo, hi);
        return atom(e->cp, e->cls, e->flags, lo, hi);
      }
      case Tok::Chr: {
        MNode* n = atom(tok.cp, tok.cls, 0, lo, hi);
        advance();
        return n;
      }
      case Tok::Open: {
        const u32 open = tok.cp;
        advance();
        MNode* inner = parseRun();
        u32 close = 0;
        if (tok.k == Tok::Close) {
          close = tok.cp;  // mixed delimiters are fine: [0, 1) intervals
          advance();
        } else {
          err(lo, lo + 1, "unclosed bracket");
        }
        MNode* g = mk(MNode::Group, lo, tok.pos);
        g->a = inner;
        g->openCp = open;
        g->closeCp = close;
        g->cls = kOrd;
        return g;
      }
      case Tok::Prime:  // a stray prime with no base
        advance();
        return atom(0x2032, kOrd, 0, lo, hi);
      case Tok::Slash:
        // a dangling / is an ordinary slash, as in TeX (formulas split across
        // sources routinely end mid-expression)
        advance();
        return atom('/', kOrd, 0, lo, hi);
      case Tok::Sup:
      case Tok::Sub: {
        // an operator without an operand: its stretch is an error leaf
        advance();
        const u32 end = resync();
        err(lo, end, "operator without operand");
        return error(lo, end);
      }
      default:
        return nullptr;
    }
  }

  MNode* parseWord(std::vector<MNode*>* items) {
    const std::string w = tok.text;
    const u32 wpos = tok.pos, wend = tok.end;
    const bool call = tok.adjOpen;
    advance();
    const MathRow* row = rowOf(w);
    // a call binds only on an adjacent `name(` (design T8 S3)
    if (row && call) return parseCall(*row, wpos);
    if (row && row->bareCp) return atom(row->bareCp, row->bareCls, 0, wpos, wend);  // dot → ⋅
    if (const SymbolInfo* e = MathDict::byName(w)) {
      if (e->flags & kFlagTextOp) {
        MNode* n = mk(MNode::Text, wpos, wend);
        n->txt = w;
        n->cls = kOp;
        n->flags = e->flags;
        n->textFont = true;
        return n;
      }
      if (e->flags & kFlagLarge) return parseBigOp(e, wpos, wend);
      return atom(e->cp, e->cls, e->flags, wpos, wend);
    }
    // Single-token contexts consume only the first letter (x^ab == x^a b):
    // rewind the lexer to just past it and re-lex the remainder.
    if (w.size() > 1 && !items) {
      lex.i = wpos + 1;
      advance();
      return atom((u8)w[0], kOrd, 0, wpos, wpos + 1);
    }
    // an unknown multi-letter word (or a function name without its '(') is
    // a NAME (Typst rule): one upright text-font box with TeX's
    // \operatorname spacing (Op: thin space before an Ord, none before an
    // opening paren) — Id(A,B), Equiv, eqv, abs. Scripts bind to the whole
    // name. Single letters stay variables in the math font.
    if (w.size() > 1) {
      MNode* n = mk(MNode::Text, wpos, wend);
      n->txt = w;
      n->cls = kOp;
      n->textFont = true;
      return n;
    }
    return atom((u8)w[0], kOrd, 0, wpos, wend);
  }

  // one argument of a slot kind
  MNode* parseArg(SlotKind kind) {
    if (kind == SlotKind::Sym) {  // exactly one symbol token
      const u32 lo = tok.pos, hi = tok.end;
      MNode* n = nullptr;
      if (tok.k == Tok::Open || tok.k == Tok::Close || tok.k == Tok::Chr) n = atom(tok.cp, kOrd, 0, lo, hi);
      else if (tok.k == Tok::Op) n = atom(tok.op->cp, tok.op->cls, 0, lo, hi);
      else if (tok.k == Tok::Word)
        if (const SymbolInfo* e = MathDict::byName(tok.text)) n = atom(e->cp, e->cls, 0, lo, hi);
      if (n) advance();
      return n;
    }
    if (kind == SlotKind::Ident && tok.k == Tok::Word) {
      MNode* n = mk(MNode::Text, tok.pos, tok.end);
      n->txt = tok.text;
      advance();
      return n;
    }
    MNode* arg = mk(MNode::Run, tok.pos);
    while (!runEnds() && !atComma()) parseMolecule(arg->kids);
    arg->hi = tok.pos;
    return arg;
  }

  // name(args…): arity from the row's slots; a missing argument is an
  // empty error leaf, extra ones one error leaf after the call
  MNode* parseCall(const MathRow& row, u32 lo) {
    advance();  // '('
    MNode* call = mk(MNode::Call, lo);
    call->txt = row.name;
    call->prim = row.prim;
    std::vector<MNode*> args;
    size_t extraLo = 0;
    bool extra = false;
    for (size_t i = 0;; i++) {
      if (i == row.params.size() && !extra) {
        extra = true;
        extraLo = tok.pos;
      }
      MNode* arg = parseArg(i < row.params.size() ? row.params[i].kind : SlotKind::Content);
      if (!arg) {  // a symbol slot without a symbol: skip to the next argument
        const u32 at = tok.pos;
        const u32 end = resync();
        err(at, end > at ? end : at + 1, "'" + row.name + "' expects a symbol here");
        arg = error(at, end);
      }
      if (!extra) args.push_back(arg);
      if (atComma()) {
        advance();
        continue;
      }
      break;
    }
    const u32 extraHi = tok.pos;
    if (tok.k == Tok::Close && tok.cp == ')') advance();
    else err(lo, tok.pos, "unclosed call '" + row.name + "'");
    call->hi = tok.pos;
    for (size_t i = args.size(); i < row.params.size(); i++)
      if (!row.params[i].optional) {
        err(lo, call->hi, "'" + row.name + "' is missing its argument '" + row.params[i].name + "'", Sev::Warning,
            "math-arity");
        args.push_back(error(call->hi, call->hi));
      }
    call->kids = args;
    MNode* bound = expand(row, call);
    if (!extra) return bound;
    err((u32)extraLo, extraHi, "'" + row.name + "' takes " + std::to_string(row.params.size()) + " argument(s)",
        Sev::Warning, "math-arity");
    MNode* r = mk(MNode::Run, lo, call->hi);
    r->kids.push_back(bound);
    r->kids.push_back(error((u32)extraLo, extraHi));
    r->cls = kOrd;
    return r;
  }

  // a template row's body with its parameters replaced (a primitive row is
  // the call itself)
  MNode* expand(const MathRow& row, MNode* call) {
    if (row.prim != Prim::None || !row.body) return call;
    auto argOf = [&](const std::string& name) -> MNode* {
      for (size_t i = 0; i < row.params.size(); i++)
        if (row.params[i].name == name) return i < call->kids.size() ? call->kids[i] : nullptr;
      return nullptr;
    };
    std::function<MNode*(const MNode*)> copy = [&](const MNode* t) -> MNode* {
      if (!t) return nullptr;
      if (t->k == MNode::Param) return argOf(t->txt);
      // an argument slot holding just #x is the argument itself
      if (t->k == MNode::Run && t->kids.size() == 1 && t->kids[0]->k == MNode::Param) return argOf(t->kids[0]->txt);
      MNode* n = arena.make<MNode>();
      *n = *t;
      n->lo = call->lo;
      n->hi = call->hi;
      n->a = copy(t->a);
      n->sub = copy(t->sub);
      n->sup = copy(t->sup);
      n->b = copy(t->b);
      n->kids.clear();
      for (const MNode* k : t->kids)
        if (MNode* c = copy(k)) n->kids.push_back(c);
      return n;
    };
    MNode* out = copy(row.body);
    // the template body is a run: a single item stands for itself
    if (out && out->k == MNode::Run && out->kids.size() == 1) out = out->kids[0];
    if (!out) return call;
    if (out->k == MNode::Call) out->txt += " (" + row.name + ")";  // the family it came from (dumps)
    return out;
  }

  // big operator: optional scripts in either order, then greedy body until a
  // relation, a closing bracket, or end (v2 §13)
  MNode* parseBigOp(const SymbolInfo* e, u32 lo, u32 hi) {
    MNode* op = mk(MNode::BigOp, lo, hi);
    op->cp = e->cp;
    op->cls = kOp;
    op->flags = e->flags;
    while (tok.k == Tok::Sup || tok.k == Tok::Sub) {
      const bool isSup = tok.k == Tok::Sup;
      const u32 at = tok.pos;
      advance();
      MNode* arg = parseFactor(nullptr);
      if (!arg) {
        err(at, at + 1, "missing script argument");
        arg = error(at, at + 1);
      }
      arg = shed(arg);
      MNode*& slot = isSup ? op->sup : op->sub;
      if (slot) err(at, tok.pos, "double script");
      else slot = arg;
    }
    op->b = parseRun(/*stopAtRel=*/true);
    op->hi = tok.pos;
    return op;
  }
};

// ---- the row registry --------------------------------------------------------
struct Registry {
  std::vector<MathRow> rows;
  Arena arena;
  Registry() {
    auto prim = [&](const char* name, Prim p, std::vector<SlotSpec> params) {
      MathRow r;
      r.name = name;
      r.prim = p;
      r.params = std::move(params);
      rows.push_back(std::move(r));
    };
    using S = SlotKind;
    prim("frac", Prim::Frac, {{"num", S::Content}, {"den", S::Content}});
    prim("stack", Prim::Stack, {{"top", S::Content}, {"bottom", S::Content}});
    prim("radical", Prim::Radical, {{"radicand", S::Content}, {"index", S::Content, true}});
    prim("lr", Prim::Lr, {{"open", S::Sym}, {"body", S::Content}, {"close", S::Sym}});
    prim("accent", Prim::Accent, {{"base", S::Content}, {"mark", S::Sym}});
    prim("rule", Prim::Rule, {{"base", S::Content}, {"side", S::Ident}});
    for (const mathrows::StdRow& sr : mathrows::kStdlib) {
      MathRow r;
      std::string_view sig = sr.signature;
      const size_t open = sig.find('(');
      r.name = std::string(sig.substr(0, open));
      std::string_view ps = sig.substr(open + 1, sig.size() - open - 2);
      while (!ps.empty()) {
        const size_t comma = ps.find(',');
        std::string_view p = ps.substr(0, comma);
        while (!p.empty() && p.front() == ' ') p.remove_prefix(1);
        SlotSpec spec;
        spec.optional = !p.empty() && p.back() == '?';
        if (spec.optional) p.remove_suffix(1);
        spec.name = std::string(p);
        r.params.push_back(spec);
        ps = comma == std::string_view::npos ? std::string_view{} : ps.substr(comma + 1);
      }
      std::string_view bare = sr.bare;
      if (!bare.empty()) {
        if (const SymbolInfo* e = MathDict::byName(bare)) {
          r.bareCp = e->cp;
          r.bareCls = e->cls;
        } else {
          u32 i = 0;
          r.bareCp = utf8Next(bare, i);
          r.bareCls = MathDict::classOfCp(r.bareCp);
        }
      }
      rows.push_back(std::move(r));
    }
    // the template bodies parse once every row is known (they call rows)
    size_t t = 0;
    for (MathRow& r : rows)
      if (r.prim == Prim::None) {
        const std::string_view body = mathrows::kStdlib[t++].body;
        Parser p{Lexer{body, 0, true}, arena, {}, nullptr, &r.params, &rows};
        p.advance();
        r.body = p.parseRun();
      }
  }
};
const Registry& registry() {
  static const Registry r;
  return r;
}

void dumpNode(std::string& out, const MNode* n, int depth, const char* role = nullptr) {
  if (!n) return;
  out.append(2 + 2 * (size_t)depth, ' ');
  if (role) appendf(out, "%s: ", role);
  static const char* const kCls[] = {"ord", "op", "bin", "rel", "open", "close", "punct", "inner"};
  const char* cls = n->cls < 8 ? kCls[n->cls] : "?";
  switch (n->k) {
    case MNode::Sym: appendf(out, "sym U+%04X %s", n->cp, cls); break;
    case MNode::Num: appendf(out, "num \"%s\"", n->txt.c_str()); break;
    case MNode::Text: appendf(out, "text \"%s\" %s%s", n->txt.c_str(), cls, n->textFont ? " textfont" : ""); break;
    case MNode::Run: out += "run"; break;
    case MNode::Attach: out += "attach"; break;
    case MNode::Frac: out += "frac"; break;
    case MNode::Group: appendf(out, "group U+%04X U+%04X", n->openCp, n->closeCp); break;
    case MNode::BigOp: appendf(out, "bigop U+%04X%s", n->cp, (n->flags & kFlagLimits) ? " limits" : ""); break;
    case MNode::Call: appendf(out, "call %s", n->txt.c_str()); break;
    case MNode::Param: appendf(out, "param #%s", n->txt.c_str()); break;
    case MNode::Error: out += "error \"" + n->txt + "\""; break;
  }
  if (n->k != MNode::Run || n->hi > n->lo) appendf(out, " [%u,%u)", n->lo, n->hi);
  out += "\n";
  dumpNode(out, n->a, depth + 1, n->k == MNode::Frac ? "num" : n->k == MNode::Group ? nullptr : "base");
  dumpNode(out, n->sub, depth + 1, "sub");
  dumpNode(out, n->sup, depth + 1, "sup");
  dumpNode(out, n->b, depth + 1, n->k == MNode::Frac ? "den" : "body");
  for (const MNode* k : n->kids) dumpNode(out, k, depth + 1);
}

}  // namespace

const std::vector<MathRow>& mathRows() { return registry().rows; }

const MathRow* mathRow(std::string_view name) {
  for (const MathRow& r : registry().rows)
    if (r.name == name) return &r;
  return nullptr;
}

bool checkRow(const MathRow& row, std::string& why) {
  for (size_t i = 0; i < row.params.size(); i++)
    for (size_t j = i + 1; j < row.params.size(); j++)
      if (row.params[i].name == row.params[j].name) {
        why = row.name + ": parameter '" + row.params[i].name + "' twice";
        return false;
      }
  if (row.prim != Prim::None) return true;
  if (!row.body) {
    why = row.name + ": no body";
    return false;
  }
  // the body: every parameter leaf is declared, every call is a known row
  // with its arity, nothing is an error leaf
  std::function<bool(const MNode*)> ok = [&](const MNode* n) -> bool {
    if (!n) return true;
    if (n->k == MNode::Error) {
      why = row.name + ": the body does not parse at \"" + n->txt + "\"";
      return false;
    }
    if (n->k == MNode::Param) {
      bool known = false;
      for (const SlotSpec& p : row.params) known = known || p.name == n->txt;
      if (!known) {
        why = row.name + ": unknown parameter #" + n->txt;
        return false;
      }
    }
    return ok(n->a) && ok(n->sub) && ok(n->sup) && ok(n->b) &&
           std::all_of(n->kids.begin(), n->kids.end(), [&](const MNode* k) { return ok(k); });
  };
  return ok(row.body);
}

MathIR parseMath(std::string_view src, Arena& arena) {
  MathIR ir;
  Parser p{Lexer{src}, arena, {}, &ir.diags};
  p.advance();
  ir.root = p.parseRun();
  // a stray closing bracket at the top: an error leaf, then go on
  while (p.tok.k != Tok::End) {
    const u32 at = p.tok.pos;
    p.err(at, at + 1, "unexpected closing bracket");
    ir.root->kids.push_back(p.error(at, at + 1));
    p.advance();
    MNode* more = p.parseRun();
    for (MNode* k : more->kids) ir.root->kids.push_back(k);
  }
  ir.root->lo = 0;
  ir.root->hi = (u32)src.size();
  return ir;
}

void reportMathDiags(const MathIR& ir, std::string_view src, Span span, DiagSink& diags) {
  // the source sits inside its delimiters ($…$, $ … $): map bytes into the
  // formula's span when the delimiters are the usual ones, else the span
  const u32 extra = span.end > span.start ? span.end - span.start - (u32)src.size() : 0;
  const bool mapped = extra > 0 && extra <= 4 && extra % 2 == 0;
  const u32 off = span.start + extra / 2;
  size_t n = 0;
  for (const MathDiag& d : ir.diags) {
    if (n++ == 8) {
      diags.add(Sev::Info, "math-parse", span, std::to_string(ir.diags.size() - 8) + " more problem(s) in this formula");
      break;
    }
    const Span sub = mapped ? Span{off + d.lo, off + (d.hi > d.lo ? d.hi : d.lo + 1)} : span;
    diags.add(d.sev, d.code, sub, d.msg);
  }
}

std::string dumpMathIR(const MathIR& ir, std::string_view src) {
  (void)src;
  std::string out;
  dumpNode(out, ir.root, 0);
  for (const MathDiag& d : ir.diags)
    appendf(out, "  diag %s %s [%u,%u) %s\n", d.sev == Sev::Error ? "error" : d.sev == Sev::Warning ? "warning" : "info",
            d.code, d.lo, d.hi, d.msg.c_str());
  return out;
}

}  // namespace tsr
