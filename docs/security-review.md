# Security review (plan P3-20; D-H09, D-R09)

This checkpoint reviews the surfaces that existed at P3-20, one by one.
Each section states the surface, the threat, what the code does, and a
conclusion. Problems found were fixed in this step and have a fixture,
unit test or fuzz target. Surfaces added later are reviewed in addenda
by the steps that add them:
- P3-21: document providers, locators and the reference manifest;
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
| bibliography file (Node) | `executor.mjs` (plan P0-11) | below `rootDir` or the document's folder, after `realpath`; otherwise `bib-load` |
| bibliography (browser) | `fetch` relative to the page | the author's (§0) |
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
