import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import test from 'node:test';

const modulePath = resolve(
  import.meta.dirname,
  '..',
  '..',
  'src',
  'main',
  'game-loading-visibility.js'
);

async function createHarness() {
  assert.equal(existsSync(modulePath), true, 'game-loading visibility controller is missing');
  const { createGameLoadingVisibilityController } = await import(pathToFileURL(modulePath).href);
  const hidden = [];
  let restoreCount = 0;
  let quitting = false;
  const controller = createGameLoadingVisibilityController({
    onHide: (window) => hidden.push(window),
    onRestore: () => { restoreCount += 1; },
    isQuitting: () => quitting
  });
  return {
    controller,
    hidden,
    get restoreCount() { return restoreCount; },
    setQuitting(value) { quitting = value; }
  };
}

test('a failed launch releases only its own visibility request', async () => {
  const harness = await createHarness();
  const accepted = harness.controller.begin({ id: 'primary' });
  const failed = harness.controller.begin({ id: 'auxiliary' });

  harness.controller.activate(accepted);
  assert.equal(harness.controller.finish(failed), true);
  assert.equal(harness.restoreCount, 0);
  assert.equal(harness.controller.size, 1);
  assert.deepEqual(harness.hidden.map((window) => window.id), ['primary', 'auxiliary']);
});

test('the launcher restores only after the final pending or active game ends', async () => {
  const harness = await createHarness();
  const primary = harness.controller.begin({ id: 'primary' });
  const auxiliary = harness.controller.begin({ id: 'auxiliary' });

  harness.controller.activate(primary);
  harness.controller.activate(auxiliary);
  harness.controller.finish(primary);
  assert.equal(harness.restoreCount, 0);

  harness.controller.finish(auxiliary);
  assert.equal(harness.restoreCount, 1);
  assert.equal(harness.controller.size, 0);
  assert.equal(harness.controller.finish(auxiliary), false, 'finishing twice must be harmless');
  assert.equal(harness.restoreCount, 1);
});

test('finishing the final game never restores while the application is quitting', async () => {
  const harness = await createHarness();
  const attempt = harness.controller.begin({ id: 'primary' });
  harness.controller.activate(attempt);
  harness.setQuitting(true);

  harness.controller.finish(attempt);

  assert.equal(harness.controller.size, 0);
  assert.equal(harness.restoreCount, 0);
});
