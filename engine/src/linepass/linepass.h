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
  Span inner;                   // code stmts: JS; Comment: body (raw)
  std::vector<Span> lineSpans;  // Para/Heading/Fence/Comment: per-line content
                                // spans (container prefixes stripped)
  Span langSpan;                // Fence: info string; Region: name
  Span labelSpan;               // Heading: trailing <id> label (empty = none)
  u8 level = 0;                 // Heading
  bool ordered = false;         // List: marker != '-'
  char marker = 0;              // List: marker class '-', '+' or '.' (N.)
  u32 markerCol = 0;            // List: the markers' column
  int start = 1;                // List (ordered)
  bool contained = false;       // Fence: inside a quote or list item (its
                                // lines are not contiguous in the source)
  std::vector<SkelNode*> kids;  // Doc/List/Item/Quote
  const char* errCode = nullptr;  // Error
  std::string errMsg;             // Error
};

struct Skeleton {
  SkelNode* root = nullptr;
};

Skeleton linepass(const SourceText& src, Arena& arena, DiagSink& diags);
std::string dumpSkeleton(const Skeleton& sk, const SourceText& src);

}  // namespace tsr
