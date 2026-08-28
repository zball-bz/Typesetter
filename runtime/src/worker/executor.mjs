// Executes the compiled document program against an OpBuf (architecture §4.1).
// Works in Node (temp-file import) and in browsers/workers (blob URL import).
import { KIND } from '../shared/ops.gen.mjs';
import { OpBuf } from '../shared/opbuf.mjs';

const CLS_EM = 1 << 2, CLS_BOLD = 1 << 3;  // frozen bits (document-model §3)
const CLS_UNDER = 1 << 16, CLS_OVER = 1 << 17, CLS_STRIKE = 1 << 18;
const styleBits = (p) =>
  ((p.bold ? CLS_BOLD : 0) | (p.italic ? CLS_EM : 0) |
   (p.underline ? CLS_UNDER : 0) | (p.overline ? CLS_OVER : 0) |
   (p.strike ? CLS_STRIKE : 0)) || undefined;

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
// page's base URL; Node reads the file — absolute paths against rootDir
// (the site/repo root), relative ones against the document's folder
async function loadResource(src, opts) {
  const s = String(src);
  if (typeof process !== 'undefined' && process.versions?.node && !/^https?:/.test(s)) {
    const { readFile } = await import('node:fs/promises');
    const { join, isAbsolute } = await import('node:path');
    const file = s.startsWith('/') ? join(opts.rootDir ?? process.cwd(), s)
      : isAbsolute(s) ? s : join(opts.baseDir ?? process.cwd(), s);
    return await readFile(file, 'utf8');
  }
  const url = opts.baseUrl ? new URL(s, opts.baseUrl) : s;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return await res.text();
}

export function buildContext(ob, opts = {}) {
  const toShadow = (x) => {
    if (typeof x === 'function') x = x();  // bare #toc / #glossary splices
    return x && typeof x === 'object' && 'opId' in x ? x : ob.makeText(String(x));
  };
  const styled = (bits) => (...kids) =>
    ob.makeNode(KIND.styled, { bits }, kids.map(toShadow));
  const node = (kind, args = {}) => (...kids) =>
    ob.makeNode(kind, args, kids.map(toShadow));
  // plain-text projection of a content value (term names → label strings)
  const shadowText = (x) =>
    x && typeof x === 'object' && 'opId' in x
      ? (x.text !== undefined ? x.text : x.children.map(shadowText).join(''))
      : String(x);
  // --- region constructors (v2 §4.1) ---------------------------------------
  // children: an Array element is one source paragraph (array of rows, each
  // an array of cell values from top-level '|' segmentation); anything else
  // is a block child (nested list, fence, nested region).
  const tableBuild = (args, children) => {
    const rows = [];  // per row: array of per-cell shadow lists
    for (const ch of children) {
      if (Array.isArray(ch)) {
        for (const row of ch) rows.push(row.map((c) => [toShadow(c)]));
      } else if (rows.length) {
        rows.at(-1).at(-1).push(toShadow(ch));  // continuation → last cell
      }
      // block content before the first row is dropped (documented limitation)
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
            acc.push(toShadow(cell));
          });
        });
        out.push(ob.makeNode(KIND.para, {}, acc));
      } else out.push(toShadow(ch));
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
    if (h) node = toShadow(h(args, children));
    else if (name === 'table') node = tableBuild(args, children);
    else if (name === 'figure') node = figureBuild(args, children);
    // generic region → role-tagged group (#!figure, #!aside, …)
    else node = ob.makeNode(KIND.group, { role: name, label: args.label }, regionJoin(children));
    // region-level style scope: #!aside(font: '…', lang: "zh-TW")
    if (args.font !== undefined || args.lang !== undefined ||
        args.color !== undefined || args.sizePx !== undefined) {
      node = ob.makeNode(KIND.styled, {
        font: args.font, lang: args.lang, color: args.color, sizePx: args.sizePx,
      }, [node]);
    }
    return node;
  };
  // --- fence dispatcher (v2 §4.1) ------------------------------------------
  const fenceHandlers = {};
  const __fence = async (tag, args = {}, body = '', offset = 0) => {
    const h = fenceHandlers[tag];
    // default path: the fence info args ARE codeblock grid options
    if (!h) return ctors.codeblock(tag, body, args);
    const mkErr = (msg) =>
      ob.makeNode(KIND.error, { message: String(msg), code: 'fence-error' }, []);
    const ctx = {
      args,
      offset,
      m: (...a) => ctors.m(...a),  // m.parse (WASM re-entry) is deferred
      error: mkErr,
      raw: (html, { width, height } = {}) =>
        ob.makeNode(KIND.raw, { html: String(html), w: width, h: height }, []),
    };
    try {
      return toShadow(await h(body, ctx));
    } catch (e) {
      return mkErr(e?.message || e);
    }
  };
  const ctors = {
    __emit: (n) => ob.emitNode(toShadow(n)),
    __region,
    __fence,
    __at: (n, s, e) => { ob.span(n, s, e); return n; },
    text: (s) => ob.makeText(String(s)),
    para: node(KIND.para),
    em: styled(CLS_EM),
    strong: styled(CLS_BOLD),
    heading: (level, label, ...kids) =>
      ob.makeNode(KIND.heading, { level, label: label ?? undefined }, kids.map(toShadow)),
    ref: (target) => ob.makeNode(KIND.ref, { target: String(target) }, []),
    term: (name, ...desc) =>
      ob.makeNode(KIND.term, { name: shadowText(name) }, desc.map(toShadow)),
    toc: () => ob.makeNode(KIND.collect, { what: 'toc' }, []),
    // footnotes (notes-design.md §1): ^[…] sugar → note; #notes() places
    // the collector explicitly (implicit at document end otherwise)
    notes: () => ob.makeNode(KIND.collect, { what: 'notes' }, []),
    note: node(KIND.note),
    // citations (notes-design.md §2): the data loads after the program
    // ran (splices are synchronous); the collector is then emitted at
    // document end with one formatted entry per key — the resolver
    // numbers cited keys and rebuilds the section in citation order
    bibliography: (src, o = {}) => {
      bibRequests.push({ src: String(src), all: !!o.all });
      return ob.makeText('');
    },
    glossary: () => ob.makeNode(KIND.collect, { what: 'glossary' }, []),
    list: (ordered, start, ...items) =>
      ob.makeNode(KIND.list, { ordered, start }, items.map(toShadow)),
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
          (Array.isArray(line) ? line : [line]).map(toShadow)));
        return ob.makeNode(KIND.codeblock, args, lines);
      }
      return ob.makeNode(KIND.codeblock, args, [ob.makeText(String(body))]);
    },
    rule: node(KIND.rule),
    comment: (body) => ob.makeNode(KIND.comment, {}, [ob.makeText(String(body))]),
    link: (url, ...kids) => ob.makeNode(KIND.link, { url: String(url) }, kids.map(toShadow)),
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
      ob.makeNode(KIND.styled, {
        bits: styleBits(patch),
        font: patch.font, lang: patch.lang, color: patch.color, sizePx: patch.sizePx,
      }, kids.map(toShadow)),
    val: (x) => toShadow(x),
    // M1: cooked-text tag; runtime markup re-entry (m.parse via WASM) is M2.
    m: (strings, ...vals) => {
      let s = strings[0];
      for (let i = 0; i < vals.length; i++) s += String(vals[i]) + strings[i + 1];
      return ob.makeText(s);
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
      } catch (e) {
        ob.emitNode(ob.makeNode(KIND.error, {
          message: `bibliography ${req.src}: ${e?.message ?? e}`, code: 'bib-load' }, []));
        continue;
      }
      const fmt = bibHooks.format ?? formatEntryDefault;
      const kids = [];
      for (const e of entries) {
        if (!e || !e.id) continue;
        let inline;
        try { inline = fmt(e, ctors); }
        catch (err) { inline = [ctors.text(`⚠ ${err?.message ?? err}`)]; }
        kids.push(ob.makeNode(KIND.group, { role: 'bibentry', name: String(e.id) },
                              (Array.isArray(inline) ? inline : [inline]).map(toShadow)));
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
      push(x) {
        styleStack.push(x);
        if (typeof x === 'number') ob.stylePush(x, {});
        else ob.stylePush(styleBits(x) || 0, {
          font: x.font, lang: x.lang, color: x.color, sizePx: x.sizePx,
        });
      },
      get height() { return styleStack.length; },
      popTo(h) { styleStack.length = Math.max(0, h); ob.stylePopTo(h); },
    },
  };
  return { ctors, dollar, finishBibliographies };
}

async function importModule(jsText) {
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

// opts: { baseUrl } (browser/worker) or { baseDir, rootDir } (Node) —
// where #bibliography(src) and other document resources resolve
export async function execute(jsText, opts = {}) {
  const ob = new OpBuf();
  const { ctors, dollar, finishBibliographies } = buildContext(ob, opts);
  const mod = await importModule(jsText);
  if (typeof mod.default !== 'function') throw new Error('document program has no default export');
  await mod.default(ctors, dollar);
  await finishBibliographies();
  return ob.finalize();
}
