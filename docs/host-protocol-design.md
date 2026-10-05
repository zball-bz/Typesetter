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

- `tsr2_set_config(doc, json)` applies a document in row order (so `doc.lang`,
  which resets the `terms.*` supplements, precedes them). Unknown paths are
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
| Emit | engine | PidRetry (re-run while math-text metrics arrive) |
| Measure | engine | Resumable (the pull loop) |
| Break, Layout, Paginate, Paint | engine | Reentrant (pure over earlier products) |

`Doc::validThrough` records how far a document has run (it replaced the old
`emitted`/`laidOut` flags); `invalidateFrom(stage)` drops the later products.
Resolve is its own stage (`stageResolve`: sidecars, references, numbering,
then the token/image needs), though `ingest()` runs both for hosts.

`engine/src/api/products.def` lists what can be inspected — `skeleton ast js
ops tree semantic mathbox blocks breaks layout paged html diags settings` —
with the stage each needs. `Doc::product(name)` serves `tsrc --stage=<name>`
and the golden runner alike.

## 3. Applying a patch: in place, REBUILD or REEXECUTE

A settings patch is applied in place when nothing it affects has run yet, or
when every stage from its first affected stage through `validThrough` is
Reentrant (e.g. `cost.*` on a laid-out document re-breaks in place). Otherwise
`tsr2_set_config` changes nothing and returns **REBUILD** (4) — the host forks —
or **REEXECUTE** (5) when the first affected stage is Execute or earlier.

`tsr2_doc_fork(doc, patch)` builds a new document from the retained ops with
the patch applied. It clones the string and style tables first so ids — and
with them the metric answers, copied unless the patch affects Measure (fonts,
base size, epsilon) — mean the same; token and image answers replay; Compile
diagnostics carry over. The fork converges without asking the host again. The
worker's relayout and paginate are forks: the live document's products are
never mutated (paginate forks at the page width and height and discards the
fork; relayout replaces the live document once its fork has converged). The
golden runner checks, for every fixture, that a fork without a patch
reproduces the document and that a width fork equals a fresh build at that
width (warm metrics included).

## 4. One drive loop

`engine/src/api/driver.h`: `driveToCompletion(doc, providers, maxRounds)` over a
`ProviderSet {tokens, images, metrics}`; `mockProviders()` is the golden set
(mock measurer, the policy's 512×384 image answer, plain code) and the native
tree-sitter token provider (`code/native_tokens.cc`, queries embedded at build
time) plugs in. `tsrc`, the golden runner and the WASM debug build share it;
the worker's `measureLoop` is the JS twin (with the per-document mailbox and
generation checks of plan P0-11).

## 5. Fixtures, profiles, tsrc

- `test/profiles/golden.json` — `{host: {width: 300}, doc: {baseSize: 16}}`.
- `X.fixture.json` next to a fixture — `{profile, settings, products}` —
  replaces the old file-name conventions (`*indent*`, `*punct-*`, `*snap*`,
  `*base18*`, `*paged*`). `"products": ["paged"]` goldens the pagination at the
  fixture's `page.height`. (`*diag*` still selects the diagnostics golden.)
- `tsrc --stage=<product> --profile=golden --fixture=X.fixture.json --ops=X.ops
  X.tsm` reproduces the golden file byte for byte; `tools/check-tsrc.mjs` checks
  all of them in gate G1. Settings layer profile < fixture < `--settings` <
  `--set path=value`; `--width/--base/--indent/--punct/--snap/--page-height`
  remain as sugar.

## 6. ABI

`tsr2_abi()` (plan P1-01) is the one handshake; `tsr2_set_config` and
`tsr2_doc_fork` are the first `tsr2_*` entry points. The remaining `tsr2_*`
surface (`tsr2_typeset(target)`, `tsr2_get(product, opts)`, inputs, resources)
arrives with plans P1-19 and P3-37.

## 7. Interim choices

- Per-stage settings views are enforced by `tools/lint-arch.mjs`
  (`settings-view-<stage>`): a stage's sources may read only `Config` members
  whose row lists the stage in `affects`. Generated struct views replace the
  lint in plan P3-02.
- The float tracker and the table/sidecar width policies still run in
  `Doc::typeset` (breaking moves into layout in P1-15); `host.width` affects
  Emit until P1-16 takes the width out of emit, so a width change forks.
