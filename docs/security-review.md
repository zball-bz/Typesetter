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
