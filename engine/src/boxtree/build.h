// The box tree builder (plan P1-18; design T6 BoxTreeBuilder): the resolved
// content tree → LayoutBlocks (block.h) + what emit shapes for each leaf.
// The only block-level dispatch on kinds and roles: roles are data (the role
// table in build.cc), never compared as spellings downstream.
#pragma once
#include "block.h"

#include "../api/config.h"
#include "../model/model.h"

namespace tsr {

class Cascade;

// what emit shapes for one leaf
struct LeafSource {
  enum class Role : u8 {
    Content,     // para / heading / error (inline content), codeblock, table,
                 // image, mathblock, raw, rule: the node itself
    Caption,     // a figure's paragraph: centred, unhyphenated, unindented
    MarkerOnly,  // an empty list item: its marker alone
  } role = Role::Content;
  const ContentNode* node = nullptr;
  Len paraIndent;  // 首行缩进 (App C): its par.indent (plan P3-01); unset = none
  // (plan P3-34; D-L08) a description item's term, run in at the start of
  // its first paragraph (emit: its content, then a space)
  const ContentNode* runIn = nullptr;
  // a float figure's caption paragraphs (the image leaf's caption rows), or
  // a code block's sidecar lines (its sidecar track)
  std::vector<const ContentNode*> rows;
  const ContentNode* sidecar = nullptr;  // a code block's sidecar group: not body
  // (plan P3-11) a code block's line in its two-track table: this leaf
  // shows its logical lines [lineLo, lineHi) (~0u: all)
  u32 lineLo = 0, lineHi = ~0u;
};

class MathEnv;
class NodePropsTable;
struct BoxTree {
  std::vector<TopTree> tops;
  std::vector<std::vector<LeafSource>> sources;  // per top, per unit
  const MathEnv* math = nullptr;  // the document's math declarations (plan P2-15)
  const Cascade* cascade = nullptr;  // the rules, for what emit makes (code tokens, plan P3-01)
};

class Registry;
BoxTree buildBoxTree(const ContentTree& tree, Interner& strs, StyleTable& styles, const NodePropsTable& props,
                     const BoxTreeSettings& cfg, const Registry& reg);

}  // namespace tsr
