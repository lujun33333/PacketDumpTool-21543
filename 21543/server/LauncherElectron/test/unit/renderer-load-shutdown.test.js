import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');
const main = readFileSync(resolve(root, 'src/main/index.js'), 'utf8');

function sourceBetween(startMarker, endMarker) {
  return segmentBetween(main, startMarker, endMarker);
}

function segmentBetween(source, startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start + startMarker.length);
  assert.ok(start >= 0 && end > start, `${startMarker} block is missing`);
  return source.slice(start, end);
}

test('renderer load rejection during application quit is logged as cancellation and never scheduled as fatal', () => {
  const createWindow = sourceBetween('function createMainWindow()', "ipcMain.handle('launcher:get-gm-catalog'");
  const loadRequest = sourceBetween(
    "logStartupEvent('electron-main', 'RENDERER_LOAD_REQUESTED');",
    "ipcMain.handle('launcher:get-gm-catalog'"
  );

  assert.match(createWindow, /const rendererLoadWindow = mainWindow;/);
  assert.match(loadRequest, /rendererLoadWindow\.loadFile\(rendererEntry\)\.catch/);
  assert.match(
    loadRequest,
    /if \(applicationQuitRequested\) \{[\s\S]*?'RENDERER_LOAD_CANCELLED_DURING_QUIT'[\s\S]*?return;[\s\S]*?\}/
  );

  const cancellationBranch = segmentBetween(
    loadRequest,
    'if (applicationQuitRequested) {',
    "logStartupEvent('electron-main', 'RENDERER_LOAD_FAILED'"
  );
  assert.doesNotMatch(cancellationBranch, /setImmediate|throw error/);
});

test('renderer load rejection outside quit remains a diagnosed fatal startup failure', () => {
  const loadRequest = sourceBetween(
    "logStartupEvent('electron-main', 'RENDERER_LOAD_REQUESTED');",
    "ipcMain.handle('launcher:get-gm-catalog'"
  );
  const cancellationReturn = loadRequest.indexOf('return;');
  const failedLog = loadRequest.indexOf("logStartupEvent('electron-main', 'RENDERER_LOAD_FAILED'");
  const fatalSchedule = loadRequest.indexOf('setImmediate(() => {');
  const fatalThrow = loadRequest.indexOf('throw error;');

  assert.ok(cancellationReturn >= 0, 'quit cancellation must return before the fatal path');
  assert.ok(failedLog > cancellationReturn, 'non-quit failure must still be logged after the quit guard');
  assert.ok(fatalSchedule > failedLog, 'non-quit failure must still schedule a fatal error');
  assert.ok(fatalThrow > fatalSchedule, 'the original renderer load error must remain fatal');
});

test('did-fail-load diagnostics distinguish application quit cancellation from a real load failure', () => {
  const createWindow = sourceBetween('function createMainWindow()', "ipcMain.handle('launcher:get-gm-catalog'");
  assert.match(
    createWindow,
    /applicationQuitRequested\s*\?\s*'RENDERER_LOAD_EVENT_CANCELLED_DURING_QUIT'\s*:\s*'RENDERER_LOAD_FAILED'/
  );
});
