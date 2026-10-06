// Content tree → flow units whose inline streams are HLists (plan P1-12;
// shape/hlist.h), which the breaker reads (plan P4-08; document-model §6).
// M2: Latin words + spaces + hyphen points, links, inline/block code,
// headings (size-composed styles), list markers, quote indents, rules.
#pragma once
#include <memory>
#include <variant>

#include "../math/math.h"
#include "../measure/measure.h"
#include "../resource/box.h"
#include "../shape/hlist.h"

namespace tsr {

// (plan P3-32; design T9 M12) Emit and Measure are width-independent, and the
// compiler proves it: stage code reads settings only through its view (Config
// is not even defined here: the architecture lint's config-closure rule), and
// neither view has host.width. A schema row that made one of them read the
// width fails here, not in a golden.
template <class View>
concept ReadsWidth = requires(const View& v) { v.widthPx; };
static_assert(!ReadsWidth<EmitSettings> && !ReadsWidth<MeasureSettings>,
              "host.width reaches no stage before Layout (emit products are width-independent)");
static_assert(ReadsWidth<LayoutSettings>, "the concept names the width row");

struct ContentNode;
struct ContentTree;

// A replaced box's size as emit knows it (plan P1-16; P3-28, design T6
// IntrinsicSize): one record for every replaced box — an image, a raw box —
// saying where its size comes from. Layout resolves the display box against
// its measure; emit reads no width.
//   Declared:    the author's w × h (a raw box's h: one leading when unset)
//   Provided:    an image's intrinsic px, from the host (boxInfo at width 0)
//   Host:        measured by the host at the box's width (boxInfo at that
//                width): h and baseline arrive with layout's width; w and h
//                hold the declared fallback
//   Placeholder: unsized or unsafe: measure × measure/3
enum class SizeSource : u8 { Declared, Provided, Host, Placeholder };
struct IntrinsicSize {
  double w = 0, h = 0;  // px (an image's intrinsic px; a raw box's declared)
  double minW = 0;      // (plan P3-14) the least width it takes (a content-fitted column's floor)
  double scale = 0;     // a fraction of the measure (0 = the intrinsic width)
  SizeSource source = SizeSource::Declared;
  bool placeholder() const { return source == SizeSource::Placeholder; }
};
// an image's dims from its intrinsic size bw × bh: the host's size fills only
// what the author left out (defect #24; plan P1-19: the answer lives in the
// resource table, never in the author's args) — a declared w (or h) stays,
// the other side follows the aspect ratio. (Plan P3-32: Measure's and
// Layout's, never Emit's.)
inline void intrinsicDims(double& w, double& h, double bw, double bh) {
  if (w > 0) h = w * bh / bw;
  else if (h > 0) w = h * bw / bh;
  else w = bw, h = bh;
}
// an image's display box: the scaled or intrinsic width, never wider than
// the measure, the height from the aspect ratio
inline void resolveImageSize(const IntrinsicSize& s, double measurePx, Su& w, Su& h) {
  double dw, dh;
  if (!s.placeholder()) {
    dw = s.scale > 0 ? s.scale * measurePx : s.w;
    if (dw > measurePx) dw = measurePx;
    if (dw < 1) dw = 1;
    dh = dw * s.h / s.w;
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
// handler-written passthrough markup (v2 §4.1): its size declared (w: its
// intrinsic width in a content-fitted column, plan P3-14) or measured by
// the host at the box's width (plan P3-28: kind svg or html)
struct RawData {
  StrRef html = 0;
  BoxKind kind = BoxKind::Html;
  IntrinsicSize size;
};
// figure-design.md §3: src 0 = placeholder (unsafe scheme or failed load —
// the box carries the alt text); its display box is layout's
struct ImageData {
  StrRef src = 0, alt = 0;
  IntrinsicSize size;
};
struct MathData {  // a display formula
  const MathBox* box = nullptr;  // laid out in Measure (plan P1-25), once its text runs are measured
  // (plan P3-29; D-S11) its rows when it has row breaks or `&`, or stands in
  // an equations block that aligns: each as wide as its group's columns
  // (box: the first); its cells, laid out, wait for its group
  std::vector<const MathBox*> rows;
  const MathRows* cells = nullptr;
  bool grouped = false;  // its group aligned it
  // (its equation number is the leaf's track, plan P3-26: FlowUnit::cells)
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
class BoxAsker;

// One Emit pass, top-level block by block (plan P1-20; design T9 M5): the
// shaping scratch is shared across the pass. A block whose display formula
// lacks text metrics is incomplete: top() returns false with what it lacked
// (the caller discards it and its diagnostics, and retries once measured).
class EmitPass {
 public:
  EmitPass(const BoxTree& bt, Arena& arena, Interner& strs, StyleTable& styles, const EmitSettings& cfg,
           DiagSink& diags, const MetricStore* metrics, const ResourceTable* rt, BoxAsker* boxes = nullptr);
  ~EmitPass();
  bool top(size_t t, TopBlock& out, std::vector<MeasureItem>& missing);

 private:
  struct State;
  const BoxTree& bt_;
  std::unique_ptr<State> st_;
};

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
  BoxAsker* boxes = nullptr;      // (plan P3-32) an inline image's intrinsic size (none: it stays pending)
};

// Fills widths from the store; returns what is still missing (deduped).
// With an ObjectEnv, deferred formulas are laid out and spliced once their
// text-font runs are measured (their misses join the request).
MeasureRequest resolveWidths(std::vector<TopBlock>& tops, MetricStore& store,
                             const StyleTable& styles, const EmitSettings& cfg,
                             ObjectEnv* objects = nullptr);

std::string dumpHLists(const std::vector<TopBlock>& tops, const Interner& strs,
                       const StyleTable& styles, BoxAsker* boxes = nullptr);
std::string dumpMathBoxes(const std::vector<TopBlock>& tops, const Interner& strs);
std::string dumpMathIRs(const std::vector<TopBlock>& tops, const Interner& strs, const MathEnv* math = nullptr);

}  // namespace tsr
