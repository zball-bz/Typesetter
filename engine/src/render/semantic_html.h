// Semantic flow serializer (v2 §9, document-model §9.2): the content tree
// rendered as ordinary flow HTML that the browser breaks itself. One
// component, four uses: first paint (progressive upgrade base), SEO/no-JS
// readers, and static export. Paragraph-level elements carry data-pid (the
// upgrade swap key), data-s/e, and label anchors ("tsr-<label>") — the
// resolver ran first, so numbers and refs are already final (§11.1).
#pragma once
#include "../model/model.h"

namespace tsr {

// rt: answered code tokens render as their styled runs (plan P1-19)
class ResourceTable;
class Registry;
class Cascade;
// reg: the element registry, for each class's semantic element (plan P2-05);
// cascade: where a document env begins, the page marks it (data-tsr-env:
// rulesToCss, plan P3-01)
std::string renderSemantic(const ContentTree& tree, Interner& strs, StyleTable& styles,
                           const ResourceTable* rt = nullptr, const Registry* reg = nullptr,
                           const Cascade* cascade = nullptr);

}  // namespace tsr
