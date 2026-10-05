// Document handle: owns all stage products; resumable typeset loop
// (architecture §2.4). Single-threaded; one pipeline state per handle.
#pragma once
#include "../ast/ast.h"
#include "../code/sidecars.h"
#include "../code/tokens.h"
#include "../inline/fragment.h"
#include "../codegen/codegen.h"
#include "../resolve/resolve.h"
#include "../layout/layout.h"
#include "../render/html_writer.h"
#include "../render/semantic_html.h"
#include "../render/typeset_html.h"
#include "../syntax/exports.h"

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
  // the element registry (plan P1-10): the built-in rows unless a host or a
  // test swaps in another before ingest; the Index resolve leaves behind
  const Registry* registry = &Registry::builtin();
  Index index;

  // NEED_TOKENS pull state (code-design.md §2): codeblocks with a language
  // and a plain body wait for a token provider before emit.
  struct TokenReq {
    u32 id = 0;
    ContentNode* node = nullptr;
    StrRef lang = 0, body = 0;
    bool provided = false;
  };
  std::vector<TokenReq> tokenReqs;

  // NEED_IMAGES pull state (figure-design.md §2): image nodes without
  // intrinsic dims wait for the host — the engine wants CSS px, not pixels.
  struct ImageReq {
    u32 id = 0;
    ContentNode* node = nullptr;
    StrRef src = 0;
    Span span;  // the node's, else its innermost spanned ancestor's (diagnostics)
    bool provided = false;
  };
  std::vector<ImageReq> imageReqs;

  std::vector<TopBlock> tops;
  // measurement faces (plan P1-04): the metric key; bound in the constructor
  FaceTable faces;
  MetricStore metrics;
  // text-font runs inside formulas (math-design.md §10): emit reports the
  // words whose body-font metrics are still missing; they ride the next
  // measure request and the document re-emits when they arrive
  std::vector<MeasureItem> mathTextMissing;
  LayoutResult layout;

  // ---- stage model (plan P1-03; stages.def, docs/host-protocol-design.md) --
  // Every stage up to validThrough has its product; invalidateFrom drops the
  // later ones. Replaces the old emitted/laidOut flags.
  int validThrough = -1;
  bool done(Stage s) const { return validThrough >= (int)s; }
  void invalidateFrom(Stage s) {
    if (validThrough >= (int)s) validThrough = (int)s - 1;
  }
  // the ops this document was ingested from: a fork rebuilds from them
  std::string opsBytes;
  // host answers, retained so a fork does not ask again
  struct TokenAnswer {
    std::string lang, body;
    std::vector<CodeToken> toks;
  };
  std::vector<TokenAnswer> tokenAnswers;
  std::vector<std::pair<std::string, std::pair<double, double>>> imageAnswers;  // src → w, h

  enum class Status { Ok, NeedMeasure };

  Doc() {
    faces.bind(&cfg, &styles, &strs);
    metrics.bind(&faces);
  }
  Doc(const Doc&) = delete;
  Doc& operator=(const Doc&) = delete;

  // Host settings (plan P1-03): one JSON document, applied in row order;
  // unknown paths and bad values are diagnostics of the Settings slice
  // (replaced by the next document). A patch whose first affected stage has
  // already run applies in place only if every stage from there on is
  // Reentrant; otherwise nothing changes and the caller rebuilds: REBUILD
  // (fork from the retained ops) or REEXECUTE (compile and run again).
  static constexpr int kApplied = 0, kRebuild = 4, kReexecute = 5;
  int configure(std::string_view json) {
    diags.begin(DiagOrigin::Settings);
    Config next = cfg;
    SettingsPatch p = applySettings(next, json, diags);
    if (!p.applied) return kApplied;
    const Stage first = firstStage(p.affects);
    if ((int)first > validThrough) {  // nothing it affects has run yet
      cfg = std::move(next);
      return kApplied;
    }
    if (first <= Stage::Execute) return kReexecute;
    for (int k = (int)first; k <= validThrough; k++)
      if (kStageRerun[k] != Rerun::Reentrant) return kRebuild;
    cfg = std::move(next);
    invalidateFrom(first);
    return kApplied;
  }

  // A new document from this one's retained ops with `patch` applied
  // (tsr2_doc_fork; relayout, paginate and any REBUILD patch). Strings and
  // styles are cloned first so ids — and therefore the metric answers,
  // copied unless the patch affects Measure — mean the same; token and image
  // answers replay. false: the patch needs re-execution (or there is
  // nothing to fork from).
  bool forkInto(Doc& f, std::string_view patch) const {
    if (!done(Stage::Ingest)) return false;
    f.cfg = cfg;
    f.diags.begin(DiagOrigin::Settings);
    SettingsPatch p = applySettings(f.cfg, patch, f.diags);
    if (p.applied && firstStage(p.affects) <= Stage::Execute) return false;
    for (const Diag& d : diags.items)  // compile happened once, for both
      if (d.origin == DiagOrigin::Compile) f.diags.items.push_back(d);
    for (StrRef r = 1; r < (StrRef)strs.count(); r++) f.strs.intern(strs.get(r));
    f.styles = styles;
    if (!(p.affects & stageBit(Stage::Measure))) {  // faces and answers stay valid
      f.faces = faces;
      f.faces.bind(&f.cfg, &f.styles, &f.strs);
      f.metrics = metrics;
      f.metrics.bind(&f.faces);
    }
    f.validThrough = (int)Stage::Execute;
    if (!f.ingest((const u8*)opsBytes.data(), opsBytes.size())) return true;
    for (const TokenReq& r : f.tokenReqs) {
      std::string_view lang = f.strs.get(r.lang), body = f.strs.get(r.body);
      for (const TokenAnswer& a : tokenAnswers)
        if (a.lang == lang && a.body == body) {
          f.provideTokens(r.id, a.toks.data(), a.toks.size());
          break;
        }
    }
    for (const ImageReq& r : f.imageReqs) {
      std::string_view src = f.strs.get(r.src);
      for (const auto& [s, wh] : imageAnswers)
        if (s == src) {
          f.provideImage(r.id, wh.first, wh.second);
          break;
        }
    }
    return true;
  }


  void compile(std::string source) {
    diags.begin(DiagOrigin::Compile);
    validThrough = (int)Stage::Compile;
    src.init(std::move(source));
    skel = linepass(src, arena, diags);
    ast = parseDoc(src, skel, arena, strs, diags);
    js = codegen(ast, src, strs);
  }

  // Ingest (decode + instantiate) and Resolve in one call, as hosts use it.
  bool ingest(const u8* buf, size_t len) {
    opsBytes.assign((const char*)buf, len);
    if (!stageIngest()) return false;
    stageResolve();
    return true;
  }
  bool stageIngest() {
    diags.begin(DiagOrigin::Ingest);
    decodeOps((const u8*)opsBytes.data(), opsBytes.size(), raw, diags);  // raw views raw.blob
    validThrough = std::min(validThrough, (int)Stage::Execute);
    if (!raw.ok) return false;
    tree = instantiate(raw, arena, strs, styles, diags, *registry);
    scanScriptErrors(tree.root);
    validThrough = (int)Stage::Ingest;
    return true;
  }
  // Resolve (once: it rewrites the instantiated tree): sidecars, references,
  // numbering, then the host needs it raises (tokens, image sizes)
  void stageResolve() {
    diags.begin(DiagOrigin::Resolve);
    extractSidecars(tree.root, arena, strs, styles, diags);
    resolveDoc(tree, arena, strs, styles, cfg, diags, *registry, index);
    tokenReqs.clear();
    scanTokenReqs(tree.root);
    // the engine tokenizes its own language (plan P1-09; code-design §2):
    // a 'tsm' code block never waits for the host
    for (const TokenReq& r : std::vector<TokenReq>(tokenReqs))
      if (strs.get(r.lang) == "tsm") {
        std::vector<CodeToken> toks = syntaxTokens(strs.get(r.body));
        provideTokens(r.id, toks.data(), toks.size());
      }
    imageReqs.clear();
    scanImageReqs(tree.root);
    validThrough = (int)Stage::Resolve;
  }

  // Execution errors arrive as error nodes (plan P0-05): report the ones the
  // executor produced (script-error / script-syntax) as diagnostics at their
  // block span. Parse errors were reported at compile time. Interim until
  // the DIAG op carries executor diagnostics (plan P2-01).
  void scanScriptErrors(const ContentNode* n) {
    if (!n) return;
    if (n->kind == Kind::error) {
      std::string_view code, msg;
      for (const ArgVal& a : n->args) {
        if (a.tag != ArgTag::Str) continue;
        if (a.key == ArgK::code) code = strs.get(a.ref);
        if (a.key == ArgK::message) msg = strs.get(a.ref);
      }
      if (code == "script-error")
        diags.add(Sev::Error, "script-error", n->span, std::string(msg));
      else if (code == "script-syntax")
        diags.add(Sev::Error, "script-syntax", n->span, std::string(msg));
    }
    for (const ContentNode* k : n->kids) scanScriptErrors(k);
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

  void scanImageReqs(ContentNode* n, Span outer = {}) {
    if (!n) return;
    if (!n->span.empty()) outer = n->span;
    if (n->kind == Kind::image) {
      double iw = 0, ih = 0;
      StrRef src = 0;
      for (const ArgVal& a : n->args) {
        if (a.key == ArgK::w && a.tag == ArgTag::Num) iw = a.num;
        if (a.key == ArgK::h && a.tag == ArgTag::Num) ih = a.num;
        if (a.key == ArgK::src && a.tag == ArgTag::Str) src = a.ref;
      }
      // author-declared dims (or an empty/unsafe src) skip the pull; the
      // unsafe case renders the placeholder without ever fetching
      if (src && !(iw > 0 && ih > 0) && safeImageSrc(strs.get(src))) {
        ImageReq r;
        r.id = (u32)imageReqs.size();
        r.node = n;
        r.src = src;
        r.span = outer;
        imageReqs.push_back(r);
      }
    }
    for (ContentNode* k : n->kids) scanImageReqs(k, outer);
  }

  bool imagesPending() const {
    for (const ImageReq& r : imageReqs)
      if (!r.provided) return true;
    return false;
  }

  // provider contract: EVERY request must be answered; 0×0 = load failure
  // (placeholder + warning), mirroring the token loop.
  void provideImage(u32 id, double wPx, double hPx) {
    if (id >= imageReqs.size() || imageReqs[id].provided) return;
    ImageReq& r = imageReqs[id];
    r.provided = true;
    auto setNum = [&](ArgK k, double v) {
      for (ArgVal& a : r.node->args)
        if (a.key == k) {
          a.tag = ArgTag::Num;
          a.num = v;
          return;
        }
      ArgVal a;
      a.key = k;
      a.tag = ArgTag::Num;
      a.num = v;
      r.node->args.push_back(a);
    };
    if (std::isfinite(wPx) && std::isfinite(hPx) && wPx > 0 && hPx > 0) {
      // intrinsic dims fill only what the author left out (defect #24,
      // D-H01 interim): a declared w (or h) stays and the other side
      // follows the image's aspect ratio
      double aw = 0, ah = 0;
      for (const ArgVal& a : r.node->args) {
        if (a.key == ArgK::w && a.tag == ArgTag::Num) aw = a.num;
        if (a.key == ArgK::h && a.tag == ArgTag::Num) ah = a.num;
      }
      if (aw > 0) setNum(ArgK::h, aw * hPx / wPx);
      else if (ah > 0) setNum(ArgK::w, ah * wPx / hPx);
      else {
        setNum(ArgK::w, wPx);
        setNum(ArgK::h, hPx);
      }
    } else {
      diags.addAs(DiagOrigin::Provide, Sev::Warning, "image-load", r.span,
                  "image failed to load: " + std::string(strs.get(r.src)));
    }
    imageAnswers.push_back({std::string(strs.get(r.src)), {wPx, hPx}});
    invalidateFrom(Stage::Emit);
  }

  // provider contract: EVERY request must be answered (empty = plain code),
  // mirroring the measurement loop. Tokens sorted, non-overlapping.
  // Host input is checked here (plan P0-11): a token with an unknown tag, an
  // empty or out-of-body range, a boundary inside a UTF-8 sequence, or out
  // of order / overlapping is dropped (its text stays plain).
  void provideTokens(u32 id, const CodeToken* toks, size_t n) {
    if (id >= tokenReqs.size() || tokenReqs[id].provided) return;
    TokenReq& r = tokenReqs[id];
    r.provided = true;
    std::string_view body = strs.get(r.body);
    auto boundary = [&](u32 at) {
      return at == body.size() || (at < body.size() && ((u8)body[at] & 0xC0) != 0x80);
    };
    std::vector<CodeToken> ok;
    ok.reserve(n);
    u32 covered = 0;
    for (size_t i = 0; i < n; i++) {
      const CodeToken& t = toks[i];
      if (t.tag >= kTokenTagCount || t.start >= t.end || t.end > body.size() ||
          t.start < covered || !boundary(t.start) || !boundary(t.end))
        continue;
      ok.push_back(t);
      covered = t.end;
    }
    tokenAnswers.push_back({std::string(strs.get(r.lang)), std::string(body), ok});
    if (!ok.empty()) foldTokens(r.node, ok.data(), ok.size(), arena, strs, styles);
    invalidateFrom(Stage::Emit);
  }

  // Drives Emit → Measure → Break → Layout as far as the host's answers
  // allow; NeedMeasure = see pendingRequests(). Earlier stages are the
  // caller's (compile, execute, ingest).
  Status typeset() {
    if (!done(Stage::Resolve)) return Status::NeedMeasure;
    if (tokensPending() || imagesPending()) return Status::NeedMeasure;
    if (done(Stage::Layout)) return Status::Ok;
    if (!done(Stage::Emit)) {
      diags.begin(DiagOrigin::Emit);  // a re-emit replaces its diagnostics
      mathTextMissing.clear();
      MathTextCtx mt{&metrics, &styles, &strs, cfg.baseSizePx, &mathTextMissing};
      tops = emitDoc(tree, arena, strs, styles, cfg, diags, &mt);
      // formulas with unmeasured text-font names laid out with stand-ins:
      // ask for the metrics and emit again once they are here
      if (!mathTextMissing.empty()) return Status::NeedMeasure;
      validThrough = (int)Stage::Emit;
    }
    if (!done(Stage::Measure)) {
      MeasureRequest missing = resolveWidths(tops, metrics, styles, cfg);
      if (!missing.empty()) return Status::NeedMeasure;
      validThrough = (int)Stage::Measure;
    }
    // Cached KP with the retry ladder folded in (break.cc): keyed by block
    // geometry, shared across documents — the editing loop's fast path.
    // a run wider than the line is set Overfull on a line of its own (the
    // final-pass rescue) and reported once per stream (plan P0-12)
    diags.begin(DiagOrigin::Layout);
    auto breakWithRetry = [&](const std::vector<LinebreakBlock>& blocks, LineWidths lw) {
      BreakResult r = breakLinesRetry(blocks, lw, cfg.cost);
      if (!r.overfullLines.empty()) {
        Span sp{};
        for (const LinebreakBlock& b : blocks)
          if (!b.span.empty()) {
            if (sp.empty()) sp = b.span;
            sp.start = std::min(sp.start, b.span.start);
            sp.end = std::max(sp.end, b.span.end);
          }
        diags.add(Sev::Warning, "overfull-line", sp,
                  std::to_string(r.overfullLines.size()) +
                      " line(s) hold a run wider than the measure");
      }
      return r;
    };
    // F2 float tracker (figure-design.md §4): walks units in reading order,
    // mirroring layout's gap accounting; decisions are STORED on the units
    // (narrow/narrowK/floatClearSu) so layout replays instead of re-deriving.
    const Su baseLeading = suRoundPx(cfg.lineHeight * cfg.baseSizePx);
    const Su paraGapSu = suRoundPx(cfg.paraSpacingEm * cfg.baseSizePx);
    const Su emGapSu = suRoundPx(cfg.baseSizePx);
    i64 flRemain = 0;  // occlusion height left, measured from the cursor
    Su flOccl = 0;
    u8 flSide = 0;
    bool firstBlock = true;
    for (TopBlock& tb : tops) {
      for (u32 ui = 0; ui < (u32)tb.units.size(); ui++) {
        FlowUnit& u = tb.units[ui];
        u.narrow = 0;
        u.narrowK = 0;
        u.narrowLeft = false;
        u.floatClearSu = 0;
        u.floatShiftSu = 0;
        // the gap layout will insert before this unit
        Su gapBefore = ui > 0 ? (u.tightAbove ? paraGapSu / 3 : paraGapSu)
                              : (firstBlock ? 0 : paraGapSu);
        if (flRemain > 0) {
          flRemain -= gapBefore;
          if (flRemain < 0) flRemain = 0;
        }
        if (u.kind == FlowUnit::K::Image && u.floatSide != 0) {
          // a float arriving while one is active: same side → STACK it
          // below (the text keeps flowing beside both; occlusion extends,
          // width = the wider); other side → clear the previous one
          // (real-world-report.md: Wikipedia opens with two thumbnails)
          i64 shift = 0;
          if (flRemain > 0 && flSide == u.floatSide) {
            shift = flRemain;
            u.floatShiftSu = (Su)flRemain;
          } else if (flRemain > 0) {
            u.floatClearSu = (Su)flRemain;
            flRemain = 0;
            flOccl = 0;
          }
          for (TableCell& c : u.cells) {  // caption breaks to the float width
            BreakResult r = breakWithRetry(c.blocks, LineWidths{u.imgW});
            c.breakpoints = std::move(r.breakpoints);
            c.breakCost = r.cost;
            c.overfullLines = std::move(r.overfullLines);
          }
          i64 capH = 0;
          for (const TableCell& c : u.cells)
            capH += (i64)c.breakpoints.size() * baseLeading;
          flRemain = shift + (i64)u.imgH + capH + paraGapSu;  // + one gap of clearance
          Su occl = u.imgW + emGapSu;
          flOccl = shift > 0 && flOccl > occl ? flOccl : occl;
          flSide = u.floatSide;
          continue;
        }
        if (u.kind != FlowUnit::K::Text && flRemain > 0) {
          // every non-text unit clears the float (figure-design.md §4)
          u.floatClearSu = (Su)flRemain;
          flRemain = 0;
          flOccl = 0;
        }
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
            c.overfullLines = std::move(r.overfullLines);
          }
          continue;
        }
        if (u.kind == FlowUnit::K::Code && !u.cells.empty() && u.sidebarW > 0) {
          for (TableCell& c : u.cells) {
            BreakResult r = breakWithRetry(c.blocks, LineWidths{u.sidebarW});
            c.breakpoints = std::move(r.breakpoints);
            c.breakCost = r.cost;
            c.overfullLines = std::move(r.overfullLines);
          }
          continue;
        }
        if (u.kind != FlowUnit::K::Text) continue;
        LineWidths lw{suFloorPx(cfg.widthPx) - u.indent};
        if (flRemain > 0 && flOccl > 0 && flOccl < lw.constant - 64) {
          lw.narrow = lw.constant - flOccl;
          lw.narrowK = (u32)((flRemain + baseLeading - 1) / baseLeading);
          u.narrow = lw.narrow;
          u.narrowK = lw.narrowK;
          u.narrowLeft = flSide == 1;
        }
        BreakResult r = breakWithRetry(u.blocks, lw);
        u.breakpoints = std::move(r.breakpoints);
        u.breakCost = r.cost;
        u.overfullLines = std::move(r.overfullLines);
        if (flRemain > 0) {
          flRemain -= (i64)u.breakpoints.size() * baseLeading;
          if (flRemain < 0) flRemain = 0;
        }
      }
      firstBlock = false;
    }
    layout = layoutDoc(tops, metrics, strs, cfg);
    validThrough = (int)Stage::Layout;
    return Status::Ok;
  }

  MeasureRequest pendingRequests() {
    MeasureRequest r = resolveWidths(tops, metrics, styles, cfg);
    for (const MeasureItem& it : mathTextMissing) {
      if (!metrics.hasFaceWord(it.str, it.face)) r.words.push_back(it);
      if (!metrics.hasFaceVmet(it.face)) {
        bool have = false;
        for (FaceId f : r.vmetFaces) have = have || f == it.face;
        if (!have) r.vmetFaces.push_back(it.face);
      }
    }
    return r;
  }

  // ---- products (products.def) ------------------------------------------
  // The stage a product needs; false = unknown product.
  static bool productStage(std::string_view name, Stage& st) {
#define PRODUCT(n, stage) \
  if (name == #n) {       \
    st = Stage::stage;    \
    return true;          \
  }
#include "products.def"
#undef PRODUCT
    return false;
  }
  // The text of a product of this document as it stands (the caller has
  // driven it far enough: productStage). `paged` renders sheets of
  // page.height px.
  std::string product(std::string_view name) {
    if (name == "skeleton") return dumpSkeleton(skel, src);
    if (name == "ast") return dumpAst(ast, src, strs);
    if (name == "js") return js.text;
    if (name == "tokens") return dumpTokens(syntaxTokens(ast, src, strs), src);
    if (name == "outline") return outlineJson(ast, src, strs, diags) + "\n";
    if (name == "astjson") return astJson(ast, src, strs) + "\n";
    if (name == "ops") return dumpOps(raw);
    if (name == "tree") return dumpTree(tree, strs, styles);
    if (name == "index") return dumpIndex(index, *registry);
    if (name == "semantic") return renderFallback();
    if (name == "mathbox") return dumpMathBoxes(tops, strs);
    if (name == "blocks") return dumpBlocks(tops, strs, styles);
    if (name == "breaks") return dumpBreaks(tops);
    if (name == "layout") return dumpLayout(layout);
    if (name == "paged") return renderPaged(cfg.pageHeightPx);
    if (name == "html") return render();
    if (name == "diags") return dumpDiags();
    if (name == "settings") return settingsJson(cfg) + "\n";
    return {};
  }

  std::string render() {
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    std::string html = renderTypeset(tops, layout, styles, strs, cfg);
    reportWriterDefects();
    return html;
  }

  // paged rendering for print (pages-design.md §2); needs a finished layout
  std::string renderPaged(double pageHeightPx) {
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    std::string html = renderPages(tops, layout, styles, strs, cfg, pageHeightPx);
    reportWriterDefects();
    return html;
  }

  // needs only the post-resolve tree — valid before any measurement
  std::string renderFallback() {
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    std::string html = renderSemantic(tree, strs, styles);
    reportWriterDefects();
    return html;
  }

  // a repeated / unlisted attribute reached the HTML writer (a serializer
  // defect; debug builds assert at the call site)
  void reportWriterDefects() {
    const WriterDefects& d = writerDefects();
    if (d.count)
      diags.add(Sev::Error, "render-attr", {},
                std::to_string(d.count) + " attribute defect(s), first: " + d.first);
  }

  // relayout (architecture §2.4): metrics persist; emit bakes width-
  // dependent products (image display boxes, sidecar columns), so the next
  // typeset() re-emits, re-breaks and re-lays out at the new measure
  // (defect #16; P1-16 takes width out of emit)
  // (deprecated: hosts fork with a host.width patch instead — tsr2_doc_fork)
  void setWidth(double widthPx) {
    if (widthPx == cfg.widthPx) return;
    cfg.widthPx = widthPx;
    invalidateFrom(Stage::Emit);
  }

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
