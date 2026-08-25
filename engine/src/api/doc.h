// Document handle: owns all stage products; resumable typeset loop
// (architecture §2.4). Single-threaded; one pipeline state per handle.
#pragma once
#include "../ast/ast.h"
#include "../codegen/codegen.h"
#include "../layout/layout.h"
#include "../render/typeset_html.h"

namespace tsr {

struct Doc {
  Config cfg;
  Arena arena;
  Interner strs{arena};
  DiagSink diags;

  SourceText src;
  Skeleton skel;
  AstNode* ast = nullptr;
  JsProgram js;

  RawOps raw;
  StyleTable styles;
  ContentTree tree;

  std::vector<TopBlock> tops;
  bool emitted = false;
  MetricStore metrics;
  LayoutResult layout;
  bool laidOut = false;

  enum class Status { Ok, NeedMeasure };

  void compile(std::string source) {
    src.init(std::move(source));
    skel = linepass(src, arena, diags);
    ast = parseDoc(src, skel, arena, strs, diags);
    js = codegen(ast, src, strs);
  }

  bool ingest(const u8* buf, size_t len) {
    raw = decodeOps(buf, len, diags);
    if (!raw.ok) return false;
    tree = instantiate(raw, arena, strs, styles, diags);
    emitted = false;
    laidOut = false;
    return true;
  }

  Status typeset() {
    if (!emitted) {
      tops = emitDoc(tree, strs, styles, cfg);
      emitted = true;
    }
    MeasureRequest missing = resolveWidths(tops, metrics, styles, cfg);
    if (!missing.empty()) return Status::NeedMeasure;
    for (TopBlock& tb : tops) {
      for (FlowUnit& u : tb.units) {
        if (u.kind != FlowUnit::K::Text) continue;
        LineWidths lw{suFloorPx(cfg.widthPx) - u.indent};
        BreakResult r = breakLines(u.blocks, lw, cfg.cost);
        // PoC retry ladder: a narrow measure can starve the ±5 cursor window
        // of feasible transitions; widen until a finite solution appears.
        if (r.cost >= 1e17) {
          for (u32 range : {10u, 20u, 50u, 0xFFFFFFFFu}) {
            r = breakLines(u.blocks, lw, cfg.cost, range);
            if (r.cost < 1e17) break;
          }
        }
        u.breakpoints = std::move(r.breakpoints);
        u.breakCost = r.cost;
      }
    }
    layout = layoutDoc(tops, metrics, cfg);
    laidOut = true;
    return Status::Ok;
  }

  MeasureRequest pendingRequests() { return resolveWidths(tops, metrics, styles, cfg); }

  std::string render() { return renderTypeset(tops, layout, styles, strs, cfg); }

  std::string dumpDiags() const {
    std::string out;
    for (const Diag& d : diags.items) {
      const char* sev = d.sev == Sev::Error ? "error" : d.sev == Sev::Warning ? "warning" : "info";
      appendf(out, "%s %s @[%u,%u) ", sev, d.code, d.span.start, d.span.end);
      out += d.msg;
      out += "\n";
    }
    return out;
  }
};

}  // namespace tsr
