#!/usr/bin/env node
// pbr-book.org chapter HTML → .tsm (real-world corpus, docs/real-world-
// report.md). Handles the site's PreTeXt-style output: h2/h3 sections,
// paragraphs with <em>/<tt>, figures (card.outerfigure → #!figure with the
// absolute image URL), literate-programming fragments (fragmentcode →
// ```cpp with the <<fragment>> names kept as text), lists. Math on the
// site is pre-rendered MathJax SVG — no LaTeX source survives — so each
// formula becomes its accessibility title in italics and is COUNTED as a
// gap by the report.
//
//   node tools/convert/html2tsm.mjs page.html --base https://pbr-book.org/4ed/Introduction/ > page.tsm
import { readFileSync } from 'node:fs';
import { Markup, decodeEntities, descriptionItems } from './kit.mjs';

const args = process.argv.slice(2);
const file = args.find((a) => !a.startsWith('--'));
const base = args.includes('--base') ? args[args.indexOf('--base') + 1] : '';
let html = readFileSync(file, 'utf8');

// (plan P3-35) the converter kit: entities by the WHATWG table, text escaped
// by the printer's escapeTsm, markup placed after it
const entities = decodeEntities;
const M = new Markup();
const flat = (s) => s.replace(/\s+/g, ' ').trim();

// main column only
const start = html.indexOf('class="maincontainer"');
if (start > 0) html = html.slice(start);
html = html.replace(/<nav[\s\S]*?<\/nav>/g, '').replace(/<script[\s\S]*?<\/script>/g, '')
  .replace(/<footer[\s\S]*?<\/footer>/g, '');

let mathCount = 0;
// MathJax SVG → italic title text
html = html.replace(/<svg[^>]*>[\s\S]*?<title[^>]*>([\s\S]*?)<\/title>[\s\S]*?<\/svg>/g, (m, t) => {
  mathCount++;
  return `<em>${t.trim()}</em>`;  // (its entities: decoded with the paragraph's)
});
html = html.replace(/<div class="displaymath">([\s\S]*?)<\/div>/g, '<p>$1</p>');

// figures
const figs = [];
html = html.replace(/<div class="card outerfigure">([\s\S]*?)<\/div>\s*<\/div>/g, (m, inner) => {
  const src = /<img src="([^"]+)"[^>]*?(?:width=(\d+))?[^>]*?(?:height=(\d+))?/.exec(inner);
  const cap = /<figcaption[^>]*>([\s\S]*?)<\/figcaption>/.exec(inner);
  if (!src) return '';
  const url = /^https?:/.test(src[1]) ? src[1] : base + src[1];
  const caption = cap ? M.finish(flat(entities(cap[1].replace(/<[^>]+>/g, ''))).replace(/^Figure [\d.]+:\s*/, '')) : '';
  figs.push(url);
  return `\n\n#!figure(src: "${url}", alt: "figure", scale: 0.8)\n${caption}\n#figure!\n\n`;
});

// literate fragments → code blocks
html = html.replace(/<div class="fragmentname">([\s\S]*?)<\/div>\s*<div class="fragmentcode">([\s\S]*?)<\/div>\s*<\/div>/g, (m, name, code) => {
  const text = entities(code.replace(/<div id="fragbit[^"]*"[^>]*>[\s\S]*?<\/div>/g, '')
    .replace(/<br\s*\/?>/g, '\n').replace(/<[^>]+>/g, '')).replace(/\u00a0/g, ' ')  // (code: a no-break space is a space)
    .replace(/>>[ \t]+(?=\S)/g, '>>\n    ');  // expanded sub-fragments start their own line
  const head = entities(name.replace(/<[^>]+>/g, '')).trim();
  return `\n\n\`\`\`cpp\n${head}\n${text.replace(/^\s*\n/, '').trimEnd()}\n\`\`\`\n\n`;
});

// block structure
const out = [];
// a fragment of HTML → its text with the kit's markup placeholders
const inlineText = (s) => flat(entities(s
  .replace(/<em>([\s\S]*?)<\/em>/g, (m, b) => M.em(b)).replace(/<i>([\s\S]*?)<\/i>/g, (m, b) => M.em(b))
  .replace(/<(?:tt|code)>([\s\S]*?)<\/(?:tt|code)>/g, (m, b) => M.code(flat(entities(b.replace(/<[^>]+>/g, '')))))
  .replace(/<b>([\s\S]*?)<\/b>/g, (m, b) => M.strong(b)).replace(/<strong>([\s\S]*?)<\/strong>/g, (m, b) => M.strong(b))
  .replace(/<a [^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/g, (m, h, t) => /^https?:/.test(h) ? M.link(t, entities(h)) : t)
  .replace(/<sup>([\s\S]*?)<\/sup>/g, '^$1')
  .replace(/<[^>]+>/g, '')));
// …as finished .tsm (ctx: escapeTsm's; a paragraph's text starts its line)
const inline = (s, ctx = 'para') => M.finish(inlineText(s), ctx, { lineStart: ctx === 'para' });

const re = /<(h2|h3|h4|p|li|pre|dl)(?:[^>]*)>([\s\S]*?)<\/\1>|```cpp\n[\s\S]*?\n```|#!figure[\s\S]*?#figure!/g;
let m;
while ((m = re.exec(html))) {
  if (m[0].startsWith('```') || m[0].startsWith('#!figure')) { out.push('', m[0], ''); continue; }
  if (m[1] === 'dl') {  // (plan P3-34) a description list
    out.push('', ...descriptionItems(m[2], inline), '');
    continue;
  }
  const tag = m[1];
  if (/^h[234]$/.test(tag)) {  // its section number dropped
    const text = M.finish(inlineText(m[2]).replace(/^[\d.]+\s*/, ''), 'heading', { lineStart: false });
    if (text) out.push('', '='.repeat(+tag[1] - 1) + ' ' + text, '');
    continue;
  }
  const body = inline(m[2]);
  if (!body) continue;
  else if (tag === 'li') out.push('- ' + body);
  else out.push('', body, '');
}
process.stderr.write(`math formulas replaced by titles: ${mathCount}; figures: ${figs.length}\n`);
process.stdout.write(out.join('\n').replace(/\n{3,}/g, '\n\n').trim() + '\n');
