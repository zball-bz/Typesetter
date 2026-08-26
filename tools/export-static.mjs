#!/usr/bin/env node
// Static export (pages-design.md §3): build-time semantic HTML — resolver-
// complete (numbers, refs, TOC, token spans), no browser, no canvas, no
// typeset pass. --hydrate (default) ships the runtime alongside so the
// page progressively upgrades to the typeset rendering client-side.
//
//   node tools/export-static.mjs post.tsm -o out/ [--no-hydrate] [--title T]
import { readFile, writeFile, mkdir, cp, access } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

const args = process.argv.slice(2);
const inputs = [];
let outDir = 'out';
let hydrate = true;
let title = null;
for (let i = 0; i < args.length; i++) {
  if (args[i] === '-o') outDir = args[++i];
  else if (args[i] === '--no-hydrate') hydrate = false;
  else if (args[i] === '--title') title = args[++i];
  else inputs.push(args[i]);
}
if (inputs.length !== 1) {
  console.error('usage: export-static.mjs <post.tsm> [-o out/] [--no-hydrate] [--title T]');
  process.exit(2);
}

const [{ renderTsm }, { TSR_CSS, TSR_CJK_FONT }] = await Promise.all([
  import(join(root, 'runtime/src/node/render.mjs')),
  import(join(root, 'runtime/src/main/shell.mjs')),
]);

const source = await readFile(inputs[0], 'utf8');
const { html: semantic, diags, ok } = await renderTsm(source);
if (diags.trim()) console.error(diags.trim());
if (!ok) process.exit(1);

const pageTitle = title ?? inputs[0].replace(/^.*\//, '').replace(/\.tsm$/, '');
const escapedSrc = source.replace(/<\/script/gi, '<\\/script');
const hydrateBlock = hydrate ? `
<script type="text/plain" id="tsr-src">${escapedSrc}</script>
<script type="module">
import { createEngine, TSR_CJK_FONT } from './assets/runtime/src/main/shell.mjs';
const el = document.getElementById('tsr-root');
const engine = createEngine();
engine.typeset(document.getElementById('tsr-src').textContent, el, {
  fontFamily: '"Crimson Text", Georgia, serif',
  cjkFontFamily: TSR_CJK_FONT,
  progressive: false,  // the static semantic page IS the first paint
}).catch((e) => console.warn('tsr hydrate failed; static page stands', e));
</script>` : '';

const html = `<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${pageTitle.replace(/[<&]/g, '')}</title>
<style>
${TSR_CSS}
body { margin: 0 auto; max-width: 42em; padding: 2em 1em;
       font-family: "Crimson Text", Georgia, serif; }
#tsr-root { --tsr-cjk-font: ${TSR_CJK_FONT.replace(/"/g, "'")}; }
.tsr-flow img { max-width: 100%; height: auto; }
.tsr-flow pre { overflow-x: auto; }
.tsr-flow code .tsr-err { color: #b00; }
</style>
</head>
<body>
<article id="tsr-root">
${semantic}</article>${hydrateBlock}
</body>
</html>
`;

await mkdir(outDir, { recursive: true });
await writeFile(join(outDir, 'index.html'), html);

if (hydrate) {
  const assets = join(outDir, 'assets');
  await cp(join(root, 'runtime/src'), join(assets, 'runtime/src'), { recursive: true });
  await mkdir(join(assets, 'engine/build-wasm'), { recursive: true });
  for (const f of ['typesetter.js', 'typesetter.wasm'])
    await cp(join(root, 'engine/build-wasm', f), join(assets, 'engine/build-wasm', f));
  await cp(join(root, 'fonts/euler-math.woff2'), join(assets, 'fonts/euler-math.woff2'));
  try {
    await access(join(root, 'runtime/assets/hl'));
    await cp(join(root, 'runtime/assets/hl'), join(assets, 'runtime/assets/hl'),
             { recursive: true });
  } catch { /* hl assets not built: hydrated code stays plain */ }
}
console.log(`exported ${inputs[0]} -> ${resolve(outDir)}/index.html` +
            (hydrate ? ' (+assets)' : ''));
