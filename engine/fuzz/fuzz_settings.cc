// libFuzzer target: the host settings codec (plan P1-03, D-H08) — the JSON
// reader, row dispatch, every value domain and the effective-settings dump.
// Settings arrive from hosts and, later, from documents: well-formedness is
// not assumed.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../src/api/settings.gen.h"

using namespace tsr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Config c;
  DiagSink d;
  applySettings(c, std::string_view((const char*)data, size), d);
  // whatever was applied must dump to a document that applies back cleanly
  std::string j = settingsJson(c);
  Config back;
  DiagSink d2;
  SettingsPatch p = applySettings(back, j, d2);
  if (!p.ok || !d2.items.empty() || settingsJson(back) != j) __builtin_trap();
  return 0;
}
