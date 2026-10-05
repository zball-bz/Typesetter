// libFuzzer target: the whole front end — line pass, inline parse, codegen
// (plan P0-03; testing.md §6).
#include <cstddef>
#include <cstdint>
#include <string>

#include "../src/api/doc.h"

using namespace tsr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Doc doc;
  doc.compile(std::string(reinterpret_cast<const char*>(data), size));
  (void)dumpAst(doc.ast, doc.src, doc.strs);
  return 0;
}
