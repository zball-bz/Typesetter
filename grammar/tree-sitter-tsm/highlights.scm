; tree-sitter-tsm highlights — the 14-tag contract (code-design.md §5,
; kTokenTags / tokens.mjs TAGS). Kept in sync by hand with the engine.
; whole-line heading capture comes FIRST: the fold contract (start asc,
; patternIndex asc, earlier wins on overlap) lets it own the line, markdown
; convention — inner labels/markers yield to it
(heading) @function
(heading_marker) @keyword
(list_marker) @keyword
(quote_marker) @keyword
(rule_line) @punctuation
(fence_delim) @keyword
(fence_content) @embedded
(block_comment) @comment
(region_open) @function
(region_close) @function
(code_statement) @function
(splice) @function
(reference) @constant
(label) @label
(code_span) @string
(math_span) @type
(strong) @attribute
(emphasis) @attribute
(link) @property
(cell_bar) @operator
