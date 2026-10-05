#!/usr/bin/env node
// Real-world corpus review (docs/remediation/PLAN.md §4.3 gate G6).
//
//   node tools/review-corpus.mjs [--label NAME]      render every document to
//                                                     .review/NAME/
//   node tools/review-corpus.mjs --check              fail on error diagnostics
//                                                     not in test/review/baseline.json
//   node tools/review-corpus.mjs --update-baseline    record today's error diagnostics
//   node tools/review-corpus.mjs --diff A B           summarize html/diag changes
//                                                     between two rendered labels
//
// Corpus: examples/real-world/**/*.tsm and, when present, the blog's
// ../zball-io/src/**/*.tsm (read-only). Output is the semantic page
// (renderTsm); it is a review aid, not a golden.
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { renderTsm } from '../runtime/src/node/render.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const has = (f) => args.includes(f);
const opt = (name, dflt) => {
  const i = args.indexOf('--' + name);
  return i >= 0 ? args[i + 1] : dflt;
};
const baselinePath = join(root, 'test/review/baseline.json');

function* walk(dir) {
  if (!existsSync(dir)) return;
  for (const e of readdirSync(dir).sort()) {
    const p = join(dir, e);
    if (e === 'node_modules' || e === '_site') continue;
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.tsm')) yield p;
  }
}

function corpus() {
  const docs = [];
  for (const p of walk(join(root, 'examples/real-world'))) docs.push({ path: p, key: relative(root, p) });
  const blog = join(root, '../zball-io/src');
  for (const p of walk(blog)) docs.push({ path: p, key: 'zball-io/' + relative(blog, p) });
  return docs;
}

// "<severity> <code> @[s,e) message" → "<severity> <code> message" (spans move with edits)
const norm = (line) => line.replace(/ @\[\d+,\d+\)/, '');
const errorsOf = (diags) => diags.split('\n').filter((l) => l.startsWith('error ')).map(norm);

async function renderAll() {
  const out = {};
  for (const d of corpus()) {
    const src = readFileSync(d.path, 'utf8');
    let r;
    try {
      r = await renderTsm(src, { baseDir: dirname(d.path), rootDir: root });
    } catch (e) {
      r = { html: '', diags: `error render-crash ${String(e && e.message || e).split('\n')[0]}`, ok: false };
    }
    out[d.key] = r;
  }
  return out;
}

function diffLabels(a, b) {
  const da = join(root, '.review', a);
  const db = join(root, '.review', b);
  let changed = 0, same = 0, missing = 0, lines = 0;
  const report = [];
  for (const p of walkFiles(da)) {
    const rel = relative(da, p);
    const q = join(db, rel);
    if (!existsSync(q)) { missing++; continue; }
    const x = readFileSync(p, 'utf8').split('\n');
    const y = readFileSync(q, 'utf8').split('\n');
    if (x.join('\n') === y.join('\n')) { same++; continue; }
    changed++;
    const set = new Set(x);
    const n = y.filter((l) => !set.has(l)).length;
    lines += n;
    report.push(`${String(n).padStart(5)} changed lines  ${rel}`);
  }
  report.sort((u, v) => parseInt(v) - parseInt(u));
  console.log(report.slice(0, 40).join('\n'));
  console.log(`diff ${a} → ${b}: ${changed} changed, ${same} identical, ${missing} missing, ${lines} changed lines`);
}

function* walkFiles(dir) {
  for (const e of readdirSync(dir).sort()) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walkFiles(p);
    else yield p;
  }
}

if (has('--diff')) {
  const i = args.indexOf('--diff');
  diffLabels(args[i + 1], args[i + 2]);
  process.exit(0);
}

const results = await renderAll();
const label = opt('label', has('--check') ? null : 'current');
if (label) {
  const dir = join(root, '.review', label);
  for (const [key, r] of Object.entries(results)) {
    const f = join(dir, key.replace(/\.tsm$/, ''));
    mkdirSync(dirname(f), { recursive: true });
    writeFileSync(f + '.html', r.html);
    writeFileSync(f + '.diags', r.diags);
  }
}

const now = Object.fromEntries(Object.entries(results).map(([k, r]) => [k, errorsOf(r.diags)]));
if (has('--update-baseline')) {
  mkdirSync(dirname(baselinePath), { recursive: true });
  const kept = Object.fromEntries(Object.entries(now).filter(([, v]) => v.length));
  writeFileSync(baselinePath, JSON.stringify(kept, null, 1) + '\n');
  console.log(`baseline: ${Object.keys(results).length} documents, ${Object.keys(kept).length} with errors`);
} else if (has('--check')) {
  const base = existsSync(baselinePath) ? JSON.parse(readFileSync(baselinePath, 'utf8')) : {};
  let bad = 0;
  for (const [k, errs] of Object.entries(now)) {
    const allowed = [...(base[k] ?? [])];
    for (const e of errs) {
      const i = allowed.indexOf(e);
      if (i >= 0) { allowed.splice(i, 1); continue; }
      console.log(`NEW ERROR ${k}: ${e}`);
      bad++;
    }
  }
  console.log(`review corpus: ${Object.keys(now).length} documents, ${bad} new error diagnostics`);
  process.exit(bad ? 1 : 0);
} else {
  console.log(`rendered ${Object.keys(results).length} documents to .review/${label}`);
}
