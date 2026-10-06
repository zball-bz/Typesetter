// The math declarations of a document (plan P2-15; design T8 MathEnv): the
// rows `$.math.symbol`, `$.math.op` and `$.math.fn` declared — DECL
// math.symbol / math.op / math.fn, positional — each in force from its place
// in the flow. A formula binds names against the rows of its node's
// declEpoch (model.h), so a formula moved or cloned by the resolver (a
// footnote's) binds as of where it was written.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "../model/model.h"
#include "ir.h"

namespace tsr {

struct MathDeclRow {
  enum K : u8 { Symbol, Op, Fn } k = Symbol;
  std::string name;
  u32 epoch = 0;          // in force where declEpoch >= epoch (1-based)
  u32 cp = 0;             // Symbol: its character
  u8 cls = kOrd;          // Symbol: its class
  bool claimCp = false;   // Symbol: the typed character takes the class too
  u8 flags = 0;           // Op: kFlagTextOp, kFlagLimits when its limits go over and under
  MathRow row;            // Fn: its params and template body
};

class MathEnv {
 public:
  // the positional declarations in flow order (their 1-based position among
  // them is the epoch); a malformed math declaration is diagnosed at its
  // span (math-decl) and ignored
  void build(const std::vector<Decl>& decls, const Interner& strs, DiagSink& diags);
  // the latest row of a name in force at `epoch`
  const MathDeclRow* find(std::string_view name, u32 epoch) const;
  // a claimed character's row in force at `epoch` (typed input takes its class)
  const MathDeclRow* claimed(u32 cp, u32 epoch) const;
  bool empty() const { return rows_.empty(); }

 private:
  std::vector<MathDeclRow> rows_;
  Arena arena_;
};

// what a formula binds against (plan P2-15): its document's declarations as
// of its epoch; null = the built-ins only. `style`: the formula's computed
// style, under which its text-font runs are made (plan P3-01: they keep its
// font, weight, language; the formula positions them)
struct MathScope {
  const MathEnv* env = nullptr;
  u32 epoch = 0;
  StyleId style = 0;
};

// A formula's source as the math lexer reads it (plan P2-15; design T8
// MathValue): its mathsrc fragments — joined with '\n' where adjacent — and
// its holes as delimited units: a math value \x01…\x02 (parse-isolated), a
// string \x03…\x04 (quoted text), an error \x05message\x06; or a formula's
// old `src` attribute. `map`: (offset, source start, source end) triples, so
// a sub-span diagnostic lands in its fragment (empty: the old form's
// delimiter heuristic); `copy`: its source as written (fragments, math holes'
// sources, a string hole quoted).
struct MathSource {
  std::string text, copy;
  std::vector<u32> map;
};
MathSource mathSource(const ContentNode* n, const Interner& strs, DiagSink* diags = nullptr);
// the common case without building anything: a formula that is its old src
// attribute or one clean fragment (no hole, no control byte) is that string,
// already interned (its source and its copy text alike)
StrRef mathSourceRef(const ContentNode* n, const Interner& strs);

}  // namespace tsr
