// WASM boundary (architecture §2.5). C ABI; strings returned as doc-owned
// buffers valid until the next call on the same doc. Measurement is the
// pull loop: tsr_typeset → 1 (NEED_MEASURE) → tsr2_requests (one binary
// batch) → tsr2_provide (the answers) → tsr_typeset again; products by
// name through tsr2_get. The JSON tsr_measure_requests / tsr_provide_* and
// the per-knob setters remain as shims (plan P3-37).
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

#include <cstring>
#define TSR_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define TSR_EXPORT extern "C"
#endif

#include "../codegen/codegen.h"
#include "../measure/measure.h"
#include "doc.h"
#include "../support/json.h"

using namespace tsr;

namespace {
struct WasmDoc {
  Doc doc;
  std::string jsOut, htmlOut, semOut, cssOut, reqOut, diagOut;
  std::string productOut;  // (plan P3-21) tsr2_product
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

// the host default document language (doc.lang): it picks the locale terms
// (supplement words, plan P1-10) and the lang attribute; default zh-CN
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

// The compiled document (plan P2-02): the hole module (UTF-8, "" when the
// document has no user code) and the LowerProgram, [u32 byte length][bytes].
// The host reads the program first: its header names the module's hash, so
// a cached module skips tsr_get_js.
TSR_EXPORT const char* tsr_get_js(WasmDoc* d) {
  return d->doc.js.js.c_str();
}
TSR_EXPORT const u8* tsr2_program(WasmDoc* d) {
  const std::string& p = d->doc.js.program;
  d->reqOut.assign(4, '\0');
  const u32 len = (u32)p.size();
  std::memcpy(&d->reqOut[0], &len, 4);
  d->reqOut += p;
  return (const u8*)d->reqOut.data();
}

TSR_EXPORT int tsr_ingest(WasmDoc* d, const u8* buf, int len) {
  return d->doc.ingest(buf, (size_t)len) ? 0 : 1;
}

TSR_EXPORT int tsr_typeset(WasmDoc* d) {
  return d->doc.typeset() == Doc::Status::Ok ? 0 : 1;
}

// The resource pull (plan P1-19; docs/host-protocol-design.md §5): one
// binary batch of every pending need of `kinds` (bit = resource kind id, 0 =
// all) — [u32 byte length][TSRQ …] — and its answer [TSRA …]; 0 = applied,
// 1 = rejected (not an answer to the open batch; the needs stay pending).
TSR_EXPORT const u8* tsr2_requests(WasmDoc* d, u32 kinds) {
  std::string body;
  d->doc.requests(body, kinds);
  d->reqOut.assign(4, '\0');
  const u32 len = (u32)body.size();
  std::memcpy(&d->reqOut[0], &len, 4);
  d->reqOut += body;
  return (const u8*)d->reqOut.data();
}
TSR_EXPORT int tsr2_provide(WasmDoc* d, const u8* buf, int len) {
  return len >= 0 && d->doc.provide(buf, (size_t)len) ? 0 : 1;
}

// The Session (plan P1-21; design T9 A5): one per host (a worker, a Node
// process), shared by the documents attached to it — content-keyed answers
// and the KP memo. Free refuses (1) while a document is attached.
// json: {"budgetBytes": n, "answerers": {"codeTokens.tsm": bool}} (or null)
TSR_EXPORT Session* tsr2_session_new(const char* json) {
  Session* s = new Session((size_t)kPolicySessionBudgetBytes);
  if (json && *json) s->configure(json);
  return s;
}
TSR_EXPORT int tsr2_session_free(Session* s) {
  if (!s || s->refs > 0) return 1;
  delete s;
  return 0;
}
// before the document measures anything (right after tsr_doc_new)
TSR_EXPORT void tsr2_doc_attach(WasmDoc* d, Session* s) { d->doc.attach(s); }

// The JSON request and the per-kind provide exports below are shims of the
// pull above (kept for existing hosts; the runtime uses tsr2_*).
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
  // pending code tokens and image sizes (their ids: need indices)
  out += ",\"tokens\":[";
  bool ft = true;
  for (u32 i = 0; i < d->doc.rt.tokenNeeds.size(); i++) {
    const TokenNeed& r = d->doc.rt.tokenNeeds[i];
    if (r.st != ResState::Pending) continue;
    if (!ft) out += ",";
    ft = false;
    appendf(out, "{\"id\":%u,\"lang\":\"", i);
    jsonEscapeInto(out, d->doc.strs.get(r.lang));
    out += "\",\"text\":\"";
    jsonEscapeInto(out, d->doc.strs.get(r.sent));
    out += "\"}";
  }
  out += "]";
  out += ",\"images\":[";
  bool fi = true;
  for (u32 i = 0; i < d->doc.rt.boxNeeds.size(); i++) {
    const BoxNeed& r = d->doc.rt.boxNeeds[i];
    if (r.st != ResState::Pending || r.kind != BoxKind::Image) continue;
    if (!fi) out += ",";
    fi = false;
    appendf(out, "{\"id\":%u,\"src\":\"", i);
    jsonEscapeInto(out, d->doc.strs.get(r.src));
    out += "\"}";
  }
  out += "]}";
  return out.c_str();
}

// (plan P3-31; design T9 A7) a declared input (inputs.def: labels), before
// Ingest; 0: taken, 1: refused (unknown, or after Ingest: a diagnostic)
TSR_EXPORT int tsr2_set_input(WasmDoc* d, const char* name, const u8* bytes, u32 len) {
  return d->doc.setInput(name ? name : "", std::string_view((const char*)bytes, len)) ? 0 : 1;
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
  d->doc.metrics.provideWord(d->doc.strs.intern(word), (FaceId)faceId, px);
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

// its stylesheet (rulesToCss, plan P3-01): what the rules add to the scopes
// the semantic page writes inline
// (plan P3-21) a product by name (Doc::product: references, docinfo, …), as
// a C string — the shim of tsr2_get for text products
TSR_EXPORT const char* tsr2_product(WasmDoc* d, const char* name) {
  d->productOut = d->doc.product(name ? name : "");
  return d->productOut.c_str();
}
// (plan P3-37; design T9 M9) every product (products.def — the diagnostics
// JSON among them; `program` is binary) by name: u32 length + its bytes; an
// unknown product, or one whose stage has not run, is empty. optsJson is
// reserved (none read yet).
TSR_EXPORT const u8* tsr2_get(WasmDoc* d, const char* product, const char* optsJson) {
  (void)optsJson;
  Stage st;
  const std::string name = product ? product : "";
  d->productOut.assign(4, '\0');
  if (Doc::productStage(name, st) && (d->doc.done(st) || name == "diags" || name == "diagnostics" || name == "settings"))
    d->productOut += d->doc.product(name);
  const u32 n = (u32)d->productOut.size() - 4;
  std::memcpy(d->productOut.data(), &n, 4);
  return (const u8*)d->productOut.data();
}

TSR_EXPORT const char* tsr2_render_css(WasmDoc* d) {
  d->cssOut = d->doc.renderCss();
  return d->cssOut.c_str();
}

// the RenderResult frame (plan P3-05; Doc::renderResult): `held` the keys
// the host holds, 16 bytes each (lo, hi); u32 length + the frame
TSR_EXPORT const u8* tsr2_render_result(WasmDoc* d, const u8* held, int nHeld) {
  std::vector<Key128> keys(nHeld > 0 ? (size_t)nHeld : 0);
  if (nHeld > 0) std::memcpy(keys.data(), held, keys.size() * sizeof(Key128));
  const std::string frame = d->doc.renderResult(keys.data(), keys.size());
  d->htmlOut.assign(4, '\0');
  const u32 len = (u32)frame.size();
  std::memcpy(&d->htmlOut[0], &len, 4);
  d->htmlOut += frame;
  return (const u8*)d->htmlOut.data();
}

// (plan P3-06; design T7 ops.fragment) a preview of what a label names, as
// JSON: {"generation": the last RenderResult's, "html": …} ("html" empty: no
// such label)
TSR_EXPORT const char* tsr2_render_fragment(WasmDoc* d, const char* label) {
  const std::string html = d->doc.renderFragment(label ? label : "");
  std::string out = "{\"generation\":";
  appendf(out, "%llu,\"html\":", (unsigned long long)d->doc.generation);
  jsonString(out, html);
  out += '}';
  d->htmlOut = std::move(out);
  return d->htmlOut.c_str();
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
  if (!d->doc.done(Stage::Layout)) return 0;  // no converged layout
  return (double)d->doc.layout.docHeightSu / 64.0;
}

// Front-end exports (plan P1-09): stateless functions of a .tsm source
// (UTF-8 in, JSON out; byte offsets), for editors and tools. The returned
// buffer is valid until the next call of the same export.
// (plan P3-35) the stateless front end takes the settings document's
// source.* rows (FrontEndOptions); none, or null: the defaults
static FrontEndOptions frontEndOf(const char* settings) {
  FrontEndOptions o;
  if (!settings || !*settings) return o;
  Config c;
  DiagSink ignored;
  applySettings(c, settings, ignored);
  o.frontMatter = c.frontMatter;
  return o;
}
TSR_EXPORT const char* tsr_syntax_tokens(const char* src, const char* settings) {
  static std::string out;
  out = tokensJson(syntaxTokens(std::string_view(src ? src : ""), frontEndOf(settings)));
  return out.c_str();
}
TSR_EXPORT const char* tsr_outline(const char* src, const char* settings) {
  static std::string out;
  out = outlineJson(std::string_view(src ? src : ""), frontEndOf(settings));
  return out.c_str();
}
TSR_EXPORT const char* tsr_parse_json(const char* src, const char* settings) {
  static std::string out;
  out = astJson(std::string_view(src ? src : ""), frontEndOf(settings));
  return out.c_str();
}

// Fragments (plan P2-13; codegen.h has the wire form): a request — texts,
// their source offsets or one clamp span — answered with
// [u32 length][the fragment program, its holes and diagnostics]. No
// document is involved; the buffer lives until the next call.
TSR_EXPORT const u8* tsr2_fragments(const u8* req, int len) {
  static std::string out;
  const std::string body = runFragmentRequest(std::string_view((const char*)req, len > 0 ? (size_t)len : 0));
  out.assign(4, '\0');
  const u32 n = (u32)body.size();
  std::memcpy(&out[0], &n, 4);
  out += body;
  return (const u8*)out.data();
}

// The one ABI handshake (plan P1-01, D-H06): the host checks it before it
// writes a single op. programAbi is lower.def's (the LowerProgram and the
// hole module, plan P2-02), resVersion resources.def's, syntaxVersion
// syntax.def's.
TSR_EXPORT const char* tsr2_abi() {
  static std::string out;
  if (out.empty()) {
    out = "{\"opsWindow\":[" + std::to_string(OPS_MIN_COMPAT) + "," + std::to_string(OPS_VERSION) +
          "],\"schemaHash\":\"" + SCHEMA_HASH +
          "\",\"programAbi\":" + std::to_string(PROGRAM_ABI) + ",\"resVersion\":" + std::to_string(RES_VERSION) +
          ",\"renderVersion\":1,\"syntaxVersion\":" +
          std::to_string(SYNTAX_VERSION) + "}";
  }
  return out.c_str();
}
