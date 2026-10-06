// libFuzzer target: fragment programs (plan P2-13, D-H08). A request crosses
// from the host on every m`…`, m.parse and sidecar parse: whatever the bytes,
// the decoder rejects them or yields texts whose program reads back valid,
// with well-formed JSON out of band naming one descriptor per hole; the
// answer is total. The input is also tried as one text, at a base and
// clamped. Seeds are the fixtures' sources (tools/fuzz.sh).
#include <cstddef>
#include <cstdint>
#include <string>

#include "../src/codegen/codegen.h"
#include "../src/support/json.h"

using namespace tsr;

static void check(const Lowered& L) {
  LowerProgram p;
  std::string why;
  if (!readLowerProgram(L.program, p, why)) __builtin_trap();
  JsonValue v;
  JsonReader r;
  if (!r.parse(L.js, v)) __builtin_trap();
  const JsonValue* holes = v.get("holes");
  if (!holes || holes->arr.size() != p.holes || !v.get("diags")) __builtin_trap();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string_view bytes(reinterpret_cast<const char*>(data), size);
  FragmentRequest req;
  if (decodeFragmentRequest(bytes, req)) check(codegenFragments(req.texts, req.clamped ? &req.clamp : nullptr));
  (void)runFragmentRequest(bytes);
  const std::vector<FragmentText> one{{std::string(bytes), 7}};
  check(codegenFragments(one, nullptr));
  const Span clamp{3, 9};
  check(codegenFragments(one, &clamp));
  return 0;
}
