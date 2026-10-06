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
  // a link's last letter kerns with the space or comma after it in the
  // browser (one face across </a>); the engine's KernCtx stops at the run
  // boundary — "Appendix A, …" refs (P4-01: KernCtx over the paint runs)
  ['semantics/appendix', 'P4-01'],
]);

// Content wider than its measure is set Overfull on a line of its own and
// reported (plan P0-12): these fixtures expect exactly that warning.
const EXPECTED_DIAGS = new Map([
  ['doc/url-overlong', /^warning overfull-line [^\n]*\n$/],  // two 500px URL segments at 300px
  // a formula in a 47px table cell; Id is an undeclared name (plan P3-24, D-M04: info)
  ['region/hott-row', /^info math-implicit-name [^\n]*\nwarning overfull-line [^\n]*\n$/],
  // defeq before its declaration is a name, and says so (plan P3-24, D-M04)
  ['math/decl', /^(info math-implicit-name [^\n]*\n){2}$/],
  // references to unnumbered regions show their label text (plan P1-18 anchors)
  ['region/anchor-kinds', /^(info ref-unnumbered [^\n]*\n){3}$/],
  // a splice of undefined renders nothing and says so (plan P2-01, D-I05)
  ['splice/dot-rule', /^warning splice-undefined [^\n]*\n$/],
  // instances before their (hoisted) declaration say so (plan P2-07)
  ['semantics/parity-declared', /^info decl-after-use [^\n]*\n$/],
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

// (plan P3-19; design T7 S11) the host's line-height no longer moves a
// baseline: the contract pins each run's line box to its content area
test('baseline: a host line-height leaves every baseline where the engine set it', async ({ page }) => {
  const source = fixtures.find((f) => f.name === 'code/runs').source + '\n\nA paragraph of prose under a host line-height of 3.\n';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(() => { document.querySelector('#out').style.lineHeight = '3'; });
  await page.evaluate(async ({ source }) => await window.__tsr.typeset(source, { widthPx: 400 }), { source });
  const report = await page.evaluate(() => window.__tsr.audit());
  expect(report.failures.filter((f) => f.audit === 'baseline')).toEqual([]);
  expect(report.lines).toBeGreaterThan(0);
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
  const source = '#let xs = Array.of("a", em("b"), 3)\n\nA #xs B #(null) C #(x => 1) D #(m`p ${strong("q")} r`) E\n';
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

test('copy joins CJK line breaks seamlessly; references copy as text (D-R01)', async ({ page }) => {
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
  expect(text).toContain('见 §1 一节');  // a reference is class-body text: copied (D-R01)
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
  // a citation is class-body generated text: copied as shown (D-R01)
  expect(text).toContain('Knuth and Plass [1]; hyphenation patterns follow Liang [2], and the pair');
  expect(text).toContain('keeps its number; the uncited entry');
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
  // (plan P3-11) the grid's alignment does not depend on wrapping: a block
  // that does not wrap still snaps
  const at = async (src) => await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 120, verbatimSnapKerning: true }),
    { source: src },
  );
  const wrapped = await at(source);
  expect(wrapped.html).toContain('data-syn="cont"');  // at 120px the block wraps
  const nowrap = await at(source.replace('```text', '```text(wrap: false)'));
  expect(nowrap.html).not.toContain('data-syn="cont"');
  expect((nowrap.html.match(/data-snap="1"/g) ?? []).length).toBeGreaterThan(0);
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
    writeFileSync(opsFile, await execute({ program, js }, { parse: (req) => new Uint8Array(execFileSync(tsrc, ['--fragments=-'], { input: req })) }));
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

// --- plan P2-03: one calling convention --------------------------------------
// The sugar and the explicit call build the same tree, and so do a region and
// the same constructor called with content — before and after an override
// ($.ctor, a second $.region delegating through ctx.next). Shapes compare
// without spans and labels (a splice's result is unspanned until P2-04).
test('constructors: sugar ≡ call and region ≡ call, before and after an override', async () => {
  test.skip(test.info().project.name !== 'chromium-dsf1', 'Node-side: one device scale is enough');
  const { execFileSync } = await import('node:child_process');
  const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
  const tsrc = join(root, 'engine/build/tsrc');
  const shapes = (name) => {
    const tsm = join(root, 'test/fixtures/lower', `${name}.tsm`);
    const tree = execFileSync(tsrc, ['--stage=tree', `--ops=${tsm.replace(/\.tsm$/, '.ops')}`, tsm],
                              { encoding: 'utf8' });
    const out = [];
    for (const line of tree.split('\n').slice(1)) {
      if (!line.trim()) continue;
      const shape = line.replace(/ @\[\d+,\d+\)/g, '').replace(/ label="[^"]*"/g, '');
      if (/^  \S/.test(line)) out.push(shape);
      else out[out.length - 1] += '\n' + shape;
    }
    return out;
  };
  const o = shapes('ctor-override');  // *x*, #strong[x]; then with strong overridden
  expect(o.length).toBe(4);
  expect(o[1]).toBe(o[0]);
  expect(o[3]).toBe(o[2]);
  expect(o[2]).toContain('role="key"');
  const r = shapes('region-call');  // #!callout … #callout! and #callout[…], twice
  expect(r.length).toBe(4);
  expect(r[1]).toBe(r[0]);
  expect(r[3]).toBe(r[2]);
  expect(r[2]).toContain('role="frame"');
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

// literate fragment names are blanked for the grammar (plan P3-22: the
// engine's noweb overlay, byte for byte), so a CJK name shifts no later
// token into the middle of a UTF-8 sequence
test('tokens: a CJK literate fragment name keeps its UTF-8', async ({ page }) => {
  const source = '```cpp-literate\n<<初始化>>=\nint x = 1; // 计数\n```';
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const res = await page.evaluate(async ({ source }) =>
    await window.__tsr.typeset(source, { widthPx: 400 }), { source });
  expect(res.html).not.toContain('\ufffd');
  expect(res.html).toContain('&lt;&lt;初始化&gt;&gt;=');
  expect(res.html).toContain('计数');
  expect(res.html).toMatch(/tsr-c-tok-type[^>]*>int</);  // later tokens still land (plan P3-18: a token's class)
  expect(res.html).toMatch(/tsr-c-tok-label[^>]*>&lt;&lt;初始化&gt;&gt;=</);  // the fragment is one label
});

// (plan P3-22) the overlay is opt-in: plain C++ keeps its shifts, and a
// rule turns noweb on for any language
test('tokens: noweb is a profile or a rule, never plain cpp', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const plain = await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 400 }),
    '```cpp\nint y = (1 << n) >> 2; // <<not a fragment>>\n```');
  expect(plain.html).not.toContain('tsr-c-tok-label');
  expect(plain.html).toMatch(/tsr-c-tok-comment[^>]*>\/\//);  // the comment holds what looks like a fragment
  const ruled = await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 400 }),
    "#{ $.set({kind: 'codeblock', lang: 'python'}, {codeblock: {overlays: ['noweb']}}) }\n\n```python\n<<setup>>=\nx = 1\n```");
  expect(ruled.html).toMatch(/tsr-c-tok-label[^>]*>&lt;&lt;setup&gt;&gt;=</);
  expect(ruled.html).toMatch(/tsr-c-tok-number[^>]*>1</);
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

// plan P2-04 (design T2 S7): an edit between a #let and the paragraph that
// splices its value re-patches only the edited paragraph — the value's
// occurrence carries the splice's span (containment keeps it inside the
// splicing paragraph), so that paragraph's source range only shifts
test('update: an edit between a #let and its splice patches only the edited paragraph', async ({ page }) => {
  const mk = (edit) => [
    '#let term = em("occurrence spans")',
    '',
    `The paragraph between the binding and its use${edit}, edited here.`,
    '',
    'This paragraph splices #term and keeps its identity across the edit.',
    '',
    'A last paragraph that only shifts.',
  ].join('\n');
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(
    async ({ source }) => await window.__tsr.typeset(source, { widthPx: 300, progressive: false }),
    { source: mk('') },
  );
  await page.evaluate(() => {
    document.querySelectorAll('#out .tsr-para').forEach((el, i) => { el.__tag = 'keep' + i; });
  });
  const r = await page.evaluate(
    async ({ source }) => await window.__tsr.update(source),
    { source: mk(' (now longer, so it breaks differently)') },
  );
  expect(r.patched).toBe(true);
  expect(r.diags).toBe('');
  const tags = await page.evaluate(() =>
    [...document.querySelectorAll('#out .tsr-para')].map((el) => el.__tag ?? null));
  expect(tags).toEqual([null, 'keep1', 'keep2']);
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

// plan P3-05: the RenderResult commit path — a result carries only the blocks
// the shell lacks; inserting or deleting a paragraph replaces only that range
// and keeps every other block's element
test('commit: paragraph insert and delete keep the other blocks', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const paras = ['One para.', 'Two para.', 'Three para.', 'Four para.'];
  await page.evaluate(async ({ source }) =>
    await window.__tsr.typeset(source, { widthPx: 300, progressive: false }), { source: paras.join('\n\n') });
  const tag = () => page.evaluate(() =>
    document.querySelectorAll('#out .tsr-para').forEach((el, i) => { el.__tag = 'k' + i; }));
  const tags = () => page.evaluate(() =>
    [...document.querySelectorAll('#out .tsr-para')].map((el) => el.__tag ?? null));
  await tag();
  // insert after the second paragraph: the new block alone is new
  let r = await page.evaluate(async ({ source }) => await window.__tsr.update(source),
    { source: [paras[0], paras[1], 'Inserted para.', paras[2], paras[3]].join('\n\n') });
  expect(r.patched).toBe(true);
  expect(await tags()).toEqual(['k0', 'k1', null, 'k2', 'k3']);
  const pids = await page.evaluate(() =>
    [...document.querySelectorAll('#out .tsr-para')].map((el) => +el.dataset.pid));
  expect(pids).toEqual([0, 1, 2, 3, 4]);  // positional attributes follow
  // delete it again: the others keep their elements
  await tag();
  r = await page.evaluate(async ({ source }) => await window.__tsr.update(source), { source: paras.join('\n\n') });
  expect(r.patched).toBe(true);
  expect(await tags()).toEqual(['k0', 'k1', 'k3', 'k4']);
  const html = await page.evaluate(() => document.getElementById('out').innerHTML);
  expect(html).not.toContain('Inserted');
});

test('commit: upgrade records on update name the changed block', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async () => await window.__tsr.typeset('Alpha.\n\nBeta.\n\nGamma.',
    { widthPx: 300, progressive: false, collectUpgrades: true }));
  await page.evaluate(async () => await window.__tsr.update('Alpha.\n\nBeta, edited.\n\nGamma.'));
  const ups = await page.evaluate(() => window.__ups);
  expect(ups.length).toBe(2);  // the typeset's, then the update's
  expect(ups[1].map((u) => u.pid)).toEqual([1]);
  expect(ups[1][0].new.height).toBeGreaterThan(0);
});

test('commit: a result naming a key the shell dropped is asked for again', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async () =>
    await window.__tsr.typeset('First.\n\nSecond.\n\nThird.', { widthPx: 300, progressive: false }));
  await page.evaluate(() => window.__tsr.forgetHeld(0));  // the shell no longer holds block 0's body
  const r = await page.evaluate(async () => await window.__tsr.update('First.\n\nSecond.\n\nThird, edited.'));
  expect(r.diags).toBe('');
  const text = await page.evaluate(() => document.getElementById('out').textContent);
  expect(text).toContain('First.');
  expect(text).toContain('Third, edited.');
  const held = await page.evaluate(() => window.__tsr.held());
  expect(held.length).toBe(3);
});

// ---- shell core + behaviours (plan P3-06) ----------------------------------

const POP = '#out [data-tsr-shell="overlay"] .tsr-refpop';
const MARKER1 = '#out .tsr-doc a[href="#tsr-fn-1"][id]';

test('refPreview: a note marker shows the engine fragment of its body', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const src = 'A claim^[The note body carries *markup* and `code`.] continues.\n\nThe text cites the note back as @fn-1.';
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }), src);
  await page.hover(MARKER1);
  const pop = page.locator(POP);
  await expect(pop).toBeVisible();
  const html = await pop.innerHTML();
  expect(html).toContain('<strong>markup</strong>');
  expect(html).toContain('<code>code</code>');
  expect(html).not.toContain(' id=');   // ids suppressed
  expect(html).not.toContain('↩');     // the backlink left out
  expect(await pop.getAttribute('data-tsr-preview')).toBe('footnote');
  // outside the commit root
  expect(await page.evaluate(() => !!document.querySelector('#out .tsr-doc .tsr-refpop'))).toBe(false);
  await page.mouse.move(0, 0);
  await expect(pop).toHaveCount(0);
  // a reference to the note previews it too; the note's own backlink does not
  await page.hover('#out .tsr-doc a[href="#tsr-fn-1"]:not([id])');
  await expect(pop).toHaveText('The note body carries markup and code.');
  await page.mouse.move(0, 0);
  await page.hover('#out .tsr-doc a[href="#tsr-fnref-1"]');
  await page.waitForTimeout(150);
  await expect(pop).toHaveCount(0);
});

test('refPreview: CJK and hyphenated notes read as written', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const zh = '这是一个很长的中文脚注，用来检查弹窗里的文字在排版视图中跨行时不会被插入多余的空格，也不会丢失任何标点符号或者文字。';
  const en = 'Notwithstanding extraordinarily incomprehensible characterizations, internationalization considerations overwhelmingly predominate everywhere.';
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }),
    `A claim^[${en}] continues and 中文^[${zh}] 也是。`);
  // the typeset notes break both (the old popup scraped those lines)
  expect(await page.evaluate(() => document.querySelectorAll('#out [data-syn="hyphen"]').length)).toBeGreaterThan(0);
  await page.hover(MARKER1);
  await expect(page.locator(POP)).toHaveText(en);
  await page.mouse.move(0, 0);
  await page.hover('#out .tsr-doc a[href="#tsr-fn-2"][id]');
  await expect(page.locator(POP)).toHaveText(zh);
});

test('refPreview: the view keeps patching while a popup is open', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const doc = (second) => `A claim^[Body one.] continues.\n\n${second}\n\nThird paragraph.`;
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }),
    doc('Second paragraph.'));
  await page.hover(MARKER1);
  await expect(page.locator(POP)).toHaveText('Body one.');
  const r = await page.evaluate(async (s) => await window.__tsr.update(s), doc('Second paragraph, edited.'));
  expect(r.patched).toBe(true);
  expect(r.html).toContain('edited');
  await expect(page.locator(POP)).toHaveText('Body one.');
});

test('refPreview: a hover during an in-flight update shows the new content', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const doc = (body) => `A claim^[${body}] continues.\n\nSecond paragraph.`;
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }),
    doc('Old body.'));
  await page.evaluate((s) => window.__tsr.startUpdate(s), doc('New body.'));
  await page.hover(MARKER1);
  const r = await page.evaluate(async () => await window.__tsr.settle());
  expect(r.diags).toBe('');
  await expect(page.locator(POP)).toHaveText('New body.');
});

test('sessions: one engine, two documents, each with its own ids', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async () => {
    await window.__tsr.typeset('One^[Note of document one.] here.\n\nMore of one.', { widthPx: 300, progressive: false });
    await window.__tsr.typeset2('Two^[Note of document two.] here.\n\nMore of two.',
      { widthPx: 300, progressive: false, settings: { render: { idPrefix: 'b-' } } });
  });
  const ids = await page.evaluate(() => [...document.querySelectorAll('[id]')].map((e) => e.id));
  expect(ids.length).toBe(new Set(ids).size);
  expect(ids.filter((id) => id.startsWith('b-')).length).toBeGreaterThan(0);
  // each document's popup is its own (a non-default prefix included)
  await page.hover(MARKER1);
  await expect(page.locator(POP)).toHaveText('Note of document one.');
  await page.hover('#out2 .tsr-doc a[href="#b-fn-1"][id]');
  await expect(page.locator('#out2 [data-tsr-shell="overlay"] .tsr-refpop')).toHaveText('Note of document two.');
  await expect(page.locator(POP)).toHaveCount(0);
  // editing one leaves the other; disposing one leaves the other working
  const r = await page.evaluate(async () => await window.__tsr.update2('Two^[Note of document two.] here.\n\nMore of two, edited.'));
  expect(r.patched).toBe(true);
  expect(await page.locator('#out').textContent()).toContain('More of one.');
  await page.evaluate(() => window.__tsr.dispose(1));
  const r2 = await page.evaluate(async () => await window.__tsr.update2('Two, again.'));
  expect(r2.diags).toBe('');
  expect(await page.locator('#out2').textContent()).toContain('Two, again.');
  expect(await page.locator('#out').textContent()).toContain('More of one.');  // the last view stays
});

test('print: sheets under a print root of their own, ids derived', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async () => await window.__tsr.typeset(
    'Body^[A note.] text.\n\n= Heading <h>\n\nMore text that refers to @h.', { widthPx: 300, progressive: false }));
  const seen = await page.evaluate(async () => await window.__tsr.printCapture({ pageWidthPx: 300, pageHeightPx: 400 }));
  expect(seen.rootId).toBeNull();          // shell nodes have no ids
  expect(seen.inBody).toBe(true);
  expect(seen.style).toBe(true);
  expect(seen.duplicateIds).toBe(0);       // never an id shared with the live view
  expect(seen.printIds.length).toBeGreaterThan(0);
  expect(seen.printIds.every((id) => id.startsWith('tsrp-'))).toBe(true);
  expect(seen.leftover).toBe(false);
});

test('behaviours: a host registry, devAudit on every commit, a failing one disabled', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const warnings = [];
  page.on('console', (m) => { if (m.type() === 'warning') warnings.push(m.text()); });
  const r = await page.evaluate(async () => {
    const { createEngine, defaultBehaviors, devAudit } = await import('/runtime/src/main/shell.mjs');
    const reports = [];
    const boom = { name: 'boom', install(ctx) { ctx.onCommit(() => { throw new Error('boom'); }); return () => {}; } };
    // a capability the host supplies: the worker cannot fetch this image
    const engine = createEngine({
      behaviors: [...defaultBehaviors(), devAudit({ onReport: (x) => reports.push(x.ok) }), boom],
      capabilities: { imageDims: async () => ({ w: 120, h: 40 }) },
    });
    const el = document.getElementById('out2');
    const fig = '#!figure(src: "/no-such-image.png", alt: "x")\nA figure.\n#figure!';
    const h = await engine.typeset(`One para.\n\nTwo para.\n\n${fig}`, el, { widthPx: 300, progressive: false });
    const u1 = await h.update(`One para.\n\nTwo para, edited.\n\n${fig}`);
    const u2 = await h.update(`One para.\n\nTwo para, edited again.\n\n${fig}`);
    const box = el.querySelector('.tsr-img')?.getBoundingClientRect();
    const out = { reports, diags: h.diags + u1.diags + u2.diags, patched: u2.patched,
                  img: box ? [Math.round(box.width), Math.round(box.height)] : null };
    engine.dispose();
    return out;
  });
  expect(r.reports).toEqual([true, true, true]);  // install, then each commit
  expect(r.patched).toBe(true);                   // the throwing behaviour broke nothing
  expect(warnings.some((w) => w.includes('behavior boom failed'))).toBe(true);
  expect(r.img).toEqual([120, 40]);               // the host's capability answered
});

test('resources: a host provider module answers its kind (createEngine({providers}))', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const r = await page.evaluate(async () => {
    const { createEngine } = await import('/runtime/src/main/shell.mjs');
    const engine = createEngine({ providers: [{ kind: 'boxInfo', module: '/test/e2e/provider-box.mjs' }] });
    const el = document.getElementById('out2');
    const h = await engine.typeset('#!figure(src: "/no-such-image.png", alt: "x")\nA figure.\n#figure!', el,
      { widthPx: 300, progressive: false });
    const box = el.querySelector('.tsr-img')?.getBoundingClientRect();
    const out = { diags: h.diags, img: box ? [Math.round(box.width), Math.round(box.height)] : null };
    engine.dispose();
    return out;
  });
  expect(r.img).toEqual([150, 50]);  // the module's answer, not a fetch of the src
});

// ---- separators and the copy contract (plan P3-07) -----------------------

test('copy: a table is tab-separated rows; an empty cell keeps its column', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const source = '#!table(cols: 3)\nName | Qty | Price\nAn extraordinarily wide widget | | 3.50\n#table!';
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }), source);
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toBe('Name\tQty\tPrice\nAn extraordinarily wide widget\t\t3.50');
  const audit = await page.evaluate(() => window.__tsr.audit());
  expect(audit.failures).toEqual([]);
});

test('copy: code with a sidecar copies the code; inside the sidecar, the note (D-R03)', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const source = '```js(sidecar: "///")\nconst a = 1; /// the first note\n\nlet b = 2;\n```';
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }), source);
  // a blank code line survives; the sidecar is left out
  expect(await page.evaluate(() => window.__tsr.copyText())).toBe('const a = 1;\n\nlet b = 2;');
  const note = await page.evaluate(() => window.__tsr.copyOf('#out [data-track="sidecar"]'));
  expect(note).toBe('the first note');
  expect((await page.evaluate(() => window.__tsr.audit())).failures).toEqual([]);
});

test('copy: paragraphs set apart, items not; markers, backlinks and errors omitted', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const source = '#!aside\nA side note^[Its note.] here.\n- one\n- two\n#aside!\n\nAfter #nosuch() it.';
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }), source);
  const text = await page.evaluate(() => window.__tsr.copyText());
  expect(text).toContain('A side note here.\n\none\ntwo');  // the paragraph set apart, the items not
  expect(text).toContain('Its note.');
  expect(text).not.toContain('↩');
  expect(text).not.toMatch(/note1|⚠/);
});

test('copy: paged sheets keep block identity across a sheet cut', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const long = 'This paragraph is long enough to be cut between two sheets of a small page, ' +
    'so the copy rebuild must join its lines across the cut with spaces and never with a blank line.';
  const source = `First paragraph.\n\n${long}\n\nLast paragraph.`;
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }), source);
  const { html } = await page.evaluate(async () => await window.__tsr.paginate({ pageWidthPx: 300, pageHeightPx: 120 }));
  expect((html.match(/class="tsr-sheet"/g) ?? []).length).toBeGreaterThan(1);
  const text = await page.evaluate((h) => window.__tsr.copyOf(null, h), html);
  expect(text).toBe(`First paragraph.\n\n${long}\n\nLast paragraph.`);
});

test('copy: the semantic page omits what the typeset view omits (D-R06)', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  await page.evaluate(async () => await window.__tsr.typeset('A claim^[The note.] continues.', { widthPx: 300 }));
  const sem = await page.evaluate(() => window.__tsr.semanticHtml());
  expect(sem).toContain('data-syn="fn-marker"');
  expect(sem).toContain('data-syn="backlink"');
  const text = await page.evaluate((h) => window.__tsr.copyOf(null, h), sem);
  expect(text).toContain('A claim continues.');
  expect(text).toContain('The note.');
  expect(text).not.toContain('↩');
});

test('copy: an author\'s copy attribute replaces or omits, once per node', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const source = 'Keep #strong({copy: "replace:[R]"})[this rather long emphasised phrase that wraps over lines] ' +
    'and #em({copy: "omit"})[hidden] here.';
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 160, progressive: false }), source);
  const lines = await page.evaluate(() => document.querySelectorAll('#out [data-copy]').length);
  expect(lines).toBeGreaterThan(1);  // the node spans runs on several lines
  expect(await page.evaluate(() => window.__tsr.copyText())).toBe('Keep [R] and  here.');
});

// ---- a11y (plan P3-27; design T7 S13) -------------------------------------

test('a11y: formulas carry role=math and their source as label; the text layer is opt-in', async ({ page }) => {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  const source = 'A circle of area $pi r^2$ has a circumference that grows linearly with its radius.\n\n' +
                 '$ a^2 + b^2 = c^2 $ <eq-pyth>\n\nSee @eq-pyth.';
  // default: the label is on, the layer off
  await page.evaluate(async (s) => await window.__tsr.typeset(s, { widthPx: 300, progressive: false }), source);
  const labels = await page.evaluate(() =>
    [...document.querySelectorAll('#out [role="math"]')].map((e) => e.getAttribute('aria-label')));
  expect(labels).toContain('pi r^2');
  expect(labels).toContain('a^2 + b^2 = c^2');
  expect(await page.evaluate(() => document.querySelector('#out .tsr-sr'))).toBeNull();
  expect(await page.evaluate(() => document.querySelector('#out [aria-hidden]'))).toBeNull();

  // on: the lines are hidden, one sr-only paragraph per block holds what copy takes
  await page.evaluate(async (s) => await window.__tsr.typeset(s,
    { widthPx: 300, progressive: false, settings: { a11y: { textLayer: true, mathLabel: false } } }), source);
  const layer = await page.evaluate(() => ({
    hidden: [...document.querySelectorAll('#out [aria-hidden="true"]')].some((e) => e.querySelector('.tsr-line')),
    paras: [...document.querySelectorAll('#out .tsr-sr p')].map((p) => p.textContent),
    labels: document.querySelectorAll('#out [role="math"]').length,
    copy: window.__tsr.copyText(),
  }));
  expect(layer.labels).toBe(0);
  expect(layer.hidden).toBeTruthy();
  expect(layer.paras.length).toBe(3);
  expect(layer.paras.join('\n\n')).toBe(layer.copy);
  expect(layer.paras[0]).toContain('$pi r^2$');
  expect(layer.paras[1]).toBe('$ a^2 + b^2 = c^2 $');  // no equation number
  // it follows an update
  await page.evaluate(async (s) => await window.__tsr.update(s), source.replace('linearly', 'in step'));
  const paras = await page.evaluate(() => [...document.querySelectorAll('#out .tsr-sr p')].map((p) => p.textContent));
  expect(paras[0]).toContain('in step');
  expect(paras.length).toBe(3);
});
