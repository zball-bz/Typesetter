#!/usr/bin/env node
// Packaging prep: stage the engine dist under vendor/ so the .vsix is
// self-contained (paths.js prefers vendor/ over the repo checkout).
import { execFileSync } from 'node:child_process';
import { rm, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const ext = join(here, '..');
const repo = join(ext, '..', '..');

execFileSync(process.execPath, [join(repo, 'tools/pack-dist.mjs')], { stdio: 'inherit' });
await rm(join(ext, 'vendor'), { recursive: true, force: true });
await mkdir(join(ext, 'vendor'), { recursive: true });
execFileSync('tar', ['xzf', join(repo, 'dist/typesetter-dist.tgz'), '-C', join(ext, 'vendor')]);
console.log('vendored engine dist into vendor/');
