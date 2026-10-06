// The project driver (plan P3-31; design T9 A7, T3 S7; D-S07): a book of
// .tsm files, each a document of its own, rendered so that references cross
// files and numbering continues across them — no document's resolve becomes
// multi-pass, and no script reads another document's resolved values.
//
//   const { docs, manifests, diagnostics } = await renderProject({
//     files: [{ doc, source, baseDir, rootDir }],  // book order
//     settings, urls, continue: ['heading', …], offset: { heading: 4 } })
//
// Pass A: each document with its key and nothing else — its labels product
//   (start-independent, independent of what it imports).
// starts: the prefix sums, in book order, of the totals of the counters in
//   `continue` (plus `offset`, where the book starts).
// Pass B: each document with project.{doc, starts, urls} and the other
//   documents' manifests as its labels input. A manifest whose title cites
//   another document changes once those resolve: then one more pass — at most
//   three in all (project-unstable after that).
import { renderTsm } from './render.mjs';

export const CONTINUED_COUNTERS = Object.freeze(['heading', 'figure', 'table', 'equation']);

export async function renderProject({ files, settings = {}, urls = {}, continue: continued = CONTINUED_COUNTERS,
                                      offset = {}, maxPasses = 3, render = renderTsm }) {
  const keys = files.map((f) => f.doc);
  if (new Set(keys).size !== keys.length) throw new Error('renderProject: two documents share a key');
  const withProject = (project) => ({ ...settings, project: { ...(settings.project ?? {}), ...project } });
  const run = (f, project, labels) => render(f.source, {
    settings: withProject(project), baseDir: f.baseDir, rootDir: f.rootDir ?? f.baseDir,
    ...(labels ? { inputs: { labels } } : {}),
  });

  // Pass A: the manifests
  let manifests = [];
  for (const f of files) manifests.push((await run(f, { doc: f.doc })).labels);

  // the starts: where each document's counters begin
  const starts = {};
  const acc = { ...offset };
  manifests.forEach((m, i) => {
    starts[keys[i]] = { ...acc };
    const totals = JSON.parse(m).totals ?? {};
    for (const c of continued) acc[c] = (acc[c] ?? 0) + (totals[c] ?? 0);
  });
  const allUrls = Object.fromEntries(keys.map((k) => [k, urls[k] ?? `${k}.html`]));

  // Pass B, again while a manifest changes (a title that cites another document)
  const diagnostics = [];
  let results = [];
  for (let pass = 2; pass <= maxPasses; pass++) {
    results = [];
    for (let i = 0; i < files.length; i++) {
      const others = manifests.filter((_, j) => j !== i);
      results.push(await run(files[i], { doc: keys[i], starts, urls: allUrls }, `[${others.join(',')}]`));
    }
    const next = results.map((r) => r.labels);
    const stable = next.every((m, i) => m === manifests[i]);
    manifests = next;
    if (stable) break;
    if (pass === maxPasses)
      diagnostics.push(`warning project-unstable: the manifests still changed after ${maxPasses} passes (titles that cite each other)`);
  }
  return {
    docs: results.map((r, i) => ({ ...r, doc: keys[i], url: allUrls[keys[i]] })),  // (its labels: r.labels)
    manifests: Object.fromEntries(keys.map((k, i) => [k, manifests[i]])),
    starts, urls: allUrls, diagnostics,
  };
}
