#!/usr/bin/env node
// Plan P1-04 golden check (PLAN §4.4: mechanical changes are verified by a
// script): every changed html/paged golden differs from HEAD in its first
// line (the .tsr-doc root tag) only.
import { execFileSync } from 'node:child_process';

const changed = execFileSync('git', ['diff', '--name-only', 'HEAD', '--', 'test/golden'], { encoding: 'utf8' })
  .split('\n').filter(Boolean);
let bad = 0;
for (const f of changed) {
  if (!/\.(html|paged)\.txt$/.test(f)) { console.log(`unexpected change: ${f}`); bad++; continue; }
  const old = execFileSync('git', ['show', `HEAD:${f}`], { encoding: 'utf8' }).split('\n');
  const now = execFileSync('cat', [f], { encoding: 'utf8' }).split('\n');
  const rootOk = /^<div class="tsr-doc( tsr-paged)?" lang="[^"]+" style="--tsr-font-body:[^"]*;font-size:[0-9.]+px">$/.test(now[0]);
  if (old.length !== now.length || !rootOk || old.slice(1).join('\n') !== now.slice(1).join('\n')) {
    console.log(`more than the root line changed: ${f}`);
    bad++;
  }
}
console.log(`root-line-only: ${changed.length} files, ${bad} problems`);
process.exit(bad ? 1 : 0);
