import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

import {
  decodeNativeRuntimeBundle,
  inspectNativeRuntimeBundle
} from '../../src/shared/native-runtime-bundle.js';
import {
  inspectNativeBridgePe,
  mapNativeBridgeFromBytes
} from '../../src/main/native-memory-loader.js';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const bundlePath = join(root, 'test', 'fixtures', 'native-memory-loader-v2.bundle');
const nativeResources = join(root, 'resources', 'native');
const TEST_CONTENT_KEY_HEX = '11'.repeat(32);
const TEST_BUNDLE_ID_HEX = 'a5'.repeat(32);

function decodeBridge() {
  const bundle = readFileSync(bundlePath);
  const contentKey = Buffer.from(TEST_CONTENT_KEY_HEX, 'hex');
  const expectedBundleId = Buffer.from(TEST_BUNDLE_ID_HEX, 'hex');
  let metadata;
  let decoded;
  try {
    metadata = inspectNativeRuntimeBundle(bundle);
    assert.deepEqual(metadata.bundleId, expectedBundleId);
    decoded = decodeNativeRuntimeBundle(bundle, { contentKey });
    assert.deepEqual(decoded.bundleId, expectedBundleId);
    const bridgeBytes = decoded.bridgeBytes;
    assert.ok(Buffer.isBuffer(bridgeBytes));
    assert.ok(Buffer.isBuffer(decoded.hookBytes));
    return {
      bytes: bridgeBytes,
      sha256: decoded.bridgeSha256
    };
  } finally {
    decoded?.bundleId.fill(0);
    decoded?.hookBytes.fill(0);
    metadata?.bundleId.fill(0);
    contentKey.fill(0);
    expectedBundleId.fill(0);
    bundle.fill(0);
  }
}

test('NativeBridge PE inspection exposes the exact manual-map requirements', () => {
  const bridge = decodeBridge();
  try {
    const image = inspectNativeBridgePe(bridge.bytes, { expectedSha256: bridge.sha256 });
    assert.equal(image.machine, 0x8664);
    assert.equal(image.pe32Plus, true);
    assert.equal(image.hasTls, false);
    assert.equal(image.hasDelayImports, false);
    assert.ok(image.exceptionEntryCount > 0);
    assert.ok((image.relocationTypeCounts.get(10) || 0) > 0);
    assert.deepEqual([...image.importModules].sort(), [
      'gdi32.dll',
      'kernel32.dll',
      'shell32.dll',
      'shlwapi.dll',
      'user32.dll'
    ]);
    const requiredExports = [
      'QQSpeedNativeBridge_ApplyAuth',
      'QQSpeedNativeBridge_BeginSession',
      'QQSpeedNativeBridge_Cancel',
      'QQSpeedNativeBridge_GetGameProcessId',
      'QQSpeedNativeBridge_GetLastError',
      'QQSpeedNativeBridge_GetProgress',
      'QQSpeedNativeBridge_GetState',
      'QQSpeedNativeBridge_IsBusy',
      'QQSpeedNativeBridge_SetHookPayload',
      'QQSpeedNativeBridge_StartGame'
    ];
    for (const name of requiredExports) assert.ok(image.exportRvas.has(name), name);
  } finally {
    bridge.bytes.fill(0);
  }
});

test('NativeBridge mapper rejects bytes that fail the pinned hash before allocation', () => {
  const bridge = decodeBridge();
  try {
    assert.throws(
      () => mapNativeBridgeFromBytes(bridge.bytes, { expectedSha256: '0'.repeat(64) }),
      /hash/i
    );
  } finally {
    bridge.bytes.fill(0);
  }
});

test('NativeBridge maps from bytes, calls exports by address and leaves no DLL file', {
  skip: process.platform !== 'win32' || process.arch !== 'x64'
}, () => {
  const before = readdirSync(nativeResources).filter((name) => /QQSpeedNativeBridge\.dll/i.test(name));
  assert.deepEqual(before, []);

  const script = String.raw`
    import assert from 'node:assert/strict';
    import { readFileSync } from 'node:fs';
    import { decodeNativeRuntimeBundle } from './src/shared/native-runtime-bundle.js';
    import { mapNativeBridgeFromBytes } from './src/main/native-memory-loader.js';

    const bundle = readFileSync('./test/fixtures/native-memory-loader-v2.bundle');
    const contentKey = Buffer.from('${TEST_CONTENT_KEY_HEX}', 'hex');
    const expectedBundleId = Buffer.from('${TEST_BUNDLE_ID_HEX}', 'hex');
    let decoded;
    try {
      decoded = decodeNativeRuntimeBundle(bundle, { contentKey });
      assert.deepEqual(decoded.bundleId, expectedBundleId);
    } finally {
      bundle.fill(0);
      contentKey.fill(0);
      expectedBundleId.fill(0);
    }
    const bridgeBytes = decoded.bridgeBytes;
    decoded.bundleId.fill(0);
    decoded.hookBytes.fill(0);
    const mapped = mapNativeBridgeFromBytes(bridgeBytes, { expectedSha256: decoded.bridgeSha256 });
    const relocated = mapNativeBridgeFromBytes(bridgeBytes, { expectedSha256: decoded.bridgeSha256 });
    bridgeBytes.fill(0);
    assert.notEqual(mapped.baseAddress, relocated.baseAddress);
    const getState = mapped.bindExport('__stdcall', 'QQSpeedNativeBridge_GetState', 'int', []);
    const getProgress = mapped.bindExport('__stdcall', 'QQSpeedNativeBridge_GetProgress', 'int', []);
    const isBusy = mapped.bindExport('__stdcall', 'QQSpeedNativeBridge_IsBusy', 'int', []);
    const getPid = mapped.bindExport('__stdcall', 'QQSpeedNativeBridge_GetGameProcessId', 'uint32', []);
    const getError = mapped.bindExport('__stdcall', 'QQSpeedNativeBridge_GetLastError', 'str16', []);
    assert.equal(getState(), 0);
    assert.equal(getProgress(), 0);
    assert.equal(isBusy(), 0);
    assert.equal(getPid(), 0);
    assert.equal(getError(), '');
    assert.equal(relocated.bindings.getState(), 0);
    assert.equal(typeof relocated.bindings.setHookPayload, 'function');
    assert.equal(relocated.cleanup(), true);
    assert.equal(mapped.cleanup(), true);
    assert.equal(mapped.cleanup(), false);
    assert.throws(() => getState(), /unloaded/i);
    console.log('NATIVE_MEMORY_LOADER_OK');
  `;
  const child = spawnSync(process.execPath, ['--input-type=module', '--eval', script], {
    cwd: root,
    encoding: 'utf8',
    timeout: 30_000,
    windowsHide: true
  });
  assert.equal(child.status, 0, `${child.stdout}\n${child.stderr}`);
  assert.match(child.stdout, /NATIVE_MEMORY_LOADER_OK/);

  const after = readdirSync(nativeResources).filter((name) => /QQSpeedNativeBridge\.dll/i.test(name));
  assert.deepEqual(after, []);
});
