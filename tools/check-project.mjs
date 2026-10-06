#!/usr/bin/env node
// The project driver (plan P3-31): a book of two chapters built with
// tools/tsm-project.mjs — references across the files resolve, numbered as
// the importer numbers them with the producer's start, linking to the other
// page; the second chapter's counters continue the first's; the manifests
// are written. Gate G6 runs it beside check-export.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dir = mkdtempSync(join(tmpdir(), 'tsm-project-'));
const fail = (m) => { console.error(`check-project: ${m}`); process.exitCode = 1; };
try {
  writeFileSync(join(dir, 'one.tsm'), '= First <ch-one>\n\n$ a = b $ <eq-a>\n\nSee @sec-two and @eq-b.\n');
  writeFileSync(join(dir, 'two.tsm'), '= Second <ch-two>\n\n== Part <sec-two>\n\n$ c = d $ <eq-b>\n\nBack to @eq-a and @ch-one.\n');
  writeFileSync(join(dir, 'tsm.project.json'), JSON.stringify({
    files: ['one.tsm', 'two.tsm'], out: 'site', settings: { doc: { lang: 'en' } }, urls: { two: 'second/' },
  }));
  execFileSync(process.execPath, [join(root, 'tools/tsm-project.mjs'), 'build', join(dir, 'tsm.project.json'), '--no-hydrate'],
               { stdio: ['ignore', 'ignore', 'inherit'] });
  const one = readFileSync(join(dir, 'site', 'one.html'), 'utf8');
  const two = readFileSync(join(dir, 'site', 'two.html'), 'utf8');
  if (!one.includes('href="second/#tsr-sec-two"')) fail('one.html: no link to the other page\'s section');
  if (!/§\s*2\.1/.test(one.replace(/<[^>]+>/g, ''))) fail('one.html: the section is not numbered 2.1 (its start)');
  if (!one.replace(/<[^>]+>/g, '').includes('Eq. (2)')) fail('one.html: the other chapter\'s equation is not (2)');
  if (!two.includes('href="one.html#tsr-eq-a"')) fail('two.html: no link back');
  if (!two.replace(/<[^>]+>/g, '').includes('(2)')) fail('two.html: its own equation does not continue the count');
  const m = JSON.parse(readFileSync(join(dir, 'site', 'two.labels.json'), 'utf8'));
  if (m.doc !== 'two' || !m.labels.some((l) => l.label === 'sec-two')) fail('two.labels.json: not its manifest');
  // a document outside the build imports a manifest itself ($.labels.import)
  const { renderTsm } = await import('../runtime/src/node/render.mjs');
  const r = await renderTsm('#{ $.labels.import("site/two.labels.json") }\n\nSee @sec-two.\n',
                            { settings: { doc: { lang: 'en' }, project: { urls: { two: 'second/' } } }, baseDir: dir, rootDir: dir });
  if (!r.html.includes('href="second/#tsr-sec-two"')) fail(`$.labels.import: no link to the imported label (${r.diagnostics.trim()})`);
  const bad = await renderTsm('#{ $.labels.import("missing.labels.json") }\n\nSee @sec-two.\n', { baseDir: dir, rootDir: dir });
  if (!bad.diagnostics.includes('labels-import')) fail('$.labels.import: a missing manifest says nothing');
  if (!process.exitCode) console.log('project: ok');
} finally {
  rmSync(dir, { recursive: true, force: true });
}
