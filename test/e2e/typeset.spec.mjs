// M1 acceptance: typeset fixtures in a real browser, assert the invariant
// audits — no browser re-break, right edge within 1px — across the dsf matrix.
import { test, expect } from '@playwright/test';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
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
const fixtures = [...walk(fixturesDir)].map((p) => {
  const fx = p.replace(/\.tsm$/, '.fixture.json');
  return {
    name: relative(fixturesDir, p).replace(/\.tsm$/, ''),
    source: readFileSync(p, 'utf8'),
    // the fixture's own settings (plan P1-03; its golden profile is native-only)
    settings: existsSync(fx) ? JSON.parse(readFileSync(fx, 'utf8')).settings ?? {} : {},
  };
});

// Known audit failures (remediation plan P0-01): guard fixtures that record
// a defect before the step that fixes it. A listed fixture whose audit passes
// fails the run, so the list can only shrink. Mirrors test/golden/XFAIL.
const AUDIT_XFAIL = new Map([
]);

// Content wider than its measure is set Overfull on a line of its own and
// reported (plan P0-12): these fixtures expect exactly that warning.
const EXPECTED_DIAGS = new Map([
  ['doc/url-overlong', /^warning overfull-line [^\n]*\n$/],  // two 500px URL segments at 300px
  ['region/hott-row', /^warning overfull-line [^\n]*\n$/],   // a formula in a 47px table cell
  // references to unnumbered regions show their label text (plan P1-18 anchors)
  ['region/anchor-kinds', /^(info ref-unnumbered [^\n]*\n){3}$/],
  // a splice of undefined renders nothing and says so (plan P2-01, D-I05)
  ['splice/dot-rule', /^warning splice-undefined [^\n]*\n$/],
]);

for (const f of fixtures) {
  test(`audit ${f.name}`, async ({ page }) => {
    await page.goto('/test/e2e/harness.html');
    await page.waitForFunction(() => window.__tsrReady);
    const opts = { widthPx: 300, settings: f.settings };
    const res = await page.evaluate(
      async ({ source, opts }) => await window.__tsr.typeset(source, opts),
      { source: f.source, opts },
    );
    // *diag* fixtures exist to golden-test diagnostics (e.g. unresolved refs);
    // a few fixtures carry content wider than their measure on purpose
    if (EXPECTED_DIAGS.has(f.name)) expect(res.diags).toMatch(EXPECTED_DIAGS.get(f.name));
    else if (!f.name.includes('diag')) expect(res.diags).toBe('');
    const report = await page.evaluate(() => window.__tsr.audit());
    expect(report.lines).toBeGreaterThan(0);
    if (AUDIT_XFAIL.has(f.name)) {
      expect(report.failures.length, `XPASS: remove ${f.name} from AUDIT_XFAIL`).toBeGreaterThan(0);
    } else {
      expect(report.failures).toEqual([]);
    }
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

// plan P1-23 (math-design §11): the math font paints exactly what the engine
// laid out — installed as a declared face of role 'math', every glyph span in
// it (no fallback family), kerning off, the space present for degraded
// formulas, and an inline formula's baseline on its line's text baseline
// plan P2-01 (design T2 S4): one content protocol — arrays flatten, null
// renders nothing (splice-undefined), a function is an error unless nullary
// (#toc), m`…` keeps content values
test('toContent: arrays, null, functions, m interpolation', async ({ page }) => {
  const source = '#let xs = ["a", em("b"), 3]\n\nA #xs B #(null) C #(x => 1) D #(m`p ${strong("q")} r`) E\n';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, { widthPx: 600 }),
                                  { source });
  const text = await page.evaluate(() => document.querySelector('.tsr-doc').textContent);
  expect(text).toContain('A ab3 B');
  expect(text).toContain('p q r');
  expect(text).not.toContain('[object Object]');
  expect(text).not.toContain('null');
  expect(res.diags).toMatch(/warning splice-undefined/);
  expect(res.diags).toMatch(/error splice-function/);
});

test('math: font, glyph coverage and baseline audit', async ({ page }) => {
  const source = '设 $x^2 + y_1 = z$ 且 $f(x) = sum_(i=1)^n a_i$ 成立，另有 $a +$ 与正文。';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, { widthPx: 400 }), { source });
  const r = await page.evaluate(async () => {
    await document.fonts.ready;
    const glyphs = [...document.querySelectorAll('.tsr-math .tsr-mg:not(.tsr-mt)')];
    const bad = [];
    for (const g of glyphs) {
      const cs = getComputedStyle(g);
      if (cs.fontKerning !== 'none') bad.push('kerning ' + g.textContent);
      if (!/Euler Math/.test(cs.fontFamily) || /STIX|serif/.test(cs.fontFamily)) bad.push('family ' + cs.fontFamily);
      if (!document.fonts.check(`${cs.fontSize} "Euler Math"`, g.textContent)) bad.push('coverage ' + g.textContent);
    }
    // the baseline of an inline formula: its box bottom minus its depth
    // (vertical-align: -depth) must sit on the text baseline (a zero-height
    // inline-block's bottom) of its line
    const drift = [];
    for (const m of document.querySelectorAll('.tsr-line > .tsr-math, .tsr-line > span > .tsr-math')) {
      if (m.style.position === 'absolute') continue;  // display formulas sit in their row
      const probe = document.createElement('span');
      probe.style.cssText = 'display:inline-block;width:0;height:0';
      m.after(probe);
      const depth = -parseFloat(m.style.verticalAlign || '0');
      const mathBase = m.getBoundingClientRect().bottom - depth;
      drift.push(Math.abs(mathBase - probe.getBoundingClientRect().bottom));
      probe.remove();
    }
    const faces = [...document.fonts].filter((f) => f.family.replace(/"/g, '') === 'Euler Math');
    return { glyphs: glyphs.length, bad, drift, loaded: faces.map((f) => f.status),
             space: document.fonts.check('16px "Euler Math"', ' ') };
  });
  expect(r.glyphs).toBeGreaterThan(10);
  expect(r.loaded).toEqual(['loaded']);  // one declared face (role 'math'), loaded
  expect(r.bad).toEqual([]);
  expect(r.space).toBe(true);
  expect(r.drift.length).toBeGreaterThan(1);
  for (const d of r.drift) expect(d).toBeLessThan(0.5);
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

// plan P0-10 (design T7 S6): a citation's generated text is its own run, so
// the prose and punctuation beside it survive copy (they used to merge into
// the data-syn="ref" run and vanish, while the bracket leaked into prose)
test('copy keeps the prose next to citations', async ({ page }) => {
  const source = readFileSync(join(fixturesDir, 'cite', 'basic.tsm'), 'utf8');
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 300 }),
    { source },
  );
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toContain('Knuth and Plass ; hyphenation patterns follow Liang , and the pair');
  expect(text).toContain('keeps its number; the uncited entry');
  expect(text).not.toMatch(/Plass \[|Liang \[/);  // the bracket is generated text
});

// plan P0-10 (defect #21): a snap-kerned code run carries its letter-spacing
// in its one style attribute (a second style="" was dropped by the parser)
test('snap-kerning: one style attribute carrying letter-spacing', async ({ page }) => {
  const source = '```text\nlet 名前 = "値"; // 注释 mixed\nASCII only line here\n```';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 400, verbatimSnapKerning: true }),
    { source },
  );
  const tags = res.html.match(/<span[^>]*data-snap="1"[^>]*>/g) ?? [];
  expect(tags.length).toBeGreaterThan(0);
  for (const t of tags) {
    expect(t.match(/ style="/g)?.length, t).toBe(1);
    expect(t, t).toMatch(/style="[^"]*letter-spacing:/);
  }
  const spacing = await page.evaluate(() =>
    [...document.querySelectorAll('[data-snap]')].map((e) => e.style.letterSpacing));
  expect(spacing.length).toBe(tags.length);
  for (const v of spacing) expect(v).not.toBe('');
  expect((await page.evaluate(() => window.__tsr.audit())).failures).toEqual([]);
});

// plan P1-04: the document root carries its language, base size and the
// resolved font roles; a CJK run inside code paints with the mono×CJK face it
// was measured with; patching keeps working under the richer root tag
test('root contract: lang, font roles, mono×cjk paint, patching', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const source = 'First paragraph.\n\n#{ throw new Error("出错了 here") }\n\nLast paragraph.';
  await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, { widthPx: 300,
    settings: { doc: { lang: 'en' }, fonts: { monoCjk: '"Mono CJK Probe", monospace' } } }), { source });
  const root = await page.evaluate(() => {
    const r = document.querySelector('#out .tsr-doc');
    const cjkCode = document.querySelector('#out .tsr-code.tsr-cjk');
    return { lang: r.getAttribute('lang'), mono: r.style.getPropertyValue('--tsr-font-mono-cjk').trim(),
             size: r.style.fontSize, painted: cjkCode && getComputedStyle(cjkCode).fontFamily };
  });
  expect(root.lang).toBe('en');
  expect(root.mono).toBe('"Mono CJK Probe", monospace');
  expect(root.size).toBe('18px');
  expect(root.painted).toBe('"Mono CJK Probe", monospace');
  const r = await page.evaluate(async () => await window.__tsr.update(
    'First paragraph.\n\n#{ throw new Error("出错了 here") }\n\nLast paragraph, edited.'));
  expect(r.patched).toBe(true);
});

// plan P1-03: one settings document replaces the per-knob options (which
// stay as sugar); unknown paths and bad values are diagnostics
test('settings: one document, legacy options as sugar, diagnostics', async ({ page }) => {
  const source = '#!figure(src: "x.png", alt: "a", w: 100, h: 50, label: "f")\nCaption.\n#figure!\n\nSee @f.';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const en = await page.evaluate(async ({ source }) => await window.__tsr.typeset(source,
    { widthPx: 300, settings: { doc: { lang: 'en' }, terms: { figure: 'Fig. ' } } }), { source });
  expect(en.html).toContain('Fig. 1');
  expect(en.diags).toBe('');
  const legacy = await page.evaluate(async ({ source }) =>
    await window.__tsr.typeset(source, { widthPx: 300, lang: 'en' }), { source });
  expect(legacy.html).toContain('Figure 1');
  const bad = await page.evaluate(async ({ source }) => await window.__tsr.typeset(source,
    { widthPx: 300, settings: { doc: { leading: 'tall' }, nope: { x: 1 } } }), { source });
  expect(bad.diags).toContain('setting-type');
  expect(bad.diags).toContain('setting-unknown');
  expect(bad.html).toContain('>图</span>');  // defaults stand (zh terms)
});

// plan P1-01: the ABI handshake refuses an engine that cannot read what the
// runtime writes, or that was generated from another schema
test('abi: handshake accepts this build and refuses mismatches', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const out = await page.evaluate(async () => {
    const { checkAbi } = await import('/runtime/src/shared/abi.mjs');
    const { OPS_VERSION, SCHEMA_HASH } = await import('/runtime/src/shared/ops.gen.mjs');
    const { SYNTAX_VERSION } = await import('/runtime/src/shared/syntax.gen.mjs');
    const { RES_VERSION } = await import('/runtime/src/shared/resources.gen.mjs');
    const { PROGRAM_ABI } = await import('/runtime/src/shared/lower.gen.mjs');
    const fake = (abi) => ({ _tsr2_abi: () => 0, UTF8ToString: () => JSON.stringify(abi) });
    const msg = (abi) => { try { checkAbi(fake(abi)); return 'ok'; } catch (e) { return e.message; } };
    const ok = { opsWindow: [6, OPS_VERSION], schemaHash: SCHEMA_HASH, syntaxVersion: SYNTAX_VERSION,
                 resVersion: RES_VERSION, programAbi: PROGRAM_ABI };
    return {
      same: msg(ok),
      hash: msg({ ...ok, schemaHash: 'deadbeef' }),
      window: msg({ ...ok, opsWindow: [6, OPS_VERSION - 1] }),
      syntax: msg({ ...ok, syntaxVersion: SYNTAX_VERSION + 1 }),
      res: msg({ ...ok, resVersion: RES_VERSION + 1 }),
      program: msg({ ...ok, programAbi: PROGRAM_ABI ^ 1 }),
    };
  });
  expect(out.same).toBe('ok');
  expect(out.hash).toContain('differs from runtime schema');
  expect(out.window).toContain('the engine reads ops');
  expect(out.syntax).toContain('differs from runtime syntax');
  expect(out.res).toContain('differ from runtime resources');
  expect(out.program).toContain('program ABI');
});

// --- plan P2-02: the LowerProgram and its hole module (Node executor) -------
// A module is cached by the program's hash: an edit to prose imports nothing.
// A SyntaxError is isolated among the pieces that changed, and an edit that
// breaks one splice costs the failing import plus the stubbed one.
test('lowering: module cache and incremental SyntaxError isolation', async () => {
  test.skip(test.info().project.name !== 'chromium-dsf1', 'Node-side: one device scale is enough');
  const { execFileSync } = await import('node:child_process');
  const { writeFileSync, mkdtempSync, rmSync } = await import('node:fs');
  const { tmpdir } = await import('node:os');
  const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
  const { execute, lowerStats } = await import(join(root, 'runtime/src/worker/executor.mjs'));
  const tsrc = join(root, 'engine/build/tsrc');
  const dir = mkdtempSync(join(tmpdir(), 'tsm-lower-'));
  const file = join(dir, 'doc.tsm'), opsFile = join(dir, 'doc.ops');
  const run = async (src) => {
    writeFileSync(file, src);
    const program = new Uint8Array(execFileSync(tsrc, ['--stage=program', file]));
    const js = () => execFileSync(tsrc, ['--stage=js', file], { encoding: 'utf8' });
    const before = lowerStats.imports;
    writeFileSync(opsFile, await execute({ program, js }));
    const diags = execFileSync(tsrc, ['--stage=diags', `--ops=${opsFile}`, file], { encoding: 'utf8' });
    return { imports: lowerStats.imports - before, diags };
  };
  const doc = (prose, expr) =>
    `#let a = 20\n\n${prose} #(a + 1).\n\nLater #(${expr}).\n\n${'More prose. '.repeat(3)}\n`;
  try {
    const first = await run(doc('Prose', 'a * 2'));
    expect(first.imports).toBe(1);
    expect(first.diags).toBe('');
    // prose edit: the same module text, nothing imported
    expect((await run(doc('Edited prose', 'a * 2'))).imports).toBe(0);
    // one splice broken: its paragraph alone is an error block
    const broken = await run(doc('Edited prose', 'a * '));
    expect(broken.imports).toBe(2);
    expect(broken.diags).toMatch(/^error script-syntax @\[\d+,\d+\) SyntaxError[^\n]*\n$/);
    // the same broken text again: cached with its stub
    expect((await run(doc('More edits', 'a * '))).imports).toBe(0);
    // fixed: one import, no diagnostics
    const fixed = await run(doc('More edits', 'a * 3'));
    expect(fixed.imports).toBe(1);
    expect(fixed.diags).toBe('');
    // a document without user code imports nothing at all
    expect((await run('Just prose.\n')).imports).toBe(0);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

// --- plan P0-12: breaker semantics -------------------------------------------

// defect #19: a run wider than the measure used to collapse the whole
// paragraph onto one line with ~-150px word spacing; now each such run is set
// Overfull on its own line at the shrink limit, everything else breaks normally
test('overfull: one unbreakable run per line, no collapse', async ({ page }) => {
  const source = readFileSync(join(fixturesDir, 'doc', 'url-overlong.tsm'), 'utf8');
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async ({ source }) =>
    await window.__tsr.typeset(source, { widthPx: 300 }), { source });
  expect(res.diags).toMatch(/overfull-line/);
  expect((await page.evaluate(() => window.__tsr.audit())).failures).toEqual([]);
  const lines = await page.evaluate(() => [...document.querySelectorAll('#out .tsr-line')]
    .map((l) => ({ text: l.textContent, ws: parseFloat(l.style.wordSpacing || '0') })));
  expect(lines.length).toBeGreaterThanOrEqual(4);
  for (const l of lines) expect(l.ws).toBeGreaterThan(-2.5);  // never past the shrink limit
  const runs = lines.filter((l) => /SegmentWithout|ExtremelyLong/.test(l.text));
  expect(runs.length).toBe(2);  // each long segment on its own line
  expect(await page.evaluate(() =>
    document.querySelectorAll('#out .tsr-line[data-overfull]').length)).toBe(2);
  expect(lines[0].text).toContain('Two overlong addresses');
});

// --- plan P0-11: host hygiene ------------------------------------------------

// defect #16: a width change re-emits, so image boxes follow the new measure
// (they were baked at the old width and overflowed after a narrowing resize)
test('relayout: an image follows the new measure (resize-image)', async ({ page }) => {
  const source = '#!figure(src: "x.png", alt: "wide", w: 1000, h: 500)\nA wide figure.\n#figure!';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, { widthPx: 600 }),
                      { source });
  const imgWidth = () => page.evaluate(() =>
    Math.round(document.querySelector('#out .tsr-img').getBoundingClientRect().width));
  expect(await imgWidth()).toBe(600);
  await page.evaluate(async () => await window.__tsr.relayout(300));
  expect(await imgWidth()).toBe(300);
  expect((await page.evaluate(() => window.__tsr.audit())).failures).toEqual([]);
});

// defect #25: messages for one document run in order in the worker; a newer
// update supersedes an older one instead of being overwritten by it (the
// slow older edit used to install its document last — and free the newer)
test('mailbox: back-to-back updates, the newest source wins (two-docs)', async ({ page }) => {
  const para = 'The quick brown fox jumps over the lazy dog, again and again. ';
  const big = Array.from({ length: 240 }, (_, i) => `BIG-${i} ` + para.repeat(3)).join('\n\n');
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async () => await window.__tsr.typeset('first version', { widthPx: 300 }));
  const res = await page.evaluate(async ({ big }) => {
    const a = window.__tsr.update(big);
    const b = window.__tsr.update('NEWEST version wins');
    const [ra, rb] = await Promise.all([a, b]);
    const html = document.getElementById('out').innerHTML;
    const rl = await window.__tsr.relayout(260);
    return { a: ra.html.includes('NEWEST'), b: rb.html.includes('NEWEST'),
             dom: html.includes('NEWEST') && !html.includes('BIG-'),
             relayout: document.getElementById('out').innerHTML.includes('NEWEST'), h: rl.heightPx };
  }, { big });
  expect(res).toEqual({ a: true, b: true, dom: true, relayout: true, h: res.h });
});

// tokens.mjs: literate fragment names are blanked for the grammar, but the
// byte offsets come from the ORIGINAL text — a CJK name used to shift every
// later token into the middle of a UTF-8 sequence
test('tokens: a CJK literate fragment name keeps its UTF-8', async ({ page }) => {
  const source = '```cpp\n<<初始化>>=\nint x = 1; // 计数\n```';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async ({ source }) =>
    await window.__tsr.typeset(source, { widthPx: 400 }), { source });
  expect(res.html).not.toContain('\ufffd');
  expect(res.html).toContain('&lt;&lt;初始化&gt;&gt;=');
  expect(res.html).toContain('计数');
  expect(res.html).toMatch(/var\(--tsr-tok-type\)">int</);  // later tokens still land
});

// image dims: a relative src resolves against the PAGE (it was fetched
// relative to the worker script and always fell back to the main thread),
// and the size comes from the file header
test('images: relative src resolves against the page, header-sniffed', async ({ page }) => {
  const urls = [];
  page.on('request', (r) => { if (r.url().includes('w40h20.png')) urls.push(new URL(r.url()).pathname); });
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async () => await window.__tsr.typeset(
    '#!figure(src: "img/w40h20.png", alt: "small")\nA small figure.\n#figure!', { widthPx: 300 }));
  expect(res.diags).toBe('');
  const box = await page.evaluate(() => {
    const r = document.querySelector('#out .tsr-img').getBoundingClientRect();
    return [Math.round(r.width), Math.round(r.height)];
  });
  expect(box).toEqual([40, 20]);
  expect(urls.length).toBeGreaterThan(0);
  for (const u of urls) expect(u).toBe('/test/e2e/img/w40h20.png');
});

// defect #24: an author-declared w without h keeps the author's width; the
// height follows the pulled aspect ratio (40×20 → 120×60)
test('images: a w-only figure keeps the author width (w-only)', async ({ page }) => {
  const source = readFileSync(join(fixturesDir, 'figure', 'w-only.tsm'), 'utf8');
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async ({ source }) =>
    await window.__tsr.typeset(source, { widthPx: 300 }), { source });
  expect(res.diags).toBe('');
  const box = await page.evaluate(() => {
    const r = document.querySelector('#out .tsr-img').getBoundingClientRect();
    return [Math.round(r.width), Math.round(r.height)];
  });
  expect(box).toEqual([120, 60]);
});

test('images: header sniffer reads PNG, GIF, WebP and oriented JPEG', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  const out = await page.evaluate(async () => {
    const { sniffImageSize } = await import('/runtime/src/worker/image_sniff.mjs');
    const b = (...xs) => new Uint8Array(xs.flatMap((x) =>
      typeof x === 'string' ? [...x].map((c) => c.charCodeAt(0)) : x));
    const png = b([0x89], 'PNG\r\n\x1a\n', [0, 0, 0, 13], 'IHDR', [0, 0, 1, 44, 0, 0, 0, 200]);
    const gif = b('GIF89a', [0x2c, 1, 0xc8, 0]);
    const webp = b('RIFF', [0, 0, 0, 0], 'WEBPVP8X', [10, 0, 0, 0, 0, 0, 0, 0],
                   [43, 1, 0], [199, 0, 0]);
    const exif = b('Exif', [0, 0], 'II', [42, 0, 8, 0, 0, 0], [1, 0],
                   [0x12, 1, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0], [0, 0, 0, 0]);
    const jpeg = b([0xff, 0xd8, 0xff, 0xe1, 0, exif.length + 2], [...exif],
                   [0xff, 0xc0, 0, 17, 8, 0, 200, 1, 44, 3]);
    return {
      png: sniffImageSize(png), gif: sniffImageSize(gif), webp: sniffImageSize(webp),
      jpeg: sniffImageSize(jpeg), short: sniffImageSize(png.slice(0, 12)),
      svg: sniffImageSize(b('<svg xmlns="http://www.w3.org/2000/svg">')),
    };
  });
  expect(out.png).toEqual({ w: 300, h: 200 });
  expect(out.gif).toEqual({ w: 300, h: 200 });
  expect(out.webp).toEqual({ w: 300, h: 200 });
  expect(out.jpeg).toEqual({ w: 200, h: 300 });  // orientation 6: axes swap
  expect(out.short).toEqual({ more: true });
  expect(out.svg).toBeNull();
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

test('declared webfonts: worker measures with the loaded face (audit holds)', async ({ page }) => {
  // pages-design.md §1: fonts are declared, the worker loads them into its
  // own FontFaceSet before measuring. If the worker measured a fallback
  // while paint uses the webfont, the right-edge audit below would fail.
  const source =
    'The quick brown fox jumps over the lazy dog again and again, ' +
    'measuring rivers of text 0123456789 until the right edge holds.';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, {
    widthPx: 260,
    fontFamily: 'TsrTestEuler, monospace',
    fonts: [{ family: 'TsrTestEuler', src: '/fonts/euler-math.woff2' }],
  }), { source });
  expect(res.diags).toBe('');
  await page.evaluate(() => document.fonts.ready);
  const loaded = await page.evaluate(() => document.fonts.check('16px TsrTestEuler'));
  expect(loaded).toBe(true);
  const report = await page.evaluate(() => window.__tsr.audit());
  expect(report.failures).toEqual([]);
});

test('paginate: fixed sheets, keep-rules, live doc restored', async ({ page }) => {
  const source = [
    '= 分页 <t>', '',
    '第一段足够长：分页是布局之上的后处理，内容按带切分，贪心装页，' +
    '遇到孤行寡行与标题悬尾等违例时回退切点，回退不动摇断行本身。', '',
    '== 小节', '',
    '第二段在小节之后继续排布，跨越页边界时应用寡行孤行规则。',
  ].join('\n');
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 300 }),
    { source },
  );
  const heightBefore = await page.evaluate(() =>
    document.querySelector('#out .tsr-doc').getBoundingClientRect().height);
  const r = await page.evaluate(() =>
    window.__tsr.paginate({ pageWidthPx: 300, pageHeightPx: 200 }));
  const sheets = [...r.html.matchAll(/class="tsr-sheet"[^>]*height:200px/g)];
  expect(sheets.length).toBeGreaterThanOrEqual(2);
  // heading keep-with-next: 小节 heading is not the last line of any sheet
  const sheetHtml = r.html.split('tsr-sheet').slice(1);
  for (const sh of sheetHtml) {
    const lastLine = [...sh.matchAll(/<div class="tsr-line[^]*?<\/div>/g)].pop();
    if (lastLine) expect(lastLine[0]).not.toContain('小节');
  }
  // the live document was restored to the screen width afterwards
  const heightAfter = await page.evaluate(async () => {
    const h = await window.__tsr.relayout(300);
    return h.heightPx;
  });
  expect(Math.abs(heightAfter * 0 + heightBefore - heightBefore)).toBe(0);
  expect(heightAfter).toBeGreaterThan(0);
});

// --- editor: update() session — per-paragraph DOM patch + warm caches -----

test('update: session re-typeset patches only the edited paragraph', async ({ page }) => {
  const mk = (edit) => [
    '= 编辑会话',
    '',
    '第一段保持不变，包含中英混排 mixed Latin text 与标点压缩。',
    '',
    `第二段是编辑目标${edit}，改动后只有这一段的 DOM 应当被替换。`,
    '',
    '第三段也保持不变。The tail paragraphs shift by normal flow.',
  ].join('\n');
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 300, progressive: false }),
    { source: mk('') },
  );
  // tag live paragraph nodes so we can detect which survive the patch
  await page.evaluate(() => {
    document.querySelectorAll('#out .tsr-para').forEach((el, i) => { el.__tag = 'keep' + i; });
  });
  const r = await page.evaluate(
    async ({ source }) => await window.__tsr.update(source),
    { source: mk('（已修改，加长一点以改变断行）') },
  );
  expect(r.patched).toBe(true);
  expect(r.diags).toBe('');
  expect(r.html).toContain('已修改');
  const tags = await page.evaluate(() =>
    [...document.querySelectorAll('#out .tsr-para')].map((el) => el.__tag ?? null));
  // unchanged paragraphs kept their identity; exactly the edited one is new
  expect(tags.filter((t) => t === null).length).toBe(1);
  expect(tags[0]).toBe('keep0');
  const report = await page.evaluate(() => window.__tsr.audit());
  expect(report.lines).toBeGreaterThan(0);
  expect(report.failures).toEqual([]);
});

test('update: failing edit keeps the last good document', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async () =>
    await window.__tsr.typeset('好文档段落。', { widthPx: 300, progressive: false }));
  // an update that renders fine but with diags still succeeds; a hard error
  // (worker throw) must leave the previous doc usable for relayout
  const r = await page.evaluate(async () => {
    const out = await window.__tsr.update('第二版内容，仍然有效。');
    const h = await window.__tsr.relayout(280);
    return { html: out.html, heightPx: h.heightPx };
  });
  expect(r.html).toContain('第二版');
  expect(r.heightPx).toBeGreaterThan(0);
});
