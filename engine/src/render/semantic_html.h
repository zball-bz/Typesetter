// Semantic flow serializer (v2 §9, document-model §9.2): the content tree
// rendered as ordinary flow HTML that the browser breaks itself. One
// component, four uses: first paint (progressive upgrade base), SEO/no-JS
// readers, and static export. Paragraph-level elements carry data-pid (the
// upgrade swap key), data-s/e, and label anchors ("tsr-<label>") — the
// resolver ran first, so numbers and refs are already final (§11.1).
#pragma once
#include "../model/model.h"

namespace tsr {

std::string renderSemantic(const ContentTree& tree, const Interner& strs,
                           const StyleTable& styles);

}  // namespace tsr
