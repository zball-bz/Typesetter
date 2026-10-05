// Normal form of the content tree (plan P0-07; design T2 S3). Moved out of the
// resolver's rewrite pass (resolve.cc) so it runs once, right after
// instantiation, and is driven by the schema's level classes instead of a
// hand-kept list of inline kinds.
#include "model.h"

namespace tsr {

namespace {
bool emptyPara(const ContentNode* p, const Interner& strs) {
  if (p->kind != Kind::para) return false;
  for (const ContentNode* k : p->kids)
    if (k->kind != Kind::text || !strs.get(k->str).empty()) return false;
  return true;
}

// a seq holding a block: a sequence of blocks (a multi-block content body)
bool blockSeq(const ContentNode* n) {
  if (n->kind != Kind::seq) return false;
  for (const ContentNode* k : n->kids)
    if (!isInlineLevel(k->kind)) return true;
  return false;
}
void spliceBlocks(ContentNode* seq, std::vector<ContentNode*>& out) {
  for (ContentNode* k : seq->kids) {
    if (blockSeq(k)) spliceBlocks(k, out);
    else out.push_back(k);
  }
}
}  // namespace

void normalize(ContentNode* n, const Interner& strs) {
  std::vector<ContentNode*> work{n};
  while (!work.empty()) {
    ContentNode* cur = work.back();
    work.pop_back();
    std::vector<ContentNode*> kids;
    kids.reserve(cur->kids.size());
    for (ContentNode* k : cur->kids) {
      if (emptyPara(k, strs)) continue;  // N1
      // N3: a sequence of blocks takes its place among its siblings; a
      // paragraph that is only such a sequence is those blocks
      if (blockSeq(k)) {
        spliceBlocks(k, kids);
        continue;
      }
      if (k->kind == Kind::para && k->kids.size() == 1 && blockSeq(k->kids[0])) {
        spliceBlocks(k->kids[0], kids);
        continue;
      }
      if (k->kind == Kind::para && k->kids.size() == 1 &&
          !isInlineLevel(k->kids[0]->kind) && k->kids[0]->kind != Kind::para)
        k = k->kids[0];  // N2
      kids.push_back(k);
    }
    cur->kids = std::move(kids);
    for (ContentNode* k : cur->kids) work.push_back(k);
  }
}

}  // namespace tsr
