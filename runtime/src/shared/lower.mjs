// The LowerProgram interpreter (plan P2-02; design T2 S5; docs/lowering-
// design.md). The engine compiles a document's markup into one binary
// program of constructor calls (engine/src/codegen/lower.h has the layout,
// lower.def the opcodes) and the user's JavaScript into a hole module; this
// file decodes the program and runs it against the executor's constructors.
// Static markup never becomes JavaScript text: it cannot fail to parse, and
// it is never parsed by V8.
//
// The hole module calls __rt.run(__h, k) for each segment k of the program
// (the stretches between verbatim statements); a document without user code
// has no module and runs as segment 0 alone. Every block that runs user code
// executes inside a frame: an exception becomes an error block (top level)
// or an error node in its parent (any deeper block), and the style stack
// returns to the frame's entry height. A statement (STMT, LET) and an IF
// that takes no branch are no content: their value is undefined, which no
// parent keeps (plan P2-12).
import {
  LOP, LCONST, LBLOCK, BFLAG, CFLAG, LOP_ASYNC, PROGRAM_ABI, LOWER_VERSION,
} from './lower.gen.mjs';

// a hole the SyntaxError isolation stubbed (D-I11): it throws this, and the
// frame around it reports script-syntax
export const STUB = Object.freeze({ stub: 'syntax' });

const utf8 = new TextDecoder();

// bytes (Uint8Array) → the program's tables and body
export function decodeProgram(bytes) {
  const b = bytes;
  let p = 0;
  const bad = (why) => { throw new Error(`LowerProgram: ${why}`); };
  const u = () => {
    let v = 0, mul = 1;
    for (let i = 0; i < 5; i++) {
      if (p >= b.length) bad('truncated');
      const c = b[p++];
      v += (c & 127) * mul;
      if (!(c & 128)) return v;
      mul *= 128;
    }
    return bad('varint too long');
  };
  if (b.length < 17 || b[0] !== 0x54 || b[1] !== 0x53 || b[2] !== 0x4c || b[3] !== 0x50) bad('not a LowerProgram');
  if (b[4] !== LOWER_VERSION) bad(`version ${b[4]}, runtime reads ${LOWER_VERSION}`);
  const abi = (b[5] | b[6] << 8 | b[7] << 16 | b[8] << 24) >>> 0;
  if (abi !== PROGRAM_ABI) bad(`ABI ${abi.toString(16)}, runtime ${PROGRAM_ABI.toString(16)}`);
  let hash = '';
  for (let i = 16; i >= 9; i--) hash += b[i].toString(16).padStart(2, '0');
  p = 17;
  const module = u() === 1;
  const docEnd = u();
  const nStr = u();
  const lens = new Array(nStr);
  let blob = 0;
  for (let i = 0; i < nStr; i++) {
    const bl = u();
    lens[i] = u();
    blob += bl;
  }
  if (p + blob > b.length) bad('string blob out of range');
  // one decode for every string; each is a slice by its UTF-16 length
  const all = utf8.decode(b.subarray(p, p + blob));
  p += blob;
  const strs = new Array(nStr);
  for (let i = 0, at = 0; i < nStr; i++) {
    strs[i] = all.slice(at, at + lens[i]);
    at += lens[i];
  }
  const ctors = new Array(u());
  for (let i = 0; i < ctors.length; i++) ctors[i] = strs[u()];
  const blocks = new Array(u());
  for (let i = 0; i < blocks.length; i++) {
    const kind = b[p++], flags = b[p++];
    blocks[i] = { kind, flags, s: u(), e: u(), holeLo: u(), holeHi: u(), pc: u() };
  }
  const holes = u();
  const pieces = new Array(u());
  for (let i = 0; i < pieces.length; i++) {
    const kind = b[p++];
    pieces[i] = { kind, ref: u(), js0: u(), js1: u() };
  }
  const bodyLen = u();
  if (p + bodyLen !== b.length) bad('body length differs');
  return { abi, hash, module, docEnd, strs, ctors, blocks, holes, pieces, body: b.subarray(p) };
}

// env (the executor's half): ob (OpBuf), call(ctor, attrs, kids) (a bound
// constructor call: shared/stdlib.mjs), region(name, args, items),
// loop(results) (a loop's iterations as one value, plan P2-12),
// fence(tag, args, body, offset, lines), val(x), emit(node), at(node, s, e,
// fresh) (a result at its occurrence: SPAN if made since id `fresh`, else
// an AT alias),
// height() (the style stack), styleInValue(h) (a nested statement left
// styles pushed: popped to h, diagnosed — D-L12 until P3-01),
// setCurrent(block), fail(err, height, s, e) → an error node,
// failBlock(err, height, block) (emits the error block), and here {s, e}:
// the interpreter keeps it on the innermost splice, region, frame or block
// it runs, which is where a diagnostic of the run points.
export class Lowering {
  constructor(prog, env) {
    this.prog = prog;
    this.env = env;
    this.b = prog.body;
    this.dv = new DataView(prog.body.buffer, prog.body.byteOffset, prog.body.byteLength);
    this.S = prog.strs;
    this.C = prog.ctors.map((name) => {
      if (typeof env.std[name] !== 'function') throw new Error(`LowerProgram: no constructor ${name}`);
      return name;
    });
    this.p = 0;
    this.h = [];
    this.seg = 0;
    this.running = false;
    // segment k: the blocks after verbatim block k-1, up to verbatim block k
    this.segStart = [0];
    prog.blocks.forEach((blk, i) => { if (blk.kind === LBLOCK.Verbatim) this.segStart.push(i + 1); });
  }

  u() {
    const b = this.b;
    let c = b[this.p++];
    if (c < 128) return c;
    let v = c & 127, mul = 128;
    for (;;) {
      c = b[this.p++];
      v += (c & 127) * mul;
      if (c < 128) return v;
      mul *= 128;
    }
  }
  k() {
    switch (this.b[this.p++]) {
      case LCONST.Null: return null;
      case LCONST.False: return false;
      case LCONST.True: return true;
      case LCONST.Uint: return this.u();
      case LCONST.F64: {
        const v = this.dv.getFloat64(this.p, true);
        this.p += 8;
        return v;
      }
      case LCONST.Str: return this.S[this.u()];
      case LCONST.Array: {
        const n = this.u(), a = new Array(n);
        for (let i = 0; i < n; i++) a[i] = this.k();
        return a;
      }
    }
    throw new Error('LowerProgram: bad constant');
  }
  // a CALL's attributes, bound by name (the constructor's params and
  // options: shared/stdlib.mjs)
  attrs() {
    const n = this.u();
    if (n === 0) return {};
    const a = {};
    for (let i = 0; i < n; i++) {
      const key = this.S[this.u()];
      a[key] = this.k();
    }
    return a;
  }
  hole(i) {
    const f = this.h[i];
    if (typeof f !== 'function') throw STUB;
    return f;
  }

  // ---- one segment of top-level blocks (the module's __rt.run) ------------
  async run(h, seg) {
    if (this.running) throw new Error('__rt.run: re-entered');
    if (seg !== this.seg || seg >= this.segStart.length) throw new Error('__rt.run: segment out of order');
    this.running = true;
    this.h = h ?? [];
    const { env } = this;
    const blocks = this.prog.blocks;
    const end = seg + 1 < this.segStart.length ? this.segStart[seg + 1] - 1 : blocks.length;
    try {
      for (let i = this.segStart[seg]; i < end; i++) {
        const blk = blocks[i];
        const async = blk.flags & BFLAG.Async;
        if (blk.flags & BFLAG.User) env.setCurrent(i);
        env.here.s = blk.s;
        env.here.e = blk.e;
        this.p = blk.pc;
        if (!(blk.flags & BFLAG.Framed)) {
          env.emit(async ? await this.va() : this.v());
          continue;
        }
        const h0 = env.height();
        try {
          if (blk.kind === LBLOCK.Stmt) {
            this.p++;
            const r = this.hole(this.u())();
            if (async) await r;
          } else {
            env.emit(async ? await this.va() : this.v());
          }
        } catch (err) {
          env.failBlock(err, h0, i);
        }
      }
      if (end < blocks.length) env.setCurrent(end);  // the verbatim statement runs next
    } finally {
      this.running = false;
      this.seg++;
    }
  }

  // ---- values -------------------------------------------------------------
  // v: a value that does not await (no async bit); va: any value
  v() {
    const op = this.b[this.p++];
    const { env } = this;
    switch (op) {
      case LOP.TEXT: {
        const str = this.S[this.u()];
        const n = env.ob.makeText(str);
        const s = this.u(), e = this.u();
        env.ob.span(n, s, e);
        const nm = this.u();
        let m = null;
        if (nm) {  // its cooked→raw map (plan P2-04)
          m = new Array(2 * nm);
          for (let k = 0; k < m.length; k++) m[k] = this.u();
          env.ob.rawmap(n, m);
        }
        // its cell cuts (plan P2-11): with its soft breaks, the provenance
        // body.rows() reads (JS-only, never on the wire)
        const ns = this.u();
        const seps = new Array(ns);
        for (let k = 0; k < ns; k++) seps[k] = this.u();
        if (ns || /\n|^\s|\s$/.test(str)) env.ob.prov.set(n.opId, { s, e, map: m, seps });
        return n;
      }
      case LOP.CALL: {
        const fl = this.b[this.p++], f = this.C[this.u()];
        let s = 0, e = 0;
        if (fl & CFLAG.Spanned) { s = this.u(); e = this.u(); }
        const attrs = this.attrs();
        const fresh = env.ob.nextId;
        const kids = [];
        for (let n = this.u(); n > 0; n--) {
          const x = this.v();
          if (x !== undefined) kids.push(x);
        }
        const node = env.call(f, attrs, kids);
        return fl & CFLAG.Spanned ? env.at(node, s, e, fresh) : node;
      }
      case LOP.HOLE: {
        const f = this.hole(this.u());
        const s = this.u(), e = this.u();  // (S5: a hole's result is unspanned)
        const nk = this.u();
        const { here } = env, ps = here.s, pe = here.e;
        here.s = s;
        here.e = e;
        const fresh = env.ob.nextId;
        try {
          return env.at(env.val(nk ? this.callHole(f, nk, false) : f()), s, e, fresh);
        } finally {
          here.s = ps;
          here.e = pe;
        }
      }
      case LOP.FRAME: {
        const s = this.u(), e = this.u();
        this.u(); this.u();
        const h0 = env.height(), p0 = this.p;
        const { here } = env, ps = here.s, pe = here.e;
        here.s = s;
        here.e = e;
        try {
          return this.v();
        } catch (err) {
          this.p = p0;
          this.skipValue();
          return env.fail(err, h0, s, e);
        } finally {
          here.s = ps;
          here.e = pe;
        }
      }
      case LOP.STMT: {
        const h0 = env.height();
        this.hole(this.u())();
        if (env.height() > h0) env.styleInValue(h0);
        return undefined;
      }
      case LOP.LET: {
        const f = this.hole(this.u());
        f(this.v());
        return undefined;
      }
      case LOP.IF: {
        let r, taken = false;
        for (let n = this.u(); n > 0; n--) {
          const c = this.u();
          if (!taken && (c === 0 || this.hole(c - 1)())) {
            taken = true;
            r = this.v();
          } else {
            this.skipValue();
          }
        }
        return r;
      }
      case LOP.SCOPE: {
        const f = this.hole(this.u());
        this.u();
        const pc = this.p;
        const r = f((table) => this.scoped(table, pc, () => this.v()));
        this.p = pc;
        this.skipValue();
        return r;
      }
      case LOP.LOOP: {
        const f = this.hole(this.u());
        this.u();
        const s = this.u(), e = this.u();
        const pc = this.p, fresh = env.ob.nextId;
        const r = f((table) => this.scoped(table, pc, () => this.v()));
        this.p = pc;
        this.skipValue();
        return env.at(env.loop(r), s, e, fresh);
      }
    }
    throw new Error(`LowerProgram: op ${op} at ${this.p - 1}`);
  }

  // a scope's body (SCOPE, LOOP: plan P2-12), against its own hole table —
  // once per call, from its start
  scoped(table, pc, value) {
    const h0 = this.h;
    this.h = table;
    this.p = pc;
    try {
      return value();
    } finally {
      this.h = h0;
    }
  }
  async scopedAsync(table, pc) {
    const h0 = this.h;
    this.h = table;
    this.p = pc;
    try {
      return await this.va();
    } finally {
      this.h = h0;
    }
  }

  async va() {
    const op = this.b[this.p];
    if (!(op & LOP_ASYNC)) return this.v();
    this.p++;
    const { env } = this;
    switch (op & 0x7f) {
      case LOP.CALL: {
        const fl = this.b[this.p++], f = this.C[this.u()];
        let s = 0, e = 0;
        if (fl & CFLAG.Spanned) { s = this.u(); e = this.u(); }
        const attrs = this.attrs();
        const fresh = env.ob.nextId;
        const kids = [];
        for (let n = this.u(); n > 0; n--) {
          const x = await this.va();
          if (x !== undefined) kids.push(x);
        }
        const node = env.call(f, attrs, kids);
        return fl & CFLAG.Spanned ? env.at(node, s, e, fresh) : node;
      }
      case LOP.HOLE: {
        const f = this.hole(this.u());
        const s = this.u(), e = this.u();
        const nk = this.u();
        const { here } = env, ps = here.s, pe = here.e;
        here.s = s;
        here.e = e;
        const fresh = env.ob.nextId;
        try {
          return env.at(env.val(await (nk ? this.callHole(f, nk, true) : f())), s, e, fresh);
        } finally {
          here.s = ps;
          here.e = pe;
        }
      }
      case LOP.FRAME: {
        const s = this.u(), e = this.u();
        this.u(); this.u();
        const h0 = env.height(), p0 = this.p;
        const { here } = env, ps = here.s, pe = here.e;
        here.s = s;
        here.e = e;
        try {
          return await this.va();
        } catch (err) {
          this.p = p0;
          this.skipValue();
          return env.fail(err, h0, s, e);
        } finally {
          here.s = ps;
          here.e = pe;
        }
      }
      case LOP.FENCE: {
        const lang = this.S[this.u()], a = this.u(), lb = this.u(), inf = this.u();
        const body = this.S[this.u()], off = this.u(), end = this.u();
        const lines = this.k(), s = this.u(), e = this.u();
        const fresh = env.ob.nextId;
        let args = a ? this.hole((a >> 1) - 1)() : {};
        if (a & 1) args = await args;
        if (lb) args = { label: this.S[lb - 1], ...args };  // ` <id>`: an explicit label: wins
        const info = inf ? this.S[inf - 1] : '';
        return env.at(env.val(await env.fence(lang, args, body, off, lines, end, info)), s, e, fresh);
      }
      case LOP.REGION: {
        const name = this.S[this.u()], a = this.u(), lb = this.u(), s = this.u(), e = this.u();
        const fresh = env.ob.nextId;
        let args = a ? this.hole((a >> 1) - 1)() : {};
        if (a & 1) args = await args;
        if (lb) args = { label: this.S[lb - 1], ...args };  // ` <id>`: an explicit label: wins
        const items = [];
        for (let n = this.u(); n > 0; n--) {
          const x = await this.va();
          if (x !== undefined) items.push(x);
        }
        return env.at(await this.region(name, args, items, s, e), s, e, fresh);
      }
      case LOP.STMT: {
        const h0 = env.height();
        await this.hole(this.u())();
        if (env.height() > h0) env.styleInValue(h0);
        return undefined;
      }
      case LOP.LET: {
        const f = this.hole(this.u());
        f(await this.va());
        return undefined;
      }
      case LOP.IF: {
        let r, taken = false;
        for (let n = this.u(); n > 0; n--) {
          const c = this.u();
          if (!taken && (c === 0 || await this.hole(c - 1)())) {
            taken = true;
            r = await this.va();
          } else {
            this.skipValue();
          }
        }
        return r;
      }
      case LOP.SCOPE: {
        const f = this.hole(this.u());
        this.u();
        const pc = this.p;
        const r = await f((table) => this.scopedAsync(table, pc));
        this.p = pc;
        this.skipValue();
        return r;
      }
      case LOP.LOOP: {
        const f = this.hole(this.u());
        this.u();
        const s = this.u(), e = this.u();
        const pc = this.p, fresh = env.ob.nextId;
        const r = await f((table) => this.scopedAsync(table, pc));
        this.p = pc;
        this.skipValue();
        return env.at(env.loop(r), s, e, fresh);
      }
    }
    throw new Error(`LowerProgram: op ${op} at ${this.p - 1}`);
  }

  // the region constructor (or a handler, which may be async) runs at the
  // region
  async region(name, args, items, s, e) {
    const { here } = this.env, ps = here.s, pe = here.e;
    here.s = s;
    here.e = e;
    try {
      return await this.env.region(name, args, items);
    } finally {
      here.s = ps;
      here.e = pe;
    }
  }

  // A hole with content arguments: f(__k), where __k() evaluates them — once,
  // at the point the call's argument list reaches them (so they are built
  // after the call's own arguments, as written). Not called (an optional
  // call that short-circuits): they are skipped.
  callHole(f, nk, async) {
    const kp = this.p;
    let got, live = true;
    const k = () => {
      if (!live) throw new Error('content arguments are evaluated during their call only');
      if (got === undefined) {
        this.p = kp;
        got = this.kids(nk);
      }
      return got;
    };
    const done = () => {
      live = false;
      if (got === undefined) {
        this.p = kp;
        for (let i = 0; i < nk; i++) this.skipValue();
      }
    };
    if (!async) {
      try { return f(k); } finally { done(); }
    }
    return (async () => {
      try { return await f(k); } finally { done(); }
    })();
  }
  // the content arguments: an array, or its promise when one awaits
  kids(nk) {
    const out = [];
    for (let i = 0; i < nk; i++) {
      if (this.b[this.p] & LOP_ASYNC) return this.kidsAsync(out, nk - i);
      const x = this.v();
      if (x !== undefined) out.push(x);
    }
    return out;
  }
  async kidsAsync(out, n) {
    for (let i = 0; i < n; i++) {
      const x = await this.va();
      if (x !== undefined) out.push(x);
    }
    return out;
  }

  // past one value without running it (a failed frame, unused content args)
  skipValue() {
    const b = this.b;
    switch (b[this.p++] & 0x7f) {
      case LOP.TEXT: {
        this.u(); this.u(); this.u();
        for (let n = 2 * this.u(); n > 0; n--) this.u();
        for (let n = this.u(); n > 0; n--) this.u();
        return;
      }
      case LOP.CALL: {
        const fl = b[this.p++];
        this.u();
        if (fl & CFLAG.Spanned) { this.u(); this.u(); }
        for (let n = this.u(); n > 0; n--) { this.u(); this.k(); }
        for (let n = this.u(); n > 0; n--) this.skipValue();
        return;
      }
      case LOP.HOLE: {
        this.u(); this.u(); this.u();
        for (let n = this.u(); n > 0; n--) this.skipValue();
        return;
      }
      case LOP.FRAME: this.u(); this.u(); this.u(); this.u(); this.skipValue(); return;
      case LOP.FENCE:
        this.u(); this.u(); this.u(); this.u(); this.u(); this.u(); this.u(); this.k(); this.u(); this.u();
        return;
      case LOP.REGION:
        this.u(); this.u(); this.u(); this.u(); this.u();
        for (let n = this.u(); n > 0; n--) this.skipValue();
        return;
      case LOP.STMT: this.u(); return;
      case LOP.LET: this.u(); this.skipValue(); return;
      case LOP.IF:
        for (let n = this.u(); n > 0; n--) { this.u(); this.skipValue(); }
        return;
      case LOP.SCOPE: this.u(); this.u(); this.skipValue(); return;
      case LOP.LOOP: this.u(); this.u(); this.u(); this.u(); this.skipValue(); return;
    }
    throw new Error('LowerProgram: bad op');
  }
}
