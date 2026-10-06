// The one native drive loop (plan P1-03; design T9 A3): tsrc, the golden
// runner and the fuzzers advance a document through the pull loop with the
// same code. Hosts answer through a ProviderSet; the JS hosts' twin is the
// worker's measureLoop.
#pragma once
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>

#include "../measure/mock.h"
#include "../support/json.h"
#include "doc.h"

namespace tsr {

// (plan P4-06; D-X09) a language's hyphenation patterns as a host answers them
struct HyphAnswer {
  std::string patterns, exceptions, hyphenChar = "-";
  u8 leftmin = 2, rightmin = 2;
};
// a language's dictionary in a directory of them (tools/hyphc.mjs: index.json
// maps BCP-47 tags to files, a file is {tag, leftmin, rightmin, hyphenChar,
// patterns, exceptions}): the tag lower-cased, then each shorter prefix of
// it (de-DE → de) — as the runtime's provider (runtime/src/shared/
// resources/providers/hyph.mjs) resolves it; false: none
inline bool hyphFromDir(const std::string& dir, std::string_view lang, HyphAnswer& out) {
  auto readJson = [](const std::string& path, JsonValue& v) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    JsonReader rd;
    return rd.parse(ss.str(), v) && v.t == JsonValue::T::Obj;
  };
  JsonValue index, v;
  if (!readJson(dir + "/index.json", index)) return false;
  std::string tag(lang);
  for (char& c : tag) c = c == '_' ? '-' : (char)std::tolower((unsigned char)c);
  const JsonValue* file = nullptr;
  while (!tag.empty() && !(file = index.get(tag))) {
    const size_t dash = tag.rfind('-');
    tag.resize(dash == std::string::npos ? 0 : dash);
  }
  if (!file || file->t != JsonValue::T::Str || !readJson(dir + "/" + file->str + ".json", v)) return false;
  auto str = [&](const char* k, std::string& dst) {
    if (const JsonValue* x = v.get(k); x && x->t == JsonValue::T::Str) dst = x->str;
  };
  auto num = [&](const char* k, u8& dst) {
    if (const JsonValue* x = v.get(k); x && x->t == JsonValue::T::Num) dst = (u8)x->num;
  };
  str("patterns", out.patterns);
  str("exceptions", out.exceptions);
  str("hyphenChar", out.hyphenChar);
  num("leftmin", out.leftmin);
  num("rightmin", out.rightmin);
  return !out.patterns.empty();
}

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
  std::function<void(const WireMetricKey& mk, double& asc, double& desc)> ink;  // (plan P5-01) fontInk
  std::function<bool(std::string_view lang, HyphAnswer& out)> hyph;  // (plan P4-06)
};

// The golden/native providers: the normative mock measurer (it depends only
// on the key's size), the policy's image answer, the mock box (its payload's
// text in lines of its width), plain code (callers add a token provider,
// e.g. native tree-sitter: nativeTokens), the dictionaries in hyphDir
// (plan P4-06: the goldens' are test/hyph — tsrc's under its current
// directory, as its profiles are)
inline ProviderSet mockProviders(std::string hyphDir = "test/hyph") {
  ProviderSet p;
  p.hyph = [hyphDir](std::string_view lang, HyphAnswer& out) { return hyphFromDir(hyphDir, lang, out); };
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
  p.ink = [](const WireMetricKey& mk, double& asc, double& desc) {  // mock.h's
    asc = 0.7 * mk.sizePx;
    desc = 0.15 * mk.sizePx;
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
        case ResKind::fontInk: {
          const auto& f = (ResKind)k.kind == ResKind::fontVmet ? p.vmet : p.ink;
          if ((ok = (bool)f)) {
            double asc = 0, desc = 0;
            f(q.mks[r.col[0]], asc, desc);
            o.setF64(0, asc);
            o.setF64(1, desc);
          }
          break;
        }
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
        case ResKind::hyphPatterns: {
          HyphAnswer h;
          if ((ok = p.hyph && p.hyph(q.strings[r.col[0]], h))) {
            o.col[0] = a.str(h.patterns);
            o.col[1] = a.str(h.exceptions);
            o.col[2] = h.leftmin;
            o.col[3] = h.rightmin;
            o.col[4] = a.str(h.hyphenChar);
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
  // (plan P3-31) declared inputs: "inputs": {"labels": [files]} — the other
  // documents' labels products, relative to the fixture
  std::vector<std::string> labels;
  // (plan P5-01) "inputs": {"mathFonts": [files]}: .tsmf blobs, back to back
  std::vector<std::string> mathFonts;
  std::string error;                  // non-empty: the file is malformed
};
// (plan P5-01) the input `mathFonts` from .tsmf files: their bytes back to
// back; false: a file cannot be read
inline bool mathFontsInput(const std::vector<std::string>& files, std::string& out, std::string& missing) {
  out.clear();
  for (const std::string& file : files) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
      missing = file;
      return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    out += ss.str();
  }
  return true;
}
// (plan P3-31) the input `labels` from manifest files (each a labels
// product): a JSON array of them; false: a file cannot be read
inline bool labelsInput(const std::vector<std::string>& files, std::string& out, std::string& missing) {
  out = "[";
  for (size_t i = 0; i < files.size(); i++) {
    std::ifstream f(files[i], std::ios::binary);
    if (!f) {
      missing = files[i];
      return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (i) out += ',';
    out += ss.str();
  }
  out += ']';
  return true;
}
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
  if (const JsonValue* in = v.get("inputs"); in && in->t == JsonValue::T::Obj) {
    if (const JsonValue* l = in->get("labels"); l && l->t == JsonValue::T::Arr)
      for (const JsonValue& x : l->arr)
        if (x.t == JsonValue::T::Str) fc.labels.push_back(x.str);
    if (const JsonValue* m = in->get("mathFonts"); m && m->t == JsonValue::T::Arr)
      for (const JsonValue& x : m->arr)
        if (x.t == JsonValue::T::Str) fc.mathFonts.push_back(x.str);
  }
  return fc;
}

}  // namespace tsr
