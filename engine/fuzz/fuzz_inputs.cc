// libFuzzer target: declared inputs (plan P3-31; design T9 A7) — the labels
// manifest decoder and what Resolve does with what it accepts. An input is
// untrusted bytes: whatever it holds, the document must resolve, typeset and
// render, and its own labels product must decode.
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>

#include "../src/api/driver.h"
#include "../src/semantic/manifest.h"

using namespace tsr;

namespace {
std::string gOps;
std::string readAll(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  // run from the repository root (tools/fuzz.sh): a document of references
  gOps = readAll("test/fixtures/project/ch2.ops");
  if (gOps.empty()) __builtin_trap();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view in((const char*)data, size);
  ExternalLabels ext;
  std::string err;
  (void)decodeLabelManifests(in, "ch2", ext, err);
  Doc doc;
  doc.configure(R"({"project":{"doc":"ch2","starts":{"ch1":{"heading":1}},"urls":{"ch1":"ch1.html"}}})");
  doc.setInput("labels", in);
  if (!doc.ingest((const u8*)gOps.data(), gOps.size())) __builtin_trap();
  if (!driveToCompletion(doc, mockProviders())) __builtin_trap();
  (void)doc.render();
  // its own product is a manifest its readers accept
  const std::string own = "[" + doc.product("labels") + "]";
  ExternalLabels back;
  if (!decodeLabelManifests(own, "", back, err)) __builtin_trap();
  return 0;
}
