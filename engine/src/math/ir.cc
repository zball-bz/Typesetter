#include "ir.h"

#include <algorithm>
#include <cstdio>
#include <functional>

#include "../../gen/math_rows.h"
#include "dict.h"
#include "env.h"

namespace tsr {

namespace {

// ---- tokenizer -------------------------------------------------------------
struct Tok {
  // Hole / TextHole / ErrorHole (plan P2-15): a formula's hole units
  enum K : u8 { End, Num, Word, Op, Chr, Sup, Sub, Slash, Open, Close, Prime, Quote, Param, Hole, TextHole,
                ErrorHole } k = End;
  std::string text;                // Num/Word/Quote/Param
  const SymbolInfo* op = nullptr;  // Op (dictionary hit)
  u32 cp = 0;                      // Chr (direct char) / Open / Close
  u8 cls = kOrd;                   // Chr fallback class
  u32 pos = 0, end = 0;            // the token's bytes
  bool adjOpen = false;            // Word: '(' follows with no space (a call)
};

inline bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

struct Lexer {
  std::string_view s;
  u32 i = 0;
  bool templ = false;  // a stdlib template: #name is a parameter
  const MathScope* scope = nullptr;  // the document's declarations (plan P2-15)

  // a dotted name is one word when it names something: a declared row, or
  // a built-in reached as std.name (plan P2-15; `.` is otherwise a
  // decimal point or punctuation, D-L13)
  bool known(std::string_view name) const {
    if (name.rfind("std.", 0) == 0) {
      const std::string_view base = name.substr(4);
      return base.find('.') == std::string_view::npos && (mathRow(base) || MathDict::byName(base));
    }
    return scope && scope->env && scope->env->find(name, scope->epoch);
  }
  // the end of the delimited unit opening at i (its closer's position, or
  // the end of the source)
  u32 unitEnd(char open, char close) const {
    int depth = 0;
    for (u32 j = i; j < s.size(); j++) {
      if (s[j] == open) depth++;
      else if (s[j] == close && --depth == 0) return j;
    }
    return (u32)s.size();
  }

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
    if (c == '\x01' || c == '\x03' || c == '\x05') {  // a hole unit (plan P2-15)
      const char close = c == '\x01' ? '\x02' : c == '\x03' ? '\x04' : '\x06';
      const u32 e = unitEnd(c, close);
      t.k = c == '\x01' ? Tok::Hole : c == '\x03' ? Tok::TextHole : Tok::ErrorHole;
      t.text = std::string(s.substr(i + 1, e - (i + 1)));
      i = e < s.size() ? e + 1 : e;
      return t;
    }
    if (c == '\\' && i + 1 < s.size() && (s[i + 1] == '$' || s[i + 1] == '#')) {  // \$ and \#: themselves
      t.k = Tok::Chr;
      t.cp = (u8)s[i + 1];
      t.cls = kOrd;
      i += 2;
      return t;
    }
    if (isLetter(c)) {
      u32 j = i;
      while (j < s.size() && isLetter(s[j])) j++;
      // a dotted name (std.frac, a declared arrow.long): the longest that names something
      for (u32 k = j, best = j; k < s.size() && s[k] == '.' && k + 1 < s.size() && isLetter(s[k + 1]);) {
        k++;
        while (k < s.size() && isLetter(s[k])) k++;
        if (known(s.substr(i, k - i))) best = k;
        j = best;
      }
      t.k = Tok::Word;
      t.text = std::string(s.substr(i, j - i));
      t.adjOpen = j < s.size() && s[j] == '(';
      i = j;
      // (plan P3-24) a name of an opening or closing delimiter (langle,
      // rceil) opens or closes a group as its character does — unless the
      // document's declarations or a row of that name say otherwise
      // (a stdlib template parses while the rows are being built: no row is a delimiter's name)
      if (!(scope && scope->env && scope->env->find(t.text, scope->epoch)) && (templ || !mathRow(t.text)))
        if (const SymbolInfo* e = MathDict::byName(t.text); e && (e->cls == kOpen || e->cls == kClose)) {
          t.k = e->cls == kOpen ? Tok::Open : Tok::Close;
          t.cp = e->cp;
        }
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
      i++;
      t.k = Tok::Sub;
      return t;
    }
    // (a `!` is a character: before a relation the parser negates it, plan
    // P3-24 — != is ≠, !in is ∉ — else it is a factorial)
    if (MathDict::isOpChar(c)) {
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
    // a direct Unicode character: a declaration that claims it (plan
    // P2-15), else its default dictionary row — the same symbol as its name
    // (plan P3-24: ∑ is sum, ≤ a relation, ⟨ an opening delimiter)
    u32 cp = utf8Next(s, i);
    t.k = Tok::Chr;
    t.cp = cp;
    t.cls = MathDict::classOfCp(cp);
    if (scope && scope->env)
      if (const MathDeclRow* r = scope->env->claimed(cp, scope->epoch)) {
        t.cls = r->cls;
        return t;
      }
    if (const SymbolInfo* e = MathDict::byCp(cp)) {
      if (e->cls == kOpen || e->cls == kClose) {
        t.k = e->cls == kOpen ? Tok::Open : Tok::Close;
      } else {
        t.k = Tok::Op;
        t.op = e;
      }
    }
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
  const MathScope* scope = nullptr;               // the document's declarations (plan P2-15)

  // a declared row of a name in force (std.-qualified names skip them)
  const MathDeclRow* declared(std::string_view name) const {
    if (!scope || !scope->env || name.rfind("std.", 0) == 0) return nullptr;
    return scope->env->find(name, scope->epoch);
  }
  // a built-in's spelling: std.name is name
  static std::string_view builtin(std::string_view name) {
    return name.rfind("std.", 0) == 0 ? name.substr(4) : name;
  }
  const MathRow* rowOf(std::string_view name) const {
    if (const MathDeclRow* d = declared(name)) return d->k == MathDeclRow::Fn ? &d->row : nullptr;
    name = builtin(name);
    if (!rows) return mathRow(name);
    for (const MathRow& r : *rows)
      if (r.name == name) return &r;
    return nullptr;
  }
  // a symbol name: a declared symbol, else the dictionary's
  bool symbolOf(std::string_view name, u32& cp, u8& cls) const {
    if (const MathDeclRow* d = declared(name)) {
      if (d->k != MathDeclRow::Symbol) return false;
      cp = d->cp;
      cls = d->cls;
      return true;
    }
    if (const SymbolInfo* e = MathDict::byName(builtin(name))) {
      cp = e->cp;
      cls = e->cls;
      return true;
    }
    return false;
  }

  // a hole's math value (plan P2-15): the source between its delimiters,
  // parsed on its own — its brackets and names cannot reach the formula
  MNode* isolated(u32 a, u32 b) {
    Parser sub{Lexer{lex.s.substr(0, b), a, false, lex.scope}, arena, {}, diags, nullptr, rows, scope};
    sub.depth = depth;
    sub.advance();
    MNode* run = sub.parseRun();
    while (sub.tok.k != Tok::End) {
      const u32 at = sub.tok.pos;
      sub.err(at, at + 1, "unexpected closing bracket");
      run->kids.push_back(sub.error(at, at + 1));
      sub.advance();
      MNode* more = sub.parseRun();
      for (MNode* k : more->kids) run->kids.push_back(k);
    }
    run->lo = a;
    run->hi = b;
    return run;
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
  // a relation ahead: an operator or a typed or claimed character of class
  // Rel (plan P3-24: predicates read the symbol, not the token kind), or a
  // `!` negating one
  bool atRel() const {
    if (tok.k == Tok::Op) return tok.op->cls == kRel;
    if (tok.k != Tok::Chr) return false;
    if (tok.cls == kRel) return true;
    u32 cp = 0, end = 0;
    u8 cls = kOrd;
    return tok.cp == '!' && negatable(cp, cls, end) && cls == kRel;
  }
  // (plan P3-24; the `!` rule) the symbol a `!` touches — an operator, a
  // typed or named symbol: its code point and class; false when the next
  // token is none of these or stands apart
  bool negatable(u32& cp, u8& cls, u32& end) const {
    Lexer la = lex;
    const Tok nt = la.next();
    if (nt.pos != tok.end) return false;
    end = nt.end;
    switch (nt.k) {
      case Tok::Op:
        cp = nt.op->cp;
        cls = nt.op->cls;
        return cp != 0;
      case Tok::Chr:
        cp = nt.cp;
        cls = nt.cls;
        return true;
      case Tok::Word: return symbolOf(nt.text, cp, cls);
      default: return false;
    }
  }
  // whether the `!` at hand negates: a symbol with a negation (the UCD's,
  // | → ∤, ‖ → ∦), or a relation without one (an error)
  bool negates() const {
    u32 cp = 0, end = 0;
    u8 cls = kOrd;
    return tok.k == Tok::Chr && tok.cp == '!' && negatable(cp, cls, end) && (MathDict::negate(cp) || cls == kRel);
  }
  // a `!` that negates: the negated symbol (!= is ≠, !in ∉), an error leaf
  // when the relation has no negation
  MNode* negation() {
    u32 cp = 0, end = 0;
    u8 cls = kOrd;
    if (!negates() || !negatable(cp, cls, end)) return nullptr;
    const u32 lo = tok.pos;
    const u32 neg = MathDict::negate(cp);
    advance();  // the '!'
    advance();  // the symbol
    if (!neg) {
      err(lo, end, "'" + std::string(lex.s.substr(lo + 1, end - lo - 1)) + "' has no negation");
      return error(lo, end);
    }
    return atom(neg, MathDict::classOfCp(neg), 0, lo, end);
  }
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
  MNode* parseRun() {
    MNode* run = mk(MNode::Run, tok.pos);
    while (!runEnds()) parseMolecule(run->kids);
    run->hi = tok.pos;
    scopes(run);
    return run;
  }
  // (plan P3-25) each large operator's scope: up to the next relation of the
  // run (a typed one too), or its end — an annotation, not a node
  static void scopes(MNode* run) {
    for (size_t i = 0; i < run->kids.size(); i++) {
      MNode* k = run->kids[i];
      MNode* op = k->k == MNode::Attach ? k->a : k;
      if (!op || op->k != MNode::Sym || !(op->flags & kFlagLarge)) continue;
      op->scopeEnd = run->hi;
      for (size_t j = i + 1; j < run->kids.size(); j++)
        if (run->kids[j]->cls == kRel) {
          op->scopeEnd = run->kids[j]->lo;
          break;
        }
    }
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

  // a group consumed as a script, fraction or argument operand sheds its
  // parentheses — only those (D-M01, plan P3-24): {…} and […] stay visible
  MNode* shed(MNode* f) { return f->k == MNode::Group && f->openCp == '(' && f->closeCp == ')' ? f->a : f; }

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
        if (f->k != MNode::Attach) {
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
      if (tok.k == Tok::Chr && tok.cp == '!' && !negates()) {
        // postfix factorial: fold into the base so fractions/scripts see n!
        // as one atom (a `!` touching a relation negates it instead: !=)
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
        if (f->k != MNode::Attach) {
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
      case Tok::Quote:
      case Tok::TextHole: {  // a string hole is quoted text (plan P2-15)
        MNode* n = mk(MNode::Text, lo, hi);
        n->txt = tok.text;
        n->cls = kOrd;
        n->textFont = true;
        advance();
        return n;
      }
      case Tok::ErrorHole: {  // a failed hole: its message, set as text
        MNode* n = mk(MNode::Error, lo, hi);
        n->txt = "\xE2\x9A\xA0 " + tok.text;
        n->textFont = true;
        advance();
        return n;
      }
      case Tok::Hole: {
        // a math hole: one unit as an operand or a script's base; in a run
        // otherwise its atoms join the run (TeX macro semantics)
        advance();
        MNode* run = isolated(lo + 1, hi > lo + 1 ? hi - 1 : lo + 1);
        if (items && tok.k != Tok::Sup && tok.k != Tok::Sub && tok.k != Tok::Prime && tok.k != Tok::Slash &&
            !run->kids.empty()) {
          for (size_t i = 0; i + 1 < run->kids.size(); i++) items->push_back(run->kids[i]);
          return run->kids.back();
        }
        return run;
      }
      case Tok::Op: {
        const SymbolInfo* e = tok.op;
        advance();
        return atom(e->cp, e->cls, e->flags, lo, hi);  // (a large one too: an Op atom, plan P3-25)
      }
      case Tok::Chr: {
        if (MNode* neg = negation()) return neg;
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
        midOf(inner);
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

  // (plan P3-24) a fence alone in its group — one | or ‖ among its atoms —
  // is its middle: stretched with the group and spaced as a relation
  // ({x | x > 0}, P(A | B)); two of them (|x|, ‖v‖) stay as they are
  static void midOf(MNode* run) {
    MNode* lone = nullptr;
    int fences = 0;
    for (MNode* k : run->kids)
      if (k->k == MNode::Sym && (k->flags & kFlagFence)) {
        fences++;
        lone = k;
      }
    if (fences == 1) {
      lone->mid = true;
      lone->cls = kRel;
    }
  }

  MNode* parseWord(std::vector<MNode*>* items) {
    const std::string w = tok.text;
    const u32 wpos = tok.pos, wend = tok.end;
    const bool call = tok.adjOpen;
    advance();
    // a declared symbol or operator (plan P2-15): its row, as of the epoch
    if (const MathDeclRow* d = declared(w)) {
      if (d->k == MathDeclRow::Symbol) return atom(d->cp, d->cls, 0, wpos, wend);
      if (d->k == MathDeclRow::Op) {
        MNode* n = mk(MNode::Text, wpos, wend);
        n->txt = w;
        n->cls = kOp;
        n->flags = d->flags;
        n->textFont = true;
        return n;
      }
    }
    const MathRow* row = rowOf(w);
    // a call binds only on an adjacent `name(` (design T8 S3)
    if (row && call) return parseCall(*row, wpos);
    if (row && row->bareCp) return atom(row->bareCp, row->bareCls, 0, wpos, wend);  // dot → ⋅
    // (plan P3-24) a row without parameters is a constant: thin, quad
    if (row && row->params.empty() && row->prim == Prim::None && row->body) {
      MNode* c = mk(MNode::Call, wpos, wend);
      c->txt = row->name;
      return bind(expand(*row, c));
    }
    if (const SymbolInfo* e = MathDict::byName(builtin(w))) {
      // (plan P3-25; design T8 D-M04) a relation or operator name as a
      // script (x_in) is that symbol — likely meant as letters: it says so
      if (!items && w.size() > 1 && (e->cls == kRel || e->cls == kBin) && !lex.templ)
        err(wpos, wend, "'" + w + "' as a script is a symbol, not letters (quote it for the letters)", Sev::Info,
            "math-implicit-name");
      if (e->flags & kFlagTextOp) {
        MNode* n = mk(MNode::Text, wpos, wend);
        n->txt = w;
        n->cls = kOp;
        n->flags = e->flags;
        n->textFont = true;
        return n;
      }
      return atom(e->cp, e->cls, e->flags, wpos, wend);
    }
    // (plan P3-25; design T8 the single-token operand rule) an unknown word
    // as an operand — a script, a fraction's numerator or denominator — is
    // its letters, never the dictionary's say: x^ab = x^{ab}, a/bc = a/(bc),
    // ab/c is italic ab over c
    if (w.size() > 1 && (!items || tok.k == Tok::Slash)) {
      MNode* run = mk(MNode::Run, wpos, wend);
      for (u32 i = 0; i < w.size(); i++) run->kids.push_back(atom((u8)w[i], kOrd, 0, wpos + i, wpos + i + 1));
      return run;
    }
    // an unknown multi-letter word (or a function name without its '(') is
    // a NAME (Typst rule): one upright text-font box with TeX's
    // \operatorname spacing (Op: thin space before an Ord, none before an
    // opening paren) — Id(A,B), Equiv, eqv, abs. Scripts bind to the whole
    // name. Single letters stay variables in the math font. (Plan P3-24,
    // D-M04: it says so, as info — a converter's slip, a function nobody
    // declared, without interrupting the writing.)
    if (w.size() > 1) {
      if (!row && !lex.templ)
        err(wpos, wend, "'" + w + "' is no known name: set upright as an operator name (declare it with $.math.op, or quote it)",
            Sev::Info, "math-implicit-name");
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
      else if (tok.k == Tok::Word) {
        u32 cp;
        u8 cls;
        if (symbolOf(tok.text, cp, cls)) n = atom(cp, cls, 0, lo, hi);
      }
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
    if (row.prim == Prim::Lr && call->kids.size() > 1 && call->kids[1]->k == MNode::Run) midOf(call->kids[1]);
    MNode* bound = expand(row, call);
    if (!lex.templ) bound = bind(bound);  // (a template's prims bind when it is called)
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

  // (plan P3-24) the bind-time rewrites over a bound call's tree: an
  // alphabet maps its letters and digits, limits/scripts set where an
  // operator's scripts go, a class sets the atom class; their argument is
  // checked here (an unknown one is an error)
  static std::string_view identOf(const MNode* n) {
    if (n && n->k == MNode::Text) return n->txt;
    if (n && n->k == MNode::Run && n->kids.size() == 1 && n->kids[0]->k == MNode::Text) return n->kids[0]->txt;
    return {};
  }
  static void mapLetters(MNode* n, int alphabet, Arena& arena) {
    if (!n) return;
    switch (n->k) {
      case MNode::Sym: n->cp = MathDict::variant(alphabet, n->cp); return;
      case MNode::Num:
      case MNode::Text: {
        if (n->k == MNode::Text && (n->flags & kFlagTextOp)) return;  // sin, lim: operators keep their face
        std::vector<MNode*> kids;
        for (u32 i = 0; i < n->txt.size();) {
          const u32 cp = utf8Next(n->txt, i);
          MNode* k = arena.make<MNode>();
          k->k = MNode::Sym;
          k->cp = MathDict::variant(alphabet, cp);
          k->cls = MathDict::classOfCp(cp);
          k->lo = n->lo;
          k->hi = n->hi;
          kids.push_back(k);
        }
        n->k = MNode::Run;
        n->txt.clear();
        n->textFont = false;
        n->kids = std::move(kids);
        return;
      }
      default:
        for (MNode* k : {n->a, n->b, n->sub, n->sup}) mapLetters(k, alphabet, arena);
        for (MNode* k : n->kids) mapLetters(k, alphabet, arena);
        return;
    }
  }
  MNode* bind(MNode* n) {
    if (!n) return n;
    for (MNode** k : {&n->a, &n->b, &n->sub, &n->sup}) *k = bind(*k);
    for (MNode*& k : n->kids) k = bind(k);
    if (n->k != MNode::Call) return n;
    auto bad = [&](std::string_view what, std::string_view v) {
      err(n->lo, n->hi, "'" + n->txt + "': no " + std::string(what) + " '" + std::string(v) + "'");
    };
    MNode* body = n->kids.empty() ? nullptr : n->kids[0];
    switch (n->prim) {
      case Prim::Variant: {
        const std::string_view a = n->kids.size() > 1 ? identOf(n->kids[1]) : std::string_view{};
        const int alphabet = MathDict::alphabet(a);
        if (alphabet < 0) bad("alphabet", a);
        else mapLetters(body, alphabet, arena);
        return body ? body : n;
      }
      case Prim::Limits: {
        const std::string_view m = n->kids.size() > 1 ? identOf(n->kids[1]) : std::string_view{};
        MNode* op = body && body->k == MNode::Run && body->kids.size() == 1 ? body->kids[0] : body;
        if (m != "limits" && m != "scripts") bad("mode", m);
        else if (op && m == "limits") op->flags |= kFlagLimitsAlways | kFlagLimits;
        else if (op) op->flags &= (u8)~(kFlagLimitsAlways | kFlagLimits);
        return body ? body : n;
      }
      case Prim::Class: {
        static constexpr std::string_view kNames[] = {"ord", "op", "bin", "rel", "open", "close", "punct", "inner"};
        const std::string_view c = identOf(n->kids.empty() ? nullptr : n->kids[0]);
        const auto at = std::find(std::begin(kNames), std::end(kNames), c);
        if (at == std::end(kNames)) bad("class", c);
        else n->cls = (u8)(at - std::begin(kNames));
        return n;
      }
      case Prim::Style: {
        const std::string_view st = n->kids.size() > 1 ? identOf(n->kids[1]) : std::string_view{};
        if (st != "display" && st != "text" && st != "script" && st != "sscript") bad("style", st);
        return n;
      }
      default: return n;
    }
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
    // (plan P3-24) spacing, style and the bind-time rewrites (stdlib.tsv
    // rows name them: thin, display, limits, bb, …; class is called as is)
    prim("space", Prim::Space, {{"mu", S::Content}});
    prim("mstyle", Prim::Style, {{"body", S::Content}, {"style", S::Ident}});
    prim("mlimits", Prim::Limits, {{"body", S::Content}, {"mode", S::Ident}});
    prim("variant", Prim::Variant, {{"body", S::Content}, {"alphabet", S::Ident}});
    prim("class", Prim::Class, {{"class", S::Ident}, {"body", S::Content}});
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
    case MNode::Sym:
      appendf(out, "sym U+%04X %s", n->cp, cls);
      if (n->flags & kFlagLarge) appendf(out, " large%s scope→%u", (n->flags & kFlagLimits) ? " limits" : "", n->scopeEnd);
      if (n->mid) out += " mid";
      break;
    case MNode::Num: appendf(out, "num \"%s\"", n->txt.c_str()); break;
    case MNode::Text: appendf(out, "text \"%s\" %s%s", n->txt.c_str(), cls, n->textFont ? " textfont" : ""); break;
    case MNode::Run: out += "run"; break;
    case MNode::Attach: out += "attach"; break;
    case MNode::Frac: out += "frac"; break;
    case MNode::Group: appendf(out, "group U+%04X U+%04X", n->openCp, n->closeCp); break;
    case MNode::Call: appendf(out, "call %s", n->txt.c_str()); break;
    case MNode::Param: appendf(out, "param #%s", n->txt.c_str()); break;
    case MNode::Error: out += "error \"" + n->txt + "\""; break;
  }
  if (n->k != MNode::Run || n->hi > n->lo) appendf(out, " [%u,%u)", n->lo, n->hi);
  out += "\n";
  dumpNode(out, n->a, depth + 1, n->k == MNode::Frac ? "num" : n->k == MNode::Group ? nullptr : "base");
  dumpNode(out, n->sub, depth + 1, "sub");
  dumpNode(out, n->sup, depth + 1, "sup");
  dumpNode(out, n->b, depth + 1, "den");
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

MNode* parseTemplateBody(std::string_view body, const std::vector<SlotSpec>& params, Arena& arena,
                         const MathScope* scope, std::vector<MathDiag>* diags) {
  Parser p{Lexer{body, 0, true, scope}, arena, {}, diags, &params, nullptr, scope};
  p.advance();
  return p.parseRun();
}

MathIR parseMath(std::string_view src, Arena& arena, const MathScope* scope) {
  MathIR ir;
  Parser p{Lexer{src, 0, false, scope}, arena, {}, &ir.diags, nullptr, nullptr, scope};
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

void reportMathDiags(const MathIR& ir, std::string_view src, Span span, DiagSink& diags, const std::vector<u32>* map) {
  // the source sits inside its delimiters ($…$, $ … $): map bytes into the
  // formula's span when the delimiters are the usual ones, else the span;
  // a fragmented formula (plan P2-15) maps through its fragments' spans
  const u32 extra = span.end > span.start && span.end - span.start >= (u32)src.size()
                        ? span.end - span.start - (u32)src.size() : 0;
  const bool mapped = extra > 0 && extra <= 4 && extra % 2 == 0;
  const u32 off = span.start + extra / 2;
  auto at = [&](u32 x) -> u32 {
    size_t k = 0;
    for (size_t i = 0; i + 2 < map->size(); i += 3)
      if ((*map)[i] <= x) k = i;
    const u32 p = (*map)[k + 1] + (x - (*map)[k]);
    return std::min(p, (*map)[k + 2]);
  };
  size_t n = 0;
  for (const MathDiag& d : ir.diags) {
    if (n++ == 8) {
      diags.add(Sev::Info, "math-parse", span, std::to_string(ir.diags.size() - 8) + " more problem(s) in this formula");
      break;
    }
    Span sub = span;
    if (map && !map->empty()) {
      sub = {at(d.lo), at(d.hi > d.lo ? d.hi : d.lo + 1)};
      if (sub.end <= sub.start) sub.end = sub.start + 1;
    } else if (mapped) {
      sub = {off + d.lo, off + (d.hi > d.lo ? d.hi : d.lo + 1)};
    }
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
