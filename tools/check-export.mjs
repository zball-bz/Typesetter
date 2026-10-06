#!/usr/bin/env node
// The static export's check (plan P3-21; design T9 M7): a post with an
// image and a bibliography beside it exports with its language and title
// (docinfo), its resources copied next to index.html (the manifest), and
// the runtime's module graph for hydration (D-I09) — the shell, the worker,
// the resource host — without what the page never loads (tests, Node code).
// Then renderTsm's resources: $.load below the document root (and denied
// above it, in the manifest), and a provider of the caller's (opts.providers)
// answering its kind.
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { renderTsm } from '../runtime/src/node/render.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = mkdtempSync(join(tmpdir(), 'tsr-export-'));
const settings = join(out, 'site.json');
writeFileSync(settings, JSON.stringify({ doc: { lang: 'en' } }));
let failures = 0;
const check = (ok, what) => {
  if (!ok) {
    console.error(`FAIL export: ${what}`);
    failures++;
  }
};
try {
  execFileSync('node', [join(root, 'tools/export-static.mjs'), join(root, 'test/export/post.tsm'), '-o', join(out, 'site'),
                        '--settings', settings], { stdio: ['ignore', 'pipe', 'pipe'] });
  const html = readFileSync(join(out, 'site/index.html'), 'utf8');
  check(html.includes('<html lang="en">'), '<html lang> is the document\'s (docinfo)');
  check(html.includes('<title>An exported post</title>'), 'the title is the first heading (docinfo)');
  check(existsSync(join(out, 'site/pic.png')), 'the image is copied beside the page');
  check(existsSync(join(out, 'site/refs.json')), 'the bibliography is copied beside the page');
  check(html.includes('class="tsr-math"') && html.includes('role="math"'), 'a formula is its box, labelled (plan P3-27)');
  check(/@font-face \{ font-family: [^;]+; src: url\("assets\//.test(html), 'the math font is declared for the static page');
  for (const f of ['runtime/src/main/shell.mjs', 'runtime/src/worker/worker.mjs', 'runtime/src/shared/resources/host.mjs',
                   'runtime/src/shared/theme.gen.mjs', 'engine/build-wasm/typesetter.wasm'])
    check(existsSync(join(out, 'site/assets', f)), `hydration has ${f}`);
  for (const f of ['runtime/src/node/render.mjs', 'test/e2e/typeset.spec.mjs'])
    check(!existsSync(join(out, 'site/assets', f)), `hydration leaves out ${f}`);
  const docDir = join(out, 'doc');
  execFileSync('mkdir', ['-p', docDir]);
  writeFileSync(join(docDir, 'data.json'), '{"n": 7}');
  writeFileSync(join(out, 'secret.txt'), 'no');
  const source = '```zz\nalpha beta\n```\n\n```json\n{"k": 1}\n```\n\n#{ const d = await $.load("data.json", {as: "json"}); }\nN is #(String(d.n)).\n\n'
    + '#{ let why = "read"; try { await $.load("../secret.txt"); } catch (e) { why = e.message; } }\nThe secret was #(why).';
  // a language of the caller's beside the built-in ones (plan P3-22: match)
  const tokens = { match: (r) => r.lang === 'zz', resolve: (rows) => rows.map((r) => ({ resId: r.resId, runs: new Uint32Array([0, 5, 1]) })) };
  const r = await renderTsm(source, { baseDir: docDir, rootDir: docDir, providers: [{ kind: 'codeTokens', provider: tokens }] });
  check(r.html.includes('N is 7.'), '$.load reads a resource beside the document');
  check(r.html.includes('The secret was resource outside the document root.'), '$.load is confined to the document root');
  check(r.manifest.some((m) => m.role === 'load' && m.url.endsWith('data.json') && m.status === 'ok'), 'the manifest has the load');
  check(r.manifest.some((m) => m.url === '../secret.txt' && m.status === 'denied'), 'the manifest has the denied load');
  check(/<span class="tsr-c-tok-[a-z]+">alpha<\/span> beta/.test(r.html), 'the caller\'s codeTokens provider answered');
  check(/<span class="tsr-c-tok-number">1<\/span>/.test(r.html), 'the built-in highlighter still answers the rest');
} catch (e) {
  console.error(String(e.stderr ?? e));
  failures++;
} finally {
  rmSync(out, { recursive: true, force: true });
}
if (failures) process.exit(1);
console.log('export: ok');
