#!/usr/bin/env node
// Value-domain parity (plan P1-02): the generated JS validators
// (runtime/src/shared/props.gen.mjs) accept exactly what the C++ reader's
// generated DFAs accept. Random strings over a domain-relevant alphabet,
// compared against the WASM build's matchDomain.
//   node tools/check-domains.mjs [--check]
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { DOMAINS, validDomain } from '../runtime/src/shared/props.gen.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const modPath = join(root, 'engine/build-wasm/typesetter_debug.js');
if (!existsSync(modPath)) {
  console.error('check-domains: engine/build-wasm/typesetter_debug.js missing (build G4 first)');
  process.exit(1);
}
const M = await (await import(pathToFileURL(modPath).href)).default();
const names = Object.keys(DOMAINS);  // enum order = schema order
const alpha = ['a', 'Z', '0', '9', '_', '-', ' ', ',', '"', "'", '\\', '#', '(', ')', '%', '.', '/',
               'd', 'e', 'g', 'r', 'b', 'v', 'h', 's', 'l', '\x01', '\x7f', '中', '𠀀', 'f', 'x',
               'A', 'F', '3', '\t', ';', '{', 'rgb(', 'var(--', 'hsl(', ':'];
let seed = 12345;
const rnd = (n) => { seed = (seed * 1103515245 + 12345) >>> 0; return (seed >>> 8) % n; };
let diffs = 0, n = 0;
for (let it = 0; it < 200000; it++) {
  let s = '';
  const len = rnd(14);
  for (let k = 0; k < len; k++) s += alpha[rnd(alpha.length)];
  if (it % 1000 === 0) s = 'a'.repeat(60 + rnd(10)) + s;  // around the length limits
  const p = M.stringToNewUTF8(s);
  names.forEach((d, k) => {
    const c = M._tsr_debug_match_domain(k, p) === 1;
    if (c !== validDomain(d, s) && diffs++ < 10) console.log(`DIFF ${d} c++=${c} js=${!c} ${JSON.stringify(s)}`);
  });
  M._free(p);
  n++;
}
console.log(`check-domains: ${n} strings × ${names.length} domains, ${diffs} differences`);
if (diffs && process.argv.includes('--check')) process.exit(1);
