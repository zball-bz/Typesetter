// The in-page audits' thresholds (plan P3-06; design T7 devAudit): one module
// shared by the runtime's devAudit behaviour and the Playwright assertions
// (both run runtime/src/main/audit.mjs), so a tolerance changes in one place.
import { SETTINGS } from './settings.gen.mjs';

export const AUDIT = Object.freeze({
  // line-integrity: a fragment whose top reaches another's bottom (minus
  // this) is a browser re-break
  bandSlackPx: 0.5,
  // right-edge: a justified line ends within this of the measure
  rightEdgePx: 1,
  // overflow: nothing escapes the paragraph box by more than this
  overflowPx: 1,
  // compression: a line shrinks at most to the breaker's limit — this share
  // of its spaces (the engine's cost.shrinkThreshold) — plus this slack, and
  // never trips above the old absolute floor
  shrinkShare: SETTINGS['cost.shrinkThreshold'].def,
  shrinkSlackPx: 0.5,
  wordSpacingFloorPx: -2.5,
  // (plan P3-19; design T7 S11) baseline: a line's baseline lies within this
  // of the engine's model (top + tallest ascent; a code row centred)
  baselinePx: 1,
});
