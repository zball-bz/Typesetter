// Native test runner: unit tests + golden tests over test/fixtures.
//   tsr_tests <repo Typesetter dir> [--update]
// Goldens live at test/golden/<area>/<name>.<stage>.txt.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../src/api/doc.h"
#include "../src/hyphen/hyphen.h"
#include "../src/inline/jslex.h"
#include "../src/math/math.h"
#include "../src/math/mathfont.h"
#include "../src/measure/mock.h"

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

// --- golden runner ---
static bool typesetWithMock(Doc& doc) {
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

  fs::path fixtures = fs::path(root) / "test" / "fixtures";
  fs::path golden = fs::path(root) / "test" / "golden";
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
        goldenCompare(g("tree"), dumpTree(doc.tree, doc.strs, doc.styles), update, label + ":tree");
        goldenCompare(g("semantic"), doc.renderFallback(), update, label + ":semantic");
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
        goldenCompare(g("html"), doc.render(), update, label + ":html");
      }
    }
  }
  printf("%d fixtures, %d failures\n", count, failures);
  return failures ? 1 : 0;
}
