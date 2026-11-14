import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const materialScript = fileURLToPath(
  new URL('../../src/scripts/offline-release-material.cjs', import.meta.url)
);

function canonicalMaster() {
  return Buffer.from(Array.from({ length: 32 }, (_, index) => index));
}

function canonicalBundleId() {
  return Buffer.from(Array.from({ length: 32 }, (_, index) => 0xa0 + index));
}

test('offline release content key follows the canonical HKDF-SHA256 contract', () => {
  const { deriveContentKey } = require(materialScript);
  const releaseMaster = canonicalMaster();
  const bundleId = canonicalBundleId();
  let contentKey;
  try {
    contentKey = deriveContentKey(releaseMaster, bundleId);
    assert.equal(
      contentKey.toString('hex'),
      'c1db7ad17a09e1cea10019adc1eeba63b4a1b20f5516cf5add6f50811a4ba7d6'
    );
  } finally {
    releaseMaster.fill(0);
    bundleId.fill(0);
    contentKey?.fill(0);
  }
});

test('offline release content key derivation rejects missing, short and zero material', () => {
  const { deriveContentKey } = require(materialScript);
  assert.throws(() => deriveContentKey(), /release master|buffer|32/i);
  assert.throws(() => deriveContentKey(Buffer.alloc(31), Buffer.alloc(32, 1)), /release master|32/i);
  assert.throws(() => deriveContentKey(Buffer.alloc(32, 1), Buffer.alloc(31)), /bundle|32/i);
  assert.throws(() => deriveContentKey(Buffer.alloc(32), Buffer.alloc(32, 1)), /release master|zero/i);
  assert.throws(() => deriveContentKey(Buffer.alloc(32, 1), Buffer.alloc(32)), /bundle|zero/i);
});

test('offline release generation owns two independent non-zero random values', () => {
  const { generateReleaseMaterial } = require(materialScript);
  let call = 0;
  const generated = generateReleaseMaterial((size) => {
    assert.equal(size, 32);
    call += 1;
    return Buffer.alloc(size, call === 1 ? 0x31 : 0x72);
  });
  try {
    assert.equal(call, 2);
    assert.equal(generated.releaseMaster.length, 32);
    assert.equal(generated.bundleId.length, 32);
    assert.equal(generated.releaseMaster.equals(generated.bundleId), false);
    assert.equal(generated.releaseMaster[0], 0x31);
    assert.equal(generated.bundleId[0], 0x72);
  } finally {
    generated.releaseMaster.fill(0);
    generated.bundleId.fill(0);
  }
});

test('release master is split into three shares and reconstructed only at runtime', () => {
  const { splitReleaseMaster } = require(materialScript);
  const releaseMaster = canonicalMaster();
  const fills = [0x55, 0xaa];
  let call = 0;
  const shares = splitReleaseMaster(releaseMaster, (size) => Buffer.alloc(size, fills[call++]));
  const reconstructed = Buffer.alloc(32);
  try {
    assert.equal(call, 2);
    assert.equal(shares.length, 3);
    for (let index = 0; index < reconstructed.length; index += 1) {
      reconstructed[index] = shares[0][index] ^ shares[1][index] ^ shares[2][index];
    }
    assert.deepEqual(reconstructed, releaseMaster);
    assert.equal(shares.some((share) => share.equals(releaseMaster)), false);
  } finally {
    releaseMaster.fill(0);
    reconstructed.fill(0);
    for (const share of shares) share.fill(0);
  }
});

test('embedded C++ source exposes only masked shares and the public bundle identity', () => {
  const { renderEmbeddedReleaseSource } = require(materialScript);
  const releaseMaster = canonicalMaster();
  const bundleId = canonicalBundleId();
  let source;
  try {
    source = renderEmbeddedReleaseSource(
      { releaseMaster, bundleId },
      (size) => Buffer.alloc(size, 0x3c)
    );
    assert.match(source, /kEmbeddedBundleId/);
    assert.match(source, /kReleaseMasterShareA/);
    assert.match(source, /kReleaseMasterShareB/);
    assert.match(source, /kReleaseMasterShareC/);
    assert.match(source, /volatile\s+const\s+std::uint8_t/);
    assert.doesNotMatch(source, /000102030405060708090a0b0c0d0e0f/i);
    assert.doesNotMatch(source, /0x00,\s*0x01,\s*0x02,\s*0x03,\s*0x04/);
  } finally {
    releaseMaster.fill(0);
    bundleId.fill(0);
    source = '';
  }
});

test('persisted embedded source restores the same release identity for a server-only rebuild', () => {
  const { parseEmbeddedReleaseSource, renderEmbeddedReleaseSource } = require(materialScript);
  const releaseMaster = canonicalMaster();
  const bundleId = canonicalBundleId();
  let parsed;
  let source = '';
  try {
    source = renderEmbeddedReleaseSource(
      { releaseMaster, bundleId },
      (size) => Buffer.alloc(size, 0x69)
    );
    parsed = parseEmbeddedReleaseSource(source);
    assert.deepEqual(parsed.releaseMaster, releaseMaster);
    assert.deepEqual(parsed.bundleId, bundleId);
    assert.throws(
      () => parseEmbeddedReleaseSource(source.replace('kReleaseMasterShareC', 'missingShareC')),
      /share c|embedded release material/i
    );
  } finally {
    parsed?.releaseMaster.fill(0);
    parsed?.bundleId.fill(0);
    releaseMaster.fill(0);
    bundleId.fill(0);
    source = '';
  }
});

test('release material masking fails closed on malformed RNG output or identity', () => {
  const { renderEmbeddedReleaseSource, splitReleaseMaster } = require(materialScript);
  const releaseMaster = canonicalMaster();
  const bundleId = canonicalBundleId();
  try {
    let call = 0;
    assert.throws(
      () => splitReleaseMaster(releaseMaster, () => (++call === 1 ? Buffer.alloc(32, 1) : Buffer.alloc(31, 2))),
      /share B|32/i
    );
    assert.throws(
      () => renderEmbeddedReleaseSource({ releaseMaster, bundleId: Buffer.alloc(31) }),
      /bundle|32/i
    );
  } finally {
    releaseMaster.fill(0);
    bundleId.fill(0);
  }
});
