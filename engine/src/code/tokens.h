// Code tokens (code-design.md §2/§3): the engine never tokenizes a host
// language — tokens arrive from a provider (web-tree-sitter in the worker;
// statically linked tree-sitter in native tests) as codeTokens resources
// (resource/resources.def); 'tsm' is answered in-engine.
#pragma once
#include "../model/model.h"
#include "../syntax/syntax.gen.h"

namespace tsr {

// The tag set (kTokenTags: tree-sitter highlight-capture names, first
// segment) is the TOKEN_TAGS row of syntax.def, shared with the worker's
// providers through runtime/src/shared/syntax.gen.mjs.

constexpr u8 kTokenTagComment = 3;  // kTokenTags[3] is "comment" (unitTokens checks)

struct CodeToken {
  u32 start = 0, end = 0;  // byte range into the code body
  u8 tag = 0;              // index into kTokenTags
};

// capture name → tag index by first dotted segment (-1 = unknown, skip)
int tokenTagFromCapture(std::string_view name);

// A provider's tokens are acceptable (plan P1-19; design T9 A1): sorted,
// disjoint, non-empty, inside the body, on UTF-8 boundaries, known tags.
// Anything else fails the whole answer (plain code, provider-invalid).
bool validTokens(std::string_view body, const CodeToken* toks, size_t n);

// The token answer over a plain code body (code-design.md §2/§3; plan P1-19:
// the answer lives in the ResourceTable and the tree is never rewritten):
// per line, its runs — a token is made under the body (Cascade.make, plan
// P3-01) as text of class tok-<tag>, so the rules of `env` style it (the
// defaults: a comment italic, hanging at its content), with the tag's colour
// "var(--tsr-tok-<tag>)" as its own; interned once per tag; untokenized
// stretches keep the base style. Emit reads it with the cascade, the
// semantic page without (its scope: the colour only; rules are its CSS).
class Cascade;
struct TokenRun {
  std::string_view text;
  StyleId style = 0;  // its tag's
  int tag = -1;       // its token tag (class tok-<tag>), -1: none
};
void tokenLines(std::string_view body, StyleId base, const Cascade* cascade, u32 env, const CodeToken* toks,
                size_t n, Interner& strs, StyleTable& styles, std::vector<std::vector<TokenRun>>& lines);

}  // namespace tsr
