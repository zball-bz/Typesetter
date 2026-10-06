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
    ap.add_argument("--woff2", default="fonts/euler-math.woff2")
    ap.add_argument("--family", default="Euler Math")
    ap.add_argument("--manifest", default="runtime/src/shared/mathfont.gen.mjs")
    # (plan P5-01; D-M06) a host's math font: its metrics as a .tsmf blob (the
    # engine loads it as the declared input mathFonts), named for math.fonts;
    # --out '' and --manifest '' skip the embedded header and the manifest
    ap.add_argument("--tsmf", default="")
    ap.add_argument("--name", default="")
    # (plan P5-01) a font whose size variants are unencoded (STIX Two,
    # Libertinus): keep only what paints by code point — the reachable
    # variants, the assemblies whose parts all are — instead of failing
    ap.add_argument("--lenient", action="store_true")
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
    if missing and args.lenient:
        def keep(table):
            for base in list(table):
                variants, asm = table[base]
                variants = [(g, a) for g, a in variants if g in rev]
                if asm and any(g not in rev for g, *_ in asm[0]): asm = None
                table[base] = (variants, asm)
        keep(vert)
        keep(horiz)
        print("lenient: dropped %d unencoded variant/assembly glyphs" % len(missing))
    elif missing:
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
    hrows = cp_chains(horiz)  # (plan P3-29) wide accents, braces, arrows

    # ------------------------------------------------------------------ woff2
    # the paint-side subset (plan P1-23): exactly the record set, plus U+0020
    # (degraded formulas paint their source with white-space: pre), and its
    # content hash, which the header and the font manifest both carry
    from fontTools import subset
    opts = subset.Options()
    opts.flavor = "woff2"
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    sub = TTFont(args.font, recalcTimestamp=False)
    sb = subset.Subsetter(opts)
    sb.populate(unicodes=sorted(cps | {0x20}))
    sb.subset(sub)
    sub.flavor = "woff2"
    # reproducible bytes (plan P3-29): the subsetter stamps head.modified
    # with the time of the run; the source font's stamp keeps the woff2 — and
    # its content hash, the blog's URL — a function of the inputs
    sub["head"].modified = font["head"].modified
    sub.save(args.woff2)
    blob = open(args.woff2, "rb").read()
    h = 0xCBF29CE484222325
    for byte in blob:
        h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    woff_cps = set(TTFont(args.woff2).getBestCmap())
    if not (cps <= woff_cps and 0x20 in woff_cps):
        print("FATAL: the woff2 subset misses record code points or U+0020"); sys.exit(1)

    # ------------------------------------------------------------------ tsmf
    # (plan P5-01) the blob form, little-endian (engine/src/math/font.cc
    # reads it): "TSMF", version, total length, upem, hhea ascender and
    # descender, MinConnectorOverlap, the woff2's content hash, name, family,
    # the constants (spec order), glyph records (by cp), vertical and
    # horizontal chains (by base cp), variant code points, assembly parts
    var_cps, parts_flat = [], []
    def flatten(rows):
        out = []
        for base, vcps, parts, aital in rows:
            off = len(var_cps); var_cps.extend(vcps)
            aoff = len(parts_flat); parts_flat.extend(parts)
            out.append((base, off, len(vcps), aoff, len(parts)))
        return out
    vflat = flatten(vrows)
    hflat = flatten(hrows)
    hhea = font["hhea"]
    if args.tsmf:
        import struct
        name = (args.name or args.family.lower().replace(" ", "-")).encode()
        fam = args.family.encode()
        b = bytearray()
        b += struct.pack("<IiiiQ", upem, hhea.ascender, -hhea.descender, mv.MinConnectorOverlap, h)
        b += struct.pack("<H", len(name)) + name + struct.pack("<H", len(fam)) + fam
        b += struct.pack("<I", len(constants)) + b"".join(struct.pack("<h", v) for _, v in constants)
        b += struct.pack("<I", len(recs))
        for cp, adv, asc, desc, it, ta in recs:
            b += struct.pack("<IHhhhh", cp, adv, asc, desc, it, ta)
        for flat in (vflat, hflat):
            b += struct.pack("<I", len(flat))
            for base, off, n, aoff, an in flat:
                b += struct.pack("<IHHHH", base, off, n, aoff, an)
        b += struct.pack("<I", len(var_cps)) + b"".join(struct.pack("<I", c) for c in var_cps)
        b += struct.pack("<I", len(parts_flat))
        for cp, st, en, f, x in parts_flat:
            b += struct.pack("<IHHHB", cp, st, en, f, x)
        blob = b"TSMF" + struct.pack("<II", 1, 12 + len(b)) + bytes(b)
        with open(args.tsmf, "wb") as f:
            f.write(blob)
        print("wrote %s (%d bytes): %d glyph records, %d/%d chains" % (args.tsmf, len(blob), len(recs), len(vflat), len(hflat)))
    if not args.out:
        return

    # ------------------------------------------------------------------ emit
    o = []
    o.append("// GENERATED by tools/mathc.py from %s. Do not edit." % args.font)
    o.append("// All linear metrics are FONT DESIGN UNITS (kUpem per em);")
    o.append("// convert with: su = units * sizePx * 64 / kUpem.")
    o.append("#pragma once")
    o.append("#include <cstdint>")
    o.append("namespace tsr { namespace mathfont {")
    o.append("")
    o.append("inline constexpr int kUpem = %d;" % upem)
    o.append("// hhea line metrics: browser glyph-span baseline sits kAscender")
    o.append("// below the span top when line-height == kAscender+kDescender.")
    o.append("inline constexpr int kAscender = %d;" % hhea.ascender)
    o.append("inline constexpr int kDescender = %d;" % -hhea.descender)
    o.append("inline constexpr int kMinConnectorOverlap = %d;" % mv.MinConnectorOverlap)
    o.append("inline constexpr int16_t kNoTopAccent = -32768;")
    o.append("// the paint side: the family and the woff2 it paints with (%s)" % args.woff2)
    o.append("inline constexpr const char* kFamily = \"%s\";" % args.family)
    o.append("inline constexpr uint64_t kContentHash = 0x%016Xull;" % h)
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
    o.append("// (plan P3-29) horizontal constructions: a variant's or part's advance is its width")
    o.append("inline constexpr VarChain kHorizChains[] = {  // sorted by baseCp")
    for base, off, n, aoff, an in hflat:
        o.append("  {0x%X,%d,%d,%d,%d}," % (base, off, n, aoff, an))
    o.append("};")
    o.append("inline constexpr int kHorizChainCount = %d;" % len(hflat))
    o.append("")
    o.append("}}  // namespace tsr::mathfont")
    out = "\n".join(o) + "\n"
    with open(args.out, "w") as f:
        f.write(out)

    if not args.manifest:
        return
    with open(args.manifest, "w") as f:
        f.write("// GENERATED by tools/mathc.py from %s. Do not edit.\n" % args.font)
        f.write("// The math font manifest (plan P1-23): paint (shell.mjs installs it as a\n")
        f.write("// declared webfont with role 'math'), static export and packaging read it.\n")
        f.write("export const MATH_FONT = { family: '%s', file: '%s', hash: '%016x', role: 'math' };\n"
                % (args.family, args.woff2, h))
    print("wrote %s: %d constants, %d glyph records, %d vertical and %d horizontal chains, %d asm parts"
          % (args.out, len(constants), len(recs), len(vflat), len(hflat), len(parts_flat)))

if __name__ == "__main__":
    main()
