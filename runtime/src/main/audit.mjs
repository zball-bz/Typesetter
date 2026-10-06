// In-page invariant audits (testing.md §5.3). One implementation: dev
// diagnostic in the runtime (the devAudit behaviour) AND the assertion body
// of the Playwright tests; the thresholds are shared/audit-constants.mjs.
import { AUDIT } from '../shared/audit-constants.mjs';

// the advance of one space in a CSS font shorthand (canvas: same shaping
// as the DOM for a lone space)
const spaceCache = new Map();
function spaceAdvance(font) {
  if (!font) return 0;
  let w = spaceCache.get(font);
  if (w === undefined) {
    const ctx = document.createElement('canvas').getContext('2d');
    ctx.font = font;
    w = ctx.measureText(' ').width;
    spaceCache.set(font, w);
  }
  return w;
}

export function auditTypeset(root) {
  const report = {
    lines: 0,
    failures: [],
    rightEdge: { max: 0, mean: 0, n: 0 },
    ok: false,
  };

  for (const line of root.querySelectorAll('.tsr-line')) {
    report.lines++;
    // Collect text rects from in-flow children only: absolutely positioned
    // gutter markers sit outside the line box and are not line fragments
    // (their rect top differs from the text's on mixed-script lines), and
    // zero-height inline-block spacers (tsr-sp) are filtered by height.
    const rects = [];
    for (const el of line.children) {
      if (getComputedStyle(el).position === 'absolute') continue;
      if (el.dataset.syn === 'math') {  // one engine-defined fragment
        const r = el.getBoundingClientRect();
        if (r.width > 0 && r.height > 0) rects.push(r);
        continue;
      }
      const range = document.createRange();
      range.selectNodeContents(el);
      for (const r of range.getClientRects()) {
        if (r.width > 0 && r.height > 0) rects.push(r);
      }
      range.detach?.();
    }

    // line-integrity: all fragments share one vertical band (v2 §7 rule 1).
    // Mixed fonts on a line legitimately differ in rect top/height (baseline
    // aligned, ascents differ) — a real browser re-break stacks fragments,
    // i.e. some fragment's top clears another's bottom.
    if (rects.length) {
      const maxTop = Math.max(...rects.map((r) => r.top));
      const minBottom = Math.min(...rects.map((r) => r.bottom));
      if (maxTop >= minBottom - AUDIT.bandSlackPx) {
        report.failures.push({
          audit: 'line-integrity',
          text: (line.textContent || '').slice(0, 48),
        });
      }
    }

    // right-edge: justified lines (any data-join — space or hyphen breaks)
    // end within 1px of the measure. Measured as the flow edge of the last
    // child element (rect.right + margin-right) so letter-spacing overhangs
    // and punct-squeeze margins are accounted exactly.
    // (a data-overfull line holds a run wider than the measure — set at the
    // shrink limit and reported as overfull-line, plan P0-12 — not a defect)
    if (line.dataset.join !== undefined && line.dataset.ragged === undefined &&
        line.dataset.overfull === undefined && rects.length) {
      const lineRect = line.getBoundingClientRect();
      let contentRight = -Infinity;
      for (const el of line.children) {
        const r = el.getBoundingClientRect();
        if (r.width === 0 && r.height === 0) continue;
        const mr = parseFloat(getComputedStyle(el).marginRight) || 0;
        contentRight = Math.max(contentRight, r.right + mr);
      }
      if (contentRight === -Infinity) contentRight = Math.max(...rects.map((r) => r.right));
      const dev = lineRect.right - contentRight; // >0 short, <0 overflow
      const adev = Math.abs(dev);
      report.rightEdge.n++;
      report.rightEdge.mean += adev;
      if (adev > report.rightEdge.max) report.rightEdge.max = adev;
      if (adev > AUDIT.rightEdgePx) {
        report.failures.push({
          audit: 'right-edge',
          dev: Math.round(dev * 1000) / 1000,
          text: (line.textContent || '').slice(0, 48),
        });
      }
    }
  }

  // overflow: nothing escapes the paragraph box horizontally
  for (const para of root.querySelectorAll('.tsr-para')) {
    if (para.scrollWidth > para.clientWidth + AUDIT.overflowPx && !para.querySelector('[data-overfull]')) {
      report.failures.push({
        audit: 'overflow',
        by: para.scrollWidth - para.clientWidth,
        pid: para.dataset.pid,
      });
    }
    // line stacking: tops strictly increase — a failed break plan collapses
    // a whole paragraph onto one line (the audit blind spot behind the
    // overprinted-specimen bug)
    let prevTop = -Infinity;
    for (const l of para.querySelectorAll('.tsr-line')) {
      if (l.dataset.cell !== undefined) continue;  // table cells share row tops
      const top = parseFloat(l.style.top);
      if (!(top > prevTop || prevTop === -Infinity)) {
        report.failures.push({ audit: 'line-stacking', pid: para.dataset.pid, top });
        break;
      }
      prevTop = top;
    }
    // and no absurd compression: a line shrinks at most to the breaker's
    // limit (shrinkThreshold 0.37 of its spaces, plan P0-12); beyond that an
    // infeasible line was force-fitted. The bound is relative to the line's
    // space advance (a monospace space is twice a serif one), never stricter
    // than the old absolute -2.5px.
    for (const l of para.querySelectorAll('.tsr-line')) {
      const ws = parseFloat(l.style.wordSpacing || '0');
      if (!(ws < AUDIT.wordSpacingFloorPx)) continue;
      const first = l.querySelector('span');
      const limit = -AUDIT.shrinkShare * spaceAdvance(first ? getComputedStyle(first).font : '') - AUDIT.shrinkSlackPx;
      if (ws < Math.min(AUDIT.wordSpacingFloorPx, limit)) {
        report.failures.push({ audit: 'compression', pid: para.dataset.pid, ws });
        break;
      }
    }
  }

  // anchors: pids unique, line spans inside their paragraph
  const pids = [...root.querySelectorAll('[data-pid]')].map((e) => e.dataset.pid);
  if (new Set(pids).size !== pids.length)
    report.failures.push({ audit: 'anchors', msg: 'duplicate data-pid' });

  if (report.rightEdge.n) report.rightEdge.mean /= report.rightEdge.n;
  report.ok = report.failures.length === 0;
  return report;
}
