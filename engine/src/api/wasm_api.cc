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

TSR_EXPORT void tsr_config(WasmDoc* d, double widthPx, double baseSizePx,
                           double lineHeight, double paraIndentEm) {
  d->doc.cfg.widthPx = widthPx;
  if (baseSizePx > 0) d->doc.cfg.baseSizePx = baseSizePx;
  if (lineHeight > 0) d->doc.cfg.lineHeight = lineHeight;
  if (paraIndentEm >= 0) d->doc.cfg.paraIndentEm = paraIndentEm;
}

TSR_EXPORT void tsr_set_punct_compress(WasmDoc* d, int mode) {
  if (mode >= 0 && mode <= 2) d->doc.cfg.punctCompress = (PunctCompress)mode;
}

TSR_EXPORT void tsr_set_font(WasmDoc* d, const char* family) {
  d->doc.cfg.bodyFont = family;
}

TSR_EXPORT void tsr_set_cjk_font(WasmDoc* d, const char* family) {
  d->doc.cfg.cjkFont = family;
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
TSR_EXPORT const char* tsr_measure_requests(WasmDoc* d) {
  MeasureRequest req = d->doc.pendingRequests();
  // group words by style
  std::unordered_map<u32, std::vector<StrRef>> byStyle;
  for (const MeasureItem& it : req.words) byStyle[it.style].push_back(it.str);
  std::vector<u32> styleIds;
  for (StyleId s : req.vmetStyles)
    if (!byStyle.count(s)) byStyle[s] = {};
  for (auto& [s, _] : byStyle) styleIds.push_back(s);
  std::unordered_map<u32, bool> needVmet;
  for (StyleId s : req.vmetStyles) needVmet[s] = true;

  std::string& out = d->reqOut;
  out.clear();
  out += "{\"styles\":[";
  bool first = true;
  for (u32 sid : styleIds) {
    StyleDesc desc = describeStyle(d->doc.cfg, d->doc.styles.get(sid));
    if (!first) out += ",";
    first = false;
    appendf(out, "{\"id\":%u,\"family\":\"", sid);
    jsonEscapeInto(out, desc.family);
    appendf(out, "\",\"sizePx\":%g,\"weight\":%d,\"italic\":%s,\"needVmet\":%s,\"words\":[",
            desc.sizePx, desc.weight, desc.italic ? "true" : "false",
            needVmet.count(sid) ? "true" : "false");
    bool fw = true;
    for (StrRef w : byStyle[sid]) {
      if (!fw) out += ",";
      fw = false;
      out += "\"";
      jsonEscapeInto(out, d->doc.strs.get(w));
      out += "\"";
    }
    out += "]}";
  }
  out += "]}";
  return out.c_str();
}

TSR_EXPORT void tsr_provide_word(WasmDoc* d, const char* word, int styleId, double px) {
  d->doc.metrics.provideWord(d->doc.strs.intern(word), (StyleId)styleId, px, d->doc.cfg);
}

TSR_EXPORT void tsr_provide_vmet(WasmDoc* d, int styleId, double ascPx, double descPx) {
  d->doc.metrics.provideVmet((StyleId)styleId, ascPx, descPx);
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

TSR_EXPORT const char* tsr_diags(WasmDoc* d) {
  d->diagOut = d->doc.dumpDiags();
  return d->diagOut.c_str();
}

TSR_EXPORT double tsr_doc_height_px(WasmDoc* d) {
  return (double)d->doc.layout.docHeightSu / 64.0;
}
