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

// ---- LaTeX math → Typst-ish math -----------------------------------------
const MATH = [
  [/\\infty/g, 'oo'], [/\\to\b/g, ' -> '], [/\\rightarrow/g, ' -> '], [/\\leftarrow/g, ' <- '],
  [/\\times/g, ' times '], [/\\cdot/g, ' dot '], [/\\circ/g, ' compose '],
  [/\\lambda/g, 'lambda'], [/\\alpha/g, 'alpha'], [/\\beta/g, 'beta'], [/\\gamma/g, 'gamma'],
  [/\\pi/g, 'pi'], [/\\sigma/g, 'sigma'], [/\\omega/g, 'omega'], [/\\epsilon/g, 'epsilon'],
  [/\\Pi/g, 'Pi'], [/\\Sigma/g, 'Sigma'],
  [/\\simeq/g, ' simeq '], [/\\equiv/g, ' equiv '], [/\\leq/g, '<='], [/\\geq/g, '>='], [/\\neq/g, '!='],
  [/\\in\b/g, ' ∈ '], [/\\wedge/g, ' ∧ '], [/\\vee/g, ' ∨ '], [/\\neg/g, '¬'],
  [/\\emptyset/g, '∅'], [/\\forall/g, 'forall'], [/\\exists/g, 'exists'],
  [/\\mathcal\{([A-Za-z])\}/g, '$1'],
  [/\\mathbf\{([A-Za-z0-9]+)\}/g, '$1'],
  [/\\mathsf\{([A-Za-z]+)\}/g, '$1'], [/\\mathrm\{([A-Za-z]+)\}/g, '$1'],
  [/\\mathbb\{([A-Z])\}/g, '$1$1'], [/\\operatorname\{([A-Za-z]+)\}/g, '$1'],
  [/\\setof\{([^{}]*)\}/g, '{$1}'], [/\\mid\b/g, ' ∣ '], [/\\bot\b/g, '⊥'], [/\\top\b/g, '⊤'],
  [/\\Rightarrow/g, ' ⇒ '],
  [/\\sqrt\{([^{}]*)\}/g, 'sqrt($1)'], [/\\frac\{([^{}]*)\}\{([^{}]*)\}/g, '($1)/($2)'],
  [/\\left\(/g, '('], [/\\right\)/g, ')'], [/\\left\[/g, '['], [/\\right\]/g, ']'],
  [/\\big\b|\\Big\b|\\bigl|\\bigr|\\Bigl|\\Bigr/g, ''],
  [/\\langle/g, 'angle.l'], [/\\rangle/g, 'angle.r'], [/\\ldots|\\cdots|\\dots/g, '...'],
  [/\\quad|\\qquad|\\,|\\;|\\!|\\ /g, ' '], [/\\mathopen\{\}|\\mathclose\{\}/g, ''],
  [/\\text\{([^{}]*)\}/g, '"$1"'], [/\\textrm\{([^{}]*)\}/g, '"$1"'],
  [/\^\{([^{}]*)\}/g, '^($1)'], [/_\{([^{}]*)\}/g, '_($1)'],
  [/\\defeq/g, ' := '], [/\\jdeq/g, ' equiv '], [/\\narrowbreak|\\allowbreak/g, ' '],
  [/\\prd\{([^{}]*)\}/g, 'Pi_($1)'], [/\\sm\{([^{}]*)\}/g, 'Sigma_($1)'],
  [/\\([A-Za-z]+)/g, '$1'],  // unknown math macro → its name as an identifier
  [/\\\{/g, '\u0002'], [/\\\}/g, '\u0003'],  // set braces survive the grouping-brace strip
  [/[{}]/g, ''],
];
const mathToTsm = (m) => {
  let s = expandMacros(m);
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
src = src.replace(/\\begin\{(equation\*?|align\*?|narrowmultline\*?|multline\*?)\}([\s\S]*?)\\end\{\1\}/g,
  (m, env, body) => stash('\n$ ' + mathToTsm(body.replace(/\\\\/g, ' ').replace(/&/g, ' ')) + ' $\n'));
src = src.replace(/\\\[([\s\S]*?)\\\]/g, (m, b) => stash('\n$ ' + mathToTsm(b) + ' $\n'));
src = src.replace(/\$([^$]+)\$/g, (m, b) => stash('$' + mathToTsm(b) + '$'));
src = src.replace(/\\\(([\s\S]*?)\\\)/g, (m, b) => stash('$' + mathToTsm(b) + '$'));
src = expandMacros(src);
src = src.replace(/\\footnote\{((?:[^{}]|\{[^{}]*\})*)\}/g, (m, b) => `^[${b}]`);
src = src.replace(/\\emph\{((?:[^{}]|\{[^{}]*\})*)\}/g, '_$1_');
src = src.replace(/\\textit\{((?:[^{}]|\{[^{}]*\})*)\}/g, '_$1_');
src = src.replace(/\\textbf\{((?:[^{}]|\{[^{}]*\})*)\}/g, '*$1*');
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
  return '\n' + body.trim().split(/\\item\s*/).filter((x) => x.trim())
    .map((it) => `${mark} ${it.replace(/^\[([^\]]*)\]\s*/, '*$1* ').replace(/\s*\n\s*/g, ' ').trim()}`).join('\n') + '\n';
});
src = src.replace(/\\begin\{tabular\}\{([^{}]*)\}([\s\S]*?)\\end\{tabular\}/g, (m, spec, body) => {
  const cols = (spec.match(/[lcr]/g) || []).length || 2;
  const rows = body.replace(/\\(toprule|midrule|bottomrule|hline|addlinespace)(\[[^\]]*\])?/g, '')
    .split(/\\\\(?:\[[^\]]*\])?/).map((r) => r.trim()).filter(Boolean)
    .map((r) => r.split(/(?<!\\)&/).map((c) => c.replace(/\s*\n\s*/g, ' ').trim()).join(' | '));
  return '\n\n#!table(cols: ' + cols + ')\n' + rows.join('\n') + '\n#table!\n\n';
});
src = src.replace(/\\begin\{(center|table|figure)\}(\[[^\]]*\])?(\{[^{}]*\})?/g, '').replace(/\\end\{(center|table|figure)\}/g, '');
src = src.replace(/\\caption\{([^{}]*)\}/g, '_$1_');
src = src.replace(/\\(hline|centering|small|large|Large|bigskip|medskip|smallskip|vspace\{[^}]*\}|hspace\{[^}]*\})/g, '');
src = src.replace(/\\\\/g, ' ').replace(/(?<!\\)&/g, ' | ');
src = src.replace(/\\([A-Za-z]+)\b\*?/g, (m, name) => `⟨\\${name}⟩`);  // survivors = visible gaps
src = src.replace(/[{}]/g, '');
src = src.replace(/\u0001M(\d+)\u0001/g, (m, i) => maths[+i]);
// HoTT sources put one sentence per line: join lines inside paragraphs
src = src.split(/\n\s*\n/).map((p) => {
  const t = p.trim();
  return /^(=|-|\+|\$ |#)/.test(t) ? t : t.replace(/\s*\n\s*/g, ' ');
}).join('\n\n');
// bibliography: --bib-ref is the path the DOCUMENT will use for the CSL-JSON
if (opt('--bib-ref')) src += `\n\n#bibliography(${JSON.stringify(opt('--bib-ref'))})\n`;
process.stdout.write(src.replace(/\n{3,}/g, '\n\n').trim() + '\n');
