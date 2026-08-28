// Content tree → flow units of linebreak blocks (document-model §6).
// M2: Latin words + spaces + hyphen points, links, inline/block code,
// headings (size-composed styles), list markers, quote indents, rules.
#pragma once
#include "../math/math.h"
#include "../measure/measure.h"

namespace tsr {

enum : u16 {
  BF_SPACE = 1,        // trimmed at line edges; carries stretch (unless punct)
  BF_HYPHEN = 2,
  BF_CJK = 4,          // CJK ideograph char block (letter-spacing target)
  BF_PUNCT_GLYPH = 8,  // CJK punct glyph (half squeezed away when its
  BF_PUNCT_SP = 16,    //   compressible half-space is absent)
  BF_PUNCT_OPEN = 32,  // glyph blank is on the LEFT (squeeze margin-left)
  BF_BOUND = 64,       // CJK–Latin boundary glue (synthetic, no character)
  BF_INDENT = 128,     // paragraph indent block (synthetic, unbreakable)
  BF_PAIR = 256,       // ——/…… two-char block: no internal letter-spacing
  BF_REF = 512,        // resolver-synthesized run (rendered data-syn="ref";
                       //   skipped by the copy rebuild, document-model §9.3)
};

struct LinebreakBlock {
  Su width = 0, breakWidth = 0, spaceWidth = 0;
  double rawPx = 0;        // unquantized measured width (justification math)
  float breakPenalty = 0;  // INF = unbreakable after this block
  float stretchWeight = 0;
  StyleId style = 0;
  u16 flags = 0;
  StrRef text = 0;
  StrRef linkUrl = 0;  // 0 = not inside a link
  StrRef anchorId = 0; // inline anchor (footnote marker): run gets id="tsr-<id>"
  // Latin word spaces: cross-space kerning context (document-model §6).
  // gap width = m(trigram) - m(prevCh) - m(nextCh); 0 = no correction.
  // Hyphen points reuse the same fields with a JUNCTION bigram (no space):
  // adjacent pieces render as one shaped run, so the browser kerns across
  // the piece boundary — the un-broken hyphen block carries that delta.
  StrRef ctxTrigram = 0, ctxPrev = 0, ctxNext = 0;
  float kernPx = 0;  // hyphen junction kern, applied when NOT broken here
  bool widthResolved = false;
  // inline formula (math-design.md §8): width DEFINED by the box, never
  // measured; layout takes the line's vertical extents from the box
  const MathBox* math = nullptr;
  Span span;
  bool isSpace() const { return flags & BF_SPACE; }
  bool isHyphen() const { return flags & BF_HYPHEN; }
  bool isCjkChar() const { return (flags & BF_CJK) && !(flags & BF_PUNCT_GLYPH); }
  bool isPunctGlyph() const { return flags & BF_PUNCT_GLYPH; }
  bool isSynthetic() const { return flags & (BF_BOUND | BF_INDENT); }
};

constexpr float BREAK_INF = 1e18f;

// One table cell: its own miniature block stream, broken to the cell width
// by the same KP breaker (document-model §6; alignment is layout-side).
struct TableCell {
  std::vector<LinebreakBlock> blocks;
  std::vector<u32> breakpoints;
  double breakCost = 0;
};

struct FlowUnit {
  enum class K : u8 { Text, Code, Rule, Raw, Table, Math, Image } kind = K::Text;
  const ContentNode* src = nullptr;
  Su indent = 0;
  bool tightAbove = false;  // list-item start: reduced inter-unit gap
  bool ragged = false;      // display unit (heading): no justify, no hyphens
  bool centered = false;    // figure caption: line slack split both sides
  StrRef anchor = 0;  // label anchor: first line renders id="tsr-<label>"
  StrRef marker = 0;  // list marker text (0 = none), rendered in the gutter
  StyleId markerStyle = 0;
  StyleId codeStyle = 0;
  // one Code line = a sequence of styled runs (CH1); plain code is a
  // single run per line. Replaces the old per-line StrRef list.
  struct CodeRun {
    StrRef text = 0;
    StyleId style = 0;
    bool isComment = false;  // comment-aware hanging (verbatim-design §4)
  };
  std::vector<std::vector<CodeRun>> codeRuns;
  // CH4 grid: monospace is a METRIC CONTRACT — every char is 1ch (CJK 2ch),
  // the engine measures exactly one thing per code style: ch itself ("0").
  bool codeWrap = true;      // absolute lines have no scroll container
  StrRef chRef = 0;          // interned "0" (the Latin ch probe)
  StrRef cjkChRef = 0;       // interned "中" (measured CJK width — no more
                             //   assumed 2:1; budget uses the real ratio)
  StrRef codeLang = 0;       // language tag (font-feature selection)
  Su sidebarW = 0;           // sidecar column width (0 = no sidecar);
                             //   sidecar rows reuse `cells` (one per line)
  i32 codeLineNo = 0;        // 0 = no numbers; else first line number
  std::vector<u32> hlLines;  // 1-based highlighted lines
  StrRef rawHtml = 0;   // Raw: handler-declared passthrough markup
  double rawHpx = 0;    // Raw: declared height (px)
  // Image (figure-design.md §3): display box in su; src 0 = placeholder
  // (unsafe scheme or failed load — the box carries the alt text)
  StrRef imgSrc = 0, imgAlt = 0;
  Su imgW = 0, imgH = 0;
  u8 floatSide = 0;  // 0 = block; 1 = left float, 2 = right float (F2)
  // F2 float tracker decisions (figure-design.md §4), stored at break time
  // and replayed verbatim by layout so the two phases cannot disagree:
  Su narrow = 0;         // Text: width of the first narrowK lines
  u32 narrowK = 0;       // Text: how many leading lines run beside the float
  bool narrowLeft = false;
  Su floatShiftSu = 0;   // stacked float: placed this far below its cursor (F2 stacking)  // Text: float on the left (lines shift right)
  Su floatClearSu = 0;   // any unit: extra advance to clear the active float
  const MathBox* mathBox = nullptr;  // Math: display formula
  StrRef eqTag = 0;                  // Math: "(n)" right-margin number
  u32 tCols = 0;               // Table: column count
  std::vector<u8> tAligns;     // Table: per-column 'l'/'c'/'r'
  std::vector<TableCell> cells;  // Table: row-major cells
  std::vector<LinebreakBlock> blocks;
  // filled by the typeset loop (Text units)
  std::vector<u32> breakpoints;
  double breakCost = 0;
};

struct TopBlock {
  u32 pid = 0;
  const ContentNode* node = nullptr;
  std::vector<FlowUnit> units;
};

std::vector<TopBlock> emitDoc(const ContentTree& tree, Arena& arena,
                              Interner& strs, StyleTable& styles,
                              const Config& cfg, DiagSink& diags);

// Fills widths from the store; returns what is still missing (deduped).
MeasureRequest resolveWidths(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const Config& cfg);

std::string dumpBlocks(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles);
std::string dumpBreaks(const std::vector<TopBlock>& tops);
std::string dumpMathBoxes(const std::vector<TopBlock>& tops, const Interner& strs);

}  // namespace tsr
