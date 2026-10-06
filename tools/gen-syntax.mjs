#!/usr/bin/env node
// engine/src/syntax/syntax.def → generated sources (plan P1-05; design T1
// SyntaxTable). Run by tools/gen-all.mjs; `--check` fails on stale outputs.
//   engine/src/syntax/syntax.gen.h   SYNTAX_VERSION, SugarId, payload structs,
//                                     character classes, reserved heads, token tags
//   engine/src/syntax/syntax.gen.cc  the generic AST dump (fields per row format)
//   runtime/src/shared/syntax.gen.json  constants for tooling (grammars, P1-09)
//   runtime/src/shared/syntax.gen.mjs   token tags for the JS token provider
//   docs/syntax-table.md
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const CHECK = process.argv.includes('--check');
const def = readFileSync(join(root, 'engine/src/syntax/syntax.def'), 'utf8')
  .split('\n').map((l) => l.replace(/^\s*\/\/.*$/, '')).join('\n');
const HDR = 'GENERATED from engine/src/syntax/syntax.def by tools/gen-syntax.mjs — do not edit.';

// row parsing: NAME(arg, arg, …) with "quoted" args (\" inside)
function rows(name) {
  const out = [];
  const re = new RegExp(`^${name}\\(([^\\n]*)\\)\\s*$`, 'gm');
  for (const m of def.matchAll(re)) {
    const args = [];
    let cur = '', q = false;
    for (let i = 0; i < m[1].length; i++) {
      const c = m[1][i];
      if (q) {
        if (c === '\\' && i + 1 < m[1].length) { cur += m[1][i + 1]; i++; continue; }
        if (c === '"') { q = false; continue; }
        cur += c;
        continue;
      }
      if (c === '"') { q = true; continue; }
      if (c === ',') { args.push(cur.trim()); cur = ''; continue; }
      cur += c;
    }
    args.push(cur.trim());
    out.push(args);
  }
  return out;
}
const version = Number(/^SYNTAX_VERSION\((\d+)\)/m.exec(def)[1]);
const classes = rows('CLASS');
const inlines = rows('INLINE');
const blocks = rows('BLOCK');
const keywords = rows('KEYWORD');
const unsupported = rows('RESERVED_UNSUPPORTED')[0];
const reserved = rows('RESERVED')[0];
const tokenTags = rows('TOKEN_TAGS')[0];
const sugars = rows('SUGAR').map(([id, form, payload, dump]) => ({ id, form, payload, dump }));
const nodes = rows('NODE').map(([id, payload, dump]) => ({ id, payload, dump }));

const CT = { u8: 'u8', bool: 'bool', i32: 'i32', u32: 'u32', str: 'StrRef', src: 'Span' };
// payload fields "name:type"; a trailing '?' (name:str?) marks a field the
// JSON AST omits when it is empty (plan P2-04: Text's rawmap)
const fieldsOf = (p) => (p ? p.split(/\s+/).filter(Boolean).map((f) => {
  const [n, t] = f.split(':');
  return t.endsWith('?') ? [n, t.slice(0, -1), true] : [n, t, false];
}) : []);
const cap = (s) => s[0].toUpperCase() + s.slice(1);
const structName = (id) => `${cap(id)}P`;

// ---- character classes: "A-Za-z_$" → a byte predicate
// "^…" is a negated class (plan P2-06: LabelChar): any byte that is not a
// listed one, an ASCII control or DEL — bytes from 0x80 belong to it (the
// lexer checks code points for Unicode whitespace)
function classPred(name, spec) {
  const negated = spec[0] === '^';
  if (negated) spec = spec.slice(1);
  const parts = [];
  for (let i = 0; i < spec.length; i++) {
    if (spec[i + 1] === '-' && i + 2 < spec.length) {
      parts.push(`(c >= '${spec[i]}' && c <= '${spec[i + 2]}')`);
      i += 2;
    } else {
      parts.push(`c == '${spec[i] === "'" ? "\\'" : spec[i] === '\\' ? '\\\\' : spec[i]}'`);
    }
  }
  if (negated)
    return `inline bool is${name}(char c) { return !((unsigned char)c < 0x20 || c == 0x7f || ${parts.join(' || ')}); }\n`;
  return `inline bool is${name}(char c) { return ${parts.join(' || ')}; }\n`;
}

let h = `// ${HDR}\n#pragma once\n#include <string_view>\n\n#include "../support/support.h"\n\nnamespace tsr {\n\n` +
  `constexpr u32 SYNTAX_VERSION = ${version};\n\n` +
  `// character classes\n` + classes.map(([n, s]) => classPred(n, s)).join('') +
  `\n// sugar: a built-in Call's slot (its meaning); the payload struct follows\nenum class SugarId : u16 { ${sugars.map((s) => s.id).join(', ')} };\n` +
  `constexpr const char* kSugarName[] = {${sugars.map((s) => JSON.stringify(s.id)).join(', ')}};\n` +
  `constexpr u32 kSugarCount = ${sugars.length};\n\n// payloads (zero-width side records trailing their node)\n`;
const payloadOf = new Map();
for (const s of [...sugars, ...nodes]) {
  const f = fieldsOf(s.payload);
  if (!f.length) continue;
  const sn = structName(s.id);
  payloadOf.set(s.id, sn);
  h += `struct ${sn} {\n${f.map(([n, t]) => `  ${CT[t]} ${n}${t === 'src' ? '{}' : ' = 0'};\n`).join('')}};\n`;
}
// ---- inline rules: the opener dispatch (first byte, longest literal opener
// first; placeholders such as HEAD / BARE_ID are the lexer's to check)
const ruleIds = inlines.map((r) => r[0]);
const literalOf = (open) => open.replace(/[A-Z][A-Z_]+$/, '');
const precs = [...new Set(inlines.map((r) => r[6]))];
const bodies = [...new Set(inlines.map((r) => r[3]))];
const byFirst = new Map();
for (const r of inlines) {
  const lit = literalOf(r[1]);
  if (!byFirst.has(lit[0])) byFirst.set(lit[0], []);
  byFirst.get(lit[0]).push([lit, r[0]]);
}
const cch = (c) => (c === "'" ? "\\'" : c === '\\' ? '\\\\' : c);
h += `\n// inline rules (INLINE rows): precedence and body mode per rule\n` +
  `enum class InlineRule : u8 { none, ${ruleIds.join(', ')} };\n` +
  `enum class InlinePrec : u8 { ${precs.join(', ')} };\n` +
  `enum class InlineBody : u8 { ${bodies.join(', ')} };\n` +
  `constexpr InlinePrec kInlinePrec[] = {InlinePrec::Markup, ${inlines.map((r) => `InlinePrec::${r[6]}`).join(', ')}};\n` +
  `constexpr InlineBody kInlineBody[] = {InlineBody::Pair, ${inlines.map((r) => `InlineBody::${r[3]}`).join(', ')}};\n` +
  `// the rule whose literal opener starts at t[i] (longest first)\n` +
  `inline InlineRule inlineOpener(std::string_view t, u32 i) {\n  switch (t[i]) {\n`;
for (const [c, list] of byFirst) {
  list.sort((x, y) => y[0].length - x[0].length);
  h += `    case '${cch(c)}':\n`;
  for (const [lit, id] of list)
    h += lit.length > 1
      ? `      if (t.substr(i, ${lit.length}) == ${JSON.stringify(lit)}) return InlineRule::${id};\n`
      : `      return InlineRule::${id};\n`;
  if (list[list.length - 1][0].length > 1) h += `      break;\n`;
}
h += `    default:\n      break;\n  }\n  return InlineRule::none;\n}\n`;
{
  const firsts = [...byFirst.keys()].map((c) => c.charCodeAt(0));
  const row = Array.from({ length: 256 }, (_, k) => (firsts.includes(k) ? 1 : 0));
  h += `// bytes that may start an inline rule (everything else is plain text)\n` +
    `constexpr bool kInlineOpenerByte[256] = {\n` +
    Array.from({ length: 16 }, (_, r) => '    ' + row.slice(r * 16, r * 16 + 16).join(', ') + ',').join('\n') + '\n};\n';
}

h += `\n// a bare splice head that cannot start a JS expression (plan P0-05)\n` +
  `inline const char* reservedSpliceHead(std::string_view w) {\n` +
  `  static constexpr std::string_view kUnsupported[] = {${unsupported.map((w) => JSON.stringify(w)).join(', ')}};\n` +
  `  static constexpr std::string_view kReserved[] = {${reserved.map((w) => JSON.stringify(w)).join(', ')}};\n` +
  `  for (std::string_view k : kUnsupported)\n    if (w == k) return "keyword-unsupported";\n` +
  `  for (std::string_view k : kReserved)\n    if (w == k) return "reserved-word";\n  return nullptr;\n}\n\n` +
  `// highlight token tags (shared with runtime/src/shared/syntax.gen.mjs)\n` +
  `constexpr const char* kTokenTags[] = {${tokenTags.map((t) => JSON.stringify(t)).join(', ')}};\n` +
  `constexpr int kTokenTagCount = ${tokenTags.length};\n\n}  // namespace tsr\n`;

// ---- the generic dump: compile each row's template to C++
function dumpCode(row, payloadExpr) {
  const fields = Object.fromEntries(fieldsOf(row.payload));
  let code = '';
  const lit = (s) => (s ? `  out += ${JSON.stringify(s)};\n` : '');
  // string values always print escaped (they sit inside quotes in the formats)
  const value = (name) => {
    if (name === '$str') return `  appendEscaped(out, strs.get(n->str));\n`;
    const t = fields[name];
    if (!t) throw new Error(`${row.id}: dump field ${name} not in payload`);
    const v = `${payloadExpr}.${name}`;
    if (t === 'str') return `  appendEscaped(out, strs.get(${v}));\n`;
    if (t === 'src') return `  appendEscaped(out, src.slice(${v}));\n`;
    if (t === 'u32') return `  appendf(out, "%u", (unsigned)${v});\n`;
    return `  appendf(out, "%d", (int)${v});\n`;
  };
  const set = (name) => {
    const t = fields[name];
    const v = `${payloadExpr}.${name}`;
    return t === 'src' ? `!${v}.empty()` : `${v} != 0`;
  };
  function compile(t) {
    let out = '';
    let i = 0, buf = '';
    while (i < t.length) {
      if (t[i] !== '{') { buf += t[i++]; continue; }
      out += lit(buf);
      buf = '';
      let depth = 0, j = i;
      for (; j < t.length; j++) {
        if (t[j] === '{') depth++;
        else if (t[j] === '}' && --depth === 0) break;
      }
      const inner = t.slice(i + 1, j);
      i = j + 1;
      if (inner.startsWith('?')) {
        const sp = inner.indexOf(' ');
        const name = inner.slice(1, sp);
        out += `  if (${set(name)}) {\n${compile(inner.slice(sp)).replace(/^(?=.)/gm, '  ')}  }\n`;
      } else if (/^\w+\?/.test(inner)) {
        const [name, alts] = [inner.slice(0, inner.indexOf('?')), inner.slice(inner.indexOf('?') + 1)];
        const [a, b] = alts.split(':');
        out += `  out += ${payloadExpr}.${name} ? ${JSON.stringify(a)} : ${JSON.stringify(b)};\n`;
      } else {
        out += value(inner);
      }
    }
    out += lit(buf);
    return out;
  }
  return compile(row.dump).replace(/^  out \+= "";\n/gm, '');
}
const indent = (code, n) => code.replace(/^(?=.)/gm, ' '.repeat(n));
let cc = `// ${HDR}\n#include "../ast/ast.h"\n#include "../support/json.h"\n\nnamespace tsr {\n\n` +
  `// one node's line: "<name> @[s,e)<fields>" — the kind (or the sugar of a\n// Call) picks the row whose format prints it\n` +
  `void dumpAstNode(std::string& out, const AstNode* n, const SourceText& src, const Interner& strs) {\n` +
  `  auto spanOut = [&] { appendf(out, " @[%u,%u)", n->span.start, n->span.end); };\n` +
  `  switch (n->kind) {\n`;
const rowCase = (label, row, pe, depth) => {
  const head = row.dump.match(/^(\{[^}]*\}|[^ {]+)/)[0];
  return `${' '.repeat(depth)}case ${label}: {\n` +
    indent(dumpCode({ ...row, dump: head }, pe), depth) +
    `${' '.repeat(depth + 2)}spanOut();\n` +
    indent(dumpCode({ ...row, dump: row.dump.slice(head.length) }, pe), depth) +
    `${' '.repeat(depth + 2)}break;\n${' '.repeat(depth)}}\n`;
};
for (const k of ['Doc', 'Text', 'Comment', 'Call', 'Splice', 'Stmt', 'Error']) {
  if (k === 'Call') {
    cc += `    case AstKind::Call:\n      switch (n->sugar) {\n`;
    for (const sg of sugars)
      cc += rowCase(`SugarId::${sg.id}`, sg, payloadOf.has(sg.id) ? `side<${payloadOf.get(sg.id)}>(n)` : '', 8);
    cc += `      }\n      break;\n`;
    continue;
  }
  const row = nodes.find((n) => n.id === k);
  cc += rowCase(`AstKind::${k}`, row, payloadOf.has(k) ? `side<${payloadOf.get(k)}>(n)` : '', 4);
}
cc += `  }\n}\n\n`;

// ---- the JSON AST (tsrc --stage=astjson, tsr_parse_json; plan P1-09): one
// node's fields, from the same payload rows
const KIND_NAMES = ['Doc', 'Text', 'Comment', 'Call', 'Splice', 'Stmt', 'Error'];
const jsonField = (name, type, v, optional) => {
  const key = `out += ${JSON.stringify(`,"${name}":`)};\n`;
  if (optional && type === 'str') return `  if (${v}) {\n  ${key}  jsonString(out, strs.get(${v}));\n  }\n`;
  if (optional && type === 'bool') return `  if (${v}) {\n  ${key}  out += "true";\n  }\n`;
  if (type === 'str') return `  ${key}  jsonString(out, strs.get(${v}));\n`;
  if (type === 'src') return `  ${key}  jsonString(out, src.slice(${v}));\n`;
  if (type === 'bool') return `  ${key}  out += ${v} ? "true" : "false";\n`;
  if (type === 'u32') return `  ${key}  appendf(out, "%u", (unsigned)${v});\n`;
  return `  ${key}  appendf(out, "%d", (int)${v});\n`;
};
// a case body: the payload's fields, then break
const jsonCase = (label, row) => {
  const f = fieldsOf(row.payload);
  if (!f.length) return `case ${label}:\n  break;\n`;
  const sn = payloadOf.get(row.id);
  return `case ${label}: {\n  const ${sn}& p = side<${sn}>(n);\n` +
    f.map(([name, t, opt]) => jsonField(name, t, 'p.' + name, opt)).join('') + `  break;\n}\n`;
};
cc += `// one node's JSON members (no braces, no kids): kind, sugar, span, str and
// its payload fields
void jsonAstNode(std::string& out, const AstNode* n, const SourceText& src, const Interner& strs) {
  static constexpr const char* kKind[] = {${KIND_NAMES.map((k) => JSON.stringify(k.toLowerCase())).join(', ')}};
  out += "\\"kind\\":\\"";
  out += kKind[(int)n->kind];
  out += "\\"";
  if (n->kind == AstKind::Call) {
    out += ",\\"sugar\\":\\"";
    out += kSugarName[(int)n->sugar];
    out += "\\"";
  }
  appendf(out, ",\\"span\\":[%u,%u]", n->span.start, n->span.end);
  if (n->str) {
    out += ",\\"str\\":";
    jsonString(out, strs.get(n->str));
  }
  switch (n->kind) {
`;
for (const k of KIND_NAMES) {
  if (k === 'Call') {
    cc += `    case AstKind::Call:\n      switch (n->sugar) {\n`;
    for (const sg of sugars) cc += indent(jsonCase(`SugarId::${sg.id}`, sg), 8);
    cc += `      }\n      break;\n`;
    continue;
  }
  cc += indent(jsonCase(`AstKind::${k}`, nodes.find((n) => n.id === k)), 4);
}
cc += `  }\n}\n\n}  // namespace tsr\n`;

const json = JSON.stringify({
  version, classes: Object.fromEntries(classes),
  inline: inlines.map(([id, open, close, body, params, guard, prec, slot, capture]) =>
    ({ id, open, close, body, params, guard, prec, slot, capture })),
  block: blocks.map(([id, shape, starter, interrupts, own, slot]) => ({ id, shape, starter, interrupts, own, slot })),
  keywords: keywords.map(([k, ...p]) => ({ keyword: k, params: p })),
  reservedHeads: { unsupported, reserved },
  tokenTags, sugar: sugars.map((s) => ({ id: s.id, form: s.form, payload: fieldsOf(s.payload).map(([n, t]) => [n, t]) })),
}, null, 1) + '\n';
const mjs = `// ${HDR}\nexport const SYNTAX_VERSION = ${version};\n` +
  `export const TOKEN_TAGS = Object.freeze(${JSON.stringify(tokenTags)});\n`;

// a markdown code span that survives backticks and pipes in table cells
const mdCode = (x) => {
  const t = x.replace(/\|/g, '\\|');
  return t.includes('`') ? '`` ' + t + ' ``' : '`' + t + '`';
};
let md = `<!-- ${HDR} -->\n# Surface syntax (generated)\n\nFrom \`engine/src/syntax/syntax.def\`, syntax version ${version}. ` +
  'Behaviour for each body mode, ownership class and block shape: `docs/syntax-design.md`.\n\n' +
  '## Inline delimiters\n\n| id | open | close | body | params | guard | precedence | slot | capture |\n|---|---|---|---|---|---|---|---|---|\n' +
  inlines.map((r) => '| ' + r.map(mdCode).join(' | ') + ' |').join('\n') +
  '\n\n## Blocks\n\n| id | shape | starter | interrupts | ownership | slot |\n|---|---|---|---|---|---|\n' +
  blocks.map((r) => '| ' + r.map(mdCode).join(' | ') + ' |').join('\n') +
  '\n\n## Sugar slots\n\n| slot | form | payload | dump |\n|---|---|---|---|\n' +
  sugars.map((s) => `| \`${s.id}\` | ${s.form} | ${s.payload || '—'} | ${mdCode(s.dump)} |`).join('\n') +
  '\n\n## Character classes\n\n' + classes.map(([n, s]) => `- \`${n}\`: ${mdCode(s)}`).join('\n') +
  `\n\n## Keywords and reserved splice heads\n\nKeyword forms (P2-12): ${keywords.map((k) => '`' + k[0] + '`').join(', ')}. ` +
  `Not yet supported as heads: ${unsupported.map((w) => '`' + w + '`').join(', ')}. Reserved: ${reserved.map((w) => '`' + w + '`').join(', ')}.\n` +
  `\n## Token tags\n\n${tokenTags.map((t) => '`' + t + '`').join(', ')}\n`;

const outputs = {
  'engine/src/syntax/syntax.gen.h': h,
  'engine/src/syntax/syntax.gen.cc': cc,
  'runtime/src/shared/syntax.gen.json': json,
  'runtime/src/shared/syntax.gen.mjs': mjs,
  'docs/syntax-table.md': md,
};
let stale = 0;
for (const [rel, text] of Object.entries(outputs)) {
  const p = join(root, rel);
  const prev = existsSync(p) ? readFileSync(p, 'utf8') : null;
  if (prev === text) continue;
  if (CHECK) { console.error(`gen-syntax: ${rel} is stale (run node tools/gen-syntax.mjs)`); stale++; continue; }
  writeFileSync(p, text);
  console.log(`wrote ${rel}`);
}
if (stale) process.exit(1);
