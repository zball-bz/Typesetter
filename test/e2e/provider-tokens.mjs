// The built-in code-token row, re-declared as a host's provider module
// (plan P5-02, equal footing): the same highlighter, registered by the host
// through createEngine({providers}) instead of by the worker.
import { tokenize } from '/runtime/src/worker/tokens.mjs';

export default {
  resolve: (rows) => Promise.all(rows.map(async (r) => ({ resId: r.resId, runs: await tokenize(r.lang, r.text) }))),
};
