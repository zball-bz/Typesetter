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

}  // namespace tsr
