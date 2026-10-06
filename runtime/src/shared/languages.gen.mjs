// GENERATED from engine/schema/languages.json by tools/gen-languages.mjs — do not edit.
// The code-highlight manifest's tables (plan P3-22): read by hl-core.mjs, the
// worker's token provider, the editor and the stdlib (fence profiles).
// LANGUAGES: a canonical language → its grammar asset basename
// (runtime/assets/hl: tree-sitter-<asset>.wasm, <asset>.scm). LANG_OF: a
// fence tag (lowercased: a name, an alias, a profile) → its language.
// CAPTURE_ALIAS: a capture's first segment → its class. EDITOR_TYPES: a
// class → the editor's semantic token type (null: plain). OVERLAYS and
// PROFILES as in the manifest.
export const LANGUAGES = Object.freeze({"json":{"asset":"json"},"javascript":{"asset":"javascript"},"typescript":{"asset":"typescript"},"python":{"asset":"python"},"cpp":{"asset":"cpp"},"rust":{"asset":"rust"},"tsm":{"asset":"tsm"}});
export const LANG_OF = Object.freeze({"json":"json","javascript":"javascript","js":"javascript","mjs":"javascript","typescript":"typescript","ts":"typescript","python":"python","py":"python","cpp":"cpp","c++":"cpp","cc":"cpp","c":"cpp","rust":"rust","rs":"rust","tsm":"tsm","cpp-literate":"cpp"});
export const CAPTURE_ALIAS = Object.freeze({"tag":"type","conditional":"keyword","repeat":"keyword","include":"keyword","boolean":"constant","constructor":"constant","method":"function","field":"property","parameter":"property"});
export const EDITOR_TYPES = Object.freeze({"keyword":"keyword","string":"string","number":"number","comment":"comment","function":"function","type":"type","constant":"enumMember","variable":"variable","operator":"operator","punctuation":"operator","property":"property","attribute":"decorator","label":"label","embedded":null});
export const OVERLAYS = Object.freeze({"noweb":{"open":"<<","close":">>","forbid":"<>\n","suffix":["+=","="],"class":"label"}});
export const PROFILES = Object.freeze({"cpp-literate":{"lang":"cpp","overlays":["noweb"]}});
