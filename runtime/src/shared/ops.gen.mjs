// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
export const OPS_VERSION = 6;
export const OPS_MIN_COMPAT = 6;
export const SCHEMA_HASH = '826478ae';
export const SINCE = Object.freeze({
  "op": {
    "1": 6,
    "2": 6,
    "3": 6,
    "4": 6,
    "5": 6,
    "6": 6
  },
  "kind": {
    "0": 6,
    "1": 6,
    "2": 6,
    "3": 6,
    "4": 6,
    "5": 6,
    "6": 6,
    "7": 6,
    "8": 6,
    "9": 6,
    "10": 6,
    "11": 6,
    "12": 6,
    "13": 6,
    "14": 6,
    "15": 6,
    "16": 6,
    "17": 6,
    "18": 6,
    "19": 6,
    "20": 6,
    "21": 6,
    "22": 6,
    "23": 6,
    "24": 6,
    "25": 6,
    "26": 6,
    "27": 6
  },
  "attr": {
    "0": {},
    "1": {},
    "2": {
      "0": 6,
      "1": 6
    },
    "3": {
      "2": 6,
      "3": 6
    },
    "4": {},
    "5": {},
    "6": {
      "4": 6,
      "24": 6,
      "25": 6,
      "26": 6,
      "27": 6
    },
    "7": {},
    "8": {
      "0": 6,
      "6": 6,
      "9": 6
    },
    "9": {
      "0": 6,
      "7": 6,
      "8": 6
    },
    "10": {},
    "11": {},
    "12": {
      "9": 6
    },
    "13": {
      "10": 6,
      "16": 6
    },
    "14": {
      "0": 6,
      "11": 6
    },
    "15": {
      "12": 6,
      "13": 6
    },
    "16": {},
    "17": {},
    "18": {
      "4": 6,
      "20": 6,
      "21": 6,
      "22": 6,
      "23": 6
    },
    "19": {
      "14": 6
    },
    "20": {},
    "21": {
      "15": 6
    },
    "22": {
      "11": 6
    },
    "23": {
      "17": 6,
      "18": 6,
      "19": 6
    },
    "24": {},
    "25": {},
    "26": {
      "11": 6,
      "18": 6,
      "19": 6,
      "28": 6,
      "29": 6,
      "30": 6
    },
    "27": {}
  }
});
export const OP = Object.freeze({
  "MAKE_TEXT": 1,
  "MAKE_NODE": 2,
  "EMIT": 3,
  "STYLE_PUSH": 4,
  "STYLE_POP_TO": 5,
  "SPAN": 6
});
export const KIND = Object.freeze({
  "doc": 0,
  "para": 1,
  "heading": 2,
  "list": 3,
  "item": 4,
  "quote": 5,
  "codeblock": 6,
  "rule": 7,
  "group": 8,
  "table": 9,
  "trow": 10,
  "tcell": 11,
  "term": 12,
  "collect": 13,
  "mathblock": 14,
  "error": 15,
  "comment": 16,
  "text": 17,
  "styled": 18,
  "link": 19,
  "code": 20,
  "ref": 21,
  "mathinline": 22,
  "raw": 23,
  "hardbreak": 24,
  "seq": 25,
  "image": 26,
  "note": 27
});
export const ARGK = Object.freeze({
  "label": 0,
  "level": 1,
  "ordered": 2,
  "start": 3,
  "lang": 4,
  "body": 5,
  "role": 6,
  "cols": 7,
  "align": 8,
  "name": 9,
  "what": 10,
  "src": 11,
  "message": 12,
  "code": 13,
  "url": 14,
  "target": 15,
  "form": 16,
  "html": 17,
  "w": 18,
  "h": 19,
  "bits": 20,
  "font": 21,
  "color": 22,
  "sizePx": 23,
  "wrap": 24,
  "lineNo": 25,
  "hl": 26,
  "sidecar": 27,
  "scale": 28,
  "alt": 29,
  "side": 30
});
export const SCHEMA = Object.freeze({
  "doc": {
    "id": 0,
    "level": "block",
    "body": "blocks",
    "attrs": {}
  },
  "para": {
    "id": 1,
    "level": "block",
    "body": "inline",
    "attrs": {}
  },
  "heading": {
    "id": 2,
    "level": "block",
    "body": "inline",
    "attrs": {
      "level": "int:1:6",
      "label": "label"
    }
  },
  "list": {
    "id": 3,
    "level": "block",
    "body": "items",
    "attrs": {
      "ordered": "bool",
      "start": "int:-1073741824:1073741824"
    }
  },
  "item": {
    "id": 4,
    "level": "block",
    "body": "blocks",
    "attrs": {}
  },
  "quote": {
    "id": 5,
    "level": "block",
    "body": "blocks",
    "attrs": {}
  },
  "codeblock": {
    "id": 6,
    "level": "block",
    "body": "code",
    "attrs": {
      "lang": "token",
      "wrap": "bool",
      "lineNo": "int:0:1048576",
      "hl": "rangeset",
      "sidecar": "str"
    }
  },
  "rule": {
    "id": 7,
    "level": "block",
    "body": "none",
    "attrs": {}
  },
  "group": {
    "id": 8,
    "level": "adaptive",
    "body": "position",
    "attrs": {
      "role": "ident",
      "label": "label",
      "name": "str"
    }
  },
  "table": {
    "id": 9,
    "level": "block",
    "body": "rows",
    "attrs": {
      "cols": "int:1:64",
      "align": "token",
      "label": "label"
    }
  },
  "trow": {
    "id": 10,
    "level": "block",
    "body": "cells",
    "attrs": {}
  },
  "tcell": {
    "id": 11,
    "level": "block",
    "body": "inline",
    "attrs": {}
  },
  "term": {
    "id": 12,
    "level": "adaptive",
    "body": "inline",
    "attrs": {
      "name": "str"
    }
  },
  "collect": {
    "id": 13,
    "level": "block",
    "body": "data",
    "attrs": {
      "what": "enum:toc|glossary|notes|bibliography",
      "form": "enum:all"
    }
  },
  "mathblock": {
    "id": 14,
    "level": "block",
    "body": "none",
    "attrs": {
      "src": "str",
      "label": "label"
    }
  },
  "error": {
    "id": 15,
    "level": "adaptive",
    "body": "none",
    "attrs": {
      "message": "str",
      "code": "ident"
    }
  },
  "comment": {
    "id": 16,
    "level": "trivia",
    "body": "text",
    "attrs": {}
  },
  "text": {
    "id": 17,
    "level": "inline",
    "body": "none",
    "attrs": {}
  },
  "styled": {
    "id": 18,
    "level": "transparent",
    "body": "position",
    "attrs": {
      "bits": "flags:EM=2,BOLD=3,UNDER=16,OVER=17,STRIKE=18",
      "font": "font",
      "lang": "lang",
      "color": "color",
      "sizePx": "num:1:2000"
    }
  },
  "link": {
    "id": 19,
    "level": "inline",
    "body": "inline",
    "attrs": {
      "url": "url"
    }
  },
  "code": {
    "id": 20,
    "level": "inline",
    "body": "text",
    "attrs": {}
  },
  "ref": {
    "id": 21,
    "level": "inline",
    "body": "none",
    "attrs": {
      "target": "str"
    }
  },
  "mathinline": {
    "id": 22,
    "level": "inline",
    "body": "none",
    "attrs": {
      "src": "str"
    }
  },
  "raw": {
    "id": 23,
    "level": "block",
    "body": "none",
    "attrs": {
      "html": "html",
      "w": "num:0:100000",
      "h": "num:0:100000"
    }
  },
  "hardbreak": {
    "id": 24,
    "level": "inline",
    "body": "none",
    "attrs": {}
  },
  "seq": {
    "id": 25,
    "level": "transparent",
    "body": "position",
    "attrs": {}
  },
  "image": {
    "id": 26,
    "level": "block",
    "body": "none",
    "attrs": {
      "src": "url",
      "alt": "str",
      "w": "num:0:100000",
      "h": "num:0:100000",
      "scale": "num:0:100",
      "side": "enum:left|right"
    }
  },
  "note": {
    "id": 27,
    "level": "inline",
    "body": "blocks",
    "attrs": {}
  }
});
