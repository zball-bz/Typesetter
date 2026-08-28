#!/usr/bin/env node
// Full-pipeline smoke over the corpus: compile (native tsrc) → execute (real
// executor) → ingest+typeset+render (native, mock metrics). Reports crashes,
// error diagnostics, and empty outputs. testing.md §1: every finding here
// graduates into a fixture.
import { execFileSync } from 'node:child_process';
import { readdirSync, writeFileSync, rmSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { execute } from '../runtime/src/worker/executor.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const tsrc = join(root, 'engine/build/tsrc');
const dir = join(root, 'test/corpus/typst');
const run = (args) => execFileSync(tsrc, args, { encoding: 'utf8', timeout: 10000 });

let pass = 0;
const findings = [];
for (const f of readdirSync(dir).filter((f) => f.endsWith('.tsm')).sort()) {
  const p = join(dir, f);
  try {
    const js = run(['--stage=js', p]);
    const ops = Buffer.from(await execute(js, { rootDir: root }));
    const opsPath = join(tmpdir(), 'corpus.ops');
    writeFileSync(opsPath, ops);
    const diags = run(['--stage=diags', p]);
    const html = run(['--stage=html', `--ops=${opsPath}`, '--width=300', p]);
    const errors = diags.split('\n').filter((l) => l.startsWith('error'));
    if (errors.length) findings.push({ f, kind: 'diag', detail: errors.join(' | ') });
    else if (!html.includes('tsr-line')) findings.push({ f, kind: 'empty', detail: 'no lines rendered' });
    else pass++;
  } catch (e) {
    findings.push({ f, kind: 'crash', detail: String(e.message || e).slice(0, 160) });
  }
}
console.log(`corpus: ${pass} pass, ${findings.length} findings`);
for (const x of findings) console.log(`  [${x.kind}] ${x.f}: ${x.detail}`);
