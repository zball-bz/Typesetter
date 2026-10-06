// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// The URL policy (engine/schema/url_policy.def; plan P3-20): the locator's table,
// the engine's safeImageSrc and safeLinkUrl. use: 'image' | 'link' | 'load'.
const SCHEMES = {"http":{"uses":["image","link","load"],"prefix":""},"https":{"uses":["image","link","load"],"prefix":""},"mailto":{"uses":["link"],"prefix":""},"data":{"uses":["image"],"prefix":"image/"}};
export function urlAllowed(src, use) {
  const s = String(src);
  const colon = s.indexOf(':');
  if (colon < 0) return true;
  const stop = s.search(/[/?#]/);
  if (stop >= 0 && stop < colon) return true;
  const r = SCHEMES[s.slice(0, colon).toLowerCase()];
  return !!r && r.uses.includes(use) && s.slice(colon + 1).startsWith(r.prefix);
}
