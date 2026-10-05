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
