#!/usr/bin/env node
// engine/schema/schema.json → generated ops vocabulary (plan P0-06, design T2 S1).
//
//   node tools/gen-schema.mjs                 write the generated files
//   node tools/gen-schema.mjs --check         fail if any generated file is stale
//   node tools/gen-schema.mjs --update-lock   accept NEW rows into schema.lock.json
//
// Outputs (committed; the C++ build needs no Node):
//   engine/src/ops/ops.def           X-macro lists for ops.h / ops.cc
//   engine/src/ops/schema.gen.h      OPS_VERSION, MIN_COMPAT, KIND_COUNT, KindInfo/AttrSpec
//   engine/src/ops/schema.gen.cc     the per-kind attribute tables (decode-time validation)
//   runtime/src/shared/ops.gen.mjs   the JS writer's constants
//   docs/schema-table.md             document-model §2.1 kind table
// schema.lock.json pins ids and since values: an edit to a locked row fails.
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const CHECK = args.includes('--check');
const schemaPath = join(root, 'engine/schema/schema.json');
const lockPath = join(root, 'engine/schema/schema.lock.json');
const S = JSON.parse(readFileSync(schemaPath, 'utf8'));
const errors = [];

// ---- lock: ids and since values are immutable -------------------------------
const lockOf = () => {
  const L = { ops: {}, kinds: {}, keys: {}, attrs: {} };
  for (const [n, o] of Object.entries(S.ops)) L.ops[n] = [o.id, o.since];
  for (const [n, k] of Object.entries(S.kinds)) {
    L.kinds[n] = [k.id, k.since];
    for (const [a, spec] of Object.entries(k.attrs)) L.attrs[`${n}.${a}`] = spec.since ?? k.since;
  }
  for (const [n, id] of Object.entries(S.keys)) L.keys[n] = id;
  return L;
};
const now = lockOf();
if (existsSync(lockPath)) {
  const locked = JSON.parse(readFileSync(lockPath, 'utf8'));
  for (const sec of Object.keys(locked)) {
    for (const [n, v] of Object.entries(locked[sec])) {
      if (!(n in now[sec])) errors.push(`lock: ${sec}.${n} was removed (ids are immutable)`);
      else if (JSON.stringify(now[sec][n]) !== JSON.stringify(v))
        errors.push(`lock: ${sec}.${n} changed ${JSON.stringify(v)} → ${JSON.stringify(now[sec][n])}`);
    }
    for (const n of Object.keys(now[sec]))
      if (!(n in locked[sec]) && !args.includes('--update-lock'))
        errors.push(`lock: new row ${sec}.${n} — run with --update-lock to accept it`);
  }
}
// unique ids
const dupes = (o, f) => {
  const seen = new Map();
  for (const [n, v] of Object.entries(o)) {
    const id = f(v);
    if (seen.has(id)) errors.push(`duplicate id ${id}: ${seen.get(id)} / ${n}`);
    seen.set(id, n);
  }
};
dupes(S.ops, (v) => v.id);
dupes(S.kinds, (v) => v.id);
dupes(S.keys, (v) => v);
for (const [n, k] of Object.entries(S.kinds))
  for (const a of Object.keys(k.attrs))
    if (!(a in S.keys)) errors.push(`kinds.${n}.attrs.${a}: no such key`);

if (errors.length) {
  for (const e of errors) console.error('gen-schema: ' + e);
  process.exit(1);
}

const byId = (o, f) => Object.entries(o).sort((a, b) => f(a[1]) - f(b[1]));
const ops = byId(S.ops, (v) => v.id);
const kinds = byId(S.kinds, (v) => v.id);
const keys = byId(S.keys, (v) => v);
const HDR = 'GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.';

// ---- ops.def -----------------------------------------------------------------
let def = `// ${HDR}\n// X-macro lists consumed by engine/src/ops/ops.h and ops.cc.\n//\n` +
  `// OPS_VERSION(${S.opsVersion})\n\n`;
for (const [n, o] of ops) def += `OP(${n}, ${o.id})\n`;
def += '\n';
for (const [n, k] of kinds) def += `KIND(${n}, ${k.id})\n`;
def += '\n';
for (const [n, id] of keys) def += `ARGK(${n}, ${id})\n`;

// ---- schema.gen.h / .cc --------------------------------------------------------
const LEVELS = ['block', 'inline', 'adaptive', 'transparent', 'trivia'];
const BODIES = ['none', 'inline', 'blocks', 'items', 'code', 'position', 'rows', 'cells', 'data', 'text'];
const DOMS = ['bool', 'int', 'num', 'str', 'token', 'ident', 'label', 'lang', 'enum', 'flags',
              'rangeset', 'color', 'font', 'html', 'url'];
const cap = (s) => s[0].toUpperCase() + s.slice(1);
const cstr = (s) => JSON.stringify(s);
let h = `// ${HDR}\n#pragma once\n#include <cstdint>\n\nnamespace tsr {\n\n` +
  `constexpr std::uint8_t OPS_VERSION = ${S.opsVersion};\n` +
  `constexpr std::uint8_t OPS_MIN_COMPAT = ${S.minCompat};\n` +
  `constexpr std::uint16_t KIND_COUNT = ${kinds.length};\n` +
  `constexpr std::uint16_t ARGK_COUNT = ${keys.length};\n\n` +
  `enum class Level : std::uint8_t { ${LEVELS.map(cap).join(', ')} };\n` +
  `enum class Body : std::uint8_t { ${BODIES.map(cap).join(', ')} };\n` +
  `enum class Dom : std::uint8_t { ${DOMS.map((d) => d === 'rangeset' ? 'RangeSet' : cap(d)).join(', ')} };\n\n` +
  `// One attribute of one kind: its wire key, value domain and default.\n` +
  `struct AttrSpec {\n  std::uint16_t key;\n  const char* name;\n  Dom dom;\n  double lo, hi;   // Int / Num\n` +
  `  const char* const* members;  // Enum names, Flags names\n  const std::uint8_t* bits;     // Flags bit positions\n` +
  `  std::uint8_t nMembers;\n  bool boolAsInt;  // Int accepting true/false (lineNo)\n  bool hasDef;\n  double def;\n` +
  `  std::uint8_t since;\n};\n\n` +
  `struct KindInfo {\n  const char* name;\n  Level level;\n  Body body;\n  std::uint8_t since;\n` +
  `  const AttrSpec* attrs;  // writer order\n  std::uint8_t nAttrs;\n};\n\n` +
  `extern const KindInfo kKinds[KIND_COUNT];  // indexed by Kind id\n\n}  // namespace tsr\n`;

let cc = `// ${HDR}\n#include "schema.gen.h"\n\nnamespace tsr {\nnamespace {\n`;
const kindRows = [];
for (const [n, k] of kinds) {
  const rows = [];
  for (const [a, spec] of Object.entries(k.attrs)) {
    const [dom, ...rest] = spec.dom.split(':');
    if (!DOMS.includes(dom)) { console.error(`gen-schema: ${n}.${a}: unknown domain ${dom}`); process.exit(1); }
    let lo = 0, hi = 0, members = 'nullptr', bits = 'nullptr', nm = 0;
    if (dom === 'int' || dom === 'num') { lo = Number(rest[0]); hi = Number(rest[1]); }
    if (dom === 'enum') {
      const ms = rest.join(':').split('|');
      cc += `const char* const kM_${n}_${a}[] = {${ms.map(cstr).join(', ')}};\n`;
      members = `kM_${n}_${a}`; nm = ms.length;
    }
    if (dom === 'flags') {
      const ms = rest.join(':').split(',').map((x) => x.split('='));
      cc += `const char* const kM_${n}_${a}[] = {${ms.map((m) => cstr(m[0])).join(', ')}};\n`;
      cc += `const std::uint8_t kB_${n}_${a}[] = {${ms.map((m) => m[1]).join(', ')}};\n`;
      members = `kM_${n}_${a}`; bits = `kB_${n}_${a}`; nm = ms.length;
    }
    const hasDef = 'def' in spec;
    const defv = hasDef ? Number(spec.def === true ? 1 : spec.def === false ? 0 : spec.def) : 0;
    rows.push(`{${S.keys[a]}, ${cstr(a)}, Dom::${dom === 'rangeset' ? 'RangeSet' : cap(dom)}, ${lo}, ${hi}, ` +
              `${members}, ${bits}, ${nm}, ${spec.coerce === 'boolAsInt'}, ${hasDef}, ${defv}, ${spec.since ?? k.since}}`);
  }
  if (rows.length) cc += `const AttrSpec kA_${n}[] = {\n    ${rows.join(',\n    ')}};\n`;
  kindRows.push(`{${cstr(n)}, Level::${cap(k.level)}, Body::${cap(k.body)}, ${k.since}, ` +
                `${rows.length ? `kA_${n}` : 'nullptr'}, ${rows.length}}`);
}
cc += `}  // namespace\n\nconst KindInfo kKinds[KIND_COUNT] = {\n    ${kindRows.join(',\n    ')}};\n\n}  // namespace tsr\n`;

// ---- ops.gen.mjs -----------------------------------------------------------------
const obj = (pairs) => Object.fromEntries(pairs);
const emit = (name, o) => `export const ${name} = Object.freeze(${JSON.stringify(o, null, 2)});\n`;
const schemaJs = obj(kinds.map(([n, k]) => [n, { id: k.id, level: k.level, body: k.body,
  attrs: Object.fromEntries(Object.entries(k.attrs).map(([a, s]) => [a, s.dom])) }]));
const js = `// ${HDR}\n` +
  `export const OPS_VERSION = ${S.opsVersion};\n` +
  `export const OPS_MIN_COMPAT = ${S.minCompat};\n` +
  emit('OP', obj(ops.map(([n, o]) => [n, o.id]))) +
  emit('KIND', obj(kinds.map(([n, k]) => [n, k.id]))) +
  emit('ARGK', obj(keys.map(([n, id]) => [n, id]))) +
  emit('SCHEMA', schemaJs);

// ---- docs/schema-table.md --------------------------------------------------------
let md = `<!-- ${HDR} -->\n# Ops vocabulary (generated)\n\nThe kind table of document-model §2.1, generated from ` +
  '`engine/schema/schema.json`. Ops version ' + S.opsVersion + ', min compat ' + S.minCompat + '.\n\n' +
  '| id | kind | level | body | attributes (writer order: domain) |\n|---|---|---|---|---|\n';
for (const [n, k] of kinds) {
  const at = Object.entries(k.attrs).map(([a, s]) => `\`${a}\`: ${s.dom.replace(/\|/g, '\\|')}`).join('; ') || '—';
  md += `| ${k.id} | \`${n}\` | ${k.level} | ${k.body} | ${at} |\n`;
}
md += '\n| op | id |\n|---|---|\n' + ops.map(([n, o]) => `| ${n} | ${o.id} |`).join('\n') + '\n';

// ---- write / check ---------------------------------------------------------------
const outputs = {
  'engine/src/ops/ops.def': def,
  'engine/src/ops/schema.gen.h': h,
  'engine/src/ops/schema.gen.cc': cc,
  'runtime/src/shared/ops.gen.mjs': js,
  'docs/schema-table.md': md,
};
let stale = 0;
for (const [rel, text] of Object.entries(outputs)) {
  const p = join(root, rel);
  const prev = existsSync(p) ? readFileSync(p, 'utf8') : null;
  if (prev === text) continue;
  if (CHECK) { console.error(`STALE ${rel} (run node tools/gen-schema.mjs)`); stale++; }
  else { writeFileSync(p, text); console.log(`wrote ${rel}`); }
}
if (!CHECK && (args.includes('--update-lock') || !existsSync(lockPath))) {
  writeFileSync(lockPath, JSON.stringify(now, null, 1) + '\n');
  console.log('wrote engine/schema/schema.lock.json');
}
if (CHECK && stale) process.exit(1);
