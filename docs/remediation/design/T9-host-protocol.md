# T9-host-protocol

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

The engine/host boundary gets a small vocabulary of its own, generated from five tables, and every host drives it through one shared loop. stages.def describes the pipeline (Compile, Execute[host], Ingest, Resolve, Emit, Measure, Break, Layout, Paginate, Paint) and records, as a fact about the code, how each stage may be re-run. It is the domain of every Affects bit, every product and every diagnostic. resources.def lists what the engine may ask for after Ingest: typed, content-addressed keys and validated answers. inputs.def lists declared blobs, such as label manifests, that arrive before Ingest. T4's settings registry carries the Precedence and Affects columns; a separate host-policy schema holds what only the runtime reads. products.def lists what a host may run to and read. A Doc is a pure function of (ops, settings, declared inputs, answers). Stages only move forward. The exceptions are a per-paragraph retry inside Emit and re-entry into the reentrant tail (Break onward). Any other change forks a fresh Doc from the retained ops on the same Session, which makes v2 §9's 'a resize is a full re-typeset' mechanical rather than hand-placed. Needs are keyed by their complete content (the full MetricKey, canonical key bytes), so a cached answer can never be stale; there are no generations and no invalidation calls. A Pending need defers its paragraph (Emit) or blocks a resumable stage (Measure). A Failed need degrades the quantity that consumed it, locally and with a diagnostic. Answers come, in order, from the Doc, from in-engine answerers, from the Session (content-keyed kinds only), and finally from the host's single provider registry, where built-in and user providers have equal standing. The worker, Node export, playground, tsrc, golden runner and fuzzers all run the same drive(doc, target) loop over ResourceHost/ProviderSet. That layer owns location, transport, timeouts, failure caching and the manifest.

## Diagnosis

The engine/host boundary has no vocabulary of its own, so it mirrors the feature list. There are six root causes.
(1) There is no typed notion of 'a datum the engine needs'.
- Tokens, images and math text each grew their own request struct, Kind scan, pending guard, provide method, JSON section, export, worker branch and native stub (doc.h:30-56, 152-255; wasm_api.cc:96-184; worker.mjs:94-137).
- Answers were stored in authored structures. Image dims overwrite the author's w/h (doc.h:212-227), and tokens rewrite the tree (tokens.cc:21-90).
(2) There is no typed notion of 'a setting'.
- There is one C setter per knob, and hosts re-list them (wasm_api.cc:42-76; worker.mjs:157-193; shell.mjs:331-397).
- Native drivers configure by filename substring (tests.cc:414-426). tsrc cannot reproduce the goldens: code/wrap gives 11750su against the golden's 8909su.
(3) There is no stage model.
- The phases are hand-ordered and guarded by two booleans, one of them dead (doc.h:51, 58, 245-365), so invalidation is hand-placed and wrong. setWidth leaves the width-dependent emit products stale (doc.h:392), and a re-emit duplicates diagnostics.
- Stages also mutate their own inputs. The resolver rewrites the tree in place (resolve.cc:176-178, 487-508), and Measure writes widths into the emitted blocks (emit.cc:926, 981). Re-running a stage is therefore unsafe in general, a fact no flag records.
- Every stage receives the whole Config, so nothing checks which stage reads which setting. The float tracker reads paragraph spacing inside Break (doc.h:266-267).
(4) Reuse lives outside the engine and is keyed by presentation and ambient state, not content.
- There are five caches, each with its own keys and failure policy.
- The measure cache keys on a StyleId (measure.h:21). It omits dppx and face state, and font loading clears it implicitly (worker.mjs:41-65).
(5) Cross-language contracts are hand-synced copies: token tags, the priority order, language lists and the diagnostic text format.
(6) Host I/O has no locator and no notion of what a document references.
- Only the bibliography sees baseUrl.
- Images with an authored size are never requested (doc.h:189), so static export cannot list a document's assets.

## Abstractions

### A1 ResourceTable + resources.def (content-addressed engine needs, one wait state)

**owner_layer**

Cross-cutting engine core:
- engine/src/resource/{resources.def, resource_table.h, metric_key.h, codec.gen.h}
- tools/gen-res.mjs (a sibling of gen-ops-ts) generates runtime/src/shared/resources.gen.mjs

**purpose**

Holds every datum the engine can only obtain after Ingest from outside (ops, settings, inputs), in one place. It covers typed keys and answers, deduplication, per-paragraph deferral, validated decoding and uniform local degradation.

**definition**

```
Admission rule: a RES row is a datum the engine can only obtain after Ingest, keyed by content the engine computes. Anything known before Ingest is a setting (A4) or a declared input (A7), never a resource.

resources.def (X-macro, RES_VERSION 1) has the columns RES(name, id, KEY, ANS, cache, docProviders, consumers):
  RES(textWidth,  1, KEY(MetricKey mk, Str text), ANS(F64 px), Content, no, Emit|Measure)
  RES(fontVmet,   2, KEY(MetricKey mk), ANS(F64 asc, F64 desc), Content, no, Emit|Measure)
  RES(fontFace,   3, KEY(Str family, Str src, U16 weight, U8 style), ANS(U8 status /*loaded|fallback*/), None, no, Emit|Measure)
  RES(codeTokens, 4, KEY(Str lang, Str maskedText), ANS(Str canonLang, StrList classes, U32List runs /*start,end,classIdx; UTF-8*/), Content, yes, Emit; product semantic)
  RES(boxInfo,    5, KEY(U8 kind /*image|svg|html*/, U32 source, Str ref /*src, or content for inline*/, F64 availPx /*0 unless width-dependent*/), ANS(F64 w, F64 h, F64 baseline), Host for src keys / Content for content keys, yes, Emit (Layout after T6))
  // reserved for T5: hyphenPatterns(Str lang) -> Bytes

MetricKey is the complete measurement tuple: {Str stack, u64 faceDigest, f64 sizePx, u16 weight, u8 italic, Str features, Str lang, f64 dppx}.
- stack is the effective CSS family stack once fontFace fallbacks are removed.
- faceDigest hashes the descriptors of the loaded declared faces in that stack.
- T4 supplies metricKeyOf(StyleId, Settings, faceAnswers).
- Any change of font, face status, dppx, features or lang produces a different key, so no invalidation channel is needed.

Keys:
- canonicalKey<K>() is self-contained bytes: UTF-8 strings, f64 bit patterns with -0 and NaN normalised, fixed-width integers. It is used only for Session lookup and write-through.
- Within a Doc, deduplication uses integer ids (MetricKeyId, StrRef, …) through a per-doc index, so need() costs O(1) integer hashing per occurrence.

C++:
  enum class ResState : u8 { Pending, Ready, Failed };
  template<ResKind K> struct Need { ResId id; ResState st; const Ans<K>* ans; };  // ans points only into this Doc's table arena
  struct ResourceCtx { ResourceTable& rt; Stage stage; u32 pid; bool deferred = false; };
  class ResourceTable {
    template<ResKind K> Need<K> need(const Key<K>&, ResourceCtx&);  // doc-local → in-engine answerer → Session (bytes copied in) → Pending, with (pid) recorded as a waiter
    BatchId encodeRequests(ByteSink&);   // every Pending need not in an open batch; batches are numbered
    ProvideReport provide(std::span<const u8>, DiagSink&);  // generated decoder and validators; settled needs wake their waiter pids
  };

Consumer rules, the same for every kind; stage classes come from stages.def (A3):
- Once stages (Compile, Ingest, Resolve) may not call need(); this is asserted.
- PidRetry (Emit): on Pending, use a stand-in and set cx.deferred. The pass's TopBlock and its (Emit, pid) diagnostic slice are discarded, and the pid is re-run once every need it waited on has settled.
- Resumable/Reentrant stages (Measure; Layout after T6) collect every need and then block. A resumed pass continues with only the unresolved items.
- Quantisation happens in the consumer: Measure applies ceil + ε. The table and the Session hold raw px, so ε is a pure Measure input.

Failed answers degrade the quantity that consumed them and raise one diagnostic in the Resource(resId) slice:
- textWidth, derived widths: if any operand of a space-context gap, hyphen-junction kern, snap probe or punctuation half failed, the derivation is dropped (plain space, no kern, default grid).
- textWidth, base widths: a failed width takes a generated per-codepoint em bound × 1.2 (default 1em; EAW W/F 1em; a table of multi-em code points such as U+2E3A = 2, U+2E3B = 3, U+FDFD = 4; emoji 1.3em), and the pid gets 'measure-failed'. This is best effort, not provable; any residual overflow is confined to the line by nowrap (v2 §7 rule 5).
- fontVmet: ascent 1.0em, descent 0.3em.
- fontFace: fallback (pages-design W semantics).
- codeTokens: plain code.
- boxInfo: placeholder box for an image; a raw unit keeps its declared or one-leading height.

Doc::Status is {Ok, NeedResources, Stalled}.

Wire format (little-endian, length-prefixed):
- Request: 'TSRQ' ver | batchId | string blob | MetricKey table | per kind {u16 kind, u32 n, u32 resId[n], key columns}.
- Answer: 'TSRA' ver | batchId | per kind {u16 kind, u32 n, u32 resId[n], u8 status[n], u8 flags[n] /*bit0 store*/, answer columns, u32 msg[n]}.
- textWidth answers are one f64 column.

The decoder (fuzz target fuzz_resanswer) validates every row. Each of the following turns the row into Failed with 'provider-invalid':
- a wrong batch, or an unknown, duplicate or already-settled resId;
- a non-finite or negative number;
- token runs that are unsorted, overlapping, past the body or off a UTF-8 boundary, or whose classIdx is out of range.
Any resId of the answered batch that has no row becomes Failed('provider-missing'). Rows with store=0 never reach the Session.
```

**surface**

No markup surface. Hosts reach the table only through drive().
Debugging: tsr2_get(doc,'resources') and tsrc --stage=resources list every need with its state, origin (doc, answerer, Session or host) and waiter pids.

**replaces**

- engine/src/api/doc.h:30-48 TokenReq/ImageReq vectors (each id space is a vector index)
- engine/src/api/doc.h:53-56 mathTextMissing side channel; doc.h:247-255 whole-document re-emit
- engine/src/api/doc.h:152-204 Kind-keyed scans and pending predicates; doc.h:246 OR-ed barrier
- engine/src/api/doc.h:208-243 provideImage (setNum into the author's ArgK::w/h at :212-227; provide-time 'image-load' diagnostic at :229) and provideTokens
- engine/src/api/doc.h:367-378 pendingRequests with linear vmet dedupe
- engine/src/api/wasm_api.cc:96-184 hand-written request JSON and four provide exports (per-word malloc and re-intern at :178-180)
- engine/src/measure/measure.h:21 StyleId-keyed u64 packing; measure.h:24-26 ε baked in at provide time; measure.h:43-51 MeasureRequest
- engine/src/emit/emit.h:124-129 MathTextCtx parameter; engine/src/math/math.cc:579-597 recording of missing metrics
- engine/src/code/tokens.cc:21-90 foldTokens mutating the post-resolve tree

### A2 ResourceHost / ResourceLocator / ProviderSet (one provider registry, one locator, one manifest)

**owner_layer**

Host layer, outside the engine core (I5):
- JS: runtime/src/shared/resources/{host.mjs, locator.mjs, lru.mjs, providers/*.mjs}
- runtime/src/shared/url_policy.gen.mjs
- Native mirror: engine/src/api/driver.h ProviderSet, with providers under engine/src/{measure,code}

**purpose**

One place that turns a need, or an execute-time load, into an answer. It applies one locator, one transport rule, one timeout and failure-caching policy, and one provider registry in which built-in, host and document providers stand on equal terms. It also records one manifest of everything a document references.

**definition**

```
JS:
  class ResourceHost {
    constructor({ locator, policy /*host-policy schema, A4*/ })
    register(kind, provider)   // provider = { module: url }  (imported in the worker; exports match(key) and resolve(items, ctx))
                               //          | { capability: name }  (runs on the main thread over cap?/cap, one batch per round)
    async answer(requestsU8) -> answersU8  // per-kind timeout from policy; a thrown provider fails all rows of its kind; rows from document-registered providers carry store=0
    async load(src, { as: 'text'|'json'|'bytes'|'module', source /*SourceId*/, requester: 'exec'|'input' })
    manifest(job) -> [{ url, role, source, status, requester }]  // a view over the 'references' product, the execution load log and fonts.declared
    seed(entries)                          // static/hydration preload
  }
  class ResourceLocator { constructor({ bases /*SourceId → URL or dir*/, root }); resolve(src, source, requester) -> { url } | { file } | { denied } }
  class LruCache { constructor({ maxEntries, maxBytes, ttlMs, failureTtlMs }) }  // validators: ETag/Last-Modified over http, (mtime, size) for files

Locator rules:
- Schemes come from the generated url_policy table, the same table the engine's safeImageSrc uses (support.h:214).
- File access is confined to root.
- A relative src resolves against the base of the SourceId that issued it, so a module's loads resolve against the module's URL.
- Project outputs (outDir, *.labels.json) are served only to requester 'input', never to 'exec'.

Caching:
- URL-keyed answers (boxInfo by src, and load) are cached only in LruCache, with validators. They never enter the Session.
- Content-keyed answers go to the Session (A5).

Document-registered providers (#use module exports {providers}):
- allowed only for kinds marked docProviders=yes, namely codeTokens and boxInfo, whose keys are authored content;
- scoped to their document;
- always answer with store=0.

Built-in providers:
- textWidth/fontVmet: CanvasMeasurer measures under the key, building the font string from mk.stack and mk sizes, never from ambient state. Its memo deduplicates only within one round.
- fontFace: FontFace.load() under policy fonts.loadTimeoutMs (4000). Loads start eagerly when fonts.declared is known. Faces sharing a family name but not a src are registered under a descriptor-derived alias. Failures add a host diagnostic.
- codeTokens: hl-core (A6).
- boxInfo, image: parallel fetch and header sniffing for PNG, GIF and WebP; a decode fallback for JPEG/EXIF and SVG; then the <img> capability.
- boxInfo, html: the 'measureHtml' capability at availPx.

Native mirror:
  struct ProviderSet { /* one std::function slot per RES row */ };
  ProviderSet mockProviders(const FixtureSpec&);  // testing.md §2 mock widths, fontFace loaded, fixture image dims (default 512x384), statically linked tokens, fixture html boxes
```

**surface**

- createEngine({ settings, policy, providers: [{ kind: 'codeTokens', module: '/hl/tla.mjs' }], capabilities: { measureHtml } })
- renderTsm(src, { settings, locator: { base, root }, providers }) -> { html, diagnostics, ok, manifest, settings, css }
- In document programs and handlers: $.load(src, {as}) and ctx.load(src, {as})
- #use modules may export { fences, regions, providers }

**replaces**

- runtime/src/worker/worker.mjs:16-35 tokenCache (clear-all at 400); worker.mjs:41-65 loadFonts (marked loaded before success; cache cleared before the race)
- runtime/src/worker/worker.mjs:66-90 imageSize: serial, no baseUrl, failures cached forever, one-off askMainForDims
- runtime/src/worker/worker.mjs:94-137 measureLoop with per-resource branches
- runtime/src/worker/executor.mjs:43-56 loadResource (any absolute path at :48-49) and :242-266 bibliography fetch path
- runtime/src/worker/canvas_measure.mjs:8-40 as a cross-document cache (it becomes a per-round dedup)
- runtime/src/worker/tokens.mjs:52-55 grammar failure cached for the worker's lifetime
- runtime/src/main/shell.mjs:218-229 'image-dims?' protocol
- runtime/src/node/render.mjs:40-49 partial driver that answers tokens only
- engine/src/api/native_cli.cc:24-36 and engine/test/tests.cc:314-325 duplicated stubs
- engine/src/support/support.h:212-224 hand-written scheme policy (generated from url_policy.def instead)
- tools/export-static.mjs:81-93 copies engine assets only

### A3 Pipeline: stages.def with rerun classes, products.def, fork-on-rebuild, origin-sliced diagnostics, tsr2_ C ABI, one drive loop

**owner_layer**

api/ as a thin sequencer, plus cross-cutting diagnostics:
- engine/src/api/{stages.def, products.def, driver.h, wasm_api.cc}
- engine/src/support/diagnostics.def
- runtime/src/shared/drive.mjs
- runtime/src/worker/worker.mjs (scheduling)

**purpose**

One description of the pipeline that every host drives, inspects and invalidates in the same way. Whether a stage may be re-run is a recorded fact, not a hand-placed flag.

**definition**

```
stages.def: STAGE(id, side, unit, rerun)
  STAGE(Compile, Engine, Doc, Once)   STAGE(Execute, Host, Doc, Once)   STAGE(Ingest, Engine, Doc, Once)   STAGE(Resolve, Engine, Doc, Once)
  STAGE(Emit, Engine, Pid, PidRetry)  STAGE(Measure, Engine, Doc, Resumable)
  STAGE(Break, Engine, Doc, Reentrant)   STAGE(Layout, Engine, Doc, Reentrant)   STAGE(Paginate, Engine, Doc, Reentrant)   STAGE(Paint, Engine, Doc, Reentrant)
Generated from it: the Affects enum (A4), the stage column of products.def, DiagSink stage ids and the per-stage settings views.

The rerun classes reflect today's code:
- Resolve mutates the instantiated tree (resolve.cc:176-178, 487-508).
- Measure writes widths into the emitted blocks (emit.cc:926, 981).
- Break resets its per-unit state (doc.h:276-280).
- Layout and the serializers return new products.

Rules:
- A settings patch whose first affected stage is s is applied in place only if every stage in [s, validThrough] is Reentrant; validThrough then becomes s-1.
- Otherwise tsr2_set_config changes nothing. It returns REBUILD when s comes after Execute and REEXECUTE otherwise.
- On REBUILD, drive() forks: tsr2_doc_fork(doc, patch) creates a fresh Doc on the same Session from the retained ops blob (doc.h:26 RawOps raw) and the declared inputs. On REEXECUTE, drive() re-runs compile and execute.
- relayout and paginate therefore either re-enter the reentrant tail or fork. They never mutate a live doc's earlier products, so arenas stay per Doc (architecture §2.3).

products.def, PRODUCT(name, stage, needs, fn). Options select serialisation only; anything that changes geometry is a setting.
- Compile: skeleton, ast, js.
- Execute: ops (recorded by the host).
- Resolve: tree; semantic (needs codeTokens unless {partial:true}); references (every external URL with role image|link|raw|font|module, plus source and span); labels (A7).
- Emit: mathbox. Measure: blocks. Break: breaks. Layout: layout.
- Paginate: paged (page.height is a setting).
- Paint: html, alias typeset; {format:'paras'} gives T7's structured result.
- Any stage: settings ({format:'json'|'css'}), resources, diagnostics (JSON), diags (text).

Doc:
  struct Doc { Settings s; Inputs in; RawOps raw; ResourceTable rt; Stage validThrough; Status advance(Stage target); int patch(std::string_view json); };
Emit runs emitTop(child, pid) for each top-level child. emit.cc:891-910 is already a per-child walk, and Emitter state resets per group (emit.cc:18-22, 819).

DiagSink: each Diag gains {Origin origin; u32 pid; u32 source}.
- Origin = Stage(s) | Settings | Input(name) | Resource(resId).
- Stage(s, pid) slices are erased by beginPass(s, pid) and by in-place re-entry.
- Settings diagnostics are replaced on each patch; Input diagnostics are replaced with their input.
- Resource diagnostics live with the answer, and consumers refer to them instead of re-raising them.
- Execute diagnostics come from the host executor and are merged by drive().
- Output order: Settings, Input, Execute, then (stage, pid, insertion), then Resource in resId order.
- Codes come from diagnostics.def {code, defaultSev, origin}, which also generates diagnostics.gen.mjs.

C ABI. All new exports use the tsr2_ prefix. The old tsr_* symbols stay as real translating shims for one engine-dist release.
  u32 tsr2_abi_version();  // OPS_VERSION | RES_VERSION | settings schema major | inputs major
  Session* tsr2_session_new(const char* json); int tsr2_session_free(Session*);  // PRECONDITION while Docs are live
  Doc* tsr2_doc_new(Session*); Doc* tsr2_doc_fork(Doc*, const char* patchJson); void tsr2_doc_free(Doc*);
  int tsr2_set_config(Doc*, const char* json);  // 0 applied | 4 REBUILD | 5 REEXECUTE
  int tsr2_set_input(Doc*, const char* name, const u8*, u32);
  int tsr2_compile(Doc*, const char* src, u32 len); int tsr2_ingest(Doc*, const u8*, u32);
  int tsr2_typeset(Doc*, const char* target);  // 0 OK | 1 NEED_RESOURCES | 2 STALLED | 3 PRECONDITION
  const u8* tsr2_requests(Doc*); int tsr2_provide(Doc*, const u8*, u32);
  const u8* tsr2_get(Doc*, const char* product, const char* optsJson);
  const u8* tsr2_parse_fragment(Doc*, const char* src, u32 len, u32 baseOffset, u32 source);  // only if T2 keeps m.parse in WASM

One loop for every host:
- Native: bool driveToCompletion(Doc&, ProviderSet&, Stage target, u32 maxRounds).
- JS: async drive(M, job, host, target).
- drive() checks tsr2_abi_version before anything else and fails hard on a mismatch.
- If a host's answer() throws, drive submits an empty answer for that batch, so the needs fail instead of spinning.

Worker scheduling, per docId:
- A mailbox holds at most one pending update. A newer update replaces an older one that has not started.
- Relayout and paginate jobs queue behind it in order.
- The running job checks its generation at every round boundary and every await. Once superseded, it aborts and frees its doc.
- Only the newest successful doc replaces the live one.
```

**surface**

- tsrc --stage=<product> [--fixture=X.fixture.json] [--profile=golden] [--set k=v]
- tsr2_get(doc,'<product>') in the playground
- The VSCode extension consumes the diagnostics JSON

**replaces**

- engine/src/api/wasm_api.cc:39-216: 24 exports; per-kind output buffers at :20; tsr_render_pages overwriting htmlOut at :205
- engine/src/api/doc.h:51, 58, 79-80, 232, 242, 253, 363, 392: emitted/laidOut flags (laidOut is never read)
- engine/src/api/doc.h:69-74 resolve fused into ingest; doc.h:245-365 hand-ordered guarded phases (float and table dispatch go to T6)
- engine/src/api/doc.h:394-403 text diagnostics; editors/vscode-tsm/src/preview.js:128 regex parse
- engine/src/api/native_cli.cc:69-104 stage if-chain; engine/test/tests.cc:404-466 golden runner stage list and the '*paged*' filename rule
- runtime/src/worker/worker.mjs:157-224 per-message pipeline; worker.mjs:216-218 last finisher frees the newer doc; worker.mjs:228-256 paginate and relayout mutating the live doc's width

### A4 Host settings channel and host-policy schema (over T4's registry)

**owner_layer**

Cross-cutting settings, host scope:
- engine/src/api/host_settings.cc over T4's generated engine/src/settings/settings.gen.h and per-stage views
- runtime/src/shared/{settings.gen.mjs (T4), policy.gen.mjs}
- test/profiles/*.json and test/fixtures/**/X.fixture.json

**purpose**

Replace every per-knob channel with one typed settings document. Each stage provably reads only the settings it declares, and a change invalidates exactly what the stage model says it does.

**definition**

```
Consumes T4 rows of the form SET(id, 'dotted.path', Type, default, Precedence, Scope, Affects ⊆ stages.def).
- Precedence is T4's lattice: HostOnly | HostDefault (a document layer overrides) | HostForce (overrides the document) | DocOnly.
- Affects is a set of stages; firstStage is its minimum.
- The same Affects vocabulary classifies T4's style properties: metric-affecting means Emit/Measure, paint-only means Paint.

Generated per-stage views:
- struct ResolveSettings, EmitSettings, MeasureSettings, BreakSettings, LayoutSettings, PaginateSettings and PaintSettings each hold only the rows whose Affects names that stage.
- Each stage function takes only its own view, for example emitDoc(…, const EmitSettings&), so reading an undeclared row is a compile error.
- Interim readers that the views make visible:
  - the float tracker reads lineHeight, paraSpacingEm and baseSizePx in Break (doc.h:266-268) until T6 moves it;
  - math text measurement reads the font rows in Emit until T8;
  - emit reads the penalties and widthPx (emit.cc:126-128, 201-239, 317, 332, 591, 721).

Rows T9 contributes:
- viewport.width: HostOnly; EMIT until T6's WidthSpec, then BREAK.
- env.dppx: HostOnly; EMIT|MEASURE through MetricKey.
- page.size and page.margins: HostDefault; PAGINATE (T6).
- fonts.declared[{family, src, weight, style}]: HostDefault, documents may add faces; EMIT|MEASURE through MetricKey and fontFace.
- doc.lang: HostDefault.
- ext.<name>.*: an open namespace, HostDefault, EXECUTE. Handlers read it as ctx.settings, which exposes only the layers that exist before execution: host, and front matter if T1/T4 adopt it.

Host policy is not in this registry. A separate host-policy schema, generated by the same tool, configures ResourceHost/ProviderSet:
- resources.timeoutMs.<kind> (imageInfo 15000)
- failureTtlMs
- maxRounds (64)
- fonts.loadTimeoutMs (4000)
- cache sizes (400 / 200000 / 16384)
The Session budget is set only through tsr2_session_new.

Applying a patch:
  int Doc::patch(std::string_view json) {
    auto r = s.dryRun(json, Layer::Host, diags /*Settings origin*/);
    if (!reentrantSpan(r.firstStage)) return r.firstStage <= Stage::Execute ? REEXECUTE : REBUILD;  // nothing applied
    s.commit(r); invalidateFrom(r.firstStage); return 0;
  }
Patch diagnostics:
- 'setting-unknown': info (document-model §11).
- 'setting-type': wrong type or out of range.
- 'setting-precedence': a layer setting a row its precedence forbids.
Numbers are parsed with std::from_chars.

Fixtures:
- test/profiles/golden.json = {viewport:{width:300}, text:{baseSize:16}}.
- X.fixture.json = {settings, products:[...], providers:{boxInfo:{...}}, inputs:{labels:[...]}}.
- The '*indent*', '*punct-*' and '*paged*' filename conventions become fixture files with identical values.
- tsrc flags: --profile, --settings, --set path=value, --fixture.

JS side:
- settings.gen.mjs (T4) exports defaults, validate() and the legacy alias map: widthPx→viewport.width, fontFamily→fonts.body, cjkFontFamily→fonts.cjk, lang→doc.lang, punctCompress→cjk.punctCompress, verbatimSnapKerning→code.snapKerning, codeFontFeatures(ByLang)→code.fontFeatures.
- The shell imports the defaults synchronously.
- The worker's semantic message carries the effective {format:'css'} projection, so fonts apply before the first swap.

Effective settings product:
- {format:'json'} for hydration.
- {format:'css'}: T4's CSS-contract projection (indent, base size, line height, font stacks, @font-face for declared fonts) for semantic and static pages.

Differential CI test: for every row that has a test value, and every fixture, patching a converged doc (in place or by fork, per A3) must equal a fresh drive byte for byte. The test runs with a warm Session, which also proves the Session is transparent.
```

**surface**

- createEngine().typeset(src, el, { settings: { viewport: { width: 640 }, doc: { lang: 'en' } }, policy: { fonts: { loadTimeoutMs: 3000 } } }); legacy named options remain as sugar
- renderTsm(src, { settings }) returns css
- export-static --settings site.json
- tsrc --set code.snapKerning=true

**replaces**

- engine/src/api/wasm_api.cc:42-76 tsr_config positional sentinels plus six per-knob setters
- engine/src/api/config.h:89-102 applyLang host switch (becomes doc.lang; locale data is T3/T4)
- engine/src/emit/emit.h:126-128 and engine/src/api/doc.h:362: whole-Config parameters to stages
- runtime/src/worker/worker.mjs:157-193 17-field destructure; worker.mjs:96 round cap literal
- runtime/src/main/shell.mjs:331-337, 358-362, 393-397 option lists; shell.mjs:11-12 and :332 diverging font defaults
- runtime/src/node/render.mjs:24-28 lang-only config; tools/export-static.mjs:36, 49, 56, 64 literal lang and fonts
- engine/src/api/native_cli.cc:38-65 --width/--indent/--punct; engine/test/tests.cc:414-426, 461-465 filename-substring configuration
- engine/src/api/doc.h:392 setWidth

### A5 Session: host-owned content-keyed answer cache, in-engine answerers, generic memo slots

**owner_layer**

Engine core: engine/src/resource/session.{h,cc}. Hosts create it: one per worker, one per Node process, and a fresh one per golden fixture.

**purpose**

Reuse across keystrokes and documents without splitting the per-document arena (architecture §2.3) and without any invalidation protocol. Because keys are complete, an entry can only be unused, never wrong.

**definition**

```
  struct Session {   // refcounted by its Docs; not a pipeline arena
    Arena arena;
    LruMap<Bytes /*canonicalKey*/, Bytes> answers[kResKinds];  // only kinds with cache == Content; byte budget fixed at tsr2_session_new; eviction only costs a re-request
    AnswererSet answerers;   // in-engine answerers, enabled per kind at creation: codeTokens 'tsm' (T1's exported tokenizer on an isolated interner), boxInfo 'svg' (viewBox parse); hyphenPatterns reserved for T5
    MemoSlots memos;         // generic typed slots with LRU and byte budget; break/ (T6) owns BreakMemo, keyed by the serialized complete BreakInput, with stored key bytes compared on hit
  };

Rules:
- need() looks in this order: doc-local, then answerer, then Session, then host.
- A Session hit is copied into the Doc's table arena. No pointer into the Session is ever handed out.
- Answerer results are deterministic and recomputable, so they are not written to the Session.
- provide() writes through only rows with store=1 of Content kinds, under canonicalKey.
- There are no generations and no invalidate call. A dppx change, a face status change, new features or a new lang all produce new keys, and old entries age out.
- tsr2_session_free while any Doc is live returns PRECONDITION. Compaction moves only Session-internal data.
- A host that wants its own tokenizer for a kind an answerer covers disables that answerer at creation, e.g. {answerers:{'codeTokens.tsm':false}}.
```

**surface**

- tsr2_session_new('{"budgetBytes":67108864,"answerers":{"codeTokens.tsm":true,"boxInfo.svg":true}}')
- The shell's dppx observer (T7) sends an env.dppx patch, which forks the doc under new keys.

**replaces**

- engine/src/break/break.cc:147-194: process-global static map, unverified 64-bit hash, XOR packing at :168, wholesale clear at :191
- engine/src/measure/measure.h:19-41 per-Doc MetricStore lost every keystroke; MetricStore::invalidate with no caller
- runtime/src/worker/worker.mjs:18, 24-35 image and token caches as cross-document mechanisms
- runtime/src/worker/canvas_measure.mjs:8-40 font-string-keyed memo cleared on font events

### A6 Code-highlight manifest, engine-side overlays, shared capture resolver

**owner_layer**

Build-time data, one engine transform, host providers:
- runtime/hl/languages.json is the single source
- tools/codehl-assets.mjs generates the worker, CMake and VSCode tables, plus engine/gen/code_overlays.h (overlay specs and tag profiles only)
- engine/src/code/overlay.cc
- runtime/src/shared/hl-core.mjs
- engine/src/code/native_provider.cc

**purpose**

One definition of the languages, aliases, classes and priority contract. Literate-style overlays become one engine transform that every provider gets for free, including user providers.

**definition**

```
languages.json:
  { "version":1, "classes":["keyword","string","number","comment","function","type","constant","variable","operator","punctuation","property","attribute","label","embedded"],
    "alias":{"conditional":"keyword","repeat":"keyword","include":"keyword","boolean":"constant","constructor":"constant","method":"function","field":"property","parameter":"property","tag":"type"},
    "overlays":{"noweb":{"open":"<<","close":">>","forbid":"<>\n","suffix":["=","+="],"class":"label"}},
    "profiles":{"cpp-literate":{"lang":"cpp","set":{"code":{"overlays":["noweb"]}}}},
    "languages":{"cpp":{"aliases":["c++","cc"],"grammar":"tree-sitter-cpp","scm":["c","cpp"],"native":false}, "json":{"grammar":"third_party/grammars/json","native":true}, ...} }

Engine overlay transform, one C++ implementation applied at Emit:
- For a code unit whose effective code.overlays (T4 cascade) is non-empty, find the delimiter spans on the original UTF-8 text.
- Replace each byte of every span with an ASCII space. This preserves the byte length, so every offset outside the spans is unchanged.
- Request codeTokens(lang, maskedText).
- Merge the overlay runs into the answer; overlay runs win any overlap.
Providers only ever tokenize plain text.

Profiles are T4 default settings rules keyed by fence tag ('cpp-literate' means lang cpp plus code.overlays). User rules sit on the same footing. Until T4's rules exist, the generated code_overlays.h table expands them.

hl-core and the native twin do only two things: alias normalisation and the priority contract.
- Priority: a stable sort by (start, pattern), with ties kept in cursor order. Both twins run the same tree-sitter C core, so cursor order is identical.
- The native twin evaluates #eq?, #not-eq?, #any-of? and #not-any-of?.
- There is no shared regex dialect, so the generator refuses native:true for any query that uses #match? or #not-match?.
- A cross-check test runs both twins on every native:true grammar fixture and requires identical runs.

Canonical language: the provider's canonLang in the answer is the only authority. It keys code.fontFeatures and the render lookups. The engine never reads the alias table.
```

**surface**

- Fence tags such as ```cpp-literate
- A fence argument set: {code: {overlays: ['noweb']}}, or a document rule (T4 syntax)
- A new built-in language is one manifest entry
- A runtime tokenizer is a codeTokens provider module (A2)

**replaces**

- engine/src/code/tokens.h:10-22 fixed 14-tag u8 contract; engine/src/code/tokens.cc:5-18 alias if-chain
- runtime/src/worker/tokens.mjs:6-24 TAGS/ALIAS/LANGS; tokens.mjs:78-129 cpp-only, always-on literate branch with a UTF-16-masked offset map
- engine/test/native_tokens.h:20-29 grammar if-chain; :55-80 predicate-less query loop with an unstable std::sort
- engine/CMakeLists.txt:43-58 grammar list and TSR_REPO_ROOT; tools/codehl-assets.mjs:23-41 GRAMMARS; editors/vscode-tsm/src/tokens.js:12-34
- engine/src/render/typeset_html.cc:332-338 font-feature lookup by raw fence tag; worker.mjs:28 cache keyed by raw tag

### A7 Declared inputs, label manifests, project driver, #use

**owner_layer**

Engine: engine/src/api/inputs.def and the 'labels' product row; T3's resolver consumes the inputs.
Host and tooling: runtime/src/node/project.mjs, tools/tsm-project.mjs.

**purpose**

Give documents cross-document references, project-wide numbering and shared libraries. Each document's resolve stays a single pass, and no script can read a resolved value.

**definition**

```
inputs.def: INPUT(labels, 1, schema LabelManifestList, consumer Resolve).
- tsr2_set_input(doc, 'labels', bytes) is called before Ingest.
- Inputs are part of the Doc's identity, and forks copy them.
- The decoder is fuzz target fuzz_inputs.
- Inputs come from the host (a project build) or from a document declaration executed host-side. T3's $.importLabels(src) makes the host call load(src, {requester:'input'}) and then tsr2_set_input. The bytes are never returned to the script.

labels product (Resolve), sorted by label:
  {version:1, docKey, totals:{<counter>: n}, entries:[{label, role, counter, local:[ints], anchorKey, source}]}
- local holds the start-independent counter values.
- The manifest contains no preformatted display and no URLs. T3 owns the role and counter semantics.
- The importing resolver (T3) formats each entry with its own locale and supplements, applying the producer's start taken from project.starts.
- URLs are composed at Paint by T7's AnchorNamer plus the host's map project.urls (docKey → URL).

renderProject({ files /*book order*/, settings, outDir }):
- Pass A (labels): each file is driven to Resolve with no labels input and no starts, and 'input' loads answer empty. It writes manifests. The result is deterministic and independent of any previous build.
- starts = prefix sums of totals in book order (a T3 counter-start setting).
- Pass B (render): each file gets settings {project:{starts, urls}} and input labels = every other manifest, then is driven to its target.
- Consistency: Pass A's local values depend on neither starts nor imports, so the producer and every consumer apply the same start.

#use(spec):
- T1/T2 lower it to `await __use(spec)`.
- The host calls load(spec, {as:'module'}) and gives each module its own SourceId; the module's relative loads resolve against its own URL.
- Exports {fences, regions, providers} register in document order (architecture §4.1 __reg). Providers are restricted as in A2.
- The module is recorded in the manifest.
- This amends architecture §4.1 (static import → dynamic __use with an explicit base).
```

**surface**

- tsm-project build book.json
- @sec-intro resolves across chapter files (T3's reference syntax)
- #use("./lib.mjs")
- $.importLabels("../vol1/book.labels.json") (T3)

**replaces**

- engine/src/codegen/codegen.cc:209-212: no `use` binding, so #use compiles to val((use(...))) and throws a ReferenceError
- engine/src/resolve/resolve.cc:271-283 unresolved cross-file refs become '??' (57 in the HoTT example)
- tools/convert/pbr2tsm.mjs:496 drops non-http links because no target exists

## Subsumption (finding → mechanism)

- **subsumed** by *A1 ResourceTable + resources.def*: `api-measure-code/per-resource-pull-plumbing`, `api-measure-code/image-dims-in-author-args`, `api-measure-code/math-text-measure-side-channel`, `api-measure-code/missed:1`, `api-measure-code/missed:3`
  - Kind scans, pending guards, the four provide exports, the JSON sections and the math side channel all become RES rows plus rc.need at each consumer.
  - Answers never touch authored data.
    - Image dims become a boxInfo answer referenced by ResId. A lone author w stays authored, and h follows the aspect ratio. Probe: {w:100} renders at 100px, not 600px.
    - Token runs are folded at Emit, so the tree is never mutated.
  - A pending need defers only its pid, so there is no whole-document re-emit.
  - missed:3: MetricKey is the complete measurement tuple. The plain, red and linked 'hello world' collapse into one request.
  - missed:1: the generated decoder validates class indexes, ordering, UTF-8 boundaries, finiteness, batch membership and missing rows.
- **subsumed** by *A2 ResourceHost/Locator + A3 'references' product + A4 (settings json/css products)*: `api-measure-code/resource-io-paths`, `render-runtime/static-export-template`, `real-world-evidence/resource-and-config-plumbing`
  - One locator, with per-SourceId bases, root confinement and the generated url_policy, serves images, fonts, grammars, data, the bibliography and #use. One LruCache with validators and a failure TTL sits behind it.
  - The static-export manifest is computed from the tree (the references product), the execution load log and fonts.declared, not from the need log. It therefore includes images with authored w and h (doc.h:189), assets of the Node semantic-only path (render.mjs:4-5) and assets that were Session hits.
  - renderTsm returns {html, diagnostics, ok, manifest, settings, css}.
  - The exporter copies or seeds resources, emits <html lang> and T4's CSS projection, and leaves the template to T7. The blog no longer re-implements indent, size and @font-face in page CSS (zball-io base.njk, eleventy.config.js:64-73).
  - The config half of resource-and-config-plumbing is A4.
- **subsumed** by *A3 pipeline + products.def + tsr2_ ABI, with A4 fixture profiles*: `api-measure-code/per-feature-api-entry-points`, `api-measure-code/native-driver-config-divergence`
  - The 24 exports become 16 generic tsr2_ exports, and the old symbols remain as shims for one release.
  - tsrc, the tests and the fuzzers share driveToCompletion and products.def, and 'typeset' is an alias.
  - --profile=golden reproduces tests.cc:414-426, so code/wrap gives 8909su.
  - The '*paged*' filename rule becomes a fixture setting, page.height=240, plus the 'paged' product.
- **subsumed** by *A4 host settings channel + host-policy schema (over T4's registry)*: `api-measure-code/config-plumbing-per-knob`, `render-runtime/config-plumbing`, `api-measure-code/magic-policy-constants`
  - tsr2_set_config(json) implements document-model §11, including the unknown-key diagnostic. Defaults exist once.
  - Constants are split three ways:
    - Runtime policy goes to the host-policy schema: round cap 64, 4000/15000 ms, cache caps 400/200000/16384. The image stub 512x384 becomes fixture data.
    - Safety rails become named constexprs: kMinLineSu, the hl span limit, MetricStore packing.
    - Grid policy becomes T4/T6 setting rows.
- **subsumed** by *A3 stages.def rerun classes + fork-on-rebuild, A4 generated per-stage views + differential test*: `api-measure-code/adhoc-invalidation-flags`
  The root cause is that dependencies were hand-placed and stages mutate their inputs. Three mechanisms remove it:
  - Rerun classes record which stages can be re-entered: Resolve and Measure mutate, Break onward is reentrant.
  - Any change that reaches a non-reentrant stage forks from the retained ops, so stale or doubly-resolved products cannot exist.
  - Per-stage settings views make an undeclared read a compile error, and the differential test checks patch-then-drive against a fresh drive for every row.
  The emitted/laidOut flags disappear. A dependency-recording product graph is still rejected (v2 §9 line 210; architecture §2.3).
- **subsumed** by *A5 Session + A1 complete MetricKey/canonical keys + A2 LruCache*: `api-measure-code/adhoc-caches`
  - Answers are content-keyed by complete keys: dppx, face status and features are part of MetricKey, so there are no generations.
  - A hit is copied into the Doc, never pointed to, and the Session is refcounted.
  - Document-provider rows carry store=0.
  - URL-keyed answers live only in the host LruCache, with validators and a failure TTL.
  - The KP memo moves to a generic memo slot whose key completeness T6 owns.
  - A warm keystroke crosses the boundary only for strings the Session has never seen.
- **subsumed** by *A6 manifest + engine-side overlay transform + hl-core*: `api-measure-code/token-tag-table-copies`, `api-measure-code/language-registry-scattered`, `api-measure-code/literate-cpp-special-case`, `real-world-evidence/literate-cpp-hack`, `api-measure-code/missed:0`
  - One manifest generates every copy.
  - Overlays are one C++ transform around the codeTokens need. They are opt-in (profile, fence set or rule), and every provider, user providers included, gets them. This ends the false positives on `(1 << n) >> 2`.
  - missed:0: masking is byte-preserving in UTF-8 and the provider never sees fragment names, so offsets cannot shift. M0 also fixes today's JS offset map as interim hygiene.
  - Runtime registerLanguage stays out (code-design §1); runtime extension is a provider module.
- **subsumed** by *A7 declared inputs + start-independent manifests + two-pass project driver (resolution semantics with T3)*: `real-world-evidence/no-cross-document-labels`
  - Manifests are declared inputs, not engine pulls, so Resolve never waits and the semantic first paint is unaffected.
  - They carry local counter values and totals, so cross-chapter numbers agree with each target's own rendering.
  - Pass A reads no manifests, so builds are idempotent.
  - Scripts cannot read manifests.
  - T3 provides external entries, counter starts and import syntax.
- **owned-by-other-theme** by *T4-style-settings*: `api-measure-code/global-feature-knobs-no-cascade`, `api-measure-code/font-role-split`, `api-measure-code/token-class-as-color-string`
  T4 owns the registry schema, the generator, the cascade, the precedence lattice, font roles, metricKeyOf and dynClasses.

  T9 supplies:
  - stages.def as the Affects domain;
  - the host layer and its transport;
  - view generation requirements;
  - the differential test;
  - token answers that carry class names, from which T4 attaches tok-* classes;
  - the settings json/css products.
- **owned-by-other-theme** by *T3-semantics*: `api-measure-code/supplements-and-lang`
  T3/T4 own the locale term tables keyed by doc.lang.

  T9 supplies:
  - doc.lang as a HostDefault row, which the document's own layer can override (the blog's front-matter lang);
  - forwarding through renderTsm and export-static;
  - the effective value in the settings product.
- **owned-by-other-theme** by *T6-layout-pagination*: `api-measure-code/grid-is-codeblock-only`, `api-measure-code/doc-typeset-hosts-layout-logic`, `emitter/measure-dependent-geometry-in-emit`, `api-measure-code/missed:4`
  Assumed from T6:
  - the BlockLayouter registry, including the verbatim grid (missed:4);
  - ParShape and ExclusionMap in layout/;
  - WidthSpec;
  - IntrinsicSize consumption of boxInfo;
  - the Paginate stage;
  - BreakMemo key completeness.

  T9's part:
  - api/ becomes a sequencer, and a lint test forbids Kind and FlowUnit::K dispatch there;
  - viewport.width's Affects drops from EMIT to BREAK once emit no longer reads widthPx (emit.cc:591, 721), and M12 records that;
  - fixture settings give T6 golden coverage.
- **owned-by-other-theme** by *T2-constructor-ir*: `api-measure-code/sidecar-api-layer-rewrite`
  T2 lowers sidecars with spans and moves the pass to model/; the three-box layout is T6's.

  T9 places that pass in the Ingest stage, and provides tsr2_parse_fragment if T2 keeps m.parse in WASM.
- **owned-by-other-theme** by *T7-render-runtime*: `render-runtime/shell-feature-inventory`, `render-runtime/shell-chunk-byte-coupling`, `api-measure-code/missed:2`
  T7 owns:
  - the shell's capability and behavior registry API;
  - structured per-paragraph results;
  - the export template;
  - AnchorNamer;
  - the Paint stage contents.

  T9 supplies:
  - the cap?/cap transport, replacing 'image-dims?';
  - fontFace status and font diagnostics;
  - the {docId, gen} mailbox protocol;
  - the semantic message carrying the effective CSS projection.
- **owned-by-other-theme** by *T1-surface-frontend*: `real-world-evidence/lexical-syntax-copies`
  T1 provides the single grammar source and an exported tokenizer.

  T9 runs that tokenizer as the in-engine codeTokens answerer for 'tsm', on an isolated interner and never on the outer document's tables. No host round trip is needed.
- **owned-by-other-theme** by *T2-constructor-ir + T3-semantics*: `codegen-ops-model/bibliography-placeholder-and-end-emission`
  The core of the item belongs to other themes:
  - the in-place async constructor value is T2's;
  - collector expansion after every ref, schema-typed entries and removing the empty-paragraph hack (resolve.cc:476-486) are T3's.

  T9 contributes:
  - ctx.load through the locator;
  - the LruCache with a failure TTL, which ends the refetch on every keystroke;
  - seeding the hydration payload, so hydration keeps the good bibliography;
  - the recorder update (M7).
- **bug-fix-only** by *migration steps M0-M9*: `api-measure-code/relayout-stale-emit`, `emitter/duplicate-diagnostics-on-reemit`, `api-measure-code/diag-format-and-duplication`, `api-measure-code/kp-cache-unverified-hash`, `break-layout-pages/break-cache-robustness`, `api-measure-code/worker-no-per-doc-serialization`, `api-measure-code/main-dims-rpc-race`, `api-measure-code/nul-byte-in-worker-source`, `api-measure-code/image-fetch-serial-and-decode`, `api-measure-code/late-font-stale-measure-cache`, `api-measure-code/image-w-only-overwritten`, `api-measure-code/unchecked-boundary-invariants`, `api-measure-code/per-word-boundary-marshalling`, `emitter/full-reemit-for-math-text`, `api-measure-code/doc-drift-api-subsystem`
  Each bug is fixed in the step that introduces the general mechanism it depends on; see migration[].fixes.
  - relayout-stale-emit is fixed in M0 by forking from the retained ops in the worker, and structurally in M3.
  - late-font-stale-measure-cache has a JS fix in M0 and becomes impossible by construction in M4/M6, because face status is part of MetricKey.
- **bug-fix-only** by *M3 fixture channel; code fixes by T7 (single style builder) and T6 (lineWidthCode at layout.cc:162; tracker out of api/)*: `api-measure-code/snap-kerning-duplicate-style-attr`, `api-measure-code/snap-ignores-sidecar-partition`, `break-layout-pages/api-hosts-layout-policy`
  These bugs shipped because no fixture could turn snap-kerning on. T9 provides the coverage channel, and the fixes land with new goldens only.

## User extension examples

### A '#!plot' fence that reads data.csv next to the post and renders SVG, in the browser, in Node static export and in hydration.

**Today**

- The handler must fetch() by itself. Only the bibliography gets a baseUrl (executor.mjs:52).
- A raw unit is one leading tall unless the handler hard-codes h. A probe SVG with viewBox 400x240 lays out at 27px (emit.cc:695-697).
- export-static copies only engine assets (export-static.mjs:81-93).

**After**

$.fence('plot', async (body, ctx) => ctx.raw(svg(await ctx.load(body.trim(), {as:'text'})), {measure:'auto'})).
- The load goes through the locator, using the base of the fence's source, plus LruCache and the failure policy. A failure becomes a fence-error diagnostic with the fence's span.
- The in-engine boxInfo 'svg' answerer sizes the box from the viewBox at the current measure, natively as well, so a golden can cover it.
- data.csv appears in the manifest and is copied or seeded.

### Highlight a DSL with no tree-sitter grammar (e.g. TLA+), keeping comment-aware hanging and literate fragments.

**Today**

- Only a $.fence handler returning a structured body works, and it skips token pulls (doc.h:152-155).
- Hanging needs the forged colour string 'var(--tsr-tok-comment)' (emit.cc:618-624).
- Literate masking exists only inside the cpp provider.

**After**

createEngine({ providers: [{ kind: 'codeTokens', module: '/hl/tla.mjs' }] }), where tla.mjs exports match(k) => k.lang === 'tla' and resolve(items) => items.map(({text}) => lexTla(text)), returning {canonLang, classes, runs}. Alternatively, a #use module exports the provider, scoped to the document with store=0.
- Answers are validated and, for host-registered providers, cached in the Session.
- ```tla set:{code:{overlays:['noweb']}} gets noweb fragments from the engine transform with no provider code.
- A native test registers the same lexer in ProviderSet::codeTokens.

### Add a built-in language, e.g. Go.

**Today**

Five hand edits: codehl-assets.mjs:23-41, tokens.mjs:21-24, native_tokens.h:20-24, CMakeLists.txt:43-46 and editors/vscode-tsm/src/tokens.js.

**After**

One entry in languages.json: {"go":{"grammar":"tree-sitter-go","aliases":["golang"]}}.
- The worker, CMake and editor tables are generated.
- native:true is refused if its query uses #match?.

### Literate noweb fragments in Rust or Python code.

**Today**

Impossible. tokens.mjs:102 applies the overlay only when name === 'cpp', and there it is always on, so ordinary C++ such as `(1 << n) >> 2` is mis-tokenized.

**After**

Any of these works:
- a 'rust-literate' profile in languages.json, which becomes a T4 default rule;
- a fence set: {code:{overlays:['noweb']}};
- a document rule.
The engine applies the same byte-preserving overlay whatever the provider, and an overlay fixture over json covers it natively.

### Expose a layout knob (e.g. the sidecar fraction) to hosts, CLI and goldens.

**Today**

Five files per knob: config.h, a tsr_set_* export, the worker destructure, and the shell's typeset and update (commits 864cb14, 264368b, b5696fc, 383d91d, c916342). sidebarFrac has no setter at all.

**After**

One settings.def row with Affects {BREAK}.
- It is reachable through tsr2_set_config, tsrc --set code.sidecar.fraction=0.35 and X.fixture.json.
- It appears only in BreakSettings, so if emit started reading it, the build would fail.
- The differential test proves an in-place re-break gives the same result as a fresh run.

### A blog post declares its own webfont and language instead of the site template.

**Today**

- The host regexes front-matter lang (zball-io eleventy.config.js:29-32) and derives paraIndent per language by hand.
- @font-face, base size and indent are copied into page CSS and again into the hydration options (eleventy.config.js:64-73).

**After**

- The document layer sets doc.lang and adds fonts.declared entries; both rows are HostDefault, overridable by the document.
- The engine requests fontFace for the new faces before the first Measure, so W holds and there is no late re-typeset.
- renderTsm returns T4's CSS projection, including @font-face, and the effective settings for hydration. The template copies nothing.

### A new kind of datum only the engine can discover (e.g. per-language hyphenation patterns, or video intrinsic size).

**Today**

About eight touch points (git 7a3c058, 217b3b7).

**After**

Four pieces of new code:
- one RES row (key, answer, cache class, docProviders, consumer stages);
- rc.need<K> at the consumer;
- one provider module, with an optional in-engine answerer;
- one ProviderSet slot.
The envelope, deduplication, validation, failure degradation, deferral, Session caching and stall diagnostics are all generated.

### A book split into chapter files with cross-chapter @refs and chapter-relative numbering.

**Today**

57 cross-chapter references render as '??' in the HoTT example, and #use throws a ReferenceError.

**After**

tsm-project build book.json.
- Pass A writes chN.labels.json with local counter values and totals.
- The driver computes the starts.
- In Pass B, each chapter receives the other manifests as inputs and renders '§3.2' in its own locale, linking through AnchorNamer and project.urls.
- #use("./macros.mjs") loads through host.load.

### Inspect a new stage product (e.g. T7's display list) in tsrc, the goldens and the playground.

**Today**

A branch in native_cli.cc:69-104, a line in the golden runner, a new C export and new worker code.

**After**

One row, PRODUCT(displaylist, Paint, {}, dumpDisplayList), is picked up by tsrc --stage=displaylist, the golden runner and tsr2_get(doc,'displaylist').

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  - Native providers and answerers are pure: the testing.md §2 mock, fixture-declared boxes, statically linked grammars and embedded queries.
  - Answers are applied by ResId, and needs are listed in document order.
  - Deferral re-runs the same pure emitTop.
  - Forks are asserted equal to fresh drives.
  - Each golden fixture gets a fresh Session; the differential test runs warm and proves Session transparency.
  - Settings use from_chars, labels are sorted, and both token twins use stable sort.
  - Quantisation moves from provideWord to Measure with the same arithmetic, ceil(px)+ε (measure.h:24-26, emit.cc:973), so it is byte-neutral.
  - MetricKey dedup is neutral because the mock depends only on sizePx (mock.h:16-31).
  - Golden churn:
    - M0-M10 are byte-neutral on the existing goldens.
    - M8 may change native json-hl/tsm-hl only where std::sort ordered ties arbitrarily; any churn is announced.
    - .ops re-recording and cite churn are announced at the T2/T3 bibliography-placement step.
    - New goldens come only from new fixtures.
    - Class and WidthSpec churn are T4's and T6's to announce.
- **I2 measurement–render robustness (v2 §7)**
  - Nowrap lines, explicit spacing and the ε over-estimate are unchanged.
  - MetricKey merges only identical measurement tuples, and providers measure under the key rather than ambient state, so a width is never applied to a different font, face status or dppx.
  - On failure:
    - Derived widths drop the derivation instead of clamping a bogus gap.
    - Base widths use a generated per-codepoint bound × 1.2, which is declared best effort.
    - fontVmet uses conservative ascent and descent.
    - Residual error stays inside the line (rule 5), and the browser never re-breaks.
  - pages-design W is kept: fontFace settles before the first Measure, and nothing is re-typeset when a font lands late.
- **I3 ops contract and OPS_VERSION discipline**
  - No T9 step bumps OPS_VERSION, except M11's raw 'measure' attribute if T2's open attribute keys have not landed by then.
  - tsr2_abi_version packs OPS_VERSION, RES_VERSION, the settings schema major and the inputs major. drive.mjs checks it before anything else and fails hard.
  - Adding a settings row is not a break.
  - No fuzz harness exists today: there is no LLVMFuzzerTestOneInput in the repo, and ci.yml has only the native, web and dist jobs. Step M1F creates fuzz_opreader and the CI fuzz job (testing.md §6).
  - Each new untrusted decoder adds its harness in its own step: fuzz_settings (M2), fuzz_resanswer (M4) and fuzz_inputs (M10).
  - Document providers (M7) and inputs (M10) are gated on those harnesses.
- **I4 execution declares, resolver decides; resolver is a pure single pass**
  - Resolve declares no needs (it is a Once stage), so it is a single pass over (tree, settings, declared inputs). This explicitly widens '(tree, config)', and v2 §11.1 is amended in M9.
  - Scripts never see resource answers, inputs or resolved values:
    - $.importLabels hands the bytes to the engine, not to the script;
    - the locator denies project outputs to 'exec';
    - document providers serve only codeTokens and boxInfo, whose keys are authored content, never resolved strings such as ref displays, and with store=0.
  - Pass A reads no manifests, so cross-document numbering has no fixpoint.
  - ctx.settings exposes only the layers that exist before execution.
- **I5 dual-target rule**
  - ResourceTable, the codecs, MetricKey, Session, the answerers, stages.def, products.def and driver.h are browser-free engine core.
  - All I/O lives in providers. Native providers are optional targets, and tsrc no longer includes test/native_tokens.h.
  - api/ shrinks to a sequencer, enforced by a lint test (no Kind or FlowUnit::K dispatch).
- **I6 emission-time style binding (v2 §12)**
  - Untouched. MetricKey is derived from the interned Styling after instantiation through T4's metricKeyOf.
  - Session keys are content bytes, never StyleIds.
  - T4's cascade binds on the schedule stack; T9 supplies only the host layer.
- **I7 block-granular containment**
  - A provider exception or a missing row becomes Failed. That degrades only the quantity it fed, in its own unit, and raises one Resource-origin diagnostic.
  - A deferral discards one pid pass.
  - A stall returns STALLED and names the pending kinds.
  - The mailbox stops an older edit from freeing a newer doc.
  - Containing execution errors is T2's job; their diagnostics flow through the same JSON.
- **I8 resumable pull loop; progressive atomic per-paragraph swaps**
  - There is exactly one wait state, resumable at stage granularity through tsr2_typeset(doc, target).
  - The semantic first paint, tsr2_get('semantic',{partial:true}), follows Resolve directly, because Resolve never waits. It is posted with the CSS projection, so fonts apply before the swap.
  - Generations and the mailbox keep commits ordered.
  - The pending(estimate) states of architecture §2.4, document-model §6.4 and v2 §9 are superseded by pages-design §1 W (implemented: 'no estimate states'). M9 amends those docs. The per-pid waiter mechanism could host an Estimated state later with a RES_VERSION bump.
- **I9 performance; editor fast path**
  - compile, execute and ingest are unchanged (under 6 ms combined, editor-design §1).
  - Within a doc, dedup uses integers, so there is no per-occurrence byte encoding.
  - A warm keystroke requests only the strings the Session lacks, and width answers are one Float64Array.
  - Code and math pids defer instead of blocking the whole document, which saves a round.
  - The mailbox drops superseded updates and aborts superseded work.
  - A relayout fork costs ingest plus resolve on top of the full re-typeset that v2 §9 already prescribes for a resize. After T6's WidthSpec, relayout re-enters Break in place.
  - bench-edit at 87K must not regress (30 ms median, editor-design §4).

## Interfaces

- **T4-style-settings** (consumes)
  - The settings.def schema with Precedence {HostOnly, HostDefault, HostForce, DocOnly}, Scope, and Affects ⊆ stages.def.
  - Generated per-stage settings views.
  - The host-policy schema, produced by the same generator.
  - settings.gen.mjs: defaults, validate and the legacy alias map.
  - metricKeyOf(StyleId, Settings, faceAnswers) returning the complete MetricKey.
  - dynClasses, for tok-* classes.
  - code.overlays and default rules for fence-tag profiles.
  - The CSS-contract projection.
  Until T4 lands, a phase-1 hand registry and hand views mirror config.h:22-73 with identical defaults.
- **T4-style-settings** (provides)
  - stages.def as the single Affects vocabulary, shared with the metric-affecting and paint-only classification of style properties.
  - The rows viewport.*, env.dppx, page.*, fonts.declared and ext.*.
  - Transport: tsr2_set_config with REBUILD/REEXECUTE results, fixture profiles and tsrc --set.
  - The differential patch-versus-fresh CI test.
  - The settings json and css products.
  - A rule: any setting that feeds a resource key (code.overlays, fonts, dppx) carries the consuming stage, EMIT.
- **T3-semantics** (consumes)
  - A resolver API for external label entries taken from the 'labels' input.
  - The manifest schema semantics: role, counter path, start-independent local values and totals.
  - Formatting by the importer, with its own locale and supplements.
  - A counter-start setting (project.starts).
  - The optional $.importLabels declaration; any ops bump it needs is T3's.
  - Locale tables keyed by doc.lang.
- **T3-semantics** (provides)
  - inputs.def 'labels', delivered before Ingest, so Resolve never waits and declares no needs.
  - Deterministic, sorted serialisation of the labels product.
  - The two-pass renderProject driver.
  - Fixture-declared inputs for goldens.
  - No requirement that the resolver be non-mutating: forks cover any re-resolution.
- **T6-layout-pagination** (consumes)
  - WidthSpec. When it lands, viewport.width's Affects becomes BREAK and relayout re-enters Break in place.
  - BreakMemo, keyed by the serialized complete BreakInput and stored in a Session memo slot.
  - The Paginate stage contents.
  - IntrinsicSize consumption of boxInfo at Layout.
  - The float tracker, tables and sidecar breaking moved out of api/doc.h:263-361.
  - At each migration step, the list of settings each stage reads, so the views and Affects rows change in lockstep.
- **T6-layout-pagination** (provides)
  - boxInfo answers (image, svg, html) by ResId; author args stay immutable.
  - Generic memo slots with LRU and byte budget.
  - Per-pid deferral with waiters.
  - Resumable or Reentrant need support at Layout.
  - A fixture channel for snap, nowrap, sidecar and paged goldens.
- **T7-render-runtime** (consumes)
  - The html product with {format:'paras'}.
  - The shell's capability and behavior registry, which registers measureHtml and the <img> fallback as capabilities.
  - AnchorNamer plus the project.urls map for cross-document URLs.
  - A dppx observer that sends an env.dppx patch.
  - The export template.
  - The Paint stage.
- **T7-render-runtime** (provides)
  - The worker protocol (open, update, relayout, paginate, close, all carrying {docId, gen}) with a coalescing mailbox.
  - cap?/cap transport with per-request ids.
  - fontFace status and font diagnostics.
  - The manifest view and seed() for static export.
  - JSON diagnostics with origin and source.
  - The effective CSS projection in the semantic message, so fonts apply before the first swap.
- **T2-constructor-ir** (provides)
  - $.load and ctx.load, __use(spec), ctx.settings (pre-execution layers only) and a SourceId per module or fragment.
  - An Execute stage and origin in stages.def and the diagnostics JSON.
  - tsr2_parse_fragment, if T2 keeps m.parse in WASM.
  - A ResourceHost for tools/record-fixtures.mjs.
- **T2-constructor-ir** (consumes)
  - Async constructor values, so #bibliography awaits ctx.load in place.
  - Per-block execution containment.
  - Sidecar lowering with spans in model/.
  - Open attribute keys, for ctx.raw {measure:'auto'}.
  - A joint amendment of architecture §4.1 (#use becomes a dynamic __use with an explicit base).
- **T1-surface-frontend** (consumes)
  - The lowering of #use(spec) to `await __use(spec)`.
  - An exported tokenizer that is pure over an isolated interner and serves as the in-engine 'tsm' codeTokens answerer.
  - A decision, taken jointly with T4, on whether front matter or #set is the document settings layer.
- **T8-math** (provides)
  - In the interim, math text runs call rc.need(fontFace, fontVmet, textWidth) at Emit, and a pending need defers only that pid. Font rows therefore carry EMIT.
  - T8's lazy text-leaf measurement can move these needs to Measure without any protocol change. T9 then keeps deferral only for codeTokens and boxInfo.
- **T5-text-shaping** (provides)
  - MetricKey already carries lang and features as real fields, not reserved ones.
  - A reserved hyphenPatterns RES row and an in-engine answerer tier for compiled-in dictionaries; gen/hyphen_en_us.h already exists as compiled-in data.
  - T5 decides which dictionaries stay resident and which are pulled.
  - TextRules remain build-time data.

## Migration

### M0 Runtime hygiene (no engine change)  → plan P0-11

Worker scheduling:
- per-docId mailbox with coalescing and a generation check at every round and await;
- capability RPC with per-request ids;
- relayout and paginate create a fresh doc from the retained ops instead of calling tsr_set_width on the live doc.

Other fixes:
- A '\0' escape replaces the raw NUL bytes.
- imageSize: Promise.all, new URL(src, baseUrl), header sniffing with a decode fallback.
- loadFonts: a font is marked loaded only on success, and the measurer clears when a late face lands; failures are retried after a TTL.
- tokens.mjs: grammar failures get a TTL, and the literate offset map is built on the original text.
- executor loadResource is confined to rootDir.

**Golden impact:** None; native goldens do not run the worker. e2e is unchanged except relayout correctness.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/worker-no-per-doc-serialization`, `api-measure-code/main-dims-rpc-race`, `api-measure-code/nul-byte-in-worker-source`, `api-measure-code/image-fetch-serial-and-decode`, `api-measure-code/late-font-stale-measure-cache`, `api-measure-code/missed:0`, `api-measure-code/relayout-stale-emit`

### M1 Engine hygiene  → plan P0-11

- DiagSink gains origins: Stage(s,pid), Settings, Input, Resource. A pass truncates its own slice when it begins.
- tsr_set_width on an emitted doc clears emitted, for ABI callers other than the worker.
- KP memo: per-field mixing, stored key bytes compared on hit, LRU.
- Bounds checks on the tag in tsr_provide_tokens, and finite checks in provideImage.

**Golden impact:** None. No diagnostics golden exists, and break results are identical in the absence of collisions.

**OPS bump (as designed):** False

**Fixes:** `emitter/duplicate-diagnostics-on-reemit`, `api-measure-code/diag-format-and-duplication`, `api-measure-code/kp-cache-unverified-hash`, `break-layout-pages/break-cache-robustness`

### M1F Fuzz infrastructure (prerequisite for every new decoder)  → plan P0-03

- Add fuzz_opreader, fuzz_linepass and fuzz_inline, seeded from the fixtures (testing.md §6).
- Add a CI fuzz job: a short per-PR run plus the 30-minute nightly from testing.md:122.
- Later steps each add their decoder's harness.
- M7's document providers and M10's inputs do not ship without them.

**Golden impact:** None.

**OPS bump (as designed):** False

### M2 Host settings channel, phase 1  → plan P1-03

- tsr2_set_config over a phase-1 hand registry mirroring config.h:22-73, with Precedence and Affects; fuzz_settings.
- Resolve still lives inside ingest and there is no fork primitive yet, so a patch on an ingested doc applies in place only when its first stage is Break or later. Otherwise it returns REBUILD, and the worker re-ingests the retained ops as in M0.
- The old setters become wrappers.
- The worker, shell, render.mjs and export-static forward one settings object.
- A host-policy schema replaces the runtime literals.
- The shell imports the defaults synchronously.

**Golden impact:** None native. In the browser the default body font becomes the registry default, "Crimson Text", Georgia, serif, instead of the shell's Georgia, serif; release note.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/config-plumbing-per-knob`, `render-runtime/config-plumbing`, `api-measure-code/magic-policy-constants`

### M3 Stage model, shared driver, fixture profiles  → plan P1-03

- stages.def (with rerun classes) and products.def drive tsrc and tsr_tests. Resolve moves out of ingest.
- tsr2_doc_fork from the retained ops, and the REBUILD/REEXECUTE rule.
- Per-stage settings views, hand-written until T4's generator.
- driver.h driveToCompletion and ProviderSet; the native token provider moves to engine/src/code.
- Fixture profiles and X.fixture.json replace the filename conventions with identical values.
- The Paginate stage; page.height becomes a setting.
- The golden runner requests semantic{partial:true}, matching today's pre-token dump at tests.cc:445-446.
- The differential test, plus a 'fork == fresh' assertion for every fixture.
- New fixtures: code/snap, code/snap-sidecar, code/nowrap-snap and ref/supplements-en. The T7 single-style-builder fix and the T6 lineWidthCode fix land with them.

**Golden impact:** Existing goldens stay byte-identical. A CI check asserts that tsrc --profile=golden reproduces every golden. New goldens come only from the new fixtures.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/native-driver-config-divergence`, `api-measure-code/adhoc-invalidation-flags`, `api-measure-code/snap-kerning-duplicate-style-attr`, `api-measure-code/snap-ignores-sidecar-partition`, `break-layout-pages/api-hosts-layout-policy`

### M4 ResourceTable (whole-document barrier unchanged)  → plan P1-19

- resources.def and the gen-res.mjs codecs; batch-numbered tsr2_requests/tsr2_provide; fuzz_resanswer.
- The complete MetricKey, through a phase-1 projection of describeStyle plus dppx and faces. fontFace is answered 'loaded' by the native mock.
- The table stores raw px, and Measure quantises.
- Canonical keys plus the per-doc integer index.
- boxInfo for images keeps a lone author w.
- Emit folds token answers; the semantic product reads the table.
- Failure degradation at the level of the consumed quantity; 'provider-missing'; STALLED.
- The old JSON and provide exports remain as shims. worker.mjs and render.mjs switch over.

**Golden impact:** Neutral expected: the mock depends only on sizePx, and quantisation arithmetic is unchanged. Verified on json-hl, tsm-hl, the figure fixtures and the math fixtures. A new fixture covers the w-only image.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/per-resource-pull-plumbing`, `api-measure-code/image-dims-in-author-args`, `api-measure-code/image-w-only-overwritten`, `api-measure-code/unchecked-boundary-invariants`, `api-measure-code/missed:1`, `api-measure-code/missed:3`, `api-measure-code/per-word-boundary-marshalling`

### M5 Per-pid deferral with waiters  → plan P1-20

- emitTop(child, pid) runs per pid.
- Code, image and math pids defer instead of hitting the whole-document barrier.
- MathTextCtx is deleted.
- Width needs from completed pids join the same round.

**Golden impact:** Neutral expected: blocks and mathbox must be byte-identical. Round counts show up in timings only.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/math-text-measure-side-channel`, `emitter/full-reemit-for-math-text`

### M6 Session  → plan P1-21

- tsr2_session_new and tsr2_session_free; refcounting; copy-in on hit.
- Content-keyed write-through that honours store bits.
- Memo slots; the KP memo moves into a slot keyed by T6's BreakInput serialisation, and the static cache goes away.
- An answerer registry, with codeTokens 'tsm' enabled once T1 exports its tokenizer.
- The JS canvas memo shrinks to per-round dedup.

**Golden impact:** Neutral, since each fixture gets a fresh Session. The differential test runs warm. Tracked with bench-edit at 87K.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/adhoc-caches`

### M7 ResourceHost, locator, references, static export  → plan P3-21

- A provider registry of module and capability providers, with document providers restricted to codeTokens and boxInfo.
- The locator with per-source bases and requester classes.
- url_policy.def generates both the engine's safeImageSrc and the JS allowlist.
- LruCache with validators; timeouts from the host-policy schema.
- $.load and ctx.load; bibliography loading through ctx.load (placement per T2/T3).
- The references product and the manifest view.
- renderTsm returns {html, diagnostics, ok, manifest, settings, css}.
- export-static copies or seeds resources, scans module imports, takes T7's template and emits <html lang> plus the CSS projection.
- tools/record-fixtures.mjs constructs a ResourceHost with base and root.

**Golden impact:** None native, provided the recorder reproduces cite/*.ops byte for byte; the CI 'recordings are current' check verifies this. Static export output changes: <html lang>, the CSS projection, copied assets. An export test is added.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/resource-io-paths`, `render-runtime/static-export-template`, `real-world-evidence/resource-and-config-plumbing`

### M8 Code-highlight manifest and engine-side overlays  → plan P3-22

- languages.json generates the worker, CMake, editor and engine overlay/profile tables.
- hl-core reduces to aliases and priority, with stable sort in both twins.
- The native twin evaluates the string predicates, and the generator refuses native:true where #match? is used.
- The engine overlay transform is opt-in through profiles, fence set: or rules.
- canonLang comes from the answer.
- pbr2tsm emits cpp-literate.

**Golden impact:** JS token output is unchanged, since it already sorts stably. Native json-hl/tsm-hl are expected to be byte-identical; any tie-order churn is announced. A new overlay fixture covers json natively. The real-world pbr corpus output changes until it is re-converted.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/token-tag-table-copies`, `api-measure-code/language-registry-scattered`, `api-measure-code/literate-cpp-special-case`, `real-world-evidence/literate-cpp-hack`

### M9 ABI completion and doc amendments  → plan P3-37

- tsr2_get serves every product, including the diagnostics JSON; VSCode drops the regex at preview.js:128.
- The tsr_* shims are removed after one engine-dist release.
- Doc amendments:
  - architecture §2.1, §2.4 (estimate states superseded by pages-design W), §2.5, §4.1 (#use, cache key);
  - document-model §6.4, §7, §10, §11;
  - v2 §6 (Session cache ownership), §9, §11.1 (declared inputs);
  - code-design §3/§5;
  - figure-design §3 versus §8;
  - the JS-twin claim at mock.h:1-2.

**Golden impact:** None; the diags text product is kept for humans.

**OPS bump (as designed):** False

**Fixes:** `api-measure-code/per-feature-api-entry-points`, `api-measure-code/doc-drift-api-subsystem`

### M10 Declared inputs, project driver, #use (with T3, T1, T2)  → plan P3-31

- inputs.def and tsr2_set_input; fuzz_inputs.
- The labels product.
- runtime/src/node/project.mjs and tools/tsm-project.mjs (Pass A, starts, Pass B).
- __use(spec) through host.load, with module SourceIds.
- T3's external entries and importLabels.

**Golden impact:** New project fixtures only, with inputs declared in X.fixture.json.

**OPS bump (as designed):** False

**Fixes:** `real-world-evidence/no-cross-document-labels`

### M11 boxInfo generalisation (with T6/T7)  → plan P3-28

- An in-engine svg viewBox answerer.
- A measureHtml capability.
- ctx.raw(html, {measure:'auto'}).
- Html boxes from the native mock fixture.

**Golden impact:** None on existing goldens, since the feature is opt-in. Adds a raw-measure fixture.

**OPS bump (as designed):** True

### M12 After T6's WidthSpec  → plan P3-32, P1-16

- viewport.width's Affects drops from EMIT to BREAK and is removed from EmitSettings; the compiler proves emit no longer reads it.
- Relayout re-enters Break in place instead of forking.
- boxInfo is consumed at Layout.

**Golden impact:** Only T6's own churn (image w/h shown as a spec in blocks dumps).

**OPS bump (as designed):** False

**Fixes:** `emitter/measure-dependent-geometry-in-emit`

## Not generalized (kept special)

- **No estimate states and no late-font re-typeset** — pages-design §1 W (implemented) decides 'No re-typeset machinery … no estimate states', superseding architecture §2.4, document-model §6.4 and v2 §9; architecture.md:211-217 lists them as deferred. fontFace settles before the first Measure. The per-pid waiters could host an Estimated state later with a RES_VERSION bump.
- **No dependency-recording product graph, and no in-place re-entry into Once/PidRetry/Resumable stages** — v2 §9 ('a resize is a full re-typeset') and architecture §2.3 (an arena per document). Forking from the retained ops costs ingest and resolve, under 6 ms (editor-design §1). A non-mutating resolver (T3) or reusing whole paragraph products (editor-design §4's 'next lever') can be revisited only if bench-edit shows a need.
- **URL-keyed answers are not cached in the Session** — A URL is not content. The host LruCache revalidates with ETag or mtime/size; the engine cannot.
- **Execute-time data (CSL-JSON, data files, modules) stays out of the ResourceTable** — The executor already runs async JS with host access (notes-design 'As built'). It shares the locator, cache, failure policy and manifest, not the engine pull.
- **No provable width bound on measurement failure** — No font-independent upper bound exists. The degradation is a declared best effort, and v2 §7 rule 5's structural containment (nowrap) limits any residual error to the line. Degrading a pid to browser-wrapped HTML was rejected because the engine owns all heights (v2 §10).
- **Safety rails stay compile-time constants** — kMinLineSu, the hl span limit and the MetricStore packing bound protect invariants, not user policy.
- **No runtime grammar loading or registerLanguage** — code-design §1: grammars are build-time only. Runtime extension is a provider module.
- **The editor's map from capture classes to VSCode token types** — It is editor presentation, and was never an identity mapping (tokens.js:12-18).
- **No arbitrary functions move across the worker boundary** — Functions cannot be structured-cloned. Worker providers are module specifiers, and main-thread work is a named capability.
- **The Session is not persisted or shared across workers or tabs** — Keeps determinism simple; it is warm for one worker's lifetime, which covers the editor fast path.
- **The JS twin of the mock measurer** — mock.h:1-2 claims runtime/src/shared/mockmeasure.mjs, which does not exist (runtime/src/shared has only opbuf.mjs and ops.gen.mjs). Providers are swappable, so the normative C++ mock suffices. The claim is removed in M9.
- **Engine Span gains no SourceId here** — Per-occurrence spans belong to T1/T2. T9 carries SourceId in the diagnostics JSON, the references product and the locator; engine-produced diagnostics use source 0 until then.

## Risks

- Per-pid deferral assumes that emitting one top-level child never depends on another. That holds today: pendingAnchor and figDepth reset per group (emit.cc:18-22, 819), and fillSpaceContexts (emit.cc:908) must run per TopBlock. A debug assertion compares per-pid output with an emitDoc slice for every fixture.
- Host API break for worker.mjs, render.mjs, the vendored VSCode copy and zball-io via engine-dist. Mitigations: the tsr2_ prefix, real tsr_ shims for one release, and a hard tsr2_abi_version gate.
- Three new untrusted decoders (answers, settings, inputs). No fuzz infrastructure exists today, so M1F is a real prerequisite, not a formality.
- Per-stage settings views are a broad mechanical signature change across resolve, emit, break and layout. Landing hand views in M3 and generated ones with T4 risks touching those signatures twice. Mitigation: hand views mirror the generator's naming.
- Fork-on-patch makes relayout pay ingest, resolve and emit until T6's WidthSpec. bench-edit's resize scenario tracks it.
- Session memory grows with distinct strings in long editor sessions. Byte budgets and LRU apply, and the arena is rebuilt when live bytes fall below half.
- FontFaceSet cannot hold two faces with the same family name but different srcs. The host must alias them on both the measure side and the paint side (T7), or MetricKey separation alone will not prevent wrong glyphs at paint.
- $.load, #use, inputs and document providers widen the I/O reachable from documents. Locator confinement, requester classes and url_policy are mandatory before M7 ships.
- Deferral chains (fontFace → textWidth; tokens → widths; math) add rounds. They are bounded by maxRounds and reported as STALLED with the pending kinds named.
- Opt-in overlays change real-world pbr rendering until pbr2tsm emits cpp-literate and the corpus is re-exported.
- #use module instances persist per worker, so user module state can make output depend on edit history. This is outside the engine's determinism contract, and record-fixtures and static export run each document in a fresh executor realm.

## Open questions (decided in PLAN.md §3)

- Image sizing precedence: figure-design §3 says w comes first, while §8 as built makes scale the only display control and treats w/h as intrinsic. I propose keeping §8 and treating a lone w as a partial intrinsic. Owners to confirm.
- v2 §6 says the JS shim owns the measurement cache. The Session moves it engine-side, owned by the host. Accept the amendment?
- Under W, a late face is painted while layout keeps fallback metrics. Should hosts be able to opt in to a re-typeset when a face lands (default off), now that a fork under the new fontFace status costs only one re-measure?
- Should T3 make Resolve non-mutating, so that a live doc.lang patch can re-enter Resolve instead of forking? It is now purely a performance question.
- Which is the document settings layer: front matter, #set, or both (T1/T4)? And should ctx.settings expose it to handlers?
- #use module lifetime: cache per worker (fast, stateful) or re-import per execution (deterministic, slower)?
- Static export of module graphs: a lexical import scan, or require bundled modules?
- Should the semantic first paint wait for tokens when every code unit is a Session hit (costless), or always paint plain code first?
- Should hyphenation patterns become a lazily provided resource per language, or stay compiled in? This is T5's decision.

## Changelog (critique responses)

- C1-1 (blocker, Resolve re-run): ACCEPT.
  - Verified: resolve.cc:165-180 inserts the caption prefix with no guard; :487-508 replaces collect nodes and turns notes into markers; doc.h:69-74 resolves once, on the fresh instantiate output.
  - Restructured rather than patched: stages.def gives each stage a rerun class (Once/PidRetry/Resumable/Reentrant). Resolve is Once and may not declare needs.
  - A patch whose first stage precedes the reentrant tail returns REBUILD, and drive forks from the retained ops (doc.h:26), so a resolved tree is never re-resolved.
  - The labelManifest pull is gone (C2-6), so Resolve never blocks.
  - M2 applies in place only Break-onward rows.
  - The proposed 'resolve twice' assertion becomes 'fork == fresh' for every fixture.
- C1-2 (Affects hand-assigned): ACCEPT.
  - Verified: the float tracker reads lineHeight, paraSpacingEm and baseSizePx in the break phase (doc.h:266-267); emit reads the penalties, widthPx and cjk knobs (emit.cc:126-128, 201-239, 317, 332, 591, 721); textFontBox reads MetricStore during emit (math.cc:579-597); ε is applied at provideWord (measure.h:24-26) and at space quantisation (emit.cc:973).
  - Added generated per-stage settings views, so undeclared reads fail to compile.
  - Raw px in the table, quantisation in Measure.
  - A differential CI test.
  - Measure writes into blocks (emit.cc:926, 981), so it is non-reentrant and MEASURE rows fork.
- C1-3 (answer lifetime): ACCEPT. A Session hit is copied into the Doc's table arena; Need::ans points only there. The Session is refcounted, tsr2_session_free with live Docs returns PRECONDITION, and compaction is internal.
- C1-4 (generation race on write-through): ACCEPT the defect; solved more generally together with C2-2.
  - Generations and tsr_session_invalidate are deleted. MetricKey is complete: stack after fontFace fallbacks, faceDigest, dppx, features and lang.
  - Providers measure under the key, not ambient state, so a stale answer cannot be stored under a new key.
  - A provider that cannot honour the key's environment sets store=0.
  - Unit test: request, change env, provide; a doc under the new env must miss.
- C1-5 (doc-provider provenance): ACCEPT. Added a per-row flags byte (bit0 store) to TSRA and a resources.def docProviders column. Only codeTokens and boxInfo, whose keys are authored content, admit document providers, always with store=0; textWidth, fontVmet and fontFace are host-only. This answers former open question 3. Test added.
- C1-6 (key canonicalisation, hot path): ACCEPT. canonicalKey<K>() bytes are used only for Session lookup and write-through. Within a Doc, a per-doc integer index gives O(1) dedup (emit.cc:925-935 does up to four lookups per space block). An interner-collision test is added.
- C1-7 (non-monotone degradation): ACCEPT.
  - Verified: emit.cc:963-976 clamps the derived gap at 0, and emit.cc:945-953 derives the junction kern.
  - Degradation now happens at the consumed quantity: any failed operand drops the derivation.
  - Base widths use a generated per-codepoint bound × 1.2.
  - fontVmet, fontFace and boxInfo degradations are declared.
  - Stated honestly that no provable bound exists; nowrap containment (v2 §7 rule 5) is the guarantee.
  - Rejected the 'browser-wrapped pid' alternative: the engine owns heights (v2 §10).
- C1-8 (project numbering, fixpoint): ACCEPT.
  - Manifests carry start-independent local counter values and totals.
  - Pass A runs with no inputs, starts are computed from the totals, then Pass B.
  - Pass-A input loads answer empty, never from disk.
  - The locator denies project outputs to 'exec'.
  - The I4 widening is stated, with v2 §11.1 amended in M9.
  - Merged with C2-14 (no display strings or URLs in manifests).
- C1-9 (shims impossible under reused names): ACCEPT. Verified the current signatures at wasm_api.cc:39, 78, 92 and the old-style callers at render.mjs:22, 30. The new surface uses the tsr2_ prefix, the old symbols stay as real shims for one release, and tsr2_abi_version is drive's first call and fails hard.
- C1-10 (no fuzz infrastructure): ACCEPT.
  - Verified: no LLVMFuzzerTestOneInput anywhere; ci.yml has jobs native, web and dist (the critic omitted dist, which is immaterial); testing.md:110-111 and 122 are plans.
  - Added step M1F.
  - Each decoder's harness lands in its own step, and M7 and M10 are gated on them.
  - The inputs decoder is listed.
- C1-11 (semantic golden neutrality): ACCEPT. Verified that tests.cc:445-446 dumps tree and semantic before typesetWithMock provides tokens (:314). The golden runner requests semantic{partial:true}.
- C1-12 (non-idempotent requests): ACCEPT. Batches are numbered, and any resId of an answered batch without a row becomes Failed('provider-missing'). If answer() throws, drive submits an empty answer, so needs fail instead of spinning.
- C1-13 (FIFO backlog): ACCEPT, merged with C2-16. The mailbox keeps only the newest pending update, a generation check at every round and await aborts superseded jobs, and relayout and paginate stay ordered after the surviving update.
- C1-14 (diagnostics outside stage passes): ACCEPT. Added Origin {Stage, Settings, Input, Resource}, with Execute merged host-side, plus defined lifetimes and order. The provide-time 'image-load' (doc.h:229) becomes a Resource-origin diagnostic.
- C1-15 (hl twin determinism): PARTIALLY ACCEPT.
  - (a) Verified: native_tokens.h:55-80 never evaluates predicates; the node_modules queries contain predicates (rust 6, python 3, javascript 4, c/cpp/typescript 1). The native twin now evaluates string predicates. The generator refuses native:true for #match?, rather than inventing a shared regex dialect.
  - (b) Adopted a stable sort, but tie-break on cursor order instead of the proposed (e desc, capture index). Both twins run the same tree-sitter C core, so cursor order is identical, JS output stays unchanged and native churn is minimised.
  - (c) Moot: overlays move into the engine (C2-7) with byte-preserving UTF-8 masking.
- C1-16 (abi hash rejects compatible pairs): ACCEPT. tsr2_abi_version packs the settings schema major; the full hash is exposed in the settings product.
- C1-17 (no Execute stage): ACCEPT, merged with C2-3. Added STAGE(Execute, Host), the EXECUTE Affects bit and the REEXECUTE result; a fork never reuses ops that an EXECUTE row has made stale.
- C1-18 (imageInfo is not content-keyed): ACCEPT. resources.def gains a cache column: URL-keyed answers live only in the host LruCache with validators (ETag, or mtime/size); the Session holds Content kinds only. The thesis now says 'content-addressed'.
- C1-19 (providers cannot be cloned to the worker): ACCEPT, merged with C2-11. Providers are module specifiers (worker) or capability names (main thread); there is no thread field.
- C1-20 (recorder in M7): ACCEPT. Verified tools/record-fixtures.mjs:25-26. M7 lists the recorder, and .ops re-recording plus cite churn are announced at the T2/T3 placement step.
- C1 missing items: adhoc-invalidation-flags (C1-1, C1-2); adhoc-caches (C1-3 to C1-6); no-cross-document-labels (C1-8); bibliography moved to owned-by-other-theme (C2-18); missed:0 made moot by engine-side byte-preserving masking.
- C1 overlaps, all accepted:
  - T4: views generator; rows that feed resource keys carry EMIT.
  - T3: no non-mutating resolver required, thanks to forks; start-independent manifests.
  - T6: the float-tracker rows carry BREAK; T6 publishes the settings each stage reads, per step.
  - T2: amend architecture §4.1 for dynamic __use; module lifetime is an open question; document providers restricted.
  - T7: the shell imports defaults synchronously and the semantic message carries the CSS projection; stamping is replaced by complete keys.
  - T8: font rows carry EMIT until lazy measurement lands.
- C2-1 (manifest is a need log): ACCEPT.
  - Verified that doc.h:189 skips images with authored w and h, and render.mjs:4-5 is semantic-only.
  - Added a Resolve-stage 'references' product computed from the tree. The manifest is a view over references, the execution load log and fonts.declared.
  - The exporter scans module imports.
- C2-2 (incomplete MetricKey, two invalidation channels): ACCEPT, together with C1-4.
  - The complete MetricKey replaces generations, tsr_session_invalidate and the fontVmet face column.
  - A new fontFace row carries face status per document before Measure, preserving W and enabling document-declared fonts (C2-9).
- C2-3 (stage list frozen): ACCEPT.
  - stages.def follows the target layering, including Execute, Paginate and Paint, and generates the Affects enum.
  - Product options select serialisation only. page.height is a setting with Affects PAGINATE, and 'paged' is a Paginate product.
  - Fragment diagnostics go under the Execute origin.
- C2-4 (Affects unchecked): ACCEPT, the same fix as C1-2. The Affects vocabulary is shared with T4's style-property classification.
- C2-5 (image-only intrinsic sizing): ACCEPT.
  - Verified emit.cc:695-697 (a raw unit defaults to one leading).
  - imageInfo is generalised into boxInfo {image, svg, html}, with an in-engine svg answerer, a measureHtml capability and ctx.raw {measure:'auto'} (M11).
  - T6 owns IntrinsicSize consumption.
- C2-6 (labelManifest violates the admission rule): ACCEPT. The RES row is removed. Manifests are declared inputs (inputs.def, tsr2_set_input), host-provided or declared by a document and loaded host-side without being exposed to the script. Resolve never waits.
- C2-7 (overlays privileged in built-in providers): ACCEPT. One C++ overlay transform wraps the codeTokens need, and providers tokenize plain text. The cross-check shrinks to the priority contract.
- C2-8 (estimate state and in-engine answerers): PARTIALLY ACCEPT.
  - The answerer tier is accepted: Session-registered in-engine answerers for tsm tokens and svg, with hyphenPatterns reserved; gen/hyphen_en_us.h is existing compiled-in data.
  - The Estimated status is rejected: pages-design §1 W (status implemented) states 'no estimate states', superseding architecture §2.4, document-model §6.4 and v2 §9 (architecture.md:211-217 lists them as deferred). Those docs are amended in M9.
- C2-9 (binary Owner): ACCEPT, with the lattice owned by T4.
  - Precedence {HostOnly, HostDefault, HostForce, DocOnly}.
  - viewport.width (HostOnly) is split from page.size/margins (HostDefault).
  - Documents may add fonts.declared entries, which resolve through fontFace before Measure.
  - An ext.<name>.* namespace, with Affects EXECUTE, is exposed as ctx.settings over the pre-execution layers.
  - Front matter versus #set is left to T1/T4.
- C2-10 (host-policy rows in the engine registry): ACCEPT. A separate host-policy schema comes from the same generator; the Session budget is set only at creation. resources.base also leaves the engine: the locator keeps per-source bases, and boxInfo keys carry the SourceId.
- C2-11 (canvas memo duplicate; thread field): ACCEPT. The canvas memo is limited to per-round dedup, and main-thread providers exist only as capabilities.
- C2-12 (BreakMemo in resource/): ACCEPT. The Session exposes generic typed memo slots; break/ (T6) owns BreakMemo, keyed by the serialized complete BreakInput, so completeness holds by construction.
- C2-13 (two canonicalisation authorities): ACCEPT. The provider's canonLang is the only authority, and engine/gen holds only overlay specs and tag profiles, never aliases.
- C2-14 (manifest display and URLs): ACCEPT. Manifests export {label, role, counter, local values, anchorKey, docKey}. The importer formats in its own locale (T3), and URLs come from AnchorNamer plus project.urls (T7).
- C2-15 (relayout arena growth): ACCEPT. Relayout forks from the retained ops (the worker in M0, tsr2_doc_fork in M3); in-place re-entry applies only after T6's WidthSpec. Verified worker.mjs:248-256 reuses the doc.
- C2-16 (FIFO superseded work): ACCEPT; see C1-13.
- C2-17 (1em per codepoint is not a bound): ACCEPT; see C1-7.
- C2-18 (bibliography disposition): ACCEPT. The item is now owned-by-other-theme (T2+T3); T9 contributes ctx.load, the failure TTL, the hydration seed and the recorder.
- C2-19 (static and typeset settings parity): ACCEPT. The settings product gains {format:'css'} from T4's CSS contract, returned by renderTsm and emitted by the exporter. Verified the duplication in zball-io eleventy.config.js:64-73.
- C2-20 (two URL policies): ACCEPT. url_policy.def generates the engine's safeImageSrc (support.h:212-224) and the locator allowlist; the locator layers its requester-class rules on top.
- C2-21 (multi-source provenance): PARTIALLY ACCEPT. SourceId is added to the diagnostics JSON, the references product and the locator (per-source bases, so module-relative loads work). Adding SourceId to the engine's Span is left to T1/T2's per-occurrence spans.
- C2 missing items: static-export-template (C2-1); adhoc-invalidation-flags (C2-3, C2-4); bibliography (C2-18); resource-and-config-plumbing (C2-19); literate-cpp-special-case (C2-7).
- C2 overlaps, ownership settled as suggested:
  - T4 owns the schema, generator, cascade and lattice; T9 owns transport, stages.def (the Affects domain), host policy and the differential test.
  - T6 owns IntrinsicSize, Paginate and BreakMemo keys; T9 owns boxInfo and memo slots.
  - T7 owns the shell registry, template, AnchorNamer and Paint; T9 owns cap transport, ResourceHost and references.
  - T8 owns math measurement; T9 keeps deferral generic.
  - T3 owns import semantics and formatting; T9 owns inputs and serialisation.
  - T2 owns m.parse, async constructors and sidecars; T9 owns the Execute stage and ctx.settings.
  - T5 decides on dictionaries; T9 supplies the full MetricKey and the answerer tier.
  - T1 supplies the tokenizer for the in-engine tsm answerer and decides on front matter jointly with T4.
