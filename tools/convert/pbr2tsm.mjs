#!/usr/bin/env node
// mmp/pbr-book-website checkout → .tsm, one file per section (batch form of
// html2tsm.mjs, plus MathSpeak recovery). The site ships math as MathJax
// SVG whose <title> holds the MathSpeak reading ("StartFraction 1 Over 4 pi
// EndFraction …") — a structured, mechanically invertible notation. This
// converter parses it back into tsm math instead of italic prose, closing
// the biggest gap from docs/real-world-report.md.
//
//   node tools/convert/pbr2tsm.mjs <repo-root> --out <dir> [--only <substr>] [--mathdebug]
//
// pbr-book.org content is CC BY-NC-ND 4.0: adapted output is for LOCAL,
// PRIVATE use only and must not be shared/committed (see .gitignore).
import { readFileSync, writeFileSync, readdirSync, mkdirSync, statSync } from 'node:fs';
import { join, basename } from 'node:path';

const args = process.argv.slice(2);
const root = args.find((a) => !a.startsWith('--'));
const opt = (k) => (args.includes(k) ? args[args.indexOf(k) + 1] : null);
const outDir = opt('--out') ?? 'pbr-tsm';
const only = opt('--only');
const mathDebug = args.includes('--mathdebug');
if (!root) { console.error('usage: pbr2tsm.mjs <repo-root> --out <dir>'); process.exit(1); }

// ---------------------------------------------------------------------------
// MathSpeak → tsm math
// ---------------------------------------------------------------------------
const GREEK = new Set(['alpha','beta','gamma','delta','epsilon','zeta','eta','theta','iota','kappa','lambda','mu','nu','xi','omicron','pi','rho','sigma','tau','upsilon','phi','chi','psi','omega','Gamma','Delta','Theta','Lambda','Xi','Pi','Sigma','Upsilon','Phi','Psi','Omega']);
const WORD_MAP = {
  'lamda': 'lambda', 'Lamda': 'Lambda',
  'equals': '=', 'plus': '+', 'minus': '-', 'times': 'xx', 'slash': '/',
  'asterisk': '*', 'percent-sign': '%', 'colon': ':', 'semicolon': ';',
  'comma': ',', 'period': '.', 'equal': '=',
  'less-than': '<', 'greater-than': '>',
  'less-than-or-equal-to': '<=', 'greater-than-or-equal-to': '>=',
  'not-equals': '!=', 'almost-equals': 'approx', 'identical-to': 'equiv',
  'proportional-to': 'prop', 'plus-or-minus': '+-', 'minus-or-plus': '-+',
  'left-parenthesis': '(', 'right-parenthesis': ')',
  'left-bracket': '[', 'right-bracket': ']',
  'left-brace': '{', 'right-brace': '}',
  'left-arrow': '<-', 'right-arrow': '->', 'left-right-arrow': '<->',
  'element-of': 'in', 'subset-of': 'subset', 'subset-of-or-equal-to': 'subseteq',
  'union': 'cup', 'intersection': 'cap',
  'infinity': 'oo', 'partial-differential': 'partial', 'empty-set': 'emptyset',
  'dot': 'cdot', 'circled-dot': 'odot', 'circled-plus': 'oplus',
  'circled-times': 'otimes', 'circled-division-slash': 'oslash',
  'vertical-bar': '|', 'double-vertical-bar': '||', 'parallel-to': 'parallel',
  'up-tack': 'bot', 'not-sign': 'not',
  'ellipsis': '...', 'midline-horizontal-ellipsis': 'cdots',
  'vertical-ellipsis': '...', 'down-right-diagonal-ellipsis': '...',
  'sigma-summation': 'sum', 'product': 'prod', 'integral': 'int',
  'cosine': 'cos', 'sine': 'sin', 'tangent': 'tan', 'limit': 'lim',
  'probability': 'Pr', 'floor': 'floor',
  'one-half': '1/2', 'one-third': '1/3', 'one-fourth': '1/4',
  'one-sixth': '1/6', 'one-eighth': '1/8', 'one-sixteenth': '1/16',
  'three-halves': '3/2', 'tilde': '~',
  'ring': 'ring', 'overTilde': 'tilde',
  'and': 'and', 'if': 'if', 'then': 'then', 'otherwise': 'otherwise',
  'for': 'for', 'where': 'where', 'with': 'with', 'while': 'while',
  'forever': 'forever', 'return': 'return', 'loop': 'loop',
  'Blank': '', 'Enlarged': '', 'Determinant': '', 'sup': 'sup',
};
const unknownTokens = new Map();  // token → count (for --mathdebug + report)

function mathspeakToTsm(title) {
  const toks = [];
  {
    const raw = title.trim().split(/\s+/);
    for (let k = 0; k < raw.length; k++) {
      const t = raw[k], n = raw[k + 1];
      if ((t === 'Super' || t === 'Sub') &&
          (n === 'Subscript' || n === 'Superscript' || n === 'Baseline')) continue;
      // a bare Superscript/Subscript followed by a token that cannot start a
      // script argument is a level-RETURN marker in this MathSpeak dialect
      const RET = new Set(['Baseline', 'right-parenthesis', 'right-bracket', 'right-brace',
        'slash', 'equals', 'comma', 'period', 'EndFraction', 'EndEndFraction',
        'Endscripts', 'EndRoot', 'EndAbsoluteValue', 'EndSet', undefined]);
      if ((t === 'Superscript' || t === 'Subscript') && RET.has(n)) continue;
      toks.push(t);
    }
  }
  let i = 0;
  const peek = () => toks[i];
  const next = () => toks[i++];
  const ord = (t) => /^\d+(st|nd|rd|th)$/.test(t);
  const cellMark = () => ord(peek()) && (toks[i + 1] === 'Row' || toks[i + 1] === 'Column');

  // parse a sequence until one of `stops` (not consumed); returns tsm string
  function seq(stops) {
    const out = [];
    while (i < toks.length && !stops.has(peek()) && !cellMark()) {
      const a = atom(stops);
      if (a === '') continue;
      if (a.startsWith('\u0004A:')) {
        if (out.length) out[out.length - 1] = `${a.slice(3)}(${out[out.length - 1]})`;
      } else if (a.startsWith('\u0004') && out.length) out[out.length - 1] += a.slice(1);
      else if (!a.startsWith('\u0004')) out.push(a);
      else out.push(a.slice(1).replace(/^[_^]/, ''));
    }
    return out.filter((s) => s !== '').join(' ');
  }
  const wrap = (s) => (/^[A-Za-z0-9]$|^[a-z]+$/.test(s) && !s.includes(' ') ? s : `(${s})`);
  // x already ending in _(a): a second _b merges to _(a b) (engine rejects double scripts)
  // trailing _(…)/^(…) group of a string, parens balanced; null if absent
  function trailingScript(base) {
    if (!base.endsWith(')')) {
      const m = /([_^])([A-Za-z0-9]+|'+)$/.exec(base);
      return m ? { i: m.index, kind: m[1], inner: m[2] } : null;
    }
    let depth = 0;
    for (let k = base.length - 1; k >= 0; k--) {
      const c = base[k];
      if (c === ')') depth++;
      else if (c === '(') {
        depth--;
        if (depth === 0) {
          if (k > 0 && (base[k - 1] === '_' || base[k - 1] === '^'))
            return { i: k - 1, kind: base[k - 1], inner: base.slice(k + 1, -1) };
          return null;
        }
      }
    }
    return null;
  }

  function trailingMerge(inner, kind, arg) {
    const t = trailingScript(inner);
    if (t && t.kind !== kind) return inner + `${kind}${wrap(arg)}`;   // int_(a) + ^b → int_(a)^b
    return inner + `${kind}${wrap(arg)}`;
  }

  function mergeScript(base, kind, arg) {
    // msubsup always reads sub before sup, so a Subscript AFTER a
    // Superscript can only be nested inside it: x^lambda then _max → x^(lambda_max)
    const t = trailingScript(base);
    if (t && t.kind === '^') {
      // msubsup reads sub first, so any further script after a superscript
      // belongs INSIDE it: e^(-int) then _(z0) or ^(z1) → attach to the int
      return base.slice(0, t.i) + `^(${trailingMerge(t.inner, kind, arg)})`;
    }
    if (t && t.kind === '_' && kind === '_') {
      // duplicated subscript: merge args (tensor indices; engine rejects x_a_b)
      return base.slice(0, t.i) + `_(${t.inner} ${arg})`;
    }
    return base + `${kind}${wrap(arg)}`;
  }

  function scripts(base, stops) {
    // Subscript/Superscript … Baseline (either order, possibly both), or
    // Underscript … Overscript … Endscripts on big operators.
    for (;;) {
      const t = peek();
      if (t === undefined || stops.has(t) || cellMark()) break;  // shared Baseline / cell marker → outer
      if (t === 'Subscript' || t === 'Sub') {
        next();
        const sub = seq(new Set(['Baseline', 'Superscript', 'Endscripts', ...stops]));
        if (peek() === 'Baseline') next();
        if (sub) base = mergeScript(base, '_', sub);
      } else if (t === 'Superscript' || t === 'Super') {
        next();
        const sup = seq(new Set(['Baseline', 'Subscript', 'Endscripts', ...stops]));
        if (peek() === 'Baseline') next();
        if (sup === 'ring') { base += '°'; continue; }         // 45 Superscript ring → degrees
        if (/^'+$/.test(sup)) base += sup;                     // Superscript double-prime → p''
        else if (sup) base = mergeScript(base, '^', sup);
      } else if (t === 'Underscript') {
        next();
        const lo = seq(new Set(['Overscript', 'Endscripts']));
        let hi = '';
        if (peek() === 'Overscript') { next(); hi = seq(new Set(['Endscripts'])); }
        if (peek() === 'Endscripts') next();
        if (lo) base = mergeScript(base, '_', lo);
        if (hi) base = mergeScript(base, '^', hi);
      } else if (t === 'squared') { next(); base += '^2'; }
      else if (t === 'cubed') { next(); base += '^3'; }
      else if (t === 'prime') { next(); base += "'"; }
      else if (t === 'double-prime') { next(); base += "''"; }
      else if (t === 'factorial') { next(); base += '!'; }
      else if (t === 'overbar') { next(); base = `bar(${base})`; }
      else if (t === 'overTilde') { next(); base = `tilde(${base})`; }
      else if (t === 'caret') { next(); base = `hat(${base})`; }
      else if (t === 'bar') { next(); base = `bar(${base})`; }
      else break;
    }
    return base;
  }

  function matrix(stops) {
    // Start M By N Matrix 1st Row [1st Column a 2nd Column b …] … EndMatrix
    // (also StartBinomialOrMatrix … Choose … EndBinomialOrMatrix). tsm has
    // no matrix construct yet — emit [a, b; c, d] as a readable fallback.
    const rows = [];
    let cur = [];
    let cell = [];
    const flushCell = () => { if (cell.length) cur.push(cell.join(' ')); cell = []; };
    const flushRow = () => { flushCell(); if (cur.length) rows.push(cur.join(', ')); cur = []; };
    const ends = new Set(['EndMatrix', 'EndDeterminant', 'EndBinomialOrMatrix', 'EndLayout']);
    while (i < toks.length && !ends.has(peek())) {
      const t = peek();
      if (ord(t) && toks[i + 1] === 'Row') { next(); next(); flushRow(); }
      else if (ord(t) && toks[i + 1] === 'Column') { next(); next(); flushCell(); }
      else if (t === 'Row') { next(); flushRow(); }          // EnlargedRow variants
      else {
        const a = atom(new Set([...ends, 'Row', 'Column']));
        if (a === '') continue;
        if (a.startsWith('\u0004A:')) {
          if (cell.length) cell[cell.length - 1] = `${a.slice(3)}(${cell[cell.length - 1]})`;
        } else if (a.startsWith('\u0004') && cell.length) cell[cell.length - 1] += a.slice(1);
        else if (!a.startsWith('\u0004')) cell.push(a);
        else cell.push(a.slice(1).replace(/^[_^]/, ''));
      }
    }
    if (i < toks.length) next();
    flushRow();
    return `[${rows.join('; ')}]`;
  }

  function atomUpper(stops) {
    let l = next() ?? '';
    l = WORD_MAP[l] ?? l;
    const g = l[0] ? l[0].toUpperCase() + l.slice(1) : l;
    if (GREEK.has(g)) return scripts(g, stops);               // upper lamda → Lambda
    return scripts(l.toUpperCase(), stops);
  }

  function atom(stops) {
    const t = next();
    switch (t) {
      case 'StartFraction': case 'StartStartFraction': {
        const overTok = t === 'StartFraction' ? 'Over' : 'OverOver';
        const endTok = t === 'StartFraction' ? 'EndFraction' : 'EndEndFraction';
        const num = seq(new Set([overTok, endTok]));
        let den = '';
        if (peek() === overTok) { next(); den = seq(new Set([endTok])); }
        if (peek() === endTok) next();
        return scripts(den ? `${wrap(num)}/${wrap(den)}` : wrap(num), stops);
      }
      case 'StartRoot': {
        const b = seq(new Set(['EndRoot'])); if (peek() === 'EndRoot') next();
        return scripts(`sqrt(${b})`, stops);
      }
      case 'StartAbsoluteValue': {
        const b = seq(new Set(['EndAbsoluteValue'])); if (peek() === 'EndAbsoluteValue') next();
        return scripts(`abs(${b})`, stops);
      }
      case 'StartSet': {
        const b = seq(new Set(['EndSet'])); if (peek() === 'EndSet') next();
        return scripts(`{ ${b} }`, stops);
      }
      case 'StartBinomialOrMatrix': {
        const a = seq(new Set(['Choose', 'EndBinomialOrMatrix', 'Row']));
        if (peek() === 'Choose') {
          next();
          const b = seq(new Set(['EndBinomialOrMatrix']));
          if (peek() === 'EndBinomialOrMatrix') next();
          return scripts(`binom(${a}, ${b})`, stops);
        }
        i -= 0; return scripts(a + ' ' + matrix(stops), stops);
      }
      case 'StartLayout': {
        const m = matrix(stops);                                // reuse row/cell walk
        const rows = m.slice(1, -1).split('; ').map((r) => r.replace(/, /g, ' '));
        return scripts(rows.join(' ; '), stops);
      }
      case 'Start': {
        // Start M By N Matrix …
        const dims = [];
        while (i < toks.length && peek() !== 'Matrix' && peek() !== 'Determinant') dims.push(next());
        const kind = next(); // Matrix | Determinant
        const m = matrix(stops);
        return scripts(kind === 'Determinant' ? `abs(${m.slice(1, -1)})` : m, stops);
      }
      case 'left': {
        const kindw = peek();
        if (kindw === 'floor' || kindw === 'ceiling') {
          next();
          const b2 = seq(new Set(['right']));
          if (peek() === 'right') { next(); if (peek() === kindw) next(); }
          return scripts(`${kindw === 'floor' ? 'floor' : 'ceil'}(${b2})`, stops);
        }
        return '';  // bare "left"/"right" grouping words carry no glyph
      }
      case 'right': return '';
      case 'prime': return "'";
      case 'overbar': case 'bar': return '\u0004A:bar';
      case 'overTilde': return '\u0004A:tilde';
      case 'caret': return '\u0004A:hat';
      case 'Superscript': case 'Super': {
        // script with no spoken base (deeply nested constructs): attach to
        // the previous atom via the \u0004 marker, resolved in seq()
        const a2 = seq(new Set(['Baseline', ...stops]));
        if (peek() === 'Baseline') next();
        return a2 ? `\u0004^${wrap(a2)}` : '';
      }
      case 'Subscript': case 'Sub': {
        const a2 = seq(new Set(['Baseline', ...stops]));
        if (peek() === 'Baseline') next();
        return a2 ? `\u0004_${wrap(a2)}` : '';
      }
      case 'ModifyingAbove': case 'ModifyingBelow': {
        const b = seq(new Set(['With']));
        if (peek() === 'With') next();
        const acc = next() ?? '';
        const accMap = { 'caret': 'hat', 'bar': 'bar', 'overbar': 'bar', 'right-arrow': 'vec', 'dot': 'dot', 'overTilde': 'tilde', 'ring': 'ring', 'bottom-brace': '' };
        const fn = accMap[acc] ?? '';
        return scripts(fn ? `${fn}(${b})` : wrap(b), stops);
      }
      case 'negative': return '-' + atom(stops);
      case 'upper': return atomUpper(stops);
      case '__never_upper__': {
        let l = next() ?? '';
        l = WORD_MAP[l] ?? l;
        if (GREEK.has(l) || GREEK.has(l[0].toUpperCase() + l.slice(1))) {
          const g = l[0].toUpperCase() + l.slice(1);           // upper lamda → Lambda
          return scripts(GREEK.has(g) ? g : l, stops);
        }
        return scripts(l.toUpperCase(), stops);
      }
      case 'normal': case 'monospace': {
        // upright: single letters need quoting; multi-letter words are
        // upright automatically (math-design §14)
        let l = peek();
        if (l === 'upper') { next(); return atomUpper(stops); }
        l = next() ?? '';
        if (WORD_MAP[l] !== undefined) return scripts(WORD_MAP[l], stops);
        return scripts(/^[A-Za-z]$/.test(l) ? `"${l}"` : l, stops);
      }
      case 'bold': case 'italic': case 'script': case 'fraktur': {
        // no bold/cal/frak math alphabets yet (documented gap) — plain letter
        return atom(stops);
      }
      case 'arc': {
        const f = atom(stops);
        return 'a' + f;                     // arc cosine → acos
      }
      case 'ln': case 'log': case 'min': case 'max': case 'sup':
        return scripts(t, stops);
      default: {
        if (WORD_MAP[t] !== undefined) return scripts(WORD_MAP[t], stops);
        if (t === 'double-prime') return "''";
        if (/^\d+(st|nd|rd|th)$/.test(t)) return scripts(t, stops);   // stray ordinal → upright word
        if (/^\.[0-9]+$/.test(t)) return scripts('0' + t, stops);     // .09 → 0.09
        if (GREEK.has(t)) return scripts(t, stops);
        if (/^[0-9][0-9,.]*$/.test(t)) return scripts(t.replace(/,/g, ''), stops);
        if (/^[A-Za-z]$/.test(t)) return scripts(t, stops);
        if (/^(Baseline|Endscripts|EndFraction|EndEndFraction|Over|OverOver|EndRoot|EndMatrix|EndLayout|EndSet|EndAbsoluteValue|EndBinomialOrMatrix|EndDeterminant|Row|Column|Matrix|With|Choose|By)$/.test(t)) return '';
        if (/^[A-Za-z][A-Za-z0-9]*$/.test(t)) return scripts(t, stops); // word → upright op
        unknownTokens.set(t, (unknownTokens.get(t) ?? 0) + 1);
        return '';
      }
    }
  }

  let s = seq(new Set([]));
  // drop a trailing sentence-punctuation atom (", " / ".") — prose, not math
  for (let prev = null; prev !== s;) { prev = s; s = s.replace(/"([A-Za-z]+)" "([A-Za-z])"/g, '"$1$2"'); }
  { // balance delimiters: the engine accepts mixed pairs, but not EOF-unclosed
    const OPEN = { '(': ')', '[': ']', '{': '}' };
    const stack = [];
    let out2 = '';
    for (const ch of s) {
      if (OPEN[ch]) { stack.push(OPEN[ch]); out2 += ch; }
      else if (ch === ')' || ch === ']' || ch === '}') {
        if (stack.length) { stack.pop(); out2 += ch; }
        // orphan closer: drop it
      } else out2 += ch;
    }
    while (stack.length) out2 += ' ' + stack.pop();
    s = out2;
  }
  s = s.replace(/[_^]\(\s*\)/g, '');
  s = s.replace(/\u0004A:[a-z]+/g, '');
  s = s.replace(/\u0004[_^]?/g, '');
  s = s.replace(/\(\s*[/*=]\s*/g, '( ');
  s = s.replace(/\s*,\s*;/g, ' ;');
  s = s.replace(/\s*[.,;]$/, '');
  return s.replace(/\s+/g, ' ').trim();
}

// ---------------------------------------------------------------------------
// HTML → tsm (html2tsm.mjs lineage, batch + MathSpeak)
// ---------------------------------------------------------------------------
const entities = (s) => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&')
  .replace(/&quot;/g, '"').replace(/&#39;|&rsquo;/g, '’').replace(/&lsquo;/g, '‘')
  .replace(/&ldquo;/g, '“').replace(/&rdquo;/g, '”').replace(/&nbsp;/g, ' ')
  .replace(/&ndash;/g, '–').replace(/&mdash;/g, '—').replace(/&#(\d+);/g, (m, n) => String.fromCodePoint(+n))
  .replace(/&([a-z]+)acute;/g, (m, c) => ({ e: 'é', a: 'á', o: 'ó', i: 'í', u: 'ú' }[c] ?? m))
  .replace(/&([a-z]+)grave;/g, (m, c) => ({ e: 'è', a: 'à' }[c] ?? m))
  .replace(/&([aou])uml;/g, (m, c) => ({ a: 'ä', o: 'ö', u: 'ü' }[c] ?? m))
  .replace(/&szlig;/g, 'ß').replace(/&ccedil;/g, 'ç').replace(/&hellip;/g, '…');

const M0 = '\u0001', M1 = '\u0002';  // math placeholders survive escaping

function convertPage(html, base, stats) {
  const start = html.indexOf('class="maincontainer"');
  if (start > 0) html = html.slice(start);
  html = html.replace(/<nav[\s\S]*?<\/nav>/g, '').replace(/<script[\s\S]*?<\/script>/g, '')
    .replace(/<footer[\s\S]*?<\/footer>/g, '');

  const maths = [];
  const stash = (t) => { maths.push(t); return `${M0}${maths.length - 1}${M1}`; };

  // display math first (div wrapper), then inline SVG
  html = html.replace(/<div class="displaymath">([\s\S]*?)<\/div>/g, (m, inner) => {
    const parts = [];
    inner.replace(/<svg[^>]*>[\s\S]*?<title[^>]*>([\s\S]*?)<\/title>[\s\S]*?<\/svg>/g, (mm, t) => {
      stats.math++;
      parts.push(mathspeakToTsm(entities(t.trim())));
      return '';
    });
    if (!parts.length) return '';
    return `<p>${stash(`\n\n$ ${parts.join(' ; ')} $\n\n`)}</p>`;
  });
  html = html.replace(/<svg[^>]*>[\s\S]*?<title[^>]*>([\s\S]*?)<\/title>[\s\S]*?<\/svg>/g, (m, t) => {
    stats.math++;
    const s = mathspeakToTsm(entities(t.trim()));
    return stash(s ? `$${s}$` : '');
  });

  // figures
  html = html.replace(/<div class="card outerfigure">([\s\S]*?)<\/div>\s*<\/div>/g, (m, inner) => {
    const src = /<img src="([^"]+)"/.exec(inner);
    const cap = /<figcaption[^>]*>([\s\S]*?)<\/figcaption>/.exec(inner);
    if (!src) return '';
    const url = /^https?:/.test(src[1]) ? src[1] : base + src[1];
    let caption = cap ? cap[1] : '';
    caption = caption.replace(new RegExp(`${M0}(\\d+)${M1}`, 'g'), (mm, n) => maths[+n])
      .replace(/<[^>]+>/g, '');
    caption = entities(caption).replace(/\s+/g, ' ').replace(/^Figure [\d.]+:\s*/, '').trim()
      .replace(/\$/g, '\\$').replace(/#/g, '\\#').replace(/@(?=[A-Za-z\[])/g, '\\@');
    stats.figs++;
    return `\n\n#!figure(src: "${url}", alt: "figure", scale: 0.8)\n${caption}\n#figure!\n\n`;
  });

  // lists ship with unclosed <li> (HTML4 optional end tags): normalize each
  // <ul>/<ol> block into explicitly closed items so the structural regex sees them
  html = html.replace(/<(ul|ol)(?:[^>]*)>([\s\S]*?)<\/\1>/g, (m, tag, body) => {
    const items = body.split(/<li(?:[^>]*)>/).slice(1)
      .map((it) => it.replace(/<\/li>\s*$/, '').trim()).filter(Boolean);
    return '\n' + items.map((it) => `<li>${it}</li>`).join('\n') + '\n';
  });

  // literate fragments → code blocks
  // depth-aware scan: <div class="fragmentname"> / <div class="fragmentcode">
  // pairs and bare fragmentcode divs (their bodies nest fragbit divs)
  {
    const divEnd = (h, open) => {          // index just past the matching </div>
      let d = 0, k = open;
      const re = /<div\b|<\/div>/g;
      re.lastIndex = open;
      for (let m2; (m2 = re.exec(h)); ) {
        d += m2[0] === '</div>' ? -1 : 1;
        if (d === 0) return re.lastIndex;
      }
      return h.length;
    };
    let out2 = '';
    let pos = 0;
    const re = /<div class="fragment(name|code)">/g;
    for (let m2; (m2 = re.exec(html)); ) {
      if (m2.index < pos) continue;        // consumed by a previous pair
      out2 += html.slice(pos, m2.index);
      const aEnd = divEnd(html, m2.index);
      let name = '', code = '', end = aEnd;
      if (m2[1] === 'name') {
        name = html.slice(m2.index, aEnd).replace(/^<div[^>]*>/, '').replace(/<\/div>$/, '');
        const after = /^\s*<div class="fragmentcode">/.exec(html.slice(aEnd));
        if (after) {
          const cStart = aEnd + after[0].length - '<div class="fragmentcode">'.length;
          const cEnd = divEnd(html, cStart);
          code = html.slice(cStart, cEnd).replace(/^<div[^>]*>/, '').replace(/<\/div>$/, '');
          end = cEnd;
        }
      } else {
        code = html.slice(m2.index, aEnd).replace(/^<div[^>]*>/, '').replace(/<\/div>$/, '');
      }
      pos = end;
      re.lastIndex = end;
      const text = entities(code.replace(/<div id="fragbit[^"]*"[^>]*>[\s\S]*?<\/div>/g, '')
        .replace(/<br\s*\/?>/g, '\n').replace(/<[^>]+>/g, ''))
        .replace(/>>[ \t]+(?=\S)/g, '>>\n    ');
      const head = entities(name.replace(/<[^>]+>/g, '')).trim();
      const bodyTxt = (head ? head + '\n' : '') + text.replace(/^\s*\n/, '').trimEnd();
      if (!bodyTxt.trim()) continue;
      stats.frags++;
      out2 += `\n\n\`\`\`cpp\n${bodyTxt}\n\`\`\`\n\n`;
    }
    html = out2 + html.slice(pos);
  }



  const inline = (s) => entities(s
    .replace(/<em>([\s\S]*?)<\/em>/g, '_$1_').replace(/<i>([\s\S]*?)<\/i>/g, '_$1_')
    .replace(/<(?:tt|code)>([\s\S]*?)<\/(?:tt|code)>/g, '`$1`')
    .replace(/<b>([\s\S]*?)<\/b>/g, '*$1*').replace(/<strong>([\s\S]*?)<\/strong>/g, '*$1*')
    .replace(/<a [^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/g, (m, h, t) => /^https?:/.test(h) ? `[${t}](${h})` : t)
    .replace(/<sup>([\s\S]*?)<\/sup>/g, '^$1')
    .replace(/<[^>]+>/g, '')).replace(/\s+/g, ' ').trim()
    .replace(/\$/g, '\\$').replace(/#/g, '\\#').replace(/@(?=[A-Za-z\[])/g, '\\@');

  const out = [];
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

  let text = out.join('\n')
    .replace(new RegExp(`${M0}(\\d+)${M1}`, 'g'), (mm, n) => maths[+n])
    .replace(/\n{3,}/g, '\n\n')
    .replace(/[ \t]+$/gm, '');
  return text.trim() + '\n';
}

// ---------------------------------------------------------------------------
// batch drive
// ---------------------------------------------------------------------------
mkdirSync(outDir, { recursive: true });
const chapters = readdirSync(join(root, '4ed')).filter((d) => {
  try { return statSync(join(root, '4ed', d)).isDirectory(); } catch { return false; }
}).sort();

const index = [];
for (const ch of chapters) {
  const dir = join(root, '4ed', ch);
  const files = readdirSync(dir).filter((f) => f.endsWith('.html')).sort()
    .map((f) => ({ dir, f, rel: `${ch}/${f}`, out: join(outDir, ch, basename(f, '.html') + '.tsm') }));
  // the chapter overview page (4ed/<Chapter>.html) carries real prose too
  try {
    statSync(join(root, '4ed', ch + '.html'));
    files.unshift({ dir: join(root, '4ed'), f: ch + '.html', rel: `${ch}.html`,
      base: 'https://pbr-book.org/4ed/',
      out: join(outDir, ch, '00_Chapter_Overview.tsm') });
  } catch {}
  for (const { dir: fdir, f, rel, out: outFile, base: baseOverride } of files) {
    if (only && !rel.includes(only)) continue;
    const stats = { math: 0, figs: 0, frags: 0 };
    const html = readFileSync(join(fdir, f), 'utf8');
    const base = baseOverride ?? `https://pbr-book.org/4ed/${encodeURI(ch)}/`;
    let tsm = convertPage(html, base, stats);
    tsm = `// Source: https://pbr-book.org/4ed/${encodeURI(ch)}/${encodeURI(basename(f, '.html'))}\n` +
      `// © Matt Pharr, Wenzel Jakob, Greg Humphreys — CC BY-NC-ND 4.0.\n` +
      `// Local private adaptation; do not redistribute.\n\n` + tsm;
    mkdirSync(join(outDir, ch), { recursive: true });
    writeFileSync(outFile, tsm);
    index.push({ file: outFile.slice(outDir.length + 1), chars: tsm.length, ...stats });
  }
}

writeFileSync(join(outDir, 'INDEX.json'), JSON.stringify(index, null, 1));
const totals = index.reduce((a, r) => ({ chars: a.chars + r.chars, math: a.math + r.math, figs: a.figs + r.figs, frags: a.frags + r.frags }), { chars: 0, math: 0, figs: 0, frags: 0 });
console.log(`${index.length} sections, ${(totals.chars / 1e6).toFixed(2)}M chars, ${totals.math} formulas, ${totals.figs} figures, ${totals.frags} fragments`);
if (unknownTokens.size) {
  const sorted = [...unknownTokens.entries()].sort((a, b) => b[1] - a[1]);
  writeFileSync(join(outDir, 'unknown-tokens.txt'), sorted.map(([t, c]) => `${c}\t${t}`).join('\n') + '\n');
  console.log(`unknown MathSpeak tokens: ${unknownTokens.size} kinds (${sorted.slice(0, 10).map(([t, c]) => `${t}×${c}`).join(', ')}${sorted.length > 10 ? ', …' : ''}) → unknown-tokens.txt`);
  if (mathDebug) for (const [t, c] of sorted) console.log(`  ${c}\t${t}`);
}
