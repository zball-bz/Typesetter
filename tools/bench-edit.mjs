#!/usr/bin/env node
// Editing-latency bench (editor-design.md §2): synthesizes a realistic long
// document, then simulates an editing session — mutate one paragraph, re-
// typeset, repeat — and reports cold/warm latency plus the worker's phase
// breakdown when available.
//
//   node tools/bench-edit.mjs [--edits 24] [--mode typeset|update]
//
// mode typeset = the pre-incremental path (fresh doc per edit);
// mode update  = handle.update() (session doc, warm caches).
import { spawn } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from '@playwright/test';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (name, dflt) => {
  const i = args.indexOf('--' + name);
  return i >= 0 ? args[i + 1] : dflt;
};
const EDITS = Number(opt('edits', 24));
const SECTIONS = Number(opt('sections', 18));
const MODE = opt('mode', 'typeset');
const PORT = 8177;

// ---- synthetic document: CJK + Latin paragraphs, code, math, a table ----
function makeDoc() {
  const zh = '排版引擎的目标是让网页上的长文获得与纸面书籍相当的阅读质量。' +
    '行的松紧应当均匀，标点应当悬挂与压缩得体，中西文之间应当有恰当的间隙。' +
    '这些细节单独看都微不足道，合在一起却决定了一页文字是否耐读。';
  const en = 'The typesetting engine pursues paragraph-level quality: line ' +
    'tension should be even across the paragraph, hyphenation should be a ' +
    'last resort rather than a habit, and the reader should never notice ' +
    'the machinery that makes justification exact.';
  const code = [
    '```js',
    'function fib(n) {',
    '  if (n < 2) return n;      // base case',
    '  let a = 0, b = 1;',
    '  for (let i = 2; i <= n; i++) [a, b] = [b, a + b];',
    '  return b;',
    '}',
    '```',
  ].join('\n');
  const math = '设 $f(x) = \\sum_{k=0}^n a_k x^k$，则其导数为 ' +
    '$f\'(x) = \\sum_{k=1}^n k a_k x^{k-1}$，逐项求导即可。';
  const parts = ['# 编辑延迟基准文档', ''];
  for (let i = 0; i < SECTIONS; i++) {
    parts.push(`## 小节 ${i + 1}`, '');
    parts.push(zh + `（第 ${i + 1} 节）`, '');
    parts.push(en, '');
    if (i % 3 === 0) parts.push(code, '');
    if (i % 4 === 1) parts.push(math, '');
  }
  return parts.join('\n');
}

const server = spawn(process.execPath, [join(root, 'tools/serve.mjs'), String(PORT)],
                     { stdio: 'ignore' });
try {
  await new Promise((r) => setTimeout(r, 400));
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(`http://localhost:${PORT}/test/e2e/harness.html`);
  const doc = makeDoc();
  console.log(`doc: ${doc.length} chars, mode: ${MODE}, edits: ${EDITS}`);

  const res = await page.evaluate(async ({ doc, edits, mode, sections }) => {
    const t0 = performance.now();
    const first = await window.__tsr.typeset(doc, { widthPx: 680, progressive: false });
    const cold = performance.now() - t0;
    const times = [];
    const timings = [];
    for (let i = 0; i < edits; i++) {
      // mutate one paragraph mid-document: the minimal realistic keystroke
      const edited = doc.replace(`（第 ${1 + (i % sections)} 节）`, `（第 ${1 + (i % sections)} 节，改${i}）`);
      const t = performance.now();
      const r = mode === 'update'
        ? await window.__tsr.update(edited)
        : await window.__tsr.typeset(edited, { widthPx: 680, progressive: false });
      times.push(performance.now() - t);
      if (r && r.timings) timings.push(r.timings);
    }
    return { cold, times, timings, diags: first.diags };
  }, { doc, edits: EDITS, mode: MODE, sections: SECTIONS });

  const sorted = [...res.times].sort((a, b) => a - b);
  const pick = (q) => sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))];
  console.log(`cold first typeset: ${res.cold.toFixed(1)} ms`);
  console.log(`edit latency: median ${pick(0.5).toFixed(1)} ms, ` +
              `p90 ${pick(0.9).toFixed(1)} ms, max ${sorted[sorted.length - 1].toFixed(1)} ms`);
  if (res.timings.length) {
    const keys = Object.keys(res.timings[0]);
    const med = (k) => {
      const v = res.timings.map((t) => t[k]).sort((a, b) => a - b);
      return v[Math.floor(v.length / 2)];
    };
    console.log('worker phase medians: ' +
      keys.map((k) => `${k} ${med(k).toFixed(1)}ms`).join(', '));
  }
  if (res.diags.trim()) console.log('diags:', res.diags.trim());
  await browser.close();
} finally {
  server.kill();
}
