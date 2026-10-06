#pragma once
#include <map>

#include "../shape/textrules.h"
#include "../support/support.h"
#include "settings_views.gen.h"

namespace tsr {

// CostParams, PunctCompress and the stages' views of Config are generated
// from the schema's "settings" rows (plan P1-03): settings_views.gen.h.
// Config itself (settings.gen.h) is the host's: no stage sees it (plan P3-32).

// App C constants (em): the CJK–Latin boundary glue and a superscript's
// raise — the rules' constants (engine/rules/locale/compat.def, plan P1-11);
// punctuation blanks are the rules' per-class table (kBlanks, plan P4-04)
constexpr double kCjkBoundaryEm = kRule_cjkBoundaryEm;
constexpr double kSuperRaiseEm = kRule_superRaiseEm;
// (Table pads are settings since plan P3-02: table.cellPad, table.rowPad;
// heading sizes are default rules since P3-01: engine/data/defaults.json.)

// (Supplement words are locale terms since plan P1-10: semantic/terms.h,
// engine/data/locale; doc.lang is the host default document language.)

}  // namespace tsr
