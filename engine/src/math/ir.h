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
  // (plan P3-24) spacing, style and the bind-time rewrites
  Space,    // space(mu): a fixed space of mu/18 em
  Style,    // mstyle(body, style: display|text|script|sscript)
  Limits,   // mlimits(body, mode: limits|scripts): where an operator's scripts go
  Variant,  // variant(body, alphabet: bb|cal|frak|bold|italic|sans|mono): its letters and digits
  Class,    // class(class: ord|op|bin|rel|open|close|punct|inner, body): its atom class
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
  bool mid = false;           // Sym (plan P3-24): a fence alone in its group — its middle, stretched
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

struct MathScope;  // env.h: a document's declarations as of an epoch

// Parses and binds one formula (calls expanded, rows checked). Never fails:
// what does not parse is an Error leaf. `scope` (plan P2-15): the document's
// declarations in force (null: the built-ins only); `std.name` always means
// the built-in. A formula's holes arrive as delimited units (env.h
// MathSource): \x01…\x02 a parse-isolated math value, \x03…\x04 a string,
// \x05…\x06 an error.
MathIR parseMath(std::string_view src, Arena& arena, const MathScope* scope = nullptr);

// the formula's diagnostics into the document's sink, at sub-spans of the
// formula's span (at most 8, the rest summarized). `map` (plan P2-15,
// env.h MathSource): (offset, source start, source end) triples placing an
// offset in its fragment; empty: the span and its delimiters
void reportMathDiags(const MathIR& ir, std::string_view src, Span span, DiagSink& diags,
                     const std::vector<u32>* map = nullptr);

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
// a template body (stdlib.tsv rows, $.math.fn): #name is a parameter
MNode* parseTemplateBody(std::string_view body, const std::vector<SlotSpec>& params, Arena& arena,
                         const MathScope* scope, std::vector<MathDiag>* diags);

}  // namespace tsr
