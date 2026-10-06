#!/usr/bin/env node
// Source spans (plan P4-03; design T5 per-item spans, document-model §9.1):
// every typeset html golden's runs and lines point at their source. For each
// fixture X.tsm with a test/golden/X.html.txt (not one declared by a
// X.tree.json, whose spans are its own):
// - a content run's data-s (relative to its block's data-s0) is the byte of
//   its first character in X.tsm — or of the backslash escaping it;
// - within a line, runs are in source order and inside the line's
//   [data-s, data-e);
// - a block's lines of one track are in source order.
// Exits 1 on a finding. Offsets are UTF-8 bytes.
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(fileURLToPath(new URL('.', import.meta.url)), '..');
const fixtures = join(root, 'test/fixtures');
const golden = process.env.SPANS_GOLDEN ?? join(root, 'test/golden');

const decode = (s) => s.replace(/&(amp|lt|gt|quot|#39);/g, (m, e) => ({ amp: '&', lt: '<', gt: '>', quot: '"', '#39': "'" })[e]);
const attr = (tag, name) => {
  const m = tag.match(new RegExp(`\\s${name}="([^"]*)"`));
  return m ? m[1] : null;
};

function* walk(dir) {
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* walk(p);
    else if (p.endsWith('.tsm')) yield p;
  }
}

let checked = 0, runs = 0;
const findings = [];
for (const tsm of walk(fixtures)) {
  const name = relative(fixtures, tsm).replace(/\.tsm$/, '');
  const html = join(golden, `${name}.html.txt`);
  if (!existsSync(html) || existsSync(tsm.replace(/\.tsm$/, '.tree.json'))) continue;
  const src = readFileSync(tsm);
  const text = readFileSync(html, 'utf8');
  checked++;
  const bad = (msg) => findings.push(`${name}: ${msg}`);
  let s0 = 0, lastLine = new Map();  // track → its last line's data-s
  for (const m of text.matchAll(/<div class="tsr-para"[^>]*>|<div class="tsr-line[^"]*"[^>]*>(.*?)<\/div>$/gm)) {
    const tag = m[0].slice(0, m[0].indexOf('>') + 1);
    if (tag.startsWith('<div class="tsr-para"')) {
      s0 = Number(attr(tag, 'data-s0') ?? 0);
      lastLine = new Map();
      continue;
    }
    const ls = attr(tag, 'data-s'), le = attr(tag, 'data-e');
    if (ls === null) continue;
    const lineS = s0 + Number(ls), lineE = s0 + Number(le);
    // one stream's lines are in source order: a line continues the one
    // above it when that one's join is space or none (para, tab, row or
    // none at all end a unit; made content — a bibliography — has its own order)
    // (a line of made text alone — a note's backlink — has no place of its own)
    const track = attr(tag, 'data-track') ?? '';
    const above = lastLine.get(track);
    const content = /<(span|a) class="tsr-r[^"]*"(?![^>]*data-syn)[^>]*data-s=/.test(m[1] ?? '');
    if (content && above && lineS < above.s && (above.join === 'space' || above.join === 'none'))
      bad(`line @${lineS} before the line @${above.s} it continues`);
    if (content) lastLine.set(track, { s: lineS, join: attr(tag, 'data-join') });
    let prev = -1;
    const lineRuns = [];
    for (const r of (m[1] ?? '').matchAll(/<(span|a) class="tsr-r[^"]*"[^>]*>([^<]*)<\/\1>/g)) {
      const rt = r[0].slice(0, r[0].indexOf('>') + 1);
      const ds = attr(rt, 'data-s');
      if (ds === null || attr(rt, 'data-syn') !== null) continue;
      lineRuns.push({ at: s0 + Number(ds), t: decode(r[2]) });
    }
    for (let k = 0; k < lineRuns.length; k++) {
      const { at, t } = lineRuns[k];
      runs++;
      // its first character there (a space: one there, or — a space the
      // parser inserted, which has no source — its first non-space); or the
      // markup it was made from (a reference's text, a call's result)
      const lead = t.match(/^[ \t\n]*/)[0].length;  // (ASCII: an ideographic space is a character)
      const first = t.codePointAt(lead);
      const at1 = (b) => src[at] === b;
      if (first === undefined) continue;
      const ch = Buffer.from(String.fromCodePoint(first));
      const here = src.subarray(at, at + ch.length);
      const escaped = src[at] === 0x5c && src.subarray(at + 1, at + 1 + ch.length).equals(ch);
      const space = lead > 0 && (at1(0x20) || at1(0x0a) || at1(0x09));
      // made text carries its node's span: a reference's or call's at its
      // markup (@ # $ ^ [ `, a heading's =), a generated prefix (a caption's
      // 图 1：) where the content it precedes starts
      const made = '@#$^[`=-+/>|'.includes(String.fromCharCode(src[at])) ||
                   lineRuns.some((o, j) => j !== k && o.at === at);
      // text code changed but kept in place (a fragment upper-cased): its
      // characters still stand where the source's do
      const recased = first < 0x80 && String.fromCharCode(src[at]).toLowerCase() === String.fromCodePoint(first).toLowerCase();
      if (!here.equals(ch) && !escaped && !space && !made && !recased)
        bad(`run "${t.slice(0, 12)}" @${at}: the source there is "${src.subarray(at, at + 8).toString()}"`);
      if (at < prev) bad(`run @${at} before the run @${prev} on its line`);
      if (at < lineS || at >= lineE) bad(`run @${at} outside its line [${lineS},${lineE})`);
      prev = at;
    }
  }
}
console.log(`check-spans: ${checked} fixtures, ${runs} runs, ${findings.length} findings`);
for (const f of findings.slice(0, Number(process.env.SPANS_SHOW ?? 40))) console.log(`  ${f}`);
process.exit(findings.length ? 1 : 0);
