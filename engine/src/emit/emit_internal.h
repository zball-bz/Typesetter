// Emit internals shared by the leaf shaper (emit.cc) and the legacy inline
// emitter (legacy.cc, the CI equivalence check of plan P1-12; deleted with
// the paragraph shaper, P4-02). The leaves are the box tree's (plan P1-18);
// a leaf's inline stream is written by an InlineSink: HList items in
// production, the old LinebreakBlocks for the check.
#pragma once
#include "emit.h"
#include "../model/model.h"
#include "../resource/resource_table.h"
#include "../math/env.h"

namespace tsr {

// (plan P3-30; finding markup-language/quote-context-heuristic) how the
// ambiguous marks — curly quotes, the em dash, the ellipsis — of a run are
// set: as its own language says (zh, ja, ko: CJK punctuation; another:
// Latin glyphs), or, a run without one, by its neighbours
enum class MarkClass : u8 { Neighbours, Cjk, Latin };
inline MarkClass markClassOf(StrRef lang, const Interner& strs) {
  if (!lang) return MarkClass::Neighbours;
  std::string_view tag = strs.get(lang);
  tag = tag.substr(0, tag.find_first_of("-_"));
  auto is = [&](const char* l) { return tag.size() == 2 && (tag[0] | 32) == l[0] && (tag[1] | 32) == l[1]; };
  return is("zh") || is("ja") || is("ko") ? MarkClass::Cjk : MarkClass::Latin;
}

struct EmitEnv {
  Arena& arena;
  DiagSink& diags;
  Interner& strs;
  StyleTable& styles;
  EmitSettings cfg;
  const MeasureNeeds* mathText = nullptr;  // text-font runs in formulas (math-design §14)
  const ResourceTable* rt = nullptr;      // answered code tokens and image sizes
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
  // an image's size: the host's intrinsic size fills only what the author
  // left out (defect #24; plan P1-19: the answer lives in the resource
  // table, never in the author's args) — a declared w (or h) stays, the
  // other side follows the image's aspect ratio
  void imageDims(StrRef src, double& iw, double& ih) const {
    if ((iw > 0 && ih > 0) || !rt) return;
    const BoxNeed* bx = rt->box(BoxKind::Image, src);
    if (!bx || bx->st != ResState::Ready) return;
    if (iw > 0) ih = iw * bx->h / bx->w;
    else if (ih > 0) iw = ih * bx->w / bx->h;
    else iw = bx->w, ih = bx->h;
  }
  // the style's em (sizePx honoured): one formula with measurement (P0-08)
  double fontPx(StyleId st) const { return emPx(cfg, styles.get(st)); }
};

struct ICtx {
  StyleDelta add;  // what enclosing kinds put on the text (inline code: mono)
  u16 addFlags = 0;  // BF_REF: resolver-synthesized content
  float mul = 1.0f;
  LinkTarget url;  // the link the text is in
  bool noHyphen = false;  // display context (headings)
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
  // the unit's inline stream is complete
  virtual void finish(Flow& u) = 0;
  // a cell's stream (table cell, caption row, sidecar line) moves to its cell
  virtual void toCell(Flow& tmp, Flow& tc) = 0;
  // the whole document is emitted (cross-unit passes)
  virtual void done(std::vector<TopBlock>& tops) = 0;
};

struct BoxTree;
// shapes every leaf of the box tree through the sink
std::vector<TopBlock> emitWith(const BoxTree& bt, EmitEnv& env, InlineSink& sink);

}  // namespace tsr
