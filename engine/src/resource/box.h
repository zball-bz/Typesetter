// A replaced box's kind (resources.def boxInfo, column kind; plan P3-28):
// what a box need asks for and who can answer it — an image's intrinsic
// size (the image provider), an svg box (its viewBox: the engine's own
// answerer; else the host) or an html box (the host, measureHtml).
#pragma once
#include "../support/support.h"

namespace tsr {

enum class BoxKind : u8 { Image, Svg, Html };

}  // namespace tsr
