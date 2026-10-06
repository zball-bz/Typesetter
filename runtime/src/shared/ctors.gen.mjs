// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// The constructor specs (plan P2-03; docs/ctor-design.md): kind constructors
// and derived ones, as the binder (runtime/src/shared/stdlib.mjs) reads them.
export const CTOR_SPECS = Object.freeze({
  "para": {
    "kind": "para",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "heading": {
    "kind": "heading",
    "params": [
      {
        "k": "attr",
        "name": "level",
        "dom": "int:1:6"
      },
      {
        "k": "attr",
        "name": "label",
        "dom": "label"
      }
    ],
    "options": [
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "list": {
    "kind": "list",
    "params": [
      {
        "k": "attr",
        "name": "ordered",
        "dom": "bool"
      },
      {
        "k": "attr",
        "name": "start",
        "dom": "int:-1073741824:1073741824"
      }
    ],
    "options": [
      "numbering",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "item": {
    "kind": "item",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "quote": {
    "kind": "quote",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "codeblock": {
    "kind": "codeblock",
    "params": [
      {
        "k": "attr",
        "name": "lang",
        "dom": "token"
      },
      {
        "k": "lines"
      }
    ],
    "options": [
      "wrap",
      "lineNo",
      "hl",
      "sidecar",
      "snapKerning",
      "sidecarFrac",
      "contIndent",
      "features",
      "overlays",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "rule": {
    "kind": "rule",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "group": {
    "kind": "group",
    "params": [],
    "options": [
      "role",
      "label",
      "name",
      "kind",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "table": {
    "kind": "table",
    "params": [
      {
        "k": "body"
      }
    ],
    "options": [
      "cols",
      "align",
      "label",
      "tracks",
      "rules",
      "header",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "row": {
    "kind": "trow",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "cell": {
    "kind": "tcell",
    "params": [],
    "options": [
      "colspan",
      "rowspan",
      "align",
      "valign",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "term": {
    "kind": "term",
    "params": [
      {
        "k": "projected",
        "name": "name",
        "dom": "str"
      }
    ],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "collect": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form",
      "cited",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "mathblock": {
    "kind": "mathblock",
    "params": [
      {
        "k": "attr",
        "name": "src",
        "dom": "str"
      },
      {
        "k": "attr",
        "name": "label",
        "dom": "label"
      }
    ],
    "options": [
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "error": {
    "kind": "error",
    "params": [
      {
        "k": "attr",
        "name": "code",
        "dom": "ident"
      },
      {
        "k": "attr",
        "name": "message",
        "dom": "str"
      }
    ],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": true,
    "derived": false,
    "async": false
  },
  "comment": {
    "kind": "comment",
    "params": [
      {
        "k": "text"
      }
    ],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "text": {
    "kind": "text",
    "params": [
      {
        "k": "text"
      }
    ],
    "options": [],
    "nullary": false,
    "sealed": true,
    "derived": false,
    "async": false
  },
  "styled": {
    "kind": "styled",
    "params": [],
    "options": [
      "font",
      "lang",
      "color",
      "sizePx",
      "weight",
      "italic",
      "decoration",
      "fontRole",
      "baseline",
      "size",
      "hang",
      "parIndent",
      "parAlign",
      "parHyphenate",
      "blockGap",
      "blockIndent",
      "keepWithNext",
      "listMarker",
      "matchKind",
      "matchRole",
      "matchClass",
      "matchLang",
      "matchDepth",
      "matchWhere",
      "snapKerning",
      "sidecarFrac",
      "contIndent",
      "features",
      "overlays",
      "punct",
      "parSingleLine",
      "keep",
      "spaceBefore",
      "spaceAfter",
      "breakBefore",
      "breakAfter",
      "parHang",
      "parHangAfter",
      "boxPadding",
      "boxBorder",
      "boxBorderColor",
      "boxBackground",
      "media",
      "beside",
      "breakerTolerance",
      "breakerStretch",
      "placeFloat",
      "textSpace",
      "textWrap",
      "autospace",
      "hyphens",
      "overflowWrap",
      "placeWidth",
      "placeGap",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "link": {
    "kind": "link",
    "params": [
      {
        "k": "attr",
        "name": "url",
        "dom": "url"
      }
    ],
    "options": [
      "target",
      "to",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "code": {
    "kind": "code",
    "params": [
      {
        "k": "text"
      }
    ],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "ref": {
    "kind": "ref",
    "params": [
      {
        "k": "attr",
        "name": "target",
        "dom": "str"
      }
    ],
    "options": [
      "form",
      "supplement",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "mathinline": {
    "kind": "mathinline",
    "params": [
      {
        "k": "attr",
        "name": "src",
        "dom": "str"
      }
    ],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "raw": {
    "kind": "raw",
    "params": [
      {
        "k": "attr",
        "name": "html",
        "dom": "html"
      }
    ],
    "options": [
      "w",
      "h",
      "measure",
      "minWidth",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "seq": {
    "kind": "seq",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": true,
    "derived": false,
    "async": false
  },
  "image": {
    "kind": "image",
    "params": [
      {
        "k": "attr",
        "name": "src",
        "dom": "url"
      }
    ],
    "options": [
      "alt",
      "w",
      "h",
      "scale",
      "side",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "note": {
    "kind": "note",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "field": {
    "kind": "field",
    "params": [
      {
        "k": "attr",
        "name": "name",
        "dom": "str"
      }
    ],
    "options": [
      "of",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "entry": {
    "kind": "entry",
    "params": [],
    "options": [
      "key",
      "sortKey",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "slot": {
    "kind": "slot",
    "params": [
      {
        "k": "attr",
        "name": "name",
        "dom": "str"
      }
    ],
    "options": [
      "or",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "when": {
    "kind": "when",
    "params": [
      {
        "k": "attr",
        "name": "of",
        "dom": "str"
      }
    ],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "each": {
    "kind": "each",
    "params": [
      {
        "k": "attr",
        "name": "of",
        "dom": "str"
      }
    ],
    "options": [
      "sep",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "math": {
    "kind": "math",
    "params": [],
    "options": [
      "display",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "mathsrc": {
    "kind": "mathsrc",
    "params": [
      {
        "k": "attr",
        "name": "src",
        "dom": "str"
      }
    ],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": true,
    "derived": false,
    "async": false
  },
  "equations": {
    "kind": "equations",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "fill": {
    "kind": "fill",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "terms": {
    "kind": "terms",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false,
    "async": false
  },
  "strong": {
    "kind": "styled",
    "params": [],
    "options": [
      "font",
      "lang",
      "color",
      "sizePx",
      "weight",
      "italic",
      "decoration",
      "fontRole",
      "baseline",
      "size",
      "hang",
      "parIndent",
      "parAlign",
      "parHyphenate",
      "blockGap",
      "blockIndent",
      "keepWithNext",
      "listMarker",
      "matchKind",
      "matchRole",
      "matchClass",
      "matchLang",
      "matchDepth",
      "matchWhere",
      "snapKerning",
      "sidecarFrac",
      "contIndent",
      "features",
      "overlays",
      "punct",
      "parSingleLine",
      "keep",
      "spaceBefore",
      "spaceAfter",
      "breakBefore",
      "breakAfter",
      "parHang",
      "parHangAfter",
      "boxPadding",
      "boxBorder",
      "boxBorderColor",
      "boxBackground",
      "media",
      "beside",
      "breakerTolerance",
      "breakerStretch",
      "placeFloat",
      "textSpace",
      "textWrap",
      "autospace",
      "hyphens",
      "overflowWrap",
      "placeWidth",
      "placeGap",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "em": {
    "kind": "styled",
    "params": [],
    "options": [
      "font",
      "lang",
      "color",
      "sizePx",
      "weight",
      "italic",
      "decoration",
      "fontRole",
      "baseline",
      "size",
      "hang",
      "parIndent",
      "parAlign",
      "parHyphenate",
      "blockGap",
      "blockIndent",
      "keepWithNext",
      "listMarker",
      "matchKind",
      "matchRole",
      "matchClass",
      "matchLang",
      "matchDepth",
      "matchWhere",
      "snapKerning",
      "sidecarFrac",
      "contIndent",
      "features",
      "overlays",
      "punct",
      "parSingleLine",
      "keep",
      "spaceBefore",
      "spaceAfter",
      "breakBefore",
      "breakAfter",
      "parHang",
      "parHangAfter",
      "boxPadding",
      "boxBorder",
      "boxBorderColor",
      "boxBackground",
      "media",
      "beside",
      "breakerTolerance",
      "breakerStretch",
      "placeFloat",
      "textSpace",
      "textWrap",
      "autospace",
      "hyphens",
      "overflowWrap",
      "placeWidth",
      "placeGap",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": false,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "style": {
    "kind": "styled",
    "params": [],
    "options": "raw",
    "nullary": false,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "figure": {
    "kind": "group",
    "params": [
      {
        "k": "body"
      }
    ],
    "options": "raw",
    "nullary": false,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "toc": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form",
      "cited",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "glossary": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form",
      "cited",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "lof": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form",
      "cited",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "lot": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form",
      "cited",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "index": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form",
      "cited",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "notes": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form",
      "cited",
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "pagebreak": {
    "kind": "group",
    "params": [],
    "options": [
      "role",
      "label",
      "name",
      "kind",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "linebreak": {
    "kind": "hardbreak",
    "params": [],
    "options": [
      "label",
      "role",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style",
      "attach"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "attach": {
    "kind": "group",
    "params": [
      {
        "k": "attr",
        "name": "attach",
        "dom": "enum:prev|next|both"
      }
    ],
    "options": [
      "role",
      "label",
      "name",
      "kind",
      "slot",
      "syn",
      "copy",
      "class",
      "ext",
      "style"
    ],
    "nullary": false,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "bibliography": {
    "kind": "collect",
    "params": [
      {
        "k": "attr",
        "name": "src",
        "dom": "str"
      }
    ],
    "options": "raw",
    "nullary": false,
    "sealed": false,
    "derived": true,
    "async": true
  },
  "counterUpdate": {
    "kind": "event",
    "params": [
      {
        "k": "attr",
        "name": "counter",
        "dom": "ident"
      }
    ],
    "options": "raw",
    "nullary": false,
    "sealed": false,
    "derived": true,
    "async": false
  },
  "node": {
    "kind": null,
    "params": [
      {
        "k": "attr",
        "name": "kind",
        "dom": "ident"
      }
    ],
    "options": "raw",
    "nullary": false,
    "sealed": true,
    "derived": true,
    "async": false
  }
});
export const STD_ALIASES = Object.freeze({
  "float": "side",
  "width": "w",
  "height": "h"
});
export const STD_FUNCTIONS = Object.freeze([
  "val",
  "m",
  "plain",
  "use"
]);
export const STD_NAMES = Object.freeze([
  "attach",
  "bibliography",
  "cell",
  "code",
  "codeblock",
  "collect",
  "comment",
  "counterUpdate",
  "each",
  "em",
  "entry",
  "equations",
  "error",
  "field",
  "figure",
  "fill",
  "glossary",
  "group",
  "heading",
  "image",
  "index",
  "item",
  "linebreak",
  "link",
  "list",
  "lof",
  "lot",
  "m",
  "math",
  "mathblock",
  "mathinline",
  "mathsrc",
  "node",
  "note",
  "notes",
  "pagebreak",
  "para",
  "plain",
  "quote",
  "raw",
  "ref",
  "row",
  "rule",
  "seq",
  "slot",
  "strong",
  "style",
  "styled",
  "table",
  "term",
  "terms",
  "text",
  "toc",
  "use",
  "val",
  "when"
]);
