// The built-in code-token provider (plan P3-21; design T9 A2/A6): the
// highlighter's runs for each (language, text) the engine asks for.
import { tokenize } from '../../../worker/tokens.mjs';

export const tokenProvider = {
  async resolve(rows, { stale }) {
    const out = [];
    for (const t of rows) {
      const runs = await tokenize(t.lang, t.text);
      if (stale?.()) return out;
      out.push({ resId: t.resId, runs });
    }
    return out;
  },
};
