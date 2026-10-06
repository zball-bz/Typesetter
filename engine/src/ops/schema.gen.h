// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
#pragma once
#include <cstdint>

namespace tsr {

constexpr std::uint8_t OPS_VERSION = 13;
constexpr std::uint8_t OPS_MIN_COMPAT = 11;
constexpr const char* SCHEMA_HASH = "42836f3b";
constexpr std::uint16_t KIND_COUNT = 38;
constexpr std::uint16_t ARGK_COUNT = 75;

enum class Level : std::uint8_t { Block, Inline, Adaptive, Transparent, Trivia };
enum class Body : std::uint8_t { None, Inline, Blocks, Items, Code, Position, Rows, Cells, Data, Text };
// what a kind becomes in an inline stream (the shaper's flatten table, plan P1-13)
enum class InlineShape : std::uint8_t { Text, Container, Code, Object, Break, Fill, Error, Skip, Unsupported };
enum class Dom : std::uint8_t { Bool, Int, Num, Str, Token, Ident, Label, Lang, Enum, Flags, RangeSet, Color, Font, Html, Url, Text, Ext, Delta };

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
  std::uint8_t textDom;  // Text: its TextDomain
  bool resolved;  // set by the resolver only: dropped from input (plan P2-05)
};

// a declaration type (plan P2-05; schema "decls"): hoisted = the last of a
// name wins, else positional
struct DeclInfo {
  const char* name;
  bool hoisted;
  std::uint8_t since;
};
constexpr std::uint16_t DECL_COUNT = 12;
extern const DeclInfo kDecls[DECL_COUNT];  // indexed by id (0: none)

// a slot (plan P2-16; schema "slots"): a value of the universal slot
// attribute a kind gives a meaning; its child is a part, exempt from the
// parent's body model, its own kids at the slot's model
enum class SlotId : std::uint8_t { None, Margin, Extra, Tag };
struct SlotInfo {
  const char* name;
  Body model;
  bool anyBlock;  // on every block-level kind
  const std::uint16_t* kinds;  // else on these
  std::uint8_t nKinds;
};
constexpr std::uint8_t SLOT_COUNT = 4;
extern const SlotInfo kSlots[SLOT_COUNT];  // indexed by SlotId

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
constexpr std::uint8_t kOpSince[] = {0, 6, 6, 6, 6, 6, 6, 7, 8, 8, 9};

}  // namespace tsr
