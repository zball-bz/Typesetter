// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// The names a hole module may bind from __rt.std (plan P2-03): every
// constructor and std function, sorted.
#pragma once

namespace tsr {

inline constexpr const char* kStdNames[] = {
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
    "text",
    "toc",
    "use",
    "val",
    "when",
};

// the constructors and functions whose result is a promise (plan P2-14:
// they load; P3-31: use): a
// splice that names one awaits it
inline constexpr const char* kStdAsync[] = {
    "bibliography",
    "use",
};

}  // namespace tsr
