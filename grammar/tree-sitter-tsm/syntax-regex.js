// Regex sources for the hand-written tsm grammars — this tree-sitter grammar
// and the VS Code TextMate grammar (tools/gen-grammars.mjs) — derived from
// the engine's syntax table, engine/src/syntax/syntax.def, through
// runtime/src/shared/syntax.gen.json (plan P1-09). Both grammars are
// documented approximations of the engine's lexer (line-oriented regexes);
// the engine's own tokens (tsr_syntax_tokens) are authoritative, and the
// native conformance check holds tree-sitter to them.
const SYN = require('../../runtime/src/shared/syntax.gen.json');

const inline = Object.fromEntries(SYN.inline.map((r) => [r.id, r]));
const block = Object.fromEntries(SYN.block.map((r) => [r.id, r]));
const esc = (s) => s.replace(/[.*+?^${}()|[\]\\/]/g, '\\$&');
const cls = (name) => SYN.classes[name];          // a character-class body, e.g. A-Za-z_
const word = (starter) => starter.split(' ')[0];   // "``` INFO" → "```"
const literal = (s) => s.replace(/[A-Z][A-Z_]+$/, '');  // "#!NAME" → "#!"
// everything up to a multi-character closer c0c1c2: ([^c0]|c0[^c1]|c0c1[^c2])*
const notCloser = (c) =>
  '(' + [...c].map((ch, k) => esc(c.slice(0, k)) + `[^${esc(ch)}]`).join('|') + ')*';

const ident = `[${cls('IdStart')}][${cls('IdCont')}]*`;
const headingMax = Number(/\{1,(\d+)\}/.exec(block.heading.starter)[1]);
const pair = (o) => `${esc(o)}[^${esc(o)}\\n]+${esc(o)}`;
const island = (o) => `${esc(o)}[^${esc(o)}\\n]*${esc(o)}`;

module.exports = {
  version: SYN.version,
  // block lines
  headingMarker: `={1,${headingMax}} `,
  headingMax,
  ruleLine: `${esc(block.rule.starter[0])}{${block.rule.starter.length},}[ \\t]*`,
  regionOpen: `${esc(literal(word(block.region.starter)))}${ident}`,
  regionClose: `#${ident}!`,  // an Explicit container's named closer
  regionArgs: '\\([^\\n]*\\)',
  statement: `${esc(block.let.starter.trimEnd())} [^\\n]*`,
  stmtOpen: esc(block.stmt.starter),
  // #{ … }: closed on its own line, or up to the first line that starts
  // with '}' (a JS block's body is indented)
  stmtBlock: `${esc(block.stmt.starter)}([^\\n]*\\}[ \\t]*|([^\\n]*[^}\\n \\t])?[ \\t]*(\\n([^}\\n][^\\n]*)?)*\\n\\}[ \\t]*)`,
  fenceDelim: `${esc(word(block.fence.starter))}[^\\n]*`,
  fenceRun: `${esc(word(block.fence.starter)[0])}{${word(block.fence.starter).length},}`,
  quoteMarker: `${esc(block.quote.starter.trimEnd())} ?`,
  // (plan P3-34) `/ ` a description item's (its term up to `: ` is text here)
  listMarker: '([-+/]|[0-9]+\\.) ',
  comment: `${esc(inline.comment.open)}${notCloser(inline.comment.close)}${esc(inline.comment.close)}`,
  commentOpen: esc(inline.comment.open),
  commentClose: esc(inline.comment.close),
  // inline atoms and pairs
  code: island(inline.code.open),
  math: island(inline.math.open),
  note: `${esc(inline.note.open)}[^\\]\\n]*${esc(inline.note.close)}`,
  strong: pair(inline.strong.open),
  em: pair(inline.em.open),
  linkText: `${esc(inline.link.open)}[^\\]\\n]*\\]`,
  // (plan P3-33) a bare http(s) URL (syntax.def url: SCHEME "://" URL_END):
  // ASCII without blanks and < > " ` | \ $; it never ends in . , : ; ! ? '
  // * _ ~ or a closing bracket (the engine keeps a balanced one: a known
  // approximation)
  url: `[Hh][Tt][Tt][Pp][Ss]?${esc(literal(inline.url.open.replace(/^[A-Z]+/, '')))}` +
    '[!#%&\'()*+,\\-./0-9:;=?@A-Z\\[\\]^_a-z{}~]*[#%&(+\\-/0-9=@A-Z\\[^a-z{}]',
  linkUrl: '\\([^)\\n]*\\)',
  // @id, or @[ids] (the refs opener after the shared '@')
  reference: `${esc(inline.ref.open)}([${cls('SpliceHead')}][${cls('SpliceCont')}-]*|` +
    `${esc(inline.refs.open.slice(inline.ref.open.length))}[^\\]\\n]+\\])`,
  label: `<[${cls('IdStart')}][${cls('IdCont')}-]*>`,
  splice: `${esc(literal(inline.splice.open))}(\\(|[${cls('SpliceHead')}][${cls('SpliceCont')}.]*)`,
  // a keyword form's head (#if, #for, #while: plan P2-12) and its else link
  keywordHead: `${esc(literal(inline.splice.open))}(${SYN.keywords.map((k) => k.keyword).join('|')})`,
  // a splice's JS arguments, on one line, parentheses nested two deep
  spliceArgs: '\\(([^()\\n]|\\(([^()\\n]|\\([^()\\n]*\\))*\\))*\\)',
};
