#!/usr/bin/env node
// Architecture lint (docs/remediation/PLAN.md gate G10; principles P1-P5).
//
//   node tools/lint-arch.mjs                  fail on violations not in the baseline
//   node tools/lint-arch.mjs --update-baseline record today's violations
//
// Each rule is a regex over source lines in a file set. Existing violations
// are listed in tools/lint-arch.baseline.json keyed by rule, file and the
// trimmed line text (line numbers move). A baseline entry that no longer
// matches is stale and fails the run, so the baseline can only shrink; new
// rules join as the plan steps that enforce them land.
import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const baselinePath = join(root, 'tools/lint-arch.baseline.json');

function* walk(dir) {
  if (!existsSync(dir)) return;
  for (const e of readdirSync(dir).sort()) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else yield p;
  }
}
const files = (dirs, exts) =>
  dirs.flatMap((d) => [...walk(join(root, d))]).filter((p) => exts.some((x) => p.endsWith(x)));

// P2: dispatch on generated ids, never on spellings
const RULES = [
  {
    id: 'role-string-compare',
    why: 'P2: no layer compares role/kind/what strings (use generated ids / element registry)',
    files: () => files(['engine/src/resolve', 'engine/src/emit', 'engine/src/boxtree', 'engine/src/layout',
                        'engine/src/paint', 'engine/src/render', 'engine/src/break'], ['.cc', '.h']),
    re: /==\s*(std::string_view\()?"[a-z][a-z-]*"/,
  },
  {
    id: 'layout-includes-model',
    why: 'P4: layout/break/paint and the typeset writer may not see model.h, not even through another header (read the box tree, fragments and the DisplayList)',
    files: () => files(['engine/src/layout', 'engine/src/break', 'engine/src/paint'], ['.cc', '.h'])
      .concat(files(['engine/src/render'], ['typeset_html.cc', 'typeset_html.h'])),
    closure: /model\/model\.h$/,
  },
  {
    id: 'shell-dom-scrape',
    why: 'P5/H: the shell may not scrape DOM ids or label spellings (use the anchors table)',
    files: () => files(['runtime/src/main'], ['.mjs']),
    re: /#tsr-fn-|tsr-fnref-|\[href\^=/,
  },
  {
    id: 'config-closure',
    why: 'P3-32 (T9 M12): stage code sees no Config definition (api/settings.gen.h), not even through another header — it reads settings through its stage view (settings_views.gen.h), so the compiler proves what each stage reads (Emit: no host.width)',
    files: () => readdirSync(join(root, 'engine/src'))
      .filter((d) => d !== 'api' && statSync(join(root, 'engine/src', d)).isDirectory())
      .flatMap((d) => files([`engine/src/${d}`], ['.cc', '.h'])),
    closure: /api\/settings\.gen\.h$/,
  },
  {
    id: 'colour-as-semantics',
    why: 'P2: semantics never travel through paint values (use classes / properties)',
    files: () => files(['engine/src'], ['.cc', '.h']),
    re: /var\(--tsr-tok-comment\)/,
  },
];

// the headers a file sees, transitively (quoted includes that resolve in the
// tree; system and generated-at-build headers are leaves): the first chain
// that reaches a header matching `target`
function includeChain(file, target) {
  const seen = new Set([file]);
  const queue = [[file]];
  while (queue.length) {
    const chain = queue.shift();
    const cur = chain[chain.length - 1];
    for (const m of readFileSync(cur, 'utf8').matchAll(/^#include\s+"([^"]+)"/gm)) {
      const next = join(dirname(cur), m[1]);
      if (seen.has(next) || !existsSync(next)) continue;
      seen.add(next);
      const c = [...chain, next];
      if (target.test(next)) return c;
      queue.push(c);
    }
  }
  return null;
}

const found = [];
for (const r of RULES) {
  for (const f of r.files()) {
    const rel = relative(root, f);
    if (r.closure) {
      const chain = includeChain(f, r.closure);
      if (chain) found.push({ rule: r.id, file: rel, line: 1, text: `sees ${chain.slice(1).map((p) => relative(join(root, 'engine/src'), p)).join(' → ')}` });
      continue;
    }
    readFileSync(f, 'utf8').split('\n').forEach((line, i) => {
      if (r.re.test(line)) found.push({ rule: r.id, file: rel, line: i + 1, text: line.trim() });
    });
  }
}
const key = (v) => `${v.rule} ${v.file} ${v.text}`;

if (process.argv.includes('--update-baseline')) {
  const counts = {};
  for (const v of found) counts[key(v)] = (counts[key(v)] ?? 0) + 1;
  writeFileSync(baselinePath, JSON.stringify(counts, null, 1) + '\n');
  console.log(`lint-arch baseline: ${found.length} known violations`);
  process.exit(0);
}

const base = existsSync(baselinePath) ? JSON.parse(readFileSync(baselinePath, 'utf8')) : {};
const left = { ...base };
let bad = 0;
for (const v of found) {
  const k = key(v);
  if (left[k] > 0) { left[k]--; continue; }
  const why = RULES.find((r) => r.id === v.rule).why;
  console.log(`NEW ${v.rule} ${v.file}:${v.line}: ${v.text}\n    ${why}`);
  bad++;
}
for (const [k, n] of Object.entries(left)) {
  if (n > 0) {
    console.log(`STALE baseline entry (fixed? remove it): ${k}`);
    bad++;
  }
}
const known = Object.values(base).reduce((a, b) => a + b, 0);
console.log(`lint-arch: ${found.length} violations (${known} baselined), ${bad} problems`);
process.exit(bad ? 1 : 0);
