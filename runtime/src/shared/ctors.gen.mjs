// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// The constructor specs (plan P2-03; docs/ctor-design.md): kind constructors
// and derived ones, as the binder (runtime/src/shared/stdlib.mjs) reads them.
export const CTOR_SPECS = Object.freeze({
  "para": {
    "kind": "para",
    "params": [],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "item": {
    "kind": "item",
    "params": [],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "quote": {
    "kind": "quote",
    "params": [],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
      "sidecar"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "rule": {
    "kind": "rule",
    "params": [],
    "options": [],
    "nullary": true,
    "sealed": false,
    "derived": false
  },
  "group": {
    "kind": "group",
    "params": [],
    "options": [
      "role",
      "label",
      "name"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false
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
      "label"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "row": {
    "kind": "trow",
    "params": [],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "cell": {
    "kind": "tcell",
    "params": [],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "collect": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": true,
    "derived": false
  },
  "comment": {
    "kind": "comment",
    "params": [
      {
        "k": "text"
      }
    ],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "derived": false
  },
  "styled": {
    "kind": "styled",
    "params": [],
    "options": [
      "bits",
      "font",
      "lang",
      "color",
      "sizePx"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "code": {
    "kind": "code",
    "params": [
      {
        "k": "text"
      }
    ],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
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
      "h"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "seq": {
    "kind": "seq",
    "params": [],
    "options": [],
    "nullary": false,
    "sealed": true,
    "derived": false
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
      "side"
    ],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "note": {
    "kind": "note",
    "params": [],
    "options": [],
    "nullary": false,
    "sealed": false,
    "derived": false
  },
  "strong": {
    "kind": "styled",
    "params": [],
    "options": [
      "bits",
      "font",
      "lang",
      "color",
      "sizePx"
    ],
    "nullary": false,
    "sealed": false,
    "derived": true
  },
  "em": {
    "kind": "styled",
    "params": [],
    "options": [
      "bits",
      "font",
      "lang",
      "color",
      "sizePx"
    ],
    "nullary": false,
    "sealed": false,
    "derived": true
  },
  "style": {
    "kind": "styled",
    "params": [],
    "options": "raw",
    "nullary": false,
    "sealed": false,
    "derived": true
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
    "derived": true
  },
  "toc": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true
  },
  "glossary": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true
  },
  "notes": {
    "kind": "collect",
    "params": [],
    "options": [
      "what",
      "form"
    ],
    "nullary": true,
    "sealed": false,
    "derived": true
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
    "derived": true
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
    "derived": true
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
  "plain"
]);
export const STD_NAMES = Object.freeze([
  "bibliography",
  "cell",
  "code",
  "codeblock",
  "collect",
  "comment",
  "em",
  "error",
  "figure",
  "glossary",
  "group",
  "heading",
  "image",
  "item",
  "link",
  "list",
  "m",
  "mathblock",
  "mathinline",
  "node",
  "note",
  "notes",
  "para",
  "plain",
  "quote",
  "raw",
  "ref",
  "row",
  "rule",
  "seq",
  "strong",
  "style",
  "styled",
  "table",
  "term",
  "text",
  "toc",
  "val"
]);
