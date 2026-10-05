// libFuzzer target: the ops reader and everything up to the semantic render
// (plan P0-03; testing.md §6). The buffer comes from JS: "trusted" does not
// extend to "well-formed".
#include <cstddef>
#include <cstdint>

#include "../src/api/doc.h"

using namespace tsr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Doc doc;
  doc.cfg.widthPx = 300;
  doc.cfg.baseSizePx = 16;
  if (doc.ingest(data, size)) (void)doc.renderFallback();
  return 0;
}
