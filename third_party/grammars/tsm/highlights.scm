; tree-sitter-tsm highlights — the 14-tag contract (code-design.md §5,
; kTokenTags / tokens.mjs TAGS). Kept in sync by hand with the engine.
; headings color per CHILD token rather than as one whole-line capture:
; the fold contract (start asc, patternIndex asc, earlier wins on overlap)
; cannot nest, so a whole-line capture would swallow inline labels and
; references — `== 标题 <label>` keeps its label colored this way
(heading (word) @function)
(heading (punct) @function)
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
(footnote) @attribute
(strong) @attribute
(emphasis) @attribute
(link) @property
(cell_bar) @operator
