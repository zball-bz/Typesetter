#include "font.h"

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
  f.variantCps = mathfont::kVariantCps;
  f.parts = mathfont::kAsmParts;
  f.family = mathfont::kFamily;
  f.contentHash = mathfont::kContentHash;
  return f;
}();
}  // namespace

MathFontRegistry::MathFontRegistry() {
  fonts_[0] = &kEuler;
  count_ = 1;
}

const MathFontRegistry& MathFontRegistry::get() {
  static const MathFontRegistry r;
  return r;
}

}  // namespace tsr
