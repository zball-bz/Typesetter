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

// One provider per resource kind (design T9 A2); a provider that returns
// false answers its row as failed (the engine degrades that quantity).
struct ProviderSet {
  std::function<bool(std::string_view lang, std::string_view text, std::vector<CodeToken>& out)> tokens;
  // a box's size (BoxKind): an image's intrinsic px (availPx 0), an svg or
  // html box's height and baseline at its width (plan P3-28)
  std::function<bool(BoxKind kind, std::string_view ref, double availPx, double& w, double& h, double& baseline)>
      boxes;
  std::function<double(const WireMetricKey& mk, std::string_view text)> width;
  std::function<void(const WireMetricKey& mk, double& asc, double& desc)> vmet;
};

// The golden/native providers: the normative mock measurer (it depends only
// on the key's size), the policy's image answer, the mock box (its payload's
// text in lines of its width), plain code (callers add a token provider,
// e.g. native tree-sitter: nativeTokens).
inline ProviderSet mockProviders() {
  ProviderSet p;
  p.tokens = [](std::string_view, std::string_view, std::vector<CodeToken>& out) {
    out.clear();
    return true;
  };
  p.boxes = [](BoxKind kind, std::string_view ref, double availPx, double& w, double& h, double& baseline) {
    if (kind == BoxKind::Image) {
      w = kPolicyNativeImagePx[0];
      h = baseline = kPolicyNativeImagePx[1];
      return true;
    }
    w = availPx;
    mockBoxPx(ref, availPx, h, baseline);
    return true;
  };
  p.width = [](const WireMetricKey& mk, std::string_view text) { return mockWordWidthPx(text, mk.sizePx); };
  p.vmet = [](const WireMetricKey& mk, double& asc, double& desc) {
    asc = 0.8 * mk.sizePx;
    desc = 0.2 * mk.sizePx;
  };
  return p;
}

// One round of the pull through the wire (plan P1-19): the document's
// request batch, decoded and answered by the providers, encoded and
// provided. false: nothing was asked.
inline bool answerRound(Doc& doc, const ProviderSet& p) {
  std::string req, ans, err;
  doc.requests(req);
  WireBatch q, a;
  if (!decodeWire((const u8*)req.data(), req.size(), false, q, err) || q.kinds.empty()) return false;
  a.batch = q.batch;
  for (const WireKind& k : q.kinds) {
    WireKind& out = a.kinds.emplace_back();
    out.kind = k.kind;
    for (const WireRow& r : k.rows) {
      WireRow& o = out.rows.emplace_back();
      o.resId = r.resId;
      o.flags = 1;
      bool ok = false;
      switch ((ResKind)k.kind) {
        case ResKind::textWidth:
          if ((ok = (bool)p.width)) o.setF64(0, p.width(q.mks[r.col[0]], q.strings[r.col[1]]));
          break;
        case ResKind::fontVmet:
          if ((ok = (bool)p.vmet)) {
            double asc = 0, desc = 0;
            p.vmet(q.mks[r.col[0]], asc, desc);
            o.setF64(0, asc);
            o.setF64(1, desc);
          }
          break;
        case ResKind::codeTokens: {
          std::vector<CodeToken> toks;
          if ((ok = p.tokens && p.tokens(q.strings[r.col[0]], q.strings[r.col[1]], toks)))
            for (const CodeToken& t : toks) o.list.insert(o.list.end(), {t.start, t.end, (u32)t.tag});
          break;
        }
        case ResKind::boxInfo: {
          double w = 0, h = 0, baseline = 0;
          if ((ok = p.boxes && r.col[0] <= (u64)BoxKind::Html &&
                    p.boxes((BoxKind)r.col[0], q.strings[r.col[1]], r.f64(2), w, h, baseline))) {
            o.setF64(0, w);
            o.setF64(1, h);
            o.setF64(2, baseline);
          }
          break;
        }
        case ResKind::fontFace:  // the native mock: every declared face loads
          o.col[0] = 0;
          ok = true;
          break;
      }
      o.status = ok ? 0 : 1;
    }
  }
  encodeWire(a, true, ans);
  return doc.provide((const u8*)ans.data(), ans.size());
}

// Advances an ingested document to Layout; false when it stalls: nothing
// left to ask while not done, or not done within maxRounds.
inline bool driveToCompletion(Doc& doc, const ProviderSet& p, u32 maxRounds = kPolicyMaxRounds) {
  for (u32 round = 0; round < maxRounds; round++) {
    if (doc.typeset() == Doc::Status::Ok) return true;
    if (!answerRound(doc, p)) return false;
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
