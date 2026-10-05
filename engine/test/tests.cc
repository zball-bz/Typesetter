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
#include "../src/hyphen/hyphen.h"
#include "../src/inline/jslex.h"
#include "../src/math/math.h"
#include "../src/code/grid.h"
#include "../src/inline/fragment.h"
#include "../src/math/mathfont.h"
#include "native_tokens.h"
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
  std::string s = "avg(3,5)\xE7\x9A\x84";  // avg(3,5)的
  CHECK(scanSpliceHead(s, 0) == 8);       // ASCII cut before 的
  std::string dot = "x. next";
  CHECK(scanSpliceHead(dot, 0) == 1);  // '.' not followed by ident start
  std::string chain = "a.b.c(1).d";
  CHECK(scanSpliceHead(chain, 0) == chain.size());
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
  using namespace mathfont;
  // constants sanity against fontTools-inspected values (Euler-Math 0.75)
  CHECK(kUpem == 1000);
  CHECK(mathConst(C::AxisHeight) == 250);
  CHECK(mathConst(C::DisplayOperatorMinHeight) == 1130);
  CHECK(mathConst(C::ScriptPercentScaleDown) > 0 &&
        mathConst(C::ScriptPercentScaleDown) <= 100);
  CHECK(kMinConnectorOverlap == 20);
  // glyph lookup
  const GlyphRec* x = mathGlyph('x');
  CHECK(x && x->adv > 0 && x->asc > 0);
  CHECK(mathGlyph(0x2211) != nullptr);           // ∑
  CHECK(mathGlyph(0x10FFFF) == nullptr);
  // dictionary
  const OpEntry* sum = mathOp("sum");
  CHECK(sum && sum->cp == 0x2211 && sum->cls == kOp &&
        (sum->flags & kFlagLarge) && (sum->flags & kFlagLimits));
  const OpEntry* arrow = mathOp("->");
  CHECK(arrow && arrow->cp == 0x2192 && arrow->cls == kRel);
  const OpEntry* nn = mathOp("NN");
  CHECK(nn && nn->cp == 0x2115);
  const OpEntry* lim = mathOp("lim");
  CHECK(lim && lim->cp == 0 && (lim->flags & kFlagTextOp) && (lim->flags & kFlagLimits));
  CHECK(mathOp("nonexistent") == nullptr);
  // variant chain: '(' has a growing chain plus a 3-part assembly
  const VarChain* paren = mathChain('(', /*vertical=*/true);
  CHECK(paren && paren->n >= 4 && paren->asmN == 3);
  CHECK(mathChain('x', true) == nullptr);
  // every chain/assembly cp has a glyph record (renderer paints by cp)
  for (int i = 0; i < kVertChainCount; i++) {
    const VarChain& c = kVertChains[i];
    for (int k = 0; k < c.n; k++) CHECK(mathGlyph(kVariantCps[c.off + k]) != nullptr);
    for (int k = 0; k < c.asmN; k++) CHECK(mathGlyph(kAsmParts[c.asmOff + k].cp) != nullptr);
  }
  // su conversion: 1em at 16px = 1024 su
  CHECK(mathSu(1000, 16) == 1024);
  CHECK(mathSu(250, 16) == 256);  // axis height = 4px
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

// --- golden runner ---
static bool typesetWithMock(Doc& doc) {
  provideNativeTokens(doc);
  // NEED_IMAGES stub (figure-design.md §6): any src measures 512x384
  for (auto& ir : doc.imageReqs) doc.provideImage(ir.id, 512, 384);
  for (int i = 0; i < 64; i++) {
    if (doc.typeset() == Doc::Status::Ok) return true;
    MeasureRequest req = doc.pendingRequests();
    if (req.empty()) return false;
    mockProvide(req, doc.metrics, doc.strs, doc.styles, doc.cfg);
  }
  return false;
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

      Doc doc;
      doc.cfg.widthPx = 300;
      doc.cfg.baseSizePx = 16;  // testing.md §2: su-exact mock metrics
      // conventions: *indent* fixtures run with the CJK 2em first-line
      // indent; *punct-full* / *punct-none* select those compression modes
      if (rel.stem().string().find("indent") != std::string::npos)
        doc.cfg.paraIndentEm = 2;
      if (rel.stem().string().find("punct-full") != std::string::npos)
        doc.cfg.punctCompress = PunctCompress::Full;
      else if (rel.stem().string().find("punct-none") != std::string::npos)
        doc.cfg.punctCompress = PunctCompress::None;
      // *snap* fixtures enable verbatim snap-kerning; *base18* runs at 18px
      if (rel.stem().string().find("snap") != std::string::npos)
        doc.cfg.verbatimSnapKerning = true;
      if (rel.stem().string().find("base18") != std::string::npos)
        doc.cfg.baseSizePx = 18;
      doc.compile(source);

      auto g = [&](const char* stage) {
        fs::path gp = golden / rel.parent_path() /
                      (rel.stem().string() + std::string(".") + stage + ".txt");
        return gp;
      };
      std::string label = rel.string();
      goldenCompare(g("skeleton"), dumpSkeleton(doc.skel, doc.src), update, label + ":skeleton");
      goldenCompare(g("ast"), dumpAst(doc.ast, doc.src, doc.strs), update, label + ":ast");
      goldenCompare(g("js"), doc.js.text, update, label + ":js");

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
        goldenCompare(g("tree"), dumpTree(doc.tree, doc.strs, doc.styles), update, label + ":tree");
        std::string semantic = doc.renderFallback();
        goldenCompare(g("semantic"), semantic, update, label + ":semantic");
        contractCheck(label, "semantic", semantic, false);
        if (!typesetWithMock(doc)) {
          printf("FAIL %s: typeset did not converge\n", label.c_str());
          failures++;
          continue;
        }
        goldenCompare(g("blocks"), dumpBlocks(doc.tops, doc.strs, doc.styles), update,
                      label + ":blocks");
        goldenCompare(g("breaks"), dumpBreaks(doc.tops), update, label + ":breaks");
        goldenCompare(g("layout"), dumpLayout(doc.layout), update, label + ":layout");
        std::string mbx = dumpMathBoxes(doc.tops, doc.strs);
        if (!mbx.empty() || fs::exists(g("mathbox")))
          goldenCompare(g("mathbox"), mbx, update, label + ":mathbox");
        std::string html = doc.render();
        goldenCompare(g("html"), html, update, label + ":html");
        contractCheck(label, "html", html, true);
        // *paged* fixtures additionally golden the print pagination
        // (pages-design.md §2) at 240px sheets
        if (rel.stem().string().find("paged") != std::string::npos) {
          std::string paged = doc.renderPaged(240);
          goldenCompare(g("paged"), paged, update, label + ":paged");
          contractCheck(label, "paged", paged, true);
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
