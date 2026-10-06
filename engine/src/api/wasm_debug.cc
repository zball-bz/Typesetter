// Test-only exports for the WASM parity check (plan P0-12): the WASM build
// (libc++) must break every fixture exactly as the native goldens
// (libstdc++) do. Linked into typesetter_debug.js only — never shipped.
#include "../measure/mock.h"
#include "wasm_api.cc"
#include "../ops/domains.gen.h"

namespace {
std::string debugOut;
}

// answers the pending word/vmet requests with the normative mock measurer
TSR_EXPORT void tsr_debug_mock_measure(WasmDoc* d) {
  Doc& doc = d->doc;
  mockProvide(doc.pendingRequests(), doc.metrics, doc.strs, doc.faces);
  // (plan P3-28) svg and html boxes: the native mock's answer (the JSON
  // request lists only images)
  for (u32 i = 0; i < doc.rt.boxNeeds.size(); i++) {
    const BoxNeed& b = doc.rt.boxNeeds[i];
    if (b.st != ResState::Pending || b.kind == BoxKind::Image) continue;
    double h = 0, base = 0;
    mockBoxPx(doc.strs.get(b.src), b.availPx, h, base);
    doc.settleBox(i, b.availPx, h, base, false);
  }
}

// stage dumps in the golden formats: "breaks", "layout", "html"
TSR_EXPORT const char* tsr_debug_dump(WasmDoc* d, const char* stage) {
  std::string_view s(stage);
  Doc& doc = d->doc;
  if (s == "breaks") debugOut = dumpBreaks(doc.layout);
  else if (s == "layout") debugOut = dumpLayout(doc.layout);
  else if (s == "html") debugOut = doc.render();
  else debugOut.clear();
  return debugOut.c_str();
}

// the C++ value-domain matcher, for the JS parity check (tools/check-domains.mjs)
TSR_EXPORT int tsr_debug_match_domain(int domain, const char* s) {
  return matchDomain((TextDomain)domain, s) ? 1 : 0;
}
