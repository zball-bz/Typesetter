# Footnotes & citations — design

Status: footnotes and citations IMPLEMENTED (screen; footnote print
inserts as built in plan P3-13, opt-in: a deferred flow, §1). As-built
deltas are listed at the end of §1 and §2.
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
- **emit**: the marker is a superscript run (its role style, `fn-marker`:
  raised, smaller, no CJK–Latin spacing) attached to what precedes it —
  `attach: prev` (plan P4-07; shaping-design §11): never a line start, the
  typed space before it not set (`word ^[n]. end` reads word¹. end), a
  punctuation glyph's trailing blank moved after it (`。¹`). Note *bodies* are lifted out of the paragraph into a new
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
  As built (plans P3-12, P3-13): there is no `renderPages` — pagination is
  `engine/src/layout/paginate.cc` over the finished layout (layout-design
  §6) and the paged product is `Doc::renderPaged` (api/doc.h). A deferred
  flow's entries are inserts (`kPagedInsert`, `insertAt` = the marker's
  source position): the box holding the marker takes their height, plus
  once per sheet the separator (the flow's rule, repeated on every sheet
  with notes; else `PageSpec.footnoteSkip`, an em), and moves to the next
  sheet with them when it no longer fits. An insert is never split and has
  no continuation mark (a PROGRESS P3-13 deviation): a note taller than a
  sheet overflows it visibly (`page-overflow`).

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
  `#note(name)[…]` form is NOT implemented — inline bodies only. *(As
  built, plan P3-13: named notes exist — `#note({label})[…]` defines one,
  `#ref(label, {form: "marker"})` places its marker again; see below.)*
- Resolver: counter + labels `fn-n` (the body item) and `fnref-n` (the
  marker); the note node is replaced by a `ref` to `fn-n` styled
  `CLS_SUP × 0.7`, so `@fn-n` from prose renders the same digit. Bodies
  are lifted into `group{role:notes}` = rule + ordered list, items at
  0.85× with a `↩` ref back to the marker; built at `#notes()` or appended
  by `resolveDoc`. Refs inside note bodies resolve normally.
- Emit: labelled refs anchor their first item (`IA_Anchor`: the run gets
  `id=`); the marker is attached to what precedes it (`attach: prev`, plan
  P4-07), so it never starts a line, and its role style has no CJK–Latin
  spacing (the digit hugs the text).
  Known nit: after a closing punct the marker follows the punct's
  trailing half-space rather than hugging the glyph.
- Render: `.tsr-sup` (paint-only raise via `position: relative`),
  semantic `<sup>` + `<ol>`; paragraphs honor `label` as anchors.
- Highlighting: `footnote` token in tree-sitter-tsm (`@attribute`).
- Print: notes print as endnotes unless their flow is deferred (plan P3-13,
  below), which makes them bottom-of-sheet inserts.
- Copy (as built, plan P3-07, D-R01): the marker and the `↩` backlink are
  decorative generated text — the footnote row's marker template and the
  notes collector's backlink carry `syn: "fn-marker"` / `syn: "backlink"`
  with `copy: "omit"`, so both pages mark them `data-syn` and copy leaves
  them out; a note's body and a `@fn-n` reference copy as text.
- Hover popup (as built, plan P3-06): the shell's `refPreview` behaviour
  (architecture §4.2). The footnote row declares `preview: "block"`, so the
  RenderResult's anchors table marks `fn-n` (not the marker's `fnref-n`)
  as previewing; a link to it — the marker or an `@fn-n` reference — shows
  the engine's fragment of the note (`tsr2_render_fragment`: the item's
  semantic HTML with its markup, no ids, no `↩`). The popup no longer
  scrapes the typeset lines, so hyphen glyphs and line joins (spaces
  between CJK lines) never reach it.

As built (plan P3-13, semantics-design §9):
- Named notes: `#note({label: "src"})[…]` is a footnote named `src`, and
  `ref("src", {form: "marker"})` marks it again. That gives one entry with
  several markers, whose back-links read `↩ a b c`. The `^[src]` sugar
  belongs to T1's surface and is not built.
- Per-chapter notes: `$.counter('footnote', {within: {counter: 'heading',
  depth: 1, prefix: false}, numbering: '①'})` restarts the circled numbers
  per chapter. Anchors use the ordinal (`fn-<n>`), so they stay unique.
- `flow: {placement: 'section-end', depth: 1}` closes each chapter with its
  notes.
- A user endnote flow is a class like `footnote` with its own flow,
  together with a collector `{like: 'notes', flow, scope: 'section'}`.
- Print: `flow: {placement: 'deferred'}` puts the notes at the foot of
  their marker's sheet, below the rule, which repeats on every sheet with
  notes. On screen the notes still close the document. The built-in row
  keeps `end`, so documents opt in, and existing output is unchanged.

As built (plan P2-08): the marker is `ref{role: fn-marker, attach: prev}` in
a super-baseline ×0.7 delta (the footnote row's marker template); `attach:
prev` is what keeps it on its word's line (the CLS_SUP bit retired), and
any inline node can say so.

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

As built (plan P2-09): a group citation `@[a, b, c]` is a parent reference
with child references and renders through the bibliography's `cite`
template; three or more consecutive ordinals compress ([1–3]); the bracket of
`@kp81[p. 5]` is the locator ([1, p. 5]), also after a group
(`@[a, b][ch. 2]` → [2, 3, ch. 2]).

### As built (2026-08)

The pull-resource design (`NEED_BIB`) was NOT used. The executor already
runs async JS with host access, so the data loads there: `#bibliography(
src, {all})` records a request and returns an empty placeholder; after the
document program ran, the executor loads the CSL-JSON (browser/worker:
fetch against the page's base URL; Node: file, `/`-paths against
`rootDir`, relative against `baseDir` — `renderTsm(src, {baseDir,
rootDir})`), formats each entry with `$.bib.format` (default
`formatEntryDefault`: numeric, author–title–container–publisher–year–
doi/url) into inline shadows, and emits `collect{what: bibliography}` with
`group{role: bibentry, name: key}` kids at document END. Formatting stays
a JS function; the engine never parses JSON or knows CSL.

Resolver: `@key` falls back from the label table to the bib table;
`@[k1, k2]` splits on commas; ordinals are first-citation order; the ref
renders `[n]` with each number linking to `bib-<key>`; the collector
rebuilds as `group{role: bibliography}` = rule + one anchored paragraph
per cited key in citation order (`{all: true}` appends the uncited). The
placeholder's empty paragraph is dropped by the rewrite pass. The
bibliography position is always document end in this version.

As built (plan P2-07): `#bibliography(src, {cited})` returns its collector
**in place** — `collect{what: bibliography, cited?}` at the call site, with
the call's span (`cited: 'cited-then-all'`, or the old `{all: true}`,
appends the uncited rows in data order; the default is the collector row's
`cited`). After the program the executor loads each source once and emits
one `entry{role: bibentry, key}` per CSL item as a trailing root with an
empty span. Those entries are instances of the `bibentry` element class
(`labels: none`, `table: bib`, `row-key: key`: a citation key is a row key,
never a label), LOCATE makes them the `bib` table's rows (the first entry
of a key wins), and MATERIALIZE drops them where they stand; the
collector lists the table's rows. Two bibliographies list the same table,
each where it is written (the first owns the row anchors).

As built (plan P2-14; design T2 S11): the bibliography is made **at its
call**. `bibliography(src, …)` is an async constructor (schema
`"async": true`): it loads the source through the executor's resource
loader (the same as `ctx.load`: below `rootDir` or the document's folder,
P0-11), formats each item with the `bib` format entry — each in its own
frame: a throwing formatter leaves `⚠ message` in that entry and a
`bib-load` warning —, emits the `entry{role: bibentry, key}` rows there,
under the style stack of the call, and returns the collector. A splice
that names an async constructor awaits it (its hole is async: codegen
reads `kStdAsync`); `#let b = bibliography(…)` binds the promise — write
`await`. A source is loaded once: a later collector naming it lists the
same rows. A load failure is an error node (`bib-load`) beside the empty
collector. `finishBibliographies`, the trailing unspanned roots and the
style-stack replay are gone; the document root's span now ends at its
last block.

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
2. Print inserts in renderPages (as built, plan P3-13: in paginate.cc).
3. `NEED_BIB` + numeric citations + bibliography collector.
4. Circled marks / per-section reset / grouped-cite ranges ([1–3]) as
   polish.

Estimated: (1) one day incl. goldens; (2) half a day; (3) one day — the
resource pull is the same shape as images and tokens.
