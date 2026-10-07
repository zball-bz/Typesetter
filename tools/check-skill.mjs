#!/usr/bin/env node
// The syntax skill's examples (skills/tsm/SKILL.md, tools/gen-skill.mjs)
// render clean: every ```tsm fence (a longer outer fence may show fences
// inside) is rendered by the engine and must give no diagnostic at all —
// an example that the engine reads otherwise than the skill says fails
// here. Resources resolve beside the citation fixtures (refs.json). Gate G6.
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { renderTsm } from '../runtime/src/node/render.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const docDir = join(root, 'test/fixtures/cite');
const lines = readFileSync(join(root, 'skills/tsm/SKILL.md'), 'utf8').split('\n');
const examples = [];
for (let i = 0; i < lines.length; i++) {
  const open = /^(`{3,})tsm\s*$/.exec(lines[i]);
  if (!open) continue;
  const n = open[1].length;
  const body = [];
  let j = i + 1;
  for (; j < lines.length && !new RegExp(`^\`{${n},}\\s*$`).test(lines[j]); j++) body.push(lines[j]);
  examples.push({ line: i + 1, src: body.join('\n') + '\n' });
  i = j;
}
let failures = 0;
for (const ex of examples) {
  const r = await renderTsm(ex.src, { baseDir: docDir, rootDir: docDir });
  if (!r.ok || r.diagnostics.trim()) {
    failures++;
    console.error(`FAIL skills/tsm/SKILL.md:${ex.line}\n  ${r.diagnostics.trim().split('\n').join('\n  ')}`);
  }
}
if (examples.length < 10) {
  console.error(`check-skill: only ${examples.length} examples found`);
  failures++;
}
console.log(`check-skill: ${examples.length} examples, ${failures} failing`);
process.exit(failures ? 1 : 0);
