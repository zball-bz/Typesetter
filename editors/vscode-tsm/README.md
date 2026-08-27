# Typesetter (tsm) for VSCode

Language support and live typeset preview for `.tsm` — the
[Typesetter](https://github.com/zball-bz/Typesetter) markup language.

## Features

- **Live preview** (`TSM: Open Typeset Preview to the Side`): the full
  engine — Knuth–Plass justification, CJK punctuation compression, math,
  figures, code highlighting — re-typesets as you type. Incremental fast
  path: keystrokes patch only the damaged paragraphs.
- **Precise highlighting** via the same tree-sitter grammar the engine
  uses for `.tsm` code blocks (plus a coarse TextMate fallback).
- **Diagnostics** from the engine mapped onto source lines.
- **Jump to source**: double-click any line in the preview. Editor
  scrolling reveals the corresponding paragraph in the preview.
- **Outline / folding / completion** for headings, regions, references.
- **Print / PDF** through the engine's paged renderer
  (`TSM: Print / Export PDF from Preview`).

## Running from the repo

The extension serves engine assets straight from the checkout: build the
wasm engine (`emcmake cmake -S engine -B engine/build-wasm && cmake --build
engine/build-wasm`), then hit F5 on this folder (or
`code --extensionDevelopmentPath=editors/vscode-tsm`).

## Packaging

```
node scripts/vendor.mjs   # stages the engine dist under vendor/
npx @vscode/vsce package  # → vscode-tsm-<version>.vsix
code --install-extension vscode-tsm-*.vsix
```

## How the preview works

VSCode webview origins cannot host the runtime's module worker, so the
preview page is an iframe served by a loopback-only static server inside
the extension; `shell.mjs`/`worker.mjs`/wasm run verbatim there
(docs/editor-design.md §5). Messages relay through the webview:
source updates down, diagnostics/timings and jump requests up.
