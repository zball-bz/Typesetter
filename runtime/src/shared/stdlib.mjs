// The constructor ABI (plan P2-03; design T2 S6; docs/ctor-design.md). Every
// constructor — a kind's, a derived one, or one the document defines — has
// one calling convention: the binder turns its arguments into a bound call
// {attrs, kids, lines, body, options} against the constructor's spec
// (engine/schema/schema.json → ctors.gen.mjs), and the call goes through the
// registry's entry for that name, whose `next` chain an override ($.ctor)
// extends. Markup arrives pre-bound (the LowerProgram's CALL ops) and skips
// the binder, so `*x*`, `#strong[x]` and an override of strong agree.
// A region is a constructor with a Body parameter. One std per execution:
// its constructors write into that execution's OpBuf.
import { KIND, SCHEMA, DECLS } from './ops.gen.mjs';
import { CTOR_SPECS, STD_ALIASES } from './ctors.gen.mjs';
import { isNode } from './opbuf.mjs';
import { STYLE_KEYS, STYLE_SUGAR, validDomain } from './props.gen.mjs';
import { Registry } from './registry.mjs';

// The content protocol's markers (plan P2-01; design T2 S4): a function a
// bare splice may call (#toc), and an object's own conversion to content
export const NULLARY = Symbol.for('tsm.nullary');
export const CONTENT = Symbol.for('tsm.content');

// style patches from the schema's run properties (plan P1-02): boolean sugar
// keys set flag bits, value keys map to styled/STYLE_PUSH attributes
export const styleBits = (p) => {
  let bits = 0;
  for (const [k, bit] of Object.entries(STYLE_SUGAR)) if (p[k]) bits |= bit;
  return bits || undefined;
};
export const styleValues = (p) => {
  const out = {};
  for (const [k, attr] of Object.entries(STYLE_KEYS)) if (p[k] !== undefined) out[attr] = p[k];
  return out;
};

// ---- specs ------------------------------------------------------------------
// a param's accepted JS type, from its attribute domain
const typeOf = (dom) => {
  if (!dom) return null;
  const t = dom.split(':')[0];
  return t === 'int' || t === 'num' || t === 'flags' ? 'number' : t === 'bool' ? 'boolean'
    : t === 'str' ? 'str' : 'string';
};
const prepare = (spec) => Object.freeze({
  ...spec,
  params: spec.params.map((p) => Object.freeze({ ...p, t: typeOf(p.dom) })),
  body: spec.params.some((p) => p.k === 'body'),
  text: spec.params.some((p) => p.k === 'text'),
  order: spec.kind ? Object.keys(SCHEMA[spec.kind].attrs) : [],
});
export const SPECS = Object.freeze(Object.fromEntries(
  Object.entries(CTOR_SPECS).map(([n, s]) => [n, prepare(s)])));
// a constructor the document defines without a base: options pass through
const USER_SPEC = prepare({ kind: null, params: [], options: 'raw', nullary: false, sealed: false });
const BODY_SPEC = prepare({ kind: null, params: [{ k: 'body' }], options: 'raw', nullary: false, sealed: false });

// the static manifest (no execution): names, params, options, Body, for
// editors (completion) and printers
export function staticManifest() {
  return Object.entries(SPECS).map(([name, s]) => describe(name, s, false));
}
function describe(name, s, user) {
  return {
    name, kind: s.kind, params: s.params.map((p) => p.name ?? p.k),
    options: s.options === 'raw' ? 'raw' : [...s.options], body: s.body,
    nullary: s.nullary, sealed: s.sealed, user,
  };
}

const SCHEMA_LEVEL = Object.fromEntries(Object.values(SCHEMA).map((k) => [k.id, k.level]));
const BODY = Symbol('tsm.body');
export const isBody = (x) => x !== null && typeof x === 'object' && x[BODY] === true;
const isPlainObject = (x) => {
  if (x === null || typeof x !== 'object' || Array.isArray(x) || isNode(x) || isBody(x)) return false;
  const proto = Object.getPrototypeOf(x);
  return proto === Object.prototype || proto === null;
};
const accepts = (p, a) => {
  switch (p.k) {
    case 'attr':
      if (a === null || a === undefined) return true;  // present but absent
      if (p.t === 'str') return typeof a === 'string' || typeof a === 'number';
      return typeof a === p.t;
    case 'projected': return typeof a === 'string' || typeof a === 'number' || isNode(a);
    case 'text': return typeof a === 'string' || typeof a === 'number';
    case 'lines': return typeof a === 'string' || Array.isArray(a);
    case 'body': return isBody(a);
  }
  return false;
};

// ---- one std per execution ----------------------------------------------------
// host: { ob, here ({s, e}: where the run is), height(), popTo(h),
// bibliography(src, options, s, e) }
export function createStd(host) {
  const { ob, here } = host;
  const diag = (sev, code, msg) => ob.diag(sev, code, msg, here.s, here.e);

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
        diag(2, 'splice-function', msg);
      }
    } else if (typeof x === 'object' && typeof x[CONTENT] === 'function') toContent(x[CONTENT](), out);
    else if (typeof x === 'object' && typeof x[Symbol.iterator] === 'function') for (const v of x) toContent(v, out);
    else {
      diag(0, 'splice-object', `an object spliced as text: ${String(x)}`);
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
  // the plain-text projection of content (term names → label strings); it
  // writes nothing
  const plain = (x) => {
    if (isNode(x)) return x.text !== undefined ? x.text : x.children.map(plain).join('');
    if (typeof x === 'string' || typeof x === 'number' || typeof x === 'bigint') return String(x);
    if (Array.isArray(x)) return x.map(plain).join('');
    if (x !== null && typeof x === 'object' && typeof x[CONTENT] === 'function') return plain(x[CONTENT]());
    return '';
  };
  // a kind's attributes in its writer order (schema.json), absent ones out
  const ordered = (order, attrs) => {
    const out = {};
    for (const k of order) {
      const v = attrs[k];
      if (v !== undefined && v !== null) out[k] = v;
    }
    return out;
  };

  // ---- the binder: arguments → bound call -------------------------------
  // 1. positional params bind left to right while the param accepts the
  //    argument (Attr: a scalar of its domain's type, null = absent;
  //    Projected: string, number or node, via plain(); Text: one text kid;
  //    Lines: a string or an array of lines; Body: a Body); the first
  //    refusal ends positional binding — so #heading(2)[T] and
  //    #list(false)[a] bind their content as kids;
  // 2. a plain object next is the options object: attributes by name
  //    (aliases float→side, width/height→w/h), an unknown one dropped with
  //    a ctor-arg warning — unless the spec passes options raw;
  // 3. the rest are kids, through toContent.
  const bind = (name, spec, args) => {
    const call = { attrs: {}, kids: [], lines: undefined, body: undefined, options: undefined, style: undefined };
    let i = 0;
    for (const p of spec.params) {
      if (i >= args.length || !accepts(p, args[i])) break;
      const a = args[i++];
      if (p.k === 'attr') { if (a !== null && a !== undefined) call.attrs[p.name] = p.t === 'str' ? String(a) : a; }
      else if (p.k === 'projected') call.attrs[p.name] = plain(a);
      else if (p.k === 'text') call.kids.push(ob.makeText(String(a)));
      else if (p.k === 'lines') call.lines = a;
      else call.body = a;
    }
    if (i < args.length && isPlainObject(args[i])) call.options = args[i++];
    for (; i < args.length; i++) toContent(args[i], call.kids);
    if (call.options && spec.options !== 'raw') {
      const viaAlias = new Set();
      for (const [k0, v] of Object.entries(call.options)) {
        if (k0 === 'style' && isPlainObject(v)) {  // universal: a styled scope around the result
          call.style = v;
          continue;
        }
        if (k0 === 'ext' && spec.options.includes('ext')) {  // EXT: scalar data under checked names
          if (!isPlainObject(v)) { diag(1, 'ctor-arg', `${name}: ext takes an object of names to values`); continue; }
          const ext = {};
          for (const [n, x] of Object.entries(v)) {
            if (!validDomain('extname', n)) diag(1, 'ctor-arg', `${name}: ext name ${n} is not [a-z][a-z0-9-]*`);
            else if (typeof x !== 'string' && typeof x !== 'number' && typeof x !== 'boolean') {
              if (x !== undefined && x !== null) diag(1, 'ctor-arg', `${name}: ext ${n} is not a scalar`);
            } else ext[n] = x;
          }
          call.attrs.ext = ext;
          continue;
        }
        const alias = STD_ALIASES[k0];
        const k = alias && spec.options.includes(alias) ? alias : k0;
        if (!spec.options.includes(k)) {
          diag(1, 'ctor-arg', `${name}: unknown option ${k0}`);
          continue;
        }
        if (v === undefined || (viaAlias.has(k) && !alias)) continue;  // an alias wins
        call.attrs[k] = v;
        if (alias) viaAlias.add(k);
      }
    }
    if (spec.body && !call.body) call.body = kidsBody(call.kids);
    return call;
  };

  // ---- Body: a region's interior, or a constructor's content kids --------
  // items (a region's interior, as the interpreter evaluated it): an Array
  // is one source paragraph of table rows (rows of cell values); anything
  // else is a block. blocks() is the interior as blocks — the rows of a
  // paragraph rejoined into one para with " | " between cells (segmentation
  // is provenance; the pipe belongs to the table) — built once; rows() is
  // the table's reading: one row per source row, a later block continuing
  // the last cell. Eager in this step: both read values already built.
  const regionBody = (items) => {
    let blocks;
    return Object.freeze({
      [BODY]: true,
      blocks: () => (blocks ??= regionJoin(items)),
      rows: () => tableRows(items),
    });
  };
  // content kids as a Body: blocks() is the kids as blocks — inline kids
  // (a sole paragraph, unwrapped by the parser) become one para again, so
  // #f(H)[x] reads like #!f(H) x #f!
  const isBlock = (n) => SCHEMA_LEVEL[n.kind] === 'block' ||
    ((n.kind === KIND.seq || n.kind === KIND.styled) && n.children.some(isBlock));
  const kidsBody = (kids) => {
    let blocks;
    return Object.freeze({
      [BODY]: true,
      kids,
      blocks: () => (blocks ??= kids.length && !kids.some(isBlock) ? [ob.makeNode(KIND.para, {}, kids)] : kids),
      rows: () => null,
    });
  };
  const regionJoin = (items) => {
    const out = [];
    for (const ch of items) {
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
  const tableRows = (items) => {
    const rows = [];  // per row: array of per-cell content lists
    for (const ch of items) {
      if (Array.isArray(ch)) {
        for (const row of ch) rows.push(row.map((c) => toContent(c)));
      } else if (rows.length) {
        rows.at(-1).at(-1).push(...toContent(ch));  // continuation → last cell
      } else if (isNode(ch) && ch.kind === KIND.error) {
        rows.push([[ch]]);  // a failed first paragraph (its frame's error) stays visible
      }
      // other block content before the first row is dropped (documented limitation)
    }
    return rows;
  };

  // ---- base implementations (the registry's first entries) ---------------
  const kindImpl = (spec) => {
    const kind = KIND[spec.kind];
    return (call) => ob.makeNode(kind, ordered(spec.order, call.attrs), call.kids);
  };
  const styledBits = (bits) => (call) => ob.makeNode(KIND.styled, { bits }, call.kids);
  const collect = (what) => () => ob.makeNode(KIND.collect, { what }, []);
  const impls = {
    text: (call) => call.kids[0] ?? ob.makeText(''),
    error: (call) => ob.makeNode(KIND.error,
      { message: String(call.attrs.message), code: String(call.attrs.code) }, []),
    // lines: a string (one text body, split at emit) or an array of lines,
    // each a node, a string or an array of runs (CH1 structured form)
    codeblock: (call) => {
      const a = call.attrs;
      if (a.lineNo === true) a.lineNo = 1;  // schema coerce boolAsInt
      let kids = call.kids;
      if (Array.isArray(call.lines)) {
        kids = call.lines.map((line) => ob.makeNode(KIND.seq, {},
          kidsOf(Array.isArray(line) ? line : [line])));
      } else if (call.lines !== undefined) kids = [ob.makeText(String(call.lines))];
      return ob.makeNode(KIND.codeblock, ordered(SPECS.codeblock.order, a), kids);
    },
    table: (call) => {
      const body = call.body;
      if (body?.kids) {  // table(opts, row(…), …): its kids are its rows
        const trows = body.kids.map((k) => (k.kind === KIND.trow ? k
          : ob.makeNode(KIND.trow, {}, [ob.makeNode(KIND.tcell, {}, [k])])));
        return ob.makeNode(KIND.table, ordered(SPECS.table.order, call.attrs), trows);
      }
      const rows = body ? body.rows() : [];
      const cols = call.attrs.cols ?? rows.reduce((m, r) => Math.max(m, r.length), 1);
      const trows = rows.map((r) => {
        const cells = r.slice(0, cols);
        while (cells.length < cols) cells.push([]);
        return ob.makeNode(KIND.trow, {}, cells.map((c) => ob.makeNode(KIND.tcell, {}, c)));
      });
      return ob.makeNode(KIND.table, ordered(SPECS.table.order, { ...call.attrs, cols }), trows);
    },
    strong: styledBits(STYLE_SUGAR.bold),
    em: styledBits(STYLE_SUGAR.italic),
    // inline/block style scope (document-model §3): patch keys font (CSS
    // family list), lang (BCP-47), color, sizePx, plus the bold/italic/
    // underline/overline/strike sugar (decorations are CH1 bits)
    style: (call) => {
      const p = call.options ?? {};
      return ob.makeNode(KIND.styled, { bits: styleBits(p), ...styleValues(p) }, call.kids);
    },
    // #!figure (figure-design.md §1): an image from the region's src (only
    // the image's own options: never label or role), then the caption
    figure: (call) => {
      const a = call.options ?? {};
      const kids = [];
      if (a.src !== undefined) {
        const o = {};
        for (const k of ['alt', 'w', 'h', 'scale', 'float', 'side']) if (a[k] !== undefined) o[k] = a[k];
        kids.push(base.image(a.src, o));
      }
      kids.push(...call.body.blocks());
      return ob.makeNode(KIND.group, { role: 'figure', label: a.label }, kids);
    },
    toc: collect('toc'),
    glossary: collect('glossary'),
    notes: collect('notes'),
    // citations (notes-design.md §2): the data loads after the program ran;
    // the collector is emitted at document end
    bibliography: (call) => host.bibliography(call.attrs.src, call.options ?? {}, here.s, here.e),
    // node(kind, attrs, ...kids): any public kind, every attribute by name
    node: (call) => {
      const kind = call.attrs.kind;
      const spec = Object.values(SPECS).find((s) => s.kind === kind && !s.derived);
      if (!spec || kind === 'text') throw new TypeError(`node: no constructor for kind ${kind}`);
      const attrs = {};
      for (const [k0, v] of Object.entries(call.options ?? {})) {
        const k = STD_ALIASES[k0] && spec.order.includes(STD_ALIASES[k0]) ? STD_ALIASES[k0] : k0;
        if (spec.order.includes(k)) attrs[k] = v;
        else diag(1, 'ctor-arg', `node(${kind}): unknown attribute ${k0}`);
      }
      return ob.makeNode(KIND[kind], ordered(spec.order, attrs), call.kids);
    },
  };

  const registry = new Registry();
  for (const [name, spec] of Object.entries(SPECS)) {
    const impl = impls[name] ?? (spec.kind ? kindImpl(spec) : null);
    if (!impl) throw new Error(`std: no implementation for ${name}`);
    registry.define('ctor', name, () => impl, { spec, sealed: spec.sealed });
  }

  // ---- invocation and trampolines ------------------------------------------
  // An entry the document defined (an override, a region handler) runs in a
  // hook frame: its result goes through toContent; a throw is an error node
  // in place and a diagnostic; the style stack returns to the entry height;
  // nesting deeper than 64 is hook-recursion. Built-in entries run plain.
  let depth = 0;
  const ctxOf = (name, call) => ({
    args: call.options ?? {}, label: call.options?.label, span: [here.s, here.e],
    std, plain, m: std.m,
  });
  const hookError = (code, message) => {
    diag(2, code, message);
    return ob.makeNode(KIND.error, { message, code }, []);
  };
  const invoke = (name, entry, call, code = 'ctor-error') => {
    if (!entry.user) return entry.fn(call);  // (built-ins read only the call)
    if (depth >= 64) return hookError('hook-recursion', `${name}: constructors nested deeper than 64`);
    depth++;
    const h0 = host.height();
    const fail = (e) => {
      if (host.height() > h0) host.popTo(h0);
      return hookError(code, String(e?.message ?? e));
    };
    let r;
    try {
      r = entry.fn(call, ctxOf(name, call));
    } catch (e) {
      depth--;
      return fail(e);
    }
    // an async hook (a region or fence handler): contained when it settles;
    // a synchronous one allocates no promise
    if (r !== null && typeof r === 'object' && typeof r.then === 'function') {
      return Promise.resolve(r).then((v) => { depth--; return one(v); }, (e) => { depth--; return fail(e); });
    }
    depth--;
    try {
      return one(r);
    } catch (e) {
      return fail(e);
    }
  };
  const tramps = new Map();
  const trampoline = (name) => {
    let t = tramps.get(name);
    if (t) return t;
    t = function (...args) {
      const entry = registry.get('ctor', name);
      if (!entry) throw new TypeError(`${name} is not a constructor`);
      const c = bind(name, entry.spec, args);
      const r = invoke(name, entry, c);
      // the universal style option (plan P2-05): a styled scope around it
      if (!c.style || !isNode(r)) return r;
      return ob.makeNode(KIND.styled, { bits: styleBits(c.style), ...styleValues(c.style) }, [r]);
    };
    Object.defineProperty(t, 'name', { value: name });
    if (registry.get('ctor', name)?.spec.nullary) t[NULLARY] = true;
    tramps.set(name, t);
    return t;
  };

  // a markup CALL (the LowerProgram): attributes arrive bound by name
  const call = (name, kv, kids) => {
    if (name === 'text') return ob.makeText(String(kv.text));
    const entry = registry.get('ctor', name);
    if (entry.spec.text) kids.unshift(ob.makeText(String(kv.text)));
    return invoke(name, entry, { attrs: kv, kids, lines: undefined, body: undefined, options: undefined });
  };

  // a region (#!name(args) … #name!): the constructor `name` if it takes a
  // Body, else the default region — a group of role `name`; either way the
  // legacy style keys of the header scope the result (#!aside(lang: …))
  const region = async (name, args = {}, items = []) => {
    const entry = registry.get('ctor', name);
    const body = regionBody(items);
    let node;
    if (entry?.spec.body) {
      const attrs = {};
      if (entry.spec.options !== 'raw')
        for (const k of entry.spec.options) if (args[k] !== undefined) attrs[k] = args[k];
      node = await invoke(name, entry, { attrs, kids: [], lines: undefined, body, options: args }, 'region-error');
    } else {
      node = ob.makeNode(KIND.group, { role: name, label: args.label }, body.blocks());
    }
    const scope = styleValues(args);
    if (Object.keys(scope).length) node = ob.makeNode(KIND.styled, scope, [node]);
    return node;
  };

  // a fence: its handler (fence namespace), else the code block of its tag
  // with the info arguments as the block's options. offset: the body's source
  // offset; lines: each body line's offset when the fence sits in a quote
  // or list item (its lines are not contiguous)
  const fence = async (tag, args = {}, body = '', offset = 0, lines = null, end = offset, info = '') => {
    const entry = registry.get('fence', tag);
    if (!entry) {
      // the default: a code block whose text carries the body's span
      // (plan P2-04; its lines inside a quote or list item are not
      // contiguous, so the span covers them from the first to the last)
      const cb = std.codeblock(tag, body, args);
      const t = cb.children?.[0];
      if (cb.kind === KIND.codeblock && t?.text !== undefined && !ob.spans.has(t.opId)) ob.span(t, offset, end);
      return cb;
    }
    // invoke frame (P2-01): a handler's error is an error node and a
    // diagnostic at the body (ctx.error's localOffset into it)
    const mkErr = (msg, localOffset = 0) => {
      const at = offset + Math.max(0, Math.floor(Number(localOffset) || 0));
      ob.diag(2, 'fence-error', String(msg), at, at + 1);
      return ob.makeNode(KIND.error, { message: String(msg), code: 'fence-error' }, []);
    };
    const ctx = {
      args,
      label: args.label,
      info,  // the opener's free words after the tag (plan P2-06)
      offset,
      lineOffsets: lines,
      m: (...a) => std.m(...a),  // m.parse (WASM re-entry) is P2-13
      error: mkErr,
      raw: (html, { width, height } = {}) =>
        ob.makeNode(KIND.raw, { html: String(html), w: width, h: height }, []),
      std,
      plain,
    };
    try {
      return one(await entry.fn(body, ctx));
    } catch (e) {
      return mkErr(e?.message || e);
    }
  };

  // ---- the user-visible std ------------------------------------------------
  const std = {};
  for (const name of Object.keys(SPECS)) std[name] = trampoline(name);
  // the base constructors (builders call these, so an override of image
  // does not reach into figure)
  const base = {};
  for (const name of Object.keys(SPECS)) {
    const entry = registry.get('ctor', name);
    base[name] = (...args) => entry.fn(bind(name, entry.spec, args));
  }
  // the sealed text and error keep their coercing signatures: text(x) is
  // String(x) as text, error(code, message) both as strings
  std.text = base.text = Object.defineProperty((s) => ob.makeText(String(s)), 'name', { value: 'text' });
  std.error = base.error = Object.defineProperty((code, message) => ob.makeNode(KIND.error,
    { message: String(message), code: String(code) }, []), 'name', { value: 'error' });
  // a splice (#x): undefined / null render nothing and say so (D-I05)
  std.val = (x) => {
    if (x === undefined || x === null) diag(1, 'splice-undefined', `#… is ${x}: nothing rendered`);
    return one(x);
  };
  std.plain = plain;
  // m`…` (interim until P2-13): the cooked strings as text and every
  // interpolation through toContent — a content value stays content
  std.m = (strings, ...vals) => {
    const parts = [];
    for (let i = 0; i < strings.length; i++) {
      if (strings[i]) parts.push(ob.makeText(strings[i]));
      if (i < vals.length) toContent(vals[i], parts);
    }
    return parts.length === 1 ? parts[0] : ob.makeNode(KIND.seq, {}, parts);
  };

  // ---- $.ctor / $.region / $.fence / $.bib.format / $.std -------------------
  const missingNext = (name) => () => { throw new TypeError(`${name} has no previous definition to delegate to`); };
  const api = {
    // $.ctor(name, next => (call, ctx) => content): an override of a
    // constructor (or a new one); returns its trampoline
    ctor(name, factory) {
      if (typeof name !== 'string' || typeof factory !== 'function')
        throw new TypeError('$.ctor(name, next => (call, ctx) => content)');
      const prev = registry.get('ctor', name);
      registry.define('ctor', name, (next) => factory(next ?? missingNext(name)),
                      { spec: prev?.spec ?? USER_SPEC, user: true });
      return (std[name] ??= trampoline(name));
    },
    // $.region(name, (body, ctx) => content): a constructor with a Body;
    // ctx.args is the header's options, ctx.next(body, {args}) delegates (to
    // the default region when nothing was defined before)
    region(name, fn) {
      if (typeof name !== 'string' || typeof fn !== 'function')
        throw new TypeError('$.region(name, (body, ctx) => content)');
      const prev = registry.get('ctor', name);
      registry.define('ctor', name, (next) => (c, ctx) => fn(c.body, {
        ...ctx,
        next: (body = c.body, o = {}) => {
          const nc = { ...c, body, options: o.args ?? c.options };
          return next ? next(nc, { ...ctx, args: nc.options ?? {} })
            : ob.makeNode(KIND.group, { role: name, label: nc.options?.label }, body.blocks());
        },
      }), { spec: prev?.spec?.body ? prev.spec : BODY_SPEC, user: true });
      return (std[name] ??= trampoline(name));
    },
    // $.fence(tag, (body, ctx) => content); ctx.next(body) delegates (to the
    // code block of the tag when nothing was defined before)
    fence(tag, fn) {
      if (typeof tag !== 'string' || typeof fn !== 'function')
        throw new TypeError('$.fence(tag, (body, ctx) => content)');
      registry.define('fence', tag, (next) => (body, ctx) => fn(body, {
        ...ctx,
        next: (b = body) => (next ? next(b, ctx) : std.codeblock(tag, b, ctx.args)),
      }), { user: true });
    },
    format(name, fn) {
      registry.define('format', name, () => fn, { user: true });
    },
    // $.declare(type, name, data, ...templates) (plan P2-05): a typed
    // declaration at this point of the flow — DECL (schema "decls": element,
    // counter, collector, rule, math.*, …); data is EXT (scalar values under
    // [a-z][a-z0-9-]* names), the templates are content
    declare(type, name, data = {}, ...templates) {
      const d = DECLS[type];
      if (!d) throw new TypeError(`$.declare: unknown declaration type ${type}`);
      if (typeof name !== 'string' || !name) throw new TypeError('$.declare: a name is required');
      const ext = {};
      if (!isPlainObject(data)) throw new TypeError('$.declare: data is an object of names to values');
      for (const [n, x] of Object.entries(data)) {
        if (!validDomain('extname', n)) throw new TypeError(`$.declare: data name ${n} is not [a-z][a-z0-9-]*`);
        if (typeof x !== 'string' && typeof x !== 'number' && typeof x !== 'boolean')
          throw new TypeError(`$.declare: data ${n} is not a scalar`);
        ext[n] = x;
      }
      ob.decl(d.id, here.s, here.e, name, ext, kidsOf(templates));
    },
    formatOf(name) {
      return registry.get('format', name)?.fn;
    },
    manifest() {
      return {
        ctors: registry.names('ctor').map((n) => {
          const e = registry.get('ctor', n);
          return describe(n, e.spec, e.user);
        }),
        fences: registry.names('fence'),
      };
    },
  };

  return { std, base, call, region, fence, toContent, one, kidsOf, plain, api };
}
