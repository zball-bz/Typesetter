import { chromium, firefox } from '@playwright/test';
for (const [name, eng] of [['chromium', chromium], ['firefox', firefox]]) {
  const browser = await eng.launch();
  const p = await browser.newPage({ viewport: { width: 900, height: 900 } });
  await p.goto('http://localhost:8788/docs/syntax/');
  await p.waitForFunction(() => document.querySelector('.print-btn') && !document.querySelector('.print-btn').disabled, null, { timeout: 30000 });
  await p.click('.print-btn');
  await p.waitForTimeout(4000);
  console.log(name, await p.evaluate(() => {
    const a = document.querySelector('article[data-zblang="zh"]');
    return JSON.stringify({
      visible: !!a && !a.hidden && getComputedStyle(a).display !== 'none',
      lines: document.querySelectorAll('article .tsr-line').length,
      printRootCleaned: !document.getElementById('tsr-print-root'),
    });
  }));
  // bilingual page: switch still works and print doesn't break it
  await p.goto('http://localhost:8788/p/hello-typesetter/');
  await p.waitForFunction(() => document.querySelector('.print-btn') && !document.querySelector('.print-btn').disabled, null, { timeout: 30000 });
  await p.click('.lang-switch button[data-zblang="en"]');
  await p.waitForFunction(() => !!document.querySelector('article[data-zblang="en"] .tsr-doc'), null, { timeout: 30000 });
  await p.click('.print-btn');
  await p.waitForTimeout(3500);
  console.log(name, 'bilingual after print:', await p.evaluate(() => {
    const en = document.querySelector('article[data-zblang="en"]');
    return JSON.stringify({ enVisible: !!en && !en.hidden });
  }));
  await browser.close();
}
