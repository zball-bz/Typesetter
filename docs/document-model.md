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
| Diagnostics | handle (append-only) | recompile |

Persistence of the middle products is what makes the pull-loop resumable and `relayout` cheap (architecture §2.4). All of it lives in the document arena.

## 1. Identity and anchoring

- **NodeId**: `u32` arena index. Document-scoped, stable for the handle lifetime, **not** stable across recompiles — continuity across edits is by source offsets, never by id.
- **pid** (paragraph id): the NodeId of each direct child of `doc`. The unit of upgrade swaps and of `tsr_render_typeset(range)`. DOM: `data-pid`.
- **Spans**: byte offsets `[start, end)` into the UTF-8 source. Every node carries one. Synthetic content takes the span of its generating construct: splice-produced nodes get the splice span; fence-handler nodes get the fence body span unless the handler passed a narrower offset (two-tier fidelity, v2 §4.1); resolver-produced content (ref text, collector expansions) gets the span of the `REF`/`COLLECT` site.

## 2. Content tree

Node = `{ kind: u16, span, style: StyleId, args, children }`. `style` is resolved at instantiation (§4) and interned. `args` is a small key→value map; value types: `str | f64 | bool | NodeId | null`.

### 2.1 Kind set

| kind | level | args | children | from |
|---|---|---|---|---|
| `doc` | block | — | blocks | M1 |
| `para` | block | — | inline | M1 |
| `heading` | block | `level`, `label?` | inline | M2 |
| `list` | block | `ordered`, `start?` | `item*` | M2 |
| `item` | block | — | blocks | M2 |
| `quote` | block | — | blocks | M2 |
| `codeblock` | block | `lang`, `body` | — | M2 |
| `rule` | block | — | — | M2 |
| `group` | block | `role?`, `label?` | blocks | M2 |
| `table` | block | `cols`, `align?`, `label?` | `trow*` | M6 |
| `trow` | block | — | `tcell*` | M6 |
| `tcell` | block | — | blocks | M6 |
| `term` | block | `name`, `label?` | blocks (description) | M4 |
| `collect` | block | `what`, params | — (expanded by resolver) | M4 |
| `mathblock` | block | `src`, `label?` | — (boxes at M7) | M7 |
| `error` | both | `message`, `code` | best-effort content | M1 |
| `comment` | both | `body` | — | M2 |
| `text` | inline | `str` | — | M1 |
| `styled` | inline | `delta` (§3) | inline | M1 |
| `link` | inline | `url` | inline | M2 |
| `code` | inline | `str` | — | M2 |
| `ref` | inline | `target`, `form?` (+resolved fields) | — | M4 |
| `mathinline` | inline | `src` | — | M7 |
| `raw` | inline | `html`, `w?`, `h?` | — | M6 |
| `hardbreak` | inline | — (syntax reserved, not yet granted) | — | — |

Notes:
- **Labelable kinds** (accept `label`): `heading`, `group`, `table`, `term`, `mathblock`. Labels are args, not nodes.
- **Figure is a convention, not a kind**: `group{role:"figure", label}` with a caption paragraph — keeps the engine kind set minimal.
- `val(x)` is not a kind: primitives splice as `text`; content values splice as themselves.
- **User constructors compose engine kinds.** There is no user-defined kind; custom constructs are built from `group`/`styled`/`raw` plus the rest. This is what keeps layout closed under the kind table.

## 3. Styles

**Base class bits** (u64; carried from v1, bit indices frozen):

```
0 LATIN   4 BOUNDARY    8 PUNCT_OPEN   12 SPACE
1 CJK     5 HAS_STYLE   9 PUNCT_CLOSE  13 LINK
2 EM      6 CODE       10 INDENT       14 CJK_SPACE
3 BOLD    7 CJK_EMPH   11 HYPHEN       15 PUNCT_SPACE
```

- `TextStyling = { classBits: u64, dynClasses: sorted u32[], inline: InlineStyle? }`, interned by hash → `StyleId`. Blocks and runs store StyleIds, never copies.
- `InlineStyle` props (initial set): `fontFamily, fontSizePx, fontWeight, italic, color, letterSpacingPx`. Extending the set is a model version bump.
- **StyleDelta** (used by both `styled` nodes and the schedule stack): `{ addBits: u64, addDyn: u32[], patch: InlineStyle? }`.
- **Resolution** (at instantiation, §4): effective style of a node = fold of (schedule stack at its `EMIT`) ∘ (path of `styled` deltas from the emitted root down to the node). Bits OR; dyn sets union; inline patches nearest-wins per prop.
- **Binding time is emission-time** (v2 §12): a DAG value emitted twice under different stacks instantiates into two differently-styled subtrees. The instantiation walk therefore *copies* per emission; the DAG is a sharing optimization of the op stream, not of the content tree.
- Block boundaries snapshot the schedule stack height; block exit and error recovery pop to it.

## 4. Ops (normative binary contract)

### 4.1 JS-side values are shadow nodes

A content value in user/constructor JS is a **shadow node** `{ kind, args, children: shadow[], span, opId }` — a lightweight JS mirror created by each constructor as it writes the op. Why: constructors must be able to **traverse and regroup** content (the table constructor walking region children and splitting cells at tree level, v2 §4.1) — bare opaque ids cannot support that, and querying WASM mid-execution would be chatty. Regrouping emits new `MAKE_NODE` ops that reference the *existing* child `opId`s — the DAG shares; nothing is re-encoded. `m`-tag fragments come back from `tsr_parse_fragment` as an ops slice plus shadows reconstructed by the shared `ops.ts` reader; splicing rebases ids.

### 4.2 Buffer layout

```
header   magic "TSOP", version u8, nStrings varint, stringBytes varint, nOps varint
strings  UTF-8 blob + varint end-offsets
ops      op stream (all ints varint/LEB128 unless noted)
```

### 4.3 Opcodes

```
0x01 MAKE_TEXT   strRef                                  → id
0x02 MAKE_NODE   kind nargs (argKey argVal)* nchildren id*  → id
0x03 EMIT        id
0x04 STYLE_PUSH  delta
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

### 6.3 Table cells (M6 v1)

Each `tcell` flattens to its own miniature block stream (`TableCell`), broken
by the same KP breaker at the cell content width. v1 geometry: **equal
columns** (`colW = measure / cols`), horizontal cell padding 0.4em, vertical
row padding 0.3em, full-width rules above/between/below rows. Cells are
**ragged** (never justified); the `align` string ('l'/'c'/'r' per column)
shifts whole lines at layout time. Cell lines carry `data-cell="1"`, no
`data-join`, and reference the cell's block stream via `cellIdx` in the
layout result. Region provenance is materialized at codegen: each source
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

```
LayoutResult {
  docHeight : i64 su
  paras: [{
    pid, rect: {x,y,w,h: su},
    estimated: bool,
    lines: [{
      y, left, width       : su          // width = measure for this line (parshape)
      blockRange           : [i, j)       // into the paragraph's block stream
      wordDeltaPx, cjkDeltaPx : f64       // raw-px spacing (what the serializer emits)
      wordDeltaSu, cjkDeltaSu : i32       // rounded, for dumps/goldens
      endsWithHyphen       : bool
      join                 : space | none // whether the break consumed a space (copy rule §9.3)
      srcSpan              : [s, e)
    }]
  }]
}
```

Leading: line advance `= max(baseLeading, ascent + descent)` where `baseLeading = round(lineHeight × fontSizePx × 64)` from the paragraph style and ascent/descent are the line's max run metrics — mixed CJK/Latin prose stays on the uniform grid (both fit under baseLeading); only oversized inline boxes (math, dropcap-adjacent) grow a line. Baseline of line i sits at `top_i + ascent_i`.

The upgrade payload (v2 §9) is `paras[]` plus per-paragraph HTML.

## 9. Serializer contracts (normative — tests assert this DOM)

Class prefix `tsr-`. Both serializers escape all text (`& < > " '`); the only unescaped path is `raw.html` (trusted, handler-declared).

### 9.1 Typeset HTML

```html
<div class="tsr-doc">                                  <!-- text-rendering:geometricPrecision -->
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
- `comment` nodes are not rendered here; `error` renders as `<span|div class="tsr-err" title="{message}">`.

### 9.2 Semantic HTML

Element mapping: `para→p, heading→h1..h6, list→ul|ol, item→li, quote→blockquote, codeblock→pre>code, rule→hr, group→div[data-role], table→table/tr/td, term→dl>dt+dd, collect→nav|section, styled→em|strong|span[class], link/ref→a, code→code, raw→passthrough`. Paragraph-level elements carry the same `data-pid` (the upgrade swap keys on it) and `data-s/e`. No positioning, no spacing styles — the browser flows it (v2 §9).

### 9.3 Copy (normative for `runtime/main/copy.ts`)

Copy produces **content text**, not markup source: walk selected `.tsr-r` runs in DOM order — skip `data-syn` runs; take runs' text (partial at the selection endpoints, character-offset within the run); between consecutive lines insert `" "` or `""` per the line's `data-join`; between paragraphs insert `"\n\n"`. Source offsets (`data-s`) are for anchoring and diagnostics, not for copy.

## 10. Diagnostics

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

```json
{
  "fonts":   { "body": "...", "cjk": "...", "mono": "...", "math": "Neo Euler" },
  "baseSizePx": 18, "lineHeight": 1.5,
  "cjkJustifyK": 0.6, "punctCompress": "book",
  "epsilon": { "perWordSu": 1 },
  "supplements": { "heading": "§", "figure": "图 ", "equation": "式 " },
  "counters": { "figure": { "resetAt": "none" } },
  "breaker": { "exponent": 3, "hyphenPenalty": 0.7, "shrinkThreshold": 0.37, "shrinkCoeff": 0.6 }
}
```

Unknown keys → diagnostic, not error (forward compat).

## 12. Dump formats (golden-test contract, byte-exact)

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
