// rulesToCss (plan P3-01; design T4 CssEmitters, D-T07): the semantic page's
// stylesheet, compiled from the same rules the typeset view folds. The page
// writes each leaf's rule-free scope inline; what the rules add arrives here:
// - the base rules (the engine's defaults, the host's) as page rules;
// - each document env (the rules a mid-document $.set or a style.where adds)
//   under [data-tsr-env="<hash>"], which the page sets on the top-level
//   block or the style.where wrapper where that env begins (envAttr).
// Every rule is `:where(…)` (specificity 0: the order decides, as in the
// cascade); a host force rule is !important. A kind maps to the element the
// page writes for it; role and class selectors wait for the page's hooks
// (P3-18 tsr-c-*, P3-23 data-role) and are left out until then; a selector
// with no faithful CSS form (depth, other attributes) is left out with a
// rule-no-css warning.
#pragma once
#include <string>

#include "../model/cascade.h"

namespace tsr {

std::string rulesToCss(const Cascade& cascade, const ContentTree& tree, const Interner& strs,
                       DiagSink* diags = nullptr);
// the attribute value marking where `env` begins on the page ("": the base env)
std::string envAttr(const Cascade& cascade, RuleEnvId env, const Interner& strs);
// a node that starts an env of its own: a style.where (a styled node with
// match attributes)
bool startsEnv(const ContentNode* n);

}  // namespace tsr
