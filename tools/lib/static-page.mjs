// The static page (pages-design.md §3; plans P3-21, P3-27, P3-31): what
// tools/export-static.mjs writes for one document and tools/tsm-project.mjs
// for each document of a project — the semantic page (resolver-complete, no
// browser), its rules, the math font when it sets formulas, and optionally
// the runtime that upgrades it to the typeset rendering client-side.
import { readFile, writeFile, mkdir, cp, access, realpath } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { MATH_FONT } from '../../runtime/src/shared/mathfont.gen.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const jsonIn = (v) => JSON.stringify(v).replace(/</g, '\\u003c');

// the page: rendered — renderTsm's result; source — for hydration; inputs —
// its declared inputs (a project page's labels), given to the hydrated
// engine too; assets — where the shared assets are, relative to the page
export async function pageHtml({ rendered, source, settings, title, fallbackTitle, hydrate = true, inputs, assets = 'assets' }) {
  const [{ TSR_CSS, THEME_CSS }, { settingOf }] = await Promise.all([
    import(join(root, 'runtime/src/main/shell.mjs')),
    import(join(root, 'runtime/src/shared/settings.gen.mjs')),
  ]);
  const { html: semantic, css: rulesCss, docinfo = {} } = rendered;
  const bodyFont = settingOf(settings, 'fonts.body');
  const cjkFont = settingOf(settings, 'fonts.cjk');
  // (plan P3-30) the engine decides the language — the document's own, the
  // host's, or detected (doc.lang: auto); an undecided one is und, never "auto"
  const lang = docinfo.lang || (settingOf(settings, 'doc.lang') === 'auto' ? 'und' : settingOf(settings, 'doc.lang'));
  const pageTitle = title ?? (docinfo.title || fallbackTitle || '');
  const hydrateBlock = hydrate ? `
<script type="text/plain" id="tsr-src">${source.replace(/<\/script/gi, '<\\/script')}</script>
<script type="module">
import { createEngine } from './${assets}/runtime/src/main/shell.mjs';
const el = document.getElementById('tsr-root');
const engine = createEngine();
engine.typeset(document.getElementById('tsr-src').textContent, el, {
  settings: ${jsonIn(settings)},${inputs ? `\n  inputs: ${jsonIn(inputs)},` : ''}
  progressive: false,  // the static semantic page IS the first paint
}).catch((e) => console.warn('tsr hydrate failed; static page stands', e));
</script>` : '';
  // (plan P3-27) formulas as boxes on the static page too: their glyphs in
  // the bundled math font, beside the page (the hydrated page declares it again)
  const hasMath = semantic.includes('class="tsr-math');
  const mathFace = hasMath
    ? `@font-face { font-family: ${JSON.stringify(MATH_FONT.family)}; src: url(${JSON.stringify(`${assets}/${MATH_FONT.file}`)}); }\n`
    : '';
  const html = `<!doctype html>
<html lang="${lang.replace(/[^A-Za-z0-9-]/g, '')}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${pageTitle.replace(/[<&]/g, '')}</title>
<style>
${mathFace}${TSR_CSS}
${THEME_CSS}
body { margin: 0 auto; max-width: 42em; padding: 2em 1em;
       font-family: ${bodyFont.replace(/[<>{};]/g, '')}; }
#tsr-root { --tsr-cjk-font: ${cjkFont.replace(/"/g, "'").replace(/[<>{};]/g, '')}; }
.tsr-flow img { max-width: 100%; height: auto; }
.tsr-flow pre { overflow-x: auto; }
/* the rules (plan P3-01: engine defaults, host rules, the document's $.set) */
${rulesCss}</style>
</head>
<body>
<article id="tsr-root">
${semantic}</article>${hydrateBlock}
</body>
</html>
`;
  return { html, hasMath };
}

// (plan P3-21) the document's own resources beside it: what the manifest
// lists by a relative reference inside the document's folder (its images,
// its loads), copied to the same place under outDir — the file's real path
// too, so a symbolic link does not export a file from elsewhere
export async function copyResources({ manifest, docDir, outDir, log = console.error }) {
  let copied = 0;
  const realDocDir = await realpath(docDir);
  for (const m of manifest) {
    if (m.status === 'denied' || /^[a-z][a-z0-9+.-]*:/i.test(m.url) || m.role === 'font') continue;
    // (a load names its file; a reference is as written: /site-root or relative)
    const from = m.url.startsWith(docDir + '/') ? m.url : resolve(docDir, m.url.startsWith('/') ? '.' + m.url : m.url);
    if (!from.startsWith(docDir + '/')) continue;
    const to = join(outDir, from.slice(docDir.length + 1));
    try {
      if (!(await realpath(from)).startsWith(realDocDir + '/')) {
        log(`export: ${m.role} ${m.url} not copied (outside the document's folder)`);
        continue;
      }
      await mkdir(dirname(to), { recursive: true });
      await cp(from, to);
      copied++;
    } catch { log(`export: ${m.role} ${m.url} not copied (missing)`); }
  }
  return copied;
}

// the shared assets: the math font (a page that sets formulas, or hydrates),
// and for hydration the runtime's module graph, the engine and the
// highlighter's assets
export async function copyAssets({ outDir, math, hydrate, log = console.error }) {
  const assets = join(outDir, 'assets');
  if (math || hydrate) {  // the math font manifest (P1-23)
    await mkdir(dirname(join(assets, MATH_FONT.file)), { recursive: true });
    await cp(join(root, MATH_FONT.file), join(assets, MATH_FONT.file));
  }
  if (!hydrate) return;
  // (plan P3-21, D-I09) the runtime's module graph from its entry, by a
  // lexical scan of its imports (a dynamic import it cannot follow warns)
  const { moduleGraph } = await import('./module-graph.mjs');
  const graph = await moduleGraph(join(root, 'runtime/src/main/shell.mjs'));
  for (const w of graph.warnings) log(w);
  for (const f of graph.files) {
    if (!f.startsWith(root + '/')) continue;
    const to = join(assets, f.slice(root.length + 1));
    await mkdir(dirname(to), { recursive: true });
    await cp(f, to);
  }
  await mkdir(join(assets, 'engine/build-wasm'), { recursive: true });
  for (const f of ['typesetter.js', 'typesetter.wasm'])
    await cp(join(root, 'engine/build-wasm', f), join(assets, 'engine/build-wasm', f));
  try {
    await access(join(root, 'runtime/assets/hl'));
    await cp(join(root, 'runtime/assets/hl'), join(assets, 'runtime/assets/hl'), { recursive: true });
  } catch { /* hl assets not built: hydrated code stays plain */ }
}

export async function readSettings(pathOrObject) {
  if (!pathOrObject) return {};
  if (typeof pathOrObject === 'object') return pathOrObject;
  return JSON.parse(await readFile(pathOrObject, 'utf8'));
}
export { writeFile, mkdir };
