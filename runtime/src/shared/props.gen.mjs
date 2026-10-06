// GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit.
// Run style properties (plans P1-02, P2-08): the $.style.push / #style / style:
// keys and their value domains.
export const STYLE_KEYS = Object.freeze({"weight":"weight","italic":"italic","decoration":"decoration","fontRole":"fontRole","baseline":"baseline","code.hang":"hang","size":"size","font":"font","lang":"lang","color":"color","sizePx":"sizePx"});
export const STYLE_SUGAR = Object.freeze({"bold":["weight",700],"italic":["italic",true],"underline":["decoration",1],"overline":["decoration",2],"strike":["decoration",4]});
export const STYLE_FLAGS = Object.freeze({"decoration":{"under":1,"over":2,"strike":4}});
export const DOMAINS = Object.freeze({
  ident: Object.freeze({ max: 64, re: /^(?:[A-Za-z_][A-Za-z0-9_\-]*)$/u }),
  label: Object.freeze({ max: 512, re: /^(?:[^\x00-\x1f\x7f]+)$/u }),
  lang: Object.freeze({ max: 64, re: /^(?:[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*)$/u }),
  size: Object.freeze({ max: 16, re: /^(?:[0-9]{1,4}(\.[0-9]{1,4})?(em|px|%))$/u }),
  intlist: Object.freeze({ max: 256, re: /^(?:-?[0-9]{1,9}(\.-?[0-9]{1,9})*)$/u }),
  rangeset: Object.freeze({ max: 0, re: /^(?: *[0-9]{1,7} *(- *[0-9]{1,7} *)?(, *[0-9]{1,7} *(- *[0-9]{1,7} *)?)*)$/u }),
  color: Object.freeze({ max: 64, re: /^(?:#([0-9A-Fa-f]{3}|[0-9A-Fa-f]{4}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})|var\(--[A-Za-z0-9_\-]+\)|(rgb|rgba|hsl|hsla)\([0-9.,/ %deg\-]*\)|[A-Za-z]+)$/u }),
  font: Object.freeze({ max: 512, re: /^(?: *("[^"'\\\x00-\x1f\x7f]+" *|'[^"'\\\x00-\x1f\x7f]+' *|[A-Za-z0-9_\-\u0080-\u{10ffff}][A-Za-z0-9 _\-\u0080-\u{10ffff}]*)(, *("[^"'\\\x00-\x1f\x7f]+" *|'[^"'\\\x00-\x1f\x7f]+' *|[A-Za-z0-9_\-\u0080-\u{10ffff}][A-Za-z0-9 _\-\u0080-\u{10ffff}]*))*)$/u }),
  copy: Object.freeze({ max: 1024, re: /^(?:text|omit|replace:[^\x00-\x08\x0b-\x1f\x7f]*)$/u }),
  classlist: Object.freeze({ max: 512, re: /^(?:[A-Za-z_][A-Za-z0-9_\-]*( [A-Za-z_][A-Za-z0-9_\-]*)*)$/u }),
  extname: Object.freeze({ max: 32, re: /^(?:[a-z][a-z0-9\-]*)$/u }),
  features: Object.freeze({ max: 256, re: /^(?:( *("[A-Za-z0-9]{4}"|'[A-Za-z0-9]{4}')( +(on|off|[0-9]{1,3}))?( *, *("[A-Za-z0-9]{4}"|'[A-Za-z0-9]{4}')( +(on|off|[0-9]{1,3}))?)* *)?)$/u }),
});
const utf8Length = (s) => { let n = 0; for (const c of s) { const cp = c.codePointAt(0); n += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; } return n; };
// the JS twin of the C++ matchDomain (early diagnostics; the reader decides)
export function validDomain(name, s) {
  const d = DOMAINS[name];
  if (!d || typeof s !== 'string') return false;
  return (!d.max || utf8Length(s) <= d.max) && d.re.test(s);
}
