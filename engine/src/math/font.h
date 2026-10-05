// The math font as a runtime object (plan P1-23; design T8 MathFont): the
// layout algorithms read a MathFont, not compiled-in globals; a process-wide
// registry holds them (Euler-Math, embedded, is id 0, zero-copy over the
// generated artifact engine/gen/euler_math.h). The paint side reads its
// line metrics from here too. More fonts arrive through the resource
// protocol (P5-01); the primary font supplies every MATH constant.
#pragma once
#include <string_view>

#include "../../gen/euler_math.h"
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
  int upem = 1000;
  int hheaAsc = 0, hheaDesc = 0;  // the line box a glyph span is pinned to
  int minConnectorOverlap = 0;
  const int16_t* constants = nullptr;
  const GlyphRec* glyphs = nullptr;
  int glyphCount = 0;
  const VarChain* vert = nullptr;
  int vertCount = 0;
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
  // a vertical variant chain (horizontal stretch is a recorded deferral)
  const VarChain* chain(u32 cp) const {
    int lo = 0, hi = vertCount;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (vert[mid].baseCp < cp) lo = mid + 1;
      else hi = mid;
    }
    return lo < vertCount && vert[lo].baseCp == cp ? &vert[lo] : nullptr;
  }
};

// One per WASM instance; documents reference fonts by id.
class MathFontRegistry {
 public:
  static const MathFontRegistry& get();
  const MathFont& primary() const { return *fonts_[0]; }
  const MathFont* byId(u16 id) const { return id < count_ ? fonts_[id] : nullptr; }

 private:
  MathFontRegistry();
  const MathFont* fonts_[4] = {};
  u16 count_ = 0;
};

// Today's literals, in one place (design T8 MathPolicy; the math.* settings
// namespace later): every value converted to su once, with a fixed rounding.
struct MathPolicy {
  // a delimiter may fall short of its target by num/den (TeX's short_fall,
  // integer: target - target*num/den reproduces target -= target/10)
  int shortfallNum = 1, shortfallDen = 10;
  int maxAssemblyRepeats = 64;  // extender repeats tried before giving up
  int missingAdvU = 600, missingAscU = 700;  // an uncovered glyph's stand-in box (design units)
  Su shortfall(Su target) const { return target - target * shortfallNum / shortfallDen; }
};
inline constexpr MathPolicy kMathPolicy{};

}  // namespace tsr
