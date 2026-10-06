// An LRU cache with validators (plan P3-21; design T9 A2 LruCache): the
// host-side cache of URL-keyed answers (an image's size, an execute-time
// load). Entries are evicted least-recently-used past maxEntries or
// maxBytes. An entry older than its time to live (a failure: failureTtlMs)
// is stale: its validator (ETag / Last-Modified over http, mtime and size
// for a file) decides whether it still holds. Content-keyed answers never
// come here: the engine's Session keeps them (design T9 A5). The limits are
// the host policy's (schema "policy": resourceCache*, resource*TtlMs), read
// when used, so createEngine({policy}) applies to a cache already made.
import { POLICY } from '../settings.gen.mjs';

export class LruCache {
  constructor(policy = POLICY) {
    this.policy = policy;
    this.map = new Map();  // key → { value, bytes, validator, failed, at } (insertion order = recency)
    this.bytes = 0;
  }
  // the entry under key (made most recent), or undefined
  get(key) {
    const e = this.map.get(key);
    if (!e) return undefined;
    this.map.delete(key);
    this.map.set(key, e);
    return e;
  }
  // whether an entry is within its time to live
  fresh(e, now = Date.now()) {
    return now - e.at < (e.failed ? this.policy.resourceFailureTtlMs : this.policy.resourceTtlMs);
  }
  set(key, value, { bytes = 0, validator = null, failed = false } = {}) {
    this.delete(key);
    this.map.set(key, { value, bytes, validator, failed, at: Date.now() });
    this.bytes += bytes;
    const { resourceCacheEntries: maxEntries, resourceCacheBytes: maxBytes } = this.policy;
    while (this.map.size > maxEntries || (this.bytes > maxBytes && this.map.size > 1)) {
      const oldest = this.map.keys().next().value;
      this.delete(oldest);
    }
  }
  // an entry revalidated: still holds, its age restarts
  touch(key) {
    const e = this.map.get(key);
    if (e) e.at = Date.now();
  }
  delete(key) {
    const e = this.map.get(key);
    if (!e) return;
    this.bytes -= e.bytes;
    this.map.delete(key);
  }
}
