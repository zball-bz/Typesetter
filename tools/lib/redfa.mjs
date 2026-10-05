// Used by tools/gen-schema.mjs to compile the schema's textual value domains (plan P1-02).
// Regular expression (byte-level subset) → minimal DFA with byte classes.
// Syntax: literals, \-escapes (\( \) \\ \. \- \xHH \/ etc.), classes [..] with
// ranges, ^ negation and \xHH, groups ( ), alternation |, quantifiers * + ?
// {m} {m,n}. Matching is whole-string. Bytes ≥ 0x80 stand for "any non-ASCII
// byte" (UTF-8 continuation and lead bytes alike), so a class containing
// \x80-\xff accepts any non-ASCII character in either language.

export function parse(src) {
  let i = 0;
  const peek = () => src[i];
  const eat = (c) => { if (src[i] !== c) throw new Error(`regex: expected ${c} at ${i} in ${src}`); i++; };
  function byteOf(esc) {
    if (esc === 'x') { const h = src.slice(i, i + 2); i += 2; return parseInt(h, 16); }
    if (esc === 'n') return 10;
    if (esc === 't') return 9;
    return esc.charCodeAt(0);
  }
  function atom() {
    const c = src[i++];
    if (c === '(') { const r = alt(); eat(')'); return r; }
    if (c === '[') {
      let neg = false;
      if (src[i] === '^') { neg = true; i++; }
      const set = new Set();
      let first = true;
      while (src[i] !== ']' || first) {
        first = false;
        let lo;
        if (src[i] === '\\') { i++; lo = byteOf(src[i++]); } else lo = src.charCodeAt(i++);
        let hi = lo;
        if (src[i] === '-' && src[i + 1] !== ']') {
          i++;
          if (src[i] === '\\') { i++; hi = byteOf(src[i++]); } else hi = src.charCodeAt(i++);
        }
        for (let b = lo; b <= hi; b++) set.add(b);
      }
      eat(']');
      const bytes = [];
      for (let b = 0; b < 256; b++) if (set.has(b) !== neg) bytes.push(b);
      return { t: 'set', bytes };
    }
    if (c === '\\') return { t: 'set', bytes: [byteOf(src[i++])] };
    if (c === '.') return { t: 'set', bytes: [...Array(256).keys()] };
    return { t: 'set', bytes: [c.charCodeAt(0)] };
  }
  function quant() {
    let a = atom();
    for (;;) {
      const c = peek();
      if (c === '*') { i++; a = { t: 'rep', a, min: 0, max: Infinity }; }
      else if (c === '+') { i++; a = { t: 'rep', a, min: 1, max: Infinity }; }
      else if (c === '?') { i++; a = { t: 'rep', a, min: 0, max: 1 }; }
      else if (c === '{') {
        i++;
        let m = '';
        while (/[0-9]/.test(src[i])) m += src[i++];
        let n = m;
        if (src[i] === ',') { i++; n = ''; while (/[0-9]/.test(src[i])) n += src[i++]; }
        eat('}');
        a = { t: 'rep', a, min: +m, max: n === '' ? Infinity : +n };
      } else return a;
    }
  }
  function seq() {
    const xs = [];
    while (i < src.length && src[i] !== '|' && src[i] !== ')') xs.push(quant());
    return { t: 'seq', xs };
  }
  function alt() {
    const xs = [seq()];
    while (src[i] === '|') { i++; xs.push(seq()); }
    return xs.length === 1 ? xs[0] : { t: 'alt', xs };
  }
  const r = alt();
  if (i !== src.length) throw new Error(`regex: trailing input at ${i} in ${src}`);
  return r;
}

// Thompson NFA
function nfa(ast) {
  const states = [];  // { eps: [], on: Map(byte → [targets]) }
  const mk = () => { states.push({ eps: [], on: [] }); return states.length - 1; };
  function build(n) {
    if (n.t === 'set') {
      const s = mk(), e = mk();
      states[s].on.push({ bytes: n.bytes, to: e });
      return [s, e];
    }
    if (n.t === 'seq') {
      const s = mk(); let cur = s;
      for (const x of n.xs) { const [a, b] = build(x); states[cur].eps.push(a); cur = b; }
      return [s, cur];
    }
    if (n.t === 'alt') {
      const s = mk(), e = mk();
      for (const x of n.xs) { const [a, b] = build(x); states[s].eps.push(a); states[b].eps.push(e); }
      return [s, e];
    }
    if (n.t === 'rep') {
      const s = mk(); let cur = s;
      for (let k = 0; k < n.min; k++) { const [a, b] = build(n.a); states[cur].eps.push(a); cur = b; }
      if (n.max === Infinity) {
        const [a, b] = build(n.a);
        states[cur].eps.push(a); states[b].eps.push(a);
        const e = mk(); states[cur].eps.push(e); states[b].eps.push(e);
        return [s, e];
      }
      const e = mk();
      for (let k = n.min; k < n.max; k++) {
        states[cur].eps.push(e);
        const [a, b] = build(n.a); states[cur].eps.push(a); cur = b;
      }
      states[cur].eps.push(e);
      return [s, e];
    }
    throw new Error('regex: bad node');
  }
  const [start, accept] = build(ast);
  return { states, start, accept };
}

// subset construction + byte classes + minimization (Moore)
export function compile(src) {
  const { states, start, accept } = nfa(parse(src));
  const closure = (set) => {
    const st = [...set], seen = new Set(set);
    while (st.length) { const x = st.pop(); for (const y of states[x].eps) if (!seen.has(y)) { seen.add(y); st.push(y); } }
    return [...seen].sort((a, b) => a - b);
  };
  const key = (s) => s.join(',');
  const d0 = closure([start]);
  const dstates = [d0], index = new Map([[key(d0), 0]]), trans = [];
  for (let q = 0; q < dstates.length; q++) {
    const row = new Array(256).fill(-1);
    for (let b = 0; b < 256; b++) {
      const next = new Set();
      for (const x of dstates[q]) for (const e of states[x].on) if (e.bytes.includes(b)) next.add(e.to);
      if (!next.size) continue;
      const c = closure([...next]); const k = key(c);
      if (!index.has(k)) { index.set(k, dstates.length); dstates.push(c); }
      row[b] = index.get(k);
    }
    trans.push(row);
  }
  let acc = dstates.map((s) => s.includes(accept));
  // minimize: partition refinement (dead state = -1 kept implicit)
  let part = acc.map((a) => (a ? 1 : 0));
  for (;;) {
    const sig = trans.map((row, q) => part[q] + ':' + row.map((t) => (t < 0 ? -1 : part[t])).join(','));
    const ids = new Map(); const np = sig.map((s) => { if (!ids.has(s)) ids.set(s, ids.size); return ids.get(s); });
    if (ids.size === new Set(part).size) { part = np; break; }
    part = np;
  }
  const n = Math.max(...part) + 1;
  const mtrans = Array.from({ length: n }, () => new Array(256).fill(-1));
  const macc = new Array(n).fill(false);
  part.forEach((p, q) => { mtrans[p] = trans[q].map((t) => (t < 0 ? -1 : part[t])); macc[p] = acc[q]; });
  // renumber so the start is 0
  const s0 = part[0];
  const order = [s0, ...[...Array(n).keys()].filter((x) => x !== s0)];
  const ren = new Map(order.map((x, k) => [x, k]));
  const T = order.map((p) => mtrans[p].map((t) => (t < 0 ? -1 : ren.get(t))));
  const A = order.map((p) => macc[p]);
  // byte equivalence classes
  const colSig = [...Array(256).keys()].map((b) => T.map((row) => row[b]).join(','));
  const cls = new Map(); const byteClass = colSig.map((s) => { if (!cls.has(s)) cls.set(s, cls.size); return cls.get(s); });
  const C = cls.size;
  const table = T.map((row) => { const r = new Array(C).fill(-1); row.forEach((t, b) => { r[byteClass[b]] = t; }); return r; });
  return { states: T.length, classes: C, byteClass, table, accept: A };
}

export function matches(dfa, bytes) {
  let q = 0;
  for (const b of bytes) { q = dfa.table[q][dfa.byteClass[b]]; if (q < 0) return false; }
  return dfa.accept[q];
}
