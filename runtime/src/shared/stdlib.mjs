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
import { KIND, SCHEMA, DECLS, ARGK } from './ops.gen.mjs';
import { CTOR_SPECS, STD_ALIASES } from './ctors.gen.mjs';
import { isNode } from './opbuf.mjs';
import { STYLE_KEYS, STYLE_SUGAR, STYLE_FLAGS, validDomain } from './props.gen.mjs';
import { Registry } from './registry.mjs';

// The content protocol's markers (plan P2-01; design T2 S4): a function a
// bare splice may call (#toc), and an object's own conversion to content
export const NULLARY = Symbol.for('tsm.nullary');
export const CONTENT = Symbol.for('tsm.content');

// A style patch → its styled attributes (plans P1-02, P2-08; design T4
// ScopeDelta): the property keys of the schema's run rows — font, lang,
// color, size ('0.7em', '70%', '22px'; a number is em), sizePx, weight,
// italic, fontRole, baseline, decoration ([under|over|strike]), code: {hang}
// — and the boolean sugar bold, underline, overline, strike (bold: false is
// weight 400). `unknown(key)` hears every other key. A style change on the
// wire is a delta node: a childless styled node with these attributes.
export const styleAttrs = (p, unknown) => {
  const out = {};
  const put = (attr, v, key) => {
    if (STYLE_FLAGS[attr]) {  // a flag set: a name or a list of names, ORed
      for (const nm of Array.isArray(v) ? v : [v]) {
        const f = STYLE_FLAGS[attr][String(nm)];
        if (f) out[attr] = (out[attr] ?? 0) | f;
        else unknown?.(`${key}: ${nm}`);
      }
    } else if (attr === 'size' && typeof v === 'number') out[attr] = `${v}em`;
    else out[attr] = v;
  };
  for (const [k, v] of Object.entries(p ?? {})) {
    if (v === undefined || v === null) continue;
    if (k === 'code' && typeof v === 'object' && !Array.isArray(v)) {
      for (const [k2, v2] of Object.entries(v)) {
        const a = STYLE_KEYS[`code.${k2}`];
        if (a && v2 !== undefined && v2 !== null) put(a, v2, `code.${k2}`);
        else if (!a) unknown?.(`code.${k2}`);
      }
    } else if (STYLE_KEYS[k]) put(STYLE_KEYS[k], v, k);
    else if (STYLE_SUGAR[k]) {
      const [a, val] = STYLE_SUGAR[k];
      if (v) put(a, STYLE_FLAGS[a] ? Object.keys(STYLE_FLAGS[a]).find((n) => STYLE_FLAGS[a][n] === val) : val, k);
      else if (a === 'weight') out.weight = 400;
    } else unknown?.(k);
  }
  // the writer's order is the schema's (styled's attribute rows)
  return Object.fromEntries(STYLED_ORDER.filter((a) => a in out).map((a) => [a, out[a]]));
};
const STYLED_ORDER = Object.keys(SCHEMA.styled.attrs);
// a key that names a style row or its sugar (a region option spelled so is a
// mistake: style: {…} carries style, plan P2-08)
export const isStyleKey = (k) => k in STYLE_KEYS || k in STYLE_SUGAR || k === 'code';

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

// A soft break (U+000A in inline text, plan P2-10) as the engine resolves it
// (model/softbreak.h): nothing between two wide characters, else a space.
// The wide classes here approximate TextRules' (CJK ideographs, kana, Hangul,
// fullwidth forms and CJK punctuation).
const WIDE = /[\u1100-\u115F\u2E80-\u303E\u3041-\u33FF\u3400-\u4DBF\u4E00-\u9FFF\uA000-\uA4CF\uAC00-\uD7A3\uF900-\uFAFF\uFE30-\uFE4F\uFF00-\uFF60\uFFE0-\uFFE6]|[\u{20000}-\u{3FFFD}]/u;
export const softJoin = (s) => (s.includes('\n')
  ? s.replace(/(.?)\n(?=(.?))/gu, (m, a, b) => a + (WIDE.test(a) && WIDE.test(b) ? '' : ' '))
  : s);

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
  // writes nothing. A soft break (plan P2-10) reads as the engine reads it
  const plain = (x) => {
    if (isNode(x)) return x.text !== undefined ? softJoin(x.text) : x.children.map(plain).join('');
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

  // ---- semantic values (plan P2-07; design T3) ------------------------------
  // an element instance: a group of role `name` whose options other than
  // label and the style keys are its EXT data (a scalar under an
  // [a-z][a-z0-9-]* name: title, …), which the element's row may read
  const isScalar = (v) => typeof v === 'string' || typeof v === 'number' || typeof v === 'boolean';
  const elementGroup = (name, o, kids) => {
    const ext = {};
    for (const [k, v] of Object.entries(o ?? {})) {
      if (k === 'label' || k === 'style' || k === 'role' || isStyleKey(k)) continue;
      if (k === 'ext' && isPlainObject(v)) {
        for (const [n, x] of Object.entries(v)) if (isScalar(x) && validDomain('extname', n)) ext[n] = x;
      } else if (isScalar(v) && validDomain('extname', k)) ext[k] = v;
    }
    return ob.makeNode(KIND.group, { role: name, label: o?.label, ext: Object.keys(ext).length ? ext : undefined }, kids);
  };
  // a supplement in its canonical row form: a string is literal text;
  // {term}, {text} and {lang: text, …} as they are
  const supplementOf = (v, who) => {
    if (typeof v === 'string') return { text: v };
    if (isPlainObject(v) && Object.values(v).length && Object.values(v).every((x) => typeof x === 'string')) return { ...v };
    throw new TypeError(`${who}: a supplement is a string, {term}, {text} or {lang: text, …}`);
  };
  // counterUpdate(name, {set, step, add, numbering, supplement}): a
  // positional counter event — a content value, applied where it is placed
  const eventAttrs = (counter, o) => {
    const who = 'counterUpdate';
    if (typeof counter !== 'string' || !validDomain('ident', counter)) throw new TypeError(`${who}(name, {set, step, add, numbering, supplement}): a counter name`);
    const a = { counter };
    for (const [k, v] of Object.entries(o)) {
      if (v === undefined) continue;
      if (k === 'set') {
        const xs = Array.isArray(v) ? v : [v];
        if (!xs.length || !xs.every((x) => Number.isInteger(x) && Math.abs(x) < 1e9)) throw new TypeError(`${who}: set takes an integer or a list of integers`);
        a.set = xs.join('.');
      } else if (k === 'step') {
        const n = v === true ? 1 : v;
        if (!Number.isInteger(n) || n < 1 || n > 16) throw new TypeError(`${who}: step takes a level 1–16`);
        a.step = n;
      } else if (k === 'add') {
        if (!Number.isInteger(v)) throw new TypeError(`${who}: add takes an integer`);
        a.add = v;
      } else if (k === 'numbering') {
        if (typeof v !== 'string') throw new TypeError(`${who}: numbering takes a pattern`);
        a.numbering = v;
      } else if (k === 'supplement') {
        a.supplement = JSON.stringify(supplementOf(v, who));
      } else diag(1, 'ctor-arg', `${who}: unknown option ${k}`);
    }
    return a;
  };

  // ---- base implementations (the registry's first entries) ---------------
  const kindImpl = (spec) => {
    const kind = KIND[spec.kind];
    return (call) => ob.makeNode(kind, ordered(spec.order, call.attrs), call.kids);
  };
  const styledBy = (attrs) => (call) => ob.makeNode(KIND.styled, attrs, call.kids);
  const styleKeyDiag = (who) => (k) => diag(1, 'ctor-arg', `${who}: unknown style key ${k}`);
  // a node again with more attributes (its span kept): a caption paragraph's
  // role, a node's own style (plan P2-08)
  const withArgs = (node, extra) => {
    const n = ob.makeNode(node.kind, { ...node.args, ...extra }, node.children);
    const sp = ob.spans.get(node.opId);
    if (sp) ob.span(n, sp[0], sp[1]);
    return n;
  };
  // a node's own style change (plan P2-08; the universal `style` attribute,
  // a delta node): on top of the one it has; a text, which takes no
  // attributes, is wrapped in a styled node instead
  const withOwnStyle = (node, patch, who) => {
    const attrs = styleAttrs(patch, styleKeyDiag(who));
    if (!isNode(node) || node.text !== undefined) return ob.makeNode(KIND.styled, attrs, toContent(node));
    const prev = node.args.style;
    const merged = { ...(prev ? prev.args : {}), ...attrs };
    if (prev?.args.decoration && attrs.decoration) merged.decoration = prev.args.decoration | attrs.decoration;
    return withArgs(node, { style: ob.makeNode(KIND.styled, merged, []) });
  };
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
    strong: styledBy({ weight: 700 }),
    em: styledBy({ italic: true }),
    // inline/block style scope (document-model §3): a style patch
    // (styleAttrs) over the kids
    style: (call) => ob.makeNode(KIND.styled, styleAttrs(call.options ?? {}, styleKeyDiag('style')), call.kids),
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
      // its paragraphs are its caption (plan P2-08: role caption)
      for (const b of call.body.blocks()) kids.push(b.kind === KIND.para ? withArgs(b, { role: 'caption' }) : b);
      return ob.makeNode(KIND.group, { role: 'figure', label: a.label }, kids);
    },
    toc: collect('toc'),
    glossary: collect('glossary'),
    notes: collect('notes'),
    // citations (notes-design.md §2): the collector, in place; the data
    // loads after the program ran and its entries become the table's rows
    bibliography: (call) => host.bibliography(call.attrs.src, call.options ?? {}, here.s, here.e),
    // the event carries the construct it was made in (where a discarded
    // one is reported: event-unplaced); placed, it takes its occurrence's
    counterUpdate: (call) => {
      const n = ob.makeNode(KIND.event, eventAttrs(call.attrs.counter, call.options ?? {}), []);
      ob.span(n, here.s, here.e);
      return n;
    },
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
      return withOwnStyle(r, c.style, name);
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
  // Body, else the default region — a group of role `name`. The header's
  // `style: {…}` (plan P2-08) is the result's own style change and never
  // reaches the handler; a style key at the top of a default region's header
  // is a mistake (it used to be sniffed as style) and says so
  const region = async (name, args = {}, items = []) => {
    const entry = registry.get('ctor', name);
    const body = regionBody(items);
    const { style, ...hargs } = args;
    let node;
    if (entry?.spec.body) {
      const attrs = {};
      if (entry.spec.options !== 'raw')
        for (const k of entry.spec.options) if (hargs[k] !== undefined) attrs[k] = hargs[k];
      node = await invoke(name, entry, { attrs, kids: [], lines: undefined, body, options: hargs }, 'region-error');
    } else {
      for (const k of Object.keys(hargs))
        if (isStyleKey(k)) diag(1, 'ctor-arg', `#!${name}: ${k} is a style: write style: {${k}: …}`);
      node = elementGroup(name, hargs, body.blocks());
    }
    if (isPlainObject(style)) node = withOwnStyle(node, style, `#!${name}`);
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

  // ---- semantic declarations: canonical rows ----------------------------------
  // The JS sugar ends here: rows reach the engine in elements.json's form
  // (one DECL each, EXT `row` = its JSON); a template is content, carried as
  // a DECL template and written {"$t": k} in the row.
  const checkSpec = (who, name, spec) => {
    if (typeof name !== 'string' || !validDomain('ident', name)) throw new TypeError(`${who}(name, spec): a name [A-Za-z_][A-Za-z0-9_-]*`);
    if (!isPlainObject(spec)) throw new TypeError(`${who}(name, spec): spec is an object`);
  };
  const declareRow = (type, name, row, templates) => {
    ob.uses(10);  // canonical rows (EXT row) are since 10
    ob.decl(DECLS[type].id, here.s, here.e, name, { row: JSON.stringify(row) }, templates);
  };
  const templater = (templates) => (v) => {
    const kids = kidsOf([v]);
    templates.push(kids.length === 1 ? kids[0] : ob.makeNode(KIND.seq, {}, kids));
    return { $t: templates.length - 1 };
  };
  const unknownField = (who, k) => diag(1, 'ctor-arg', `${who}: unknown field ${k}`);
  // counter: {within: name | {counter, depth, sep}, depth, sep (of within),
  // numbering (a pattern), start, gap: 'zero' | 'one', levels (by-level, and
  // its depth), levelArg, keyed}
  const counterRow = (name, spec, who = '$.counter') => {
    checkSpec(who, name, spec);
    const row = {};
    let within;
    for (const [k, v] of Object.entries(spec)) {
      if (v === undefined) continue;
      switch (k) {
        case 'within':
          if (typeof v === 'string') within = { ...within, counter: v };
          else if (isPlainObject(v)) within = { ...within, ...v };
          else throw new TypeError(`${who}: within names a counter`);
          break;
        case 'depth': within = { ...within, depth: v }; break;
        case 'sep': within = { ...within, sep: String(v) }; break;
        case 'numbering':
          if (typeof v !== 'string') throw new TypeError(`${who}: numbering is a pattern ('1.1', 'A', '(i)', …)`);
          row.pattern = v;
          break;
        case 'start': row.start = Array.isArray(v) ? [...v] : [v]; break;
        case 'gap':
          if (v !== 'zero' && v !== 'one') throw new TypeError(`${who}: gap is 'zero' or 'one'`);
          row.gap = v;
          break;
        case 'levels': row.shape = 'by-level'; row.depth = v; break;
        case 'levelArg': row['level-arg'] = v; break;
        case 'keyed': row.keyed = !!v; break;
        default: unknownField(who, k);
      }
    }
    if (within) {
      if (!within.counter) throw new TypeError(`${who}: depth and sep belong to within`);
      row.within = within;
    }
    return row;
  };
  const selectorOf = (x) => (typeof x === 'string' ? { node: x } : isPlainObject(x) ? { ...x } : (() => {
    throw new TypeError('$.element: a selector is a node kind or {node, role, inside, <attribute>: value}');
  })());
  // title: 'text' (the content's text) or {arg: name} (an attribute, or the
  // instance's EXT data of that name)
  const titleOf = (v) => {
    const t = Array.isArray(v) ? v[0] : v;
    if (t === 'text' || t === null) return t === null ? 'none' : 'text';
    if (isPlainObject(t) && typeof t.arg === 'string') return t.arg in ARGK ? { arg: t.arg } : { ext: t.arg };
    throw new TypeError("$.element: title is 'text' or {arg: name}");
  };
  // element: {select, like, counter (a name, or {name, …} declaring it),
  // numbering ('always' | 'labelled' | false, or a pattern for its
  // counter), supplement, title, labels, outline, sites [{where, at,
  // template}], ref (a template), forms {name: template}, alias, flow,
  // table, rowKey, box, html}
  const elementRow = (name, spec) => {
    const who = '$.element';
    checkSpec(who, name, spec);
    const templates = [];
    const tpl = templater(templates);
    const row = {};
    const counters = [];
    let pattern;
    for (const [k, v] of Object.entries(spec)) {
      if (v === undefined) continue;
      switch (k) {
        case 'select': row.select = (Array.isArray(v) ? v : [v]).map(selectorOf); break;
        case 'like': case 'outline': case 'table': case 'box': case 'html': case 'labels': row[k] = v; break;
        case 'rowKey': row['row-key'] = v; break;
        case 'counter':
          if (typeof v === 'string') row.counter = v;
          else if (isPlainObject(v)) {
            const { name: cn = name, ...c } = v;
            counters.push([cn, counterRow(cn, c, who)]);
            row.counter = cn;
          } else throw new TypeError(`${who}: counter is a name or {name, within, …}`);
          break;
        case 'numbering':
          if (v === false || v === null || v === 'never') row.numbering = 'never';
          else if (v === true || v === 'always') row.numbering = 'always';
          else if (v === 'labelled') row.numbering = 'labelled';
          else if (typeof v === 'string') pattern = v;
          else throw new TypeError(`${who}: numbering is 'always', 'labelled', false or a pattern`);
          break;
        case 'supplement': row.supplement = supplementOf(v, who); break;
        case 'title': row.title = titleOf(v); break;
        case 'sites':
          row.sites = (Array.isArray(v) ? v : [v]).map((x) => {
            if (!isPlainObject(x)) throw new TypeError(`${who}: a site is {where, at, template}`);
            const { where = 'prepend', at, template, arg } = x;
            const [w, a] = String(where).split(':');  // 'prepend:first-para'
            const site = { where: w };
            if (at ?? a) site.at = at ?? a;
            if (arg !== undefined) site.arg = arg;
            site.template = tpl(template ?? []);
            return site;
          });
          break;
        case 'ref': row.ref = tpl(v); break;
        case 'forms':
          if (!isPlainObject(v)) throw new TypeError(`${who}: forms is {name: template}`);
          row.forms = Object.fromEntries(Object.entries(v).map(([f, t]) => [f, tpl(t)]));
          break;
        case 'alias': {
          if (!isPlainObject(v)) throw new TypeError(`${who}: alias is {prefix, body, ref}`);
          const { ref, ...a } = v;
          row.alias = ref === undefined ? a : { ...a, ref: tpl(ref) };
          break;
        }
        case 'flow': {
          if (!isPlainObject(v)) throw new TypeError(`${who}: flow is {name, placement, marker, markerAlias}`);
          const { marker, markerAlias, ...f } = v;
          if (marker !== undefined) f.marker = tpl(marker);
          if (markerAlias !== undefined) {
            const { ref, ...a } = markerAlias;
            f['marker-alias'] = ref === undefined ? a : { ...a, ref: tpl(ref) };
          }
          row.flow = f;
          break;
        }
        default: unknownField(who, k);
      }
    }
    // a numbered element without a counter counts with its own
    if (!row.counter && !spec.like && (pattern !== undefined || row.numbering === 'always' || row.numbering === 'labelled')) {
      counters.push([name, {}]);
      row.counter = name;
    }
    if (pattern !== undefined) {
      if (!row.counter) throw new TypeError(`${who}: a numbering pattern belongs to a counter: $.counter(name, {numbering})`);
      const own = counters.find(([cn]) => cn === row.counter);
      if (own) own[1].pattern = pattern;
      else counters.push([row.counter, { pattern }]);
    }
    if (row.counter && !row.numbering && !spec.like) row.numbering = 'always';
    return { row, templates, counters };
  };
  // collector: {query (or select / table / flow), context, wrap,
  // entry, empty, rows, cite}
  const collectorRow = (name, spec) => {
    const who = '$.collector';
    checkSpec(who, name, spec);
    const templates = [];
    const tpl = templater(templates);
    const row = {};
    for (const [k, v] of Object.entries(spec)) {
      if (v === undefined) continue;
      switch (k) {
        case 'query': row.query = { ...row.query, ...v }; break;
        case 'select': row.query = { ...row.query, classes: v }; break;
        case 'table': case 'flow': row.query = { ...row.query, [k]: v }; break;
        case 'context': case 'rows': row[k] = v; break;
        case 'wrap': case 'entry': case 'empty': case 'cite': row[k] = tpl(v); break;
        default: unknownField(who, k);
      }
    }
    return { row, templates };
  };
  // the constructor $.element returns: an instance of the element (its
  // options → elementGroup; content kids as blocks)
  const elementCtor = (name) => Object.defineProperty((...args) => {
    const o = isPlainObject(args[0]) ? args.shift() : {};
    return elementGroup(name, o, kidsBody(kidsOf(args)).blocks());
  }, 'name', { value: name });

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
            : elementGroup(name, nc.options, body.blocks());
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
    // $.element / $.counter / $.counter.system / $.collector (plan P2-07;
    // design T3 "Semantic declarations"): a row of engine/data/elements.json's
    // form, declared at this point (hoisted: the document's last declaration
    // of a name wins; it patches a built-in row of that name field by field)
    element(name, spec = {}) {
      const { row, templates, counters } = elementRow(name, spec);
      for (const [cn, c] of counters) declareRow('counter', cn, c, []);
      declareRow('element', name, row, templates);
      return elementCtor(name);
    },
    counter(name, spec = {}) {
      declareRow('counter', name, counterRow(name, spec), []);
    },
    counterSystem(name, spec = {}) {
      checkSpec('$.counter.system', name, spec);
      const symbols = spec.symbols;
      if (!Array.isArray(symbols) || !symbols.length || !symbols.every((x) => typeof x === 'string'))
        throw new TypeError('$.counter.system: symbols is a list of strings');
      const row = { symbols: [...symbols] };
      if (spec.mode !== undefined) {
        if (!['numeric', 'alphabetic', 'cyclic', 'fixed'].includes(spec.mode))
          throw new TypeError('$.counter.system: mode is numeric, alphabetic, cyclic or fixed');
        row.mode = spec.mode;
      }
      declareRow('counter-system', name, row, []);
    },
    collector(name, spec = {}) {
      const { row, templates } = collectorRow(name, spec);
      declareRow('collector', name, row, templates);
      const ctor = Object.defineProperty((o = {}) => ob.makeNode(KIND.collect,
        { what: name, cited: isPlainObject(o) ? o.cited : undefined }, []), 'name', { value: name });
      ctor[NULLARY] = true;
      return ctor;
    },
    // $.labels.import(src): cross-document labels — P3-31; until then it
    // says so and imports nothing
    labelsImport(src) {
      diag(0, 'labels-import', `$.labels.import(${JSON.stringify(String(src))}): cross-document labels arrive with P3-31; nothing imported`);
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
