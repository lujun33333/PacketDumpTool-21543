import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const root = new URL('../../', import.meta.url);

async function source(path) {
  return readFile(new URL(path, root), 'utf8');
}

test('main process has no plaintext native staging or detached DLL cleaner', async () => {
  const [core, main, packageJson] = await Promise.all([
    source('src/main/native-core.js'),
    source('src/main/index.js'),
    source('package.json')
  ]);

  for (const retired of [
    'stageNativeRuntime',
    'updateNativeRuntimeLease',
    'cleanupNativeRuntimeSession',
    'NATIVE_RUNTIME_SESSION_FILES',
    'runtime-lease.json',
    'QQSpeedNativeBridge.dll',
    'qqspeed_clean_hook_64.dll'
  ]) {
    assert.equal(`${core}\n${main}`.includes(retired), false, `plaintext runtime residue remains: ${retired}`);
  }
  assert.doesNotMatch(main, /scheduleNativeRuntimeCleanup|runtimeRecordsByGamePid|runtimeDir/);

  const manifest = JSON.parse(packageJson);
  assert.equal(
    manifest.build.files.some((entry) => String(entry).includes('qqspeed-runtime.bundle')),
    false,
    'encrypted source bundle must be injected into the EXE, not copied into ASAR'
  );
});

test('utility process accepts no DLL path and performs no path-based native load', async () => {
  const [worker, loader] = await Promise.all([
    source('src/main/native-worker.js'),
    source('src/main/native-memory-loader.js')
  ]);
  for (const retired of [
    'dllPath',
    'runtimeDir',
    'koffi.load',
    'readFileSync'
  ]) {
    assert.equal(worker.includes(retired), false, `path-based worker residue remains: ${retired}`);
  }
  assert.match(worker, /mapNativeBridgeFromBytes/);
  assert.match(worker, /setHookPayload/);
  assert.match(worker, /runtimeMaterial/);
  assert.doesNotMatch(`${worker}\n${loader}`, /QQSpeedNativeBridge_|qqspeed_clean_hook/i);
  assert.match(loader, /(?:sha256|fnv|symbolHash|nameHash)/i);
});
