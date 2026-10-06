// Document handle: owns all stage products; resumable typeset loop
// (architecture §2.4). Single-threaded; one pipeline state per handle.
#pragma once
#include <limits>
#include <memory>
#include "../ast/ast.h"
#include "../code/languages.gen.h"
#include "../code/overlay.h"
#include "../code/tokens.h"
#include "../codegen/codegen.h"
#include "../math/env.h"
#include "../model/cascade.h"
#include "../resolve/resolve.h"
#include "../semantic/declare.h"
#include "../semantic/locale.h"
#include "../semantic/manifest.h"
#include "../boxtree/build.h"
#include "../resource/resource_table.h"
#include "../resource/session.h"
#include "../layout/layout.h"
#include "../layout/paginate.h"
#include "../render/html_writer.h"
#include "../render/rules_css.h"
#include "../render/semantic_html.h"
#include "../render/typeset_html.h"
#include "../support/hash128.h"
#include "../syntax/exports.h"
#include "settings.gen.h"  // (plan P3-32) Config: the host's, never a stage's

namespace tsr {

struct Doc {
  Config cfg;
  Arena arena;
  Interner strs{arena};
  DiagSink diags;

  SourceText src;
  Skeleton skel;
  AstNode* ast = nullptr;
  Lowered js;  // the LowerProgram and the hole module (plan P2-02)

  RawOps raw;
  StyleTable styles;
  NodePropsTable nodeProps;  // block properties by the cascade (plan P3-01)
  Cascade cascade{strs};     // the rules: defaults, host, $.set, style.where
  ContentTree tree;
  // the element registry (plan P1-10, P2-07): built at Ingest (PHASE 0,
  // semantic/declare.h) from the built-in rows — or `registryBase`, a
  // test's replacement in their form — the host's semantics.* settings and
  // the document's declarations; the Index resolve leaves behind
  std::string registryBase;
  std::shared_ptr<const Registry> registryOwn;
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
  MathEnv mathEnv;  // the document's math declarations, by epoch (plan P2-15)
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
  // the generation of the last RenderResult (plan P3-06): a preview
  // fragment is stamped with it, so a host shows only content that matches
  // the view it committed
  u64 generation = 0;

  // ---- stage model (plan P1-03; stages.def, docs/host-protocol-design.md) --
  // Every stage up to validThrough has its product; invalidateFrom drops the
  // later ones. Replaces the old emitted/laidOut flags.
  int validThrough = -1;
  bool done(Stage s) const { return validThrough >= (int)s; }
  void invalidateFrom(Stage s) {
    if (validThrough >= (int)s) validThrough = (int)s - 1;
    if (s <= Stage::Layout) layoutAsks = 0;  // a new layout: its own rounds
  }
  // (plan P3-28) how many times this layout has asked for host boxes: one
  // round settles every box (widths never depend on answers), a second one
  // the boxes a float's narrowing moved; after that the declared size
  // stands (box-unsettled)
  static constexpr u32 kLayoutAsks = 2;
  u32 layoutAsks = 0;
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
    faces.bind(cfg, &styles, &strs);
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
      if (!reentrant(kStageRerun[k])) return kRebuild;
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
    f.langSource = langSource;  // (its language, as this document decided it)
    f.inputs = inputs;          // (plan P3-31: its declared inputs are its identity)
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
      f.faces.bind(f.cfg, &f.styles, &f.strs);
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
      const TokenNeed* src = rt.tokens(strs.find(f.strs.get(t.lang)), strs.find(f.strs.get(t.body)), t.overlays);
      if (src && src->st != ResState::Pending) f.settleTokens(i, src->toks.data(), src->toks.size(), src->st);
    }
    for (u32 i = 0; i < f.rt.boxNeeds.size(); i++) {
      const BoxNeed& b = f.rt.boxNeeds[i];
      if (b.st != ResState::Pending) continue;
      const BoxNeed* src = rt.box(b.kind, strs.find(f.strs.get(b.src)), b.availPx);
      if (src && src->st != ResState::Pending)
        f.settleBox(i, src->w, src->h, src->baseline, src->st == ResState::Failed);
    }
    // (plan P3-28) the boxes measured at a width: a fork at the same widths
    // asks again for none of them (strings are cloned: same refs)
    for (const BoxNeed& b : rt.boxNeeds)
      if (b.st == ResState::Ready) {
        bool fresh = false;
        const u32 i = f.rt.needBox(b.kind, b.src, b.availPx, b.span, &fresh);
        if (fresh) f.settleBox(i, b.w, b.h, b.baseline, false);
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
    skel = linepass(src, arena, diags, FrontEndOptions{cfg.frontMatter});  // (plan P3-35) its host options
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
    documentLanguage();
    registryOwn = declaredRegistry(raw, cfg, registryBase, diags);
    registry = registryOwn.get();
    // the base rules (plan P3-01): the engine's defaults, which read some
    // host settings ({"setting": "code.scale"}), then the host's (style.rules)
    {
      JsonValue settings;
      JsonReader jr;
      jr.parse(settingsJson(cfg), settings);
      auto setting = [&](std::string_view path, std::string& out) -> bool {
        const JsonValue* v = &settings;
        for (size_t at = 0; v && at <= path.size();) {
          size_t dot = path.find('.', at);
          if (dot == std::string_view::npos) dot = path.size();
          v = v->get(path.substr(at, dot - at));
          at = dot + 1;
        }
        out.clear();
        if (!v) return false;
        if (v->t == JsonValue::T::Num) appendf(out, "%g", v->num);
        else if (v->t == JsonValue::T::Bool) out = v->b ? "true" : "false";
        else if (v->t == JsonValue::T::Str) out = v->str;
        else return false;
        return true;
      };
      // a code block's rule by its fence tag (lang)
      auto codeRule = [&](std::string_view lang, ArgK key, std::string_view value) {
        StyleRule r;
        r.sel.kind = (u16)Kind::codeblock;
        r.sel.where.push_back({ArgK::lang, strs.intern(lang)});
        r.patch.push_back({key, ArgTag::Str, 0, strs.intern(value)});
        return r;
      };
      // env 1: the per-language code features (code.fontFeaturesByLang:
      // rules on code blocks of that language, plan P3-02) — a built-in
      // language's for each of its fence tags (plan P3-22: c++ and
      // cpp-literate are cpp), the tag as configured winning — then
      // style.rules
      std::vector<StyleRule> host;
      const auto& byLang = cfg.codeFontFeaturesByLang;
      for (const auto& [lang, feats] : byLang)
        for (const LangAlias& a : kLangAliases)
          if (a.lang == languageOfTag(lang) && a.tag != lang && !byLang.count(std::string(a.tag)))
            host.push_back(codeRule(a.tag, ArgK::features, feats));
      for (const auto& [lang, feats] : byLang) host.push_back(codeRule(lang, ArgK::features, feats));
      if (!cfg.styleRules.empty())
        for (StyleRule& r : parseRules(cfg.styleRules, strs, diags, "style.rules", setting)) host.push_back(std::move(r));
      // env 0: the engine's defaults, then the fence profiles' (plan P3-22:
      // cpp-literate's code blocks get the noweb overlay)
      std::vector<StyleRule> defaults = parseRules(defaultRulesJson(), strs, diags, "defaults", setting);
      for (const LangProfile& p : kProfiles) defaults.push_back(codeRule(p.tag, ArgK::overlays, overlayNames(p.overlays)));
      for (StyleRule& r : defaults) r.builtin = true;
      cascade.setBase(std::move(defaults), std::move(host));
    }
    tree = instantiate(raw, arena, strs, styles, nodeProps, cascade, diags, *registry);
    checkDeclarations(raw, tree, *registry, strs, diags);
    mathEnv.build(tree.decls, strs, diags);
    validThrough = (int)Stage::Ingest;
    return true;
  }
  // (plan P3-31) project.urls, parsed once per value: where the project's
  // documents are published (an external reference's link)
  std::unordered_map<std::string, std::string> projectUrls_;
  std::string projectUrlsFrom_;
  const std::unordered_map<std::string, std::string>& projectUrls() {
    if (projectUrlsFrom_ != cfg.projectUrls) {
      projectUrlsFrom_ = cfg.projectUrls;
      projectUrls_.clear();
      JsonValue v;
      JsonReader rd;
      if (!cfg.projectUrls.empty() && rd.parse(cfg.projectUrls, v) && v.t == JsonValue::T::Obj)
        for (size_t k = 0; k < v.keys.size(); k++)
          if (v.vals[k].t == JsonValue::T::Str) projectUrls_[v.keys[k]] = v.vals[k].str;
    }
    return projectUrls_;
  }

  // (plan P3-31; design T9 A7) its declared inputs (inputs.def), given
  // before Ingest: part of its identity, a fork copies them; never shown to
  // a script (the decoders validate them). false: an unknown input, or too
  // late (the document has ingested)
  std::unordered_map<std::string, std::string> inputs;
  bool setInput(std::string_view name, std::string_view bytes) {
    bool known = false;
#define INPUT(n, version, schema, stage) known = known || name == #n;
#include "inputs.def"
#undef INPUT
    if (!known) {
      diags.add(Sev::Warning, "input-unknown", {}, "no declared input '" + std::string(name) + "' (inputs.def)");
      return false;
    }
    if (done(Stage::Ingest)) {
      diags.add(Sev::Warning, "input-late", {}, "input '" + std::string(name) + "' after Ingest: ignored (fork a new document)");
      return false;
    }
    inputs[std::string(name)] = std::string(bytes);
    return true;
  }

  // (plan P3-30; D-T06) Phase 0: the document's language — its own
  // ($.doc({lang}): the last), else the host's doc.lang, else (auto) the
  // language of its text — before anything reads it (the terms, the faces,
  // the page's lang); docinfo reports it and where it came from
  std::string langSource = "host";
  void documentLanguage() {
    std::string_view declared;
    for (const RawDecl& d : raw.decls) {
      if (d.type >= DECL_COUNT || std::string_view(kDecls[d.type].name) != "doc") continue;
      for (const ArgVal& a : d.args)
        if (a.key == ArgK::ext && a.tag == ArgTag::Str && a.name < raw.strings.size() && raw.strings[a.name] == "lang" &&
            a.ref < raw.strings.size() && !raw.strings[a.ref].empty())
          declared = raw.strings[a.ref];
    }
    if (!declared.empty()) {
      cfg.lang = std::string(declared);
      langSource = "document";
    } else if (cfg.lang == "auto") {
      cfg.lang = detectDocumentLang(raw);
      langSource = "detected";
    }
  }

  // Resolve (once: it rewrites the instantiated tree): references,
  // numbering, then the host needs it raises (tokens, image sizes). (A code
  // block's sidecars arrive split, from the default fence: plan P2-13.)
  void stageResolve() {
    diags.begin(DiagOrigin::Resolve);
    resolveDoc(tree, arena, strs, styles, nodeProps, cascade, cfg, diags, *registry, index, inputs["labels"]);
    rt.clear();
    waitTokens.clear();
    waitBoxes.clear();
    if (tree.root) {
      waitTokens.resize(tree.root->kids.size());
      waitBoxes.resize(tree.root->kids.size());
      for (u32 pid = 0; pid < tree.root->kids.size(); pid++) {
        scanTokenNeeds(tree.root->kids[pid], pid);
        scanImageNeeds(tree.root->kids[pid]);
      }
    }
    scanHyphNeeds();
    validThrough = (int)Stage::Resolve;
  }

  // (plan P4-06; D-X09) the languages the text names (a run's text.lang,
  // the document's): one whose words hyphenate and that the resident en-US
  // does not serve asks the host for its patterns, once — the Session keeps
  // them —, and Emit waits for every answer: a dictionary arriving later
  // must never change breaks from one pass to the next
  std::vector<StrRef> hyphLangs_;  // 0: the document's
  bool hyphResolved_ = false;
  std::string_view langTag(StrRef l) const { return l ? strs.get(l) : std::string_view(cfg.lang); }
  // a language with patterns of its own: its words hyphenate (its pack),
  // the resident dictionary is not its, and it is written in a script that
  // hyphenates — a CJK language's Latin words are und's, en-US (design T5:
  // the dictionary of a (language, script) walks to und's)
  static bool ownPatterns(std::string_view tag) {
    if (residentHyphenLang(tag) || !localeHyphenates(tag)) return false;
    const std::string full = maximizeLocale(tag);  // lang-Script-REGION
    for (std::string_view cjk : {"-Hani", "-Hans", "-Hant", "-Jpan", "-Kore", "-Hang", "-Hira", "-Kana", "-Bopo"})
      if (full.find(cjk) != std::string::npos) return false;
    return true;
  }
  void collectLangs(const ContentNode* n) {
    if (!n) return;
    const StrRef l = styles.get(n->style).lang;
    if (std::find(hyphLangs_.begin(), hyphLangs_.end(), l) == hyphLangs_.end()) hyphLangs_.push_back(l);
    for (const ContentNode* k : n->kids) collectLangs(k);
  }
  void scanHyphNeeds() {
    hyphLangs_.assign(1, 0);
    hyphResolved_ = false;
    collectLangs(tree.root);
    for (StrRef l : hyphLangs_) {
      const std::string_view tag = langTag(l);
      if (!ownPatterns(tag)) continue;
      bool fresh = false;
      const u32 i = rt.needHyph(strs.intern(tag), &fresh);
      std::shared_ptr<const HyphenDict> d;
      if (fresh && session().hyph(tag, d)) settleHyph(i, std::move(d));
    }
  }
  void settleHyph(u32 i, std::shared_ptr<const HyphenDict> d) {
    HyphNeed& h = rt.hyphNeeds[i];
    h.st = d ? ResState::Ready : ResState::Failed;
    h.dict = std::move(d);
    invalidateFrom(Stage::Emit);
  }
  // once every need has settled: each language's dictionary — the
  // resident one for en, en-US and a language whose words do not hyphenate
  // (its Latin words: und's, en-US), the host's, else the resident one
  // when the language falls back to it (en-GB → en), else none (the words
  // break at soft and explicit hyphens only); a diagnostic says which
  void resolveHyph() {
    if (hyphResolved_) return;
    hyphResolved_ = true;
    rt.hyphDicts.clear();
    for (StrRef l : hyphLangs_) {
      const std::string_view tag = langTag(l);
      const HyphenDict* d = &residentHyphenDict();
      if (ownPatterns(tag)) {
        const HyphNeed& h = rt.hyphNeeds[rt.needHyph(strs.intern(tag))];
        d = h.dict.get();
        if (!d) {
          for (const std::string& name : localeChain(tag))
            if (name != "root" && residentHyphenLang(name)) d = &residentHyphenDict();
          diags.addAs(DiagOrigin::Resolve, d ? Sev::Info : Sev::Warning, "hyph-unavailable", {},
                      "no hyphenation patterns for '" + std::string(tag) + "' (the host has none): " +
                          (d ? "en-US's are used" : "its words break only at soft and explicit hyphens"));
        }
      }
      rt.hyphDicts[l] = d;
    }
  }

  // a code block with a language and a plain body needs its tokens; the
  // engine answers its own language (plan P1-09; code-design §2): 'tsm'.
  // Its overlays (plan P3-22; code/overlay.h) blank their spans in the text
  // the provider tokenizes.
  void scanTokenNeeds(const ContentNode* n, u32 pid) {
    if (!n) return;
    if (n->kind == Kind::codeblock && !n->kids.empty() && n->kids[0]->kind == Kind::text) {
      const StrRef lang = attrStr(n, ArgK::lang);
      if (lang && !strs.get(lang).empty()) {
        const StrRef body = n->kids[0]->str;
        std::vector<std::string_view> unknown;
        const u32 overlays = overlayMask(strs.get(nodeProps.get(n->props).codeOverlays), &unknown);
        for (std::string_view u : unknown)
          diags.add(Sev::Warning, "code-overlay", n->span,
                    "no overlay '" + std::string(u) + "' (engine/schema/languages.json has: " + overlayNames(~0u) + ")");
        bool fresh = false;
        const u32 i = rt.needTokens(lang, body, overlays, &fresh);
        TokenNeed& need = rt.tokenNeeds[i];
        if (fresh && overlays) {
          need.spans = overlaySpans(strs.get(body), overlays);
          if (!need.spans.empty()) need.sent = strs.intern(maskSpans(strs.get(body), need.spans));
        }
        waitTokens[pid].push_back(i);
        // an in-engine answerer, else the Session (plan P1-21), else the host
        std::vector<CodeToken> toks;
        if (need.st == ResState::Pending &&
            (session().answerTokens(strs.get(lang), strs.get(need.sent), toks) ||
             session().tokens(strs.get(lang), strs.get(need.sent), toks)))
          settleTokens(i, toks.data(), toks.size(), ResState::Ready);
      }
    }
    for (const ContentNode* k : n->kids) scanTokenNeeds(k, pid);
  }

  // an image without both declared dims needs its intrinsic size
  // (figure-design.md §2): the engine wants CSS px, not pixels. (Plan P3-32,
  // design T9 M12) filed here so the first batch asks for it beside the
  // words; its consumers come after Emit — Measure (an inline image's box),
  // Layout (a block's) — so no block waits to emit
  void scanImageNeeds(const ContentNode* n, Span outer = {}) {
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
        rt.needBox(BoxKind::Image, src, 0, outer);
    }
    for (const ContentNode* k : n->kids) scanImageNeeds(k, outer);
  }

  // ---- settling needs (the wire's rows and the old per-kind shims) --------
  // Tokens: an unacceptable answer fails whole (plain code, provider-invalid).
  void settleTokens(u32 i, const CodeToken* toks, size_t n, ResState st) {
    if (i >= rt.tokenNeeds.size() || rt.tokenNeeds[i].st != ResState::Pending) return;
    TokenNeed& t = rt.tokenNeeds[i];
    if (st == ResState::Ready && !validTokens(strs.get(t.sent), toks, n)) {
      diags.addAs(DiagOrigin::Provide, Sev::Warning, "provider-invalid", {},
                  "code tokens for '" + std::string(strs.get(t.lang)) + "' rejected: unsorted, overlapping, "
                  "off a UTF-8 boundary, past the body or of an unknown tag");
      st = ResState::Failed;
    }
    t.st = st;
    if (st == ResState::Ready) t.toks.assign(toks, toks + n);
    if (st == ResState::Ready && !t.spans.empty()) t.merged = mergeSpans(t.toks, t.spans);
    invalidateFrom(Stage::Emit);
  }
  // Box sizes: an image's 0×0 (or anything not finite and positive) is a
  // failed load: a placeholder and a warning. The author's own dims stay
  // theirs (emit fills only what they left out — defect #24). An svg or
  // html box (plan P3-28) is measured at its width: its height (0 is an
  // empty box) and baseline; failed, it keeps its declared size.
  void settleBox(u32 i, double wPx, double hPx, double baselinePx, bool failed) {
    if (i >= rt.boxNeeds.size() || rt.boxNeeds[i].st != ResState::Pending) return;
    BoxNeed& b = rt.boxNeeds[i];
    const bool image = b.kind == BoxKind::Image;
    if (!failed && std::isfinite(wPx) && std::isfinite(hPx) && (image ? wPx > 0 && hPx > 0 : hPx >= 0)) {
      b.st = ResState::Ready;
      b.w = image ? wPx : b.availPx;
      b.h = hPx;
      b.baseline = std::isfinite(baselinePx) && baselinePx >= 0 && baselinePx <= hPx ? baselinePx : hPx;
    } else {
      b.st = ResState::Failed;
      if (image)
        diags.addAs(DiagOrigin::Provide, Sev::Warning, "image-load", b.span,
                    "image failed to load: " + std::string(strs.get(b.src)));
      else
        diags.addAs(DiagOrigin::Provide, Sev::Warning, "box-measure", b.span,
                    "the host could not measure a raw(measure: 'host') box: its declared size is used");
    }
    if (b.emit) invalidateFrom(Stage::Emit);  // (a layout box: Layout is still to run)
  }

  // (plan P3-28; design T6 S14, T9 M11) a stage's way to host boxes, for
  // one run: an answered box's size; an unanswered one is answered by the
  // engine when it can (an svg its attributes size), else filed — the run
  // is provisional (the pull goes on) — or, past the layout's asks, settled
  // as failed (box-unsettled). Lookup: answers only (the sheets' own layout,
  // at render).
  struct BoxPull final : BoxAsker {
    enum class Mode : u8 { Ask, Settle, Lookup };
    Doc& d;
    Mode mode;
    bool emit;               // its consumer is Emit (an inline box)
    std::vector<u32> filed;  // the needs this run filed
    BoxPull(Doc& doc, Mode m, bool atEmit) : d(doc), mode(m), emit(atEmit) {}
    static BoxAnswer answerOf(const BoxNeed& b) {
      BoxAnswer a;
      a.ready = b.st == ResState::Ready;
      a.pending = b.st == ResState::Pending;
      if (a.ready) a.w = b.w, a.h = b.h, a.baseline = b.baseline;
      return a;
    }
    BoxAnswer ask(BoxKind kind, StrRef ref, double widthPx, Span span) override {
      if (mode == Mode::Lookup) {
        const BoxNeed* b = d.rt.box(kind, ref, widthPx);
        return b ? answerOf(*b) : BoxAnswer{};
      }
      bool fresh = false;
      const u32 i = d.rt.needBox(kind, ref, widthPx, span, &fresh);
      if (emit) d.rt.boxNeeds[i].emit = true;
      double h = 0, base = 0;
      if (fresh && kind == BoxKind::Svg && svgBoxPx(d.strs.get(ref), widthPx, h, base))
        d.settleBox(i, widthPx, h, base, false);  // the engine's own answer
      BoxNeed& b = d.rt.boxNeeds[i];
      if (b.st == ResState::Pending) {
        if (mode == Mode::Ask) {
          if (std::find(filed.begin(), filed.end(), i) == filed.end()) filed.push_back(i);
        } else {
          b.st = ResState::Failed;
          d.diags.add(Sev::Warning, "box-unsettled", b.span,
                      kind == BoxKind::Image
                          ? "an image's size was still unanswered after " + std::to_string(kLayoutAsks) +
                                " rounds: its placeholder is used"
                          : "a raw(measure: 'host') box was still unmeasured after " + std::to_string(kLayoutAsks) +
                                " rounds (its width kept changing): its declared size is used");
        }
      }
      return answerOf(b);
    }
  };
  // the old per-kind provide exports (shims of tsr2_provide): an answer to
  // no pending need is reported, never silently dropped
  void provideImage(u32 id, double wPx, double hPx) {
    if (id >= rt.boxNeeds.size() || rt.boxNeeds[id].st != ResState::Pending) return unmatched("image", id);
    settleBox(id, wPx, hPx, hPx, false);
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
    for (u32 i = 0; i < rt.hyphNeeds.size() && want(ResKind::hyphPatterns); i++)
      if (rt.hyphNeeds[i].st == ResState::Pending) b.hyphs.push_back(i);
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
        const BoxNeed& bn = rt.boxNeeds[b.boxes[i]];
        k.rows[i].col[0] = (u64)bn.kind;
        k.rows[i].col[1] = str(bn.src);
        k.rows[i].setF64(2, bn.availPx);
      }
    }
    if (!b.hyphs.empty()) {
      WireKind& k = kind(ResKind::hyphPatterns, b.hyphs.size());
      for (size_t i = 0; i < b.hyphs.size(); i++) k.rows[i].col[0] = str(rt.hyphNeeds[b.hyphs[i]].lang);
    }
    if (!b.tokens.empty()) {
      WireKind& k = kind(ResKind::codeTokens, b.tokens.size());
      for (size_t i = 0; i < b.tokens.size(); i++) {
        k.rows[i].col[0] = str(rt.tokenNeeds[b.tokens[i]].lang);
        k.rows[i].col[1] = str(rt.tokenNeeds[b.tokens[i]].sent);
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
        seenBoxes(b.boxes.size()), seenHyphs(b.hyphs.size());
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
              session().putTokens(strs.get(tn.lang), strs.get(tn.sent), tn.toks);
            break;
          }
          case ResKind::boxInfo:
            if (!fresh(seenBoxes, r.resId, t)) break;
            settleBox(b.boxes[r.resId], r.f64(0), r.f64(1), r.f64(2), !ok);
            break;
          case ResKind::hyphPatterns: {
            // (plan P4-06) compiled here; text that does not compile fails
            // the language (provider-invalid). Kept in the Session either
            // way: a document's breaks never change from one edit to the next
            if (!fresh(seenHyphs, r.resId, t)) break;
            const u32 hi = b.hyphs[r.resId];
            const std::string tag(strs.get(rt.hyphNeeds[hi].lang));
            std::shared_ptr<HyphenDict> d;
            if (ok) {
              d = std::make_shared<HyphenDict>();
              std::string err;
              if (!HyphenDict::compile(tag, a.strings[r.col[0]], a.strings[r.col[1]], (u8)r.col[2], (u8)r.col[3],
                                       a.strings[r.col[4]], *d, err)) {
                diags.addAs(DiagOrigin::Provide, Sev::Warning, "provider-invalid", {},
                            "hyphenation patterns for '" + tag + "' rejected: " + err);
                d.reset();
                t.invalid++;
              }
            }  // (a host without them: hyph-unavailable says so, at Emit)
            if (r.flags & 1) session().putHyph(tag, d);
            settleHyph(hi, std::move(d));
            break;
          }
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
        settleBox(b.boxes[i], 0, 0, 0, true);
      }
    for (u32 i = 0; i < seenHyphs.size(); i++)
      if (!seenHyphs[i]) {
        tally[(u16)ResKind::hyphPatterns].missing++;
        settleHyph(b.hyphs[i], nullptr);
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
      boxtree = buildBoxTree(tree, strs, styles, nodeProps, cfg, *registry);
      boxtree.math = &mathEnv;
      boxtree.cascade = &cascade;
      validThrough = (int)Stage::BoxTree;
    }
    if (!done(Stage::Emit)) {
      if (rt.hyphPending()) return Status::NeedMeasure;  // (plan P4-06) its dictionaries first
      resolveHyph();
      const size_t n = boxtree.tops.size();
      if (tops.size() != n || emitted.size() != n) {
        tops.assign(n, {});
        emitted.assign(n, 0);
      }
      // (plan P3-28) an inline host box files its need while its block
      // emits: the block waits for it and emits again with the answer
      BoxPull inlineBoxes(*this, BoxPull::Mode::Ask, /*atEmit=*/true);
      EmitPass pass(boxtree, arena, strs, styles, cfg, diags, nullptr, &rt, &inlineBoxes);
      bool waiting = false;
      std::vector<MeasureItem> none;
      for (size_t t = 0; t < n; t++) {
        if (emitted[t]) continue;
        const u32 pid = boxtree.tops[t].pid;
        if (topWaits(pid)) {  // a token or box answer of this block is pending
          waiting = true;
          continue;
        }
        diags.beginPid(DiagOrigin::Emit, pid);  // a retry replaces its diagnostics
        inlineBoxes.filed.clear();
        pass.top(t, tops[t], none);
        if (!inlineBoxes.filed.empty()) {
          if (pid >= waitBoxes.size()) waitBoxes.resize(pid + 1);
          waitBoxes[pid].insert(waitBoxes[pid].end(), inlineBoxes.filed.begin(), inlineBoxes.filed.end());
          waiting = true;
          continue;
        }
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
      // their block's Emit slice; (plan P3-32) inline images take their
      // intrinsic size here — an unanswered one keeps Measure waiting
      BoxPull images(*this, BoxPull::Mode::Ask, /*atEmit=*/false);
      ObjectEnv oe{arena, strs, styles, cfg.baseSizePx, &diags, &mathEnv, &images};
      diags.origin = DiagOrigin::Emit;
      MeasureRequest missing = resolveWidths(tops, metrics, styles, cfg, &oe);
      diags.pid = ~0u;
      if (!missing.empty() || !images.filed.empty()) return Status::NeedMeasure;
      validThrough = (int)Stage::Measure;
    }
    // layout breaks the paragraphs (plan P1-15): overfull streams report
    // under the Layout origin
    diags.begin(DiagOrigin::Layout);
    // (plan P3-28) a host box at its width: a run that asks is provisional
    BoxPull boxes(*this, layoutAsks < kLayoutAsks ? BoxPull::Mode::Ask : BoxPull::Mode::Settle, false);
    layout = layoutDoc(tops, metrics, strs, cfg, diags, &session().breakMemo, false, &boxes);
    if (!boxes.filed.empty()) {
      layoutAsks++;
      return Status::NeedMeasure;
    }
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
    ObjectEnv oe{arena, strs, styles, cfg.baseSizePx, &diags, &mathEnv};
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
    if (name == "js") return js.js;
    if (name == "lower") return dumpLowerProgram(js.program);
    if (name == "program") return js.program;
    if (name == "tokens") return dumpTokens(syntaxTokens(ast, src, strs), src);
    if (name == "outline") return outlineJson(ast, src, strs, diags) + "\n";
    if (name == "astjson") return astJson(ast, src, strs) + "\n";
    if (name == "ops") return dumpOps(raw);
    if (name == "tree") return dumpTree(tree, strs, styles);
    if (name == "index") return dumpIndex(index, *registry);
    if (name == "semantic") return renderFallback();
    if (name == "css") return renderCss();
    if (name == "blocktree") return dumpBlockTree(boxtree.tops, strs);
    if (name == "mathir") return dumpMathIRs(tops, strs, &mathEnv);
    if (name == "mathbox") return dumpMathBoxes(tops, strs);
    if (name == "hlist") {
      BoxPull answers(*this, BoxPull::Mode::Lookup, false);
      return dumpHLists(tops, strs, styles, &answers);
    }
    if (name == "breaks") return dumpBreaks(layout);
    if (name == "layout") return dumpLayout(layout);
    if (name == "vlist") return dumpVList(layout, tops);
    if (name == "dl") return dumpDisplayList(layout, tops, styles, strs, cfg);
    if (name == "paged") return renderPaged(cfg.pageHeightPx);
    if (name == "html") return render();
    if (name == "diags") return dumpDiags();
    if (name == "diagnostics") return diagnosticsJson() + "\n";  // (plan P3-37)
    if (name == "settings") return settingsJson(cfg) + "\n";
    if (name == "references") return referencesJson();  // (plan P3-21)
    if (name == "docinfo") return docinfoJson();
    if (name == "labels") return labelsProduct(index, *registry, cfg.projectDoc);  // (plan P3-31)
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
    AnchorScope ids(cfg.idPrefix, false, &projectUrls());  // (plan P3-06) render.idPrefix; (P3-31) project.urls
    // paint (plan P1-18): each block's DisplayList, written by the
    // stateless typeset backend
    std::string html;
    writeRoot(html, "tsr-doc", paintRoot(cfg, &layout));
    DLBlock b;
    for (size_t p = 0; p < layout.paras.size(); p++) {
      paintBlock(layout, p, tops, strs, cfg, b, &styles, &metrics);
      writeBlock(html, b, styles, strs, cfg.baseSizePx);
    }
    html += "</div>\n";
    reportWriterDefects();
    return html;
  }

  // The RenderResult frame (plan P3-05; design T7 RenderResult): each
  // block keyed by a 128-bit hash of its body (the block without its
  // positional attributes: data-pid, data-s0, margin-bottom), the blocks the
  // host does not hold (`held`) as the legacy writer spells them — so a
  // frame that sends every block is the legacy document's body —, the
  // root's open tag, the anchors and a generation stamp: a host commits
  // only what changed. Little-endian:
  //   "TSRR"; u32 n, the head as JSON {generation, heightPx, root, anchors:
  //   [[label, pid, class], …], gaps: [each block's margin-bottom as the
  //   writer spells it, "" for none]}; u32 blocks; per block 56 bytes: u32 pid,
  //   s0, s1 (its source range), state (2: exact), f64 hPx, gapAfterPx (< 0:
  //   the last), u64 keyLo, keyHi, u32 off, len (into the HTML, in UTF-16
  //   units — a host decodes the HTML once and slices it; len 0: held);
  //   then the HTML.
  std::string renderResult(const Key128* held, size_t nHeld) {
    if (!renderReady()) return {};
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    AnchorScope ids(cfg.idPrefix, false, &projectUrls());
    std::unordered_set<Key128, Key128Hash> have(held, held + nHeld);
    std::string root;
    writeRoot(root, "tsr-doc", paintRoot(cfg, &layout));
    while (!root.empty() && root.back() == '\n') root.pop_back();
    std::string table, html, anchors, gaps;
    u32 html16 = 0;  // the HTML so far, in UTF-16 units
    auto units16 = [](std::string_view s) {  // a lead byte counts 1, a 4-byte one 2
      u32 n = 0;
      for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? (c >= 0xF0 ? 2 : 1) : 0;
      return n;
    };
    auto put32 = [&](u32 v) { table.append((const char*)&v, 4); };
    auto put64 = [&](u64 v) { table.append((const char*)&v, 8); };
    auto putF = [&](double v) { table.append((const char*)&v, 8); };
    // (plan P3-06) its class, and "block" when a reference to it may show
    // its content in place (the class's preview; never a marker's own label)
    auto anchorRow = [&](StrRef label, u32 pid) {
      if (!label) return;
      std::string cls;
      bool preview = false;
      auto it = index.labels.find(std::string(strs.get(label)));
      if (it != index.labels.end() && it->second.inst != kNoInst) {
        const ElementClass& C = registry->cls(index.instances[it->second.inst].cls);
        cls = C.name;
        preview = it->second.k == LabelTarget::K::Instance && C.preview == ElementClass::Preview::Block;
      }
      if (!anchors.empty()) anchors += ',';
      anchors += '[';
      jsonString(anchors, strs.get(label));
      appendf(anchors, ",%u,", pid);
      jsonString(anchors, cls);
      anchors += preview ? ",\"block\"]" : ",\"\"]";
    };
    const u32 n = (u32)layout.paras.size();
    put32(n);
    DLBlock b;
    for (size_t p = 0; p < layout.paras.size(); p++) {
      paintBlock(layout, p, tops, strs, cfg, b, &styles, &metrics);
      // its key: its open tag without the positional attributes, then its
      // nodes (written in place after its legacy open tag; kept unless held)
      std::string open;
      writeBlockOpen(open, b, false);
      const size_t at = html.size();
      writeBlockOpen(html, b, true);
      const size_t nodesAt = html.size();
      writeBlockNodes(html, b, styles, strs, cfg.baseSizePx);
      Hasher kh;
      kh.bytes(open.data(), open.size());
      kh.bytes(html.data() + nodesAt, html.size() - nodesAt);
      const Key128 key = kh.done();
      const std::string_view body = std::string_view(html).substr(at);
      u32 s1 = b.srcBase;
      for (const DLNode& x : b.nodes) {
        s1 = std::max(s1, x.span.end);
        anchorRow(x.anchor, b.pid);
        anchorRow(x.anchor2, b.pid);
      }
      for (const DLRun& r : b.runs) anchorRow(r.id, b.pid);
      put32(b.pid);
      put32(b.srcBase);
      put32(s1);
      put32(2);  // exact
      putF(suToPx(b.h));
      putF(b.gapAfterPx);
      put64(key.lo);
      put64(key.hi);
      if (p) gaps += ',';
      gaps += '"';
      if (b.gapAfterPx >= 0) fmtPx(gaps, b.gapAfterPx);
      gaps += '"';
      const bool send = !have.count(key);
      const u32 len16 = send ? units16(body) : 0;
      put32(html16);
      put32(len16);
      html16 += len16;
      if (!send) html.resize(at);
    }
    generation = ++session().generation;
    std::string head = "{\"generation\":";
    appendf(head, "%llu,\"heightPx\":%g,\"idPrefix\":", (unsigned long long)generation,
            (double)layout.docHeightSu / 64.0);
    jsonString(head, cfg.idPrefix);
    head += ",\"root\":";
    jsonString(head, root);
    head += ",\"container\":" + containerJson();
    head += ",\"anchors\":[" + anchors + "],\"gaps\":[" + gaps + "]}";
    std::string out = "TSRR";
    const u32 hl = (u32)head.size();
    out.append((const char*)&hl, 4);
    out += head;
    out += table;
    out += html;
    reportWriterDefects();
    return out;
  }

  // (plan P3-21; design T9 A2) the resources the document references that
  // the engine knows of — its images' sources, where they stand, whether the
  // URL policy admits them — one JSON row per line; the host's manifest
  // adds its execution loads and the declared fonts
  std::string referencesJson() const {
    std::string out;
    std::function<void(const ContentNode*)> walk = [&](const ContentNode* n) {
      if (n->kind == Kind::image)
        if (StrRef src = attrStr(n, ArgK::src)) {
          const std::string_view v = strs.get(src);
          out += "{\"role\":\"image\",\"src\":";
          jsonString(out, v);
          appendf(out, ",\"s\":%u,\"e\":%u,\"allowed\":%s}\n", n->span.start, n->span.end,
                  safeImageSrc(v) ? "true" : "false");
        }
      for (const ContentNode* k : n->kids) walk(k);
    };
    if (tree.root) walk(tree.root);
    return out;
  }
  // (plan P3-21) what a page around the document needs: its language and
  // title (its first heading's text)
  std::string docinfoJson() const {
    std::string title;
    std::function<bool(const ContentNode*)> first = [&](const ContentNode* n) {
      if (n->kind == Kind::heading) {
        excerptInto(n, strs, title);
        return true;
      }
      for (const ContentNode* k : n->kids)
        if (first(k)) return true;
      return false;
    };
    if (tree.root) first(tree.root);
    std::string out = "{\"lang\":";
    jsonString(out, cfg.lang);
    out += ",\"langSource\":";  // (plan P3-30) host, document ($.doc) or detected (auto)
    jsonString(out, langSource);
    out += ",\"title\":";
    jsonString(out, title);
    out += "}\n";
    return out;
  }

  // (plan P3-19; design T7 S11) what the container applies: each font
  // role's content-height factor — (ascent + descent) / em of a regular
  // face of it the document measured — as the line-height its runs take
  // (the contract's --tsr-lh-*), so a run's line box is its content area
  // and a line's baseline sits at its top plus its tallest ascent, whatever
  // the host's line-height
  std::string containerJson() const {
    double f[4] = {0, 0, 0, 0};  // body, cjk, mono, monoCjk
    for (StyleId id = 0; id < (StyleId)styles.count(); id++) {
      const Styling& st = styles.get(id);
      if (st.fontFamily || st.weight > 500 || st.italic || !metrics.hasVmet(id)) continue;
      const int r = (st.fontRole == FONTROLE_MONO ? 2 : 0) + (st.script == SCRIPT_CJK ? 1 : 0);
      if (f[r] > 0) continue;
      const VMet& v = metrics.vmet(id);
      const double em = emPx(cfg.baseSizePx, st);
      if (em > 0) f[r] = suToPx(v.ascent + v.descent) / em;
    }
    static const char* const kRole[] = {"body", "cjk", "mono", "monoCjk"};
    std::string out = "{\"lh\":{";
    bool first = true;
    for (int r = 0; r < 4; r++) {
      if (f[r] <= 0) continue;
      appendf(out, "%s\"%s\":%.4f", first ? "" : ",", kRole[r], f[r]);
      first = false;
    }
    out += "}}";
    return out;
  }

  // paged rendering for print (pages-design.md §2); needs a finished layout
  std::string renderPaged(double pageHeightPx) {
    if (!renderReady()) return {};
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    AnchorScope ids(cfg.idPrefix, false, &projectUrls());
    // the sheets (layout/paginate.cc), each band's nodes rebased into its
    // sheet by the same stateless writer
    // (plan P3-12) the page's geometry: its content height; a sheet's
    // inserts sit an em below its flow
    PageSpec spec;
    spec.h = suRoundPx(pageHeightPx);
    spec.footnoteSkip = suRoundPx(cfg.baseSizePx);
    // (plan P3-14) blocks for one medium only (media): the sheets' own
    // layout, without the screen's and with the paged ones (its overfull
    // reports are the screen layout's already)
    bool media = false;
    for (const TopBlock& t : tops)
      for (const LayoutBlock& b : t.tree->blocks) media = media || b.tr.media;
    LayoutResult own;
    if (media) {
      DiagSink scratch;
      BoxPull boxes(*this, BoxPull::Mode::Lookup, false);  // (no pull at render: a box at a new width keeps its declared size)
      own = layoutDoc(tops, metrics, strs, cfg, scratch, &session().breakMemo, /*paged=*/true, &boxes);
    }
    const LayoutResult& lay = media ? own : layout;
    const PageResult pr = paginate(lay, spec, &diags);
    std::vector<DLBlock> dl(lay.paras.size());
    for (size_t p = 0; p < lay.paras.size(); p++) paintBlock(lay, p, tops, strs, cfg, dl[p], &styles, &metrics);
    std::string html;
    writeRoot(html, "tsr-doc tsr-paged", paintRoot(cfg));
    for (const Page& pg : pr.pages) {
      {
        Tag t(html, "div");
        t.attrSafe("class", "tsr-sheet");
        // (plan P3-12) a sheet an atom overflows shows it: never clipped —
        // nor one a table wider than the measure crosses (plan P3-14, D-Y09)
        bool wide = false;
        for (const PageBand& band : pg.bands) wide = wide || lay.paras[band.para].overflowR > lay.paras[band.para].w;
        t.decl("position", "relative")
            .decl("overflow", pg.overflow > 0 || wide || lay.gutterSu > 0 ? "visible" : "hidden")
            .px("height", suToPx(pr.height));
        // (plan P3-16) line numbers left of the measure: the sheet clips at
        // the measure extended by their gutter
        if (lay.gutterSu > 0 && pg.overflow <= 0 && !wide) {
          char buf[48];
          std::string clip = "inset(0px 0px 0px -";
          clip.append(buf, fmtPxBuf(buf, suToPx(lay.gutterSu)));
          clip += ")";
          t.decl("clip-path", clip);
        }
        t.open();
        html += "\n";
      }
      // (plan P3-07) a sheet's bands in their block's wrapper, one per block
      // per sheet: copy's block identity on paged sheets (a block cut across
      // sheets keeps one)
      for (size_t k = 0; k < pg.bands.size(); k++) {
        const PageBand& band = pg.bands[k];
        if (k == 0 || pg.bands[k - 1].para != band.para) {
          Tag t(html, "div");
          t.attrSafe("class", "tsr-band");
          if (!dl[band.para].role.empty()) t.attr("data-role", dl[band.para].role);
          t.num("data-b", dl[band.para].pid);
          t.open();
          html += "\n";
        }
        // its place on the sheet; a repeated header row carries no ids
        const Su at = (Su)((i64)lay.paras[band.para].y - pg.top + band.yShift);
        if (band.repeat) {
          AnchorScope none(cfg.idPrefix, /*suppress=*/true);
          writeNodes(html, dl[band.para], band.lo, band.hi, at, styles, strs, cfg.baseSizePx);
        } else {
          writeNodes(html, dl[band.para], band.lo, band.hi, at, styles, strs, cfg.baseSizePx);
        }
        if (k + 1 == pg.bands.size() || pg.bands[k + 1].para != band.para) html += "</div>\n";
      }
      html += "</div>\n";
    }
    html += "</div>\n";
    reportWriterDefects();
    return html;
  }

  // (plan P3-27) how the plain page shows formulas: render.math, a11y.mathLabel
  SemanticMath semanticMath() {
    SemanticMath m;
    m.boxes = cfg.renderMath == kRenderMathBoxes;
    m.label = cfg.a11yMathLabel;
    m.basePx = cfg.baseSizePx;
    m.env = &mathEnv;
    m.arena = &arena;
    return m;
  }
  // needs only the post-resolve tree — valid before any measurement
  std::string renderFallback() {
    diags.begin(DiagOrigin::Render);
    writerDefects() = {};
    AnchorScope ids(cfg.idPrefix, false, &projectUrls());
    std::string html = renderSemantic(tree, strs, styles, &rt, registry, &cascade, &nodeProps, semanticMath());
    (void)rulesToCss(cascade, tree, strs, &diags, registry);  // what its stylesheet leaves out (rule-no-css)
    reportWriterDefects();
    return html;
  }

  // (plan P3-06; design T7 ops.fragment) a preview of what `label` names:
  // the post-resolve semantic HTML of its element (references resolved, no
  // ids, no reference back to a flow marker: a note's ↩), stamped by the
  // caller with `generation`; "" when nothing carries the label
  std::string renderFragment(std::string_view label) {
    AnchorScope ids(cfg.idPrefix, /*suppress=*/true);
    const std::function<bool(StrRef)> backlink = [&](StrRef to) {
      auto it = index.labels.find(std::string(strs.get(to)));
      return it != index.labels.end() && it->second.k == LabelTarget::K::Marker;
    };
    return renderSemanticFragment(tree, strs, styles, &rt, registry, &cascade, &nodeProps, label, backlink,
                                  semanticMath());
  }

  // the semantic page's stylesheet (rulesToCss, plan P3-01): what the rules
  // add to the scopes the page writes inline; valid with the semantic page
  std::string renderCss() { return rulesToCss(cascade, tree, strs, nullptr, registry); }

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
  // (plan P3-37; design T9 M9) the diagnostics as data, for hosts (the VS
  // Code extension): [{sev, code, span: [s, e], message, origin, pid?}] —
  // the same rows, in the same order, as `diags` (kept for humans)
  std::string diagnosticsJson() const {
    static constexpr const char* kOrigin[] = {"settings", "compile", "ingest", "resolve",
                                             "provide",  "emit",    "layout", "render"};
    std::string out = "[";
    for (const Diag& d : diags.items) {
      if (out.size() > 1) out += ',';
      out += "{\"sev\":\"";
      out += d.sev == Sev::Error ? "error" : d.sev == Sev::Warning ? "warning" : "info";
      out += "\",\"code\":";
      jsonString(out, d.code);
      appendf(out, ",\"span\":[%u,%u],\"message\":", d.span.start, d.span.end);
      jsonString(out, d.msg);
      out += ",\"origin\":\"";
      out += kOrigin[(u8)d.origin];
      out += '"';
      if (d.pid != ~0u) appendf(out, ",\"pid\":%u", d.pid);
      out += '}';
    }
    out += "]";
    return out;
  }
};

}  // namespace tsr
