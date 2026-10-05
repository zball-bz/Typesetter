// The VS Code extension's engine bridge (plan P1-09): tokens and outline come
// from the engine wasm in the extension host, converted to UTF-16 indices.
// Pure Node (no page): editors/vscode-tsm/src/{engine,features}.js have no
// vscode dependency.
import { test, expect } from '@playwright/test';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const require = createRequire(import.meta.url);
const engine = require(join(root, 'editors/vscode-tsm/src/engine.js'));
const features = require(join(root, 'editors/vscode-tsm/src/features.js'));

const SRC = [
  '= 标题 <intro>',
  '',
  '正文有 *加粗*、`代码` 与 $a^2$；引用 @intro 😀 和 #strong[强调]。',
  '',
  '#!figure(label: "f1")',
  '图注',
  '#figure!',
  '',
  '== 第二节',
  '',
  '```js',
  'const x = 1;',
  '```',
].join('\n');

test('editor: engine tokens and outline in UTF-16', async ({}, testInfo) => {
  test.skip(testInfo.project.name !== 'chromium-dsf1', 'node-only: one project is enough');
  await engine.load(root);
  expect(engine.ready()).toBe(true);
  const toks = engine.tokens(SRC);
  const text = (t) => SRC.slice(t.s, t.e);
  const find = (tag, s) => toks.find((t) => t.tag === tag && text(t) === s);
  expect(find('keyword', '=')).toBeTruthy();
  expect(find('label', '<intro>')).toBeTruthy();
  expect(find('string', '`代码`')).toBeTruthy();
  expect(find('type', '$a^2$')).toBeTruthy();
  expect(find('constant', '@intro')).toBeTruthy();  // after CJK and before an astral emoji
  expect(find('function', '#strong')).toBeTruthy();
  expect(find('function', '#figure!')).toBeTruthy();
  expect(find('embedded', 'const x = 1;')).toBeTruthy();
  for (let i = 1; i < toks.length; i++) expect(toks[i].s).toBeGreaterThanOrEqual(toks[i - 1].e);

  const o = engine.outline(SRC);
  expect(o.headings.map((h) => [h.level, h.title, h.label])).toEqual([[1, '标题', 'intro'], [2, '第二节', null]]);
  expect(SRC.slice(o.headings[1].span[0], o.headings[1].span[1])).toBe('== 第二节');
  expect(o.regions.map((r) => [r.name, r.label])).toEqual([['figure', 'f1']]);
  expect(o.labels.map((l) => [l.id, l.rule])).toEqual([['intro', 'heading'], ['f1', 'region']]);
  expect(o.fences.map((f) => f.lang)).toEqual(['js']);

  const lineOf = (i) => SRC.slice(0, i).split('\n').length - 1;
  const last = SRC.split('\n').length - 1;
  const tree = features.headingTree(o, lineOf, last);
  expect(tree.map((h) => [h.title, h.line, h.endLine])).toEqual([['标题', 0, last]]);
  expect(tree[0].children.map((h) => [h.title, h.line, h.endLine])).toEqual([['第二节', 8, last]]);
  expect(features.foldingRanges(o, lineOf, last)).toEqual([
    { start: 0, end: last }, { start: 4, end: 6 }, { start: 8, end: last }, { start: 10, end: 12 }]);
  expect(features.regionNames(o)).toEqual(['figure', 'table']);
  expect(features.labelNames(o)).toEqual(['intro', 'f1']);
});
