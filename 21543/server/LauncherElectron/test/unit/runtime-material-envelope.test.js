import assert from 'node:assert/strict';
import {
  constants,
  createHash,
  createPublicKey,
  generateKeyPairSync,
  privateDecrypt,
  publicEncrypt
} from 'node:crypto';
import test from 'node:test';

import {
  RuntimeMaterialError,
  encodePreloadRuntimeMaterialPlaintext,
  encodePreloadRuntimeMaterialRequestBody,
  encodePreloadRuntimeMaterialResponseBody,
  encodeRuntimeMaterialPlaintext,
  encodeRuntimeMaterialRequestBody,
  encodeRuntimeMaterialResponseBody,
  parsePreloadRuntimeMaterialRequestBody,
  parseRuntimeMaterialRequestBody,
  unwrapPreloadRuntimeMaterialResponseBody,
  unwrapRuntimeMaterialResponseBody
} from '../../src/main/runtime-material-envelope.js';

function createIdentity() {
  const pair = generateKeyPairSync('rsa', { modulusLength: 2048, publicExponent: 0x10001 });
  const jwk = pair.publicKey.export({ format: 'jwk' });
  const publicKey = Buffer.concat([
    Buffer.from(jwk.e, 'base64url'),
    Buffer.from(jwk.n, 'base64url')
  ]);
  return {
    algorithm: 'rsa-oaep-sha256',
    publicKey,
    fingerprint: createHash('sha256').update(publicKey).digest(),
    decrypt(ciphertext) {
      return privateDecrypt({
        key: pair.privateKey,
        padding: constants.RSA_PKCS1_OAEP_PADDING,
        oaepHash: 'sha256'
      }, ciphertext);
    }
  };
}

function publicKeyObject(wire) {
  return createPublicKey({
    key: {
      kty: 'RSA',
      e: wire.subarray(0, 3).toString('base64url'),
      n: wire.subarray(3).toString('base64url')
    },
    format: 'jwk'
  });
}

const bundleId = () => Buffer.from('10'.repeat(32), 'hex');
const clientNonce = () => Buffer.from('20'.repeat(16), 'hex');
const tokenId = () => Buffer.from('30'.repeat(16), 'hex');
const contentKey = () => Buffer.from('40'.repeat(32), 'hex');

test('runtime material request carries only bundle, nonce and per-install RSA public identity', () => {
  const identity = createIdentity();
  const body = encodeRuntimeMaterialRequestBody({
    bundleId: bundleId(),
    clientNonce: clientNonce(),
    publicKey: identity.publicKey
  });
  const parsed = parseRuntimeMaterialRequestBody(body);
  try {
    assert.deepEqual(parsed.bundleId, bundleId());
    assert.deepEqual(parsed.clientNonce, clientNonce());
    assert.deepEqual(parsed.publicKey, identity.publicKey);
    assert.equal(body.length, 313);
  } finally {
    parsed.bundleId.fill(0);
    parsed.clientNonce.fill(0);
    parsed.publicKey.fill(0);
  }

  assert.throws(() => parseRuntimeMaterialRequestBody(Buffer.concat([body, Buffer.from([0])])), /length|trailing/i);
  assert.throws(() => encodeRuntimeMaterialRequestBody({
    bundleId: bundleId(),
    clientNonce: clientNonce(),
    publicKey: Buffer.alloc(258)
  }), /public key|259/i);
});

test('runtime material response decrypts and binds key to uin, install identity, bundle, nonce, token and expiry', async () => {
  const identity = createIdentity();
  const issuedAt = 1_750_000_000_000;
  const expiresAt = issuedAt + 30_000;
  const plaintext = encodeRuntimeMaterialPlaintext({
    uin: 123456,
    bundleId: bundleId(),
    tokenId: tokenId(),
    clientNonce: clientNonce(),
    installFingerprint: identity.fingerprint,
    issuedAt,
    expiresAt,
    contentKey: contentKey()
  });
  const ciphertext = publicEncrypt({
    key: publicKeyObject(identity.publicKey),
    padding: constants.RSA_PKCS1_OAEP_PADDING,
    oaepHash: 'sha256'
  }, plaintext);
  const body = encodeRuntimeMaterialResponseBody({
    bundleId: bundleId(),
    tokenId: tokenId(),
    clientNonce: clientNonce(),
    issuedAt,
    expiresAt,
    ciphertext
  });

  const result = await unwrapRuntimeMaterialResponseBody({
    body,
    uin: 123456,
    expectedBundleId: bundleId(),
    expectedClientNonce: clientNonce(),
    identity,
    decrypt: identity.decrypt,
    now: () => issuedAt + 1_000
  });
  try {
    assert.deepEqual(result.contentKey, contentKey());
    assert.deepEqual(result.bundleId, bundleId());
    assert.deepEqual(result.tokenId, tokenId());
    assert.equal(result.expiresAt, expiresAt);
  } finally {
    result.contentKey.fill(0);
    result.bundleId.fill(0);
    result.tokenId.fill(0);
    plaintext.fill(0);
    ciphertext.fill(0);
  }
});

test('v2 preload material uses QQSRMAT2 subject zero while v1 keeps authenticated UIN validation', async () => {
  const identity = createIdentity();
  const issuedAt = 1_750_000_000_000;
  const expiresAt = issuedAt + 30_000;
  assert.throws(() => encodeRuntimeMaterialPlaintext({
    uin: 0,
    bundleId: bundleId(), tokenId: tokenId(), clientNonce: clientNonce(),
    installFingerprint: identity.fingerprint, issuedAt, expiresAt, contentKey: contentKey()
  }), /UIN/i);

  const plaintext = encodePreloadRuntimeMaterialPlaintext({
    bundleId: bundleId(),
    tokenId: tokenId(),
    clientNonce: clientNonce(),
    installFingerprint: identity.fingerprint,
    issuedAt,
    expiresAt,
    contentKey: contentKey()
  });
  assert.equal(plaintext.subarray(0, 8).toString('ascii'), 'QQSRMAT2');
  assert.equal(plaintext.readUInt32BE(12), 0);
  const ciphertext = publicEncrypt({
    key: publicKeyObject(identity.publicKey),
    padding: constants.RSA_PKCS1_OAEP_PADDING,
    oaepHash: 'sha256'
  }, plaintext);
  const body = encodePreloadRuntimeMaterialResponseBody({
    bundleId: bundleId(), tokenId: tokenId(), clientNonce: clientNonce(),
    issuedAt, expiresAt, ciphertext
  });

  let decryptOutput;
  const result = await unwrapPreloadRuntimeMaterialResponseBody({
    body,
    expectedBundleId: bundleId(),
    expectedClientNonce: clientNonce(),
    identity,
    decrypt(ciphertextInput) {
      decryptOutput = identity.decrypt(ciphertextInput);
      return decryptOutput;
    }
  });
  assert.deepEqual(result.contentKey, contentKey());
  assert.equal(decryptOutput.every((byte) => byte === 0), true);

  result.contentKey.fill(0);
  result.bundleId.fill(0);
  result.tokenId.fill(0);
  plaintext.fill(0);
  ciphertext.fill(0);
});

test('v1 and v2 request and response parsers reject the other protocol version', async () => {
  const identity = createIdentity();
  const v1Request = encodeRuntimeMaterialRequestBody({
    bundleId: bundleId(), clientNonce: clientNonce(), publicKey: identity.publicKey
  });
  const v2Request = encodePreloadRuntimeMaterialRequestBody({
    bundleId: bundleId(), clientNonce: clientNonce(), publicKey: identity.publicKey
  });
  assert.equal(parseRuntimeMaterialRequestBody(v1Request).bundleId.length, 32);
  assert.equal(parsePreloadRuntimeMaterialRequestBody(v2Request).bundleId.length, 32);
  assert.throws(() => parseRuntimeMaterialRequestBody(v2Request), /header|version|invalid/i);
  assert.throws(() => parsePreloadRuntimeMaterialRequestBody(v1Request), /header|version|invalid/i);

  const issuedAt = 1_750_000_000_000;
  const expiresAt = issuedAt + 30_000;
  const v1Plaintext = encodeRuntimeMaterialPlaintext({
    uin: 123456, bundleId: bundleId(), tokenId: tokenId(), clientNonce: clientNonce(),
    installFingerprint: identity.fingerprint, issuedAt, expiresAt, contentKey: contentKey()
  });
  const v2Plaintext = encodePreloadRuntimeMaterialPlaintext({
    bundleId: bundleId(), tokenId: tokenId(), clientNonce: clientNonce(),
    installFingerprint: identity.fingerprint, issuedAt, expiresAt, contentKey: contentKey()
  });
  const encrypt = (plaintext) => publicEncrypt({
    key: publicKeyObject(identity.publicKey),
    padding: constants.RSA_PKCS1_OAEP_PADDING,
    oaepHash: 'sha256'
  }, plaintext);
  const v1Ciphertext = encrypt(v1Plaintext);
  const v2Ciphertext = encrypt(v2Plaintext);
  const responseFields = {
    bundleId: bundleId(), tokenId: tokenId(), clientNonce: clientNonce(), issuedAt, expiresAt
  };
  const v1Body = encodeRuntimeMaterialResponseBody({ ...responseFields, ciphertext: v1Ciphertext });
  const v2Body = encodePreloadRuntimeMaterialResponseBody({ ...responseFields, ciphertext: v2Ciphertext });

  await assert.rejects(() => unwrapRuntimeMaterialResponseBody({
    body: v2Body, uin: 123456, expectedBundleId: bundleId(), expectedClientNonce: clientNonce(),
    identity, decrypt: identity.decrypt
  }), (error) => error instanceof RuntimeMaterialError && error.code === 'RUNTIME_MATERIAL_INVALID');
  await assert.rejects(() => unwrapPreloadRuntimeMaterialResponseBody({
    body: v1Body, expectedBundleId: bundleId(), expectedClientNonce: clientNonce(),
    identity, decrypt: identity.decrypt
  }), (error) => error instanceof RuntimeMaterialError && error.code === 'RUNTIME_MATERIAL_INVALID');

  v1Plaintext.fill(0); v2Plaintext.fill(0);
  v1Ciphertext.fill(0); v2Ciphertext.fill(0);
});

test('runtime material response tolerates client wall-clock skew and rejects every substituted binding', async () => {
  const identity = createIdentity();
  const issuedAt = 1_750_000_000_000;
  const expiresAt = issuedAt + 30_000;
  const plaintext = encodeRuntimeMaterialPlaintext({
    uin: 123456,
    bundleId: bundleId(),
    tokenId: tokenId(),
    clientNonce: clientNonce(),
    installFingerprint: identity.fingerprint,
    issuedAt,
    expiresAt,
    contentKey: contentKey()
  });
  const ciphertext = publicEncrypt({
    key: publicKeyObject(identity.publicKey),
    padding: constants.RSA_PKCS1_OAEP_PADDING,
    oaepHash: 'sha256'
  }, plaintext);
  const validFields = {
    bundleId: bundleId(), tokenId: tokenId(), clientNonce: clientNonce(), issuedAt, expiresAt, ciphertext
  };
  const validBody = encodeRuntimeMaterialResponseBody(validFields);

  for (const clientClock of [issuedAt - 10 * 60_000, expiresAt + 10 * 60_000]) {
    const result = await unwrapRuntimeMaterialResponseBody({
      body: validBody,
      uin: 123456,
      identity,
      decrypt: identity.decrypt,
      now: () => clientClock,
      expectedBundleId: bundleId(),
      expectedClientNonce: clientNonce()
    });
    result.contentKey.fill(0);
    result.bundleId.fill(0);
    result.tokenId.fill(0);
  }

  for (const mutation of [
    { uin: 123457 },
    { body: encodeRuntimeMaterialResponseBody({ ...validFields, tokenId: Buffer.alloc(16, 0x99) }) },
    { body: encodeRuntimeMaterialResponseBody({ ...validFields, clientNonce: Buffer.alloc(16, 0x99) }) }
  ]) {
    await assert.rejects(
      () => unwrapRuntimeMaterialResponseBody({
        body: mutation.body || validBody,
        uin: mutation.uin || 123456,
        expectedBundleId: bundleId(),
        expectedClientNonce: clientNonce(),
        identity,
        decrypt: identity.decrypt,
        now: () => issuedAt + 1_000
      }),
      /binding|mismatch|invalid/i
    );
  }
  plaintext.fill(0);
  ciphertext.fill(0);
});

test('runtime material cannot replay a self-consistent response for another request and accepts UIN 10001', async () => {
  const identity = createIdentity();
  const issuedAt = 1_750_000_000_000;
  const replayNonce = Buffer.alloc(16, 0x91);
  const plaintext = encodeRuntimeMaterialPlaintext({
    uin: 10001,
    bundleId: bundleId(),
    tokenId: tokenId(),
    clientNonce: replayNonce,
    installFingerprint: identity.fingerprint,
    issuedAt,
    expiresAt: issuedAt + 30_000,
    contentKey: contentKey()
  });
  const ciphertext = publicEncrypt({
    key: publicKeyObject(identity.publicKey),
    padding: constants.RSA_PKCS1_OAEP_PADDING,
    oaepHash: 'sha256'
  }, plaintext);
  const replayBody = encodeRuntimeMaterialResponseBody({
    bundleId: bundleId(), tokenId: tokenId(), clientNonce: replayNonce,
    issuedAt, expiresAt: issuedAt + 30_000, ciphertext
  });

  await assert.rejects(
    () => unwrapRuntimeMaterialResponseBody({
      body: replayBody,
      uin: 10001,
      expectedBundleId: bundleId(),
      expectedClientNonce: clientNonce(),
      identity,
      decrypt: identity.decrypt,
      now: () => issuedAt + 1_000
    }),
    (error) => error instanceof RuntimeMaterialError && error.code === 'RUNTIME_MATERIAL_BINDING_MISMATCH'
  );
  plaintext.fill(0);
  ciphertext.fill(0);
  replayNonce.fill(0);
});
