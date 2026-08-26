#pragma once
#include "../layout/layout.h"

namespace tsr {

// Typeset serializer (document-model §9.1).
std::string renderTypeset(const std::vector<TopBlock>& tops, const LayoutResult& lr,
                          const StyleTable& styles, const Interner& strs,
                          const Config& cfg);

// Paged serializer (pages-design.md §2): cuts the finished layout into
// fixed-height sheets with keep-rules; print CSS breaks after each sheet.
std::string renderPages(const std::vector<TopBlock>& tops, const LayoutResult& lr,
                        const StyleTable& styles, const Interner& strs,
                        const Config& cfg, double pageHeightPx);

}  // namespace tsr
