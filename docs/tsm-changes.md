# .tsm author-facing changes

This log lists every change that an author of `.tsm` documents can observe:
new syntax, changed meaning of existing syntax, new diagnostics and changed
defaults. Each entry names the remediation plan step
(`docs/remediation/PLAN.md`) that introduced it and, where needed, how to
migrate an existing document.

Format: `- [step] what changed — migration (if any)`.

## Unreleased (remediation/audit-2026-10)

- [P0-04] A `$` that is not closed within its own block (paragraph, heading, cell, link text) is literal text. It no longer takes a closing `$` from a later block, which used to swallow and duplicate the blocks in between.
- [P0-04] An inline `%--` comment that is not closed within its block is literal text, with an error diagnostic, instead of hiding the rest of the document.
- [P0-04] An unbalanced `#let` or `#{` statement reports `statement-unclosed` and is dropped up to the next blank line or the next block start (heading, list item, quote, fence, region, statement). It no longer swallows the rest of the document (`#let`) or pastes broken JavaScript that failed the whole document (`#{`). A `#{` used to recover at the end of its line.
- [P0-04] CRLF line endings read exactly like LF: fence bodies, comments and multi-line formulas no longer carry `\r`, and lines ending in `\r\n` join paragraphs as LF lines do.
- [P0-04] Inside regions, a `|` within `$…$` or a code span no longer splits table cells: `$|x|$` is one formula.
- [P0-05] A runtime error in a block (a ReferenceError in a splice, a throwing `#{…}`) no longer fails the whole document. The block renders as an error block (`script-error`) and the other blocks render normally. A JavaScript syntax error in a statement or splice is isolated to its block (`script-syntax`).
- [P0-05] `#let list = 1` (or any other constructor name) is legal: it shadows the name for your code, while `- item` lists and other markup keep working. Names starting with `__` are reserved (`reserved-name`).
- [P0-05] Repeating `#let x = …` reassigns `x` (it used to fail the whole document). Closures see the latest value.
- [P0-05] `#if`, `#for`, `#while`, `#use`, `#else` and other JavaScript keywords used as a splice head become error blocks (`keyword-unsupported`, `reserved-word`) until the keyword forms land. Before, they failed the whole document.
- [P0-05] Region and fence arguments must be named (`#!table(cols: 3)`, ```` ```js(lineNo: 1) ````). A positional list such as `#!table(3)` is an error block (`header-positional`) instead of failing the document.
- [P0-05] `#let` and `#{…}` inside a list item, quote or region show an error block (`statement-nested-unsupported`) instead of silently disappearing.
- [P0-05] A throwing `$.region` handler becomes an error block (`region-error`), as a fence handler already did.
- [P0-06] Style values are validated. Accepted values:
  - `color`: `#hex`, a color name, `rgb()`/`hsl()`, or `var(--name)`;
  - `font`: a comma-separated family list;
  - `lang`: a BCP-47 tag;
  - `sizePx`: 1–2000.

  Anything else (for example `"red;letter-spacing:5px"`) is dropped with an `ops-arg` warning, so a value can no longer inject CSS. Other numeric arguments are clamped into their range: heading `level` is 1–6, table `cols` is 1–64.
