// libFuzzer target: the LowerProgram reader (plan P2-02, D-H08). A program is
// produced by the engine but crosses to the host and back through caches,
// so its decoder must take any bytes: whatever the input, the reader either
// rejects it with a reason or accepts a program whose every reference is in
// range — and the dump of an accepted program is total. Seeds are the
// fixtures' programs (tools/fuzz.sh).
#include <cstddef>
#include <cstdint>
#include <string>

#include "../src/codegen/lower.h"

using namespace tsr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string_view bytes(reinterpret_cast<const char*>(data), size);
  LowerProgram p;
  std::string why;
  const bool ok = readLowerProgram(bytes, p, why);
  if (!ok && why.empty()) __builtin_trap();
  std::string dump = dumpLowerProgram(bytes);
  if (ok != (dump.rfind("invalid: ", 0) != 0)) __builtin_trap();
  return 0;
}
