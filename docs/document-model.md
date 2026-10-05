# Document Model Specification

Status: **draft for review**. Normative for `engine/src/model|ops|resolve|render`, for `runtime/src/shared/ops.ts` and the executor, and for the dump formats golden tests consume. Companions: [design-decisions-v2.md](design-decisions-v2.md) (§ refs), [architecture.md](architecture.md), [testing.md](testing.md).

---

## 0. Products and lifetimes

A document handle owns, in order of production:

| product | lifetime | rebuilt by |
|---|---|---|
| SourceText | handle | recompile |
| AST, JsProgram | transient (until ingest) | recompile |
| **ContentTree** (post-resolve) + label/term/bib tables | handle | recompile |
| **BlockStreams** (per paragraph) + MetricStore | handle | recompile; entries invalidated by dppx change |
| **LayoutResult** | until next typeset/relayout | `tsr_typeset` / `tsr_relayout` |
| Diagnostics | handle; each entry carries its origin pass (compile / ingest / provide / emit / render) | a pass that re-runs (emit after late metrics or a width change, render) replaces its own slice (plan P0-11) |

Persistence of the middle products is what makes the pull-loop resumable and `relayout` cheap (architecture §2.4). All of it lives in the document arena.

## 1. Identity and anchoring

- **NodeId**: `u32` arena index. Document-scoped, stable for the handle lifetime, **not** stable across recompiles — continuity across edits is by source offsets, never by id.
- **pid** (paragraph id): the NodeId of each direct child of `doc`. The unit of upgrade swaps and of `tsr_render_typeset(range)`. DOM: `data-pid`.
- **Spans**: byte offsets `[start, end)` into the UTF-8 source. Every node carries one. Synthetic content takes the span of its generating construct: splice-produced nodes get the splice span; fence-handler nodes get the fence body span unless the handler passed a narrower offset (two-tier fidelity, v2 §4.1); resolver-produced content (ref text, collector expansions) gets the span of the `REF`/`COLLECT` site.

## 2. Content tree

Node = `{ kind: u16, span, style: StyleId, args, children }`. `style` is resolved at instantiation (§4) and interned. `args` is a small key→value map; value types: `str | f64 | bool | NodeId | null`.

### 2.1 Kind set

> The authoritative, generated kind table (levels, body models, attributes and their value domains) is [`docs/schema-table.md`](schema-table.md), generated from `engine/schema/schema.json` (remediation P0-06). Where the prose below disagrees, the schema wins.

| kind | level | args | children | from |
|---|---|---|---|---|
| `doc` | block | — | blocks | M1 |
| `para` | block | — | inline | M1 |
| `heading` | block | `level`, `label?` | inline | M2 |
| `list` | block | `ordered`, `start?` | `item*` | M2 |
| `item` | block | — | blocks | M2 |
| `quote` | block | — | blocks | M2 |
| `codeblock` | block | `lang`, `wrap?`, `lineNo?`, `hl?` (ops v3) | plain text child OR one `seq` per line of styled runs (CH1) | M2/CH |
| `rule` | block | — | — | M2 |
| `group` | block | `role?`, `label?` | blocks | M2 |
| `table` | block | `cols`, `align?`, `label?` | `trow*` | M6 |
| `trow` | block | — | `tcell*` | M6 |
| `tcell` | block | — | blocks | M6 |
| `term` | block | `name`, `label?` | blocks (description) | M4 |
| `collect` | block | `what`, params | — (expanded by resolver) | M4 |
| `mathblock` | block | `src`, `label?`, `name?` (resolver: "(n)") | — (MathBox at emit) | M7 ✓ |
| `error` | both | `message`, `code` | best-effort content | M1 |
| `comment` | both | `body` | — | M2 |
| `text` | inline | `str` | — | M1 |
| `styled` | inline | `delta` (§3) | inline | M1 |
| `link` | inline | `url` | inline | M2 |
| `code` | inline | `str` | — | M2 |
| `ref` | inline | `target`, `form?` (+resolved fields) | — | M4 |
| `mathinline` | inline | `src` | — (MathBox segments at emit) | M7 ✓ |
| `raw` | inline | `html`, `w?`, `h?` | — | M6 |
| `hardbreak` | inline | — (syntax reserved, not yet granted; the engine sets it as a forced line break, plan P1-13) | — | — |

Notes:
- **Labelable kinds** (accept `label`): `heading`, `group`, `table`, `term`, `mathblock`. Labels are args, not nodes.
- **Figure is a declared class, not a kind**: `group{role:"figure", label}` with a caption paragraph is selected by the figure row of the element registry (docs/semantics-design.md; plan P1-10) — keeps the engine kind set minimal, and a document can declare classes the same way.
- `val(x)` is not a kind: primitives splice as `text`; content values splice as themselves.
- **User constructors compose engine kinds.** There is no user-defined kind; custom constructs are built from `group`/`styled`/`raw` plus the rest. This is what keeps layout closed under the kind table.

## 3. Styles

**Base class bits** (u64; carried from v1, bit indices frozen):

```
0 LATIN   4 BOUNDARY    8 PUNCT_OPEN   12 SPACE
1 CJK     5 HAS_STYLE   9 PUNCT_CLOSE  13 LINK
2 EM      6 CODE       10 INDENT       14 CJK_SPACE
3 BOLD    7 CJK_EMPH   11 HYPHEN       15 PUNCT_SPACE
16 UNDER  17 OVER      18 STRIKE   (CH1: text-decoration, metric-neutral)
```

- `TextStyling = { classBits: u64, dynClasses: sorted u32[], inline: InlineStyle? }`, interned by hash → `StyleId`. Blocks and runs store StyleIds, never copies.
- `InlineStyle` props (initial set): `fontFamily, fontSizePx, fontWeight, italic, color, letterSpacingPx`. As built (plan P1-02): the run properties are rows of the `props` section of `engine/schema/schema.json`; `Styling`, its equality/hash/patch/dump code, the typeset serializer's declarations and the executor's key tables are generated from them (docs/style-design.md), and every textual value is checked against a generated domain DFA at decode. Adding a property is a row plus its consumer, not an ops version bump.
- **Implemented subset (ops v2)**: `fontFamily` (CSS family list; wins over the class-based body/cjk/mono split and the `.tsr-cjk` var by inline-style specificity), `sizePx` (absolute base; `sizeMul` still composes on top), `color`, plus `lang` (BCP-47 → per-run `lang` attribute, drives OpenType 'locl' forms — the promised set extension). `fontWeight`/`italic` ride the class bits; `letterSpacingPx` is deliberately NOT exposed — letter-spacing is the engine's CJK justification channel and a user value would fight it. Caveat: canvas measurement has no lang concept, so a `lang` that changes advances via 'locl' is invisible to measurement — defined-width dashes/ellipses are immune, fullwidth punctuation is 1em in every locale, so the practical exposure is nil (recorded).
- Authoring surface: inline `#style({font, lang, color, sizePx, bold, italic})[…]`; block scope `#{ $.style.push({…}) } … #{ $.style.popTo(h) }`; region scope `#!aside(font: '…', lang: "zh-TW")` (the region wraps itself in a `styled` node).
- **StyleDelta** (used by both `styled` nodes and the schedule stack): `{ addBits: u64, addDyn: u32[], patch: InlineStyle? }`.
- **Resolution** (at instantiation, §4): effective style of a node = fold of (schedule stack at its `EMIT`) ∘ (path of `styled` deltas from the emitted root down to the node). Bits OR; dyn sets union; inline patches nearest-wins per prop.
- **Binding time is emission-time** (v2 §12): a DAG value emitted twice under different stacks instantiates into two differently-styled subtrees. The instantiation walk therefore *copies* per emission; the DAG is a sharing optimization of the op stream, not of the content tree.
- **InstLimits** (remediation P0-07). Because copies are per occurrence, a few bytes of ops can describe an exponential tree. The copy runs on an explicit stack with two bounds: at most max(262144, 64 × raw nodes) nodes, and nesting at most 256 deep. A cut-off subtree becomes `error{inst-limit}` with one diagnostic.
- **Normal form** (P0-07, `model/normalize.cc`). Right after instantiation, three rules apply; inline-ness comes from the schema's level classes:
  - a paragraph made only of empty text vanishes;
  - a paragraph whose only child is a block- or adaptive-level node is replaced by that node (for example a `#toc` splice alone on a line);
  - (P1-08) a `seq` holding a block — the value of a content body with several blocks, `#callout[⏎- a⏎⏎text⏎]` — takes its place among its siblings, and a paragraph that is only such a `seq` is those blocks. A block `seq` inside inline content (`#strong[⏎p1⏎⏎p2⏎]`) is not split yet (T2's level normalization).
- Block boundaries snapshot the schedule stack height; block exit and error recovery pop to it.

## 4. Ops (normative binary contract)

### 4.1 JS-side values are shadow nodes

A content value in user/constructor JS is a **shadow node** `{ kind, args, children: shadow[], span, opId }` — a lightweight JS mirror created by each constructor as it writes the op. Why: constructors must be able to **traverse and regroup** content (the table constructor walking region children and splitting cells at tree level, v2 §4.1) — bare opaque ids cannot support that, and querying WASM mid-execution would be chatty. Regrouping emits new `MAKE_NODE` ops that reference the *existing* child `opId`s — the DAG shares; nothing is re-encoded. `m`-tag fragments come back from `tsr_parse_fragment` as an ops slice plus shadows reconstructed by the shared `ops.ts` reader; splicing rebases ids.

### 4.2 Buffer layout

```
header   magic "TSOP", version u8 (=2), nStrings varint, stringBytes varint, nOps varint
strings  UTF-8 blob + varint end-offsets
ops      op stream (all ints varint/LEB128 unless noted)
```

### 4.3 Opcodes

```
0x01 MAKE_TEXT   strRef                                  → id
0x02 MAKE_NODE   kind nargs (argKey argVal)* nchildren id*  → id
0x03 EMIT        id
0x04 STYLE_PUSH  bits npatch (argKey argVal)*   (v2: npatch was fixed 0 in v1)
0x05 STYLE_POP_TO height
0x06 SPAN        id start end       (post-hoc span attach; codegen wraps
                                     constructor calls in __at(node, s, e))
```

- Ids are implicit: each MAKE op takes the next sequence number. Post-order by construction (JS evaluation order), so every referenced child id < the referencing op's id.
- `argVal` is tag-prefixed: `0=null, 1=bool, 2=f64 (8 bytes LE), 3=strRef, 4=nodeId`.
- `EMIT`/`STYLE_*` form the schedule and are only valid at top level (codegen wraps top-level content in `EMIT`; `$.style` writes stack ops).
- **Validation** (the reader is a fuzz target — it consumes JS-produced input and must reject, never crash): magic/version; string refs and node ids in range; child id < own id; unknown kind → `error` node + diagnostic, not a crash; stack height underflow → diagnostic + clamp.

## 5. Resolver

Pure function of (ContentTree, Config). Document-order walk:

- **Counters**: section numbers from the derived heading tree; counter classes `figure | equation | footnote | <user>` with reset rules from config (`none | section`). Counter values are snapshotted into the label table at each label site.
- **Label table**: `label → { nodeId, kind, counters, textExcerpt }`. Duplicate label → diagnostic `label-duplicate`, first wins.
- **REF resolution**: `form = number | name | full` (default per target kind); output replaces the ref node's rendered content and sets `targetAnchor`. Unresolved → literal `??` content + `ref-unresolved` diagnostic.
- **Collectors**: `collect{what: toc|glossary|lof|bibliography}` expand into engine-kind subtrees (nested lists of links) from the tables. Citation order for bibliography = first-citation order.
- Runs before any rendering, so fallback HTML already carries final numbers (v2 §11.1).

Implementation notes (M4, normative for the dumps):
- The resolver **mutates the tree in place** between instantiation and emission; `--stage=tree` shows the post-resolve tree.
- Every heading gets an anchor: user label, or auto label `h-<number>` (also the fallback when a user label is a duplicate). Labeled units render `id="tsr-<label>"` on their first line; resolved refs render as `<a href="#tsr-<label>" data-syn="ref">`.
- `ref` keeps its kind; the resolver fills kids (display text) and a `url` arg. Display text per target kind uses the config supplements (`supHeading` "§", `supTable` "表 ", `supFigure` "图 ").
- `term` rewrites to `group{role:"term", label:name}` — bold name para (inline description joined with " — "), block description children follow.
- A paragraph whose only child is a `term`/`collect` splice is that construct at block level (unwrapped before dispatch).


## 6. Block stream

### 6.1 Units: `su` (subpixel unit) = 1/64 CSS px, `i32`

All engine-internal widths/positions are integer `su`. Why fixed-point: golden determinism (no float-summation-order variance across platforms/compilers) and a direct match to layout-engine precision. Measurement f64 px values are quantized on ingestion:

- word/advance widths: `ceil(px * 64)` **plus** `config.epsilon.perWordSu` (default 1) — this *is* the §7 ε policy: systematic overestimate, lines may only come out short;
- container widths: `floor(px * 64)`;
- vertical metrics: `round`.

Widths are measured per string with two exceptions. ——/…… **pair blocks are
never measured**: their width is the App C-normative 2em, and the typeset
serializer pins the pair span to `display:inline-block;width:2em` — canvas
and DOM disagree on cluster shaping / font selection for consecutive U+2014,
so measurement there cannot predict rendering. CJK-class runs measure (and
render, via `--tsr-cjk-font`) with `fonts.cjk`, keeping ambiguous glyphs
(dashes, ellipses, curly quotes) out of the Latin face.

**Quantized widths feed the breaker only.** Justification arithmetic (line slack, per-gap Δ) uses the **raw f64 px** measurements, which the metric store retains alongside the quantized value — otherwise ε and ceil would leak into the right edge as a systematic ~0.3px shortfall. Overflow safety comes from quantization; edge precision comes from raw math.

Document-height accumulation uses i64.

### 6.2 Block struct (formalizing the PoC)

```
LinebreakBlock {
  width, breakWidth, spaceWidth : su
  breakPenalty                  : f32   (INF allowed)
  stretchWeight                 : f32   (0 = rigid; Latin space 1.0; CJK glue = k)
  style                         : StyleId
  flags                         : isSpace | isHyphen | isCJK | isBoundary | isIndent
  content                       : text strRef | inlineBox nodeId
  span                          : source span
}
```

`stretchWeight` carries the v2 §8 k-rule into the breaker: line stretchability = Σ weights; the renderer distributes `Δword` per unit weight, so cost model and rendering agree by construction. Emission rules per Appendix C of v2.

**Breaking semantics (as built, plan P0-12; rules at the top of `break/break.cc`).** The breaker reads the TeX item projection of the blocks (`break/items.h`: Box / Glue / Penalty{Normal, Forbidden, Forced} / Disc{pre}): `BREAK_INF` is Forbidden — never a candidate; glue at a break and at a line start (paragraph start included) is discarded, so the optimizer measures exactly the range layout renders; a hyphen point adds its glyph only when broken; the paragraph end is a Forced break whose line has fil stretch and normal shrink. Line cost is `min(mapped(x)^exponent, 1e4)` (an integer power by multiplication), Overfull (x < −shrinkThreshold) is a class, penalties are i32 thousandths; ties go to lower demerits, then fewer lines, then the later parent. The search (plan P1-14) is TeX's active list: a node leaves it as soon as its line becomes Overfull, a Forced break deactivates every earlier node, line counts are kept apart only while the parshape prefix makes widths depend on them — no window, no line-count pruning, no retry ladder; `BreakParams` adds optional tolerance and emergency-stretch passes (off by default). When no path exists, the final pass rescues: the best active node breaks at the first legal break after the run, the line is Overfull — set at the shrink limit by layout, marked `data-overfull` in HTML, reported as `overfull-line`. A breakpoint is the index of the next line's first block after discard. Native and WASM builds produce identical breaks (`tools/wasm-goldens.mjs`).

### 6.3 Table cells (M6 v1)

Each `tcell` flattens to its own miniature block stream (`TableCell`), broken
by the same KP breaker at the cell content width. v1 geometry: **equal
columns** (`colW = measure / cols`), horizontal cell padding 0.4em, vertical
row padding 0.3em, full-width rules above/between/below rows. Cells are
**ragged** (never justified); the `align` string ('l'/'c'/'r' per column)
shifts whole lines at layout time. Cell lines carry `data-cell="1"`, no
`data-join`, and reference the cell's block stream via `cellIdx` in the
layout result; a label inside a cell (an inline term) anchors the cell's
first line, and the cell's breaks appear in the breaks dump (`cell=`). Region provenance is materialized at codegen: each source
line of a region paragraph is a row, segmented at top-level unescaped `|`
(code spans and splices are opaque; `\|` escapes; `||` is an empty cell;
leading/trailing empties of |-framed lines drop). Non-tabular regions rejoin
the segmentation (" | ") into ordinary paragraphs and become
`group{role:<name>}`. Fences compile to a runtime dispatcher: unknown tags
fall back to plain code blocks; handlers (registered `#{ $.fence(tag, fn) }`
before use) may be async, get `{args, offset, m, error, raw}`, and a thrown
handler becomes a renderable error node. `raw` nodes are block units with
handler-declared height (default one leading). Deferred: `m.parse` WASM
re-entry, the indented cell-continuation rule, `#use`.

### 6.4 Measurement states

MetricStore entries per (strRef × StyleId): `exact | pending(estimate) | invalid`. Bundled-font entries are born `exact` (precompiled metrics). A paragraph is `estimated` if any of its blocks is pending; upgrades re-run break+layout+render for exactly those paragraphs when measurements arrive.

## 7. Measurement buffers

- **Request** (`tsr_measure_requests`): list of style descriptors (id, family, sizePx, weight, italic) needing vertical metrics + per-style list of strings needing widths. Only missing entries are requested (the JS cache sits above; this is the engine-side dedup).
- **Provide**: per style `{ascentPx, descentPx: f64}`; per string `{widthPx: f64}`. Quantization per §6.1 happens engine-side.

## 8. Layout result

As built (plan P1-18; docs/layout-design.md): layout walks the box tree
(`engine/src/boxtree/`) with one layouter per `LayouterId` and produces, per
top-level block, a frame of **fragments** in paint order and its vertical
list.

```
LayoutResult {
  docHeight : i64 su
  paras: [{                                  // one per top-level block (pid)
    pid, rect: {x,y,w,h: su},
    lines: [Fragment {                       // engine/src/layout/layout.h
      kind                 : Line | Rule | CodeRow | Raw | Math | Image
      y, left, width, height : su            // y = the top (a rule's too)
      unit, cell           : the leaf, and its other track (a cell, a caption row, a sidecar row)
      items                : [i, j)          // into the stream's HList (blocks [i, j) for dumps)
      wordDeltaPx, cjkDeltaPx : f64          // raw-px spacing (what paint copies)
      wordDeltaSu, cjkDeltaSu : i32          // rounded, for dumps/goldens
      endsWithHyphen, ragged, noGlue, overfull : bool
      join                 : space | none    // whether the break consumed a space (copy rule §9.3)
      anchor, anchor2      : the ids it carries (exactly one fragment per anchored block)
      marker               : a list marker / line number in the gutter
      srcSpan              : [s, e)
    }],
    vlist: [{unit, gap, clear, y, h, outOfFlow}]   // tsrc --stage=vlist
  }]
  breaks: [{pid, unit, cell, breakpoints, cost}]  // tsrc --stage=breaks
}
```

Leading: line advance `= max(baseLeading, ascent + descent)` where `baseLeading = round(lineHeight × fontSizePx × 64)` from the paragraph style and ascent/descent are the line's max run metrics — mixed CJK/Latin prose stays on the uniform grid (both fit under baseLeading); only oversized inline boxes (math, dropcap-adjacent) grow a line. The baseline of line i is communicated as `Fragment::baseline` (plan P1-18): the CSS inline formula, half the leading above the extents — `top_i + (advance_i − (ascent_i + descent_i))/2 + ascent_i`; a code row centres its style's extents in the row, a display formula its box. Pinning it in the HTML (a line-height the host cannot override) is P3's (T7 S11).

The upgrade payload (v2 §9) is `paras[]` plus per-paragraph HTML.

## 9. Serializer contracts (normative — tests assert this DOM)

Class prefix `tsr-`. Both serializers escape all text (`& < > " '`); the only unescaped path is `raw.html` (trusted, handler-declared).

Both serializers build start tags through `render/html_writer.h` (plan P0-10): attribute names come from the explicit allowlist `kHtmlAttrs` (checked at compile time — an unlisted name does not build), every element has at most ONE `style` attribute (declarations merge into it), ids are spelled by `AnchorNamer` (`tsr-` + the escaped label), and a repeated attribute is a serializer defect — debug builds assert, release keeps the first value and reports `render-attr`.

### 9.1 Typeset HTML

As built (plan P1-18; docs/render-design.md): paint (`engine/src/paint/`)
turns each frame's fragments into a DisplayList block — runs formed at run
instance boundaries, typed inline payloads, every px value the HTML prints —
and the typeset writer (`render/typeset_html.cc`) is a stateless walk of it:
it reads no Config, no emit unit and no content tree. Ids are the fragments'
anchors (each anchored block's first fragment; an enclosing label sharing it
is an empty `<span id data-syn="anchor">` first inside it), so the flowing
and the paged output carry the same ids.

```html
<div class="tsr-doc" lang="zh-CN" style="--tsr-font-body:…;--tsr-font-cjk:…;--tsr-font-mono:…;--tsr-font-mono-cjk:…;font-size:18px">  <!-- P1-04: the measured fonts; text-rendering:geometricPrecision -->
  <div class="tsr-para" data-pid="7" style="position:relative;height:{h}px">
    <div class="tsr-line" data-s="120" data-e="181" data-join="space"
         style="top:{y}px;left:{x}px;width:{w}px;word-spacing:{d}px">
      <span class="tsr-r" data-s="120">real text </span>
      <span class="tsr-r tsr-cjk" data-s="131" style="letter-spacing:{c}px">中文串</span>
      <span class="tsr-r" data-syn="hyphen">-</span>
    </div>
  </div>
</div>
```

- CSS contract: `.tsr-line { position:absolute; white-space:nowrap; contain:layout style paint; }` — the v2 §7 rules are *serializer output*, not page-author responsibility.
- Runs carry `data-s` when they map 1:1 to a source slice; synthetic runs (hyphens, resolved refs, escapes-containing runs) carry `data-syn` instead.
- A line holding a run wider than the measure carries `data-overfull="1"` (plan P0-12): it is set at the shrink limit and overflows on purpose; the audit skips its right edge and the paragraph's overflow check.
- Run boundary (interim key until P4-01's run instances): style, link and the generated-reference flag — a resolved ref's text (`[1]`, `??`) is never merged with the authored prose or punctuation beside it. A line-final hyphen opens inside its word's link (`<a … data-syn="hyphen">-</a>`).
- `comment` nodes are not rendered here; `error` renders as `<span|div class="tsr-err" title="{message}">`.

### 9.2 Semantic HTML

Element mapping: `para→p, heading→h1..h6, list→ul|ol, item→li, quote→blockquote, codeblock→pre>code, rule→hr, group→div[data-role], table→table/tr/td, term→dl>dt+dd, collect→nav|section, styled→em|strong|span[class], link/ref→a, code→code, raw→passthrough`. Paragraph-level elements carry the same `data-pid` (the upgrade swap keys on it) and `data-s/e`. No positioning, no spacing styles — the browser flows it (v2 §9).

Implementation notes (M5): the semantic serializer runs on the post-resolve
tree with no measurements — the worker posts it immediately after ingest and
the shell paints it before the pull loop starts. Upgrade = one keyed
`data-pid` swap; the shell reports `{pid, old:{top,height}, new:{top,height}}`
per paragraph to the caller (`onUpgrade`), scroll anchoring stays caller-side
(v2 §9). `relayout(widthPx)` re-breaks/lays out the worker-held doc with
persisted metrics. The `data-join` attribute reflects whether the break
consumed a REAL source space: CJK inter-character breaks and synthetic glue
(boundary, punct halves, indents) render `data-join="none"`, so the §9.3
rebuild reproduces source text exactly. It is decided by the break, not by
alignment (plan P1-17): a wrapped heading, caption or error block joins
like a paragraph; only the paragraph end and a hard line break are real
line boundaries (no attribute), and table cells and sidecar rows never
carry one. Every stream's lines come from one `materializeLines` (layout),
so paragraphs, cells, float captions and sidecar rows share the hyphen,
height, span and spacing rules — a tight ragged line shrinks to fit, as the
breaker assumed. Deferred: `pending(estimate)` states
and webfont-settle re-typesets, `size-adjust` fallback descriptors.

### 9.3 Copy (normative for `runtime/main/copy.mjs`)

Copy produces **content text**, not markup source: walk selected `.tsr-r` runs in DOM order — skip `data-syn` runs; take runs' text (partial at the selection endpoints, character-offset within the run); between consecutive lines insert `" "` or `""` per the line's `data-join`; between paragraphs insert `"\n\n"`. Source offsets (`data-s`) are for anchoring and diagnostics, not for copy.

### 9.4 Math runs (M7)

- A formula renders as one `<span class="tsr-math" data-syn="math"
  data-src="$…$">` inline box (width/height/vertical-align from the MathBox;
  display formulas are a `Math` fragment: a centred row with an explicit
  height, the formula placed in it by paint, and an optional `.tsr-eqno`
  right-margin number, `data-syn="eqno"`, its offset computed by paint).
  Inside: absolutely positioned `.tsr-mg` glyph runs (baseline pinned by
  line-height == hhea height) and `.tsr-mr` rule boxes. Glyphs are painted
  BY CODEPOINT in the bundled font — the artifact's gating check guarantees
  every variant/assembly glyph is cmap-reachable.
- Copy (§9.3 extension): `data-syn="math"` runs contribute their `data-src`
  source text; a formula split into break segments carries the source on
  the FIRST segment only (later segments have empty data-src). Audits treat
  a math span as one engine-defined line fragment.
- Semantic fallback (§9.2): `<code class="tsr-mathsrc">$src$</code>` inline;
  `<p class="tsr-mathblock">` for display. MathML stays rejected (v2 §13).

## 10. Diagnostics

As built (plan P0-11): `DiagSink` stamps every diagnostic with the pass that reported it, and `begin(origin)` drops that pass's earlier slice, so a re-emit (late math-text metrics, `tsr_set_width`) never duplicates its warnings. A diagnostic about a generated node without a span (a figure's image) points at its innermost spanned block. The wire format is still the text dump until the JSON form below lands.

`{ severity: error|warning|info, code, span, message, related?: span[] }`, JSON via `tsr_diagnostics`. Initial code table:

```
parse-block          error    unparsable block (recovered at block boundary)
parse-inline         error    inline parse failure (block became error node)
splice-js            error    splice lexing failure (unbalanced, regex literal, …)
region-unclosed      error    #!name without matching closer
region-mismatch      error    closer name does not match innermost open region
script-error         error    document program threw (per top-level block)
fence-error          error    fence handler threw / ctx.error(...)
ops-invalid          error    op buffer failed validation
ref-unresolved       warning  @id with no label
label-duplicate      warning  same label declared twice (first wins)
style-underflow      warning  STYLE_POP_TO above current height (clamped)
measure-fallback     info     glyphs measured via fallback font
```

## 11. Config (`tsr_doc_new` JSON)

As built (plan P1-03): one settings document, `tsr2_set_config(doc, json)`, whose rows are generated from the schema (`docs/settings-table.md`; paths such as `host.width`, `doc.lang`, `fonts.body`, `code.snapKerning`, `cost.exponent`); see `docs/host-protocol-design.md`. The sketch below is the original plan of the document's shape.

```json
{
  "fonts":   { "body": "...", "cjk": "...", "mono": "...", "math": "Euler Math (bundled woff2; metrics precompiled)" },
  // fonts.cjk is live (M6+): CJK-class runs measure AND render with this
  // stack (renderer: .tsr-cjk { font-family: var(--tsr-cjk-font) }) so
  // U+2014/…/fullwidth puncts never resolve into the Latin face,
  "baseSizePx": 18, "lineHeight": 1.5,
  "cjkJustifyK": 0.6, "punctCompress": "book",
  "epsilon": { "perWordSu": 1 },
  "supplements": { "heading": "§", "figure": "图 ", "equation": "式 " },
  "counters": { "figure": { "resetAt": "none" } },
  "breaker": { "exponent": 3, "hyphenPenalty": 0.7, "shrinkThreshold": 0.37, "shrinkCoeff": 0.6, "cap": 1e4 }   // exponent: integer 1..4
}
```

Unknown keys → diagnostic, not error (forward compat).

## 12. Dump formats (golden-test contract, byte-exact)

`--stage=mathbox` (M7): per-formula box trees in document order —
`display pid=N` / `inline pid=N "src"` headers, then an indented tree of
`hbox/glyph/rule/glue` lines with atom class, `w/asc/desc` in su, `@(dx,dy)`
child offsets (dy positive = up) and per-glyph `px` sizes. Only fixtures
containing math carry this golden.

- **Tree dump** — one node per line, children indented two spaces:

  ```
  para @[0,42)
    text @[0,10) str="均值是 "
    styled @[10,18) delta=+EM
      text @[11,17) str="就地"
  ```

  Strings JSON-escaped; args in fixed key order; StyleId printed as resolved `classBits+dyn+inline` (ids are not stable).
- **Ops disassembly** — `%7 = MAKE_NODE em children=[%6]`, `EMIT %8`, one op per line.
- **Block stream dump** — one block per line: `word "The" w=512su pen=INF style=LATIN @[0,3)`.
- **Layout dump** — per line: `L3 y=288su left=0 w=19200su Δw=27su Δc=16su join=space blocks=[14,22)`.
- HTML dumps are the serializer output itself, prettified by a stable formatter (one element per line).

Every stage type implements `dump()` once; `tsrc --stage=…` and the golden tests share it verbatim.
