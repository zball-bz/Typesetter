// print (plan P3-06; design T7 "Shell core + Behavior registry"):
// print-to-PDF is the browser's print engine over the engine's paged
// layout. The sheets come from ops.paginate — a fork of the document at the
// page measure — under an id prefix of their own, so they never share an id
// with the live view; they are injected into the PARENT document under a
// print root that @media print shows alone. (A hidden-iframe approach
// printed blank pages in some browsers — focus and removal races; the parent
// already has every font loaded.) The shell's nodes carry data-tsr-print and
// are held by reference: no ids. The sheet size is still the A4 literal and
// page.height until PageSpec (plan P3-12).

const A4W = 793.7, A4H = 1122.5;  // css px

// a prefix no live id starts with and that starts no live id: the two id
// sets cannot meet, whatever the labels
export function derivedPrefix(prefix) {
  for (const c of 'pqrstuvwxyz') {
    const q = `tsr${c}-`;
    if (!q.startsWith(prefix) && !prefix.startsWith(q)) return q;
  }
  return `${prefix}print-`;
}

export function print({ pageWidthPx: defWidth = 666, marginPx: defMargin = 64 } = {}) {
  return {
    name: 'print',
    install(ctx) {
      let active = null;  // { root, style } while a print runs
      const cleanup = () => {
        active?.root.remove();
        active?.style.remove();
        active = null;
      };
      ctx.expose('print', async ({ pageWidthPx = defWidth, pageHeightPx = ctx.setting('page.height'),
                                   marginPx = defMargin } = {}) => {
        const { html } = await ctx.ops.paginate({ pageWidthPx, pageHeightPx,
                                                  idPrefix: derivedPrefix(ctx.setting('render.idPrefix')) });
        cleanup();
        const style = document.createElement('style');
        style.dataset.tsrPrint = 'style';
        // Gecko sizes A4 at FRACTIONAL css px (793.70 × 1122.52) and
        // fragments with zero overflow tolerance: a 995px sheet inside a
        // 994.52px page content box splits into content + clipped-blank
        // page — every page doubles. (Chromium tolerates the sub-pixel
        // overflow, which is why it hid there.) Clamp the margins so the
        // content box clears the sheets with ≥3px slack on both axes, and
        // never force a break after the LAST sheet — Gecko honors that
        // literally too, as a trailing blank page.
        const mx = Math.max(0, Math.min(marginPx, Math.floor((A4W - pageWidthPx - 3) / 2)));
        const my = Math.max(0, Math.min(marginPx, Math.floor((A4H - pageHeightPx - 3) / 2)));
        style.textContent =
          `@page { size: A4; margin: ${my}px ${mx}px }` +
          `[data-tsr-print="root"] { display: none; }` +
          `@media print {` +
          ` body { margin: 0 !important; }` +
          ` body > :not([data-tsr-print="root"]) { display: none !important; }` +
          ` [data-tsr-print="root"] { display: block !important; }` +
          ` .tsr-sheet { break-after: page; page-break-after: always; }` +
          ` .tsr-sheet:last-child { break-after: auto; page-break-after: auto; }` +
          `}`;
        document.head.appendChild(style);
        const root = document.createElement('div');
        root.dataset.tsrPrint = 'root';
        // the measure/render contract, as on the live container
        ctx.applyContract(root);
        root.innerHTML = html;
        document.body.appendChild(root);
        active = { root, style };
        try { await document.fonts.ready; } catch { /* print what settled */ }
        const done = new Promise((r) => window.addEventListener('afterprint', r, { once: true }));
        window.print();
        await Promise.race([done, new Promise((r) => setTimeout(r, 120000))]);
        if (active?.root === root) cleanup();
        return { html };
      });
      return cleanup;
    },
  };
}
