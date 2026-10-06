#include "font.h"

#include <algorithm>
#include <cstring>

namespace tsr {

namespace {
// Euler-Math, embedded: the generated artifact wrapped, zero-copy
const MathFont kEuler = [] {
  MathFont f;
  f.id = 0;
  f.upem = mathfont::kUpem;
  f.hheaAsc = mathfont::kAscender;
  f.hheaDesc = mathfont::kDescender;
  f.minConnectorOverlap = mathfont::kMinConnectorOverlap;
  f.constants = mathfont::kConstants;
  f.glyphs = mathfont::kGlyphs;
  f.glyphCount = mathfont::kGlyphCount;
  f.vert = mathfont::kVertChains;
  f.vertCount = mathfont::kVertChainCount;
  f.horiz = mathfont::kHorizChains;
  f.horizCount = mathfont::kHorizChainCount;
  f.variantCps = mathfont::kVariantCps;
  f.parts = mathfont::kAsmParts;
  f.family = mathfont::kFamily;
  f.name = "euler";
  f.contentHash = mathfont::kContentHash;
  return f;
}();

// a little-endian reader over the blob that fails, never reads past it
struct Rd {
  std::string_view b;
  size_t at = 0;
  bool ok = true;
  bool need(size_t n) {
    if (ok && n > b.size() - at) ok = false;
    return ok;
  }
  template <class T>
  T get() {
    T v{};
    if (!need(sizeof(T))) return v;
    std::memcpy(&v, b.data() + at, sizeof(T));
    at += sizeof(T);
    return v;
  }
  std::string_view bytes(size_t n) {
    if (!need(n)) return {};
    std::string_view v = b.substr(at, n);
    at += n;
    return v;
  }
  // a count of records of `size` bytes each that must still fit
  u32 count(size_t size, u32 max) {
    const u32 n = get<u32>();
    if (ok && (n > max || (size_t)n > (b.size() - at) / size)) ok = false;
    return ok ? n : 0;
  }
};
constexpr size_t kGlyphRecBytes = 14, kChainBytes = 12, kPartBytes = 11;
constexpr int kMetricEm = 8;
}  // namespace

MathFontRegistry::MathFontRegistry() { fonts_.push_back(&kEuler); }

MathFontRegistry& MathFontRegistry::get() {
  static MathFontRegistry r;
  return r;
}

bool MathFontRegistry::decode(std::string_view tsmf, OwnedMathFont& o, std::string& err) {
  auto bad = [&](const char* why) {
    err = why;
    return false;
  };
  if (tsmf.size() < 12 || tsmf.substr(0, 4) != "TSMF") return bad("not a .tsmf blob");
  Rd r{tsmf, 4};
  if (r.get<u32>() != 1) return bad("unknown .tsmf version");
  const u32 total = r.get<u32>();
  if (total < 12 || total > tsmf.size()) return bad("truncated");
  if (total < tsmf.size()) return bad("trailing bytes");  // one blob (loadAll splits)
  o.clear();
  MathFont& f = o.font;
  f.upem = (int)r.get<u32>();
  f.hheaAsc = r.get<int32_t>();
  f.hheaDesc = r.get<int32_t>();
  f.minConnectorOverlap = r.get<int32_t>();
  f.contentHash = r.get<u64>();
  if (!r.ok || f.upem < 16 || f.upem > 16384) return bad("bad units per em");
  auto limited = [](int v, int lim) { return v >= -lim && v <= lim; };
  if (!limited(f.hheaAsc, 4 * f.upem) || !limited(f.hheaDesc, 4 * f.upem) || f.hheaAsc + f.hheaDesc <= 0 ||
      !limited(f.minConnectorOverlap, f.upem))
    return bad("bad line metrics");
  o.name = std::string(r.bytes(r.get<u16>()));
  o.family = std::string(r.bytes(r.get<u16>()));
  if (!r.ok) return bad("truncated");
  // a name math.fonts can spell; a family a CSS string holds as is
  if (o.name.empty() || o.name.size() > 64) return bad("bad font name");
  for (char c : o.name)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return bad("bad font name");
  if (o.family.empty() || o.family.size() > 128) return bad("bad family");
  for (char c : o.family)
    if ((u8)c < 0x20 || c == '"' || c == '\\' || c == '<' || c == '>' || c == ';' || (u8)c == 0x7F) return bad("bad family");
  const u32 nc = r.count(2, 1024);
  if (nc != (u32)C::RadicalDegreeBottomRaisePercent + 1) return bad("wrong number of MATH constants");
  o.constants.resize(nc);
  for (int16_t& c : o.constants) c = r.get<int16_t>();
  // a metric is at most kMetricEm em (real fonts: under 4) and a percent
  // one, so no sum of them a formula makes leaves Su's range
  const int lim = kMetricEm * f.upem;
  for (u32 k = 0; k < nc && r.ok; k++) {
    const C c = (C)k;
    const bool pct = c == C::ScriptPercentScaleDown || c == C::ScriptScriptPercentScaleDown ||
                     c == C::RadicalDegreeBottomRaisePercent;
    if (pct ? o.constants[k] < 0 || o.constants[k] > 100 : !limited(o.constants[k], lim))
      return bad("a MATH constant out of range");
  }
  const u32 ng = r.count(kGlyphRecBytes, 1u << 20);
  o.glyphs.resize(ng);
  for (u32 k = 0; k < ng && r.ok; k++) {
    GlyphRec& g = o.glyphs[k];
    g.cp = r.get<u32>();
    g.adv = r.get<uint16_t>();
    g.asc = r.get<int16_t>();
    g.desc = r.get<int16_t>();
    g.italic = r.get<int16_t>();
    g.topAccent = r.get<int16_t>();
    if (g.cp >= 0x110000 || (k > 0 && g.cp <= o.glyphs[k - 1].cp)) return bad("glyph records not sorted by code point");
    if (g.adv > lim || !limited(g.asc, lim) || !limited(g.desc, lim) || !limited(g.italic, lim) ||
        (g.topAccent != mathfont::kNoTopAccent && !limited(g.topAccent, lim)))
      return bad("a glyph metric out of range");
  }
  for (std::vector<VarChain>* t : {&o.vert, &o.horiz}) {
    const u32 n = r.count(kChainBytes, 1u << 16);
    t->resize(n);
    for (u32 k = 0; k < n && r.ok; k++) {
      VarChain& c = (*t)[k];
      c.baseCp = r.get<u32>();
      c.off = r.get<uint16_t>();
      c.n = r.get<uint16_t>();
      c.asmOff = r.get<uint16_t>();
      c.asmN = r.get<uint16_t>();
      if (k > 0 && c.baseCp <= (*t)[k - 1].baseCp) return bad("chains not sorted by base code point");
    }
  }
  const u32 nv = r.count(4, 1u << 16);
  o.variantCps.resize(nv);
  for (u32& v : o.variantCps) v = r.get<u32>();
  const u32 np = r.count(kPartBytes, 1u << 16);
  o.parts.resize(np);
  for (AsmPart& p : o.parts) {
    p.cp = r.get<u32>();
    p.startOverlap = r.get<uint16_t>();
    p.endOverlap = r.get<uint16_t>();
    p.fullAdv = r.get<uint16_t>();
    p.isExtender = r.get<uint8_t>();
    if (p.fullAdv > lim || p.startOverlap > lim || p.endOverlap > lim || p.isExtender > 1)
      return bad("an assembly part out of range");
  }
  if (!r.ok) return bad("truncated");
  if (r.at != r.b.size()) return bad("trailing bytes");
  for (const std::vector<VarChain>* t : {&o.vert, &o.horiz})
    for (const VarChain& c : *t)
      if ((u32)c.off + c.n > nv || (u32)c.asmOff + c.asmN > np) return bad("a chain points past its tables");
  // a size variant or an assembly part is one of the font's own glyphs (it
  // paints by its code point, in this font)
  auto has = [&](u32 cp) {
    auto it = std::lower_bound(o.glyphs.begin(), o.glyphs.end(), cp,
                               [](const GlyphRec& g, u32 c) { return g.cp < c; });
    return it != o.glyphs.end() && it->cp == cp;
  };
  for (u32 v : o.variantCps)
    if (!has(v)) return bad("a size variant without a glyph record");
  for (const AsmPart& p : o.parts)
    if (!has(p.cp)) return bad("an assembly part without a glyph record");
  f.name = o.name;
  f.family = o.family;
  f.constants = o.constants.data();
  f.glyphs = o.glyphs.data();
  f.glyphCount = (int)o.glyphs.size();
  f.vert = o.vert.data();
  f.vertCount = (int)o.vert.size();
  f.horiz = o.horiz.data();
  f.horizCount = (int)o.horiz.size();
  f.variantCps = o.variantCps.data();
  f.parts = o.parts.data();
  return true;
}

const MathFont* MathFontRegistry::load(std::string_view tsmf, std::string& err) {
  const Key128 blob = hash128(tsmf);
  for (const auto& o : owned_)
    if (o->blob == blob) return &o->font;
  auto o = std::make_unique<OwnedMathFont>();
  if (!decode(tsmf, *o, err)) return nullptr;
  o->blob = blob;
  if (fonts_.size() >= kMaxFonts) {
    err = "too many math fonts loaded";
    return nullptr;
  }
  o->font.id = (u16)fonts_.size();
  fonts_.push_back(&o->font);
  owned_.push_back(std::move(o));
  return fonts_.back();
}

std::vector<const MathFont*> MathFontRegistry::loadAll(std::string_view blobs, std::string& err) {
  std::vector<const MathFont*> out;
  while (!blobs.empty()) {
    u32 total = 0;
    if (blobs.size() >= 12) std::memcpy(&total, blobs.data() + 8, 4);
    if (blobs.size() < 12 || blobs.substr(0, 4) != "TSMF" || total < 12) {
      err = "not a .tsmf blob";
      break;
    }
    if (total > blobs.size()) {
      err = "a truncated .tsmf blob";
      break;
    }
    const MathFont* f = load(blobs.substr(0, total), err);
    if (!f) break;
    out.push_back(f);
    blobs.remove_prefix(total);
  }
  return out;
}

}  // namespace tsr
