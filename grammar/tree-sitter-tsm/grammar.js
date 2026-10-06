// tree-sitter-tsm — highlighting grammar for the Typesetter markup language.
// Deliberately line-oriented and approximate: the engine is the authority on
// .tsm (its tokens highlight ```tsm blocks and the VS Code editor); this
// grammar is the editor's cold-start fallback. Its delimiters, character
// classes and limits come from the engine's syntax table through
// syntax-regex.js (plan P1-09); regenerate with `node tools/gen-grammars.mjs`.
// Block markers lex as whole-line tokens; inline tokens are regex
// approximations with a word rule keeping identifiers (foo_bar) intact.
const R = require('./syntax-regex.js');
const re = (src) => new RegExp(src);

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
      $.code_block,
      $.paragraph_line,
    ),

    _newline: () => token(/\r?\n/),

    // = 标题 … <label>   (whole line; trailing label captured separately)
    heading: ($) => prec.right(seq(
      field('marker', $.heading_marker),
      repeat($._inline),
    )),
    heading_marker: () => token(prec(3, re(R.headingMarker))),

    rule_line: () => token(prec(3, re(R.ruleLine))),

    // #!name(args…) — args may span lines; approximate as the rest of line
    region_open: () => token(prec(3, re(`${R.regionOpen}(${R.regionArgs})?[ \\t]*`))),
    region_close: () => token(prec(3, re(`${R.regionClose}[ \\t]*`))),

    // #let … statement lines
    code_statement: () => token(prec(2, re(R.statement))),
    // #{ … } statement blocks (one token, as the engine colors them)
    code_block: () => token(prec(2, re(R.stmtBlock))),

    // ``` fences: opener with info, body lines opaque, closer
    fenced_block: ($) => seq(
      field('open', $.fence_delim),
      token(/\r?\n/),
      repeat(seq(optional($.fence_content), token(/\r?\n/))),
      field('close', $.fence_delim),
    ),
    fence_delim: () => token(prec(4, re(R.fenceDelim))),
    fence_content: () => token(prec(1, /[^`\n][^\n]*|`[^`][^\n]*|`/)),

    block_comment: () => token(prec(4, re(R.comment))),

    list_line: ($) => prec.right(seq(
      field('marker', $.list_marker),
      repeat($._inline),
    )),
    list_marker: () => token(prec(3, re(R.listMarker))),
    quote_line: ($) => prec.right(seq(
      field('marker', $.quote_marker),
      repeat($._inline),
    )),
    quote_marker: () => token(prec(3, re(R.quoteMarker))),

    paragraph_line: ($) => prec.right(repeat1($._inline)),

    _inline: ($) => choice(
      $.code_span,
      $.math_span,
      $.footnote,
      $.strong,
      $.emphasis,
      $.link,
      $.autolink,
      $.reference,
      $.label,
      $.keyword_form,
      $.splice,
      $.cell_bar,
      $.word,
      $.punct,
    ),

    code_span: () => token(re(R.code)),
    math_span: () => token(re(R.math)),
    // ^[…] footnote sugar (notes-design.md §1); body approximated as opaque
    footnote: () => token(prec(1, re(R.note))),
    strong: () => token(re(R.strong)),
    emphasis: () => token(re(R.em)),
    link: () => token(re(R.linkText + R.linkUrl)),
    autolink: () => token(prec(1, re(R.url))),  // (plan P3-33) a bare http(s) URL
    reference: () => token(re(R.reference)),
    label: () => token(re(R.label)),
    // #name.head(...)  #(expr)  #toc — the head, then its JS arguments
    // (approximated to one line, parentheses nested two deep)
    splice: ($) => prec.right(seq($.splice_head, optional($.splice_args))),
    splice_head: () => token(re(R.splice)),
    splice_args: () => token.immediate(re(R.spliceArgs)),
    // #if (c) [ … ] else [ … ], #for (h) [ … ], #while (c) [ … ]: the head
    // and its JS (the bodies are inline content; `else` is a word here)
    keyword_form: ($) => prec.right(seq($.keyword_head, optional($.splice_args))),
    keyword_head: () => token(prec(1, re(R.keywordHead))),
    cell_bar: () => token('|'),

    // (plan P3-33, PrevIdent) an '@' right after a word is text (user@host):
    // the word takes it (no lookbehind here; the longest token wins)
    word: () => token(prec(-1, /[A-Za-z0-9_]+(@[A-Za-z0-9_.-]*)?/)),
    punct: () => token(prec(-2, /[^\sA-Za-z0-9_]|[ \t]+/)),
  },
});
