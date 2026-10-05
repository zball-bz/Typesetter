// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
#pragma once
#include <cstdint>

namespace tsr {

constexpr std::uint8_t OPS_VERSION = 6;
constexpr std::uint8_t OPS_MIN_COMPAT = 6;
constexpr const char* SCHEMA_HASH = "accf561c";
constexpr std::uint16_t KIND_COUNT = 28;
constexpr std::uint16_t ARGK_COUNT = 31;

enum class Level : std::uint8_t { Block, Inline, Adaptive, Transparent, Trivia };
enum class Body : std::uint8_t { None, Inline, Blocks, Items, Code, Position, Rows, Cells, Data, Text };
// what a kind becomes in an inline stream (the shaper's flatten table, plan P1-13)
enum class InlineShape : std::uint8_t { Text, Container, Code, Object, Break, Error, Skip, Unsupported };
enum class Dom : std::uint8_t { Bool, Int, Num, Str, Token, Ident, Label, Lang, Enum, Flags, RangeSet, Color, Font, Html, Url };

// One attribute of one kind: its wire key, value domain and default.
struct AttrSpec {
  std::uint16_t key;
  const char* name;
  Dom dom;
  double lo, hi;   // Int / Num
  const char* const* members;  // Enum names, Flags names
  const std::uint8_t* bits;     // Flags bit positions
  std::uint8_t nMembers;
  bool boolAsInt;  // Int accepting true/false (lineNo)
  bool hasDef;
  double def;
  std::uint8_t since;
};

struct KindInfo {
  const char* name;
  Level level;
  Body body;
  InlineShape inl;
  std::uint8_t since;
  const AttrSpec* attrs;  // writer order
  std::uint8_t nAttrs;
};

extern const KindInfo kKinds[KIND_COUNT];  // indexed by Kind id
// the version an opcode first appeared in (0 = no such opcode)
constexpr std::uint8_t kOpSince[] = {0, 6, 6, 6, 6, 6, 6};

}  // namespace tsr
