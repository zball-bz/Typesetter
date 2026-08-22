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
  const ctors = {
    __emit: (n) => ob.emitNode(toShadow(n)),
    __at: (n, s, e) => { ob.span(n, s, e); return n; },
    text: (s) => ob.makeText(String(s)),
    para: node(KIND.para),
    em: styled(CLS_EM),
    strong: styled(CLS_BOLD),
    heading: (level, ...kids) => ob.makeNode(KIND.heading, { level }, kids.map(toShadow)),
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
