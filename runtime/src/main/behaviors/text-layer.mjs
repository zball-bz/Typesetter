// a11y.textLayer (plan P3-27; design T7 "ContentText projection"): the
// typeset lines are hidden from assistive technology and a visually hidden
// copy of the view's content text — the copy projection, one paragraph per
// block — stands in for them, so a screen reader reads words, not lines,
// hyphens, markers and equation numbers. A default behaviour that does
// nothing unless the setting is on; the layer lives in the session overlay
// (outside the commit root) and follows every commit.
export function textLayer() {
  return {
    name: 'textLayer',
    css: '.tsr-sr { position: absolute; width: 1px; height: 1px; margin: -1px; padding: 0; border: 0;' +
         ' overflow: hidden; clip-path: inset(50%); white-space: pre-wrap; }',
    install(ctx) {
      let layer = null;
      const run = () => {
        const root = ctx.root();
        if (!root || !ctx.setting('a11y.textLayer')) {
          if (layer) root?.removeAttribute('aria-hidden');
          layer?.remove();
          layer = null;
          return;
        }
        root.setAttribute('aria-hidden', 'true');
        if (!layer) {
          layer = document.createElement('div');
          layer.className = 'tsr-sr';
          layer.dataset.tsrShell = 'text-layer';
          ctx.overlay.appendChild(layer);
        }
        const range = document.createRange();
        range.selectNodeContents(root);
        layer.replaceChildren(...ctx.ops.contentBlocks(range).map((text) => {
          const p = document.createElement('p');
          p.textContent = text;
          return p;
        }));
      };
      run();
      ctx.onCommit(run);
      return () => layer?.remove();
    },
  };
}
