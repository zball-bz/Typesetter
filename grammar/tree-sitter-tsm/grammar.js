// tree-sitter-tsm — highlighting grammar for the Typesetter markup language.
// Deliberately line-oriented and approximate: it exists to color .tsm
// sources in code blocks (code-design.md), not to re-implement the engine's
// linepass/inline parser. Block markers lex as whole-line tokens; inline
// tokens are regex approximations with a word rule keeping identifiers
// (foo_bar) intact.
module.exports = grammar({
  name: 'tsm',

  extras: () => [],

  rules: {
    document: ($) => repeat(choice($._block, $._newline)),

    _block: ($) => choice(
      $.fenced_block,
      $.block_comment,
      $.heading,
      $.rule_line,
      $.region_open,
      $.region_close,
      $.list_line,
      $.quote_line,
      $.code_statement,
      $.paragraph_line,
    ),

    _newline: () => token(/\r?\n/),

    // = 标题 … <label>   (whole line; trailing label captured separately)
    heading: ($) => prec.right(seq(
      field('marker', $.heading_marker),
      repeat($._inline),
    )),
    heading_marker: () => token(prec(3, /={1,6} /)),

    rule_line: () => token(prec(3, /-{3,}[ \t]*/)),

    // #!name(args…) — args may span lines; approximate as the rest of line
    region_open: () => token(prec(3, /#![A-Za-z_][A-Za-z0-9_]*(\([^\n]*\))?[ \t]*/)),
    region_close: () => token(prec(3, /#[A-Za-z_][A-Za-z0-9_]*![ \t]*/)),

    // #let … / bare #statement lines (codegen CodeStmt); approximation:
    // a line that IS a splice-with-call keeps inline handling instead
    code_statement: () => token(prec(2, /#let [^\n]*/)),

    // ``` fences: opener with info, body lines opaque, closer
    fenced_block: ($) => seq(
      field('open', $.fence_delim),
      token(/\r?\n/),
      repeat(seq(optional($.fence_content), token(/\r?\n/))),
      field('close', $.fence_delim),
    ),
    fence_delim: () => token(prec(4, /```[^\n]*/)),
    fence_content: () => token(prec(1, /[^`\n][^\n]*|`[^`][^\n]*|`/)),

    block_comment: () => token(prec(4, /%--([^-]|-[^-]|--[^%])*--%/)),

    list_line: ($) => prec.right(seq(
      field('marker', $.list_marker),
      repeat($._inline),
    )),
    list_marker: () => token(prec(3, /([-+]|[0-9]+\.) /)),
    quote_line: ($) => prec.right(seq(
      field('marker', $.quote_marker),
      repeat($._inline),
    )),
    quote_marker: () => token(prec(3, /> ?/)),

    paragraph_line: ($) => prec.right(repeat1($._inline)),

    _inline: ($) => choice(
      $.code_span,
      $.math_span,
      $.footnote,
      $.strong,
      $.emphasis,
      $.link,
      $.reference,
      $.label,
      $.splice,
      $.cell_bar,
      $.word,
      $.punct,
    ),

    code_span: () => token(/`[^`\n]*`/),
    math_span: () => token(/\$[^$\n]*\$/),
    // ^[…] footnote sugar (notes-design.md §1); body approximated as opaque
    footnote: () => token(prec(1, /\^\[[^\]\n]*\]/)),
    strong: () => token(/\*[^*\n]+\*/),
    emphasis: () => token(/_[^_\n]+_/),
    link: () => token(/\[[^\]\n]*\]\([^)\n]*\)/),
    reference: () => token(/@([A-Za-z][A-Za-z0-9_-]*|\[[^\]\n]+\])/),
    label: () => token(/<[A-Za-z][A-Za-z0-9_-]*>/),
    // #name.head(...)  #(expr)  #toc — args approximated to the call parens
    splice: () => token(/#(\(|[A-Za-z_][A-Za-z0-9_.]*)/),
    cell_bar: () => token('|'),

    word: () => token(prec(-1, /[A-Za-z0-9_]+/)),
    punct: () => token(prec(-2, /[^\sA-Za-z0-9_]|[ \t]+/)),
  },
});
