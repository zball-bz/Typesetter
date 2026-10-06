#!/usr/bin/env node
// HoTT-book LaTeX → .tsm (real-world corpus, docs/real-world-report.md).
// A deliberately small subset: sectioning, emphasis, \cite → @key,
// \cref → @label, \footnote → ^[…], itemize/enumerate, ``quotes'', ---,
// \index dropped, a macro table for the book's own commands, and a
// LaTeX-math → Typst-math translation for the common operators. The bib
// converts to CSL-JSON (bib2csl). Unknown macros are kept, marked ⟨\name⟩,
// so they show up in the report as gaps rather than vanishing silently.
//
//   node tools/convert/tex2tsm.mjs chapter.tex [--bib refs.bib --bib-out refs.json]
import { readFileSync, writeFileSync } from 'node:fs';
import { TEX_MATH } from '../../runtime/src/shared/math-vocab.gen.mjs';
import { em, strong, markers, termText } from './prose.mjs';

const args = process.argv.slice(2);
const file = args.find((a) => !a.startsWith('--'));
const opt = (k) => (args.includes(k) ? args[args.indexOf(k) + 1] : null);
let src = readFileSync(file, 'utf8');

// keys: bib keys / labels contain ':' '.' — not @ref identifier chars
const safeKey = (k) => k.trim().replace(/[^A-Za-z0-9_-]+/g, '-');

// ---- bib → CSL-JSON -------------------------------------------------------
function bib2csl(text) {
  const out = [];
  const re = /@(\w+)\s*\{\s*([^,\s]+)\s*,/g;
  let m;
  while ((m = re.exec(text))) {
    const type = m[1].toLowerCase();
    const id = m[2];
    let i = re.lastIndex, depth = 1;
    for (; i < text.length && depth; i++) {
      if (text[i] === '{') depth++;
      else if (text[i] === '}') depth--;
    }
    const body = text.slice(re.lastIndex, i - 1);
    re.lastIndex = i;
    const fields = {};
    const fre = /(\w+)\s*=\s*(\{((?:[^{}]|\{[^{}]*\})*)\}|"([^"]*)"|(\w+))/g;
    let f;
    while ((f = fre.exec(body)))
      fields[f[1].toLowerCase()] = (f[3] ?? f[4] ?? f[5]).replace(/[{}]/g, '').replace(/\s+/g, ' ').trim();
    const people = (s) => s ? s.split(/\s+and\s+/).map((p) => {
      if (p.includes(',')) {
        const [family, given] = p.split(',').map((x) => x.trim());
        return { family, given };
      }
      const w = p.trim().split(' ');
      return { family: w.at(-1), given: w.slice(0, -1).join(' ') };
    }) : undefined;
    const TYPES = { article: 'article-journal', book: 'book', inproceedings: 'paper-conference',
                    phdthesis: 'thesis', misc: 'document', unpublished: 'manuscript',
                    incollection: 'chapter', techreport: 'report' };
    const e = { id: safeKey(id), type: TYPES[type] ?? 'document' };
    if (fields.author) e.author = people(fields.author);
    if (fields.editor) e.editor = people(fields.editor);
    if (fields.title) e.title = fields.title;
    if (fields.journal || fields.booktitle) e['container-title'] = fields.journal ?? fields.booktitle;
    if (fields.volume) e.volume = fields.volume;
    if (fields.number) e.issue = fields.number;
    if (fields.pages) e.page = fields.pages.replace(/--/g, '-');
    if (fields.publisher) e.publisher = fields.publisher;
    if (fields.school) e.publisher = fields.school;
    if (fields.year) e.issued = { 'date-parts': [[Number(fields.year)]] };
    if (fields.doi) e.DOI = fields.doi;
    if (fields.url) e.URL = fields.url;
    out.push(e);
  }
  return out;
}
if (opt('--bib'))
  writeFileSync(opt('--bib-out') ?? 'refs.json',
    JSON.stringify(bib2csl(readFileSync(opt('--bib'), 'utf8')), null, 1) + '\n');

// ---- the book's macros (macros.tex subset) ----------------------------------
const MACROS = [
  [/\\UU\b/g, '\\mathcal{U}'], [/\\unit\b/g, '\\mathbf{1}'], [/\\emptyt\b/g, '\\mathbf{0}'],
  [/\\eqv\{([^{}]*)\}\{([^{}]*)\}/g, '$1 \\simeq $2'],
  [/\\texteqv\{([^{}]*)\}\{([^{}]*)\}/g, '\\mathsf{Equiv}($1,$2)'],
  [/\\idtype\[([^\]]*)\]\{([^{}]*)\}\{([^{}]*)\}/g, '\\mathsf{Id}_{$1}($2,$3)'],
  [/\\idtype\{([^{}]*)\}\{([^{}]*)\}/g, '\\mathsf{Id}($1,$2)'],
  [/\\idtypevar\{([^{}]*)\}/g, '\\mathsf{Id}_{$1}'],
  [/\\LEM\{([^{}]*)\}/g, '\\mathsf{LEM}_{$1}'], [/\\choice\{([^{}]*)\}/g, '\\mathsf{AC}_{$1}'],
  [/\\pairr\{([^{}]*)\}/g, '($1)'], [/\\ct\b/g, '\\cdot'], [/\\leadsto/g, '\\to'],
  [/\\tprd\{([^{}]*)\}/g, '\\Pi_{($1)}'], [/\\sm\{([^{}]*)\}/g, '\\Sigma_{($1)}'],
  [/\\Coq\b/g, 'Coq'], [/\\Agda\b/g, 'Agda'], [/\\HoTT\b/g, 'HoTT'],
  [/\\xspace/g, ''],
];
// TeX takes undelimited arguments too: \eqv A B, \idtype[\UU]AB — one
// token each (a brace group, a control word, or a single character)
const ARG = String.raw`(?:\{(?:[^{}]|\{[^{}]*\})*\}|\\[A-Za-z]+|[^\s\\{}])`;
const OPT = String.raw`(?:\[([^\]]*)\])?`;
const strip = (a) => (a && a.startsWith('{') ? a.slice(1, -1) : (a ?? ''));
const macroN = (name, n, fn, opt = false) => [
  new RegExp(String.raw`\\` + name + (opt ? OPT : '') + Array(n).fill(String.raw`\s*(` + ARG + ')').join(''), 'g'),
  (...m) => fn(...(opt ? [m[1], ...m.slice(2, 2 + n)] : m.slice(1, 1 + n)).map(strip)),
];
const MACROS2 = [
  macroN('idtype', 2, (o, a, b) => `\\mathsf{Id}${o ? `_{${o}}` : ''}(${a},${b})`, true),
  macroN('idtypevar', 1, (a) => `\\mathsf{Id}_{${a}}`),
  macroN('eqvspaced', 2, (a, b) => `${a} \\simeq ${b}`),
  macroN('eqv', 2, (a, b) => `${a} \\simeq ${b}`),
  macroN('texteqv', 2, (a, b) => `\\mathsf{Equiv}(${a},${b})`),
  macroN('pairr', 1, (a) => `(${a})`),
  macroN('LEM', 1, (a) => `\\mathsf{LEM}_{${a}}`),
  macroN('choice', 1, (a) => `\\mathsf{AC}_{${a}}`),
  macroN('tprd', 1, (a) => `\\Pi_{(${a})}`),
  macroN('sm', 1, (a) => `\\Sigma_{(${a})}`),
  macroN('prd', 1, (a) => `\\Pi_{(${a})}`),
];
const expandMacros = (s) => {
  for (const [re, rep] of MACROS2) s = s.replace(re, rep);
  for (const [re, rep] of MACROS) s = s.replace(re, rep);
  return s;
};

// ---- LaTeX math → .tsm math ---------------------------------------------------
// (plan P3-24) the symbols are the engine's one map (TEX_MATH, generated from
// engine/data/math/symbols.tsv; tests.cc lexes every target), the alphabets
// its rows (bb, cal, frak, bold, sans, mono, italic), the spaces its rows
// (thin, med, thick, quad, wide). A macro with no symbol is kept as its
// name — the engine says so (info math-implicit-name) — and listed on stderr.
const TEX_ALPHABET = { mathbb: 'bb', mathcal: 'cal', mathscr: 'cal', mathfrak: 'frak', mathbf: 'bold',
                       boldsymbol: 'bold', mathsf: 'sans', mathtt: 'mono', mathit: 'italic' };
const TEX_SPACE = { quad: 'quad', qquad: 'wide', ',': 'thin', ':': 'med', '>': 'med', ';': 'thick', '!': '', ' ': '' };
const unknownMath = new Set();
const MATH = [
  [/\\defeq/g, ' := '], [/\\jdeq/g, ' equiv '], [/\\narrowbreak|\\allowbreak/g, ' '],
  [/\\prd\{([^{}]*)\}/g, 'Pi_($1)'], [/\\sm\{([^{}]*)\}/g, 'Sigma_($1)'],
  [/\\setof\{([^{}]*)\}/g, '\u0002$1\u0003'],
  [/\\(mathbb|mathcal|mathscr|mathfrak|mathbf|boldsymbol|mathsf|mathtt|mathit)\{([A-Za-z0-9]+)\}/g,
    (m, c, x) => (x.length > 1 && /^[A-Za-z]+$/.test(x) && (c === 'mathsf' || c === 'mathrm')
      ? ` class(op, "${x}") ` : ` ${TEX_ALPHABET[c]}(${x}) `)],
  [/\\(?:operatorname|mathrm)\{([A-Za-z]+)\}/g, (m, x) => (x.length > 1 ? ` class(op, "${x}") ` : ` "${x}" `)],
  [/\\(?:text|textrm|mbox)\{([^{}]*)\}/g, '"$1"'],
  [/\\sqrt\{([^{}]*)\}/g, 'sqrt($1)'], [/\\frac\{([^{}]*)\}\{([^{}]*)\}/g, 'frac($1, $2)'],
  [/\\(?:left|right)\s*\./g, ''], [/\\left|\\right/g, ''],
  [/\\mathopen\{\}|\\mathclose\{\}/g, ''],
  [/\\(quad|qquad)\b|\\([,:;>! ])/g, (m, w, c) => ` ${TEX_SPACE[w ?? c]} `],
  [/\^\{([^{}]*)\}/g, '^($1)'], [/_\{([^{}]*)\}/g, '_($1)'],
  [/\\\{/g, '\u0002'], [/\\\}/g, '\u0003'],  // set braces survive the grouping-brace strip
  [/\\([A-Za-z]+)/g, (m, name) => {
    if (TEX_MATH[name]) return ` ${TEX_MATH[name]} `;
    unknownMath.add(name);
    return ` ${name} `;
  }],
  [/[{}]/g, ''],
];
// (plan P3-29; design T8 S9) what the engine sets, converted rather than
// flattened or deleted: matrices, cases and inner aligned rows as grids
// (rows `;`, cells `&`), braces, sized delimiters, phantoms, wide accents,
// over/under-set symbols. \cancel has no paint form (D-M05): its argument is
// kept and the converter says so.
const unsupportedMath = new Set();
// a balanced {…} group at i (whitespace skipped): [content, end], or null
const group = (s, i) => {
  while (s[i] === ' ' || s[i] === '\n') i++;
  if (s[i] !== '{') return null;
  let depth = 0;
  for (let j = i; j < s.length; j++) {
    if (s[j] === '\\') { j++; continue; }
    if (s[j] === '{') depth++;
    else if (s[j] === '}' && --depth === 0) return [s.slice(i + 1, j), j + 1];
  }
  return null;
};
// \name{a}{b}… → fn(a, b, …, rest): argc groups, then (opt) a ^{t} or _{b}
// annotation; the innermost first, so arguments are already converted
const calls = (s, name, argc, fn, annotation) => {
  const re = new RegExp(String.raw`\\` + name + String.raw`(?![A-Za-z])`, 'g');
  for (let guard = 0; guard < 1000; guard++) {
    const hits = [...s.matchAll(re)];
    if (!hits.length) return s;
    const h = hits[hits.length - 1];  // the last: its arguments hold no unconverted call
    let at = h.index + h[0].length;
    const args = [];
    for (let k = 0; k < argc; k++) {
      const g = group(s, at);
      if (!g) break;
      args.push(g[0]);
      at = g[1];
    }
    if (args.length < argc) return s;
    let note = '';
    if (annotation) {
      let j = at;
      while (s[j] === ' ') j++;
      if (s[j] === annotation) {
        const g = group(s, j + 1);
        if (g) { note = g[0]; at = g[1]; }
        else if (s[j + 1] && /\S/.test(s[j + 1])) { note = s[j + 1]; at = j + 2; }
      }
    }
    s = s.slice(0, h.index) + fn(...args, note) + s.slice(at);
  }
  return s;
};
// the top-level rows of an environment body (`\\`, outside groups and
// nested environments)
const rowsOf = (body) => {
  const rows = [];
  let depth = 0, from = 0;
  for (let i = 0; i < body.length; i++) {
    if (body.startsWith('\\begin{', i)) depth++;
    else if (body.startsWith('\\end{', i)) depth--;
    else if (body[i] === '{') depth++;
    else if (body[i] === '}') depth--;
    else if (depth === 0 && body.startsWith('\\\\', i)) {
      rows.push(body.slice(from, i));
      i++;
      from = i + 1;
      continue;
    }
    if (body[i] === '\\' && body[i + 1] !== '\\') i++;  // an escaped character
  }
  rows.push(body.slice(from));
  return rows.map((r) => r.replace(/\\(hline|nonumber|notag)\b/g, '').replace(/^\s*\[[^\]]*\]/, '').trim())
    .filter((r) => r !== '');
};
const TEX_GRID = { matrix: 'grid(c, ', pmatrix: 'pmat(', bmatrix: 'bmat(', Bmatrix: 'Bmat(', vmatrix: 'vmat(',
                   Vmatrix: 'Vmat(', smallmatrix: 'grid(c, ', cases: 'cases(', aligned: 'aligned(', split: 'aligned(',
                   gathered: 'grid(c, ', array: null };
const TEX_DELIM = { '(': '(', ')': ')', '[': '[', ']': ']', '|': '|', '/': '/', '\\{': '{', '\\}': '}', '\\|': '‖',
                    '\\langle': '⟨', '\\rangle': '⟩', '\\lvert': '|', '\\rvert': '|', '\\lVert': '‖', '\\rVert': '‖',
                    '\\lfloor': '⌊', '\\rfloor': '⌋', '\\lceil': '⌈', '\\rceil': '⌉' };
const TEX_ACCENT = { hat: 'hat', widehat: 'hat', tilde: 'tilde', widetilde: 'tilde', bar: 'bar', overline: 'overline',
                     underline: 'underline', vec: 'vec', overrightarrow: 'vec', dot: 'dot', ddot: 'ddot', check: 'check',
                     widecheck: 'check', breve: 'breve', acute: 'acute', grave: 'grave', mathring: 'ring' };
const constructs = (s) => {
  // environments, the innermost first
  const envRe = /\\begin\{(matrix|pmatrix|bmatrix|Bmatrix|vmatrix|Vmatrix|smallmatrix|cases|aligned|split|gathered|array)\}(\{[^{}]*\})?((?:(?!\\begin\{)[\s\S])*?)\\end\{\1\}/;
  for (let m; (m = s.match(envRe));) {
    const [all, env, spec, body] = m;
    const head = env === 'array' ? `grid(${(spec ?? '{c}').replace(/[^lcr]/g, '') || 'c'}, ` : TEX_GRID[env];
    s = s.replace(all, ` ${head}${rowsOf(body).join('; ')}) `);
  }
  for (const [tex, fn] of [['overbrace', 'overbrace'], ['underbrace', 'underbrace']])
    s = calls(s, tex, 1, (x, note) => ` ${fn}(${x}${note ? `, ${note}` : ''}) `, tex === 'overbrace' ? '^' : '_');
  for (const [tex, fn] of [['phantom', 'phantom'], ['hphantom', 'hphantom'], ['vphantom', 'vphantom'], ['smash', 'smash']])
    s = calls(s, tex, 1, (x) => ` ${fn}(${x}) `);
  // (TeX's letters are variables: \widehat{xyz} is x y z under one hat)
  const letters = (x) => (/^\s*[A-Za-z]{2,}\s*$/.test(x) ? x.trim().split('').join(' ') : x);
  for (const [tex, fn] of Object.entries(TEX_ACCENT)) s = calls(s, tex, 1, (x) => ` ${fn}(${letters(x)}) `);
  s = calls(s, 'overset', 2, (a, b) => ` limits(${b})^(${a}) `);
  s = calls(s, 'stackrel', 2, (a, b) => ` limits(${b})^(${a}) `);
  s = calls(s, 'underset', 2, (a, b) => ` limits(${b})_(${a}) `);
  for (const tex of ['cancel', 'bcancel', 'xcancel'])
    s = calls(s, tex, 1, (x) => { unsupportedMath.add(tex); return ` ${x} `; });
  // \big( … \Biggr]: one sized delimiter (\left/\right pairs are the engine's own)
  s = s.replace(/\\(big|Big|bigg|Bigg)[lrm]?\s*(\\[A-Za-z]+|\\[{}|]|[()[\]|/.])/g, (m, size, d) =>
    (d === '.' ? ' ' : TEX_DELIM[d] ? ` ${size}(${TEX_DELIM[d]}) ` : m));
  return s;
};
const mathToTsm = (m) => {
  let s = constructs(expandMacros(m));
  for (const [re, rep] of MATH) s = s.replace(re, rep);
  return s.replace(/\s+/g, ' ').trim().replace(/\u0002/g, '{').replace(/\u0003/g, '}');
};

// ---- prose --------------------------------------------------------------------
src = src.replace(/(^|[^\\])%.*$/gm, '$1');
src = src.replace(/^[ \t]*\\index(see)?\{[^{}]*(\{[^{}]*\}[^{}]*)*\}(\{[^{}]*\})?[ \t]*\n/gm, '');
src = src.replace(/\\index\{[^{}]*(\{[^{}]*\}[^{}]*)*\}/g, '');
src = src.replace(/\\indexsee\{[^{}]*\}\{[^{}]*\}/g, '');
src = src.replace(/\\label\{([^{}]*)\}/g, (m, l) => ` <${safeKey(l)}>`);
src = src.replace(/\\addlinespace(\[[^\]]*\])?/g, '');
src = src.replace(/\\OPT[A-Za-z]+|\\noindent|\\clearpage|\\newpage|\\addlinespace|\\toprule|\\midrule|\\bottomrule/g, '');
src = src.replace(/\\(markboth|addcontentsline|setcounter|pagenumbering)(\{(?:[^{}]|\{[^{}]*\})*\})+/g, '');
src = src.replace(/\\chapter\*?\{([^{}]*)\}/g, (m, t) => `\n= ${t}\n`);
src = src.replace(/\\section\*?\{([^{}]*)\}/g, (m, t) => `\n== ${t}\n`);
src = src.replace(/\\subsection\*?\{([^{}]*)\}/g, (m, t) => `\n=== ${t}\n`);
src = src.replace(/\\subsubsection\*?\{([^{}]*)\}/g, (m, t) => `\n==== ${t}\n`);
// math islands first so the prose rules never touch them
const maths = [];
const stash = (m) => { maths.push(m); return `\u0001M${maths.length - 1}\u0001`; };
// (plan P3-29, D-S11) display environments: an align's rows are display
// lines one after the other — one equations block, aligned at `&`, each row
// its own equation (its label); a multline's rows are one formula's rows
// (`\` ending a line); an equation is one display. A row's \label (already
// ` <key>`) follows its formula.
const labelOf = (row) => {
  let label = '';
  const text = row.replace(/\s*<([A-Za-z0-9_-]+)>/g, (m, k) => { label = label || k; return ' '; });
  return [text, label ? ` <${label}>` : ''];
};
src = src.replace(/\\begin\{(equation\*?|align\*?|flalign\*?|alignat\*?|gather\*?|narrowmultline\*?|multline\*?)\}(\{\d+\})?([\s\S]*?)\\end\{\1\}/g,
  (m, env, n, body) => {
    const rows = rowsOf(body);
    if (/^(equation|multline|narrowmultline)/.test(env)) {
      const [text, label] = labelOf(rows.join(' \\\\ '));
      const parts = rowsOf(text).map(mathToTsm);
      return stash('\n$ ' + parts.join(' \\\n  ') + ' $' + label + '\n');
    }
    return stash('\n' + rows.map((r) => {
      const [text, label] = labelOf(r);
      return '$ ' + mathToTsm(text) + ' $' + label;
    }).join('\n') + '\n');
  });
src = src.replace(/\\\[([\s\S]*?)\\\]/g, (m, b) => stash('\n$ ' + mathToTsm(b) + ' $\n'));
src = src.replace(/\$([^$]+)\$/g, (m, b) => stash('$' + mathToTsm(b) + '$'));
src = src.replace(/\\\(([\s\S]*?)\\\)/g, (m, b) => stash('$' + mathToTsm(b) + '$'));
src = expandMacros(src);
// (plan P3-33) TeX's text-mode spacing, discretionary hyphen and accents: a
// backslash before a letter or a blank is no .tsm escape (it would show)
const ACCENTS = { '"': '\u0308', "'": '\u0301', '`': '\u0300', '^': '\u0302', '~': '\u0303', '=': '\u0304',
  '.': '\u0307', u: '\u0306', v: '\u030C', H: '\u030B', c: '\u0327', r: '\u030A', k: '\u0328' };
// (never the second backslash of TeX's line break \\)
src = src.replace(/(?<!\\)\\(["'`^~=.])\{?([A-Za-z])\}?/g, (m, a, ch) => (ch + ACCENTS[a]).normalize('NFC'))
  .replace(/(?<!\\)\\([uvHcrk])\{([A-Za-z])\}/g, (m, a, ch) => (ch + ACCENTS[a]).normalize('NFC'))
  .replace(/(?<!\\)\\[ ;:]/g, ' ').replace(/(?<!\\)\\,/g, '\u2009').replace(/(?<!\\)\\[!@/-]/g, '');
src = src.replace(/\\footnote\{((?:[^{}]|\{[^{}]*\})*)\}/g, (m, b) => `^[${b}]`);
src = src.replace(/\\emph\{((?:[^{}]|\{[^{}]*\})*)\}/g, (m, b) => em(b));
src = src.replace(/\\textit\{((?:[^{}]|\{[^{}]*\})*)\}/g, (m, b) => em(b));
src = src.replace(/\\textbf\{((?:[^{}]|\{[^{}]*\})*)\}/g, (m, b) => strong(b));
src = src.replace(/\\textsc\{([^{}]*)\}/g, '$1');
src = src.replace(/\\texttt\{([^{}]*)\}/g, '`$1`');
src = src.replace(/\\url\{([^{}]*)\}/g, '$1');
src = src.replace(/\\cite\{([^{}]*)\}/g, (m, keys) => {
  const ks = keys.split(',').map(safeKey);
  return ks.length === 1 ? `@${ks[0]}` : `@[${ks.join(', ')}]`;
});
src = src.replace(/\\(cref|Cref|autoref|ref|eqref)\{([^{}]*)\}/g, (m, c, l) => `@${safeKey(l.split(',')[0])}`);
src = src.replace(/``/g, '“').replace(/''/g, '”').replace(/---/g, '—').replace(/--/g, '–').replace(/~/g, ' ');
src = src.replace(/\\begin\{(itemize|enumerate|description)\}([\s\S]*?)\\end\{\1\}/g, (m, env, body) => {
  const mark = env === 'enumerate' ? '+' : '-';
  return '\n' + body.trim().split(/\\item\s*/).filter((x) => x.trim()).map((it) => {
    const flat = (s) => s.replace(/\s*\n\s*/g, ' ').trim();
    // (plan P3-34) a description item is `/ term: description` (a colon in
    // the term escaped); another list's [label] stays a bold lead
    const dt = env === 'description' && /^\[([^\]]*)\]\s*/.exec(it);
    if (dt) return `/ ${termText(flat(dt[1]))}: ${flat(it.slice(dt[0].length))}`;
    return `${mark} ${flat(it.replace(/^\[([^\]]*)\]\s*/, (m, b) => strong(b) + ' '))}`;
  }).join('\n') + '\n';
});
src = src.replace(/\\begin\{tabular\}\{([^{}]*)\}([\s\S]*?)\\end\{tabular\}/g, (m, spec, body) => {
  const cols = (spec.match(/[lcr]/g) || []).length || 2;
  const rows = body.replace(/\\(toprule|midrule|bottomrule|hline|addlinespace)(\[[^\]]*\])?/g, '')
    .split(/\\\\(?:\[[^\]]*\])?/).map((r) => r.trim()).filter(Boolean)
    .map((r) => r.split(/(?<!\\)&/).map((c) => c.replace(/\s*\n\s*/g, ' ').trim()).join(' | '));
  return '\n\n#!table(cols: ' + cols + ')\n' + rows.join('\n') + '\n#table!\n\n';
});
src = src.replace(/\\begin\{(center|table|figure)\}(\[[^\]]*\])?(\{[^{}]*\})?/g, '').replace(/\\end\{(center|table|figure)\}/g, '');
src = src.replace(/\\caption\{([^{}]*)\}/g, (m, b) => em(b));
src = src.replace(/\\(hline|centering|small|large|Large|bigskip|medskip|smallskip|vspace\{[^}]*\}|hspace\{[^}]*\})/g, '');
src = src.replace(/\\\\/g, ' ').replace(/(?<!\\)&/g, ' | ');
src = src.replace(/\\([A-Za-z]+)\b\*?/g, (m, name) => `⟨\\\\${name}⟩`);  // survivors = visible gaps (an escaped \\)
src = src.replace(/[{}]/g, '');
src = src.replace(/\u0001M(\d+)\u0001/g, (m, i) => maths[+i]);
// HoTT sources put one sentence per line: join lines inside paragraphs
src = src.split(/\n\s*\n/).map((p) => {
  const t = p.trim();
  return /^(=|-|\+|\$ |#|\/ )/.test(t) ? t : t.replace(/\s*\n\s*/g, ' ');
}).join('\n\n');
// bibliography: --bib-ref is the path the DOCUMENT will use for the CSL-JSON
if (opt('--bib-ref')) src += `\n\n#bibliography(${JSON.stringify(opt('--bib-ref'))})\n`;
// (plan P3-33) emphasis resolved with its final neighbours (prose.mjs)
process.stdout.write(markers(src).replace(/\n{3,}/g, '\n\n').trim() + '\n');
if (unknownMath.size) console.error(`tex2tsm: math macros with no symbol, kept as names: ${[...unknownMath].sort().join(' ')}`);
if (unsupportedMath.size)
  console.error(`tex2tsm: math-unsupported: ${[...unsupportedMath].sort().map((m) => '\\' + m).join(' ')} (no stroke to paint: the argument is kept)`);
