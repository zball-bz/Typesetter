#!/usr/bin/env node
// The .tsm syntax skill (an agent skill: skills/tsm/SKILL.md + reference.md)
// from its hand-written source and the engine's own tables, so it follows
// the language as the engine reads it:
//   skills/tsm.src.md                 the prose ({{…}} placeholders below)
//   engine/schema/schema.json         constructors, options, style keys, aliases
//   runtime/src/shared/syntax.gen.json  the syntax version
//   engine/schema/languages.json      code-fence languages, aliases, profiles
//   engine/data/math/{stdlib,symbols}.tsv  math functions and symbols
// Syntax only: what the engine reads and what it means, never how to write.
// Run by tools/gen-all.mjs; `--check` fails on stale outputs. Its examples
// render clean: tools/check-skill.mjs (gate G6).
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const CHECK = process.argv.includes('--check');
const read = (p) => readFileSync(join(root, p), 'utf8');
const schema = JSON.parse(read('engine/schema/schema.json'));
const syntax = JSON.parse(read('runtime/src/shared/syntax.gen.json'));
const langs = JSON.parse(read('engine/schema/languages.json'));
const { STYLE_KEYS, STYLE_SUGAR, STYLE_FLAGS } = await import(join(root, 'runtime/src/shared/props.gen.mjs'));
const HDR = 'GENERATED from skills/tsm.src.md and the engine\'s tables by tools/gen-skill.mjs — do not edit; ' +
  'edit the source and run node tools/gen-all.mjs';
const code = (s) => '`' + s + '`';
const cell = (s) => String(s).replace(/\|/g, '\\|');
const tsv = (p) => read(p).split('\n').filter((l) => l && !l.startsWith('#')).slice(1).map((l) => l.split('\t'));

// a domain as an author reads it
function domain(dom = '') {
  if (dom === 'bool') return 'true / false';
  if (dom.startsWith('flags:'))  // a set of names: one or a list
    return dom.slice(6).split(',').map((f) => `"${f.split('=')[0].toLowerCase()}"`).join(' / ') + ' or a list of them';
  if (dom.startsWith('enum:')) return dom.slice(5).split('|').map((v) => `"${v}"`).join(' / ');
  const m = /^(int|num):([^:]+):(.+)$/.exec(dom);
  if (m) return `${m[1] === 'int' ? 'integer' : 'number'} ${m[2]}–${m[3]}`;
  return { str: 'string', token: 'string', ident: 'name', label: 'label', url: 'URL', len: 'length',
           html: 'HTML', rangeset: '"1,3-5"', intlist: 'list of integers', tracks: 'column list',
           names: 'list of names', features: 'font features', color: 'colour', font: 'font list',
           lang: 'BCP-47 tag', weight: '100–900', gap: 'length', size: 'length or percent',
           lens: '1–4 lengths (CSS order)' }[dom] ?? dom;
}

// --- SKILL.md: the source with its placeholders filled ----------------------
const languageList = Object.entries(langs.languages).filter(([k]) => !k.startsWith('$'))
  .map(([name, l]) => code(name) + (l.aliases?.length ? ` (${l.aliases.map(code).join(', ')})` : '')).join(', ') +
  '; ' + Object.entries(langs.profiles ?? {}).map(([p, v]) =>
    `${code(p)} (${v.lang} with ${(v.overlays ?? []).join(', ')} overlays)`).join(', ');
const aliases = Object.entries(schema.stdlib.aliases).map(([a, b]) => `${code(a)} means ${code(b)}`).join(', ');
const fill = {
  'generated-note': HDR,
  'syntax-version': String(syntax.version),
  languages: languageList,
  aliases,
};
let skill = read('skills/tsm.src.md').replace(/\{\{([a-z-]+)\}\}/g, (m, k) => {
  if (!(k in fill)) throw new Error(`skills/tsm.src.md: unknown placeholder ${m}`);
  return fill[k];
});

// --- reference.md: the tables ----------------------------------------------
const out = [`<!-- ${HDR} -->`, '# .tsm reference tables', '',
  'Generated from the engine\'s schema and math tables; the syntax itself is SKILL.md.', ''];

// constructors: a kind's public constructor, then the derived ones
out.push('## Constructors', '',
  'Leading arguments bind to the parameters while they fit, then one options object, then the content ' +
  '(`#name(params…, {options})[content]`). Every constructor also takes the universal options ' +
  `${Object.keys(schema.universal).filter((k) => !k.startsWith('$') && k !== 'style').map(code).join(', ')} and ${code('style: {…}')}.`, '',
  '| constructor | parameters | options |', '|---|---|---|');
const SKIP_KINDS = new Set(['doc']);
const param = (p) => {
  const [k, name] = p.split(':');
  return { attr: name, projected: name, text: 'text', lines: 'lines', body: 'content' }[k] ?? p;
};
const optionsOf = (attrs, bound) => Object.entries(attrs ?? {})
  .filter(([a, r]) => !bound.has(a) && !(r.flags ?? []).includes('resolved'))
  .map(([a, r]) => `${code(a)} ${cell(domain(r.dom))}`).join('; ');
for (const [kind, k] of Object.entries(schema.kinds)) {
  if (kind.startsWith('$') || SKIP_KINDS.has(kind) || !k.ctor || k.ctor.sealed) continue;
  const name = k.ctor.name ?? kind;
  const params = (k.ctor.params ?? []).map(param);
  const bound = new Set((k.ctor.params ?? []).map((p) => p.split(':')[1]).filter(Boolean));
  const opts = kind === 'styled' ? 'a style patch (Style keys)' : optionsOf(k.attrs, bound);
  out.push(`| ${code(k.ctor.nullary ? name : `${name}(${params.join(', ')})`)}${k.ctor.nullary ? ' (bare: `#' + name + '`)' : ''} | ${params.map(code).join(', ') || '—'} | ${opts || '—'} |`);
}
for (const [name, c] of Object.entries(schema.stdlib.ctors)) {
  if (c.sealed && name !== 'node') continue;
  const params = (c.params ?? []).map(param);
  // a derived constructor's options are its implementation's (raw) or none of
  // its kind's: #toc is a collect node, yet takes no `what`
  const opts = name === 'style' ? 'a style patch (Style keys)' : c.options === 'raw' ? 'any (passed to the implementation)' : '';
  out.push(`| ${code(c.nullary ? name : `${name}(${params.join(', ')})`)}${c.nullary ? ' (bare: `#' + name + '`)' : ''} | ${params.map(code).join(', ') || '—'} | ${opts || '—'} |`);
}
out.push('', `Functions: ${schema.stdlib.functions.map(code).join(', ')}` +
  ` (${schema.stdlib.asyncFunctions.map(code).join(', ')} returns a promise; a splice of it awaits).`, '');

// style keys: the patch spelling (a dotted key nests: par.indent is {par: {indent}})
out.push('## Style keys', '',
  'A dotted key nests: `par.indent` is written `{par: {indent: …}}`. Lengths are CSS lengths; a bare number is em.', '',
  '| key | value |', '|---|---|');
for (const [key, attr] of Object.entries(STYLE_KEYS))
  out.push(`| ${code(key)} | ${cell(domain(schema.kinds.styled.attrs[attr]?.dom))} |`);
const sugarValue = (a, v) => {
  const flag = Object.entries(STYLE_FLAGS[a] ?? {}).find(([, f]) => f === v);
  return flag ? `"${flag[0]}"` : JSON.stringify(v);
};
out.push('', `Sugar: ${Object.entries(STYLE_SUGAR).map(([k, [a, v]]) => `${code(k)} is ${code(`${a}: ${sugarValue(a, v)}`)}`).join(', ')}.`, '');

// math functions
out.push('## Math functions', '',
  'A call binds only on a `(` directly after the name; without it the name means its bare symbol, else it is an upright name.', '',
  '| call | bare name means |', '|---|---|');
for (const [sig, , bare] of tsv('engine/data/math/stdlib.tsv'))
  out.push(`| ${code(sig.replace(/\(\)$/, ''))} | ${bare && bare !== '-' ? cell(bare) : '—'} |`);
out.push('', 'Primitives callable by name: `frac(a, b)`, `attach(x, t: …, b: …, tl: …, tr: …, bl: …, br: …)`, `class(rel, …)`, `lr(…)`.', '');

// math symbols, by class
out.push('## Math symbols', '', 'Name → symbol, by spacing class. A typed symbol behaves as its name.', '');
const byClass = new Map();
for (const [name, cp, cls, flags] of tsv('engine/data/math/symbols.tsv')) {
  const key = cp === '0' || cp === '0000' ? 'operator name (upright)' : `${cls}${/large/.test(flags ?? '') ? ' (large)' : ''}`;
  if (!byClass.has(key)) byClass.set(key, []);
  const ch = cp === '0' || cp === '0000' ? '' : ' ' + String.fromCodePoint(parseInt(cp, 16));
  byClass.get(key).push(code(name) + ch);
}
for (const [cls, list] of [...byClass].sort(([a], [b]) => a.localeCompare(b)))
  out.push(`- **${cls}**: ${list.join(', ')}`);
out.push('');

// --- write -------------------------------------------------------------------
let stale = 0;
function emit(rel, text) {
  const p = join(root, rel);
  const old = existsSync(p) ? readFileSync(p, 'utf8') : null;
  if (old === text) return;
  if (CHECK) {
    console.error(`gen-skill: ${rel} is stale (run node tools/gen-all.mjs)`);
    stale++;
    return;
  }
  mkdirSync(dirname(p), { recursive: true });
  writeFileSync(p, text);
  console.log(`wrote ${rel}`);
}
emit('skills/tsm/SKILL.md', skill);
emit('skills/tsm/reference.md', out.join('\n'));
if (stale) process.exit(1);
