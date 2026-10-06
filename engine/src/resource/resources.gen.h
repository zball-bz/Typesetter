// GENERATED from engine/src/resource/resources.def by tools/gen-res.mjs — do not edit.
// The resource kinds and their wire columns (docs/host-protocol-design.md §5).
#pragma once
#include "../support/support.h"

namespace tsr {

constexpr u32 RES_VERSION = 2;

enum class ResKind : u16 {
  textWidth = 1,
  fontVmet = 2,
  fontFace = 3,
  codeTokens = 4,
  boxInfo = 5,
  hyphPatterns = 6,
};
constexpr u32 kResKindCount = 6;

enum class ColType : u8 { Str, MetricKey, U8, U16, U32, F64, U32List };
struct ResCol {
  const char* name;
  ColType type;
};
enum class ResCache : u8 { Content, Host, None };
struct ResKindInfo {
  ResKind kind;
  const char* name;
  ResCache cache;
  bool docProviders;
  u8 nKey, nAns;
  ResCol key[4], ans[6];
};
inline constexpr ResKindInfo kResKinds[] = {
    {ResKind::textWidth, "textWidth", ResCache::Content, false, 2, 1, {{"mk", ColType::MetricKey}, {"text", ColType::Str}}, {{"px", ColType::F64}}},
    {ResKind::fontVmet, "fontVmet", ResCache::Content, false, 1, 2, {{"mk", ColType::MetricKey}}, {{"asc", ColType::F64}, {"desc", ColType::F64}}},
    {ResKind::fontFace, "fontFace", ResCache::None, false, 4, 1, {{"family", ColType::Str}, {"src", ColType::Str}, {"weight", ColType::U16}, {"style", ColType::U8}}, {{"status", ColType::U8}}},
    {ResKind::codeTokens, "codeTokens", ResCache::Content, true, 2, 1, {{"lang", ColType::Str}, {"text", ColType::Str}}, {{"runs", ColType::U32List}}},
    {ResKind::boxInfo, "boxInfo", ResCache::Host, true, 3, 3, {{"kind", ColType::U8}, {"ref", ColType::Str}, {"availPx", ColType::F64}}, {{"w", ColType::F64}, {"h", ColType::F64}, {"baseline", ColType::F64}}},
    {ResKind::hyphPatterns, "hyphPatterns", ResCache::Content, false, 1, 5, {{"lang", ColType::Str}}, {{"patterns", ColType::Str}, {"exceptions", ColType::Str}, {"leftmin", ColType::U8}, {"rightmin", ColType::U8}, {"hyphenChar", ColType::Str}}},
};
inline const ResKindInfo* resKindInfo(u16 id) {
  for (const ResKindInfo& k : kResKinds)
    if ((u16)k.kind == id) return &k;
  return nullptr;
}

}  // namespace tsr
