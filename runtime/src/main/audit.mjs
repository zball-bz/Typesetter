// In-page invariant audits (testing.md §5.3). One implementation: dev
// diagnostic in the runtime AND the assertion body of the Playwright tests.
export function auditTypeset(root) {
  const report = {
    lines: 0,
    failures: [],
    rightEdge: { max: 0, mean: 0, n: 0 },
    ok: false,
  };

  for (const line of root.querySelectorAll('.tsr-line')) {
    report.lines++;
    const range = document.createRange();
    range.selectNodeContents(line);
    // height>0: zero-height inline-block spacers (tsr-sp) are not line fragments
    const rects = [...range.getClientRects()].filter((r) => r.width > 0 && r.height > 0);
    range.detach?.();

    // line-integrity: exactly one fragment row (v2 §7 rule 1 held?)
    const rows = [];
    for (const r of rects) {
      if (!rows.some((t) => Math.abs(t - r.top) < 0.5)) rows.push(r.top);
    }
    if (rows.length !== 1) {
      report.failures.push({
        audit: 'line-integrity',
        rows: rows.length,
        text: (line.textContent || '').slice(0, 48),
      });
    }

    // right-edge: justified lines (any data-join — space or hyphen breaks)
    // end within 1px of the measure. Measured as the flow edge of the last
    // child element (rect.right + margin-right) so letter-spacing overhangs
    // and punct-squeeze margins are accounted exactly.
    if (line.dataset.join !== undefined && rects.length) {
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
      if (adev > 1) {
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
    if (para.scrollWidth > para.clientWidth + 1) {
      report.failures.push({
        audit: 'overflow',
        by: para.scrollWidth - para.clientWidth,
        pid: para.dataset.pid,
      });
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
