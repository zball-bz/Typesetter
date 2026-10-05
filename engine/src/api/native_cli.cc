// tsrc — stage inspection CLI (architecture §2.1), on the shared drive loop
// (api/driver.h) and one settings document (plan P1-03):
//   tsrc --stage=<product> [--ops=f.ops] [--profile=golden|path.json]
//        [--fixture=f.fixture.json] [--settings=f.json] [--set path=value]…
//        <file.tsm>
// Products are products.def (skeleton ast js ops tree semantic mathbox blocks
// breaks layout paged html diags settings); those after Ingest need --ops.
// Settings layer in order: profile, fixture, --settings, --set. A profile
// name resolves to test/profiles/<name>.json under the current directory.
// Legacy flags (--width --base --indent --punct --snap --page-height) are
// sugar for their settings rows. Post-ops stages use the normative mock
// measurer, the policy's image answer and the native token provider — with
// --profile=golden and a fixture's X.fixture.json, tsrc reproduces the
// golden files byte for byte (tools/check-tsrc.mjs).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../code/native_tokens.h"
#include "driver.h"

using namespace tsr;

static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// --set path=value: value is JSON when it parses as JSON, else a string
static std::string setDocument(const std::string& arg) {
  size_t eq = arg.find('=');
  std::string path = arg.substr(0, eq), value = eq == std::string::npos ? "" : arg.substr(eq + 1);
  JsonValue probe;
  JsonReader rd;
  std::string v;
  if (rd.parse(value, probe)) v = value;
  else jsonString(v, value);
  size_t dot = path.find('.');
  std::string sec = path.substr(0, dot), key = dot == std::string::npos ? "" : path.substr(dot + 1);
  std::string out = "{";
  jsonString(out, sec);
  out += ":{";
  jsonString(out, key);
  out += ":" + v + "}}";
  return out;
}

static std::string legacy(const char* sec, const char* key, const std::string& jsonValue) {
  return std::string("{\"") + sec + "\":{\"" + key + "\":" + jsonValue + "}}";
}

int main(int argc, char** argv) {
  std::string stage = "ast", opsPath, file;
  std::vector<std::string> layers;  // settings documents, applied in order
  std::string profile, fixture;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto val = [&](size_t n) { return a.substr(n); };
    if (a.rfind("--stage=", 0) == 0) stage = val(8);
    else if (a.rfind("--ops=", 0) == 0) opsPath = val(6);
    else if (a.rfind("--profile=", 0) == 0) profile = val(10);
    else if (a.rfind("--fixture=", 0) == 0) fixture = val(10);
    else if (a.rfind("--settings=", 0) == 0) {
      std::string t;
      if (!readFile(val(11), t)) {
        fprintf(stderr, "cannot read %s\n", val(11).c_str());
        return 2;
      }
      layers.push_back(t);
    } else if (a.rfind("--set", 0) == 0 && a.size() > 6 && (a[5] == '=' || a[5] == ' ')) {
      layers.push_back(setDocument(val(6)));
    } else if (a == "--set" && i + 1 < argc) {
      layers.push_back(setDocument(argv[++i]));
    }
    // legacy sugar (deprecated)
    else if (a.rfind("--width=", 0) == 0) layers.push_back(legacy("host", "width", val(8)));
    else if (a.rfind("--base=", 0) == 0) layers.push_back(legacy("doc", "baseSize", val(7)));
    else if (a.rfind("--indent=", 0) == 0) layers.push_back(legacy("par", "indent", val(9)));
    else if (a.rfind("--punct=", 0) == 0) layers.push_back(legacy("cjk", "punctCompress", "\"" + val(8) + "\""));
    else if (a.rfind("--page-height=", 0) == 0) layers.push_back(legacy("page", "height", val(14)));
    else if (a == "--snap") layers.push_back(legacy("code", "snapKerning", "true"));
    else file = a;
  }
  if (file.empty()) {
    fprintf(stderr, "usage: tsrc --stage=<product> [--ops=f.ops] [--profile=P] [--fixture=F] "
                    "[--settings=F] [--set path=value] file.tsm\n");
    return 2;
  }
  Stage need;
  if (!Doc::productStage(stage, need)) {
    fprintf(stderr, "unknown product %s\n", stage.c_str());
    return 2;
  }
  std::string source;
  if (!readFile(file, source)) {
    fprintf(stderr, "cannot read %s\n", file.c_str());
    return 2;
  }

  // settings: profile < fixture < --settings < --set
  std::vector<std::string> docs;
  FixtureConfig fx;
  if (!fixture.empty()) {
    std::string t;
    if (!readFile(fixture, t)) {
      fprintf(stderr, "cannot read %s\n", fixture.c_str());
      return 2;
    }
    fx = parseFixtureConfig(t);
    if (!fx.error.empty()) {
      fprintf(stderr, "%s: %s\n", fixture.c_str(), fx.error.c_str());
      return 2;
    }
    if (profile.empty()) profile = fx.profile;
  }
  if (!profile.empty()) {
    std::string path = profile.find('/') == std::string::npos && profile.find(".json") == std::string::npos
                           ? "test/profiles/" + profile + ".json"
                           : profile;
    std::string t;
    if (!readFile(path, t)) {
      fprintf(stderr, "cannot read profile %s\n", path.c_str());
      return 2;
    }
    docs.push_back(t);
  }
  if (!fixture.empty()) docs.push_back(fx.settings);
  docs.insert(docs.end(), layers.begin(), layers.end());

  Doc doc;
  for (const std::string& d : docs) doc.configure(d);
  doc.compile(std::move(source));

  if (need > Stage::Compile && need <= Stage::Execute) need = Stage::Ingest;
  if (need >= Stage::Ingest || (stage == "diags" && !opsPath.empty())) {
    if (opsPath.empty()) {
      fprintf(stderr, "product %s needs --ops=\n", stage.c_str());
      return 2;
    }
    std::string ops;
    if (!readFile(opsPath, ops)) {
      fprintf(stderr, "cannot read %s\n", opsPath.c_str());
      return 2;
    }
    if (!doc.ingest((const u8*)ops.data(), ops.size())) {
      fprintf(stderr, "ops decode failed:\n%s", doc.dumpDiags().c_str());
      return 1;
    }
    // semantic is the pre-answer render; everything later (and diags with
    // ops) drives the pull loop to completion
    if (need >= Stage::Emit || stage == "diags") {
      ProviderSet p = mockProviders();
      p.tokens = [](Doc& d) { provideNativeTokens(d); };
      if (!driveToCompletion(doc, p)) {
        fprintf(stderr, "typeset did not converge\n");
        return 1;
      }
    }
  }
  std::string out = doc.product(stage);
  fwrite(out.data(), 1, out.size(), stdout);
  return 0;
}
