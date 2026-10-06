// Emit internals of the leaf shaper (emit.cc). The leaves are the box
// tree's (plan P1-18); a leaf's inline stream is written by an InlineSink:
// HList items (the paragraph shaper, plan P4-02), which the breaker reads
// (plan P4-08).
#pragma once
#include "emit.h"
#include "../model/model.h"
#include "../resource/resource_table.h"
#include "../math/env.h"
#include "../shape/context.h"

namespace tsr {

struct EmitEnv {
  Arena& arena;
  DiagSink& diags;
  Interner& strs;
  StyleTable& styles;
  EmitSettings cfg;
  const MeasureNeeds* mathText = nullptr;  // text-font runs in formulas (math-design §14)
  const ResourceTable* rt = nullptr;      // answered code tokens (image sizes: Measure's and Layout's, plan P3-32)
  const MathEnv* math = nullptr;          // the document's math declarations (plan P2-15)
  const Cascade* cascade = nullptr;       // the rules, for code tokens (plan P3-01)
  BoxAsker* boxes = nullptr;              // (plan P3-28) inline host boxes: answers, or a need filed
  StrRef spaceRef = 0, hyphenRef = 0, bulletRef = 0;
  StrRef errorSyn = 0;  // "error": error text's data-syn (plan P3-07)
  StrRef emptyRef = 0;  // "": an anchor box's text (plan P3-13)
  // the presentation kinds put on their text (plan P2-08): the mono font role,
  // bold, a CJK run
  StyleDelta mono, bold, cjk;
  const Flow* leafFlow = nullptr;  // the leaf's own stream (not a cell's) and
  Span leafSpan{};                 //   its node's span

  StyleId compose(StyleId base, const StyleDelta& d, float mul) { return tsr::compose(styles, base, d, mul); }
  // the style's em (sizePx honoured): one formula with measurement (P0-08)
  double fontPx(StyleId st) const { return emPx(cfg, styles.get(st)); }
};

struct ICtx {
  StyleDelta add;  // what enclosing kinds put on the text (inline code: mono)
  SynKind synKind = SynKind::Content;  // (plan P4-01) Ref: a resolver reference's text
  float mul = 1.0f;
  LinkTarget url;  // the link the text is in
  // (plan P4-06) the block's own rule for a run whose text.hyphens is
  // unset: its par.hyphenate — auto, or manual where its words do not
  // hyphenate (a heading's role style, a language whose pack says so)
  u8 hyphens = HYPHENS_AUTO;
  // (plan P3-07) the copy policy of the text: the innermost node's
  // `copy` / `syn` attributes
  CopyMode copy = CopyMode::Text;
  StrRef syn = 0;
  StrRef copyText = 0;
  u32 copyGroup = 0;
  StrRef error = 0;  // (plan P3-16) inside an error node: its message
};

struct InlineSink {
  virtual ~InlineSink() = default;
  virtual void walk(const ContentNode* n, Flow& u, ICtx ctx) = 0;
  // (plan P3-07) a block's own `copy` / `syn` attributes: its text's policy
  virtual void copyPolicy(const ContentNode* n, Flow& u, ICtx& ctx) = 0;
  // the paragraph indent (首行缩进, App C): an unbreakable fixed-width box
  virtual void indent(Flow& u, StyleId st, Span span, double px, double em) = 0;
  // (plan P4-08) the stream's base style — its paragraph's, its row's —
  // whose space is its justification unit (HList::juSu)
  virtual void base(StyleId st) = 0;
  // the unit's inline stream is complete (plan P4-02: walk and indent
  // record it; finish shapes it as one paragraph)
  virtual void finish(Flow& u) = 0;
  // a cell's stream (table cell, caption row, sidecar line) moves to its cell
  virtual void toCell(Flow& tmp, Flow& tc) = 0;
};

struct BoxTree;
// shapes every leaf of the box tree through the sink

}  // namespace tsr
