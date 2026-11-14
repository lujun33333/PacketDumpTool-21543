import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');
const main = readFileSync(resolve(root, 'src/main/index.js'), 'utf8');

function sourceBetween(start, end) {
  const startIndex = main.indexOf(start);
  const endIndex = main.indexOf(end, startIndex + start.length);
  assert.ok(startIndex >= 0, `missing source anchor: ${start}`);
  assert.ok(endIndex > startIndex, `missing source anchor: ${end}`);
  return main.slice(startIndex, endIndex);
}

test('main process creates the application tray with the packaged icon', () => {
  assert.match(main, /import \{[^}]*\bMenu\b[^}]*\bTray\b[^}]*\} from 'electron';/s);
  assert.match(main, /import \{ createTrayController \} from '\.\/tray-controller\.js';/);

  const traySetup = sourceBetween('function createApplicationTray()', 'function createMainWindow()');
  assert.match(traySetup, /createTrayController\(\{/);
  assert.match(traySetup, /iconPath:\s*join\(appRoot, 'assets', 'app\.ico'\)/);
  assert.match(traySetup, /createTray:\s*\(iconPath\) => new Tray\(iconPath\)/);
  assert.match(traySetup, /buildMenu:\s*\(template\) => Menu\.buildFromTemplate\(template\)/);
  assert.match(traySetup, /onQuit:\s*requestApplicationQuit/);
  assert.match(traySetup, /onResetClosePreference:\s*resetClosePreferencePrompt/);

  const gatedStart = sourceBetween('function startLauncherAfterUpdateGate()', 'function createUpdateWindow()');
  assert.match(gatedStart, /createMainWindow\(\);[\s\S]*?createApplicationTray\(\);/);

  const ready = sourceBetween("app.whenReady().then(async () => {", "app.on('window-all-closed'");
  assert.match(ready, /await runStartupUpdateGate\(\)[\s\S]*?startLauncherAfterUpdateGate\(\)/);
});

test('title-bar minus still minimizes while native close requests the shared renderer dialog', () => {
  const minimizeHandler = sourceBetween("ipcMain.handle('window:minimize'", "ipcMain.handle('window:close'");
  assert.match(minimizeHandler, /BrowserWindow\.fromWebContents\(event\.sender\)\?\.minimize\(\)/);
  assert.doesNotMatch(minimizeHandler, /\.hide\(\)/);

  const closeHandler = sourceBetween("mainWindow.on('close'", "mainWindow.on('closed'");
  const hideBranch = closeHandler.indexOf('if (!applicationQuitRequested)');
  const cancellationBranch = closeHandler.indexOf('hasPendingNativeSession()');
  assert.ok(hideBranch >= 0 && cancellationBranch > hideBranch);
  assert.match(closeHandler, /if \(!applicationQuitRequested\)[\s\S]*?event\.preventDefault\(\)[\s\S]*?requestRendererCloseDecision\(mainWindow\)[\s\S]*?return;/);
});

test('close IPC requests a decision and apply-close-action owns the selected result', () => {
  const closeIpc = sourceBetween("ipcMain.handle('window:close'", 'const hasSingleInstanceLock');
  assert.match(closeIpc, /requestRendererCloseDecision\(window\)/);
  assert.match(closeIpc, /ipcMain\.handle\('window:apply-close-action'/);
  assert.match(closeIpc, /action === 'tray'[\s\S]*?trayController\?\.hideToTray\(window\)/);
  assert.match(closeIpc, /action === 'quit'[\s\S]*?requestApplicationQuit\(\)/);
  assert.match(closeIpc, /Unsupported close action/);
});

test('a late ready-to-show event cannot reopen a window already hidden to the tray', () => {
  const windowBlock = sourceBetween('function createMainWindow()', "ipcMain.handle('launcher:get-state'");
  assert.match(windowBlock, /mainWindow\.on\('hide',[\s\S]*?mainWindowShouldBeVisible\s*=\s*false/);
  assert.match(windowBlock, /mainWindow\.once\('ready-to-show'[\s\S]*?if \(mainWindowShouldBeVisible\)/);
});

test('accepted game starts hide in the tray and failed starts restore the launcher', () => {
  const loadingHelper = sourceBetween('function hideForGameLoading(', 'function restoreAfterGameLoadingFailure(');
  const loadingController = sourceBetween(
    'const gameLoadingVisibility = createGameLoadingVisibilityController(',
    'function getSettingsDir()'
  );
  assert.match(loadingController, /trayController\?\.hideForGameLoading\(window\)/);
  assert.doesNotMatch(loadingHelper, /\.minimize\(\)/);

  const instanceStart = sourceBetween("ipcMain.handle('launcher:start-instance'", "ipcMain.handle('launcher:start-game'");
  const primaryStart = sourceBetween("ipcMain.handle('launcher:start-game'", "ipcMain.handle('window:minimize'");
  assert.match(instanceStart, /const loadingAttempt = hideForGameLoading\(/);
  assert.match(primaryStart, /const loadingAttempt = hideForGameLoading\(/);
  assert.ok(instanceStart.indexOf('hideForGameLoading(') < instanceStart.indexOf('await startAdditionalNativeGame'));
  assert.ok(primaryStart.indexOf('hideForGameLoading(') < primaryStart.indexOf('await startNativeGame'));
  assert.match(instanceStart, /catch \(error\) \{[\s\S]*?restoreAfterGameLoadingFailure\(loadingAttempt\);[\s\S]*?throw error;/);
  assert.match(primaryStart, /catch \(error\) \{[\s\S]*?restoreAfterGameLoadingFailure\(loadingAttempt\);[\s\S]*?throw error;/);

  const primaryStatus = sourceBetween('function setLaunchStatus(', 'function logRejectedIpc(');
  const recordStatus = sourceBetween('function setRecordStatus(', 'function rejectRecordWaiters(');
  assert.doesNotMatch(primaryStatus, /restoreAfterGameLoadingFailure\(\)/);
  assert.match(recordStatus, /FAILED_SESSION_PHASES\.has[\s\S]*?restoreAfterGameLoadingFailure\(record\.gameLoadingAttempt\)/);
});

test('game process monitors release the owning attempt instead of global window state', () => {
  const primaryExit = sourceBetween('function handleGameProcessExit(', 'function beginGameProcessMonitor(');
  const auxiliaryMonitor = sourceBetween('function beginAuxiliaryGameProcessMonitor(', 'function isLaunchBusy()');
  assert.match(primaryExit, /finishGameLoadingAttempt\(loadingAttempt\)/);
  assert.doesNotMatch(primaryExit, /hiddenForGameLoading/);
  assert.match(auxiliaryMonitor, /finishGameLoadingAttempt\(loadingAttempt\)/);
});

test('tray exit requests real app quit, reuses pending-session cleanup, and destroys the tray', () => {
  const quitRequest = sourceBetween('function requestApplicationQuit()', 'function createApplicationTray()');
  assert.match(quitRequest, /applicationQuitRequested = true;/);
  assert.match(quitRequest, /app\.quit\(\)/);

  const closeHandler = sourceBetween("mainWindow.on('close'", "mainWindow.on('closed'");
  assert.match(closeHandler, /hasPendingNativeSession\(\)[\s\S]*?cancelAllPendingNativeSessions\(\)[\s\S]*?mainWindow\?\.close\(\)/);

  const beforeQuit = sourceBetween("app.on('before-quit'", "app.on('second-instance'");
  assert.match(beforeQuit, /applicationQuitRequested = true;/);
  assert.match(beforeQuit, /trayController\?\.destroy\(\)/);
});
