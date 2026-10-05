// The math IR (plan P1-24; design T8 "MathRow registry + MathCall IR"): a
// formula's source → one tree of a closed node set, with every construct a
// Call of a MathRow — the built-in primitives (C++) and the template rows of
// engine/data/math/stdlib.tsv (abs, binom, the accents, sqrt …), checked by
// one validator. Errors are local: a malformed stretch becomes an Error leaf
// (its source slice, set as text) with a sub-span diagnostic, and the rest
// of the formula lays out. `tsrc --stage=mathir` prints the tree.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "../support/support.h"
#include "atom.h"

namespace tsr {

// the closed set of layout primitives (the rest are template rows)
enum class Prim : u8 {
  None,
  Frac,     // frac(num, den)
  Stack,    // stack(top, bottom): two rows on the axis, no rule
  Radical,  // radical(radicand, index?)
  Lr,       // lr(open: sym, body, close: sym): fenced, stretched to the body
  Accent,   // accent(base, mark: sym)
  Rule,     // rule(base, side: over|under)
};
enum class SlotKind : u8 { Content, Sym, Ident };

struct MNode {
  enum K : u8 { Sym, Num, Text, Run, Attach, Frac, Group, BigOp, Call, Param, Error } k = Sym;
  u32 cp = 0;
  u8 cls = kOrd, flags = 0;
  std::string txt;            // Num/Text: the glyphs; Call: the row; Param: its name;
                              //   Error: the source slice
  bool textFont = false;      // Text/Error: set in the document's text font
  bool primeSup = false;      // Attach: the sup holds only primes so far
  Prim prim = Prim::None;     // Call
  u32 lo = 0, hi = 0;         // the source bytes it came from
  MNode* a = nullptr;         // Attach/BigOp: base; Frac: numerator; Group: inner run
  MNode* sub = nullptr;       // Attach/BigOp
  MNode* sup = nullptr;       // Attach/BigOp
  MNode* b = nullptr;         // Frac: denominator; BigOp: body
  u32 openCp = 0, closeCp = 0;  // Group (closeCp 0: unclosed)
  std::vector<MNode*> kids;   // Run; Call: the arguments
};

// a diagnostic of one formula, at bytes [lo, hi) of its source
struct MathDiag {
  Sev sev = Sev::Error;
  const char* code = "math-parse";
  u32 lo = 0, hi = 0;
  std::string msg;
};

struct MathIR {
  MNode* root = nullptr;  // a Run
  std::vector<MathDiag> diags;
};

// Parses and binds one formula (calls expanded, rows checked). Never fails:
// what does not parse is an Error leaf.
MathIR parseMath(std::string_view src, Arena& arena);

// the formula's diagnostics into the document's sink, at sub-spans of the
// formula's span (at most 8, the rest summarized)
void reportMathDiags(const MathIR& ir, std::string_view src, Span span, DiagSink& diags);

// tsrc --stage=mathir: the tree and its diagnostics
std::string dumpMathIR(const MathIR& ir, std::string_view src);

// ---- the row registry --------------------------------------------------------
struct SlotSpec {
  std::string name;
  SlotKind kind = SlotKind::Content;
  bool optional = false;
};
struct MathRow {
  std::string name;
  Prim prim = Prim::None;       // set only by the C++ primitive table
  std::vector<SlotSpec> params;
  const MNode* body = nullptr;  // a template: Param leaves stand for the arguments
  u32 bareCp = 0;               // the meaning without '(' (dot → ⋅), 0 = none
  u8 bareCls = kOrd;
};
// the rows (primitives, then stdlib.tsv); nullptr = not a function
const MathRow* mathRow(std::string_view name);
// the one validator (stdlib rows at start-up; the unit test asserts all pass)
bool checkRow(const MathRow& row, std::string& why);
const std::vector<MathRow>& mathRows();

}  // namespace tsr
