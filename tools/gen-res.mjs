#!/usr/bin/env node
// engine/src/resource/resources.def → the column tables of the resource
// codecs (plan P1-19; design T9 A1): engine/src/resource/resources.gen.h
// (C++, read by resource/codec.cc) and runtime/src/shared/resources.gen.mjs
// (read by runtime/src/shared/rescodec.mjs). Run by tools/gen-all.mjs;
// `--check` fails on stale outputs.
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const CHECK = process.argv.includes('--check');
const HDR = 'GENERATED from engine/src/resource/resources.def by tools/gen-res.mjs — do not edit.';
const TYPES = ['Str', 'MetricKey', 'U8', 'U16', 'U32', 'F64', 'U32List'];

const def = readFileSync(join(root, 'engine/src/resource/resources.def'), 'utf8');
const version = Number(/^RES_VERSION\((\d+)\)/m.exec(def)?.[1]);
if (!version) throw new Error('resources.def: RES_VERSION missing');
const rows = [...def.matchAll(/^RES\((\w+), (\d+), (\w+), (true|false), "([^"]*)", "([^"]*)"\)/gm)].map((m) => {
  const cols = (s) => s.split(/\s+/).filter(Boolean).map((c) => {
    const [name, type] = c.split(':');
    if (!TYPES.includes(type)) throw new Error(`resources.def ${m[1]}: unknown column type ${type}`);
    return { name, type };
  });
  return { name: m[1], id: Number(m[2]), cache: m[3], doc: m[4] === 'true', key: cols(m[5]), ans: cols(m[6]) };
});
if (!['Content', 'Host', 'None'].every(Boolean) || rows.some((r) => !['Content', 'Host', 'None'].includes(r.cache)))
  throw new Error('resources.def: cache must be Content, Host or None');
const ids = new Set(rows.map((r) => r.id));
if (ids.size !== rows.length) throw new Error('resources.def: duplicate id');
for (const r of rows) if (r.ans.filter((c) => c.type === 'U32List').length > 1)
  throw new Error(`resources.def ${r.name}: at most one list column`);

const colsC = (cs) => cs.map((c) => `{"${c.name}", ColType::${c.type}}`).join(', ');
const h = `// ${HDR}
// The resource kinds and their wire columns (docs/host-protocol-design.md §5).
#pragma once
#include "../support/support.h"

namespace tsr {

constexpr u32 RES_VERSION = ${version};

enum class ResKind : u16 {
${rows.map((r) => `  ${r.name} = ${r.id},`).join('\n')}
};
constexpr u32 kResKindCount = ${rows.length};

enum class ColType : u8 { ${TYPES.join(', ')} };
struct ResCol {
  const char* name;
  ColType type;
};
enum class ResCache : u8 { Content, Host, None };
struct ResKindInfo {
  ResKind kind;
  const char* name;
  ResCache cache;
  bool docProviders;
  u8 nKey, nAns;
  ResCol key[4], ans[4];
};
inline constexpr ResKindInfo kResKinds[] = {
${rows.map((r) => `    {ResKind::${r.name}, "${r.name}", ResCache::${r.cache}, ${r.doc}, ${r.key.length}, ${r.ans.length}, {${colsC(r.key)}}, {${colsC(r.ans)}}},`).join('\n')}
};
inline const ResKindInfo* resKindInfo(u16 id) {
  for (const ResKindInfo& k : kResKinds)
    if ((u16)k.kind == id) return &k;
  return nullptr;
}

}  // namespace tsr
`;
const mjs = `// ${HDR}
// The resource kinds and their wire columns (docs/host-protocol-design.md §5).
export const RES_VERSION = ${version};
export const RES_KINDS = ${JSON.stringify(Object.fromEntries(rows.map((r) => [r.name, { id: r.id, cache: r.cache, docProviders: r.doc, key: r.key, ans: r.ans }])), null, 2)};
export const RES_BY_ID = Object.fromEntries(Object.entries(RES_KINDS).map(([name, k]) => [k.id, { name, ...k }]));
`;
const outputs = {
  'engine/src/resource/resources.gen.h': h,
  'runtime/src/shared/resources.gen.mjs': mjs,
};
let stale = 0;
for (const [rel, text] of Object.entries(outputs)) {
  const p = join(root, rel);
  const prev = existsSync(p) ? readFileSync(p, 'utf8') : null;
  if (prev === text) continue;
  if (CHECK) { console.error(`gen-res: ${rel} is stale (run node tools/gen-res.mjs)`); stale++; continue; }
  writeFileSync(p, text);
  console.log(`wrote ${rel}`);
}
if (stale) process.exit(1);
