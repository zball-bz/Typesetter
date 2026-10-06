// Hyphenation dictionaries in the browser (plan P4-06; D-X09): a language's
// patterns reach the engine through the hyphPatterns resource row, which the
// worker's provider answers from runtime/assets/hyph (tools/hyphc.mjs
// --assets). German text hyphenates by German patterns — every line that
// ends in the hyphen glyph breaks a word where the `hyphen` package's
// de-1996 does, and somewhere en-US would not — and a language the assets
// do not have says so instead of hyphenating by English patterns.
import { test, expect } from '@playwright/test';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const GERMAN = 'Die Silbentrennung der Übersetzung folgt den deutschen Mustern: ' +
  'Bundesverfassungsgericht, Fußgängerübergang, Donaudampfschifffahrtsgesellschaft, ' +
  'Rechtsschutzversicherungsgesellschaften und Kraftfahrzeughaftpflichtversicherung.';
// a word's hyphenated prefixes ("Sil-", "Silben-", …) by a package's patterns
const prefixes = (lang, word) => {
  const parts = require(`hyphen/${lang}`).hyphenateSync(word).split('­');
  return parts.slice(0, -1).map((_, i) => parts.slice(0, i + 1).join('') + '-');
};

async function typeset(page, source) {
  await page.goto('/test/e2e/harness.html');
  await page.waitForFunction(() => window.__tsrReady);
  return page.evaluate(async ({ source }) => await window.__tsr.typeset(source, { widthPx: 160 }), { source });
}

test('German hyphenates by its own patterns', async ({ page }) => {
  const res = await typeset(page, `#style({lang: 'de'})[${GERMAN}]`);
  expect(res.diags).toBe('');
  // the text again, a soft hyphen where a line ends in the hyphen glyph
  const lines = await page.evaluate(() => [...document.querySelectorAll('#out .tsr-line')]
    .map((l) => ({ text: l.textContent.trim(), hyphen: !!l.querySelector('[data-syn="hyphen"]') })));
  const text = lines.map((l) => (l.hyphen ? l.text.slice(0, -1) + '\u00AD' : l.text + ' ')).join('');
  let breaks = 0, notEnglish = 0;
  for (const token of text.split(/[^\p{L}\u00AD]+/u).filter((t) => t.includes('\u00AD'))) {
    const word = token.replaceAll('\u00AD', '');
    for (let at = token.indexOf('\u00AD'), k = 0; at >= 0; at = token.indexOf('\u00AD', at + 1), k++) {
      const piece = word.slice(0, at - k) + '-';
      breaks++;
      expect(prefixes('de-1996', word), `${piece} (${word})`).toContain(piece);
      if (!prefixes('en-us', word).includes(piece)) notEnglish++;
    }
  }
  expect(breaks).toBeGreaterThan(2);
  expect(notEnglish).toBeGreaterThan(0);
});

test('a language without patterns says so', async ({ page }) => {
  const res = await typeset(page, `#style({lang: 'tlh'})[Extraordinarily unhyphenatable representation.]`);
  expect(res.diags).toMatch(/hyph-unavailable [^\n]*'tlh'/);
});
