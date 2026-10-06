// The math font as a runtime object (plan P1-23; design T8 MathFont): the
// layout algorithms read a MathFont, not compiled-in globals; a process-wide
// registry holds them (Euler-Math, embedded, is id 0, zero-copy over the
// generated artifact engine/gen/euler_math.h). The paint side reads its
// line metrics from here too. (Plan P5-01; D-M06) a host's fonts arrive as
// .tsmf blobs (tools/mathc.py --tsmf) in the declared input mathFonts and
// join the registry, deduplicated by the blob's hash; a document's math.fonts
// names its chain — the first font that covers a code point sets it, the
// primary (the first) supplies every MATH constant.
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../gen/euler_math.h"
#include "../support/hash128.h"
#include "../support/support.h"

namespace tsr {

using mathfont::C;
using mathfont::GlyphRec;
using mathfont::VarChain;
using mathfont::AsmPart;

// what a glyph leaf is painted in: a math font, or the document's text font
// (names and operators measured by the host; replaces MathBox::textFont)
constexpr u16 kTextFont = 0xFFFF;

struct MathFont {
  u16 id = 0;
  std::string_view name;    // what math.fonts calls it ("euler": the embedded)
  int upem = 1000;
  int hheaAsc = 0, hheaDesc = 0;  // the line box a glyph span is pinned to
  int minConnectorOverlap = 0;
  const int16_t* constants = nullptr;
  const GlyphRec* glyphs = nullptr;
  int glyphCount = 0;
  const VarChain* vert = nullptr;
  int vertCount = 0;
  const VarChain* horiz = nullptr;  // (plan P3-29) widths: wide accents, braces, arrows
  int horizCount = 0;
  const u32* variantCps = nullptr;
  const AsmPart* parts = nullptr;
  std::string_view family;  // the paint-side family (the font manifest's)
  u64 contentHash = 0;      // the woff2 it paints with (mathc.py)

  int constant(C c) const { return constants[(int)c]; }
  // design units → su at a size (rounded once, at use)
  Su su(double units, double sizePx) const { return (Su)std::llround(units * sizePx * 64.0 / (double)upem); }
  const GlyphRec* glyph(u32 cp) const {
    int lo = 0, hi = glyphCount;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (glyphs[mid].cp < cp) lo = mid + 1;
      else hi = mid;
    }
    return lo < glyphCount && glyphs[lo].cp == cp ? &glyphs[lo] : nullptr;
  }
  // a vertical variant chain, or (plan P3-29) a horizontal one
  const VarChain* chain(u32 cp) const { return find(vert, vertCount, cp); }
  const VarChain* hchain(u32 cp) const { return find(horiz, horizCount, cp); }
  static const VarChain* find(const VarChain* t, int n, u32 cp) {
    int lo = 0, hi = n;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (t[mid].baseCp < cp) lo = mid + 1;
      else hi = mid;
    }
    return lo < n && t[lo].baseCp == cp ? &t[lo] : nullptr;
  }
};

// (plan P5-01) a decoded .tsmf: the font and the tables it points into —
// its name and family views too, so it stays where it was decoded
struct OwnedMathFont {
  OwnedMathFont() = default;
  OwnedMathFont(const OwnedMathFont&) = delete;
  OwnedMathFont& operator=(const OwnedMathFont&) = delete;
  void clear() {
    font = MathFont{};
    name.clear(), family.clear(), constants.clear(), glyphs.clear();
    vert.clear(), horiz.clear(), variantCps.clear(), parts.clear();
    blob = {};
  }
  MathFont font;
  std::string name, family;
  std::vector<int16_t> constants;
  std::vector<GlyphRec> glyphs;
  std::vector<VarChain> vert, horiz;
  std::vector<u32> variantCps;
  std::vector<AsmPart> parts;
  Key128 blob;  // its .tsmf bytes' hash: the registry's identity of it
};

// One per WASM instance (process-wide); documents reference fonts by id.
class MathFontRegistry {
 public:
  static constexpr u16 kMaxFonts = 64;
  static MathFontRegistry& get();
  // the embedded font (Euler Math): the default chain, id 0
  const MathFont& primary() const { return *fonts_[0]; }
  const MathFont* byId(u16 id) const { return id < fonts_.size() ? fonts_[id] : nullptr; }
  // decode a .tsmf blob, validated like ops (magic, version, bounds,
  // sortedness, index ranges, a name and family that are safe to paint);
  // false and err when it is not one
  static bool decode(std::string_view tsmf, OwnedMathFont& out, std::string& err);
  // decode and add it; idempotent by the blob's hash (the same bytes: the
  // font already there); null and err when malformed or the registry is full
  const MathFont* load(std::string_view tsmf, std::string& err);
  // a declared input's blobs, back to back (each says its length): the
  // fonts loaded, in order; a malformed one stops the scan (err)
  std::vector<const MathFont*> loadAll(std::string_view blobs, std::string& err);

 private:
  MathFontRegistry();
  std::vector<const MathFont*> fonts_;
  std::vector<std::unique_ptr<OwnedMathFont>> owned_;
};

// Today's literals, in one place (design T8 MathPolicy; the math.* settings
// namespace later): every value converted to su once, with a fixed rounding.
struct MathPolicy {
  // a delimiter may fall short of its target by num/den (TeX's short_fall,
  // integer: target - target*num/den reproduces target -= target/10)
  int shortfallNum = 1, shortfallDen = 10;
  int maxAssemblyRepeats = 64;  // extender repeats tried before giving up
  int missingAdvU = 600, missingAscU = 700;  // an uncovered glyph's stand-in box (design units)
  // (plan P3-25; finding math/spacing-edge-classes) the room either side of
  // a fraction, in em: TeX's \nulldelimiterspace, Typst's 0.1em — two
  // fractions side by side no longer touch
  int fracPadNum = 1, fracPadDen = 10;
  Su shortfall(Su target) const { return target - target * shortfallNum / shortfallDen; }
};
inline constexpr MathPolicy kMathPolicy{};

}  // namespace tsr
