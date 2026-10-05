// Document handle: owns all stage products; resumable typeset loop
// (architecture §2.4). Single-threaded; one pipeline state per handle.
#pragma once
#include <limits>
#include <memory>
#include "../ast/ast.h"
#include "../code/sidecars.h"
#include "../code/tokens.h"
#include "../inline/fragment.h"
#include "../codegen/codegen.h"
#include "../resolve/resolve.h"
#include "../boxtree/build.h"
#include "../resource/resource_table.h"
#include "../resource/session.h"
#include "../layout/layout.h"
#include "../layout/paginate.h"
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

  // what the engine asks the host for after Ingest (plan P1-19;
  // resource/resources.def): code tokens and image sizes here, widths and
  // vertical metrics in the MetricStore
  ResourceTable rt;

  Session* session_ = nullptr;
  std::unique_ptr<Session> own_;
  std::vector<u32> sessionMk_;  // per FaceId: its session metric key, ~0u = not yet
  BoxTree boxtree;  // the block structure (plan P1-18)
  std::vector<TopBlock> tops;
  // measurement faces (plan P1-04): the metric key; bound in the constructor
  FaceTable faces;
  MetricStore metrics;
  // Emit per top-level block (plan P1-20; design T9 M5): a block waits while
  // a code block or image of it waits for its answer; the others are
  // emitted, and measured, meanwhile. Formulas never hold emit back: they lay
  // out in Measure (plan P1-25)
  std::vector<u8> emitted;                              // per top
  std::vector<std::vector<u32>> waitTokens, waitBoxes;  // per pid: its needs
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
  enum class Status { Ok, NeedMeasure };

  // ---- the Session (plan P1-21; design T9 A5) -----------------------------
  // The host's content-keyed answer cache and memo slots, shared by its
  // documents; a document nobody attached to one gets its own.
  Session& session() {
    if (!session_) {
      own_ = std::make_unique<Session>();
      session_ = own_.get();
      session_->refs++;
    }
    return *session_;
  }
  // before the document measures anything
  void attach(Session* s) {
    if (!s || s == session_) return;
    if (session_) session_->refs--;
    own_.reset();
    session_ = s;
    s->refs++;
    sessionMk_.clear();
  }
  // a face's metric key in the session: its canonical bytes (design T9 A1:
  // strings, f64 bit patterns with -0 and NaN normalised, fixed-width ints)
  u32 sessionMk(FaceId f) {
    if (f < sessionMk_.size() && sessionMk_[f] != ~0u) return sessionMk_[f];
    const FaceKey& k = faces.get(f);
    std::string b;
    auto str = [&](StrRef r) {
      std::string_view v = strs.get(r);
      const u32 n = (u32)v.size();
      b.append((const char*)&n, 4);
      b.append(v);
    };
    auto f64 = [&](double v) {
      if (v == 0) v = 0;  // -0
      if (v != v) v = std::numeric_limits<double>::quiet_NaN();
      b.append((const char*)&v, 8);
    };
    str(k.family);
    b.append((const char*)&k.faceDigest, 8);
    f64(k.sizePx);
    b.append((const char*)&k.weight, 2);
    b += (char)k.italic;
    b += (char)k.caps;
    str(k.features);
    str(k.lang);
    f64(k.dppx);
    if (sessionMk_.size() <= f) sessionMk_.resize((size_t)f + 1, ~0u);
    return sessionMk_[f] = session().metricKey(b);
  }
  struct Backing : MetricBacking {
    Doc* d;
    explicit Backing(Doc* doc) : d(doc) {}
    bool width(FaceId f, StrRef s, double& px) const override {
      return d->session().width(d->sessionMk(f), d->strs.get(s), px);
    }
    bool vmet(FaceId f, double& asc, double& desc) const override {
      return d->session().vmet(d->sessionMk(f), asc, desc);
    }
  } backing{this};

  Doc() {
    faces.bind(&cfg, &styles, &strs);
    metrics.bind(&faces);
    metrics.bindBacking(&backing);
  }
  ~Doc() {
    if (session_) session_->refs--;
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
    f.attach(session_);  // the fork shares its source's Session (plan P1-21)
    if (!(p.affects & stageBit(Stage::Measure))) {  // faces and answers stay valid
      f.faces = faces;
      f.faces.bind(&f.cfg, &f.styles, &f.strs);
      f.metrics = metrics;
      f.metrics.bind(&f.faces);
      f.metrics.bindBacking(&f.backing);
    }
    f.validThrough = (int)Stage::Execute;
    if (!f.ingest((const u8*)opsBytes.data(), opsBytes.size())) return true;
    // settled answers replay by key (content-keyed: the same key, the same
    // answer), so the fork does not ask again
    for (u32 i = 0; i < f.rt.tokenNeeds.size(); i++) {
      const TokenNeed& t = f.rt.tokenNeeds[i];
      if (t.st != ResState::Pending) continue;
      const TokenNeed* src = rt.tokens(strs.find(f.strs.get(t.lang)), strs.find(f.strs.get(t.body)));
      if (src && src->st != ResState::Pending) f.settleTokens(i, src->toks.data(), src->toks.size(), src->st);
    }
    for (u32 i = 0; i < f.rt.boxNeeds.size(); i++) {
      const BoxNeed& b = f.rt.boxNeeds[i];
      if (b.st != ResState::Pending) continue;
      const BoxNeed* src = rt.box(strs.find(f.strs.get(b.src)));
      if (src && src->st != ResState::Pending) f.settleBox(i, src->w, src->h, src->st == ResState::Failed);
    }
    return true;
  }


  // A document compiles and ingests once (finding
  // api-measure-code/unchecked-boundary-invariants): a second call is refused
  // with a diagnostic instead of accumulating arena, diagnostics and needs.
  void compile(std::string source) {
    if (validThrough >= (int)Stage::Compile) {
      diags.add(Sev::Error, "doc-reuse", {}, "compile on a document that already compiled: use a new document");
      return;
    }
    diags.begin(DiagOrigin::Compile);
    validThrough = (int)Stage::Compile;
    src.init(std::move(source));
    skel = linepass(src, arena, diags);
    ast = parseDoc(src, skel, arena, strs, diags);
    js = codegen(ast, src, strs);
  }

  // Ingest (decode + instantiate) and Resolve in one call, as hosts use it.
  bool ingest(const u8* buf, size_t len) {
    if (done(Stage::Ingest)) {
      diags.add(Sev::Error, "doc-reuse", {}, "ingest on a document that already ingested: use a new document");
      return false;
    }
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
    validThrough = (int)Stage::Ingest;
    return true;
  }
  // Resolve (once: it rewrites the instantiated tree): sidecars, references,
  // numbering, then the host needs it raises (tokens, image sizes)
  void stageResolve() {
    diags.begin(DiagOrigin::Resolve);
    extractSidecars(tree.root, arena, strs, styles, diags);
    resolveDoc(tree, arena, strs, styles, cfg, diags, *registry, index);
    rt.clear();
    waitTokens.clear();
    waitBoxes.clear();
    if (tree.root) {
      waitTokens.resize(tree.root->kids.size());
      waitBoxes.resize(tree.root->kids.size());
      for (u32 pid = 0; pid < tree.root->kids.size(); pid++) {
        scanTokenNeeds(tree.root->kids[pid], pid);
        scanImageNeeds(tree.root->kids[pid], pid);
      }
    }
    validThrough = (int)Stage::Resolve;
  }

  // a code block with a language and a plain body needs its tokens; the
  // engine answers its own language (plan P1-09; code-design §2): 'tsm'
  void scanTokenNeeds(const ContentNode* n, u32 pid) {
    if (!n) return;
    if (n->kind == Kind::codeblock && !n->kids.empty() && n->kids[0]->kind == Kind::text) {
      const StrRef lang = attrStr(n, ArgK::lang);
      if (lang && !strs.get(lang).empty()) {
        const u32 i = rt.needTokens(lang, n->kids[0]->str);
        waitTokens[pid].push_back(i);
        // an in-engine answerer, else the Session (plan P1-21), else the host
        std::vector<CodeToken> toks;
        if (rt.tokenNeeds[i].st == ResState::Pending &&
            (session().answerTokens(strs.get(lang), strs.get(n->kids[0]->str), toks) ||
             session().tokens(strs.get(lang), strs.get(n->kids[0]->str), toks)))
          settleTokens(i, toks.data(), toks.size(), ResState::Ready);
      }
    }
    for (const ContentNode* k : n->kids) scanTokenNeeds(k, pid);
  }

  // an image without both declared dims needs its intrinsic size
  // (figure-design.md §2): the engine wants CSS px, not pixels
  void scanImageNeeds(const ContentNode* n, u32 pid, Span outer = {}) {
    if (!n) return;
    if (!n->span.empty()) outer = n->span;
    if (n->kind == Kind::image) {
      const double iw = attrNum(n, ArgK::w, 0), ih = attrNum(n, ArgK::h, 0);
      const StrRef src = attrStr(n, ArgK::src);
      // an unsafe scheme is reported here, once (plan P1-16: not by emit,
      // which re-runs), and renders the placeholder without ever fetching
      if (src && !safeImageSrc(strs.get(src)))
        diags.add(Sev::Warning, "image-src", n->span.empty() ? outer : n->span, "image src scheme not allowed");
      else if (src && !(iw > 0 && ih > 0))
        waitBoxes[pid].push_back(rt.needBox(src, outer));
    }
    for (const ContentNode* k : n->kids) scanImageNeeds(k, pid, outer);
  }

  // ---- settling needs (the wire's rows and the old per-kind shims) --------
  // Tokens: an unacceptable answer fails whole (plain code, provider-invalid).
  void settleTokens(u32 i, const CodeToken* toks, size_t n, ResState st) {
    if (i >= rt.tokenNeeds.size() || rt.tokenNeeds[i].st != ResState::Pending) return;
    TokenNeed& t = rt.tokenNeeds[i];
    if (st == ResState::Ready && !validTokens(strs.get(t.body), toks, n)) {
      diags.addAs(DiagOrigin::Provide, Sev::Warning, "provider-invalid", {},
                  "code tokens for '" + std::string(strs.get(t.lang)) + "' rejected: unsorted, overlapping, "
                  "off a UTF-8 boundary, past the body or of an unknown tag");
      st = ResState::Failed;
    }
    t.st = st;
    if (st == ResState::Ready) t.toks.assign(toks, toks + n);
    invalidateFrom(Stage::Emit);
  }
  // Box sizes: 0×0 (or anything not finite and positive) is a failed load:
  // a placeholder and a warning. The author's own dims stay theirs (emit
  // fills only what they left out — defect #24).
  void settleBox(u32 i, double wPx, double hPx, bool failed) {
    if (i >= rt.boxNeeds.size() || rt.boxNeeds[i].st != ResState::Pending) return;
    BoxNeed& b = rt.boxNeeds[i];
    if (!failed && std::isfinite(wPx) && std::isfinite(hPx) && wPx > 0 && hPx > 0) {
      b.st = ResState::Ready;
      b.w = wPx;
      b.h = hPx;
    } else {
      b.st = ResState::Failed;
      diags.addAs(DiagOrigin::Provide, Sev::Warning, "image-load", b.span,
                  "image failed to load: " + std::string(strs.get(b.src)));
    }
    invalidateFrom(Stage::Emit);
  }
  // the old per-kind provide exports (shims of tsr2_provide): an answer to
  // no pending need is reported, never silently dropped
  void provideImage(u32 id, double wPx, double hPx) {
    if (id >= rt.boxNeeds.size() || rt.boxNeeds[id].st != ResState::Pending) return unmatched("image", id);
    settleBox(id, wPx, hPx, false);
  }
  void provideTokens(u32 id, const CodeToken* toks, size_t n) {
    if (id >= rt.tokenNeeds.size() || rt.tokenNeeds[id].st != ResState::Pending) return unmatched("tokens", id);
    settleTokens(id, toks, n, ResState::Ready);
  }
  void unmatched(const char* kind, u32 id) {
    diags.addAs(DiagOrigin::Provide, Sev::Warning, "provider-invalid", {},
                std::string(kind) + " answer " + std::to_string(id) + " matches no pending need");
  }

  // ---- the resource pull (plan P1-19; docs/host-protocol-design.md §5) -----
  // A new batch of every pending need (of the kinds in `kinds`, bit = kind
  // id; 0 = all: a host that answers only some kinds asks only for them);
  // the host answers it with provide().
  void requests(std::string& out, u32 kinds = 0) {
    auto want = [&](ResKind k) { return kinds == 0 || (kinds >> (u16)k & 1); };
    ResourceTable::Batch& b = rt.batch;
    b = {};
    b.id = rt.nextBatch++;
    b.open = true;
    if (want(ResKind::textWidth) || want(ResKind::fontVmet)) {
      MeasureRequest mr = pendingRequests();
      if (want(ResKind::textWidth)) b.words = std::move(mr.words);
      if (want(ResKind::fontVmet)) b.vmets = std::move(mr.vmetFaces);
    }
    for (u32 i = 0; i < rt.tokenNeeds.size() && want(ResKind::codeTokens); i++)
      if (rt.tokenNeeds[i].st == ResState::Pending) b.tokens.push_back(i);
    for (u32 i = 0; i < rt.boxNeeds.size() && want(ResKind::boxInfo); i++)
      if (rt.boxNeeds[i].st == ResState::Pending) b.boxes.push_back(i);
    WireBatch w;
    w.batch = b.id;
    std::unordered_map<StrRef, u32> strIdx;
    auto str = [&](StrRef r) -> u32 {
      auto it = strIdx.find(r);
      if (it != strIdx.end()) return it->second;
      return strIdx[r] = w.str(strs.get(r));
    };
    std::unordered_map<FaceId, u32> mkIdx;
    auto mk = [&](FaceId f) -> u32 {
      auto it = mkIdx.find(f);
      if (it != mkIdx.end()) return it->second;
      const FaceKey& k = faces.get(f);
      WireMetricKey m;
      m.stack = str(k.family);
      m.faceDigest = k.faceDigest;
      m.sizePx = k.sizePx;
      m.weight = k.weight;
      m.italic = k.italic;
      m.features = str(k.features);
      m.lang = str(k.lang);
      m.dppx = k.dppx;
      w.mks.push_back(m);
      return mkIdx[f] = (u32)w.mks.size() - 1;
    };
    auto kind = [&](ResKind k, size_t n) -> WireKind& {
      WireKind& wk = w.kinds.emplace_back();
      wk.kind = (u16)k;
      wk.rows.resize(n);
      for (u32 i = 0; i < n; i++) wk.rows[i].resId = i;
      return wk;
    };
    if (!b.boxes.empty()) {
      WireKind& k = kind(ResKind::boxInfo, b.boxes.size());
      for (size_t i = 0; i < b.boxes.size(); i++) {
        k.rows[i].col[0] = 0;  // an image
        k.rows[i].col[1] = str(rt.boxNeeds[b.boxes[i]].src);
      }
    }
    if (!b.tokens.empty()) {
      WireKind& k = kind(ResKind::codeTokens, b.tokens.size());
      for (size_t i = 0; i < b.tokens.size(); i++) {
        k.rows[i].col[0] = str(rt.tokenNeeds[b.tokens[i]].lang);
        k.rows[i].col[1] = str(rt.tokenNeeds[b.tokens[i]].body);
      }
    }
    if (!b.vmets.empty()) {
      WireKind& k = kind(ResKind::fontVmet, b.vmets.size());
      for (size_t i = 0; i < b.vmets.size(); i++) k.rows[i].col[0] = mk(b.vmets[i]);
    }
    if (!b.words.empty()) {
      WireKind& k = kind(ResKind::textWidth, b.words.size());
      for (size_t i = 0; i < b.words.size(); i++) {
        k.rows[i].col[0] = mk(b.words[i].face);
        k.rows[i].col[1] = str(b.words[i].str);
      }
    }
    encodeWire(w, false, out);
  }

  // The host's answer to the open batch: every row is validated; a row that
  // is missing, invalid or failed degrades the quantity that consumed it
  // (design T9 A1) with one diagnostic per kind. false: rejected whole (not
  // a well-formed answer to the open batch) — the needs stay pending.
  bool provide(const u8* p, size_t n) {
    WireBatch a;
    std::string err;
    ResourceTable::Batch& b = rt.batch;
    if (!decodeWire(p, n, true, a, err) || !b.open || a.batch != b.id) {
      if (err.empty()) err = "answers batch " + std::to_string(a.batch) + ", the open batch is " + std::to_string(b.id);
      diags.addAs(DiagOrigin::Provide, Sev::Warning, "provider-invalid", {}, "resource answer rejected: " + err);
      return false;
    }
    b.open = false;
    struct Tally {
      u32 missing = 0, invalid = 0, failed = 0;
    };
    Tally tally[kResKindCount + 1] = {};
    std::vector<u8> seenWords(b.words.size()), seenVmets(b.vmets.size()), seenTokens(b.tokens.size()),
        seenBoxes(b.boxes.size());
    auto fresh = [&](std::vector<u8>& seen, u32 id, Tally& t) {
      if (id >= seen.size() || seen[id]) {
        t.invalid++;
        return false;
      }
      seen[id] = 1;
      return true;
    };
    auto okNum = [](double v) { return std::isfinite(v) && v >= 0; };
    for (const WireKind& k : a.kinds) {
      Tally& t = tally[k.kind <= kResKindCount ? k.kind : 0];
      for (const WireRow& r : k.rows) {
        const bool ok = r.status == 0;
        switch ((ResKind)k.kind) {
          case ResKind::textWidth:
            if (!fresh(seenWords, r.resId, t)) break;
            if (ok && okNum(r.f64(0))) {
              const MeasureItem& w = b.words[r.resId];
              metrics.provideWord(w.str, w.face, r.f64(0));
              if (r.flags & 1) session().putWidth(sessionMk(w.face), strs.get(w.str), r.f64(0));  // write-through
            } else {
              failWord(b.words[r.resId], ok ? t.invalid : t.failed);
            }
            break;
          case ResKind::fontVmet:
            if (!fresh(seenVmets, r.resId, t)) break;
            if (ok && okNum(r.f64(0)) && okNum(r.f64(1))) {
              metrics.provideVmet(b.vmets[r.resId], r.f64(0), r.f64(1));
              if (r.flags & 1) session().putVmet(sessionMk(b.vmets[r.resId]), r.f64(0), r.f64(1));
            } else {
              failVmet(b.vmets[r.resId], ok ? t.invalid : t.failed);
            }
            break;
          case ResKind::codeTokens: {
            if (!fresh(seenTokens, r.resId, t)) break;
            std::vector<CodeToken> toks;
            const bool shaped = r.list.size() % 3 == 0;
            for (size_t i = 0; shaped && i + 2 < r.list.size(); i += 3)
              toks.push_back({r.list[i], r.list[i + 1], (u8)std::min<u32>(r.list[i + 2], 255)});
            if (!ok) t.failed++;
            const u32 ti = b.tokens[r.resId];
            settleTokens(ti, toks.data(), toks.size(), ok && shaped ? ResState::Ready : ResState::Failed);
            const TokenNeed& tn = rt.tokenNeeds[ti];
            if ((r.flags & 1) && tn.st == ResState::Ready)  // write-through
              session().putTokens(strs.get(tn.lang), strs.get(tn.body), tn.toks);
            break;
          }
          case ResKind::boxInfo:
            if (!fresh(seenBoxes, r.resId, t)) break;
            settleBox(b.boxes[r.resId], r.f64(0), r.f64(1), !ok);
            break;
          default:
            t.invalid++;
            break;
        }
      }
    }
    // a row of the batch without an answer fails as 'provider-missing'
    Tally& tw = tally[(u16)ResKind::textWidth];
    for (u32 i = 0; i < seenWords.size(); i++)
      if (!seenWords[i]) failWord(b.words[i], tw.missing);
    Tally& tv = tally[(u16)ResKind::fontVmet];
    for (u32 i = 0; i < seenVmets.size(); i++)
      if (!seenVmets[i]) failVmet(b.vmets[i], tv.missing);
    for (u32 i = 0; i < seenTokens.size(); i++)
      if (!seenTokens[i]) {
        tally[(u16)ResKind::codeTokens].missing++;
        settleTokens(b.tokens[i], nullptr, 0, ResState::Failed);
      }
    for (u32 i = 0; i < seenBoxes.size(); i++)
      if (!seenBoxes[i]) {
        tally[(u16)ResKind::boxInfo].missing++;
        settleBox(b.boxes[i], 0, 0, true);
      }
    for (const ResKindInfo& k : kResKinds) {
      const Tally& t = tally[(u16)k.kind];
      if (t.missing)
        diags.addAs(DiagOrigin::Provide, Sev::Warning, "provider-missing", {},
                    std::to_string(t.missing) + " " + k.name + " answer(s) missing");
      if (t.invalid)
        diags.addAs(DiagOrigin::Provide, Sev::Warning, "provider-invalid", {},
                    std::to_string(t.invalid) + " " + k.name + " answer(s) invalid");
      if (t.failed)
        diags.addAs(DiagOrigin::Provide, Sev::Warning, "measure-failed", {},
                    std::to_string(t.failed) + " " + k.name + " answer(s) failed");
    }
    return true;
  }
  // a width the host did not give: the em bound (design T9 A1)
  void failWord(const MeasureItem& w, u32& count) {
    count++;
    metrics.provideWord(w.str, w.face, failedWidthPx(strs.get(w.str), faces.get(w.face).sizePx));
  }
  // vertical metrics the host did not give: ascent 1em, descent 0.3em
  void failVmet(FaceId f, u32& count) {
    count++;
    const double px = faces.get(f).sizePx;
    metrics.provideVmet(f, px, 0.3 * px);
  }

  // Drives Emit → Measure → Layout (breaking included) as far as the host's answers
  // allow; NeedMeasure = see pendingRequests(). Earlier stages are the
  // caller's (compile, execute, ingest).
  Status typeset() {
    if (!done(Stage::Resolve)) return Status::NeedMeasure;
    if (done(Stage::Layout)) return Status::Ok;
    if (!done(Stage::BoxTree)) {  // the block structure (plan P1-18): once per resolved tree
      boxtree = buildBoxTree(tree, strs, styles, cfg);
      validThrough = (int)Stage::BoxTree;
    }
    if (!done(Stage::Emit)) {
      const size_t n = boxtree.tops.size();
      if (tops.size() != n || emitted.size() != n) {
        tops.assign(n, {});
        emitted.assign(n, 0);
      }
      EmitPass pass(boxtree, arena, strs, styles, cfg, diags, nullptr, &rt);
      bool waiting = false;
      std::vector<MeasureItem> none;
      for (size_t t = 0; t < n; t++) {
        if (emitted[t]) continue;
        const u32 pid = boxtree.tops[t].pid;
        if (topWaits(pid)) {  // a token or image answer of this block is pending
          waiting = true;
          continue;
        }
        diags.beginPid(DiagOrigin::Emit, pid);  // a retry replaces its diagnostics
        pass.top(t, tops[t], none);
        emitted[t] = 1;
      }
      diags.origin = DiagOrigin::Emit;
      diags.pid = ~0u;
      diags.sortPids(DiagOrigin::Emit);
      if (waiting) return Status::NeedMeasure;
      validThrough = (int)Stage::Emit;
    }
    if (!done(Stage::Measure)) {
      metrics.setEpsilon((Su)cfg.epsilonPerWordSu);  // Measure quantizes (plan P1-19)
      // formulas finalize here (plan P1-25): their layout diagnostics are
      // their block's Emit slice
      ObjectEnv oe{arena, strs, styles, cfg.baseSizePx, &diags};
      diags.origin = DiagOrigin::Emit;
      MeasureRequest missing = resolveWidths(tops, metrics, styles, cfg, &oe);
      diags.pid = ~0u;
      if (!missing.empty()) return Status::NeedMeasure;
      fuseLegacy(tops);  // the legacy breaker's blocks (until P4-08)
      validThrough = (int)Stage::Measure;
    }
    // layout breaks the paragraphs (plan P1-15): overfull streams report
    // under the Layout origin
    diags.begin(DiagOrigin::Layout);
    layout = layoutDoc(tops, metrics, strs, cfg, diags, &session().breakMemo);
    validThrough = (int)Stage::Layout;
    return Status::Ok;
  }

  // a top-level block waits while one of its code or image answers is pending
  bool topWaits(u32 pid) const {
    if (pid < waitTokens.size())
      for (u32 i : waitTokens[pid])
        if (rt.tokenNeeds[i].st == ResState::Pending) return true;
    if (pid < waitBoxes.size())
      for (u32 i : waitBoxes[pid])
        if (rt.boxNeeds[i].st == ResState::Pending) return true;
    return false;
  }

  // the widths and vertical metrics still missing (the emitted blocks'; a
  // pending formula's text runs among them)
  MeasureRequest pendingRequests() {
    ObjectEnv oe{arena, strs, styles, cfg.baseSizePx, &diags};
    diags.origin = DiagOrigin::Emit;
    MeasureRequest r = resolveWidths(tops, metrics, styles, cfg, &oe);
    diags.pid = ~0u;
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
    if (name == "blocktree") return dumpBlockTree(boxtree.tops, strs);
    if (name == "mathir") return dumpMathIRs(tops, strs);
    if (name == "mathbox") return dumpMathBoxes(tops, strs);
    if (name == "blocks") return dumpBlocks(tops, strs, styles);
    if (name == "hlist") return dumpHLists(tops, strs, styles);
    if (name == "breaks") return dumpBreaks(layout);
    if (name == "layout") return dumpLayout(layout);
    if (name == "vlist") return dumpVList(layout, tops);
    if (name == "dl") return dumpDisplayList(layout, tops, styles, strs, cfg);
    if (name == "paged") return renderPaged(cfg.pageHeightPx);
    if (name == "html") return render();
    if (name == "diags") return dumpDiags();
    if (name == "settings") return settingsJson(cfg) + "\n";
    return {};
  }

  // rendering needs a converged typeset (finding unchecked-boundary-invariants)
  bool renderReady() {
    if (done(Stage::Layout)) return true;
    diags.add(Sev::Error, "render-precondition", {}, "render before the typeset converged");
    return false;
  }
  std::string render() {
    if (!renderReady()) return {};
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    // paint (plan P1-18): each block's DisplayList, written by the
    // stateless typeset backend
    std::string html;
    writeRoot(html, "tsr-doc", paintRoot(cfg));
    DLBlock b;
    for (size_t p = 0; p < layout.paras.size(); p++) {
      paintBlock(layout, p, tops, strs, cfg, b);
      writeBlock(html, b, styles, strs, cfg.baseSizePx);
    }
    html += "</div>\n";
    reportWriterDefects();
    return html;
  }

  // paged rendering for print (pages-design.md §2); needs a finished layout
  std::string renderPaged(double pageHeightPx) {
    if (!renderReady()) return {};
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    // the sheets (layout/paginate.cc), each band's nodes rebased into its
    // sheet by the same stateless writer
    const PageResult pr = paginate(layout, tops, pageHeightPx);
    std::vector<DLBlock> dl(layout.paras.size());
    for (size_t p = 0; p < layout.paras.size(); p++) paintBlock(layout, p, tops, strs, cfg, dl[p]);
    std::string html;
    writeRoot(html, "tsr-doc tsr-paged", paintRoot(cfg));
    for (const Page& pg : pr.pages) {
      {
        Tag t(html, "div");
        t.attrSafe("class", "tsr-sheet");
        t.decl("position", "relative").decl("overflow", "hidden").px("height", suToPx(pr.height));
        t.open();
        html += "\n";
      }
      for (const PageBand& band : pg.bands)
        writeNodes(html, dl[band.para], band.lo, band.hi, (Su)((i64)layout.paras[band.para].y - pg.top), styles,
                   strs, cfg.baseSizePx);
      html += "</div>\n";
    }
    html += "</div>\n";
    reportWriterDefects();
    return html;
  }

  // needs only the post-resolve tree — valid before any measurement
  std::string renderFallback() {
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    std::string html = renderSemantic(tree, strs, styles, &rt);
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

  // relayout (architecture §2.4): emit reads no width (plan P1-16: image
  // boxes and sidecar columns are layout's), so a width change re-enters
  // Layout in place — the structural fix of defect #16. Hosts send a
  // host.width settings patch (tsr2_set_config); this is its shorthand.
  void setWidth(double widthPx) {
    if (widthPx == cfg.widthPx) return;
    cfg.widthPx = widthPx;
    invalidateFrom(Stage::Layout);
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
