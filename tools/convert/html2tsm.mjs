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

const args = process.argv.slice(2);
const file = args.find((a) => !a.startsWith('--'));
const base = args.includes('--base') ? args[args.indexOf('--base') + 1] : '';
let html = readFileSync(file, 'utf8');

const entities = (s) => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&')
  .replace(/&quot;/g, '"').replace(/&#39;|&rsquo;/g, '’').replace(/&lsquo;/g, '‘')
  .replace(/&ldquo;/g, '“').replace(/&rdquo;/g, '”').replace(/&nbsp;/g, ' ')
  .replace(/&ndash;/g, '–').replace(/&mdash;/g, '—').replace(/&#(\d+);/g, (m, n) => String.fromCodePoint(+n));

// main column only
const start = html.indexOf('class="maincontainer"');
if (start > 0) html = html.slice(start);
html = html.replace(/<nav[\s\S]*?<\/nav>/g, '').replace(/<script[\s\S]*?<\/script>/g, '')
  .replace(/<footer[\s\S]*?<\/footer>/g, '');

let mathCount = 0;
// MathJax SVG → italic title text
html = html.replace(/<svg[^>]*>[\s\S]*?<title[^>]*>([\s\S]*?)<\/title>[\s\S]*?<\/svg>/g, (m, t) => {
  mathCount++;
  return `<em>${entities(t.trim())}</em>`;
});
html = html.replace(/<div class="displaymath">([\s\S]*?)<\/div>/g, '<p>$1</p>');

// figures
const figs = [];
html = html.replace(/<div class="card outerfigure">([\s\S]*?)<\/div>\s*<\/div>/g, (m, inner) => {
  const src = /<img src="([^"]+)"[^>]*?(?:width=(\d+))?[^>]*?(?:height=(\d+))?/.exec(inner);
  const cap = /<figcaption[^>]*>([\s\S]*?)<\/figcaption>/.exec(inner);
  if (!src) return '';
  const url = /^https?:/.test(src[1]) ? src[1] : base + src[1];
  const caption = cap ? entities(cap[1].replace(/<[^>]+>/g, '')).replace(/\s+/g, ' ').replace(/^Figure [\d.]+:\s*/, '').trim() : '';
  figs.push(url);
  return `\n\n#!figure(src: "${url}", alt: "figure", scale: 0.8)\n${caption}\n#figure!\n\n`;
});

// literate fragments → code blocks
html = html.replace(/<div class="fragmentname">([\s\S]*?)<\/div>\s*<div class="fragmentcode">([\s\S]*?)<\/div>\s*<\/div>/g, (m, name, code) => {
  const text = entities(code.replace(/<div id="fragbit[^"]*"[^>]*>[\s\S]*?<\/div>/g, '')
    .replace(/<br\s*\/?>/g, '\n').replace(/<[^>]+>/g, ''))
    .replace(/>>[ \t]+(?=\S)/g, '>>\n    ');  // expanded sub-fragments start their own line
  const head = entities(name.replace(/<[^>]+>/g, '')).trim();
  return `\n\n\`\`\`cpp\n${head}\n${text.replace(/^\s*\n/, '').trimEnd()}\n\`\`\`\n\n`;
});

// block structure
const out = [];
const inline = (s) => entities(s
  .replace(/<em>([\s\S]*?)<\/em>/g, '_$1_').replace(/<i>([\s\S]*?)<\/i>/g, '_$1_')
  .replace(/<(?:tt|code)>([\s\S]*?)<\/(?:tt|code)>/g, '`$1`')
  .replace(/<b>([\s\S]*?)<\/b>/g, '*$1*').replace(/<strong>([\s\S]*?)<\/strong>/g, '*$1*')
  .replace(/<a [^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/g, (m, h, t) => /^https?:/.test(h) ? `[${t}](${h})` : t)
  .replace(/<sup>([\s\S]*?)<\/sup>/g, '^$1')
  .replace(/<[^>]+>/g, '')).replace(/\s+/g, ' ').trim()
  .replace(/\$/g, '\\$').replace(/#/g, '\\#').replace(/@(?=[A-Za-z\[])/g, '\\@');

const re = /<(h2|h3|h4|p|li|pre)(?:[^>]*)>([\s\S]*?)<\/\1>|```cpp\n[\s\S]*?\n```|#!figure[\s\S]*?#figure!/g;
let m;
while ((m = re.exec(html))) {
  if (m[0].startsWith('```') || m[0].startsWith('#!figure')) { out.push('', m[0], ''); continue; }
  const tag = m[1], body = inline(m[2]);
  if (!body) continue;
  if (tag === 'h2') out.push('', '= ' + body.replace(/^[\d.]+\s*/, ''), '');
  else if (tag === 'h3') out.push('', '== ' + body.replace(/^[\d.]+\s*/, ''), '');
  else if (tag === 'h4') out.push('', '=== ' + body, '');
  else if (tag === 'li') out.push('- ' + body);
  else out.push('', body, '');
}
process.stderr.write(`math formulas replaced by titles: ${mathCount}; figures: ${figs.length}\n`);
process.stdout.write(out.join('\n').replace(/\n{3,}/g, '\n\n').trim() + '\n');
