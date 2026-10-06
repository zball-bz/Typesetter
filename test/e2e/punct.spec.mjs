// The punctuation matrix (plan P4-04, its prerequisite; design T5 step 7):
// every class of full-width punctuation and their combinations — opening and
// closing brackets, stops and commas, dashes, ellipses, quotes, Latin beside
// them — in the three compression modes, justified (every line but a
// paragraph's last) and ragged (its last). At each device pixel ratio (the
// playwright projects), every run's rendered width — its box and its
// margins — equals the width the engine set it at (data-w,
// render.runWidths) within 1px.
//
// PUNCT_RECORD=1 writes the per-case deviations to punct-baseline.json (the
// baseline recorded before P4-04 changed the realization).
import { test, expect } from '@playwright/test';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';

const CASES = [
  ['stops', '我们说，这是一句话。那么、然后；接着：最后！真的？'],
  ['brackets', '前文（括号）后文「引号」再文《书名》又文【方括】末尾〔龟甲〕完。'],
  ['close-open', '甲）（乙」「丙》《丁】【戊〕〔己。'],
  ['open-open', '甲（「乙」）丙《【丁】》戊。'],
  ['close-close', '甲。」乙）。丙」）丁！」戊？）完。'],
  ['stop-close', '甲，」乙；）丙：」完。'],
  ['dash', '甲——乙—丙——丁。'],
  ['ellipsis', '甲……乙…丙……丁。'],
  ['quotes', '他说“你好”，又说‘再见’。“引号”开头，结尾“引号”。'],
  ['latin', '中文（English）中文“quoted”中文：OK，结束。'],
  // a run in its own size: its blanks are its em, not the paragraph's
  ['sized', '前文#style({sizePx: 22})[大字，（括号）「引号」。]后文。'],
];
const MODES = ['book', 'full', 'none'];
const BASELINE = new URL('./punct-baseline.json', import.meta.url);

// one paragraph per case and mode, the case repeated over several lines
const source = CASES.flatMap(([, text]) =>
  MODES.map((m) => `#style({text: {punct: '${m}'}})[${text.repeat(5)}]`)).join('\n\n');

test('punctuation matrix: rendered runs keep the widths the engine set', async ({ page }, info) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, {
    widthPx: 300, settings: { render: { runWidths: true } },
  }), { source });
  // per paragraph (case × mode): the largest |rendered − set| of its runs
  const devs = await page.evaluate(() => [...document.querySelectorAll('#out .tsr-para')].map((p) => {
    let max = 0, n = 0, worst = '';
    for (const r of p.querySelectorAll('[data-w]')) {
      const cs = getComputedStyle(r);
      const outer = r.getBoundingClientRect().width + (parseFloat(cs.marginLeft) || 0) + (parseFloat(cs.marginRight) || 0);
      const dev = Math.abs(outer - parseFloat(r.dataset.w));
      n++;
      if (dev > max) {
        max = dev;
        worst = r.textContent;
      }
    }
    return { max: Math.round(max * 1000) / 1000, n, worst };
  }));
  expect(devs.length).toBe(CASES.length * MODES.length);
  const report = {};
  CASES.forEach(([name], c) => MODES.forEach((m, k) => {
    const d = devs[c * MODES.length + k];
    expect(d.n, `${name}/${m}: runs`).toBeGreaterThan(5);
    report[`${name}/${m}`] = d.max;
  }));
  const dsf = info.project.name;
  if (process.env.PUNCT_RECORD) {
    const all = existsSync(BASELINE) ? JSON.parse(readFileSync(BASELINE, 'utf8')) : {};
    all[dsf] = report;
    writeFileSync(BASELINE, JSON.stringify(all, null, 2) + '\n');
    return;
  }
  for (const [k, max] of Object.entries(report)) expect(max, `${k} at ${dsf}`).toBeLessThanOrEqual(1);
});
