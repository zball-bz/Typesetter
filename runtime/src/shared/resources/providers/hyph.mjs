// The built-in hyphenation-pattern provider (plan P4-06; D-X09): a
// language's Liang patterns for each hyphPatterns row, from the dictionary
// assets tools/hyphc.mjs --assets writes (runtime/assets/hyph: index.json
// maps BCP-47 tags to files; a file is {tag, leftmin, rightmin, hyphenChar,
// patterns, exceptions}). A tag resolves as written, lower-cased, then by
// its ever shorter prefixes (de-AT → de → de-1996); a language the assets
// do not have fails its row (the engine falls back and says so).
const BASE = new URL('../../../../assets/hyph/', import.meta.url);

// read(url) → the file's text, or a promise of it (fetch in a worker; a
// host passes its own, e.g. node:fs for file: URLs)
export function hyphProvider({ base = BASE, read = (url) => fetch(url).then((r) => {
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.text();
}) } = {}) {
  let index = null;
  const files = new Map();  // file → Promise<dictionary>
  const json = (name) => Promise.resolve().then(() => read(new URL(name, base))).then(JSON.parse);
  const load = (file) => {
    if (!files.has(file)) files.set(file, json(`${file}.json`));
    return files.get(file);
  };
  return {
    async resolve(rows, { stale } = {}) {
      index ??= json('index.json');
      const map = await index;
      const out = [];
      for (const r of rows) {
        let tag = String(r.lang).toLowerCase().replace(/_/g, '-'), file;
        while (tag && !(file = map[tag])) tag = tag.includes('-') ? tag.slice(0, tag.lastIndexOf('-')) : '';
        if (!file) {
          out.push({ resId: r.resId, failed: true, msg: `no hyphenation patterns for ${r.lang}` });
          continue;
        }
        const d = await load(file);
        if (stale?.()) return out;
        out.push({ resId: r.resId, patterns: d.patterns, exceptions: d.exceptions, leftmin: d.leftmin,
                   rightmin: d.rightmin, hyphenChar: d.hyphenChar });
      }
      return out;
    },
  };
}
