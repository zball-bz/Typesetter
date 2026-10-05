# Typesetter — working notes for Claude

TeX-grade web typesetting engine: a C++ core compiled to WASM (production)
and native (tests/tools), a JS runtime (worker + shell), the `.tsm` markup
language, a VSCode extension, and a static-export path used by the blog at
https://zball.io (repo zball-bz/zball-io consumes the rolling `engine-dist`
release).

## Layout

```
engine/src/        C++ core: api/ (Doc, wasm_api.cc) · ops/ (generated from
                   engine/schema/schema.json = single source of truth) · markup/inline/codegen · resolve/ · emit/ ·
                   break/ (Knuth–Plass) · layout/ · render/ (typeset + semantic +
                   paged serializers) · math/ · code/ (token fold)
engine/test/       native runner (unit + goldens); engine/gen/ generated tables
runtime/src/       worker/ (executor, canvas measure, tokens) · main/ (shell.mjs,
                   copy, audit) · node/render.mjs (renderTsm for SSGs) · shared/ops
grammar/           tree-sitter-tsm (highlighting grammar; vendored parser.c in
                   third_party/grammars/tsm)
editors/vscode-tsm VSCode extension (no build step; vendor/ via scripts/vendor.mjs)
tools/             serve, record-fixtures, corpus-run, codehl-assets, pack-dist,
                   export-static, bench-edit, hyphc, gen-schema/gen-all
test/              fixtures/ (+ .ops recordings) · golden/ · e2e/ (Playwright) ·
                   corpus/ (typst-derived smoke corpus)
docs/              architecture.md (stage map) + one *-design.md per feature
apps/playground    minimal engine host page
```

## Pipeline (docs/architecture.md)

markup → linepass → inline → codegen (JS) → executor (ops v5) → ingest →
resolver → emit → KP break → layout → render. Measurement is a **pull loop**:
`tsr_typeset()` returns NEED_MEASURE and the host answers word widths,
tokens, and image dims until it converges. Nothing in `engine/src` outside
`api/` may assume a browser.

## Build & test

```
cmake -S engine -B engine/build -G Ninja && cmake --build engine/build       # native
emcmake cmake -S engine -B engine/build-wasm && cmake --build engine/build-wasm
npm run test:engine      # ctest (goldens: ./engine/build/tsr_tests <repo> --update)
npm run corpus           # 199 typst corpus docs
npm run e2e              # Playwright audit matrix, dsf 1/1.25/1.5/2
npm run record           # re-record .ops after codegen/ops changes
npm run hl-assets        # regenerate runtime/assets/hl (gitignored)
npm run bench:edit       # editing-latency bench
tools/gate.sh --quick|--full   # remediation gates G1-G10 (docs/remediation/PLAN.md §4.3)
tools/fuzz.sh --smoke|--long   # libFuzzer targets; findings go to test/fuzz/<target>/
tools/bench.sh                 # perf gate table (update/relayout at 7.8K/35K/87K)
node tools/review-corpus.mjs --check   # real-world corpus: no new error diagnostics
node tools/lint-arch.mjs       # architecture lint (baseline only shrinks)
node tools/wasm-goldens.mjs --check    # WASM build breaks every fixture like the native goldens
```

Audit remediation in progress: follow `docs/remediation/PLAN.md` and resume
from `docs/remediation/PROGRESS.md`.

CI (`.github/workflows/ci.yml`) runs native (ASan Debug + Release), the web
job (wasm + recordings check + corpus + e2e), and on main publishes the
rolling `engine-dist` release.

## Conventions

- Every engine change lands with goldens/e2e green; re-record goldens only
  after inspecting the diff. Vocabulary lives in engine/schema/schema.json:
  edit it, run `node tools/gen-all.mjs`, never hand-edit ops.def or *.gen.*
  (schema.lock.json pins ids and since values). New vocabulary takes the next
  `since` (and raises opsVersion to it) — old buffers stay readable; only a
  change of meaning of an existing row raises minCompat and needs a full
  fixture re-record (`npm run record`).
- Design first: a `docs/<feature>-design.md` precedes implementation and is
  updated with as-built deltas.
- Commit per milestone; messages end with
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Playwright probe scripts must live in the repo root (node_modules); they
  are gitignored as `probe-*.mjs` — delete them when done.
- The blog consumes the dist by content hash: same bytes → same URL. After
  an engine change, re-vendor (`scripts/fetch-engine.mjs --local` in the blog
  repo) before deploying.
