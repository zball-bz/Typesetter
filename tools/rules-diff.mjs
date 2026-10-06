#!/usr/bin/env node
// Compare two TextRules versions (plan P1-11; design T5, review R8): which
// codepoints change class or column, and — with --corpus — which boundaries
// of the fixtures and the real-world documents see a different class pair or
// column set, the input to P4's class migration (RULES_VERSION 1 must keep
// compat's pair results for every codepoint compat classifies, unless
// allowlisted).
//   node tools/rules-diff.mjs --b engine/rules/locale/<new>.def [--a <old>.def]
//        [--corpus] [--allow allow.txt] [--check]
// --a defaults to engine/rules/locale/compat.def. --allow lists "U+XXXX" or
// "U+XXXX..U+YYYY" codepoints whose change is intended; --check exits 1 on
// any other change.
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildRules } from './ucdc.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const arg = (name, dflt) => {
  const i = process.argv.indexOf(name);
  return i >= 0 ? process.argv[i + 1] : dflt;
};
const A = buildRules(arg('--a', join(root, 'engine/rules/locale/compat.def')));
const bPath = arg('--b');
if (!bPath) {
  console.error('usage: rules-diff.mjs --b <rules.def> [--a <rules.def>] [--corpus] [--allow f] [--check]  (the engine\'s: engine/rules/locale/default.def)');
  process.exit(2);
}
const B = buildRules(bPath);
// (plan P4-05) a codepoint as the engine reads it — its columns, blanks and
// ambiguous kind; class names differ between rules versions (compat's
// Other is RULES_VERSION 1's Alpha, Digit, …) and are not compared
const desc = (R, cp) => R.behaviour(cp);

// the allowlist: `U+XXXX[..U+YYYY] [kern]` per line — any change there, or
// (kern) only its kerning (plan P4-05: eligibility is a class's column)
const allowed = [];
for (const line of existsSync(arg('--allow', '')) ? readFileSync(arg('--allow'), 'utf8').split('\n') : []) {
  const m = /^\s*U\+([0-9A-Fa-f]+)(?:\.\.U\+([0-9A-Fa-f]+))?(\s+kern)?/.exec(line);
  if (m) allowed.push([parseInt(m[1], 16), parseInt(m[2] ?? m[1], 16), !!m[3]]);
}
const strip = (x) => x.split('+').filter((t) => t && t !== '-' && t !== 'kern').join('+');
const kernOnly = (a, b) => strip(a) === strip(b);
const isAllowed = (cp, a, b) => allowed.some(([lo, hi, k]) => cp >= lo && cp <= hi && (!k || kernOnly(a, b)));

// codepoints, as ranges of identical changes
let changes = 0, unexpected = 0;
for (let cp = 0; cp < 0x110000;) {
  const a = desc(A, cp), b = desc(B, cp);
  if (a === b) {
    cp++;
    continue;
  }
  let end = cp;
  while (end + 1 < 0x110000 && desc(A, end + 1) === a && desc(B, end + 1) === b) end++;
  const hex = (n) => 'U+' + n.toString(16).toUpperCase().padStart(4, '0');
  const ok = isAllowed(cp, a, b) && isAllowed(end, a, b);
  console.log(`${ok ? 'allowed ' : ''}${hex(cp)}${end > cp ? '..' + hex(end) : ''}  ${a} → ${b}`);
  changes += end - cp + 1;
  if (!ok) unexpected += end - cp + 1;
  cp = end + 1;
}
console.log(`rules-diff: ${changes} codepoints change (${unexpected} not allowlisted)`);

if (process.argv.includes('--corpus')) {
  const files = [];
  const walk = (d) => {
    if (!existsSync(d)) return;
    for (const e of readdirSync(d).sort()) {
      const p = join(d, e);
      if (statSync(p).isDirectory()) walk(p);
      else if (p.endsWith('.tsm')) files.push(p);
    }
  };
  walk(join(root, 'test/fixtures'));
  walk(join(root, 'examples/real-world'));
  walk(join(root, '../zball-io/src/docs'));
  let boundaries = 0, changed = 0;
  const examples = new Map();
  for (const f of files) {
    const cps = [...readFileSync(f, 'utf8')].map((ch) => ch.codePointAt(0));
    for (let i = 0; i + 1 < cps.length; i++) {
      boundaries++;
      const a = `${desc(A, cps[i])}|${desc(A, cps[i + 1])}`, b = `${desc(B, cps[i])}|${desc(B, cps[i + 1])}`;
      if (a === b) continue;
      changed++;
      const key = `${a} → ${b}`;
      if (!examples.has(key)) examples.set(key, `${f.replace(root + '/', '')}: ${String.fromCodePoint(cps[i], cps[i + 1])}`);
    }
  }
  for (const [k, v] of examples) console.log(`boundary ${k}   e.g. ${v}`);
  console.log(`rules-diff: ${changed} of ${boundaries} corpus boundaries change their class pair`);
}
if (process.argv.includes('--check') && unexpected) process.exit(1);
