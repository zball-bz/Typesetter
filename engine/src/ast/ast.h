#pragma once
#include "../linepass/linepass.h"

namespace tsr {

enum class AstKind : u8 {
  Doc, Para, CodeStmt, Text, Styled, Splice,
  Heading, ListB, Item, Quote, CodeBlockB, Rule, Comment, Link, Code, SpliceArg,
  Ref, Region, Row, Cell, Math
};

struct AstNode {
  AstKind kind;
  Span span;
  StrRef str = 0;   // Text; Code: code text; CodeBlockB: body; Comment: body;
                    // Ref: target label; Region: name; Math: formula source
  StrRef aux = 0;   // CodeBlockB: lang; Link: url; Heading: label
  u8 tag = 0;       // Styled: marker; CodeStmt: 0=let 1=block; Heading: level
  bool ordered = false;  // ListB
  int num = 1;           // ListB: start
  Span expr;             // Splice: expression; CodeStmt: inner JS
  u32 lastCallStart = 0; // Splice: '(' offset of trailing call, 0 if none
  std::vector<AstNode*> kids;  // Splice: SpliceArg children
};

AstNode* parseDoc(const SourceText& src, const Skeleton& sk, Arena& arena,
                  Interner& strs, DiagSink& diags);
std::string dumpAst(const AstNode* doc, const SourceText& src, const Interner& strs);

}  // namespace tsr
