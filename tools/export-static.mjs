#!/usr/bin/env node
// Static export (pages-design.md §3; plan P3-36, design T7 S14): build-time
// semantic HTML — resolver-complete (numbers, refs, TOC, token spans), no
// browser, no canvas, no typeset pass — through runtime/src/node/export.mjs
// (exportStatic, the function a site generator calls). --hydrate (default)
// ships the runtime alongside so the page progressively upgrades to the
// typeset rendering client-side, with the bundle's resolved settings.
//
//   node tools/export-static.mjs post.tsm -o out/ [--no-hydrate] [--title T]
//        [--settings site.json] [--profile page|feed] [--template layout.mjs] [--embed]
// --settings: the settings document (docs/settings-table.md).
// --profile feed: a feed item — formulas as source, no hydration, the
//   article alone (unless a template says otherwise).
// --template: a module whose default export (parts) → html wraps the page
//   (parts: lang, title, head, article, root, hydrate, styles, settings,
//   docinfo, profile).
// --embed: the document's images inlined as data: URIs (else copied).
import { readFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

const args = process.argv.slice(2);
const inputs = [];
let outDir = 'out';
let hydrate = true;
let title = null;
let settingsPath = null;
let profile = 'page';
let templatePath = null;
let embed = false;
for (let i = 0; i < args.length; i++) {
  if (args[i] === '-o') outDir = args[++i];
  else if (args[i] === '--no-hydrate') hydrate = false;
  else if (args[i] === '--title') title = args[++i];
  else if (args[i] === '--settings') settingsPath = args[++i];
  else if (args[i] === '--profile') profile = args[++i];
  else if (args[i] === '--template') templatePath = args[++i];
  else if (args[i] === '--embed') embed = true;
  else inputs.push(args[i]);
}
if (inputs.length !== 1) {
  console.error('usage: export-static.mjs <post.tsm> [-o out/] [--no-hydrate] [--title T] [--settings f.json] ' +
    '[--profile page|feed] [--template layout.mjs] [--embed]');
  process.exit(2);
}

const { renderTsm } = await import(join(root, 'runtime/src/node/render.mjs'));
const { writePage, copyAssets, readSettings } = await import('./lib/static-page.mjs');

const settings = await readSettings(settingsPath);
const source = await readFile(inputs[0], 'utf8');
const docDir = dirname(resolve(inputs[0]));
const bundle = await renderTsm(source, { settings, baseDir: docDir, rootDir: docDir, profile });
if (bundle.diagnostics.trim()) console.error(bundle.diagnostics.trim());
if (!bundle.ok) process.exit(1);

const template = templatePath ? (await import(pathToFileURL(resolve(templatePath)).href)).default : undefined;
const page = await writePage(bundle, {
  outDir, docDir, source, title, template, hydrate, embedResources: embed,
  fallbackTitle: inputs[0].replace(/^.*\//, '').replace(/\.tsm$/, ''),
});
await copyAssets({ outDir, math: page.math, hydrate: page.hydrate });
console.log(`exported ${inputs[0]} -> ${resolve(outDir)}/index.html` +
            (page.copy.length ? ` (+${page.copy.length} resource${page.copy.length > 1 ? 's' : ''})` : '') +
            (page.embedded ? ` (${page.embedded} embedded)` : '') + (page.hydrate ? ' (+assets)' : ''));
