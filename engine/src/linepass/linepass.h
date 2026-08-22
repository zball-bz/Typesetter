// Line-structure pass (v2 §4, App B): container tree with quotes and lists,
// leaf blocks (para, heading, fence, rule, code statements, comments).
// No lazy continuation; content columns per App B.
#pragma once
#include "../source/source.h"

namespace tsr {

enum class SkelKind : u8 {
  Doc, Para, Heading, List, Item, Quote, Fence, Rule, CodeLet, CodeBlock, Comment
};

struct SkelNode {
  SkelKind kind;
  Span span;
  Span inner;                   // code stmts: JS; Comment: body
  std::vector<Span> lineSpans;  // Para/Heading/Fence: per-line content spans
  Span langSpan;                // Fence: info string
  u8 level = 0;                 // Heading
  bool ordered = false;         // List
  int start = 1;                // List (ordered)
  std::vector<SkelNode*> kids;  // Doc/List/Item/Quote
};

struct Skeleton {
  SkelNode* root = nullptr;
};

Skeleton linepass(const SourceText& src, Arena& arena, DiagSink& diags);
std::string dumpSkeleton(const Skeleton& sk, const SourceText& src);

}  // namespace tsr
