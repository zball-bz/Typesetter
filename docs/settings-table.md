<!-- GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit. -->
# Host settings (generated)

The settings document of document-model §11 / docs/host-protocol-design.md, generated from `engine/schema/schema.json`. `affects` lists the stages whose products a change invalidates; the first one decides whether a patch applies in place or rebuilds the document.

| setting | domain | default | precedence | affects | replaces |
|---|---|---|---|---|---|
| `host.width` | num:1:100000 | `300` | HostOnly | Layout, Paint | `widthPx` |
| `host.dppx` | num:0.25:16 | `1` | HostOnly | Measure |  |
| `host.loadedFaces` | str | `""` | HostOnly | Measure |  |
| `host.epsilonSu` | num:0:64 | `1` | HostOnly | Emit, Measure |  |
| `doc.lang` | lang | `"zh-CN"` | HostDefault | Resolve, Paint | `lang` |
| `doc.baseSize` | num:4:96 | `18` | HostDefault | BoxTree, Emit, Measure, Layout, Paint | `baseSizePx` |
| `doc.leading` | num:0.5:4 | `1.5` | HostDefault | Emit, Layout, Paint | `lineHeight` |
| `doc.parGap` | num:0:10 | `1.2` | HostDefault | Layout, Paint |  |
| `doc.cjkJustify` | num:0:4 | `0.6` | HostDefault | Emit, Layout |  |
| `doc.cjkGlue` | num:0:1 | `0.1` | HostDefault | Emit |  |
| `fonts.body` | font | `"\"Crimson Text\", Georgia, serif"` | HostDefault | Measure, Paint | `fontFamily` |
| `fonts.cjk` | font | `"\"Noto Serif CJK SC\", \"Source Han Serif SC\", \"Songti SC\", SimSun, serif"` | HostDefault | Measure, Paint | `cjkFontFamily` |
| `fonts.mono` | font | `"monospace"` | HostDefault | Measure, Paint |  |
| `fonts.monoCjk` | font | `""` | HostDefault | Measure, Paint |  |
| `par.indent` | num:0:20 | `0` | HostDefault | BoxTree, Emit | `paraIndentEm` |
| `list.indent` | num:0:20 | `1.5` | HostDefault | BoxTree |  |
| `quote.indent` | num:0:20 | `1` | HostDefault | BoxTree |  |
| `cjk.punctCompress` | enum:full\|book\|none | `"book"` | HostDefault | Emit | `punctCompress` |
| `break.hyphenPenalty` | num:0:1e18 | `0.7` | HostDefault | Emit |  |
| `break.urlPenalty` | num:0:1e18 | `1.2` | HostDefault | Emit |  |
| `break.urlMinLen` | int:1:100000 | `20` | HostDefault | Emit |  |
| `break.mathRelAfter` | num:0:1e18 | `0.8` | HostDefault | Emit |  |
| `break.mathRelBefore` | num:0:1e18 | `0.85` | HostDefault | Emit |  |
| `break.mathBinAfter` | num:0:1e18 | `0.95` | HostDefault | Emit |  |
| `cost.exponent` | int:1:4 | `3` | HostDefault | Layout |  |
| `cost.shrinkThreshold` | num:0:1 | `0.37` | HostDefault | Layout |  |
| `cost.shrinkCoeff` | num:0:100 | `0.6` | HostDefault | Layout |  |
| `cost.cap` | num:1:1e12 | `10000` | HostDefault | Layout |  |
| `code.scale` | num:0.1:4 | `0.85` | HostDefault | BoxTree, Emit, Layout |  |
| `code.contIndent` | int:0:40 | `2` | HostDefault | Layout |  |
| `code.sidecarFrac` | num:0.1:0.9 | `0.4` | HostDefault | Layout |  |
| `code.snapKerning` | bool | `false` | HostDefault | Layout, Paint | `verbatimSnapKerning` |
| `code.fontFeatures` | features | `""` | HostDefault | Measure, Paint | `codeFontFeatures` |
| `code.fontFeaturesByLang` | map:features | `{}` | HostDefault | Paint | `codeFontFeaturesByLang` |
| `terms.heading` | str | `""` | HostDefault | Resolve |  |
| `terms.table` | str | `""` | HostDefault | Resolve |  |
| `terms.figure` | str | `""` | HostDefault | Resolve |  |
| `terms.equation` | str | `""` | HostDefault | Resolve |  |
| `terms.captionSep` | str | `""` | HostDefault | Resolve |  |
| `page.height` | num:16:100000 | `995` | HostDefault | Paginate |  |

## Host policy

| policy | default | meaning |
|---|---|---|
| `maxRounds` | `64` | pull-loop rounds before a typeset is declared stalled |
| `fontDeadlineMs` | `4000` | declared fonts measure as their fallback after this |
| `fontRetryMs` | `30000` | a failed font load is retried after this |
| `grammarRetryMs` | `30000` | a failed highlight grammar load is retried after this |
| `imageTimeoutMs` | `15000` | main-thread image size fallback timeout |
| `sessionBudgetBytes` | `67108864` | the worker Session's answer cache and memo budget (content-keyed: widths, vertical metrics, code tokens, KP results) |
| `nativeImagePx` | `[512,384]` | the native/golden image provider's answer |
