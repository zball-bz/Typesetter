// Op buffer writer + shadow nodes (document-model §4). The C++ OpReader is
// the other half of this contract; cross-tested via recorded fixtures.
import { OP, OPS_MIN_COMPAT, ARGK, SINCE } from './ops.gen.mjs';

const ARG_NULL = 0, ARG_BOOL = 1, ARG_NUM = 2, ARG_STR = 3, ARG_NODE = 4;

// Node values (plan P2-01; design T2 S4): frozen, branded with a
// module-private symbol — node-ness is the brand, never a duck-typed field,
// and a value cannot be changed after its op was written (spans live in the
// buffer's side table)
const BRAND = Symbol('tsm.node');
export const isNode = (x) => x !== null && typeof x === 'object' && x[BRAND] === true;
const nodeValue = (fields) => Object.freeze(Object.assign(Object.create(null), fields, { [BRAND]: true }));

export class OpBuf {
  constructor() {
    this.ops = [];            // raw bytes of the op stream
    this.strMap = new Map();  // string → ref
    this.strList = [];
    this.nextId = 0;
    this.opCount = 0;
    this.spans = new Map();   // opId → [start, end] (node values are frozen)
    // per-buffer version (plan P1-01): the newest vocabulary row used, at
    // least MIN_COMPAT — a buffer readable by every engine that knows it
    this.version = OPS_MIN_COMPAT;
  }
  uses(since) {
    if (since > this.version) this.version = since;
  }

  vint(v) {
    if (v < 0 || !Number.isFinite(v)) throw new Error(`bad varint ${v}`);
    let x = Math.floor(v);
    while (x > 127) {
      this.ops.push((x & 127) | 128);
      x = Math.floor(x / 128);
    }
    this.ops.push(x);
  }
  f64(v) {
    const b = new Uint8Array(8);
    new DataView(b.buffer).setFloat64(0, v, true);
    for (const x of b) this.ops.push(x);
  }
  strRef(s) {
    let r = this.strMap.get(s);
    if (r === undefined) {
      r = this.strList.length;
      this.strMap.set(s, r);
      this.strList.push(s);
    }
    return r;
  }

  makeText(s) {
    const str = String(s);
    this.opCount++;
    this.uses(SINCE.op[OP.MAKE_TEXT]);
    this.ops.push(OP.MAKE_TEXT);
    this.vint(this.strRef(str));
    const id = this.nextId++;
    return nodeValue({ kind: 17 /* text */, args: Object.freeze({}), children: Object.freeze([]), opId: id, text: str });
  }

  makeNode(kind, args = {}, children = []) {
    this.opCount++;
    this.uses(SINCE.op[OP.MAKE_NODE]);
    this.uses(SINCE.kind[kind] ?? OPS_MIN_COMPAT);
    this.ops.push(OP.MAKE_NODE);
    this.vint(kind);
    const keys = Object.keys(args).filter((k) => args[k] !== undefined);
    this.vint(keys.length);
    for (const k of keys) {
      const argk = ARGK[k];
      if (argk === undefined) throw new Error(`unknown arg key: ${k}`);
      this.uses(SINCE.attr[kind]?.[argk] ?? OPS_MIN_COMPAT);
      this.vint(argk);
      const v = args[k];
      if (v === null) this.ops.push(ARG_NULL);
      else if (typeof v === 'boolean') { this.ops.push(ARG_BOOL, v ? 1 : 0); }
      else if (typeof v === 'number') { this.ops.push(ARG_NUM); this.f64(v); }
      else if (typeof v === 'string') { this.ops.push(ARG_STR); this.vint(this.strRef(v)); }
      else if (isNode(v)) { this.ops.push(ARG_NODE); this.vint(v.opId); }
      else throw new Error(`bad arg value for ${k}`);
    }
    this.vint(children.length);
    for (const c of children) {
      if (!isNode(c)) throw new Error('a child is not a node value');
      this.vint(c.opId);
    }
    const id = this.nextId++;
    return nodeValue({ kind, args: Object.freeze({ ...args }), children: Object.freeze([...children]), opId: id });
  }

  emitNode(shadow) {
    this.opCount++;
    this.uses(SINCE.op[OP.EMIT]);
    this.ops.push(OP.EMIT);
    this.vint(shadow.opId);
  }
  stylePush(bits, patch = {}) {
    this.opCount++;
    this.uses(SINCE.op[OP.STYLE_PUSH]);
    this.ops.push(OP.STYLE_PUSH);
    this.vint(bits);
    const entries = Object.entries(patch).filter(
      ([k, v]) => v !== undefined && v !== null && ARGK[k] !== undefined);
    this.ops.push(entries.length);
    for (const [k, v] of entries) {
      this.vint(ARGK[k]);
      if (typeof v === 'number') { this.ops.push(ARG_NUM); this.f64(v); }
      else { this.ops.push(ARG_STR); this.vint(this.strRef(String(v))); }
    }
  }
  stylePopTo(h) {
    this.opCount++;
    this.uses(SINCE.op[OP.STYLE_POP_TO]);
    this.ops.push(OP.STYLE_POP_TO);
    this.vint(h);
  }
  span(shadow, s, e) {
    this.opCount++;
    this.uses(SINCE.op[OP.SPAN]);
    this.ops.push(OP.SPAN);
    this.vint(shadow.opId);
    this.vint(s);
    this.vint(e);
    this.spans.set(shadow.opId, [s, e]);
  }
  // an occurrence alias (plan P2-04, AT since 8): a value spliced again is a
  // new id standing for it, with this occurrence's span (the value itself
  // never changes; the engine instantiates the target at the alias span)
  at(target, s, e) {
    this.opCount++;
    this.uses(SINCE.op[OP.AT]);
    this.ops.push(OP.AT);
    this.vint(target.opId);
    this.vint(s);
    this.vint(e);
    const id = this.nextId++;
    this.spans.set(id, [s, e]);
    const alias = { kind: target.kind, args: target.args, children: target.children, opId: id };
    if (target.text !== undefined) alias.text = target.text;
    return nodeValue(alias);
  }
  // a text's cooked→raw map (plan P2-04, RAWMAP since 8): pairs (cooked
  // offset, raw offset relative to its span start), identity between them
  rawmap(text, pairs) {
    this.opCount++;
    this.uses(SINCE.op[OP.RAWMAP]);
    this.ops.push(OP.RAWMAP);
    this.vint(text.opId);
    this.vint(pairs.length / 2);
    for (const x of pairs) this.vint(x);
  }
  // an execution diagnostic (plan P2-01, DIAG since 7): severity 0 info,
  // 1 warning, 2 error; a stable code; the source span it is about
  diag(sev, code, message, s = 0, e = s) {
    this.opCount++;
    this.uses(SINCE.op[OP.DIAG]);
    this.ops.push(OP.DIAG, sev);
    this.vint(this.strRef(String(code)));
    this.vint(this.strRef(String(message)));
    this.vint(Math.max(0, Math.floor(s)));
    this.vint(Math.max(Math.floor(s), Math.floor(e)));
  }

  finalize() {
    const enc = new TextEncoder();
    const strBytes = this.strList.map((s) => enc.encode(s));
    const blobLen = strBytes.reduce((a, b) => a + b.length, 0);

    const head = [];
    const pushV = (v) => {
      let x = Math.floor(v);
      while (x > 127) { head.push((x & 127) | 128); x = Math.floor(x / 128); }
      head.push(x);
    };
    // nOps for the reader = number of op *records*; we track via a separate count
    pushV(this.strList.length);
    pushV(blobLen);
    pushV(this.opCount);

    const offs = [];
    let acc = 0;
    for (const b of strBytes) {
      acc += b.length;
      let x = acc;
      while (x > 127) { offs.push((x & 127) | 128); x = Math.floor(x / 128); }
      offs.push(x);
    }

    const total = 5 + head.length + blobLen + offs.length + this.ops.length;
    const out = new Uint8Array(total);
    let p = 0;
    out.set([0x54, 0x53, 0x4f, 0x50, this.version], p); p += 5;  // "TSOP" + version
    out.set(head, p); p += head.length;
    for (const b of strBytes) { out.set(b, p); p += b.length; }
    out.set(offs, p); p += offs.length;
    out.set(this.ops, p); p += this.ops.length;
    return out;
  }
}
