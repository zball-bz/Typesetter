// Code token folding (code-design.md §2/§3): the engine never tokenizes —
// tokens arrive from a provider (web-tree-sitter in the worker; statically
// linked tree-sitter in native tests) through the NEED_TOKENS pull state,
// and fold into the codeblock's structured-line form (CH1).
#pragma once
#include "../model/model.h"
#include "../syntax/syntax.gen.h"

namespace tsr {

// The tag set (kTokenTags: tree-sitter highlight-capture names, first
// segment) is the TOKEN_TAGS row of syntax.def, shared with the worker's
// providers through runtime/src/shared/syntax.gen.mjs.

struct CodeToken {
  u32 start = 0, end = 0;  // byte range into the code body
  u8 tag = 0;              // index into kTokenTags
};

// capture name → tag index by first dotted segment (-1 = unknown, skip)
int tokenTagFromCapture(std::string_view name);

// Rewrites a plain-body codeblock (single text child) into per-line seq
// children of styled text runs. Tokens must be sorted, non-overlapping.
// Token color = "var(--tsr-tok-<tag>)" — theming lives entirely in CSS.
void foldTokens(ContentNode* cb, const CodeToken* toks, size_t n,
                Arena& arena, Interner& strs, StyleTable& styles);

}  // namespace tsr
