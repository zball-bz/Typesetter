# Style properties (design; as built from plan P1-02)

## 1. One vocabulary, declared once

The engine understands a closed set of run properties. Each is one row of the
`props` section of `engine/schema/schema.json` (the shared schema of the ops
contract, design T2/T4):

| row | field | type | patched by | dump | typeset CSS |
|---|---|---|---|---|---|
| `text.weight` | `weight` | u16 (0 = 400) | `weight` (100–900); sugar `bold` (700; `bold: false` is 400) | `BOLD` (700), `W<n>` | class `tsr-b` (700) |
| `text.italic` | `italic` | bool | `italic` | `EM` | class `tsr-i` |
| `text.decoration` | `decoration` | flags UNDER/OVER/STRIKE (ORed) | `decoration` (names); sugar `underline overline strike` | `U` `O` `S` | `text-decoration` |
| `text.fontRole` | `fontRole` | enum body/mono (0 = inherited: body) | `fontRole`; the engine for inline code, code blocks, list markers | `CODE` (mono) | class `tsr-code` (mono) |
| `text.baseline` | `baseline` | enum super/sub | `baseline`; footnote markers | `SUP` `SUB` | class `tsr-sup` |
| `code.hang` | `hang` | enum indent/content | `code: {hang}`; comment tokens (content) | `hang=` | (layout: a code line's continuation hangs at the run's content) |
| `text.sizeMul` | `sizeMul` | size multiplier | `size` (`'0.7em'`, `'70%'` multiply; `'22px'` sets `sizePx` and resets it — D-T01) and the engine (headings, code, markers) | `x%.2f` | `font-size` (with `sizePx`, through `emPx`) |
| `text.font` | `fontFamily` | string | `font` (domain `font`) | `font="…"` | `font-family` (escaped) |
| `text.lang` | `lang` | string | `lang` (domain `lang`) | `lang=` | the `lang` attribute |
| `text.color` | `color` | string | `color` (domain `color`) | `color=` | `color` |
| `text.size` | `sizePx` | px | `sizePx` (domain `num:1:2000`; absolute: resets the multiplier, D-T01) | `size=%gpx` | `font-size` |
| `engine.script` | `script` | internal | T5's classifier only (a CJK run; never on the wire) | `CJK` | class `tsr-cjk`; selects the face |

As built (plan P2-08): the class bits retired from `Styling`. Engine code
composes a `StyleDelta` (weight, italic, decorations, font role, baseline,
script, size multiplier) where it used to OR bits in; links are no style (a
link is a run of its own); the face is chosen from the font role and the
script; the dumps keep the bits' spellings. On the wire (ops 11, the one
MIN_COMPAT bump of P2) every style change is a **delta node** — a childless
`styled` node whose attributes are the rows above (`styled.bits` retired):
a `styled` node's own attributes, the universal `style` attribute of any
node (its own change, applied after its parent's), and `STYLE_PUSH <delta>`
(the schedule stack). The JS side builds them from one patch form
(`styleAttrs`: the property keys, `code: {hang}`, the boolean sugar), in
`$.style.push`, `style(patch, …)`, `strong`/`em`, a constructor's `style:`
option and a region header's `style: {…}`.

`tools/gen-schema.mjs` generates from these rows:

- `engine/src/model/props.gen.h`: `struct Styling` (one field per row, in row
  order), `==`, `StylingHash`, `canonicalize` (−0 → +0, NaN → initial),
  `applyStyleArg` (folds a `styled` attribute or a `STYLE_PUSH` patch value),
  `appendStyleFields` (the value part shared by the tree and block dumps; each
  dump keeps its own flag tokens);
- `engine/src/render/style_css.gen.h`: `runCss`, the typeset serializer's run
  declarations in `cssOrder`;
- `runtime/src/shared/props.gen.mjs`: `STYLE_KEYS` (the patch keys →
  attributes), `STYLE_SUGAR` (boolean keys → attribute and value) and
  `STYLE_FLAGS` (a flag row's names), read by the executor instead of three
  hand-kept key lists.

Adding a run property is one row plus its consumer (measurement for a metric
property, the serializer for a paint property); nothing else is spelled out by
hand.

## 2. Value domains, validated once

Textual attribute domains (`ident label lang rangeset color font`) are defined
in the `domains` section as a byte-level regular expression plus a maximum
UTF-8 length. The generator compiles each expression to a minimal DFA over
byte classes and emits:

- C++ (`engine/src/ops/domains.gen.{h,cc}`): `matchDomain(TextDomain, string_view)`
  — the authoritative check, run by the ops reader at decode (P0-06) and
  fuzzed with it;
- JS (`DOMAINS` in `props.gen.mjs`): the same expression as a `u`-flag RegExp
  and the same byte limit, `validDomain(name, s)`, for early diagnostics.

Bytes `\x80-\xff` in an expression stand for "any non-ASCII character", so
both languages accept the same strings (`tools/check-domains.mjs` runs a
differential test against the WASM build in gate G4). A value outside its
domain is dropped with an `ops-arg` warning; CSS injection
(`red;letter-spacing:9px`) cannot reach a style attribute.

## 3. Faces (plan P1-04)

Widths are measured in a **face** — `FaceKey {family (resolved list), px size,
weight, italic}` (`engine/src/measure/face.h`) — and the metric store keys on
`(string, FaceId)`. A style resolves to its face once (`FaceTable::faceOf`,
memoised): styles that differ in paint only (color, link, decoration, lang)
share a face and therefore their metrics. Measurement requests are per face
(the request's `id` is a FaceId).

Family resolution: an explicit `text.font` wins; otherwise the role (mono for
code runs, body otherwise — run roles select it from P2-08) and the script:
Latin → `role.latin`; CJK → `role.cjk` → `body.cjk` → `role.latin` (CJK glyphs
never fall into a Latin face). Settings: `fonts.body`, `fonts.cjk`,
`fonts.mono`, `fonts.monoCjk`. CJK italic is measured upright, as it is
painted (upright with emphasis marks).

The typeset root carries what was measured: `<div class="tsr-doc" lang
style="--tsr-font-body;--tsr-font-cjk;--tsr-font-mono;--tsr-font-mono-cjk;font-size">`,
all resolved engine-side; the CSS contract paints `.tsr-doc`, `.tsr-cjk`,
`.tsr-code` and `.tsr-code.tsr-cjk` from these variables, so measurement and
paint name the same family. `lang` is not part of the face: the residual
'locl'/generic-fallback exposure (document-model §3) remains documented.

## 4. The cascade (as built, plan P3-01)

Rules patch the same rows: a rule is a selector and a patch of styled
attributes (`$.set`, `style.where`, the host's `style.rules`, the engine's
`engine/data/defaults.json`, whose patches may name a setting:
`{"setting": "code.scale", "unit": "em"}`). The block rows (`gran: block` in
the `props` section: `par.*`, `block.*`, `list.marker`) form `NodeProps`; the
run rows `Styling`. One fold (`model/cascade.cc`) computes both, with each
node's rule-free scope for the semantic page, whose stylesheet the same
rules compile to (`render/rules_css.cc`). Precedence, scoping and the made
and lifted nodes: docs/document-model.md §3, v2 §12.

Plan P3-02 added the rows with evidence of scoped use: block
`codeblock.snapKerning`, `codeblock.sidecarFrac`, `codeblock.contIndent`
(their settings are the defaults through `defaults.json`; a code block's
same-named arguments are its own style), run `text.features` (code's font
features: measurement and paint read the run's) and run `text.punct` (CJK
punctuation compression, `full|book|none`; unset reads the document's
`cjk.punctCompress`).

Plan P3-09 made `par.align` the choice of a **line-end glue preset**
(break.h `LineEnds`, design T6): `justify` (interior glue absorbs the slack,
the last line ends with fil), `start` / `end` (2em of finite stretch at the
free end, interior glue rigid — D-Y01), `center` (1em each side). The
breaker optimizes with the same glue the lines are set with, so a ragged
paragraph avoids hyphens and tight lines as TeX's ragged-right does; table
cells take their column's halign as a preset. New block row
`par.singleLine`: `align` | `center` — a paragraph that is one line is
centred (the semantic page: a shrink-to-fit centred box). The caption rule
(D-Y05, LaTeX's `singlelinecheck`): justified, hyphenated, a one-line
caption centred — block and float captions alike.

## 5. Not yet

Element style sections (P3-14) and scoped document knobs (P3-02); the
semantic page's role and class hooks (P3-18, P3-23); a token's colour is
still its own until the class channel (P3-18).
