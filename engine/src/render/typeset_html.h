// The typeset backends' writer (document-model §9.1, pages-design.md §2;
// plan P1-18): a stateless walk of the DisplayList (paint/displaylist.h).
// It reads no Config, no FlowUnit and no content tree (architecture lint).
#pragma once
#include "../paint/displaylist.h"

namespace tsr {

// the document root (.tsr-doc, .tsr-doc.tsr-paged)
void writeRoot(std::string& out, const char* cls, const DLRoot& root);
// a flowing block: its .tsr-para container and its nodes, sources relative
// to the block
void writeBlock(std::string& out, const DLBlock& b, const StyleTable& styles, const Interner& strs, double basePx);
// nodes [lo, hi) of a block rebased by yShift (a paged band), sources absolute
void writeNodes(std::string& out, const DLBlock& b, u32 lo, u32 hi, Su yShift, const StyleTable& styles,
                const Interner& strs, double basePx);

}  // namespace tsr
