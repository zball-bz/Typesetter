// libFuzzer target: a host math font (plan P5-01; D-M06) — the .tsmf
// decoder and what math layout does with a font it accepts. A .tsmf arrives
// as untrusted bytes in the declared input mathFonts: whatever it holds,
// decoding must stay in bounds, an accepted font's tables must be what its
// lookups assume (sorted, indices in range), and every formula must lay out
// with it as the primary font (its MATH constants) and as a fallback.
#include <cstddef>
#include <cstdint>
#include <string>

#include "../src/math/env.h"
#include "../src/math/math.h"

using namespace tsr;

namespace {
// what reaches every table: scripts and limits, fractions, radicals, the
// vertical and horizontal stretch paths, accents, grids, text runs
const char* const kFormulas[] = {
    "x = (-b +- sqrt(b^2 - 4 a c))/(2 a)",
    "sum_(n=1)^oo 1/n^s < oo <=> s > 1",
    "hat(f)(xi) = int_(-oo)^oo f(x) e^(-2 pi i x xi) d x",
    "lr(( 1/(2/(3/(4/5))) ))",
    "overbrace(a + b + c, n) underbrace(x y z, m) widehat(x y z)",
    "vec(e) bar(x) root(3, 8) binom(n, k)",
    "f(x) = cases(x & \"if\" x >= 0; -x & \"otherwise\")",
    "mat(1, 2; 3, 4) vmat(x & y; z & w)",
    "a_(b_(c_(d_e)))^(f^(g^h)) varrho varsigma",
    "lim_(h -> 0) (f(x+h) - f(x))/h hstretch(x + y + z, arrow.r, over)",
};
// what a glyph is measured in, it is painted in: a leaf whose code points
// its font covers is as wide as their advances in that font (a leaf it
// does not cover is a stand-in)
bool painted(const MathBox* b, const MathFont* const* byId, const Interner& strs) {
  if (b->kind == MathKind::Glyph && b->font != kTextFont) {
    if (b->font > 1) return false;
    const MathFont& f = *byId[b->font];
    const std::string_view t = strs.get(b->text);
    double adv = 0;
    for (u32 i = 0; i < t.size();) {
      const GlyphRec* g = f.glyph(utf8Next(t, i));
      if (!g) return true;
      adv += g->adv;
    }
    const Su w = f.su(adv, b->px);
    return b->w >= w - 1 && b->w <= w + 1;
  }
  for (const MathKid& k : b->kids)
    if (!painted(k.box, byId, strs)) return false;
  return true;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  OwnedMathFont o;
  std::string err;
  if (!MathFontRegistry::decode(std::string_view((const char*)data, size), o, err)) {
    if (err.empty()) __builtin_trap();  // a refusal says why
    return 0;
  }
  const MathFont& f = o.font;
  // the invariants the lookups rest on
  for (int i = 0; i < f.glyphCount; i++)
    if (f.glyph(f.glyphs[i].cp) != &f.glyphs[i]) __builtin_trap();
  for (int i = 0; i < f.vertCount; i++)
    if (f.chain(f.vert[i].baseCp) != &f.vert[i]) __builtin_trap();
  for (int i = 0; i < f.horizCount; i++)
    if (f.hchain(f.horiz[i].baseCp) != &f.horiz[i]) __builtin_trap();
  o.font.id = 1;
  const MathFont& euler = MathFontRegistry::get().primary();
  const MathFont* const byId[2] = {&euler, &f};
  for (int order = 0; order < 2; order++) {
    MathEnv env;
    env.fonts = order == 0 ? std::vector<const MathFont*>{&f, &euler}
                           : std::vector<const MathFont*>{&euler, &f};
    const MathScope scope{&env, 0, 0};
    for (const char* src : kFormulas) {
      for (const bool display : {false, true}) {
        Arena arena;
        Interner strs{arena};
        DiagSink diags;
        MathBox* b = layoutMathFormula(src, display, 16.0, arena, strs, diags, Span{}, nullptr, true, &scope);
        if (!b || !painted(b, byId, strs)) __builtin_trap();
        (void)dumpMathBox(b, strs);
      }
    }
  }
  return 0;
}
