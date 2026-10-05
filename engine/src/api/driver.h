// The one native drive loop (plan P1-03; design T9 A3): tsrc, the golden
// runner and the fuzzers advance a document through the pull loop with the
// same code. Hosts answer through a ProviderSet; the JS hosts' twin is the
// worker's measureLoop.
#pragma once
#include <functional>

#include "../measure/mock.h"
#include "../support/json.h"
#include "doc.h"

namespace tsr {

struct ProviderSet {
  std::function<void(Doc&)> tokens;  // answer every pending token request
  std::function<void(Doc&)> images;  // answer every pending image request
  std::function<void(Doc&, const MeasureRequest&)> metrics;  // words + vertical metrics
};

// The golden/native providers: the normative mock measurer, the policy's
// image answer, plain code (callers add a token provider, e.g. native
// tree-sitter: provideNativeTokens).
inline ProviderSet mockProviders() {
  ProviderSet p;
  p.tokens = [](Doc& doc) {
    for (auto& r : doc.tokenReqs)
      if (!r.provided) doc.provideTokens(r.id, nullptr, 0);
  };
  p.images = [](Doc& doc) {
    for (auto& r : doc.imageReqs)
      if (!r.provided) doc.provideImage(r.id, kPolicyNativeImagePx[0], kPolicyNativeImagePx[1]);
  };
  p.metrics = [](Doc& doc, const MeasureRequest& req) {
    mockProvide(req, doc.metrics, doc.strs, doc.styles, doc.cfg);
  };
  return p;
}

// Advances an ingested document to Layout; false when it does not converge
// within maxRounds (a stalled provider, or a provider that answers nothing).
inline bool driveToCompletion(Doc& doc, ProviderSet& p, u32 maxRounds = kPolicyMaxRounds) {
  for (u32 round = 0; round < maxRounds; round++) {
    if (p.tokens && doc.tokensPending()) p.tokens(doc);
    if (p.images && doc.imagesPending()) p.images(doc);
    if (doc.typeset() == Doc::Status::Ok) return true;
    if (doc.tokensPending() || doc.imagesPending()) {
      if (!p.tokens || !p.images) return false;
      continue;
    }
    MeasureRequest req = doc.pendingRequests();
    if (req.empty() || !p.metrics) return false;
    p.metrics(doc, req);
  }
  return false;
}

// A fixture's configuration (X.fixture.json, plan P1-03): {"profile":
// "golden", "settings": {…}, "products": ["paged", …]} — replaces the old
// file-name conventions. `settings` comes back as a settings document.
struct FixtureConfig {
  std::string profile = "golden";
  std::string settings = "{}";
  std::vector<std::string> products;  // extra products to golden
  std::string error;                  // non-empty: the file is malformed
};
inline FixtureConfig parseFixtureConfig(std::string_view text) {
  FixtureConfig fc;
  JsonValue v;
  JsonReader rd;
  if (!rd.parse(text, v) || v.t != JsonValue::T::Obj) {
    fc.error = rd.error() ? rd.error() : "expected an object";
    return fc;
  }
  if (const JsonValue* p = v.get("profile"); p && p->t == JsonValue::T::Str) fc.profile = p->str;
  if (const JsonValue* st = v.get("settings")) {
    fc.settings.clear();
    jsonDump(fc.settings, *st);
  }
  if (const JsonValue* pr = v.get("products"); pr && pr->t == JsonValue::T::Arr)
    for (const JsonValue& x : pr->arr)
      if (x.t == JsonValue::T::Str) fc.products.push_back(x.str);
  return fc;
}

}  // namespace tsr
