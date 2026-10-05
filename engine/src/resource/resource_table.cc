#include "resource_table.h"

namespace tsr {

double failedWidthPx(std::string_view text, double sizePx) {
  double em = 0;
  u32 i = 0;
  while (i < text.size()) {
    const u32 cp = utf8Next(text, i);
    if (cp == 0x2E3A) em += 2;       // ⸺ two-em dash
    else if (cp == 0x2E3B) em += 3;  // ⸻ three-em dash
    else if (cp == 0xFDFD) em += 4;  // ﷽
    else if ((cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF)) em += 1.3;
    else em += 1;
  }
  return 1.2 * em * sizePx;
}

}  // namespace tsr
