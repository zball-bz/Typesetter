// The horizontal item list (plan P1-12; design T5 "HList"): what emit
// produces for every inline stream (a paragraph, a heading, a table cell, a
// caption row, a sidecar line). Each item says what it is — a Box, a Glue of
// a class, a Penalty, a Disc — and each run instance says how its boxes are
// painted, so no later layer re-derives script semantics from flag bits.
//
// Break legality is TeX's (normative):
// - a Glue is a legal breakpoint iff the item just before it is a Box or a
//   Disc; a break there drops it and every following Glue/Penalty up to the
//   next Box/Disc;
// - a Penalty p is a legal breakpoint iff p < kPenInf;
// - a Disc is a legal breakpoint (its pre box ends the line; unbroken it is
//   its own width);
// - a Box is never a breakpoint: adjacent Boxes are unbreakable.
// A boundary (the items between two Boxes/Discs) holds at most one legal
// breakpoint; lintHList checks it on every golden.
//
// The canonical TeX form (plan P4-08: the breaker reads it, break/items.h):
// a box that may break after it carries a Penalty right after it (none when
// an InterChar glue follows: that glue is the break); a glue whose own
// penalty is not 0 carries it right before it. A glue's stretch and shrink
// are its weight `x` times the list's justification unit `juSu` (v2 §8):
// the breaker's capacity and the renderer's distribution are one rule.
#pragma once
#include <string>
#include <vector>

#include "../model/style.h"

namespace tsr {

struct ContentNode;

struct MathBox;

constexpr float kPenInf = 1e18f;  // a penalty this large forbids the break

enum class IK : u8 { Box, Glue, Penalty, Disc };
// glue classes: a typed space or soft break; the gap between two CJK
// characters (letter-spacing); the script-boundary space (CJK–Latin, CJK–
// formula); a punctuation blank; the space between the parts of an object
// Fill (plan P2-16): fil glue — no width, no finite stretch; it takes its
// line's slack (a fill kind: a site's ∎ pushed to the measure's end)
enum class GC : u8 { Word, InterChar, Autospace, Blank, ObjectSpace, Fill };
enum : u8 {
  IA_SourceSpace = 1,  // a typed space or soft break: copy reads ' '
  IA_Anchor = 2,       // the first item of an anchored scope (ColdRec::anchor)
  IA_OwnedByNext = 4,  // a Blank that belongs to the following glyph
  IA_Displaced = 8,    // a Blank moved by attach: painted as a spacer
};
enum : u8 { IS_Resolved = 1 };  // HItem::st: the width is known

struct HItem {  // the 24-byte hot record (design I9)
  IK k = IK::Box;
  u8 cls = 0;    // Glue: GC; Box: the CC of its first codepoint (0 for objects, spacers)
  u8 attrs = 0;  // IA_*
  u8 st = 0;     // IS_*
  u32 run = 0;   // run instance (HList::runs): changes exactly where paint opens a run
  u32 aux = 0;   // Box/Glue: AdvanceSpec index (InterChar: none); Disc: DiscRec index
  Su w = 0;      // natural width (Disc: unbroken), filled by resolveWidths
  float x = 0;   // Glue: stretch weight; Penalty/Disc: penalty; Box: the weight of
                 //   its InterChar gap (LetterSpaced/Pinned: finish gives it to the glue)
  u32 cold = 0;  // ColdRec index (a Penalty and an InterChar glue share their owner's)
};
static_assert(sizeof(HItem) == 24);

struct ColdRec {
  u32 srcStart = 0, srcEnd = 0;  // source byte span (today: the text node's)
  double rawPx = 0;              // unquantized advance (Disc: the junction kern;
                                 //   an InterChar glue has none: it shares its box's record)
  float blankLpx = 0, blankRpx = 0;  // a punctuation glyph's resolved blanks
  StrRef anchor = 0; // IA_Anchor: the anchor name (id="tsr-<anchor>")
};
static_assert(sizeof(ColdRec) == 32);

struct AdvanceSpec {
  // Measured: m(str); Defined: em of the face (a pinned dash/ellipsis);
  // Fixed: em of the face, synthetic (indent, autospace, blanks);
  // MeasuredMinusBlanks: m(str) less the glyph's blank; KernCtx: m(str)
  // corrected by the context, m(tri) - m(prev) - m(next); Object: a part of
  // HList::parts (its box, or the glue before it; a deferred object's one
  // placeholder part has no extents until resolveWidths splices the parts)
  enum K : u8 { Measured, Defined, Fixed, MeasuredMinusBlanks, KernCtx, Object };
  double em = 0;
  StrRef str = 0;  // the codepoints painted (and copied)
  StrRef prev = 0, next = 0, tri = 0;  // KernCtx: tri = prev + str + next
  u32 obj = 0;  // Object: the HList::parts index
  K k = Measured;
};
static_assert(sizeof(AdvanceSpec) == 32);

struct DiscRec {
  u32 pre = 0, preN = 0;    // HList::side: the boxes that end the line when broken
  u32 post = 0, postN = 0;  // HList::side: the boxes that start the next line
  u32 spec = ~0u;           // the junction's KernCtx (the unbroken width), ~0u = none
};

enum class RealizeClass : u8 { Plain, LetterSpaced, BlankBearing, Pinned, Rigid, Object };
// what a run is for copy and paint (the vocabulary is T7's): authored
// content, resolver-generated reference text, a paragraph indent
enum class SynKind : u8 { Content, Ref, Indent };
// (plan P3-07; design T7 CopyPolicy, D-R01) what copy takes of a run: its
// text, nothing (data-syn=<kind>), or a replacement (data-copy, once per
// group). Decided at emit from the nodes' `copy` and `syn` attributes — the
// built-in templates mark the decorative generated text (a footnote
// marker, a backlink), errors are omitted —, never by the code path that
// made the run; references and citations copy as text.
enum class CopyMode : u8 { Text, Omit, Replace };

// (plan P3-04; design T7 AnchorNamer) where a link run points: an
// internal target is a label — its anchor, which the serializer spells —
// an external one the URL as written
struct LinkTarget {
  StrRef ref = 0;
  bool anchor = false;
  StrRef doc = 0;  // (plan P3-31) an anchor in this other document of the project (its key)
  explicit operator bool() const { return ref != 0; }
  bool operator==(const LinkTarget&) const = default;
};

struct RunRec {
  StyleId face = 0;  // style (paint projection)
  LinkTarget link;
  SynKind syn = SynKind::Content;
  CopyMode copy = CopyMode::Text;
  StrRef synName = 0;   // its data-syn when not Text
  StrRef copyText = 0;  // Replace: what copy takes instead
  u32 copyGroup = 0;    // Replace: the node it replaces (taken once), per unit
  RealizeClass rc = RealizeClass::Plain;
  StrRef anchor = 0;    // the anchor of the run's first item
  StrRef error = 0;     // (plan P3-16, document-model §9.1) an error's text: its message (tsr-err, title)
};

// Inline objects (plan P1-13; shape/objects.h is the registry): every atomic
// inline thing — a formula, an image, raw markup, the error box of a kind
// that cannot appear inline — enters the list the same way: Box parts with
// AdvanceSpec::Object{part}, ObjectSpace glue between parts, per-part
// extents. Layout, paint and copy never test for a kind; they ask the part.
enum class ObjKind : u8 { Math, Image, Raw, Error };
struct InlineObject {
  ObjKind kind = ObjKind::Math;
  u8 firstCC = 0, lastCC = 0;  // edge classes (CC): what pair rules see at its edges
  bool deferred = false;       // structure still needs metrics (math until T8)
  const ContentNode* node = nullptr;
  StyleId style = 0;
  StrRef src = 0;   // math: its source as written (copy); image: src; raw: markup
  StrRef formula = 0;  // math (plan P2-15): its source as the lexer reads it (holes as units)
  u32 epoch = 0;       // math: its declaration epoch (what its names bind to)
  StrRef alt = 0;   // image: alt text
  u32 part0 = 0, nParts = 0;  // HList::parts
};
struct ObjPart {
  u32 obj = 0;
  const MathBox* math = nullptr;  // a formula segment
  Su w = 0, asc = 0, desc = 0;    // extents (above / below the baseline)
  Su glueBefore = 0;              // the ObjectSpace glue before it (part > 0)
};

struct HList {
  std::vector<HItem> items, side;
  std::vector<ColdRec> cold;
  std::vector<AdvanceSpec> specs;
  std::vector<DiscRec> discs;
  std::vector<RunRec> runs;
  std::vector<InlineObject> objs;
  std::vector<ObjPart> parts;
  bool hasDeferred = false;  // an object waits for metrics (resolveWidths splices it)
  // (plan P4-08; v2 §8, D-X01) the justification unit: a space of the
  // stream's base style (its paragraph's, its cell's), measured — every
  // glue's stretch and shrink is its weight times it
  StyleId juStyle = 0;
  StrRef juStr = 0;  // " " (emit's), what is measured
  Su juSu = 0;
  double juPx = 0;
  bool empty() const { return items.empty(); }
};

inline bool isCarrier(const HItem& it) { return it.k != IK::Penalty; }
inline bool isBoxOrDisc(const HItem& it) { return it.k == IK::Box || it.k == IK::Disc; }
// TeX legality at item i
inline bool isBreakpoint(const std::vector<HItem>& v, size_t i) {
  const HItem& it = v[i];
  switch (it.k) {
    case IK::Box: return false;
    case IK::Glue: return i > 0 && isBoxOrDisc(v[i - 1]);
    case IK::Penalty: return it.x < kPenInf;
    case IK::Disc: return true;
  }
  return false;
}
inline bool isLetterSpacedBox(const HList& h, const HItem& it) {
  return it.k == IK::Box && h.runs[it.run].rc == RealizeClass::LetterSpaced;
}

// tsrc --stage=hlist: kind, class, attrs, w, weight, numeric penalty, run,
// source span per item; the justification unit; the run table per list
void dumpHList(std::string& out, const HList& h, const Interner& strs, const StyleTable& styles,
               const char* indent);
// the legality lint: at most one legal breakpoint per boundary; none after an
// opening or before a closing punctuation glyph (kinsoku); runs contiguous,
// singleton runs alone, every non-final box of a LetterSpaced run followed by
// InterChar glue. Returns one line per violation.
std::string lintHList(const HList& h);

}  // namespace tsr
