// vscode-tsm preview page + loopback server (editor-design.md §5): the
// extension's browser-side pieces are plain web code, so they get CI
// coverage without a VSCode host. The spec spins the extension's own
// static server and drives preview.html over postMessage.
import { test, expect } from '@playwright/test';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const { startServer } = require(join(root, 'editors/vscode-tsm/src/server.js'));

let srv;
test.beforeAll(async () => {
  srv = await startServer({
    assetRoot: root,
    mediaDir: join(root, 'editors/vscode-tsm/media'),
    docRoots: new Map(),
  });
});
test.afterAll(() => srv.close());

test('preview page: typeset, incremental update, reveal, jump', async ({ page }) => {
  await page.goto(`http://127.0.0.1:${srv.port}/preview.html`);
  const result = await page.evaluate(async () => {
    const events = [];
    window.addEventListener('message', (e) => {
      if (e.data?.type === 'state' || e.data?.type === 'jump') events.push(e.data);
    });
    const src = ['= 预览标题', '', '第一段正文，包含 *强调* 与 `code`。', '',
                 '第二段 second paragraph with English text mixed in.'].join('\n');
    const nStates = () => events.filter((s) => s.type === 'state').length;
    const waitFor = (n) => new Promise((r) => {
      const iv = setInterval(() => { if (nStates() >= n) { clearInterval(iv); r(); } }, 25);
    });
    window.postMessage({ type: 'src', text: src, version: 1,
      cfg: { fontFamily: 'Georgia, serif', baseSizePx: 18 } }, '*');
    await waitFor(1);
    window.postMessage({ type: 'src', text: src.replace('第二段', '第二段（改）'),
      version: 2, cfg: {} }, '*');
    await waitFor(2);
    window.postMessage({ type: 'reveal', offset: 20 }, '*');
    await new Promise((r) => setTimeout(r, 100));
    document.querySelectorAll('.tsr-para')[2]?.querySelector('.tsr-line')
      ?.dispatchEvent(new MouseEvent('dblclick', { bubbles: true }));
    await new Promise((r) => setTimeout(r, 100));
    return {
      paras: document.querySelectorAll('.tsr-para').length,
      states: events.filter((s) => s.type === 'state'),
      jumps: events.filter((s) => s.type === 'jump'),
      marked: document.querySelectorAll('.tsr-jump').length,
      edited: document.body.textContent.includes('第二段（改）'),
    };
  });
  expect(result.paras).toBe(3);
  expect(result.states.map((s) => s.version)).toEqual([1, 2]);
  expect(result.states.every((s) => s.diags === '' && !s.error)).toBe(true);
  expect(result.edited).toBe(true);
  expect(result.marked).toBe(1);
  expect(result.jumps.length).toBe(1);
  expect(result.jumps[0].offset).toBeGreaterThan(0);
});

test('preview page: a broken engine load surfaces as error state', async ({ page }) => {
  // sanity: unknown asset paths 404 rather than hanging the pump
  const res = await page.request.get(`http://127.0.0.1:${srv.port}/no/such/file.mjs`);
  expect(res.status()).toBe(404);
});
