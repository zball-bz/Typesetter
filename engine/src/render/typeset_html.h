#pragma once
#include "../layout/layout.h"

namespace tsr {

// Typeset serializer (document-model §9.1).
std::string renderTypeset(const std::vector<TopBlock>& tops, const LayoutResult& lr,
                          const StyleTable& styles, const Interner& strs,
                          const Config& cfg);

}  // namespace tsr
