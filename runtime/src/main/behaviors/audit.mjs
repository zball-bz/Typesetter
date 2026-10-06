// devAudit (plan P3-06; design T7 "Shell core + Behavior registry"): the
// in-page invariant audits (../audit.mjs, thresholds in
// shared/audit-constants.mjs) run on the view after the first commit and
// after every later one. Opt-in: createEngine({ behaviors:
// [...defaultBehaviors(), devAudit({ onReport })] }); without onReport a
// failing report is a console warning.
import { auditTypeset } from '../audit.mjs';

export function devAudit({ onReport } = {}) {
  const report = onReport ?? ((r) => { if (!r.ok) console.warn('tsr devAudit:', r.failures); });
  return {
    name: 'devAudit',
    install(ctx) {
      const run = () => { const root = ctx.root(); if (root) report(auditTypeset(root)); };
      run();
      ctx.onCommit(run);
      return () => {};
    },
  };
}
