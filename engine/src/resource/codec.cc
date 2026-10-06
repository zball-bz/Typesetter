#include "codec.h"

#include <cstring>

namespace tsr {

double WireRow::f64(int c) const {
  double v;
  std::memcpy(&v, &col[c], 8);
  return v;
}
void WireRow::setF64(int c, double v) { std::memcpy(&col[c], &v, 8); }

u32 WireBatch::str(std::string_view s) {
  if (s.empty()) return 0;
  strings.emplace_back(s);
  return (u32)strings.size() - 1;
}

namespace {

struct Sink {
  std::string& out;
  void raw(const void* p, size_t n) { out.append((const char*)p, n); }
  void u8_(u8 v) { raw(&v, 1); }
  void u16_(u16 v) { raw(&v, 2); }  // the engine targets are little-endian (wasm32, x86-64, arm64)
  void u32_(u32 v) { raw(&v, 4); }
  void u64_(u64 v) { raw(&v, 8); }
  void f64_(double v) { raw(&v, 8); }
};
struct Source {
  const u8* p;
  size_t n, at = 0;
  bool ok = true;
  bool take(void* dst, size_t k) {
    if (!ok || k > n - at) return ok = false;
    std::memcpy(dst, p + at, k);
    at += k;
    return true;
  }
  u8 u8_() {
    u8 v = 0;
    take(&v, 1);
    return v;
  }
  u16 u16_() {
    u16 v = 0;
    take(&v, 2);
    return v;
  }
  u32 u32_() {
    u32 v = 0;
    take(&v, 4);
    return v;
  }
  u64 u64_() {
    u64 v = 0;
    take(&v, 8);
    return v;
  }
  double f64_() {
    double v = 0;
    take(&v, 8);
    return v;
  }
  // a count of items of `size` bytes each that must still fit
  u32 count(size_t size) {
    u32 c = u32_();
    if (ok && size && (size_t)c > (n - at) / size) ok = false;
    return ok ? c : 0;
  }
};

size_t colSize(ColType t) {
  switch (t) {
    case ColType::U8: return 1;
    case ColType::U16: return 2;
    case ColType::F64: return 8;
    case ColType::Str: case ColType::MetricKey: case ColType::U32: case ColType::U32List:
      return 4;  // Str, MetricKey, U32; a U32List's count
  }
}

}  // namespace

void encodeWire(const WireBatch& b, bool answer, std::string& out) {
  Sink s{out};
  out.clear();
  s.raw(answer ? kWireAnswer : kWireRequest, 4);
  s.u32_(RES_VERSION);
  s.u32_(b.batch);
  s.u32_((u32)b.strings.size());
  for (const std::string& x : b.strings) {
    s.u32_((u32)x.size());
    s.raw(x.data(), x.size());
  }
  if (!answer) {
    s.u32_((u32)b.mks.size());
    for (const WireMetricKey& m : b.mks) {
      s.u32_(m.stack);
      s.u64_(m.faceDigest);
      s.f64_(m.sizePx);
      s.u16_(m.weight);
      s.u8_(m.italic);
      s.u32_(m.features);
      s.u32_(m.lang);
      s.f64_(m.dppx);
    }
  }
  s.u32_((u32)b.kinds.size());
  for (const WireKind& k : b.kinds) {
    const ResKindInfo* info = resKindInfo(k.kind);
    s.u16_(k.kind);
    s.u32_((u32)k.rows.size());
    for (const WireRow& r : k.rows) s.u32_(r.resId);
    if (answer) {
      for (const WireRow& r : k.rows) s.u8_(r.status);
      for (const WireRow& r : k.rows) s.u8_(r.flags);
    }
    const u8 nCols = answer ? info->nAns : info->nKey;
    const ResCol* cols = answer ? info->ans : info->key;
    for (u8 c = 0; c < nCols; c++) {
      for (const WireRow& r : k.rows) {
        switch (cols[c].type) {
          case ColType::U8: s.u8_((u8)r.col[c]); break;
          case ColType::U16: s.u16_((u16)r.col[c]); break;
          case ColType::F64: s.u64_(r.col[c]); break;
          case ColType::U32List: s.u32_((u32)r.list.size()); break;
          case ColType::Str: case ColType::MetricKey: case ColType::U32:
            s.u32_((u32)r.col[c]); break;
        }
      }
      if (cols[c].type == ColType::U32List)
        for (const WireRow& r : k.rows)
          for (u32 v : r.list) s.u32_(v);
    }
    if (answer)
      for (const WireRow& r : k.rows) s.u32_(r.msg);
  }
}

bool decodeWire(const u8* p, size_t n, bool answer, WireBatch& out, std::string& err) {
  Source s{p, n};
  out = WireBatch{};
  out.strings.clear();
  char magic[4] = {};
  s.take(magic, 4);
  if (!s.ok || std::memcmp(magic, answer ? kWireAnswer : kWireRequest, 4) != 0) {
    err = "bad magic";
    return false;
  }
  if (const u32 v = s.u32_(); s.ok && v != RES_VERSION) {
    err = "resource version " + std::to_string(v) + ", expected " + std::to_string(RES_VERSION);
    return false;
  }
  out.batch = s.u32_();
  const u32 nStr = s.count(4);
  out.strings.reserve(nStr);
  for (u32 i = 0; i < nStr && s.ok; i++) {
    const u32 len = s.count(1);
    if (!s.ok) break;
    out.strings.emplace_back((const char*)p + s.at, len);
    s.at += len;
  }
  if (out.strings.empty()) out.strings.emplace_back();
  auto strOk = [&](u64 i) { return i < out.strings.size(); };
  if (!answer) {
    const u32 nMk = s.count(4 + 8 + 8 + 2 + 1 + 4 + 4 + 8);
    for (u32 i = 0; i < nMk && s.ok; i++) {
      WireMetricKey m;
      m.stack = s.u32_();
      m.faceDigest = s.u64_();
      m.sizePx = s.f64_();
      m.weight = s.u16_();
      m.italic = s.u8_();
      m.features = s.u32_();
      m.lang = s.u32_();
      m.dppx = s.f64_();
      if (!strOk(m.stack) || !strOk(m.features) || !strOk(m.lang)) {
        err = "metric key string out of range";
        return false;
      }
      out.mks.push_back(m);
    }
  }
  const u32 nKinds = s.count(2 + 4);
  for (u32 ki = 0; ki < nKinds && s.ok; ki++) {
    WireKind k;
    k.kind = s.u16_();
    const ResKindInfo* info = resKindInfo(k.kind);
    if (!s.ok) break;
    if (!info) {
      err = "unknown resource kind " + std::to_string(k.kind);
      return false;
    }
    const u32 rows = s.count(4);
    k.rows.resize(rows);
    for (WireRow& r : k.rows) r.resId = s.u32_();
    if (answer) {
      if ((size_t)rows * 2 > n - s.at) s.ok = false;
      for (WireRow& r : k.rows) r.status = s.u8_();
      for (WireRow& r : k.rows) r.flags = s.u8_();
    }
    const u8 nCols = answer ? info->nAns : info->nKey;
    const ResCol* cols = answer ? info->ans : info->key;
    for (u8 c = 0; c < nCols && s.ok; c++) {
      const ColType t = cols[c].type;
      if ((size_t)rows * colSize(t) > n - s.at) {
        s.ok = false;
        break;
      }
      for (WireRow& r : k.rows) {
        switch (t) {
          case ColType::U8: r.col[c] = s.u8_(); break;
          case ColType::U16: r.col[c] = s.u16_(); break;
          case ColType::F64: r.col[c] = s.u64_(); break;
          case ColType::U32List: {
            const u32 len = s.u32_();
            r.list.resize(0);
            r.col[c] = len;
            break;
          }
          case ColType::Str: case ColType::MetricKey: case ColType::U32:
            r.col[c] = s.u32_(); break;
        }
        if ((t == ColType::Str && !strOk(r.col[c])) || (t == ColType::MetricKey && r.col[c] >= out.mks.size())) {
          err = std::string("column ") + cols[c].name + " index out of range";
          return false;
        }
      }
      if (t == ColType::U32List)
        for (WireRow& r : k.rows) {
          const u32 len = (u32)r.col[c];
          if ((size_t)len > (n - s.at) / 4) {
            s.ok = false;
            break;
          }
          r.list.resize(len);
          for (u32& v : r.list) v = s.u32_();
        }
    }
    if (answer) {
      if ((size_t)rows * 4 > n - s.at) s.ok = false;
      for (WireRow& r : k.rows) {
        r.msg = s.u32_();
        if (s.ok && !strOk(r.msg)) {
          err = "message index out of range";
          return false;
        }
      }
    }
    out.kinds.push_back(std::move(k));
  }
  if (!s.ok) {
    err = "truncated";
    return false;
  }
  if (s.at != n) {
    err = "trailing bytes";
    return false;
  }
  return true;
}

}  // namespace tsr
