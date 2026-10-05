# Code Architecture

Status: **draft for review**. Companion to [design-decisions-v2.md](design-decisions-v2.md); maps the v2 pipeline onto modules, boundaries, build, tests, and milestones. Section references (§n) point into v2.

---

## 1. Repository layout

```
Typesetter/
  docs/                    design documents (this file, design-decisions-v2)
  engine/                  C++ engine → typesetter.wasm (+ native builds)
    CMakeLists.txt
    schema/                schema.json: ops vocabulary, settings, domains (gen-schema)
    gen/                   committed build-time artifacts (math vocabulary, hyphen patterns, math font metrics, TextRules)
    data/                  data the generators and the engine read (math symbols, MathML Core, elements, locales)
    rules/                 TextRules classes and the pinned UCD
    src/                   modules, one directory per pipeline stage (see §2.2)
    test/                  native unit + golden tests (ctest)
  runtime/                 JS/TS runtime
    src/shared/            worker↔main protocol types, generated ops.ts
    src/worker/            engine host, executor, measurers, fence registry, op writer
    src/main/              public API shell, DOM injection/upgrade, copy, observers
  tools/                   build-time generators: gen-all (gen-schema, gen-syntax, gen-res, mathdict.py, ucdc), hyphc, mathc.py
  fonts/                   bundled fonts (Euler-Math)
  apps/playground/         dev editor page (successor of the PoC demo)
  test/
    fixtures/              *.tsm sources + recorded *.ops buffers (see §6)
    golden/                per-stage expected outputs
    e2e/                   Playwright: screenshots + invariant audits
```

Source extension: `.tsm`. C++ namespace: `tsr`.

## 2. C++ engine

### 2.1 The dual-target rule

**The engine core compiles to two targets: WASM (production) and a native binary (tests, tools, fuzzing).** Nothing outside `engine/src/api/` may include an Emscripten header or assume a browser. All host interaction goes through two seams:

- the **measurement seam** — the pull-based request/provide protocol (§2.4); native tests plug in a mock measurer;
- the **boundary seam** — `api/` exposes the C-ABI for WASM (`wasm_api.cc`) and a CLI (`tsrc`) for native.

Why: golden tests and fuzzing run natively in CI with gdb/ASan/UBSan available; iteration speed does not pay the emcc tax; the WASM glue stays a thin adapter that cannot accumulate logic.

`tsrc` is the inspectability tool (v2's "every stage inspectable"): `tsrc --stage=skeleton|ast|js|tree|blocktree|blocks|hlist|breaks|layout|vlist|dl|semantic|html|paged input.tsm` prints that stage's dump. Stages after ops ingestion read recorded `.ops` fixtures (see §6 for why).

### 2.2 Module map

One directory per stage; a stage's input and output are named types with a debug dump. Dependency order is strictly top-to-bottom — no cycles, enforced by include layering.

```
support/    arena, string interning, utf8, span, Result, diagnostics sink
source/     SourceText (raw bytes, line starts, CRLF cooking); no source-map
            builder — the LowerProgram's ops carry their source spans
linepass/   SourceText → BlockSkeleton         (one container protocol — quote,
                                                list item, region — verbatim carries,
                                                statements; docs/syntax-design.md §6)
syntax/     syntax.def → syntax.gen.{h,cc}     (character classes, delimiters,
                                                blocks, sugar slots + payloads,
                                                token tags; docs/syntax-design.md);
            cursor.h (a leaf's joined text) and lexer.h (atoms, brackets)
inline/     BlockSkeleton → AST                (inline parser over each leaf's joined
                                                text, dispatched by the INLINE rows;
                                                atoms and brackets from syntax/lexer.h;
                                                PackCC was planned and never adopted)
ast/        CallAST: Call{slot}/Splice/Stmt/Error nodes, side records, generic
            dump (plan P1-05)
codegen/    AST → LowerProgram + hole module   (lower.def opcodes; docs/lowering-design.md)
ops/        generated vocabulary (ops.def, schema.gen.*), OpReader + value validation; writer lives in JS (§3)
model/      ContentTree: node defs, instantiation from ops (EMIT walk with
            style-stack resolution §12), anchors, diagnostics, comment nodes
elements/   the element registry: one data row per class of node
            (engine/data/elements.json; docs/semantics-design.md)
semantic/   counters, locale terms, the Index (LOCATE, BIND) and MATERIALIZE
            (templates, references, sites, collectors, flows)
resolve/    the phase driver (§11.1): LOCATE → BIND → MATERIALIZE; the input
            tree is not changed, the output replaces it (plan P1-10)
shape/      TextRules: the one character classifier (classes from
            engine/rules + the pinned UCD via tools/ucdc.mjs; docs/shaping-design.md);
            hlist.h: the horizontal item list (Box/Glue/Penalty/Disc, run
            instances, cold records) and its legality lint; objects.h: the
            inline object registry (formula, image, raw, error box)
boxtree/    ContentTree → the box tree (plan P1-18; docs/layout-design.md):
            LayoutBlocks with a layouter by content model (Paragraph, Stack,
            Replaced, Grid, Table), traits (TraitTable), indents, markers and
            anchors; roles are data (the role table), never compared downstream
emit/       the box tree's leaves → their shaped content: HLists (script
                                                segmentation, CJK rules App C,
                                                hyphenation, run instances), code
                                                lines, cells and typed payloads;
            fuseLegacy lowers each list to the legacy breaker's blocks (until
            P4-08); legacy.cc keeps the pre-HList emitter as its CI oracle
hyphen/     Liang runtime over compiled patterns (gen/)
measure/    MeasureRequest batching, per-doc metric store, exact/pending/invalid
            states (§9), ε policy (§7)
break/      Knuth–Plass DP (port of PoC linebreak.ts, cost fn + parshape widths)
layout/     the box tree + shaped leaves → frames of fragments (a layouter
                                                per LayouterId; breaks, vertical
                                                metrics, spacing values, k-rule §8,
                                                anchors); paginate.cc cuts sheets;
            never sees model.h (architecture lint, transitively)
paint/      fragments → the DisplayList (runs, inline payloads, the px values
            the HTML prints; docs/render-design.md)
render/     semantic_html.cc (flow HTML §9, a tree walk) and typeset_html.cc
            (the stateless DisplayList writer for flowing and paged output),
            html_writer.h (escaping, the attribute allowlist, AnchorNamer)
api/        wasm_api.cc (C ABI, EMSCRIPTEN_KEEPALIVE), native_cli.cc (tsrc)
```

### 2.3 Memory model

**Arena per document.** AST, content tree, block streams, layout frames all allocate from a document-scoped bump arena and die together at `doc_free`. Strings are interned per document. Styles (`TextStyling`) are interned by hash — blocks store style ids, not copies. No smart-pointer graphs anywhere in the pipeline.

Why: the pipeline is single-pass over document-scoped data; wholesale free is both the fastest and the simplest correct policy, and it makes leak analysis trivial (one arena counter).

### 2.4 The engine is a resumable state machine, not a blocking pipeline

Measurement is asynchronous to the engine: canvas measurement lives in the worker, DOM-backend measurement lives on the main thread, and native fallback (§9) deliberately renders before measurements settle. Therefore the engine **never blocks on a measurement callback**; it returns control:

```
tsr_typeset(doc, params)      → NEED_MEASURE | OK
tsr_measure_requests(doc)     → batch buffer (unique string×style tuples + style tuples)
tsr_measure_provide(doc, buf) → void          (then call tsr_typeset again to resume)
```

The doc handle retains stage products (content tree, per-paragraph block streams, metric store), so resuming re-runs only what the new measurements invalidate — which is also exactly the machinery `relayout(width)` (re-break only) and dppx invalidation (§6) need. The `pending(estimate)` state makes the same loop serve estimate-first typesetting for fallback upgrades.

As built (plan P1-03; docs/host-protocol-design.md): the stages and their rerun classes are `engine/src/api/stages.def`; the document records `validThrough` and a settings patch either re-enters the Reentrant tail in place or returns REBUILD, upon which the host forks a new document from the retained ops (`tsr2_doc_fork`, metric/token/image answers carried over). Relayout and paginate are forks — the live document's products are never mutated. Products are `engine/src/api/products.def`; `api/driver.h` is the one native drive loop.

Two rejected alternatives, recorded:

- **Synchronous EM_JS callback** (v1's protocol): incompatible with the DOM measurement backend (worker→main is inherently async) and with clean native testing.
- **Atomics.wait on SharedArrayBuffer**: requires COOP/COEP headers, which **GitHub Pages cannot set** — the deployment target forbids it. The pull model needs neither SAB nor ASYNCIFY (also rejected: size/perf cost for no remaining benefit).

### 2.5 Boundary surface (api/)

Small C ABI; buffers are length-prefixed regions in WASM memory that JS copies out. One document handle = one pipeline state; handles are independent; the engine is single-threaded.

```
tsr_version()
tsr_doc_new(config_json) / tsr_doc_free(doc)
tsr_compile(doc, src)            → status;  outputs: tsr2_program (LowerProgram bytes),
                                    tsr_get_js (the hole module; "" without user code)
tsr_ingest_ops(doc, buf)         → status   (decode ops → content tree → resolver)
tsr_typeset(doc, params)         → NEED_MEASURE | OK      (params: width, dppx)
tsr_measure_requests(doc)        → buffer
tsr_measure_provide(doc, buf)
tsr_render_semantic(doc)         → html
tsr_render_typeset(doc, range?)  → html     (whole doc or paragraph range, for upgrades)
tsr_layout_info(doc)             → buffer   (paragraph ids, rects, line maps — upgrade payload §9)
tsr_parse_fragment(doc, str)     → ops buffer   (m`…` / m.parse re-entry)
tsr_diagnostics(doc)             → buffer
tsr_relayout(doc, params)        → NEED_MEASURE | OK   (reuses cached block streams)
```

## 3. The ops contract (the one shared artifact)

The op buffer is the only data structure both languages must agree on, so it has a single source of truth: **`engine/schema/schema.json`** (opcodes, node kinds with their level and body model, argument keys, and every kind's attributes with value domains). `tools/gen-schema.mjs` (run by `tools/gen-all.mjs`, checked by CI) generates `engine/src/ops/ops.def` (the X-macro lists the C++ side includes), `engine/src/ops/schema.gen.{h,cc}` (constants and the per-kind attribute tables the reader validates against), `runtime/src/shared/ops.gen.mjs` and `docs/schema-table.md`; `engine/schema/schema.lock.json` pins ids and since values. The reader validates every argument against its kind's domain at decode (out-of-domain values are dropped with an `ops-arg` warning; unknown vocabulary becomes an `error{ops-invalid}` node), so consumers never see unchecked values. The buffer header carries a version byte (plan P1-01): the writer stamps the newest vocabulary row it used, at least `minCompat` (rows carry the `since` they were added in), and the reader accepts `minCompat..opsVersion` — an op, kind or attribute newer than the buffer's byte is malformed. New vocabulary takes the next `since` (old buffers stay readable, byte-identical); only a change of meaning of an existing row raises `minCompat` and needs a full re-record. Before writing any op the host performs the one ABI handshake, `tsr2_abi()` → `{opsWindow, schemaHash, programAbi, resVersion, renderVersion, syntaxVersion}` (`runtime/src/shared/abi.mjs`; the worker and the Node renderer check it): the engine's window must cover what the runtime writes and both sides must come from the same schema.

**Encoding: a value DAG plus an emission schedule.**

- `MAKE_TEXT(str)`, `MAKE_NODE(kind, args, child_ids)` — constructors append MAKE ops in JS evaluation order (children before parents, so ids are simple sequence numbers). A JS content value is just an id; storing it in a variable and using it twice yields a DAG, no copying.
- `EMIT(id)`, `STYLE_PUSH(delta)`, `STYLE_POP_TO(height)` — the schedule. Codegen wraps top-level content in `EMIT`; `$.style` writes stack ops; markup styling *inside* a value compiles to structural `MAKE_NODE(styled, …)` nodes instead.

Why this shape: §12 fixed style binding at **emission time** — a stored value takes the styles active where it is spliced, and may be emitted twice under different styles. A flat post-order stream without the DAG/schedule split cannot express that; this encoding makes the decided semantics structural. Instantiation (model/) walks EMITs, composing the schedule stack with structural style nodes into interned `TextStyling` per run.

Layout: header (version, counts) · string table (UTF-8 blob + varint offsets) · op stream. The JS writer (`worker/opbuf.ts`) and C++ `OpReader` are cross-tested against shared binary fixtures (§6).

## 4. JS runtime

### 4.1 Worker (module worker — required for real ES imports)

- **host.ts** — loads the WASM module (Emscripten `MODULARIZE` + `EXPORT_ES6`, `ENVIRONMENT=worker,node`), drives the pipeline: compile → execute → ingest → typeset-loop → render, and the upgrade re-loop when pending measurements settle.
- **executor.ts** — *(as built, plan P2-02: `executor.mjs` decodes the LowerProgram and runs it with `shared/lower.mjs`; only the hole module — the user's code — is imported as a Blob-URL ES module, cached by hash; `docs/lowering-design.md`)* turns the generated program into a **Blob-URL ES module** and `import()`s it. Consequence for codegen: `#use "./x.js"` compiles to a real static `import`, resolved against a caller-supplied base URL; after imports, generated `__reg(mod)` calls auto-register `fences` exports (document-order registration, §4.1 of v2). `//# sourceURL` + the source map make user code debuggable in devtools. The context argument is built here: constructors bound to an `OpBuf` instance, the `m` tag (calls `tsr_parse_fragment`, splices the returned ops, rebasing ids), and `$` (style stack ops, counters, fence registration). Constructors also maintain **shadow nodes** — lightweight JS mirrors of what they wrote — so user code can traverse and regroup content values (table cell splitting); see document-model §4.1.
- **measure/** — `canvas.ts` (OffscreenCanvas + `textRendering='geometricPrecision'`), `domproxy.ts` (batches forwarded to main), `fontfile.ts` (precompiled bundled-font metrics from `gen/`). All behind one `Measurer` interface; the cache (keyed string×style×dppx) sits above the backends.
- **fences.ts** — tag → handler registry; wraps handler calls (async, try/catch → error block ops, `ctx` construction per v2 §4.1).
- **opbuf.ts** — the writer half of §3.

### 4.2 Main thread (thin by design)

- **shell.ts** — public API: `typeset(source, container, opts) → handle { relayout, on, destroy }`. Owns the source string (copy rebuilds from it — no worker round-trip on copy).
- **dom.ts** — semantic HTML injection, per-paragraph atomic swaps, anchor bookkeeping, `size-adjust` fallback font setup (§9).
- **copy.ts** — clipboard listener: selection → `data-s/e` offsets → clean source text (strips `\n`, hyphen artifacts, comments).
- **observe.ts** — ResizeObserver + dppx `matchMedia` one-shots; forwards events to the worker.

### 4.3 Protocol (postMessage, transferables for buffers)

```
main → worker : init{wasmURL, fonts, baseURL}
                typeset{docId, source, opts}
                relayout{docId, width, dppx}
                mainMeasured{reqId, results}          (DOM backend only)
                dispose{docId}
worker → main : ready
                semantic{docId, html}                  (fallback phase, §9)
                paragraphs{docId, [{pid, html, rect, lineMap}]}   (typeset/upgrade)
                needMainMeasure{reqId, batch}          (DOM backend only)
                diagnostics{docId, items}
                fatal{docId, error}
```

The `semantic` → `paragraphs` sequence *is* the native-fallback state machine as seen from the DOM: inject flow HTML immediately, swap paragraphs as they arrive.

As built (plan P0-11, `runtime/src/worker/worker.mjs`): the worker keeps one **mailbox per docId** — messages for a document run strictly in order (an older update can no longer install its document over a newer one, and paginate's width round trip cannot interleave with a relayout). A newer `update` or `relayout` supersedes the running one of its kind: the running job checks its generation after every await and stops, queued jobs of the same kind coalesce, and superseded requests are answered with the newer result. The main-thread image-size fallback is an RPC with per-request ids (`image-dims?{rid, src}` → `image-dims{rid, w, h}`). Image sizes are resolved against the page's base URL, looked up in parallel, and read from the file header (PNG/GIF/WebP/JPEG with EXIF orientation) before falling back to a decode. A width change re-enters Layout in place (plan P1-16: emit records image size specs and sidecar flags, layout resolves them at the measure; the `host.width` patch applies to the live document); paginate forks the document (`tsr2_doc_fork` with a `host.width` / `page.height` patch), and every message carries one settings document (`settings`) instead of per-knob fields.

## 5. Build and generated artifacts

- **engine**: CMake presets `native-debug` (ASan/UBSan), `native-release`, `wasm-release` (emcmake). C++20, `-fno-exceptions -fno-rtti` (errors are diagnostics, not exceptions — WASM size and the §11 error-block model both want this). There is no parser generator: the front end is hand-written C++ driven by the generated syntax table (`docs/syntax-design.md`).
- **runtime**: esbuild (as in the PoC) → ESM bundle + the worker file; no framework.
- **tools** (Node, build-time only; outputs committed under `engine/gen/` so CI needs no network):
  - `mathdict.py` — `engine/data/math/symbols.tsv` + the pinned UCD and MathML Core operator dictionary → `engine/gen/math_dict.h`, `math/atom.h` (the math vocabulary, docs/math-design.md §3);
  - `hyphc` — TeX hyphenation patterns → compact trie;
  - `mathc.py` (python3 + fontTools) — `fonts/Euler-Math.otf` → `engine/gen/euler_math.h` (MATH constants, glyph records, vertical chains, assemblies);
  - `gen-schema` — `engine/schema/schema.json` → `ops.def`, `schema.gen.{h,cc}`, `shared/ops.gen.mjs`, `docs/schema-table.md` (§3);
  - `gen-syntax` — `engine/src/syntax/syntax.def` → `syntax.gen.{h,cc}`, `shared/syntax.gen.{mjs,json}`, `docs/syntax-table.md`.
- **playground**: esbuild dev server; the page is also the e2e harness target.

## 6. Testing architecture

Operational spec (fixtures, mock measurer, audit suite, Playwright matrix, CI jobs): **[testing.md](testing.md)**. The structural constraint it is built around: the ops boundary splits what can run natively — **execution requires a JS engine**, so —

- **Native golden tests** (fast, per-commit, ASan): `.tsm → skeleton → AST → generated JS` from sources; `ops fixtures → tree → resolve → blocks → breaks → layout → HTML` from **recorded `.ops` buffers**. A Node harness (`tools/record-fixtures`) regenerates the recordings from the same `.tsm` sources via the real wasm+executor; regeneration is a reviewed diff.
- **Cross-language contract tests**: opbuf writer (TS) and OpReader (C++) against the same binary fixtures; protocol version bump tests.
- **Runtime unit tests** (vitest): executor context, measurement cache/invalidation, fence registry semantics.
- **e2e (Playwright)** on the playground: screenshot regression; the three §16 invariant audits — every line element renders exactly one line box; right-edge deviation ≤ ε; anchors survive paragraph upgrades. The audit implementation ships in `runtime` (dev flag) and is imported by the tests — one implementation, two uses (§7 rule 5).
- **Fuzzing** (native, libFuzzer, cheap given the dual-target rule): linepass + inline parser on arbitrary bytes; OpReader on arbitrary buffers (must reject, never crash — it consumes JS-produced input).

## 7. Vertical slice and milestones

**M1 slice scope** (everything end-to-end, everything minimal): linepass = paragraphs + blank lines only; inline = text, `*`/`_`, `#name`/`#(expr)`/`#let`; codegen + executor + ops full (the contract does not shrink well — build it right once); model = paragraph/text/styled; no resolver; emit = Latin words + spaces, no hyphenation; measure = canvas backend only; break = PoC port; layout = single column, uniform leading; render = typeset serializer only; shell = worker + injection, no fallback, no copy. Exit: English paragraphs typeset in the playground at 1.0× and 1.25× dppx with zero spurious re-breaks (the audit passes) — the original PoC failure is the slice's acceptance test.

```
M0  scaffolding: repo layout, CMake presets + esbuild, CI (native tests, wasm build,
    e2e smoke), ops.def + gen-ops-ts + cross-language fixture test
M1  vertical slice (above)
M2  markup completeness: full line pass (lists, quotes, regions, comments),
    full splice grammar, hyphenation port, links, verbatim islands
M3  CJK: emission rules (App C), mixed justification k-rule, clreq conformance fixtures
M4  resolver: labels/refs/terms/collectors, diagnostics surfacing, section tree
    [DONE — shipped together with M6: engine/src/resolve/; trailing <id>
     labels, @id/@[…] sugar, auto heading anchors h-<number>, TOC/glossary
     collectors, term → group{role:term} rewrite]
M5  native fallback: semantic serializer, estimate states, paragraph upgrade
    protocol, copy handler, size-adjust polish
    [DONE except estimate states + size-adjust — engine/src/render/
     semantic_html.cc (both serializers share pids/anchors), worker posts
     semantic HTML pre-measurement, shell swaps keyed on data-pid with
     old/new-rect upgrade records, copy.mjs implements §9.3 (line joins
     fixed: data-join reflects consumed REAL spaces — CJK breaks join
     'none'), width-only relayout on the persistent worker-held doc.
     Deferred: pending(estimate) metric states + webfont settle re-typeset
     (worker-scope font loading), size-adjust fallback descriptors]
M6  extensibility: fence handler API, #!table + provenance queries, #use ergonomics
    [DONE except #use — pulled ahead of M5 for the M4 synergy (numbered,
     referenceable tables): #!name(args) regions (generic → group{role:name}),
     codegen-materialized '|' segmentation provenance, __region/__fence
     dispatchers, $.fence/$.region registration, raw passthrough units,
     #!table with equal columns + per-column alignment + three-line rules.
     Deferred: #use, cell-continuation indent rule; m.parse ENGINE half done
     (parseInlineFragment, verbatim-design §5) — JS handler surface still deferred]
M7  math: operator dictionary artifact, box model, Euler-Math metrics, inline boxes — DONE (mathc.py -> euler_math.h artifact; MathBox layout in su, zero measurement; $...$ islands; display/limits/stretch; equation labels+refs; 3-class inline breaks; woff2 subset). Deferred: cut-in kerning (no font data), horizontal stretch (wide accents), corpus math opt-ins
CH  code highlighting — DONE (build-time tree-sitter grammars as emcc side
    modules + NEED_TOKENS pull state; native tests statically link the same
    parse tables; decoration bits, structured code lines, ch-grid wrap +
    line numbers + hl rows; ops v3). See code-design.md.
F   figures: image kind + NEED_IMAGES pull + numbered captions/refs +
    float wrap (parshape) — DONE, see figure-design.md
M8  pages — DONE, see pages-design.md (W declared webfonts kill the M5
    settle deferral; P1 band pagination + print-to-PDF via the browser;
    P2 static export: tools/export-static.mjs, semantic page + hydration)
```

Order rationale: M1 buys the robustness contract (the project's reason to exist) at minimum surface; M2–M3 make it a real typesetter for the blog's actual content; resolver before fallback because fallback HTML must already carry resolved numbers (§11.1); math late — independent and metric-precomputed, it slots in without touching the core.

## 8. Conventions

- C++20, `-fno-exceptions -fno-rtti`; errors flow through the diagnostics sink or `Result`; asserts fatal in debug, stripped in release.
- Include layering follows the module map order; `include/tsr/` holds only the boundary headers.
- Every stage type has `dump()` (stable text form) — golden tests and `tsrc` share it.
- Generated files live in `gen/` (committed) or `build/` (never committed); no hand edits under `gen/`.
- TS strict mode; `shared/` may not import from `worker/` or `main/`.
- Naming: `tsr` (C++ namespace, C-ABI prefix), `.tsm` (markup sources), `typesetter.wasm` (artifact).
