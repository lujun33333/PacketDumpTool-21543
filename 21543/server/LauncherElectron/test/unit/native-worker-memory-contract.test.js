import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const worker = readFileSync(resolve(root, 'src/main/native-worker.js'), 'utf8');

test('start-session obtains v2 preload material and creates GameApp before reporting preloaded', () => {
  const start = worker.slice(
    worker.indexOf('async function initializeSession'),
    worker.indexOf('\nasync function waitForNativeState')
  );
  assert.ok(start.length > 0, 'initializeSession is missing');
  assert.match(start, /initializeNativeRuntimeContext/);
  assert.match(start, /transition\('begin'/);
  assert.match(start, /await preloadNativeGame\(nativeContext/);
  assert.match(start, /transition\('native-preloaded'/);
});

test('preload gates plaintext mapping behind v2 install material in strict order', () => {
  const preload = worker.slice(
    worker.indexOf('async function preloadNativeGame'),
    worker.indexOf('\nasync function initializeSession')
  );
  assert.match(preload, /requestPreloadRuntimeMaterial\([\s\S]*?startNativeGameFromMaterial\(context, runtimeMaterial\)/);

  const startFromMaterial = worker.slice(
    worker.indexOf('async function startNativeGameFromMaterial'),
    worker.indexOf('\nasync function preloadNativeGame')
  );
  const ordered = [
    'decodeNativeRuntimeBundle(',
    'mapNativeBridgeFromBytes(',
    'setHookPayload(',
    'beginSession.async(',
    'waitForNativeState(2',
    'requirePreloadedGameProcess('
  ];
  let previous = -1;
  for (const marker of ordered) {
    const index = startFromMaterial.indexOf(marker);
    assert.ok(index > previous, `${marker} is absent or out of order`);
    previous = index;
  }
  assert.match(startFromMaterial, /contentKey\.fill\(0\)/);
  assert.match(startFromMaterial, /bridgeBytes\.fill\(0\)/);
  assert.match(startFromMaterial, /hookBytes\.fill\(0\)/);
  assert.match(startFromMaterial, /preloadedGameProcessId\s*=\s*preloaded\.gameProcessId/);
});

test('login reuses an early preload or performs the authenticated fallback before applying auth', () => {
  const authenticate = worker.slice(
    worker.indexOf('async function authenticate'),
    worker.indexOf('\nasync function forgetCredential')
  );
  const ordered = [
    'loginPrivateServer(',
    'applyAuth(',
    'waitForNativeState(4'
  ];
  let previous = -1;
  for (const marker of ordered) {
    const index = authenticate.indexOf(marker);
    assert.ok(index > previous, `${marker} is absent or out of order`);
    previous = index;
  }
  assert.match(authenticate, /runtimeMaterial:\s*deferredNativePreload\s*\?/);
  assert.match(authenticate, /deferredNativePreload[\s\S]*?startNativeGameFromMaterial\(context, authResult\?\.runtimeMaterial\)/);
  assert.match(authenticate, /nativeStillPreloaded\s*&&\s*Boolean\(mappedBridge\)/);
  assert.match(authenticate, /deferredLoginCanRetry\s*=\s*deferredNativePreload\s*&&\s*!mappedBridge/);
  assert.match(authenticate, /authenticatedNative\.gameProcessId\s*!==\s*preloadedGameProcessId/);
  assert.match(authenticate, /cancelRequested[\s\S]*?error\?\.code\s*===\s*'ABORTED'[\s\S]*?return;/);
});

test('worker uses the in-memory mapper and never accepts a native path or plaintext IPC payload', () => {
  for (const retired of ['dllPath', 'runtimeDir', 'koffi.load', 'readFileSync']) {
    assert.equal(worker.includes(retired), false, retired);
  }
  assert.match(worker, /readEmbeddedRuntimeBundle\(process\.execPath\)/);
  assert.match(worker, /mapNativeBridgeFromBytes/);
  assert.match(worker, /cleanupMappedBridge/);
  assert.doesNotMatch(worker, /post(?:Message)?\([^)]*(?:bridgeBytes|hookBytes|contentKey)/s);
});

test('worker redacts progress text without classifying normal status as an error', () => {
  assert.match(worker, /safe\.status\.message\s*=\s*safeMessage\(safe\.status\.message\)/);
  assert.doesNotMatch(worker, /safe\.status\.message\s*=\s*customerMessage\(safe\.status\.message\)/);
});
