#!/usr/bin/env node
// engine/src/codegen/lower.def → the LowerProgram opcode tables (plan P2-02;
// docs/lowering-design.md): engine/src/codegen/lower.gen.h (C++: codegen,
// reader, fuzz) and runtime/src/shared/lower.gen.mjs (the interpreter).
// PROGRAM_ABI = FNV-1a 32 over the def's rows (comments and blank lines
// excluded, so a comment edit is not an ABI change). Run by
// tools/gen-all.mjs; `--check` fails on stale outputs.
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const CHECK = process.argv.includes('--check');
const HDR = 'GENERATED from engine/src/codegen/lower.def by tools/gen-lower.mjs — do not edit.';

const def = readFileSync(join(root, 'engine/src/codegen/lower.def'), 'utf8');
const rowsText = def.split('\n').map((l) => l.replace(/\/\/.*$/, '').trim()).filter(Boolean);
const one = (name) => {
  const m = new RegExp(`^${name}\\((\\d+)\\)`, 'm').exec(def);
  if (!m) throw new Error(`lower.def: ${name} missing`);
  return Number(m[1]);
};
const version = one('LOWER_VERSION');
const protocol = one('LOWER_PROTOCOL');
const table = (macro) => [...def.matchAll(new RegExp(`^${macro}\\((\\w+),\\s*(\\d+)`, 'gm'))]
  .map((m) => ({ name: m[1], code: Number(m[2]) }));
const lops = table('LOP'), consts = table('CONST'), blocks = table('BLOCK'),
  bflags = table('BFLAG'), pieces = table('PIECE'), cflags = table('CFLAG');
for (const [what, t, max] of [['LOP', lops, 127], ['CONST', consts, 255], ['BLOCK', blocks, 255],
  ['BFLAG', bflags, 128], ['PIECE', pieces, 255], ['CFLAG', cflags, 128]]) {
  if (!t.length) throw new Error(`lower.def: no ${what} rows`);
  if (new Set(t.map((r) => r.code)).size !== t.length) throw new Error(`lower.def: duplicate ${what} code`);
  if (t.some((r) => r.code > max)) throw new Error(`lower.def: ${what} code above ${max}`);
}
let h32 = 0x811c9dc5;
for (const ch of Buffer.from(rowsText.join('\n'), 'utf8')) {
  h32 ^= ch;
  h32 = Math.imul(h32, 0x01000193) >>> 0;
}
const abi = '0x' + h32.toString(16).padStart(8, '0');

const enumC = (name, t, type) =>
  `enum class ${name} : ${type} {\n${t.map((r) => `  ${r.name} = ${r.code},`).join('\n')}\n};`;
const h = `// ${HDR}
#pragma once
#include "../support/support.h"

namespace tsr {

constexpr u32 LOWER_VERSION = ${version};
constexpr u32 LOWER_PROTOCOL = ${protocol};
constexpr u32 PROGRAM_ABI = ${abi}u;
constexpr u8 kLopAsync = 0x80;

${enumC('Lop', lops, 'u8')}
${enumC('LConst', consts, 'u8')}
${enumC('LBlock', blocks, 'u8')}
${enumC('LPiece', pieces, 'u8')}
${bflags.map((r) => `constexpr u8 kBlock${r.name} = ${r.code};`).join('\n')}
${cflags.map((r) => `constexpr u8 kCall${r.name} = ${r.code};`).join('\n')}

inline const char* lopName(u8 op) {
  switch (op & 0x7f) {
${lops.map((r) => `    case ${r.code}: return "${r.name}";`).join('\n')}
  }
  return nullptr;
}

}  // namespace tsr
`;
const obj = (t) => `Object.freeze({ ${t.map((r) => `${r.name}: ${r.code}`).join(', ')} })`;
const mjs = `// ${HDR}
export const LOWER_VERSION = ${version};
export const LOWER_PROTOCOL = ${protocol};
export const PROGRAM_ABI = ${abi};
export const LOP_ASYNC = 0x80;
export const LOP = ${obj(lops)};
export const LCONST = ${obj(consts)};
export const LBLOCK = ${obj(blocks)};
export const LPIECE = ${obj(pieces)};
export const BFLAG = ${obj(bflags)};
export const CFLAG = ${obj(cflags)};
`;
const outputs = {
  'engine/src/codegen/lower.gen.h': h,
  'runtime/src/shared/lower.gen.mjs': mjs,
};
let stale = 0;
for (const [rel, text] of Object.entries(outputs)) {
  const p = join(root, rel);
  const prev = existsSync(p) ? readFileSync(p, 'utf8') : null;
  if (prev === text) continue;
  if (CHECK) { console.error(`gen-lower: ${rel} is stale (run node tools/gen-lower.mjs)`); stale++; continue; }
  writeFileSync(p, text);
  console.log(`wrote ${rel}`);
}
if (stale) process.exit(1);
