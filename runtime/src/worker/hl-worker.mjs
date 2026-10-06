// The highlighter's own thread (plan P5-02): web-tree-sitter, a language's
// grammar and its query are loaded and run here, beside the engine's
// worker, so a document's first highlighter start — import, wasm, query
// compile — overlaps its execution and first passes instead of following
// them. The shell starts it with the engine's worker and hands it one end
// of a MessageChannel ({type: 'port'}); the engine's worker holds the
// other (hl-thread.mjs). Messages on the port:
//   {op: 'prefetch', lang}           load a language (no answer)
//   {op: 'tokenize', id, lang, text} → {id, runs}  (runs: Uint32Array, transferred)
import { prefetch, tokenize } from './tokens.mjs';

self.onmessage = ({ data }) => {
  if (data?.type !== 'port') return;
  const port = data.port;
  port.onmessage = async ({ data: m }) => {
    if (m.op === 'prefetch') {
      prefetch(m.lang);
      return;
    }
    let runs;
    try {
      runs = await tokenize(m.lang, m.text);
    } catch {
      runs = new Uint32Array(0);  // plain code, never a stall (tokens.mjs's contract)
    }
    port.postMessage({ id: m.id, runs }, [runs.buffer]);
  };
};
