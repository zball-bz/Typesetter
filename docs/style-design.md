# Style properties (design; as built from plan P1-02)

## 1. One vocabulary, declared once

The engine understands a closed set of run properties. Each is one row of the
`props` section of `engine/schema/schema.json` (the shared schema of the ops
contract, design T2/T4):

| row | field | type | patched by | dump | typeset CSS |
|---|---|---|---|---|---|
| `text.weight` | `weight` | u16 (0 = 400) | (P2-08: the legacy `bits` flag BOLD = 700); sugar `bold` | `BOLD` (700), `W<n>` | class `tsr-b` (700) |
| `text.italic` | `italic` | bool | legacy flag EM; sugar `italic` | `EM` | class `tsr-i` |
| `text.decoration` | `decoration` | flags UNDER/OVER/STRIKE (ORed) | legacy flags; sugar `underline overline strike` | `U` `O` `S` | `text-decoration` |
| `text.fontRole` | `fontRole` | enum body/mono (0 = inherited: body) | engine (inline code, code blocks, list markers: `mono`) | `CODE` (mono) | class `tsr-code` (mono) |
| `text.baseline` | `baseline` | enum super/sub | engine (footnote markers) | `SUP` `SUB` | class `tsr-sup` |
| `text.sizeMul` | `sizeMul` | size multiplier | composed by the engine (headings, code, sup) | `x%.2f` | `font-size` (with `sizePx`, through `emPx`) |
| `text.font` | `fontFamily` | string | `font` (domain `font`) | `font="…"` | `font-family` (escaped) |
| `text.lang` | `lang` | string | `lang` (domain `lang`) | `lang=` | the `lang` attribute |
| `text.color` | `color` | string | `color` (domain `color`) | `color=` | `color` |
| `text.size` | `sizePx` | px | `sizePx` (domain `num:1:2000`) | `size=%gpx` | `font-size` |
| `engine.script` | `script` | internal | T5's classifier only (a CJK run; never on the wire) | `CJK` | class `tsr-cjk`; selects the face |

As built (plan P2-08, structural step): the class bits retired from `Styling`.
Engine code composes a `StyleDelta` (weight, italic, decorations, font role,
baseline, script, size multiplier) where it used to OR bits in; links are no
style (a link is a run of its own); the face is chosen from the font role and
the script. Until the wire change of the same step, the v6–10 `styled.bits`
flags decode onto these rows (`applyLegacyBits`, generated from each row's
`legacy` map), and the dumps keep the bits' spellings.

`tools/gen-schema.mjs` generates from these rows:

- `engine/src/model/props.gen.h`: `struct Styling` (one field per row, in row
  order), `==`, `StylingHash`, `canonicalize` (−0 → +0, NaN → initial),
  `applyStyleArg` (folds a `styled` attribute or a `STYLE_PUSH` patch value),
  `appendStyleFields` (the value part shared by the tree and block dumps; each
  dump keeps its own flag tokens);
- `engine/src/render/style_css.gen.h`: `runCss`, the typeset serializer's run
  declarations in `cssOrder`;
- `runtime/src/shared/props.gen.mjs`: `STYLE_KEYS` (the `$.style.push` /
  `#style` / region keys → attributes) and `STYLE_SUGAR` (boolean keys →
  flag bits), read by the executor instead of three hand-kept key lists.

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

## 4. Not yet

The cascade, block/extent properties, document properties and the settings
codec arrive with plan P1-03 (settings), P3-01 (cascade) and P3-02 (scoped
knobs); the semantic serializer keeps its own property subset until P3-01.
