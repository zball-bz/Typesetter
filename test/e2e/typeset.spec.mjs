// M1 acceptance: typeset fixtures in a real browser, assert the invariant
// audits — no browser re-break, right edge within 1px — across the dsf matrix.
import { test, expect } from '@playwright/test';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, dirname, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const fixturesDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'fixtures');
function* walk(dir) {
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.tsm')) yield p;
  }
}
const fixtures = [...walk(fixturesDir)].map((p) => ({
  name: relative(fixturesDir, p).replace(/\.tsm$/, ''),
  source: readFileSync(p, 'utf8'),
}));

for (const f of fixtures) {
  test(`audit ${f.name}`, async ({ page }) => {
    await page.goto('/test/e2e/harness.html');
    await page.waitForFunction(() => window.__tsrReady);
    // convention (mirrors the native runner): *indent* fixtures run with the
    // CJK 2em first-line indent
    const opts = {
      widthPx: 300,
      paraIndentEm: f.name.includes('indent') ? 2 : 0,
      punctCompress: f.name.includes('punct-full') ? 'full'
        : f.name.includes('punct-none') ? 'none' : 'book',
    };
    const res = await page.evaluate(
      async ({ source, opts }) => await window.__tsr.typeset(source, opts),
      { source: f.source, opts },
    );
    // *diag* fixtures exist to golden-test diagnostics (e.g. unresolved refs)
    if (!f.name.includes('diag')) expect(res.diags).toBe('');
    const report = await page.evaluate(() => window.__tsr.audit());
    expect(report.lines).toBeGreaterThan(0);
    expect(report.failures).toEqual([]);
  });
}

test('wide measure long doc', async ({ page }) => {
  const source = fixtures.find((f) => f.name === 'doc/english').source;
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 580 }),
    { source },
  );
  const report = await page.evaluate(() => window.__tsr.audit());
  expect(report.failures).toEqual([]);
});

// --- M5: progressive upgrade, relayout, copy contract (§9.2/§9.3) ---------

test('progressive semantic phase, upgrade records, relayout', async ({ page }) => {
  const source = fixtures.find((f) => f.name === 'doc/refs').source;
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 300 }),
    { source },
  );
  expect(res.hasSemantic).toBe(true);
  expect(res.upgrades).toBeGreaterThan(0);
  // width-only relayout keeps every audit invariant
  await page.evaluate(() => window.__tsr.relayout(500));
  const report = await page.evaluate(() => window.__tsr.audit());
  expect(report.failures).toEqual([]);
  expect(report.lines).toBeGreaterThan(0);
});

test('copy rebuilds exact content text (Latin, hyphenated)', async ({ page }) => {
  // single paragraph with single spaces: the §9.3 rebuild must reproduce the
  // source exactly — hyphen glyphs skipped, line breaks rejoined per join
  const source =
    'The measurement contract keeps every rendered line inside its measure ' +
    'while typography survives copying and hyphenation disappears again.';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 220 }),
    { source },
  );
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toBe(source);
});

test('copy joins CJK line breaks seamlessly and skips resolved refs', async ({ page }) => {
  const cjk = '排版引擎在断行处不引入空格，标点挤压后的文本也保持原样，复制即内容。';
  const source = '= 引言 <s>\n\n' + cjk + '\n\n见 @s 一节。';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 160 }),
    { source },
  );
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toContain(cjk);          // rejoined with no inserted characters
  expect(text).not.toContain('§');      // resolved ref runs are synthetic
  expect(text).toContain('见');
});
