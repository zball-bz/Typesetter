// Math parser + box layout (math-design.md §5–§7).
// Lineage: OpenType MATH constants drive every construct (Typst crosswalk);
// the TeXbook supplies what the font cannot: the inter-atom spacing matrix,
// bin→ord demotion (Rules 5–6), and the style-transition algebra.
#include "math.h"

#include "mathfont.h"

namespace tsr {

namespace {

using namespace mathfont;

std::string cpToUtf8(u32 cp) {
  std::string s;
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) {
    s += (char)(0xC0 | (cp >> 6));
    s += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += (char)(0xE0 | (cp >> 12));
    s += (char)(0x80 | ((cp >> 6) & 0x3F));
    s += (char)(0x80 | (cp & 0x3F));
  } else {
    s += (char)(0xF0 | (cp >> 18));
    s += (char)(0x80 | ((cp >> 12) & 0x3F));
    s += (char)(0x80 | ((cp >> 6) & 0x3F));
    s += (char)(0x80 | (cp & 0x3F));
  }
  return s;
}

// ---- style algebra (TeXbook; KaTeX Style.ts encoding) ----------------------
enum : u8 { D = 0, Dc, T, Tc, S, Sc, SS, SSc };
constexpr u8 kSupStyle[8] = {S, Sc, S, Sc, SS, SSc, SS, SSc};
constexpr u8 kSubStyle[8] = {Sc, Sc, Sc, Sc, SSc, SSc, SSc, SSc};
constexpr u8 kNumStyle[8] = {T, Tc, S, Sc, SS, SSc, SS, SSc};
constexpr u8 kDenStyle[8] = {Tc, Tc, Sc, Sc, SSc, SSc, SSc, SSc};
inline bool isCramped(u8 st) { return st & 1; }
inline bool isScriptStyle(u8 st) { return st >= S; }
inline bool isDisplay(u8 st) { return st <= Dc; }
inline double styleScale(u8 st) {
  if (st >= SS) return mathConst(C::ScriptScriptPercentScaleDown) / 100.0;
  if (st >= S) return mathConst(C::ScriptPercentScaleDown) / 100.0;
  return 1.0;
}

// ---- inter-atom glue (TeXbook p.170; KaTeX spacingData) --------------------
// value&3: 0 none / 1 thin(3mu) / 2 med(4mu) / 3 thick(5mu);
// value&4: suppressed in script/scriptscript styles (the parenthesized set).
constexpr u8 kSpaceTab[8][8] = {
    //         Ord Op Bin Rel Open Close Punct Inner
    /*Ord*/   {0, 1, 6, 7, 0, 0, 0, 5},
    /*Op*/    {1, 1, 0, 7, 0, 0, 0, 5},
    /*Bin*/   {6, 6, 0, 0, 6, 0, 0, 6},
    /*Rel*/   {7, 7, 0, 0, 7, 0, 0, 7},
    /*Open*/  {0, 0, 0, 0, 0, 0, 0, 0},
    /*Close*/ {0, 1, 6, 7, 0, 0, 0, 5},
    /*Punct*/ {5, 5, 0, 5, 5, 5, 5, 5},
    /*Inner*/ {5, 1, 6, 7, 5, 0, 5, 5},
};
constexpr int kMuOf[4] = {0, 3, 4, 5};

// ---- parse tree ------------------------------------------------------------
struct MNode {
  enum K : u8 { Run, Atom, Text, Script, Frac, Group, BigOp, Call } k = Atom;
  u32 cp = 0;
  u8 cls = kOrd, flags = 0;
  std::string txt;            // Text: literal glyph run; Call: function name
  MNode* a = nullptr;         // Script/BigOp: base; Frac: numerator
  MNode* sub = nullptr;       // Script/BigOp
  MNode* sup = nullptr;       // Script/BigOp
  MNode* b = nullptr;         // Frac: denominator; BigOp: body
  u32 openCp = 0, closeCp = 0;  // Group
  std::vector<MNode*> kids;   // Run; Call: arguments
};

// ---- tokenizer -------------------------------------------------------------
struct Tok {
  enum K : u8 { End, Num, Word, Op, Chr, Sup, Sub, Slash, Open, Close, Prime } k = End;
  std::string text;               // Num/Word
  const OpEntry* op = nullptr;    // Op (dictionary hit)
  u32 cp = 0;                     // Chr (direct char) / Open / Close
  u8 cls = kOrd;                  // Chr fallback class
  u32 pos = 0;                    // token start offset (for re-lexing)
};

inline bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
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

  Tok next() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i >= s.size()) return {};
    char c = s[i];
    Tok t;
    t.pos = i;
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
      i = j;
      return t;
    }
    switch (c) {
      case '^': i++; t.k = Tok::Sup; return t;
      case '_':
        // _|_ is ⊥ — let the munch see it first
        break;
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
      // maximal munch may claim _|_ ; otherwise structural subscript
      if (i + 2 < s.size() && s[i + 1] == '|' && s[i + 2] == '_') {
        if (const OpEntry* e = mathOp("_|_")) {
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
      std::string name(s.substr(i, j - i));
      if (const OpEntry* e = mathOp(name)) {
        i = j;
        t.k = Tok::Op;
        t.op = e;
        return t;
      }
      // unknown negation: emit '!' alone, the word lexes next round
      i++;
      t.k = Tok::Chr;
      t.cp = '!';
      t.cls = kOrd;
      return t;
    }
    if (isOpChar(c)) {
      u32 runEnd = i;
      while (runEnd < s.size() && isOpChar(s[runEnd]) && runEnd - i < 4) runEnd++;
      for (u32 len = runEnd - i; len >= 1; len--) {
        std::string cand(s.substr(i, len));
        if (const OpEntry* e = mathOp(cand)) {
          i += len;
          t.k = Tok::Op;
          t.op = e;
          return t;
        }
      }
      i++;
      t.k = Tok::Chr;
      t.cp = (u8)c;
      t.cls = kOrd;
      return t;
    }
    // direct Unicode character: class from the dictionary if any entry
    // names this codepoint, Ord otherwise
    u32 cp = utf8Next(s, i);
    t.k = Tok::Chr;
    t.cp = cp;
    t.cls = kOrd;
    for (int k = 0; k < kOpCount; k++) {
      if (kOps[k].cp == cp && !(kOps[k].flags & kFlagAccent)) {
        t.cls = kOps[k].cls;
        break;
      }
    }
    return t;
  }
};

// structural function names (call-syntax constructs beyond the dictionary)
inline bool isCallName(const std::string& w) {
  return w == "sqrt" || w == "root" || w == "frac" || w == "binom" ||
         w == "abs" || w == "norm" || w == "floor" || w == "ceil";
}

// ---- parser ----------------------------------------------------------------
struct Parser {
  Lexer lex;
  Arena& arena;
  DiagSink& diags;
  Span span;
  Tok tok;
  int errors = 0;

  void advance() { tok = lex.next(); }
  void err(const char* msg) {
    if (errors++ == 0) diags.add(Sev::Error, "math-parse", span, msg);
  }

  MNode* mk(MNode::K k) {
    MNode* n = arena.make<MNode>();
    n->k = k;
    return n;
  }
  MNode* atom(u32 cp, u8 cls, u8 flags = 0) {
    MNode* n = mk(MNode::Atom);
    n->cp = cp;
    n->cls = cls;
    n->flags = flags;
    return n;
  }

  bool atRel() const { return tok.k == Tok::Op && tok.op->cls == kRel; }
  bool runEnds() const { return tok.k == Tok::End || tok.k == Tok::Close; }

  // one full expression run; stops at End / Close (caller consumes)
  MNode* parseRun(bool stopAtRel = false) {
    MNode* run = mk(MNode::Run);
    while (!runEnds()) {
      if (stopAtRel && atRel()) break;
      parseMolecule(run->kids);
    }
    return run;
  }

  // factor + postfix scripts/primes + fraction chaining; visible unscripted
  // groups splice their atoms into the run (TeX: '(' is an Open atom)
  void parseMolecule(std::vector<MNode*>& items) {
    MNode* f = parseFactor(&items);
    if (!f) return;
    f = attachPostfix(f);
    while (tok.k == Tok::Slash) {
      advance();
      MNode* rhs = parseFactor(nullptr, true);
      if (!rhs) {
        err("missing denominator");
        break;
      }
      rhs = attachPostfix(rhs, /*allowFraction=*/false);
      MNode* fr = mk(MNode::Frac);
      fr->a = shed(f);
      fr->b = shed(rhs);
      fr->cls = kOrd;
      f = fr;
    }
    items.push_back(f);
  }

  // group consumed as a script/fraction argument sheds its parens
  MNode* shed(MNode* f) { return f->k == MNode::Group ? f->a : f; }

  MNode* attachPostfix(MNode* f, bool allowFraction = true) {
    (void)allowFraction;
    for (;;) {
      if (tok.k == Tok::Sup || tok.k == Tok::Sub) {
        bool isSup = tok.k == Tok::Sup;
        advance();
        MNode* arg = parseFactor(nullptr, true);
        if (!arg) {
          err("missing script argument");
          return f;
        }
        arg = shed(arg);
        if (f->k != MNode::Script && f->k != MNode::BigOp) {
          MNode* sc = mk(MNode::Script);
          sc->a = f;
          sc->cls = f->cls;
          f = sc;
        }
        MNode*& slot = isSup ? f->sup : f->sub;
        if (slot) err("double script");
        else slot = arg;
        continue;
      }
      if (tok.k == Tok::Chr && tok.cp == '!') {
        // postfix factorial: fold into the base so fractions/scripts see n!
        // as one atom ("!=", "!in" were already claimed by the lexer)
        advance();
        MNode* r = mk(MNode::Run);
        r->kids.push_back(f);
        r->kids.push_back(atom('!', kOrd));
        r->cls = kOrd;
        f = r;
        continue;
      }
      if (tok.k == Tok::Prime) {
        advance();
        if (f->k != MNode::Script && f->k != MNode::BigOp) {
          MNode* sc = mk(MNode::Script);
          sc->a = f;
          sc->cls = f->cls;
          f = sc;
        }
        if (!f->sup) {
          f->sup = mk(MNode::Run);
        } else if (f->sup->k != MNode::Run) {
          MNode* r = mk(MNode::Run);
          r->kids.push_back(f->sup);
          f->sup = r;
        }
        f->sup->kids.push_back(atom(0x2032, kOrd));
        continue;
      }
      break;
    }
    return f;
  }

  // scriptArg: no splicing target (single-token semantics: x^ab = x^a · b)
  // items == nullptr: single-token context (script/fraction argument)
  MNode* parseFactor(std::vector<MNode*>* items, bool scriptArg = false) {
    switch (tok.k) {
      case Tok::Num: {
        MNode* n = mk(MNode::Text);
        n->txt = tok.text;
        n->cls = kOrd;
        advance();
        return n;
      }
      case Tok::Word:
        (void)scriptArg;
        return parseWord(items);
      case Tok::Op: {
        const OpEntry* e = tok.op;
        advance();
        if (e->flags & kFlagLarge) return parseBigOp(e);
        return atom(e->cp, e->cls, e->flags);
      }
      case Tok::Chr: {
        MNode* n = atom(tok.cp, tok.cls);
        advance();
        return n;
      }
      case Tok::Open: {
        u32 open = tok.cp;
        u32 close = open == '(' ? ')' : open == '[' ? ']' : '}';
        advance();
        MNode* inner = parseRun();
        if (tok.k == Tok::Close && tok.cp == close) advance();
        else err("unclosed bracket");
        MNode* g = mk(MNode::Group);
        g->a = inner;
        g->openCp = open;
        g->closeCp = close;
        g->cls = kOrd;
        return g;
      }
      case Tok::Prime: {
        // stray prime with no base
        advance();
        return atom(0x2032, kOrd);
      }
      case Tok::Sup:
      case Tok::Sub:
      case Tok::Slash: {
        err("operator without operand");
        advance();
        return nullptr;
      }
      default:
        return nullptr;
    }
  }

  MNode* parseWord(std::vector<MNode*>* items) {
    std::string w = tok.text;
    u32 wpos = tok.pos;
    advance();
    if (isCallName(w)) return parseCall(w);
    if (const OpEntry* e = mathOp(w)) {
      if (e->flags & kFlagAccent) return parseCall(w, e);
      if (e->flags & kFlagTextOp) {
        MNode* n = mk(MNode::Text);
        n->txt = w;
        n->cls = kOp;
        n->flags = e->flags;
        return n;
      }
      if (e->flags & kFlagLarge) return parseBigOp(e);
      return atom(e->cp, e->cls, e->flags);
    }
    // unknown word: juxtaposed letters; scripts bind to the LAST letter.
    // Single-token contexts consume only the first letter (x^ab == x^a b):
    // rewind the lexer to just past it and re-lex the remainder.
    if (w.size() > 1 && !items) {
      lex.i = wpos + 1;
      advance();
      return atom((u8)w[0], kOrd);
    }
    if (w.size() > 1) {
      for (size_t k = 0; k + 1 < w.size(); k++)
        items->push_back(atom((u8)w[k], kOrd));
    }
    return atom((u8)w[w.size() - 1], kOrd);
  }

  // sqrt(x) root(n, x) frac(a, b) binom(n, k) abs(x) … and accents hat(x)
  MNode* parseCall(const std::string& name, const OpEntry* accent = nullptr) {
    MNode* call = mk(MNode::Call);
    call->txt = name;
    if (accent) {
      call->cp = accent->cp;
      call->flags = accent->flags;
    }
    if (tok.k != Tok::Open || tok.cp != '(') {
      err("function needs (…) argument");
      return call;
    }
    advance();
    for (;;) {
      MNode* arg = mk(MNode::Run);
      while (!runEnds() && !(tok.k == Tok::Op && tok.op->cls == kPunct &&
                             tok.op->cp == ','))
        parseMolecule(arg->kids);
      call->kids.push_back(arg);
      if (tok.k == Tok::Op && tok.op->cls == kPunct && tok.op->cp == ',') {
        advance();
        continue;
      }
      break;
    }
    if (tok.k == Tok::Close && tok.cp == ')') advance();
    else err("unclosed call");
    return call;
  }

  // big operator: optional scripts in either order, then greedy body until a
  // relation, a closing bracket, or end (v2 §13)
  MNode* parseBigOp(const OpEntry* e) {
    MNode* op = mk(MNode::BigOp);
    op->cp = e->cp;
    op->cls = kOp;
    op->flags = e->flags;
    while (tok.k == Tok::Sup || tok.k == Tok::Sub) {
      bool isSup = tok.k == Tok::Sup;
      advance();
      MNode* arg = parseFactor(nullptr, true);
      if (!arg) {
        err("missing script argument");
        break;
      }
      arg = shed(arg);
      MNode*& slot = isSup ? op->sup : op->sub;
      if (slot) err("double script");
      else slot = arg;
    }
    op->b = parseRun(/*stopAtRel=*/true);
    return op;
  }
};

// ---- layout ----------------------------------------------------------------
struct Layouter {
  Arena& arena;
  Interner& strs;
  DiagSink& diags;
  Span span;
  double basePx;
  bool coverageWarned = false;

  MathBox* mkBox(MathKind k) {
    MathBox* b = arena.make<MathBox>();
    b->kind = k;
    return b;
  }

  Su toSu(double units, u8 st) { return mathSu(units, basePx * styleScale(st)); }
  Su constSu(C c, u8 st) { return toSu(mathConst(c), st); }

  const GlyphRec* rec(u32 cp) {
    const GlyphRec* r = mathGlyph(cp);
    if (!r && !coverageWarned) {
      coverageWarned = true;
      diags.add(Sev::Warning, "math-coverage", span,
                "symbol U+" + std::to_string(cp) + " not in math font");
    }
    return r;
  }

  MathBox* glyphBox(u32 cp, u8 cls, u8 st) {
    MathBox* b = mkBox(MathKind::Glyph);
    b->cls = b->firstCls = b->lastCls = cls;
    b->text = strs.intern(cpToUtf8(cp));
    b->px = (float)(basePx * styleScale(st));
    if (const GlyphRec* r = rec(cp)) {
      b->w = toSu(r->adv, st);
      b->asc = toSu(r->asc, st);
      b->desc = toSu(r->desc, st);
      b->italic = toSu(r->italic, st);
      b->topAccent = r->topAccent != kNoTopAccent ? toSu(r->topAccent, st)
                                                  : (b->w + b->italic) / 2;
    } else {
      b->w = toSu(600, st);
      b->asc = toSu(700, st);
      b->topAccent = b->w / 2;
    }
    return b;
  }

  // literal glyph run (digits, text operators): one box, summed advances
  MathBox* textBox(std::string_view txt, u8 cls, u8 st) {
    MathBox* b = mkBox(MathKind::Glyph);
    b->cls = b->firstCls = b->lastCls = cls;
    b->text = strs.intern(txt);
    b->px = (float)(basePx * styleScale(st));
    double advU = 0;
    int ascU = 0, descU = 0, italU = 0;
    u32 i = 0;
    while (i < txt.size()) {
      u32 cp = utf8Next(txt, i);
      if (const GlyphRec* r = rec(cp)) {
        advU += r->adv;
        if (r->asc > ascU) ascU = r->asc;
        if (r->desc > descU) descU = r->desc;
        italU = r->italic;
      } else advU += 600;
    }
    b->w = toSu(advU, st);
    b->asc = toSu(ascU, st);
    b->desc = toSu(descU, st);
    b->italic = toSu(italU, st);
    b->topAccent = b->w / 2;
    return b;
  }

  MathBox* spacer(Su w) {
    MathBox* b = mkBox(MathKind::Spacer);
    b->w = w;
    return b;
  }

  // vertical glyph stretching (Typst fragment/glyph.rs::stretch): walk the
  // variant chain for the first glyph tall enough, else build the assembly
  // with extender repetition and uniform connector overlaps.
  MathBox* stretchVert(u32 cp, u8 cls, u8 st, Su target) {
    MathBox* natural = glyphBox(cp, cls, st);
    if (natural->asc + natural->desc >= target) return natural;
    const VarChain* ch = mathChain(cp, /*vertical=*/true);
    if (!ch) return natural;
    MathBox* best = natural;
    for (int k = 0; k < ch->n; k++) {
      u32 vcp = kVariantCps[ch->off + k];
      MathBox* vb = glyphBox(vcp, cls, st);
      best = vb;
      if (vb->asc + vb->desc >= target) return vb;
    }
    if (ch->asmN == 0) return best;
    // assembly, font units first (bottom-to-top part order per OpenType)
    const AsmPart* parts = &kAsmParts[ch->asmOff];
    const int minOv = kMinConnectorOverlap;
    double targetU = (double)target * kUpem /
                     (64.0 * basePx * styleScale(st));  // su → design units
    std::vector<const AsmPart*> list;
    for (int r = 1; r <= 64; r++) {
      list.clear();
      for (int k = 0; k < ch->asmN; k++) {
        int copies = parts[k].isExtender ? r : 1;
        for (int c = 0; c < copies; c++) list.push_back(&parts[k]);
      }
      if (list.size() < 2) continue;
      double full = 0;
      for (const AsmPart* pp : list) full += pp->fullAdv;
      double maxH = full - (double)minOv * (double)(list.size() - 1);
      if (maxH >= targetU || r == 64) {
        // uniform overlap, clamped to every joint's connector capacity
        int maxOv = INT32_MAX;
        for (size_t k = 0; k + 1 < list.size(); k++) {
          int cap = list[k]->endOverlap < list[k + 1]->startOverlap
                        ? list[k]->endOverlap
                        : list[k + 1]->startOverlap;
          if (cap < maxOv) maxOv = cap;
        }
        if (maxOv < minOv) maxOv = minOv;
        double o = (full - targetU) / (double)(list.size() - 1);
        if (o < minOv) o = minOv;
        if (o > maxOv) o = maxOv;
        double H = full - o * (double)(list.size() - 1);
        MathBox* out = mkBox(MathKind::HBox);
        out->cls = out->firstCls = out->lastCls = cls;
        out->asc = toSu(H, st);
        out->desc = 0;
        double cursor = 0;  // ink height consumed, from the bottom
        for (const AsmPart* pp : list) {
          MathBox* g = glyphBox(pp->cp, cls, st);
          // part baseline so its ink bottom sits at `cursor` above box bottom
          Su dy = toSu(cursor, st) + g->desc;
          out->kids.push_back({0, dy, g});
          if (g->w > out->w) out->w = g->w;
          cursor += pp->fullAdv - o;
        }
        return out;
      }
    }
    return best;
  }

  // wrap a stretched glyph so its box is centred on the math axis
  MathBox* centerOnAxis(MathBox* b, u8 cls, u8 st) {
    Su axis = constSu(C::AxisHeight, st);
    Su h = b->asc + b->desc;
    MathBox* o = mkBox(MathKind::HBox);
    o->cls = o->firstCls = o->lastCls = cls;
    o->w = b->w;
    o->italic = b->italic;
    o->asc = h / 2 + axis;
    o->desc = h - o->asc;
    o->kids.push_back({0, o->asc - b->asc, b});
    return o;
  }

  Su pairGlue(u8 l, u8 r, u8 st) {
    if (l > kInner || r > kInner) return 0;
    u8 v = kSpaceTab[l][r];
    if ((v & 4) && isScriptStyle(st)) return 0;
    int mu = kMuOf[v & 3];
    if (mu == 0) return 0;
    return toSu(mu * (double)kUpem / 18.0, st);
  }

  MathBox* layout(MNode* n, u8 st) {
    switch (n->k) {
      case MNode::Run: return layoutRun(n, st);
      case MNode::Atom: return glyphBox(n->cp, n->cls, st);
      case MNode::Text: return textBox(n->txt, n->cls, st);
      case MNode::Script: return layoutScript(n, st);
      case MNode::Frac: return layoutFrac(n, st);
      case MNode::Group: return layoutGroup(n, st);
      case MNode::BigOp: return layoutBigOp(n, st);
      case MNode::Call: return layoutCall(n, st);
    }
    return mkBox(MathKind::HBox);
  }

  MathBox* layoutRun(MNode* n, u8 st) {
    return layoutSlice(n->kids, 0, n->kids.size(), st, true, true);
  }

  MathBox* layoutSlice(const std::vector<MNode*>& kids, size_t lo, size_t hi,
                       u8 st, bool startEdge, bool endEdge) {
    std::vector<MathBox*> boxes;
    boxes.reserve(hi - lo);
    for (size_t k = lo; k < hi; k++) boxes.push_back(layout(kids[k], st));
    return assemble(boxes, st, startEdge, endEdge);
  }

  // demotion (TeXbook Rules 5–6) + pair glue + horizontal assembly.
  // startEdge/endEdge false = this run is an inner slice of a segmented
  // formula: the formula start/end demotion rules do not apply.
  MathBox* assemble(std::vector<MathBox*>& boxes, u8 st, bool startEdge = true,
                    bool endEdge = true) {
    // Rule 5: Bin after {start, Bin, Op, Rel, Open, Punct} → Ord.
    // Rule 6: Bin before {Rel, Close, Punct, end} → Ord (KaTeX
    // binRightCanceller folded into the same forward pass).
    for (size_t i = 0; i < boxes.size(); i++) {
      u8 c = boxes[i]->cls;
      if (c == kBin) {
        u8 prev = i ? boxes[i - 1]->lastCls : 0xFF;
        if ((i == 0 && startEdge) || (i && (prev == kBin || prev == kOp ||
            prev == kRel || prev == kOpen || prev == kPunct)))
          boxes[i]->cls = boxes[i]->firstCls = boxes[i]->lastCls = kOrd;
      }
      if (c == kRel || c == kClose || c == kPunct) {
        if (i && boxes[i - 1]->cls == kBin)
          boxes[i - 1]->cls = boxes[i - 1]->firstCls = boxes[i - 1]->lastCls = kOrd;
      }
    }
    if (endEdge && !boxes.empty() && boxes.back()->cls == kBin)
      boxes.back()->cls = boxes.back()->firstCls = boxes.back()->lastCls = kOrd;

    MathBox* out = mkBox(MathKind::HBox);
    out->cls = kOrd;
    Su x = 0;
    for (size_t i = 0; i < boxes.size(); i++) {
      if (i) {
        Su g = pairGlue(boxes[i - 1]->lastCls, boxes[i]->firstCls, st);
        if (g) {
          out->kids.push_back({x, 0, spacer(g)});
          x += g;
        }
      }
      out->kids.push_back({x, 0, boxes[i]});
      x += boxes[i]->w;
      if (boxes[i]->asc > out->asc) out->asc = boxes[i]->asc;
      if (boxes[i]->desc > out->desc) out->desc = boxes[i]->desc;
    }
    out->w = x;
    out->topAccent = x / 2;
    if (!boxes.empty()) {
      out->firstCls = boxes.front()->firstCls;
      out->lastCls = boxes.back()->lastCls;
      out->italic = boxes.back()->italic;
    }
    return out;
  }

  // scripts: MATH constants with the TeX 18a character-base refinement and
  // Typst's joint collision resolution (scripts.rs::compute_script_shifts)
  MathBox* layoutScript(MNode* n, u8 st) {
    // lim_(n->oo) in display style: text operators with the limits flag
    // take their scripts above/below (TeXbook \\op limits convention)
    if (n->a->k == MNode::Text && (n->a->flags & kFlagLimits) &&
        (n->a->flags & kFlagTextOp) && isDisplay(st)) {
      MathBox* base = textBox(n->a->txt, kOp, st);
      return attachLimits(base, n->sub, n->sup, st);
    }
    MathBox* base = layout(n->a, st);
    return attachScripts(base, n->sub, n->sup, st,
                         /*isChar=*/n->a->k == MNode::Atom);
  }

  MathBox* attachScripts(MathBox* base, MNode* subN, MNode* supN, u8 st,
                         bool isChar) {
    if (!subN && !supN) return base;
    MathBox* sup = supN ? layout(supN, kSupStyle[st]) : nullptr;
    MathBox* sub = subN ? layout(subN, kSubStyle[st]) : nullptr;

    Su shiftUp = 0, shiftDown = 0;
    if (sup) {
      Su u0 = isChar ? 0 : base->asc - constSu(C::SuperscriptBaselineDropMax, st);
      Su su1 = constSu(isCramped(st) ? C::SuperscriptShiftUpCramped
                                     : C::SuperscriptShiftUp, st);
      Su su2 = sup->desc + constSu(C::SuperscriptBottomMin, st);
      shiftUp = u0 > su1 ? u0 : su1;
      if (su2 > shiftUp) shiftUp = su2;
    }
    if (sub) {
      Su v0 = isChar ? 0 : base->desc + constSu(C::SubscriptBaselineDropMin, st);
      Su sd1 = constSu(C::SubscriptShiftDown, st);
      shiftDown = v0 > sd1 ? v0 : sd1;
      if (!sup) {
        Su top = sub->asc - constSu(C::SubscriptTopMax, st);
        if (top > shiftDown) shiftDown = top;
      }
    }
    if (sup && sub) {
      Su gap = (shiftUp - sup->desc) - (sub->asc - shiftDown);
      Su gapMin = constSu(C::SubSuperscriptGapMin, st);
      if (gap < gapMin) {
        Su deficit = gapMin - gap;
        Su maxUp = constSu(C::SuperscriptBottomMaxWithSubscript, st) -
                   (shiftUp - sup->desc);
        Su up = deficit < maxUp ? deficit : (maxUp > 0 ? maxUp : 0);
        shiftUp += up;
        shiftDown += deficit - up;
      }
    }

    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = base->cls;
    out->w = base->w;
    out->asc = base->asc;
    out->desc = base->desc;
    out->kids.push_back({0, 0, base});
    Su right = base->w;
    if (sup) {
      Su dx = base->w;
      out->kids.push_back({dx, shiftUp, sup});
      if (dx + sup->w > right) right = dx + sup->w;
      if (shiftUp + sup->asc > out->asc) out->asc = shiftUp + sup->asc;
      if (sup->desc - shiftUp > out->desc) out->desc = sup->desc - shiftUp;
    }
    if (sub) {
      // OpenType convention: the subscript hangs back by the italic
      // correction (the ∫ slant tuck); the advance includes the full ink
      Su dx = base->w - base->italic;
      if (dx < 0) dx = 0;
      out->kids.push_back({dx, -shiftDown, sub});
      if (dx + sub->w > right) right = dx + sub->w;
      if (shiftDown + sub->desc > out->desc) out->desc = shiftDown + sub->desc;
      if (sub->asc - shiftDown > out->asc) out->asc = sub->asc - shiftDown;
    }
    out->w = right + constSu(C::SpaceAfterScript, st);
    out->lastCls = base->cls;
    return out;
  }

  // fractions: Typst fraction.rs verbatim (MATH constants, axis-centred bar)
  MathBox* layoutFrac(MNode* n, u8 st) {
    bool disp = isDisplay(st);
    MathBox* num = layout(n->a, kNumStyle[st]);
    MathBox* den = layout(n->b, kDenStyle[st]);
    Su axis = constSu(C::AxisHeight, st);
    Su thick = constSu(C::FractionRuleThickness, st);
    Su numGapMin = constSu(disp ? C::FractionNumDisplayStyleGapMin
                                : C::FractionNumeratorGapMin, st);
    Su denGapMin = constSu(disp ? C::FractionDenomDisplayStyleGapMin
                                : C::FractionDenominatorGapMin, st);
    Su numUp = constSu(disp ? C::FractionNumeratorDisplayStyleShiftUp
                            : C::FractionNumeratorShiftUp, st);
    Su denDown = constSu(disp ? C::FractionDenominatorDisplayStyleShiftDown
                              : C::FractionDenominatorShiftDown, st);
    Su numFloor = axis + thick / 2 + numGapMin + num->desc;
    if (numFloor > numUp) numUp = numFloor;
    Su denFloor = -axis + thick / 2 + denGapMin + den->asc;
    if (denFloor > denDown) denDown = denFloor;

    Su w = num->w > den->w ? num->w : den->w;
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    out->w = w;
    out->asc = numUp + num->asc;
    out->desc = denDown + den->desc;
    MathBox* bar = mkBox(MathKind::Rule);
    bar->w = w;
    bar->asc = thick;
    out->kids.push_back({(w - num->w) / 2, numUp, num});
    out->kids.push_back({0, axis - thick / 2, bar});
    out->kids.push_back({(w - den->w) / 2, -denDown, den});
    return out;
  }

  // fenced content: delimiters stretch to the content when it outgrows the
  // natural glyph — target 2·max(asc−axis, desc+axis), 10% shortfall
  // tolerated (Typst short_fall); centred on the axis when stretched.
  MathBox* fencedRun(const std::vector<MNode*>& kidsN, u32 openCp, u32 closeCp,
                     u8 st) {
    std::vector<MathBox*> inner;
    inner.reserve(kidsN.size());
    Su iAsc = 0, iDesc = 0;
    for (MNode* k : kidsN) {
      MathBox* b = layout(k, st);
      if (b->asc > iAsc) iAsc = b->asc;
      if (b->desc > iDesc) iDesc = b->desc;
      inner.push_back(b);
    }
    Su axis = constSu(C::AxisHeight, st);
    Su over = iAsc - axis, under = iDesc + axis;
    Su target = 2 * (over > under ? over : under);
    target -= target / 10;  // short_fall
    auto delim = [&](u32 cp, u8 cls) {
      MathBox* g = glyphBox(cp, cls, st);
      if (g->asc + g->desc >= target) return g;  // natural glyph suffices
      MathBox* sg = stretchVert(cp, cls, st, target);
      return centerOnAxis(sg, cls, st);
    };
    std::vector<MathBox*> boxes;
    boxes.reserve(inner.size() + 2);
    boxes.push_back(delim(openCp, kOpen));
    for (MathBox* b : inner) boxes.push_back(b);
    boxes.push_back(delim(closeCp, kClose));
    MathBox* out = assemble(boxes, st);
    out->cls = kOrd;  // firstCls/lastCls stay Open/Close for neighbour glue
    return out;
  }

  MathBox* layoutGroup(MNode* n, u8 st) {
    return fencedRun(n->a->kids, n->openCp, n->closeCp, st);
  }

  // limits above/below a display operator (MATH constants; K assembleSupSub,
  // T compute_limit_shifts). Horizontal centres slide by ±italic/2.
  MathBox* attachLimits(MathBox* base, MNode* subN, MNode* supN, u8 st) {
    MathBox* sup = supN ? layout(supN, kSupStyle[st]) : nullptr;
    MathBox* sub = subN ? layout(subN, kSubStyle[st]) : nullptr;
    if (!sup && !sub) return base;
    Su w = base->w;
    if (sup && sup->w > w) w = sup->w;
    if (sub && sub->w > w) w = sub->w;
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = base->cls;
    out->w = w;
    out->asc = base->asc;
    out->desc = base->desc;
    Su cx = w / 2;
    out->kids.push_back({cx - base->w / 2, 0, base});
    if (sup) {
      Su rise = constSu(C::UpperLimitGapMin, st) + sup->desc;
      Su rise2 = constSu(C::UpperLimitBaselineRiseMin, st);
      Su dy = base->asc + (rise > rise2 ? rise : rise2);
      out->kids.push_back({cx + base->italic / 2 - sup->w / 2, dy, sup});
      out->asc = dy + sup->asc;
    }
    if (sub) {
      Su drop = constSu(C::LowerLimitGapMin, st) + sub->asc;
      Su drop2 = constSu(C::LowerLimitBaselineDropMin, st);
      Su dy = base->desc + (drop > drop2 ? drop : drop2);
      out->kids.push_back({cx - base->italic / 2 - sub->w / 2, -dy, sub});
      out->desc = dy + sub->desc;
    }
    return out;
  }

  // big operator + scripts, then the greedy body as an opaque subrun.
  MathBox* layoutBigOp(MNode* n, u8 st) {
    MathBox* op;
    bool textOp = (n->flags & kFlagTextOp) != 0;
    if (textOp) {
      op = textBox(n->txt, kOp, st);
    } else {
      op = glyphBox(n->cp, kOp, st);
      if (isDisplay(st)) {
        // grow to DisplayOperatorMinHeight and centre on the axis (T glyph
        // stretch; K makeLargeOp Size2 swap)
        Su minH = constSu(C::DisplayOperatorMinHeight, st);
        if (op->asc + op->desc < minH)
          op = centerOnAxis(stretchVert(n->cp, kOp, st, minH), kOp, st);
      }
    }
    bool limits = (n->flags & kFlagLimits) && isDisplay(st);
    MathBox* scripted = limits ? attachLimits(op, n->sub, n->sup, st)
                               : attachScripts(op, n->sub, n->sup, st,
                                               /*isChar=*/false);
    MathBox* body = n->b && !n->b->kids.empty() ? layoutRun(n->b, st) : nullptr;
    if (!body) {
      scripted->cls = scripted->firstCls = scripted->lastCls = kOp;
      return scripted;
    }
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = kOp;
    out->firstCls = kOp;
    out->lastCls = body->lastCls;
    Su x = scripted->w;
    out->kids.push_back({0, 0, scripted});
    Su g = pairGlue(kOp, body->firstCls, st);
    x += g;
    out->kids.push_back({x, 0, body});
    out->w = x + body->w;
    out->asc = scripted->asc > body->asc ? scripted->asc : body->asc;
    out->desc = scripted->desc > body->desc ? scripted->desc : body->desc;
    return out;
  }

  // radicals: MATH constants per Typst radical.rs; surd stretched to the
  // radicand + gap + rule, leftover split half above / half below (TeXbook
  // p.443 item 11); degree raised by RadicalDegreeBottomRaisePercent.
  MathBox* layoutRadical(MNode* degN, MNode* radN, u8 st) {
    MathBox* rad = layout(radN, (u8)(st | 1));  // cramped
    bool disp = isDisplay(st);
    Su gap = constSu(disp ? C::RadicalDisplayStyleVerticalGap
                          : C::RadicalVerticalGap, st);
    Su thick = constSu(C::RadicalRuleThickness, st);
    Su extra = constSu(C::RadicalExtraAscender, st);
    Su target = rad->asc + rad->desc + gap + thick;
    MathBox* surd = stretchVert(0x221A, kOrd, st, target);
    Su surdH = surd->asc + surd->desc;
    Su excess = surdH - target;
    Su below = rad->desc + (excess > 0 ? excess / 2 : 0);
    Su top = surdH - below;         // surd ink top, above baseline
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    Su x = 0;
    if (degN && !degN->kids.empty()) {
      MathBox* deg = layoutRun(degN, SS);  // degree in scriptscript
      Su kb = constSu(C::RadicalKernBeforeDegree, st);
      Su ka = constSu(C::RadicalKernAfterDegree, st);  // typically negative
      Su raise = (Su)((i64)surdH *
                      mathConst(C::RadicalDegreeBottomRaisePercent) / 100);
      Su degDy = -below + raise + deg->desc;
      out->kids.push_back({kb, degDy, deg});
      x = kb + deg->w + ka;
      if (x < 0) x = 0;
      if (degDy + deg->asc > top + extra && degDy + deg->asc > out->asc)
        out->asc = degDy + deg->asc;
    }
    out->kids.push_back({x, surd->desc - below, surd});
    x += surd->w;
    MathBox* bar = mkBox(MathKind::Rule);
    bar->w = rad->w;
    bar->asc = thick;
    out->kids.push_back({x, top - thick, bar});
    out->kids.push_back({x, 0, rad});
    out->w = x + rad->w;
    if (top + extra > out->asc) out->asc = top + extra;
    if (rad->asc > out->asc) out->asc = rad->asc;
    out->desc = below > rad->desc ? below : rad->desc;
    return out;
  }

  // accents: TopAccentAttachment alignment (T fragment/glyph.rs), cramped
  // base; the accent rides at max(0, base.asc − AccentBaseHeight).
  MathBox* layoutAccent(u32 accCp, MNode* baseN, u8 st) {
    MathBox* base = layoutRun(baseN, (u8)(st | 1));
    // single-glyph base: the glyph's own TopAccentAttachment is authoritative
    // (the run wrapper only knows w/2 — same for Euler's x, off by ~0.04em
    // for f-like glyphs whose ink centre leads the advance centre)
    Su baseAttach = base->topAccent;
    if (baseN->kids.size() == 1 && baseN->kids[0]->k == MNode::Atom) {
      if (const GlyphRec* r = mathGlyph(baseN->kids[0]->cp))
        if (r->topAccent != kNoTopAccent) baseAttach = toSu(r->topAccent, st);
    }
    MathBox* acc = glyphBox(accCp, kOrd, st);
    Su dy = base->asc - constSu(C::AccentBaseHeight, st);
    if (dy < 0) dy = 0;
    Su x = baseAttach - acc->topAccent;
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = out->firstCls = out->lastCls = kOrd;
    out->w = base->w;
    out->italic = base->italic;
    out->topAccent = base->topAccent;
    out->kids.push_back({0, 0, base});
    out->kids.push_back({x, dy, acc});
    out->asc = base->asc;
    if (dy + acc->asc > out->asc) out->asc = dy + acc->asc;
    out->desc = base->desc;
    return out;
  }

  // binomial: barless stack (Stack* MATH constants) fenced in parens
  MathBox* layoutBinom(MNode* topN, MNode* botN, u8 st) {
    bool disp = isDisplay(st);
    MathBox* top = layout(topN, kNumStyle[st]);
    MathBox* bot = layout(botN, kDenStyle[st]);
    Su upMin = constSu(disp ? C::StackTopDisplayStyleShiftUp
                            : C::StackTopShiftUp, st);
    Su downMin = constSu(disp ? C::StackBottomDisplayStyleShiftDown
                              : C::StackBottomShiftDown, st);
    Su gapMin = constSu(disp ? C::StackDisplayStyleGapMin : C::StackGapMin, st);
    Su up = upMin, down = downMin;
    Su gap = (up - top->desc) - (bot->asc - down);
    if (gap < gapMin) {  // split the deficit both ways (Typst stack leftover)
      Su d = gapMin - gap;
      up += d / 2;
      down += d - d / 2;
    }
    Su w = top->w > bot->w ? top->w : bot->w;
    MathBox* stack = mkBox(MathKind::HBox);
    stack->cls = stack->firstCls = stack->lastCls = kOrd;
    stack->w = w;
    stack->topAccent = w / 2;
    stack->asc = up + top->asc;
    stack->desc = down + bot->desc;
    stack->kids.push_back({(w - top->w) / 2, up, top});
    stack->kids.push_back({(w - bot->w) / 2, -down, bot});
    // fence in stretched parens
    Su axis = constSu(C::AxisHeight, st);
    Su over = stack->asc - axis, under = stack->desc + axis;
    Su target = 2 * (over > under ? over : under);
    target -= target / 10;
    MathBox* open = centerOnAxis(stretchVert('(', kOpen, st, target), kOpen, st);
    MathBox* close = centerOnAxis(stretchVert(')', kClose, st, target), kClose, st);
    MathBox* out = mkBox(MathKind::HBox);
    out->cls = kOrd;
    out->firstCls = kOpen;
    out->lastCls = kClose;
    out->kids.push_back({0, 0, open});
    out->kids.push_back({open->w, 0, stack});
    out->kids.push_back({open->w + stack->w, 0, close});
    out->w = open->w + stack->w + close->w;
    out->asc = stack->asc > open->asc ? stack->asc : open->asc;
    out->desc = stack->desc > open->desc ? stack->desc : open->desc;
    out->topAccent = out->w / 2;
    return out;
  }

  MathBox* layoutCall(MNode* n, u8 st) {
    auto arg = [&](size_t i) -> MNode* {
      static MNode empty;
      return i < n->kids.size() ? n->kids[i] : &empty;
    };
    if (n->flags & kFlagAccent) return layoutAccent(n->cp, arg(0), st);
    if (n->txt == "sqrt") return layoutRadical(nullptr, arg(0), st);
    if (n->txt == "root") return layoutRadical(arg(0), arg(1), st);
    if (n->txt == "abs") return fencedRun(arg(0)->kids, '|', '|', st);
    if (n->txt == "norm") return fencedRun(arg(0)->kids, 0x2016, 0x2016, st);
    if (n->txt == "floor") return fencedRun(arg(0)->kids, 0x230A, 0x230B, st);
    if (n->txt == "ceil") return fencedRun(arg(0)->kids, 0x2308, 0x2309, st);
    if (n->txt == "frac" && n->kids.size() >= 2) {
      MNode fr;
      fr.k = MNode::Frac;
      fr.a = arg(0);
      fr.b = arg(1);
      return layoutFrac(&fr, st);
    }
    if (n->txt == "binom" && n->kids.size() >= 2)
      return layoutBinom(arg(0), arg(1), st);
    diags.add(Sev::Warning, "math-unknown-call", span,
              "unknown construct '" + n->txt + "'");
    std::vector<MathBox*> boxes;
    boxes.push_back(textBox(n->txt, kOrd, st));
    for (size_t i = 0; i < n->kids.size(); i++)
      for (MNode* k : n->kids[i]->kids) boxes.push_back(layout(k, st));
    return assemble(boxes, st);
  }
};

}  // namespace

// effective edge classes of a parse node, for segmentation's demotion
// preview (mirrors what layout will produce)
static void effClsOf(const MNode* n, u8& f, u8& l) {
  switch (n->k) {
    case MNode::Atom:
    case MNode::Text:
      f = l = n->cls;
      return;
    case MNode::Script:
      effClsOf(n->a, f, l);
      l = f;
      return;
    case MNode::Group:
      f = kOpen;
      l = kClose;
      return;
    case MNode::BigOp: {
      f = kOp;
      l = kOp;
      if (n->b && !n->b->kids.empty()) {
        u8 bf;
        effClsOf(n->b->kids.back(), bf, l);
      }
      return;
    }
    default:
      f = l = kOrd;
      return;
  }
}

MathBox* layoutMathFormula(std::string_view src, bool display, double sizePx,
                           Arena& arena, Interner& strs, DiagSink& diags,
                           Span span) {
  Parser p{Lexer{src}, arena, diags, span, {}, 0};
  p.advance();
  MNode* run = p.parseRun();
  if (p.tok.k != Tok::End) p.err("unexpected closing bracket");
  Layouter L{arena, strs, diags, span, sizePx, false};
  if (p.errors) {
    // degrade: the raw source as an upright text box (still one formula box)
    return L.textBox(src, mathfont::kOrd, display ? D : T);
  }
  return L.layout(run, display ? D : T);
}

std::vector<MathSeg> layoutMathSegments(std::string_view src, bool display,
                                        double sizePx, Arena& arena,
                                        Interner& strs, DiagSink& diags,
                                        Span span) {
  std::vector<MathSeg> out;
  Parser p{Lexer{src}, arena, diags, span, {}, 0};
  p.advance();
  MNode* run = p.parseRun();
  if (p.tok.k != Tok::End) p.err("unexpected closing bracket");
  Layouter L{arena, strs, diags, span, sizePx, false};
  u8 st = display ? D : T;
  if (p.errors) {
    out.push_back({L.textBox(src, kOrd, st), 0, 0});
    return out;
  }
  const std::vector<MNode*>& kids = run->kids;
  size_t n = kids.size();
  if (display || n == 0) {
    out.push_back({L.layout(run, st), 0, 0});
    return out;
  }
  // demotion preview over top-level effective classes
  std::vector<u8> f(n), l(n);
  for (size_t i = 0; i < n; i++) effClsOf(kids[i], f[i], l[i]);
  for (size_t i = 0; i < n; i++) {
    if (f[i] == kBin && l[i] == kBin) {
      u8 prev = i ? l[i - 1] : 0xFF;
      if (i == 0 || prev == kBin || prev == kOp || prev == kRel ||
          prev == kOpen || prev == kPunct)
        f[i] = l[i] = kOrd;
    }
    if ((f[i] == kRel || f[i] == kClose || f[i] == kPunct) && i &&
        f[i - 1] == kBin && l[i - 1] == kBin)
      f[i - 1] = l[i - 1] = kOrd;
  }
  if (n && f[n - 1] == kBin && l[n - 1] == kBin) f[n - 1] = l[n - 1] = kOrd;

  // cut points: a top-level Rel atom is its own segment (break before AND
  // after); a top-level Bin atom ends its segment (break after)
  struct Cut { size_t lo, hi; u8 brk; };
  std::vector<Cut> cuts;
  size_t cur = 0;
  u8 pending = 0;
  for (size_t i = 0; i < n; i++) {
    bool relAtom = kids[i]->k == MNode::Atom && f[i] == kRel;
    bool binAtom = kids[i]->k == MNode::Atom && f[i] == kBin && l[i] == kBin;
    if (relAtom) {
      if (i > cur) {
        cuts.push_back({cur, i, pending});
      }
      cuts.push_back({i, i + 1, i > cur || !cuts.empty() ? (u8)2 : pending});
      pending = 1;  // after-Rel
      cur = i + 1;
      continue;
    }
    if (binAtom) {
      cuts.push_back({cur, i + 1, pending});
      pending = 3;  // after-Bin
      cur = i + 1;
    }
  }
  if (cur < n) cuts.push_back({cur, n, pending});
  if (cuts.size() <= 1) {
    out.push_back({L.layout(run, st), 0, 0});
    return out;
  }
  MathBox* prev = nullptr;
  for (size_t c = 0; c < cuts.size(); c++) {
    MathBox* b = L.layoutSlice(kids, cuts[c].lo, cuts[c].hi, st,
                               /*startEdge=*/c == 0,
                               /*endEdge=*/c + 1 == cuts.size());
    Su glue = prev ? L.pairGlue(prev->lastCls, b->firstCls, st) : 0;
    out.push_back({b, glue, cuts[c].brk});
    prev = b;
  }
  return out;
}

static void dumpBox(std::string& out, const MathBox* b, const Interner& strs,
                    int depth, Su dx, Su dy) {
  for (int i = 0; i < depth; i++) out += "  ";
  static const char* kClsName[] = {"Ord",  "Op",    "Bin",   "Rel",
                                   "Open", "Close", "Punct", "Inner"};
  const char* cls = b->cls <= 7 ? kClsName[b->cls] : "?";
  switch (b->kind) {
    case MathKind::Glyph:
      out += "glyph \"";
      appendEscaped(out, strs.get(b->text));
      appendf(out, "\" %s w=%d asc=%d desc=%d", cls, b->w, b->asc, b->desc);
      if (b->italic) appendf(out, " it=%d", b->italic);
      appendf(out, " px=%g", (double)b->px);
      break;
    case MathKind::Rule:
      appendf(out, "rule w=%d h=%d", b->w, b->asc + b->desc);
      break;
    case MathKind::Spacer:
      appendf(out, "glue w=%d", b->w);
      break;
    case MathKind::HBox:
      appendf(out, "hbox %s w=%d asc=%d desc=%d", cls, b->w, b->asc, b->desc);
      break;
  }
  if (dx || dy) appendf(out, " @(%d,%d)", dx, dy);
  out += "\n";
  for (const MathKid& k : b->kids) dumpBox(out, k.box, strs, depth + 1, k.dx, k.dy);
}

std::string dumpMathBox(const MathBox* box, const Interner& strs) {
  std::string out;
  if (box) dumpBox(out, box, strs, 0, 0, 0);
  return out;
}

}  // namespace tsr
