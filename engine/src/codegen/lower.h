// The LowerProgram (plan P2-02; design T2 S5; docs/lowering-design.md): a
// document's markup as one binary program of constructor calls, with holes
// where the user's JavaScript goes — that code alone lives in the hole
// module. codegen writes the format (lower.def), runtime/src/shared/lower.mjs
// interprets it; the reader here validates and dumps it (tsrc --stage=lower,
// the fuzz target fuzz_lower).
//
// Layout (all integers LEB128 varints unless sized):
//   "TSLP" version:u8 abi:u32le hash:u64le moduleFlag docEnd
//   nStr (byteLen u16Len)*nStr blob             strings: UTF-8, valid
//   nCtor strRef*nCtor                           constructor names
//   nBlock (kind:u8 flags:u8 s e holeLo holeHi pc)*nBlock
//   nHole
//   nPiece (kind:u8 ref js0 js1)*nPiece          js0/js1: UTF-16 offsets
//   bodyLen body                                  preorder ops (lower.def)
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../support/support.h"
#include "lower.gen.h"

namespace tsr {

struct LBlockRow {
  LBlock kind = LBlock::Content;
  u8 flags = 0;  // kBlock*
  u32 s = 0, e = 0, holeLo = 0, holeHi = 0, pc = 0;
};
struct LPieceRow {
  LPiece kind = LPiece::Hole;
  u32 ref = 0;           // Hole: the hole; Verbatim: the block
  u32 js0 = 0, js1 = 0;  // its UTF-16 range in the hole module
};

// The writer codegen fills: strings and constructor names are deduplicated.
class LowerWriter {
 public:
  std::string body;
  std::vector<LBlockRow> blocks;
  std::vector<LPieceRow> pieces;
  u32 holes = 0;

  u32 str(std::string_view s);
  u32 ctor(std::string_view name);
  // an op byte; its position, for setting the async bit once the subtree
  // is written (markAsync)
  size_t op(Lop o) {
    body += (char)(u8)o;
    return body.size() - 1;
  }
  void markAsync(size_t at) { body[at] = (char)((u8)body[at] | kLopAsync); }
  void u(u32 v);
  void constNull() { body += (char)(u8)LConst::Null; }
  void constBool(bool b) { body += (char)(u8)(b ? LConst::True : LConst::False); }
  void constUint(u32 v) {
    body += (char)(u8)LConst::Uint;
    u(v);
  }
  void constStr(std::string_view s) {
    body += (char)(u8)LConst::Str;
    u(str(s));
  }
  void constArrayHead(u32 n) {
    body += (char)(u8)LConst::Array;
    u(n);
  }
  // the program bytes (hash: the hole module's, 0 = no module)
  std::string finish(u64 hash, bool module, u32 docEnd) const;

 private:
  std::vector<std::string> strs_;
  std::unordered_map<std::string, u32> strIdx_;
  std::vector<u32> ctors_;
};

// A program read back and validated: every count, reference and offset in
// range, the strings valid UTF-8 of the stated UTF-16 lengths, the blocks
// tiling the body, and each block's ops parsing as the grammar of lower.def.
struct LowerProgram {
  u32 version = 0, abi = 0;
  u64 hash = 0;
  bool module = false;
  u32 docEnd = 0;
  std::vector<std::string_view> strs;
  std::vector<u32> ctors;
  std::vector<LBlockRow> blocks;
  u32 holes = 0;
  std::vector<LPieceRow> pieces;
  std::string_view body;
};
bool readLowerProgram(std::string_view bytes, LowerProgram& p, std::string& why);

// tsrc --stage=lower: the program as text (the *.lower.txt goldens); a
// malformed program dumps as "invalid: <why>"
std::string dumpLowerProgram(std::string_view bytes);

u64 fnv1a64(std::string_view s);
// UTF-8 as a WHATWG decoder reads it: every invalid sequence becomes U+FFFD
// (what V8 and TextDecoder make of the same bytes)
void appendUtf8Sanitized(std::string& out, std::string_view s);
// UTF-16 code units of valid UTF-8
u32 utf16Length(std::string_view s);

}  // namespace tsr
