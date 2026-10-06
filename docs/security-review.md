# Security review (plan P3-20; D-H09, D-R09)

This checkpoint reviews the surfaces that existed at P3-20, one by one.
Each section states the surface, the threat, what the code does, and a
conclusion. Problems found were fixed in this step and have a fixture,
unit test or fuzz target. Surfaces added later are reviewed in addenda
by the steps that add them:
- P3-21: document providers, locators and the reference manifest;
- P3-23: the presentation map's elements, roles and selectors;
- P3-31: inputs and `#use`;
- P5-01: `.tsmf`.

## 0. Trust model

A document is **code**. Its statements and splices are JavaScript that the
executor runs:
- in the browser: in the session's worker, with the page's origin;
- in Node: in the exporting process.

The engine does not sandbox a document's script. A host that typesets a
document runs its author's code. This is the model behind RawHtml (§1) and
the reason the review guards the paths a *trusted* author can get wrong
by accident (a pasted `javascript:` link) rather than an adversarial one.

**Conclusion: accepted.** A host must not typeset an untrusted document
without isolating the worker, for example in a cross-origin iframe, and,
for Node, the exporting process. The engine-dist consumer (the blog)
typesets only its own author's posts.

## 1. RawHtml: the one unescaped path (D-R09)

`raw(html, …)`, from a document's script or a fence handler, is written
unescaped:
- `render/typeset_html.cc` (`FragKind::Raw`, "the ONE unescaped path");
- `render/semantic_html.cc` (`Kind::raw`).

No other path writes markup from document data: every other attribute
value goes through `Tag::attr` (escaped) or a validated literal.

**Conclusion: accepted, by §0.** The input is the author's own code's
output.

Plan P3-28: the shell's `measureHtml` capability writes the same markup,
for a `raw(measure: 'host')` box, into a hidden probe in the document's view
to measure it, and removes it in the same task — the markup the page then
paints, nothing more.

## 2. Element and attribute allowlists (D-R09)

Attributes:
- Both writers emit only the names in `render/html_writer.h kHtmlAttrs`.
- `Tag::attr` takes a `consteval AttrName`, so an unlisted attribute is a
  compile error. A repeated one is a `render-attr` diagnostic, and the first
  wins.
- P3-14 added `colspan` and `rowspan` (table cells).

Elements: the writers use literal element names, except two data-driven
ones.
- A class's `html` field accepts only `figure`.
- A role's `html` (`elements.json roles`, host `semantics.*` patches) was
  any lowercase name, so `script` or `iframe` would have been written as an
  element. **Fixed:** a role reads as a phrasing element of an allowlist
  (sup, sub, strong, b, em, i, small, span, code, kbd, samp, var, mark,
  cite, q, abbr, dfn, s, u, del, ins, bdi, time, data). Anything else
  refuses the registry. Tested in `unitRegistry`.

Values:
- `class` tokens: domain `classlist`.
- Ids: AnchorNamer over the domain `label`, with no control characters,
  escaped.
- `lang`: domain `lang`.
- Inline CSS values: domains `color`, `font` (escaped with `declEsc`),
  `size`, `len`, `lens`; numbers are formatted by the writer.
- `title` (an error's message), `data-copy` and `alt`: escaped.

**Conclusion: closed.** The allowlists grow only with an engine version
and a review addendum.

## 3. Locators and resource paths

| resource | where | policy |
|---|---|---|
| image `src` | `support.h safeImageSrc`, both writers, emit | relative paths, http(s), `data:image/*`; anything else is a placeholder plus `image-src` |
| link `href` | **new:** `support.h safeLinkUrl`, checked when the ops are decoded (`ops.cc`, `Dom::Url` with key `url`: a link's or a reference's) | relative references, http(s), `mailto`; anything else drops the URL with an `ops-arg` warning |
| bibliography file (Node) | `executor.mjs` (plan P0-11); since P3-21 the resource host's locator (addendum) | below `rootDir` or the document's folder, after `realpath`; otherwise `bib-load` |
| bibliography (browser) | `fetch` relative to the page; since P3-21 the locator, http(s) only | the author's (§0) |
| webfonts | host settings `fonts.*` | the host's, trusted |

The link policy was the finding of this review. The `url` domain accepted
any string, so `[x](javascript:…)` became a live `href` on the typeset
page, on the semantic page, and on the no-JS static export, where the
reader clicks it. A scheme is whatever precedes the first `:` that comes
before any `/`, `?` or `#`. It is compared exactly against the allowlist,
so spellings with tabs or spaces, which browsers strip, are not allowed
schemes either. Tests: fixture `inline/link-url-diag`, `unitRegistry`.

Locators with requester classes and document providers arrive with P3-21
(D-I09), whose addendum reviews them.

**Conclusion: closed** for the surfaces that exist.

## 4. Decoders

Every decoder of host- or document-supplied bytes has a libFuzzer target
(`engine/fuzz/`, `tools/fuzz.sh`):

| decoder | input | target |
|---|---|---|
| ops reader (`ops/ops.cc`) and everything up to the semantic page: ingest, declarations' JSON (registry patches), rule JSON, normalize, resolve | the executor's ops buffer | `fuzz_opreader` |
| settings (`api/settings.gen.cc`) | `tsr2_set_config` JSON | `fuzz_settings` |
| resource answers (RES) | `tsr2_provide` | `fuzz_resanswer` |
| LowerProgram | the compiler's program buffer | `fuzz_lower` |
| fragment programs (plan P2-13: `m\`…\``, `m.parse`, sidecar parse requests) | the host's request bytes | `fuzz_fragment` |
| line pass, inline parser | `.tsm` source | `fuzz_linepass`, `fuzz_inline` |
| declared inputs: labels manifests (plan P3-31) | `tsr2_set_input` | `fuzz_inputs` |
| `.tsmf` math fonts (plan P5-01) and math layout with what it accepts | `tsr2_set_input` (`mathFonts`) | `fuzz_tsmf` |

The runtime's JS decoders read only the engine's own output: the
RenderResult frame (`commit.mjs decodeResult`) and the request codec
(`rescodec.mjs`).

**Acceptance:** `tools/fuzz.sh --long 30` ran all seven targets, 4.3 minutes each and about 26 million executions in all, with no crash (2026-10-06, PROGRESS P3-20).

## Addendum P3-21: the resource host, document providers, the manifest

**Surfaces.**
- One locator for every load: `$.load`, `ctx.load`, `#bibliography` and an
  image's size.
- A provider registry open to the host (`createEngine({providers})`,
  `renderTsm({providers, host})`) and, from P3-31, to documents (`#use`).
- The reference manifest that `renderTsm` returns and the static exporter
  copies from.

**The locator** (`runtime/src/shared/resources/locator.mjs`). A reference is
checked against `url_policy.def` for its use before it is resolved: a load
takes http(s) only, an image also `data:image/*`. A relative reference
resolves against its source's base. In Node, a file must lie below the
document root or the document's folder:
- by path, when it is resolved (`..` segments normalized);
- by real path, when it is read, so a symbolic link may not lead out
  either (`readFileConfined`).

This replaces the executor's own check (P0-11) and now covers `$.load` and
`ctx.load` too. A denial is an error to the document's script ("resource
outside the document root", "resource scheme not allowed for a load") and a
`denied` row in the manifest. Tests: `tools/check-export.mjs` (`$.load`
below the root, denied above it).

**Document providers.** A provider that a document registers answers only
the kinds whose `resources.def` column `docProviders` is true:
- `codeTokens`: runs over the document's own code text;
- `boxInfo`: the size of the document's own image.

Neither answer is executable or reaches markup unescaped:
- Token runs are validated by the engine (sorted, disjoint, on UTF-8
  boundaries, of a known tag; else the row fails and the code is plain).
- A box is three finite non-negative numbers.

Their rows are answered `store: false`, so the Session never serves one
document's answer to another. Every other kind (widths, font metrics, font
faces) is the host's: `register(kind, p, {document: true})` refuses it. A
provider that throws fails its kind's rows, never the batch.

**Host providers** are the host's code, trusted like its settings (§0).

**The manifest** is data for the exporter and the host. The exporter copies
an entry only when it is a relative path or a file below the post's folder,
after resolving symbolic links, so a manifest row cannot make the exporter
copy a file from elsewhere.

**Conclusion: closed.** Document providers wait for `#use` (P3-31), whose
addendum reviews module loading.

## Addendum P3-23: the presentation map

**Surface.** The elements the semantic page writes for kinds, roles and
classes are data: `engine/data/elements.json`'s `html` rows. The host
patches them through `semantics.html`, a document through `$.element(name,
{html})`. P3-20's role allowlist (§2) becomes the rows' allowlists
(`engine/src/elements/presentation.h`):
- a block row names a flow or sectioning element (`p`, `div`, `section`,
  `aside`, `nav`, `figure`, `dl`, …, `h{level}`);
- an inline row names a phrasing element (§2's list);
- a slot names a flow element;
- `aria` names a role of WAI-ARIA 1.2 document structure or DPUB-ARIA;
- a projection is one of five fixed shapes.

A row naming anything else refuses the registry: a document's declaration
is `decl-invalid`, and the built-in rows stand. Tests: `unitRegistry`
(script, iframe, an unknown ARIA role and an image slot are refused; `like:`
inherits).

**Attributes.** One name joins `kHtmlAttrs`: `role`, whose value comes only
from the allowlist above. `data-role` writes a node's role, which is a
validated identifier (domain `ident`). The stylesheet's new selectors,
`[data-role="…"]` and `.tsr-c-…`, take a role or class only when it is a
CSS-safe token; anything else is left out.

**Sidecar notes** now appear inline in the semantic page's code. They are
written through the same escaping inline writer as any paragraph, and the
fence's marker is escaped text.

**Conclusion: closed.**

## Addendum P3-31: declared inputs, the project driver, `#use`

**Surfaces.**
- The declared input `labels` (`inputs.def`): other documents' label
  manifests, given by the host (`tsr2_set_input`, `{inputs}`, the project
  driver) or read for the document by `$.labels.import(src)`.
- The settings `project.doc`, `project.starts`, `project.urls`.
- `#use(spec)`: a JavaScript module the host imports for the document, and
  the providers it registers.

**Inputs.** A manifest is untrusted data even when the host passes it: it
is another document's output, possibly stale or hand-edited. The decoder
(`semantic/manifest.cc`) validates before anything is kept:
- the shape (`{v: 1, doc, totals, labels}`, each label's fields typed);
- sizes: at most 4096 manifests, 2^20 labels each, labels and anchors of
  256 bytes, 16 number groups of 16 components;
- integers within ±10^9.

A malformed blob is refused whole (`input-labels`, warning) and nothing is
imported. Fuzz target `fuzz_inputs` runs the decoder and the resolver on
arbitrary blobs. A manifest's strings reach the page only as text (a title,
a number: escaped by the writers) or as an id after `#` (the attribute
writer escapes it). The page of another document is a link's URL: the
`project.urls` value, or the doc key when there is none. Until this step it
was written as the href's head unchecked, so a manifest whose `doc` was
`javascript:…` would have made every reference into it a script link.
`AnchorNamer::href(doc, label)` now applies the link policy
(`url_policy.def`, as `#link`'s URL at ingest), and a page outside it is
none, so the reference points into this page. Test: `unitRenderFragment`.

`$.labels.import(src)` reads through the document's resource job, with the
locator and confinement of `$.load` (requester `input`). The script gets
nothing back, so a document cannot read files through it that `$.load`
could not.

**`#use` modules.** A module is the document's code (§0) and is trusted as
the document is: in Node it runs in the exporting process with that
process's rights; in a browser it runs in the session's worker. The review
guards what a trusted author can get wrong by accident:
- **Resolution.** The spec resolves like a load (use `load`). In a browser
  that means http(s) only, so no `data:`, `blob:` or `javascript:`. In Node
  the file must lie below the document root or the document's folder, by
  path and by real path (`readFileConfined`), so a symbolic link may not
  lead out. A denied or failed module is a `use-module` warning and a
  `denied` or `failed` row (role `module`) in the manifest.
- **The import.** The module is imported from its URL with `?h=<hash of the
  bytes read>`. The hash keys the module instance; it is not an integrity
  check. In a browser the import fetches the URL again, so a server that
  answers differently the second time is imported as it answers then (the
  same trust as any script the page loads). The module's own imports are
  its code, not a document reference, so they are not confined (by §0).
- **Registration.** The module registers only through this execution's `$`
  and `std`. Its providers are this job's, for `docProviders` kinds only
  (`codeTokens`, `boxInfo`; `ResourceJob.register` refuses the others),
  and answered `store: false`, so nothing it answers reaches another
  document through the Session.
- **Retention.** Each distinct module content is a new instance that the
  worker (or the Node process) keeps until it ends. An editing session that
  changes a module often keeps its old instances. **Accepted**: the cost is
  bounded by the edits, and a host recycles its worker.

**Determinism** is a correctness property, not a security one (D-I08). The
recorder executes every fixture twice in one process; the `checkExecution`
policy does the same for a host in dev mode (`exec-nondeterministic`).

**Conclusion: closed.** One defect was fixed: the unchecked page of another
document.

## Addendum P5-01: host math fonts (`.tsmf`), reference ink

**Surface.** A host's math font arrives as the declared input `mathFonts`:
`.tsmf` blobs back to back (`tools/mathc.py --tsmf`, the format in
math-design §15). In a browser, the worker fetches each declared font's
`metrics` URL (role `math`). In Node, `renderTsm` reads it below `rootDir`.
Either way it is the host's file, trusted by §0. The engine still treats
the bytes as untrusted, as it does labels manifests. A document cannot
supply one: a declared input is never shown to a script, and no document
call adds to `mathFonts`.

**The decoder** (`math/font.cc MathFontRegistry::decode`) validates the
bytes as the ops reader does:
- magic and version;
- a total length that is exactly the blob's (`loadAll` splits the input by
  each blob's own length);
- every count bounded, both by a fixed limit and by the bytes left;
- glyph records and variant chains sorted by code point;
- every chain's variant and part ranges inside their tables;
- every size variant and assembly part one of the font's own glyph records
  (a `fuzz_tsmf` finding: a variant without a record laid out as a
  stand-in painted in another font; replay `test/fuzz/fuzz_tsmf/`);
- no trailing bytes;
- `upem` in 16…16384, and the line metrics within ±4 em;
- every MATH constant, glyph metric and assembly part within ±8 em (real
  fonts stay under 4), and the three percentage constants within 0…100.
  So no sum a formula makes leaves the Su range, and no style scale exceeds
  1.

A refusal says why (`math-font-invalid`). The target `fuzz_tsmf` decodes
arbitrary bytes. For every blob it accepts, it checks the lookup invariants
and lays out ten formulas in two styles, with the font first in the chain
(its constants) and second (fallback glyphs). Those formulas cover scripts,
limits, fractions, radicals, the vertical and horizontal stretch paths,
accents, grids and text runs.

**Paint.** A host font's name is `[a-z0-9-]{1,64}`. Its family has at most
128 bytes and contains no control character, `"`, `\`, `<`, `>` or `;`.
The family reaches markup only as the quoted CSS string of a glyph span's
inline `font-family`, written by `declEsc`, so it cannot close the string,
the declaration or the attribute. The static export writes each host math
face's `@font-face` from the host's own font list (`renderTsm`'s `fonts`),
with `<>{};` removed and the values JSON-quoted. That output cannot close
the `<style>` element.

**The registry** is process-wide (one per WASM instance), and fonts are
identified by the hash of their blob. The same bytes are one font, decoded
once. Different bytes are another font, even with the same name or the same
woff2 hash, so no document is ever given another blob's tables. A
document's `math.fonts` resolves names only among its own input's fonts and
the embedded `euler`. What other documents loaded is invisible to it, and
its output never depends on them. The registry holds at most 64 fonts and
keeps each one until the process ends. A 65th distinct font is refused
(`math-font-invalid`). **Accepted:** the bound is fixed, and a host
recycles its worker.

**Reference ink** (`math.referenceInk`, resource row `fontInk`) adds two
non-negative finite numbers per face. They are validated like `fontVmet`'s
and clamped by `MetricStore::hostPx`. A failed or missing row falls back to
0.7 em and 0.2 em with a diagnostic. Reference ink is a host kind, not a
document provider's (`docProviders` false).

**Conclusion: closed.**

