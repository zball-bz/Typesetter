// Content tree → flow units whose inline streams are HLists (plan P1-12;
// shape/hlist.h), lowered to linebreak blocks for the legacy breaker
// (document-model §6).
// M2: Latin words + spaces + hyphen points, links, inline/block code,
// headings (size-composed styles), list markers, quote indents, rules.
#pragma once
#include <memory>
#include <variant>

#include "../math/math.h"
#include "../measure/measure.h"
#include "../shape/hlist.h"

namespace tsr {

struct ContentNode;
struct ContentTree;

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
  BF_FIL = 1024,       // fil glue (plan P2-16: fill): takes the line's slack
  BF_SYNTH = 2048,     // an inline object's synthetic glue (between a formula's
                       //   parts; plan P3-26: no character, not a boundary)
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
  // (plan P3-26) an inline object's part (shape/objects.h): its kind and
  // extents — its width DEFINED by the object, never measured; payload: the
  // part's own (a formula segment's box), for the oracle to compare
  bool obj = false;
  ObjKind objKind = ObjKind::Math;
  Su objAsc = 0, objDesc = 0;
  const void* objPayload = nullptr;
  Span span;
  bool isSpace() const { return flags & BF_SPACE; }
  bool isHyphen() const { return flags & BF_HYPHEN; }
  bool isCjkChar() const { return (flags & BF_CJK) && !(flags & BF_PUNCT_GLYPH); }
  bool isPunctGlyph() const { return flags & BF_PUNCT_GLYPH; }
  bool isSynthetic() const { return flags & (BF_BOUND | BF_INDENT | BF_SYNTH); }
};

constexpr float BREAK_INF = kPenInf;

// What the legacy breaker reads of a block (plan P1-12): production lowers
// each HList to these (fuseLegacy); the full LinebreakBlock is built only for
// the blocks dump and the equivalence check (fuseCheck, legacy.h).
struct BreakBlock {
  Su width = 0, breakWidth = 0, spaceWidth = 0;
  float breakPenalty = 0;  // INF = unbreakable after this block
  u16 flags = 0;           // the BF_ kind bits
  bool isSpace() const { return flags & BF_SPACE; }
  bool isHyphen() const { return flags & BF_HYPHEN; }
};

// An image's size as emit knows it (plan P1-16; design T6 S4): intrinsic
// px (declared, or pulled from the host) and the scale; layout resolves the
// display box against its measure — emit reads no width.
struct ImageSize {
  double iw = 0, ih = 0;     // intrinsic px
  double scale = 0;          // a fraction of the measure (0 = the intrinsic width)
  bool placeholder = false;  // unsized or unsafe: measure × measure/3
};
// the display box: the scaled or intrinsic width, never wider than the
// measure, the height from the aspect ratio
inline void resolveImageSize(const ImageSize& s, double measurePx, Su& w, Su& h) {
  double dw, dh;
  if (!s.placeholder) {
    dw = s.scale > 0 ? s.scale * measurePx : s.iw;
    if (dw > measurePx) dw = measurePx;
    if (dw < 1) dw = 1;
    dh = dw * s.ih / s.iw;
  } else {
    dw = measurePx;
    dh = measurePx / 3;
  }
  w = suRoundPx(dw);
  h = suRoundPx(dh);
}

// One shaped inline stream (plan P1-18): a paragraph's, a table cell's, a
// float caption row's or a sidecar row's — the item list KP breaks and
// layout materializes (document-model §6; alignment is layout-side).
struct Flow {
  HList hl;
  StrRef anchor = 0;  // a label inside it (an inline labelled group): its first line's id
  Span span;          // (plan P3-07) a cell's or sidecar row's node: an empty cell's line
  std::vector<BreakBlock> blocks;  // fuseLegacy(hl), for the legacy breaker
  std::vector<u32> blockStart;     // block b = hl.items [blockStart[b], blockStart[b+1])
  std::vector<LinebreakBlock> legacy;  // MIGRATION: the legacy emitter's blocks (fuseCheck)
};
using TableCell = Flow;

// The typed payloads of a leaf (plan P1-18; findings
// emitter/flowunit-kind-switch, break-layout-pages/unit-kind-switch): what
// emit shapes besides inline streams, one type per kind of content. Which
// layouter reads it is the box tree's (boxtree/block.h), never a kind switch.
struct RuleData {};
// one code line = a sequence of styled runs (CH1); plain code is a single
// run per line
struct CodeRun {
  StrRef text = 0;
  StyleId style = 0;
  bool hang = false;  // code.hang content: comment-aware hanging (verbatim-design §4),
                      // read from the run's style (plan P2-08)
  StrRef cls = 0;     // tok-<tag> for a token run (rendered from P3-18)
};
struct GridData {  // a code block (verbatim-design.md)
  StrRef features = 0;  // its text.features (plan P3-02): painted on its rows
  StyleId codeStyle = 0;
  std::vector<std::vector<CodeRun>> lines;
  // CH4 grid: monospace is a METRIC CONTRACT — every char is 1ch (CJK 2ch),
  // the engine measures exactly one thing per code style: ch itself ("0").
  bool wrap = true;          // absolute lines have no scroll container
  StrRef chRef = 0;          // interned "0" (the Latin ch probe)
  StrRef cjkChRef = 0;       // interned "中" (measured CJK width — no more
                             //   assumed 2:1; budget uses the real ratio)
  StrRef lang = 0;           // language tag (font-feature selection)
  i32 lineNo = 0;            // 0 = no numbers; else first line number
  std::vector<u32> hlLines;  // 1-based highlighted lines
  u32 firstLine = 0;         // (plan P3-11) a two-track table's row: the block line it shows first
  bool snap = false;         // snap-kerning: its probes are measured even when it does not wrap
  // (plan P3-07) each logical line's source: exact when its length is the
  // line's (a verbatim body: a row is its slice), else the line's node
  // (or the body) as a whole; empty when the code has no source
  std::vector<Span> lineSpans;
};
struct RawData {  // handler-declared passthrough markup and its height
  StrRef html = 0;
  double hPx = 0;
  double wPx = 0;  // (plan P3-14) its declared width: its intrinsic width in a content-fitted column
  double minWPx = 0;  // (plan P3-14) the least width it takes (a content-fitted column's floor)
};
// figure-design.md §3: src 0 = placeholder (unsafe scheme or failed load —
// the box carries the alt text); its display box is layout's
struct ImageData {
  StrRef src = 0, alt = 0;
  ImageSize size;
};
struct MathData {  // a display formula
  const MathBox* box = nullptr;  // laid out in Measure (plan P1-25), once its text runs are measured
  StrRef tag = 0;  // "(n)" right-margin number
  StrRef src = 0;  // its source as written (the copy contract)
  StrRef formula = 0;  // (plan P2-15) its source as the lexer reads it
  u32 epoch = 0;       // its declaration epoch
  double sizePx = 0;
  Span span;
  StyleId style = 0;  // its context style (paint: colour)
};
using LeafData = std::variant<std::monostate, RuleData, GridData, RawData, ImageData, MathData>;

// A leaf's shaped content: its inline stream (paragraphs), its other tracks
// (`cells`: a table's cells, a float's caption rows, a code block's sidecar
// rows — by the leaf's layouter) and its payload.
struct FlowUnit : Flow {
  std::vector<Flow> cells;
  LeafData data;
};

struct TopTree;
struct TopBlock {
  u32 pid = 0;
  const TopTree* tree = nullptr;  // its box tree (Doc::boxtree)
  std::vector<FlowUnit> units;    // per leaf
};

// Shapes the box tree's leaves. mathText: text-font runs in formulas measure
// through the pull loop; the emitter reports what is still missing (Doc
// re-emits once provided)
// rt: the answered resources (code tokens, image sizes; plan P1-19)
struct BoxTree;
class ResourceTable;

// One Emit pass, top-level block by block (plan P1-20; design T9 M5): the
// shaping scratch is shared across the pass. A block whose display formula
// lacks text metrics is incomplete: top() returns false with what it lacked
// (the caller discards it and its diagnostics, and retries once measured).
class EmitPass {
 public:
  EmitPass(const BoxTree& bt, Arena& arena, Interner& strs, StyleTable& styles, const EmitSettings& cfg,
           DiagSink& diags, const MetricStore* metrics, const ResourceTable* rt);
  ~EmitPass();
  bool top(size_t t, TopBlock& out, std::vector<MeasureItem>& missing);

 private:
  struct State;
  const BoxTree& bt_;
  std::unique_ptr<State> st_;
};
std::vector<TopBlock> emitDoc(const BoxTree& bt, Arena& arena, Interner& strs, StyleTable& styles,
                              const EmitSettings& cfg, DiagSink& diags, const MeasureNeeds* mathText = nullptr,
                              const ResourceTable* rt = nullptr);

// (plan P3-25) where an inline formula may break: the settings' class
// tables (math.breakAfter / math.breakBefore)
inline MathBreaks mathBreaks(const EmitSettings& cfg) { return MathBreaks{cfg.mathBreakAfter, cfg.mathBreakBefore}; }

// What resolveWidths needs to lay out a deferred formula (plan P1-13): the
// document's arena, strings and styles.
class MathEnv;
struct ObjectEnv {
  Arena& arena;
  Interner& strs;
  StyleTable& styles;
  double docBasePx;
  DiagSink* diags = nullptr;  // a finalized layout's own diagnostics (coverage)
  const MathEnv* math = nullptr;  // the document's math declarations (plan P2-15)
};

// Fills widths from the store; returns what is still missing (deduped).
// With an ObjectEnv, deferred formulas are laid out and spliced once their
// text-font runs are measured (their misses join the request).
MeasureRequest resolveWidths(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const EmitSettings& cfg,
                             ObjectEnv* objects = nullptr);

// The lowering of an HList to today's blocks (plan P1-12; the legacy breaker
// reads them until P4-08): a specified table per item and glue class, equal
// field by field to what the pre-HList emitter produced (fuseCheck, legacy.h).
// The full form feeds the dumps and the check; production keeps only what
// the breaker reads.
void fuseLegacy(const HList& h, std::vector<LinebreakBlock>& blocks, std::vector<u32>& blockStart);
void fuseLegacy(const HList& h, std::vector<BreakBlock>& blocks, std::vector<u32>& blockStart);
void fuseLegacy(std::vector<TopBlock>& tops);  // every unit and cell, the breaker's form

std::string dumpBlocks(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles);
std::string dumpHLists(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles);
std::string dumpMathBoxes(const std::vector<TopBlock>& tops, const Interner& strs);
std::string dumpMathIRs(const std::vector<TopBlock>& tops, const Interner& strs, const MathEnv* math = nullptr);

}  // namespace tsr
