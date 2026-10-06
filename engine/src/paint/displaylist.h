// The DisplayList (plan P1-18; design T7 "DisplayList"): one render IR for
// every typeset backend. Paint annotates layout's fragments — runs formed at
// run-instance boundaries, typed inline payloads, the geometry the backends
// print — and reads the Config the backends must not; the typeset and paged
// serializers are stateless walks of it (render/typeset_html.cc) that read
// no Config, no FlowUnit and no content tree (architecture lint).
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "../layout/layout.h"

namespace tsr {

// One DOM run of a line, typed by what it paints (design T7 DLRun /
// DLSpacer / DLInlineBox).
struct DLRun {
  enum class K : u8 {
    Words,     // a Latin run: the boxes of items [i, j), its glue as spaces
    Chars,     // a letter-spaced CJK run: the boxes of items [i, j)
    Glyph,     // one box's text (a punctuation glyph, a pinned dash, an error)
    Hyphen,    // a line-final hyphen glyph
    Spacer,    // an indent or a boundary spacer of width w
    Math,      // an inline formula part
    Image,     // an inline image (or its placeholder)
    Raw,       // inline handler markup
    CodeText,  // a code row's segment (text)
    CodeCont,  // a code row's continuation indent: `n` spaces
  } k = K::Words;
  u32 i = 0, j = 0;      // Words / Chars: the line's item range
  StyleId face = 0;      // the run's style
  StrRef link = 0;       // a link run is an <a>
  StrRef id = 0;         // an inline anchor (a footnote marker)
  bool synRef = false;   // a resolver-synthesized run (copy skips it)
  u32 dataS = ~0u;       // its source start (absolute), ~0u = none
  const char* cls = nullptr;  // an extra class (a squeezed glyph's tsr-sqL / tsr-sqR)
  const char* syn = nullptr;  // its data-syn (hyphen, indent, boundary, …)
  // the run's own spacing (copied from layout's line values)
  enum class Fit : u8 { None, LetterSpacing, Pinned } fit = Fit::None;
  double letterPx = 0, marginRightPx = 0, widthPx = 0;
  bool marginRight = false;
  StrRef text = 0;              // Glyph: its text
  std::string_view seg;         // CodeText: its text
  u32 n = 0;                    // CodeCont: the column count
  const MathBox* math = nullptr;  // Math
  Span span;                    // Math / Image / Raw: its source
  StrRef src = 0, alt = 0;      // Math: its source text; Image: src/alt; Raw: markup
  Su w = 0, h = 0;              // Image / Raw: the box
};

// One fragment, painted (design T7 DLLine / DLBox): geometry in su (the
// writer rebases y), the attributes it carries and its runs.
struct DLNode {
  FragKind kind = FragKind::Line;
  Su y = 0, left = 0, width = 0;
  double heightPx = 0;          // Raw / Math / Image / hl CodeRow: the box height
  Su baseline = 0;              // layout's baseline below y (P3: pinned in the HTML)
  StrRef anchor = 0;            // its id
  StrRef anchor2 = 0;           // a second id: an empty marker span first inside it
  Span span;                    // data-s / data-e
  const char* join = nullptr;   // data-join
  bool ragged = false;          // data-ragged
  bool cell = false;            // data-cell
  bool overfull = false;        // data-overfull
  bool hl = false;              // a highlighted code row
  bool contJoin = false;        // a code row continued by the next: data-join=none
  double wordSpacingPx = 0;
  std::string_view features;    // a code row's font-feature-settings
  double lineHeightPx = 0;      // a code row's line-height (centred baseline)
  StrRef marker = 0;            // the gutter (a list marker, a line number)
  StyleId markerStyle = 0;
  // Math: the formula, its source, its number, where it sits in the row
  const MathBox* math = nullptr;
  StrRef mathSrc = 0, eqTag = 0;
  double mathTopPx = 0, eqRightPx = 0;
  // Image: src (0 = placeholder) and alt; Raw: markup
  StrRef src = 0, alt = 0;
  const HList* h = nullptr;     // Line: the stream its Words / Chars runs read
  u32 runBegin = 0, runEnd = 0;  // into DLBlock::runs
};

struct DLBlock {
  u32 pid = 0;
  u32 srcBase = 0;          // source anchors are relative to it (flowing output)
  Su h = 0;
  double gapAfterPx = -1;   // the gap to the next block (< 0: the last)
  std::vector<DLNode> nodes;  // fragment order
  std::vector<DLRun> runs;
};

// the document root's render contract (plan P1-04)
struct DLRoot {
  std::string_view lang;
  std::string_view fontBody, fontCjk, fontMono, fontMonoCjk;
  double basePx = 0;
};

struct TopBlock;
struct PaintSettings;
class StyleTable;

// Paint (design T7 paintBlock): block p of the layout into `out` (reused).
void paintBlock(const LayoutResult& lr, size_t p, const std::vector<TopBlock>& tops, const Interner& strs,
                const PaintSettings& cfg, DLBlock& out);
DLRoot paintRoot(const PaintSettings& cfg);
// tsrc --stage=dl (debug)
std::string dumpDisplayList(const LayoutResult& lr, const std::vector<TopBlock>& tops, const StyleTable& styles,
                            const Interner& strs, const PaintSettings& cfg);

}  // namespace tsr
