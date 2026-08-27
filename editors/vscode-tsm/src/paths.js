// Asset root resolution: running from the Typesetter repo the extension
// serves engine assets straight from the checkout; a packaged .vsix ships
// them under vendor/ (scripts/vendor.mjs unpacks the engine dist there).
const path = require('node:path');
const fs = require('node:fs');

function assetRoot(extensionPath) {
  const candidates = [
    path.join(extensionPath, 'vendor'),
    path.join(extensionPath, '..', '..'), // repo checkout
  ];
  for (const c of candidates) {
    if (fs.existsSync(path.join(c, 'engine', 'build-wasm', 'typesetter.wasm')))
      return c;
  }
  throw new Error(
    'tsm: engine assets not found — build the wasm engine (repo checkout) ' +
    'or run scripts/vendor.mjs (packaged extension)');
}

module.exports = { assetRoot };
