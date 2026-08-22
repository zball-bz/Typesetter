# Testing Specification

Status: **draft for review**. Operationalizes [architecture.md](architecture.md) §6 against the [document-model](document-model.md) dump formats. Rendering is verified with **Playwright** — both screenshot regression and, more importantly, programmatic invariant audits that encode the v2 §7/§16 contracts.

---

## 0. Principles

1. **Determinism first.** Golden tests are byte-exact against `dump()` output; anything nondeterministic (real fonts, float platform variance, network) is excluded from golden paths by construction (mock measurer, `su` fixed-point, recorded fixtures, self-hosted fonts).
2. **Test at stage boundaries.** Each pipeline stage is tested from its input type to its dump — small blast radius, failures name the stage.
3. **One audit implementation.** The in-page invariant audits ship in the runtime (dev flag) and are the same code Playwright calls — a dev diagnostic and a CI assertion can never disagree (v2 §7 rule 5, §16).
4. **Every bug becomes a fixture.** Founding example: `fixtures/dpr/dpr125-nowrap.tsm` — the original PoC 1.25× spurious-rebreak bug is e2e acceptance for M1.

## 1. Fixtures

```
test/fixtures/
  <area>/<name>.tsm          source (area ∈ splice, line, cjk, ref, region, fence, math, dpr, doc)
  <area>/<name>.ops          recorded op buffer (regenerated, reviewed as a diff)
test/golden/
  <area>/<name>.<stage>.txt  expected dump per asserted stage
```

- **Recording workflow**: stages after codegen need JS execution, which native tests cannot do. `tools/record-fixtures` (Node) runs the real wasm + executor over every `.tsm` and rewrites `.ops` files. Regeneration is a normal PR diff — an unexpected `.ops` change is itself a signal. Native tests consume `.ops` directly; the seam (codegen → execution → ops) is covered continuously by e2e and by the contract tests below.
- **CJK conformance fixtures** are named after the clreq rule they exercise (`cjk/clreq-punct-compress-1.tsm` …) so coverage against the reference is greppable.
- **Adversarial fixtures** are first-class: `#avg的结果` (ASCII cut), `值是 #x. 下一句` (dot rule), `a %-- b` inside code (comment precedence), `#f("a|b")` in a table cell (tree-level split), nested comments, `\|`/`\*`/`\#` escapes, unclosed regions, regex-literal rejection.

## 2. Mock measurer (normative — implemented twice, identically)

Used by all deterministic tests in C++ (native) and the Node harness. With `em = fontSizePx` (fixtures use 16 so every value is su-exact):

```
width(cp):  space 0.25em · ASCII printable 0.5em · CJK (per engine classifier) 1.0em
            · else 0.6em ;  no bigram/kerning deltas
ascent 0.8em · descent 0.2em
```

A shared fixture asserts both implementations produce identical `.blocks` dumps for a probe document; drift fails CI.

## 3. Per-stage deterministic tests (native, ctest, ASan on debug preset)

| stage | from → to | method + invariants |
|---|---|---|
| linepass | `.tsm` → skeleton dump | goldens; property: node spans partition the byte range; islands round-trip verbatim |
| inline/splice | skeleton → AST dump | goldens; adversarial set above; every error path lands as `error` node + diagnostic, never abort |
| codegen | AST → JS text | goldens; **syntactic validity of emitted JS** checked in the Node job (parse via `new Function`; imports stripped) |
| ops reader | `.ops` → tree dump | goldens; validation-rejection tests (truncated, id-forward, bad kind, stack underflow) |
| model/instantiate | `.ops` → tree dump | style resolution unit tests; dedicated fixture: one DAG value emitted twice under different stacks → two styled instances (emission-time binding, v2 §12) |
| resolver | tree → tree dump | counter/label/collector goldens; `ref-unresolved` and `label-duplicate` fixtures assert diagnostics + "??" node |
| emit | tree → blocks dump | mock measurer; Appendix C rules one fixture each; hyphenation against a reference word list (en-US) |
| break | blocks → breaks dump | goldens (port PoC bench cases); properties: every line width ≤ measure + ε·nwords; INF-cost retry ladder exercised |
| layout | breaks → layout dump | goldens; invariants: no overlapping line rects, uniform grid unless an oversized box is present, k-rule arithmetic |
| render | tree/layout → HTML | goldens both serializers; escaping fixture (`<script>` in prose stays escaped; `raw` passes through); DOM-shape assertions (§9 of the model spec) via a tiny HTML parser |

## 4. Cross-language contract tests (Node, vitest)

- `ops.ts` writer ↔ C++ `OpReader`: shared binary fixtures checked in; a generative test builds random DAGs+schedules in TS, decodes with the native `tsrc --stage=tree --from-ops`, compares dumps.
- Protocol version: bumping `ops.def` without regenerating `ops.ts` fails a checksum test.
- Executor unit tests: context construction, `#use` import rebasing, fence registry (document-order, unknown-tag fallback, throwing handler → `fence-error` op sequence), shadow-node traversal/regroup (table cell split on the `#f("a|b")` fixture).
- Measurement cache: keying by (string × style × dppx), invalidation on dppx event, estimate → exact upgrade transitions.

## 5. Playwright e2e (the rendering authority)

### 5.1 Harness

`test/e2e/harness.html` — not the playground: a chrome-free page that loads the dev runtime, self-hosted fonts only (no network; CI runs offline), and exposes:

```js
window.__tsr = {
  typeset(source, opts),        // resolves when typeset (incl. upgrades) settles
  audit(),                      // runs the audit suite, returns a report object
  phase(),                      // 'semantic' | 'typeset' — fallback observation
}
```

Tests `await __tsr.typeset(fixture)` then assert on `audit()` and/or screenshot. Fixtures are the same `.tsm` files as §1.

### 5.2 Project matrix — dpr is a first-class axis

```
chromium  dsf 1.0 | 1.25 | 1.5 | 2.0     audits + screenshots (linux CI only)
firefox   default dsf                     audits only
webkit    default dsf                     audits only
```

`deviceScaleFactor` is set per browser context; the **1.25 project exists because of the founding bug** and runs the full fixture set. Fractional-dsf emulation is guaranteed on Chromium; on Firefox/WebKit the audit suite runs at native dsf (best-effort emulation is not relied upon).

### 5.3 Audit suite (in-page, one implementation)

| audit | method | asserts |
|---|---|---|
| line-integrity | per `.tsr-line`: `Range` over contents → `getClientRects()` grouped by `top` | exactly **1** fragment row — no browser re-break, ever (v2 §7.1) |
| right-edge | per justified line: last inline box right edge vs `left + width` | deviation ≤ 1px; report max/mean per document (tracked metric) |
| overflow | per `.tsr-para`: `scrollWidth ≤ clientWidth + 1` | no horizontal escape |
| anchors | `data-pid` unique; `data-s/e` monotone within a paragraph and inside its span | anchor system consistent |
| upgrade | run with fallback enabled: capture `phase()==='semantic'` DOM, await typeset, re-audit | same `data-pid` set; per-pid source range preserved; audits pass in both phases |
| copy | programmatic selection across a line break and across a hyphenated word → clipboard (Chromium, clipboard permissions granted) | clipboard text equals expected content text per model-spec §9.3 (no `\n`, no hyphen, `data-join` honored) |
| shift metric | `PerformanceObserver('layout-shift')` across fallback→typeset swap | recorded as a metric with a generous budget (warn, not fail, initially) |
| perf tripwire | `typeset()` wall time on `doc/long-10k.tsm` | generous threshold (fails only on order-of-magnitude regressions) |

### 5.4 Screenshot policy

- **Chromium on Linux CI only** — font rasterization differs per OS/engine; cross-platform screenshot goldens are a maintenance tar pit. Firefox/WebKit correctness is covered by audits, which are rasterization-independent.
- Pinned Playwright version (pins browser builds); `await document.fonts.ready` + typeset-settled before capture; `toHaveScreenshot({ maxDiffPixelRatio: 0.001 })`.
- Goldens are **per-dsf** (1.0 / 1.25 / 2.0): subpixel spacing legitimately differs across dsf.
- A `gallery.html` page renders all fixtures for human review — typographic *quality* (kerning feel, rag, spacing rhythm) is reviewed by eye there; screenshots only guard against *unintended change*.

## 6. Fuzzing (native, libFuzzer, nightly job)

- `fuzz_linepass`, `fuzz_inline`: arbitrary bytes → must terminate without crash/ASan report; corpus seeded from fixtures.
- `fuzz_opreader`: arbitrary buffers → must reject invalid input gracefully (it consumes JS-produced data; "trusted" does not extend to "well-formed").

## 7. CI pipeline

| job | runs | gates PR |
|---|---|---|
| native-debug (ASan/UBSan) | ctest: §3 + §6 smoke (1k iters) | yes |
| native-release | ctest + breaker perf bench (report only) | yes |
| wasm + node | wasm build; §4 contract tests; codegen-JS-parses; `record-fixtures --check` (fails if `.ops` stale) | yes |
| e2e-chromium | §5 audits + screenshots, dsf ×4 | yes |
| e2e-ff-webkit | §5 audits | yes |
| fuzz-nightly | §6, 30 min budget | no (files issues) |

Failure artifacts: stage dumps, screenshot diffs, audit reports (JSON), recorded traces (Playwright trace on retry).

## 8. Deliberately untested

- Cross-platform pixel identity (rasterization varies; audits carry correctness instead).
- Find-in-page across line boundaries — accepted degradation (v2 §8), documented, not asserted.
- Kerning/shaping quality — browser-owned; reviewed by eye via the gallery, not asserted.
