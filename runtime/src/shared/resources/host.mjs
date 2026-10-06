// The resource host (plan P3-21; design T9 A2 ResourceHost): one place that
// turns the engine's needs and a document's execute-time loads into
// answers — one locator, one cache and failure policy, one provider
// registry in which built-in, host and document providers stand on equal
// terms — and records one manifest of everything a document references.
//
//   const host = new ResourceHost({ policy })        // providers + cache (its limits: policy)
//   host.register('boxInfo', provider)                // provider.resolve(rows, ctx) → answer rows
//   host.register('codeTokens', { match: (row) => row.lang === 'tla', resolve })   // some rows only
//   const job = host.job({ bases: { doc }, root })    // one document's locator and manifest
//   await job.answer(request, { stale, capability }) // the pull loop's batch → its answer
//   await job.load(src, { as: 'text' | 'json' | 'bytes' })   // $.load, ctx.load, #bibliography
//   await job.module(src)                            // #use (plan P3-31): the module, imported by URL + content hash
//   job.register(kind, provider)                     // a document's own provider (#use's providers)
//   job.manifest()                                   // [{ url, role, source, status, requester }]
//
// A kind may have several providers (plan P3-22): a row goes to the latest
// registered whose match(row) accepts it (no match: every row), so a host
// adds a language without replacing the built-in highlighter.
// A document-registered provider (#use, P3-31) may answer only the kinds
// whose keys are authored content (resources.def docProviders: codeTokens,
// boxInfo); its rows carry store: false (the Session keeps no answer of
// one document's code for another). A provider that throws fails the rows
// of its kind (boxInfo: the engine's placeholder), never the batch.
import { RES_KINDS } from '../resources.gen.mjs';
import { POLICY } from '../settings.gen.mjs';
import { LruCache } from './lru.mjs';
import { ResourceLocator, within } from './locator.mjs';

const FAILED = {  // a kind's rows when its provider failed
  boxInfo: (r, msg) => ({ resId: r.resId, w: 0, h: 0, baseline: 0, failed: true, msg }),
  codeTokens: (r, msg) => ({ resId: r.resId, runs: [], failed: true, msg }),
  hyphPatterns: (r, msg) => ({ resId: r.resId, failed: true, msg }),
};

export class ResourceHost {
  constructor({ policy = POLICY, cache = new LruCache(policy) } = {}) {
    this.policy = policy;
    this.cache = cache;
    this.providers = new Map();  // kind → [{ provider, document }], the latest first
    this.seeded = new Map();     // url / file → value (static export, hydration)
  }
  register(kind, provider, { document = false } = {}) {
    const k = RES_KINDS[kind];
    if (!k) throw new TypeError(`resource provider: no kind ${kind}`);
    if (document && !k.docProviders)
      throw new TypeError(`resource provider: a document may not answer ${kind} (only kinds of authored content)`);
    this.providers.set(kind, [{ provider, document }, ...(this.providers.get(kind) ?? [])]);
    return this;
  }
  seed(entries) {
    for (const [key, value] of Object.entries(entries ?? {})) this.seeded.set(key, value);
  }
  job({ bases = {}, root = null } = {}) {
    return new ResourceJob(this, new ResourceLocator({ bases, root }));
  }
}

class ResourceJob {
  constructor(host, locator) {
    this.host = host;
    this.locator = locator;
    this.log = [];  // the execution loads and the needs answered, in order
  }
  note(entry) {
    this.log.push(entry);
  }
  manifest() {
    const seen = new Set();
    return this.log.filter((e) => {
      const k = `${e.role}\0${e.url}`;
      if (seen.has(k)) return false;
      seen.add(k);
      return true;
    });
  }

  // one batch of the pull loop: each kind's rows to its provider
  async answer(req, ctx = {}) {
    const ans = { batch: req.batch, kinds: {} };
    for (const [kind, rows] of Object.entries(req.kinds)) {
      if (!rows.length) continue;
      // (plan P3-31) the document's own providers first, then the host's
      const entries = [...(this.own?.get(kind) ?? []), ...(this.host.providers.get(kind) ?? [])];
      if (!entries?.length) throw new Error(`resource host: no provider for ${kind}`);
      // each row to the first provider that takes it (none: the oldest)
      const shares = new Map();
      for (const r of rows) {
        const entry = entries.find((x) => !x.provider.match || x.provider.match(r)) ?? entries[entries.length - 1];
        if (!shares.has(entry)) shares.set(entry, []);
        shares.get(entry).push(r);
      }
      ans.kinds[kind] = [];
      for (const [entry, mine] of shares) {
        try {
          const out = await entry.provider.resolve(mine, { ...ctx, req, job: this, host: this.host });
          ans.kinds[kind].push(...(entry.document ? out.map((r) => ({ ...r, store: false })) : out));
        } catch (e) {
          const fail = FAILED[kind];
          if (!fail) throw e;
          ans.kinds[kind].push(...mine.map((r) => fail(r, String(e?.message ?? e))));
        }
        if (ctx.stale?.()) return null;
      }
    }
    return ans;
  }

  // (plan P3-31; D-I08) a module (#use): resolved and read as a load (the
  // same locator, policy, cache and manifest, role 'module'), then imported
  // by its URL with its content's hash — ?h=… — so a changed module is a new
  // URL (no stale instance) and its own relative imports resolve against it;
  // an unchanged one is the same instance (shared by the worker's documents:
  // a module keeps no state across executions)
  async module(src, { source = 'doc' } = {}) {
    const where = this.locator.resolve(src, { source, requester: 'exec', use: 'load' });
    if (where.denied) {
      this.note({ url: String(src), role: 'module', source, requester: 'exec', status: 'denied' });
      throw new Error(`module ${where.denied}`);
    }
    const key = where.url ?? where.file;
    let bytes;
    try {
      bytes = await this.read(where);
    } catch (e) {
      this.note({ url: key, role: 'module', source, requester: 'exec', status: 'failed' });
      throw e;
    }
    this.note({ url: key, role: 'module', source, requester: 'exec', status: 'ok' });
    let url = where.url;
    if (!url) url = (await import('node:url')).pathToFileURL(where.file).href;
    return import(/* @vite-ignore */ `${url}${url.includes('?') ? '&' : '?'}h=${contentHash(bytes)}`);
  }
  // (plan P3-31) a provider a document registers (a #use module's
  // providers): this job's only, for the kinds of authored content
  // (resources.def docProviders); its rows are not stored in the Session
  register(kind, provider) {
    const k = RES_KINDS[kind];
    if (!k) throw new TypeError(`resource provider: no kind ${kind}`);
    if (!k.docProviders) throw new TypeError(`resource provider: a document may not answer ${kind} (only kinds of authored content)`);
    (this.own ??= new Map()).set(kind, [{ provider, document: true }, ...(this.own.get(kind) ?? [])]);
    return this;
  }

  // an execute-time load (requester 'exec'): resolved by the locator, read
  // through the cache (revalidated when stale), recorded in the manifest
  async load(src, { as = 'text', source = 'doc', requester = 'exec', role = 'load' } = {}) {
    const where = this.locator.resolve(src, { source, requester, use: 'load' });
    if (where.denied) {
      this.note({ url: String(src), role, source, requester, status: 'denied' });
      throw new Error(`resource ${where.denied}`);
    }
    const key = where.url ?? where.file;
    try {
      const bytes = await this.read(where);
      this.note({ url: key, role, source, requester, status: 'ok' });
      return as === 'bytes' ? bytes : as === 'json' ? JSON.parse(utf8(bytes)) : utf8(bytes);
    } catch (e) {
      this.note({ url: key, role, source, requester, status: 'failed' });
      throw e;
    }
  }
  async read(where) {
    const host = this.host;
    const key = where.url ?? where.file;
    if (host.seeded.has(key)) return toBytes(host.seeded.get(key));
    const hit = host.cache.get(key);
    if (hit && host.cache.fresh(hit)) {
      if (hit.failed) throw new Error(hit.value);
      return hit.value;
    }
    try {
      const got = where.file ? await readFileConfined(where, hit) : await fetchValidated(where.url, hit);
      if (got.unchanged) {
        host.cache.touch(key);
        return hit.value;
      }
      host.cache.set(key, got.bytes, { bytes: got.bytes.length, validator: got.validator });
      return got.bytes;
    } catch (e) {
      host.cache.set(key, String(e?.message ?? e), { failed: true });
      throw e;
    }
  }
}

const utf8 = (b) => new TextDecoder().decode(b);
// (plan P3-31) a module's content hash (FNV-1a 64, hex): its import URL's ?h=
const contentHash = (bytes) => {
  let h = 0xcbf29ce484222325n;
  for (const b of bytes) h = ((h ^ BigInt(b)) * 0x100000001b3n) & 0xffffffffffffffffn;
  return h.toString(16).padStart(16, '0');
};
const toBytes = (v) => (v instanceof Uint8Array ? v : new TextEncoder().encode(typeof v === 'string' ? v : JSON.stringify(v)));

// http(s): a conditional request when the entry has a validator
async function fetchValidated(url, hit) {
  const headers = {};
  if (hit?.validator?.etag) headers['If-None-Match'] = hit.validator.etag;
  if (hit?.validator?.modified) headers['If-Modified-Since'] = hit.validator.modified;
  const res = await fetch(url, { headers });
  if (res.status === 304 && hit) return { unchanged: true };
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  const bytes = new Uint8Array(await res.arrayBuffer());
  const validator = { etag: res.headers.get('etag'), modified: res.headers.get('last-modified') };
  return { bytes, validator };
}
// a file (Node): below its roots after resolving links, revalidated by
// mtime and size
async function readFileConfined(where, hit) {
  const { readFile, realpath, stat } = await import('node:fs/promises');
  const real = await realpath(where.file);
  const roots = await Promise.all(where.roots.map((d) => realpath(d).catch(() => d)));
  if (!roots.some((d) => within(real, d))) throw new Error('resource outside the document root');
  const st = await stat(real);
  const validator = { mtime: st.mtimeMs, size: st.size };
  if (hit?.validator && hit.validator.mtime === validator.mtime && hit.validator.size === validator.size)
    return { unchanged: true };
  return { bytes: new Uint8Array(await readFile(real)), validator };
}
