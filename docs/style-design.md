# Style properties (design; as built from plan P1-02)

## 1. One vocabulary, declared once

The engine understands a closed set of run properties. Each is one row of the
`props` section of `engine/schema/schema.json` (the shared schema of the ops
contract, design T2/T4):

| row | field | type | patched by | dump | typeset CSS |
|---|---|---|---|---|---|
| `text.bits` | `bits` | flag set (ORed) | `styled.bits` / `STYLE_PUSH` bits; sugar `bold italic underline overline strike` | flag tokens (each dump its own order) | classes; `text-decoration` from UNDER/OVER/STRIKE |
| `text.sizeMul` | `sizeMul` | size multiplier | composed by the engine (headings, code, sup) | `x%.2f` | `font-size` (with `sizePx`, through `emPx`) |
| `text.font` | `fontFamily` | string | `font` (domain `font`) | `font="…"` | `font-family` (escaped) |
| `text.lang` | `lang` | string | `lang` (domain `lang`) | `lang=` | the `lang` attribute |
| `text.color` | `color` | string | `color` (domain `color`) | `color=` | `color` |
| `text.size` | `sizePx` | px | `sizePx` (domain `num:1:2000`) | `size=%gpx` | `font-size` |

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

## 3. Not yet

The cascade, block/extent properties, document properties and the settings
codec arrive with plan P1-03 (settings), P3-01 (cascade) and P3-02 (scoped
knobs); the semantic serializer keeps its own property subset until P3-01.
