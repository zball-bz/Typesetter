# Footnotes & citations — design

Status: footnotes IMPLEMENTED (screen; print inserts pending), citations
DESIGN. As-built deltas for footnotes are listed at the end of §1.
Builds on design-decisions-v2 §11.1 and
document-model §counters/collectors: *execution declares, the resolver
decides*. Both features are reference-shaped — they reuse the label table,
the counter automata, and the collector mechanism that already number
sections, figures and equations.

## 1. Footnotes

### Syntax

```
正文里的一个断言^[脚注正文，支持完整行内标记与 $x^2$。]继续。
```

`^[…]` is an inline note: the body is inline content (markup, math, code
spans, links), no block content — a note that needs paragraphs is an
endnote section, not a footnote. Named form for reuse / long bodies:

```
断言^[note-a]。            …later…
#note(note-a)[脚注正文写在这里，离开正文行。]
```

### Pipeline

- **codegen/ops**: new Kind `note` (inline, kids = body). No new ARGK: the
  optional name rides `label`. `#note(name)[…]` is a definition site whose
  body attaches to the first `^[name]` marker (resolver joins by label; a
  marker with no definition → `note-undefined` diag, rendered as `?`).
- **resolver pass 1**: counter class `footnote` (reset rule from config:
  `none | section`, default none for blog articles). Each note gets an
  ordinal in document order and an auto-label `fn-<n>`; the marker node
  gets `ArgK::number` = "n". Notes are also entries in the label table so
  `@fn-3` works like any reference.
- **emit**: the marker becomes a superscript inline block — a Latin-class
  block with the number text, style flag `sup`, `breakPenalty=INF` glued
  to the preceding block (never a line start; CJK 禁则 treats it as a
  closing punct). Note *bodies* are lifted out of the paragraph into a new
  FlowUnit kind `Note` appended to the document's note list (not to the
  flow). Each body is a TableCell-like block stream broken to the measure
  minus a hanging indent for the number.
- **layout / render, screen**: notes render as an end-of-document section
  (`.tsr-notes`, a rule + numbered hanging-indent paragraphs) with a
  back-link `↩` to the marker's anchor; markers link to the note. This is
  the collector `collect{what: notes}` expanding implicitly at document
  end when absent — authors can place `#notes()` explicitly (e.g. before
  the bibliography). Hovering a marker shows the note body (title attr /
  small popover in the shell — shell concern, not engine).
- **paged render (print)**: TeX's insert problem. renderPages already
  cuts bands greedily into sheets; the note pass extends the cost model:
  when a band containing marker *k* is placed on a sheet, that sheet's
  available height shrinks by the height of note *k* (+ a one-time
  separator rule). If the band no longer fits, the band moves to the next
  sheet together with its notes (TeX's behavior); a note taller than a
  page splits by lines with a continuation mark. Notes always sit at the
  sheet bottom, below the last band, above the margin. Widow/orphan and
  keep-rules stay as they are.

### Measurement

Marker superscripts are measured like any word at the `sup` style (0.7em,
raised 0.35em via vertical-align in the renderer; the line box grows by
nothing — the leading absorbs it, as in TeX). Note bodies use the body
style at 0.85em.

### Numbering display

`cfg.supNote` = "" (bare digits) by default; CJK books sometimes prefer
circled digits ①②③ — a config switch (`noteMarks: digits | circled`)
selects the glyph set at emit time; the counter is unchanged.

### As built (2026-08)

- Ops v6 adds inline `Kind::note` (`^[…]` → `note(...)` ctor); the named
  `#note(name)[…]` form is NOT implemented — inline bodies only.
- Resolver: counter + labels `fn-n` (the body item) and `fnref-n` (the
  marker); the note node is replaced by a `ref` to `fn-n` styled
  `CLS_SUP × 0.7`, so `@fn-n` from prose renders the same digit. Bodies
  are lifted into `group{role:notes}` = rule + ordered list, items at
  0.85× with a `↩` ref back to the marker; built at `#notes()` or appended
  by `resolveDoc`. Refs inside note bodies resolve normally.
- Emit: labelled refs set `LinebreakBlock.anchorId` (run gets `id=`);
  a superscript ref forces `breakPenalty = INF` on the block before it, so
  a marker never starts a line. No CJK–Latin boundary is inserted before
  it (cross-node boundaries were never inserted; the digit hugs the text).
  Known nit: after a closing punct the marker follows the punct's
  trailing half-space rather than hugging the glyph.
- Render: `.tsr-sup` (paint-only raise via `position: relative`),
  semantic `<sup>` + `<ol>`; paragraphs honor `label` as anchors.
- Highlighting: `footnote` token in tree-sitter-tsm (`@attribute`).
- Print: notes currently print as endnotes (the section is ordinary flow);
  bottom-of-sheet inserts remain as designed above.

## 2. Citations

### Syntax

```
#bibliography("refs.json")          % anywhere; usually near the end
如 @knuth84 所述……                    % same @ namespace as labels
@[knuth84, liang83]                  % grouped cite → [1, 2]
```

`refs.json` is CSL-JSON (the de-facto interchange format Zotero/Pandoc
emit) — no BibTeX parser in the engine. Keys are the CSL `id`.

### Pipeline

- **bibliography data** is a *resource*, like images and tokens: a fourth
  pull request (`NEED_BIB`) — the worker fetches the JSON (relative to the
  document), the Node renderer reads it from disk; 0 entries = failure
  diag. The engine receives the parsed entries as an ops-like flat list
  (id, type, fields) so the resolver never parses JSON.
- **resolver**: `@key` resolves against labels first, then bib ids
  (`ref-unresolved` stays the diag when neither matches). Cited keys get
  ordinals in *first-citation order*; the reference node renders as
  `[n]` (numeric style, default) linking to the entry anchor.
- **rendering style is a JS function**, not engine code: the executor
  runs a user-overridable `formatEntry(entry) → inline markup` (default:
  numeric, author – title – container – year) at *collect* time; the
  engine sees only markup. CSL processors are deliberately out of scope
  (design-decisions-v2 §11.1) — a user who needs APA/Chicago writes the
  function or pre-renders with citeproc.
- **collector**: `#bibliography` also *is* the collector: it expands into
  a `.tsr-bib` section — numbered hanging-indent entries in citation
  order, each with its anchor. Uncited entries are omitted unless
  `all: true`.
- **paged render**: nothing new — the bibliography is ordinary flow.

## 3. What is shared

| concern | footnotes | citations |
|---|---|---|
| declaration | `^[…]` / `#note()` | `@key` / `#bibliography()` |
| counter | `footnote` class, doc order | first-citation order |
| label table | `fn-n` entries | bib ids |
| collector | `notes` (implicit at end) | `bibliography` (explicit) |
| pull loop | — | `NEED_BIB` |
| print pass | bottom-of-sheet inserts | — |

## 4. Order of work

1. Footnote marker + end-of-document notes (screen): ops kind, resolver
   counter, emit superscript, notes section, e2e audit for the marker's
   glue rule. One fixture per: basic, named/define, in-heading (diag),
   nested markup/math in body.
2. Print inserts in renderPages.
3. `NEED_BIB` + numeric citations + bibliography collector.
4. Circled marks / per-section reset / grouped-cite ranges ([1–3]) as
   polish.

Estimated: (1) one day incl. goldens; (2) half a day; (3) one day — the
resource pull is the same shape as images and tokens.
