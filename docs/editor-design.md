# Editor integration & incremental re-typeset

Goal: make local .tsm authoring feel native — a VSCode extension with live
typeset preview, and an engine/runtime fast path so a keystroke re-typesets
a long document well inside frame budget.

## 1. The measurement (bench-edit.mjs)

`node tools/bench-edit.mjs [--sections N] [--mode typeset|update]`
synthesizes a mixed CJK/Latin document with code blocks and math, then
simulates an editing session (mutate one paragraph → re-typeset). Baseline
(pre-incremental, fresh doc per edit, Chromium, this machine):

| doc size | median edit latency |
|---|---|
| 7.8K chars | 11.8 ms |
| 35K chars | 42.0 ms |
| 87K chars | 98.7 ms |

Breakdown showed three cost centers: full innerHTML swap on the main thread
(~45 ms at 87K — the largest), wasm emit+KP+layout (~30 ms), and the
provider round trips. Compile/execute/ingest are cheap (<6 ms combined).

## 2. Worker fast path (`update`)

- **Persistent measurer** (canvas_measure.mjs): word widths and vmets are
  memoized per font across documents. Invalidated when a new FontFace lands
  (widths measured against a fallback are stale). Token results
  (tree-sitter) are cached per (lang, body) the same way (worker.mjs).
- **Session update** (worker `update` message, shell `handle.update(src)`):
  re-typesets new source under the same doc handle. The new doc replaces
  the old only on success — a failing edit keeps the last good document
  alive for relayout/paginate.
- **Cross-document KP cache** (break.cc `breakLinesRetry`): the DP reads
  only block geometry (width, spaceWidth, breakWidth, breakPenalty) plus
  LineWidths and CostParams, so results are keyed by an FNV-1a content hash
  and shared process-wide. An edit re-breaks only the paragraphs it
  actually changed. The retry ladder is folded into the cached computation.
  Any new field the DP starts reading MUST be added to `breakKey`.
- Phase timings ride on every result message (`timings`), so the bench and
  the preview can attribute latency without instrumented builds.

## 3. Per-paragraph DOM patch

The typeset serializer emits one normal-flow container per paragraph
(`.tsr-para`), lines absolutely positioned inside. `shell.mjs patchIn`
chunks the old and new HTML strings at paragraph boundaries, matches
common prefix/suffix, and replaces only the differing middle — a
one-paragraph edit touches one DOM node and the tail shifts by normal
flow. Full `swapIn` remains the fallback (structure mismatch, external DOM
mutation).

For that to work the serialized form of untouched paragraphs must be
byte-identical across edits, so **source anchors are paragraph-relative on
screen**: `data-s`/`data-e` on lines/runs/formulas are offsets from the
paragraph's `data-s0` base. Absolute offset = s0 + s. The paged serializer
keeps absolute offsets (base 0). Nothing in the runtime consumed absolute
`data-s` before this change (they are anchors, never copy content).

Known limitation: inserting/deleting a paragraph shifts `data-pid` of the
tail, so those chunks all differ and the tail is replaced wholesale. Fine
for the dominant intra-paragraph keystroke; revisit with content-keyed
pids if it ever shows up in profiles.

## 4. Results

| doc size | before | after | |
|---|---|---|---|
| 35K chars | 42 ms | **13 ms** | median |
| 87K chars | 99 ms | **30 ms** | median |

Residual at 87K: full-document render string (~7 ms), emit inside the
engine pass (~7 ms), execute (~4 ms). Next lever if ever needed: per-unit
render caching keyed like the KP cache.

## 5. VSCode extension (editors/vscode-tsm)

Desktop-first. Pieces:

- **Language basics**: `tsm` language id, minimal TextMate grammar for
  first-paint coarse coloring; precise highlighting via a semantic tokens
  provider running web-tree-sitter + grammar/tree-sitter-tsm in the
  extension host (Node), mapping highlights.scm captures to VSCode token
  types. One grammar, two consumers (engine folding + editor).
- **Preview**: a webview hosting an iframe served by an in-extension
  localhost static server (the runtime's module worker + wasm work
  verbatim there; VSCode webview origins cannot host module workers from
  extension resources). Debounced `handle.update()` per document change;
  generation counter drops stale results; progressive semantic paint only
  on first open.
- **Diagnostics**: engine diags carry byte spans; the preview posts them
  back and the extension maps them to `vscode.Diagnostic`.
- **Scroll sync / click-to-source**: `data-s0 + data-s` anchors give
  line-level source mapping both ways.
- **Outline/folding/completion**: from the tree-sitter parse in the host.
