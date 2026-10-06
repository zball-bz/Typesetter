// Semantic flow serializer (v2 §9, document-model §9.2): the content tree
// rendered as ordinary flow HTML that the browser breaks itself. One
// component, four uses: first paint (progressive upgrade base), SEO/no-JS
// readers, and static export. Paragraph-level elements carry data-pid (the
// upgrade swap key), data-s/e, and label anchors ("tsr-<label>") — the
// resolver ran first, so numbers and refs are already final (§11.1).
#pragma once
#include <functional>

#include "../model/model.h"

namespace tsr {

// rt: answered code tokens render as their styled runs (plan P1-19)
class ResourceTable;
class Registry;
class Cascade;
class NodePropsTable;
class MathEnv;
class Arena;
// (plan P3-27; design T7 S13) how the page shows a formula: its box
// (render.math boxes — the typeset page's glyph boxes, text runs estimated
// before measurement; role=math with its source as aria-label when
// a11y.mathLabel), else its source
struct SemanticMath {
  bool boxes = false;
  bool label = false;
  double basePx = 16;
  const MathEnv* env = nullptr;  // the declarations its names bind to
  Arena* arena = nullptr;        // where the boxes live
};
// reg: the element registry, for each class's semantic element (plan P2-05);
// cascade: where a document env begins, the page marks it (data-tsr-env:
// rulesToCss, plan P3-01); props: a code block's overlays (plan P3-22: the
// key of its tokens)
std::string renderSemantic(const ContentTree& tree, Interner& strs, StyleTable& styles,
                           const ResourceTable* rt = nullptr, const Registry* reg = nullptr,
                           const Cascade* cascade = nullptr, const NodePropsTable* props = nullptr,
                           const SemanticMath& math = {});

// (plan P3-06; design T7 ops.fragment) a preview: the content of the element
// a label identifies on the semantic page — a list item's when the label is
// its first block's (the page hoists it onto the <li>: a footnote's body) —
// without the references `backlink` says lead back to where the preview is
// shown (a note's ↩); the caller suppresses ids (AnchorScope). "" when no
// node carries the label.
std::string renderSemanticFragment(const ContentTree& tree, Interner& strs, StyleTable& styles,
                                   const ResourceTable* rt, const Registry* reg, const Cascade* cascade,
                                   const NodePropsTable* props, std::string_view label,
                                   const std::function<bool(StrRef)>& backlink, const SemanticMath& math = {});

}  // namespace tsr
