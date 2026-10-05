#!/usr/bin/env node
// Records .ops buffers for every fixture: native tsrc compiles (the
// LowerProgram and its hole module, plan P2-02), the real executor runs it
// (testing.md §1). `--check` fails on stale files.
// A fixture for vocabulary without surface syntax (plan P1-13: a hard line
// break, inline raw markup) declares its tree in X.tree.json instead —
// {"emit": [node…]}, node = "text" | {"text", "span"} | {"kind", "args",
// "kids", "span"} — encoded with the runtime's own OpBuf.
import { execFileSync } from 'node:child_process';
import { readdirSync, readFileSync, writeFileSync, existsSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execute } from '../runtime/src/worker/executor.mjs';
import { OpBuf } from '../runtime/src/shared/opbuf.mjs';
import { KIND } from '../runtime/src/shared/ops.gen.mjs';

function opsFromTree(tree) {
  const ob = new OpBuf();
  const make = (n) => {
    if (typeof n === 'string') return ob.makeText(n);
    let node;
    if ('text' in n) node = ob.makeText(n.text);
    else {
      if (KIND[n.kind] === undefined) throw new Error(`tree.json: unknown kind ${n.kind}`);
      node = ob.makeNode(KIND[n.kind], n.args ?? {}, (n.kids ?? []).map(make));
    }
    if (n.span) ob.span(node, n.span[0], n.span[1]);
    return node;
  };
  for (const n of tree.emit) ob.emitNode(make(n));
  return ob.finalize();
}

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
  const treePath = tsm.replace(/\.tsm$/, '.tree.json');
  let ops;
  if (existsSync(treePath)) {
    ops = Buffer.from(opsFromTree(JSON.parse(readFileSync(treePath, 'utf8'))));
  } else {
    const program = new Uint8Array(execFileSync(tsrc, ['--stage=program', tsm]));
    const js = () => execFileSync(tsrc, ['--stage=js', tsm], { encoding: 'utf8' });
    ops = Buffer.from(await execute({ program, js }, { baseDir: dirname(tsm), rootDir: root }));
  }
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
