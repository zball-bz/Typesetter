// Prose helpers shared by the converters (plan P3-33): .tsm text that keeps
// its meaning under the prose rules (docs/syntax-design.md "Prose guards"):
//   - an emphasis marker between two ASCII letters or digits is text
//     (Intraword), and a pair needs a glyph inside each marker: emphasis
//     there is written #em[…] / #strong[…];
//   - a bare http(s) URL is a link, verbatim: nothing inside it is escaped
//     (nor inside a link's (target), which is read as written);
//   - a backslash escapes ASCII punctuation only.
// The converters mark emphasis with em() / strong() while they build text
// and resolve the marks last, when the neighbours are final (markers()).

const EM = ['\uE000', '\uE001'];
const STRONG = ['\uE002', '\uE003'];
export const em = (body) => EM[0] + body + EM[1];
export const strong = (body) => STRONG[0] + body + STRONG[1];

const alnum = (c) => c !== undefined && /[A-Za-z0-9]/.test(c);

// the marks → _x_ / *x*, or the function form where a marker would be text
export function markers(s) {
  const pair = /([\uE000\uE002])([^\uE000-\uE003]*)([\uE001\uE003])/;  // innermost first
  for (let m = pair.exec(s); m; m = pair.exec(s)) {
    const isStrong = m[1] === STRONG[0];
    const body = m[2];
    const before = s[m.index - 1], after = s[m.index + m[0].length];
    let rep;
    if (!body.trim()) rep = body;
    else if (alnum(before) || alnum(after) || /^\s|\s$/.test(body)) rep = `#${isStrong ? 'strong' : 'em'}[${body}]`;
    else rep = (isStrong ? '*' : '_') + body + (isStrong ? '*' : '_');
    s = s.slice(0, m.index) + rep + s.slice(m.index + m[0].length);
  }
  return s.replace(/[\uE000-\uE003]/g, '');  // (an unpaired mark)
}

// `$`, `#` and `@` before an identifier escaped, except inside a bare URL and
// a link's (target)
const VERBATIM = /(\]\([^)\s]*\)|https?:\/\/[^\s<>"`|\\$]*)/;
export function escapeProse(s) {
  return s.split(VERBATIM).map((part, k) => (k % 2 ? part
    : part.replace(/\$/g, '\\$').replace(/#/g, '\\#').replace(/@(?=[A-Za-z[])/g, '\\@'))).join('');
}
