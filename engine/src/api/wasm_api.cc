// WASM boundary (architecture §2.5). C ABI; strings returned as doc-owned
// buffers valid until the next call on the same doc. Measurement is the
// pull loop: tsr_typeset → 1 (NEED_MEASURE) → tsr_measure_requests (JSON)
// → tsr_provide_* per item → tsr_typeset again.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TSR_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define TSR_EXPORT extern "C"
#endif

#include "../measure/measure.h"
#include "doc.h"
#include "../support/json.h"

using namespace tsr;

namespace {
struct WasmDoc {
  Doc doc;
  std::string jsOut, htmlOut, semOut, reqOut, diagOut;
};

void jsonEscapeInto(std::string& out, std::string_view s) {
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) appendf(out, "\\u%04x", c);
        else out += (char)c;
    }
  }
}
}  // namespace

TSR_EXPORT WasmDoc* tsr_doc_new() { return new WasmDoc(); }
TSR_EXPORT void tsr_doc_free(WasmDoc* d) { delete d; }

// One settings document per call (plan P1-03; docs/host-protocol-design.md,
// docs/settings-table.md). 0 = applied; diagnostics name unknown paths and
// bad values.
TSR_EXPORT int tsr2_set_config(WasmDoc* d, const char* json) {
  return d->doc.configure(json ? std::string_view(json) : std::string_view{});
}

// A new document from this one's retained ops with a settings patch applied
// (plan P1-03): what relayout and paginate use, and the answer to a
// tsr2_set_config that returned REBUILD (4). Metric, token and image answers
// carry over, so the new document converges without asking the host again
// (except for metrics when the patch changes measurement). Null when the
// patch needs re-execution (5) or the source was never ingested.
TSR_EXPORT WasmDoc* tsr2_doc_fork(WasmDoc* d, const char* patchJson) {
  WasmDoc* f = new WasmDoc();
  if (!d->doc.forkInto(f->doc, patchJson ? std::string_view(patchJson) : std::string_view("{}"))) {
    delete f;
    return nullptr;
  }
  return f;
}

// --- deprecated per-knob setters (MD-06): wrappers over tsr2_set_config ----
namespace {
int configure1(WasmDoc* d, const char* section, const char* key, const std::string& jsonValue) {
  std::string j = "{";
  jsonString(j, section);
  j += ":{";
  jsonString(j, key);
  j += ":" + jsonValue + "}}";
  return d->doc.configure(j);
}
std::string jstr(const char* s) {
  std::string out;
  jsonString(out, s ? s : "");
  return out;
}
std::string jnum(double v) {
  char b[40];
  std::snprintf(b, sizeof b, "%.17g", v);
  return b;
}
}  // namespace

TSR_EXPORT void tsr_config(WasmDoc* d, double widthPx, double baseSizePx,
                           double lineHeight, double paraIndentEm) {
  configure1(d, "host", "width", jnum(widthPx));
  if (baseSizePx > 0) configure1(d, "doc", "baseSize", jnum(baseSizePx));
  if (lineHeight > 0) configure1(d, "doc", "leading", jnum(lineHeight));
  if (paraIndentEm >= 0) configure1(d, "par", "indent", jnum(paraIndentEm));
}

TSR_EXPORT void tsr_set_punct_compress(WasmDoc* d, int mode) {
  static const char* const kModes[] = {"full", "book", "none"};
  if (mode >= 0 && mode <= 2) configure1(d, "cjk", "punctCompress", jstr(kModes[mode]));
}

TSR_EXPORT void tsr_set_font(WasmDoc* d, const char* family) {
  configure1(d, "fonts", "body", jstr(family));
}

TSR_EXPORT void tsr_set_cjk_font(WasmDoc* d, const char* family) {
  configure1(d, "fonts", "cjk", jstr(family));
}

// BCP-47 tag → supplement words (Figure/图 …); default is zh
TSR_EXPORT void tsr_set_lang(WasmDoc* d, const char* lang) {
  if (lang && *lang) configure1(d, "doc", "lang", jstr(lang));
}

TSR_EXPORT void tsr_set_snap_kerning(WasmDoc* d, int on) {
  configure1(d, "code", "snapKerning", on ? "true" : "false");
}

// lang "" sets the default; else a per-language override (verbatim §3)
TSR_EXPORT void tsr_set_code_features(WasmDoc* d, const char* lang,
                                      const char* features) {
  if (!lang || !*lang) {
    configure1(d, "code", "fontFeatures", jstr(features));
    return;
  }
  std::string m = "{";
  bool first = true;
  for (const auto& [k, v] : d->doc.cfg.codeFontFeaturesByLang) {
    if (k == lang) continue;
    if (!first) m += ",";
    first = false;
    jsonString(m, k);
    m += ":";
    jsonString(m, v);
  }
  if (!first) m += ",";
  jsonString(m, lang);
  m += ":" + jstr(features) + "}";
  configure1(d, "code", "fontFeaturesByLang", m);
}

TSR_EXPORT int tsr_compile(WasmDoc* d, const char* src) {
  d->doc.compile(std::string(src));
  return 0;
}

TSR_EXPORT const char* tsr_get_js(WasmDoc* d) {
  d->jsOut = d->doc.js.text;
  return d->jsOut.c_str();
}

TSR_EXPORT int tsr_ingest(WasmDoc* d, const u8* buf, int len) {
  return d->doc.ingest(buf, (size_t)len) ? 0 : 1;
}

TSR_EXPORT int tsr_typeset(WasmDoc* d) {
  return d->doc.typeset() == Doc::Status::Ok ? 0 : 1;
}

// JSON: {"styles":[{"id":0,"family":"...","sizePx":18,"weight":400,
//   "italic":false,"needVmet":true,"words":["The","fox"]}]}
// One entry per measurement face (plan P1-04): "id" is a FaceId, opaque to
// the host, which echoes it in tsr_provide_word / tsr_provide_vmet.
TSR_EXPORT const char* tsr_measure_requests(WasmDoc* d) {
  MeasureRequest req = d->doc.pendingRequests();
  std::vector<FaceId> order;  // first-request order (deterministic)
  std::unordered_map<u32, std::vector<StrRef>> byFace;
  std::unordered_map<u32, bool> needVmet;
  for (FaceId f : req.vmetFaces) {
    if (!byFace.count(f)) order.push_back(f);
    byFace[f];
    needVmet[f] = true;
  }
  for (const MeasureItem& it : req.words) {
    if (!byFace.count(it.face)) order.push_back(it.face);
    byFace[it.face].push_back(it.str);
  }

  std::string& out = d->reqOut;
  out.clear();
  out += "{\"styles\":[";  // NOLINT
  bool first = true;
  for (FaceId f : order) {
    StyleDesc desc = describeFace(d->doc.faces, f);
    if (!first) out += ",";
    first = false;
    appendf(out, "{\"id\":%u,\"family\":\"", f);
    jsonEscapeInto(out, desc.family);
    appendf(out, "\",\"sizePx\":%g,\"weight\":%d,\"italic\":%s,\"needVmet\":%s,\"words\":[",
            desc.sizePx, desc.weight, desc.italic ? "true" : "false",
            needVmet.count(f) ? "true" : "false");
    bool fw = true;
    for (StrRef w : byFace[f]) {
      if (!fw) out += ",";
      fw = false;
      out += "\"";
      jsonEscapeInto(out, d->doc.strs.get(w));
      out += "\"";
    }
    out += "]}";
  }
  out += "]";
  // NEED_TOKENS pull state (code-design.md §2): unanswered codeblocks
  out += ",\"tokens\":[";
  bool ft = true;
  for (const Doc::TokenReq& r : d->doc.tokenReqs) {
    if (r.provided) continue;
    if (!ft) out += ",";
    ft = false;
    appendf(out, "{\"id\":%u,\"lang\":\"", r.id);
    jsonEscapeInto(out, d->doc.strs.get(r.lang));
    out += "\",\"text\":\"";
    jsonEscapeInto(out, d->doc.strs.get(r.body));
    out += "\"}";
  }
  out += "]";
  // NEED_IMAGES pull state (figure-design.md §2): unanswered image nodes
  out += ",\"images\":[";
  bool fi = true;
  for (const Doc::ImageReq& r : d->doc.imageReqs) {
    if (r.provided) continue;
    if (!fi) out += ",";
    fi = false;
    appendf(out, "{\"id\":%u,\"src\":\"", r.id);
    jsonEscapeInto(out, d->doc.strs.get(r.src));
    out += "\"}";
  }
  out += "]}";
  return out.c_str();
}

// intrinsic CSS dims; 0×0 = load failure (placeholder + warning)
TSR_EXPORT void tsr_provide_image(WasmDoc* d, int id, double wPx, double hPx) {
  d->doc.provideImage((u32)id, wPx, hPx);
}

// triples: (start, end, tagId) × n as a flat u32 array; n may be 0 —
// every request must be answered (empty = plain code)
TSR_EXPORT void tsr_provide_tokens(WasmDoc* d, int id, const u32* triples, int n) {
  std::vector<CodeToken> toks;
  if (n > 0) toks.reserve((size_t)n);
  for (int i = 0; i < n; i++) {
    u32 tag = triples[i * 3 + 2];
    if (tag >= (u32)kTokenTagCount) continue;  // never truncate into a valid tag
    toks.push_back({triples[i * 3], triples[i * 3 + 1], (u8)tag});
  }
  d->doc.provideTokens((u32)id, toks.data(), toks.size());
}

// faceId: the "id" of a measurement request; one the document never issued
// is ignored (plan P0-11)
TSR_EXPORT void tsr_provide_word(WasmDoc* d, const char* word, int faceId, double px) {
  if (faceId < 0 || (size_t)faceId >= d->doc.faces.count()) return;
  d->doc.metrics.provideWord(d->doc.strs.intern(word), (FaceId)faceId, px, d->doc.cfg);
}

TSR_EXPORT void tsr_provide_vmet(WasmDoc* d, int faceId, double ascPx, double descPx) {
  if (faceId < 0 || (size_t)faceId >= d->doc.faces.count()) return;
  d->doc.metrics.provideVmet((FaceId)faceId, ascPx, descPx);
}

TSR_EXPORT const char* tsr_render(WasmDoc* d) {
  d->htmlOut = d->doc.render();
  return d->htmlOut.c_str();
}

// Semantic flow HTML (document-model §9.2) — valid right after ingest,
// before any measurement: the progressive-upgrade first paint.
TSR_EXPORT const char* tsr_render_semantic(WasmDoc* d) {
  d->semOut = d->doc.renderFallback();
  return d->semOut.c_str();
}

TSR_EXPORT void tsr_set_width(WasmDoc* d, double widthPx) {
  d->doc.setWidth(widthPx);
}

// Paged print rendering (pages-design.md §2): requires a converged typeset
// at the current width; sheets of pageHeightPx with keep-rules.
TSR_EXPORT const char* tsr_render_pages(WasmDoc* d, double pageHeightPx) {
  d->htmlOut = d->doc.renderPaged(pageHeightPx);
  return d->htmlOut.c_str();
}

TSR_EXPORT const char* tsr_diags(WasmDoc* d) {
  d->diagOut = d->doc.dumpDiags();
  return d->diagOut.c_str();
}

TSR_EXPORT double tsr_doc_height_px(WasmDoc* d) {
  return (double)d->doc.layout.docHeightSu / 64.0;
}

// The one ABI handshake (plan P1-01, D-H06): the host checks it before it
// writes a single op. Fields of subsystems that do not exist yet carry 0
// (programAbi → P2-02, syntaxVersion → P1-05, resVersion → P1-19).
TSR_EXPORT const char* tsr2_abi() {
  static std::string out;
  if (out.empty()) {
    out = "{\"opsWindow\":[" + std::to_string(OPS_MIN_COMPAT) + "," + std::to_string(OPS_VERSION) +
          "],\"schemaHash\":\"" + SCHEMA_HASH +
          "\",\"programAbi\":0,\"resVersion\":0,\"renderVersion\":1,\"syntaxVersion\":0}";
  }
  return out.c_str();
}
