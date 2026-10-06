// refPreview (plan P3-06; design T7 "Shell core + Behavior registry"):
// hovering or focusing a reference shows what it names — a footnote's body —
// in a popup. Which references preview is data: the RenderResult's anchors
// table marks the targets whose class declares `preview: "block"` (the
// built-in footnote row); refPreview({ classes }) is a host override and
// wins. The content is the engine's fragment of the target (semantic HTML:
// references resolved, no ids, no backlink), stamped with the generation of
// the view it belongs to. The popup lives in the session's overlay, outside
// the commit root, so the view keeps patching while it is open.

export const REF_PREVIEW_CSS = `
.tsr-refpop { position: absolute; z-index: 20; max-width: 28em; max-height: 45vh;
  overflow: auto; padding: 0.5em 0.7em; font-size: 0.85em; line-height: 1.45;
  font-family: var(--tsr-pop-font, inherit); background: var(--tsr-pop-bg, #fffdf7);
  color: var(--tsr-pop-fg, #1c1c1a); border: 1px solid rgba(0,0,0,0.18);
  border-radius: 4px; box-shadow: 0 4px 14px rgba(0,0,0,0.12); white-space: normal; }
.tsr-refpop > :first-child { margin-top: 0; }
.tsr-refpop > :last-child { margin-bottom: 0; }
.tsr-refpop p { margin: 0.4em 0; }
@media (prefers-color-scheme: dark) {
  .tsr-refpop { background: var(--tsr-pop-bg, #2a2a28); color: var(--tsr-pop-fg, #e6e4dc);
    border-color: rgba(255,255,255,0.18); } }
`;

export function refPreview({ classes } = {}) {
  const wants = (ref) => !!ref && (classes ? classes.includes(ref.cls) : ref.preview === 'block');
  return {
    name: 'refPreview',
    css: REF_PREVIEW_CSS,
    install(ctx) {
      // the popup's face: the body stack, then the CJK one (it is flow text)
      const face = [ctx.setting('fonts.body'), ctx.setting('fonts.cjk')].filter(Boolean).join(', ');
      let pop = null, current = null, ticket = 0;
      const hide = () => {
        ticket++;
        pop?.remove();
        pop = null;
        current = null;
      };
      // a reference (an <a> in the view) that previews, or null
      const refOf = (t) => {
        const a = t?.closest?.('a[href]');
        if (!a || !ctx.root()?.contains(a)) return null;
        const ref = ctx.refAt(a);
        return wants(ref) ? { a, ref } : null;
      };
      const inPop = (t) => !!pop && pop.contains(t);
      const place = (a) => {
        const or = ctx.overlay.getBoundingClientRect();
        const cr = ctx.container.getBoundingClientRect();
        const mr = a.getBoundingClientRect();
        const w = Math.min(pop.offsetWidth, cr.width);
        let left = mr.left - or.left;
        const right = cr.right - or.left;  // the container's right edge, overlay coordinates
        if (left + w > right) left = Math.max(cr.left - or.left, right - w);
        pop.style.left = `${left}px`;
        pop.style.top = `${mr.bottom - or.top + 6}px`;
      };
      // the fragment of the current reference, (re)written into the popup
      const fill = async (hit) => {
        const mine = ++ticket;
        const frag = await ctx.ops.fragment(hit.ref.label);
        if (mine !== ticket || current !== hit.a) return;
        if (!frag?.html) { hide(); return; }
        if (!pop) {
          pop = document.createElement('div');
          pop.className = 'tsr-refpop';
          pop.setAttribute('role', 'tooltip');
          pop.style.setProperty('--tsr-pop-font', face);
          ctx.overlay.appendChild(pop);
        }
        pop.dataset.tsrPreview = hit.ref.cls;
        pop.innerHTML = frag.html;
        place(hit.a);
      };
      const show = (hit) => {
        if (current === hit.a) return;
        hide();
        current = hit.a;
        fill(hit);
      };
      ctx.listen(ctx.container, 'mouseover', (e) => {
        const hit = refOf(e.target);
        if (hit) show(hit);
        else if (!inPop(e.target)) hide();
      });
      // leaving the reference keeps the popup while the pointer moves INTO it
      // (long notes scroll); leaving both hides
      ctx.listen(ctx.container, 'mouseout', (e) => {
        if ((refOf(e.target) || inPop(e.target)) && !refOf(e.relatedTarget) && !inPop(e.relatedTarget)) hide();
      });
      ctx.listen(ctx.container, 'focusin', (e) => { const hit = refOf(e.target); if (hit) show(hit); });
      ctx.listen(ctx.container, 'focusout', hide);
      ctx.listen(window, 'scroll', hide, { passive: true });
      // a commit under an open popup: the reference still in the view gets
      // the new content (its target may have changed); a replaced one closes
      ctx.onCommit(() => {
        if (!current) return;
        if (!current.isConnected) { hide(); return; }
        const hit = refOf(current);
        if (hit) fill(hit);
        else hide();
      });
      return hide;
    },
  };
}
