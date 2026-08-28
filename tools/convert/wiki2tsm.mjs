#!/usr/bin/env node
// Wikipedia wikitext → .tsm (real-world corpus, docs/real-world-report.md).
// A pragmatic subset: headings, bold/italic, links (kept as text; external
// links as [text](url)), <ref> → footnotes ^[…], lists, {{Main}} → 参见/See
// also line, thumbnails → #!figure via Commons FilePath, other templates
// and tables dropped. Not a MediaWiki parser — enough to typeset prose.
//
//   node tools/convert/wiki2tsm.mjs page.wikitext [--lang zh] > page.tsm
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const file = args.find((a) => !a.startsWith('--'));
const lang = args.includes('--lang') ? args[args.indexOf('--lang') + 1] : 'en';
let src = readFileSync(file, 'utf8');

// --- strip: comments, opaque tags, tables, templates (balanced) ----------
src = src.replace(/<!--[\s\S]*?-->/g, '');
src = src.replace(/\{\|[\s\S]*?\|\}/g, '');
src = src.replace(/<(gallery|math|timeline|imagemap)[\s\S]*?<\/\1>/g, '');
const balanced = (s, open, close, handle) => {
  let out = '';
  let i = 0;
  while (i < s.length) {
    if (s.startsWith(open, i)) {
      let depth = 0, j = i;
      for (; j < s.length; j++) {
        if (s.startsWith(open, j)) { depth++; j += open.length - 1; }
        else if (s.startsWith(close, j)) { depth--; j += close.length - 1; if (depth === 0) { j++; break; } }
      }
      out += handle(s.slice(i + open.length, j - close.length));
      i = j;
    } else out += s[i++];
  }
  return out;
};
src = balanced(src, '{{', '}}', (inner) => {
  const [name, ...rest] = inner.split('|');
  const n = name.trim().toLowerCase();
  if (n === 'main' || n === 'see also' || n === '主条目' || n === '参见' || n === '主條目')
    return `\n${lang === 'zh' ? '参见：' : 'See also: '}${rest.join('、')}\n`;
  if (n === 'lang' && rest.length >= 2) return rest[1];
  if (n === 'nowrap' || n === 'notatypo') return rest.join('');
  return '';
});

// --- refs → footnotes; named refs reuse the first body --------------------
const refBodies = new Map();
const cleanRef = (t) => t.replace(/\[\[([^\]|]*\|)?([^\]]*)\]\]/g, '$2')
  .replace(/'''?/g, '').replace(/<[^>]+>/g, '').replace(/\s+/g, ' ').replace(/[\[\]]/g, '').trim();
src = src.replace(/<ref([^>/]*)\/>/g, (m, a) => {
  const name = /name\s*=\s*"?([^">]+)"?/.exec(a)?.[1]?.trim();
  const body = name && refBodies.get(name);
  return body ? `^[${body}]` : '';
});
src = src.replace(/<ref([^>]*)>([\s\S]*?)<\/ref>/g, (m, a, body) => {
  const name = /name\s*=\s*"?([^">]+)"?/.exec(a)?.[1]?.trim();
  const b = cleanRef(body);
  if (name) refBodies.set(name, b);
  return b ? `^[${b}]` : '';
});

// --- images → figures (balanced: captions nest [[links]]) ------------------
const isFile = (s) => /^\[\[(Image|File|文件|圖像|图像|檔案):/i.test(s);
{
  let out = '';
  let i = 0;
  while (i < src.length) {
    if (src.startsWith('[[', i) && isFile(src.slice(i, i + 12))) {
      let depth = 0, j = i;
      for (; j < src.length; j++) {
        if (src.startsWith('[[', j)) { depth++; j++; }
        else if (src.startsWith(']]', j)) { depth--; j++; if (depth === 0) { j++; break; } }
      }
      const inner = src.slice(i + 2, j - 2);
      const parts = [];
      let d = 0, cur = '';
      for (const ch of inner) {  // split on top-level '|'
        if (ch === '[') d++;
        if (ch === ']') d--;
        if (ch === '|' && d === 0) { parts.push(cur); cur = ''; } else cur += ch;
      }
      parts.push(cur);
      const name = parts[0].replace(/^[^:]*:/, '').trim();
      const caption = parts.slice(1)
        .filter((p) => !/^(thumb|thumbnail|right|left|center|none|upright(=[\d.]+)?|\d+px|frame|frameless|border|bottom|top|alt=.*|link=.*)$/i.test(p.trim()))
        .join('|').replace(/\[\[([^\]|]*\|)?([^\]]*)\]\]/g, '$2').replace(/'''?/g, '');
      const enc = encodeURIComponent(name.replace(/ /g, '_'));
      const url = `https://commons.wikimedia.org/wiki/Special:FilePath/${enc}?width=800`;
      out += `\n\n#!figure(src: "${url}", alt: "${name.replace(/"/g, '')}", scale: 0.45, float: "right")\n${caption.trim() || name}\n#figure!\n\n`;
      i = j;
    } else out += src[i++];
  }
  src = out;
}

// --- inline markup ---------------------------------------------------------
const inline = (t) => t
  .replace(/\[\[([^\]|]+)\|([^\]]+)\]\]/g, '$2')
  .replace(/\[\[([^\]]+)\]\]/g, '$1')
  .replace(/\[(https?:[^\s\]]+) ([^\]]+)\]/g, '[$2]($1)')
  .replace(/\[(https?:[^\s\]]+)\]/g, '$1')
  .replace(/'''''([^']+)'''''/g, '*_$1_*')
  .replace(/'''([^']+)'''/g, '*$1*')
  .replace(/''([^']+)''/g, '_$1_')
  .replace(/<br\s*\/?>/gi, ' ')
  .replace(/<\/?(span|small|sup|sub|i|b|u|s|code|nowiki|abbr|cite|em|strong|div)[^>]*>/gi, '')
  .replace(/&nbsp;/g, ' ').replace(/&ndash;/g, '–').replace(/&mdash;/g, '—').replace(/&amp;/g, '&')
  .replace(/\$/g, '\\$').replace(/#/g, '\\#').replace(/@(?=[A-Za-z\[])/g, '\\@');  // ^[…] here are OUR footnotes (from <ref>)

// --- block structure -------------------------------------------------------
const STOP = /^(references|see also|external links|notes|further reading|参考文献|参见|外部链接|注释|參考資料|外部連結|參見|延伸阅读)$/i;
const out = [];
for (const line of src.split('\n')) {
  const h = /^(=+)\s*(.*?)\s*\1\s*$/.exec(line);
  if (h) {
    const title = inline(h[2]).replace(/<[^>]+>/g, '').trim();
    if (STOP.test(title)) break;
    out.push('', '='.repeat(Math.max(1, h[1].length - 1)) + ' ' + title, '');
    continue;
  }
  if (/^#!figure|^#figure!/.test(line)) { out.push(line); continue; }
  if (/^\*+\s*/.test(line)) { out.push('- ' + inline(line.replace(/^\*+\s*/, ''))); continue; }
  if (/^#+\s*/.test(line)) { out.push('+ ' + inline(line.replace(/^#+\s*/, ''))); continue; }
  if (/^[:;]/.test(line)) { out.push(inline(line.replace(/^[:;]+\s*/, ''))); continue; }
  if (/^\s*$/.test(line)) { out.push(''); continue; }
  out.push(inline(line));
}
const text = out.join('\n').replace(/<[^>]+>/g, '').replace(/\n{3,}/g, '\n\n');
process.stdout.write(text.trim() + '\n');
