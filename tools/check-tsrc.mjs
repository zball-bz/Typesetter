#!/usr/bin/env node
// tsrc reproduces every golden (plan P1-03; document-model §12: "tsrc
// --stage=… and the golden tests share it verbatim"): for each fixture and
// each golden file it has, `tsrc --stage=<product> --profile=golden
// [--fixture=X.fixture.json] --ops=X.ops X.tsm` must print the golden bytes.
//   node tools/check-tsrc.mjs [--check]
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const tsrc = join(root, 'engine/build/tsrc');
const fixtures = join(root, 'test/fixtures');
function* walk(dir) {
  for (const e of readdirSync(dir).sort()) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.tsm')) yield p;
  }
}
let checked = 0;
const diffs = [];
for (const tsm of walk(fixtures)) {
  const rel = relative(fixtures, tsm).replace(/\.tsm$/, '');
  const gdir = join(root, 'test/golden', dirname(rel));
  const stem = rel.split('/').pop();
  if (!existsSync(gdir)) continue;
  const fx = tsm.replace(/\.tsm$/, '.fixture.json');
  const ops = tsm.replace(/\.tsm$/, '.ops');
  for (const g of readdirSync(gdir)) {
    if (!g.startsWith(stem + '.') || !g.endsWith('.txt')) continue;
    const product = g.slice(stem.length + 1, -4);
    if (product.includes('.')) continue;  // another fixture's file (stem prefix)
    const args = [`--stage=${product}`, '--profile=golden'];
    if (existsSync(fx)) args.push(`--fixture=${fx}`);
    if (existsSync(ops)) args.push(`--ops=${ops}`);
    args.push(tsm);
    let out;
    try {
      out = execFileSync(tsrc, args, { cwd: root, encoding: 'utf8', maxBuffer: 1 << 26 });
    } catch (e) {
      diffs.push(`${rel}.${product}: tsrc failed: ${String(e.stderr ?? e).trim()}`);
      continue;
    }
    checked++;
    if (out !== readFileSync(join(gdir, g), 'utf8')) diffs.push(`${rel}.${product}: differs`);
  }
}
for (const d of diffs) console.log('DIFF ' + d);
console.log(`check-tsrc: ${checked} golden files, ${diffs.length} differences`);
if (diffs.length && process.argv.includes('--check')) process.exit(1);
