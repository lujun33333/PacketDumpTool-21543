import assert from 'node:assert/strict';
import {
  constants,
  createHash,
  createPublicKey,
  generateKeyPairSync,
  privateDecrypt,
  publicEncrypt
} from 'node:crypto';
import { EventEmitter } from 'node:events';
import { describe, it } from 'node:test';

import {
  CONFIG_KEY,
  PrivateLoginError,
  loginPrivateServer,
  requestPreloadRuntimeMaterial
} from '../../src/main/login/private-login-client.js';
import {
  decodeMessagePacket,
  encodeMessagePacket,
  hashLoginPassword
} from '../../src/main/login/private-protocol.js';
import {
  encodePreloadRuntimeMaterialPlaintext,
  encodePreloadRuntimeMaterialResponseBody,
  encodeRuntimeMaterialPlaintext,
  encodeRuntimeMaterialResponseBody,
  parsePreloadRuntimeMaterialRequestBody,
  parseRuntimeMaterialRequestBody
} from '../../src/main/runtime-material-envelope.js';

const fixedRandom = (length) => Buffer.alloc(length, 0x5a);

class FakeSocket extends EventEmitter {
  constructor({ connect = true, onWrite } = {}) {
    super();
    this.destroyed = false;
    this.writes = [];
    this.writeReferences = [];
    this.onWrite = onWrite;
    if (connect) {
      queueMicrotask(() => this.emit('connect'));
    }
  }

  write(value, callback) {
    this.writeReferences.push(value);
    const bytes = Buffer.from(value);
    this.writes.push(bytes);
    queueMicrotask(() => {
      this.onWrite?.(bytes, this);
      callback?.();
    });
    return true;
  }

  end() {
    this.destroyed = true;
  }

  destroy() {
    this.destroyed = true;
  }
}

function successResponse(uin, gameKey) {
  const responseBody = Buffer.alloc(22);
  responseBody.writeUInt16BE(0, 0);
  responseBody.writeUInt32BE(uin, 2);
  gameKey.copy(responseBody, 6);
  return encodeMessagePacket({
    msgId: 1,
    body: responseBody,
    key: CONFIG_KEY,
    encrypt: true,
    randomBytes: fixedRandom
  });
}

function runtimeIdentity() {
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

function rsaPublicFromWire(wire) {
  return createPublicKey({
    key: {
      kty: 'RSA',
      e: wire.subarray(0, 3).toString('base64url'),
      n: wire.subarray(3).toString('base64url')
    },
    format: 'jwk'
  });
}

describe('private login TCP client', () => {
  it('requests anonymous preload material with msgId 3 before any login packet', async () => {
    const identity = runtimeIdentity();
    const bundleId = Buffer.from('11'.repeat(32), 'hex');
    const tokenId = Buffer.from('22'.repeat(16), 'hex');
    const contentKey = Buffer.from('33'.repeat(32), 'hex');
    const issuedAt = 1_750_000_000_000;
    let socket;
    const createConnection = () => {
      socket = new FakeSocket({
        onWrite(request, activeSocket) {
          const decoded = decodeMessagePacket(request, { key: CONFIG_KEY });
          assert.equal(decoded.msgId, 3);
          // The server initializes HeadSeq to zero and MsgID 3 is not part of
          // the generic sequence-reset list, so an anonymous first packet must
          // advance to sequence one instead of being silently dropped.
          assert.equal(request.readUInt32BE(8), 1);
          assert.equal(decoded.body[0], 2);
          const materialRequest = parsePreloadRuntimeMaterialRequestBody(decoded.body);
          const plaintext = encodePreloadRuntimeMaterialPlaintext({
            bundleId: materialRequest.bundleId,
            tokenId,
            clientNonce: materialRequest.clientNonce,
            installFingerprint: identity.fingerprint,
            issuedAt,
            expiresAt: issuedAt + 30_000,
            contentKey
          });
          const ciphertext = publicEncrypt({
            key: rsaPublicFromWire(materialRequest.publicKey),
            padding: constants.RSA_PKCS1_OAEP_PADDING,
            oaepHash: 'sha256'
          }, plaintext);
          const responseBody = encodePreloadRuntimeMaterialResponseBody({
            bundleId: materialRequest.bundleId,
            tokenId,
            clientNonce: materialRequest.clientNonce,
            issuedAt,
            expiresAt: issuedAt + 30_000,
            ciphertext
          });
          activeSocket.emit('data', encodeMessagePacket({
            msgId: 3,
            body: responseBody,
            key: CONFIG_KEY,
            encrypt: true,
            randomBytes: fixedRandom
          }));
          plaintext.fill(0);
          ciphertext.fill(0);
          responseBody.fill(0);
          materialRequest.bundleId.fill(0);
          materialRequest.clientNonce.fill(0);
          materialRequest.publicKey.fill(0);
        }
      });
      return socket;
    };

    const result = await requestPreloadRuntimeMaterial({
      host: '127.0.0.1',
      port: 28880,
      timeoutMs: 1000,
      createConnection,
      randomBytes: fixedRandom,
      runtimeMaterial: {
        required: true,
        bundleId,
        ...identity,
        decrypt: identity.decrypt,
        randomBytes: () => Buffer.alloc(16, 0x77)
      }
    });
    try {
      assert.equal(socket.writes.length, 1);
      assert.equal(socket.writeReferences[0].every((byte) => byte === 0), true);
      const decoded = decodeMessagePacket(socket.writes[0], { key: CONFIG_KEY });
      assert.equal(decoded.msgId, 3);
      assert.deepEqual(result.contentKey, contentKey);
      assert.deepEqual(result.bundleId, bundleId);
      assert.deepEqual(result.tokenId, tokenId);
    } finally {
      result.contentKey.fill(0);
      result.bundleId.fill(0);
      result.tokenId.fill(0);
    }
  });

  it('distinguishes preload network failure from an unsupported connected server', async () => {
    const identity = runtimeIdentity();
    const runtimeMaterial = {
      required: true,
      bundleId: Buffer.alloc(32, 0x11),
      ...identity,
      decrypt: identity.decrypt,
      randomBytes: () => Buffer.alloc(16, 0x77)
    };
    await assert.rejects(
      requestPreloadRuntimeMaterial({
        host: '127.0.0.1', port: 28880, timeoutMs: 1000, runtimeMaterial,
        createConnection() {
          const socket = new FakeSocket({ connect: false });
          queueMicrotask(() => socket.emit('error', new Error('ECONNREFUSED')));
          return socket;
        }
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'NETWORK_ERROR'
    );

    await assert.rejects(
      requestPreloadRuntimeMaterial({
        host: '127.0.0.1', port: 28880, timeoutMs: 1000, runtimeMaterial,
        createConnection: () => new FakeSocket({ onWrite: (_request, socket) => socket.emit('close') })
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'RUNTIME_MATERIAL_UNSUPPORTED'
    );

    await assert.rejects(
      requestPreloadRuntimeMaterial({
        host: '127.0.0.1', port: 28880, timeoutMs: 20, runtimeMaterial,
        createConnection: () => new FakeSocket()
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'NETWORK_ERROR'
    );
  });

  it('rejects a v1 response as invalid without falling back to the authenticated login flow', async () => {
    const identity = runtimeIdentity();
    const issuedAt = 1_750_000_000_000;
    let socket;
    const createConnection = () => {
      socket = new FakeSocket({
        onWrite(request, activeSocket) {
          const decoded = decodeMessagePacket(request, { key: CONFIG_KEY });
          const materialRequest = parsePreloadRuntimeMaterialRequestBody(decoded.body);
          const plaintext = encodeRuntimeMaterialPlaintext({
            uin: 246810,
            bundleId: materialRequest.bundleId,
            tokenId: Buffer.alloc(16, 0x22),
            clientNonce: materialRequest.clientNonce,
            installFingerprint: identity.fingerprint,
            issuedAt,
            expiresAt: issuedAt + 30_000,
            contentKey: Buffer.alloc(32, 0x33)
          });
          const ciphertext = publicEncrypt({
            key: rsaPublicFromWire(materialRequest.publicKey),
            padding: constants.RSA_PKCS1_OAEP_PADDING,
            oaepHash: 'sha256'
          }, plaintext);
          const responseBody = encodeRuntimeMaterialResponseBody({
            bundleId: materialRequest.bundleId,
            tokenId: Buffer.alloc(16, 0x22),
            clientNonce: materialRequest.clientNonce,
            issuedAt,
            expiresAt: issuedAt + 30_000,
            ciphertext
          });
          activeSocket.emit('data', encodeMessagePacket({
            msgId: 3, body: responseBody, key: CONFIG_KEY, encrypt: true, randomBytes: fixedRandom
          }));
          plaintext.fill(0);
          ciphertext.fill(0);
          responseBody.fill(0);
          materialRequest.bundleId.fill(0);
          materialRequest.clientNonce.fill(0);
          materialRequest.publicKey.fill(0);
        }
      });
      return socket;
    };

    await assert.rejects(
      requestPreloadRuntimeMaterial({
        host: '127.0.0.1', port: 28880, timeoutMs: 1000, createConnection,
        runtimeMaterial: {
          required: true, bundleId: Buffer.alloc(32, 0x11), ...identity,
          decrypt: identity.decrypt, randomBytes: () => Buffer.alloc(16, 0x77)
        }
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'RUNTIME_MATERIAL_INVALID'
    );
    assert.equal(socket.writes.length, 1);
    assert.equal(decodeMessagePacket(socket.writes[0], { key: CONFIG_KEY }).msgId, 3);
  });

  it('keeps the authenticated socket open for msgId 3 and returns one-time runtime material', async () => {
    const identity = runtimeIdentity();
    const bundleId = Buffer.from('11'.repeat(32), 'hex');
    const tokenId = Buffer.from('22'.repeat(16), 'hex');
    const contentKey = Buffer.from('33'.repeat(32), 'hex');
    const gameKey = Buffer.from('44'.repeat(16), 'hex');
    const issuedAt = 1_750_000_000_000;
    let socket;
    const createConnection = () => {
      socket = new FakeSocket({
        onWrite(request, activeSocket) {
          const decoded = decodeMessagePacket(request, { key: CONFIG_KEY });
          if (decoded.msgId === 1) {
            activeSocket.emit('data', successResponse(246810, gameKey));
            return;
          }
          assert.equal(decoded.msgId, 3);
          const materialRequest = parseRuntimeMaterialRequestBody(decoded.body);
          const plaintext = encodeRuntimeMaterialPlaintext({
            uin: 246810,
            bundleId: materialRequest.bundleId,
            tokenId,
            clientNonce: materialRequest.clientNonce,
            installFingerprint: identity.fingerprint,
            issuedAt,
            expiresAt: issuedAt + 30_000,
            contentKey
          });
          const ciphertext = publicEncrypt({
            key: rsaPublicFromWire(materialRequest.publicKey),
            padding: constants.RSA_PKCS1_OAEP_PADDING,
            oaepHash: 'sha256'
          }, plaintext);
          const responseBody = encodeRuntimeMaterialResponseBody({
            bundleId: materialRequest.bundleId,
            tokenId,
            clientNonce: materialRequest.clientNonce,
            issuedAt,
            expiresAt: issuedAt + 30_000,
            ciphertext
          });
          activeSocket.emit('data', encodeMessagePacket({
            msgId: 3,
            body: responseBody,
            key: CONFIG_KEY,
            encrypt: true,
            randomBytes: fixedRandom
          }));
          plaintext.fill(0);
          ciphertext.fill(0);
          responseBody.fill(0);
          materialRequest.bundleId.fill(0);
          materialRequest.clientNonce.fill(0);
          materialRequest.publicKey.fill(0);
        }
      });
      return socket;
    };

    const result = await loginPrivateServer({
      host: '127.0.0.1',
      port: 28880,
      username: 'fixture-user',
      password: 'fixture-password',
      timeoutMs: 1000,
      createConnection,
      randomBytes: fixedRandom,
      runtimeMaterial: {
        required: true,
        bundleId,
        ...identity,
        decrypt: identity.decrypt,
        randomBytes: () => Buffer.alloc(16, 0x77),
        now: () => issuedAt + 1_000
      }
    });
    try {
      assert.equal(socket.writes.length, 2);
      assert.equal(decodeMessagePacket(socket.writes[0], { key: CONFIG_KEY }).msgId, 1);
      assert.equal(decodeMessagePacket(socket.writes[1], { key: CONFIG_KEY }).msgId, 3);
      assert.equal(socket.writes[0].readUInt32BE(8), 0);
      assert.equal(socket.writes[1].readUInt32BE(8), 1);
      assert.equal(result.uin, 246810);
      assert.deepEqual(result.gameKey, gameKey);
      assert.deepEqual(result.runtimeMaterial.contentKey, contentKey);
      assert.deepEqual(result.runtimeMaterial.bundleId, bundleId);
      assert.deepEqual(result.runtimeMaterial.tokenId, tokenId);
    } finally {
      result.gameKey.fill(0);
      result.runtimeMaterial.contentKey.fill(0);
      result.runtimeMaterial.bundleId.fill(0);
      result.runtimeMaterial.tokenId.fill(0);
    }
  });

  it('reports an explicit server-upgrade error when legacy server closes after msgId 1', async () => {
    const identity = runtimeIdentity();
    const createConnection = () => new FakeSocket({
      onWrite(request, socket) {
        const decoded = decodeMessagePacket(request, { key: CONFIG_KEY });
        if (decoded.msgId === 1) {
          socket.emit('data', successResponse(246810, Buffer.alloc(16, 0x44)));
        } else {
          socket.emit('close');
        }
      }
    });

    await assert.rejects(
      () => loginPrivateServer({
        host: '127.0.0.1', port: 28880, username: 'fixture-user', password: 'fixture-password',
        timeoutMs: 1000, createConnection, randomBytes: fixedRandom,
        runtimeMaterial: {
          required: true,
          bundleId: Buffer.alloc(32, 0x11),
          ...identity,
          decrypt: identity.decrypt,
          randomBytes: () => Buffer.alloc(16, 0x77)
        }
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'RUNTIME_MATERIAL_UNAVAILABLE'
    );
  });

  it('handles a fragmented response and returns only native handoff material', async () => {
    const username = 'fixture-user';
    const password = 'fixture-password';
    const gameKey = Buffer.from('102132435465768798a9bacbdcedfe0f', 'hex');
    let socket;
    const createConnection = () => {
      socket = new FakeSocket({
        onWrite(_request, activeSocket) {
          const response = successResponse(246810, gameKey);
          activeSocket.emit('data', response.subarray(0, 3));
          activeSocket.emit('data', response.subarray(3, 19));
          activeSocket.emit('data', response.subarray(19));
        }
      });
      return socket;
    };

    const result = await loginPrivateServer({
      host: '127.0.0.1',
      port: 28880,
      username,
      password,
      passwordMode: 'md5',
      timeoutMs: 1000,
      createConnection,
      randomBytes: fixedRandom
    });

    assert.deepEqual(Object.keys(result).sort(), ['gameKey', 'signKey', 'uin']);
    assert.equal(result.uin, 246810);
    assert.deepEqual(result.gameKey, gameKey);
    assert.equal(result.signKey, hashLoginPassword(password));
    assert.equal(socket.destroyed, true);

    const request = decodeMessagePacket(socket.writes[0], { key: CONFIG_KEY });
    assert.equal(request.msgId, 1);
    const usernameLength = request.body[0];
    assert.equal(request.body.subarray(1, 1 + usernameLength).toString(), username);
    const digestLength = request.body[1 + usernameLength];
    assert.equal(digestLength, 32);
    assert.equal(
      request.body.subarray(2 + usernameLength, 2 + usernameLength + digestLength).toString('ascii'),
      hashLoginPassword(password)
    );
    assert.equal(request.body.includes(Buffer.from(password)), false);
  });

  it('uses the original wire password and sign key when passwordMode is plain', async () => {
    const username = 'plain-user';
    const password = 'plain-password';
    const gameKey = Buffer.from('00112233445566778899aabbccddeeff', 'hex');
    let requestPacket;
    const createConnection = () => new FakeSocket({
      onWrite(request, socket) {
        requestPacket = Buffer.from(request);
        socket.emit('data', successResponse(135790, gameKey));
      }
    });

    const result = await loginPrivateServer({
      host: '127.0.0.1',
      port: 28880,
      username,
      password,
      passwordMode: 'plain',
      timeoutMs: 1000,
      createConnection,
      randomBytes: fixedRandom
    });

    assert.equal(result.signKey, password);
    const request = decodeMessagePacket(requestPacket, { key: CONFIG_KEY });
    const usernameLength = request.body[0];
    const passwordLength = request.body[1 + usernameLength];
    assert.equal(passwordLength, Buffer.byteLength(password));
    assert.equal(
      request.body.subarray(2 + usernameLength, 2 + usernameLength + passwordLength).toString(),
      password
    );
  });

  it('rejects unsupported password modes before opening a socket', async () => {
    let connectionAttempts = 0;
    await assert.rejects(
      loginPrivateServer({
        host: '127.0.0.1',
        port: 28880,
        username: 'fixture-user',
        password: 'fixture-password',
        passwordMode: 'unsupported',
        createConnection() {
          connectionAttempts += 1;
          return new FakeSocket();
        }
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'INVALID_INPUT'
    );
    assert.equal(connectionAttempts, 0);
  });

  it('maps server UIN sentinels to stable credential errors', async () => {
    const createConnection = () => new FakeSocket({
      onWrite(_request, socket) {
        const body = Buffer.alloc(6);
        body.writeUInt32BE(0, 2);
        socket.emit('data', encodeMessagePacket({
          msgId: 1,
          body,
          key: CONFIG_KEY,
          encrypt: true,
          randomBytes: fixedRandom
        }));
      }
    });

    await assert.rejects(
      loginPrivateServer({
        host: '127.0.0.1',
        port: 28880,
        username: 'fixture-user',
        password: 'fixture-password',
        createConnection,
        timeoutMs: 1000,
        randomBytes: fixedRandom
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'INVALID_CREDENTIALS'
    );
  });

  it('shows a clear customer-facing ban message for the unavailable-account sentinel', async () => {
    const createConnection = () => new FakeSocket({
      onWrite(_request, socket) {
        const body = Buffer.alloc(6);
        body.writeUInt32BE(1, 2);
        socket.emit('data', encodeMessagePacket({
          msgId: 1,
          body,
          key: CONFIG_KEY,
          encrypt: true,
          randomBytes: fixedRandom
        }));
      }
    });

    await assert.rejects(
      loginPrivateServer({
        host: '127.0.0.1',
        port: 28880,
        username: 'banned-user',
        password: 'fixture-password',
        createConnection,
        timeoutMs: 1000,
        randomBytes: fixedRandom
      }),
      (error) => error instanceof PrivateLoginError &&
        error.code === 'ACCOUNT_UNAVAILABLE' &&
        error.message === '该账号已被封禁，如有疑问请联系管理员。'
    );
  });

  it('honors an already-aborted signal without opening a socket', async () => {
    const controller = new AbortController();
    controller.abort();
    let connectionAttempts = 0;

    await assert.rejects(
      loginPrivateServer({
        host: '127.0.0.1',
        port: 28880,
        username: 'fixture-user',
        password: 'fixture-password',
        signal: controller.signal,
        createConnection() {
          connectionAttempts += 1;
          return new FakeSocket();
        }
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'ABORTED'
    );
    assert.equal(connectionAttempts, 0);
  });

  it('propagates cancellation while connecting and destroys the socket', async () => {
    const controller = new AbortController();
    let socket;
    const pending = loginPrivateServer({
      host: '127.0.0.1',
      port: 28880,
      username: 'fixture-user',
      password: 'fixture-password',
      signal: controller.signal,
      timeoutMs: 1000,
      createConnection() {
        socket = new FakeSocket({ connect: false });
        return socket;
      }
    });

    controller.abort();
    await assert.rejects(
      pending,
      (error) => error instanceof PrivateLoginError && error.code === 'ABORTED'
    );
    assert.equal(socket.destroyed, true);
  });

  it('times out a connection and destroys its socket', async () => {
    let socket;
    const createConnection = () => {
      socket = new FakeSocket({ connect: false });
      return socket;
    };

    await assert.rejects(
      loginPrivateServer({
        host: '127.0.0.1',
        port: 28880,
        username: 'fixture-user',
        password: 'fixture-password',
        timeoutMs: 20,
        createConnection
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'TIMEOUT'
    );
    assert.equal(socket.destroyed, true);
  });

  it('rejects invalid inputs before opening a socket', async () => {
    let connectionAttempts = 0;
    await assert.rejects(
      loginPrivateServer({
        host: '',
        port: 70000,
        username: '',
        password: '',
        createConnection() {
          connectionAttempts += 1;
          return new FakeSocket();
        }
      }),
      (error) => error instanceof PrivateLoginError && error.code === 'INVALID_INPUT'
    );
    assert.equal(connectionAttempts, 0);
  });
});
