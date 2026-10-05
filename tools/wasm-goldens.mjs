#!/usr/bin/env node
// WASM parity (plan P0-12): every fixture is broken by the WASM build (libc++,
// Emscripten) with the normative mock measurer and compared with the native
// goldens (libstdc++). Same inputs, same conventions as engine/test/tests.cc;
// a difference means the result depends on the toolchain (container
// iteration order, FP contraction, …).
//   node tools/wasm-goldens.mjs --check     fail on any breakpoint difference
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const modPath = join(root, 'engine/build-wasm/typesetter_debug.js');
if (!existsSync(modPath)) {
  console.error('wasm-goldens: engine/build-wasm/typesetter_debug.js missing (build G4 first)');
  process.exit(1);
}
const { default: create } = await import(pathToFileURL(modPath).href);
const M = await create();

const fixtures = join(root, 'test/fixtures');
function* walk(dir) {
  for (const e of readdirSync(dir).sort()) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.tsm')) yield p;
  }
}
const str = (s) => M.stringToNewUTF8(s);

let checked = 0;
const diffs = [];
for (const tsm of walk(fixtures)) {
  const rel = relative(fixtures, tsm).replace(/\.tsm$/, '');
  const stem = rel.split('/').pop();
  const goldenPath = join(root, 'test/golden', rel + '.breaks.txt');
  const opsPath = tsm.replace(/\.tsm$/, '.ops');
  if (!existsSync(goldenPath) || !existsSync(opsPath)) continue;
  const doc = M._tsr_doc_new();
  try {
    // conventions mirror the native runner (tests.cc)
    M._tsr_config(doc, 300, stem.includes('base18') ? 18 : 16, 0,
                  stem.includes('indent') ? 2 : 0);
    if (stem.includes('punct-full')) M._tsr_set_punct_compress(doc, 0);
    else if (stem.includes('punct-none')) M._tsr_set_punct_compress(doc, 2);
    if (stem.includes('snap')) M._tsr_set_snap_kerning(doc, 1);
    const sp = str(readFileSync(tsm, 'utf8'));
    M._tsr_compile(doc, sp);
    M._free(sp);
    const ops = readFileSync(opsPath);
    const op = M._malloc(ops.length);
    M.HEAPU8.set(ops, op);
    const ok = M._tsr_ingest(doc, op, ops.length) === 0;
    M._free(op);
    if (!ok) { diffs.push(`${rel}: ingest failed`); continue; }
    let done = false;
    for (let round = 0; round < 64 && !done; round++) {
      if (M._tsr_typeset(doc) === 0) { done = true; break; }
      const req = JSON.parse(M.UTF8ToString(M._tsr_measure_requests(doc)));
      // images: the native stub's 512×384; tokens: plain (they never reach
      // a text unit's breaks)
      for (const im of req.images ?? []) M._tsr_provide_image(doc, im.id, 512, 384);
      for (const t of req.tokens ?? []) M._tsr_provide_tokens(doc, t.id, 0, 0);
      M._tsr_debug_mock_measure(doc);
    }
    if (!done) { diffs.push(`${rel}: did not converge`); continue; }
    const sp2 = str('breaks');
    const got = M.UTF8ToString(M._tsr_debug_dump(doc, sp2));
    M._free(sp2);
    const want = readFileSync(goldenPath, 'utf8');
    checked++;
    if (got !== want) diffs.push(`${rel}: breaks differ\n  wasm:   ${got.trim().split('\n').join('\n          ')}\n  native: ${want.trim().split('\n').join('\n          ')}`);
  } finally {
    M._tsr_doc_free(doc);
  }
}
for (const d of diffs) console.log('DIFF ' + d);
console.log(`wasm-goldens: ${checked} fixtures, ${diffs.length} differences`);
if (diffs.length && process.argv.includes('--check')) process.exit(1);
