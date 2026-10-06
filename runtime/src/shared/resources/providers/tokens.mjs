// The built-in code-token provider (plan P3-21; design T9 A2/A6): the
// highlighter's runs for each (language, text) the engine asks for.
import { tokenize, prefetch } from '../../../worker/tokens.mjs';

export const tokenProvider = {
  // (plan P5-02) a language the document will ask for, loaded meanwhile
  prefetch,
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
