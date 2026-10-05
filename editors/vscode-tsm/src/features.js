// Editor features from the engine's outline (plan P1-09), as plain data —
// extension.js turns them into vscode objects; test/e2e/editor.spec.mjs
// checks them in Node. `lineOf(index)` maps a UTF-16 index to its line.

// built-in region handlers: the constructors that take a Body, from the
// runtime's static manifest (plan P2-03); other names render as role groups
const builtinRegions = (manifest = []) => manifest.filter((c) => c.body).map((c) => c.name).sort();
// region handlers the document registers itself: $.region("name", …)
const definedRegions = (text = '') =>
  [...text.matchAll(/\$\.region\(\s*(["'])([A-Za-z_][\w-]*)\1/g)].map((m) => m[2]);

// heading tree: [{ title, level, line, endLine, children }], each heading
// extending to the line before the next heading of the same or higher level
function headingTree(outline, lineOf, lastLine) {
  const roots = [];
  const stack = [];
  const flat = outline.headings.map((h) => ({
    title: h.title || '(untitled)', level: h.level, line: lineOf(h.span[0]), endLine: lastLine, children: [],
  }));
  for (let i = 0; i < flat.length; i++) {
    const h = flat[i];
    for (let j = i + 1; j < flat.length; j++)
      if (flat[j].level <= h.level) {
        h.endLine = Math.max(h.line, flat[j].line - 1);
        break;
      }
    while (stack.length && stack[stack.length - 1].level >= h.level) stack.pop();
    (stack.length ? stack[stack.length - 1].children : roots).push(h);
    stack.push(h);
  }
  return roots;
}

// folding ranges [{ start, end }] (lines): heading sections, fences, regions
function foldingRanges(outline, lineOf, lastLine) {
  const out = [];
  const walk = (hs) => {
    for (const h of hs) {
      if (h.endLine > h.line) out.push({ start: h.line, end: h.endLine });
      walk(h.children);
    }
  };
  walk(headingTree(outline, lineOf, lastLine));
  for (const x of [...outline.fences, ...outline.regions]) {
    const s = lineOf(x.span[0]), e = lineOf(Math.max(x.span[0], x.span[1] - 1));
    if (e > s) out.push({ start: s, end: e });
  }
  return out.sort((a, b) => a.start - b.start || b.end - a.end);
}

// completion candidates: region names (the manifest's Body constructors,
// the handlers the document registers, its own regions) and its labels
function regionNames(outline, manifest = [], text = '') {
  return [...new Set([...builtinRegions(manifest), ...definedRegions(text), ...outline.regions.map((r) => r.name)])];
}

function labelNames(outline) {
  return [...new Set(outline.labels.map((l) => l.id))];
}

module.exports = { headingTree, foldingRanges, regionNames, labelNames };
