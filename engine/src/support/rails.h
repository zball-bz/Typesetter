// Safety rails (plan P3-02; finding api-measure-code/magic-policy-constants):
// named bounds that keep hostile or degenerate input bounded. They are not
// policy: what a user may tune is a setting (engine/schema/schema.json
// "settings"), what typography decides is a rule (engine/rules).
#pragma once
#include "support.h"

namespace tsr {

// lines one highlight range ("3-7") expands to, and the largest number a
// range set reads
constexpr u32 kRailRangeLines = 10000;
constexpr u64 kRailRangeNumber = 100000000;
// the narrowest measure a line, a code column or a table cell keeps (1px)
constexpr i32 kRailMinLineSu = 64;
// Stern–Brocot steps of a code grid solve (each step is one mediant)
constexpr int kRailGridSteps = 64;

}  // namespace tsr
