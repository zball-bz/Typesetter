// Semantic tokens from the SAME grammar the engine highlights .tsm with
// (grammar/tree-sitter-tsm via runtime/assets/hl) — one grammar, two
// consumers. Captures follow the engine's priority contract (start asc,
// patternIndex asc, earlier pattern wins on overlap), so editor coloring
// matches published pages. Indices here are UTF-16 code units (tree-sitter
// node indices), which is exactly what VSCode wants.
const path = require('node:path');
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');

// capture head → VSCode standard semantic token type (null = leave plain)
const TYPE_OF = {
  keyword: 'keyword', string: 'string', number: 'number', comment: 'comment',
  function: 'function', type: 'type', constant: 'enumMember',
  variable: 'variable', operator: 'operator', punctuation: 'operator',
  property: 'property', attribute: 'decorator', label: 'label',
  embedded: null,
};
const LEGEND = ['keyword', 'string', 'number', 'comment', 'function', 'type',
                'enumMember', 'variable', 'operator', 'property', 'decorator',
                'label'];

let loading = null;
function load(assetRoot) {
  return (loading ??= (async () => {
    const hl = path.join(assetRoot, 'runtime', 'assets', 'hl');
    const mod = await import(pathToFileURL(path.join(hl, 'web-tree-sitter.js')).href);
    await mod.Parser.init({
      locateFile: () => path.join(hl, 'web-tree-sitter.wasm'),
    });
    const lang = await mod.Language.load(path.join(hl, 'tree-sitter-tsm.wasm'));
    const query = new mod.Query(lang, fs.readFileSync(path.join(hl, 'tsm.scm'), 'utf8'));
    return { mod, lang, query };
  })());
}

// → [{ s, e, type }] non-overlapping, ascending, UTF-16 indices
async function tsmTokens(assetRoot, text) {
  const { mod, lang, query } = await load(assetRoot);
  const parser = new mod.Parser();
  parser.setLanguage(lang);
  const tree = parser.parse(text);
  const caps = [];
  for (const m of query.matches(tree.rootNode)) {
    for (const c of m.captures) {
      const type = TYPE_OF[c.name.split('.')[0]];
      if (!type) continue;
      caps.push({ s: c.node.startIndex, e: c.node.endIndex, pat: m.patternIndex, type });
    }
  }
  caps.sort((a, b) => a.s - b.s || a.pat - b.pat);
  const out = [];
  let covered = 0;
  for (const c of caps) {
    if (c.s < covered || c.e <= c.s) continue; // earlier pattern won
    out.push({ s: c.s, e: c.e, type: c.type });
    covered = c.e;
  }
  tree.delete();
  parser.delete();
  return out;
}

module.exports = { tsmTokens, LEGEND };
