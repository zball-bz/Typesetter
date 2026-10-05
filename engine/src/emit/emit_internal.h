// Emit internals shared by the block walk (emit.cc) and the legacy inline
// emitter (legacy.cc, the CI equivalence check of plan P1-12; deleted with
// the paragraph shaper, P4-02). The block walk is one; the inline stream of
// a unit is written by an InlineSink: HList items in production, the old
// LinebreakBlocks for the check.
#pragma once
#include "emit.h"

namespace tsr {

struct EmitEnv {
  Arena& arena;
  DiagSink& diags;
  Interner& strs;
  StyleTable& styles;
  const Config& cfg;
  const MathTextCtx* mathText = nullptr;  // text-font runs in formulas (math-design §10)
  StrRef spaceRef = 0, hyphenRef = 0, bulletRef = 0;

  StyleId compose(StyleId base, u64 addBits, float mul) {
    return tsr::compose(styles, base, addBits, mul);
  }
  // the style's em (sizePx honoured): one formula with measurement (P0-08)
  double fontPx(StyleId st) const { return emPx(cfg, styles.get(st)); }
};

struct ICtx {
  u64 addBits = 0;
  u16 addFlags = 0;  // BF_REF: resolver-synthesized content
  float mul = 1.0f;
  StrRef url = 0;
  bool noHyphen = false;  // display context (headings)
};

struct InlineSink {
  virtual ~InlineSink() = default;
  virtual void walk(const ContentNode* n, FlowUnit& u, ICtx ctx) = 0;
  // the paragraph indent (首行缩进, App C): an unbreakable fixed-width box
  virtual void indent(FlowUnit& u, StyleId st, Span span, double px) = 0;
  // the unit's inline stream is complete
  virtual void finish(FlowUnit& u) = 0;
  // a cell's stream (table cell, caption row, sidecar line) moves to its cell
  virtual void toCell(FlowUnit& tmp, TableCell& tc) = 0;
  // the whole document is emitted (cross-unit passes)
  virtual void done(std::vector<TopBlock>& tops) = 0;
};

std::vector<TopBlock> emitWith(const ContentTree& tree, EmitEnv& env, InlineSink& sink);

}  // namespace tsr
