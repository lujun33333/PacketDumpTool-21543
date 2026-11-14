import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');
const main = readFileSync(resolve(root, 'src/main/index.js'), 'utf8');

function sourceBetween(startMarker, endMarker) {
  const start = main.indexOf(startMarker);
  const end = main.indexOf(endMarker, start + startMarker.length);
  assert.ok(start >= 0 && end > start, `${startMarker} block is missing`);
  return main.slice(start, end);
}

test('main begins native preload immediately after the update gate creates the launcher window', () => {
  const gatedStart = sourceBetween('function startLauncherAfterUpdateGate()', 'function createUpdateWindow()');
  const createWindow = gatedStart.indexOf('createMainWindow();');
  const createTray = gatedStart.indexOf('createApplicationTray();');
  const preload = gatedStart.indexOf('startStartupNativePreload();');
  assert.ok(createWindow >= 0 && createTray > createWindow && preload > createTray);

  const ready = sourceBetween("app.whenReady().then(async () => {", "app.on('window-all-closed'");
  assert.ok(ready.indexOf('await runStartupUpdateGate()') < ready.indexOf('startLauncherAfterUpdateGate()'));
});

test('startup preload is fire-and-forget and skips an invalid game installation', () => {
  const helper = sourceBetween('function startStartupNativePreload()', 'function requestApplicationQuit()');
  assert.match(helper, /prepareGameApp\(appRoot, getSettingsDir\(\)\)/);
  assert.match(helper, /if \(!readiness\.ready\)[\s\S]*?return;/);
  assert.match(helper, /void preloadNativeGame\(\)\.catch/);
});
