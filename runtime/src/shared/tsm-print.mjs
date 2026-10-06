// The .tsm printer and escaper (plan P3-35; design T1 S12, FrontEndExports).
//
//   print(ast, { src })      the engine's AST JSON (tsr_parse_json, tsrc
//                            --stage=astjson) back to .tsm whose parse is the
//                            same tree, spans aside (conformance (c):
//                            tools/check-print.mjs). Sugar where its inverse
//                            parse is exact; `src` (the parsed source) gives an
//                            error node its own text.
//   escapeTsm(text, ctx, { before, after, lineStart })
//                            text that parses as itself where it stands: ctx
//                            'para' (prose), 'heading', 'link' (a link's
//                            text), 'body' (inside […]: a content argument,
//                            a note), 'term' (a description item's term),
//                            'cells' (a region's paragraph: its `|` cut cells);
//                            `before` / `after` are the characters around it
//                            ('' = none or unknown: then the safe choice).
//
// The lexical facts come from the syntax table (syntax.gen.mjs): the
// Escapable class, the identifier classes, the keyword heads.
import { SYNTAX } from './syntax.gen.mjs';

// a character class of syntax.def ("A-Za-z_$", "^ <>…" negated) as a test
function classTest(spec) {
  const neg = spec.startsWith('^');
  const s = neg ? spec.slice(1) : spec;
  const ranges = [];
  for (let i = 0; i < s.length; i++) {
    if (s[i + 1] === '-' && i + 2 < s.length) {
      ranges.push([s.charCodeAt(i), s.charCodeAt(i + 2)]);
      i += 2;
    } else ranges.push([s.charCodeAt(i), s.charCodeAt(i)]);
  }
  return (c) => {
    if (!c) return false;
    const x = c.charCodeAt(0);
    const hit = ranges.some(([a, b]) => x >= a && x <= b);
    return neg ? !hit && x > 0x20 && x !== 0x7f : hit;
  };
}
const C = SYNTAX.classes;
const isEscapable = classTest(C.Escapable);
const isSpliceHead = classTest(C.SpliceHead);
const isSpliceCont = classTest(C.SpliceCont);
const isIdStart = classTest(C.IdStart);
const isIdCont = classTest(C.IdCont);
const isIdJoin = classTest(C.IdJoin);
const isLabelChar = classTest(C.LabelChar);
const isAlnum = (c) => !!c && /[A-Za-z0-9]/.test(c);
const isBlank = (c) => c === ' ' || c === '\t' || c === '\n';
const isSchemeChar = (c) => !!c && /[A-Za-z0-9+.-]/.test(c);

// a line's start that a block rule would read: what to escape there
function lineStartEscape(s) {
  if (/^={1,6}( |$)/.test(s)) return 0;            // a heading
  if (/^[-+]( |\t|$)/.test(s)) return 0;           // a list item
  if (/^\d{1,9}\.( |\t|$)/.test(s)) return s.indexOf('.');  // N.
  if (/^>/.test(s)) return 0;                      // a quote
  if (/^\/( |\t)/.test(s)) return 0;               // a description item
  if (/^---/.test(s)) return 0;                    // a rule
  return -1;
}

// a label suffix's shape at s[i] (` <id>`): `<` LabelChar+ `>`
const labelAt = (s, i) => {
  if (s[i] !== '<') return false;
  let j = i + 1;
  while (j < s.length && isLabelChar(s[j])) j++;
  return j > i + 1 && s[j] === '>';
};

export function escapeTsm(text, ctx = 'para', { before = '', after = '', lineStart = false, seps = null } = {}) {
  let out = '';
  let atLine = lineStart;
  const cut = new Set(seps ?? []);
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    // (a private-use character is a converter's markup placeholder: unknown)
    const pua = (x) => (x && x >= '\uE000' && x <= '\uF8FF' ? '' : x);
    const prev = pua(i > 0 ? text[i - 1] : before);
    const next = pua(i + 1 < text.length ? text[i + 1] : after);
    if (c === '\n') {
      out += '\n';
      atLine = true;
      continue;
    }
    if (atLine) {
      atLine = false;
      const at = lineStartEscape(text.slice(i));
      if (at === 0) {
        out += '\\' + c;
        continue;
      }
      if (at > 0) {  // N. : its dot
        out += text.slice(i, i + at) + '\\.';
        i += at;
        continue;
      }
    }
    let esc = false;
    switch (c) {
      case '\\': esc = next === '' || next === '\n' || isEscapable(next); break;
      case '`': case '$': case '[': case ']': esc = true; break;
      case '*': case '_': esc = !(isAlnum(prev) && isAlnum(next)); break;
      case '#': esc = next === '' || isSpliceHead(next) || next === '(' || next === '{' || next === '!'; break;
      case '@': esc = !isSpliceCont(prev) && (next === '' || isIdStart(next) || next === '['); break;
      case '%': esc = next === '' || (next === '-' && (i + 2 >= text.length ? true : text[i + 2] === '-')); break;
      case '^': esc = next === '' || next === '['; break;
      case '<': esc = labelAt(text, i) || (i + 1 >= text.length && after === ''); break;
      case ':': {
        // a URL's scheme behind it (an autolink), or a term's end
        let s = i;
        while (s > 0 && isSchemeChar(text[s - 1])) s--;
        const scheme = text.slice(s, i).toLowerCase();
        esc = (scheme === 'http' || scheme === 'https') && text.slice(i + 1, i + 3) === '//';
        if (ctx === 'term') esc = esc || next === '' || isBlank(next);
        break;
      }
      case '|': esc = ctx === 'cells' && !cut.has(i); break;
      default: break;
    }
    out += esc ? '\\' + c : c;
  }
  return out;
}

// ---- the printer ------------------------------------------------------------

// UTF-8 byte offsets into s (the engine's cooked offsets) as string indices
function byteIndices(s, offsets) {
  const at = new Map();
  let b = 0;
  for (let i = 0; i < s.length; i++) {
    at.set(b, i);
    const c = s.codePointAt(i);
    b += c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    if (c >= 0x10000) i++;
  }
  return offsets.map((o) => at.get(o) ?? -1);
}

const sugarOf = (n) => (n.kind === 'call' ? n.sugar : null);
const INLINE_SUGAR = new Set(['strong', 'em', 'code', 'link', 'note', 'ref', 'math', 'linebreak', 'termpart', 'arg']);
const isInlineNode = (n) => n.kind === 'text' || n.kind === 'splice' || n.kind === 'keyword' ||
  n.kind === 'comment' || (n.kind === 'call' && INLINE_SUGAR.has(n.sugar)) || n.kind === 'error';

// a bare reference id: IdStart IdCont* (IdJoin IdCont+)*
const bareId = (s) => {
  if (!s || !isIdStart(s[0])) return false;
  let i = 1;
  while (i < s.length) {
    if (isIdCont(s[i])) i++;
    else if (isIdJoin(s[i]) && isIdCont(s[i + 1])) i += 2;
    else return false;
  }
  return true;
};

// a URL the lexer would read back whole (syntax.def url: lexUrl's rules)
const autolinkable = (url) => /^https?:\/\/[^\s<>"`|\\$\u0080-￿]*[^\s<>"`|\\$.,:;!?'*_~)\]\u0080-￿]$/i.test(url) &&
  [['(', ')'], ['[', ']']].every(([o, c]) => [...url].filter((x) => x === o).length >= [...url].filter((x) => x === c).length);
const urlEnds = (c) => c === '' || /[\s<>"`|\\$\u0080-￿.,:;!?'*_~)\]]/.test(c);

export function print(ast, { src = null } = {}) {
  const P = new Printer(src);
  return P.doc(ast);
}

class Printer {
  // src: the parsed source; spans are its UTF-8 byte offsets
  constructor(src) {
    this.src = src;
    this.bytes = src ? new TextEncoder().encode(src) : null;
  }

  doc(n) {
    const text = this.blocks(n.kids ?? []).join('\n').replace(/\n+$/, '');
    return this.src !== null && !this.src.endsWith('\n') ? text : text + '\n';  // (the source's last line end)
  }

  // blocks → lines: a blank line between blocks; a run of inline content
  // among them (a content body's statements and its text) is one paragraph,
  // and a statement needs only its line end after it. Two ordered lists one
  // after the other differ in their marker class, so they stay two.
  blocks(kids, ctx = 'para') {
    const units = [];
    for (const k of kids) {
      const inl = isInlineNode(k) && k.kind !== 'error';
      if (inl && units.length && units.at(-1).inline) units.at(-1).kids.push(k);
      else units.push({ inline: inl, kids: [k] });
    }
    const lines = [];
    let prevList = null;  // the marker class of the list just printed
    units.forEach((u, i) => {
      if (i > 0) {
        const prev = units[i - 1];
        const stmt = (x) => !x.inline && x.kids[0].kind === 'stmt';
        if (!stmt(prev) && !stmt(u)) lines.push('');
      }
      if (u.inline) {
        lines.push(...this.inline(u.kids, ctx === 'cells' ? 'cells' : 'para', { lineStart: true }).split('\n'));
        prevList = null;
        return;
      }
      const n = u.kids[0];
      if (sugarOf(n) === 'list' && n.ordered) {
        // N. (any start) or + (from 1): adjacent ordered lists alternate
        const next = units[i + 1]?.kids[0];
        const nextDot = next && sugarOf(next) === 'list' && next.ordered && (next.start ?? 1) !== 1;
        const cls = (n.start ?? 1) !== 1 ? '.' : prevList === '+' ? '.' : prevList === '.' || nextDot ? '+' : '.';
        lines.push(...this.list(n, cls));
        prevList = cls;
        return;
      }
      prevList = sugarOf(n) === 'list' ? '-' : null;
      lines.push(...this.block(n, ctx));
    });
    return lines;
  }
  list(n, cls) {
    const out = [];
    (n.kids ?? []).forEach((item, k) => {
      const marker = !n.ordered ? '- ' : cls === '+' ? '+ ' : `${(n.start ?? 1) + k}. `;
      if (k > 0 && this.multi(item, (n.kids ?? [])[k - 1])) out.push('');
      out.push(...this.item(item, marker));
    });
    return out;
  }

  block(n, ctx = 'para') {
    if (n.kind === 'error') return this.verbatim(n).split('\n');
    if (n.kind === 'comment') return `%--${n.str}--%`.split('\n');
    if (n.kind === 'stmt') {
      if (!n.let) return `#{${n.js}}`.split('\n');
      if (!n.content) return `#let${n.js}`.split('\n');
      const kids = n.kids ?? [];
      if (kids.every(isInlineNode)) {  // a one-line content literal stays one
        const s = this.inline(kids, 'body', { before: '[', after: ']' });
        if (!s.includes('\n')) return [`#let ${n.js} = [${s}]`];
      }
      return [`#let ${n.js} = [`, ...this.body(kids), ']'];
    }
    if (isInlineNode(n)) return this.inline([n], ctx === 'cells' ? 'cells' : 'para', { lineStart: true }).split('\n');
    switch (n.sugar) {
      case 'para': return this.inline(n.kids ?? [], ctx, { lineStart: true }).split('\n');
      case 'heading': {
        const text = this.inline(n.kids ?? [], 'heading', { after: n.label ? ' ' : '' });
        return ['='.repeat(n.level) + ' ' + text + (n.label ? ` <${n.label}>` : '')];
      }
      case 'rule': return ['---'];
      case 'quote': return this.blocks(n.kids ?? []).map((l) => (l ? '> ' + l : '>'));
      case 'list': return this.list(n, '.');
      case 'terms': {
        const out = [];
        (n.kids ?? []).forEach((item, k) => {
          if (k > 0 && this.multi(item, (n.kids ?? [])[k - 1])) out.push('');
          out.push(...this.term(item));
        });
        return out;
      }
      case 'fence': return this.fence(n);
      case 'region': return this.region(n);
      default: return this.verbatim(n).split('\n');
    }
  }

  // a list's item: its marker, then its blocks at its content column
  item(item, marker) {
    const kids = item.kids ?? [];
    if (!kids.length) return [marker];
    const pad = ' '.repeat(marker.length);
    const lines = this.blocks(kids);
    return lines.map((l, i) => (i === 0 ? marker + l : l ? pad + l : ''));
  }
  // (plan P3-34) a description item: `/ term: description`
  term(item) {
    const kids = item.kids ?? [];
    const part = kids.find((k) => sugarOf(k) === 'termpart');
    const rest = kids.filter((k) => k !== part);
    const head = '/ ' + this.inline(part?.kids ?? [], 'term', { after: ':' }) + ':';
    if (!rest.length) return [head];
    if (sugarOf(rest[0]) === 'para') {
      const lines = this.blocks(rest);
      return lines.map((l, i) => (i === 0 ? `${head} ${l}` : l ? '  ' + l : ''));
    }
    return [head, ...this.blocks(rest).map((l) => (l ? '  ' + l : ''))];
  }
  // whether a blank line goes between two items (either holds several blocks)
  multi(a, b) { return (a.kids ?? []).length > 1 || (b.kids ?? []).length > 1 || (sugarOf(a) === 'item' && (a.kids ?? []).some((k) => sugarOf(k) !== 'para' && sugarOf(k) !== 'termpart')); }

  fence(n) {
    const body = n.str ?? '';
    let run = 3;
    for (const m of body.matchAll(/^[ \t]*(`{3,})/gm)) run = Math.max(run, m[1].length + 1);
    const ticks = '`'.repeat(run);
    const head = ticks + (n.lang ?? '') + (n.args ? `(${n.args})` : '') + (n.info ? ' ' + n.info : '') +
      (n.label ? ` <${n.label}>` : '');
    return [head, ...(body === '' ? [] : body.split('\n')), ticks];
  }

  region(n) {
    const head = '#!' + n.str + (n.args ? `(${n.args})` : '') + (n.label ? ` <${n.label}>` : '');
    const inner = this.blocks(n.kids ?? [], 'cells');
    return [head, ...inner, `#${n.str}!`];
  }

  // a content body (a content literal, a block-form argument): its blocks,
  // or its inline content as one paragraph
  body(kids) {
    return this.blocks(kids, 'body');
  }

  verbatim(n) {
    if (this.bytes && n.span) return new TextDecoder().decode(this.bytes.subarray(n.span[0], n.span[1]));
    return '';
  }

  // ---- inline -----------------------------------------------------------------
  // kids → text: each non-text piece first, then each text escaped with its
  // neighbours' characters
  inline(kids, ctx, { before = '', after = '', lineStart = false } = {}) {
    const pieces = kids.map((k) => (k.kind === 'text' ? null : this.piece(k, ctx)));
    // a splice that the next piece would continue ends with ';'
    for (let i = 0; i < kids.length; i++) {
      if (kids[i].kind !== 'splice') continue;
      const nxt = i + 1 < kids.length ? (pieces[i + 1] ?? kids[i + 1].str ?? '') : after;
      const c = nxt[0] ?? '';
      if (c && (isSpliceCont(c) || c === '(' || c === '[' || c === ';' || c === '.')) pieces[i] += ';';
    }
    let out = '';
    let atLine = lineStart;
    for (let i = 0; i < kids.length; i++) {
      const k = kids[i];
      let s;
      if (k.kind === 'text') {
        const prev = out ? out.at(-1) : before;
        let next = after;
        for (let j = i + 1; j < kids.length; j++) {
          const p = pieces[j] ?? kids[j].str ?? '';
          if (p) {
            next = p[0];
            break;
          }
        }
        const seps = k.seps ? byteIndices(k.str ?? '', k.seps.split(',').map(Number)) : null;
        s = escapeTsm(k.str ?? '', ctx, { before: prev, after: next, lineStart: atLine, seps });
      } else {
        s = pieces[i];
        // a bare URL reads back as one only between a non-scheme character
        // and one that ends it
        if (sugarOf(k) === 'link' && s === k.url) {
          const next = i + 1 < kids.length ? (pieces[i + 1] ?? escapeTsm(kids[i + 1].str ?? '', ctx))[0] ?? '' : after;
          const prev = out ? out.at(-1) : before;
          if (isSchemeChar(prev) || !urlEnds(next ?? ''))
            s = '[' + this.inline(k.kids ?? [], 'link', { before: '[', after: ']' }) + '](' + k.url + ')';
        }
      }
      out += s;
      if (s) atLine = s.endsWith('\n');
    }
    return out;
  }

  piece(n, ctx) {
    if (n.kind === 'error') return this.verbatim(n);
    if (n.kind === 'comment') return `%--${n.str}--%`;
    if (n.kind === 'splice') {
      return '#' + n.expr + (n.kids ?? []).map((a) => this.bracket(a.kids ?? [])).join('');
    }
    if (n.kind === 'keyword') {
      let out = '';
      (n.kids ?? []).forEach((br, k) => {
        if (k === 0) out += `#${n.str} (${br.head}) `;
        else out += br.head ? ` else if (${br.head}) ` : ' else ';
        out += this.bracket(br.kids ?? [], true);
      });
      return out;
    }
    switch (n.sugar) {
      case 'strong':
      case 'em': {
        const m = n.sugar === 'strong' ? '*' : '_';
        return m + this.inline(n.kids ?? [], ctx, { before: m, after: m }) + m;
      }
      case 'code': {
        const str = n.str ?? '';
        let run = 1;
        for (const m of str.matchAll(/`+/g)) if (m[0].length >= run) run = m[0].length + 1;
        const ticks = '`'.repeat(run);
        const pad = /^[ `]|[ `]$/.test(str) ? ' ' : '';
        return ticks + pad + str + pad + ticks;
      }
      case 'math': {
        const s = (n.str ?? '').replace(/\$/g, '\\$');
        return n.display ? `$ ${s} $` + (n.label ? ` <${n.label}>` : '') : `$${s}$`;
      }
      case 'link': {
        const kids = n.kids ?? [];
        if (kids.length === 1 && kids[0].kind === 'text' && kids[0].str === n.url && autolinkable(n.url)) return n.url;
        return '[' + this.inline(kids, 'link', { before: '[', after: ']' }) + '](' + n.url + ')';
      }
      case 'note': return '^' + this.bracket(n.kids ?? []);
      case 'ref': {
        const t = n.str ?? '';
        const sup = n.kids?.length ? this.bracket(n.kids) : '';
        if (bareId(t)) return '@' + t + sup;
        return '@[' + t.split(', ').map((id) => id.replace(/[\],\\]/g, '\\$&')).join(', ') + ']' + sup;
      }
      case 'linebreak': return '\\\n';
      default: return this.verbatim(n);
    }
  }

  // […]: inline content, or blocks in the block form (`[` ending its line,
  // `]` on a line of its own). A keyword body (`edges`) that holds a
  // statement is written in the block form, its edge blanks left to the
  // parser (it gives a body of inline content its line ends back).
  bracket(kids, edges = false) {
    if (kids.every((k) => isInlineNode(k))) return '[' + this.inline(kids, 'body', { before: '[', after: ']' }) + ']';
    let ks = kids;
    if (edges) {
      const blank = (k) => k?.kind === 'text' && /^[ \t\n]*$/.test(k.str ?? '');
      ks = ks.slice(blank(ks[0]) ? 1 : 0, blank(ks.at(-1)) ? -1 : undefined);
    }
    return '[\n' + this.blocks(ks, 'body').join('\n') + '\n]';
  }
}
