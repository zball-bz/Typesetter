# Real-world corpus

Published documents converted to `.tsm` to see how the engine behaves on
text it was not written for. Findings: `docs/real-world-report.md`.

| file | source | license |
|---|---|---|
| `wiki-typesetting.tsm` | en.wikipedia.org/wiki/Typesetting | CC BY-SA 4.0 |
| `wiki-huozi.tsm` | zh.wikipedia.org/wiki/活字印刷术 | CC BY-SA 4.0 |
| `hott-introduction.tsm` + `hott-refs.json` | github.com/HoTT/book (introduction.tex, references.bib) | CC BY-SA 4.0 |
| `pbr-1-1.tsm`, `pbr-1-2.tsm` (not committed) | pbr-book.org/4ed/Introduction | © Pharr, Jakob, Humphreys — regenerate locally |

Regenerate everything: `node examples/real-world/fetch.mjs` (the converters
live in `tools/convert/`). Render any file in the browser through
`test/e2e/harness.html` or the VSCode preview; the figures reference the
originals' image URLs.
