#pragma once
#include "../ast/ast.h"
#include "lower.h"

namespace tsr {

// A compiled document (plan P2-02; docs/lowering-design.md): the markup as a
// LowerProgram of constructor calls, and the hole module — the user's
// JavaScript only (empty when the document has none).
struct Lowered {
  std::string program;
  std::string js;
};

// AST → LowerProgram + hole module. runtime/src/shared/lower.mjs runs the
// program; spans ride on its ops (SPAN at execution).
Lowered codegen(const AstNode* doc, const SourceText& src, const Interner& strs);

// Fragments (plan P2-13; docs/lowering-design.md §6): markup parsed at run
// time — m`…`, m.parse, ctx.m.parse, a code block's sidecar notes — as one
// LowerProgram the same interpreter runs: block i is text i, as a content
// body (one paragraph is its inline content). Its spans are `base` plus its
// own offsets, or all `clamp` (a text with no place in the source). No
// JavaScript is generated or evaluated: Lowered.js is, out of band, JSON
// {"holes": [descriptor…], "diags": [{sev, code, msg, s, e}…]} — a hole is
// {"v": k}, the k-th interpolation (written `#(__mk);` in the text), or
// {"p": "a.b"}, a bare value head looked up on the scope (D-L07), with
// "k": 1 when it takes content arguments ("a": 1 when they await). Any other
// splice, a statement, a keyword form or an argument list stays text (info
// fragment-splice).
struct FragmentText {
  std::string text;
  u32 base = 0;
};
Lowered codegenFragments(const std::vector<FragmentText>& texts, const Span* clamp);

// tsr2_fragments' wire form. Request: [u32 n][u8 clamped][u32 s][u32 e],
// then per text [u32 base][u32 len][len bytes] (u32 little-endian).
// Response: [u32 len][program][u32 len][the JSON] ({"error": …} and an
// empty program for a malformed request).
struct FragmentRequest {
  std::vector<FragmentText> texts;
  bool clamped = false;
  Span clamp;
};
bool decodeFragmentRequest(std::string_view bytes, FragmentRequest& out);
std::string runFragmentRequest(std::string_view request);

}  // namespace tsr
