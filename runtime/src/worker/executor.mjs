// Executes the compiled document program against an OpBuf (architecture §4.1).
// Works in Node (temp-file import) and in browsers/workers (blob URL import).
import { KIND } from '../shared/ops.gen.mjs';
import { OpBuf } from '../shared/opbuf.mjs';

const CLS_EM = 1 << 2, CLS_BOLD = 1 << 3;  // frozen bits (document-model §3)

export function buildContext(ob) {
  const toShadow = (x) =>
    x && typeof x === 'object' && 'opId' in x ? x : ob.makeText(String(x));
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
  const regionHandlers = {};
  const __region = (name, args = {}, children = []) => {
    const h = regionHandlers[name];
    if (h) return toShadow(h(args, children));
    if (name === 'table') return tableBuild(args, children);
    // generic region → role-tagged group (#!figure, #!aside, …)
    return ob.makeNode(KIND.group, { role: name, label: args.label }, regionJoin(children));
  };
  // --- fence dispatcher (v2 §4.1) ------------------------------------------
  const fenceHandlers = {};
  const __fence = async (tag, args = {}, body = '', offset = 0) => {
    const h = fenceHandlers[tag];
    if (!h) return ob.makeNode(KIND.codeblock, { lang: tag }, [ob.makeText(body)]);
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
    glossary: () => ob.makeNode(KIND.collect, { what: 'glossary' }, []),
    list: (ordered, start, ...items) =>
      ob.makeNode(KIND.list, { ordered, start }, items.map(toShadow)),
    item: node(KIND.item),
    quote: node(KIND.quote),
    codeblock: (lang, body) =>
      ob.makeNode(KIND.codeblock, { lang: String(lang) }, [ob.makeText(String(body))]),
    rule: node(KIND.rule),
    comment: (body) => ob.makeNode(KIND.comment, {}, [ob.makeText(String(body))]),
    link: (url, ...kids) => ob.makeNode(KIND.link, { url: String(url) }, kids.map(toShadow)),
    code: (s) => ob.makeNode(KIND.code, {}, [ob.makeText(String(s))]),
    seq: node(KIND.seq),
    val: (x) => toShadow(x),
    // M1: cooked-text tag; runtime markup re-entry (m.parse via WASM) is M2.
    m: (strings, ...vals) => {
      let s = strings[0];
      for (let i = 0; i < vals.length; i++) s += String(vals[i]) + strings[i + 1];
      return ob.makeText(s);
    },
  };
  const styleStack = [];
  const dollar = {
    fence(tag, fn) { fenceHandlers[tag] = fn; },     // registration precedes use
    region(name, fn) { regionHandlers[name] = fn; },
    style: {
      push(bits) { styleStack.push(bits); ob.stylePush(bits); },
      get height() { return styleStack.length; },
      popTo(h) { styleStack.length = Math.max(0, h); ob.stylePopTo(h); },
    },
  };
  return { ctors, dollar };
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

export async function execute(jsText) {
  const ob = new OpBuf();
  const { ctors, dollar } = buildContext(ob);
  const mod = await importModule(jsText);
  if (typeof mod.default !== 'function') throw new Error('document program has no default export');
  await mod.default(ctors, dollar);
  return ob.finalize();
}
