// libFuzzer target: the whole front end — line pass, inline parse, codegen
// (plan P0-03; testing.md §6) — and its exports (plan P1-09: tokens, outline,
// AST JSON), whose tokens must stay sorted, non-overlapping and in the source.
// Every program codegen writes must read back valid (plan P2-02), its hole
// pieces inside the module.
#include <cstddef>
#include <cstdint>
#include <string>

#include "../src/api/doc.h"

using namespace tsr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Doc doc;
  doc.compile(std::string(reinterpret_cast<const char*>(data), size));
  (void)dumpAst(doc.ast, doc.src, doc.strs);
  LowerProgram prog;
  std::string why;
  if (!readLowerProgram(doc.js.program, prog, why)) __builtin_trap();
  if (prog.module != !doc.js.js.empty()) __builtin_trap();
  const u32 jsUnits = utf16Length(doc.js.js);
  for (const LPieceRow& r : prog.pieces)
    if (r.js1 > jsUnits) __builtin_trap();
  u32 covered = 0;
  for (const CodeToken& t : syntaxTokens(doc.ast, doc.src, doc.strs)) {
    if (t.start < covered || t.end <= t.start || t.end > size || t.tag >= kTokenTagCount) __builtin_trap();
    covered = t.end;
  }
  (void)outlineJson(doc.ast, doc.src, doc.strs, doc.diags);
  (void)astJson(doc.ast, doc.src, doc.strs);
  return 0;
}
