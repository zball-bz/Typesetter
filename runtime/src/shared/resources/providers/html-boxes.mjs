// The built-in host-box provider (plan P3-28; design T9 M11): a raw box an
// author asked the host to measure (raw(html, {measure: 'host'})) — an
// html box, or an svg whose attributes do not size it (the engine answers
// the others itself) — measured at its width by the main thread's
// measureHtml capability in the document's own view (`scope`: its doc id),
// so the page's fonts and CSS apply. Answers are cached in the host's LRU
// cache by (view, width, markup); a failure only for its failure time to
// live, and it fails the row: the box keeps its declared size.
export function htmlBoxProvider({ timeoutMs = 4000 } = {}) {
  return {
    match: (row) => row.kind !== 0,  // BoxKind: 0 image, 1 svg, 2 html
    async resolve(rows, { host, capability, scope }) {
      return Promise.all(rows.map(async (r) => {
        const key = `html-box\0${scope ?? ''}\0${r.availPx}\0${r.ref}`;
        const hit = host.cache.get(key);
        let v = hit && host.cache.fresh(hit) ? hit.value : null;
        if (!v) {
          const got = capability
            ? await capability('measureHtml', { html: r.ref, widthPx: r.availPx, scope }, timeoutMs).catch(() => null)
            : null;
          const ok = got && Number.isFinite(got.h) && got.h >= 0;
          v = ok ? { h: got.h, baseline: Number.isFinite(got.baseline) ? got.baseline : got.h } : { failed: true };
          host.cache.set(key, v, { failed: !ok });
        }
        return v.failed
          ? { resId: r.resId, w: 0, h: 0, baseline: 0, failed: true, msg: 'measureHtml failed' }
          : { resId: r.resId, w: r.availPx, h: v.h, baseline: v.baseline };
      }));
    },
  };
}
