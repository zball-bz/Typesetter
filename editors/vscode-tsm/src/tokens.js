// Cold-start semantic tokens (plan P1-09): until the engine has loaded in the
// extension host (src/engine.js, the authority), the tree-sitter grammar
// (grammar/tree-sitter-tsm via runtime/assets/hl) colors the first paint.
// A capture's class and the priority contract are the runtime's hl-core
// (plan P3-22: one copy, shared with the worker, twinned natively); the
// class → token type table is generated from engine/schema/languages.json.
// Indices here are UTF-16 code units (tree-sitter node indices), which is
// exactly what VSCode wants.
const path = require('node:path');
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');
const { TYPE_OF, LEGEND } = require('./hl.gen.js');

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
    const core = await import(pathToFileURL(path.join(assetRoot, 'runtime', 'src', 'shared', 'hl-core.mjs')).href);
    return { mod, lang, query, core };
  })());
}

// → [{ s, e, type }] non-overlapping, ascending, UTF-16 indices
async function tsmTokens(assetRoot, text) {
  const { mod, lang, query, core } = await load(assetRoot);
  const parser = new mod.Parser();
  parser.setLanguage(lang);
  const tree = parser.parse(text);
  const caps = [];
  for (const m of query.matches(tree.rootNode)) {
    for (const c of m.captures) {
      const tag = core.tagOf(c.name);
      const type = tag >= 0 ? TYPE_OF[core.TOKEN_TAGS[tag]] : null;
      if (!type) continue;
      caps.push({ s: c.node.startIndex, e: c.node.endIndex, pat: m.patternIndex, type });
    }
  }
  const out = core.resolveCaptures(caps).map(({ s, e, type }) => ({ s, e, type }));
  tree.delete();
  parser.delete();
  return out;
}

module.exports = { tsmTokens, LEGEND, TYPE_OF };
