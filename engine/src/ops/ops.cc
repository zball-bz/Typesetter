#include "ops.h"

namespace tsr {

const char* kindName(Kind k) {
  switch (k) {
#define OP(n, c)
#define KIND(n, c) \
  case Kind::n:    \
    return #n;
#define ARGK(n, c)
#include "ops.def"
#undef OP
#undef KIND
#undef ARGK
  }
  return "?";
}

const char* argName(ArgK a) {
  switch (a) {
#define OP(n, c)
#define KIND(n, c)
#define ARGK(n, c) \
  case ArgK::n:    \
    return #n;
#include "ops.def"
#undef OP
#undef KIND
#undef ARGK
  }
  return "?";
}

namespace {
struct Reader {
  const u8* p;
  const u8* end;
  bool fail = false;
  u8 byte() {
    if (p >= end) { fail = true; return 0; }
    return *p++;
  }
  u64 varint() {
    u64 v = 0;
    int shift = 0;
    for (;;) {
      u8 b = byte();
      if (fail) return 0;
      v |= (u64)(b & 0x7F) << shift;
      if (!(b & 0x80)) return v;
      shift += 7;
      if (shift > 63) { fail = true; return 0; }
    }
  }
  double f64() {
    if (p + 8 > end) { fail = true; return 0; }
    double d;
    std::memcpy(&d, p, 8);
    p += 8;
    return d;
  }
};
}  // namespace

void decodeOps(const u8* buf, size_t len, RawOps& out, DiagSink& diags) {
  out = RawOps{};  // reset before any views exist — safe to move-assign empty
  RawOps& r = out;
  auto bad = [&](const char* msg) {
    diags.add(Sev::Error, "ops-invalid", {}, msg);
    r.ok = false;
  };
  if (len < 5 || std::memcmp(buf, "TSOP", 4) != 0) { bad("bad magic"); return; }
  if (buf[4] != OPS_VERSION) { bad("ops version mismatch"); return; }
  Reader rd{buf + 5, buf + len};
  u64 nStrings = rd.varint();
  u64 stringBytes = rd.varint();
  u64 nOps = rd.varint();
  if (rd.fail || rd.p + stringBytes > rd.end) { bad("truncated header"); return; }
  r.blob.assign((const char*)rd.p, stringBytes);
  rd.p += stringBytes;
  u64 prev = 0;
  for (u64 i = 0; i < nStrings; i++) {
    u64 off = rd.varint();
    if (rd.fail || off < prev || off > stringBytes) { bad("bad string table"); return; }
    r.strings.push_back(std::string_view(r.blob).substr(prev, off - prev));
    prev = off;
  }
  for (u64 k = 0; k < nOps; k++) {
    u8 opb = rd.byte();
    if (rd.fail) { bad("truncated ops"); return; }
    switch ((Op)opb) {
      case Op::MAKE_TEXT: {
        u64 s = rd.varint();
        if (rd.fail || s >= r.strings.size()) { bad("MAKE_TEXT bad str"); return; }
        RawNode n;
        n.kind = Kind::text;
        n.isText = true;
        n.str = (StrRef)s;
        r.nodes.push_back(std::move(n));
        break;
      }
      case Op::MAKE_NODE: {
        RawNode n;
        u64 kind = rd.varint();
        if (rd.fail || kind >= KIND_COUNT) { bad("MAKE_NODE bad kind"); return; }
        n.kind = (Kind)kind;
        u64 nargs = rd.varint();
        if (rd.fail || nargs > 64) { bad("MAKE_NODE bad nargs"); return; }
        for (u64 i = 0; i < nargs; i++) {
          ArgVal a;
          a.key = (ArgK)rd.varint();
          a.tag = (ArgTag)rd.byte();
          switch (a.tag) {
            case ArgTag::Null: break;
            case ArgTag::Bool: a.num = rd.byte() ? 1 : 0; break;
            case ArgTag::Num: a.num = rd.f64(); break;
            case ArgTag::Str: {
              u64 s = rd.varint();
              if (rd.fail || s >= r.strings.size()) { bad("arg bad str"); return; }
              a.ref = (u32)s;
              break;
            }
            case ArgTag::Node: {
              u64 id = rd.varint();
              if (rd.fail || id >= r.nodes.size()) { bad("arg bad node id"); return; }
              a.ref = (u32)id;
              break;
            }
            default: { bad("arg bad tag"); return; }
          }
          if (rd.fail) { bad("truncated arg"); return; }
          n.args.push_back(a);
        }
        u64 nch = rd.varint();
        if (rd.fail || nch > 1u << 20) { bad("MAKE_NODE bad nchildren"); return; }
        for (u64 i = 0; i < nch; i++) {
          u64 id = rd.varint();
          if (rd.fail || id >= r.nodes.size()) { bad("child id out of range"); return; }
          n.children.push_back((u32)id);
        }
        r.nodes.push_back(std::move(n));
        break;
      }
      case Op::EMIT: {
        u64 id = rd.varint();
        if (rd.fail || id >= r.nodes.size()) { bad("EMIT bad id"); return; }
        r.sched.push_back({Op::EMIT, (u32)id, 0});
        break;
      }
      case Op::STYLE_PUSH: {
        u64 bits = rd.varint();
        u8 npatch = rd.byte();
        if (rd.fail || npatch != 0) { bad("STYLE_PUSH patch unsupported"); return; }
        r.sched.push_back({Op::STYLE_PUSH, 0, bits});
        break;
      }
      case Op::STYLE_POP_TO: {
        u64 h = rd.varint();
        if (rd.fail) { bad("STYLE_POP_TO truncated"); return; }
        r.sched.push_back({Op::STYLE_POP_TO, (u32)h, 0});
        break;
      }
      case Op::SPAN: {
        u64 id = rd.varint();
        u64 s = rd.varint();
        u64 e = rd.varint();
        if (rd.fail || id >= r.nodes.size()) { bad("SPAN bad id"); return; }
        r.nodes[id].span = {(u32)s, (u32)e};
        break;
      }
      default:
        { bad("unknown op"); return; }
    }
  }
  if (rd.p != rd.end) { bad("trailing bytes"); return; }
  r.ok = true;
}

std::string dumpOps(const RawOps& r) {
  std::string out;
  for (size_t i = 0; i < r.nodes.size(); i++) {
    const RawNode& n = r.nodes[i];
    appendf(out, "%%%zu = %s", i, n.isText ? "MAKE_TEXT" : "MAKE_NODE");
    if (n.isText) {
      out += " \"";
      appendEscaped(out, r.strings[n.str]);
      out += "\"";
    } else {
      appendf(out, " %s", kindName(n.kind));
      for (const ArgVal& a : n.args) {
        appendf(out, " %s=", argName(a.key));
        switch (a.tag) {
          case ArgTag::Null: out += "null"; break;
          case ArgTag::Bool: out += a.num ? "true" : "false"; break;
          case ArgTag::Num: appendf(out, "%g", a.num); break;
          case ArgTag::Str:
            out += "\"";
            appendEscaped(out, r.strings[a.ref]);
            out += "\"";
            break;
          case ArgTag::Node: appendf(out, "%%%u", a.ref); break;
        }
      }
      out += " children=[";
      for (size_t c = 0; c < n.children.size(); c++)
        appendf(out, "%s%%%u", c ? "," : "", n.children[c]);
      out += "]";
    }
    if (!n.span.empty()) appendf(out, " @[%u,%u)", n.span.start, n.span.end);
    out += "\n";
  }
  for (const SchedItem& s : r.sched) {
    if (s.op == Op::EMIT) appendf(out, "EMIT %%%u\n", s.a);
    else if (s.op == Op::STYLE_PUSH) appendf(out, "STYLE_PUSH bits=0x%llx\n", (unsigned long long)s.bits);
    else appendf(out, "STYLE_POP_TO %u\n", s.a);
  }
  return out;
}

}  // namespace tsr
