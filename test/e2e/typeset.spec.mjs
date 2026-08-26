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

test('math copies as source text', async ({ page }) => {
  const source = '面积为 $pi r^2$ 的圆，其周长为 $2 pi r$。';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 280 }),
    { source },
  );
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toContain('$pi r^2$');
  expect(text).toContain('$2 pi r$');
  expect(text).toContain('面积为');
});

test('code font features configurable per language', async ({ page }) => {
  const source = '```js\nconst a = 1;\n```\n\n```json\n{"k": 1}\n```';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, {
    widthPx: 400,
    codeFontFeatures: '"calt" 0',
    codeFontFeaturesByLang: { js: '"liga" 1, "ss01" 1' },
  }), { source });
  const feats = await page.evaluate(() =>
    [...document.querySelectorAll('.tsr-line')]
      .map((l) => l.style.fontFeatureSettings).filter(Boolean));
  // browsers normalize the serialized value ("liga" 1 → "liga")
  expect(feats).toContain('"liga", "ss01"');  // js override
  expect(feats).toContain('"calt" 0');        // json falls to default
});

test('wrapped code copies as its logical lines', async ({ page }) => {
  const line = 'const aVeryLongIdentifierName = anotherLongIdentifier + someMoreLength;';
  const source = '```js\n' + line + '\nshort();\n```';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 220 }),
    { source },
  );
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toContain(line);        // rejoined across grid-wrap rows
  expect(text).toContain('short();');
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

test('block figure: pulled dims, centred image, caption prefix, copy skips', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async () => {
    const cv = document.createElement('canvas');
    cv.width = 64; cv.height = 48;
    cv.getContext('2d').fillStyle = '#c00';
    cv.getContext('2d').fillRect(0, 0, 64, 48);
    const uri = cv.toDataURL('image/png');
    const source = '#!figure(src: "' + uri + '", alt: "示例", label: "f1")\n' +
      '一张红色示例图。\n#figure!\n\n见@f1。';
    return await window.__tsr.typeset(source, { widthPx: 300 });
  });
  expect(res.diags).toBe('');
  const img = page.locator('.tsr-img');
  await expect(img).toHaveCount(1);
  const box = await img.boundingBox();
  expect(box.width).toBeCloseTo(64, 0);   // natural size, under the measure
  expect(box.height).toBeCloseTo(48, 0);
  // centred: left inset ≈ (300 - 64) / 2 within the container
  const holder = await page.locator('#out').boundingBox();
  expect(box.x - holder.x).toBeCloseTo((300 - 64) / 2, 0);
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toContain('图 1：一张红色示例图。');
  expect(text).toContain('见');
  expect(text).not.toContain('data:image'); // the image itself never copies
  // the ref resolved and links to the figure anchor
  await expect(page.locator('a[href="#tsr-f1"]').first()).toBeVisible();
});

test('figure: scale option and placeholder on refused scheme', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async () => {
    const source =
      '#!figure(src: "x.png", alt: "半宽", w: 400, h: 100, scale: 0.5)\n半宽图。\n#figure!\n\n' +
      '#!figure(src: "javascript:alert(1)", alt: "拒绝")\n占位。\n#figure!';
    return await window.__tsr.typeset(source, { widthPx: 320 });
  });
  expect(res.diags).toContain('image-src');       // refused scheme warned
  expect(res.diags).not.toContain('error');
  const img = page.locator('.tsr-img');
  await expect(img).toHaveCount(1);               // declared dims: no fetch
  const box = await img.boundingBox();
  expect(box.width).toBeCloseTo(160, 0);          // scale 0.5 × 320
  expect(box.height).toBeCloseTo(40, 0);          // aspect 4:1 preserved
  const ph = page.locator('.tsr-imgph');
  await expect(ph).toHaveCount(1);
  await expect(ph).toHaveText('拒绝');
  const html = await page.content();
  expect(html).not.toContain('javascript:alert'); // refused src never renders
});

test('float figure: narrowed wrap lines, edge placement, recovery', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async () => {
    const source =
      '#!figure(src: "x.png", alt: "浮", w: 400, h: 300, scale: 0.4, float: "right")\n' +
      '右浮动图。\n#figure!\n\n' +
      '正文围绕浮动图排布，前几行的断行宽度收窄为版心减去浮动框与间隙，' +
      '越过浮动框底部之后恢复整幅版心宽度继续排布，环绕自然结束，' +
      '此段足够长以同时覆盖收窄与恢复两种状态，从而一次验证两侧。';
    return await window.__tsr.typeset(source, { widthPx: 300 });
  });
  expect(res.diags).toBe('');
  const img = page.locator('.tsr-img');
  const ibox = await img.boundingBox();
  const holder = await page.locator('#out').boundingBox();
  expect(ibox.width).toBeCloseTo(120, 0);            // scale 0.4 × 300
  expect(ibox.x + ibox.width - holder.x).toBeCloseTo(300, 0);  // right edge
  const widths = await page.evaluate(() =>
    [...document.querySelectorAll('.tsr-line')]
      .filter((l) => !l.querySelector('[data-syn="marker"]'))
      .map((l) => parseFloat(l.style.width)));
  const body = widths.slice(1); // drop the figure-caption-free first para? none: caption rows are data-cell
  expect(Math.min(...body)).toBeLessThan(200);        // narrowed lines exist
  expect(Math.max(...body)).toBeCloseTo(300, 0);      // and recovery to full
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toContain('图 1：右浮动图。');          // caption still copies
});
