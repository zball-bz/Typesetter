// libFuzzer target: resource answers (plan P1-19, D-H08) — the TSRA decoder
// and every row validator of Doc::provide. A host's answer is untrusted
// input: whatever it holds, the document must still converge with honest
// providers afterwards and render. The input is an answer to the open batch
// of a document with token, image, width and vertical-metric needs (its
// batch id is patched in, so most inputs reach row validation).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

#include "../src/api/driver.h"

using namespace tsr;

namespace {
std::string gOps[2];
std::string readAll(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  // run from the repository root (tools/fuzz.sh)
  gOps[0] = readAll("test/fixtures/code/json-hl.ops");
  gOps[1] = readAll("test/fixtures/figure/w-only.ops");
  if (gOps[0].empty() || gOps[1].empty()) __builtin_trap();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Doc doc;
  const std::string& ops = gOps[size ? data[size - 1] & 1 : 0];
  if (!doc.ingest((const u8*)ops.data(), ops.size())) __builtin_trap();
  ProviderSet honest = mockProviders();
  // two rounds: the tokens/images batch, then (after emit) the measure batch
  for (int round = 0; round < 2 && doc.typeset() != Doc::Status::Ok; round++) {
    std::string req;
    doc.requests(req);
    std::string ans((const char*)data, size);
    if (ans.size() >= 12) std::memcpy(&ans[8], &doc.rt.batch.id, 4);
    doc.provide((const u8*)ans.data(), ans.size());
    if (round == 0 && doc.rt.barrierPending()) answerRound(doc, honest);
  }
  if (!driveToCompletion(doc, honest)) __builtin_trap();
  (void)doc.render();
  return 0;
}
