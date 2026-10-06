# Host protocol (design; as built from plan P1-03)

How hosts — the browser worker, the Node renderer, `tsrc`, the golden runner,
the fuzzers — configure the engine, drive it and read its products. Design
source: `docs/remediation/design/T9-host-protocol.md` (A3, A4) and T4 (M3).

## 1. One settings document

Every host knob is a row of the `settings` section of
`engine/schema/schema.json` (table: `docs/settings-table.md`): a dotted path
(`host.width`, `doc.lang`, `code.snapKerning`, …), the `Config` member it sets,
a value domain, a default, a precedence and the stages it **affects**.
`tools/gen-schema.mjs` generates `Config` and its JSON codec
(`api/settings.gen.{h,cc}`) and the JS side (`shared/settings.gen.mjs`:
defaults, rows, the legacy option map, `settingsFromOptions`).

- `tsr2_set_config(doc, json)` applies a document in row order. `doc.lang` is
  the host default document language: it picks the locale terms (supplement
  words, docs/semantics-design.md §3), which `terms.*` override word by word
  (plan P1-10). As built (plan P3-30, D-T06): its default is `auto` — the
  engine detects the language of the text at ingest — and a document's own
  `$.doc({lang})` wins over the host's; the result is docinfo's `lang` (its
  `langSource`: host, document, detected). The shell sets the container's
  `lang` from it (the first paint's and every result's), never "auto";
  `export-static` writes it as `<html lang>`. Unknown paths are
  `setting-unknown`, values outside their domain `setting-type` (the row keeps
  its value), malformed JSON `setting-json`; these form the Settings slice of
  the diagnostics, replaced by the next document.
- The per-knob `tsr_*` setters (`tsr_config`, `tsr_set_font`, `tsr_set_lang`, …)
  are deprecated wrappers that build one-row documents (MD-06).
- JS hosts pass `settings`: `createEngine().typeset(src, el, {settings})`,
  `renderTsm(src, {settings})`, `export-static --settings f.json`. The old named
  options (`widthPx`, `fontFamily`, `lang`, `punctCompress`, …) are sugar for
  their rows; an explicit `settings` object wins.
- **Host policy** (`policy` section: pull-loop round cap, font deadline and
  retry, grammar retry, image timeout, cache sizes, the native image answer) is
  not document configuration: `createEngine({policy})` overrides the worker's,
  native drivers read `kPolicy*`.

## 2. Stages and products

`engine/src/api/stages.def` lists the pipeline with a rerun class per stage:

| stage | side | rerun |
|---|---|---|
| Compile | engine | Once |
| Execute | host | Once |
| Ingest | engine | Once |
| Resolve | engine | Once (it rewrites the instantiated tree) |
| BoxTree | engine | Once (the block structure of the resolved tree, plan P1-18; Emit reads it) |
| Emit | engine | PidRetry (re-run while math-text metrics arrive) |
| Measure | engine | Resumable (the pull loop) |
| Layout (breaking included, plan P1-15) | engine | Provisional (plan P3-28): Reentrant, and it may ask — a host box at its width (boxInfo); a run that asks is provisional, never painted, and runs again when the answers arrive |
| Paginate, Paint | engine | Reentrant (pure over earlier products) |

`Doc::validThrough` records how far a document has run (it replaced the old
`emitted`/`laidOut` flags); `invalidateFrom(stage)` drops the later products.
Resolve is its own stage (`stageResolve`: sidecars, references, numbering,
then the token/image needs), though `ingest()` runs both for hosts.

`engine/src/api/products.def` lists what can be inspected — `skeleton ast js
tokens outline astjson ops tree index semantic blocktree mathbox blocks hlist
breaks layout vlist paged html dl diags settings` —
with the stage each needs. `Doc::product(name)` serves `tsrc --stage=<name>`
and the golden runner alike.

## 3. Applying a patch: in place, REBUILD or REEXECUTE

A settings patch is applied in place when nothing it affects has run yet, or
when every stage from its first affected stage through `validThrough` is
Reentrant or Provisional (e.g. `cost.*` on a laid-out document re-breaks in place). Otherwise
`tsr2_set_config` changes nothing and returns **REBUILD** (4) — the host forks —
or **REEXECUTE** (5) when the first affected stage is Execute or earlier.

`tsr2_doc_fork(doc, patch)` builds a new document from the retained ops with
the patch applied. It clones the string and style tables first so ids — and
with them the metric answers, copied unless the patch affects Measure (fonts,
base size, epsilon) — mean the same; token and image answers replay; Compile
diagnostics carry over. The fork converges without asking the host again.
The worker's paginate is a fork (at the page width and height, discarded
afterwards). Relayout is not (plan P1-16): emit reads no width, so
`host.width` affects only Layout and Paint and its patch applies to the
live document in place — it breaks and lays out again, nothing earlier
re-runs (a patch that answered REBUILD would still fork). The golden runner
checks, for every fixture, that a fork without a patch reproduces the
document, that a width fork equals a fresh build at that width (warm
metrics included), and that the in-place width patch equals it too.

## 4. One drive loop

`engine/src/api/driver.h`: `driveToCompletion(doc, providers, maxRounds)` over a
`ProviderSet {tokens, images, width, vmet}` — one provider per resource kind;
`answerRound` takes the document's request batch through the wire codec,
answers every row and provides it (a provider that cannot answer marks its
row failed). `mockProviders()` is the golden set (the mock measurer on the
key's size, the policy's 512×384 image answer, plain code) and the native
tree-sitter token provider (`code/native_tokens.cc`, queries embedded at build
time) plugs in. `tsrc`, the golden runner and `fuzz_resanswer` share it; the
worker's `measureLoop` is the JS twin (with the per-document mailbox and
generation checks of plan P0-11). A document that is not done and has
nothing left to ask, or is not done within `policy.maxRounds`, has stalled.

## 4a. The resource pull (plan P1-19; design T9 A1)

Everything the engine can only learn after Ingest is a row of
`engine/src/resource/resources.def` (`tools/gen-res.mjs` generates the
column tables of both codecs: `resource/resources.gen.h`,
`runtime/src/shared/resources.gen.mjs`):

| kind | key | answer | cache |
|---|---|---|---|
| textWidth (1) | metric key, text | px | Content |
| fontVmet (2) | metric key | ascent, descent px | Content |
| fontFace (3) | family, src, weight, style | status (reserved: no declared faces yet) | None |
| codeTokens (4) | language, body (its overlay spans blanked: plan P3-22) | runs (start, end, tag)… | Content |
| boxInfo (5) | kind (0 image, 1 svg, 2 html), ref (src, or the markup), the width it is measured at (px; 0: an image's intrinsic size) | w, h, baseline px (baseline from the top) | Host |
| hyphPatterns (6) | language (BCP-47, as the text names it; plan P4-06) | TeX patterns, exceptions (hyphenated words), leftmin, rightmin, the hyphen glyph | Content |
| fontInk (7) | metric key | the reference ink: ascent of "H", descent of "p", px (plan P5-01; asked only under `math.referenceInk`) | Content |

The **metric key** is the complete measurement tuple (D-T04): the resolved
family stack, the digest of the loaded declared faces in it (0 until
`fonts.declared`), size px, weight, italic, font features (code runs:
`code.fontFeatures`), the shaping language (the run's, else `doc.lang`) and
`host.dppx`. A width is never applied under another key, so no answer is
ever invalidated. The engine keeps raw px; Measure quantizes (ceil to su +
`host.epsilonSu`).

**Wire** (little-endian; `resource/codec.cc`, `runtime/src/shared/rescodec.mjs`):

- request `tsr2_requests(doc, kinds)` → `[u32 length]` then `'TSRQ' u32
  version u32 batch | u32 nStrings (u32 len, bytes)… | u32 nKeys (u32 stack,
  u64 faceDigest, f64 size, u16 weight, u8 italic, u32 features, u32 lang,
  f64 dppx)… | u32 nKinds (u16 kind, u32 n, u32 resId[n], key columns)…`
  — every pending need of the kinds in the mask (bit = kind id; 0 = all), a
  new batch number each call;
- answer `tsr2_provide(doc, bytes, len)` ← `'TSRA' u32 version u32 batch |
  strings | u32 nKinds (u16 kind, u32 n, u32 resId[n], u8 status[n] (1 =
  failed), u8 flags[n] (bit0 store), answer columns, u32 msg[n])…`.
  Columns are column-major; a list column is its counts then its values.

**Validation and degradation**: an answer that is malformed or not for the
open batch is refused whole (`provider-invalid`; the needs stay pending). A
row that is a duplicate, out of range, non-finite or negative is invalid; a
row the answer leaves out is `provider-missing`; a failed row is
`measure-failed`. Each degrades only what consumed it: a width becomes the
code-point em bound × 1.2, vertical metrics 1em / 0.3em, tokens plain code
(tokens must be sorted, disjoint, on UTF-8 boundaries and of a known tag, or
the whole row fails), an image a placeholder with an `image-load` warning.
One diagnostic per kind and cause, in the Provide slice.

**The Session** (plan P1-21; design T9 A5; `resource/session.h`): a host's
documents share one (`tsr2_session_new(json)` — `{"budgetBytes", "answerers":
{"codeTokens.tsm": bool}}`, `tsr2_doc_attach(doc, session)` before the
document measures, `tsr2_session_free` refuses while a document is
attached; forks share their source's). A need is looked up in the
document's own table, then an in-engine answerer (the answerer registry:
`codeTokens.tsm`), then the Session (copied in), then the host. Answers of
Content kinds the host marks `store` are written through; Host kinds (image
sizes) stay in the host's cache. There are no generations and no
invalidation: keys are complete, so an entry can only be unused. The KP memo
is a Session memo slot. A document nobody attached gets a private Session
(each golden fixture; the runner also replays every fixture on one warm
Session and requires the same bytes).

**Per-block deferral** (plan P1-20; design T9 M5): Emit runs per top-level
block. A block waits while one of its code blocks (or, plan P3-28, an
inline `raw(measure: 'host')` box) waits for its answer; a block whose
display formula lacks text metrics defers (the attempt and its Emit
diagnostics are discarded, what it lacked rides the next request). Every
other block is emitted meanwhile, and its widths join the same round —
there is no whole-document barrier and no document re-emit.

**Image sizes after Emit** (plan P3-32; design T9 M12): no block waits to
emit for an image. The Resolve scan files every image's `boxInfo` need, so
the first round asks for it beside the widths; its consumers come later.
An inline image is a pending object that Measure settles (the object
table's image finalizer: Measure waits while the answer is pending); a
block or floated image is a `Provided` size spec that Layout asks for
through the same `BoxAsker` as P3-28's host boxes (pending: a provisional
layout; failed: the placeholder). Emit reads no answer and no width — the
second by construction (stage views, `config-closure` lint).

Answers never touch the authored tree: emit folds code tokens, Measure and
Layout fill an image's missing dims from the table (an author's lone `w` or
`h` stays, the other follows the aspect ratio), and the semantic product
reads the same token answers. The JSON `tsr_measure_requests` and the per-kind
`tsr_provide_*` exports remain as shims over the same table.

## 4b. The resource host (plan P3-21; design T9 A2, M7; D-I09)

The engine's needs (§4a) and a document's execute-time loads are answered
in one place, `runtime/src/shared/resources/`:

- **ResourceHost** (`host.mjs`; one per worker, one per Node process unless
  the caller passes its own): a provider registry, one LRU cache and
  seeded entries (a static export's known bytes). The cache (`lru.mjs`)
  is limited by entries and bytes and has a time to live, with a shorter
  one for failures. These are the policy rows `resourceCacheEntries`,
  `resourceCacheBytes`, `resourceTtlMs` and `resourceFailureTtlMs`.
  `host.register(kind, provider)` makes `provider.resolve(rows, ctx)` the
  answerer of a `resources.def` kind. Built-in, host and document providers
  stand on equal terms. A **document** provider (`{document: true}`; `#use`,
  P3-31) may answer only kinds whose `docProviders` column is true
  (`codeTokens`, `boxInfo`: keys that are authored content), and its rows
  are answered with `store: false`, so the Session keeps no answer of one
  document's code for another. A provider that throws fails its kind's rows
  (an image's placeholder, plain code), never the batch. A kind may have
  several providers (plan P3-22): a row goes to the latest registered whose
  `match(row)` accepts it (none declared: every row). A host thus adds a
  language (`match: (r) => r.lang === 'tla'`) beside the built-in
  highlighter.
- **ResourceJob** (`host.job({bases, root})`, one per document): the pull
  loop's `answer(request, {stale, capability})`, the loads
  (`load(src, {as: 'text' | 'json' | 'bytes', role})`) and the
  **manifest**: `[{url, role, source, status, requester}]` of every load,
  image and denial.
- **ResourceLocator** (`locator.mjs`): a reference resolves against the
  base of the source that made it (`bases.doc`: the page URL in a worker,
  the document's folder in Node), a `/path` against `root`. The requester
  class (`exec`: `$.load`, `ctx.load`, `#bibliography`; `image`; `input`,
  P3-31) and the use (`image`, `link`, `load`) select the scheme policy of
  `engine/schema/url_policy.def` (plan P3-20). The C++ `safeImageSrc` /
  `safeLinkUrl` and the JS `urlAllowed` are generated from it. A file must
  lie below `root` or the document's folder, checked by path when resolved
  and by real path when read. Reads revalidate: http(s) by ETag and
  Last-Modified, files by mtime and size.

Providers in the worker: canvas `textWidth`, `fontVmet` and `fontInk`, `codeTokens`
(the highlighter), `hyphPatterns` (plan P4-06, `providers/hyph.mjs`: the
dictionary assets of `runtime/assets/hyph` — tools/hyphc.mjs --assets —
by tag, then by its shorter prefixes; Emit waits for the answers, a
language without one falls back to en-US or does not hyphenate, and says
so) and `boxInfo`: images (kind 0, `providers/images.mjs`: the
header sniff, then decode, then the main thread's `imageDims` capability;
sizes cached by URL, failures only for the failure time to live) and, plan
P3-28, svg and html boxes (kinds 1 and 2, `providers/html-boxes.mjs`): the
main thread's `measureHtml({html, widthPx, scope})` capability lays the
markup out at the width in a hidden probe inside the document's typeset root
(`scope`: the worker session's doc id) and answers `{h, baseline}`; answers
are cached by (scope, width, markup). The engine answers an svg its
attributes size (`resource/box.h svgBoxPx`) without asking. A box need is
keyed by (kind, payload, width); Layout's needs make its run provisional
(stages.def `Provisional`): the pull loop answers them like any batch, and
Layout runs again — at most twice asking. The native driver's `boxes`
provider and the WASM debug mock answer svg and html boxes with
`measure/mock.h mockBoxPx` (the markup's text in 16px mock widths, in
lines of the width, 20px each; baseline 4px above the bottom). The host adds its own
with `createEngine({providers: [{kind, module}]})`: the worker imports each
module and registers its default export before the next job runs. In Node,
`renderTsm(source, {providers: [{kind, provider}]})` or
`renderTsm(source, {host})`.

`$.load(src, {as})` (a document's script) and `ctx.load(src, {as})` (a
fence or region handler) are the same job load; `#bibliography(src)` reads
through it with role `bibliography`.

**Products.** `tsr2_product(doc, name)` returns a product's text after
Resolve:
- `references`: one JSON line per image, `{role, src, s, e, allowed}`;
- `docinfo`: `{lang, title}`, where the title is the first heading's text;
- `settings`: the effective settings document.

`renderTsm` returns `{html, css, diagnostics, ok, manifest, settings,
docinfo}` (`diags` is diagnostics' older name). Its manifest is the
references, the job's log and the fonts the caller declared (`opts.fonts`).

## 5. Fixtures, profiles, tsrc

- `test/profiles/golden.json` — `{host: {width: 300}, doc: {baseSize: 16, lang: "zh-CN"}}`.
- `X.fixture.json` next to a fixture — `{profile, settings, products, inputs}` —
  replaces the old file-name conventions (`*indent*`, `*punct-*`, `*snap*`,
  `*base18*`, `*paged*`). `"products": ["paged"]` goldens the pagination at the
  fixture's `page.height`. (`*diag*` still selects the diagnostics golden.)
- `tsrc --stage=<product> --profile=golden --fixture=X.fixture.json --ops=X.ops
  X.tsm` reproduces the golden file byte for byte; `tools/check-tsrc.mjs` checks
  all of them in gate G1. Settings layer profile < fixture < `--settings` <
  `--set path=value`; `--width/--base/--indent/--punct/--snap/--page-height`
  remain as sugar.

## 5a. Declared inputs and cross-document labels (plan P3-31)

As built (design T9 A7, T3 S7; INTEGRATION: T9's transport, T3's semantics):
- `engine/src/api/inputs.def` lists the declared inputs — blobs a host gives
  a document before Ingest, part of its identity (a fork copies them), never
  shown to a script: `labels`, the other documents' labels products as one
  JSON array (`semantic/manifest.h`, a validating decoder; fuzz target
  `fuzz_inputs`). `tsr2_set_input(doc, name, bytes, len)` (0 taken, 1
  refused: unknown or after Ingest); `createEngine().typeset(src, el,
  {inputs: {labels}})`, `renderTsm(src, {inputs})`, `tsrc --labels=F`
  (repeatable), and a fixture's `"inputs": {"labels": [files]}`.
  (Plan P5-01) `mathFonts`: a host's math fonts, `.tsmf` blobs back to back
  (math-design §15; decoder `MathFontRegistry::decode`, fuzz target
  `fuzz_tsmf`), named by `math.fonts`. The worker builds the input from the
  declared fonts of role `math` that have a `metrics` URL (fetched once per
  URL). `renderTsm` builds it from the same entries, read below `rootDir`.
  A binary input is passed as a `Uint8Array`. Elsewhere it comes from
  `tsrc --math-font=F` and a fixture's `"inputs": {"mathFonts": [files]}`.
- The `labels` product (Resolve): `{"v":1, "doc", "totals": {counter: n},
  "labels": [{label, class, level, number: [[counter, [n…]]…], title,
  anchor}]}` — labels sorted, one per line; a number's components by counter
  (within-parents first), start-independent (the document's own start taken
  off); totals: the steps of each document-wide counter's first component.
  A fixture whose products include `labels` and that has an `X.labels.json`
  beside it is checked against it (`tsr_tests --update` rewrites it).
- Settings: `project.doc` (its key), `project.starts` ({doc: {counter: n}}:
  its own row offsets its counters; the others' are added to the numbers of
  their labels), `project.urls` ({doc: url}: an external reference links to
  `url#<id>`, the doc key when unset; a page outside the link policy,
  `url_policy.def`, is none — the security review's P3-31 addendum).
- Resolve: a reference finds a local label first, then a citation key, then
  another document's label (`ref-shadowed`, info, when a local one hides
  it); an external label is formatted with this document's class templates,
  terms and counter patterns after its producer's start, or shows its
  number, title or label when this document lacks its class; `#link({target})`
  reaches it too.
- The project driver (`runtime/src/node/project.mjs` `renderProject`; CLI
  `tools/tsm-project.mjs build [tsm.project.json]`, D-S07: `{files, out,
  settings, urls, continue, offset}`; a document's key is its file name):
  Pass A renders each document with only its key — its manifest; the
  starts are the prefix sums, in book order, of the totals of the counters
  in `continue` (default heading, figure, table, equation; plus `offset`);
  Pass B renders each with `project.{doc, starts, urls}` and the others'
  manifests as its labels input, once more when a manifest changed (a title
  citing another document), at most three passes (`project-unstable`). It
  writes `<key>.html` (the static page of `tools/lib/static-page.mjs`, the
  one export-static writes; hydration gets the same settings and inputs)
  and `<key>.labels.json`. `tools/check-project.mjs` (gate G6) builds a book.
- `$.labels.import(src)` (a document's own): the host reads `src`
  (requester `input`) and adds it to the labels input before Ingest; the
  script gets nothing back; a failure is `labels-import`. A recorded
  fixture declares the same file in its `inputs`.

## 5b. Modules: `#use` (plan P3-31; D-I08)

As built:
- `#use(spec)` is the async std function `use` (schema `stdlib.asyncFunctions`):
  a statement-position splice whose value is nothing. The executor's
  `host.use(spec)` asks the document's resource job for the module —
  `ResourceJob.module(src)` resolves it with the one locator (use `load`:
  http(s) in a browser; in Node a file below the document root or folder,
  by real path), reads it through the cache, records a `module` row in the
  manifest, and `import()`s its URL with `?h=<FNV-1a 64 of its bytes>`. A
  changed module is a new URL; an unchanged one is the same instance,
  shared by the worker's documents; its own relative imports resolve
  against it as usual.
- Registration, with the `$` of this execution: the default export is
  called as `default($, std)` (may be async); `fences` → `$.fence`,
  `regions` → `$.region`, `providers: [{kind, provider}]` →
  `job.register(kind, provider)`: a document provider of this job only,
  for `docProviders` kinds, consulted before the host's. A module that
  cannot be read, is denied, or throws is `use-module` (warning) at the
  `#use`, and the document goes on without it.
- Determinism (D-I08): a module keeps no mutable state across executions —
  each execution passes a fresh `$`. The recorder (`tools/record-fixtures.mjs`)
  executes every fixture twice in one process and fails on other bytes
  (`NONDETERMINISTIC`); the policy row `checkExecution` (createEngine's
  `policy`, renderTsm's `policy`) makes the worker or Node renderer execute
  again with a fresh job and report other ops as `exec-nondeterministic`.
- Base: a session's relative paths resolve against `opts.baseUrl` of
  `typeset` (default `document.baseURI`); the e2e audit gives each fixture
  its folder.

## 6. ABI

`tsr2_abi()` (plan P1-01) is the one handshake (`resVersion` is
resources.def's: a runtime and an engine with different wire formats refuse
each other); `tsr2_set_config`, `tsr2_doc_fork`, `tsr2_requests` and
`tsr2_provide` are the `tsr2_*` entry points so far. The remaining surface
(`tsr2_typeset(target)`, `tsr2_get(product, opts)`, inputs) arrives with plan
P3-37.

As built (plan P3-37; design T9 M9): `tsr2_get(doc, product, opts)` serves
every product of `products.def` as u32 length + bytes (empty: unknown, or
its stage not run; `diags`, `diagnostics` and `settings` at any stage;
`opts` reserved), the diagnostics JSON among them (document-model §10);
the worker posts it with every result. Inputs came with P3-31
(`tsr2_set_input`). `tsr2_typeset(target)` was not added: `tsr_typeset`
drives to Layout and returns NEED_MEASURE while a need is open, paginate is
a fork, and a product is read at its own stage (`tsr2_get` checks it) — no
host asked to stop earlier. The `tsr_*` shims stay (architecture §2.5): the
remediation publishes no engine-dist release (MD-07), after which they were
to go.

## 7. Interim choices

- Per-stage settings views are enforced by `tools/lint-arch.mjs`
  (`settings-view-<stage>`): a stage's sources may read only `Config` members
  whose row lists the stage in `affects`. Generated struct views replace the
  lint in plan P3-02.
- The float tracker and the table/sidecar width policies still run in
  `Doc::typeset` (breaking moves into layout in P1-15); `host.width` affects
  Emit until P1-16 takes the width out of emit, so a width change forks.
