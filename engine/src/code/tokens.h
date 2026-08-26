// Code token folding (code-design.md §2/§3): the engine never tokenizes —
// tokens arrive from a provider (web-tree-sitter in the worker; statically
// linked tree-sitter in native tests) through the NEED_TOKENS pull state,
// and fold into the codeblock's structured-line form (CH1).
#pragma once
#include "../model/model.h"

namespace tsr {

// Fixed tag set: tree-sitter highlight-capture names, first segment.
// Shared contract with runtime/src/worker/tokens.mjs — keep in sync.
constexpr const char* kTokenTags[] = {
    "keyword", "string", "number", "comment", "function", "type",
    "constant", "variable", "operator", "punctuation", "property",
    "attribute", "label", "embedded",
};
constexpr int kTokenTagCount = 14;

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
