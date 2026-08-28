// Loopback static server for the preview iframe. VSCode webview origins
// cannot host the runtime's module worker (cross-origin worker scripts),
// so the preview lives in an iframe served from 127.0.0.1 where
// shell.mjs + worker.mjs + wasm run verbatim (editor-design.md §5).
const http = require('node:http');
const fs = require('node:fs/promises');
const path = require('node:path');

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.css': 'text/css',
  '.json': 'application/json',
  '.scm': 'text/plain; charset=utf-8',
  '.tsm': 'text/plain; charset=utf-8',
  '.woff2': 'font/woff2',
  '.otf': 'font/otf',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
};

// docRoots: extra prefixes mapping URL paths to directories (the document's
// own folder, for relative image srcs in the previewed .tsm)
function startServer({ assetRoot, mediaDir, docRoots = new Map() }) {
  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url, 'http://x');
      const p = path.normalize(decodeURIComponent(url.pathname));
      let file = null;
      if (p === '/preview.html' || p === '/') {
        file = path.join(mediaDir, 'preview.html');
      } else {
        for (const [prefix, dir] of docRoots) {
          if (p.startsWith(prefix)) {
            const cand = path.join(dir, p.slice(prefix.length));
            if (cand.startsWith(dir)) file = cand;
            break;
          }
        }
        if (!file) {
          const cand = path.join(assetRoot, p);
          if (!cand.startsWith(assetRoot)) throw new Error('escape');
          file = cand;
        }
      }
      let data;
      try {
        data = await fs.readFile(file);
      } catch (e) {
        // document assets: relative srcs resolve against the document's
        // folder; site-root srcs (/images/x.png) against the SSG's static
        // dirs up the tree (Eleventy/Astro `public`, Hugo `static`) — the
        // fallback list is computed per document by preview.js
        const roots = docRoots.get('__fallback') ?? [];
        let found = null;
        for (const dir of roots) {
          const cand = path.join(dir, p);
          if (!cand.startsWith(dir)) continue;
          try {
            data = await fs.readFile(cand);
            found = cand;
            break;
          } catch { /* next root */ }
        }
        if (!found) throw e;
        file = found;
      }
      res.writeHead(200, {
        'content-type': MIME[path.extname(file)] || 'application/octet-stream',
        'cache-control': 'no-store',
      });
      res.end(data);
    } catch {
      res.writeHead(404);
      res.end('not found');
    }
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => {
      resolve({ port: server.address().port, close: () => server.close() });
    });
  });
}

module.exports = { startServer };
