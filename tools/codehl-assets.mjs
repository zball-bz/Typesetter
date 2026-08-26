#!/usr/bin/env node
// Builds the code-highlighting assets (code-design.md §2/§6 CH3):
//   runtime/assets/hl/tree-sitter-<lang>.wasm   emcc SIDE_MODULE per grammar
//   runtime/assets/hl/<lang>.scm                highlights query (inherits flattened)
//   runtime/assets/hl/web-tree-sitter.{js,wasm} runtime copied from node_modules
// Grammars come from checked-in parser.c in the npm grammar packages — the
// same tables the native tests link statically. Requires emcc (emsdk).
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, copyFileSync, existsSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = join(root, 'runtime/assets/hl');
mkdirSync(out, { recursive: true });

const emcc = process.env.EMCC ||
  (existsSync('/home/dev/emsdk/upstream/emscripten/emcc')
    ? '/home/dev/emsdk/upstream/emscripten/emcc' : 'emcc');

// name → {srcDir, entry, scm: [paths to concatenate, base-first]}
const nm = (p) => join(root, 'node_modules', p);
const GRAMMARS = {
  json: { srcDir: join(root, 'third_party/grammars/json'), srcs: ['parser.c'],
          scm: [join(root, 'third_party/grammars/json/highlights.scm')] },
  javascript: { srcDir: nm('tree-sitter-javascript/src'), srcs: ['parser.c', 'scanner.c'],
                scm: [nm('tree-sitter-javascript/queries/highlights.scm')] },
  typescript: { srcDir: nm('tree-sitter-typescript/typescript/src'), srcs: ['parser.c', 'scanner.c'],
                scm: [nm('tree-sitter-javascript/queries/highlights.scm'),
                      nm('tree-sitter-typescript/queries/highlights.scm')] },
  python: { srcDir: nm('tree-sitter-python/src'), srcs: ['parser.c', 'scanner.c'],
            scm: [nm('tree-sitter-python/queries/highlights.scm')] },
  cpp: { srcDir: nm('tree-sitter-cpp/src'), srcs: ['parser.c', 'scanner.c'],
         scm: [nm('tree-sitter-c/queries/highlights.scm'),
               nm('tree-sitter-cpp/queries/highlights.scm')] },
  rust: { srcDir: nm('tree-sitter-rust/src'), srcs: ['parser.c', 'scanner.c'],
          scm: [nm('tree-sitter-rust/queries/highlights.scm')] },
};

for (const [name, g] of Object.entries(GRAMMARS)) {
  const srcs = g.srcs.map((s) => join(g.srcDir, s)).filter(existsSync);
  const wasm = join(out, `tree-sitter-${name}.wasm`);
  execFileSync(emcc, [
    '-Os', '-fno-exceptions', '-sSIDE_MODULE=2',
    `-sEXPORTED_FUNCTIONS=_tree_sitter_${name}`,
    '-I', g.srcDir, '-I', join(g.srcDir, '..'),
    ...srcs, '-o', wasm,
  ], { stdio: ['ignore', 'inherit', 'inherit'] });
  // flatten query inheritance (web-tree-sitter has no `; inherits:` support)
  const scm = g.scm.filter(existsSync)
    .map((p) => readFileSync(p, 'utf8').replace(/^;+\s*inherits:.*$/m, ''))
    .join('\n');
  writeFileSync(join(out, `${name}.scm`), scm);
  console.log(`${name}: ${(statSync(wasm).size / 1024).toFixed(0)}KB wasm, ${(scm.length / 1024).toFixed(1)}KB scm`);
}
copyFileSync(nm('web-tree-sitter/web-tree-sitter.js'), join(out, 'web-tree-sitter.js'));
copyFileSync(nm('web-tree-sitter/web-tree-sitter.wasm'), join(out, 'web-tree-sitter.wasm'));
console.log('runtime copied; done');
