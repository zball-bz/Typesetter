// The engine in the extension host (plan P1-09; editor-design.md §5): the
// wasm build the preview uses, loaded once and checked with the ABI
// handshake. .tsm tokens and the outline (headings, regions, fences, labels,
// diagnostics) come from it — tsr_syntax_tokens / tsr_outline — converted
// from the engine's UTF-8 byte offsets to the UTF-16 indices VS Code uses.
// No vscode dependency: test/e2e/editor.spec.mjs drives it from Node.
const path = require('node:path');
const { pathToFileURL } = require('node:url');

let loading = null;
let engine = null;  // set once loaded: { M, TOKEN_TAGS }

function load(assetRoot) {
  return (loading ??= (async () => {
    const at = (...p) => pathToFileURL(path.join(assetRoot, ...p)).href;
    const mod = await import(at('engine', 'build-wasm', 'typesetter.js'));
    const M = await mod.default();
    const { checkAbi } = await import(at('runtime', 'src', 'shared', 'abi.mjs'));
    checkAbi(M);
    const { TOKEN_TAGS } = await import(at('runtime', 'src', 'shared', 'syntax.gen.mjs'));
    engine = { M, TOKEN_TAGS };
    return engine;
  })());
}
const ready = () => engine !== null;

function call(fn, text) {
  const { M } = engine;
  const p = M.stringToNewUTF8(text);
  try {
    return JSON.parse(M.UTF8ToString(M[fn](p)));
  } finally {
    M._free(p);
  }
}

// UTF-8 byte offset → UTF-16 index, for every byte offset of `text`
function byteToUtf16(text) {
  const map = [];
  for (let i = 0; i < text.length; i++) {
    const cp = text.codePointAt(i);
    const n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    for (let k = 0; k < n; k++) map.push(i);
    if (cp > 0xffff) i++;  // a surrogate pair
  }
  map.push(text.length);
  return map;
}

// → [{ s, e, tag }] sorted, non-overlapping, UTF-16 indices (tag: a TOKEN_TAGS name)
function tokens(text) {
  const map = byteToUtf16(text);
  return call('_tsr_syntax_tokens', text).map(([s, e, t]) => ({ s: map[s], e: map[e], tag: engine.TOKEN_TAGS[t] }));
}

// the outline with every span as UTF-16 [s, e]
function outline(text) {
  const map = byteToUtf16(text);
  const o = call('_tsr_outline', text);
  const conv = (x) => ({ ...x, span: [map[x.span[0]], map[x.span[1]]] });
  for (const k of ['headings', 'regions', 'fences', 'labels', 'diagnostics']) o[k] = o[k].map(conv);
  return o;
}

module.exports = { load, ready, tokens, outline, byteToUtf16 };
