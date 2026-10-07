#!/usr/bin/env node
// Every generator in one place (docs/remediation/PLAN.md gate G9, MD-05).
//   node tools/gen-all.mjs           regenerate
//   node tools/gen-all.mjs --check   fail if any generated file is stale
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const check = process.argv.includes('--check');
const GENERATORS = ['tools/gen-schema.mjs', 'tools/gen-syntax.mjs', 'tools/gen-languages.mjs', 'tools/gen-res.mjs', 'tools/gen-lower.mjs', 'tools/mathdict.py', 'tools/gen-grammars.mjs', 'tools/ucdc.mjs', 'tools/gen-skill.mjs'];
let failed = 0;
for (const g of GENERATORS) {
  try {
    const exe = g.endsWith('.py') ? 'python3' : process.execPath;  // mathdict.py: plain python3
    execFileSync(exe, [join(root, g), ...(check ? ['--check'] : [])], { stdio: 'inherit' });
  } catch {
    failed++;
  }
}
if (failed) process.exit(1);
console.log(check ? 'gen-all: generated files are current' : 'gen-all: done');
