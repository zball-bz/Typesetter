// Test-only exports for the WASM parity check (plan P0-12): the WASM build
// (libc++) must break every fixture exactly as the native goldens
// (libstdc++) do. Linked into typesetter_debug.js only — never shipped.
#include "../measure/mock.h"
#include "wasm_api.cc"

namespace {
std::string debugOut;
}

// answers the pending word/vmet requests with the normative mock measurer
TSR_EXPORT void tsr_debug_mock_measure(WasmDoc* d) {
  Doc& doc = d->doc;
  mockProvide(doc.pendingRequests(), doc.metrics, doc.strs, doc.styles, doc.cfg);
}

// stage dumps in the golden formats: "breaks", "layout", "html"
TSR_EXPORT const char* tsr_debug_dump(WasmDoc* d, const char* stage) {
  std::string_view s(stage);
  Doc& doc = d->doc;
  if (s == "breaks") debugOut = dumpBreaks(doc.tops);
  else if (s == "layout") debugOut = dumpLayout(doc.layout);
  else if (s == "html") debugOut = doc.render();
  else debugOut.clear();
  return debugOut.c_str();
}
