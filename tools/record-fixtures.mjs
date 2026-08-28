#!/usr/bin/env node
// Records .ops buffers for every fixture: native tsrc emits the compiled JS,
// the real executor runs it (testing.md §1). `--check` fails on stale files.
import { execFileSync } from 'node:child_process';
import { readdirSync, readFileSync, writeFileSync, existsSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execute } from '../runtime/src/worker/executor.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const tsrc = join(root, 'engine/build/tsrc');
const fixtures = join(root, 'test/fixtures');
const check = process.argv.includes('--check');

function* walk(dir) {
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.tsm')) yield p;
  }
}

let stale = 0, wrote = 0;
for (const tsm of walk(fixtures)) {
  const js = execFileSync(tsrc, ['--stage=js', tsm], { encoding: 'utf8' });
  const ops = Buffer.from(await execute(js, { baseDir: dirname(tsm), rootDir: root }));
  const opsPath = tsm.replace(/\.tsm$/, '.ops');
  const prev = existsSync(opsPath) ? readFileSync(opsPath) : null;
  if (prev && prev.equals(ops)) continue;
  if (check) {
    console.error(`STALE ${opsPath}`);
    stale++;
  } else {
    writeFileSync(opsPath, ops);
    console.log(`wrote ${opsPath} (${ops.length} bytes)`);
    wrote++;
  }
}
if (check && stale) process.exit(1);
console.log(check ? 'all recordings current' : `${wrote} recording(s) updated`);
