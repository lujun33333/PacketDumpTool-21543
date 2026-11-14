import assert from 'node:assert/strict';
import test from 'node:test';

const codecModule = new URL('../../src/shared/native-runtime-bundle.js', import.meta.url);

async function loadCodec() {
  return import(codecModule);
}

function samplePeBytes(size, multiplier) {
  const bytes = Buffer.alloc(size);
  bytes.write('MZ', 0, 'ascii');
  for (let index = 2; index < bytes.length; index += 1) {
    bytes[index] = (index * multiplier + 17) & 0xff;
  }
  return bytes;
}

const contentKey = () => Buffer.from('11'.repeat(32), 'hex');
const wrongKey = () => Buffer.from('22'.repeat(32), 'hex');
const bundleId = () => Buffer.from('a5'.repeat(32), 'hex');
const fixedNonce = () => Buffer.from('33'.repeat(12), 'hex');

test('runtime bundle v2 round-trips independent Bridge and Hook entries with an external key', async () => {
  const {
    RUNTIME_BUNDLE_ARCH,
    RUNTIME_BUNDLE_SCHEMA_VERSION,
    decodeNativeRuntimeBundle,
    encodeNativeRuntimeBundle,
    inspectNativeRuntimeBundle
  } = await loadCodec();
  const bridgeBytes = samplePeBytes(8192, 31);
  const hookBytes = samplePeBytes(4096, 19);

  const bundle = encodeNativeRuntimeBundle(
    { bridgeBytes, hookBytes },
    { contentKey: contentKey(), bundleId: bundleId(), nonce: fixedNonce() }
  );
  const inspected = inspectNativeRuntimeBundle(bundle);
  assert.equal(inspected.schemaVersion, RUNTIME_BUNDLE_SCHEMA_VERSION);
  assert.deepEqual(inspected.bundleId, bundleId());
  assert.equal(inspected.bridgeSize, bridgeBytes.length);
  assert.equal(inspected.hookSize, hookBytes.length);

  const decoded = decodeNativeRuntimeBundle(bundle, { contentKey: contentKey() });
  try {
    assert.equal(decoded.arch, RUNTIME_BUNDLE_ARCH);
    assert.deepEqual(decoded.bundleId, bundleId());
    assert.deepEqual(decoded.bridgeBytes, bridgeBytes);
    assert.deepEqual(decoded.hookBytes, hookBytes);
    assert.match(decoded.bridgeSha256, /^[0-9a-f]{64}$/);
    assert.match(decoded.hookSha256, /^[0-9a-f]{64}$/);
    assert.match(decoded.bundleSha256, /^[0-9a-f]{64}$/);
  } finally {
    decoded.bundleId.fill(0);
    decoded.bridgeBytes.fill(0);
    decoded.hookBytes.fill(0);
  }
});

test('runtime bundle uses a fresh nonce by default and never exposes native DLL names', async () => {
  const { encodeNativeRuntimeBundle } = await loadCodec();
  const entries = {
    bridgeBytes: samplePeBytes(8192, 31),
    hookBytes: samplePeBytes(4096, 19)
  };
  const first = encodeNativeRuntimeBundle(entries, { contentKey: contentKey(), bundleId: bundleId() });
  const second = encodeNativeRuntimeBundle(entries, { contentKey: contentKey(), bundleId: bundleId() });

  assert.notDeepEqual(first, second);
  for (const bundle of [first, second]) {
    assert.equal(bundle.includes(Buffer.from('QQSpeedNativeBridge.dll')), false);
    assert.equal(bundle.includes(Buffer.from('qqspeed_clean_hook_64.dll')), false);
  }
});

test('runtime bundle rejects a missing or wrong content key and authenticated tampering', async () => {
  const { decodeNativeRuntimeBundle, encodeNativeRuntimeBundle } = await loadCodec();
  const bundle = encodeNativeRuntimeBundle(
    {
      bridgeBytes: samplePeBytes(8192, 31),
      hookBytes: samplePeBytes(4096, 19)
    },
    { contentKey: contentKey(), bundleId: bundleId(), nonce: fixedNonce() }
  );

  assert.throws(() => decodeNativeRuntimeBundle(bundle), /content key|required|32/i);
  assert.throws(() => decodeNativeRuntimeBundle(bundle, { contentKey: wrongKey() }), /authentication|tampered/i);
  for (const offset of [bundle.length - 17, bundle.length - 1]) {
    const tampered = Buffer.from(bundle);
    tampered[offset] ^= 0x80;
    assert.throws(
      () => decodeNativeRuntimeBundle(tampered, { contentKey: contentKey() }),
      /authentication|integrity|tampered/i
    );
  }
});

test('runtime bundle validates schema, entry sizes, ids, keys, truncation and trailing bytes', async () => {
  const {
    MAX_NATIVE_ENTRY_BYTES,
    RUNTIME_BUNDLE_HEADER_OFFSETS,
    decodeNativeRuntimeBundle,
    encodeNativeRuntimeBundle,
    inspectNativeRuntimeBundle
  } = await loadCodec();
  const entries = {
    bridgeBytes: samplePeBytes(8192, 31),
    hookBytes: samplePeBytes(4096, 19)
  };
  const options = { contentKey: contentKey(), bundleId: bundleId(), nonce: fixedNonce() };
  const bundle = encodeNativeRuntimeBundle(entries, options);

  const wrongSchema = Buffer.from(bundle);
  wrongSchema.writeUInt16LE(0xffff, RUNTIME_BUNDLE_HEADER_OFFSETS.schemaVersion);
  assert.throws(() => inspectNativeRuntimeBundle(wrongSchema), /schema/i);

  const oversized = Buffer.from(bundle);
  oversized.writeUInt32LE(MAX_NATIVE_ENTRY_BYTES + 1, RUNTIME_BUNDLE_HEADER_OFFSETS.bridgeSize);
  assert.throws(() => inspectNativeRuntimeBundle(oversized), /size|large|limit/i);

  assert.throws(() => encodeNativeRuntimeBundle(entries, { ...options, contentKey: Buffer.alloc(31) }), /content key|32/i);
  assert.throws(() => encodeNativeRuntimeBundle(entries, { ...options, bundleId: Buffer.alloc(31) }), /bundle id|32/i);
  assert.throws(() => encodeNativeRuntimeBundle({ bridgeBytes: entries.bridgeBytes }, options), /hook/i);
  assert.throws(() => decodeNativeRuntimeBundle(bundle.subarray(0, bundle.length - 1), { contentKey: contentKey() }), /length|truncated|size/i);
  assert.throws(() => inspectNativeRuntimeBundle(Buffer.concat([bundle, Buffer.from([0])])), /length|trailing|size/i);
});
