// Executes a compiled document — its LowerProgram (run by shared/lower.mjs)
// and its hole module — against an OpBuf (architecture §4.1; plan P2-02).
// Works in Node (temp-file import) and in browsers/workers (blob URL import).
import { KIND } from '../shared/ops.gen.mjs';
import { OpBuf, isNode } from '../shared/opbuf.mjs';
import { STYLE_KEYS, STYLE_SUGAR } from '../shared/props.gen.mjs';
import { decodeProgram, Lowering, STUB } from '../shared/lower.mjs';
import { BFLAG, LPIECE, PROGRAM_ABI } from '../shared/lower.gen.mjs';

// style patches from the schema's run properties (plan P1-02): boolean sugar
// keys set flag bits, value keys map to styled/STYLE_PUSH attributes
const styleBits = (p) => {
  let bits = 0;
  for (const [k, bit] of Object.entries(STYLE_SUGAR)) if (p[k]) bits |= bit;
  return bits || undefined;
};
const styleValues = (p) => {
  const out = {};
  for (const [k, attr] of Object.entries(STYLE_KEYS)) if (p[k] !== undefined) out[attr] = p[k];
  return out;
};

// Default numeric bibliography formatter over CSL-JSON (notes-design.md §2):
// "Author, Author, and Author. Title. Container vol(issue), pages.
//  Publisher, year. doi/url". Overridable per document via $.bib.format.
export function formatEntryDefault(e, c) {
  const people = (e.author ?? e.editor ?? []).map((p) =>
    p.literal ?? [p.given, p.family].filter(Boolean).join(' '));
  const names = people.length <= 1 ? people.join('')
    : people.length === 2 ? people.join(' and ')
    : people.slice(0, -1).join(', ') + ', and ' + people.at(-1);
  const year = e.issued?.['date-parts']?.[0]?.[0] ?? e.issued?.raw ?? '';
  const parts = [];
  if (names) parts.push(c.text(names + '.'));
  if (e.title) parts.push(c.text(' '), c.em(c.text(e.title)), c.text('.'));
  if (e['container-title']) {
    let s = ' ' + e['container-title'];
    if (e.volume) s += ' ' + e.volume;
    if (e.issue) s += '(' + e.issue + ')';
    if (e.page) s += ', ' + e.page;
    parts.push(c.text(s + '.'));
  }
  const tail = [e.publisher, year].filter(Boolean).join(', ');
  if (tail) parts.push(c.text(' ' + tail + '.'));
  if (e.DOI) parts.push(c.text(' '), c.link('https://doi.org/' + e.DOI, c.text('doi:' + e.DOI)));
  else if (e.URL) parts.push(c.text(' '), c.link(e.URL, c.text(e.URL)));
  return parts;
}

// resource loader for #bibliography(src): browser/worker fetch against the
// page's base URL; Node reads the file — /site-root paths against rootDir
// (the site/repo root), relative ones against the document's folder. A
// document reads only below rootDir or its own folder (plan P0-11): no
// `../../..` walk and no OS-absolute path reaches the rest of the disk.
async function loadResource(src, opts) {
  const s = String(src);
  if (typeof process !== 'undefined' && process.versions?.node && !/^https?:/.test(s)) {
    const { readFile, realpath } = await import('node:fs/promises');
    const { join, resolve, sep } = await import('node:path');
    const rootDir = resolve(opts.rootDir ?? process.cwd());
    const baseDir = resolve(opts.baseDir ?? rootDir);
    const file = s.startsWith('/') ? join(rootDir, s) : resolve(baseDir, s);
    const within = (p, d) => p === d || p.startsWith(d.endsWith(sep) ? d : d + sep);
    const allowed = (p, roots) => roots.some((d) => within(p, d));
    if (!allowed(file, [rootDir, baseDir])) throw new Error('resource outside the document root');
    const real = await realpath(file);  // a symlink may not lead out either
    const realRoots = await Promise.all([rootDir, baseDir].map((d) => realpath(d).catch(() => d)));
    if (!allowed(real, realRoots)) throw new Error('resource outside the document root');
    return await readFile(real, 'utf8');
  }
  const url = opts.baseUrl ? new URL(s, opts.baseUrl) : s;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return await res.text();
}

// The content protocol's markers (plan P2-01; design T2 S4): a function a
// bare splice may call (#toc), and an object's own conversion to content
export const NULLARY = Symbol.for('tsm.nullary');
export const CONTENT = Symbol.for('tsm.content');

// prog: the decoded LowerProgram (its block table: where diagnostics point)
export function buildContext(ob, opts = {}, prog = { blocks: [], docEnd: 0 }) {
  const blocks = prog.blocks;
  const docEnd = prog.docEnd;
  // the top-level block whose user code runs (an unframed statement's
  // failure covers the rest of the document from it)
  let current = blocks.findIndex((b) => b.flags & BFLAG.User);
  // where the interpreter is: the innermost splice, region, frame or block
  // it runs — what a diagnostic of the run points at (shared/lower.mjs
  // keeps it current)
  const here = { s: 0, e: 0 };
  const unitSpan = () => [here.s, here.e];
  // toContent (plan P2-01): one conversion for every place a value becomes
  // content — splices, constructor children, handler returns, m`…`:
  //   a node value → itself; string/number/bigint → text; null, undefined,
  //   false → nothing; arrays and iterables → their items, flattened; an
  //   object with [CONTENT]() → its result; a NULLARY function → its call;
  //   any other function → an error node (splice-function); anything else →
  //   its String() with an info diagnostic (splice-object)
  const toContent = (x, out = []) => {
    if (isNode(x)) out.push(x);
    else if (typeof x === 'string' || typeof x === 'number' || typeof x === 'bigint') out.push(ob.makeText(String(x)));
    else if (x === null || x === undefined || x === false) { /* nothing */ }
    else if (Array.isArray(x)) for (const v of x) toContent(v, out);
    else if (typeof x === 'function') {
      if (x[NULLARY]) toContent(x(), out);
      else {
        const msg = `a function is not content${x.name ? ` (${x.name})` : ''}: call it`;
        out.push(ob.makeNode(KIND.error, { message: msg, code: 'splice-function' }, []));
        ob.diag(2, 'splice-function', msg, ...unitSpan());
      }
    } else if (typeof x === 'object' && typeof x[CONTENT] === 'function') toContent(x[CONTENT](), out);
    else if (typeof x === 'object' && typeof x[Symbol.iterator] === 'function') for (const v of x) toContent(v, out);
    else {
      ob.diag(0, 'splice-object', `an object spliced as text: ${String(x)}`, ...unitSpan());
      out.push(ob.makeText(String(x)));
    }
    return out;
  };
  // one node: a single value itself, several as a seq, none as an empty seq
  const one = (x) => {
    const xs = toContent(x);
    return xs.length === 1 ? xs[0] : ob.makeNode(KIND.seq, {}, xs);
  };
  const kidsOf = (kids) => kids.flatMap((k) => toContent(k));
  const styled = (bits) => (...kids) => ob.makeNode(KIND.styled, { bits }, kidsOf(kids));
  const node = (kind, args = {}) => (...kids) => ob.makeNode(kind, args, kidsOf(kids));
  // plain-text projection of a content value (term names → label strings)
  const shadowText = (x) =>
    isNode(x) ? (x.text !== undefined ? x.text : x.children.map(shadowText).join(''))
      : toContent(x).map(shadowText).join('');
  // --- region constructors (v2 §4.1) ---------------------------------------
  // children: an Array element is one source paragraph (array of rows, each
  // an array of cell values from top-level '|' segmentation); anything else
  // is a block child (nested list, fence, nested region).
  const tableBuild = (args, children) => {
    const rows = [];  // per row: array of per-cell shadow lists
    for (const ch of children) {
      if (Array.isArray(ch)) {
        for (const row of ch) rows.push(row.map((c) => toContent(c)));
      } else if (rows.length) {
        rows.at(-1).at(-1).push(...toContent(ch));  // continuation → last cell
      } else if (isNode(ch) && ch.kind === KIND.error) {
        rows.push([[ch]]);  // a failed first paragraph (its frame's error) stays visible
      }
      // other block content before the first row is dropped (documented limitation)
    }
    const cols = args.cols ?? rows.reduce((m, r) => Math.max(m, r.length), 1);
    const trows = rows.map((r) => {
      const cells = r.slice(0, cols);
      while (cells.length < cols) cells.push([]);
      return ob.makeNode(KIND.trow, {}, cells.map((c) => ob.makeNode(KIND.tcell, {}, c)));
    });
    return ob.makeNode(KIND.table, { cols, align: args.align, label: args.label }, trows);
  };
  const regionJoin = (children) => {
    // non-tabular interiors: rows of one source paragraph rejoin into one
    // para (cells reunited with " | " — segmentation is provenance, the
    // pipe convention belongs to the table constructor)
    const out = [];
    for (const ch of children) {
      if (Array.isArray(ch)) {
        const acc = [];
        ch.forEach((row, ri) => {
          if (ri) acc.push(ob.makeText(' '));
          row.forEach((cell, ci) => {
            if (ci) acc.push(ob.makeText(' | '));
            acc.push(...toContent(cell));
          });
        });
        out.push(ob.makeNode(KIND.para, {}, acc));
      } else out.push(...toContent(ch));
    }
    return out;
  };
  // #!figure default builder (figure-design.md §1): an image node from the
  // region args (if src is given) followed by the caption paragraphs
  const figureBuild = (args, children) => {
    const kids = [];
    if (args.src !== undefined) kids.push(ctors.image(args.src, args));
    kids.push(...regionJoin(children));
    return ob.makeNode(KIND.group, { role: 'figure', label: args.label }, kids);
  };
  const regionHandlers = {};
  const __region = (name, args = {}, children = []) => {
    const h = regionHandlers[name];
    let node;
    if (h) {
      // a throwing handler is contained here, like a fence handler (P0-05)
      try { node = one(h(args, children)); }
      catch (e) {  // invoke frame: an error node and its diagnostic (P2-01)
        const msg = String(e?.message ?? e);
        ob.diag(2, 'region-error', msg, ...unitSpan());
        return ob.makeNode(KIND.error, { message: msg, code: 'region-error' }, []);
      }
    }
    else if (name === 'table') node = tableBuild(args, children);
    else if (name === 'figure') node = figureBuild(args, children);
    // generic region → role-tagged group (#!figure, #!aside, …)
    else node = ob.makeNode(KIND.group, { role: name, label: args.label }, regionJoin(children));
    // region-level style scope: #!aside(font: '…', lang: "zh-TW")
    const scope = styleValues(args);
    if (Object.keys(scope).length) node = ob.makeNode(KIND.styled, scope, [node]);
    return node;
  };
  // --- fence dispatcher (v2 §4.1) ------------------------------------------
  const fenceHandlers = {};
  // offset: the body's source offset; lines: each body line's offset when the
  // fence sits in a quote or list item (its lines are not contiguous)
  const __fence = async (tag, args = {}, body = '', offset = 0, lines = null) => {
    const h = fenceHandlers[tag];
    // default path: the fence info args ARE codeblock grid options
    if (!h) return ctors.codeblock(tag, body, args);
    // invoke frame (P2-01): a handler's error is an error node and a
    // diagnostic at the body (ctx.error's localOffset into it)
    const mkErr = (msg, localOffset = 0) => {
      const at = offset + Math.max(0, Math.floor(Number(localOffset) || 0));
      ob.diag(2, 'fence-error', String(msg), at, at + 1);
      return ob.makeNode(KIND.error, { message: String(msg), code: 'fence-error' }, []);
    };
    const ctx = {
      args,
      offset,
      lineOffsets: lines,
      m: (...a) => ctors.m(...a),  // m.parse (WASM re-entry) is deferred
      error: mkErr,
      raw: (html, { width, height } = {}) =>
        ob.makeNode(KIND.raw, { html: String(html), w: width, h: height }, []),
    };
    try {
      return one(await h(body, ctx));
    } catch (e) {
      return mkErr(e?.message || e);
    }
  };
  const ctors = {
    __emit: (n) => { for (const x of toContent(n)) ob.emitNode(x); },
    __region,
    __fence,
    __at: (n, s, e) => { if (isNode(n)) ob.span(n, s, e); return n; },
    text: (s) => ob.makeText(String(s)),
    para: node(KIND.para),
    em: styled(STYLE_SUGAR.italic),
    strong: styled(STYLE_SUGAR.bold),
    heading: (level, label, ...kids) =>
      ob.makeNode(KIND.heading, { level, label: label ?? undefined }, kidsOf(kids)),
    ref: (target) => ob.makeNode(KIND.ref, { target: String(target) }, []),
    term: (name, ...desc) =>
      ob.makeNode(KIND.term, { name: shadowText(name) }, kidsOf(desc)),
    toc: Object.assign(() => ob.makeNode(KIND.collect, { what: 'toc' }, []), { [NULLARY]: true }),
    // footnotes (notes-design.md §1): ^[…] sugar → note; #notes() places
    // the collector explicitly (implicit at document end otherwise)
    notes: Object.assign(() => ob.makeNode(KIND.collect, { what: 'notes' }, []), { [NULLARY]: true }),
    note: node(KIND.note),
    // citations (notes-design.md §2): the data loads after the program
    // ran (splices are synchronous); the collector is then emitted at
    // document end with one formatted entry per key — the resolver
    // numbers cited keys and rebuilds the section in citation order
    bibliography: (src, o = {}) => {
      bibRequests.push({ src: String(src), all: !!o.all, s: here.s, e: here.e });
      return ob.makeText('');
    },
    glossary: Object.assign(() => ob.makeNode(KIND.collect, { what: 'glossary' }, []), { [NULLARY]: true }),
    list: (ordered, start, ...items) =>
      ob.makeNode(KIND.list, { ordered, start }, kidsOf(items)),
    item: node(KIND.item),
    quote: node(KIND.quote),
    // body: string (plain, split on \n at emit) OR array of lines, each a
    // shadow/string or array of runs (CH1 structured form). opts: grid args.
    codeblock: (lang, body, opts = {}) => {
      const args = { lang: String(lang), wrap: opts.wrap,
                     lineNo: opts.lineNo === true ? 1 : opts.lineNo,
                     hl: opts.hl, sidecar: opts.sidecar };
      if (Array.isArray(body)) {
        const lines = body.map((line) => ob.makeNode(KIND.seq, {},
          kidsOf(Array.isArray(line) ? line : [line])));
        return ob.makeNode(KIND.codeblock, args, lines);
      }
      return ob.makeNode(KIND.codeblock, args, [ob.makeText(String(body))]);
    },
    rule: Object.assign(node(KIND.rule), { [NULLARY]: true }),
    comment: (body) => ob.makeNode(KIND.comment, {}, [ob.makeText(String(body))]),
    link: (url, ...kids) => ob.makeNode(KIND.link, { url: String(url) }, kidsOf(kids)),
    code: (s) => ob.makeNode(KIND.code, {}, [ob.makeText(String(s))]),
    seq: node(KIND.seq),
    // opts: alt, scale (fraction of measure), w/h (intrinsic CSS px —
    // declaring both skips the NEED_IMAGES pull), float: "left"/"right" (F2)
    image: (src, opts = {}) => ob.makeNode(KIND.image, {
      src: String(src), alt: opts.alt, w: opts.w, h: opts.h,
      scale: opts.scale, side: opts.float ?? opts.side,
    }, []),
    mathinline: (src) => ob.makeNode(KIND.mathinline, { src: String(src) }, []),
    mathblock: (src, label) =>
      ob.makeNode(KIND.mathblock, { src: String(src), label }, []),
    // inline/block style scope (document-model §3): patch keys font (CSS
    // family list), lang (BCP-47), color, sizePx, plus bold/italic sugar
    // patch keys: font/lang/color/sizePx + bold/italic/underline/overline/
    // strike sugar (decorations are CH1 bits, metric-neutral)
    style: (patch = {}, ...kids) =>
      ob.makeNode(KIND.styled, { bits: styleBits(patch), ...styleValues(patch) },
                  kidsOf(kids)),
    // a splice (#x): undefined / null render nothing and say so (D-I05)
    val: (x) => {
      if (x === undefined || x === null) ob.diag(1, 'splice-undefined', `#… is ${x}: nothing rendered`, ...unitSpan());
      return one(x);
    },
    plain: (x) => shadowText(x),
    // block-granular error (plan P0-05): parse errors lowered by codegen
    error: (code, message) =>
      ob.makeNode(KIND.error, { message: String(message), code: String(code) }, []),
    // m`…` (interim, plan P2-01): the cooked strings as text and every
    // interpolation through toContent — a content value stays content;
    // markup re-entry (m.parse via WASM) comes later
    m: (strings, ...vals) => {
      const parts = [];
      for (let i = 0; i < strings.length; i++) {
        if (strings[i]) parts.push(ob.makeText(strings[i]));
        if (i < vals.length) toContent(vals[i], parts);
      }
      return parts.length === 1 ? parts[0] : ob.makeNode(KIND.seq, {}, parts);
    },
  };
  const styleStack = [];
  const bibRequests = [];
  const bibHooks = { format: null };
  // runs after the document program: load + format + emit bibliographies
  const finishBibliographies = async () => {
    for (const req of bibRequests) {
      let entries;
      try {
        entries = JSON.parse(await loadResource(req.src, opts));
        if (!Array.isArray(entries)) throw new Error('CSL-JSON array expected');
      } catch (e) {  // reported at the #bibliography call
        errorSpan(req.s, req.e, 'bib-load', `bibliography ${req.src}: ${e?.message ?? e}`);
        continue;
      }
      const fmt = bibHooks.format ?? formatEntryDefault;
      const kids = [];
      for (const e of entries) {
        if (!e || !e.id) continue;
        let inline;
        try { inline = fmt(e, ctors); }
        catch (err) {  // invoke frame: the entry shows the failure, and says so
          const msg = `bibliography ${req.src}: entry ${e.id}: ${err?.message ?? err}`;
          ob.diag(1, 'bib-load', msg, req.s, req.e);
          inline = [ctors.text(`⚠ ${err?.message ?? err}`)];
        }
        kids.push(ob.makeNode(KIND.group, { role: 'bibentry', name: String(e.id) }, kidsOf([inline])));
      }
      ob.emitNode(ob.makeNode(KIND.collect,
        { what: 'bibliography', form: req.all ? 'all' : undefined }, kids));
    }
  };
  const dollar = {
    fence(tag, fn) { fenceHandlers[tag] = fn; },     // registration precedes use
    region(name, fn) { regionHandlers[name] = fn; },
    bib: {
      set format(fn) { bibHooks.format = fn; },
      get format() { return bibHooks.format ?? formatEntryDefault; },
    },
    style: {
      // a patch object only (plan P2-01: the raw bit-number form is gone)
      push(x) {
        if (x === null || typeof x !== 'object') throw new TypeError('$.style.push takes a style patch object');
        styleStack.push(x);
        ob.stylePush(styleBits(x) || 0, styleValues(x));
      },
      get height() { return styleStack.length; },
      // a pop above the current height is clamped here and diagnosed by the
      // reader (style-underflow) — it used to grow the stack with holes
      popTo(h) {
        const t = Math.min(Math.max(0, Math.trunc(Number(h)) || 0), styleStack.length);
        styleStack.length = t;
        ob.stylePopTo(Number(h) > styleStack.length ? Math.trunc(Number(h)) : t);
      },
    },
  };
  // --- execution containment (plan P0-05 → P2-02, D-I10/D-I11) ------------
  // A frame that throws becomes an error at its place — an error block at
  // top level, an error node in its parent deeper down — and the style
  // stack returns to the frame's entry height (v2 §12). An unframed
  // (verbatim) statement that throws stops the program: the rest becomes
  // one error block.
  // an executor error: an error node at the block and its diagnostic (the
  // DIAG op, plan P2-01: the engine no longer scans error nodes for them)
  const errorSpan = (s, e, code, message) => {
    const n = ob.makeNode(KIND.error, { message, code }, []);
    ob.span(n, s, e);
    ob.diag(2, code, message, s, e);
    ob.emitNode(n);
  };
  const errorAt = (i, code, message, toEnd = false) => {
    const u = blocks[i];
    if (!u) return errorSpan(0, 0, code, message);
    errorSpan(u.s, toEnd ? Math.max(docEnd, u.e) : u.e, code, message);
  };
  const describe = (e) => `${e?.name ?? 'Error'}: ${e?.message ?? String(e)}`;
  const SYNTAX_MSG = 'SyntaxError: invalid JavaScript in this block';
  const failure = (err, h) => {
    if (styleStack.length > h) dollar.style.popTo(h);
    return err === STUB ? ['script-syntax', SYNTAX_MSG] : ['script-error', describe(err)];
  };
  // the interpreter's half (shared/lower.mjs)
  const env = {
    ob,
    ctors,
    here,
    height: () => styleStack.length,
    setCurrent: (i) => { current = i; },
    failBlock: (err, h, i) => errorAt(i, ...failure(err, h)),
    fail: (err, h, s, e) => {
      const [code, message] = failure(err, h);
      const n = ob.makeNode(KIND.error, { message, code }, []);
      ob.span(n, s, e);
      ob.diag(2, code, message, s, e);
      return n;
    },
  };
  const helpers = {
    // a verbatim statement the SyntaxError isolation stubbed
    syntax: (i) => errorAt(i, 'script-syntax', SYNTAX_MSG),
    failRest: (e) => {
      if (styleStack.length) dollar.style.popTo(0);
      errorAt(current, 'script-error',
              `${describe(e)} (the rest of the document was not executed)`, true);
    },
  };
  return { ctors, dollar, finishBibliographies, helpers, env };
}

const isSyntaxError = (e) => e?.name === 'SyntaxError' || e instanceof SyntaxError;

// --- the hole module (plan P2-02, D-I11) -------------------------------------
// Modules are cached by the program's hash of their text: an edit that leaves
// the user code alone (prose) imports nothing. Piece texts that compiled are
// remembered, so a SyntaxError is first looked for among the changed pieces.
const MODULE_CACHE = 16;
const moduleCache = new Map();  // hash → module (insertion order = LRU)
// imports attempted (the e2e checks that a cached or isolated module costs
// what lowering-design.md §5 says)
export const lowerStats = { imports: 0 };
const knownGood = new Set();    // piece texts that compiled in some module
const remember = (texts) => {
  if (knownGood.size > 20000) knownGood.clear();
  for (const t of texts) knownGood.add(t);
};

async function loadModule(prog, js) {
  const hit = moduleCache.get(prog.hash);
  if (hit) {
    moduleCache.delete(prog.hash);
    moduleCache.set(prog.hash, hit);
    return hit;
  }
  const text = typeof js === 'function' ? js() : js;
  const pieces = prog.pieces.map((pc, i) => ({ ...pc, i, text: text.slice(pc.js0, pc.js1) }));
  let mod;
  try {
    mod = await importModule(text);
    remember(pieces.map((pc) => pc.text));
  } catch (e) {
    if (!isSyntaxError(e) || !pieces.length) throw e;
    mod = await isolateSyntax(text, pieces);
  }
  if (mod.abi !== PROGRAM_ABI)
    throw new Error(`hole module ABI ${Number(mod.abi).toString(16)} differs from the runtime's ${PROGRAM_ABI.toString(16)}`);
  if (typeof mod.default !== 'function') throw new Error('hole module has no default export');
  moduleCache.set(prog.hash, mod);
  if (moduleCache.size > MODULE_CACHE) moduleCache.delete(moduleCache.keys().next().value);
  return mod;
}

// SyntaxError isolation, failure path only (D-I11): stub the pieces not known
// to compile (a hole → null, which its frame reports as script-syntax; a
// verbatim statement → __rt.syntax(block)), then restore them by bisection;
// what still does not compile stays stubbed. At most 2⌈log2 n⌉+4 imports
// for n suspects.
async function isolateSyntax(text, pieces) {
  const stubText = (pc) => (pc.kind === LPIECE.Hole ? 'null' : `__rt.syntax(${pc.ref});`);
  const build = (stubbed) => {
    let t = text;
    const order = [...stubbed].map((i) => pieces[i]).sort((a, b) => b.js0 - a.js0);
    for (const pc of order) t = t.slice(0, pc.js0) + stubText(pc) + t.slice(pc.js1);
    return t;
  };
  const tryImport = async (stubbed) => {
    try { return await importModule(build(stubbed)); }
    catch (e) { if (isSyntaxError(e)) return null; throw e; }
  };
  let suspects = pieces.filter((pc) => !knownGood.has(pc.text)).map((pc) => pc.i);
  let stubbed = new Set(suspects);
  let mod = suspects.length ? await tryImport(stubbed) : null;
  if (!mod && suspects.length < pieces.length) {  // the fault is in an unchanged piece
    suspects = pieces.map((pc) => pc.i);
    stubbed = new Set(suspects);
    mod = await tryImport(stubbed);
  }
  if (!mod) throw new SyntaxError('document program is invalid outside user code');
  let budget = 2 * Math.ceil(Math.log2(suspects.length + 1)) + 4;
  // knownBad: this group, restored, is known not to compile
  const visit = async (group, knownBad) => {
    if (!group.length || budget <= 0) return;  // exhausted: the group stays stubbed
    if (!knownBad) {
      budget--;
      const m = await tryImport(new Set([...stubbed].filter((i) => !group.includes(i))));
      if (m) {
        for (const i of group) stubbed.delete(i);
        mod = m;
        return;
      }
    }
    if (group.length === 1) return;  // the culprit: it stays stubbed
    const mid = group.length >> 1, a = group.slice(0, mid);
    await visit(a, false);
    // a restored cleanly: the fault is in the other half
    await visit(group.slice(mid), a.every((i) => !stubbed.has(i)));
  };
  await visit(suspects, true);  // all suspects restored = the module that failed
  remember(pieces.filter((pc) => !stubbed.has(pc.i)).map((pc) => pc.text));
  return mod;
}

async function importModule(jsText) {
  lowerStats.imports++;
  if (typeof URL !== 'undefined' && typeof Blob !== 'undefined' && typeof window !== 'undefined') {
    const url = URL.createObjectURL(new Blob([jsText], { type: 'text/javascript' }));
    try { return await import(/* @vite-ignore */ url); }
    finally { URL.revokeObjectURL(url); }
  }
  if (typeof process !== 'undefined' && process.versions?.node) {
    const { writeFileSync, rmSync, mkdtempSync } = await import('node:fs');
    const { tmpdir } = await import('node:os');
    const { join } = await import('node:path');
    const { pathToFileURL } = await import('node:url');
    const dir = mkdtempSync(join(tmpdir(), 'tsm-'));
    const file = join(dir, 'doc.mjs');
    writeFileSync(file, jsText);
    try { return await import(pathToFileURL(file).href); }
    finally { rmSync(dir, { recursive: true, force: true }); }
  }
  // worker scope: blob URLs work in module workers
  const url = URL.createObjectURL(new Blob([jsText], { type: 'text/javascript' }));
  try { return await import(/* @vite-ignore */ url); }
  finally { URL.revokeObjectURL(url); }
}

// compiled: { program: Uint8Array, js: string | () => string } — the
// LowerProgram and its hole module (js is only read when the module is not
// cached: a host passes a getter to skip copying it out of the engine).
// opts: { baseUrl } (browser/worker) or { baseDir, rootDir } (Node) — where
// #bibliography(src) and other document resources resolve.
export async function execute(compiled, opts = {}) {
  const prog = decodeProgram(compiled.program);
  const ob = new OpBuf();
  const { ctors, dollar, finishBibliographies, helpers, env } = buildContext(ob, opts, prog);
  const mod = prog.module ? await loadModule(prog, compiled.js) : null;
  const lowering = new Lowering(prog, env);
  const rt = {
    std: ctors,
    run: (h, seg) => lowering.run(h, seg),
    syntax: helpers.syntax,
  };
  try {
    if (mod) await mod.default(rt, dollar);
    else await lowering.run([], 0);
  } catch (e) {
    helpers.failRest(e);  // an unframed statement threw (D-I10)
  }
  await finishBibliographies();
  return ob.finalize();
}
