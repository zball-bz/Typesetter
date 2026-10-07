// The module graph of an entry, by a lexical scan (plan P3-21, D-I09): the
// files a static export copies for hydration (tools/lib/static-page.mjs),
// and the files a document's #use module imports — renderTsm lists them in
// its manifest, so a page publishes them beside the module. Followed: static `import …
// from '…'` / `export … from '…'`, `import('…')` with a literal specifier,
// and `new URL('…', import.meta.url)` (a worker, a sibling asset). A
// dynamic import whose specifier is not a literal cannot be followed: it is
// reported, and the caller copies what it needs some other way. Bare
// specifiers (node:fs, packages) and URLs with a scheme are not files; an
// import whose specifier starts with a comment (/* @vite-ignore */, /* a
// document's module */) is a deliberate one of user code, not followed.
import { readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';

const STATIC = /\b(?:import|export)\s[^'";]*?\bfrom\s*['"]([^'"]+)['"]|\bimport\s*['"]([^'"]+)['"]/g;
const DYNAMIC = /(?<![.\w$])import\s*\(\s*([^)]*?)\s*\)/g;  // (not a method named import)
const URL_REF = /new\s+URL\s*\(\s*['"]([^'"]+)['"]\s*,\s*import\.meta\.url\s*\)/g;
const literal = (s) => /^['"][^'"]+['"]$/.test(s) ? s.slice(1, -1) : null;
const isFile = (spec) => spec.startsWith('.') || spec.startsWith('/');

export async function moduleGraph(entry) {
  const files = new Set();
  const warnings = [];
  const todo = [resolve(entry)];
  while (todo.length) {
    const file = todo.pop();
    if (files.has(file)) continue;
    files.add(file);
    if (!/\.m?js$/.test(file)) continue;  // an asset (wasm, a font): no imports of its own
    let text;
    try {
      text = await readFile(file, 'utf8');
    } catch {
      warnings.push(`module-graph: ${file} cannot be read`);
      files.delete(file);
      continue;
    }
    const add = (spec) => {
      // (a directory — an asset folder the caller copies whole — is no module)
      if (spec && isFile(spec) && !spec.endsWith('/')) todo.push(resolve(dirname(file), spec));
    };
    for (const m of text.matchAll(STATIC)) add(m[1] ?? m[2]);
    for (const m of text.matchAll(URL_REF)) add(m[1]);
    for (const m of text.matchAll(DYNAMIC)) {
      const arg = m[1];
      const lit = literal(arg);
      if (lit) add(lit);
      else if (!/new\s+URL\s*\(/.test(arg) && !/^\/\*/.test(arg)) warnings.push(`module-graph: ${file}: a dynamic import of ${arg.slice(0, 40)} is not followed`);
    }
  }
  return { files: [...files], warnings };
}
