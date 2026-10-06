// GENERATED from engine/src/syntax/syntax.def by tools/gen-syntax.mjs — do not edit.
#pragma once
#include <string_view>

#include "../support/support.h"

namespace tsr {

constexpr u32 SYNTAX_VERSION = 5;

// character classes
inline bool isSpliceHead(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$'; }
inline bool isSpliceCont(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '$'; }
inline bool isIdStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
inline bool isIdCont(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
inline bool isIdJoin(char c) { return c == '-' || c == '.' || c == ':'; }
inline bool isLabelChar(char c) { return !((unsigned char)c < 0x20 || c == 0x7f || c == ' ' || c == '<' || c == '>' || c == '[' || c == ']' || c == '@' || c == ',' || c == ';' || c == '\\'); }
inline bool isEscapable(char c) { return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~'); }

// sugar: a built-in Call's slot (its meaning); the payload struct follows
enum class SugarId : u16 { para, heading, list, item, quote, rule, fence, region, strong, em, code, link, note, ref, linebreak, math, arg };
constexpr const char* kSugarName[] = {"para", "heading", "list", "item", "quote", "rule", "fence", "region", "strong", "em", "code", "link", "note", "ref", "linebreak", "math", "arg"};
constexpr u32 kSugarCount = 17;

// payloads (zero-width side records trailing their node)
struct HeadingP {
  u8 level = 0;
  StrRef label = 0;
};
struct ListP {
  bool ordered = 0;
  i32 start = 0;
};
struct FenceP {
  StrRef lang = 0;
  Span args{};
  u32 bodyOffset = 0;
  u32 bodyEnd = 0;
  StrRef lines = 0;
  StrRef info = 0;
  StrRef label = 0;
};
struct RegionP {
  Span args{};
  StrRef label = 0;
};
struct LinkP {
  StrRef url = 0;
};
struct MathP {
  bool display = 0;
  StrRef label = 0;
};
struct SpliceP {
  StrRef expr = 0;
  u32 lastCall = 0;
  bool named = 0;
};
struct StmtP {
  bool let = 0;
  StrRef js = 0;
  bool content = 0;
};
struct ErrorP {
  StrRef message = 0;
};
struct TextP {
  StrRef rawmap = 0;
  StrRef seps = 0;
};
struct BranchP {
  Span head{};
};

// inline rules (INLINE rows): precedence and body mode per rule
enum class InlineRule : u8 { none, code, math, comment, url, splice, strong, em, link, note, ref, refs, brk };
enum class InlinePrec : u8 { Island, Comment, Markup };
enum class InlineBody : u8 { Verbatim, CallChain, Pair, LinkText, Content, Ident, IdList, None };
constexpr InlinePrec kInlinePrec[] = {InlinePrec::Markup, InlinePrec::Island, InlinePrec::Island, InlinePrec::Comment, InlinePrec::Island, InlinePrec::Markup, InlinePrec::Markup, InlinePrec::Markup, InlinePrec::Markup, InlinePrec::Markup, InlinePrec::Markup, InlinePrec::Markup, InlinePrec::Markup};
constexpr InlineBody kInlineBody[] = {InlineBody::Pair, InlineBody::Verbatim, InlineBody::Verbatim, InlineBody::Verbatim, InlineBody::Verbatim, InlineBody::CallChain, InlineBody::Pair, InlineBody::Pair, InlineBody::LinkText, InlineBody::Content, InlineBody::Ident, InlineBody::IdList, InlineBody::None};
// the rule whose literal opener starts at t[i] (longest first)
inline InlineRule inlineOpener(std::string_view t, u32 i) {
  switch (t[i]) {
    case '`':
      return InlineRule::code;
    case '$':
      return InlineRule::math;
    case '%':
      if (t.substr(i, 3) == "%--") return InlineRule::comment;
      break;
    case ':':
      if (t.substr(i, 3) == "://") return InlineRule::url;
      break;
    case '#':
      return InlineRule::splice;
    case '*':
      return InlineRule::strong;
    case '_':
      return InlineRule::em;
    case '[':
      return InlineRule::link;
    case '^':
      if (t.substr(i, 2) == "^[") return InlineRule::note;
      break;
    case '@':
      if (t.substr(i, 2) == "@[") return InlineRule::refs;
      return InlineRule::ref;
    case '\\':
      return InlineRule::brk;
    default:
      break;
  }
  return InlineRule::none;
}
// bytes that may start an inline rule (everything else is plain text)
constexpr bool kInlineOpenerByte[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// a bare splice head that cannot start a JS expression (plan P0-05)
inline const char* reservedSpliceHead(std::string_view w) {
  static constexpr std::string_view kUnsupported[] = {"if", "else", "for", "while", "let"};
  static constexpr std::string_view kReserved[] = {"break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "export", "extends", "finally", "function", "import", "in", "instanceof", "new", "return", "switch", "throw", "try", "typeof", "var", "void", "with", "yield", "static", "enum", "await"};
  for (std::string_view k : kUnsupported)
    if (w == k) return "keyword-unsupported";
  for (std::string_view k : kReserved)
    if (w == k) return "reserved-word";
  return nullptr;
}

// the keyword forms (plan P2-12): #kw (JS) [content]; an elseChain one continues
// with else [content] / else if (JS) [content]
struct KeywordRow {
  std::string_view name;
  bool elseChain;
};
constexpr KeywordRow kKeywords[] = {{"if", true}, {"for", false}, {"while", false}};
inline int keywordIndex(std::string_view w) {
  for (int k = 0; k < (int)(sizeof kKeywords / sizeof kKeywords[0]); k++)
    if (w == kKeywords[k].name) return k;
  return -1;
}

// highlight token tags (shared with runtime/src/shared/syntax.gen.mjs)
constexpr const char* kTokenTags[] = {"keyword", "string", "number", "comment", "function", "type", "constant", "variable", "operator", "punctuation", "property", "attribute", "label", "embedded"};
constexpr int kTokenTagCount = 14;

}  // namespace tsr
