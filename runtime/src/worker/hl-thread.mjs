// The code-token provider of the browser worker (plan P5-02): the built-in
// provider's contract (shared/resources/providers/tokens.mjs), answered on
// the highlighter's own thread (hl-worker.mjs). The shell starts that
// worker beside this one and joins them with a MessageChannel ('hl-port');
// it loads nothing until a prefetch — which worker.mjs sends right after
// compile for the languages a document's fences name — or a request.
// Without a port (another host), or when the thread fails, the tokens are
// made here as before.
import { tokenProvider } from '../shared/resources/providers/tokens.mjs';

let port = null;       // to the highlighter worker; false: it failed
let nextId = 1;
const waiting = new Map();  // id → resolve

// the shell's port (worker.mjs 'hl-port'): answers arrive on it
export function attachHighlighter(p) {
  port = p;
  p.onmessage = ({ data }) => {
    const done = waiting.get(data.id);
    waiting.delete(data.id);
    done?.(data.runs);
  };
}
// it could not start (the shell saw its error): answer here from now on
export function detachHighlighter() {
  port = false;
  for (const done of waiting.values()) done(null);
  waiting.clear();
}

function tokenizeThere(lang, text) {
  if (!port) return Promise.resolve(null);
  const id = nextId++;
  return new Promise((resolve) => {
    waiting.set(id, resolve);
    port.postMessage({ op: 'tokenize', id, lang, text });
  });
}

export const threadedTokenProvider = {
  prefetch(lang) {
    if (port) port.postMessage({ op: 'prefetch', lang });
    else tokenProvider.prefetch(lang);
  },
  async resolve(rows, ctx) {
    // every row at once: the thread answers them in order
    const runs = await Promise.all(rows.map((r) => tokenizeThere(r.lang, r.text)));
    if (runs.some((r) => r === null)) return tokenProvider.resolve(rows, ctx);  // no thread: here
    return rows.map((r, i) => ({ resId: r.resId, runs: runs[i] }));
  },
};
