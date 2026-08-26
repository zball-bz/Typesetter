// Document handle: owns all stage products; resumable typeset loop
// (architecture §2.4). Single-threaded; one pipeline state per handle.
#pragma once
#include "../ast/ast.h"
#include "../code/tokens.h"
#include "../inline/fragment.h"
#include "../codegen/codegen.h"
#include "../resolve/resolve.h"
#include "../layout/layout.h"
#include "../render/semantic_html.h"
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

  // NEED_TOKENS pull state (code-design.md §2): codeblocks with a language
  // and a plain body wait for a token provider before emit.
  struct TokenReq {
    u32 id = 0;
    ContentNode* node = nullptr;
    StrRef lang = 0, body = 0;
    bool provided = false;
  };
  std::vector<TokenReq> tokenReqs;

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
    decodeOps(buf, len, raw, diags);  // in place: raw.strings view raw.blob
    if (!raw.ok) return false;
    tree = instantiate(raw, arena, strs, styles, diags);
    extractSidecars(tree.root);
    resolveDoc(tree, arena, strs, styles, cfg, diags);
    tokenReqs.clear();
    scanTokenReqs(tree.root);
    emitted = false;
    laidOut = false;
    return true;
  }

  // verbatim-design §5: split each code line at the fence-declared marker;
  // the code part re-forms the body (what the tokenizer will see), the
  // comment part parses as an inline fragment into a trailing
  // group{role:"sidecar-lines"} child — one seq per logical line.
  void extractSidecars(ContentNode* n) {
    if (!n) return;
    if (n->kind == Kind::codeblock && n->kids.size() == 1 &&
        n->kids[0]->kind == Kind::text) {
      StrRef mk = 0;
      for (const ArgVal& a : n->args)
        if (a.key == ArgK::sidecar && a.tag == ArgTag::Str) mk = a.ref;
      std::string marker(mk ? strs.get(mk) : std::string_view{});
      if (!marker.empty()) {
        std::string_view body = strs.get(n->kids[0]->str);
        StyleId st = n->kids[0]->style;
        Span sp = n->kids[0]->span;
        std::string codeBody;
        ContentNode* group = arena.make<ContentNode>();
        group->kind = Kind::group;
        group->span = sp;
        group->style = n->style;
        {
          ArgVal a;
          a.key = ArgK::role;
          a.tag = ArgTag::Str;
          a.ref = strs.intern("sidecar-lines");
          group->args.push_back(a);
        }
        bool any = false;
        size_t pos = 0;
        while (pos <= body.size()) {
          size_t eol = body.find('\n', pos);
          if (eol == std::string_view::npos) eol = body.size();
          std::string_view line = body.substr(pos, eol - pos);
          size_t cut = line.find(marker);
          ContentNode* seq = arena.make<ContentNode>();
          seq->kind = Kind::seq;
          seq->span = sp;
          seq->style = n->style;
          if (cut != std::string_view::npos) {
            std::string_view code = line.substr(0, cut);
            while (!code.empty() && (code.back() == ' ' || code.back() == '\t'))
              code.remove_suffix(1);
            std::string_view note = line.substr(cut + marker.size());
            while (!note.empty() && note.front() == ' ') note.remove_prefix(1);
            codeBody.append(code);
            if (!note.empty()) {
              auto ns2 = parseInlineFragment(note, st, sp, arena, strs, styles, diags);
              seq->kids.assign(ns2.begin(), ns2.end());
              any = true;
            }
          } else {
            codeBody.append(line);
          }
          group->kids.push_back(seq);
          if (eol == body.size()) break;
          codeBody += '\n';
          pos = eol + 1;
        }
        if (any) {
          n->kids[0]->str = strs.intern(codeBody);
          n->kids.push_back(group);
        }
      }
    }
    for (ContentNode* k : n->kids) extractSidecars(k);
  }

  void scanTokenReqs(ContentNode* n) {
    if (!n) return;
    if (n->kind == Kind::codeblock && !n->kids.empty() &&
        n->kids[0]->kind == Kind::text) {
      StrRef lang = 0;
      for (const ArgVal& a : n->args)
        if (a.key == ArgK::lang && a.tag == ArgTag::Str) lang = a.ref;
      if (lang && !strs.get(lang).empty()) {
        TokenReq r;
        r.id = (u32)tokenReqs.size();
        r.node = n;
        r.lang = lang;
        r.body = n->kids[0]->str;
        tokenReqs.push_back(r);
      }
    }
    for (ContentNode* k : n->kids) scanTokenReqs(k);
  }

  bool tokensPending() const {
    for (const TokenReq& r : tokenReqs)
      if (!r.provided) return true;
    return false;
  }

  // provider contract: EVERY request must be answered (empty = plain code),
  // mirroring the measurement loop. Tokens sorted, non-overlapping.
  void provideTokens(u32 id, const CodeToken* toks, size_t n) {
    if (id >= tokenReqs.size() || tokenReqs[id].provided) return;
    TokenReq& r = tokenReqs[id];
    r.provided = true;
    if (n > 0) foldTokens(r.node, toks, n, arena, strs, styles);
    emitted = false;
  }

  Status typeset() {
    if (tokensPending()) return Status::NeedMeasure;
    if (!emitted) {
      tops = emitDoc(tree, arena, strs, styles, cfg, diags);
      emitted = true;
    }
    MeasureRequest missing = resolveWidths(tops, metrics, styles, cfg);
    if (!missing.empty()) return Status::NeedMeasure;
    // PoC retry ladder: a narrow measure can starve the ±5 cursor window
    // of feasible transitions; widen until a finite solution appears.
    auto breakWithRetry = [&](const std::vector<LinebreakBlock>& blocks, LineWidths lw) {
      BreakResult r = breakLines(blocks, lw, cfg.cost);
      if (r.cost >= 1e17) {
        for (u32 range : {10u, 20u, 50u, 0xFFFFFFFFu}) {
          r = breakLines(blocks, lw, cfg.cost, range);
          if (r.cost < 1e17) break;
        }
      }
      return r;
    };
    for (TopBlock& tb : tops) {
      for (FlowUnit& u : tb.units) {
        if (u.kind == FlowUnit::K::Table && u.tCols > 0) {
          // equal columns (v1); each cell breaks to its content width
          Su colW = (suFloorPx(cfg.widthPx) - u.indent) / (Su)u.tCols;
          Su pad = suRoundPx(kTableCellPadEm * cfg.baseSizePx);
          Su cellW = colW - 2 * pad;
          if (cellW < 64) cellW = 64;
          for (TableCell& c : u.cells) {
            BreakResult r = breakWithRetry(c.blocks, LineWidths{cellW});
            c.breakpoints = std::move(r.breakpoints);
            c.breakCost = r.cost;
          }
          continue;
        }
        if (u.kind == FlowUnit::K::Code && !u.cells.empty() && u.sidebarW > 0) {
          for (TableCell& c : u.cells) {
            BreakResult r = breakWithRetry(c.blocks, LineWidths{u.sidebarW});
            c.breakpoints = std::move(r.breakpoints);
            c.breakCost = r.cost;
          }
          continue;
        }
        if (u.kind != FlowUnit::K::Text) continue;
        LineWidths lw{suFloorPx(cfg.widthPx) - u.indent};
        BreakResult r = breakWithRetry(u.blocks, lw);
        u.breakpoints = std::move(r.breakpoints);
        u.breakCost = r.cost;
      }
    }
    layout = layoutDoc(tops, metrics, strs, cfg);
    laidOut = true;
    return Status::Ok;
  }

  MeasureRequest pendingRequests() { return resolveWidths(tops, metrics, styles, cfg); }

  std::string render() { return renderTypeset(tops, layout, styles, strs, cfg); }

  // needs only the post-resolve tree — valid before any measurement
  std::string renderFallback() { return renderSemantic(tree, strs); }

  // width-only relayout (architecture §2.4): metrics persist, the next
  // typeset() re-breaks and re-lays out at the new measure
  void setWidth(double widthPx) { cfg.widthPx = widthPx; laidOut = false; }

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
