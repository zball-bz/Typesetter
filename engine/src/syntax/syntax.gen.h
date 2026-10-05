// GENERATED from engine/src/syntax/syntax.def by tools/gen-syntax.mjs — do not edit.
#pragma once
#include <string_view>

#include "../support/support.h"

namespace tsr {

constexpr u32 SYNTAX_VERSION = 1;

// character classes
inline bool isSpliceHead(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$'; }
inline bool isSpliceCont(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '$'; }
inline bool isIdStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
inline bool isIdCont(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
inline bool isIdJoin(char c) { return c == '-' || c == '.' || c == ':'; }
inline bool isEscapable(char c) { return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~'); }

// sugar: a built-in Call's slot (its meaning); the payload struct follows
enum class SugarId : u16 { para, heading, list, item, quote, rule, fence, region, strong, em, code, link, note, ref, math, arg, row, cell };
constexpr const char* kSugarName[] = {"para", "heading", "list", "item", "quote", "rule", "fence", "region", "strong", "em", "code", "link", "note", "ref", "math", "arg", "row", "cell"};
constexpr u32 kSugarCount = 18;

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
};
struct RegionP {
  Span args{};
};
struct LinkP {
  StrRef url = 0;
};
struct MathP {
  bool display = 0;
  StrRef label = 0;
};
struct SpliceP {
  Span expr{};
  u32 lastCallStart = 0;
};
struct StmtP {
  bool let = 0;
  Span js{};
};
struct ErrorP {
  StrRef message = 0;
};

// a bare splice head that cannot start a JS expression (plan P0-05)
inline const char* reservedSpliceHead(std::string_view w) {
  static constexpr std::string_view kUnsupported[] = {"if", "else", "for", "while", "use", "let"};
  static constexpr std::string_view kReserved[] = {"break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "export", "extends", "finally", "function", "import", "in", "instanceof", "new", "return", "switch", "throw", "try", "typeof", "var", "void", "with", "yield", "static", "enum", "await"};
  for (std::string_view k : kUnsupported)
    if (w == k) return "keyword-unsupported";
  for (std::string_view k : kReserved)
    if (w == k) return "reserved-word";
  return nullptr;
}

// highlight token tags (shared with runtime/src/shared/syntax.gen.mjs)
constexpr const char* kTokenTags[] = {"keyword", "string", "number", "comment", "function", "type", "constant", "variable", "operator", "punctuation", "property", "attribute", "label", "embedded"};
constexpr int kTokenTagCount = 14;

}  // namespace tsr
