// Token provider (code-design.md §2): web-tree-sitter + per-language side
// modules, lazily loaded, all compute in wasm. The languages, their aliases
// and the capture classes are engine/schema/languages.json's; a capture's
// class and the priority contract are hl-core's, shared with the editor and
// twinned natively (engine/src/code/native_tokens.cc). Overlays (noweb
// fragments) are the engine's (plan P3-22): the text this sees has them
// blanked already.
import { POLICY } from '../shared/settings.gen.mjs';
import { LANGUAGES } from '../shared/languages.gen.mjs';
import { languageOf, resolveCaptures, tagOf } from '../shared/hl-core.mjs';

const HL_BASE = new URL('../../assets/hl/', import.meta.url);
// Node (static export, pages-design.md §3): web-tree-sitter resolves asset
// strings through fs, not fetch — hand it filesystem paths there.
const IS_NODE = typeof process !== 'undefined' && !!process.versions?.node;
let tsMod = null;   // web-tree-sitter module (lazy)
let initDone = null;
// name → {lang, query} | {failedAt}: a failed grammar load (missing asset,
// network) is retried after LOAD_RETRY_MS instead of never (plan P0-11)
const langs = new Map();
const LOAD_RETRY_MS = POLICY.grammarRetryMs;
const now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

// (plan P5-02) one load per language at a time: the provider and a
// prefetch share it
const inflight = new Map();
function load(name) {
  const known = langs.get(name);
  if (known && !('failedAt' in known)) return Promise.resolve(known);
  if (known && now() - known.failedAt < LOAD_RETRY_MS) return Promise.resolve(null);
  if (!inflight.has(name)) inflight.set(name, loadNow(name).finally(() => inflight.delete(name)));
  return inflight.get(name);
}
async function loadNow(name) {
  let entry = null;
  try {
    const asRef = IS_NODE
      ? (await import('node:url')).fileURLToPath
      : (u) => u.href;
    const langUrl = new URL(`tree-sitter-${name}.wasm`, HL_BASE);
    const scmUrl = new URL(`${name}.scm`, HL_BASE);
    // (plan P5-02) the language's grammar and query are fetched while the
    // runtime starts, not after it (a browser: three requests at once)
    const fetched = (u, as) => fetch(u).then((r) => {
      if (!r.ok) throw new Error(`${u}: ${r.status}`);
      return as === 'text' ? r.text() : r.arrayBuffer().then((b) => new Uint8Array(b));
    });
    const grammar = IS_NODE ? null : fetched(langUrl, 'bytes');
    const query = IS_NODE
      ? (await import('node:fs/promises')).readFile(asRef(scmUrl), 'utf8')
      : fetched(scmUrl, 'text');
    grammar?.catch(() => {});  // (awaited below; a failure surfaces there)
    query.catch(() => {});
    if (!tsMod) tsMod = await import(new URL('../../assets/hl/web-tree-sitter.js', import.meta.url));
    if (!initDone) initDone = tsMod.Parser.init({
      locateFile: () => asRef(new URL('web-tree-sitter.wasm', HL_BASE)),
    }).catch((e) => { initDone = null; throw e; });  // a failed init retries too
    await initDone;
    const lang = await tsMod.Language.load(grammar ? await grammar : asRef(langUrl));
    entry = { lang, query: new tsMod.Query(lang, await query) };
  } catch {
    // missing asset / load failure → plain code, never a stall
    langs.set(name, { failedAt: now() });
    return null;
  }
  langs.set(name, entry);
  return entry;
}

// (plan P5-02) a language a document's fences name, loaded ahead of the
// provider's request (worker.mjs: right after compile, from the engine's
// codelangs product) — the highlighter's start overlaps the document's
// execution and first passes; an unknown tag is nothing
export function prefetch(langTag) {
  const name = languageOf(langTag);
  if (name && name in LANGUAGES) load(LANGUAGES[name].asset).catch(() => {});
}

// web-tree-sitter node indices are UTF-16 code units (JS string offsets);
// the engine folds by UTF-8 BYTE — build the mapping once per body
// (identity fast-path for pure-ASCII text).
function u16ToU8Map(text) {
  if (!/[\u0080-\uffff]/.test(text)) return null;  // ASCII: identity
  const map = new Uint32Array(text.length + 1);
  let bytes = 0;
  for (let i = 0; i < text.length; ) {
    map[i] = bytes;
    const cp = text.codePointAt(i);
    const units = cp > 0xffff ? 2 : 1;
    if (units === 2) map[i + 1] = bytes;  // low surrogate → same start
    bytes += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    i += units;
  }
  map[text.length] = bytes;
  return map;
}

// → flat Uint32Array of (start, end, tagId) triples (possibly empty)
export async function tokenize(langTag, text) {
  const name = languageOf(langTag);
  if (!name || !(name in LANGUAGES)) return new Uint32Array(0);
  const entry = await load(LANGUAGES[name].asset);
  if (!entry) return new Uint32Array(0);
  const u8 = u16ToU8Map(text);  // UTF-8 byte offsets (see above)
  const parser = new tsMod.Parser();
  parser.setLanguage(entry.lang);
  const tree = parser.parse(text);
  const caps = [];
  for (const m of entry.query.matches(tree.rootNode)) {
    for (const c of m.captures) {
      const tag = tagOf(c.name);
      if (tag < 0) continue;
      const s = u8 ? u8[c.node.startIndex] : c.node.startIndex;
      const e = u8 ? u8[c.node.endIndex] : c.node.endIndex;
      caps.push({ s, e, pat: m.patternIndex, tag });
    }
  }
  const out = [];
  for (const c of resolveCaptures(caps)) out.push(c.s, c.e, c.tag);
  tree.delete();
  parser.delete();
  return Uint32Array.from(out);
}
