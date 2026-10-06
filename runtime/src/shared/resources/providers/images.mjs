// The built-in image-box provider (plan P3-21; design T9 A2 boxInfo): an
// image's intrinsic size, by its src resolved by the job's locator (the
// page's base, the scheme policy) — fetched in parallel and read from the
// header (PNG, GIF, WebP, JPEG), else decoded (SVG, AVIF); then the main
// thread's <img> capability (a cross-origin image without CORS). Sizes are
// cached by URL in the host's LRU cache; a failure only for its failure
// time to live (it used to stay for the worker's lifetime). 0×0 is a
// failure: the engine's placeholder and its image-load warning.
import { sniffImageSize } from '../../../worker/image_sniff.mjs';

export function imageProvider({ timeoutMs = 4000 } = {}) {
  return {
    match: (row) => row.kind === 0,  // BoxKind image (plan P3-28: svg and html boxes are measured)
    async resolve(rows, { job, host, capability }) {
      const dims = await Promise.all(rows.map((r) => sizeOf(r.ref, job, host, capability, timeoutMs)));
      return rows.map((r, k) => ({ resId: r.resId, w: dims[k].w, h: dims[k].h, baseline: dims[k].h }));
    },
  };
}

async function sizeOf(src, job, host, capability, timeoutMs) {
  const where = job.locator.resolve(src, { requester: 'image', use: 'image' });
  if (where.denied || !where.url) {
    job.note({ url: String(src), role: 'image', source: 'doc', requester: 'image', status: 'denied' });
    return { w: 0, h: 0 };
  }
  const url = where.url;
  const key = 'image-size\0' + url;
  const hit = host.cache.get(key);
  if (hit && host.cache.fresh(hit)) return hit.value;
  let dims;
  try {
    dims = await fetchSize(url);
  } catch (e) {
    dims = capability
      ? await capability('imageDims', { src: url }, timeoutMs).catch(() => ({ w: 0, h: 0 }))
      : { w: 0, h: 0 };
    if (!(dims.w > 0)) console.warn(`tsr: image failed to load: ${url}`, e);
  }
  const ok = dims.w > 0 && dims.h > 0;
  host.cache.set(key, ok ? dims : { w: 0, h: 0 }, { failed: !ok });
  job.note({ url, role: 'image', source: 'doc', requester: 'image', status: ok ? 'ok' : 'failed' });
  return ok ? dims : { w: 0, h: 0 };
}

// the header carries the size: read a prefix of the body, decode only
// formats the sniffer does not know
async function fetchSize(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  const chunks = [];
  let have = 0;
  if (res.body) {
    const reader = res.body.getReader();
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      have += value.length;
      const head = chunks.length === 1 ? value : concat(chunks, have);
      const r = sniffImageSize(head);
      if (r && !r.more) {
        reader.cancel().catch(() => {});
        if (r.w > 0 && r.h > 0) return r;
        break;
      }
      if (!r) break;  // unknown format: decode below
    }
    for (;;) {  // the rest of the body, for the decoder
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      have += value.length;
    }
  }
  const blob = res.body ? new Blob(chunks, { type: res.headers.get('content-type') ?? '' }) : await res.blob();
  const bm = await createImageBitmap(blob);
  const dims = { w: bm.width, h: bm.height };
  bm.close();
  return dims;
}
function concat(chunks, n) {
  const out = new Uint8Array(n);
  let at = 0;
  for (const c of chunks) {
    out.set(c, at);
    at += c.length;
  }
  return out;
}
