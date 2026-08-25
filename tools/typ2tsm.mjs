#!/usr/bin/env node
// Extracts translatable cases from the Typst test suite (Apache-2.0,
// github.com/typst/typst) into .tsm corpus files. Only the shared pure-markup
// subset is taken: no #-calls, no math, no term lists, no unicode escapes.
// The corpus serves as OUR golden/smoke input — Typst's pixel refs are not
// portable; its real-world syntax edge cases are.
import { readFileSync, writeFileSync, mkdirSync, readdirSync, statSync, rmSync } from 'node:fs';
import { join, dirname, relative, basename } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const suiteDir = process.argv[2];
if (!suiteDir) { console.error('usage: typ2tsm.mjs <typst tests/suite dir>'); process.exit(2); }
const outDir = join(root, 'test/corpus/typst');
rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });

function* walk(dir) {
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.typ')) yield p;
  }
}

const SKIP_DIRS = /\/(math|visualize|pdf|pdftags|introspection|loading|foundations|scripting|styling|symbols|html)\//;
const reject = [
  /#[a-zA-Z_{([]/,   // any hash call, code block, or expression (Typst code is not JS)
  /\$/,              // math
  /^\s*\/ /m,        // term lists (dropped feature)
  /\\u\{/,           // unicode escapes
  /<[a-z-]+>/,       // labels (M4)
  /@[a-zA-Z]/,       // refs (M4)
  /```\S*[^`\n]*raw/, // odd raw configs
];

let total = 0, taken = 0;
const report = {};
for (const file of walk(suiteDir)) {
  if (SKIP_DIRS.test(file)) continue;
  const rel = relative(suiteDir, file).replace(/\//g, '-').replace(/\.typ$/, '');
  const text = readFileSync(file, 'utf8');
  const parts = text.split(/^--- (.+?) ---$/m);
  // parts: [preamble, name1, body1, name2, body2, ...]
  for (let i = 1; i + 1 < parts.length; i += 2) {
    const header = parts[i].trim();
    const name = header.split(/\s+/)[0];
    const flags = header.split(/\s+/).slice(1);
    let body = parts[i + 1];
    total++;
    if (flags.includes('eval') || flags.includes('empty')) continue;  // error-expectation cases
    // strip comments (Typst // comments; ours use %-- --%)
    body = body.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
    body = body.replace(/\s+$/, '').replace(/^\n+/, '');
    if (!body.trim()) continue;
    if (reject.some((r) => r.test(body))) continue;
    taken++;
    const area = rel.split('-')[0];
    report[area] = (report[area] || 0) + 1;
    writeFileSync(join(outDir, `${rel}--${name}.tsm`), body + '\n');
  }
}
console.log(`cases: ${total} total, ${taken} translated (${((taken / total) * 100).toFixed(1)}%)`);
console.log('by area:', JSON.stringify(report));
