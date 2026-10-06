// SurfaceLexer primitives (plan P1-06; design T1 SurfaceLexer). Every scan
// runs over a bounded view (a LeafText, or a prefix of one), so none can leave
// its leaf; '\n' in a view is a line join. The inline parser, the content-body
// bracket counter and the region cell splitter share these, so they agree on
// where an atom — an escape, a code span, a math island, an inline comment, a
// splice head with its JS — begins and ends ("verbatim islands first").
#pragma once
#include <unordered_map>

#include "syntax.gen.h"

namespace tsr {

// `` ``a`b`` ``: a run of N backticks closes at the next run of exactly N
// (the fence run-length rule); body = (bodyStart, bodyEnd)
struct CodeSpanLex {
  u32 run = 0, bodyStart = 0, bodyEnd = 0, end = 0;
};
bool lexCodeSpan(std::string_view t, u32 i, CodeSpanLex& out);
// the code span's text: joins read as spaces; one leading and one trailing
// space are stripped when both are present and the body is not all spaces
std::string codeSpanText(std::string_view body);

// '$'…'$': `close` is the closing '$' ('\' escapes the next byte for
// delimiting); the island needs a non-empty body
bool lexMath(std::string_view t, u32 i, u32& close);
// a math body with '\$' decoded (the only escape the island decodes)
std::string mathText(std::string_view body);

// '%--' … '--%', nesting; `end` is one past the closer
bool lexComment(std::string_view t, u32 i, u32& end);

constexpr u32 kNoPos = ~0u;

// A splice after '#': '(' JS ')' or a head chain
//   SpliceHead SpliceCont* ('.' SpliceHead SpliceCont* | '(' JS ')')*
// `lastCall` is the '(' of a trailing call (0 = none); `jsEnd` is where a
// failed JS scan stopped (for the diagnostic); `openAt` is a '(' whose JS
// ran out of text before it balanced (the line pass treats it as an open
// construct, plan P1-08), else kNoPos.
struct SpliceLex {
  bool paren = false;
  u32 end = 0, lastCall = 0, jsEnd = 0, openAt = kNoPos;
};
bool lexSplice(std::string_view t, u32 hash, SpliceLex& out);

// A keyword form after '#' (plan P2-12; syntax.def KEYWORD rows):
//   '#' kw Blank* '(' JS ')' Blank* '[' content ']'
// and, for an elseChain keyword, after each body:
//   Blank* 'else' Blank* ('if' Blank* '(' JS ')' Blank*)? '[' content ']'
// Blank is a space or a tab. The callers match the bodies (a block body
// closes on a later line). `openAt`: a '(' whose JS ran out of text (an
// open construct, as lexSplice's).
struct KwHead {
  int kw = -1;                     // kKeywords index
  u32 headStart = 0, headEnd = 0;  // the JS inside the parentheses
  u32 bodyOpen = kNoPos;           // its first body's '['
  u32 openAt = kNoPos;
};
bool lexKeywordHead(std::string_view t, u32 hash, KwHead& out);
// an else link at p (one past a body's ']'): `cond` set for `else if`
struct KwElse {
  bool cond = false;
  u32 headStart = 0, headEnd = 0;
  u32 bodyOpen = kNoPos;
  u32 openAt = kNoPos;
};
bool lexElse(std::string_view t, u32 p, KwElse& out);

// One past the atom starting at t[i], or i when none starts there.
u32 atomEnd(std::string_view t, u32 i);

// '[' … ']' matching over a bounded view, memoised (one scan records every
// bracket it passes). IslandAware skips atoms (the content-body counter);
// Plain only honours escapes — the reading a body falls back to when an island
// would swallow its closer, which then bounds the islands inside it (P0-04:
// `[price $5](u) and $x$` is a link).
class BracketMatcher {
 public:
  enum Mode : u8 { IslandAware, Plain };
  explicit BracketMatcher(std::string_view t) : t_(t) {}
  i32 match(u32 open, Mode m);
  // a content body's closer: island-aware, else plain
  i32 body(u32 open) {
    i32 c = match(open, IslandAware);
    return c >= 0 ? c : match(open, Plain);
  }

 private:
  std::string_view t_;
  std::unordered_map<u32, i32> memo_[2];
};

}  // namespace tsr
