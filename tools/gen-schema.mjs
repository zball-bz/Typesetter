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
import { compile as compileDfa } from './lib/redfa.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const CHECK = args.includes('--check');
const schemaPath = join(root, 'engine/schema/schema.json');
const lockPath = join(root, 'engine/schema/schema.lock.json');
const S = JSON.parse(readFileSync(schemaPath, 'utf8'));
const errors = [];
// the universal attributes (plan P2-05): every kind but doc and text accepts
// them after its own; a kind's own row of the same name wins
const NO_UNIVERSALS = new Set(['doc', 'text']);
for (const [n, k] of Object.entries(S.kinds)) {
  if (NO_UNIVERSALS.has(n)) continue;
  for (const [a, spec] of Object.entries(S.universal ?? {}))
    if (a !== '$comment' && !(a in k.attrs)) k.attrs[a] = { ...spec };
}
const resolvedAttr = (spec) => (spec.flags ?? []).includes('resolved');

// ---- lock: ids and since values are immutable -------------------------------
const lockOf = () => {
  const L = { ops: {}, kinds: {}, keys: {}, attrs: {}, decls: {} };
  for (const [n, o] of Object.entries(S.ops)) L.ops[n] = [o.id, o.since];
  for (const [n, k] of Object.entries(S.kinds)) {
    L.kinds[n] = [k.id, k.since];
    for (const [a, spec] of Object.entries(k.attrs)) L.attrs[`${n}.${a}`] = spec.since ?? k.since;
  }
  for (const [n, id] of Object.entries(S.keys)) L.keys[n] = id;
  for (const [n, d] of Object.entries(S.decls ?? {})) if (n !== '$comment') L.decls[n] = [d.id, d.since];
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
// the shaper's flatten table (plan P1-13): what every kind becomes in an
// inline stream — closed, so a new kind cannot be dropped silently
const INLINES = ['text', 'container', 'code', 'object', 'break', 'error', 'skip', 'unsupported'];
for (const [n, k] of Object.entries(S.kinds))
  if (!INLINES.includes(k.inline)) errors.push(`kinds.${n}.inline: one of ${INLINES.join(', ')}`);

if (errors.length) {
  for (const e of errors) console.error('gen-schema: ' + e);
  process.exit(1);
}

const byId = (o, f) => Object.entries(o).sort((a, b) => f(a[1]) - f(b[1]));
const ops = byId(S.ops, (v) => v.id);
const kinds = byId(S.kinds, (v) => v.id);
const keys = byId(S.keys, (v) => v);
const HDR = 'GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.';
// The ABI handshake's schemaHash (plan P1-01, D-H06): FNV-1a 32 over the
// canonical JSON of the vocabulary (comments excluded), so the WASM engine
// and the JS writer prove they were generated from the same schema.
const canon = (v) => Array.isArray(v) ? v.map(canon)
  : v && typeof v === 'object' ? Object.fromEntries(Object.keys(v).filter((k) => k !== '$comment').sort().map((k) => [k, canon(v[k])]))
  : v;
const SCHEMA_HASH = (() => {
  let h = 0x811c9dc5;
  for (const c of Buffer.from(JSON.stringify(canon(S)), 'utf8')) { h ^= c; h = Math.imul(h, 0x01000193) >>> 0; }
  return h.toString(16).padStart(8, '0');
})();

// ---- ops.def -----------------------------------------------------------------
let def = `// ${HDR}\n// X-macro lists consumed by engine/src/ops/ops.h and ops.cc.\n//\n` +
  `// OPS_VERSION(${S.opsVersion})\n\n`;
for (const [n, o] of ops) def += `OP(${n}, ${o.id})\n`;
def += '\n';
for (const [n, k] of kinds) def += `KIND(${n}, ${k.id})\n`;
def += '\n';
// ARGK(enumerator, name, id): a key named after a C++ keyword gets a
// trailing underscore as its enumerator (class → class_), its name unchanged
const CPP_RESERVED = new Set(['class', 'default', 'delete', 'new', 'this', 'union', 'enum', 'struct', 'template', 'operator']);
for (const [n, id] of keys) def += `ARGK(${CPP_RESERVED.has(n) ? n + '_' : n}, ${n}, ${id})\n`;

// ---- schema.gen.h / .cc --------------------------------------------------------
const LEVELS = ['block', 'inline', 'adaptive', 'transparent', 'trivia'];
const BODIES = ['none', 'inline', 'blocks', 'items', 'code', 'position', 'rows', 'cells', 'data', 'text'];
const DOMS = ['bool', 'int', 'num', 'str', 'token', 'ident', 'label', 'lang', 'enum', 'flags',
              'rangeset', 'color', 'font', 'html', 'url', 'text', 'ext'];
// a "text" attribute domain is any regex domain of the domains section (plan
// P2-05: copy, classlist): checked through its index in TextDomain
const TEXT_DOMS = Object.keys(S.domains ?? {}).filter((d) => d !== '$comment');
const cap = (s) => s[0].toUpperCase() + s.slice(1);
const cstr = (s) => JSON.stringify(s);
let h = `// ${HDR}\n#pragma once\n#include <cstdint>\n\nnamespace tsr {\n\n` +
  `constexpr std::uint8_t OPS_VERSION = ${S.opsVersion};\n` +
  `constexpr std::uint8_t OPS_MIN_COMPAT = ${S.minCompat};\n` +
  `constexpr const char* SCHEMA_HASH = "${SCHEMA_HASH}";\n` +
  `constexpr std::uint16_t KIND_COUNT = ${kinds.length};\n` +
  `constexpr std::uint16_t ARGK_COUNT = ${keys.length};\n\n` +
  `enum class Level : std::uint8_t { ${LEVELS.map(cap).join(', ')} };\n` +
  `enum class Body : std::uint8_t { ${BODIES.map(cap).join(', ')} };\n` +
  `// what a kind becomes in an inline stream (the shaper's flatten table, plan P1-13)\n` +
  `enum class InlineShape : std::uint8_t { ${INLINES.map(cap).join(', ')} };\n` +
  `enum class Dom : std::uint8_t { ${DOMS.map((d) => d === 'rangeset' ? 'RangeSet' : cap(d)).join(', ')} };\n\n` +
  `// One attribute of one kind: its wire key, value domain and default.\n` +
  `struct AttrSpec {\n  std::uint16_t key;\n  const char* name;\n  Dom dom;\n  double lo, hi;   // Int / Num\n` +
  `  const char* const* members;  // Enum names, Flags names\n  const std::uint8_t* bits;     // Flags bit positions\n` +
  `  std::uint8_t nMembers;\n  bool boolAsInt;  // Int accepting true/false (lineNo)\n  bool hasDef;\n  double def;\n` +
  `  std::uint8_t since;\n  std::uint8_t textDom;  // Text: its TextDomain\n` +
  `  bool resolved;  // set by the resolver only: dropped from input (plan P2-05)\n};\n\n` +
  `// a declaration type (plan P2-05; schema "decls"): hoisted = the last of a\n// name wins, else positional\n` +
  `struct DeclInfo {\n  const char* name;\n  bool hoisted;\n  std::uint8_t since;\n};\n` +
  `constexpr std::uint16_t DECL_COUNT = ${Object.keys(S.decls ?? {}).filter((d) => d !== '$comment').length + 1};\n` +
  `extern const DeclInfo kDecls[DECL_COUNT];  // indexed by id (0: none)\n\n` +
  `struct KindInfo {\n  const char* name;\n  Level level;\n  Body body;\n  InlineShape inl;\n  std::uint8_t since;\n` +
  `  const AttrSpec* attrs;  // writer order\n  std::uint8_t nAttrs;\n};\n\n` +
  `extern const KindInfo kKinds[KIND_COUNT];  // indexed by Kind id\n` +
  `// the version an opcode first appeared in (0 = no such opcode)\n` +
  `constexpr std::uint8_t kOpSince[] = {${(() => { const t = new Array(Math.max(...ops.map(([, o]) => o.id)) + 1).fill(0); for (const [, o] of ops) t[o.id] = o.since; return t.join(', '); })()}};\n\n}  // namespace tsr\n`;

let cc = `// ${HDR}\n#include "schema.gen.h"\n\nnamespace tsr {\nnamespace {\n`;
const kindRows = [];
for (const [n, k] of kinds) {
  const rows = [];
  for (const [a, spec] of Object.entries(k.attrs)) {
    let [dom, ...rest] = spec.dom.split(':');
    let textDom = 0;
    if (!DOMS.includes(dom) && TEXT_DOMS.includes(dom)) { textDom = TEXT_DOMS.indexOf(dom); dom = 'text'; }
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
              `${members}, ${bits}, ${nm}, ${spec.coerce === 'boolAsInt'}, ${hasDef}, ${defv}, ${spec.since ?? k.since}, ` +
              `${textDom}, ${resolvedAttr(spec)}}`);
  }
  if (rows.length) cc += `const AttrSpec kA_${n}[] = {\n    ${rows.join(',\n    ')}};\n`;
  kindRows.push(`{${cstr(n)}, Level::${cap(k.level)}, Body::${cap(k.body)}, InlineShape::${cap(k.inline)}, ${k.since}, ` +
                `${rows.length ? `kA_${n}` : 'nullptr'}, ${rows.length}}`);
}
const declRows = ['{nullptr, false, 0}'];
for (const [n, d] of Object.entries(S.decls ?? {}).filter(([n]) => n !== '$comment').sort((a, b) => a[1].id - b[1].id)) {
  if (d.id !== declRows.length) { console.error(`gen-schema: decls.${n}: ids must run 1, 2, …`); process.exit(1); }
  if (!['hoisted', 'positional'].includes(d.binding)) { console.error(`gen-schema: decls.${n}: binding`); process.exit(1); }
  declRows.push(`{${cstr(n)}, ${d.binding === 'hoisted'}, ${d.since}}`);
}
cc += `}  // namespace\n\nconst KindInfo kKinds[KIND_COUNT] = {\n    ${kindRows.join(',\n    ')}};\n\n` +
  `const DeclInfo kDecls[DECL_COUNT] = {\n    ${declRows.join(',\n    ')}};\n\n}  // namespace tsr\n`;

// ---- ops.gen.mjs -----------------------------------------------------------------
const obj = (pairs) => Object.fromEntries(pairs);
const emit = (name, o) => `export const ${name} = Object.freeze(${JSON.stringify(o, null, 2)});\n`;
const schemaJs = obj(kinds.map(([n, k]) => [n, { id: k.id, level: k.level, body: k.body, inline: k.inline,
  attrs: Object.fromEntries(Object.entries(k.attrs).map(([a, s]) => [a, s.dom])),
  resolved: Object.entries(k.attrs).filter(([, s]) => resolvedAttr(s)).map(([a]) => a) }]));
const declsJs = obj(Object.entries(S.decls ?? {}).filter(([n]) => n !== '$comment')
  .map(([n, d]) => [n, { id: d.id, since: d.since, hoisted: d.binding === 'hoisted' }]));
// since tables for the writer's per-buffer version (plan P1-01)
const sinceJs = {
  op: obj(ops.map(([, o]) => [o.id, o.since])),
  kind: obj(kinds.map(([, k]) => [k.id, k.since])),
  attr: obj(kinds.map(([, k]) => [k.id, obj(Object.entries(k.attrs).map(([a, sp]) => [S.keys[a], sp.since ?? k.since]))])),
};
const js = `// ${HDR}\n` +
  `export const OPS_VERSION = ${S.opsVersion};\n` +
  `export const OPS_MIN_COMPAT = ${S.minCompat};\n` +
  `export const SCHEMA_HASH = '${SCHEMA_HASH}';\n` +
  emit('SINCE', sinceJs) +
  emit('OP', obj(ops.map(([n, o]) => [n, o.id]))) +
  emit('KIND', obj(kinds.map(([n, k]) => [n, k.id]))) +
  emit('ARGK', obj(keys.map(([n, id]) => [n, id]))) +
  emit('SCHEMA', schemaJs) +
  emit('DECLS', declsJs);

// ---- textual value domains (plan P1-02): one regex → C++ DFA + JS RegExp --------
const domains = Object.entries(S.domains ?? {}).filter(([n]) => n !== '$comment');
const domEnum = (n) => (n === 'rangeset' ? 'RangeSet' : cap(n));
let domH = `// ${HDR}\n#pragma once\n#include <cstdint>\n#include <string_view>\n\nnamespace tsr {\n\n` +
  `// textual attribute domains (schema "domains"); whole-string match on bytes\n` +
  `enum class TextDomain : std::uint8_t { ${domains.map(([n]) => domEnum(n)).join(', ')} };\n` +
  `bool matchDomain(TextDomain d, std::string_view s);\n\n}  // namespace tsr\n`;
let domCc = `// ${HDR}\n#include "domains.gen.h"\n\nnamespace tsr {\nnamespace {\n\n`;
const dfaRows = [];
for (const [n, d] of domains) {
  const dfa = compileDfa(d.re);
  if (dfa.states >= 255) { console.error(`gen-schema: domain ${n}: ${dfa.states} DFA states`); process.exit(1); }
  domCc += `// ${n}: ${d.re.replace(/\*\//g, '*\\/')}  (max ${d.max || 'none'})\n`;
  domCc += `const std::uint8_t kCls_${n}[256] = {${dfa.byteClass.join(',')}};\n`;
  domCc += `const std::uint8_t kT_${n}[${dfa.states * dfa.classes}] = {${dfa.table.flat().map((t) => (t < 0 ? 255 : t)).join(',')}};\n`;
  domCc += `const bool kAcc_${n}[${dfa.states}] = {${dfa.accept.map((a) => (a ? 'true' : 'false')).join(',')}};\n\n`;
  dfaRows.push(`{kCls_${n}, kT_${n}, kAcc_${n}, ${dfa.classes}, ${d.max | 0}}`);
}
domCc += `struct Dfa {\n  const std::uint8_t* cls;\n  const std::uint8_t* next;  // [state * classes + class], 255 = dead\n` +
  `  const bool* accept;\n  std::uint8_t classes;\n  std::uint32_t max;  // UTF-8 bytes, 0 = none\n};\n` +
  `const Dfa kDfas[] = {\n    ${dfaRows.join(',\n    ')}};\n\n}  // namespace\n\n` +
  `bool matchDomain(TextDomain d, std::string_view s) {\n  const Dfa& f = kDfas[(int)d];\n` +
  `  if (f.max && s.size() > f.max) return false;\n  unsigned q = 0;\n` +
  `  for (unsigned char c : s) {\n    q = f.next[q * f.classes + f.cls[c]];\n    if (q == 255) return false;\n  }\n` +
  `  return f.accept[q];\n}\n\n}  // namespace tsr\n`;
// JS: the byte classes \x80-\xff mean any non-ASCII character (u flag)
const jsRe = (re) => re.replace(/\\x80-\\xff/g, '\\u0080-\\u{10ffff}');
const domJs = `export const DOMAINS = Object.freeze({\n${domains.map(([n, d]) =>
  `  ${n}: Object.freeze({ max: ${d.max | 0}, re: /^(?:${jsRe(d.re)})$/u }),`).join('\n')}\n});\n` +
  `const utf8Length = (s) => { let n = 0; for (const c of s) { const cp = c.codePointAt(0); ` +
  `n += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; } return n; };\n` +
  `// the JS twin of the C++ matchDomain (early diagnostics; the reader decides)\n` +
  `export function validDomain(name, s) {\n  const d = DOMAINS[name];\n  if (!d || typeof s !== 'string') return false;\n` +
  `  return (!d.max || utf8Length(s) <= d.max) && d.re.test(s);\n}\n`;

// ---- run properties (plan P1-02): Styling, its ops, dumps and CSS ----------------
const props = Object.entries(S.props ?? {}).filter(([n]) => n !== '$comment');
const flagsOf = (attrDom) => Object.fromEntries(attrDom.split(':').slice(1).join(':').split(',').map((x) => x.split('=')).map(([k, v]) => [k, +v]));
const bitsRow = props.find(([, r]) => r.type === 'bits');
const bitsFlags = bitsRow ? flagsOf(S.kinds.styled.attrs[bitsRow[1].attr].dom) : {};
const CT = { bits: 'u64', mul: 'float', str: 'StrRef', px: 'float' };
const INIT = { bits: '0', mul: '1.0f', str: '0', px: '0' };
let ph = `// ${HDR}\n// Run style properties (schema "props"; plan P1-02, design T4 M2).\n#pragma once\n#include <cstring>\n\n#include "../ops/ops.h"\n\nnamespace tsr {\n\n` +
  `// The effective run style: one field per property row, in row order. StrRef 0 /\n// 0.0 = not set.\nstruct Styling {\n`;
for (const [n, r] of props) ph += `  ${CT[r.type]} ${r.field} = ${INIT[r.type]};  // ${n}\n`;
ph += `  bool operator==(const Styling& o) const {\n    return ${props.map(([, r]) => `${r.field} == o.${r.field}`).join(' &&\n           ')};\n  }\n};\n\n`;
ph += `// a hash over the canonical bits of every field\nstruct StylingHash {\n  size_t operator()(const Styling& s) const {\n` +
  `    u64 h = 1469598103934665603ull;\n    auto mix = [&](u64 v) { h = (h ^ v) * 1099511628211ull; };\n`;
for (const [, r] of props)
  ph += (r.type === 'mul' || r.type === 'px')
    ? `    {\n      u32 b;\n      std::memcpy(&b, &s.${r.field}, 4);\n      mix(b);\n    }\n`
    : `    mix((u64)s.${r.field});\n`;
ph += `    return (size_t)h;\n  }\n};\n\n`;
ph += `// canonical floats (plan P0-08): -0 → +0, NaN (and a negative px) → the initial value\ninline void canonicalize(Styling& s) {\n`;
for (const [, r] of props) {
  if (r.type === 'mul') ph += `  if (!(s.${r.field} == s.${r.field})) s.${r.field} = 1.0f;\n  if (s.${r.field} == 0) s.${r.field} = 0.0f;\n`;
  if (r.type === 'px') ph += `  if (!(s.${r.field} == s.${r.field}) || s.${r.field} < 0) s.${r.field} = 0;\n  if (s.${r.field} == 0) s.${r.field} = 0.0f;\n`;
}
ph += `}\n\n// folds one styled attribute or STYLE_PUSH patch value onto a style (values\n// were validated at decode); intern(ref) maps a buffer string to a StrRef\n` +
  `template <class Intern>\ninline void applyStyleArg(Styling& st, const ArgVal& a, Intern intern) {\n`;
for (const [, r] of props) {
  if (!r.attr) continue;
  if (r.type === 'bits') ph += `  if (a.key == ArgK::${r.attr} && a.tag == ArgTag::Num && a.num >= 0) st.${r.field} |= (u64)a.num;  // flags OR in\n`;
  if (r.type === 'str') ph += `  if (a.key == ArgK::${r.attr} && a.tag == ArgTag::Str) st.${r.field} = intern(a.ref);\n`;
  if (r.type === 'px') ph += `  if (a.key == ArgK::${r.attr} && a.tag == ArgTag::Num) st.${r.field} = (float)a.num;\n`;
}
ph += `}\n\n// the value part of the tree and block dumps, in row order (each dump keeps\n// its own flag tokens and size-multiplier spelling)\n` +
  `inline void appendStyleFields(std::string& out, const Styling& s, const Interner& strs) {\n`;
for (const [, r] of props) {
  if (!r.dump) continue;
  if (r.type === 'str' && r.dump.quoted) ph += `  if (s.${r.field}) {\n    out += " ${r.dump.label}=\\"";\n    appendEscaped(out, strs.get(s.${r.field}));\n    out += "\\"";\n  }\n`;
  else if (r.type === 'str') ph += `  if (s.${r.field}) {\n    out += " ${r.dump.label}=";\n    out += strs.get(s.${r.field});\n  }\n`;
  else if (r.type === 'px') ph += `  if (s.${r.field} > 0) appendf(out, " ${r.dump.label}=%gpx", (double)s.${r.field});\n`;
}
ph += `}\n\n}  // namespace tsr\n`;

// the typeset serializer's run attributes and declarations
let css = `// ${HDR}\n// A typeset run's attributes and style declarations (schema "props"; plan\n// P1-02). Values were validated at decode; text values are attribute-escaped.\n#pragma once\n#include <cstring>\n\n#include "../model/style.h"\n#include "html_writer.h"\n\nnamespace tsr {\n\n` +
  `inline void runCss(Tag& t, const Styling& st, double basePx, const Interner& strs) {\n`;
for (const [, r] of props) if (r.html) css += `  if (st.${r.field}) t.attr("${r.html}", strs.get(st.${r.field}));\n`;
for (const [, r] of props.filter(([, r]) => r.css).sort((a, b) => a[1].cssOrder - b[1].cssOrder)) {
  if (r.cssValue === 'emPx') {
    const conds = props.filter(([, x]) => x.type === 'mul' || x.type === 'px')
      .map(([, x]) => (x.type === 'mul' ? `st.${x.field} != 1.0f` : `st.${x.field} > 0`));
    css += `  if (${conds.join(' || ')}) t.px("${r.css}", emPx(basePx, st));\n`;
  } else if (r.cssFlags) {
    const bits = Object.entries(r.cssFlags).map(([f, kw]) => [`(1ull << ${bitsFlags[f]})`, kw]);
    css += `  if (st.${r.field} & (${bits.map((b) => b[0]).join(' | ')})) {\n    char buf[64];\n    size_t n = 0;\n` +
      `    auto add = [&](const char* w) {\n      if (n) buf[n++] = ' ';\n      std::memcpy(buf + n, w, std::strlen(w));\n      n += std::strlen(w);\n    };\n` +
      bits.map(([b, kw]) => `    if (st.${r.field} & ${b}) add("${kw}");\n`).join('') +
      `    t.decl("${r.css}", std::string_view(buf, n));\n  }\n`;
  } else if (r.type === 'str') {
    css += `  if (st.${r.field}) t.declEsc("${r.css}", strs.get(st.${r.field}));\n`;
  }
}
css += `}\n\n}  // namespace tsr\n`;

const sugar = bitsRow ? Object.fromEntries(Object.entries(bitsRow[1].sugar ?? {}).map(([k, f]) => [k, 2 ** bitsFlags[f]])) : {};
const propsJs = `// ${HDR}\n// Run style properties (plan P1-02): the $.style.push / #style / region keys\n// and their value domains.\n` +
  `export const STYLE_KEYS = Object.freeze(${JSON.stringify(Object.fromEntries(props.filter(([, r]) => r.attr && r.type !== 'bits').map(([, r]) => [r.key ?? r.attr, r.attr])))});\n` +
  `export const STYLE_SUGAR = Object.freeze(${JSON.stringify(sugar)});\n` + domJs;

// ---- host settings (plan P1-03): Config, its JSON codec, JS defaults --------------
const STAGES = [...readFileSync(join(root, 'engine/src/api/stages.def'), 'utf8')
  .matchAll(/^STAGE\((\w+),/gm)].map((m) => m[1]);
const settings = Object.entries(S.settings ?? {}).filter(([n]) => n !== '$comment');
const policy = Object.entries(S.policy ?? {}).filter(([n]) => n !== '$comment');
for (const [n, r] of settings) {
  for (const a of r.affects) if (!STAGES.includes(a)) errors.push(`settings.${n}: unknown stage ${a}`);
  if (!/^[a-z]+\.[A-Za-z]+$/.test(n)) errors.push(`settings.${n}: paths are section.name`);
}
if (errors.length) { for (const e of errors) console.error('gen-schema: ' + e); process.exit(1); }
const textDomains = domains.map(([n]) => n);
const ctypeOf = (r) => r.ctype ?? (r.dom.startsWith('num') ? 'double' : r.dom.startsWith('int') ? 'int'
  : r.dom === 'bool' ? 'bool' : r.dom.startsWith('map') ? 'std::map<std::string, std::string>' : 'std::string');
const cLit = (r, v) => {
  const t = ctypeOf(r);
  if (r.ctype === 'PunctCompress') return `PunctCompress::${cap(v)}`;
  if (t.startsWith('std::map')) return '{}';
  if (t === 'std::string') return JSON.stringify(v);
  if (t === 'bool') return v ? 'true' : 'false';
  return String(v);
};
const costRows = settings.filter(([, r]) => r.field.startsWith('cost.'));
const cfgRows = settings.filter(([, r]) => !r.field.startsWith('cost.'));
const pc = settings.find(([, r]) => r.ctype === 'PunctCompress');
let sh = `// ${HDR}\n// Host settings (schema "settings"; plan P1-03, design T4 M3 / T9 A4).\n#pragma once\n#include <map>\n#include <string>\n#include <string_view>\n\n` +
  `#include "../support/support.h"\n#include "stages.h"\n\nnamespace tsr {\n\n` +
  `// Adjacent-punctuation compression style (clreq; v2 App C).\n//   Full: every adjacent gap compressed (newspaper-tight)\n` +
  `//   Book: close+close and open+open set solid, but a breakable half-width\n//         breathing space is kept between a closing/dot and an opening punct\n` +
  `//   None: full-width style — all punctuation spaces kept (rigid where 禁则\n//         forbids a break)\n` +
  `enum class PunctCompress : u8 { ${pc[1].dom.slice(5).split('|').map((m, k) => `${cap(m)} = ${k}`).join(', ')} };\n\n` +
  `// Line cost (document-model §11; TeX-bounded since P0-12): x below\n// -shrinkThreshold is Overfull; cost = min(mapped(x)^exponent, cap).\nstruct CostParams {\n`;
for (const [n, r] of costRows) sh += `  ${ctypeOf(r)} ${r.field.slice(5)} = ${cLit(r, r.def)};  // ${n}\n`;
sh += `};\n\n// Every host setting, one member per row (defaults = the registry's).\nstruct Config {\n`;
for (const [n, r] of cfgRows) sh += `  ${ctypeOf(r)} ${r.field} = ${cLit(r, r.def)};  // ${n}\n`;
sh += `  CostParams cost;\n};\n\n` +
  `// host policy (schema "policy"): how hosts drive the engine\n` +
  policy.filter(([, r]) => typeof r.def === 'number').map(([n, r]) => `constexpr u32 kPolicy${cap(n)} = ${r.def};  // ${r.doc}\n`).join('') +
  policy.filter(([, r]) => Array.isArray(r.def)).map(([n, r]) => `constexpr double kPolicy${cap(n)}[] = {${r.def.join(', ')}};  // ${r.doc}\n`).join('') +
  `\n// the result of a settings document: which stages its applied rows affect\nstruct SettingsPatch {\n  bool ok = true;       // the document parsed\n` +
  `  u32 applied = 0;      // rows applied\n  u32 affects = 0;      // stageBit() set of the applied rows\n};\n` +
  `// applies a JSON settings document onto c in row order; unknown paths and\n// bad values are diagnostics (the row keeps its value)\n` +
  `SettingsPatch applySettings(Config& c, std::string_view json, DiagSink& diags);\n` +
  `// the effective settings as one JSON document (every row)\nstd::string settingsJson(const Config& c);\n\n}  // namespace tsr\n`;

const rowCase = ([n, r], k) => {
  const f = r.field.startsWith('cost.') ? `c.cost.${r.field.slice(5)}` : `c.${r.field}`;
  const [dom, ...rest] = r.dom.split(':');
  let body;
  if (dom === 'num') body = `double x;\n      if (!num(v, ${rest[0]}, ${rest[1]}, false, x, why)) return false;\n      ${f} = x;`;
  else if (dom === 'int') body = `double x;\n      if (!num(v, ${rest[0]}, ${rest[1]}, true, x, why)) return false;\n      ${f} = (${ctypeOf(r)})x;`;
  else if (dom === 'bool') body = `if (v.t != JsonValue::T::Bool) return type(why, "true or false");\n      ${f} = v.b;`;
  else if (dom === 'enum') {
    const ms = rest.join(':').split('|');
    body = `static const char* const kM[] = {${ms.map((m) => JSON.stringify(m)).join(', ')}};\n      int m = member(v, kM, ${ms.length}, why);\n      if (m < 0) return false;\n      ${f} = (${ctypeOf(r)})m;`;
  } else if (dom === 'map') {
    const vd = rest[0];
    body = `if (v.t != JsonValue::T::Obj) return type(why, "an object of strings");\n      std::map<std::string, std::string> mm;\n` +
      `      for (size_t mi = 0; mi < v.keys.size(); mi++) {\n        const JsonValue& mv = v.vals[mi];\n        if (mv.t != JsonValue::T::Str${vd ? ` || !matchDomain(TextDomain::${domEnum(vd)}, mv.str)` : ''}) return type(why, "${vd ?? 'string'} values");\n        mm[v.keys[mi]] = mv.str;\n      }\n      ${f} = std::move(mm);`;
  } else if (textDomains.includes(dom)) body = `if (v.t != JsonValue::T::Str || (${r.optional ? '!v.str.empty() && ' : ''}!matchDomain(TextDomain::${domEnum(dom)}, v.str))) return type(why, "${dom}${r.optional ? ' or empty' : ''}");\n      ${f} = v.str;`;
  else if (dom === 'str') body = `if (v.t != JsonValue::T::Str || v.str.size() > 4096) return type(why, "a string");\n      ${f} = v.str;`;
  else { console.error(`gen-schema: settings.${n}: unknown domain ${r.dom}`); process.exit(1); }
  if (r.apply) body += `\n      ${r.apply}(c, ${f});`;
  return `    case ${k}: {  // ${n}\n      ${body}\n      return true;\n    }\n`;
};
let sc = `// ${HDR}\n#include "settings.gen.h"\n\n#include <algorithm>\n#include <cmath>\n#include <cstdio>\n\n#include "../ops/domains.gen.h"\n#include "../support/json.h"\n#include "config.h"\n\nnamespace tsr {\nnamespace {\n\n` +
  `struct Row {\n  const char* path;\n  u32 affects;  // stageBit set\n  bool group;   // the value is an object (map rows)\n};\nconst Row kRows[] = {\n` +
  settings.map(([n, r]) => `    {${JSON.stringify(n)}, ${r.affects.map((a) => `stageBit(Stage::${a})`).join(' | ')}, ${r.dom.startsWith('map')}},`).join('\n') +
  `\n};\nconstexpr u32 kRowCount = sizeof kRows / sizeof kRows[0];\n\n` +
  `bool type(std::string& why, const char* want) {\n  why = std::string("expected ") + want;\n  return false;\n}\n` +
  `bool num(const JsonValue& v, double lo, double hi, bool integral, double& x, std::string& why) {\n` +
  `  if (v.t != JsonValue::T::Num || !std::isfinite(v.num)) return type(why, integral ? "an integer" : "a number");\n` +
  `  if (integral && v.num != std::floor(v.num)) return type(why, "an integer");\n` +
  `  if (v.num < lo || v.num > hi) {\n    char b[96];\n    std::snprintf(b, sizeof b, "a value in [%g, %g]", lo, hi);\n    why = std::string("expected ") + b;\n    return false;\n  }\n  x = v.num;\n  return true;\n}\n` +
  `int member(const JsonValue& v, const char* const* ms, int n, std::string& why) {\n  if (v.t == JsonValue::T::Str)\n    for (int k = 0; k < n; k++)\n      if (v.str == ms[k]) return k;\n` +
  `  why = "expected one of";\n  for (int k = 0; k < n; k++) why += std::string(k ? "|" : " ") + ms[k];\n  return -1;\n}\n\n` +
  `bool applyRow(Config& c, u32 row, const JsonValue& v, std::string& why) {\n  switch (row) {\n` +
  settings.map(rowCase).join('') + `    default:\n      return false;\n  }\n}\n\n` +
  `int rowOf(std::string_view path) {\n  for (u32 k = 0; k < kRowCount; k++)\n    if (path == kRows[k].path) return (int)k;\n  return -1;\n}\n` +
  `bool isPrefix(std::string_view path) {\n  for (u32 k = 0; k < kRowCount; k++) {\n    std::string_view p = kRows[k].path;\n` +
  `    if (p.size() > path.size() && p.substr(0, path.size()) == path && p[path.size()] == '.') return true;\n  }\n  return false;\n}\n\n` +
  `struct Hit {\n  u32 row;\n  const JsonValue* v;\n};\n` +
  `void collect(const JsonValue& o, const std::string& prefix, std::vector<Hit>& hits, DiagSink& diags) {\n` +
  `  for (size_t i = 0; i < o.keys.size(); i++) {\n    const std::string& k = o.keys[i];\n    const JsonValue& v = o.vals[i];\n    if (!k.empty() && k[0] == '$') continue;  // $comment, $vocab: annotations\n` +
  `    std::string path = prefix.empty() ? k : prefix + "." + k;\n    int r = rowOf(path);\n` +
  `    if (r >= 0) hits.push_back({(u32)r, &v});\n    else if (v.t == JsonValue::T::Obj && isPrefix(path)) collect(v, path, hits, diags);\n` +
  `    else diags.add(Sev::Warning, "setting-unknown", {}, "unknown setting '" + path + "'");\n  }\n}\n\n` +
  `void num(std::string& out, double v) {\n  char b[40];\n  std::snprintf(b, sizeof b, "%.15g", v);\n  out += b;\n}\n\n}  // namespace\n\n` +
  `SettingsPatch applySettings(Config& c, std::string_view json, DiagSink& diags) {\n  SettingsPatch p;\n  JsonValue doc;\n  JsonReader rd;\n` +
  `  if (!rd.parse(json, doc)) {\n    diags.add(Sev::Error, "setting-json", {}, std::string("settings: ") + rd.error() + " at byte " + std::to_string(rd.offset()));\n    p.ok = false;\n    return p;\n  }\n` +
  `  if (doc.t != JsonValue::T::Obj) {\n    diags.add(Sev::Error, "setting-json", {}, "settings: expected an object");\n    p.ok = false;\n    return p;\n  }\n` +
  `  std::vector<Hit> hits;\n  collect(doc, "", hits, diags);\n` +
  `  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.row < b.row; });  // row order\n` +
  `  for (const Hit& h : hits) {\n    std::string why;\n    if (applyRow(c, h.row, *h.v, why)) {\n      p.applied++;\n      p.affects |= kRows[h.row].affects;\n` +
  `    } else {\n      diags.add(Sev::Warning, "setting-type", {}, std::string(kRows[h.row].path) + ": " + why);\n    }\n  }\n  return p;\n}\n\n` +
  `std::string settingsJson(const Config& c) {\n  std::string out = "{";\n`;
let lastSec = null;
settings.forEach(([n, r], k) => {
  const [sec, name] = n.split('.');
  const f = r.field.startsWith('cost.') ? `c.cost.${r.field.slice(5)}` : `c.${r.field}`;
  let pre = '';
  if (sec !== lastSec) pre = `${lastSec ? '}, ' : ''}"${sec}": {`;
  else pre = ', ';
  lastSec = sec;
  sc += `  out += ${JSON.stringify(pre + JSON.stringify(name) + ': ')};\n`;
  const t = ctypeOf(r);
  if (r.ctype === 'PunctCompress') sc += `  { static const char* const kM[] = {${r.dom.slice(5).split('|').map((m) => JSON.stringify(m)).join(', ')}}; jsonString(out, kM[(int)${f}]); }\n`;
  else if (t.startsWith('std::map')) sc += `  out += '{';\n  { bool first = true; for (const auto& [mk, mv] : ${f}) { if (!first) out += ", "; first = false; jsonString(out, mk); out += ": "; jsonString(out, mv); } }\n  out += '}';\n`;
  else if (t === 'std::string') sc += `  jsonString(out, ${f});\n`;
  else if (t === 'bool') sc += `  out += ${f} ? "true" : "false";\n`;
  else sc += `  num(out, (double)${f});\n`;
});
sc += `  out += "}}";\n  return out;\n}\n\n}  // namespace tsr\n`;

// JS: defaults, rows, legacy options, policy
const nested = {};
for (const [n, r] of settings) { const [a, b] = n.split('.'); (nested[a] ??= {})[b] = r.def; }
const jsRows = Object.fromEntries(settings.map(([n, r]) => [n, { dom: r.dom, def: r.def, prec: r.prec, affects: r.affects }]));
const legacy = Object.fromEntries(settings.filter(([, r]) => r.legacy).map(([n, r]) => [r.legacy, n]));
const settingsJs = `// ${HDR}\n// Host settings (schema "settings"; plan P1-03) and host policy.\n` +
  emit('SETTINGS', jsRows) + emit('SETTINGS_DEFAULTS', nested) + emit('LEGACY_OPTIONS', legacy) +
  emit('POLICY', Object.fromEntries(policy.map(([n, r]) => [n, r.def]))) +
  `export const STAGES = Object.freeze(${JSON.stringify(STAGES)});\n` +
  `// the value of a dotted setting in a (partial) settings document, else its default\n` +
  `export function settingOf(settings, path) {\n  const [a, b] = path.split('.');\n  const v = settings?.[a]?.[b];\n  return v !== undefined ? v : SETTINGS[path]?.def;\n}\n` +
  `// one settings document from createEngine's legacy named options (MD-06) and\n// an explicit \`settings\` object, which wins\n` +
  `export function settingsFromOptions(opts = {}) {\n  const out = {};\n  for (const [opt, path] of Object.entries(LEGACY_OPTIONS)) {\n` +
  `    if (opts[opt] === undefined || opts[opt] === null) continue;\n    const [a, b] = path.split('.');\n    (out[a] ??= {})[b] = opts[opt];\n  }\n` +
  `  for (const [a, sec] of Object.entries(opts.settings ?? {})) {\n    if (sec && typeof sec === 'object' && !Array.isArray(sec)) out[a] = { ...(out[a] ?? {}), ...sec };\n    else out[a] = sec;\n  }\n  return out;\n}\n`;

let setMd = `<!-- ${HDR} -->\n# Host settings (generated)\n\nThe settings document of document-model §11 / docs/host-protocol-design.md, generated from ` +
  '`engine/schema/schema.json`. `affects` lists the stages whose products a change invalidates; the first one decides whether a patch applies in place or rebuilds the document.\n\n' +
  '| setting | domain | default | precedence | affects | replaces |\n|---|---|---|---|---|---|\n' +
  settings.map(([n, r]) => `| \`${n}\` | ${r.dom.replace(/\|/g, '\\|')} | \`${JSON.stringify(r.def).replace(/\|/g, '\\|')}\` | ${r.prec} | ${r.affects.join(', ')} | ${r.legacy ? '`' + r.legacy + '`' : ''} |`).join('\n') +
  '\n\n## Host policy\n\n| policy | default | meaning |\n|---|---|---|\n' +
  policy.map(([n, r]) => `| \`${n}\` | \`${JSON.stringify(r.def)}\` | ${r.doc} |`).join('\n') + '\n';

// ---- constructor specs (plan P2-03; docs/ctor-design.md) ----------------------
// A kind's "ctor" and the stdlib's derived constructors: parsed params, the
// options (every attribute not bound positionally, or "raw"), the names the
// hole module may bind (engine/src/codegen/stdnames.gen.h, sorted so that a
// new name changes only the modules that mention it).
const ctorErrors = [];
const parseParam = (kindName, attrs, p) => {
  const [k, name, dom] = p.split(':');
  if (k === 'text' || k === 'lines' || k === 'body') return name === undefined ? { k } : (ctorErrors.push(`${kindName}: param ${p}`), null);
  if (k !== 'attr' && k !== 'projected') return ctorErrors.push(`${kindName}: unknown param kind ${p}`), null;
  const d = dom ?? attrs?.[name]?.dom;
  if (!d) return ctorErrors.push(`${kindName}: param ${p} names no attribute of its kind`), null;
  return { k, name, dom: d };
};
const ctorSpecs = {};
const addCtor = (name, kindName, c, derived) => {
  if (ctorSpecs[name]) ctorErrors.push(`ctor ${name} defined twice`);
  const attrs = kindName ? S.kinds[kindName]?.attrs : null;
  if (kindName && !attrs) ctorErrors.push(`ctor ${name}: unknown kind ${kindName}`);
  const params = (c.params ?? []).map((p) => parseParam(name, attrs, p)).filter(Boolean);
  const bound = new Set(params.map((p) => p.name).filter(Boolean));
  const options = c.options === 'raw' ? 'raw'
    : Object.entries(attrs ?? {}).filter(([a, sp]) => !bound.has(a) && !resolvedAttr(sp)).map(([a]) => a);
  ctorSpecs[name] = { kind: kindName ?? null, params, options, nullary: !!c.nullary, sealed: !!c.sealed, derived };
};
for (const [n, k] of kinds) if (k.ctor) addCtor(k.ctor.name ?? n, n, k.ctor, false);
for (const [n, c] of Object.entries(S.stdlib?.ctors ?? {})) addCtor(n, c.kind, c, true);
const stdFunctions = S.stdlib?.functions ?? [];
for (const f of stdFunctions) if (ctorSpecs[f]) ctorErrors.push(`std function ${f} is also a constructor`);
const aliases = S.stdlib?.aliases ?? {};
if (ctorErrors.length) { for (const e of ctorErrors) console.error('gen-schema: ' + e); process.exit(1); }
const stdNames = [...Object.keys(ctorSpecs), ...stdFunctions].sort();
const ctorsJs = `// ${HDR}\n// The constructor specs (plan P2-03; docs/ctor-design.md): kind constructors\n` +
  `// and derived ones, as the binder (runtime/src/shared/stdlib.mjs) reads them.\n` +
  emit('CTOR_SPECS', ctorSpecs) +
  emit('STD_ALIASES', aliases) +
  emit('STD_FUNCTIONS', stdFunctions) +
  emit('STD_NAMES', stdNames);
const stdNamesH = `// ${HDR}\n// The names a hole module may bind from __rt.std (plan P2-03): every\n` +
  `// constructor and std function, sorted.\n#pragma once\n\nnamespace tsr {\n\n` +
  `inline constexpr const char* kStdNames[] = {\n${stdNames.map((n) => `    "${n}",`).join('\n')}\n};\n\n}  // namespace tsr\n`;
const ctorSig = (name) => {
  const c = ctorSpecs[name];
  const ps = c.params.map((p) => (p.k === 'attr' || p.k === 'projected' ? p.name : p.k));
  if (c.options === 'raw' || c.options.length) ps.push('{options}');
  if (!c.nullary || ps.length) ps.push('…');
  return `\`${name}(${ps.join(', ')})\`${c.nullary ? ' (nullary)' : ''}${c.sealed ? ' (sealed)' : ''}`;
};

// ---- docs/schema-table.md --------------------------------------------------------
let md = `<!-- ${HDR} -->\n# Ops vocabulary (generated)\n\nThe kind table of document-model §2.1, generated from ` +
  '`engine/schema/schema.json`. Ops version ' + S.opsVersion + ', min compat ' + S.minCompat + '.\n\n' +
  '| id | kind | level | body | inline | attributes (writer order: domain) | constructor |\n|---|---|---|---|---|---|---|\n';
for (const [n, k] of kinds) {
  const at = Object.entries(k.attrs).map(([a, s]) => `\`${a}\`: ${s.dom.replace(/\|/g, '\\|')}`).join('; ') || '—';
  const ct = k.ctor ? ctorSig(k.ctor.name ?? n) : '—';
  md += `| ${k.id} | \`${n}\` | ${k.level} | ${k.body} | ${k.inline} | ${at} | ${ct} |\n`;
}
md += '\nDerived constructors (`stdlib.ctors`): ' +
  Object.keys(ctorSpecs).filter((c) => ctorSpecs[c].derived).map(ctorSig).join(', ') +
  '. Std functions: ' + stdFunctions.map((f) => `\`${f}\``).join(', ') + '.\n';
md += '\n| op | id |\n|---|---|\n' + ops.map(([n, o]) => `| ${n} | ${o.id} |`).join('\n') + '\n';

// ---- write / check ---------------------------------------------------------------
const outputs = {
  'engine/src/ops/ops.def': def,
  'engine/src/ops/schema.gen.h': h,
  'engine/src/ops/schema.gen.cc': cc,
  'runtime/src/shared/ops.gen.mjs': js,
  'engine/src/ops/domains.gen.h': domH,
  'engine/src/ops/domains.gen.cc': domCc,
  'engine/src/model/props.gen.h': ph,
  'engine/src/render/style_css.gen.h': css,
  'runtime/src/shared/props.gen.mjs': propsJs,
  'engine/src/api/settings.gen.h': sh,
  'engine/src/api/settings.gen.cc': sc,
  'runtime/src/shared/settings.gen.mjs': settingsJs,
  'docs/settings-table.md': setMd,
  'docs/schema-table.md': md,
  'runtime/src/shared/ctors.gen.mjs': ctorsJs,
  'engine/src/codegen/stdnames.gen.h': stdNamesH,
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
