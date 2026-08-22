// Op buffer writer + shadow nodes (document-model §4). The C++ OpReader is
// the other half of this contract; cross-tested via recorded fixtures.
import { OP, OPS_VERSION, ARGK } from './ops.gen.mjs';

const ARG_NULL = 0, ARG_BOOL = 1, ARG_NUM = 2, ARG_STR = 3, ARG_NODE = 4;

export class OpBuf {
  constructor() {
    this.ops = [];            // raw bytes of the op stream
    this.strMap = new Map();  // string → ref
    this.strList = [];
    this.nextId = 0;
    this.opCount = 0;
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
    this.ops.push(OP.MAKE_TEXT);
    this.vint(this.strRef(str));
    const id = this.nextId++;
    return { kind: 17 /* text */, args: {}, children: [], opId: id, text: str, span: null };
  }

  makeNode(kind, args = {}, children = []) {
    this.opCount++;
    this.ops.push(OP.MAKE_NODE);
    this.vint(kind);
    const keys = Object.keys(args).filter((k) => args[k] !== undefined);
    this.vint(keys.length);
    for (const k of keys) {
      const argk = ARGK[k];
      if (argk === undefined) throw new Error(`unknown arg key: ${k}`);
      this.vint(argk);
      const v = args[k];
      if (v === null) this.ops.push(ARG_NULL);
      else if (typeof v === 'boolean') { this.ops.push(ARG_BOOL, v ? 1 : 0); }
      else if (typeof v === 'number') { this.ops.push(ARG_NUM); this.f64(v); }
      else if (typeof v === 'string') { this.ops.push(ARG_STR); this.vint(this.strRef(v)); }
      else if (v && typeof v === 'object' && 'opId' in v) { this.ops.push(ARG_NODE); this.vint(v.opId); }
      else throw new Error(`bad arg value for ${k}`);
    }
    this.vint(children.length);
    for (const c of children) this.vint(c.opId);
    const id = this.nextId++;
    return { kind, args, children, opId: id, span: null };
  }

  emitNode(shadow) {
    this.opCount++;
    this.ops.push(OP.EMIT);
    this.vint(shadow.opId);
  }
  stylePush(bits) {
    this.opCount++;
    this.ops.push(OP.STYLE_PUSH);
    this.vint(bits);
    this.ops.push(0);  // no inline patch (M1)
  }
  stylePopTo(h) {
    this.opCount++;
    this.ops.push(OP.STYLE_POP_TO);
    this.vint(h);
  }
  span(shadow, s, e) {
    this.opCount++;
    this.ops.push(OP.SPAN);
    this.vint(shadow.opId);
    this.vint(s);
    this.vint(e);
    shadow.span = [s, e];
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
    out.set([0x54, 0x53, 0x4f, 0x50, OPS_VERSION], p); p += 5;  // "TSOP" + version
    out.set(head, p); p += head.length;
    for (const b of strBytes) { out.set(b, p); p += b.length; }
    out.set(offs, p); p += offs.length;
    out.set(this.ops, p); p += this.ops.length;
    return out;
  }
}
