#!/usr/bin/env node
// Every generator in one place (docs/remediation/PLAN.md gate G9, MD-05).
//   node tools/gen-all.mjs           regenerate
//   node tools/gen-all.mjs --check   fail if any generated file is stale
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const check = process.argv.includes('--check');
const GENERATORS = ['tools/gen-schema.mjs', 'tools/gen-syntax.mjs', 'tools/gen-grammars.mjs'];
let failed = 0;
for (const g of GENERATORS) {
  try {
    execFileSync(process.execPath, [join(root, g), ...(check ? ['--check'] : [])], { stdio: 'inherit' });
  } catch {
    failed++;
  }
}
if (failed) process.exit(1);
console.log(check ? 'gen-all: generated files are current' : 'gen-all: done');
