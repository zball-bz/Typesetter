// The resource wire format, JS side (plan P1-19; docs/host-protocol-design.md
// §5): decode a request batch (TSRQ), encode its answer (TSRA). The columns
// come from resources.def (resources.gen.mjs); the engine twin is
// engine/src/resource/codec.cc.
import { RES_VERSION, RES_BY_ID, RES_KINDS } from './resources.gen.mjs';

const dec = new TextDecoder();
const enc = new TextEncoder();
const SIZE = { U8: 1, U16: 2, U32: 4, Str: 4, MetricKey: 4, F64: 8, U32List: 4 };

// bytes → { batch, strings, mks: [{stack, faceDigest, sizePx, weight, italic,
// features, lang, dppx}], kinds: { name: [{ resId, ...keyColumns }] } }
// (Str columns decoded to strings, MetricKey columns to their index)
export function decodeRequest(bytes) {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let at = 0;
  const u8 = () => v.getUint8(at++);
  const u16 = () => { const x = v.getUint16(at, true); at += 2; return x; };
  const u32 = () => { const x = v.getUint32(at, true); at += 4; return x; };
  const f64 = () => { const x = v.getFloat64(at, true); at += 8; return x; };
  const magic = dec.decode(bytes.subarray(0, 4));
  at = 4;
  if (magic !== 'TSRQ') throw new Error('tsr: not a resource request');
  const ver = u32();
  if (ver !== RES_VERSION) throw new Error(`tsr: resource version ${ver}, expected ${RES_VERSION}`);
  const batch = u32();
  const strings = [];
  for (let n = u32(), i = 0; i < n; i++) {
    const len = u32();
    strings.push(dec.decode(bytes.subarray(at, at + len)));
    at += len;
  }
  const mks = [];
  for (let n = u32(), i = 0; i < n; i++) {
    const stack = strings[u32()];
    const lo = u32(), hi = u32();
    mks.push({ stack, faceDigest: hi * 2 ** 32 + lo, sizePx: f64(), weight: u16(), italic: u8() !== 0,
               features: strings[u32()], lang: strings[u32()], dppx: f64() });
  }
  const kinds = {};
  for (let nk = u32(), ki = 0; ki < nk; ki++) {
    const info = RES_BY_ID[u16()];
    const n = u32();
    const rows = [];
    for (let i = 0; i < n; i++) rows.push({ resId: u32() });
    for (const c of info.key) {
      for (const r of rows) {
        const t = c.type;
        r[c.name] = t === 'U8' ? u8() : t === 'U16' ? u16() : t === 'F64' ? f64() : t === 'Str' ? strings[u32()] : u32();
      }
    }
    kinds[info.name] = rows;
  }
  return { batch, strings, mks, kinds };
}

// { batch, kinds: { name: [{ resId, failed?, store?, msg?, ...answerColumns }] } } → bytes
export function encodeAnswer({ batch, kinds }) {
  const strings = [''];
  const strIdx = new Map([['', 0]]);
  const str = (s) => {
    if (!s) return 0;
    if (!strIdx.has(s)) { strIdx.set(s, strings.length); strings.push(s); }
    return strIdx.get(s);
  };
  const entries = Object.entries(kinds).filter(([, rows]) => rows.length);
  for (const [, rows] of entries) for (const r of rows) str(r.msg);
  const blobs = strings.map((s) => enc.encode(s));
  let size = 4 + 4 + 4 + 4 + blobs.reduce((a, b) => a + 4 + b.length, 0) + 4;
  for (const [name, rows] of entries) {
    const info = RES_KINDS[name];
    size += 2 + 4 + rows.length * (4 + 1 + 1 + 4);
    for (const c of info.ans) {
      size += rows.length * SIZE[c.type];
      if (c.type === 'U32List') for (const r of rows) size += 4 * (r[c.name]?.length ?? 0);
    }
  }
  const out = new Uint8Array(size);
  const v = new DataView(out.buffer);
  let at = 0;
  const u8 = (x) => v.setUint8(at++, x);
  const u16 = (x) => { v.setUint16(at, x, true); at += 2; };
  const u32 = (x) => { v.setUint32(at, x, true); at += 4; };
  const f64 = (x) => { v.setFloat64(at, x, true); at += 8; };
  out.set(enc.encode('TSRA'), 0);
  at = 4;
  u32(RES_VERSION);
  u32(batch);
  u32(blobs.length);
  for (const b of blobs) { u32(b.length); out.set(b, at); at += b.length; }
  u32(entries.length);
  for (const [name, rows] of entries) {
    const info = RES_KINDS[name];
    u16(info.id);
    u32(rows.length);
    for (const r of rows) u32(r.resId);
    for (const r of rows) u8(r.failed ? 1 : 0);
    for (const r of rows) u8(r.store === false ? 0 : 1);
    for (const c of info.ans) {
      const t = c.type;
      for (const r of rows) {
        const x = r[c.name] ?? 0;
        if (t === 'U8') u8(x); else if (t === 'U16') u16(x); else if (t === 'F64') f64(x);
        else if (t === 'U32List') u32(x?.length ?? 0); else u32(x);
      }
      if (t === 'U32List') for (const r of rows) for (const x of r[c.name] ?? []) u32(x);
    }
    for (const r of rows) u32(str(r.msg));
  }
  return out;
}
