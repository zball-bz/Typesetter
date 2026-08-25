#!/usr/bin/env node
// Extracts translatable cases from the Typst test suite (Apache-2.0,
// github.com/typst/typst) into .tsm corpus files. The corpus serves as OUR
// golden/smoke input — Typst's pixel refs are not portable; its real-world
// syntax edge cases are.
//
// v2 (post M4/M6): beyond the shared pure-markup subset, the extractor now
// TRANSLATES constructs our engine gained:
//   - #set …(…) lines are stripped (style configuration only — the corpus
//     is smoke input, not a rendering comparison)
//   - <label> / @ref pass through verbatim (same syntax; unresolved refs
//     are warnings, not errors)
//   - term lists  "/ Name: desc"  →  #term[Name][desc]
//   - simple literal #table(columns:…, align:…, [cell]…)  →  #!table region
// Still rejected: any other #-call/code, math, unicode escapes.
import { readFileSync, writeFileSync, mkdirSync, readdirSync, statSync, rmSync } from 'node:fs';
import { join, dirname, relative } from 'node:path';
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
  /#[a-zA-Z_{([]/,    // any remaining hash call/code (Typst code is not JS)
  /\$/,               // math
  /\\u\{/,            // unicode escapes
  /```\S*[^`\n]*raw/, // odd raw configs
];

// --- balanced consume helpers ------------------------------------------------
function scanBalanced(s, i) { // s[i] === '(' → index after matching ')', or -1
  let depth = 0;
  for (; i < s.length; i++) {
    const c = s[i];
    if (c === '"') { i++; while (i < s.length && s[i] !== '"') i += s[i] === '\\' ? 2 : 1; continue; }
    if (c === '(' || c === '[' || c === '{') depth++;
    else if (c === ')' || c === ']' || c === '}') { depth--; if (depth === 0) return i + 1; }
  }
  return -1;
}

// strip #set …(…) statements (possibly multi-line); null = give up
function stripSets(body) {
  for (;;) {
    const m = /^[ \t]*#set\s+[a-zA-Z_][\w.]*/m.exec(body);
    if (!m) return body;
    let end = m.index + m[0].length;
    if (body[end] === '(') {
      end = scanBalanced(body, end);
      if (end < 0) return null;
    }
    // remove the statement plus a trailing newline
    while (end < body.length && body[end] !== '\n') {
      if (!/\s/.test(body[end])) return null; // trailing junk on the set line
      end++;
    }
    body = body.slice(0, m.index) + body.slice(end + 1);
  }
}

// "/ Name: desc" term lines → #term[Name][desc]
function xlatTerms(body) {
  return body.replace(/^\/ ([^:\n]+):[ \t]*(.*)$/gm, (all, name, desc) =>
    `#term[${name.trim()}][${desc.trim()}]`);
}

// split top-level comma args inside …(…) content
function splitArgs(s) {
  const out = [];
  let depth = 0, cur = '';
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '"') { let j = i + 1; while (j < s.length && s[j] !== '"') j += s[j] === '\\' ? 2 : 1; cur += s.slice(i, j + 1); i = j; continue; }
    if (c === '(' || c === '[' || c === '{') depth++;
    if (c === ')' || c === ']' || c === '}') depth--;
    if (c === ',' && depth === 0) { out.push(cur.trim()); cur = ''; continue; }
    cur += c;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}

const ALIGN = { left: 'l', center: 'c', right: 'r', start: 'l', end: 'r' };
// simple literal #table(...) → #!table region; null = not translatable
function xlatTables(body) {
  for (;;) {
    const m = /#table\(/.exec(body);
    if (!m) return body;
    // regions stand alone on their lines: only a line-start #table( (at most
    // leading whitespace before it) can become a #!table region
    const bol = body.lastIndexOf('\n', m.index - 1) + 1;
    if (!/^\s*$/.test(body.slice(bol, m.index))) return null;
    const open = m.index + m[0].length - 1;
    const end = scanBalanced(body, open);
    if (end < 0) return null;
    const args = splitArgs(body.slice(open + 1, end - 1));
    let cols = 0;
    let align = '';
    const cells = [];
    for (const a of args) {
      let mm;
      if ((mm = /^columns:\s*(\d+)$/.exec(a))) { cols = +mm[1]; continue; }
      if ((mm = /^columns:\s*\(([^)]*)\)$/.exec(a))) {
        const parts = splitArgs(mm[1]).filter(Boolean);
        if (!parts.every((p) => /^[\w.]+%?$/.test(p) || /^\d+(\.\d+)?(pt|em|fr|cm|mm|%)$/.test(p))) return null;
        cols = parts.length;
        continue;
      }
      if ((mm = /^align:\s*\(([^)]*)\)$/.exec(a))) {
        const parts = splitArgs(mm[1]).filter(Boolean);
        for (const p of parts) {
          if (!(p in ALIGN)) return null;
          align += ALIGN[p];
        }
        continue;
      }
      if (/^align:\s*(left|center|right)$/.test(a)) { align = ''; continue; } // uniform: default
      if (/^(stroke|fill|inset|gutter|column-gutter|row-gutter):/.test(a)) {
        if (/[\[\]]/.test(a)) return null; // content in style args: give up
        continue; // pure styling: dropped, corpus is smoke input
      }
      if (a.startsWith('[') && a.endsWith(']')) {
        const inner = a.slice(1, -1);
        if (/[#$\[\]]/.test(inner)) return null; // nested code/content: give up
        cells.push(inner.replace(/\|/g, '\\|').replace(/\s*\n\s*/g, ' ').trim());
        continue;
      }
      return null; // headers, spans, expressions: not translatable
    }
    if (cols < 1) cols = Math.max(1, cells.length);
    const rows = [];
    for (let i = 0; i < cells.length; i += cols)
      rows.push(cells.slice(i, i + cols).map((c) => (c === '' ? ' ' : c)).join(' | '));
    const region = `#!table(cols: ${cols}${align ? `, align: ${JSON.stringify(align)}` : ''})\n` +
      (rows.length ? rows.join('\n') + '\n' : '') + `#table!`;
    body = body.slice(0, m.index) + region + body.slice(end);
  }
}

let total = 0, taken = 0, translated = 0;
const report = {};
for (const file of walk(suiteDir)) {
  if (SKIP_DIRS.test(file)) continue;
  const rel = relative(suiteDir, file).replace(/\//g, '-').replace(/\.typ$/, '');
  const text = readFileSync(file, 'utf8');
  const parts = text.split(/^--- (.+?) ---$/m);
  for (let i = 1; i + 1 < parts.length; i += 2) {
    const header = parts[i].trim();
    const name = header.split(/\s+/)[0];
    const flags = header.split(/\s+/).slice(1);
    let body = parts[i + 1];
    total++;
    if (flags.includes('eval') || flags.includes('empty')) continue; // error-expectation cases
    body = body.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n'); // Typst comments
    const before = body;
    body = stripSets(body);
    if (body === null) continue;
    body = xlatTerms(body);
    body = xlatTables(body);
    if (body === null) continue;
    body = body.replace(/\s+$/, '').replace(/^\n+/, '');
    if (!body.trim()) continue;
    // residual-code probe: OUR OWN translated constructs (#term[…],
    // #!table(...), #name! closers) must not trip the hash filter
    const probe = body
      .replaceAll('#term[', '\x01[')
      .replace(/^#!\w+.*$/gm, '')
      .replace(/^#\w+!$/gm, '');
    if (reject.some((r) => r.test(probe))) continue;
    taken++;
    if (body !== before.replace(/\s+$/, '').replace(/^\n+/, '')) translated++;
    const area = rel.split('-')[0];
    report[area] = (report[area] || 0) + 1;
    writeFileSync(join(outDir, `${rel}--${name}.tsm`), body + '\n');
  }
}
console.log(`cases: ${total} total, ${taken} taken (${((taken / total) * 100).toFixed(1)}%), ${translated} via translation`);
console.log('by area:', JSON.stringify(report));
