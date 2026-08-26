#!/usr/bin/env python3
# Compiles fonts/Euler-Math.otf -> engine/gen/euler_math.h (math-design.md §3).
# Same committed-artifact pattern as hyphc.mjs. Requires python3 + fontTools.
#
#   python3 tools/mathc.py [--font fonts/Euler-Math.otf] [--out engine/gen/euler_math.h]
#
# Emits, all in font design units (su conversion happens at use time):
#   - MathConstants (spec order) + upem + MinConnectorOverlap
#   - per-glyph records (cp, advance, ink asc/desc, italic corr, top accent)
#     for the shipped ranges plus everything variant chains / assemblies /
#     the operator dictionary reference
#   - vertical + horizontal variant chains (codepoints, not glyph ids)
#   - assemblies (part cp, start/end overlap, full advance, extender flag)
#   - the operator dictionary: name -> (cp, atom class, flags), curated here,
#     filtered against cmap coverage (dropped entries are listed)
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

# Codepoint ranges shipped by default (math-design.md §3); variant/assembly/
# dictionary references outside these are added individually.
RANGES = [
    (0x0021, 0x007E), (0x00A1, 0x00FF), (0x0131, 0x0131),
    (0x0300, 0x036F), (0x0370, 0x03FF), (0x2010, 0x2027),
    (0x2032, 0x2057), (0x20D0, 0x20FF), (0x2100, 0x214F),
    (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2300, 0x23FF),
    (0x25A0, 0x25FF), (0x27C0, 0x27FF), (0x2900, 0x2AFF),
    (0x1D400, 0x1D7FF),
]

# ---------------------------------------------------------------------------
# Operator dictionary (v2 §13): Typst-codex-flavoured names + v1 tier-1 ASCII
# sequences + AA..ZZ blackboard. Classes are TeX atom classes; flags mark
# large operators, stretchiness, display limits, multi-letter text operators
# and accents. Entries whose codepoint the font does not cover are dropped
# with a notice (fallback marking per v2 §13).
ORD, OP, BIN, REL, OPEN, CLOSE, PUNCT, INNER = range(8)
LARGE, STRETCHY, LIMITS, TEXTOP, ACCENT = 1, 2, 4, 8, 16

DICT = []
def d(name, cp, cls, flags=0): DICT.append((name, cp, cls, flags))

# Greek (Typst codex naming: epsilon=U+03B5, phi=U+03C6; TeX-style var* kept)
_greek = {
    "alpha": 0x3B1, "beta": 0x3B2, "gamma": 0x3B3, "delta": 0x3B4,
    "epsilon": 0x3B5, "varepsilon": 0x3F5, "zeta": 0x3B6, "eta": 0x3B7,
    "theta": 0x3B8, "vartheta": 0x3D1, "iota": 0x3B9, "kappa": 0x3BA,
    "lambda": 0x3BB, "mu": 0x3BC, "nu": 0x3BD, "xi": 0x3BE,
    "omicron": 0x3BF, "pi": 0x3C0, "varpi": 0x3D6, "rho": 0x3C1,
    "varrho": 0x3F1, "sigma": 0x3C3, "varsigma": 0x3C2, "tau": 0x3C4,
    "upsilon": 0x3C5, "phi": 0x3C6, "varphi": 0x3D5, "chi": 0x3C7,
    "psi": 0x3C8, "omega": 0x3C9,
    "Gamma": 0x393, "Delta": 0x394, "Theta": 0x398, "Lambda": 0x39B,
    "Xi": 0x39E, "Pi": 0x3A0, "Sigma": 0x3A3, "Upsilon": 0x3A5,
    "Phi": 0x3A6, "Psi": 0x3A8, "Omega": 0x3A9,
}
for n, c in _greek.items(): d(n, c, ORD)

# Big operators
for n, c, f in [
    ("sum", 0x2211, LIMITS), ("prod", 0x220F, LIMITS), ("coprod", 0x2210, LIMITS),
    ("int", 0x222B, 0), ("iint", 0x222C, 0), ("iiint", 0x222D, 0),
    ("oint", 0x222E, 0),
    ("bigcup", 0x22C3, LIMITS), ("bigcap", 0x22C2, LIMITS),
    ("bigvee", 0x22C1, LIMITS), ("bigwedge", 0x22C0, LIMITS),
    ("bigoplus", 0x2A01, LIMITS), ("bigotimes", 0x2A02, LIMITS),
    ("bigodot", 0x2A00, LIMITS), ("biguplus", 0x2A04, LIMITS),
    ("bigsqcup", 0x2A06, LIMITS),
]: d(n, c, OP, LARGE | f)

# Text operators (multi-letter, upright). cp = 0: rendered as letters.
for n in ["lim", "limsup", "liminf", "max", "min", "sup", "inf", "argmax",
          "argmin", "gcd", "det", "Pr"]:
    d(n, 0, OP, TEXTOP | LIMITS)
for n in ["sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos",
          "arctan", "sinh", "cosh", "tanh", "coth", "log", "ln", "lg", "exp",
          "deg", "dim", "ker", "hom", "arg", "mod"]:
    d(n, 0, OP, TEXTOP)
# NOTE: v1 listed `inf` as ∞; that collides with the infimum operator.
# ∞ is `oo` / `infty` / `infinity`; `inf` is the operator (recorded deviation).

# Binary operators (tier-1 sequences + names)
for n, c in [
    ("+-", 0xB1), ("-+", 0x2213), ("pm", 0xB1), ("mp", 0x2213),
    ("o+", 0x2295), ("o-", 0x2296), ("ox", 0x2297), ("o.", 0x2299),
    ("oplus", 0x2295), ("ominus", 0x2296), ("otimes", 0x2297),
    ("odot", 0x2299), ("oslash", 0x2298),
    ("xx", 0xD7), ("times", 0xD7), ("cdot", 0x22C5), ("div", 0xF7),
    ("circ", 0x2218), ("bullet", 0x2219), ("star", 0x22C6),
    ("wedge", 0x2227), ("vee", 0x2228), ("cap", 0x2229), ("cup", 0x222A),
    ("setminus", 0x2216), ("sqcap", 0x2293), ("sqcup", 0x2294),
    ("uplus", 0x228E), ("amalg", 0x2A3F), ("ast", 0x2217),
    ("*", 0x2217), ("+", 0x2B), ("-", 0x2212),
]: d(n, c, BIN)

# Relations (tier-1 sequences + names)
for n, c in [
    ("!=", 0x2260), (">=", 0x2265), ("<=", 0x2264), ("<<", 0x226A),
    (">>", 0x226B), (":=", 0x2254), ("=:", 0x2255), ("::=", 0x2A74),
    ("~=", 0x2245), ("-=", 0x2261), ("~~", 0x2248),
    ("=", 0x3D), ("<", 0x3C), (">", 0x3E), (":", 0x2236),
    ("neq", 0x2260), ("leq", 0x2264), ("geq", 0x2265), ("ll", 0x226A),
    ("gg", 0x226B), ("sim", 0x223C), ("~", 0x223C), ("simeq", 0x2243),
    ("approx", 0x2248), ("equiv", 0x2261), ("cong", 0x2245),
    ("propto", 0x221D), ("prop", 0x221D),
    ("prec", 0x227A), ("succ", 0x227B), ("preceq", 0x2AAF), ("succeq", 0x2AB0),
    ("subset", 0x2282), ("supset", 0x2283), ("subseteq", 0x2286),
    ("supseteq", 0x2287), ("sqsubseteq", 0x2291), ("sqsupseteq", 0x2292),
    ("in", 0x2208), ("ni", 0x220B), ("notin", 0x2209),
    ("parallel", 0x2225), ("perp", 0x22A5), ("mid", 0x2223),
    ("vdash", 0x22A2), ("dashv", 0x22A3), ("models", 0x22A8),
    ("|-", 0x22A2), ("|--", 0x22A2), ("|=", 0x22A8), ("|==", 0x22A8),
    ("--|", 0x22A3), ("_|_", 0x22A5),
    # negated relations (the ! pattern resolves through these entries)
    ("!in", 0x2209), ("!ni", 0x220C), ("!exists", 0x2204),
    ("!subset", 0x2284), ("!supset", 0x2285), ("!subseteq", 0x2288),
    ("!supseteq", 0x2289), ("!|", 0x2224), ("!||", 0x2226),
    ("!sim", 0x2241), ("!~", 0x2241), ("!approx", 0x2249), ("!~~", 0x2249),
    ("!equiv", 0x2262), ("!-=", 0x2262), ("!cong", 0x2247),
    ("!prec", 0x2280), ("!succ", 0x2281), ("!parallel", 0x2226),
    ("!leq", 0x2270), ("!<=", 0x2270), ("!geq", 0x2271), ("!>=", 0x2271),
    ("!<", 0x226E), ("!>", 0x226F),
]: d(n, c, REL)

# Arrows (relations; horizontally stretchy where the font has chains)
for n, c in [
    ("->", 0x2192), ("<-", 0x2190), ("<->", 0x2194), ("=>", 0x21D2),
    ("<=>", 0x21D4), ("==>", 0x27F9), ("<==", 0x27F8), ("-->", 0x27F6),
    ("<--", 0x27F5), ("|->", 0x21A6), ("|-->", 0x27FC), ("~>", 0x21DD),
    ("to", 0x2192), ("gets", 0x2190), ("mapsto", 0x21A6),
    ("uparrow", 0x2191), ("downarrow", 0x2193), ("updownarrow", 0x2195),
    ("Uparrow", 0x21D1), ("Downarrow", 0x21D3), ("Updownarrow", 0x21D5),
    ("nearrow", 0x2197), ("searrow", 0x2198), ("nwarrow", 0x2196),
    ("swarrow", 0x2199), ("hookrightarrow", 0x21AA), ("hookleftarrow", 0x21A9),
    ("rightharpoonup", 0x21C0), ("leftharpoonup", 0x21BC),
    ("rightleftharpoons", 0x21CC), ("leftrightarrows", 0x21C6),
    ("!->", 0x219B), ("!=>", 0x21CF),
]: d(n, c, REL, STRETCHY)

# Ordinary symbols
for n, c in [
    ("forall", 0x2200), ("exists", 0x2203), ("nexists", 0x2204),
    ("neg", 0xAC), ("not", 0xAC), ("top", 0x22A4), ("bot", 0x22A5),
    ("empty", 0x2205), ("emptyset", 0x2205), ("infty", 0x221E),
    ("infinity", 0x221E), ("oo", 0x221E),
    ("partial", 0x2202), ("nabla", 0x2207), ("grad", 0x2207),
    ("hbar", 0x210F), ("ell", 0x2113), ("Re", 0x211C), ("Im", 0x2111),
    ("aleph", 0x2135), ("wp", 0x2118), ("angle", 0x2220),
    ("triangle", 0x25B3), ("square", 0x25A1), ("degree", 0xB0),
    ("prime", 0x2032), ("'", 0x2032),
    ("...", 0x2026), ("ldots", 0x2026), ("cdots", 0x22EF),
    ("vdots", 0x22EE), ("ddots", 0x22F1),
    (":.", 0x2234), (":'", 0x2235), ("therefore", 0x2234), ("because", 0x2235),
]: d(n, c, ORD)

# Delimiters (stretchy; parser maps bracket chars directly, names for the rest)
for n, c, cls in [
    ("(", 0x28, OPEN), (")", 0x29, CLOSE), ("[", 0x5B, OPEN), ("]", 0x5D, CLOSE),
    ("{", 0x7B, OPEN), ("}", 0x7D, CLOSE),
    ("langle", 0x27E8, OPEN), ("rangle", 0x27E9, CLOSE),
    ("lceil", 0x2308, OPEN), ("rceil", 0x2309, CLOSE),
    ("lfloor", 0x230A, OPEN), ("rfloor", 0x230B, CLOSE),
    ("|", 0x7C, ORD), ("||", 0x2016, ORD),
]: d(n, c, cls, STRETCHY)

# Punctuation
for n, c in [(",", 0x2C), (";", 0x3B)]: d(n, c, PUNCT)

# Accents, call-style hat(x). SPACING forms, not combining marks: Firefox
# and Chromium disagree on isolated combining-mark placement (different
# shaping fallbacks), while spacing glyphs anchor at their origin in every
# browser. All are cmapped in Euler with TopAccentAttachment entries.
# `bar` renders as an Overbar* RULE (layout-side); `vec` has no spacing
# equivalent (U+20D7 combining only) — the one remaining shaping-dependent
# accent, recorded.
for n, c in [
    ("hat", 0x2C6), ("tilde", 0x2DC), ("bar", 0xAF), ("vec", 0x20D7),
    ("dot", 0x2D9), ("ddot", 0xA8), ("breve", 0x2D8), ("check", 0x2C7),
    ("ring", 0x2DA), ("acute", 0xB4), ("grave", 0x60),
]: d(n, c, ORD, ACCENT)

# Blackboard bold AA..ZZ (letterlike exceptions inline)
_bb_exc = {"C": 0x2102, "H": 0x210D, "N": 0x2115, "P": 0x2119, "Q": 0x211A,
           "R": 0x211D, "Z": 0x2124}
for i in range(26):
    L = chr(ord("A") + i)
    d(L + L, _bb_exc.get(L, 0x1D538 + i), ORD)

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

    # dictionary coverage filter
    kept, dropped = [], []
    for name, cp, cls, flags in DICT:
        if cp != 0 and cp not in cmap:
            dropped.append((name, cp))
            continue
        kept.append((name, cp, cls, flags))
    kept.sort(key=lambda e: e[0])
    names = [e[0] for e in kept]
    assert len(names) == len(set(names)), "duplicate dictionary names"

    # record set: ranges ∩ cmap, plus chain/assembly refs, plus dict cps
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
    for name, cp, cls, flags in kept:
        if cp: cps.add(cp)

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
    vrows, hrows = cp_chains(vert), cp_chains(horiz)

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
    o.append("  int16_t asmItalic;")
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
            out.append((base, off, len(vcps), aoff, len(parts), aital))
        return out
    vflat = flatten(vrows)
    hflat = flatten(hrows)
    o.append("inline constexpr uint32_t kVariantCps[] = {")
    o.append("  " + ",".join("0x%X" % c for c in var_cps) + ",")
    o.append("};")
    o.append("inline constexpr AsmPart kAsmParts[] = {")
    for cp, s, e, f, x in parts_flat:
        o.append("  {0x%X,%d,%d,%d,%d}," % (cp, s, e, f, x))
    o.append("};")
    for tag, flat in (("Vert", vflat), ("Horiz", hflat)):
        o.append("inline constexpr VarChain k%sChains[] = {  // sorted by baseCp" % tag)
        for base, off, n, aoff, an, aital in flat:
            o.append("  {0x%X,%d,%d,%d,%d,%d}," % (base, off, n, aoff, an, aital))
        o.append("};")
        o.append("inline constexpr int k%sChainCount = %d;" % (tag, len(flat)))
    o.append("")
    o.append("// Operator dictionary: TeX atom classes + behaviour flags.")
    o.append("enum : uint8_t { kOrd, kOp, kBin, kRel, kOpen, kClose, kPunct, kInner };")
    o.append("enum : uint8_t { kFlagLarge = 1, kFlagStretchy = 2, kFlagLimits = 4,")
    o.append("                 kFlagTextOp = 8, kFlagAccent = 16 };")
    o.append("struct OpEntry { const char* name; uint32_t cp; uint8_t cls, flags; };")
    o.append("inline constexpr OpEntry kOps[] = {  // sorted by name (strcmp)")
    for name, cp, cls, flags in kept:
        esc = name.replace("\\", "\\\\").replace('"', '\\"')
        o.append('  {"%s",0x%X,%d,%d},' % (esc, cp, cls, flags))
    o.append("};")
    o.append("inline constexpr int kOpCount = %d;" % len(kept))
    o.append("")
    o.append("}}  // namespace tsr::mathfont")
    out = "\n".join(o) + "\n"
    with open(args.out, "w") as f:
        f.write(out)

    print("wrote %s: %d constants, %d glyph records, %d vert + %d horiz chains,"
          % (args.out, len(constants), len(recs), len(vflat), len(hflat)),
          "%d asm parts, %d dict entries (%d dropped)"
          % (len(parts_flat), len(kept), len(dropped)))
    if dropped:
        print("dropped (not in font):",
              " ".join("%s(U+%04X)" % (n, c) for n, c in dropped))

if __name__ == "__main__":
    main()
