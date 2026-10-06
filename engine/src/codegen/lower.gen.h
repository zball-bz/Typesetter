// GENERATED from engine/src/codegen/lower.def by tools/gen-lower.mjs — do not edit.
#pragma once
#include "../support/support.h"

namespace tsr {

constexpr u32 LOWER_VERSION = 1;
constexpr u32 LOWER_PROTOCOL = 2;
constexpr u32 PROGRAM_ABI = 0x3d3ccfbbu;
constexpr u8 kLopAsync = 0x80;

enum class Lop : u8 {
  TEXT = 1,
  CALL = 2,
  HOLE = 3,
  FRAME = 4,
  FENCE = 5,
  REGION = 6,
  STMT = 8,
  VERBATIM = 9,
  IF = 10,
  SCOPE = 11,
  LOOP = 12,
  LET = 13,
};
enum class LConst : u8 {
  Null = 0,
  False = 1,
  True = 2,
  Uint = 3,
  F64 = 4,
  Str = 5,
  Array = 6,
};
enum class LBlock : u8 {
  Content = 0,
  Stmt = 1,
  Verbatim = 2,
};
enum class LPiece : u8 {
  Hole = 0,
  Verbatim = 1,
};
constexpr u8 kBlockUser = 1;
constexpr u8 kBlockFramed = 2;
constexpr u8 kBlockAsync = 4;
constexpr u8 kCallSpanned = 1;

inline const char* lopName(u8 op) {
  switch (op & 0x7f) {
    case 1: return "TEXT";
    case 2: return "CALL";
    case 3: return "HOLE";
    case 4: return "FRAME";
    case 5: return "FENCE";
    case 6: return "REGION";
    case 8: return "STMT";
    case 9: return "VERBATIM";
    case 10: return "IF";
    case 11: return "SCOPE";
    case 12: return "LOOP";
    case 13: return "LET";
  }
  return nullptr;
}

}  // namespace tsr
