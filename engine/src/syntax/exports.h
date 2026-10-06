// Front-end exports (plan P1-09; design T1 FrontEndExports): the engine is
// the authority on .tsm structure for editors, the 'tsm' fence highlighter
// and tools. Pure functions of the source (a scratch parse) or of an already
// parsed document; byte offsets into the UTF-8 source.
#pragma once
#include "../ast/ast.h"
#include "../code/tokens.h"
#include "../linepass/linepass.h"

namespace tsr {

// Highlight tokens in the 14-tag contract (kTokenTags): sorted,
// non-overlapping; a construct's markers and atoms get their own tags, plain
// prose none. Multi-line tokens are allowed (a fence body, a comment).
std::vector<CodeToken> syntaxTokens(const AstNode* doc, const SourceText& src, const Interner& strs);
std::vector<CodeToken> syntaxTokens(std::string_view source, const FrontEndOptions& opts = {});

// {"headings":[{level,title,label,span}], "regions":[{name,label,span}],
//  "fences":[{lang,span}], "labels":[{id,rule,span}],
//  "diagnostics":[{sev,code,span,message}], "frontMatter"?: {span, text}}
std::string outlineJson(const AstNode* doc, const SourceText& src, const Interner& strs,
                        const DiagSink& diags);
std::string outlineJson(std::string_view source, const FrontEndOptions& opts = {});

// The CallAST as JSON: {kind, sugar?, span, str?, …payload, kids?}
std::string astJson(const AstNode* doc, const SourceText& src, const Interner& strs);
std::string astJson(std::string_view source, const FrontEndOptions& opts = {});

// tsrc --stage=tokens: one line per token, `[s,e) tag "text"`
std::string dumpTokens(const std::vector<CodeToken>& toks, const SourceText& src);
// tokens as a compact JSON array [[s,e,tag],…] (the C ABI)
std::string tokensJson(const std::vector<CodeToken>& toks);

}  // namespace tsr
