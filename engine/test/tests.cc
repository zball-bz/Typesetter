// Native test runner: unit tests + golden tests over test/fixtures.
//   tsr_tests <repo Typesetter dir> [--update]
// Goldens live at test/golden/<area>/<name>.<stage>.txt.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <set>

#include "../src/api/doc.h"
#include "../src/layout/paginate.h"
#include "../src/support/hash128.h"
#include "../src/code/overlay.h"
#include "../src/code/tokens.h"
#include "../src/model/cascade.h"
#include "../src/break/items.h"
#include "../src/hyphen/hyphen.h"
#include "../src/inline/jslex.h"
#include "../src/math/math.h"
#include "../src/code/grid.h"
#include "../src/syntax/lexer.h"
#include "../src/syntax/exports.h"
#include "../src/semantic/terms.h"
#include "semantic_data.gen.h"
#include "../src/math/dict.h"
#include "../src/math/ir.h"
#include "../src/math/font.h"
#include "../src/api/driver.h"
#include "../src/render/html_writer.h"
#include "../src/code/native_tokens.h"
#include "../src/measure/mock.h"
#include "contract.h"

namespace fs = std::filesystem;
using namespace tsr;

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static bool readFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}
static void writeFile(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.write(s.data(), (std::streamsize)s.size());
}

// --- unit tests ---
static void unitJslex() {
  auto bal = [](std::string_view s) { return scanJs(s, 0, true); };
  CHECK(bal("(a,b)").ok && bal("(a,b)").end == 5);
  CHECK(bal("(a(b)c)").ok);
  CHECK(bal("({\"x\":\")\"})").ok);
  CHECK(bal("(`t ${f(1)} u`)").ok);
  CHECK(bal("(// )\n)").ok);
  CHECK(bal("(/* ) */)").ok);
  CHECK(!bal("(a").ok);
  JsScan toEol = scanJs(" x = (1 +\n 2)\nrest", 0, false);
  CHECK(toEol.ok && toEol.end == 13);  // newline inside parens continues
  JsScan semi = scanJs(" a = 1; tail", 0, false);
  CHECK(semi.ok && semi.hitSemicolon && semi.end == 7);
}

static void unitSpliceHead() {
  auto end = [](std::string s) {
    SpliceLex l;
    return lexSplice(s, 0, l) ? l.end : 0u;
  };
  CHECK(end("#avg(3,5)\xE7\x9A\x84") == 9);  // ASCII cut before 的
  CHECK(end("#x. next") == 2);                 // '.' not followed by ident start
  CHECK(end("#a.b.c(1).d") == 11);
  SpliceLex l;
  CHECK(lexSplice("#f(1)(2)[x]", 0, l) && l.end == 8 && l.lastCall == 5);
  CHECK(lexSplice("#f(1).g", 0, l) && l.lastCall == 0);
  CHECK(!lexSplice("#(1 + ", 0, l) && l.paren);
  // atoms and bracket bodies (plan P1-06)
  CodeSpanLex c;
  CHECK(lexCodeSpan("``a`b`` x", 0, c) && c.end == 7 && codeSpanText("a`b") == "a`b");
  CHECK(!lexCodeSpan("``a`", 0, c) && c.run == 2);
  CHECK(codeSpanText(" `x` ") == "`x`" && codeSpanText("  ") == "  ");
  CHECK(mathText("a \\$ b \\, c") == "a $ b \\, c");
  std::string body = "[code `a]b` here] tail";
  BracketMatcher bm(body);
  CHECK(bm.body(0) == 16);
  std::string price = "[price $5](u) and $x$";
  BracketMatcher pm(price);
  CHECK(pm.match(0, BracketMatcher::IslandAware) < 0 && pm.match(0, BracketMatcher::Plain) == 9);
  std::string deep(20000, '[');
  BracketMatcher dm(deep);
  for (u32 k = 0; k < deep.size(); k++) CHECK(dm.body(k) < 0);  // linear: one scan
}

static void unitSu() {
  CHECK(suCeilPx(1.0) == 64);
  CHECK(suCeilPx(1.001) == 65);
  CHECK(suFloorPx(1.999) == 127);
  CHECK(suToPx(96) == 1.5);
}

static void unitMock() {
  CHECK(mockWordWidthPx("The", 16) == 24.0);   // 3 * 0.5em * 16
  CHECK(mockWordWidthPx(" ", 16) == 4.0);
  CHECK(mockWordWidthPx("\xE4\xB8\xAD", 16) == 16.0);  // 中 = 1em
}

static void unitMathFont() {
  // the MathFont object (plan P1-23): Euler-Math is the registry's id 0
  const MathFont& F = MathFontRegistry::get().primary();
  CHECK(F.id == 0 && F.family == "Euler Math" && F.contentHash != 0 && MathFontRegistry::get().byId(0) == &F);
  // constants sanity against fontTools-inspected values (Euler-Math 0.75)
  CHECK(F.upem == 1000);
  CHECK(F.constant(C::AxisHeight) == 250);
  CHECK(F.constant(C::DisplayOperatorMinHeight) == 1130);
  CHECK(F.constant(C::ScriptPercentScaleDown) > 0 &&
        F.constant(C::ScriptPercentScaleDown) <= 100);
  CHECK(F.minConnectorOverlap == 20);
  // glyph lookup
  const GlyphRec* x = F.glyph('x');
  CHECK(x && x->adv > 0 && x->asc > 0);
  CHECK(F.glyph(0x2211) != nullptr);           // ∑
  CHECK(F.glyph(0x10FFFF) == nullptr);
  // the vocabulary (plan P1-22: MathDict, font-independent)
  const SymbolInfo* sum = MathDict::byName("sum");
  CHECK(sum && sum->cp == 0x2211 && sum->cls == kOp &&
        (sum->flags & kFlagLarge) && (sum->flags & kFlagLimits));
  const SymbolInfo* arrow = MathDict::byName("->");
  CHECK(arrow && arrow->cp == 0x2192 && arrow->cls == kRel);
  const SymbolInfo* nn = MathDict::byName("NN");
  CHECK(nn && nn->cp == 0x2115);
  const SymbolInfo* lim = MathDict::byName("lim");
  CHECK(lim && lim->cp == 0 && (lim->flags & kFlagTextOp) && (lim->flags & kFlagLimits));
  CHECK(MathDict::byName("nonexistent") == nullptr);
  u32 len = 0;
  const SymbolInfo* munch = MathDict::matchOp("|-->x", 0, len);  // the longest key
  CHECK(munch && len == 4 && munch->cp == 0x27FC);
  CHECK(MathDict::matchOp("+-", 0, len) && len == 2 && MathDict::matchOp("@", 0, len) == nullptr);
  CHECK(MathDict::classOfCp(0x2295) == kBin && MathDict::classOfCp(0x41) == kOrd);
  CHECK(MathDict::negate(0x3D) == 0x2260 && MathDict::negate(0x2208) == 0x2209);
  // every key lexes back to its own symbol (gate math_dict_lexes_every_key):
  // a word, an operator key through the trie, a !word, AA..ZZ
  for (int i = 0; i < mathdict::kSymbolCount; i++) {
    std::string_view k = mathdict::kSymbols[i].name;
    bool ops = true;
    for (char c : k) ops = ops && std::string_view("+-*=<>|~:;.,!@&?%").find(c) != std::string_view::npos;
    if (ops) CHECK(MathDict::matchOp(k, 0, len) == &mathdict::kSymbols[i] && len == k.size());
    else CHECK(MathDict::byName(k) == &mathdict::kSymbols[i]);
  }

  // variant chain: '(' has a growing chain plus a 3-part assembly
  const VarChain* paren = F.chain('(');
  CHECK(paren && paren->n >= 4 && paren->asmN == 3);
  CHECK(F.chain('x') == nullptr);
  // every chain/assembly cp has a glyph record (renderer paints by cp)
  for (int i = 0; i < F.vertCount; i++) {
    const VarChain& c = F.vert[i];
    for (int k = 0; k < c.n; k++) CHECK(F.glyph(F.variantCps[c.off + k]) != nullptr);
    for (int k = 0; k < c.asmN; k++) CHECK(F.glyph(F.parts[c.asmOff + k].cp) != nullptr);
  }
  // su conversion: 1em at 16px = 1024 su
  CHECK(F.su(1000, 16) == 1024);
  CHECK(F.su(250, 16) == 256);  // axis height = 4px
}

static std::string mathDump(const char* src) {
  Arena a;
  Interner strs{a};
  DiagSink d;
  MathBox* b = layoutMathFormula(src, false, 16, a, strs, d, {});
  std::string out = dumpMathBox(b, strs);
  for (const Diag& dg : d.items) { out += "DIAG "; out += dg.code; out += "\n"; }
  return out;
}

static void unitMathLayout() {
  std::string s = mathDump("x + y");
  CHECK(s.find("glyph \"x\" Ord") != std::string::npos);
  CHECK(s.find("glyph \"+\" Bin") != std::string::npos);
  CHECK(s.find("glue w=228") != std::string::npos);  // medium 4mu at 16px
  CHECK(s.find("DIAG") == std::string::npos);
  std::string neg = mathDump("-x");  // Rule 5: Bin at start demotes
  CHECK(neg.find("Bin") == std::string::npos && neg.find("glue") == std::string::npos);
  std::string rel = mathDump("x = y");
  CHECK(rel.find("glue w=284") != std::string::npos);  // thick 5mu at 16px
  std::string sup = mathDump("a^2");  // script size = ScriptPercentScaleDown
  CHECK(sup.find("px=11.2") != std::string::npos);
  std::string fr = mathDump("(n+1)/2");  // consumed group sheds its parens
  CHECK(fr.find("rule") != std::string::npos);
  CHECK(fr.find("glyph \"(\"") == std::string::npos);
  std::string grp = mathDump("(a+b) c");  // visible group splices Open/Close
  CHECK(grp.find("glyph \"(\" Open") != std::string::npos);
  std::string ar = mathDump("x -> y");
  CHECK(ar.find("glyph \"\xE2\x86\x92\" Rel") != std::string::npos);  // U+2192
  std::string nn = mathDump("NN");
  CHECK(nn.find("glyph \"\xE2\x84\x95\" Ord") != std::string::npos);  // U+2115
  std::string lim = mathDump("lim_n x");
  CHECK(lim.find("glyph \"lim\" Op") != std::string::npos);
  std::string sum = mathDump("sum_(i=0)^n i/n ~> (n+1)/2");
  CHECK(sum.find("glyph \"\xE2\x88\x91\" Op") != std::string::npos);  // U+2211
  CHECK(sum.find("glyph \"\xE2\x87\x9D\"") != std::string::npos);     // U+21DD
  CHECK(sum.find("DIAG") == std::string::npos);
  std::string ab = mathDump("x^ab");  // single-token script: x^a then b
  CHECK(ab.find("glyph \"b\" Ord") != std::string::npos);
}

static std::string mathDumpD(const char* src) {  // display style
  Arena a;
  Interner strs{a};
  DiagSink d;
  MathBox* b = layoutMathFormula(src, true, 16, a, strs, d, {});
  std::string out = dumpMathBox(b, strs);
  for (const Diag& dg : d.items) { out += "DIAG "; out += dg.code; out += "\n"; }
  return out;
}

static void unitMathStretch() {
  auto ascOf = [](const std::string& d) {
    return atoi(d.c_str() + d.find("asc=") + 4);
  };
  // display big operator: glyph grown + true limits -> taller than text style
  std::string sumD = mathDumpD("sum_(n=1)^oo x_n");
  std::string sumT = mathDump("sum_(n=1)^oo x_n");
  CHECK(sumD.find("DIAG") == std::string::npos);
  CHECK(ascOf(sumD) > ascOf(sumT));
  // radicals: no more math-unsupported; overbar rule present; natural surd
  std::string sq = mathDump("sqrt(x+1)");
  CHECK(sq.find("DIAG") == std::string::npos);
  CHECK(sq.find("rule") != std::string::npos);
  CHECK(sq.find("\xE2\x88\x9A") != std::string::npos);  // U+221A
  CHECK(mathDump("sqrt(a/b)").find("DIAG") == std::string::npos);
  // root: scriptscript degree raised on the surd
  std::string rt = mathDump("root(3, 2)");
  CHECK(rt.find("DIAG") == std::string::npos);
  CHECK(rt.find("glyph \"3\"") != std::string::npos);
  // accents: SPACING circumflex above a cramped base (browser-portable)
  std::string hat = mathDump("hat(x)");
  CHECK(hat.find("DIAG") == std::string::npos);
  CHECK(hat.find("\xCB\x86") != std::string::npos);  // U+02C6, spacing
  // bar is a Rule construct (Overbar* constants), never a glyph
  std::string bar = mathDump("bar(x)");
  CHECK(bar.find("DIAG") == std::string::npos);
  CHECK(bar.find("rule") != std::string::npos);
  CHECK(bar.find("\xCC\x84") == std::string::npos);   // no U+0304
  CHECK(bar.find("\xC2\xAF") == std::string::npos);   // no U+00AF either
  CHECK(mathDump("underline(x)").find("rule") != std::string::npos);
  // groups stay visible atoms; fenced tall content stretches without diags
  CHECK(mathDump("(a+b) c").find("glyph \"(\" Open") != std::string::npos);
  CHECK(mathDump("abs(a/b)").find("DIAG") == std::string::npos);
  // factorial binds postfix: the numerator of n!/2 is "n!"
  std::string fact = mathDump("n!/2");
  size_t ruleAt = fact.find("rule");
  CHECK(ruleAt != std::string::npos);
  CHECK(fact.find("glyph \"!\"") < ruleAt);   // '!' above the bar
  CHECK(fact.find("glyph \"n\"") < ruleAt);
  // binom: barless stack (no rule)
  std::string bi = mathDump("binom(n, k)");
  CHECK(bi.find("DIAG") == std::string::npos);
  CHECK(bi.find("rule") == std::string::npos);
  CHECK(bi.find("glyph \"n\"") != std::string::npos);
}

static void unitMathSegments() {
  Arena a;
  Interner strs{a};
  DiagSink d;
  // (plan P3-25) the class tables: after Rel 0.8, after Bin 0.95, before Rel 0.85
  MathBreaks br;
  br.after.fill(-1);
  br.before.fill(-1);
  br.after[kRel] = 0.8;
  br.after[kBin] = 0.95;
  br.before[kRel] = 0.85;
  auto segs = layoutMathSegments("a + b = c", false, 16, a, strs, d, {}, br);
  CHECK(segs.size() == 4);  // [a +][b][=][c]
  CHECK(segs[1].penalty == 0.95f);  // after a Bin
  CHECK(segs[2].penalty == 0.85f);  // before a Rel
  CHECK(segs[3].penalty == 0.8f);   // after a Rel
  CHECK(segs[2].glueBefore > 0 && segs[3].glueBefore > 0);  // thick glue
  auto neg = layoutMathSegments("-x", false, 16, a, strs, d, {}, br);
  CHECK(neg.size() == 1);  // unary minus demoted: no break
  auto disp = layoutMathSegments("a + b", true, 16, a, strs, d, {}, br);
  CHECK(disp.size() == 1);  // display formulas never segment
  auto opq = layoutMathSegments("(a = b)", false, 16, a, strs, d, {}, br);
  CHECK(opq.size() == 1);  // groups are opaque
  // (plan P3-25) a sum's atoms are the formula's: it breaks inside the sum
  // (finding math/bigop-greedy-body), and a scripted base keeps its edges
  auto sum = layoutMathSegments("sum_i a_i + b_i = x", false, 16, a, strs, d, {}, br);
  CHECK(sum.size() == 4);
  auto sq = layoutMathSegments("(a+b)^2 - c", false, 16, a, strs, d, {}, br);
  CHECK(sq.size() == 2);  // the minus after a scripted group is a Bin: a break after it
  MathBreaks none;
  none.after.fill(-1);
  none.before.fill(-1);
  CHECK(layoutMathSegments("a + b = c", false, 16, a, strs, d, {}, none).size() == 1);
}

static void unitGrid() {
  // strict 2:1 → exact 2/1, zero deltas (the common case costs nothing)
  GridSpec g = solveGrid(8.0, 16.0, 40);
  CHECK(g.exact && g.p == 2 && g.q == 1);
  CHECK(g.dLatinPx == 0 && g.dCjkPx == 0);
  // 5:3 pair → 5/3 within q≤7; thinner side gains positive spacing
  GridSpec h = solveGrid(9.0, 15.0, 40);
  CHECK(h.exact && h.p == 5 && h.q == 3);
  CHECK(h.dLatinPx == 0 && h.dCjkPx == 0);  // exactly rational: no deltas
  // slightly-thin CJK: best q≤7 approximation is still 2/1 and the CJK
  // side gains the (modest) spacing; natural flow would NOT be exact
  GridSpec k = solveGrid(8.0, 15.6, 20);
  CHECK(k.p == 2 && k.q == 1 && !k.exact);
  CHECK(k.dCjkPx > 0.3 && k.dCjkPx < 0.5 && k.dLatinPx == 0);
  // irrational-ish ratio, q capped at 7
  GridSpec m = solveGrid(8.0, 8.0 * 1.618, 200);
  CHECK(m.q <= 7);
}

static void unitImageSrc() {
  // figure-design.md §5: relative / http(s) / data:image only
  CHECK(safeImageSrc("photos/a.jpg"));
  CHECK(safeImageSrc("/abs/path.png"));
  CHECK(safeImageSrc("a/b:c.png"));  // ':' after '/' is not a scheme
  CHECK(safeImageSrc("https://x.example/i.png"));
  CHECK(safeImageSrc("HTTP://x.example/i.png"));
  CHECK(safeImageSrc("data:image/png;base64,AAAA"));
  CHECK(!safeImageSrc(""));
  CHECK(!safeImageSrc("javascript:alert(1)"));
  CHECK(!safeImageSrc("data:text/html,<script>"));
  CHECK(!safeImageSrc("file:///etc/passwd"));
  CHECK(!safeImageSrc("vbscript:x"));
}

// Fragment programs (plan P2-13): markup parsed at run time lowers to the
// same LowerProgram — every inline form, spans moved by the base or all
// clamped, bare value heads and interpolations as out-of-band holes, and
// JavaScript left as text.
static void unitFragment() {
  std::vector<FragmentText> texts{{"\xE5\x9D\x87\xE6\x91\x8A *O* $n log n$ @sec [doc](https://x) `code` #name.x "
                                   "#strong[s] #(__m0); #f(1)",
                                   5}};
  Lowered L = codegenFragments(texts, nullptr);
  LowerProgram p;
  std::string why;
  CHECK(readLowerProgram(L.program, p, why));
  const std::string dump = dumpLowerProgram(L.program);
  for (const char* k : {"CALL strong", "CALL math", "CALL mathsrc", "CALL ref", "CALL link", "CALL code", "HOLE 0", "HOLE 2"})
    CHECK(dump.find(k) != std::string::npos);
  CHECK(dump.find("TEXT \"#f(1)\"") != std::string::npos);  // JavaScript stays text
  CHECK(dump.find("[5,") != std::string::npos);              // spans moved by the base
  CHECK(L.js.find("{\"p\":\"name.x\"}") != std::string::npos);
  CHECK(L.js.find("{\"p\":\"strong\",\"k\":1}") != std::string::npos);
  CHECK(L.js.find("{\"v\":0}") != std::string::npos);
  CHECK(L.js.find("fragment-splice") != std::string::npos);
  // clamped: every span is the clamp; two texts are two blocks
  const Span clamp{40, 44};
  Lowered C = codegenFragments({{"a *b*", 0}, {"= H\n\npara", 0}}, &clamp);
  CHECK(readLowerProgram(C.program, p, why) && p.blocks.size() == 2);
  const std::string cd = dumpLowerProgram(C.program);
  CHECK(cd.find("[40,44)") != std::string::npos && cd.find("[0,") == std::string::npos);
  CHECK(cd.find("CALL heading") != std::string::npos);
  // the wire form round-trips; a malformed request is an error answer
  std::string req;
  auto u32le = [&](u32 v) {
    for (int i = 0; i < 4; i++) req += (char)((v >> (8 * i)) & 0xff);
  };
  u32le(1);
  req += (char)0;
  u32le(0);
  u32le(0);
  u32le(7);
  u32le(3);
  req += "*a*";
  FragmentRequest fr;
  CHECK(decodeFragmentRequest(req, fr) && fr.texts.size() == 1 && fr.texts[0].base == 7);
  CHECK(runFragmentRequest(req).find("\"holes\":[]") != std::string::npos);
  CHECK(runFragmentRequest(req.substr(0, 10)).find("\"error\"") != std::string::npos);
}

// The resource pull (plan P1-19): the wire codec round-trips every column
// type; an answer to another batch is refused whole; a missing or invalid
// row degrades only what consumed it, with one diagnostic per kind.
static void unitResources(const fs::path& root) {
  {
    WireBatch b;
    b.batch = 7;
    b.mks.push_back({b.str("Serif"), 0x123456789ull, 18, 700, 1, b.str("liga"), b.str("ja"), 2});
    WireKind& w = b.kinds.emplace_back();
    w.kind = (u16)ResKind::textWidth;
    w.rows.resize(2);
    w.rows[1].resId = 1;
    w.rows[1].col[1] = b.str("fox");
    WireKind& bx = b.kinds.emplace_back();
    bx.kind = (u16)ResKind::boxInfo;
    bx.rows.resize(1);
    bx.rows[0].col[0] = 0;
    bx.rows[0].col[1] = b.str("a.png");
    bx.rows[0].setF64(2, 320.5);
    std::string q, a, err;
    encodeWire(b, false, q);
    WireBatch d;
    CHECK(decodeWire((const u8*)q.data(), q.size(), false, d, err));
    CHECK(d.batch == 7 && d.mks.size() == 1 && d.mks[0].faceDigest == 0x123456789ull && d.mks[0].dppx == 2);
    CHECK(d.strings[d.mks[0].lang] == "ja" && d.kinds.size() == 2 && d.strings[d.kinds[0].rows[1].col[1]] == "fox");
    CHECK(d.kinds[1].rows[0].f64(2) == 320.5);
    WireBatch ans;
    ans.batch = 7;
    WireKind& t = ans.kinds.emplace_back();
    t.kind = (u16)ResKind::codeTokens;
    t.rows.resize(1);
    t.rows[0].list = {0, 3, 1};
    t.rows[0].status = 1;
    t.rows[0].msg = ans.str("no grammar");
    encodeWire(ans, true, a);
    CHECK(decodeWire((const u8*)a.data(), a.size(), true, d, err) && d.kinds[0].rows[0].list.size() == 3 &&
          d.strings[d.kinds[0].rows[0].msg] == "no grammar");
    CHECK(!decodeWire((const u8*)a.data(), a.size() - 1, true, d, err));  // truncated
    CHECK(!decodeWire((const u8*)q.data(), q.size(), true, d, err));      // a request is not an answer
  }
  std::string ops;
  readFile(root / "test" / "fixtures" / "figure" / "w-only.ops", ops);
  auto fresh = [&](Doc& doc) { return doc.ingest((const u8*)ops.data(), ops.size()); };
  auto has = [](const Doc& doc, std::string_view code) {
    for (const Diag& d : doc.diags.items)
      if (code == d.code) return true;
    return false;
  };
  {
    Doc doc;
    CHECK(fresh(doc) && doc.typeset() == Doc::Status::NeedMeasure && doc.rt.boxNeeds.size() == 1);
    std::string req, ans;
    doc.requests(req);
    WireBatch stale;
    stale.batch = doc.rt.batch.id + 1;
    encodeWire(stale, true, ans);
    CHECK(!doc.provide((const u8*)ans.data(), ans.size()) && has(doc, "provider-invalid"));
    CHECK(doc.rt.boxNeeds[0].st == ResState::Pending);  // refused whole: still pending
    // (plan P3-32) one batch: the image's size beside every width — one
    // width answered NaN, the rest and the image missing
    WireBatch q, a;
    std::string err;
    CHECK(decodeWire((const u8*)req.data(), req.size(), false, q, err));
    a.batch = q.batch;
    for (const WireKind& k : q.kinds)
      if (k.kind == (u16)ResKind::textWidth) {
        WireKind& w = a.kinds.emplace_back();
        w.kind = k.kind;
        w.rows.resize(1);
        w.rows[0].setF64(0, std::nan(""));
      }
    encodeWire(a, true, ans);
    CHECK(doc.provide((const u8*)ans.data(), ans.size()));
    CHECK(doc.rt.boxNeeds[0].st == ResState::Failed && has(doc, "provider-missing") && has(doc, "image-load"));
    CHECK(doc.typeset() == Doc::Status::Ok);  // degraded, never stuck
    CHECK(has(doc, "measure-failed") || has(doc, "provider-invalid"));
    CHECK(doc.render().find("tsr-imgph") != std::string::npos);  // the failed image: a placeholder
  }
  {
    // the Session (plan P1-21): a second document on a warm Session asks
    // its host for nothing it already answered — widths, vertical metrics,
    // the image's size aside (a Host-cached kind) — and breaks from the memo
    Session sess;
    ProviderSet p = mockProviders();
    Doc a, b;
    a.attach(&sess);
    b.attach(&sess);
    CHECK(fresh(a) && driveToCompletion(a, p) && sess.refs == 2);
    CHECK(fresh(b) && b.typeset() == Doc::Status::NeedMeasure);
    std::string req, err;
    b.requests(req);
    WireBatch q;
    CHECK(decodeWire((const u8*)req.data(), req.size(), false, q, err));
    for (const WireKind& k : q.kinds) CHECK(k.kind == (u16)ResKind::boxInfo);
    const size_t memoBytes = sess.breakMemo.bytes();
    CHECK(driveToCompletion(b, p) && b.render() == a.render() && sess.breakMemo.bytes() == memoBytes);
    CHECK(sess.stats.widthHits > 0);
    // an answerer the host disables leaves the kind to the host
    Session own;
    CHECK(own.configure(R"({"answerers":{"codeTokens.tsm":false}})"));
    std::vector<CodeToken> toks;
    CHECK(!own.answerTokens("tsm", "= a", toks) && sess.answerTokens("tsm", "= a", toks) && !toks.empty());
  }
  {
    // (plan P3-32; design T9 M12) no block waits to emit for an image: its
    // size is Layout's (a figure's) or Measure's (an inline image's), asked
    // for in the first round beside every width
    Doc doc;
    CHECK(fresh(doc) && doc.typeset() == Doc::Status::NeedMeasure);
    CHECK(doc.emitted.size() == 2 && doc.emitted[0] && doc.emitted[1]);
    std::string req, err;
    doc.requests(req);
    WireBatch q;
    CHECK(decodeWire((const u8*)req.data(), req.size(), false, q, err));
    bool box = false, width = false;
    for (const WireKind& k : q.kinds) {
      box = box || k.kind == (u16)ResKind::boxInfo;
      width = width || k.kind == (u16)ResKind::textWidth;
    }
    CHECK(box && width);
  }
  {
    // the author's lone w survives the host's size (defect #24), and the
    // tree keeps the author's args (finding image-dims-in-author-args)
    Doc doc;
    CHECK(fresh(doc));
    ProviderSet p = mockProviders();
    CHECK(driveToCompletion(doc, p));
    for (const ParaFrame& fr : doc.layout.paras)
      for (const Fragment& l : fr.lines)
        if (l.kind == FragKind::Image) CHECK(suToPx(l.width) == 120 && suToPx(l.height) == 90);
    CHECK(doc.product("tree").find("h=") == std::string::npos);
  }
}


// The math IR (plan P1-24): every built-in row passes the one validator;
// calls bind only on an adjacent `name(`, a bare accent or function word is
// a symbol or a name, arity is checked, and errors stay local.
// (plan P3-24; design T8 MathDict test gates) the lexer gate: every key of
// the dictionary parses back to its own symbol — a name, an operator key, a
// delimiter in a symbol slot — and every TeX target the converters emit
// (tools/convert, kTexNames) to the symbol it means
// (plan P3-28; design T6 S14, T9 M11) host boxes: an svg its attributes
// size is the engine's own answer; a box the host measures is asked for at
// its width — one need per width, a provisional layout never painted, at
// most two layout rounds — and a failure keeps its declared size
static void unitHostBoxes(const fs::path& root) {
  double h = 0, b = 0;
  CHECK(svgBoxPx("<svg viewBox='0 0 400 100'></svg>", 300, h, b) && h == 75 && b == 75);
  CHECK(svgBoxPx(" <SVG width=\"50%\" viewBox=\"0,0,4,3\">", 300, h, b) && h == 112.5);
  CHECK(svgBoxPx("<svg height=40px viewBox='0 0 1 1'>", 300, h, b) && h == 40);
  CHECK(!svgBoxPx("<svg width='10em' viewBox='0 0 4 3'>", 300, h, b));
  CHECK(!svgBoxPx("<svg>", 300, h, b) && !svgBoxPx("<svgx viewBox='0 0 1 1'>", 300, h, b) &&
        !svgBoxPx("<div><svg viewBox='0 0 1 1'></svg></div>", 300, h, b) &&
        !svgBoxPx("<svg viewBox='0 0 0 1'>", 300, h, b));
  CHECK(boxKindOf("<svg/>") == BoxKind::Svg && boxKindOf("<div></div>") == BoxKind::Html &&
        boxKindOf("<svgx>") == BoxKind::Html);

  std::string ops;
  readFile(root / "test" / "fixtures" / "region" / "raw-host-diag.ops", ops);
  auto fresh = [&](Doc& doc) { return doc.ingest((const u8*)ops.data(), ops.size()); };
  auto has = [](const Doc& doc, std::string_view code) {
    for (const Diag& d : doc.diags.items)
      if (code == d.code) return true;
    return false;
  };
  // the host's box rows, by kind and width
  std::vector<std::pair<BoxKind, double>> asked;
  ProviderSet p = mockProviders();
  auto mock = p.boxes;
  p.boxes = [&](BoxKind k, std::string_view ref, double w, double& bw, double& bh, double& bb) {
    asked.push_back({k, w});
    return mock(k, ref, w, bw, bh, bb);
  };
  // the first raw block's (the html box's) height
  auto htmlBoxPx = [](const Doc& doc) { return suToPx(doc.layout.paras[1].lines[0].height); };
  {
    Doc doc;
    CHECK(fresh(doc) && doc.configure(R"({"host":{"width":300}})") == 0);
    bool painted = false;
    u32 rounds = 0;
    for (; rounds < 16 && doc.typeset() != Doc::Status::Ok; rounds++) {
      painted = painted || !doc.render().empty();  // a provisional layout is never painted
      CHECK(answerRound(doc, p));
    }
    CHECK(!painted && doc.typeset() == Doc::Status::Ok);
    // the inline box at its declared width, the html block and the svg the
    // engine cannot size at the measure; the two viewBox svgs never asked
    CHECK(asked.size() == 3);
    u32 at60 = 0, at300 = 0;
    for (auto& [k, w] : asked) (w == 60 ? at60 : w == 300 ? at300 : at60 += 100)++;
    CHECK(at60 == 1 && at300 == 2);
    CHECK(htmlBoxPx(doc) == 60 && suToPx(doc.layout.paras[2].lines[0].height) == 75);
    // a new width: only the width-dependent boxes, at it, in place
    asked.clear();
    doc.setWidth(900);
    CHECK(doc.typeset() == Doc::Status::NeedMeasure && doc.done(Stage::Measure));
    CHECK(driveToCompletion(doc, p) && asked.size() == 2 && asked[0].second == 900 && asked[1].second == 900);
    CHECK(htmlBoxPx(doc) == 20 && suToPx(doc.layout.paras[2].lines[0].height) == 225);
    // a fork at the same width asks for no box again
    asked.clear();
    Doc f;
    CHECK(doc.forkInto(f, "{}") && driveToCompletion(f, p) && asked.empty() && htmlBoxPx(f) == 20);
  }
  {
    // past the layout's asks a box keeps its declared height (box-unsettled)
    Doc doc;
    CHECK(fresh(doc));
    for (u32 r = 0; r < 16; r++) {
      if (doc.typeset() == Doc::Status::Ok) break;
      if (doc.done(Stage::Measure)) {  // the layout asked: its boxes left unanswered, its last round
        doc.layoutAsks = Doc::kLayoutAsks;
        continue;
      }
      CHECK(answerRound(doc, p));
    }
    CHECK(doc.done(Stage::Layout) && has(doc, "box-unsettled") && htmlBoxPx(doc) == 30);
  }
  {
    // a host that cannot measure: box-measure, the declared size
    ProviderSet none = mockProviders();
    none.boxes = [&](BoxKind k, std::string_view ref, double w, double& bw, double& bh, double& bb) {
      return k == BoxKind::Image && mock(k, ref, w, bw, bh, bb);
    };
    Doc doc;
    CHECK(fresh(doc) && driveToCompletion(doc, none) && has(doc, "box-measure") && htmlBoxPx(doc) == 30);
  }
}

static void unitMathDict() {
  Arena arena;
  auto means = [&](std::string_view src, const SymbolInfo& e) {
    const bool delim = e.cls == kOpen || e.cls == kClose;
    const std::string text = delim ? "lr(" + std::string(src) + ", x, " + std::string(src) + ")" : std::string(src);
    MathIR m = parseMath(text, arena);
    if (!m.root || m.root->kids.size() != 1) return false;
    const MNode* n = m.root->kids[0];
    if (delim) return n->k == MNode::Call && !n->kids.empty() && n->kids[0]->cp == e.cp;
    if (!e.cp) return n->k == MNode::Text && n->txt == src;  // a text operator
    return n->k == MNode::Sym && n->cp == e.cp;
  };
  for (int i = 0; i < mathdict::kSymbolCount; i++) {
    const SymbolInfo& e = mathdict::kSymbols[i];
    const bool ok = means(e.name, e);
    if (!ok) printf("FAIL math key '%s' does not parse to its symbol\n", e.name);
    CHECK(ok);
  }
  for (int i = 0; i < mathdict::kTexNameCount; i++) {
    const mathdict::TexName& t = mathdict::kTexNames[i];
    const std::string_view target = t.target;
    bool ok = false;
    if (target[0] == '!') {  // a negation: !base is the base's negation
      const SymbolInfo* b = MathDict::byName(target.substr(1));
      MathIR m = parseMath(target, arena);
      ok = b && m.diags.empty() && m.root->kids.size() == 1 && m.root->kids[0]->cp == MathDict::negate(b->cp);
    } else if (const SymbolInfo* e = MathDict::byName(target)) {
      ok = means(target, *e);
    }
    if (!ok) printf("FAIL TeX \\%s → '%s' does not parse to its symbol\n", t.tex, t.target);
    CHECK(ok);
  }
  // (plan P3-24) the `!` rule, (…)-only shedding, a lone bar a middle, the
  // alphabets, typed symbols as their names
  auto one = [&](std::string_view src) { return parseMath(src, arena); };
  MathIR neq = one("a != b");
  CHECK(neq.diags.empty() && neq.root->kids.size() == 3 && neq.root->kids[1]->cp == 0x2260 && neq.root->kids[1]->cls == kRel);
  MathIR fact = one("n! / 2");
  CHECK(fact.diags.empty() && fact.root->kids.size() == 1 && fact.root->kids[0]->k == MNode::Frac);
  MathIR none = one("a !<< b");
  CHECK(!none.diags.empty() && none.root->kids[1]->k == MNode::Error);
  MathIR braces = one("x^{a b}");
  CHECK(braces.root->kids[0]->k == MNode::Attach && braces.root->kids[0]->sup->k == MNode::Group);
  MathIR parens = one("x^(a b)");
  CHECK(parens.root->kids[0]->sup->k == MNode::Run);
  MathIR set = one("{x | x > 0}");
  CHECK(set.root->kids[0]->k == MNode::Group && set.root->kids[0]->a->kids[1]->mid);
  MathIR abs = one("(|x| + 1)");
  CHECK(!abs.root->kids[0]->a->kids[0]->mid && !abs.root->kids[0]->a->kids[2]->mid);
  MathIR bb = one("bb(R) cal(A) bb(1)");
  CHECK(bb.diags.empty() && bb.root->kids.size() == 3 && bb.root->kids[0]->kids[0]->cp == 0x211D &&
        bb.root->kids[1]->kids[0]->cp == 0x1D49C && bb.root->kids[2]->kids[0]->kids[0]->cp == 0x1D7D9);
  MathIR typed = one("∑_i a_i ≤ b");
  CHECK(typed.root->kids.size() == 4 && typed.root->kids[0]->k == MNode::Attach &&
        (typed.root->kids[0]->a->flags & kFlagLarge) && typed.root->kids[2]->cls == kRel);
  // (plan P3-25) the single-token operand rule: an unknown word as an
  // operand is its letters; a big operator is an Op atom with its scope
  MathIR xab = one("x^ab");
  CHECK(xab.root->kids.size() == 1 && xab.root->kids[0]->sup->k == MNode::Run && xab.root->kids[0]->sup->kids.size() == 2);
  MathIR abc = one("a/bc + ab/c");
  CHECK(abc.root->kids[0]->k == MNode::Frac && abc.root->kids[0]->b->kids.size() == 2 &&
        abc.root->kids[2]->k == MNode::Frac && abc.root->kids[2]->a->k == MNode::Run);
  MathIR scope = one("sum_i a_i + b = c");
  CHECK(scope.root->kids[0]->a->scopeEnd == 14);  // up to the = (its byte)
  MathIR xin = one("x_in");
  CHECK(xin.diags.size() == 1 && std::string_view(xin.diags[0].code) == "math-implicit-name");
}

static void unitMathIR() {
  std::string why;
  for (const MathRow& r : mathRows()) {
    const bool ok = checkRow(r, why);
    if (!ok) printf("FAIL math row: %s\n", why.c_str());
    CHECK(ok);
  }
  Arena arena;
  auto ir = [&](std::string_view src) { return parseMath(src, arena); };
  auto has = [](const MathIR& m, const char* code) {
    for (const MathDiag& d : m.diags)
      if (std::string_view(d.code) == code) return true;
    return false;
  };
  MathIR dot = ir("p dot q");  // defect #23: a bare dot is ⋅, the formula stays whole
  CHECK(dot.diags.empty() && dot.root->kids.size() == 3 && dot.root->kids[1]->k == MNode::Sym &&
        dot.root->kids[1]->cp == 0x22C5 && dot.root->kids[1]->cls == kBin);
  MathIR hat = ir("hat(x) + hat");
  CHECK(hat.diags.empty() && hat.root->kids[0]->k == MNode::Call && hat.root->kids[0]->prim == Prim::Accent &&
        hat.root->kids[2]->k == MNode::Sym);
  MathIR sq = ir("sqrt x");  // no '(' : a name, not an error
  CHECK(sq.diags.empty() && sq.root->kids[0]->k == MNode::Text);
  MathIR spaced = ir("abs (x)");  // not adjacent: a name and a group
  CHECK(spaced.root->kids[0]->k == MNode::Text && spaced.root->kids[1]->k == MNode::Group);
  MathIR prime = ir("f'^2");  // primes and ^ merge: f^{′2}
  CHECK(prime.diags.empty() && prime.root->kids[0]->k == MNode::Attach &&
        prime.root->kids[0]->sup->kids.size() == 2);
  MathIR extra = ir("abs(x, y) = 1");
  CHECK(has(extra, "math-arity") && !has(extra, "math-parse"));
  MathIR missing = ir("frac(a)");
  CHECK(has(missing, "math-arity") && missing.root->kids[0]->k == MNode::Call &&
        missing.root->kids[0]->kids.size() == 2 && missing.root->kids[0]->kids[1]->k == MNode::Error);
  MathIR bad = ir("^2 + a = b");  // the error leaf covers its stretch, the relation survives
  CHECK(bad.diags.size() == 1 && bad.diags[0].lo == 0 && bad.diags[0].hi == 7 && bad.root->kids.size() == 3 &&
        bad.root->kids[0]->k == MNode::Error && bad.root->kids[0]->txt == "^2 + a" && bad.root->kids[1]->cls == kRel);
  std::string deep(5000, '(');  // the nesting bound: no stack overflow
  MathIR nested = ir(deep);
  std::string calls;
  for (int i = 0; i < 3000; i++) calls += "abs(";
  MathIR nestedCalls = ir(calls);
  CHECK(!nested.diags.empty() && !nestedCalls.diags.empty());
  MathIR notA = ir("not A");  // `not` stays ¬
  CHECK(notA.root->kids[0]->k == MNode::Sym && notA.root->kids[0]->cp == 0xAC);
  DiagSink ds;
  reportMathDiags(bad, "^2 + a = b", Span{10, 22}, ds);  // $…$ around it: bytes map inside
  CHECK(ds.items.size() == 1 && ds.items[0].span.start == 11 && ds.items[0].span.end == 18);
}

// the font artifact keeps every glyph it shipped before the vocabulary left
// it (plan P1-22 gate: the record set is a superset of the baseline)
static void unitMathGlyphs(const fs::path& root) {
  std::string base;
  CHECK(readFile(root / "engine/data/math/glyph-cps.baseline.txt", base));
  size_t missing = 0, n = 0;
  for (size_t at = 0; at < base.size();) {
    size_t nl = base.find('\n', at);
    std::string line = base.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    at = nl == std::string::npos ? base.size() : nl + 1;
    if (line.empty() || line[0] == '#') continue;
    n++;
    if (!MathFontRegistry::get().primary().glyph((u32)std::stoul(line, nullptr, 16))) missing++;
  }
  CHECK(n > 2000 && missing == 0);
}

// --- golden runner ---
// the shared drive loop (api/driver.h) with the golden providers: native
// tree-sitter tokens, the policy's image answer, the mock measurer
static bool typesetWithMock(Doc& doc) {
  ProviderSet p = mockProviders();
  p.tokens = [](std::string_view lang, std::string_view text, std::vector<CodeToken>& out) {
    out = nativeTokens(lang, text);
    return true;
  };
  return driveToCompletion(doc, p);
}

static void goldenCompare(const fs::path& goldenPath, const std::string& actual,
                          bool update, const std::string& label) {
  if (update) {
    writeFile(goldenPath, actual);
    printf("UPDATED %s\n", label.c_str());
    return;
  }
  std::string expected;
  if (!readFile(goldenPath, expected)) {
    printf("FAIL %s: missing golden %s (run with --update)\n", label.c_str(),
           goldenPath.string().c_str());
    failures++;
    return;
  }
  if (expected != actual) {
    printf("FAIL %s: golden mismatch\n--- expected ---\n%s--- actual ---\n%s",
           label.c_str(), expected.c_str(), actual.c_str());
    failures++;
  }
}

// --- output contract checks (contract.h; plan P0-01) ---
static std::set<std::string> xfail;      // "<fixture>:<output>:<check>"
static std::set<std::string> xfailSeen;  // entries that did fail this run
static int xfailCount = 0;

static void loadXfail(const fs::path& p) {
  std::string txt;
  if (!readFile(p, txt)) return;
  size_t pos = 0;
  while (pos < txt.size()) {
    size_t eol = txt.find('\n', pos);
    if (eol == std::string::npos) eol = txt.size();
    std::string line = txt.substr(pos, eol - pos);
    pos = eol + 1;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
    while (!line.empty() && line.front() == ' ') line.erase(line.begin());
    if (!line.empty()) xfail.insert(line);
  }
}

static void contractCheck(const std::string& label, const char* output, const std::string& html,
                          bool typeset) {
  auto fails = contract::check(html, typeset);
  for (const char* c : {"attr-dup", "id-unique", "anchor-closure", "allowlist", "line-spans"}) {
    std::string key = label + ":" + output + ":" + c;
    bool listed = xfail.count(key) > 0;
    auto it = fails.find(c);
    if (it != fails.end()) {
      if (listed) {
        xfailSeen.insert(key);
        xfailCount++;
      } else {
        printf("FAIL contract %s: %s\n", key.c_str(), it->second.c_str());
        failures++;
      }
    }
  }
}

// --- fuzz regressions (plan P0-03): every crash libFuzzer found is kept as
// test/fuzz/<target>/<name> and replayed here, so the ASan/UBSan build (G2)
// proves it stays fixed ---
// Host inputs are checked at the provider boundary (plan P0-11): hostile
// tokens (unknown tag, out of the body, inside a UTF-8 sequence, overlapping)
// are dropped, non-finite metrics and image dims are refused, and a width
// change re-emits (image boxes follow the new measure — defect #16).
static void unitHostInputs(const fs::path& root) {
  auto load = [&](const char* rel, Doc& doc) {
    std::string ops;
    readFile(root / "test" / "fixtures" / rel, ops);
    doc.cfg.widthPx = 600;
    doc.cfg.baseSizePx = 16;
    return doc.ingest((const u8*)ops.data(), ops.size());
  };
  {
    Doc doc;
    CHECK(load("code/tsm-hl.ops", doc) && !doc.rt.tokenNeeds.empty());
    std::string_view body = doc.strs.get(doc.rt.tokenNeeds[0].body);
    u32 cjk = (u32)body.find("\xE6\xAD\xA3");  // 正: a 3-byte sequence
    CHECK(cjk != (u32)std::string_view::npos);
    const CodeToken bad[] = {
        {0, 1, 200},                        // unknown tag
        {cjk, cjk + 1, 0},                  // ends inside 正
        {cjk + 1, cjk + 3, 1},              // starts inside 正
        {cjk, cjk + 3, 2},                  // fine
        {cjk + 1, cjk + 6, 3},              // overlaps the previous one
        {(u32)body.size(), (u32)body.size() + 4, 0},  // past the body
    };
    // (tsm is answered in-engine: re-open the need to answer it badly)
    doc.rt.tokenNeeds[0].st = ResState::Pending;
    doc.provideTokens(0, bad, std::size(bad));  // rejected whole: plain code, provider-invalid
    CHECK(doc.rt.tokenNeeds[0].st == ResState::Failed);
    for (size_t i = 1; i < doc.rt.tokenNeeds.size(); i++) doc.provideTokens((u32)i, nullptr, 0);
    for (int i = 0; i < 64 && doc.typeset() != Doc::Status::Ok; i++)
      mockProvide(doc.pendingRequests(), doc.metrics, doc.strs, doc.faces);
    std::string html = doc.render();
    CHECK(html.find("\xE6\xAD\xA3") != std::string::npos);  // 正 survives whole
    const FaceId f0 = doc.metrics.faceOf(0);
    doc.metrics.provideWord(doc.strs.intern("nan"), f0, std::nan(""));
    doc.metrics.provideWord(doc.strs.intern("big"), f0, 1e300);
    CHECK(doc.metrics.word(doc.strs.intern("nan"), 0).px == 0);
    CHECK(doc.metrics.word(doc.strs.intern("big"), 0).px == 1e6);
  }
  {
    Doc doc;
    CHECK(load("figure/pull-diag.ops", doc) && doc.rt.boxNeeds.size() == 1);
    doc.provideImage(0, std::nan(""), 384);  // refused: placeholder + image-load
    bool loadDiag = false;
    for (const Diag& d : doc.diags.items) loadDiag = loadDiag || std::string_view(d.code) == "image-load";
    CHECK(loadDiag);
  }
  {
    Doc doc;
    CHECK(load("figure/w-only.ops", doc));
    for (u32 i = 0; i < doc.rt.boxNeeds.size(); i++) doc.provideImage(i, 1000, 500);
    auto imgW = [&] {
      for (int i = 0; i < 64 && doc.typeset() != Doc::Status::Ok; i++)
        mockProvide(doc.pendingRequests(), doc.metrics, doc.strs, doc.faces);
      for (const ParaFrame& fr : doc.layout.paras)
        for (const Fragment& l : fr.lines)
          if (l.kind == FragKind::Image) return suToPx(l.width);
      return -1.0;
    };
    CHECK(imgW() == 120);  // the author's w (defect #24), h from the ratio
    doc.cfg.widthPx = 600;
    doc.setWidth(100);
    CHECK(!doc.done(Stage::Layout) && doc.done(Stage::Measure));  // emit reads no width (plan P1-16)
    CHECK(imgW() == 100);  // clamped to the NEW measure (defect #16)
    doc.setWidth(600);
    CHECK(imgW() == 120);
    size_t n = doc.diags.items.size();
    doc.setWidth(100);
    (void)imgW();
    CHECK(doc.diags.items.size() == n);  // a relayout replaces, never repeats
    CHECK(doc.configure("{\"host\":{\"width\":300}}") == Doc::kApplied && !doc.done(Stage::Layout) &&
          doc.done(Stage::Measure));  // the settings patch applies in place
  }
}

// The cooked→raw maps (plan P2-04; design T1 TextRaw), over every fixture's
// AST: every cooked byte sits at its mapped raw offset — the same byte, a
// space standing for blanks or a line join, or the character of an escape —
// and the raw offsets stay inside the span, never going back. A text without
// a map is positionally its own slice (same length, identity offsets).
static void unitRawMaps(const fs::path& root) {
  long texts = 0, mapped = 0;
  for (auto& e : fs::recursive_directory_iterator(root / "test" / "fixtures")) {
    if (!e.is_regular_file() || e.path().extension() != ".tsm") continue;
    std::string text;
    readFile(e.path(), text);
    Doc doc;
    doc.compile(text);
    const std::string_view all = doc.src.view();
    std::vector<const AstNode*> work{doc.ast};
    while (!work.empty()) {
      const AstNode* n = work.back();
      work.pop_back();
      for (const AstNode* k : n->kids()) work.push_back(k);
      if (n->kind != AstKind::Text) continue;
      texts++;
      std::string_view cooked = doc.strs.get(n->str);
      std::string_view m = doc.strs.get(side<TextP>(n).rawmap);
      const std::string where = fs::relative(e.path(), root).string() + " @" + std::to_string(n->span.start);
      std::vector<std::pair<u32, u32>> pairs;
      if (m.empty()) {
        if (cooked.size() != n->span.end - n->span.start) {
          printf("FAIL raw map: %s: unmapped text is not its slice's length\n", where.c_str());
          failures++;
          continue;
        }
        pairs.push_back({0, 0});
      } else {
        mapped++;
      }
      for (size_t p = 0; p < m.size();) {
        u32 c = 0, r = 0;
        while (p < m.size() && m[p] != ':') c = c * 10 + (u32)(m[p++] - '0');
        p++;
        while (p < m.size() && m[p] != ',') r = r * 10 + (u32)(m[p++] - '0');
        p++;
        pairs.push_back({c, r});
      }
      bool ok = !pairs.empty() && pairs[0].first == 0;
      size_t k = 0;
      u32 prevRaw = 0;
      for (u32 i = 0; ok && i < cooked.size(); i++) {
        while (k + 1 < pairs.size() && pairs[k + 1].first <= i) k++;
        const u32 raw = n->span.start + pairs[k].second + (i - pairs[k].first);
        if (raw < prevRaw || raw > n->span.end) { ok = false; break; }
        prevRaw = raw;
        const char c = cooked[i];
        if (c == ' ' || c == '\n') continue;  // blanks or a line join (a soft break, plan P2-10)
        if (raw < all.size() && all[raw] == c) continue;
        if (raw + 1 < all.size() && all[raw] == '\\' && all[raw + 1] == c) continue;
        ok = false;
      }
      if (!ok) {
        printf("FAIL raw map: %s: map %.*s does not place \"%.*s\"\n", where.c_str(), (int)m.size(), m.data(),
               (int)cooked.size(), cooked.data());
        failures++;
      }
    }
  }
  CHECK(mapped > 0);
  printf("unit: raw maps: %ld texts, %ld mapped, all consistent\n", texts, mapped);
}

// The ops version window (plan P1-01): MIN_COMPAT..OPS_VERSION is read, the
// buffer remembers its version, anything outside the window is refused.
// Conformance (b) of plan P1-09: the tree-sitter grammar (the editor's
// cold-start fallback) against the engine's tokens, byte by byte over every
// fixture's non-blank bytes. The two may differ only in the listed ways —
// the grammar is a line-oriented regex approximation — and must agree on
// most of the text.
static void unitTokenConformance(const fs::path& root) {
  // (engine tag, tree-sitter tag); "-" = no token. Reasons:
  //   jslex     splice heads / JS arguments / statements (no JS lexer; a
  //             /* comment */ in an argument list reads as a strong pair;
  //             a one-line content literal is one statement token; a
  //             template literal in an argument list reads as a code span)
  //   pairs     strict pairs and footnotes (regex pairs, opaque ^[…])
  //   lines     islands, links and comments across lines or nested; a
  //             paragraph's trailing ` <x>` (text: label-like-text, P2-06)
  //   blocks    fences in containers, escaped markers, region bars
  static const char* kAllowed[][3] = {
      {"-", "attribute", "pairs"},         {"-", "embedded", "blocks"},
      {"-", "function", "jslex"},          {"-", "keyword", "blocks"},
      {"-", "operator", "blocks"},         {"-", "string", "lines"},
      {"-", "type", "lines"},              {"attribute", "-", "pairs"},
      {"comment", "-", "lines"},           {"constant", "-", "pairs"},
      {"constant", "attribute", "pairs"},  {"embedded", "-", "jslex"},
      {"embedded", "function", "jslex"},   {"embedded", "keyword", "jslex"},
      {"embedded", "attribute", "jslex"},  {"embedded", "string", "jslex"},
      {"-", "label", "lines"},
      {"function", "-", "jslex"},          {"function", "keyword", "jslex"},
      {"function", "label", "jslex"},      {"keyword", "-", "blocks"},
      {"keyword", "embedded", "blocks"},   {"label", "function", "lines"},
      {"label", "keyword", "lines"},       {"property", "-", "lines"},
      {"punctuation", "-", "jslex"},       {"punctuation", "function", "jslex"},
      {"punctuation", "type", "lines"},
      {"string", "-", "lines"},            {"string", "attribute", "pairs"},
      {"type", "-", "lines"},              {"type", "attribute", "pairs"},
      {"type", "function", "lines"},       {"type", "keyword", "lines"},
      {"type", "property", "lines"},
  };
  auto name = [](int t) { return t < 0 ? "-" : kTokenTags[t]; };
  long bytes = 0, same = 0;
  std::set<std::string> unknown;
  for (auto& e : fs::recursive_directory_iterator(root / "test" / "fixtures")) {
    if (!e.is_regular_file() || e.path().extension() != ".tsm") continue;
    std::string text;
    readFile(e.path(), text);
    std::vector<int> eng(text.size(), -1), ts(text.size(), -1);
    for (const CodeToken& t : syntaxTokens(text))
      for (u32 k = t.start; k < t.end; k++) eng[k] = t.tag;
    for (const CodeToken& t : nativeTokens("tsm", text))
      for (u32 k = t.start; k < t.end; k++) ts[k] = t.tag;
    for (size_t k = 0; k < text.size(); k++) {
      char c = text[k];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
      bytes++;
      if (eng[k] == ts[k]) {
        same++;
        continue;
      }
      bool ok = false;
      for (auto& a : kAllowed) ok = ok || (std::string_view(a[0]) == name(eng[k]) && std::string_view(a[1]) == name(ts[k]));
      if (!ok) unknown.insert(std::string(name(eng[k])) + " / " + name(ts[k]) + " in " +
                              fs::relative(e.path(), root).string());
    }
  }
  for (const std::string& u : unknown) {
    printf("FAIL token conformance: engine/tree-sitter %s\n", u.c_str());
    failures++;
  }
  // P1-09 set 85%; P2-07 gave the grammar statement blocks and splice
  // arguments (97%), so the floor rises with it
  double pct = bytes ? 100.0 * same / bytes : 100;
  CHECK(pct >= 95.0);
  printf("unit: token conformance %.1f%% of %ld non-blank bytes\n", pct, bytes);
}

// TextRules (plan P1-11): the one classifier reproduces, over every
// codepoint, the five classifiers it replaced (literal copies below, frozen
// here); the mock measurer's wide ranges are pinned to the same literal; the
// UCD columns read the pinned 17.0.0 data.
// fmtPxBuf (perf, ahead of plan P1-12): the integer formatter is printf's "%.3f" with
// trailing zeros trimmed — exact ties (k/16, k/1024), negatives that round
// to zero, subnormals, large values, random doubles.
// (plan P3-22) the noweb overlay: spans, masking, merging; a capture alias
static void unitOverlays() {
  std::vector<std::string_view> unknown;
  const u32 noweb = overlayMask("noweb weave", &unknown);
  CHECK(noweb == 1 && unknown.size() == 1 && unknown[0] == "weave" && overlayNames(noweb) == "noweb");
  CHECK(languageOfTag("c++") == "cpp" && languageOfTag("cpp-literate") == "cpp" && languageOfTag("tla").empty());
  // a definition (with its suffix), a reference, a shift and an unclosed name
  const std::string body = "<<a b>>+= x << 1; \"<<c>>\" <<d\n>>";
  const std::vector<CodeToken> spans = overlaySpans(body, noweb);
  CHECK(spans.size() == 2 && spans[0].start == 0 && spans[0].end == 9 && spans[1].start == 19 && spans[1].end == 24);
  CHECK(spans[0].tag == (u8)tokenTagFromCapture("label"));
  const std::string masked = maskSpans(body, spans);
  CHECK(masked.size() == body.size() && masked.substr(0, 10) == "          " && masked.substr(10, 6) == "x << 1");
  // a string run across the second span keeps its parts outside it
  const std::vector<CodeToken> runs = {{10, 11, 7}, {18, 25, 1}};
  const std::vector<CodeToken> merged = mergeSpans(runs, spans);
  CHECK(merged.size() == 5);
  CHECK(merged[0].start == 0 && merged[0].end == 9 && merged[1].start == 10 && merged[2].start == 18 &&
        merged[2].end == 19 && merged[3].start == 19 && merged[3].end == 24 && merged[4].start == 24 &&
        merged[4].end == 25 && merged[4].tag == 1);
  CHECK(validTokens(body, merged.data(), merged.size()));
  CHECK(overlaySpans("<<>> << >>", noweb).size() == 1);  // an empty name is none; a spaced one is
  CHECK(tokenTagFromCapture("conditional.ternary") == 0 && tokenTagFromCapture("nope") == -1);
}

static void unitFmtPx() {
  auto ref = [](double px) {
    char b[64];
    int n = std::snprintf(b, sizeof b, "%.3f", px);
    std::string s(b, (size_t)n);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s + "px";
  };
  auto got = [](double px) {
    char b[48];
    return std::string(b, fmtPxBuf(b, px));
  };
  std::vector<double> vs = {0.0, -0.0, 1.0, -1.0, 0.0005, -0.0005, 0.0015, 0.0025, 1e-300, -1e-300,
                            4.9e-324, 123456.789, 1e6, 0.1, 0.7, 2.675, 1.0005, 12.3456, -0.0004,
                            4503599627370495.5, 9007199254740993.0, 1e20};
  for (int k = -4096; k <= 4096; k++) {
    vs.push_back(k / 16.0);
    vs.push_back(k / 1024.0);
    vs.push_back(k / 2048.0 + 0.0005);
  }
  u64 x = 0x9E3779B97F4A7C15ull;
  for (int i = 0; i < 200000; i++) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    vs.push_back(((double)(x % 20000000) - 10000000.0) / 997.0);
    vs.push_back((double)(x >> 11) / 9007199254740992.0 * 2048.0 - 1024.0);
  }
  int bad = 0;
  for (double v : vs)
    if (ref(v) != got(v) && bad++ < 5)
      printf("FAIL fmtPx %.17g: %s vs printf %s\n", v, got(v).c_str(), ref(v).c_str());
  CHECK(bad == 0);
}

// (plan P4-02) extended grapheme clusters: the shaper's unit
static void unitClusters() {
  auto clusters = [](std::string_view s) {
    std::vector<std::string> out;
    for (u32 i = 0; i < s.size();) {
      const u32 j = clusterEnd(s, i);
      out.emplace_back(s.substr(i, j - i));
      i = j;
    }
    return out;
  };
  using V = std::vector<std::string>;
  CHECK(clusters("ab c") == V({"a", "b", " ", "c"}));
  CHECK(clusters("e\xCC\x81x") == V({"e\xCC\x81", "x"}));                    // e + U+0301
  CHECK(clusters("\xE4\xB8\xAD" "a" "\xE6\x96\x87") == V({"\xE4\xB8\xAD", "a", "\xE6\x96\x87"}));  // 中a文 (fast path)
  CHECK(clusters("\xE8\x91\x9B\xF3\xA0\x84\x80\xE5\xAD\x97")             // 葛 + IVS U+E0100, 字
        == V({"\xE8\x91\x9B\xF3\xA0\x84\x80", "\xE5\xAD\x97"}));
  CHECK(clusters("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB!")      // 👩‍💻!
        == V({"\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB", "!"}));
  CHECK(clusters("\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\xF0\x9F\x87\xA8")  // 🇯🇵 + a lone 🇨
        == V({"\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5", "\xF0\x9F\x87\xA8"}));
  CHECK(clusters("\xE1\x84\x80\xE1\x85\xA1x") == V({"\xE1\x84\x80\xE1\x85\xA1", "x"}));  // Hangul L V
  CHECK(clusters("a\r\nb") == V({"a", "\r\n", "b"}));
  CHECK(clusters(" \xCC\x81") == V({" ", "\xCC\x81"}));  // a space stays a glue
  CHECK(joinsWithoutSpace(0x4E2D, true, 0x6587, true) && !joinsWithoutSpace(0x4E2D, true, 'a', false));
  CHECK(joinsWithoutSpace(0x201D, true, 0x7136, true) && !joinsWithoutSpace(0x201D, false, 0x7136, true));
}

// (plan P4-05) RULES_VERSION 1 against compat's literal predicates: every
// codepoint the allowlist (test/golden/RULES, rules-diff's) does not name
// classifies as compat did; kerning is a class column everywhere
static void unitTextRules(const fs::path& root) {
  std::vector<std::pair<u32, u32>> allowed;
  {
    std::ifstream f(root / "test/golden/RULES");
    std::string line;
    while (std::getline(f, line)) {
      if (line.rfind("U+", 0) != 0 || line.find(" kern") != std::string::npos) continue;
      const u32 a = (u32)std::stoul(line.substr(2), nullptr, 16);
      const size_t dots = line.find("..U+");
      allowed.push_back({a, dots == std::string::npos ? a : (u32)std::stoul(line.substr(dots + 4), nullptr, 16)});
    }
  }
  CHECK(allowed.size() > 30);
  auto isAllowed = [&](u32 cp) {
    for (auto [a, b] : allowed)
      if (cp >= a && cp <= b) return true;
    return false;
  };
  auto oldCjk = [](u32 cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
           (cp >= 0x20000 && cp <= 0x2FA1F);
  };
  auto oldOpen = [](u32 cp) {
    switch (cp) {
      case 0xFF08: case 0x3014: case 0xFF3B: case 0xFF5B: case 0x300A: case 0x3008:
      case 0x300C: case 0x300E: case 0x3010: case 0x201C: case 0x2018:
        return true;
      default:
        return false;
    }
  };
  auto oldClose = [](u32 cp) {
    switch (cp) {
      case 0xFF09: case 0x3015: case 0xFF3D: case 0xFF5D: case 0x300B: case 0x3009:
      case 0x300D: case 0x300F: case 0x3011: case 0x201D: case 0x2019: case 0x3002:
      case 0xFF0E: case 0xFF0C: case 0x3001: case 0xFF1B: case 0xFF1A: case 0xFF01:
      case 0xFF1F:
        return true;
      default:
        return false;
    }
  };
  u32 bad = 0;
  for (u32 cp = 0; cp < 0x110000; cp++) {
    bool cjk = oldCjk(cp), op = oldOpen(cp), cl = oldClose(cp);
    bool ok = mockIsWide(cp) == cjk;  // (the mock measurer's copy is frozen)
    if (!isAllowed(cp))
      ok = ok && isWide(cp) == cjk && isOpenPunct(cp) == op && isClosePunct(cp) == cl &&
           isIdeo(cp) == (cjk && !op && !cl) && isPunctGlyph(cp) == (op || cl) && noStart(cp) == cl &&
           takesAutospace(cp) == (cjk && !op && !cl) &&
           joinsWide(cp) == (cjk || cp == 0x2014 || cp == 0x2026) &&
           isAmbDashOrEllipsis(cp) == (cp == 0x2014 || cp == 0x2026) &&
           isAmbQuote(cp) == (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D);
    if (!ok && bad++ < 5) printf("FAIL textrules: U+%04X classifies differently\n", cp);
  }
  CHECK(bad == 0);
  CHECK(RULES_VERSION == 1 && std::string_view(UNICODE_VERSION) == "17.0.0");
  // kerning by class (plan P4-05): letters and narrow punctuation — curly
  // quotes and dashes too — not CJK, not marks, not spaces
  CHECK(kernEligible('A') && kernEligible('7') && kernEligible(0x201C) && kernEligible(0x2014) &&
        !kernEligible(0x4E2D) && !kernEligible(0x0301) && !kernEligible(' ') && !kernEligible(0x00A0));
  // the new coverage: Hangul, small kana, the prolonged sound mark, the
  // brackets compat missed, the break controls
  CHECK(isWide(0xAC00) && !takesAutospace(0xAC00) && !joinsWide(0xAC00) && noStart(0x3063) && noStart(0x30FC) &&
        isOpenPunct(0x3016) && isClosePunct(0xFF60) && isPunctGlyph(0x30FB) && noStart(0x30FB) &&
        ccOf(0x200B) == CC::ZwSpace && ccOf(0x2060) == CC::WordJoiner && ccOf(0x00A0) == CC::NbSpace &&
        ccOf(0x202F) == CC::NbRigid && ccOf(0x00AD) == CC::SoftHyphen && ccOf(0x3000) == CC::IdeoSpace);
  CHECK(cpInfo(0x1F600).extPict && cpInfo(0x4E00).eaw == EAW::W && cpInfo(0x41).eaw == EAW::Na);
  CHECK(cpInfo(0x0301).gcb == GCB::Extend && cpInfo(0x1F1E6).gcb == GCB::Regional_Indicator &&
        cpInfo(0x1100).gcb == GCB::L && cpInfo(0x0D).gcb == GCB::CR && cpInfo(0x200D).gcb == GCB::ZWJ);
  CHECK(cpInfo(0x4E00).cc == CC::Ideo && cpInfo(0x3002).cc == CC::FullStopW && cpInfo(0x41).cc == CC::Alpha);
}

// The element registry and locale terms (plan P1-10): the built-in rows
// load, the terms language falls back exact → script → language → root, and
// a registry whose figure row has another name produces the same document
// (built-in rows have no privilege: only the index names the class).
static void unitRegistry(const fs::path& root) {
  const Registry& reg = Registry::builtin();
  CHECK(reg.classes.size() > 6 && reg.collector("toc") && reg.collector("bibliography"));
  CHECK(reg.reservedShape("h-1.2") && reg.reservedShape("fn-3") && reg.reservedShape("bib-x") &&
        !reg.reservedShape("h-index") && !reg.reservedShape("fig-1"));
  CHECK(localePackFor("ja-JP") == "ja" && localePackFor("zh-TW") == "zh-Hant" &&
        localePackFor("zh-Hant-HK") == "zh-Hant" && localePackFor("zh-CN") == "zh-Hans" &&
        localePackFor("zh") == "zh-Hans" && localePackFor("de") == "en" && localePackFor("en-GB") == "en");
  {
    Config c;
    c.lang = "zh-HK";
    CHECK(Terms(c).get("figure") == "\xE5\x9C\x96 " && Terms(c).get("backref") == "\xE2\x86\xA9");
  }
  std::string err;
  CHECK(!Registry::fromJson(R"({"classes":{"x":{"counter":"nope"}}})", err) && !err.empty());
  // a numbered class needs a counter (fuzz finding, plan P2-07)
  CHECK(!Registry::fromJson(R"({"classes":{"x":{"numbering":"always"}}})", err) && !err.empty());
  // (plans P3-20, P3-23) a presentation row names allowlisted elements and
  // ARIA roles only: a role an inline phrasing element, a block a flow one
  CHECK(!Registry::fromJson(R"({"html":{"x":{"element":"script","inline":true}}})", err) &&
        err.find("allowlist") != std::string::npos);
  CHECK(!Registry::fromJson(R"({"html":{"x":{"element":"iframe"}}})", err) && err.find("allowlist") != std::string::npos);
  CHECK(!Registry::fromJson(R"({"html":{"x":{"element":"div","aria":"button"}}})", err) &&
        err.find("ARIA") != std::string::npos);
  CHECK(!Registry::fromJson(R"({"html":{"x":{"element":"figure","slots":{"caption":"img"}}}})", err));
  CHECK(Registry::fromJson(R"({"html":{"x":{"element":"mark","inline":true}}})", err) != nullptr);
  {
    auto r = Registry::fromJson(
        R"({"html":{"a":{"element":"section","aria":"note","typeset":{"frame":true}},"b":{"like":"a","element":"aside"}}})", err);
    CHECK(r && r->htmlRow("b") && r->htmlRow("b")->element == "aside" && r->htmlRow("b")->aria == "note" &&
          r->htmlRow("b")->frame);
  }
  // (plan P3-20) link hrefs: relative, http(s), mailto — never another scheme
  for (const char* ok : {"a.html", "/x/y", "#top", "?q=1", "https://e.org/a:b", "HTTP://e", "mailto:a@b", "x/y:z"})
    CHECK(safeLinkUrl(ok));
  for (const char* bad : {"javascript:alert(1)", "JavaScript:x", "java\tscript:x", " javascript:x", "data:text/html,x",
                          "vbscript:x", "file:///etc/passwd"})
    CHECK(!safeLinkUrl(bad));
  // (plan P3-13) flows: a placement is one of four; a class like another
  // patches its flow (the marker-alias's reference form kept); a collector
  // like another takes its query and templates, its scope given
  CHECK(!Registry::fromJson(R"({"classes":{"x":{"flow":{"name":"f","placement":"page"}}}})", err) &&
        err.find("placement") != std::string::npos);
  {
    std::string j(kElementsJson);
    j.insert(j.find("\"refsection\": {"),  // (after the footnote row)
             R"("endnote": {"like": "footnote", "select": [{"node": "note", "role": "endnote"}],)"
             R"( "alias": {"prefix": "en-", "body": "ordinal"},)"
             R"( "flow": {"name": "endnotes", "placement": "section-end", "depth": 2,)"
             R"( "marker-alias": {"prefix": "enref-", "body": "ordinal"}}},)");
    j.insert(j.find("\"bibliography\": {", j.find("\"collectors\": {")),  // (after notes)
             R"("endnotes": {"like": "notes", "query": {"flow": "endnotes", "scope": "section", "depth": 2}},)");
    std::unique_ptr<Registry> r = Registry::fromJson(j, err);
    CHECK(r != nullptr);
    if (!r) printf("  (%s)\n", err.c_str());
    if (r) {
      const ElementClass* en = nullptr;
      for (const ElementClass& c : r->classes)
        if (c.name == "endnote") en = &c;
      CHECK(en && en->flow && en->flow->name == "endnotes" && en->flow->depth == 2 &&
            en->flow->placement == FlowDef::Placement::SectionEnd && en->flow->markerAlias.hasRef &&
            en->alias.body == AliasRule::Body::Ordinal && !en->flow->marker.empty());
      const CollectorDef* c = r->collector("endnotes");
      CHECK(c && c->src == CollectorDef::Src::Flow && c->flow == "endnotes" &&
            c->scope == CollectorDef::Scope::Section && c->scopeDepth == 2 && !c->entry.empty() && !c->wrap.empty());
      CHECK(r->reservedShape("enref-3.2") && r->reservedShape("en-7") && !r->reservedShape("en-x"));
    }
  }

  std::string json(kElementsJson);
  size_t cls = json.find("\"classes\"");
  size_t fig = json.find("\"figure\": {", cls);
  CHECK(cls != std::string::npos && fig != std::string::npos);
  json.replace(fig, 8, "\"illustration\"");
  for (const char* ref : {"\"like\": \"figure\"", "\"inside\": \"figure\"",
                          "\"classes\": [\"figure\"]"})  // the rows built on it (P3-03), the lists naming it (P3-13)
    for (size_t at = json.find(ref); at != std::string::npos; at = json.find(ref, at + 1))
      json.replace(json.find("figure", at), 6, "illustration");
  std::unique_ptr<Registry> renamed = Registry::fromJson(json, err);
  CHECK(renamed != nullptr);
  if (!renamed) return;
  for (const char* rel : {"region/figure", "figure/block", "labels/registry-diag", "ref/supplements-en"}) {
    fs::path tsm = root / "test" / "fixtures" / (std::string(rel) + ".tsm");
    fs::path ops = tsm;
    ops.replace_extension(".ops");
    std::string src, buf, profile;
    if (!readFile(tsm, src) || !readFile(ops, buf)) continue;
    readFile(root / "test" / "profiles" / "golden.json", profile);
    auto run = [&](const std::string& base, std::string& index) {
      Doc d;
      d.registryBase = base;
      d.configure(profile);
      d.compile(src);
      d.ingest((const u8*)buf.data(), buf.size());
      ProviderSet p = mockProviders();
      driveToCompletion(d, p);
      index = d.product("index");
      return d.product("tree") + d.product("html") + d.product("diags");
    };
    std::string i1, i2;
    std::string a = run("", i1), b = run(json, i2);
    CHECK(a == b);
    size_t at;
    while ((at = i2.find("illustration")) != std::string::npos) i2.replace(at, 12, "figure");
    CHECK(i1 == i2 && i1.find("instance figure") != std::string::npos);
  }
  // (plan P2-07) the same, declared by the document: the figure row written
  // with $.element under another name (semantics/parity-declared, its
  // declarations after its instances) reads exactly as the built-in row
  auto products = [&](const char* rel) {
    fs::path tsm = root / "test" / "fixtures" / "semantics" / (std::string(rel) + ".tsm");
    fs::path ops = tsm;
    ops.replace_extension(".ops");
    std::string src, buf, profile;
    if (!readFile(tsm, src) || !readFile(ops, buf)) return std::string();
    readFile(root / "test" / "profiles" / "golden.json", profile);
    Doc d;
    d.configure(profile);
    d.compile(src);
    d.ingest((const u8*)buf.data(), buf.size());
    ProviderSet p = mockProviders();
    driveToCompletion(d, p);
    // (the semantic page writes a figure's caption from its caption part,
    // plan P3-23, which the figure constructor makes and a #!sketch region
    // does not: the registry rows' products are compared)
    return d.product("index") + d.product("html") + d.product("blocks");
  };
  std::string builtin = products("parity-builtin"), declared = products("parity-declared");
  size_t at;
  while ((at = declared.find("sketch")) != std::string::npos) declared.replace(at, 6, "figure");
  CHECK(!builtin.empty() && builtin == declared && builtin.find("instance figure 2") != std::string::npos);
}

// AST bytes (plan P1-05 bench gate): node + side record + kid slot per
// node, against what the pre-CallAST layout (a 64-byte node with an
// std::vector of kid pointers) took for the same tree — over every fixture.
static void unitAstBytes(const fs::path& root) {
  size_t bytes = 0, before = 0, nodes = 0;
  std::function<void(const AstNode*)> walk = [&](const AstNode* n) {
    nodes++;
    bytes += sizeof(AstNode) + n->side + n->nkids * sizeof(AstNode*);
    before += 64 + n->nkids * sizeof(AstNode*);
    for (const AstNode* k : n->kids()) walk(k);
  };
  for (auto& e : fs::recursive_directory_iterator(root / "test" / "fixtures")) {
    if (!e.is_regular_file() || e.path().extension() != ".tsm") continue;
    std::string text;
    readFile(e.path(), text);
    Doc doc;
    doc.compile(std::move(text));
    if (doc.ast) walk(doc.ast);
  }
  CHECK(nodes > 0 && bytes <= before);
  printf("unit: AST %zu nodes, %zu bytes (pre-CallAST layout %zu)\n", nodes, bytes, before);
}

static void unitOpsWindow(const fs::path& root) {
  std::string ops;
  // a buffer that uses no vocabulary newer than MIN_COMPAT stays at it (a
  // text without a cooked→raw map, no DIAG, no AT: plan P2-04)
  readFile(root / "test" / "fixtures" / "cjk" / "basic.ops", ops);
  CHECK(ops.size() > 5 && (u8)ops[4] == OPS_MIN_COMPAT);
  for (int v : {(int)OPS_MIN_COMPAT - 1, (int)OPS_VERSION + 1}) {
    std::string b = ops;
    b[4] = (char)v;
    RawOps r;
    DiagSink d;
    decodeOps((const u8*)b.data(), b.size(), r, d);
    CHECK(!r.ok && !d.items.empty() &&
          d.items[0].msg.find("outside the reader's window") != std::string::npos);
  }
  RawOps r;
  DiagSink d;
  decodeOps((const u8*)ops.data(), ops.size(), r, d);
  CHECK(r.ok && r.version == OPS_MIN_COMPAT);
}

static void fuzzRegressions(const fs::path& root) {
  fs::path dir = root / "test" / "fuzz";
  if (!fs::exists(dir)) return;
  for (auto& e : fs::recursive_directory_iterator(dir)) {
    if (!e.is_regular_file()) continue;
    std::string target = e.path().parent_path().filename().string();
    std::string data;
    readFile(e.path(), data);
    Doc doc;
    doc.cfg.widthPx = 300;
    doc.cfg.baseSizePx = 16;
    if (target == "fuzz_opreader") {
      if (doc.ingest((const u8*)data.data(), data.size())) (void)doc.renderFallback();
    } else if (target == "fuzz_fragment") {  // (plan P2-13) as the target checks
      FragmentRequest req;
      std::vector<std::vector<FragmentText>> runs{{{data, 7}}};
      if (decodeFragmentRequest(data, req)) runs.push_back(req.texts);
      for (const auto& texts : runs) {
        LowerProgram prog;
        std::string why;
        CHECK(readLowerProgram(codegenFragments(texts, nullptr).program, prog, why));
      }
    } else {
      doc.compile(data);
      (void)dumpAst(doc.ast, doc.src, doc.strs);
      LowerProgram prog;  // the program codegen wrote reads back (as fuzz_inline checks)
      std::string why;
      CHECK(readLowerProgram(doc.js.program, prog, why));
    }
  }
}

// The front end's nesting bound (kMaxNesting, fuzz finding): containers,
// content bodies and inline pairs past it are text or cut, said once
// (nest-limit); the AST stays shallow and codegen's program reads back.
static void unitNestLimit() {
  auto rep = [](const std::string& open, const std::string& close, int n) {
    std::string s;
    for (int i = 0; i < n; i++) s += open;
    s += "x";
    for (int i = 0; i < n; i++) s += close;
    return s + "\n";
  };
  const std::string srcs[] = {
      rep(">", "", 100000),            // quotes: one per level
      rep("- ", "", 5000),             // list items
      rep("*_", "_*", 20000),          // inline pairs
      rep("#emph[", "]", 20000),       // content arguments
      rep("^[", "]", 20000),           // notes
      rep("[", "](u)", 20000),         // link text
      rep("#strong[#emph[", "]]", 5000),
      rep("#!r\n", "#r!\n", 3000),     // regions
  };
  for (const std::string& src : srcs) {
    Doc doc;
    doc.compile(src);
    u32 depth = 0;
    std::function<void(const AstNode*, u32)> walk = [&](const AstNode* n, u32 d) {
      depth = std::max(depth, d);
      for (const AstNode* k : n->kids()) walk(k, d + 1);
    };
    walk(doc.ast, 0);
    CHECK(depth <= 3 * kMaxNesting);
    LowerProgram prog;
    std::string why;
    CHECK(readLowerProgram(doc.js.program, prog, why));
    u32 said = 0;
    for (const Diag& d : doc.diags.items) said += std::string_view(d.code) == "nest-limit";
    CHECK(said >= 1);
  }
  {  // within the bound nothing is cut
    Doc doc;
    doc.compile(rep("> ", "", 40) + rep("#emph[", "]", 40));
    for (const Diag& d : doc.diags.items) CHECK(std::string_view(d.code) != "nest-limit");
  }
}

// CRLF line terminators read as LF (plan P0-04): a CRLF source and its LF
// twin produce the same AST, spans aside.
static void unitCrlf() {
  auto astNoSpans = [](const std::string& src) {
    Doc d;
    d.compile(src);
    std::string a = dumpAst(d.ast, d.src, d.strs), out;
    for (size_t i = 0; i < a.size(); i++) {
      if (a[i] == '@' && i + 1 < a.size() && a[i + 1] == '[') {
        while (i < a.size() && a[i] != ')') i++;
        continue;
      }
      out += a[i];
    }
    return out;
  };
  std::string lf = "= Head\n\nA para\nwith $a +\nb$ math %-- c\nd --% end.\n\n```js\nlet x = 1;\nx++\n```\n\n%-- block\ncomment --%\n";
  std::string crlf;
  for (char c : lf) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  CHECK(astNoSpans(lf) == astNoSpans(crlf));
}

// InstLimits (plan P0-07, defect #12): a tiny ops buffer describing an
// exponential DAG (each node references the previous one twice, 2^40 leaves)
// and a 300-deep chain must instantiate within the budget, with an
// inst-limit diagnostic.
static std::string bombOps(int levels, bool chain) {
  std::string ops;
  auto v = [&](u64 x) {
    while (x > 127) { ops += (char)((x & 127) | 128); x >>= 7; }
    ops += (char)x;
  };
  ops += (char)1;  // MAKE_TEXT "x"
  v(0);
  for (int k = 1; k <= levels; k++) {
    ops += (char)2;  // MAKE_NODE seq, no args
    v(25);
    v(0);
    if (chain) { v(1); v((u64)k - 1); }
    else { v(2); v((u64)k - 1); v((u64)k - 1); }
  }
  ops += (char)3;  // EMIT the top node
  v((u64)levels);
  std::string head = "TSOP";
  head += (char)OPS_VERSION;
  std::string h;
  auto hv = [&](u64 x) {
    while (x > 127) { h += (char)((x & 127) | 128); x >>= 7; }
    h += (char)x;
  };
  hv(1);                  // strings
  hv(1);                  // string bytes
  hv((u64)levels + 2);    // ops
  return head + h + "x" + std::string(1, (char)1) + ops;
}
static size_t countNodes(const ContentNode* n) {
  size_t c = 1;
  for (const ContentNode* k : n->kids) c += countNodes(k);
  return c;
}
static void unitInstLimits() {
  for (bool chain : {false, true}) {
    std::string buf = bombOps(chain ? 300 : 40, chain);
    Doc doc;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(doc.ingest((const u8*)buf.data(), buf.size()));
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    bool limit = false;
    for (const Diag& d : doc.diags.items) limit = limit || std::string_view(d.code) == "inst-limit";
    CHECK(limit);
    CHECK(countNodes(doc.tree.root) <= kInstMinBudget + 1024);  // + error placeholders
    if (!chain) printf("unit: exponential DAG instantiated in %.1f ms\n", ms);
  }
}

// Measurement faces (plan P1-04): paint-only variants share a face (and so
// their metrics), CJK italic measures upright, mono×CJK resolves through
// fonts.monoCjk → fonts.cjk.
static void unitFaces() {
  Doc doc;
  Styling base;
  StyleId plain = doc.styles.idOf(base);
  Styling red = base;
  red.color = doc.strs.intern("red");
  red.decoration |= DECORATION_UNDER;
  CHECK(doc.faces.faceOf(doc.styles.idOf(red)) == doc.faces.faceOf(plain));
  Styling bold = base;
  bold.weight = 700;
  CHECK(doc.faces.faceOf(doc.styles.idOf(bold)) != doc.faces.faceOf(plain));
  Styling cjkItalic = base;
  cjkItalic.script = SCRIPT_CJK;
  cjkItalic.italic = true;
  Styling cjk = base;
  cjk.script = SCRIPT_CJK;
  CHECK(doc.faces.faceOf(doc.styles.idOf(cjkItalic)) == doc.faces.faceOf(doc.styles.idOf(cjk)));
  Styling monoCjk = base;
  monoCjk.fontRole = FONTROLE_MONO;
  monoCjk.script = SCRIPT_CJK;
  FaceId mc = doc.faces.faceOf(doc.styles.idOf(monoCjk));
  CHECK(doc.faces.family(mc) == doc.cfg.cjkFont);  // no monoCjk set: body CJK
  Doc d2;
  d2.configure(R"({"fonts":{"monoCjk":"\"Sarasa Mono SC\""}})");
  CHECK(d2.faces.family(d2.faces.faceOf(d2.styles.idOf(monoCjk))) == "\"Sarasa Mono SC\"");
  // one metric answer serves every style of the face
  doc.metrics.provideWord(doc.strs.intern("word"), doc.faces.faceOf(plain), 40);
  CHECK(doc.metrics.hasWord(doc.strs.intern("word"), doc.styles.idOf(red)));
}

// The settings codec (plan P1-03): one JSON document, rows applied in
// schema order, unknown paths and bad values diagnosed and skipped.
// The cascade's factories (plan P3-01): the defaults parse clean; a code
// token is made as text of class tok-<tag> (a comment italic and hanging by
// the rules, its colour its own); make folds a role's rule under the site's
// deltas; reenter keeps a moved node's own delta over the new parent.
static void unitCascade() {
  Arena a;
  Interner strs{a};
  StyleTable styles;
  DiagSink diags;
  Cascade cascade(strs);
  auto setting = [](std::string_view k, std::string& out) {
    out = k == "code.scale" ? "0.85" : k == "par.indent" ? "2" : k == "list.indent" || k == "quote.indent" ? "1.5"
        : k == "code.snapKerning" ? "false" : k == "code.sidecarFrac" ? "0.4" : k == "code.contIndent" ? "2" : "";
    return true;
  };
  cascade.setBase(parseRules(defaultRulesJson(), strs, diags, "defaults", setting), {});
  CHECK(diags.items.empty());
  {
    const std::string body = "x // c";
    const CodeToken toks[] = {{0, 1, 0}, {2, 6, kTokenTagComment}};
    std::vector<std::vector<TokenRun>> lines;
    tokenLines(body, 0, &cascade, 0, toks, 2, strs, styles, lines);
    CHECK(lines.size() == 1 && lines[0].size() == 3);
    const Styling& k = styles.get(lines[0][0].style);
    const Styling& c = styles.get(lines[0][2].style);
    // (plan P3-18) a token is its class (tok-<tag>), coloured by the theme
    CHECK(!k.italic && k.hang == 0 && !k.color && strs.get(k.classes) == "tok-keyword");
    CHECK(c.italic && c.hang == HANG_CONTENT && !c.color && strs.get(c.classes) == "tok-comment");
    tokenLines(body, 0, nullptr, 0, toks, 2, strs, styles, lines);  // the semantic page's: no rules
    CHECK(!styles.get(lines[0][2].style).italic);
  }
  {
    Styling st, scope;
    st.weight = 700;  // a site in bold
    Cascade::NodeView v{Kind::ref};
    v.role = strs.intern("fn-marker");
    cascade.make(st, scope, v, 0, StyleDelta{}, 1.0f);
    CHECK(st.baseline == BASELINE_SUPER && st.sizeMul == 0.7f && st.weight == 700 && scope.baseline == 0);
    Styling st2, scope2;
    cascade.make(st2, scope2, v, 0, StyleDelta{}, 0.5f);  // the site's own size wins over the rule's
    CHECK(st2.sizeMul == 0.5f && scope2.sizeMul == 0.5f);
  }
  {
    Styling parentScope, scope, st;
    scope.weight = 700;  // its own delta at its old place
    scope.sizeMul = 1.2f;
    st.sizeMul = 0.85f;  // the new parent's (a note body)
    Cascade::NodeView v{Kind::styled};
    cascade.reenter(st, v, 0, scope, parentScope);
    CHECK(st.weight == 700 && st.sizeMul == 0.85f * 1.2f);
    Styling code;
    code.sizeMul = 0.85f;
    cascade.reenter(code, Cascade::NodeView{Kind::code}, 0, parentScope, parentScope);  // rules fold in again
    CHECK(code.fontRole == FONTROLE_MONO && code.sizeMul == 0.85f * 0.85f);
  }
}

// The RenderResult frame (plan P3-05): every block's body under its key;
// the bodies with their positional attributes put back (as the shell does)
// are the legacy render byte for byte; held keys are not sent; keys are
// stable across renders and generations increase.
// (plan P3-12; design T6 PageBuilder, D-Y04) the page builder's mechanisms
// over synthetic layouts: their producers (page floats, table headers,
// footnote inserts, #pagebreak) arrive in later steps
static void unitPaginate() {
  auto frag = [](i64 y, Su h, PenTier t = PenTier::Normal, u8 paged = 0) {
    Fragment f;
    f.y = (Su)y;
    f.height = h;
    f.brk = t;
    f.paged = paged;
    f.srcSpan = Span{(u32)y, (u32)(y + 1)};
    return f;
  };
  auto layoutOf = [](std::vector<Fragment> lines) {
    LayoutResult lr;
    ParaFrame fr;
    fr.lines = std::move(lines);
    lr.paras.push_back(std::move(fr));
    return lr;
  };
  auto pagesOf = [](const PageResult& pr) {
    std::vector<std::vector<u32>> out;
    for (const Page& pg : pr.pages) {
      out.emplace_back();
      for (const PageBand& b : pg.bands) out.back().push_back(b.lo);
    }
    return out;
  };
  using V = std::vector<std::vector<u32>>;
  {  // widows and orphans relax before keep-with-next (D-Y04)
    DiagSink d;
    const PageResult pr = paginate(layoutOf({frag(0, 100), frag(100, 100, PenTier::KeepWithNext),
                                             frag(200, 100, PenTier::WidowOrphan), frag(300, 100)}),
                                   PageSpec{250, 0}, &d);
    CHECK(pagesOf(pr) == (V{{0, 1}, {2, 3}}));
    CHECK(d.items.size() == 1 && std::string_view(d.items[0].code) == "keep-violated");
  }
  {  // a relaxed cut must keep something together on the next sheet
    const PageResult pr = paginate(layoutOf({frag(0, 100), frag(100, 100, PenTier::WidowOrphan),
                                             frag(200, 200, PenTier::KeepWithNext)}),
                                   PageSpec{250, 0});
    CHECK(pagesOf(pr) == (V{{0, 1}, {2}}));
  }
  {  // an atom taller than a sheet is set alone and overflows it, visibly
    DiagSink d;
    const PageResult pr = paginate(layoutOf({frag(0, 100), frag(100, 400), frag(500, 100)}), PageSpec{250, 0}, &d);
    CHECK(pagesOf(pr) == (V{{0}, {1}, {2}}) && pr.pages[1].overflow == 150 && pr.pages[0].overflow == 0);
    CHECK(d.items.size() == 1 && std::string_view(d.items[0].code) == "page-overflow");
  }
  {  // a forced break ends the sheet
    const PageResult pr = paginate(layoutOf({frag(0, 50), frag(50, 50, PenTier::Forced), frag(100, 50)}),
                                   PageSpec{250, 0});
    CHECK(pagesOf(pr) == (V{{0}, {1, 2}}));
  }
  {  // a page float lifts to the top of its sheet; the flow moves below it
    const PageResult pr = paginate(layoutOf({frag(0, 100), frag(100, 100, PenTier::Normal, kPagedMovable),
                                             frag(200, 100)}),
                                   PageSpec{250, 0});
    CHECK(pagesOf(pr) == (V{{1, 0}, {2}}));
    CHECK(pr.pages[0].bands[0].yShift == -100 && pr.pages[0].bands[1].yShift == 100);
  }
  {  // ... or waits for the next sheet's top when it does not fit; the
     // flow closes over the room it left (plan P3-15)
    const PageResult pr = paginate(layoutOf({frag(0, 200), frag(200, 100, PenTier::Normal, kPagedMovable),
                                             frag(300, 20)}),
                                   PageSpec{250, 0});
    CHECK(pagesOf(pr) == (V{{0, 2}, {1}}));  // carried to the top of sheet 2
    CHECK(pr.pages[0].bands[1].yShift == -100 && pr.pages[1].bands[0].lo == 1 && pr.pages[1].bands[0].yShift == 0);
  }
  {  // (plan P3-15) a bottom float sinks to its sheet's foot; a page float
     // waits for a sheet of floats after its sheet
    const PageResult pr =
        paginate(layoutOf({frag(0, 50), frag(50, 40, PenTier::Normal, kPagedMovable | kPagedBottom), frag(90, 50),
                           frag(140, 60, PenTier::Normal, kPagedMovable | kPagedPage), frag(200, 50)}),
                 PageSpec{300, 0});
    CHECK(pagesOf(pr) == (V{{0, 2, 4, 1}, {3}}));
    CHECK(pr.pages[0].bands[3].yShift == 260 - 50 && pr.pages[0].bands[1].yShift == -40 &&
          pr.pages[0].bands[2].yShift == -100 && pr.pages[1].bands[0].yShift == 0);
  }
  {  // a footnote insert goes to the bottom of its reference's sheet
    Fragment ins = frag(1000, 50, PenTier::Normal, kPagedInsert);
    ins.insertAt = 100;
    const PageResult pr = paginate(layoutOf({frag(0, 100), frag(100, 100), ins}), PageSpec{300, 10});
    CHECK(pagesOf(pr) == (V{{0, 1, 2}}));
    CHECK(pr.pages[0].bands[2].yShift == 250 - 1000);  // at 300 - 60 + 10
  }
  {  // (plan P3-13) a separator (a deferred flow's rule) stands above each
     // sheet's inserts in place of the skip; a sheet without any has none
    Fragment sep = frag(900, 20, PenTier::Normal, kPagedInsert | kPagedHeader);
    Fragment a = frag(1000, 50, PenTier::Normal, kPagedInsert), b = frag(1050, 50, PenTier::Normal, kPagedInsert);
    a.insertAt = 0;
    b.insertAt = 300;
    const PageResult pr = paginate(layoutOf({frag(0, 100), frag(100, 100), frag(200, 100), frag(300, 100), sep, a, b}),
                                   PageSpec{300, 10});
    CHECK(pagesOf(pr) == (V{{0, 1, 4, 5}, {2, 3, 4, 6}}));
    CHECK(pr.pages[0].bands[2].repeat && pr.pages[0].bands[2].yShift == 230 - 900 &&
          pr.pages[0].bands[3].yShift == 250 - 1000);  // the rule at 300 - 70, the insert below it
    CHECK(pr.pages[1].bands[2].yShift == 200 + 230 - 900 && pr.pages[1].bands[3].yShift == 200 + 250 - 1050);
  }
  {  // a table's header rows repeat atop its continuation sheet
    std::vector<Fragment> t = {frag(0, 50, PenTier::Normal, kPagedHeader), frag(50, 100), frag(150, 100)};
    for (Fragment& f : t) f.table = 5;
    const PageResult pr = paginate(layoutOf(t), PageSpec{160, 0});
    CHECK(pagesOf(pr) == (V{{0, 1}, {0, 2}}));
    CHECK(pr.pages[1].bands[0].repeat && pr.pages[1].bands[0].yShift == 150 && pr.pages[1].bands[1].yShift == 50);
  }
}

// (plan P3-08; design T6 conservative bands) no line overlaps a float: every
// in-flow line of the float fixtures, in document coordinates, stays clear
// of every float box (image and caption rows), at any measure
static void unitFloatsNeverOverlap(const fs::path& root) {
  for (const char* name : {"float", "stack", "float-in-list", "both-sides", "wide-float"})
    for (double width : {300.0, 240.0, 180.0, 420.0}) {
      Doc doc;
      std::string ops, profile, src;
      readFile(root / "test" / "fixtures" / "figure" / (std::string(name) + ".ops"), ops);
      readFile(root / "test" / "profiles" / "golden.json", profile);
      readFile(root / "test" / "fixtures" / "figure" / (std::string(name) + ".tsm"), src);
      doc.configure(profile);
      doc.configure(R"({"host": {"width": )" + std::to_string(width) + "}}");
      doc.compile(src);
      CHECK(doc.ingest((const u8*)ops.data(), ops.size()) && typesetWithMock(doc));
      struct Box { i64 y0, y1; Su x0, x1; };
      std::vector<Box> floats;
      for (const ParaFrame& f : doc.layout.paras)
        for (const VEntry& e : f.vlist) {
          if (!e.out) continue;  // a float: its unit's fragments (image, caption rows)
          Box b{INT64_MAX, INT64_MIN, INT32_MAX, INT32_MIN};
          for (const Fragment& l : f.lines) {
            if (l.unitIdx != e.unit) continue;
            b.y0 = std::min(b.y0, (i64)f.y + l.y);
            b.y1 = std::max(b.y1, (i64)f.y + l.y + l.height);
            b.x0 = std::min(b.x0, l.left);
            b.x1 = std::max(b.x1, l.left + l.width);
          }
          floats.push_back(b);
        }
      CHECK(!floats.empty());
      for (const ParaFrame& f : doc.layout.paras)
        for (const Fragment& l : f.lines) {
          if (l.kind != FragKind::Line || l.cellIdx >= 0) continue;
          const i64 y0 = (i64)f.y + l.y, y1 = y0 + l.height;
          for (const Box& b : floats) {
            const bool meets = y0 < b.y1 && b.y0 < y1 && l.left < b.x1 && b.x0 < l.left + l.width;
            if (meets) printf("FAIL float overlap: %s at %gpx, line y=%lld\n", name, width, (long long)y0);
            CHECK(!meets);
          }
        }
    }
}

// (plan P3-06) a preview fragment, the anchors' preview policy and a
// document's own id prefix
static void unitRenderFragment(const fs::path& root) {
  Doc doc;
  std::string ops, profile, src;
  readFile(root / "test" / "fixtures" / "notes" / "basic.ops", ops);
  readFile(root / "test" / "profiles" / "golden.json", profile);
  readFile(root / "test" / "fixtures" / "notes" / "basic.tsm", src);
  doc.configure(profile);
  doc.compile(src);
  CHECK(doc.ingest((const u8*)ops.data(), ops.size()) && typesetWithMock(doc));
  // the note's body as the semantic page writes it: no ids, no backlink
  const std::string f = doc.renderFragment("fn-1");
  CHECK(f.find("The note body carries <strong>markup</strong>") != std::string::npos);
  CHECK(f.find(" id=") == std::string::npos && f.find("fnref") == std::string::npos &&
        f.find("\xE2\x86\xA9") == std::string::npos);
  CHECK(doc.renderFragment("no-such-label").empty());
  // the anchors: a note previews, its marker does not; the head names the
  // prefix and the generation a fragment is stamped with
  auto headOf = [](const std::string& fr, JsonValue& h) {
    u32 hl = 0;
    if (fr.size() < 8) return false;
    std::memcpy(&hl, fr.data() + 4, 4);
    JsonReader rd;
    return rd.parse(fr.substr(8, hl), h);
  };
  JsonValue h;
  CHECK(headOf(doc.renderResult(nullptr, 0), h) && h.get("idPrefix") && h.get("idPrefix")->str == "tsr-");
  CHECK(h.get("generation") && (u64)h.get("generation")->num == doc.generation);
  int seen = 0;
  for (const JsonValue& a : h.get("anchors")->arr) {
    if (a.arr.size() != 4) continue;
    if (a.arr[0].str == "fn-1") seen += a.arr[2].str == "footnote" && a.arr[3].str == "block";
    if (a.arr[0].str == "fnref-1") seen += a.arr[3].str.empty();
    if (a.arr[0].str == "top") seen += a.arr[3].str.empty();
  }
  CHECK(seen == 3);
  // another prefix spells every id and internal href, in every backend
  CHECK(doc.configure(R"({"render": {"idPrefix": "a2-"}})") == Doc::kApplied);
  const std::string html = doc.render(), sem = doc.renderFallback(), paged = doc.renderPaged(600);
  for (const std::string* out : {&html, &sem, &paged}) {
    CHECK(out->find("id=\"a2-fn-1\"") != std::string::npos && out->find("href=\"#a2-fn-1\"") != std::string::npos);
    CHECK(out->find("tsr-fn") == std::string::npos);
  }
  CHECK(headOf(doc.renderResult(nullptr, 0), h) && h.get("idPrefix")->str == "a2-");
  // a render's prefix ends with it
  CHECK(AnchorNamer::current().prefix == kAnchorPrefix && !AnchorNamer::current().suppress);
  // (plan P3-31) another document's page is a link's URL: one outside the
  // policy (a project.urls value, a manifest's doc key) is none
  const std::unordered_map<std::string, std::string> urls{{"ch2", "ch2.html"}, {"bad", "javascript:alert(1)"}};
  {
    AnchorScope scope("p-", false, &urls);
    CHECK(AnchorNamer::href("ch2", "fig") == "ch2.html#p-fig");
    CHECK(AnchorNamer::href("ch3", "fig") == "ch3#p-fig");
    CHECK(AnchorNamer::href("bad", "fig") == "#p-fig");
    CHECK(AnchorNamer::href("vbscript:x", "fig") == "#p-fig");
  }
}

static void unitRenderResult(const fs::path& root) {
  Doc doc;
  std::string ops, profile, src;
  readFile(root / "test" / "fixtures" / "doc" / "structure.ops", ops);
  readFile(root / "test" / "profiles" / "golden.json", profile);
  readFile(root / "test" / "fixtures" / "doc" / "structure.tsm", src);
  doc.configure(profile);
  doc.compile(src);
  CHECK(doc.ingest((const u8*)ops.data(), ops.size()) && typesetWithMock(doc));
  struct Block {
    u32 pid, s0, s1, state;
    double h, gap;
    Key128 key;
    u32 off, len;
  };
  auto parse = [](const std::string& f, std::string& head, std::vector<Block>& bs, std::string& html) {
    if (f.size() < 8 || f.compare(0, 4, "TSRR") != 0) return false;
    u32 hl;
    std::memcpy(&hl, f.data() + 4, 4);
    head = f.substr(8, hl);
    size_t at = 8 + hl;
    u32 n;
    std::memcpy(&n, f.data() + at, 4);
    at += 4;
    bs.resize(n);
    for (Block& b : bs) {
      std::memcpy(&b.pid, f.data() + at, 4);
      std::memcpy(&b.s0, f.data() + at + 4, 4);
      std::memcpy(&b.s1, f.data() + at + 8, 4);
      std::memcpy(&b.state, f.data() + at + 12, 4);
      std::memcpy(&b.h, f.data() + at + 16, 8);
      std::memcpy(&b.gap, f.data() + at + 24, 8);
      std::memcpy(&b.key, f.data() + at + 32, 16);
      std::memcpy(&b.off, f.data() + at + 48, 4);
      std::memcpy(&b.len, f.data() + at + 52, 4);
      at += 56;
    }
    html = f.substr(at);
    return true;
  };
  std::string head, html;
  std::vector<Block> bs;
  CHECK(parse(doc.renderResult(nullptr, 0), head, bs, html) && !bs.empty());
  JsonValue h;
  JsonReader rd;
  CHECK(rd.parse(head, h) && h.get("gaps") && h.get("gaps")->arr.size() == bs.size());
  // a frame that sends every block holds the legacy body as it is
  CHECK(h.get("root")->str + "\n" + html + "</div>\n" == doc.render());
  // the shell's repositioning: a body stripped of its positional attributes,
  // then given (pid, s0, gap)
  std::string legacy = h.get("root")->str + "\n";
  for (size_t i = 0; i < bs.size(); i++) {
    // offsets in UTF-16 units → bytes
    auto byteAt = [&](u32 u16) {
      size_t b = 0;
      for (u32 u = 0; u < u16 && b < html.size();) {
        const unsigned char c = (unsigned char)html[b];
        const size_t w = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        u += w == 4 ? 2 : 1;
        b += w;
      }
      return b;
    };
    const size_t b0 = byteAt(bs[i].off), b1 = byteAt(bs[i].off + bs[i].len);
    std::string body = html.substr(b0, b1 - b0);
    {  // strip: ' data-pid="…" data-s0="…"' and ';margin-bottom:…'
      const size_t a = body.find(" data-pid=\""), z = body.find(" style=\"");
      body.erase(a, z - a);
      const size_t m = body.find(";margin-bottom:");
      if (m != std::string::npos) body.erase(m, body.find("\">") - m);
    }
    const std::string cls = "<div class=\"tsr-para\"";
    CHECK(body.compare(0, cls.size(), cls) == 0);
    std::string pos = " data-pid=\"" + std::to_string(bs[i].pid) + "\" data-s0=\"" + std::to_string(bs[i].s0) + "\"";
    body.insert(cls.size(), pos);
    const std::string gap = h.get("gaps")->arr[i].str;
    if (!gap.empty()) body.insert(body.find("\">"), ";margin-bottom:" + gap);
    legacy += body;
  }
  legacy += "</div>\n";
  CHECK(legacy == doc.render());
  // held: nothing sent, keys stable, a later generation
  std::vector<Key128> keys;
  for (const Block& b : bs) keys.push_back(b.key);
  std::string head2, html2;
  std::vector<Block> bs2;
  CHECK(parse(doc.renderResult(keys.data(), keys.size()), head2, bs2, html2) && html2.empty());
  for (size_t i = 0; i < bs.size() && i < bs2.size(); i++) CHECK(bs2[i].key == bs[i].key && bs2[i].len == 0);
  JsonValue g2;
  JsonReader rd2;
  CHECK(rd2.parse(head2, g2) && g2.get("generation")->num > h.get("generation")->num);
}

static void unitSettings() {
  {
    Config c;
    DiagSink d;
    SettingsPatch p = applySettings(c, R"({"host":{"width":420},"cost":{"exponent":2},
        "cjk":{"punctCompress":"full"},"code":{"fontFeaturesByLang":{"js":"\"liga\" 1"}},
        "terms":{"figure":"Fig. "},"doc":{"lang":"en"},"bogus":{"x":1},"$comment":"ignored"})", d);
    CHECK(p.ok && p.applied == 6);
    CHECK(c.widthPx == 420 && c.cost.exponent == 2 && c.punctCompress == PunctCompress::Full);
    CHECK(c.codeFontFeaturesByLang.at("js") == "\"liga\" 1");
    // terms.* override single words of the document language's locale pack
    Terms t(c);
    CHECK(c.lang == "en" && t.get("figure") == "Fig. " && t.get("table") == "Table ");
    CHECK(p.affects & stageBit(Stage::Emit));
    CHECK(d.items.size() == 1 && std::string_view(d.items[0].code) == "setting-unknown");
  }
  {
    Config c;
    DiagSink d;
    applySettings(c, R"({"host":{"width":-5},"cost":{"exponent":2.5},"fonts":{"body":"x;y"},
        "code":{"snapKerning":1},"cjk":{"punctCompress":"tight"}})", d);
    CHECK(d.items.size() == 5 && c.widthPx == 300 && c.cost.exponent == 3 && !c.verbatimSnapKerning);
    for (const Diag& x : d.items) CHECK(std::string_view(x.code) == "setting-type");
  }
  for (const char* bad : {"", "{", "[1]", "{\"a\":}", "{\"host\":{\"width\":1e999}}x"}) {
    Config c;
    DiagSink d;
    SettingsPatch p = applySettings(c, bad, d);
    CHECK(!p.ok && !d.items.empty() && std::string_view(d.items[0].code) == "setting-json");
  }
  {  // the effective document round-trips
    Config a;
    a.widthPx = 333;
    a.codeFontFeaturesByLang["cpp"] = "\"calt\" 0";
    Config b;
    DiagSink d;
    SettingsPatch p = applySettings(b, settingsJson(a), d);
    CHECK(p.ok && d.items.empty() && settingsJson(b) == settingsJson(a));
  }
}

// The breaker's item adapter (plan P0-12): legal breaks, discardables and the
// block each break consumes.
static void unitBreakItems() {
  auto blk = [](u16 flags, Su w, float pen, Su sw = 0, Su bw = 0) {
    BreakBlock b;
    b.flags = flags;
    b.width = w;
    b.breakPenalty = pen;
    b.spaceWidth = sw;
    b.breakWidth = bw;
    return b;
  };
  std::vector<BreakBlock> bl = {
      blk(0, 100, BREAK_INF),                            // 0 word piece
      blk(BF_HYPHEN, -3, 0.7f, 0, 40),                   // 1 hyphen point
      blk(0, 80, BREAK_INF),                             // 2 word piece
      blk(BF_SPACE, 20, 0, 20),                          // 3 space
      blk(BF_CJK, 1024, 0, 102),                         // 4 CJK char, breakable after
      blk(BF_SPACE | BF_BOUND, 10, 0.8f),                // 5 math break glue, penalty
      blk(BF_SPACE | BF_PUNCT_SP | BF_PUNCT_OPEN, 512, BREAK_INF),  // 6 rigid half
      blk(0, 300, 1.2f),                                 // 7 URL piece
  };
  std::vector<BItem> it;
  blocksToItems(bl, it);
  auto kinds = [&] {
    std::string s;
    for (const BItem& x : it)
      s += x.k == ItemKind::Box ? 'B' : x.k == ItemKind::Glue ? 'G'
         : x.k == ItemKind::Disc ? 'D' : x.tag == PenTag::Forbidden ? 'f' : 'p';
    return s;
  };
  CHECK(kinds() == "BDBGBfGppGfGBp");
  CHECK(it[1].w == -3 && it[1].pre == 40 && it[1].pen == 700);
  CHECK(it[5].k == ItemKind::Penalty && it[5].tag == PenTag::Forbidden && it[6].stretch == 102 &&
        it[7].pen == 0 && it[7].block == 4);
  CHECK(it[8].pen == 800 && it[8].block == 5 && it[9].w == 10);
  CHECK(it[13].pen == 1200 && it[13].block == 7);
  CHECK(penForbidden(BREAK_INF) && !penForbidden(1e17f) && penThousandths(0.95f) == 950);
}

// Breaker semantics (plans P0-12, P1-14; design T6 S1/S2).
static void unitBreakSemantics() {
  BreakParams cp;
  auto word = [](Su w, float pen = BREAK_INF) {
    BreakBlock b;
    b.width = w;
    b.breakPenalty = pen;
    return b;
  };
  auto space = [](Su w = 256) {
    BreakBlock b;
    b.flags = BF_SPACE;
    b.width = b.spaceWidth = w;
    return b;
  };
  {  // discard: the space at the break is not in the line — an exact fit is free
    std::vector<BreakBlock> bl = {word(4000), space(), word(4000), space(), word(4000),
                                      space(), word(9000)};
    BreakResult r = breakLines(bl, ParShape{4000 + 256 + 4000 + 256 + 4000}, cp);
    CHECK((r.breakpoints == std::vector<u32>{6, 7}) && r.cost == 0);
  }
  {  // a Forbidden (BREAK_INF) block is never a break; the rescue keeps the
     // overlong run on a line of its own instead of collapsing the paragraph
    std::vector<BreakBlock> bl = {word(3000), space(), word(30000), space(), word(3000),
                                      space(), word(30000), space(), word(3000)};
    BreakResult r = breakLines(bl, ParShape{19200}, cp);
    // the rescue breaks from the best active node (lowest demerits): the
    // short word joins its run rather than standing alone underfull
    CHECK(!r.feasible && r.pass == 3 && (r.breakpoints == std::vector<u32>{4, 8, 9}));
    CHECK((r.overfullLines == std::vector<u32>{0, 1}));
    BreakMemo memo;
    BreakResult c = breakLinesCached(bl, ParShape{19200}, cp, &memo);
    CHECK(c.breakpoints == r.breakpoints && c.overfullLines == r.overfullLines);
  }
  {  // the last line has fil stretch and normal shrink: slightly long is one line
    std::vector<BreakBlock> bl = {word(6000), space(), word(6000), space(), word(6800)};
    BreakResult r = breakLines(bl, ParShape{19200}, cp);  // 19312 > 19200, shrink 512
    CHECK((r.breakpoints == std::vector<u32>{5}));
  }
  {  // identical lines after discard report the latest break (the next line's
     // first block): CJK char, then a space — layout's trimmed range
    BreakBlock cjk = word(1024, 0);
    cjk.flags = BF_CJK;
    cjk.spaceWidth = 102;
    std::vector<BreakBlock> bl;
    for (int k = 0; k < 18; k++) bl.push_back(cjk);
    bl.push_back(space());
    bl.push_back(word(4000));
    BreakResult r = breakLines(bl, ParShape{18432}, cp);
    CHECK((r.breakpoints == std::vector<u32>{19, 20}));
  }
  {  // a Forced penalty breaks wherever it appears
    std::vector<BItem> it(5);
    it[0].k = ItemKind::Box; it[0].w = 1000; it[0].block = 0;
    it[1].k = ItemKind::Penalty; it[1].tag = PenTag::Forced; it[1].block = 0;
    it[2].k = ItemKind::Box; it[2].w = 1000; it[2].block = 1;
    it[3].k = ItemKind::Glue; it[3].w = it[3].stretch = it[3].shrink = 256; it[3].block = 2;
    it[4].k = ItemKind::Box; it[4].w = 1000; it[4].block = 3;
    BreakResult r = breakItems(it, 4, ParShape{19200}, cp);
    CHECK((r.breakpoints == std::vector<u32>{1, 4}));
  }
  {  // cost is bounded and the power is an integer product
    std::vector<BreakBlock> bl = {word(100), space(), word(100)};
    BreakResult r = breakLines(bl, ParShape{19200}, cp);
    CHECK(r.cost == 0);  // a short last line costs nothing (fil)
    BreakParams sq = cp;
    sq.cost.exponent = 2;
    std::vector<BreakBlock> two = {word(9000), space(), word(9000), space(), word(9000)};
    BreakResult a = breakLines(two, ParShape{18432}, sq);
    CHECK(a.cost >= 0 && a.cost <= sq.cost.cap * 2);
  }
}

// The KP memo (plans P0-11, P1-14) answers exactly what breakLines
// computes: keys are validated on hit, and eviction under many distinct
// streams only costs recomputation.
static void unitBreakMemo() {
  BreakMemo memo;
  memo.setBudget(20000 * 4);  // small: the second round must evict and recompute
  u64 seed = 12345;
  auto rnd = [&](u32 n) {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    return (u32)(seed >> 33) % n;
  };
  BreakParams cp;
  int mismatches = 0;
  for (int round = 0; round < 2; round++) {  // round 2: hits (or recomputes after eviction)
    seed = 12345;
#ifdef NDEBUG
    const int kParas = 9000;
#else
    const int kParas = 600;   // sanitizer builds: consistency only
#endif
    for (int p = 0; p < kParas; p++) {
      std::vector<BreakBlock> bl(40 + rnd(160));
      for (size_t i = 0; i < bl.size(); i++) {
        BreakBlock& b = bl[i];
        b.width = (Su)(64 * (2 + rnd(60)));
        b.spaceWidth = (i % 2) ? (Su)(64 * 4) : 0;
        b.breakWidth = 0;
        b.breakPenalty = (i % 2) ? 0.f : 1e9f;  // break at spaces only
      }
      bl.back().breakPenalty = 0;
      ParShape lw{(Su)(64 * (300 + rnd(200)))};
      BreakResult a = breakLinesCached(bl, lw, cp, &memo);
      BreakResult b = breakLines(bl, lw, cp);
      if (a.breakpoints != b.breakpoints || a.cost != b.cost || a.overfullLines != b.overfullLines) mismatches++;
      if (a.breakpoints.empty() || a.breakpoints.back() != bl.size()) mismatches++;
    }
  }
  CHECK(mismatches == 0);
}

// the shared HTML writer (plan P0-10): one style attribute, one escaper,
// ids through AnchorNamer, first-wins on a repeated attribute (release)
static void unitHtmlWriter() {
  std::string out;
  {
    Tag t(out, "span");
    t.attrSafe("class", "tsr-r");
    t.attr("lang", "en");
    t.style("font-size:14px");
    t.attrSafe("data-snap", "1");
    t.style("letter-spacing:0.5px");
    t.id("a\"<b");
    t.open();
  }
  CHECK(out == "<span class=\"tsr-r\" lang=\"en\" style=\"font-size:14px;letter-spacing:0.5px\" "
               "data-snap=\"1\" id=\"tsr-a&quot;&lt;b\">");
  CHECK(pxStr(1.0) == "1px" && pxStr(-0.1234) == "-0.123px" && pxStr(2.5) == "2.5px");
  static_assert(htmlAttrIndex("href") >= 0 && htmlAttrIndex("onclick") < 0 &&
                htmlAttrIndex("data-x") < 0);  // the allowlist is explicit
  out.clear();
  {  // a declaration after a later attribute still lands in the one style
    Tag t(out, "span");
    t.px("left", 1.5);
    t.attrSafe("data-snap", "1");
    t.px("letter-spacing", 0.25).declEsc("font-family", "\"A&B\"");
    t.open();
  }
  CHECK(out == "<span style=\"left:1.5px;letter-spacing:0.25px;font-family:&quot;A&amp;B&quot;\" "
               "data-snap=\"1\">");
#ifdef NDEBUG
  writerDefects() = {};
  out.clear();
  {
    Tag t(out, "a");
    t.attr("href", "#x");
    t.attr("href", "#y");
    t.open();
  }
  CHECK(out == "<a href=\"#x\">");
  CHECK(writerDefects().count == 1 && writerDefects().first == "<a> href");
  writerDefects() = {};
#endif
}

int main(int argc, char** argv) {
  std::string root;
  bool update = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--update") update = true;
    else root = a;
  }
  unitJslex();
  unitSpliceHead();
  unitSu();
  unitMock();
  unitMathFont();
  unitMathLayout();
  unitMathStretch();
  unitMathSegments();
  unitGrid();
  unitFragment();
  unitImageSrc();
  unitCrlf();
  unitNestLimit();
  unitCascade();
  unitRenderResult(fs::path(root));
  unitRenderFragment(fs::path(root));
  unitFloatsNeverOverlap(fs::path(root));
  unitPaginate();
  unitInstLimits();
  unitHtmlWriter();
  unitBreakMemo();
  unitBreakItems();
  unitSettings();
  unitFaces();
  unitBreakSemantics();

  if (root.empty()) {
    printf("%s\n", failures ? "UNIT FAILURES" : "unit ok (no fixture root given)");
    return failures ? 1 : 0;
  }

  // hyphenation vs the npm implementation's truth list
  {
    std::string truth;
    if (readFile(fs::path(root) / "test" / "golden" / "hyphen" / "words.txt", truth)) {
      size_t pos = 0;
      while (pos < truth.size()) {
        size_t eol = truth.find('\n', pos);
        if (eol == std::string::npos) eol = truth.size();
        std::string line = truth.substr(pos, eol - pos);
        pos = eol + 1;
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string word = line.substr(0, sp), expect = line.substr(sp + 1);
        std::vector<u32> pts = hyphenPoints(word);
        std::string got;
        u32 prev = 0;
        for (u32 p : pts) {
          got += word.substr(prev, p - prev);
          got += '-';
          prev = p;
        }
        got += word.substr(prev);
        if (got != expect) {
          printf("FAIL hyphen %s: got %s want %s\n", word.c_str(), got.c_str(), expect.c_str());
          failures++;
        }
      }
    }
  }

  fuzzRegressions(fs::path(root));
  unitHostInputs(fs::path(root));
  unitResources(fs::path(root));
  unitHostBoxes(fs::path(root));
  unitMathGlyphs(fs::path(root));
  unitMathIR();
  unitOpsWindow(fs::path(root));
  unitAstBytes(fs::path(root));
  unitTokenConformance(fs::path(root));
  unitRawMaps(fs::path(root));
  unitRegistry(fs::path(root));
  unitTextRules(root);
  unitClusters();
  unitOverlays();
  unitMathDict();
  unitFmtPx();

  fs::path fixtures = fs::path(root) / "test" / "fixtures";
  fs::path golden = fs::path(root) / "test" / "golden";
  loadXfail(golden / "XFAIL");
  int count = 0;
  if (fs::exists(fixtures)) {
    for (auto& entry : fs::recursive_directory_iterator(fixtures)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".tsm") continue;
      count++;
      fs::path rel = fs::relative(entry.path(), fixtures);
      std::string source;
      readFile(entry.path(), source);

      // configuration (plan P1-03): the fixture's profile (golden: 300px,
      // 16px base — testing.md §2, su-exact mock metrics) plus its own
      // X.fixture.json settings; no file-name conventions
      fs::path fxPath = entry.path();
      fxPath.replace_extension(".fixture.json");
      FixtureConfig fx;
      if (fs::exists(fxPath)) {
        std::string t;
        readFile(fxPath, t);
        fx = parseFixtureConfig(t);
      }
      std::string profile;
      readFile(fs::path(root) / "test" / "profiles" / (fx.profile + ".json"), profile);
      std::string label = rel.string();
      if (!fx.error.empty() || profile.empty()) {
        printf("FAIL %s: fixture configuration: %s\n", label.c_str(),
               fx.error.empty() ? "profile not found" : fx.error.c_str());
        failures++;
        continue;
      }
      auto hasProduct = [&](const char* p) {
        return std::find(fx.products.begin(), fx.products.end(), p) != fx.products.end();
      };
      Doc doc;
      doc.configure(profile);
      doc.configure(fx.settings);
      doc.compile(source);

      auto g = [&](const char* stage) {
        fs::path gp = golden / rel.parent_path() /
                      (rel.stem().string() + std::string(".") + stage + ".txt");
        return gp;
      };
      for (const char* p : {"skeleton", "ast", "lower", "js", "tokens", "outline", "astjson"})
        goldenCompare(g(p), doc.product(p), update, label + ":" + p);

      fs::path opsPath = entry.path();
      opsPath.replace_extension(".ops");
      if (fs::exists(opsPath)) {
        std::string ops;
        readFile(opsPath, ops);
        // (plan P3-31) its declared inputs: the fixture's labels manifests
        // (every document built from it below takes them too)
        std::string labelsIn;
        if (!fx.labels.empty()) {
          std::vector<std::string> files;
          for (const std::string& f : fx.labels) files.push_back((entry.path().parent_path() / f).string());
          std::string missing;
          if (!labelsInput(files, labelsIn, missing)) {
            printf("FAIL %s: cannot read input %s\n", label.c_str(), missing.c_str());
            failures++;
            continue;
          }
          doc.setInput("labels", labelsIn);
        }
        if (!doc.ingest((const u8*)ops.data(), ops.size())) {
          printf("FAIL %s: ops decode\n%s", label.c_str(), doc.dumpDiags().c_str());
          failures++;
          continue;
        }
        // recorded fixtures are valid by construction (plan P0-06): the
        // reader must not report value faults outside *diag* fixtures
        if (rel.stem().string().find("diag") == std::string::npos)
          for (const Diag& d : doc.diags.items)
            if (std::string_view(d.code) == "ops-invalid" || std::string_view(d.code) == "ops-arg") {
              printf("FAIL %s: %s %s\n", label.c_str(), d.code, d.msg.c_str());
              failures++;
            }
        goldenCompare(g("tree"), doc.product("tree"), update, label + ":tree");
        goldenCompare(g("index"), doc.product("index"), update, label + ":index");
        std::string semantic = doc.product("semantic");
        goldenCompare(g("semantic"), semantic, update, label + ":semantic");
        contractCheck(label, "semantic", semantic, false);
        if (!typesetWithMock(doc)) {
          printf("FAIL %s: typeset did not converge\n", label.c_str());
          failures++;
          continue;
        }
        for (const char* p : {"blocktree", "blocks", "hlist", "breaks", "layout", "vlist"})
          goldenCompare(g(p), doc.product(p), update, label + ":" + p);
        // the DisplayList dump (plan P1-18; a debug product): on request
        if (hasProduct("dl")) goldenCompare(g("dl"), doc.product("dl"), update, label + ":dl");
        // the HList contract (plan P1-12): every list passes the legality
        // lint (the legacy emitter's field-by-field oracle left with P4-02)
        {
          std::string lint;
          for (const TopBlock& tb : doc.tops)
            for (const FlowUnit& u : tb.units) {
              lint += lintHList(u.hl);
              for (const TableCell& c : u.cells) lint += lintHList(c.hl);
            }
          if (!lint.empty()) {
            printf("FAIL %s: hlist lint\n%s", label.c_str(), lint.c_str());
            failures++;
          }
          // the flatten table is closed (plan P1-13): no kind reaches an
          // inline stream unhandled — only a fixture about it may see one
          if (rel.stem().string().find("unsupported") == std::string::npos)
            for (const Diag& d : doc.diags.items)
              if (std::string_view(d.code) == "shape-unsupported") {
                printf("FAIL %s: %s %s\n", label.c_str(), d.code, d.msg.c_str());
                failures++;
              }
        }
        std::string mir = doc.product("mathir");  // plan P1-24: the IR of every formula
        if (!mir.empty() || fs::exists(g("mathir")))
          goldenCompare(g("mathir"), mir, update, label + ":mathir");
        std::string mbx = doc.product("mathbox");
        if (!mbx.empty() || fs::exists(g("mathbox")))
          goldenCompare(g("mathbox"), mbx, update, label + ":mathbox");
        std::string html = doc.product("html");
        goldenCompare(g("html"), html, update, label + ":html");
        // *diag* fixtures golden every diagnostic of the full pipeline (P0-09 m)
        if (rel.stem().string().find("diag") != std::string::npos)
          goldenCompare(g("diags"), doc.product("diags"), update, label + ":diags");
        {  // (plan P3-37) the diagnostics JSON: the text product's rows, in order
          JsonValue dj;
          JsonReader rd;
          const std::string text = doc.product("diags");
          std::vector<std::string> codes;
          for (size_t a = 0; a < text.size();) {
            size_t e = text.find('\n', a);
            if (e == std::string::npos) e = text.size();
            const std::string line = text.substr(a, e - a);
            const size_t sp = line.find(' '), sp2 = line.find(' ', sp + 1);
            if (sp != std::string::npos) codes.push_back(line.substr(sp + 1, sp2 - sp - 1));
            a = e + 1;
          }
          bool same = rd.parse(doc.product("diagnostics"), dj) && dj.t == JsonValue::T::Arr && dj.arr.size() == codes.size();
          for (size_t k = 0; same && k < codes.size(); k++) same = dj.arr[k].get("code") && dj.arr[k].get("code")->str == codes[k];
          if (!same) {
            printf("FAIL %s: the diagnostics JSON is not the diags text's rows\n", label.c_str());
            failures++;
          }
        }
        contractCheck(label, "html", html, true);
        // a fixture may golden the print pagination (pages-design.md §2):
        // "products": ["paged"] with its page.height setting
        // the semantic page's stylesheet (rulesToCss, plan P3-01): "products": ["css"]
        if (hasProduct("css")) goldenCompare(g("css"), doc.product("css"), update, label + ":css");
        // (plan P3-30) the document's language and where it came from
        if (hasProduct("docinfo")) goldenCompare(g("docinfo"), doc.product("docinfo"), update, label + ":docinfo");
        // (plan P3-31) its labels product; a fixture's X.labels.json beside
        // it (another fixture's input) is that product
        if (hasProduct("labels")) {
          const std::string labels = doc.product("labels");
          goldenCompare(g("labels"), labels, update, label + ":labels");
          const fs::path manifest = entry.path().parent_path() / (rel.stem().string() + ".labels.json");
          std::string committed;
          if (fs::exists(manifest) && readFile(manifest, committed) && committed != labels) {
            if (update) {
              std::ofstream(manifest, std::ios::binary) << labels;
            } else {
              printf("FAIL %s: %s is not its labels product (tsr_tests --update)\n", label.c_str(),
                     manifest.filename().string().c_str());
              failures++;
            }
          }
        }
        // (pagination reports into the paged render's slice — keep-violated,
        // page-overflow, plan P3-12: the screen diagnostics are taken first)
        const std::string screenDiags = doc.product("diags");
        if (hasProduct("paged")) {
          std::string paged = doc.product("paged");
          goldenCompare(g("paged"), paged, update, label + ":paged");
          contractCheck(label, "paged", paged, true);
        }
        for (const Diag& d : doc.diags.items)  // the writer saw a serializer defect
          if (std::string_view(d.code) == "render-attr") {
            printf("FAIL %s: %s %s\n", label.c_str(), d.code, d.msg.c_str());
            failures++;
          }
        // warm == fresh (plan P1-21): the same document on a Session warmed
        // by every fixture before it reproduces the fresh build exactly
        {
          static Session warmSession;
          Doc warm;
          warm.attach(&warmSession);
          warm.configure(profile);
          warm.configure(fx.settings);
          warm.compile(source);
          if (!labelsIn.empty()) warm.setInput("labels", labelsIn);
          if (!warm.ingest((const u8*)ops.data(), ops.size()) || !typesetWithMock(warm) ||
              warm.product("html") != html || warm.product("diags") != screenDiags ||
              warm.product("breaks") != doc.product("breaks")) {
            printf("FAIL %s: a warm Session differs from a fresh build\n", label.c_str());
            failures++;
          }
        }
        // fork == fresh (plan P1-03): a fork without a patch reproduces the
        // document, and a width patch equals a fresh build at that width
        // (with a warm metric store: the copy must be transparent)
        {
          Doc same;
          if (!doc.forkInto(same, "{}") || !typesetWithMock(same) ||
              same.product("html") != html || same.product("diags") != screenDiags) {
            printf("FAIL %s: fork differs from its source\n", label.c_str());
            failures++;
          }
          Doc narrow, fresh;
          fresh.configure(profile);
          fresh.configure(fx.settings);
          fresh.configure("{\"host\":{\"width\":260}}");
          fresh.compile(source);
          if (!labelsIn.empty()) fresh.setInput("labels", labelsIn);
          bool ok = doc.forkInto(narrow, "{\"host\":{\"width\":260}}") && typesetWithMock(narrow) &&
                    fresh.ingest((const u8*)ops.data(), ops.size()) && typesetWithMock(fresh);
          if (!ok || narrow.product("html") != fresh.product("html") ||
              narrow.product("diags") != fresh.product("diags")) {
            printf("FAIL %s: fork at 260px differs from a fresh build\n", label.c_str());
            failures++;
          }
          // relayout in place (plan P1-16): a width patch on a typeset
          // document re-enters Layout only and equals the fresh build
          Doc live;
          bool inPlace = doc.forkInto(live, "{}") && typesetWithMock(live) &&
                         live.configure("{\"host\":{\"width\":260}}") == Doc::kApplied &&
                         live.done(Stage::Measure) && typesetWithMock(live);
          if (!inPlace || live.product("html") != fresh.product("html") ||
              live.product("diags") != fresh.product("diags")) {
            printf("FAIL %s: relayout in place at 260px differs from a fresh build\n", label.c_str());
            failures++;
          }
        }
      }
    }
  }
  for (const std::string& k : xfail)
    if (!xfailSeen.count(k)) {
      printf("XPASS contract %s: remove it from test/golden/XFAIL\n", k.c_str());
      failures++;
    }
  printf("%d fixtures, %d failures, %d xfail\n", count, failures, xfailCount);
  return failures ? 1 : 0;
}
