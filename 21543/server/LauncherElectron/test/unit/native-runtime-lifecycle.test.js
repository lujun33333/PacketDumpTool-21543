import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..', '..');
const main = readFileSync(resolve(root, 'src/main/index.js'), 'utf8');
const worker = readFileSync(resolve(root, 'src/main/native-worker.js'), 'utf8');
const nativeBridge = readFileSync(resolve(root, 'native/NativeBridge/NativeBridge.cpp'), 'utf8');

test('main delegates native lifetime to one utility process without disk leases or scavenging', () => {
  assert.match(main, /utilityProcess\.fork\(nativeWorkerEntry/);
  assert.match(main, /child\.on\('exit'/);
  assert.doesNotMatch(main, /scavengeNativeRuntime|updateNativeRuntimeLease|runtime-lease\.json|runtimeDir/);
});

test('utility keeps decoded payloads local, zeroes them, and owns Bridge cleanup', () => {
  const mapping = worker.indexOf('mapNativeBridgeFromBytes(');
  const handoff = worker.indexOf('bindings.setHookPayload(');
  const begin = worker.indexOf('bindings.beginSession.async(');
  assert.ok(mapping >= 0 && handoff > mapping && begin > handoff);
  assert.match(worker, /bridgeBytes\.fill\(0\)/);
  assert.match(worker, /hookBytes\.fill\(0\)/);
  assert.match(worker, /contentKey\.fill\(0\)/);
  assert.match(worker, /function cleanupMappedBridge\(\)/);
  assert.match(worker, /finishSession[\s\S]*?cleanupMappedBridge\(\)/);
  assert.match(worker, /process\.on\('exit'[\s\S]*?cleanupMappedBridge\(\)/);
});

test('closing the UI cancels an unfinished utility session but leaves an active GameApp monitored', () => {
  assert.match(main, /mainWindow\.on\('close'[\s\S]*?hasPendingNativeSession\(\)[\s\S]*?cancelAllPendingNativeSessions\(\)/);
  assert.match(main, /cancelAllPendingNativeSessions[\s\S]*?auxiliaryLaunches\.values\(\)[\s\S]*?cancelAuxiliaryNativeSession/);
  assert.match(main, /cancelAllPendingNativeSessions[\s\S]*?activeLaunch && !activeLaunch\.runFinished[\s\S]*?cancelNativeSession\(\)/);
  assert.match(main, /beginGameProcessMonitor\(gameProcessId,[^\n]*record\.gameLoadingAttempt\)/);
  assert.match(main, /handleGameProcessExit[\s\S]*?finishGameLoadingAttempt\(loadingAttempt\)/);
  assert.match(main, /createGameLoadingVisibilityController\([\s\S]*?onRestore:\s*restoreMainWindow/);
  assert.doesNotMatch(main, /scheduleNativeRuntimeCleanup|cleanupRuntimeRecord/);
});

test('utility verifies bundle and PE identities before in-memory mapping', () => {
  const inspect = worker.indexOf('inspectNativeRuntimeBundle(');
  const decode = worker.indexOf('decodeNativeRuntimeBundle(');
  const validate = worker.indexOf("decodedRuntime?.bridgeSha256 !== context.metadata.bridgeSha256");
  const map = worker.indexOf('mapNativeBridgeFromBytes(');
  assert.ok(inspect >= 0 && decode > inspect && validate > decode && map > validate);
  assert.match(worker, /expectedSha256:\s*decodedRuntime\.bridgeSha256/);
  assert.doesNotMatch(worker, /koffi\.load\(|dllPath/);
});

test('manual-mapped Bridge derives every game path from the selected game root', () => {
  const initStart = nativeBridge.indexOf('BOOL InitInstance(');
  const initEnd = nativeBridge.indexOf('\nLRESULT CALLBACK WndProc', initStart);
  assert.ok(initStart >= 0 && initEnd > initStart);
  const initInstance = nativeBridge.slice(initStart, initEnd);

  // A manually mapped image is not registered in the loader module list, so
  // GetModuleFileNameW(mappedBase) cannot be a prerequisite for launching.
  assert.doesNotMatch(initInstance, /ResolveLauncherDirectory\s*\(/);
  const selectedRoot = initInstance.indexOf('lstrcpynW(GamePath, gSelectedGameRoot.c_str(), MAX_PATH)');
  const buildPaths = initInstance.indexOf('BuildGamePaths()');
  assert.ok(selectedRoot >= 0 && buildPaths > selectedRoot);
});
