// GENERATED from engine/src/resource/resources.def by tools/gen-res.mjs — do not edit.
// The resource kinds and their wire columns (docs/host-protocol-design.md §5).
export const RES_VERSION = 1;
export const RES_KINDS = {
  "textWidth": {
    "id": 1,
    "cache": "Content",
    "docProviders": false,
    "key": [
      {
        "name": "mk",
        "type": "MetricKey"
      },
      {
        "name": "text",
        "type": "Str"
      }
    ],
    "ans": [
      {
        "name": "px",
        "type": "F64"
      }
    ]
  },
  "fontVmet": {
    "id": 2,
    "cache": "Content",
    "docProviders": false,
    "key": [
      {
        "name": "mk",
        "type": "MetricKey"
      }
    ],
    "ans": [
      {
        "name": "asc",
        "type": "F64"
      },
      {
        "name": "desc",
        "type": "F64"
      }
    ]
  },
  "fontFace": {
    "id": 3,
    "cache": "None",
    "docProviders": false,
    "key": [
      {
        "name": "family",
        "type": "Str"
      },
      {
        "name": "src",
        "type": "Str"
      },
      {
        "name": "weight",
        "type": "U16"
      },
      {
        "name": "style",
        "type": "U8"
      }
    ],
    "ans": [
      {
        "name": "status",
        "type": "U8"
      }
    ]
  },
  "codeTokens": {
    "id": 4,
    "cache": "Content",
    "docProviders": true,
    "key": [
      {
        "name": "lang",
        "type": "Str"
      },
      {
        "name": "text",
        "type": "Str"
      }
    ],
    "ans": [
      {
        "name": "runs",
        "type": "U32List"
      }
    ]
  },
  "boxInfo": {
    "id": 5,
    "cache": "Host",
    "docProviders": true,
    "key": [
      {
        "name": "kind",
        "type": "U8"
      },
      {
        "name": "ref",
        "type": "Str"
      },
      {
        "name": "availPx",
        "type": "F64"
      }
    ],
    "ans": [
      {
        "name": "w",
        "type": "F64"
      },
      {
        "name": "h",
        "type": "F64"
      },
      {
        "name": "baseline",
        "type": "F64"
      }
    ]
  }
};
export const RES_BY_ID = Object.fromEntries(Object.entries(RES_KINDS).map(([name, k]) => [k.id, { name, ...k }]));
