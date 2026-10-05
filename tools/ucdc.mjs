#!/usr/bin/env node
// TextRules compiler (plan P1-11; design T5 TextRules, D-X04): the pinned
// Unicode 17.0.0 UCD files (engine/rules/ucd/17.0.0) plus a rules version's
// class rules (engine/rules/locale/compat.def) → engine/gen/textrules.h —
// the class enum, the per-class columns, a range table of (class, kern) and
// one of the UCD columns (UAX #29 grapheme break, Extended_Pictographic,
// UAX #11 East Asian width), and the rules' constants.
//   node tools/ucdc.mjs            regenerate
//   node tools/ucdc.mjs --check    fail if the generated header is stale
//   node tools/ucdc.mjs --fetch    re-download the pinned UCD files
// Imported by tools/rules-diff.mjs (buildRules).
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
export const UNICODE_VERSION = '17.0.0';
const ucdDir = join(root, 'engine/rules/ucd', UNICODE_VERSION);
const UCD_FILES = {
  'LineBreak.txt': 'LineBreak.txt',
  'EastAsianWidth.txt': 'EastAsianWidth.txt',
  'Scripts.txt': 'Scripts.txt',
  'emoji-data.txt': 'emoji/emoji-data.txt',
  'GraphemeBreakProperty.txt': 'auxiliary/GraphemeBreakProperty.txt',
};
const MAX = 0x110000;
export const GCB = ['Other', 'CR', 'LF', 'Control', 'Extend', 'ZWJ', 'Regional_Indicator', 'Prepend',
                    'SpacingMark', 'L', 'V', 'T', 'LV', 'LVT'];
export const EAW = ['N', 'A', 'H', 'W', 'F', 'Na'];

const strip = (t) => t.split('\n').map((l) => l.replace(/\/\/.*$/, '').trim()).filter(Boolean);
const rowsOf = (lines, name) => lines.filter((l) => l.startsWith(name + '(')).map((l) =>
  l.slice(name.length + 1, l.lastIndexOf(')')).trim());

export function classList() {
  const t = readFileSync(join(root, 'engine/rules/classes.def'), 'utf8');
  return [...t.matchAll(/CC\((\w+)\)/g)].map((m) => m[1]);
}

// UCD "XXXX..YYYY ; Value # …" files → per-codepoint values
function ucdProperty(file, map, dflt) {
  const out = new Array(MAX).fill(dflt);
  for (const line of readFileSync(join(ucdDir, file), 'utf8').split('\n')) {
    const body = line.replace(/#.*$/, '').trim();
    if (!body) continue;
    const [range, value] = body.split(';').map((s) => s.trim());
    const v = map(value);
    if (v === undefined) continue;
    const [a, b] = range.split('..').map((h) => parseInt(h, 16));
    for (let cp = a; cp <= (b ?? a); cp++) out[cp] = v;
  }
  return out;
}

// a rules version → per-codepoint (class, kern) and the class columns
export function buildRules(defPath = join(root, 'engine/rules/locale/compat.def')) {
  const classes = classList();
  const ccIndex = (n) => {
    const i = classes.indexOf(n);
    if (i < 0) throw new Error(`${defPath}: unknown class ${n}`);
    return i;
  };
  const lines = strip(readFileSync(defPath, 'utf8'));
  const version = Number(rowsOf(lines, 'RULES_VERSION')[0]);
  const cc = new Uint8Array(MAX).fill(ccIndex(rowsOf(lines, 'DEFAULT')[0]));
  const wide = new Uint8Array(MAX);
  for (const spec of rowsOf(lines, 'WIDE'))
    for (const r of spec.split(/\s+/)) {
      const [a, b] = r.split('..').map((h) => parseInt(h, 16));
      for (let cp = a; cp <= (b ?? a); cp++) wide[cp] = 1;
    }
  const wideDefault = ccIndex(rowsOf(lines, 'WIDE_DEFAULT')[0]);
  for (let cp = 0; cp < MAX; cp++) if (wide[cp]) cc[cp] = wideDefault;
  for (const row of rowsOf(lines, 'CLASS')) {
    const [name, cps] = row.split(',').map((s) => s.trim());
    for (const h of cps.split(/\s+/)) cc[parseInt(h, 16)] = ccIndex(name);
  }
  // a class is wide everywhere or nowhere
  const wideOf = new Int8Array(classes.length).fill(-1);
  for (let cp = 0; cp < MAX; cp++) {
    const c = cc[cp];
    if (wideOf[c] === -1) wideOf[c] = wide[cp];
    else if (wideOf[c] !== wide[cp]) throw new Error(`${defPath}: class ${classes[c]} is both wide and narrow`);
  }
  const columns = { wide: classes.map((_, c) => wideOf[c] === 1) };
  for (const row of rowsOf(lines, 'COLUMN')) {
    const [name, members] = row.split(',').map((s) => s.trim());
    const set = new Set(members.split(/\s+/).map(ccIndex));
    columns[name] = classes.map((_, c) => set.has(c));
  }
  const cutoff = parseInt(rowsOf(lines, 'KERN_CUTOFF')[0], 16);
  const kern = new Uint8Array(MAX);
  for (let cp = 0; cp < MAX; cp++) kern[cp] = !wide[cp] && cp < cutoff ? 1 : 0;
  const consts = rowsOf(lines, 'CONST').map((r) => r.split(',').map((s) => s.trim()));
  return { version, classes, cc, kern, columns, consts };
}

function generate() {
  const R = buildRules();
  const gcbIdx = Object.fromEntries(GCB.map((g, i) => [g, i]));
  const eawIdx = Object.fromEntries(EAW.map((g, i) => [g, i]));
  const gcb = ucdProperty('GraphemeBreakProperty.txt', (v) => gcbIdx[v], 0);
  const eaw = ucdProperty('EastAsianWidth.txt', (v) => eawIdx[v], 0);
  const ext = ucdProperty('emoji-data.txt', (v) => (v === 'Extended_Pictographic' ? 1 : undefined), 0);

  // (class, kern) ranges and UCD-column ranges, each covering [0, 0x110000)
  const ccRanges = [];
  for (let cp = 0; cp < MAX; cp++) {
    const v = (R.cc[cp] << 1) | R.kern[cp];
    if (!ccRanges.length || ccRanges[ccRanges.length - 1][1] !== v) ccRanges.push([cp, v]);
  }
  const ucdRanges = [];
  for (let cp = 0; cp < MAX; cp++) {
    const v = gcb[cp] | (eaw[cp] << 4) | (ext[cp] << 7);
    if (!ucdRanges.length || ucdRanges[ucdRanges.length - 1][1] !== v) ucdRanges.push([cp, v]);
  }
  const hex = (n) => '0x' + n.toString(16).toUpperCase();
  const flagNames = Object.keys(R.columns);
  const flags = R.classes.map((_, c) => flagNames.reduce((f, n, k) => f | (R.columns[n][c] ? 1 << k : 0), 0));
  const wrap = (items, per) => {
    const out = [];
    for (let i = 0; i < items.length; i += per) out.push('    ' + items.slice(i, i + per).join(', ') + ',');
    return out.join('\n');
  };
  const h = `// GENERATED by tools/ucdc.mjs from engine/rules (classes.def, locale/compat.def) and the
// pinned UCD ${UNICODE_VERSION} — do not edit. API: engine/src/shape/textrules.h.
#pragma once
#include <cstdint>

namespace tsr {

constexpr std::uint32_t RULES_VERSION = ${R.version};
constexpr const char* UNICODE_VERSION = "${UNICODE_VERSION}";

enum class CC : std::uint8_t { ${R.classes.join(', ')}, N };
constexpr const char* kCCName[] = {${R.classes.map((c) => JSON.stringify(c)).join(', ')}};

// class columns
${flagNames.map((n, k) => `constexpr std::uint8_t kCC_${n} = ${1 << k};`).join('\n')}
constexpr std::uint8_t kCCFlags[] = {${flags.join(', ')}};

// UAX #29 grapheme break and UAX #11 East Asian width values
enum class GCB : std::uint8_t { ${GCB.join(', ')} };
enum class EAW : std::uint8_t { ${EAW.join(', ')} };

// [start, next start): class << 1 | kern-eligible
struct CCRange { std::uint32_t start; std::uint8_t v; };
constexpr CCRange kCCRanges[] = {
${wrap(ccRanges.map(([a, v]) => `{${hex(a)}, ${v}}`), 6)}
};
// [start, next start): gcb | eaw << 4 | extPict << 7
struct UcdRange { std::uint32_t start; std::uint8_t v; };
constexpr UcdRange kUcdRanges[] = {
${wrap(ucdRanges.map(([a, v]) => `{${hex(a)}, ${v}}`), 6)}
};

// the rules' constants (em)
${R.consts.map(([n, v]) => `constexpr double kRule_${n} = ${v};`).join('\n')}

}  // namespace tsr
`;
  return h;
}

async function fetchUcd() {
  mkdirSync(ucdDir, { recursive: true });
  for (const [name, path] of Object.entries(UCD_FILES)) {
    const res = await fetch(`https://www.unicode.org/Public/${UNICODE_VERSION}/ucd/${path}`);
    if (!res.ok) throw new Error(`${path}: HTTP ${res.status}`);
    writeFileSync(join(ucdDir, name), Buffer.from(await res.arrayBuffer()));
    console.log(`fetched ${name}`);
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  if (process.argv.includes('--fetch')) await fetchUcd();
  const out = join(root, 'engine/gen/textrules.h');
  const text = generate();
  const old = existsSync(out) ? readFileSync(out, 'utf8') : null;
  if (process.argv.includes('--check')) {
    if (old !== text) {
      console.error('stale: engine/gen/textrules.h (run node tools/ucdc.mjs)');
      process.exit(1);
    }
  } else if (old !== text) {
    writeFileSync(out, text);
    console.log('wrote engine/gen/textrules.h');
  }
}
