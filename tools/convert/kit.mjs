// The converters' shared kit (plan P3-35; design T1 S12): .tsm text that
// parses as what the source said.
//   - decodeEntities: the HTML named and numeric character references, by
//     the WHATWG table (entities.gen.mjs);
//   - Markup: markup is built as placeholders while a converter assembles
//     its text, the text is escaped by the printer's escapeTsm, then the
//     markup goes in — no character of the text is markup by accident (x_i,
//     a stray `*`, `[`), none of the markup is escaped;
//   - em / strong are resolved with their final neighbours: a marker that
//     would be text there (between letters or digits, beside a blank) is
//     the function form #em[…] / #strong[…];
//   - description items (`/ term: description`, plan P3-34).
import { escapeTsm } from '../../runtime/src/shared/tsm-print.mjs';
import { ENTITIES, LEGACY } from './entities.gen.mjs';

// ---- entities ---------------------------------------------------------------
// the C1 controls a numeric reference names, as browsers read them (HTML §13.2.5.80)
const C1 = { 0x80: 0x20ac, 0x82: 0x201a, 0x83: 0x0192, 0x84: 0x201e, 0x85: 0x2026, 0x86: 0x2020, 0x87: 0x2021,
  0x88: 0x02c6, 0x89: 0x2030, 0x8a: 0x0160, 0x8b: 0x2039, 0x8c: 0x0152, 0x8e: 0x017d, 0x91: 0x2018, 0x92: 0x2019,
  0x93: 0x201c, 0x94: 0x201d, 0x95: 0x2022, 0x96: 0x2013, 0x97: 0x2014, 0x98: 0x02dc, 0x99: 0x2122, 0x9a: 0x0161,
  0x9b: 0x203a, 0x9c: 0x0153, 0x9e: 0x017e, 0x9f: 0x0178 };
const numeric = (n) => {
  if (!Number.isFinite(n) || n === 0 || n > 0x10ffff || (n >= 0xd800 && n <= 0xdfff)) return '�';
  return String.fromCodePoint(C1[n] ?? n);
};
export function decodeEntities(s) {
  return s.replace(/&(#[xX][0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);?/g, (m, body) => {
    if (body[0] === '#') return numeric(body[1] === 'x' || body[1] === 'X' ? parseInt(body.slice(2), 16) : parseInt(body.slice(1), 10));
    if (m.endsWith(';') && Object.hasOwn(ENTITIES, body)) return ENTITIES[body];
    // without ';': the longest legacy name it starts with (&ampx → &x)
    for (let k = body.length; k > 1; k--) {
      const name = body.slice(0, k);
      if (LEGACY.has(name) && Object.hasOwn(ENTITIES, name)) return ENTITIES[name] + m.slice(1 + k);
    }
    return m;
  });
}

// ---- deferred markup --------------------------------------------------------
const EM = ['', ''];
const STRONG = ['', ''];
const STASH = ['', ''];
const LINK = ['', '', ''];
const NOTE = ['', ''];

const alnum = (c) => c !== undefined && /[A-Za-z0-9]/.test(c);
// a code span's markup: a backtick run longer than any inside it, padded
// when the code starts or ends with a backtick or a blank
export const codeSpan = (text) => {
  let run = 1;
  for (const m of text.matchAll(/`+/g)) if (m[0].length >= run) run = m[0].length + 1;
  const ticks = '`'.repeat(run);
  const pad = /^[ `]|[ `]$/.test(text) ? ' ' : '';
  return ticks + pad + text + pad + ticks;
};

export class Markup {
  constructor() { this.stash = []; }
  // markup as written (a formula, a reference, a figure block): placed as is
  raw(tsm) {
    this.stash.push(tsm);
    return STASH[0] + (this.stash.length - 1) + STASH[1];
  }
  code(text) { return this.raw(codeSpan(text)); }
  em(body) { return EM[0] + body + EM[1]; }
  strong(body) { return STRONG[0] + body + STRONG[1]; }
  // a link: its text (escaped with the rest), its target as written
  link(body, url) { return LINK[0] + body + LINK[1] + this.raw(url) + LINK[2]; }
  // a footnote ^[…]: its body escaped with the rest
  note(body) { return NOTE[0] + body + NOTE[1]; }

  // text with markup → .tsm: the text escaped (ctx, lineStart: escapeTsm's),
  // the stash, links and notes in place, emphasis last (its neighbours final)
  finish(s, ctx = 'para', { lineStart = true } = {}) {
    let out = escapeTsm(s, ctx, { lineStart });
    out = out.replace(new RegExp(`${STASH[0]}(\\d+)${STASH[1]}`, 'g'), (m, n) => this.stash[+n]);
    out = out.replace(new RegExp(`${LINK[0]}([^${LINK[0]}${LINK[1]}${LINK[2]}]*)${LINK[1]}([^${LINK[2]}]*)${LINK[2]}`, 'g'),
      (m, body, url) => `[${body}](${url})`);
    out = out.replace(new RegExp(`${NOTE[0]}([^${NOTE[0]}${NOTE[1]}]*)${NOTE[1]}`, 'g'), (m, body) => `^[${body}]`);
    return markers(out);
  }
}

// the emphasis marks → _x_ / *x*, or the function form where a marker would
// be text (a letter or digit outside it, a blank inside it)
export function markers(s) {
  const pair = /([])([^-]*)([])/;  // innermost first
  for (let m = pair.exec(s); m; m = pair.exec(s)) {
    const isStrong = m[1] === STRONG[0];
    const body = m[2];
    const before = s[m.index - 1], after = s[m.index + m[0].length];
    let rep;
    if (!body.trim()) rep = body;
    else if (alnum(before) || alnum(after) || /^\s|\s$/.test(body)) rep = `#${isStrong ? 'strong' : 'em'}[${body}]`;
    else rep = (isStrong ? '*' : '_') + body + (isStrong ? '*' : '_');
    s = s.slice(0, m.index) + rep + s.slice(m.index + m[0].length);
  }
  return s.replace(/[-]/g, '');  // (an unpaired mark)
}

// ---- description lists (plan P3-34) -------------------------------------------
// a description item's term: a colon that would end it escaped
export const termText = (s) => s.replace(/:(?=\s|$)/g, '\\:');

// an HTML <dl>'s body as description items, `/ term: definition` (a term's
// further definitions continue its item); `inline` converts a fragment of
// HTML to finished .tsm text
export function descriptionItems(body, inline) {
  const out = [];
  for (const m of body.matchAll(/<(dt|dd)(?:\s[^>]*)?>([\s\S]*?)(?=<\/?(?:dt|dd|dl)\b|$)/g)) {
    const text = inline(m[2].replace(/<\/(?:dt|dd)>\s*$/, ''), m[1] === 'dt' ? 'term' : 'para');
    if (!text) continue;
    if (m[1] === 'dt') out.push(`/ ${text}:`);
    else if (out.length) out[out.length - 1] += /:$/.test(out.at(-1)) ? ' ' + text : '\n  ' + text;
  }
  return out;
}
