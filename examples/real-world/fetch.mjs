#!/usr/bin/env node
// Fetch + convert the real-world corpus (docs/real-world-report.md).
//   node examples/real-world/fetch.mjs        → examples/real-world/*.tsm
// Wikipedia (CC BY-SA 4.0) and the HoTT book (CC BY-SA 4.0) outputs are
// committed with attribution; pbr-book.org (all rights reserved, free to
// read online) is converted locally only and gitignored.
import { execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');
const tmp = join(here, '.cache');
mkdirSync(tmp, { recursive: true });
const get = async (url) => {
  const r = await fetch(url, { headers: { 'user-agent': 'Typesetter-corpus/1.0 (https://github.com/zball-bz/Typesetter)' } });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return await r.text();
};
const conv = (tool, file, out, extra = []) =>
  writeFileSync(join(here, out), execFileSync(process.execPath,
    [join(root, 'tools/convert', tool), file, ...extra], { encoding: 'utf8', maxBuffer: 1 << 26 }));

const wiki = async (lang, title, out) => {
  const f = join(tmp, `${out}.wikitext`);
  writeFileSync(f, await get(`https://${lang}.wikipedia.org/w/index.php?title=${encodeURIComponent(title)}&action=raw`));
  conv('wiki2tsm.mjs', f, `${out}.tsm`, ['--lang', lang]);
};
await wiki('en', 'Typesetting', 'wiki-typesetting');
await wiki('zh', '活字印刷术', 'wiki-huozi');

const hott = join(tmp, 'introduction.tex');
writeFileSync(hott, await get('https://raw.githubusercontent.com/HoTT/book/master/introduction.tex'));
const bib = join(tmp, 'references.bib');
writeFileSync(bib, await get('https://raw.githubusercontent.com/HoTT/book/master/references.bib'));
conv('tex2tsm.mjs', hott, 'hott-introduction.tsm',
  ['--bib', bib, '--bib-out', join(here, 'hott-refs.json'), '--bib-ref', '/examples/real-world/hott-refs.json']);

for (const [page, out] of [
  ['Photorealistic_Rendering_and_the_Ray-Tracing_Algorithm', 'pbr-1-1'],
  ['pbrt_System_Overview', 'pbr-1-2'],
]) {
  const f = join(tmp, `${out}.html`);
  writeFileSync(f, await get(`https://pbr-book.org/4ed/Introduction/${page}`));
  conv('html2tsm.mjs', f, `${out}.tsm`, ['--base', 'https://pbr-book.org/4ed/Introduction/']);
}
console.log('corpus regenerated in', here);
