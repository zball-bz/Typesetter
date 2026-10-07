// The static export (plan P3-36; design T7 S14, pages-design.md §3): a
// rendered bundle (renderTsm's) as a page — or, profile 'feed', a feed
// item. Pure: it returns the page, its parts and the resources to place
// beside it; the caller writes them (tools/export-static.mjs, a site
// generator's own pipeline).
//
//   const bundle = await renderTsm(src, { baseDir, settings });
//   const { html, copy } = await exportStatic(bundle, { template: (p) => myLayout(p), source: src, docDir });
//
// opts:
//   template(parts) → html   the page around the document (default: a bare
//                            page; a feed bundle's: the article alone). parts:
//                            { lang, title, head, article, hydrate, styles,
//                            settings, docinfo, profile, root }
//   hydrate (default: true on a page, false on a feed): the runtime upgrades
//                            the page client-side with the bundle's settings,
//                            verbatim (and inputs: a project page's labels)
//   source                   the document's text, for hydration
//   embedResources           images inlined as data: URIs (else copied)
//   docDir                   where the document's relative resources are
//   assets (default 'assets') where the shared assets are, relative to the page
import { readFile, realpath } from 'node:fs/promises';
import { extname, join, resolve } from 'node:path';
import { MATH_FONT } from '../shared/mathfont.gen.mjs';
import { settingOf } from '../shared/settings.gen.mjs';

const jsonIn = (v) => JSON.stringify(v).replace(/</g, '\\u003c');
// (the engine's attribute escaping: escapeHtml)
const attr = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
const MIME = { '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.gif': 'image/gif',
  '.webp': 'image/webp', '.svg': 'image/svg+xml', '.avif': 'image/avif' };

// the default page: the parts in a bare document
export const defaultTemplate = (p) => `<!doctype html>
<html lang="${attr(p.lang)}">
<head>
${p.head}
</head>
<body>
${p.article}${p.hydrate}
</body>
</html>
`;
// a feed item: the document alone (formulas as source: the feed profile)
export const feedTemplate = (p) => `<article lang="${attr(p.lang)}">
${p.root}</article>
`;

export async function exportStatic(bundle, {
  template, hydrate, source = '', inputs, title, fallbackTitle = '', embedResources = false,
  docDir = null, assets = 'assets',
} = {}) {
  const profile = bundle.profile ?? 'page';
  const feed = profile === 'feed';
  const doHydrate = (hydrate ?? !feed) && !feed;
  const settings = bundle.settings ?? {};
  // hydration: the resolved settings back verbatim, the host's own rows
  // (host.width, host.dppx, …: the browser's to say) aside
  const { host: _host, ...pageSettings } = settings;
  const docinfo = bundle.docinfo ?? {};
  // (plan P3-30) the engine decides the language — the document's own, the
  // host's, or detected; an undecided one is und, never "auto"
  const lang = (docinfo.lang || (settingOf(settings, 'doc.lang') === 'auto' ? 'und' : settingOf(settings, 'doc.lang')))
    .replace(/[^A-Za-z0-9-]/g, '');
  const pageTitle = title ?? (docinfo.title || fallbackTitle);
  let semantic = bundle.html ?? '';

  // the document's resources: inlined (images, embedResources) or copied
  // (a relative reference inside the document's folder, its real path too)
  const copy = [];
  let embedded = 0;
  if (docDir) {
    const realDocDir = await realpath(docDir);
    for (const m of bundle.resources ?? bundle.manifest ?? []) {
      if (m.status === 'denied' || /^[a-z][a-z0-9+.-]*:/i.test(m.url) || m.role === 'font' || m.role === 'font-metrics') continue;
      const from = m.url.startsWith(docDir + '/') ? m.url : resolve(docDir, m.url.startsWith('/') ? '.' + m.url : m.url);
      if (!from.startsWith(docDir + '/')) continue;
      let real;
      try { real = await realpath(from); } catch { continue; }
      if (!real.startsWith(realDocDir + '/')) continue;
      const mime = MIME[extname(from).toLowerCase()];
      if (embedResources && m.role === 'image' && mime) {
        const uri = `data:${mime};base64,${(await readFile(real)).toString('base64')}`;
        const before = semantic;
        semantic = semantic.split(`src="${attr(m.url)}"`).join(`src="${uri}"`);
        if (semantic !== before) {
          embedded++;
          continue;
        }
      }
      copy.push({ from: real, to: from.slice(docDir.length + 1) });
    }
  }

  // formulas as boxes (plan P3-27): their glyphs in the bundled math font
  const math = /class="tsr-math[" ]/.test(semantic);
  const styles = bundle.styles ?? {};
  const bodyFont = settingOf(settings, 'fonts.body');
  const cjkFont = settingOf(settings, 'fonts.cjk');
  // (the typeset page's root says them; the static page's code reads them
  // too: rules_css's var(--tsr-font-mono))
  const monoFont = settingOf(settings, 'fonts.mono');
  const monoCjkFont = settingOf(settings, 'fonts.monoCjk');
  const cssFont = (f) => f.replace(/"/g, "'").replace(/[<>{};]/g, '');
  // (plan P5-01) and the host's math fonts, where the host serves them
  const mathFace = math
    ? `@font-face { font-family: ${JSON.stringify(MATH_FONT.family)}; src: url(${JSON.stringify(`${assets}/${MATH_FONT.file}`)}); }\n` +
      (bundle.fonts ?? []).filter((f) => f.role === 'math')
        .map((f) => `@font-face { font-family: ${JSON.stringify(f.family.replace(/[<>{};]/g, ''))};` +
          ` src: url(${JSON.stringify(f.src.replace(/[<>{};]/g, ''))}); }\n`).join('')
    : '';
  const head = `<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${String(pageTitle).replace(/[<&]/g, '')}</title>
<style>
${mathFace}${styles.contract ?? ''}
${styles.theme ?? ''}
body { margin: 0 auto; max-width: 42em; padding: 2em 1em;
       font-family: ${bodyFont.replace(/[<>{};]/g, '')}; }
#tsr-root { --tsr-cjk-font: ${cssFont(cjkFont)}; --tsr-font-mono: ${cssFont(monoFont)};${monoCjkFont ? ` --tsr-font-mono-cjk: ${cssFont(monoCjkFont)};` : ''} }
.tsr-flow img { max-width: 100%; height: auto; }
.tsr-flow pre { overflow-x: auto; }
/* the rules (plan P3-01: engine defaults, host rules, the document's $.set) */
${styles.rules ?? bundle.css ?? ''}</style>`;
  const hydrateBlock = doHydrate ? `
<script type="text/plain" id="tsr-src">${source.replace(/<\/script/gi, '<\\/script')}</script>
<script type="module">
import { createEngine } from './${assets}/runtime/src/main/shell.mjs';
const el = document.getElementById('tsr-root');
const engine = createEngine();
engine.typeset(document.getElementById('tsr-src').textContent, el, {
  settings: ${jsonIn(pageSettings)},${inputs ? `\n  inputs: ${jsonIn(inputs)},` : ''}
  progressive: false,  // the static semantic page IS the first paint
}).catch((e) => console.warn('tsr hydrate failed; static page stands', e));
</script>` : '';
  const parts = {
    lang, title: pageTitle, head, root: semantic, article: `<article id="tsr-root">\n${semantic}</article>`,
    hydrate: hydrateBlock, styles, settings, docinfo, profile,
  };
  const html = (template ?? (feed ? feedTemplate : defaultTemplate))(parts);
  return { html, parts, copy, embedded, math, hydrate: doHydrate };
}
