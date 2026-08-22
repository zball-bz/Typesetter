#pragma once
#include "../ast/ast.h"

namespace tsr {

struct JsProgram {
  std::string text;
};

// AST → one ES module: `export default async (ctors, $) => { … }`.
// Markup styling compiles to structural constructors (em/strong); spans are
// attached post-hoc via __at(node, s, e) (SPAN op).
JsProgram codegen(const AstNode* doc, const SourceText& src, const Interner& strs);

}  // namespace tsr
