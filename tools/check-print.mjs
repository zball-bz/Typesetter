#!/usr/bin/env node
// Printer conformance (plan P3-35; design T1 S12 (c)): parse(print(parse(x)))
// equals parse(x), spans and source layout aside, for every fixture and the
// real-world corpus (examples/real-world; the blog's ../zball-io when present).
//
//   node tools/check-print.mjs [--verbose] [files…]
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseTsm } from '../runtime/src/node/render.mjs';
import { print } from '../runtime/src/shared/tsm-print.mjs';

const root = join(fileURLToPath(new URL('.', import.meta.url)), '..');
const args = process.argv.slice(2);
const verbose = args.includes('--verbose');
const named = args.filter((a) => !a.startsWith('--'));

function* walk(dir) {
  if (!existsSync(dir)) return;
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.tsm')) yield p;
  }
}
const files = named.length ? named : [...walk(join(root, 'test/fixtures')), ...walk(join(root, 'examples/real-world')),
  ...walk(join(root, '../zball-io/src'))];

// the tree without what the source's layout decides: spans, raw maps, a
// fence's body offsets and line starts
const LAYOUT = new Set(['span', 'rawmap', 'bodyOffset', 'bodyEnd', 'lines']);
const norm = (n) => {
  const o = {};
  for (const [k, v] of Object.entries(n)) {
    if (LAYOUT.has(k)) continue;
    o[k] = k === 'kids' ? v.map(norm) : v;
  }
  return o;
};
// the first place two trees differ
function diff(a, b, path = '') {
  if (typeof a !== 'object' || typeof b !== 'object' || a === null || b === null)
    return a === b ? null : `${path}: ${JSON.stringify(a)} ≠ ${JSON.stringify(b)}`;
  const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
  for (const k of keys) {
    const d = diff(a[k], b[k], `${path}/${k}${a.sugar ? `(${a.sugar})` : a.kind ? `(${a.kind})` : ''}`);
    if (d) return d;
  }
  return null;
}

let bad = 0;
for (const f of files) {
  const src = readFileSync(f, 'utf8');
  const a = await parseTsm(src);
  const printed = print(a, { src });
  const b = await parseTsm(printed);
  const d = diff(norm(a), norm(b));
  if (d) {
    bad++;
    console.error(`PRINT ${relative(root, f)}: ${d.slice(0, 300)}`);
    if (verbose) console.error(printed);
  }
}

// escapeTsm's property (plan P3-35): any text, escaped as a paragraph's
// (or a heading's, a link's, a term's), parses back as that text — strings
// over the markup characters from a fixed seed; blanks as the parser keeps
// them (single, inside the text, none around a line end)
let seed = 0x2f6e2b1;
const rnd = (n) => {
  seed = (seed * 1103515245 + 12345) >>> 0;
  return seed % n;
};
const ALPHABET = [...'ab1 *_#$`[]^@%:/\\<>|-=+.!(){};,"\'中\n'];
let badText = 0;
const textOf = (n) => (n.kind === 'text' ? n.str : (n.kids ?? []).map(textOf).join(''));
for (let k = 0; k < 600; k++) {
  let s = '';
  const len = 1 + rnd(24);
  for (let i = 0; i < len; i++) s += ALPHABET[rnd(ALPHABET.length)];
  s = s.replace(/[ \n]+/g, (m) => (m.includes('\n') ? '\n' : ' ')).replace(/^[ \n]+|[ \n]+$/g, '');
  if (!s) continue;
  const ctx = ['para', 'heading', 'link', 'term'][k % 4];
  if (ctx !== 'para' && s.includes('\n')) s = s.replace(/\n/g, ' ');
  const ast = ctx === 'heading'
    ? { kind: 'doc', kids: [{ kind: 'call', sugar: 'heading', level: 2, kids: [{ kind: 'text', str: s }] }] }
    : ctx === 'link'
      ? { kind: 'doc', kids: [{ kind: 'call', sugar: 'para', kids: [{ kind: 'call', sugar: 'link', url: 'u', kids: [{ kind: 'text', str: s }] }] }] }
      : ctx === 'term'
        ? { kind: 'doc', kids: [{ kind: 'call', sugar: 'terms', kids: [{ kind: 'call', sugar: 'item', kids: [
          { kind: 'call', sugar: 'termpart', kids: [{ kind: 'text', str: s }] },
          { kind: 'call', sugar: 'para', kids: [{ kind: 'text', str: 'x' }] }] }] }] }
        : { kind: 'doc', kids: [{ kind: 'call', sugar: 'para', kids: [{ kind: 'text', str: s }] }] };
  const printed = print(ast);
  const back = await parseTsm(printed);
  const first = back.kids?.[0];
  const node = ctx === 'link' ? first?.kids?.[0] : ctx === 'term' ? first?.kids?.[0]?.kids?.[0] : first;
  const got = node ? textOf(node) : '';
  const ok = (ctx === 'link' ? sugarIs(first?.kids?.[0], 'link') : true) && got === s &&
    (back.kids ?? []).length === 1 && (node?.kids ?? []).every((x) => x.kind === 'text');
  if (!ok) {
    badText++;
    if (badText <= 10) console.error(`ESCAPE ${ctx} ${JSON.stringify(s)} → ${JSON.stringify(printed)} → ${JSON.stringify(got)}`);
  }
}
function sugarIs(n, s) { return n?.kind === 'call' && n.sugar === s; }

console.log(`check-print: ${files.length} documents, ${bad} round-trip difference(s); escapeTsm: ${badText} failure(s) in 600 texts`);
process.exit(bad || badText ? 1 : 0);
