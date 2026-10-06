// Executes a compiled document — its LowerProgram (run by shared/lower.mjs)
// and its hole module — against an OpBuf (architecture §4.1; plan P2-02).
// Works in Node (temp-file import) and in browsers/workers (blob URL import).
import { KIND } from '../shared/ops.gen.mjs';
import { OpBuf, isNode } from '../shared/opbuf.mjs';
import { createStd, styleBits, styleValues, NULLARY, CONTENT } from '../shared/stdlib.mjs';
import { decodeProgram, Lowering, STUB } from '../shared/lower.mjs';
import { BFLAG, LPIECE, PROGRAM_ABI } from '../shared/lower.gen.mjs';

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

// the content protocol's markers (plan P2-01), defined with the stdlib
export { NULLARY, CONTENT };

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
  // the constructors, regions and fences (plan P2-03: shared/stdlib.mjs)
  const S = createStd({
    ob,
    here,
    height: () => styleStack.length,
    popTo: (h) => dollar.style.popTo(h),
    // citations (notes-design.md §2): the data loads after the program
    // ran; the collector is then emitted at document end with one formatted
    // entry per key — the resolver numbers cited keys and rebuilds the
    // section in citation order
    bibliography: (src, o, s, e) => {
      bibRequests.push({ src: String(src), all: !!o.all, s, e });
      return ob.makeText('');
    },
  });
  const { std, kidsOf } = S;
  const styleStack = [];
  const bibRequests = [];
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
      const fmt = S.api.formatOf('bib') ?? formatEntryDefault;
      const kids = [];
      for (const e of entries) {
        if (!e || !e.id) continue;
        let inline;
        try { inline = fmt(e, std); }
        catch (err) {  // invoke frame: the entry shows the failure, and says so
          const msg = `bibliography ${req.src}: entry ${e.id}: ${err?.message ?? err}`;
          ob.diag(1, 'bib-load', msg, req.s, req.e);
          inline = [std.text(`⚠ ${err?.message ?? err}`)];
        }
        kids.push(ob.makeNode(KIND.group, { role: 'bibentry', name: String(e.id) }, kidsOf([inline])));
      }
      ob.emitNode(ob.makeNode(KIND.collect,
        { what: 'bibliography', form: req.all ? 'all' : undefined }, kids));
    }
  };
  const dollar = {
    // registration precedes use; each returns the constructor's trampoline
    ctor: S.api.ctor,
    region: S.api.region,
    fence: S.api.fence,
    declare: S.api.declare,
    bib: {
      set format(fn) { S.api.format('bib', fn); },
      get format() { return S.api.formatOf('bib') ?? formatEntryDefault; },
    },
    // $.std: the base constructors (unaffected by overrides), plain(), and
    // the manifest of what this execution has defined so far
    std: Object.freeze({ base: S.base, plain: S.plain, manifest: S.api.manifest }),
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
    here,
    std,
    call: S.call,
    region: S.region,
    fence: S.fence,
    val: std.val,
    emit: (n) => { for (const x of S.toContent(n)) ob.emitNode(x); },
    // a construct's result at its occurrence (plan P2-04): made during the
    // construct (id ≥ fresh) → SPAN unless it has one; an earlier value
    // spliced here → an AT alias
    at: (n, s, e, fresh) => {
      if (!isNode(n)) return n;
      if (n.opId >= fresh) {
        if (!ob.spans.has(n.opId)) ob.span(n, s, e);
        return n;
      }
      return ob.at(n, s, e);
    },
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
  return { std, dollar, finishBibliographies, helpers, env };
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
  const { std, dollar, finishBibliographies, helpers, env } = buildContext(ob, opts, prog);
  const mod = prog.module ? await loadModule(prog, compiled.js) : null;
  const lowering = new Lowering(prog, env);
  const rt = {
    std,
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
