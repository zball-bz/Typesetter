#!/usr/bin/env node
// Static export (pages-design.md §3): build-time semantic HTML — resolver-
// complete (numbers, refs, TOC, token spans), no browser, no canvas, no
// typeset pass. --hydrate (default) ships the runtime alongside so the
// page progressively upgrades to the typeset rendering client-side.
//
//   node tools/export-static.mjs post.tsm -o out/ [--no-hydrate] [--title T]
//                                 [--settings site.json]
// --settings: the settings document (docs/settings-table.md) — document
// language, fonts, sizes — used for the static page and passed to hydration.
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

const args = process.argv.slice(2);
const inputs = [];
let outDir = 'out';
let hydrate = true;
let title = null;
let settingsPath = null;
for (let i = 0; i < args.length; i++) {
  if (args[i] === '-o') outDir = args[++i];
  else if (args[i] === '--no-hydrate') hydrate = false;
  else if (args[i] === '--title') title = args[++i];
  else if (args[i] === '--settings') settingsPath = args[++i];
  else inputs.push(args[i]);
}
if (inputs.length !== 1) {
  console.error('usage: export-static.mjs <post.tsm> [-o out/] [--no-hydrate] [--title T] [--settings f.json]');
  process.exit(2);
}

const { renderTsm } = await import(join(root, 'runtime/src/node/render.mjs'));
const { pageHtml, copyResources, copyAssets, readSettings } = await import('./lib/static-page.mjs');

const settings = await readSettings(settingsPath);
const source = await readFile(inputs[0], 'utf8');
const docDir = dirname(resolve(inputs[0]));
const rendered = await renderTsm(source, { settings, baseDir: docDir, rootDir: docDir });
if (rendered.diagnostics.trim()) console.error(rendered.diagnostics.trim());
if (!rendered.ok) process.exit(1);

const { html, hasMath } = await pageHtml({
  rendered, source, settings, title, hydrate,
  fallbackTitle: inputs[0].replace(/^.*\//, '').replace(/\.tsm$/, ''),
});
await mkdir(outDir, { recursive: true });
await writeFile(join(outDir, 'index.html'), html);
const copied = await copyResources({ manifest: rendered.manifest, docDir, outDir });
await copyAssets({ outDir, math: hasMath, hydrate });
console.log(`exported ${inputs[0]} -> ${resolve(outDir)}/index.html` +
            (copied ? ` (+${copied} resource${copied > 1 ? 's' : ''})` : '') + (hydrate ? ' (+assets)' : ''));
