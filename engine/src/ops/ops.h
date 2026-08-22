// Op buffer decoding (document-model §4). The writer lives in runtime JS.
#pragma once
#include "../support/support.h"

namespace tsr {

constexpr u8 OPS_VERSION = 1;

enum class Op : u8 {
#define OP(n, c) n = c,
#define KIND(n, c)
#define ARGK(n, c)
#include "ops.def"
#undef OP
#undef KIND
#undef ARGK
};

enum class Kind : u16 {
#define OP(n, c)
#define KIND(n, c) n = c,
#define ARGK(n, c)
#include "ops.def"
#undef OP
#undef KIND
#undef ARGK
};
constexpr u16 KIND_COUNT = 26;

enum class ArgK : u16 {
#define OP(n, c)
#define KIND(n, c)
#define ARGK(n, c) n = c,
#include "ops.def"
#undef OP
#undef KIND
#undef ARGK
};

const char* kindName(Kind k);
const char* argName(ArgK a);

enum class ArgTag : u8 { Null = 0, Bool = 1, Num = 2, Str = 3, Node = 4 };
struct ArgVal {
  ArgK key;
  ArgTag tag;
  double num = 0;   // Bool: 0/1; Num: value
  u32 ref = 0;      // Str: StrRef (raw-buffer index); Node: node id
};

struct RawNode {
  Kind kind;
  Span span;                 // set by SPAN ops; default empty (synthetic)
  StrRef str = 0;            // MAKE_TEXT payload (raw-buffer string index)
  bool isText = false;
  std::vector<ArgVal> args;
  std::vector<u32> children;
};

struct SchedItem {
  Op op;          // EMIT | STYLE_PUSH | STYLE_POP_TO
  u32 a = 0;      // EMIT: node id; STYLE_POP_TO: height
  u64 bits = 0;   // STYLE_PUSH: class bits delta
};

// Decoded, validated buffer. Strings live in the buffer's own table; the
// model instantiation re-interns what it keeps.
struct RawOps {
  std::vector<std::string_view> strings;  // views into `blob`
  std::string blob;
  std::vector<RawNode> nodes;
  std::vector<SchedItem> sched;
  bool ok = false;
};

RawOps decodeOps(const u8* buf, size_t len, DiagSink& diags);
std::string dumpOps(const RawOps& r);

}  // namespace tsr
