// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// Host settings (schema "settings"; plan P1-03) and host policy.
export const SETTINGS = Object.freeze({
  "host.width": {
    "dom": "num:1:100000",
    "def": 300,
    "prec": "HostOnly",
    "affects": [
      "Layout",
      "Paint"
    ]
  },
  "host.epsilonSu": {
    "dom": "num:0:64",
    "def": 1,
    "prec": "HostOnly",
    "affects": [
      "Emit",
      "Measure"
    ]
  },
  "doc.lang": {
    "dom": "lang",
    "def": "zh-CN",
    "prec": "HostDefault",
    "affects": [
      "Resolve",
      "Paint"
    ]
  },
  "doc.baseSize": {
    "dom": "num:4:96",
    "def": 18,
    "prec": "HostDefault",
    "affects": [
      "Emit",
      "Measure",
      "Layout",
      "Paint"
    ]
  },
  "doc.leading": {
    "dom": "num:0.5:4",
    "def": 1.5,
    "prec": "HostDefault",
    "affects": [
      "Emit",
      "Layout",
      "Paint"
    ]
  },
  "doc.parGap": {
    "dom": "num:0:10",
    "def": 1.2,
    "prec": "HostDefault",
    "affects": [
      "Layout",
      "Paint"
    ]
  },
  "doc.cjkJustify": {
    "dom": "num:0:4",
    "def": 0.6,
    "prec": "HostDefault",
    "affects": [
      "Emit",
      "Layout"
    ]
  },
  "doc.cjkGlue": {
    "dom": "num:0:1",
    "def": 0.1,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "fonts.body": {
    "dom": "font",
    "def": "\"Crimson Text\", Georgia, serif",
    "prec": "HostDefault",
    "affects": [
      "Measure",
      "Paint"
    ]
  },
  "fonts.cjk": {
    "dom": "font",
    "def": "\"Noto Serif CJK SC\", \"Source Han Serif SC\", \"Songti SC\", SimSun, serif",
    "prec": "HostDefault",
    "affects": [
      "Measure",
      "Paint"
    ]
  },
  "fonts.mono": {
    "dom": "font",
    "def": "monospace",
    "prec": "HostDefault",
    "affects": [
      "Measure",
      "Paint"
    ]
  },
  "fonts.monoCjk": {
    "dom": "font",
    "def": "",
    "prec": "HostDefault",
    "affects": [
      "Measure",
      "Paint"
    ]
  },
  "par.indent": {
    "dom": "num:0:20",
    "def": 0,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "list.indent": {
    "dom": "num:0:20",
    "def": 1.5,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "quote.indent": {
    "dom": "num:0:20",
    "def": 1,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "cjk.punctCompress": {
    "dom": "enum:full|book|none",
    "def": "book",
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "break.hyphenPenalty": {
    "dom": "num:0:1e18",
    "def": 0.7,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "break.urlPenalty": {
    "dom": "num:0:1e18",
    "def": 1.2,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "break.urlMinLen": {
    "dom": "int:1:100000",
    "def": 20,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "break.mathRelAfter": {
    "dom": "num:0:1e18",
    "def": 0.8,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "break.mathRelBefore": {
    "dom": "num:0:1e18",
    "def": 0.85,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "break.mathBinAfter": {
    "dom": "num:0:1e18",
    "def": 0.95,
    "prec": "HostDefault",
    "affects": [
      "Emit"
    ]
  },
  "cost.exponent": {
    "dom": "int:1:4",
    "def": 3,
    "prec": "HostDefault",
    "affects": [
      "Layout"
    ]
  },
  "cost.shrinkThreshold": {
    "dom": "num:0:1",
    "def": 0.37,
    "prec": "HostDefault",
    "affects": [
      "Layout"
    ]
  },
  "cost.shrinkCoeff": {
    "dom": "num:0:100",
    "def": 0.6,
    "prec": "HostDefault",
    "affects": [
      "Layout"
    ]
  },
  "cost.cap": {
    "dom": "num:1:1e12",
    "def": 10000,
    "prec": "HostDefault",
    "affects": [
      "Layout"
    ]
  },
  "code.scale": {
    "dom": "num:0.1:4",
    "def": 0.85,
    "prec": "HostDefault",
    "affects": [
      "Emit",
      "Layout"
    ]
  },
  "code.contIndent": {
    "dom": "int:0:40",
    "def": 2,
    "prec": "HostDefault",
    "affects": [
      "Layout"
    ]
  },
  "code.sidecarFrac": {
    "dom": "num:0.1:0.9",
    "def": 0.4,
    "prec": "HostDefault",
    "affects": [
      "Layout"
    ]
  },
  "code.snapKerning": {
    "dom": "bool",
    "def": false,
    "prec": "HostDefault",
    "affects": [
      "Layout",
      "Paint"
    ]
  },
  "code.fontFeatures": {
    "dom": "features",
    "def": "",
    "prec": "HostDefault",
    "affects": [
      "Paint"
    ]
  },
  "code.fontFeaturesByLang": {
    "dom": "map:features",
    "def": {},
    "prec": "HostDefault",
    "affects": [
      "Paint"
    ]
  },
  "terms.heading": {
    "dom": "str",
    "def": "",
    "prec": "HostDefault",
    "affects": [
      "Resolve"
    ]
  },
  "terms.table": {
    "dom": "str",
    "def": "",
    "prec": "HostDefault",
    "affects": [
      "Resolve"
    ]
  },
  "terms.figure": {
    "dom": "str",
    "def": "",
    "prec": "HostDefault",
    "affects": [
      "Resolve"
    ]
  },
  "terms.equation": {
    "dom": "str",
    "def": "",
    "prec": "HostDefault",
    "affects": [
      "Resolve"
    ]
  },
  "terms.captionSep": {
    "dom": "str",
    "def": "",
    "prec": "HostDefault",
    "affects": [
      "Resolve"
    ]
  },
  "page.height": {
    "dom": "num:16:100000",
    "def": 995,
    "prec": "HostDefault",
    "affects": [
      "Paginate"
    ]
  }
});
export const SETTINGS_DEFAULTS = Object.freeze({
  "host": {
    "width": 300,
    "epsilonSu": 1
  },
  "doc": {
    "lang": "zh-CN",
    "baseSize": 18,
    "leading": 1.5,
    "parGap": 1.2,
    "cjkJustify": 0.6,
    "cjkGlue": 0.1
  },
  "fonts": {
    "body": "\"Crimson Text\", Georgia, serif",
    "cjk": "\"Noto Serif CJK SC\", \"Source Han Serif SC\", \"Songti SC\", SimSun, serif",
    "mono": "monospace",
    "monoCjk": ""
  },
  "par": {
    "indent": 0
  },
  "list": {
    "indent": 1.5
  },
  "quote": {
    "indent": 1
  },
  "cjk": {
    "punctCompress": "book"
  },
  "break": {
    "hyphenPenalty": 0.7,
    "urlPenalty": 1.2,
    "urlMinLen": 20,
    "mathRelAfter": 0.8,
    "mathRelBefore": 0.85,
    "mathBinAfter": 0.95
  },
  "cost": {
    "exponent": 3,
    "shrinkThreshold": 0.37,
    "shrinkCoeff": 0.6,
    "cap": 10000
  },
  "code": {
    "scale": 0.85,
    "contIndent": 2,
    "sidecarFrac": 0.4,
    "snapKerning": false,
    "fontFeatures": "",
    "fontFeaturesByLang": {}
  },
  "terms": {
    "heading": "",
    "table": "",
    "figure": "",
    "equation": "",
    "captionSep": ""
  },
  "page": {
    "height": 995
  }
});
export const LEGACY_OPTIONS = Object.freeze({
  "widthPx": "host.width",
  "lang": "doc.lang",
  "baseSizePx": "doc.baseSize",
  "lineHeight": "doc.leading",
  "fontFamily": "fonts.body",
  "cjkFontFamily": "fonts.cjk",
  "paraIndentEm": "par.indent",
  "punctCompress": "cjk.punctCompress",
  "verbatimSnapKerning": "code.snapKerning",
  "codeFontFeatures": "code.fontFeatures",
  "codeFontFeaturesByLang": "code.fontFeaturesByLang"
});
export const POLICY = Object.freeze({
  "maxRounds": 64,
  "fontDeadlineMs": 4000,
  "fontRetryMs": 30000,
  "grammarRetryMs": 30000,
  "imageTimeoutMs": 15000,
  "tokenCacheEntries": 400,
  "measureCacheWords": 200000,
  "nativeImagePx": [
    512,
    384
  ]
});
export const STAGES = Object.freeze(["Compile","Execute","Ingest","Resolve","Emit","Measure","Layout","Paginate","Paint"]);
// the value of a dotted setting in a (partial) settings document, else its default
export function settingOf(settings, path) {
  const [a, b] = path.split('.');
  const v = settings?.[a]?.[b];
  return v !== undefined ? v : SETTINGS[path]?.def;
}
// one settings document from createEngine's legacy named options (MD-06) and
// an explicit `settings` object, which wins
export function settingsFromOptions(opts = {}) {
  const out = {};
  for (const [opt, path] of Object.entries(LEGACY_OPTIONS)) {
    if (opts[opt] === undefined || opts[opt] === null) continue;
    const [a, b] = path.split('.');
    (out[a] ??= {})[b] = opts[opt];
  }
  for (const [a, sec] of Object.entries(opts.settings ?? {})) {
    if (sec && typeof sec === 'object' && !Array.isArray(sec)) out[a] = { ...(out[a] ?? {}), ...sec };
    else out[a] = sec;
  }
  return out;
}
