// Content tree → flow units of linebreak blocks (document-model §6).
// M2: Latin words + spaces + hyphen points, links, inline/block code,
// headings (size-composed styles), list markers, quote indents, rules.
#pragma once
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
  // Latin word spaces: cross-space kerning context (document-model §6).
  // gap width = m(trigram) - m(prevCh) - m(nextCh); 0 = no correction.
  StrRef ctxTrigram = 0, ctxPrev = 0, ctxNext = 0;
  bool widthResolved = false;
  Span span;
  bool isSpace() const { return flags & BF_SPACE; }
  bool isHyphen() const { return flags & BF_HYPHEN; }
  bool isCjkChar() const { return (flags & BF_CJK) && !(flags & BF_PUNCT_GLYPH); }
  bool isPunctGlyph() const { return flags & BF_PUNCT_GLYPH; }
  bool isSynthetic() const { return flags & (BF_BOUND | BF_INDENT); }
};

constexpr float BREAK_INF = 1e18f;

struct FlowUnit {
  enum class K : u8 { Text, Code, Rule } kind = K::Text;
  const ContentNode* src = nullptr;
  Su indent = 0;
  bool tightAbove = false;  // list-item start: reduced inter-unit gap
  bool ragged = false;      // display unit (heading): no justify, no hyphens
  StrRef marker = 0;  // list marker text (0 = none), rendered in the gutter
  StyleId markerStyle = 0;
  StyleId codeStyle = 0;
  std::vector<StrRef> codeLines;
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

std::vector<TopBlock> emitDoc(const ContentTree& tree, Interner& strs,
                              StyleTable& styles, const Config& cfg);

// Fills widths from the store; returns what is still missing (deduped).
MeasureRequest resolveWidths(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const Config& cfg);

std::string dumpBlocks(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles);
std::string dumpBreaks(const std::vector<TopBlock>& tops);

}  // namespace tsr
