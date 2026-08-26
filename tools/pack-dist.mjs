#!/usr/bin/env node
// Engine distribution tarball: everything a site generator needs to render
// .tsm at build time and hydrate in the browser — mirroring the repo layout
// so the runtime's relative imports hold verbatim.
//
//   node tools/pack-dist.mjs            → dist/typesetter-dist.tgz
//
// Consumed by the blog repo (zball-bz/zball-io): its CI downloads the
// rolling `engine-dist` release asset and unpacks it into vendor/.
import { rm, mkdir, cp, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const stage = join(root, 'dist/pkg');
await rm(join(root, 'dist'), { recursive: true, force: true });
await mkdir(stage, { recursive: true });

const parts = [
  ['engine/build-wasm/typesetter.js', 'engine/build-wasm/typesetter.js'],
  ['engine/build-wasm/typesetter.wasm', 'engine/build-wasm/typesetter.wasm'],
  ['runtime/src', 'runtime/src'],
  ['runtime/assets/hl', 'runtime/assets/hl'],
  ['fonts/euler-math.woff2', 'fonts/euler-math.woff2'],
];
for (const [from, to] of parts) {
  await mkdir(dirname(join(stage, to)), { recursive: true });
  await cp(join(root, from), join(stage, to), { recursive: true });
}

const sha = process.env.GITHUB_SHA?.slice(0, 9) ?? 'local';
await writeFile(join(stage, 'package.json'), JSON.stringify({
  name: '@zball/typesetter',
  version: '0.0.0-' + sha,
  type: 'module',
  exports: {
    '.': './runtime/src/node/render.mjs',
    './shell': './runtime/src/main/shell.mjs',
  },
}, null, 2) + '\n');

execFileSync('tar', ['czf', join(root, 'dist/typesetter-dist.tgz'), '-C', stage, '.']);
console.log('packed dist/typesetter-dist.tgz (' + sha + ')');
