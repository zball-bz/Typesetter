# Typesetter

A TeX-grade typesetting engine for the web. Paragraphs are set by
Knuth–Plass optimal line breaking with Liang hyphenation; Chinese text gets
GB/T 15834 punctuation compression, 禁则, and CJK–Latin spacing; math,
figures with float wrap, highlighted code, tables, and paged print output
are engine-native. The same `.tsm` source renders as a blog post, a live
editor preview, or a paginated PDF.

- **Engine**: C++ core compiled to WASM (and native for tests), measured
  through the browser's own canvas so layout matches paint exactly.
- **Runtime**: a module worker owns execution and measurement; the page
  only injects HTML. Progressive: semantic HTML paints first, the typeset
  result swaps in.
- **Markup**: `.tsm` — Markdown-shaped with Typst-like extensibility
  (`#!regions`, splices, `#let`, a JS execution stage).
- **Editor**: `editors/vscode-tsm` — highlighting, diagnostics, live
  incremental preview, jump-to-source, print.
- **Static export**: `renderTsm()` for SSGs; zball.io runs on Eleventy + this.

See `docs/architecture.md` for the stage map and `docs/*-design.md` for
each feature's design; `CLAUDE.md` has the build/test commands.

License: MIT.
