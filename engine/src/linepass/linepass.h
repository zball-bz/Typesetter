// Line-structure pass (v2 §4, App B; plan P1-07, design T1 BlockAutomaton):
// one container protocol — Prefix (quote), Column (list item), Explicit
// (region) — matched on every physical line; leaf blocks (para, heading,
// fence, rule, code statements, comments). No lazy continuation; content
// columns per App B, a tab advancing to the next multiple of 4.
#pragma once
#include "../source/source.h"

namespace tsr {

enum class SkelKind : u8 {
  Doc, Para, Heading, List, Item, Quote, Fence, Rule, CodeLet, CodeBlock, Comment,
  Region,  // #!name(args) … #name! (v2 §4.1); langSpan=name, inner=args
  Error    // block-granular parse error (plan P0-05): errCode, errMsg
};

struct SkelNode {
  SkelKind kind;
  Span span;                    // containers: every line attributed to them
  Span inner;                   // code stmts: JS (a content literal: its name); Comment: body (raw)
  std::vector<Span> lineSpans;  // Para/Heading/Fence/Comment: per-line content
                                // spans (container prefixes stripped); code
                                // stmts: the JS's lines (a content literal:
                                // its body's)
  bool content = false;         // CodeLet: `#let x = [ … ]`, a content literal (plan P2-12)
  Span langSpan;                // Fence: info string; Region: name
  Span labelSpan;               // Heading / Region: trailing <id> label (empty = none)
  Span termSpan;                // Item of a description list (plan P3-34): its term
  u8 level = 0;                 // Heading
  bool ordered = false;         // List: marker != '-'
  char marker = 0;              // List: marker class '-', '+', '.' (N.) or '/' (a description list, P3-34)
  u32 markerCol = 0;            // List: the markers' column
  int start = 1;                // List (ordered)
  bool contained = false;       // Fence: inside a quote or list item (its
                                // lines are not contiguous in the source)
  std::vector<Span> bodies;     // Para: block-form content bodies, from the
                                // '[' to the ']' of their closer line
  std::vector<u32> literalAt;   // Para: openers that reached their bound
                                // without a closer (reverted: literal text),
                                // ascending
  std::vector<SkelNode*> kids;  // Doc/List/Item/Quote
  const char* errCode = nullptr;  // Error
  std::string errMsg;             // Error
};

// A reverted carry: the opener's line and the line where it hit its bound
// (for incremental re-lexing, design T1 I8).
struct RevertedWindow {
  u32 openerLine = 0, boundLine = 0;
};

struct Skeleton {
  SkelNode* root = nullptr;
  std::vector<RevertedWindow> windows;
};

// The syntactic nesting the front end builds (the parse's InstLimits, plan
// P0-07): containers, content bodies and inline pairs nest at most
// kMaxNesting deep along any path. A deeper container marker is text, a
// deeper body is cut to an error node (nest-limit), so every walk of the
// tree stays shallow and codegen's program stays inside its reader's bound.
constexpr u32 kMaxNesting = 128;

Skeleton linepass(const SourceText& src, Arena& arena, DiagSink& diags);
// The same pass over any list of raw line slices (a content body, plan P1-08)
// `depth` levels deep.
Skeleton linepassLines(const SourceText& src, const std::vector<Span>& lines, Arena& arena,
                       DiagSink& diags, u32 depth = 0);
std::string dumpSkeleton(const Skeleton& sk, const SourceText& src);

}  // namespace tsr
