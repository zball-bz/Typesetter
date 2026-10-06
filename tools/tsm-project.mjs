#!/usr/bin/env node
// A project of .tsm files (plan P3-31; D-S07: one configuration,
// tsm.project.json, shared with the static export): its documents rendered
// together — references across files, numbering continued — each to a
// static page beside the others, with its labels manifest.
//
//   node tools/tsm-project.mjs build [tsm.project.json] [-o out/] [--no-hydrate]
//
// tsm.project.json:
//   { "files": ["ch1.tsm", "ch2.tsm"],         // book order; a document's key is its file name
//     "out": "out",                            // where the pages go (default: out/)
//     "settings": { … } | "site.json",         // the settings document (docs/settings-table.md)
//     "urls": { "ch2": "two/" },               // where a document is published (default: <key>.html)
//     "continue": ["heading", "figure", …],    // counters numbered across files (default: heading, figure, table, equation)
//     "offset": { "heading": 4 } }             // where the book's counters start
import { readFile } from 'node:fs/promises';
import { basename, dirname, join, resolve } from 'node:path';

const args = process.argv.slice(2);
if (args[0] !== 'build') {
  console.error('usage: tsm-project.mjs build [tsm.project.json] [-o out/] [--no-hydrate]');
  process.exit(2);
}
let configPath = 'tsm.project.json', outOpt = null, hydrate = true;
for (let i = 1; i < args.length; i++) {
  if (args[i] === '-o') outOpt = args[++i];
  else if (args[i] === '--no-hydrate') hydrate = false;
  else configPath = args[i];
}
const config = JSON.parse(await readFile(configPath, 'utf8'));
const base = dirname(resolve(configPath));
if (!Array.isArray(config.files) || !config.files.length) {
  console.error(`${configPath}: "files" lists the documents in book order`);
  process.exit(2);
}
const { renderProject, CONTINUED_COUNTERS } = await import('../runtime/src/node/project.mjs');
const { writePage, copyAssets, readSettings, writeFile, mkdir } = await import('./lib/static-page.mjs');

const settings = await readSettings(typeof config.settings === 'string' ? join(base, config.settings) : config.settings);
const files = [];
for (const f of config.files) {
  const path = resolve(base, f);
  files.push({ doc: basename(f).replace(/\.tsm$/, ''), path, source: await readFile(path, 'utf8'), baseDir: dirname(path) });
}
const project = await renderProject({
  files, settings, urls: config.urls ?? {}, continue: config.continue ?? CONTINUED_COUNTERS, offset: config.offset ?? {},
});
for (const d of project.diagnostics) console.error(d);

const outDir = resolve(base, outOpt ?? config.out ?? 'out');
await mkdir(outDir, { recursive: true });
let math = false, failed = false;
for (let i = 0; i < files.length; i++) {
  const f = files[i], r = project.docs[i];
  if (r.diagnostics.trim()) console.error(`${f.doc}:\n${r.diagnostics.trim()}`);
  if (!r.ok) failed = true;
  // its page: the project's settings, its manifests as the hydrated engine's input
  const others = Object.entries(project.manifests).filter(([k]) => k !== f.doc).map(([, m]) => m);
  // (plan P3-36) exportStatic: hydration gets the bundle's resolved settings
  // (its project rows: the render's), the others' manifests as its input
  const page = await writePage(r, {
    outDir, name: `${f.doc}.html`, docDir: f.baseDir, source: f.source, hydrate, fallbackTitle: f.doc,
    inputs: { labels: `[${others.join(',')}]` },
  });
  math = math || page.math;
  await writeFile(join(outDir, `${f.doc}.labels.json`), project.manifests[f.doc]);
}
await copyAssets({ outDir, math, hydrate });
console.log(`built ${files.length} document(s) -> ${outDir}`);
process.exit(failed ? 1 : 0);
