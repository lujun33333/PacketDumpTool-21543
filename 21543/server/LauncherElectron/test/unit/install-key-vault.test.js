import assert from 'node:assert/strict';
import {
  constants,
  createPublicKey,
  publicEncrypt
} from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';

import {
  InstallKeyVaultError,
  createInstallKeyVault
} from '../../src/main/install-key-vault.js';
import {
  protectBytesWithDpapi,
  unprotectBytesWithDpapi
} from '../../src/main/windows-dpapi.js';

function base64Url(bytes) {
  return Buffer.from(bytes).toString('base64url');
}

function publicKeyFromWire(wire) {
  assert.equal(wire.length, 259);
  return createPublicKey({
    key: {
      kty: 'RSA',
      e: base64Url(wire.subarray(0, 3)),
      n: base64Url(wire.subarray(3))
    },
    format: 'jwk'
  });
}

const fakeDpapi = Object.freeze({
  protect(bytes) {
    return Buffer.concat([Buffer.from('sealed:'), Buffer.from(bytes).reverse()]);
  },
  unprotect(bytes) {
    const value = Buffer.from(bytes);
    assert.equal(value.subarray(0, 7).toString(), 'sealed:');
    return Buffer.from(value.subarray(7)).reverse();
  }
});

test('install key vault creates one RSA identity and persists only a DPAPI-wrapped private key', async (t) => {
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-install-key-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const vault = createInstallKeyVault(root, fakeDpapi);

  const [first, concurrent] = await Promise.all([
    vault.getPublicIdentity(),
    vault.getPublicIdentity()
  ]);
  const second = await createInstallKeyVault(root, fakeDpapi).getPublicIdentity();

  assert.equal(first.algorithm, 'rsa-oaep-sha256');
  assert.equal(first.publicKey.length, 259);
  assert.equal(first.fingerprint.length, 32);
  assert.deepEqual(concurrent, first);
  assert.deepEqual(second, first);
  assert.equal(vault.file, join(root, 'runtime-install-key.json'));

  const persisted = readFileSync(vault.file, 'utf8');
  assert.doesNotMatch(persisted, /BEGIN (?:RSA )?PRIVATE KEY/);
  assert.match(persisted, /"privateKeyDpapi"/);
  assert.doesNotMatch(persisted, /"privateKey"\s*:/);
});

test('install key vault decrypts only RSA-OAEP-SHA256 material for its installation identity', async (t) => {
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-install-decrypt-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const vault = createInstallKeyVault(root, fakeDpapi);
  const identity = await vault.getPublicIdentity();
  const plaintext = Buffer.from('one-time-runtime-material');
  const ciphertext = publicEncrypt({
    key: publicKeyFromWire(identity.publicKey),
    padding: constants.RSA_PKCS1_OAEP_PADDING,
    oaepHash: 'sha256'
  }, plaintext);

  const decrypted = await vault.decrypt(ciphertext);
  try {
    assert.deepEqual(decrypted, plaintext);
  } finally {
    decrypted.fill(0);
    ciphertext.fill(0);
  }
  await assert.rejects(() => vault.decrypt(Buffer.alloc(255)), /ciphertext|256/i);
});

test('install key vault rejects malformed or substituted persisted identities', async (t) => {
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-install-corrupt-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const vault = createInstallKeyVault(root, fakeDpapi);
  await vault.getPublicIdentity();
  const parsed = JSON.parse(readFileSync(vault.file, 'utf8'));
  parsed.fingerprint = Buffer.alloc(32, 0xff).toString('base64');
  writeFileSync(vault.file, JSON.stringify(parsed));

  await assert.rejects(
    () => createInstallKeyVault(root, fakeDpapi).getPublicIdentity(),
    (error) => error instanceof InstallKeyVaultError && error.code === 'INSTALL_KEY_INVALID'
  );
});

test('Windows DPAPI byte wrapper round-trips binary input for the current user', {
  skip: process.platform !== 'win32'
}, () => {
  const plaintext = Buffer.from('00010203ff80414243', 'hex');
  const protectedBytes = protectBytesWithDpapi(plaintext, { description: 'QQSpeedLauncher test' });
  const cleartext = unprotectBytesWithDpapi(protectedBytes);
  try {
    assert.notDeepEqual(protectedBytes, plaintext);
    assert.deepEqual(cleartext, plaintext);
  } finally {
    protectedBytes.fill(0);
    cleartext.fill(0);
  }
});
