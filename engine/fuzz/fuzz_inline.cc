// libFuzzer target: the whole front end — line pass, inline parse, codegen
// (plan P0-03; testing.md §6) — and its exports (plan P1-09: tokens, outline,
// AST JSON), whose tokens must stay sorted, non-overlapping and in the source.
#include <cstddef>
#include <cstdint>
#include <string>

#include "../src/api/doc.h"

using namespace tsr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Doc doc;
  doc.compile(std::string(reinterpret_cast<const char*>(data), size));
  (void)dumpAst(doc.ast, doc.src, doc.strs);
  u32 covered = 0;
  for (const CodeToken& t : syntaxTokens(doc.ast, doc.src, doc.strs)) {
    if (t.start < covered || t.end <= t.start || t.end > size || t.tag >= kTokenTagCount) __builtin_trap();
    covered = t.end;
  }
  (void)outlineJson(doc.ast, doc.src, doc.strs, doc.diags);
  (void)astJson(doc.ast, doc.src, doc.strs);
  return 0;
}
