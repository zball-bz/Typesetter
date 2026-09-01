// Token provider (code-design.md §2): web-tree-sitter + per-language side
// modules, lazily loaded, all compute in wasm. Priority contract (shared
// with engine/test/native_tokens.h): captures sort by (start asc,
// patternIndex asc); earlier pattern wins on overlap.
// Tag set mirrors engine/src/code/tokens.h kTokenTags — keep in sync.
const TAGS = ['keyword', 'string', 'number', 'comment', 'function', 'type',
              'constant', 'variable', 'operator', 'punctuation', 'property',
              'attribute', 'label', 'embedded'];
const ALIAS = { tag: 'type', conditional: 'keyword', repeat: 'keyword',
                include: 'keyword', boolean: 'constant', constructor: 'constant',
                method: 'function', field: 'property', parameter: 'property' };
function tagOf(name) {
  const head = name.split('.')[0];
  const t = TAGS.indexOf(head);
  if (t >= 0) return t;
  const a = ALIAS[head];
  return a ? TAGS.indexOf(a) : -1;
}

// language tag normalization → asset basename
const LANGS = { json: 'json', js: 'javascript', javascript: 'javascript',
                mjs: 'javascript', ts: 'typescript', typescript: 'typescript',
                py: 'python', python: 'python', cpp: 'cpp', 'c++': 'cpp',
                cc: 'cpp', rust: 'rust', rs: 'rust', tsm: 'tsm' };

const HL_BASE = new URL('../../assets/hl/', import.meta.url);
// Node (static export, pages-design.md §3): web-tree-sitter resolves asset
// strings through fs, not fetch — hand it filesystem paths there.
const IS_NODE = typeof process !== 'undefined' && !!process.versions?.node;
let tsMod = null;   // web-tree-sitter module (lazy)
let initDone = null;
const langs = new Map();   // name → {lang, query} | null (failed/unknown)

async function load(name) {
  if (langs.has(name)) return langs.get(name);
  let entry = null;
  try {
    const asRef = IS_NODE
      ? (await import('node:url')).fileURLToPath
      : (u) => u.href;
    if (!tsMod) tsMod = await import(new URL('../../assets/hl/web-tree-sitter.js', import.meta.url));
    if (!initDone) initDone = tsMod.Parser.init({
      locateFile: () => asRef(new URL('web-tree-sitter.wasm', HL_BASE)),
    });
    await initDone;
    const lang = await tsMod.Language.load(asRef(new URL(`tree-sitter-${name}.wasm`, HL_BASE)));
    const scmUrl = new URL(`${name}.scm`, HL_BASE);
    const scm = IS_NODE
      ? await (await import('node:fs/promises')).readFile(asRef(scmUrl), 'utf8')
      : await (await fetch(scmUrl)).text();
    entry = { lang, query: new tsMod.Query(lang, scm) };
  } catch {
    entry = null;  // missing asset / load failure → plain code, never a stall
  }
  langs.set(name, entry);
  return entry;
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

// Literate-programming fragments (pbrt / noweb style): `<<Name>>`
// references and `<<Name>>=` / `<<Name>>+=` definition headers are not
// valid C++, so the grammar shreds them into type/variable/operator
// noise and the surrounding parse degrades. Instead of forking the
// grammar (530K-line parser.c, and a lexical fight with `<<` shifts),
// the provider recognizes them itself: each fragment span becomes one
// `label` token, and the text handed to tree-sitter has those spans
// blanked (same length, so offsets need no mapping).
const FRAGMENT_RE = /<<[^<>\n]+>>(?:\+?=)?/g;
const LABEL_TAG = TAGS.indexOf('label');

function literateSpans(text) {
  const spans = [];
  for (const m of text.matchAll(FRAGMENT_RE)) spans.push([m.index, m.index + m[0].length]);
  return spans;
}

// → flat Uint32Array of (start, end, tagId) triples (possibly empty)
export async function tokenize(langTag, text) {
  const name = LANGS[String(langTag).toLowerCase()];
  if (!name) return new Uint32Array(0);
  const entry = await load(name);
  if (!entry) return new Uint32Array(0);
  let fragments = [];
  if (name === 'cpp') {
    fragments = literateSpans(text);
    if (fragments.length) {
      let masked = '';
      let pos = 0;
      for (const [a, b] of fragments) { masked += text.slice(pos, a) + ' '.repeat(b - a); pos = b; }
      text = masked + text.slice(pos);
    }
  }
  const u8 = u16ToU8Map(text);
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
  for (const [a, b] of fragments) {
    const s0 = u8 ? u8[a] : a;
    const e0 = u8 ? u8[b] : b;
    caps.push({ s: s0, e: e0, pat: -1, tag: LABEL_TAG });  // pat -1: fragments win overlaps
  }
  caps.sort((a, b) => a.s - b.s || a.pat - b.pat);
  const out = [];
  let covered = 0;
  for (const c of caps) {
    if (c.s < covered || c.e <= c.s) continue;  // earlier pattern won
    out.push(c.s, c.e, c.tag);
    covered = c.e;
  }
  tree.delete();
  parser.delete();
  return Uint32Array.from(out);
}
