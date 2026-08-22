#!/usr/bin/env node
// ops.def → runtime/src/shared/ops.gen.mjs (single source of truth: architecture §3).
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const def = readFileSync(join(root, 'engine/src/ops/ops.def'), 'utf8');

const groups = { OP: {}, KIND: {}, ARGK: {} };
for (const m of def.matchAll(/^(OP|KIND|ARGK)\((\w+),\s*(\d+)\)/gm)) {
  groups[m[1]][m[2]] = Number(m[3]);
}
const version = Number(def.match(/OPS_VERSION\((\d+)\)/)?.[1] ?? 1);

const emit = (name, obj) =>
  `export const ${name} = Object.freeze(${JSON.stringify(obj, null, 2)});\n`;

const out =
  '// GENERATED from engine/src/ops/ops.def by tools/gen-ops-ts.mjs — do not edit.\n' +
  `export const OPS_VERSION = ${version};\n` +
  emit('OP', groups.OP) +
  emit('KIND', groups.KIND) +
  emit('ARGK', groups.ARGK);

const dest = join(root, 'runtime/src/shared/ops.gen.mjs');
mkdirSync(dirname(dest), { recursive: true });
writeFileSync(dest, out);
console.log(`wrote ${dest} (version ${version})`);
