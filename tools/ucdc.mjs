#!/usr/bin/env node
// TextRules compiler (plan P1-11; design T5 TextRules, D-X04): the pinned
// Unicode 17.0.0 UCD files (engine/rules/ucd/17.0.0) plus a rules version's
// class rules (engine/rules/locale/compat.def) → engine/gen/textrules.h —
// the class enum, the per-class columns, a range table of (class, kern) and
// one of the UCD columns (UAX #29 grapheme break, Extended_Pictographic,
// UAX #11 East Asian width), the letters and their lowercase mappings
// (UnicodeData.txt, plan P4-06), and the rules' constants.
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
  'UnicodeData.txt': 'UnicodeData.txt',
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

// a rules version → per-codepoint (class, kern) and the class columns.
// Rows (applied in this order): DEFAULT; LB(lb…, Class) — UAX #14 classes,
// the first matching row wins; WIDE ranges + WIDE_DEFAULT (a codepoint in
// them is wide, the default class unless named later); SCRIPT(script…,
// Class) — Scripts.txt, within the WIDE ranges; CLASS_LB(lb…, Class) — as
// LB, within the WIDE ranges, over everything so far; CLASS(Class, cp…); then COLUMN, KERN_CUTOFF, BLANK, ADVANCE, EMERGENCY, CONST.
// INCLUDE(file) splices a file (relative to the including one). Without
// COLUMN(wide) a class's width comes from the WIDE ranges (compat), without
// COLUMN(kern) its kerning from KERN_CUTOFF.
const ucdCache = new Map();
function ucdColumn(file) {
  if (!ucdCache.has(file)) ucdCache.set(file, ucdProperty(file, (v) => v, file === 'LineBreak.txt' ? 'XX' : 'Unknown'));
  return ucdCache.get(file);
}
function expand(path, seen = new Set()) {
  if (seen.has(path)) throw new Error(`${path}: INCLUDE cycle`);
  seen.add(path);
  const out = [];
  for (const line of strip(readFileSync(path, 'utf8'))) {
    const m = /^INCLUDE\((.+)\)$/.exec(line);
    if (m) out.push(...expand(join(dirname(path), m[1].trim()), seen));
    else out.push(line);
  }
  return out;
}
export function buildRules(defPath = join(root, 'engine/rules/locale/default.def')) {
  const classes = classList();
  const ccIndex = (n) => {
    const i = classes.indexOf(n);
    if (i < 0) throw new Error(`${defPath}: unknown class ${n}`);
    return i;
  };
  const lines = expand(defPath);
  const version = Number(rowsOf(lines, 'RULES_VERSION')[0]);
  const cc = new Uint8Array(MAX).fill(ccIndex(rowsOf(lines, 'DEFAULT')[0]));
  const split = (row) => row.split(',').map((x) => x.trim());
  const byLb = (rows, onlyUnset, within = null) => {
    if (!rows.length) return;
    const lb = ucdColumn('LineBreak.txt');
    const set = new Uint8Array(MAX);
    for (const row of rows) {
      const [lbs, name] = split(row);
      const want = new Set(lbs.split(/\s+/));
      const c = ccIndex(name);
      for (let cp = 0; cp < MAX; cp++)
        if (want.has(lb[cp]) && !(onlyUnset && set[cp]) && (!within || within[cp])) {
          cc[cp] = c;
          set[cp] = 1;
        }
    }
  };
  byLb(rowsOf(lines, 'LB'), true);
  const wide = new Uint8Array(MAX);
  for (const spec of rowsOf(lines, 'WIDE'))
    for (const r of spec.split(/\s+/)) {
      const [a, b] = r.split('..').map((h) => parseInt(h, 16));
      for (let cp = a; cp <= (b ?? a); cp++) wide[cp] = 1;
    }
  const wideDefaultRow = rowsOf(lines, 'WIDE_DEFAULT')[0];
  if (wideDefaultRow) {
    const wideDefault = ccIndex(wideDefaultRow);
    for (let cp = 0; cp < MAX; cp++) if (wide[cp]) cc[cp] = wideDefault;
  }
  const scriptRows = rowsOf(lines, 'SCRIPT');
  if (scriptRows.length) {
    const sc = ucdColumn('Scripts.txt');
    for (const row of scriptRows) {
      const [names, name] = split(row);
      const want = new Set(names.split(/\s+/));
      const c = ccIndex(name);
      // (the Han section's: within its WIDE ranges)
      for (let cp = 0; cp < MAX; cp++) if (wide[cp] && want.has(sc[cp])) cc[cp] = c;
    }
  }
  byLb(rowsOf(lines, 'CLASS_LB'), false, wide);  // (the Han section's: within its ranges)
  for (const row of rowsOf(lines, 'CLASS')) {
    const [name, cps] = split(row);
    for (const h of cps.split(/\s+/)) cc[parseInt(h, 16)] = ccIndex(name);
  }
  const columns = {};
  const colRows = Object.fromEntries(rowsOf(lines, 'COLUMN').map((row) => {
    const [name, members] = split(row);
    return [name, new Set(members.split(/\s+/).map(ccIndex))];
  }));
  if (colRows.wide) {
    columns.wide = classes.map((_, c) => colRows.wide.has(c));
  } else {
    // a class is wide everywhere or nowhere (compat: the WIDE ranges say)
    const wideOf = new Int8Array(classes.length).fill(-1);
    for (let cp = 0; cp < MAX; cp++) {
      const c = cc[cp];
      if (wideOf[c] === -1) wideOf[c] = wide[cp];
      else if (wideOf[c] !== wide[cp]) throw new Error(`${defPath}: class ${classes[c]} is both wide and narrow`);
    }
    columns.wide = classes.map((_, c) => wideOf[c] === 1);
  }
  for (const [name, set] of Object.entries(colRows)) {
    if (name === 'wide' || name === 'kern') continue;
    columns[name] = classes.map((_, c) => set.has(c));
  }
  const kern = new Uint8Array(MAX);
  if (colRows.kern) {
    for (let cp = 0; cp < MAX; cp++) kern[cp] = colRows.kern.has(cc[cp]) ? 1 : 0;
  } else {
    const cutoff = parseInt(rowsOf(lines, 'KERN_CUTOFF')[0], 16);
    for (let cp = 0; cp < MAX; cp++) kern[cp] = !columns.wide[cc[cp]] && cp < cutoff ? 1 : 0;
  }
  const consts = rowsOf(lines, 'CONST').map((r) => r.split(',').map((x) => x.trim()));
  // (plan P4-04) blanks per class, defined advances
  const blanks = classes.map(() => [0, 0]);
  for (const row of rowsOf(lines, 'BLANK')) {
    const [members, l, r] = split(row);
    for (const n of members.split(/\s+/)) blanks[ccIndex(n)] = [Number(l), Number(r)];
  }
  const advances = rowsOf(lines, 'ADVANCE').map((row) => {
    const [cps, em] = split(row);
    const seq = cps.split(/\s+/).map((h) => parseInt(h, 16));
    if (seq.length > 3) throw new Error(`${defPath}: an ADVANCE sequence holds at most 3 codepoints`);
    return { seq, em: Number(em) };
  }).sort((a, b) => b.seq.length - a.seq.length);
  // (plan P4-06) the emergency table: separators and the sides a break takes
  const SIDES = { before: 1, after: 2, both: 3 };
  const emergency = rowsOf(lines, 'EMERGENCY').flatMap((row) => {
    const [side, cps] = split(row);
    if (!SIDES[side]) throw new Error(`${defPath}: EMERGENCY side ${side} (before, after or both)`);
    // a row of single codepoints; a sequence is written joined by '+' (002F+002F)
    return cps.split(/\s+/).map((tok) => {
      const seq = tok.split('+').map((h) => parseInt(h, 16));
      if (seq.length > 2) throw new Error(`${defPath}: an EMERGENCY separator holds at most 2 codepoints`);
      return { seq, side: SIDES[side] };
    });
  }).sort((a, b) => b.seq.length - a.seq.length);
  // (plan P4-05) what the engine reads of a codepoint, the columns compat
  // lacked derived as compat's engine read them — rules-diff compares this
  const col = (name, c, dflt) => (columns[name] ? columns[name][c] : dflt);
  const behaviour = (cp) => {
    const c = cc[cp];
    const w = columns.wide[c], o = col('open', c, false), cl = col('close', c, false);
    const punct = col('punct', c, o || cl);
    // (a class the shaper reads by name: an ambiguous mark, a space, a break control)
    const named = /^(Amb|Space$|NbSpace$|NbRigid$|ZwSpace$|WordJoiner$|SoftHyphen$|NewLine$)/.test(classes[c]) ? classes[c] : '';
    return [w && 'wide', punct && 'punct', o && 'open', cl && 'close', col('nostart', c, cl) && 'nostart',
      col('autospace', c, w && !punct) && 'autospace', col('ambwide', c, w) && 'ambwide', col('joins', c, false) && 'joins',
      kern[cp] && 'kern', blanks[c].some((x) => x) && `blank${blanks[c][0]}/${blanks[c][1]}`, named]
      .filter(Boolean).join('+') || '-';
  };
  return { version, classes, cc, kern, columns, consts, blanks, advances, emergency, behaviour };
}

// (plan P4-06) UnicodeData.txt: the letters (General_Category L*) as the
// edges of their ranges, and the simple lowercase mappings (field 13)
function unicodeData() {
  const letter = new Uint8Array(MAX);
  const lower = [];
  let first = -1;
  for (const line of readFileSync(join(ucdDir, 'UnicodeData.txt'), 'utf8').split('\n')) {
    if (!line) continue;
    const f = line.split(';');
    const cp = parseInt(f[0], 16);
    const isL = f[2][0] === 'L';
    if (f[1].endsWith(', First>')) {
      first = cp;
      continue;
    }
    for (let c = f[1].endsWith(', Last>') ? first : cp; c <= cp; c++) letter[c] = isL ? 1 : 0;
    if (f[13]) lower.push([cp, parseInt(f[13], 16)]);
  }
  const edges = [];
  for (let cp = 0, in_ = 0; cp <= MAX; cp++) {
    const v = cp < MAX ? letter[cp] : 0;
    if (v !== in_) {
      edges.push(cp);
      in_ = v;
    }
  }
  return { edges, lower };
}

function generate() {
  const R = buildRules();
  const U = unicodeData();  // (plan P4-05) engine/rules/locale/default.def
  const gcbIdx = Object.fromEntries(GCB.map((g, i) => [g, i]));
  const eawIdx = Object.fromEntries(EAW.map((g, i) => [g, i]));
  const gcb = ucdProperty('GraphemeBreakProperty.txt', (v) => gcbIdx[v], 0);
  const eaw = ucdProperty('EastAsianWidth.txt', (v) => eawIdx[v], 0);
  const ext = ucdProperty('emoji-data.txt', (v) => (v === 'Extended_Pictographic' ? 1 : undefined), 0);

  // (plan P4-05) (class, kern) as a two-level table — [cp >> 7] → one of the
  // deduplicated blocks of 128 — and the UCD columns as ranges, covering [0, 0x110000)
  const blockIds = new Map(), blocks = [], index = [];
  for (let b = 0; b < MAX / 128; b++) {
    const vals = [];
    for (let k = 0; k < 128; k++) vals.push((R.cc[b * 128 + k] << 1) | R.kern[b * 128 + k]);
    const key = vals.join(',');
    if (!blockIds.has(key)) {
      blockIds.set(key, blocks.length);
      blocks.push(vals);
    }
    index.push(blockIds.get(key));
  }
  if (blocks.length > 256) throw new Error('the class table needs more than 256 blocks');
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
  const h = `// GENERATED by tools/ucdc.mjs from engine/rules (classes.def, locale/default.def) and the
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

// (plan P4-05) class << 1 | kern-eligible, two levels: kCCIndex[cp >> 7]
// names the block of 128 that holds cp
constexpr std::uint8_t kCCIndex[${index.length}] = {
${wrap(index, 32)}
};
constexpr std::uint8_t kCCBlocks[${blocks.length}][128] = {
${blocks.map((b) => '    {' + b.join(', ') + '},').join('\n')}
};
// [start, next start): gcb | eaw << 4 | extPict << 7
struct UcdRange { std::uint32_t start; std::uint8_t v; };
constexpr UcdRange kUcdRanges[] = {
${wrap(ucdRanges.map(([a, v]) => `{${hex(a)}, ${v}}`), 6)}
};

// the rules' constants (em; a count where its name says so)
${R.consts.map(([n, v]) => `constexpr double kRule_${n} = ${v};`).join('\n')}

// (plan P4-04) each class's punctuation blanks (em): leading, trailing
struct Blank { float l, r; };
constexpr Blank kBlanks[] = {${R.blanks.map(([l, r]) => `{${l}, ${r}}`).join(', ')}};
// (plan P4-04) defined advances: a sequence set at a defined width (em),
// longest first
struct DefinedAdvance { std::uint32_t seq[3]; std::uint8_t len; float em; };
constexpr DefinedAdvance kDefinedAdvances[] = {
${R.advances.map((a) => `    {{${[...a.seq, 0, 0].slice(0, 3).map(hex).join(', ')}}, ${a.seq.length}, ${a.em}},`).join('\n')}
};
// (plan P4-06) the emergency table: a separator (1–2 codepoints, longest
// first) and the sides of it a break may take (1 before, 2 after)
struct EmergencySep { std::uint32_t seq[2]; std::uint8_t len, side; };
constexpr EmergencySep kEmergencySeps[] = {
${R.emergency.map((e) => `    {{${[...e.seq, 0].slice(0, 2).map(hex).join(', ')}}, ${e.seq.length}, ${e.side}},`).join('\n')}
};

// (plan P4-06) the letters (UCD General_Category L*): the edges of their
// ranges — a letter range starts at an even index, ends at an odd one
constexpr std::uint32_t kLetterEdges[] = {
${wrap(U.edges.map(hex), 10)}
};
// the simple lowercase mappings (UnicodeData.txt field 13), by codepoint
struct CaseMap { std::uint32_t from, to; };
constexpr CaseMap kLower[] = {
${wrap(U.lower.map(([a, b]) => `{${hex(a)}, ${hex(b)}}`), 6)}
};

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
