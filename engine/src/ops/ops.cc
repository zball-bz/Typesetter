#include "ops.h"

#include <cmath>

#include "domains.gen.h"

namespace tsr {

const char* kindName(Kind k) {
  switch (k) {
#define OP(n, c)
#define KIND(n, c) \
  case Kind::n:    \
    return #n;
#define ARGK(e, n, c)
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
#define ARGK(e, n, c) \
  case ArgK::e:       \
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
    if (end - p < 8) { fail = true; return 0; }
    double d;
    std::memcpy(&d, p, 8);
    p += 8;
    return d;
  }
};
}  // namespace

// Shared arg decoding for MAKE_NODE args and STYLE_PUSH patches (v2).
static const char* readArg(Reader& rd, const RawOps& r, ArgVal& a, bool allowNode) {
  a.key = (ArgK)rd.varint();
  if (a.key == ArgK::ext) {  // EXT (plan P2-05): argKey nameStrRef argVal
    u64 nm = rd.varint();
    if (rd.fail || nm >= r.strings.size()) return "EXT bad name";
    a.name = (u32)nm;
  }
  a.tag = (ArgTag)rd.byte();
  switch (a.tag) {
    case ArgTag::Null: break;
    case ArgTag::Bool: a.num = rd.byte() ? 1 : 0; break;
    case ArgTag::Num: a.num = rd.f64(); break;  // domains are checked per kind (validate)
    case ArgTag::Str: {
      u64 s = rd.varint();
      if (rd.fail || s >= r.strings.size()) return "arg bad str";
      a.ref = (u32)s;
      break;
    }
    case ArgTag::Node: {
      if (!allowNode) return "patch bad tag";
      u64 id = rd.varint();
      if (rd.fail || id >= r.nodes.size()) return "arg bad node id";
      a.ref = (u32)id;
      break;
    }
    default: return "arg bad tag";
  }
  return rd.fail ? "truncated arg" : nullptr;
}

// --- decode-time validation against the schema (plan P0-06) -------------------
// Author-value faults (wrong tag, out of domain, NaN) drop the argument with
// an 'ops-arg' warning; ints and nums are clamped into their domain, -0 → +0,
// flags are masked to their public members. An unknown kind or (kind, key)
// pair makes the node an error{ops-invalid}: unknown vocabulary is never
// silently dropped. After this, consumers never see an out-of-domain value.
static const AttrSpec* findSpec(u16 kind, u16 key) {
  if (kind >= KIND_COUNT) return nullptr;
  const KindInfo& ki = kKinds[kind];
  for (u8 i = 0; i < ki.nAttrs; i++)
    if (ki.attrs[i].key == key) return &ki.attrs[i];
  return nullptr;
}

// true = keep (possibly rewritten); `why` set = report a warning
static bool validateArg(ArgVal& a, const AttrSpec& sp, const RawOps& r, std::string& why) {
  if (a.tag == ArgTag::Null) return false;  // null means absent
  if (sp.dom == Dom::Delta) {  // (plan P2-08) a style change: a childless styled node
    if (a.tag != ArgTag::Node || a.ref >= r.nodes.size() || r.nodes[a.ref].kind != Kind::styled ||
        !r.nodes[a.ref].children.empty()) {
      why = "expected a style change (a childless styled node)";
      return false;
    }
    return true;
  }
  if (a.tag == ArgTag::Node) { why = "node-valued arguments are not accepted"; return false; }
  auto str = [&]() -> std::string_view { return r.strings[a.ref]; };
  auto wantStr = [&](bool ok, const char* what) {
    if (a.tag != ArgTag::Str) { why = std::string("expected ") + what; return false; }
    if (!ok) { why = std::string("not a valid ") + what; return false; }
    return true;
  };
  auto clampNum = [&](double lo, double hi, bool integral) {
    if (a.tag != ArgTag::Num || !std::isfinite(a.num)) { why = "expected a finite number"; return false; }
    double v = integral ? std::trunc(a.num) : a.num;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (v == 0) v = 0;  // -0 → +0
    if (v != a.num) why = "value clamped into its domain";
    a.num = v;
    return true;
  };
  switch (sp.dom) {
    case Dom::Bool:
      if (a.tag != ArgTag::Bool) { why = "expected a boolean"; return false; }
      return true;
    case Dom::Int:
      if (a.tag == ArgTag::Bool && sp.boolAsInt) {
        a.tag = ArgTag::Num;
        a.num = a.num ? 1 : 0;
        return true;
      }
      return clampNum(sp.lo, sp.hi, true);
    case Dom::Num: return clampNum(sp.lo, sp.hi, false);
    case Dom::Flags: {
      if (a.tag != ArgTag::Num || !std::isfinite(a.num) || a.num < 0 || a.num > 9007199254740991.0) {
        why = "expected a non-negative bit set";
        return false;
      }
      u64 v = (u64)a.num, mask = 0;
      for (u8 i = 0; i < sp.nMembers; i++) mask |= (u64)1 << sp.bits[i];
      if (v & ~mask) why = "bits outside the public set were masked";
      a.num = (double)(v & mask);
      return true;
    }
    case Dom::Url:
      // (plan P3-20) a link's or a reference's href: an allowed scheme only
      // (an image's src has its own policy: a placeholder and image-src)
      if (sp.key == (u16)ArgK::url && a.tag == ArgTag::Str && !safeLinkUrl(str())) {
        why = "a link URL is relative, http(s) or mailto";
        return false;
      }
      return wantStr(true, "string");
    case Dom::Str:
    case Dom::Html:
    case Dom::Token: return wantStr(true, "string");
    case Dom::Ident: return wantStr(a.tag == ArgTag::Str && matchDomain(TextDomain::Ident, str()), "identifier");
    case Dom::Label: return wantStr(a.tag == ArgTag::Str && matchDomain(TextDomain::Label, str()), "label");
    case Dom::Lang: return wantStr(a.tag == ArgTag::Str && matchDomain(TextDomain::Lang, str()), "language tag");
    case Dom::RangeSet:
      return wantStr(a.tag == ArgTag::Str && matchDomain(TextDomain::RangeSet, str()), "line range set");
    case Dom::Color: return wantStr(a.tag == ArgTag::Str && matchDomain(TextDomain::Color, str()), "color");
    case Dom::Font: return wantStr(a.tag == ArgTag::Str && matchDomain(TextDomain::Font, str()), "font family list");
    case Dom::Text:
      return wantStr(a.tag == ArgTag::Str && matchDomain((TextDomain)sp.textDom, str()), "value of its domain");
    case Dom::Delta:  // checked above
      return true;
    case Dom::Ext: {  // EXT: a scalar under a validated name (plan P2-05)
      if (!matchDomain(TextDomain::Extname, r.strings[a.name])) {
        why = "EXT name is not [a-z][a-z0-9-]{0,31}";
        return false;
      }
      return a.tag == ArgTag::Bool || a.tag == ArgTag::Num || a.tag == ArgTag::Str;
    }
    case Dom::Enum: {
      bool ok = false;
      if (a.tag == ArgTag::Str)
        for (u8 i = 0; i < sp.nMembers; i++) ok = ok || str() == sp.members[i];
      return wantStr(ok, "member of its enumeration");
    }
  }
  return false;
}

static u32 addString(RawOps& r, std::string s) {
  r.extra.push_back(std::move(s));
  r.strings.push_back(r.extra.back());
  return (u32)(r.strings.size() - 1);
}

// the executor's diagnostic codes (plan P2-01: stable; a DIAG op names one
// of them, anything else reads as script-diag; P2-03 adds the constructor
// ABI's: ctor-arg, ctor-error, hook-recursion; P2-13 a fragment's:
// fragment-splice, fragment-parse — its parser's, "code: message"; P3-14
// table-cells: a row longer than its table's columns; P3-31 labels-import,
// use-module: a declared input or a module the host could not read)
static const char* execDiagCode(std::string_view code) {
  static const char* const kCodes[] = {"splice-undefined", "splice-function", "splice-object", "script-error",
                                       "script-syntax", "region-error", "fence-error", "bib-load",
                                       "ctor-arg", "ctor-error", "hook-recursion", "row-spans-markup",
                                       "style-in-value", "fragment-splice", "fragment-parse", "table-cells",
                                       "labels-import", "use-module"};
  for (const char* c : kCodes)
    if (code == c) return c;
  return "script-diag";
}

bool checkStyledValue(ArgVal& a, std::string_view text, std::string& why) {
  const AttrSpec* sp = findSpec((u16)Kind::styled, (u16)a.key);
  if (!sp || sp->dom == Dom::Delta) {
    why = "not a style attribute";
    return false;
  }
  RawOps r;
  r.strings.push_back(text);
  const u32 ref = a.ref;
  a.ref = 0;
  const bool ok = validateArg(a, *sp, r, why);
  a.ref = ref;
  return ok;
}

Dom styledDom(ArgK k) {
  const AttrSpec* sp = findSpec((u16)Kind::styled, (u16)k);
  return sp ? sp->dom : Dom::Delta;
}

void decodeOps(const u8* buf, size_t len, RawOps& out, DiagSink& diags) {
  out = RawOps{};  // reset before any views exist — safe to move-assign empty
  RawOps& r = out;
  auto bad = [&](const char* msg) {
    diags.add(Sev::Error, "ops-invalid", {}, msg);
    r.ok = false;
  };
  if (len < 5 || std::memcmp(buf, "TSOP", 4) != 0) { bad("bad magic"); return; }
  // version window (plan P1-01): a buffer is written at the newest
  // vocabulary row it uses; anything newer than its byte is malformed
  const u8 ver = buf[4];
  if (ver < OPS_MIN_COMPAT || ver > OPS_VERSION) { bad("ops version outside the reader's window"); return; }
  r.version = ver;
  Reader rd{buf + 5, buf + len};
  struct Pending { u32 node; std::string msg; };
  std::vector<Pending> pendingWarn, pendingErr;
  struct ExecDiag {
    Sev sev;
    const char* code;
    Span span;
    std::string msg;
  };
  std::vector<ExecDiag> pendingDiag;
  u64 nStrings = rd.varint();
  u64 stringBytes = rd.varint();
  u64 nOps = rd.varint();
  // compare lengths, never form an out-of-range pointer (fuzz: strtab-ptr-overflow)
  if (rd.fail || stringBytes > (u64)(rd.end - rd.p)) { bad("truncated header"); return; }
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
    if (opb < std::size(kOpSince) && kOpSince[opb] > ver) { bad("op newer than the buffer version"); return; }
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
        if (rd.fail) { bad("MAKE_NODE truncated"); return; }
        n.kind = kind < KIND_COUNT ? (Kind)kind : Kind::error;
        u64 nargs = rd.varint();
        if (rd.fail || nargs > 64) { bad("MAKE_NODE bad nargs"); return; }
        for (u64 i = 0; i < nargs; i++) {
          ArgVal a;
          const char* err = readArg(rd, r, a, /*allowNode=*/true);
          if (err) { bad(err); return; }
          n.args.push_back(a);
        }
        u64 nch = rd.varint();
        if (rd.fail || nch > 1u << 20) { bad("MAKE_NODE bad nchildren"); return; }
        for (u64 i = 0; i < nch; i++) {
          u64 id = rd.varint();
          if (rd.fail || id >= r.nodes.size()) { bad("child id out of range"); return; }
          n.children.push_back((u32)id);
        }
        // validate against the schema
        std::string invalid;
        if (kind >= KIND_COUNT) invalid = "unknown kind " + std::to_string(kind);
        else if (kKinds[kind].since > ver) invalid = std::string("kind ") + kindName(n.kind) + " is newer than the buffer";
        std::vector<ArgVal> kept;
        for (ArgVal& a : n.args) {
          if (!invalid.empty()) break;
          const AttrSpec* sp = findSpec((u16)kind, (u16)a.key);
          if (sp && sp->since > ver) sp = nullptr;  // newer than the buffer: unknown here
          if (!sp) {
            invalid = std::string("kind ") + kindName(n.kind) + " has no attribute " +
                      ((u16)a.key < ARGK_COUNT ? argName(a.key) : std::to_string((u16)a.key));
            break;
          }
          std::string why;
          if (sp->resolved) why = "set by the resolver, not by input";
          else if (validateArg(a, *sp, r, why)) kept.push_back(a);
          if (!why.empty())
            pendingWarn.push_back({(u32)r.nodes.size(), std::string(kindName(n.kind)) + "." +
                                                            sp->name + ": " + why});
        }
        if (!invalid.empty()) {
          n.kind = Kind::error;
          n.children.clear();
          kept.clear();
          ArgVal m{ArgK::message, ArgTag::Str, 0, addString(r, "invalid ops: " + invalid)};
          ArgVal c{ArgK::code, ArgTag::Str, 0, addString(r, "ops-invalid")};
          kept = {m, c};
          pendingErr.push_back({(u32)r.nodes.size(), invalid});
        }
        n.args = std::move(kept);
        r.nodes.push_back(std::move(n));
        break;
      }
      case Op::EMIT: {
        u64 id = rd.varint();
        if (rd.fail || id >= r.nodes.size()) { bad("EMIT bad id"); return; }
        r.sched.push_back({Op::EMIT, (u32)id});
        break;
      }
      case Op::STYLE_PUSH: {  // (plan P2-08) a delta node: a childless styled node
        u64 id = rd.varint();
        if (rd.fail || id >= r.nodes.size()) { bad("STYLE_PUSH bad id"); return; }
        if (r.nodes[id].kind != Kind::styled || !r.nodes[id].children.empty()) {
          bad("STYLE_PUSH: not a style change (a childless styled node)");
          return;
        }
        r.sched.push_back({Op::STYLE_PUSH, (u32)id});
        break;
      }
      case Op::STYLE_POP_TO: {
        u64 h = rd.varint();
        if (rd.fail) { bad("STYLE_POP_TO truncated"); return; }
        r.sched.push_back({Op::STYLE_POP_TO, (u32)h});
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
      case Op::AT: {
        // an occurrence alias (plan P2-04; design T2 S7): a value spliced
        // again gets a new id that stands for it with this occurrence's span
        u64 id = rd.varint();
        u64 s = rd.varint();
        u64 e = rd.varint();
        if (rd.fail || id >= r.nodes.size()) { bad("AT bad id"); return; }
        RawNode n;
        const RawNode& t = r.nodes[id];
        n.kind = t.kind;
        n.isText = t.isText;
        n.alias = t.alias != kNoAlias ? t.alias : (u32)id;  // aliases never chain
        n.span = {(u32)s, (u32)e};
        r.nodes.push_back(std::move(n));
        break;
      }
      case Op::DECL: {
        // a declaration (plan P2-05): type s e nargs (argKey argVal)* ntempl id*
        RawDecl d;
        u64 type = rd.varint(), s = rd.varint(), e = rd.varint(), nargs = rd.varint();
        if (rd.fail || nargs > 64) { bad("DECL bad header"); return; }
        d.span = {(u32)s, (u32)std::max(s, e)};
        u32 emits = 0;
        for (const SchedItem& it : r.sched) emits += it.op == Op::EMIT;
        d.flowIndex = emits;
        bool ok = type > 0 && type < DECL_COUNT && kDecls[type].since <= ver;
        d.type = ok ? (u16)type : 0;
        for (u64 i = 0; i < nargs; i++) {
          ArgVal a;
          const char* err = readArg(rd, r, a, /*allowNode=*/false);
          if (err) { bad(err); return; }
          const bool named = a.key == ArgK::name && a.tag == ArgTag::Str;
          const bool ext = a.key == ArgK::ext && matchDomain(TextDomain::Extname, r.strings[a.name]) &&
                           (a.tag == ArgTag::Bool || a.tag == ArgTag::Num || a.tag == ArgTag::Str);
          if (named || ext) d.args.push_back(a);
          else diags.add(Sev::Warning, "ops-arg", d.span, "DECL: an argument is neither its name nor EXT data");
        }
        u64 nt = rd.varint();
        if (rd.fail || nt > 1u << 16) { bad("DECL bad template count"); return; }
        for (u64 i = 0; i < nt; i++) {
          u64 id = rd.varint();
          if (rd.fail || id >= r.nodes.size()) { bad("DECL template id out of range"); return; }
          d.templates.push_back((u32)id);
        }
        if (ok) r.decls.push_back(std::move(d));
        else diags.add(Sev::Error, "ops-invalid", d.span, "DECL: unknown declaration type " + std::to_string(type));
        break;
      }
      case Op::RAWMAP: {
        // a text's cooked→raw map (plan P2-04): cooked offsets increasing
        // inside the text, raw offsets non-decreasing; a bad map is dropped
        // with a warning (the text keeps its span)
        u64 id = rd.varint();
        u64 n = rd.varint();
        if (rd.fail || id >= r.nodes.size() || n > (u64)(rd.end - rd.p)) { bad("RAWMAP bad header"); return; }
        std::vector<u32> m;
        bool ok = r.nodes[id].isText && r.nodes[id].alias == kNoAlias;
        u64 pc = 0, pr = 0;
        for (u64 k = 0; k < n; k++) {
          u64 c = rd.varint(), raw = rd.varint();
          if (rd.fail) { bad("RAWMAP truncated"); return; }
          if (ok && ((k && c <= pc) || raw < pr || c > r.strings[r.nodes[id].str].size() || raw > 0xFFFFFFFFu))
            ok = false;
          m.push_back((u32)c);
          m.push_back((u32)raw);
          pc = c;
          pr = raw;
        }
        if (ok) r.nodes[id].rawmap = std::move(m);
        else diags.add(Sev::Warning, "ops-arg", r.nodes[id].span, "text: invalid cooked-to-raw map");
        break;
      }
      case Op::DIAG: {
        // an executor diagnostic (plan P2-01, D-I04): severity, stable code,
        // message, source span — the one channel for execution warnings and
        // the errors of executor-built error nodes
        u8 sev = rd.byte();
        u64 code = rd.varint();
        u64 msg = rd.varint();
        u64 s = rd.varint();
        u64 e = rd.varint();
        if (rd.fail || code >= r.strings.size() || msg >= r.strings.size() || s > e || e > 0xFFFFFFFFull) {
          bad("DIAG malformed");
          return;
        }
        pendingDiag.push_back({sev == 2 ? Sev::Error : sev == 0 ? Sev::Info : Sev::Warning,
                               execDiagCode(r.strings[code]), Span{(u32)s, (u32)e}, std::string(r.strings[msg])});
        break;
      }
      default:
        { bad("unknown op"); return; }
    }
  }
  if (rd.p != rd.end) { bad("trailing bytes"); return; }
  for (const ExecDiag& d : pendingDiag) diags.add(d.sev, d.code, d.span, d.msg);
  // value diagnostics carry the node's span (SPAN ops follow MAKE_NODE)
  for (const Pending& p : pendingErr)
    diags.add(Sev::Error, "ops-invalid", r.nodes[p.node].span, p.msg);
  for (const Pending& p : pendingWarn)
    diags.add(Sev::Warning, "ops-arg", r.nodes[p.node].span, p.msg);
  r.ok = true;
}

std::string dumpOps(const RawOps& r) {
  std::string out;
  for (size_t i = 0; i < r.nodes.size(); i++) {
    const RawNode& n = r.nodes[i];
    if (n.alias != kNoAlias) {
      appendf(out, "%%%zu = AT %%%u @[%u,%u)\n", i, n.alias, n.span.start, n.span.end);
      continue;
    }
    appendf(out, "%%%zu = %s", i, n.isText ? "MAKE_TEXT" : "MAKE_NODE");
    if (n.isText) {
      out += " \"";
      appendEscaped(out, r.strings[n.str]);
      out += "\"";
      for (size_t k = 0; k < n.rawmap.size(); k += 2)
        appendf(out, "%s%u:%u", k ? "," : " raw=", n.rawmap[k], n.rawmap[k + 1]);
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
    else if (s.op == Op::STYLE_PUSH) appendf(out, "STYLE_PUSH %%%u\n", s.a);
    else appendf(out, "STYLE_POP_TO %u\n", s.a);
  }
  return out;
}

}  // namespace tsr
