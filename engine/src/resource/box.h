// A replaced box's kind (resources.def boxInfo, column kind; plan P3-28):
// what a box need asks for and who can answer it — an image's intrinsic
// size (the image provider), an svg box (its viewBox: the engine's own
// answerer; else the host) or an html box (the host, measureHtml).
#pragma once
#include <string_view>

#include "../support/support.h"

namespace tsr {

enum class BoxKind : u8 { Image, Svg, Html };

// the kind of a raw box's markup: an <svg> root is svg, anything else html
BoxKind boxKindOf(std::string_view markup);

// The engine's own svg answer (design T9 M11): an <svg> root whose size
// its attributes fix at a width — a px height, or a viewBox scaled to its
// width (a px or % width attribute, else the box's) — sitting on its
// bottom (baseline = height). false: the host measures it.
bool svgBoxPx(std::string_view markup, double widthPx, double& h, double& baseline);

// A stage's way to a host box's size (plan P3-28; design T6 S14): the
// answer at a width, or not ready — a need still pending (filed: the
// stage's run is then provisional, the pull goes on) or one that failed
// (the caller's fallback; the failure was reported). Doc implements it per
// run. An image's (plan P3-32) is its intrinsic size, asked at width 0.
struct BoxAnswer {
  bool ready = false;
  bool pending = false;  // not ready and not failed: its answer is still to come
  double w = 0, h = 0, baseline = 0;  // px (w: an image's intrinsic width); baseline from the top
};
class BoxAsker {
 public:
  virtual BoxAnswer ask(BoxKind kind, StrRef ref, double widthPx, Span span) = 0;

 protected:
  ~BoxAsker() = default;
};

}  // namespace tsr
