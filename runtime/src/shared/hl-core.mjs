// The highlighter's core (plan P3-22; design T9 A6), shared by the worker's
// token provider and the editor: a capture's class and the priority
// contract, the same as the native twin's (engine/src/code/native_tokens.cc).
// Everything else — the classes, the capture and language aliases, the
// overlays — is engine/schema/languages.json's (languages.gen.mjs).
import { TOKEN_TAGS } from './syntax.gen.mjs';
import { CAPTURE_ALIAS, LANG_OF } from './languages.gen.mjs';

export { TOKEN_TAGS };

// a capture name → its class index (its first segment, or that segment's
// alias), -1: none
export function tagOf(captureName) {
  const head = String(captureName).split('.')[0];
  const t = TOKEN_TAGS.indexOf(head);
  if (t >= 0) return t;
  const a = CAPTURE_ALIAS[head];
  return a ? TOKEN_TAGS.indexOf(a) : -1;
}

// a fence tag → its built-in language (a name, an alias, a profile), else null
export const languageOf = (tag) => LANG_OF[String(tag).toLowerCase()] ?? null;

// The priority contract: captures [{s, e, pat, ...}] in the query cursor's
// order → the winners, ascending and disjoint. A stable sort by (start,
// pattern) keeps ties in cursor order; an earlier capture wins an overlap.
export function resolveCaptures(caps) {
  caps.sort((a, b) => a.s - b.s || a.pat - b.pat);
  const out = [];
  let covered = 0;
  for (const c of caps) {
    if (c.s < covered || c.e <= c.s) continue;
    out.push(c);
    covered = c.e;
  }
  return out;
}
