// Op buffer decoding (document-model §4). The writer lives in runtime JS.
#pragma once
#include <deque>

#include "../support/support.h"
#include "schema.gen.h"

namespace tsr {

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

constexpr u32 kNoAlias = 0xFFFFFFFFu;

struct RawNode {
  Kind kind;
  Span span;                 // set by SPAN ops; default empty (synthetic)
  u32 alias = kNoAlias;      // AT (plan P2-04): an occurrence of node `alias`,
                             // instantiated with this node's span at its root
  std::vector<u32> rawmap;   // RAWMAP (plan P2-04): a text's cooked→raw
                             // breakpoints, (cooked, raw − span start) pairs
  StrRef str = 0;            // MAKE_TEXT payload (raw-buffer string index)
  bool isText = false;
  std::vector<ArgVal> args;
  std::vector<u32> children;
};

struct SchedItem {
  Op op;          // EMIT | STYLE_PUSH | STYLE_POP_TO
  u32 a = 0;      // EMIT: node id; STYLE_POP_TO: height
  u64 bits = 0;   // STYLE_PUSH: class bits delta
  std::vector<ArgVal> patch;  // STYLE_PUSH: InlineStyle patch (v2)
};

// Decoded, validated buffer. Strings live in the buffer's own table; the
// model instantiation re-interns what it keeps.
struct RawOps {
  std::vector<std::string_view> strings;  // views into `blob` (and `extra`)
  std::string blob;
  std::deque<std::string> extra;          // strings the reader synthesizes (error messages)
  std::vector<RawNode> nodes;
  std::vector<SchedItem> sched;
  u8 version = 0;  // the buffer's version byte (within OPS_MIN_COMPAT..OPS_VERSION)
  bool ok = false;
};

// Decodes IN PLACE into `out` (out is reset first). The strings are views
// into out.blob — decoding into the final home avoids dangling views when a
// short blob would sit in std::string's SSO buffer (a returned/moved RawOps
// would carry views into the moved-from object).
void decodeOps(const u8* buf, size_t len, RawOps& out, DiagSink& diags);
std::string dumpOps(const RawOps& r);

}  // namespace tsr
