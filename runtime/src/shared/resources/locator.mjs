// The locator (plan P3-21; design T9 A2 ResourceLocator): where a
// document's reference points, by the source that made it and who asks.
//   bases:     SourceId → a URL (the page's, a module's) or a directory
//              (Node: the document's folder); 'doc' is the document's own
//   root:      a directory every file access is confined to (Node: the site
//              or repository root; P0-11)
//   requester: 'exec' (a document's script: $.load, #bibliography),
//              'image' (an image's src), 'input' (declared inputs, P3-31 —
//              project outputs are served to it only)
// The scheme policy is the engine's (url_policy.gen.mjs): a relative
// reference resolves against its source's base; a /site-root path against
// root when the base is a directory. resolve → { url } | { file, roots } |
// { denied: reason }; a file's real path is checked again when it is read
// (a symbolic link may not lead out of the roots either).
import { urlAllowed } from '../url_policy.gen.mjs';

const isUrl = (s) => /^[a-z][a-z0-9+.-]*:/i.test(s);

export class ResourceLocator {
  constructor({ bases = {}, root = null } = {}) {
    this.bases = bases;
    this.root = root;
  }
  resolve(src, { source = 'doc', requester = 'exec', use = 'load' } = {}) {
    const s = String(src);
    if (!urlAllowed(s, use)) return { denied: `scheme not allowed for a ${use}` };
    if (isUrl(s)) return { url: s };
    const base = this.bases[source] ?? this.bases.doc;
    if (base && isUrl(String(base))) {
      try {
        return { url: new URL(s, base).href };
      } catch {
        return { denied: 'not a URL' };
      }
    }
    if (base == null && this.root == null) return { url: s };  // a page-relative reference
    return fileOf(s, base, this.root, requester);
  }
}

// a file reference: /site-root paths against root, relative ones against
// the base directory; confined to both (P0-11), synchronously by path
function fileOf(s, base, root, requester) {
  const join = (...parts) => normalize(parts.join('/'));
  const rootDir = root ?? base;
  const baseDir = base ?? rootDir;
  const file = s.startsWith('/') ? join(rootDir, s) : join(baseDir, s);
  const roots = [...new Set([rootDir, baseDir].filter(Boolean).map(normalize))];
  if (!roots.some((d) => within(file, d))) return { denied: 'outside the document root' };  // ("resource outside …")
  return { file, roots, requester };
}
// a path with its '.' and '..' segments resolved (no file system access)
export function normalize(p) {
  const abs = p.startsWith('/');
  const out = [];
  for (const seg of p.split('/')) {
    if (!seg || seg === '.') continue;
    if (seg === '..') out.pop();
    else out.push(seg);
  }
  return (abs ? '/' : '') + out.join('/');
}
export const within = (p, d) => p === d || p.startsWith(d.endsWith('/') ? d : d + '/');
