import { chromium } from '@playwright/test';
const browser = await chromium.launch();
const p = await browser.newPage({ viewport: { width: 900, height: 800 } });
const errs = [];
p.on('console', (m) => { if (m.type() === 'error') errs.push(m.text().slice(0, 120)); });
await p.goto('http://localhost:8788/docs/figures/');
await p.waitForFunction(() => document.querySelector('.print-btn') && !document.querySelector('.print-btn').disabled, null, { timeout: 25000 });
await p.click('.print-btn');
await p.waitForFunction(() => {
  const f = document.querySelector('iframe');
  return f && f.contentDocument && f.contentDocument.querySelectorAll('.tsr-sheet').length > 0;
}, null, { timeout: 30000 });
const info = await p.evaluate(() => {
  const d = document.querySelector('iframe').contentDocument;
  const sheets = d.querySelectorAll('.tsr-sheet');
  return { sheets: sheets.length, h: sheets[0].style.height, css: !!d.querySelector('style') };
});
console.log('print iframe:', JSON.stringify(info));
console.log('errors:', errs.length ? errs : 'none');
await browser.close();
