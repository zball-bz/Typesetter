# Typesetter Project Plan

## Status

This document is the proposed plan for moving the repository from a proof of concept into a real engine. It is intended for discussion, not as a frozen spec.

Current assumptions:

- The current codebase becomes a reference-only PoC.
- The current paragraph breaker stays as the first production breaker.
- Hyphenation will be reimplemented in-house.
- Math rendering is part of the target product, not an optional add-on.
- Intermediate-stage visualization and debugging are first-class requirements.
- A future local backend or native/WASM core is possible, but should not distort early architecture unnecessarily.

## Goals

- Build a real paragraph and inline layout engine for web-first publishing.
- Support Latin, CJK, and mixed-script text with explicit control over typography.
- Support a custom markup language designed for authoring, not just demo input.
- Support custom math rendering with AMS Euler and simpler syntax than LaTeX.
- Keep the engine debuggable at every stage of the pipeline.
- Keep the architecture compatible with later native or WASM acceleration.

## Non-Goals For The First Real Iteration

- Full TeX compatibility.
- Full Markdown compatibility at the syntax edge cases level.
- Immediate C++ rewrite.
- Solving all block layout problems before paragraph and inline layout are stable.

## High-Level Principles

1. Separate authoring, layout, and rendering.
2. Keep the core engine parser-agnostic and renderer-agnostic.
3. Use explicit intermediate representations between stages.
4. Make every stage inspectable and serializable.
5. Optimize only after the data model and boundaries are stable.
6. Keep the door open for native/WASM by using compact, data-oriented runtime models in hot paths.

## Proposed Repository Structure

```text
poc/
  browser-demo/
    src/
    index.html
    magazine.html
docs/
  project-plan.md
  architecture.md
  pipeline.md
  markup.md
  math.md
  hyphenation.md
  debug.md
  native.md
engine/
  src/
    model/
    style/
    markup/
    math/
    hyphen/
    emit/
    break/
    measure/
    render/
    debug/
  tests/
  bench/
apps/
  playground/
  inspector/
tools/
  pattern-compiler/
  fixtures/
native/
  cpp/
```

Notes:

- `poc/` is frozen reference code. New architecture work should not happen there.
- `engine/` is the real product.
- `apps/playground` is the demo surface.
- `apps/inspector` is the stage-by-stage debugger and visualizer.
- `native/` can remain mostly empty until there is profiler evidence for it.

## Main Workstreams

### 1. Core Data Model

We need one canonical set of models for the new engine.

Key decisions:

- Keep rich syntax and semantic objects in early stages.
- Use compact numeric IDs in hot layout stages.
- Do not carry large object graphs and `Set<number>` values through the final hot path.

Target idea:

- Semantic stages may use expressive objects.
- Layout stages should use tables plus IDs:
  - `styleId`
  - `textId`
  - `nodeId`
  - `blockId`

This makes caching, serialization, debugging, and native interop easier.

### 2. Markup Language

The markup language is its own product decision, not just parser implementation detail.

The parser pipeline should be:

1. Source text
2. Concrete syntax tree with source spans
3. Normalized document AST
4. Style/directive resolution

Requirements:

- Good authoring ergonomics for prose.
- Explicit support for inline directives and block directives.
- First-class math syntax.
- Deterministic parsing rules.
- Good diagnostics with source spans.
- Extensibility without making the grammar incoherent.

Important design choice:

- Decide whether the surface syntax is "Markdown-like with custom extensions" or a cleaner custom language with selective Markdown compatibility.

Recommendation:

- Aim for Markdown-like familiarity at the surface level.
- Keep the internal AST strict and typed.
- Do not let Markdown edge-case compatibility dominate architecture.

### 3. Math Rendering Engine

Math is a core subsystem.

Minimum plan:

- Define a math syntax and grammar.
- Parse math into a dedicated `MathAST`.
- Convert `MathAST` into a `MathBoxTree`.
- Render math as inline or display layout through a dedicated math layout engine.

Required design choices:

- Inline math syntax and display math syntax.
- Operator dictionary and precedence rules.
- Font metrics source for AMS Euler.
- Stretchy delimiters now or later.
- Whether inline math is an opaque inline box in paragraph layout or can expose internal break opportunities.

Recommendation:

- Start with inline math as an opaque inline box.
- Treat display math as separate block layout.
- Delay internal line-breaking inside complex math until the core box model is stable.

### 4. Hyphenation

Hyphenation should become an owned subsystem.

Plan:

- Build a pattern compiler in `tools/pattern-compiler/`.
- Compile TeX pattern sources into a compact runtime format.
- Implement a runtime hyphenator with:
  - exceptions
  - left and right minima
  - soft hyphen handling
  - caching
  - debug trace output

Recommendation:

- English first.
- Design language packs as pluggable units.
- Keep the runtime independent from DOM and browser globals.

### 5. Emission And Paragraph Layout

The paragraph breaker should work on a stable block stream interface.

Planned flow:

1. Normalized styled AST
2. Script segmentation and inline flow resolution
3. Token or run emission
4. Hyphenation and special-case expansion
5. `LinebreakBlock[]`
6. Breaker result

Important requirement:

- Latin, CJK, punctuation, boundaries, links, code, emphasis, math, and future inline constructs must all converge into one main emission path.

Recommendation:

- Keep one `ParagraphBreaker` interface.
- Treat the current breaker as the first implementation, not the forever architecture.
- Design emission so that future breakers can consume the same block stream.

### 6. Rendering

Rendering should consume a render-oriented tree, not raw parser nodes or raw blocks.

Planned flow:

1. Break result plus content references
2. Render tree or span tree
3. DOM or HTML adapter

Requirements:

- Efficient span merging.
- Style inheritance.
- Word spacing and letter spacing control.
- Hooks for debug overlays.

Recommendation:

- Build a dedicated render tree layer.
- Keep the browser DOM renderer as an adapter.

### 7. Intermediate Visualization And Debugging

Every stage should be inspectable.

The engine should be able to emit serializable debug artifacts for:

- CST
- normalized AST
- style resolution
- script segmentation
- hyphenation decisions
- emitted blocks with widths and penalties
- paragraph-break decisions
- render tree
- final DOM mapping
- math box tree

`apps/inspector` should load those artifacts and visualize them.

Possible inspector views:

- tree views
- token streams
- side-by-side source and AST mapping
- color overlays for script and style runs
- breakpoint graph and line slack display
- math box diagrams
- performance timeline for each stage

This should be treated as core infrastructure, not debugging garnish.

## Core Pipeline Proposal

```text
SourceDocument
  -> MarkupCST
  -> DocAST
  -> StyledDocAST
  -> InlineFlow
  -> MathBoxTree injection
  -> LinebreakBlock[]
  -> BreakResult
  -> RenderTree
  -> DOM/HTML/native output
```

Each transition should have:

- explicit input type
- explicit output type
- optional debug snapshot
- no hidden DOM dependency

## Performance Strategy

Performance work should be staged.

### Phase A: Make It Correct And Inspectable

- Stable interfaces
- serializable intermediate models
- golden tests
- debug artifacts

### Phase B: Remove Obvious Waste

- measurement caching
- hyphenation caching
- block emission caching
- layout result caching by width and style context
- avoid repeated style-object cloning in hot paths

### Phase C: Data-Oriented Optimization

- replace object-heavy hot structures with compact arrays and IDs
- pooled arrays or typed arrays where justified
- reduce per-block allocation overhead
- isolate hot loops

### Phase D: Native/WASM Evaluation

Only after profiling:

- paragraph breaker
- hyphenation runtime
- math layout
- any future shaping-heavy work

## Local Backend And Native/WASM Considerations

Possible backend roles:

- static precomputation for blog builds
- regression test runner
- benchmark runner
- document snapshot generator for the inspector
- optional pre-typesetting service

Native/WASM is possible, including a future C++ plus Emscripten path, but should be treated as a compatibility target, not the initial implementation language.

Recommendation:

- Keep the first real engine in TypeScript.
- Make the hot runtime models FFI-friendly.
- Keep the native boundary coarse-grained:
  - paragraph in
  - layout result out

This keeps the option open without forcing the entire architecture into native-first constraints too early.

## Migration Plan

### Milestone 0: Freeze And Move The PoC

- Move current repo contents into `poc/browser-demo/`.
- Keep it runnable as a reference.
- Stop adding new architecture work there.

### Milestone 1: Create The New Skeleton

- Add `docs/`, `engine/`, `apps/`, `tools/`, `native/`.
- Write the architecture and pipeline docs.
- Define the new core interfaces.

### Milestone 2: Stabilize Core Models

- Replace the dual type-system direction with one agreed model.
- Define stage boundaries and IDs.
- Define debug artifact formats.

### Milestone 3: Port Existing Layout Core

- Port the current breaker into `engine/break/`.
- Port measurement behind interfaces.
- Recreate Latin-only parity in the new playground.

### Milestone 4: Own Hyphenation

- Build the pattern compiler.
- build the runtime hyphenator
- replace external runtime dependency in the engine path

### Milestone 5: Build Real Emission

- mixed Latin and CJK emission
- punctuation rules
- inline object handling
- math placeholders or boxes

### Milestone 6: Build Render Tree And DOM Adapter

- span-tree generation
- browser renderer
- debug overlays

### Milestone 7: Replace Demo Parser

- real markup parser
- directives
- diagnostics
- source mapping

### Milestone 8: Add Math Rendering

- math parser
- math layout
- inline and display math integration

### Milestone 9: Optimize And Evaluate Native Path

- benchmark real corpora
- optimize hot paths
- decide whether any subsystem should move to native or WASM

## Testing Strategy

We need more than unit tests.

Required test categories:

- parser fixtures
- hyphenation fixtures
- emission fixtures
- breakpoint golden tests
- render-tree golden tests
- visual regression tests
- benchmark corpus tests

Inspector snapshots should become reusable test fixtures.

## Open Questions For Discussion

1. How far should markup compatibility with Markdown go?
2. What is the real scope of the first math release?
3. What parts of CJK behavior belong in parsing, normalization, or emission?
4. Should style state stay class-oriented, property-oriented, or hybrid?
5. How compact should runtime layout models be from day one?
6. Should the backend be only build-time tooling, or a permanent runtime service?
7. What debug artifacts should be stable external formats versus internal-only traces?
8. When do we consider native/WASM justified?

## Immediate Next Steps

1. Review this document and settle the main architectural choices.
2. Move the current implementation under `poc/`.
3. Write `docs/pipeline.md` and `docs/markup.md`.
4. Create the new engine skeleton and interface definitions.
5. Stand up `apps/inspector` early so new subsystems are built with observability from the start.
