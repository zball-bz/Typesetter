# Math font test data (plan P5-01)

`.tsmf` files are math-font metrics compiled by `tools/mathc.py --tsmf`
(math-design §15). They hold the MATH table's constants, glyph metrics,
variant chains and assembly parts. They contain no glyph outlines.

| file | what | source |
|---|---|---|
| `stix-two-math.tsmf` | STIX Two Math 2.13 b171, name `stix-two-math`, compiled with `--lenient` (its unencoded size variants dropped) | `STIXTwoMath-Regular.otf` |
| `euler-math.tsmf` | Euler Math, name `euler-copy`: the embedded font's own tables, used for the codec's round-trip test (`unitMathFontFiles`) | `fonts/Euler-Math.otf` |
| `truncated.tsmf` | the first 600 bytes of `stix-two-math.tsmf`, a malformed input | — |

Regenerate them with:

```
python3 tools/mathc.py --font STIXTwoMath-Regular.otf --family "STIX Two Math" \
  --woff2 /tmp/stix.woff2 --out '' --manifest '' --lenient \
  --tsmf test/math/stix-two-math.tsmf --name stix-two-math
python3 tools/mathc.py --woff2 /tmp/euler.woff2 --out '' --manifest '' \
  --tsmf test/math/euler-math.tsmf --name euler-copy
head -c 600 test/math/stix-two-math.tsmf > test/math/truncated.tsmf
```

## Licences

Both fonts are licensed under the SIL Open Font License, Version 1.1
(https://openfontlicense.org). The metrics derived from them here are
distributed under the same licence.

- STIX Two Math: Copyright 2001-2021 The STIX Fonts Project Authors
  (https://github.com/stipub/stixfonts).
- Euler Math 0.75 (designer Hermann Zapf): Copyright (c) 1997, 2009
  American Mathematical Society; (c) 2009, 2021 Khaled Hosny; (c)
  2022-2026 Daniel Flipo (CTAN `euler-math`). It is the font bundled in
  `fonts/` (math-design §1).
