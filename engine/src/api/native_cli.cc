// tsrc — stage inspection CLI (architecture §2.1).
//   tsrc --stage=skeleton|ast|js|diags <file.tsm>
//   tsrc --stage=ops|tree|blocks|breaks|layout|html --ops=<file.ops> [--width=300] <file.tsm>
// Post-ops stages use the normative mock measurer.
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../measure/mock.h"
#include "doc.h"

using namespace tsr;

static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static bool typesetWithMock(Doc& doc) {
  for (int i = 0; i < 64; i++) {
    Doc::Status st = doc.typeset();
    if (st == Doc::Status::Ok) return true;
    MeasureRequest req = doc.pendingRequests();
    if (req.empty()) return false;
    mockProvide(req, doc.metrics, doc.strs, doc.styles, doc.cfg);
  }
  return false;
}

int main(int argc, char** argv) {
  std::string stage = "ast", opsPath, file, punct;
  double width = 300, indentEm = 0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a.rfind("--stage=", 0) == 0) stage = a.substr(8);
    else if (a.rfind("--ops=", 0) == 0) opsPath = a.substr(6);
    else if (a.rfind("--width=", 0) == 0) width = atof(a.c_str() + 8);
    else if (a.rfind("--indent=", 0) == 0) indentEm = atof(a.c_str() + 9);
    else if (a.rfind("--punct=", 0) == 0) punct = a.substr(8);
    else file = a;
  }
  if (file.empty()) {
    fprintf(stderr, "usage: tsrc --stage=<stage> [--ops=f.ops] [--width=px] file.tsm\n");
    return 2;
  }
  std::string source;
  if (!readFile(file, source)) {
    fprintf(stderr, "cannot read %s\n", file.c_str());
    return 2;
  }

  Doc doc;
  doc.cfg.widthPx = width;
  doc.cfg.paraIndentEm = indentEm;
  if (punct == "full") doc.cfg.punctCompress = PunctCompress::Full;
  else if (punct == "none") doc.cfg.punctCompress = PunctCompress::None;
  else if (punct == "book" || punct.empty()) doc.cfg.punctCompress = PunctCompress::Book;
  doc.compile(std::move(source));

  std::string out;
  if (stage == "skeleton") out = dumpSkeleton(doc.skel, doc.src);
  else if (stage == "ast") out = dumpAst(doc.ast, doc.src, doc.strs);
  else if (stage == "js") out = doc.js.text;
  else if (stage == "diags") out = doc.dumpDiags();
  else {
    if (opsPath.empty()) {
      fprintf(stderr, "stage %s needs --ops=\n", stage.c_str());
      return 2;
    }
    std::string ops;
    if (!readFile(opsPath, ops)) {
      fprintf(stderr, "cannot read %s\n", opsPath.c_str());
      return 2;
    }
    if (!doc.ingest((const u8*)ops.data(), ops.size())) {
      fprintf(stderr, "ops decode failed:\n%s", doc.dumpDiags().c_str());
      return 1;
    }
    if (stage == "ops") out = dumpOps(doc.raw);
    else if (stage == "tree") out = dumpTree(doc.tree, doc.strs, doc.styles);
    else {
      if (!typesetWithMock(doc)) {
        fprintf(stderr, "typeset did not converge\n");
        return 1;
      }
      if (stage == "blocks") out = dumpBlocks(doc.tops, doc.strs, doc.styles);
      else if (stage == "breaks") out = dumpBreaks(doc.tops);
      else if (stage == "layout") out = dumpLayout(doc.layout);
      else if (stage == "html") out = doc.render();
      else {
        fprintf(stderr, "unknown stage %s\n", stage.c_str());
        return 2;
      }
    }
  }
  fwrite(out.data(), 1, out.size(), stdout);
  return 0;
}
