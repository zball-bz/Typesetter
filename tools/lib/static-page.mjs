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

// the page (plan P3-36: runtime/src/node/export.mjs's exportStatic, the one
// a site generator uses too) written into outDir with its resources
export async function writePage(bundle, { outDir, name = 'index.html', docDir, log = console.error, ...opts }) {
  const { exportStatic } = await import(join(root, 'runtime/src/node/export.mjs'));
  const page = await exportStatic(bundle, { docDir, ...opts });
  await mkdir(outDir, { recursive: true });
  await writeFile(join(outDir, name), page.html);
  for (const { from, to } of page.copy) {
    const dest = join(outDir, to);
    try {
      await mkdir(dirname(dest), { recursive: true });
      await cp(from, dest);
    } catch { log(`export: ${to} not copied`); }
  }
  return page;
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
  const { moduleGraph } = await import('../../runtime/src/node/module-graph.mjs');
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
