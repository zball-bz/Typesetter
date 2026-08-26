# Verbatim Grid Design (V)

Status: **design settled (discussion of 2026-08-26) — V1 in progress.**
Generalizes the CH4 character grid ([code-design.md](code-design.md) §4)
into a generic monospace/verbatim text layer. Orthogonal to highlighting:
the grid consumes styled runs, whoever produced them.

## 1. The two uses of a grid (the load-bearing split)

- **Budget**: wrap column arithmetic. Needs only a CONSERVATIVE per-char
  width; in-row rendering still flows naturally at the font's true
  advances. A wrong estimate costs a slightly early break, never
  misalignment. Always on, effectively free.
- **Alignment**: same-column-across-rows (figures, aligned columns).
  Natural flow CANNOT deliver this when the font pair is not exactly
  rational: drift = n·|r − p/q|·ch. Alignment therefore requires either
  pinned rendering (fixed-width spans — the math-glyph precedent) or
  snap-kerning (§3). Enabled per feature, not globally.

## 2. Grid atom via Stern–Brocot

Measure chL ("0") and chC ("中") per code style; r = chC/chL. Walk the
Stern–Brocot tree and take the FIRST convergent p/q satisfying BOTH:

- error budget: maxCols × |r − p/q| × chL < 0.5px (per-row drift under
  half a pixel), and
- **q ≤ 7** (user bound: beyond that the result no longer reads as a
  monospace grid).

Grid atom g = chL/q; width classes {latin: q, cjk: p} atoms (a strict 2:1
font collapses to the current contract immediately). If no convergent
satisfies both, the block stays budget-only (no alignment features).

## 3. Snap-kerning vs ligatures (mutually exclusive, by mechanism)

- **snap-kerning**: letter-spacing Δ = g·q − chL on Latin runs (or the
  CJK mirror) pulls true advances onto the rational grid — then even
  natural flow aligns, no span-pinning needed. Requires script-split runs
  (the body-text CJK mechanism, one extra cut at fold/emit).
- **Hard conflict**: non-zero letter-spacing DISABLES liga/calt in
  browsers. So per block: `ligatures: on` → budget mode, natural flow;
  `ligatures: off` → snap-kerning permitted. Not a tradeoff dial — a
  binary switch.
- **Ligature sets are configurable**: `font-feature-settings` injected on
  code spans; Config.verbatim.fontFeatures default + per-language map +
  fence-arg override (Haskell and C++ want different sets). Measurement
  is unaffected: programming ligatures keep component advance by
  convention (`=>` occupies 2ch).

## 4. Continuation strategies (contIndent)

- `n` (fixed columns), default 2 — the CH4 behavior.
- `hanging` (NEW DEFAULT): this logical line's own leading-whitespace
  columns + n.
- **comment-aware hanging** (auto, on top of either): when the break
  point falls inside a `comment`-tagged run, the continuation aligns to
  that run's start column + its lead-in width, where the lead-in is the
  run's opening "punctuation streak + one space" (`// `, `# `, `-- `,
  `;; `, `* ` all fall out of one language-blind scan). Covers block-
  comment bodies and long line comments without a language-aware wrap
  hook — the token runs already carry the language knowledge we need.
- `bracket` (option, later): align to the column after the last unclosed
  opening bracket — a per-line text scan, no parser needed.

The indent itself renders as literal spaces in the flow (font-independent,
the e2e242e lesson), synthetic for both copy paths.

## 5. Sidebar comments (V3, recorded — build on demand)

Fence-declared marker (e.g. `///` or `#`), chosen by the author to be a
comment or illegal in the source language (string-literal collisions are
the author's risk, documented). Character-level preprocessing splits each
line BEFORE tokenization; code wraps to a measure that stops at the
sidebar column; the sidebar column word-wraps independently; each logical
line is an anchor row with height = max(code rows, comment rows) — a
two-column zip, closed inside the unit, fully deterministic. Copy must
restore `/// …` at end of line. TeX precedent: listings escapechar,
algorithmicx right-aligned \Comment. Scope ≈ half of CH4.

## 6. Rejected / withdrawn

- **Elastic tabstops**: withdrawn by the user (poor editing experience);
  the two-pass column negotiation is recorded here in case it returns.
- **Full language-aware wrap hook**: not opened — comment-aware hanging
  covers the known cases; the provider channel is the natural place for
  per-line metadata if a real counterexample appears.
- **Unifying with the Knuth–Plass text path**: proportional text and the
  grid are different species; two code paths stay.

## 7. Milestones

- **V1**: chC measurement + Stern–Brocot atom (budget), hanging default +
  comment-aware hanging, ligature config (font-feature-settings).
- **V1.5**: script-split runs + snap-kerning (ligatures-off blocks).
- **V3**: sidebar comment column (on demand).
