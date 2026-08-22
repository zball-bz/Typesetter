import { defineConfig } from '@playwright/test';

// dpr is a first-class test axis (testing.md §5.2): the 1.25 project exists
// because of the founding fractional-DPR re-break bug.
const viewport = { width: 960, height: 720 };

export default defineConfig({
  testDir: 'test/e2e',
  timeout: 30000,
  use: { baseURL: 'http://localhost:8123' },
  webServer: {
    command: 'node tools/serve.mjs 8123',
    url: 'http://localhost:8123/test/e2e/harness.html',
    reuseExistingServer: true,
  },
  projects: [
    { name: 'chromium-dsf1', use: { browserName: 'chromium', deviceScaleFactor: 1, viewport } },
    { name: 'chromium-dsf1.25', use: { browserName: 'chromium', deviceScaleFactor: 1.25, viewport } },
    { name: 'chromium-dsf1.5', use: { browserName: 'chromium', deviceScaleFactor: 1.5, viewport } },
    { name: 'chromium-dsf2', use: { browserName: 'chromium', deviceScaleFactor: 2, viewport } },
  ],
});
