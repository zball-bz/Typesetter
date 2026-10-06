// The resource wire format (plan P1-19; design T9 A1;
// docs/host-protocol-design.md §5): a request batch (TSRQ) and its answer
// (TSRA), little-endian, columns per kind as resources.def declares them.
// This file only moves bytes and checks their structure; what an answer
// means — and whether its values are acceptable — is the ResourceTable's.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "resources.gen.h"

namespace tsr {

// a metric key on the wire: strings are indices into the batch's blob
struct WireMetricKey {
  u32 stack = 0;
  u64 faceDigest = 0;
  double sizePx = 0;
  u16 weight = 400;
  u8 italic = 0;
  u32 features = 0, lang = 0;
  double dppx = 1;
};
struct WireRow {
  u32 resId = 0;
  u8 status = 0;          // answers: 0 answered, 1 failed
  u8 flags = 0;           // answers: bit0 store (a session may keep it)
  u64 col[6] = {};        // the scalar columns in order (F64 as its bits)
  std::vector<u32> list;  // the U32List column, if the kind has one
  u32 msg = 0;            // answers: a string index (0 = "")
  double f64(int c) const;
  void setF64(int c, double v);
};
struct WireKind {
  u16 kind = 0;
  std::vector<WireRow> rows;
};
struct WireBatch {
  u32 batch = 0;
  std::vector<std::string> strings{""};  // [0] is always ""
  std::vector<WireMetricKey> mks;        // requests only
  std::vector<WireKind> kinds;
  u32 str(std::string_view s);  // appends s to the blob, returns its index
};

constexpr char kWireRequest[4] = {'T', 'S', 'R', 'Q'};
constexpr char kWireAnswer[4] = {'T', 'S', 'R', 'A'};

// requests: key columns; answers: status, flags, answer columns, messages
void encodeWire(const WireBatch& b, bool answer, std::string& out);
// false (and `err`) on any structural fault: magic, version, a length past
// the end, an unknown kind, a string or metric-key index out of range
bool decodeWire(const u8* p, size_t n, bool answer, WireBatch& out, std::string& err);

}  // namespace tsr
