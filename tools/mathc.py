#!/usr/bin/env python3
# Compiles fonts/Euler-Math.otf -> engine/gen/euler_math.h (math-design.md §3).
# Same committed-artifact pattern as hyphc.mjs. Requires python3 + fontTools.
#
#   python3 tools/mathc.py [--font fonts/Euler-Math.otf] [--out engine/gen/euler_math.h]
#
# Emits, all in font design units (su conversion happens at use time):
#   - MathConstants (spec order) + upem + MinConnectorOverlap
#   - per-glyph records (cp, advance, ink asc/desc, italic corr, top accent)
#     for the shipped ranges plus everything vertical variant chains and
#     assemblies reference
#   - vertical variant chains (codepoints, not glyph ids) and assemblies
#     (part cp, start/end overlap, full advance, extender flag); horizontal
#     chains and assembly italic corrections are not compiled (nothing reads
#     them; horizontal stretch is a recorded deferral)
#
# GATING CHECK (math-design.md §1): every glyph referenced from a variant
# chain or assembly MUST be reachable through cmap — the HTML renderer paints
# by codepoint. The build fails otherwise.
import argparse, sys
from fontTools.ttLib import TTFont
from fontTools.pens.boundsPen import BoundsPen

# ---------------------------------------------------------------------------
# OpenType MATH constants, spec order. Plain-int fields marked with 'i'.
CONSTANTS = [
    ("ScriptPercentScaleDown", "i"),
    ("ScriptScriptPercentScaleDown", "i"),
    ("DelimitedSubFormulaMinHeight", "i"),
    ("DisplayOperatorMinHeight", "i"),
    ("MathLeading", "v"),
    ("AxisHeight", "v"),
    ("AccentBaseHeight", "v"),
    ("FlattenedAccentBaseHeight", "v"),
    ("SubscriptShiftDown", "v"),
    ("SubscriptTopMax", "v"),
    ("SubscriptBaselineDropMin", "v"),
    ("SuperscriptShiftUp", "v"),
    ("SuperscriptShiftUpCramped", "v"),
    ("SuperscriptBottomMin", "v"),
    ("SuperscriptBaselineDropMax", "v"),
    ("SubSuperscriptGapMin", "v"),
    ("SuperscriptBottomMaxWithSubscript", "v"),
    ("SpaceAfterScript", "v"),
    ("UpperLimitGapMin", "v"),
    ("UpperLimitBaselineRiseMin", "v"),
    ("LowerLimitGapMin", "v"),
    ("LowerLimitBaselineDropMin", "v"),
    ("StackTopShiftUp", "v"),
    ("StackTopDisplayStyleShiftUp", "v"),
    ("StackBottomShiftDown", "v"),
    ("StackBottomDisplayStyleShiftDown", "v"),
    ("StackGapMin", "v"),
    ("StackDisplayStyleGapMin", "v"),
    ("StretchStackTopShiftUp", "v"),
    ("StretchStackBottomShiftDown", "v"),
    ("StretchStackGapAboveMin", "v"),
    ("StretchStackGapBelowMin", "v"),
    ("FractionNumeratorShiftUp", "v"),
    ("FractionNumeratorDisplayStyleShiftUp", "v"),
    ("FractionDenominatorShiftDown", "v"),
    ("FractionDenominatorDisplayStyleShiftDown", "v"),
    ("FractionNumeratorGapMin", "v"),
    ("FractionNumDisplayStyleGapMin", "v"),
    ("FractionRuleThickness", "v"),
    ("FractionDenominatorGapMin", "v"),
    ("FractionDenomDisplayStyleGapMin", "v"),
    ("SkewedFractionHorizontalGap", "v"),
    ("SkewedFractionVerticalGap", "v"),
    ("OverbarVerticalGap", "v"),
    ("OverbarRuleThickness", "v"),
    ("OverbarExtraAscender", "v"),
    ("UnderbarVerticalGap", "v"),
    ("UnderbarRuleThickness", "v"),
    ("UnderbarExtraDescender", "v"),
    ("RadicalVerticalGap", "v"),
    ("RadicalDisplayStyleVerticalGap", "v"),
    ("RadicalRuleThickness", "v"),
    ("RadicalExtraAscender", "v"),
    ("RadicalKernBeforeDegree", "v"),
    ("RadicalKernAfterDegree", "v"),
    ("RadicalDegreeBottomRaisePercent", "i"),
]

# Codepoint ranges shipped by default (math-design.md §3); variant and
# assembly references outside these are added individually. The vocabulary
# is no longer the font compiler's (plan P1-22: engine/data/math/symbols.tsv,
# tools/mathdict.py); 0x02B0-0x02FF (spacing modifier letters: the accents)
# replaces the dictionary code points it used to add, and engine/test checks
# the record set stays a superset of engine/data/math/glyph-cps.baseline.txt.
RANGES = [
    (0x0021, 0x007E), (0x00A1, 0x00FF), (0x0131, 0x0131), (0x02B0, 0x02FF),
    (0x0300, 0x036F), (0x0370, 0x03FF), (0x2010, 0x2027),
    (0x2032, 0x2057), (0x20D0, 0x20FF), (0x2100, 0x214F),
    (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2300, 0x23FF),
    (0x25A0, 0x25FF), (0x27C0, 0x27FF), (0x2900, 0x2AFF),
    (0x1D400, 0x1D7FF),
]

# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="fonts/Euler-Math.otf")
    ap.add_argument("--out", default="engine/gen/euler_math.h")
    args = ap.parse_args()

    font = TTFont(args.font)
    upem = font["head"].unitsPerEm
    math = font["MATH"].table
    mc = math.MathConstants
    cmap = font.getBestCmap()
    rev = {}
    for cp, gname in sorted(cmap.items()):
        rev.setdefault(gname, cp)  # smallest cp wins
    hmtx = font["hmtx"]
    gset = font.getGlyphSet()

    def val(field, kind):
        v = getattr(mc, field, None)
        if v is None: return 0
        return int(v) if kind == "i" else int(v.Value)

    constants = [(name, val(name, kind)) for name, kind in CONSTANTS]

    # italic corrections / top accents by glyph name
    gi = math.MathGlyphInfo
    italics = {}
    if gi.MathItalicsCorrectionInfo:
        t = gi.MathItalicsCorrectionInfo
        for g, r in zip(t.Coverage.glyphs, t.ItalicsCorrection):
            italics[g] = int(r.Value)
    topacc = {}
    if gi.MathTopAccentAttachment:
        t = gi.MathTopAccentAttachment
        for g, r in zip(t.TopAccentCoverage.glyphs, t.TopAccentAttachment):
            topacc[g] = int(r.Value)

    # variants + assemblies (glyph-name space first)
    mv = math.MathVariants
    def chains(cov, cons):
        out = {}
        for base, con in zip(cov.glyphs if cov else [], cons or []):
            variants = [(r.VariantGlyph, int(r.AdvanceMeasurement))
                        for r in con.MathGlyphVariantRecord]
            asm = None
            if con.GlyphAssembly:
                a = con.GlyphAssembly
                asm = ([(p.glyph, int(p.StartConnectorLength),
                         int(p.EndConnectorLength), int(p.FullAdvance),
                         int(p.PartFlags) & 1) for p in a.PartRecords],
                       int(a.ItalicsCorrection.Value) if a.ItalicsCorrection else 0)
            out[base] = (variants, asm)
        return out
    vert = chains(mv.VertGlyphCoverage, mv.VertGlyphConstruction)
    horiz = chains(mv.HorizGlyphCoverage, mv.HorizGlyphConstruction)

    # GATING CHECK: every referenced glyph is cmap-reachable
    missing = set()
    for table in (vert, horiz):
        for base, (variants, asm) in table.items():
            for g, _ in variants:
                if g not in rev: missing.add(g)
            if asm:
                for g, *_ in asm[0]:
                    if g not in rev: missing.add(g)
    if missing:
        print("FATAL: variant/assembly glyphs not cmap-reachable:", sorted(missing))
        sys.exit(1)

    # record set: ranges ∩ cmap, plus every variant/assembly reference (the
    # horizontal ones too: their glyphs stay paintable as plain glyphs)
    cps = set()
    for lo, hi in RANGES:
        for cp in range(lo, hi + 1):
            if cp in cmap: cps.add(cp)
    for table in (vert, horiz):
        for base, (variants, asm) in table.items():
            if base in rev: cps.add(rev[base])
            for g, _ in variants: cps.add(rev[g])
            if asm:
                for g, *_ in asm[0]: cps.add(rev[g])

    def ink(gname):
        pen = BoundsPen(gset)
        gset[gname].draw(pen)
        if pen.bounds is None: return (0, 0)
        _, ymin, _, ymax = pen.bounds
        return (int(round(ymax)), int(round(-ymin)))  # asc, desc

    NO_TA = -32768
    recs = []
    for cp in sorted(cps):
        g = cmap[cp]
        adv = int(hmtx[g][0])
        asc, desc = ink(g)
        recs.append((cp, adv, asc, desc, italics.get(g, 0),
                     topacc.get(g, NO_TA)))

    # chains in cp space, only for bases that made the record set
    def cp_chains(table):
        rows = []
        for base, (variants, asm) in sorted(table.items(),
                                            key=lambda kv: rev.get(kv[0], 1 << 30)):
            if base not in rev or rev[base] not in cps: continue
            vcps = [rev[g] for g, _ in variants]
            parts = []
            aital = 0
            if asm:
                parts = [(rev[g], s, e, f, x) for g, s, e, f, x in asm[0]]
                aital = asm[1]
            rows.append((rev[base], vcps, parts, aital))
        return rows
    vrows = cp_chains(vert)

    # ------------------------------------------------------------------ emit
    o = []
    o.append("// GENERATED by tools/mathc.py from %s. Do not edit." % args.font)
    o.append("// All linear metrics are FONT DESIGN UNITS (kUpem per em);")
    o.append("// convert with: su = units * sizePx * 64 / kUpem.")
    o.append("#pragma once")
    o.append("#include <cstdint>")
    o.append("namespace tsr { namespace mathfont {")
    o.append("")
    hhea = font["hhea"]
    o.append("inline constexpr int kUpem = %d;" % upem)
    o.append("// hhea line metrics: browser glyph-span baseline sits kAscender")
    o.append("// below the span top when line-height == kAscender+kDescender.")
    o.append("inline constexpr int kAscender = %d;" % hhea.ascender)
    o.append("inline constexpr int kDescender = %d;" % -hhea.descender)
    o.append("inline constexpr int kMinConnectorOverlap = %d;" % mv.MinConnectorOverlap)
    o.append("inline constexpr int16_t kNoTopAccent = -32768;")
    o.append("")
    o.append("// OpenType MATH constants, spec order.")
    o.append("enum class C : uint8_t {")
    for name, _ in constants:
        o.append("  %s," % name)
    o.append("};")
    o.append("inline constexpr int16_t kConstants[] = {")
    o.append("  " + ",".join(str(v) for _, v in constants) + ",")
    o.append("};")
    o.append("")
    o.append("struct GlyphRec {")
    o.append("  uint32_t cp;")
    o.append("  uint16_t adv;        // advance width")
    o.append("  int16_t asc, desc;   // ink extents above/below baseline")
    o.append("  int16_t italic;      // italic correction")
    o.append("  int16_t topAccent;   // top accent attachment x (kNoTopAccent = none)")
    o.append("};")
    o.append("inline constexpr GlyphRec kGlyphs[] = {  // sorted by cp")
    for cp, adv, asc, desc, it, ta in recs:
        o.append("  {0x%X,%d,%d,%d,%d,%d}," % (cp, adv, asc, desc, it, ta))
    o.append("};")
    o.append("inline constexpr int kGlyphCount = %d;" % len(recs))
    o.append("")
    o.append("struct VarChain {")
    o.append("  uint32_t baseCp;")
    o.append("  uint16_t off, n;         // into kVariantCps: growing size chain")
    o.append("  uint16_t asmOff, asmN;   // into kAsmParts (0 parts = no assembly)")
    o.append("};")
    o.append("struct AsmPart {")
    o.append("  uint32_t cp;")
    o.append("  uint16_t startOverlap, endOverlap, fullAdv;")
    o.append("  uint8_t isExtender;")
    o.append("};")
    var_cps, parts_flat = [], []
    def flatten(rows):
        out = []
        for base, vcps, parts, aital in rows:
            off = len(var_cps); var_cps.extend(vcps)
            aoff = len(parts_flat); parts_flat.extend(parts)
            out.append((base, off, len(vcps), aoff, len(parts)))
        return out
    vflat = flatten(vrows)
    o.append("inline constexpr uint32_t kVariantCps[] = {")
    o.append("  " + ",".join("0x%X" % c for c in var_cps) + ",")
    o.append("};")
    o.append("inline constexpr AsmPart kAsmParts[] = {")
    for cp, s, e, f, x in parts_flat:
        o.append("  {0x%X,%d,%d,%d,%d}," % (cp, s, e, f, x))
    o.append("};")
    o.append("inline constexpr VarChain kVertChains[] = {  // sorted by baseCp")
    for base, off, n, aoff, an in vflat:
        o.append("  {0x%X,%d,%d,%d,%d}," % (base, off, n, aoff, an))
    o.append("};")
    o.append("inline constexpr int kVertChainCount = %d;" % len(vflat))
    o.append("")
    o.append("}}  // namespace tsr::mathfont")
    out = "\n".join(o) + "\n"
    with open(args.out, "w") as f:
        f.write(out)

    print("wrote %s: %d constants, %d glyph records, %d vertical chains, %d asm parts"
          % (args.out, len(constants), len(recs), len(vflat), len(parts_flat)))

if __name__ == "__main__":
    main()
