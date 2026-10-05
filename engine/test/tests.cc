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
#include "../src/break/items.h"
#include "../src/hyphen/hyphen.h"
#include "../src/inline/jslex.h"
#include "../src/math/math.h"
#include "../src/code/grid.h"
#include "../src/inline/fragment.h"
#include "../src/syntax/lexer.h"
#include "../src/syntax/exports.h"
#include "../src/semantic/terms.h"
#include "semantic_data.gen.h"
#include "../src/math/dict.h"
#include "../src/math/font.h"
#include "../src/api/driver.h"
#include "../src/emit/legacy.h"
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
  auto segs = layoutMathSegments("a + b = c", false, 16, a, strs, d, {});
  CHECK(segs.size() == 4);           // [a +][b][=][c]
  CHECK(segs[1].brkBefore == 3);     // after-Bin
  CHECK(segs[2].brkBefore == 2);     // before-Rel
  CHECK(segs[3].brkBefore == 1);     // after-Rel
  CHECK(segs[2].glueBefore > 0 && segs[3].glueBefore > 0);  // thick glue
  auto neg = layoutMathSegments("-x", false, 16, a, strs, d, {});
  CHECK(neg.size() == 1);            // unary minus demoted: no break
  auto disp = layoutMathSegments("a + b", true, 16, a, strs, d, {});
  CHECK(disp.size() == 1);           // display formulas never segment
  auto opq = layoutMathSegments("(a = b)", false, 16, a, strs, d, {});
  CHECK(opq.size() == 1);            // groups are opaque
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

static void unitFragment() {
  Arena a;
  Interner strs{a};
  StyleTable styles;
  DiagSink d;
  auto ns = parseInlineFragment(
      "\xE5\x9D\x87\xE6\x91\x8A *O* \xE5\xA4\x8D\xE6\x9D\x82\xE5\xBA\xA6 $n log n$ \xE8\xA7\x81 @sec \xE5\x92\x8C [doc](https://x) \xE4\xB8\x8E `code`",
      0, {5, 9}, a, strs, styles, d);
  int kinds[8] = {0};
  std::function<void(const ContentNode*)> walk = [&](const ContentNode* n) {
    if (n->kind == Kind::text) kinds[0]++;
    if (n->kind == Kind::mathinline) kinds[1]++;
    if (n->kind == Kind::ref) kinds[2]++;
    if (n->kind == Kind::link) kinds[3]++;
    if (n->kind == Kind::code) kinds[4]++;
    for (const ContentNode* k : n->kids) walk(k);
  };
  for (const ContentNode* n : ns) walk(n);
  CHECK(kinds[1] == 1 && kinds[2] == 1 && kinds[3] == 1 && kinds[4] == 1);
  CHECK(kinds[0] >= 4);
  // bold bits folded into the leaf style
  bool sawBold = false;
  for (const ContentNode* n : ns)
    if (n->kind == Kind::text && (styles.get(n->style).bits & CLS_BOLD))
      sawBold = true;
  CHECK(sawBold);
  // every node stamped with the caller's span
  CHECK(!ns.empty() && ns[0]->span.start == 5 && ns[0]->span.end == 9);
  // splice stays literal with an Info diag
  DiagSink d2;
  auto ns2 = parseInlineFragment("x #toc y", 0, {}, a, strs, styles, d2);
  bool lit = false;
  for (const ContentNode* n : ns2)
    if (n->kind == Kind::text &&
        strs.get(n->str).find("#toc") != std::string::npos)
      lit = true;
  CHECK(lit);
  CHECK(!d2.items.empty() && d2.items[0].sev == Sev::Info);
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
    WireBatch empty;
    empty.batch = doc.rt.batch.id;
    encodeWire(empty, true, ans);
    CHECK(doc.provide((const u8*)ans.data(), ans.size()));
    CHECK(doc.rt.boxNeeds[0].st == ResState::Failed && has(doc, "provider-missing") && has(doc, "image-load"));
    // the measure batch: one width answered NaN, the rest missing
    CHECK(doc.typeset() == Doc::Status::NeedMeasure);
    doc.requests(req);
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
    // per-block deferral (plan P1-20): the paragraph is emitted while the
    // figure waits for its image, and its widths join the image's round
    Doc doc;
    CHECK(fresh(doc) && doc.typeset() == Doc::Status::NeedMeasure);
    CHECK(doc.emitted.size() == 2 && doc.emitted[0] && !doc.emitted[1]);
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

// The ops version window (plan P1-01): MIN_COMPAT..OPS_VERSION is read, the
// buffer remembers its version, anything outside the window is refused.
// Conformance (b) of plan P1-09: the tree-sitter grammar (the editor's
// cold-start fallback) against the engine's tokens, byte by byte over every
// fixture's non-blank bytes. The two may differ only in the listed ways —
// the grammar is a line-oriented regex approximation — and must agree on
// most of the text.
static void unitTokenConformance(const fs::path& root) {
  // (engine tag, tree-sitter tag); "-" = no token. Reasons:
  //   jslex     splice heads / JS arguments / statements (no JS lexer)
  //   pairs     strict pairs and footnotes (regex pairs, opaque ^[…])
  //   lines     islands, links and comments across lines or nested
  //   blocks    fences in containers, escaped markers, region bars
  static const char* kAllowed[][3] = {
      {"-", "attribute", "pairs"},         {"-", "embedded", "blocks"},
      {"-", "function", "jslex"},          {"-", "keyword", "blocks"},
      {"-", "operator", "blocks"},         {"-", "string", "lines"},
      {"-", "type", "lines"},              {"attribute", "-", "pairs"},
      {"comment", "-", "lines"},           {"constant", "-", "pairs"},
      {"constant", "attribute", "pairs"},  {"embedded", "-", "jslex"},
      {"embedded", "function", "jslex"},   {"embedded", "keyword", "jslex"},
      {"function", "-", "jslex"},          {"function", "keyword", "jslex"},
      {"function", "label", "jslex"},      {"keyword", "-", "blocks"},
      {"keyword", "embedded", "blocks"},   {"label", "function", "lines"},
      {"label", "keyword", "lines"},       {"property", "-", "lines"},
      {"punctuation", "-", "jslex"},       {"punctuation", "type", "lines"},
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
  double pct = bytes ? 100.0 * same / bytes : 100;
  CHECK(pct >= 85.0);
  printf("unit: token conformance %.1f%% of %ld non-blank bytes\n", pct, bytes);
}

// TextRules (plan P1-11): the one classifier reproduces, over every
// codepoint, the five classifiers it replaced (literal copies below, frozen
// here); the mock measurer's wide ranges are pinned to the same literal; the
// UCD columns read the pinned 17.0.0 data.
// fmtPxBuf (perf, ahead of plan P1-12): the integer formatter is printf's "%.3f" with
// trailing zeros trimmed — exact ties (k/16, k/1024), negatives that round
// to zero, subnormals, large values, random doubles.
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

static void unitTextRules() {
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
    bool ok = isWide(cp) == cjk && isOpenPunct(cp) == op && isClosePunct(cp) == cl &&
              isIdeo(cp) == (cjk && !op && !cl) &&
              joinsWide(cp) == (cjk || cp == 0x2014 || cp == 0x2026) &&
              kernEligible(cp) == !(cjk || cp >= 0x2000) &&
              isAmbDashOrEllipsis(cp) == (cp == 0x2014 || cp == 0x2026) &&
              isAmbQuote(cp) == (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) &&
              mockIsWide(cp) == cjk;
    if (!ok && bad++ < 5) printf("FAIL textrules: U+%04X classifies differently\n", cp);
  }
  CHECK(bad == 0);
  CHECK(RULES_VERSION == 0 && std::string_view(UNICODE_VERSION) == "17.0.0");
  CHECK(cpInfo(0x1F600).extPict && cpInfo(0x4E00).eaw == EAW::W && cpInfo(0x41).eaw == EAW::Na);
  CHECK(cpInfo(0x0301).gcb == GCB::Extend && cpInfo(0x1F1E6).gcb == GCB::Regional_Indicator &&
        cpInfo(0x1100).gcb == GCB::L && cpInfo(0x0D).gcb == GCB::CR && cpInfo(0x200D).gcb == GCB::ZWJ);
  CHECK(cpInfo(0x4E00).cc == CC::Ideo && cpInfo(0x3002).cc == CC::FullStopW && cpInfo(0x41).cc == CC::Other);
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

  std::string json(kElementsJson);
  size_t cls = json.find("\"classes\"");
  size_t fig = json.find("\"figure\": {", cls);
  CHECK(cls != std::string::npos && fig != std::string::npos);
  json.replace(fig, 8, "\"illustration\"");
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
    auto run = [&](const Registry* r, std::string& index) {
      Doc d;
      d.registry = r;
      d.configure(profile);
      d.compile(src);
      d.ingest((const u8*)buf.data(), buf.size());
      ProviderSet p = mockProviders();
      driveToCompletion(d, p);
      index = d.product("index");
      return d.product("tree") + d.product("html") + d.product("diags");
    };
    std::string i1, i2;
    std::string a = run(&reg, i1), b = run(renamed.get(), i2);
    CHECK(a == b);
    size_t at;
    while ((at = i2.find("illustration")) != std::string::npos) i2.replace(at, 12, "figure");
    CHECK(i1 == i2 && i1.find("instance figure") != std::string::npos);
  }
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
  readFile(root / "test" / "fixtures" / "inline" / "emph.ops", ops);
  CHECK(ops.size() > 5 && (u8)ops[4] == OPS_MIN_COMPAT);  // today's buffers: v6
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
    } else {
      doc.compile(data);
      (void)dumpAst(doc.ast, doc.src, doc.strs);
    }
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
  red.bits |= CLS_LINK | CLS_UNDER;
  CHECK(doc.faces.faceOf(doc.styles.idOf(red)) == doc.faces.faceOf(plain));
  Styling bold = base;
  bold.bits |= CLS_BOLD;
  CHECK(doc.faces.faceOf(doc.styles.idOf(bold)) != doc.faces.faceOf(plain));
  Styling cjkItalic = base;
  cjkItalic.bits |= CLS_CJK | CLS_EM;
  Styling cjk = base;
  cjk.bits |= CLS_CJK;
  CHECK(doc.faces.faceOf(doc.styles.idOf(cjkItalic)) == doc.faces.faceOf(doc.styles.idOf(cjk)));
  Styling monoCjk = base;
  monoCjk.bits |= CLS_CODE | CLS_CJK;
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
    BreakResult r = breakLines(bl, LineWidths{4000 + 256 + 4000 + 256 + 4000}, cp);
    CHECK((r.breakpoints == std::vector<u32>{6, 7}) && r.cost == 0);
  }
  {  // a Forbidden (BREAK_INF) block is never a break; the rescue keeps the
     // overlong run on a line of its own instead of collapsing the paragraph
    std::vector<BreakBlock> bl = {word(3000), space(), word(30000), space(), word(3000),
                                      space(), word(30000), space(), word(3000)};
    BreakResult r = breakLines(bl, LineWidths{19200}, cp);
    // the rescue breaks from the best active node (lowest demerits): the
    // short word joins its run rather than standing alone underfull
    CHECK(!r.feasible && r.pass == 3 && (r.breakpoints == std::vector<u32>{4, 8, 9}));
    CHECK((r.overfullLines == std::vector<u32>{0, 1}));
    BreakMemo memo;
    BreakResult c = breakLinesCached(bl, LineWidths{19200}, cp, &memo);
    CHECK(c.breakpoints == r.breakpoints && c.overfullLines == r.overfullLines);
  }
  {  // the last line has fil stretch and normal shrink: slightly long is one line
    std::vector<BreakBlock> bl = {word(6000), space(), word(6000), space(), word(6800)};
    BreakResult r = breakLines(bl, LineWidths{19200}, cp);  // 19312 > 19200, shrink 512
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
    BreakResult r = breakLines(bl, LineWidths{18432}, cp);
    CHECK((r.breakpoints == std::vector<u32>{19, 20}));
  }
  {  // a Forced penalty breaks wherever it appears
    std::vector<BItem> it(5);
    it[0].k = ItemKind::Box; it[0].w = 1000; it[0].block = 0;
    it[1].k = ItemKind::Penalty; it[1].tag = PenTag::Forced; it[1].block = 0;
    it[2].k = ItemKind::Box; it[2].w = 1000; it[2].block = 1;
    it[3].k = ItemKind::Glue; it[3].w = it[3].stretch = it[3].shrink = 256; it[3].block = 2;
    it[4].k = ItemKind::Box; it[4].w = 1000; it[4].block = 3;
    BreakResult r = breakItems(it, 4, LineWidths{19200}, cp);
    CHECK((r.breakpoints == std::vector<u32>{1, 4}));
  }
  {  // cost is bounded and the power is an integer product
    std::vector<BreakBlock> bl = {word(100), space(), word(100)};
    BreakResult r = breakLines(bl, LineWidths{19200}, cp);
    CHECK(r.cost == 0);  // a short last line costs nothing (fil)
    BreakParams sq = cp;
    sq.cost.exponent = 2;
    std::vector<BreakBlock> two = {word(9000), space(), word(9000), space(), word(9000)};
    BreakResult a = breakLines(two, LineWidths{18432}, sq);
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
      LineWidths lw{(Su)(64 * (300 + rnd(200)))};
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
  unitMathGlyphs(fs::path(root));
  unitOpsWindow(fs::path(root));
  unitAstBytes(fs::path(root));
  unitTokenConformance(fs::path(root));
  unitRegistry(fs::path(root));
  unitTextRules();
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
      for (const char* p : {"skeleton", "ast", "js", "tokens", "outline", "astjson"})
        goldenCompare(g(p), doc.product(p), update, label + ":" + p);

      fs::path opsPath = entry.path();
      opsPath.replace_extension(".ops");
      if (fs::exists(opsPath)) {
        std::string ops;
        readFile(opsPath, ops);
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
        // the HList contract (plan P1-12): fuseLegacy equals, field by
        // field, what the legacy emitter makes of the same document, and
        // every list passes the legality lint
        {
          std::string d = fuseCheck(doc.tops, doc.boxtree, doc.arena, doc.strs, doc.styles, doc.cfg,
                                    doc.metrics, doc.cfg.baseSizePx);
          if (!d.empty()) {
            printf("FAIL %s: fuseLegacy differs from the legacy blocks\n%s", label.c_str(), d.c_str());
            failures++;
          }
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
        std::string mbx = doc.product("mathbox");
        if (!mbx.empty() || fs::exists(g("mathbox")))
          goldenCompare(g("mathbox"), mbx, update, label + ":mathbox");
        std::string html = doc.product("html");
        goldenCompare(g("html"), html, update, label + ":html");
        // *diag* fixtures golden every diagnostic of the full pipeline (P0-09 m)
        if (rel.stem().string().find("diag") != std::string::npos)
          goldenCompare(g("diags"), doc.product("diags"), update, label + ":diags");
        contractCheck(label, "html", html, true);
        // a fixture may golden the print pagination (pages-design.md §2):
        // "products": ["paged"] with its page.height setting
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
          if (!warm.ingest((const u8*)ops.data(), ops.size()) || !typesetWithMock(warm) ||
              warm.product("html") != html || warm.product("diags") != doc.product("diags") ||
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
              same.product("html") != html || same.product("diags") != doc.product("diags")) {
            printf("FAIL %s: fork differs from its source\n", label.c_str());
            failures++;
          }
          Doc narrow, fresh;
          fresh.configure(profile);
          fresh.configure(fx.settings);
          fresh.configure("{\"host\":{\"width\":260}}");
          fresh.compile(source);
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
