import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');
const main = readFileSync(resolve(root, 'src/main/index.js'), 'utf8');
const updater = readFileSync(resolve(root, 'src/main/launcher-updater.js'), 'utf8');
const updateHtml = readFileSync(resolve(root, 'src/renderer/update.html'), 'utf8');
const updatePreloadPath = resolve(root, 'src/preload/update-preload.cjs');
const updateRendererPath = resolve(root, 'src/renderer/update.js');

function sourceBetween(startMarker, endMarker) {
  const start = main.indexOf(startMarker);
  const end = main.indexOf(endMarker, start + startMarker.length);
  assert.ok(start >= 0 && end > start, `${startMarker} block is missing`);
  return main.slice(start, end);
}

test('startup bypasses the updater before login or GameApp preload', () => {
  assert.doesNotMatch(main, /from '\.\/launcher-updater\.js';/);
  assert.doesNotMatch(main, /runMandatoryLauncherUpdate\(/);
  const ready = sourceBetween("app.whenReady().then(async () => {", "app.on('window-all-closed'");
  const updateGate = ready.indexOf('await runStartupUpdateGate()');
  const launcherStart = ready.indexOf('startLauncherAfterUpdateGate()');
  assert.ok(updateGate >= 0 && launcherStart > updateGate);
  assert.doesNotMatch(ready.slice(0, updateGate), /startStartupNativePreload\(\)/);

  const gatedStart = sourceBetween('function startLauncherAfterUpdateGate()', 'function createUpdateWindow()');
  assert.match(gatedStart, /createMainWindow\(\)/);
  assert.match(gatedStart, /createApplicationTray\(\)/);
  assert.match(gatedStart, /startStartupNativePreload\(\)/);
});

test('runtime update gate is disabled without opening the updater window', () => {
  const gate = sourceBetween('async function runStartupUpdateGate()', 'function createMainWindow()');
  assert.match(gate, /updateGatePassed = true;/);
  assert.match(gate, /UPDATE_GATE_DISABLED/);
  assert.match(gate, /return true;/);
  assert.doesNotMatch(gate, /runMandatoryLauncherUpdate\(|createUpdateWindow\(|publishUpdateProgress\(/);
});

test('packaged update checks use primary and backup update servers', () => {
  assert.match(
    updater,
    /UPDATE_MANIFEST_URL = 'http:\/\/43\.226\.61\.100:28881\/update\.json'/
  );
  assert.match(
    updater,
    /'http:\/\/114\.132\.178\.23:28881\/update\.json'/
  );
});

test('update shell is local, sandboxed, and has no Node integration', () => {
  const updateWindow = sourceBetween('function createUpdateWindow()', 'function destroyUpdateWindow()');
  assert.match(updateWindow, /loadFile\(updateRendererEntry\)/);
  assert.match(updateWindow, /preload:\s*updatePreloadEntry/);
  assert.match(updateWindow, /contextIsolation:\s*true/);
  assert.match(updateWindow, /sandbox:\s*true/);
  assert.match(updateWindow, /nodeIntegration:\s*false/);
  assert.match(updateWindow, /webSecurity:\s*true/);
  assert.match(main, /const UPDATE_WINDOW_SHOW_DELAY_MS = 300;/);
  assert.match(main, /launcher:update-progress/);
  assert.match(updateHtml, /正在检查登录器更新/);
  assert.match(updateHtml, /data-update-progress/);
  assert.match(updateHtml, /data-update-percent/);
  assert.match(updateHtml, /src="\.\/update\.js"/);
  assert.ok(existsSync(updatePreloadPath), 'dedicated update preload is missing');
  assert.ok(existsSync(updateRendererPath), 'update renderer script is missing');
  const updatePreload = readFileSync(updatePreloadPath, 'utf8');
  const updateRenderer = readFileSync(updateRendererPath, 'utf8');
  assert.match(updatePreload, /contextBridge\.exposeInMainWorld\('launcherUpdate'/);
  assert.match(updatePreload, /launcher:update-progress/);
  assert.match(updateRenderer, /launcherUpdate\?\.onProgress/);
});

test('a second launch restores the updater or recreates a missing launcher window', () => {
  const restore = sourceBetween('function restoreMainWindow(', 'function requestRendererCloseDecision(');
  assert.match(restore, /if \(!app\.isReady\(\)\)/);
  assert.match(restore, /if \(!updateGatePassed\)/);
  assert.match(restore, /createUpdateWindow\(\)/);
  assert.match(restore, /updateWindowMayShow = true/);
  assert.match(restore, /showUpdateWindowWhenReady\(\)/);
  assert.match(restore, /if \(!mainWindow \|\| mainWindow\.isDestroyed\(\)\)/);
  assert.match(restore, /startLauncherAfterUpdateGate\(\)/);

  const singleInstance = sourceBetween("app.on('second-instance'", "logStartupEvent('electron-main', 'APP_READY_BEGIN')");
  assert.match(singleInstance, /SECOND_INSTANCE_RECEIVED/);
  assert.match(singleInstance, /restoreMainWindow\('second-instance'\)/);
});
