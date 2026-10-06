// Stage ids and rerun classes from stages.def (plan P1-03).
#pragma once
#include "../support/support.h"

namespace tsr {

enum class Stage : u8 {
#define STAGE(id, side, unit, rerun) id,
#include "stages.def"
#undef STAGE
};
enum class Rerun : u8 { Once, PidRetry, Resumable, Reentrant, Provisional };
// a stage a settings patch may re-enter in place (its product is pure over
// earlier ones and its answers)
constexpr bool reentrant(Rerun r) { return r == Rerun::Reentrant || r == Rerun::Provisional; }

constexpr Rerun kStageRerun[] = {
#define STAGE(id, side, unit, rerun) Rerun::rerun,
#include "stages.def"
#undef STAGE
};
constexpr const char* kStageName[] = {
#define STAGE(id, side, unit, rerun) #id,
#include "stages.def"
#undef STAGE
};
constexpr u32 kStageCount = sizeof kStageRerun / sizeof kStageRerun[0];
constexpr u32 stageBit(Stage s) { return 1u << (u32)s; }
inline Stage firstStage(u32 mask) {
  for (u32 k = 0; k < kStageCount; k++)
    if (mask & (1u << k)) return (Stage)k;
  return Stage::Paint;
}

}  // namespace tsr
