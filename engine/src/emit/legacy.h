// MIGRATION ONLY (plan P1-12; deleted with the paragraph shaper, P4-02): the
// pre-HList emitter as the oracle of fuseLegacy. Linked by the native tools
// only (the golden runner, tsrc --fuse-check); the WASM build never
// references it.
#pragma once
#include "emit.h"
#include "../boxtree/build.h"

namespace tsr {

std::vector<TopBlock> emitDocLegacy(const BoxTree& bt, Arena& arena, Interner& strs,
                                    StyleTable& styles, const Config& cfg, DiagSink& diags,
                                    const MathTextCtx* mathText);
MeasureRequest resolveWidthsLegacy(std::vector<TopBlock>& tops, MetricStore& store,
                                   const StyleTable& styles, const Config& cfg);
// "" when fuseLegacy of every unit's and cell's HList equals, field by field,
// the blocks the legacy emitter produces for the same document and metrics
std::string fuseCheck(const std::vector<TopBlock>& tops, const BoxTree& bt, Arena& arena,
                      Interner& strs, StyleTable& styles, const Config& cfg, MetricStore& metrics,
                      double baseSizePx);

}  // namespace tsr
