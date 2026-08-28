# Real-world corpus report

Three published sources were converted to `.tsm` and typeset to see how
the engine behaves on text it was not written for (2026-08-28):

| source | form | size | what it exercises |
|---|---|---|---|
| Wikipedia *Typesetting* (en) | wikitext | 20K chars, 8 figures, 11 refs | floats, footnotes from `<ref>`, long URLs |
| Wikipedia 活字印刷术 (zh) | wikitext | 20K chars, 8 figures | CJK justification, mixed quotes, floats |
| pbr-book.org 4ed §1.1–1.2 | HTML | 87K chars, 16 figures, 20 code fragments | italic-heavy prose, literate code, figures |
| HoTT book *Introduction* | LaTeX + BibTeX | 50K chars, 55 formulas, 23 citations | math, citations/bibliography, quotes |

Converters: `tools/convert/{wiki2tsm,html2tsm,tex2tsm}.mjs`; corpus:
`examples/real-world/` (`fetch.mjs` regenerates). Acceptance = the e2e
audit (no browser re-break, right edge within 1px, no compression past
the shrink threshold) at 680px, Chromium.

## Engine defects found and fixed

1. **Italic → roman junctions were 1px short.** Word-space kern contexts
   (`gap = m(prev+' '+next) − m(prev) − m(next)`) were budgeted across
   style boundaries, but the browser shapes each run separately and never
   kerns across them. Every justified line ending a title in italics
   (pbr-book, bibliography entries) missed by ~1px. Fix: contexts only
   between blocks of one style/link (emit.cc `fillSpaceContexts`).
2. **Long URLs overflowed.** A Wikipedia footnote URL compressed its line
   by 60px. Fix: glyph-free break opportunities after `/ ? & = . - _` in
   unhyphenatable tokens ≥ 20 chars (`cfg.urlBreakPenalty`, `emitWord`).
3. **Curly quotes in English were CJK punctuation.** `“ spaces ”` and
   `don ’ t` in the HoTT text — U+2018/2019/201C/201D took the full-width
   path with half-em compressible spaces and the CJK font. Fix: Latin
   context (no CJK neighbour) keeps them in the Latin word.
4. **Remote figures could not be measured.** `NEED_IMAGES` fetched in the
   worker; cross-origin images without CORS (Commons, pbr-book.org) are
   opaque there. Fix: the worker asks the main thread, which reads
   `naturalWidth/Height` off an `<img>`.
5. **Caption/ref supplements were Chinese in English documents** (“图 3：”
   in PBR). Fix: `tsr_set_lang` / `renderTsm(src, {lang})` →
   Figure/Table/Eq. + `: ` for non-CJK documents.

After these, the four committed documents pass the audit with 0 failures
except one HoTT line (+1.0px, an inline formula boundary — open).

## Engine gaps observed (not fixed)

- **Stacked floats.** Wikipedia opens with two thumbnails in a row; the
  second float clears the first, and the text after both starts beside
  the *second* one, leaving the first float's height empty on the left.
  TeX-style float queue needs `LineWidths` in piecewise form (K1 lines at
  width A, K2 at width B …) instead of the single narrow prefix; KP's
  `widths.at(i)` already abstracts this, so it is break.h + layout replay
  + the tracker in `Doc::typeset`. The most visible remaining defect.
- **Footnote inserts in print** still render as endnotes (notes-design §1).
- **Table column widths** are equal; the HoTT points-of-view table wants
  content-fitted columns.
- **Bibliography hanging indent**: entries are flush paragraphs; a
  hanging-indent unit (first line full, continuation indented) is a small
  layout feature worth having for both notes and bibliographies.

## Converter gaps (source-side, documented, not engine problems)

- pbr-book.org ships math as pre-rendered MathJax SVG; 77 formulas
  became their accessibility titles in italics ("upper A is proportional
  to cosine theta"). A LaTeX source is needed to typeset them.
- HoTT: cross-chapter `\cref` targets (chapters not converted) resolve to
  `??`; the book's own macro set is covered only as far as the
  introduction uses it; unknown math macros become identifiers.
- Wikipedia: tables and most templates are dropped; images resolve via
  `Special:FilePath` at 800px.

## What reads well

CJK justification with punctuation compression on the 活字印刷术 page,
Knuth–Plass on the long HoTT paragraphs (no rivers, ~2 hyphens/screen),
the literate code fragments in PBR (tokens highlighted, `<<fragment>>`
names preserved), and the HoTT bibliography generated from BibTeX via
CSL-JSON with first-citation numbering.
