// libFuzzer target: the line pass (plan P0-03; testing.md §6).
// Arbitrary bytes must terminate without crashes or sanitizer reports.
#include <cstddef>
#include <cstdint>
#include <string>

#include "../src/linepass/linepass.h"

using namespace tsr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  SourceText src;
  src.init(std::string(reinterpret_cast<const char*>(data), size));
  Arena arena;
  DiagSink diags;
  Skeleton sk = linepass(src, arena, diags);
  (void)sk;
  return 0;
}
