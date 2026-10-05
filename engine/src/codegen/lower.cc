#include "lower.h"

#include <cstring>

namespace tsr {

u64 fnv1a64(std::string_view s) {
  u64 h = 1469598103934665603ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

// The WHATWG UTF-8 decoder's reading: a sequence's allowed second byte
// depends on its lead (no overlongs, no surrogates, nothing past U+10FFFF);
// a broken sequence becomes one U+FFFD for its maximal valid prefix and the
// unexpected byte is read again.
static size_t utf8Seq(std::string_view s, size_t i, bool& ok) {
  const size_t n = s.size();
  const u8 c = (u8)s[i];
  ok = true;
  if (c < 0x80) return 1;
  int need;
  u8 lo = 0x80, hi = 0xBF;
  if (c >= 0xC2 && c <= 0xDF) need = 1;
  else if (c >= 0xE0 && c <= 0xEF) {
    need = 2;
    if (c == 0xE0) lo = 0xA0;
    if (c == 0xED) hi = 0x9F;
  } else if (c >= 0xF0 && c <= 0xF4) {
    need = 3;
    if (c == 0xF0) lo = 0x90;
    if (c == 0xF4) hi = 0x8F;
  } else {
    ok = false;
    return 1;
  }
  size_t j = i + 1;
  for (int got = 0; got < need; got++) {
    if (j >= n || (u8)s[j] < lo || (u8)s[j] > hi) {
      ok = false;
      return j - i;
    }
    lo = 0x80;
    hi = 0xBF;
    j++;
  }
  return j - i;
}

static bool validUtf8(std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    if ((u8)s[i] < 0x80) {
      i++;
      continue;
    }
    bool ok;
    i += utf8Seq(s, i, ok);
    if (!ok) return false;
  }
  return true;
}

void appendUtf8Sanitized(std::string& out, std::string_view s) {
  if (validUtf8(s)) {
    out += s;
    return;
  }
  for (size_t i = 0; i < s.size();) {
    bool ok;
    size_t k = utf8Seq(s, i, ok);
    if (ok) out.append(s.data() + i, k);
    else out += "\xEF\xBF\xBD";
    i += k;
  }
}

u32 utf16Length(std::string_view s) {
  u32 n = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) n++;
    if (c >= 0xF0) n++;
  }
  return n;
}

// ---- writer -------------------------------------------------------------------
static void putVarint(std::string& out, u32 v) {
  while (v > 127) {
    out += (char)((v & 127) | 128);
    v >>= 7;
  }
  out += (char)v;
}

void LowerWriter::u(u32 v) { putVarint(body, v); }

u32 LowerWriter::str(std::string_view s) {
  std::string clean;
  appendUtf8Sanitized(clean, s);
  auto it = strIdx_.find(clean);
  if (it != strIdx_.end()) return it->second;
  u32 r = (u32)strs_.size();
  strIdx_.emplace(clean, r);
  strs_.push_back(std::move(clean));
  return r;
}

u32 LowerWriter::ctor(std::string_view name) {
  u32 r = str(name);
  for (u32 i = 0; i < ctors_.size(); i++)
    if (ctors_[i] == r) return i;
  ctors_.push_back(r);
  return (u32)ctors_.size() - 1;
}

std::string LowerWriter::finish(u64 hash, bool module, u32 docEnd) const {
  std::string out = "TSLP";
  out += (char)LOWER_VERSION;
  for (int i = 0; i < 4; i++) out += (char)((PROGRAM_ABI >> (8 * i)) & 0xff);
  for (int i = 0; i < 8; i++) out += (char)((hash >> (8 * i)) & 0xff);
  putVarint(out, module ? 1 : 0);
  putVarint(out, docEnd);
  putVarint(out, (u32)strs_.size());
  for (const std::string& s : strs_) {
    putVarint(out, (u32)s.size());
    putVarint(out, utf16Length(s));
  }
  for (const std::string& s : strs_) out += s;
  putVarint(out, (u32)ctors_.size());
  for (u32 c : ctors_) putVarint(out, c);
  putVarint(out, (u32)blocks.size());
  for (const LBlockRow& b : blocks) {
    out += (char)(u8)b.kind;
    out += (char)b.flags;
    for (u32 v : {b.s, b.e, b.holeLo, b.holeHi, b.pc}) putVarint(out, v);
  }
  putVarint(out, holes);
  putVarint(out, (u32)pieces.size());
  for (const LPieceRow& p : pieces) {
    out += (char)(u8)p.kind;
    for (u32 v : {p.ref, p.js0, p.js1}) putVarint(out, v);
  }
  putVarint(out, (u32)body.size());
  out += body;
  return out;
}

// ---- reader -------------------------------------------------------------------
namespace {
constexpr int kMaxDepth = 4096;  // nesting of values (far beyond any document)

struct Cur {
  std::string_view b;
  size_t p = 0;
  bool bad = false;
  bool u(u32& v) {
    v = 0;
    for (int shift = 0; shift < 35; shift += 7) {
      if (p >= b.size()) return false;
      u8 c = (u8)b[p++];
      if (shift == 28 && (c & 0x70)) return false;  // beyond 32 bits
      v |= (u32)(c & 127) << shift;
      if (!(c & 128)) return true;
    }
    return false;
  }
  bool byte(u8& v) {
    if (p >= b.size()) return false;
    v = (u8)b[p++];
    return true;
  }
  bool fixed(u64& v, int n) {
    if (p + n > b.size()) return false;
    v = 0;
    for (int i = 0; i < n; i++) v |= (u64)(u8)b[p + i] << (8 * i);
    p += n;
    return true;
  }
};

// Walks (and when `out` is set, prints) the op grammar of lower.def.
struct Walker {
  const LowerProgram& P;
  Cur c;
  std::string* out;
  std::string why;
  u32 next = 0;  // holes are numbered in preorder: the next one expected

  bool fail(const char* w) {
    if (why.empty()) why = w;
    return false;
  }
  void indent(int d) {
    if (out) out->append(2 * (size_t)d, ' ');
  }
  bool span(u32& s, u32& e) {
    if (!c.u(s) || !c.u(e)) return fail("truncated span");
    if (s > e || e > P.docEnd) return fail("span out of range");
    if (out) appendf(*out, " [%u,%u)", s, e);
    return true;
  }
  bool strRef(u32& r) {
    if (!c.u(r)) return fail("truncated string ref");
    if (r >= P.strs.size()) return fail("string ref out of range");
    return true;
  }
  void quoted(u32 r) {
    if (!out) return;
    *out += '"';
    appendEscaped(*out, P.strs[r]);
    *out += '"';
  }
  bool constant(int depth) {
    if (depth > 8) return fail("constant nesting");
    u8 tag;
    if (!c.byte(tag)) return fail("truncated constant");
    switch ((LConst)tag) {
      case LConst::Null:
        if (out) *out += "null";
        return true;
      case LConst::False:
      case LConst::True:
        if (out) *out += (LConst)tag == LConst::True ? "true" : "false";
        return true;
      case LConst::Uint: {
        u32 v;
        if (!c.u(v)) return fail("truncated constant");
        if (out) appendf(*out, "%u", v);
        return true;
      }
      case LConst::F64: {
        u64 bits;
        if (!c.fixed(bits, 8)) return fail("truncated constant");
        double d;
        std::memcpy(&d, &bits, 8);
        if (d != d) return fail("NaN constant");
        if (out) appendf(*out, "%g", d);
        return true;
      }
      case LConst::Str: {
        u32 r;
        if (!strRef(r)) return false;
        quoted(r);
        return true;
      }
      case LConst::Array: {
        u32 n;
        if (!c.u(n)) return fail("truncated constant");
        if (n > c.b.size() - c.p) return fail("array length out of range");
        if (out) *out += "[";
        for (u32 i = 0; i < n; i++) {
          if (out && i) *out += ",";
          if (!constant(depth + 1)) return false;
        }
        if (out) *out += "]";
        return true;
      }
    }
    return fail("unknown constant tag");
  }
  // a hole reference; each is the next in preorder
  bool holeRef(u32& h) {
    if (!c.u(h)) return fail("truncated hole ref");
    if (h != next) return fail("hole out of preorder");
    if (next++ >= P.holes) return fail("hole ref out of range");
    return true;
  }
  // a FENCE / REGION args operand: 0, or (hole + 1) << 1 | awaits
  bool argsRef(u32& a, bool& awaits) {
    awaits = false;
    if (!c.u(a)) return fail("truncated args ref");
    if (a == 0) return true;
    if (a < 2) return fail("bad args ref");
    awaits = a & 1;
    if ((a >> 1) - 1 != next) return fail("hole out of preorder");
    if (next++ >= P.holes) return fail("hole ref out of range");
    return true;
  }
  bool count(u32& n) {
    if (!c.u(n)) return fail("truncated count");
    if (n > c.b.size() - c.p) return fail("count out of range");  // each item takes a byte
    return true;
  }
  // one value; async: the op claims to await (checked against its kids).
  // rows: a REGION item, where ROWS (a table paragraph) is a value too
  bool value(int depth, bool& awaits, bool rows = false) {
    if (depth > kMaxDepth) return fail("nesting too deep");
    u8 opb;
    if (!c.byte(opb)) return fail("truncated op");
    const bool async = opb & kLopAsync;
    const Lop op = (Lop)(opb & 0x7f);
    awaits = async;
    indent(depth);
    if (out && lopName(opb)) *out += lopName(opb);
    if (out && async) *out += " async";
    bool kidsAwait = false;
    u32 s, e;
    switch (op) {
      case Lop::TEXT: {
        u32 r;
        if (!strRef(r)) return false;
        if (out) *out += " ";
        quoted(r);
        if (!span(s, e)) return false;
        if (out) *out += "\n";
        if (async) return fail("TEXT cannot await");
        return true;
      }
      case Lop::CALL: {
        u8 flags;
        u32 ct, na, nk;
        if (!c.byte(flags) || !c.u(ct)) return fail("truncated CALL");
        if (ct >= P.ctors.size()) return fail("ctor out of range");
        if (out) *out += " " + std::string(P.strs[P.ctors[ct]]);
        if ((flags & kCallSpanned) && !span(s, e)) return false;
        if (flags & ~kCallSpanned) return fail("unknown CALL flags");
        if (!count(na)) return false;
        for (u32 i = 0; i < na; i++) {
          u32 k;
          if (!strRef(k)) return false;
          if (out) *out += " " + std::string(P.strs[k]) + "=";
          if (!constant(0)) return false;
        }
        if (!count(nk)) return false;
        if (out) *out += "\n";
        for (u32 i = 0; i < nk; i++) {
          bool a;
          if (!value(depth + 1, a)) return false;
          kidsAwait |= a;
        }
        break;
      }
      case Lop::HOLE: {
        u32 h, nk;
        if (!holeRef(h)) return false;
        if (out) appendf(*out, " %u", h);
        if (!span(s, e) || !count(nk)) return false;
        if (out) *out += "\n";
        for (u32 i = 0; i < nk; i++) {
          bool a;
          if (!value(depth + 1, a)) return false;
          kidsAwait |= a;
        }
        if (kidsAwait && !async) return fail("HOLE with awaiting kids must await");
        return true;  // a hole may await on its own
      }
      case Lop::FRAME: {
        u32 lo, hi;
        if (!span(s, e) || !c.u(lo) || !c.u(hi)) return fail("truncated FRAME");
        if (lo != next || lo > hi || hi > P.holes) return fail("FRAME hole range out of range");
        if (out) appendf(*out, " holes %u..%u\n", lo, hi);
        bool a;
        if (!value(depth + 1, a, rows)) return false;
        if (next != hi) return fail("FRAME hole range differs from its holes");
        kidsAwait = a;
        break;
      }
      case Lop::FENCE: {
        u32 lang, args, body, off;
        bool argsAwait;
        if (!strRef(lang) || !argsRef(args, argsAwait) || !strRef(body) || !c.u(off)) return fail("truncated FENCE");
        if (off > P.docEnd) return fail("FENCE body offset out of range");
        if (out) {
          *out += " ";
          quoted(lang);
          if (args) appendf(*out, " args=hole %u%s", (args >> 1) - 1, argsAwait ? " async" : "");
          *out += " body=";
          quoted(body);
          appendf(*out, " at=%u lines=", off);
        }
        if (!constant(0) || !span(s, e)) return false;
        if (out) *out += "\n";
        if (!async) return fail("FENCE must await");
        return true;
      }
      case Lop::REGION: {
        u32 name, args, n;
        bool argsAwait;
        if (!strRef(name) || !argsRef(args, argsAwait)) return fail("truncated REGION");
        if (out) {
          *out += " ";
          quoted(name);
          if (args) appendf(*out, " args=hole %u%s", (args >> 1) - 1, argsAwait ? " async" : "");
        }
        if (!span(s, e) || !count(n)) return false;
        if (out) *out += "\n";
        for (u32 i = 0; i < n; i++) {
          bool a;
          if (!value(depth + 1, a, true)) return false;
          kidsAwait |= a;
        }
        (void)argsAwait;
        if (!async) return fail("REGION must await");
        return true;
      }
      case Lop::ROWS: {
        if (!rows) return fail("ROWS outside a REGION");
        u32 nr;
        if (!count(nr)) return false;
        if (out) *out += "\n";
        for (u32 r = 0; r < nr; r++) {
          u32 nc;
          if (!count(nc)) return false;
          indent(depth + 1);
          if (out) *out += "row\n";
          for (u32 k = 0; k < nc; k++) {
            bool a;
            if (!value(depth + 2, a)) return false;
            kidsAwait |= a;
          }
        }
        break;
      }
      default:
        return fail("op not a value");
    }
    if (kidsAwait != async) return fail("async bit disagrees with the subtree");
    return true;
  }
};
}  // namespace

bool readLowerProgram(std::string_view bytes, LowerProgram& p, std::string& why) {
  p = LowerProgram{};
  Cur c{bytes};
  auto bad = [&](const char* w) {
    why = w;
    return false;
  };
  if (bytes.size() < 17 || bytes.substr(0, 4) != "TSLP") return bad("not a LowerProgram");
  c.p = 4;
  u8 ver;
  u64 abi, hash;
  c.byte(ver);
  c.fixed(abi, 4);
  c.fixed(hash, 8);
  p.version = ver;
  p.abi = (u32)abi;
  p.hash = hash;
  if (p.version != LOWER_VERSION) return bad("program version differs");
  if (p.abi != PROGRAM_ABI) return bad("program ABI differs");
  u32 mod, n;
  if (!c.u(mod) || mod > 1 || !c.u(p.docEnd)) return bad("truncated header");
  p.module = mod == 1;
  if (!c.u(n) || n > bytes.size() - c.p) return bad("bad string count");
  std::vector<std::pair<u32, u32>> lens(n);
  u64 blob = 0;
  for (auto& [bl, ul] : lens) {
    if (!c.u(bl) || !c.u(ul)) return bad("truncated string table");
    blob += bl;
  }
  if (blob > bytes.size() - c.p) return bad("string blob out of range");
  for (auto [bl, ul] : lens) {
    std::string_view s = bytes.substr(c.p, bl);
    if (!validUtf8(s)) return bad("string not UTF-8");
    if (utf16Length(s) != ul) return bad("string UTF-16 length differs");
    p.strs.push_back(s);
    c.p += bl;
  }
  if (!c.u(n) || n > bytes.size() - c.p) return bad("bad ctor count");
  for (u32 i = 0; i < n; i++) {
    u32 r;
    if (!c.u(r) || r >= p.strs.size()) return bad("ctor name out of range");
    p.ctors.push_back(r);
  }
  if (!c.u(n) || n > bytes.size() - c.p) return bad("bad block count");
  for (u32 i = 0; i < n; i++) {
    LBlockRow b;
    u8 kind;
    if (!c.byte(kind) || !c.byte(b.flags)) return bad("truncated block table");
    if (kind > (u8)LBlock::Verbatim) return bad("unknown block kind");
    b.kind = (LBlock)kind;
    if (b.flags & ~(kBlockUser | kBlockFramed | kBlockAsync)) return bad("unknown block flags");
    if (!c.u(b.s) || !c.u(b.e) || !c.u(b.holeLo) || !c.u(b.holeHi) || !c.u(b.pc))
      return bad("truncated block table");
    p.blocks.push_back(b);
  }
  if (!c.u(p.holes)) return bad("truncated hole count");
  if (!c.u(n) || n > bytes.size() - c.p) return bad("bad piece count");
  for (u32 i = 0; i < n; i++) {
    LPieceRow r;
    u8 kind;
    if (!c.byte(kind) || kind > (u8)LPiece::Verbatim) return bad("bad piece kind");
    r.kind = (LPiece)kind;
    if (!c.u(r.ref) || !c.u(r.js0) || !c.u(r.js1)) return bad("truncated piece table");
    if (r.js0 > r.js1) return bad("piece range inverted");
    p.pieces.push_back(r);
  }
  u32 bodyLen;
  if (!c.u(bodyLen) || bodyLen != bytes.size() - c.p) return bad("body length differs");
  p.body = bytes.substr(c.p);
  if (p.module != !p.pieces.empty()) return bad("module flag disagrees with the pieces");
  // pieces: the holes in order, then the verbatim blocks
  u32 nh = 0;
  for (const LPieceRow& r : p.pieces) {
    if (r.kind == LPiece::Hole) {
      if (r.ref != nh++) return bad("hole pieces out of order");
    } else if (r.ref >= p.blocks.size() || p.blocks[r.ref].kind != LBlock::Verbatim) {
      return bad("verbatim piece names no verbatim block");
    }
  }
  if (nh != p.holes) return bad("hole count differs from the pieces");
  if (p.pieces.size() - nh > p.blocks.size()) return bad("more verbatim pieces than blocks");
  // blocks tile the body; each holds exactly its grammar
  u32 next = 0, verbatims = 0;
  for (size_t i = 0; i < p.blocks.size(); i++) {
    const LBlockRow& b = p.blocks[i];
    const u32 end = i + 1 < p.blocks.size() ? p.blocks[i + 1].pc : bodyLen;
    if (b.pc > end || end > bodyLen) return bad("block offsets out of order");
    if (b.s > b.e || b.e > p.docEnd) return bad("block span out of range");
    if (b.holeLo > b.holeHi || b.holeHi > p.holes) return bad("block hole range out of range");
    if (b.holeLo != next) return bad("block hole range out of order");
    Walker w{p, Cur{p.body.substr(0, end), b.pc}, nullptr, {}, next};
    bool awaits = false;
    if (b.kind == LBlock::Content) {
      if (!w.value(0, awaits)) {
        why = w.why;
        return false;
      }
    } else {
      u8 opb;
      u32 ref;
      if (!w.c.byte(opb) || !w.c.u(ref)) return bad("truncated statement block");
      awaits = opb & kLopAsync;
      if (b.kind == LBlock::Stmt) {
        if ((opb & 0x7f) != (u8)Lop::STMT || ref != w.next++ || ref >= p.holes) return bad("bad STMT block");
      } else {
        const u64 at = (u64)p.holes + ref;
        if ((opb & 0x7f) != (u8)Lop::VERBATIM || ref != verbatims++ || at >= p.pieces.size() ||
            p.pieces[at].kind != LPiece::Verbatim || p.pieces[at].ref != i)
          return bad("bad VERBATIM block");
        if (awaits) return bad("VERBATIM cannot await");
      }
    }
    if (w.c.p != end) return bad("block does not end where the next begins");
    if (w.next != b.holeHi) return bad("block hole range differs from its holes");
    next = w.next;
    if (awaits != bool(b.flags & kBlockAsync)) return bad("block async flag differs from its ops");
  }
  return true;
}

std::string dumpLowerProgram(std::string_view bytes) {
  LowerProgram p;
  std::string why;
  if (!readLowerProgram(bytes, p, why)) return "invalid: " + why + "\n";
  std::string out;
  appendf(out, "lower v%u abi %08x docEnd %u holes %u", p.version, p.abi, p.docEnd, p.holes);
  if (p.module) appendf(out, " module %016llx\n", (unsigned long long)p.hash);
  else out += " module none\n";
  for (size_t i = 0; i < p.pieces.size(); i++) {
    const LPieceRow& r = p.pieces[i];
    appendf(out, "piece %zu %s %u js [%u,%u)\n", i, r.kind == LPiece::Hole ? "hole" : "verbatim block",
            r.ref, r.js0, r.js1);
  }
  static const char* kKinds[] = {"content", "stmt", "verbatim"};
  for (size_t i = 0; i < p.blocks.size(); i++) {
    const LBlockRow& b = p.blocks[i];
    const u32 end = i + 1 < p.blocks.size() ? p.blocks[i + 1].pc : (u32)p.body.size();
    appendf(out, "block %zu %s [%u,%u)", i, kKinds[(u8)b.kind], b.s, b.e);
    if (b.flags & kBlockUser) out += " user";
    if (b.flags & kBlockFramed) out += " framed";
    if (b.flags & kBlockAsync) out += " async";
    if (b.holeHi > b.holeLo) appendf(out, " holes %u..%u", b.holeLo, b.holeHi);
    out += "\n";
    Walker w{p, Cur{p.body.substr(0, end), b.pc}, &out, {}, b.holeLo};
    if (b.kind == LBlock::Content) {
      bool a;
      w.value(1, a);
    } else {
      u8 opb;
      u32 ref;
      w.c.byte(opb);
      w.c.u(ref);
      appendf(out, "  %s%s %u\n", lopName(opb), (opb & kLopAsync) ? " async" : "", ref);
    }
  }
  return out;
}

}  // namespace tsr
