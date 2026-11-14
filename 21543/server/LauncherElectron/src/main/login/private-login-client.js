import { createConnection as createNetConnection } from 'node:net';
import { randomBytes as cryptoRandomBytes } from 'node:crypto';

import {
  LoginPacketFramer,
  ProtocolError,
  decodeMessagePacket,
  encodeLoginRequestBody,
  encodeMessagePacket,
  hashLoginPassword,
  parseLoginResponseBody
} from './private-protocol.js';
import {
  RuntimeMaterialError,
  encodePreloadRuntimeMaterialRequestBody,
  encodeRuntimeMaterialRequestBody,
  unwrapPreloadRuntimeMaterialResponseBody,
  unwrapRuntimeMaterialResponseBody
} from '../runtime-material-envelope.js';

const CONFIG_KEY_BYTES = Buffer.from([
  0x66, 0x5f, 0xae, 0x33, 0x44, 0xdf, 0x99, 0x11,
  0x88, 0x99, 0x00, 0xaa, 0x6d, 0x78, 0xdd, 0xee
]);
const DEFAULT_TIMEOUT_MS = 30_000;

export const CONFIG_KEY = Buffer.from(CONFIG_KEY_BYTES);

export class PrivateLoginError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'PrivateLoginError';
    this.code = code;
  }
}

function validateOptions({ host, port, username, password, passwordMode = 'md5', timeoutMs }) {
  const normalizedHost = typeof host === 'string' ? host.trim() : '';
  const normalizedUsername = typeof username === 'string' ? username.trim() : '';
  const normalizedTimeout = timeoutMs ?? DEFAULT_TIMEOUT_MS;
  if (
    !normalizedHost ||
    normalizedHost.length > 253 ||
    normalizedHost.includes('\0') ||
    !Number.isInteger(port) ||
    port < 1 ||
    port > 0xffff ||
    !normalizedUsername ||
    normalizedUsername.includes('\0') ||
    typeof password !== 'string' ||
    !password ||
    password.includes('\0') ||
    (passwordMode !== 'plain' && passwordMode !== 'md5') ||
    !Number.isInteger(normalizedTimeout) ||
    normalizedTimeout < 1 ||
    normalizedTimeout > 120_000
  ) {
    throw new PrivateLoginError('INVALID_INPUT', '登录参数无效。');
  }
  if (Buffer.byteLength(normalizedUsername, 'utf8') > 64 || Buffer.byteLength(password, 'utf8') > 64) {
    throw new PrivateLoginError('INVALID_INPUT', '账号或密码超出长度限制。');
  }
  return {
    host: normalizedHost,
    port,
    username: normalizedUsername,
    password,
    passwordMode,
    timeoutMs: normalizedTimeout
  };
}

function loginError(code) {
  switch (code) {
    case 'INVALID_CREDENTIALS':
      return new PrivateLoginError(code, '账号或密码错误。');
    case 'ACCOUNT_UNAVAILABLE':
      return new PrivateLoginError(code, '该账号已被封禁，如有疑问请联系管理员。');
    case 'TIMEOUT':
      return new PrivateLoginError(code, '连接登录服务器超时。');
    case 'ABORTED':
      return new PrivateLoginError(code, '登录已取消。');
    case 'PROTOCOL_ERROR':
      return new PrivateLoginError(code, '登录服务器返回了无效数据。');
    case 'RUNTIME_MATERIAL_UNAVAILABLE':
      return new PrivateLoginError(code, '登录服务器尚未提供本版本所需的运行材料，请升级服务器。');
    case 'RUNTIME_MATERIAL_EXPIRED':
      return new PrivateLoginError(code, '登录服务器下发的运行材料已过期，请重新登录。');
    case 'RUNTIME_MATERIAL_INVALID':
      return new PrivateLoginError(code, '登录服务器下发的运行材料无效。');
    case 'RUNTIME_MATERIAL_UNSUPPORTED':
      return new PrivateLoginError(code, '登录服务器不支持预登录运行材料协议，请升级服务器。');
    default:
      return new PrivateLoginError('NETWORK_ERROR', '无法连接登录服务器。');
  }
}

function validateEndpoint({ host, port, timeoutMs }) {
  const normalizedHost = typeof host === 'string' ? host.trim() : '';
  const normalizedTimeout = timeoutMs ?? DEFAULT_TIMEOUT_MS;
  if (!normalizedHost || normalizedHost.length > 253 || normalizedHost.includes('\0') ||
      !Number.isInteger(port) || port < 1 || port > 0xffff ||
      !Number.isInteger(normalizedTimeout) || normalizedTimeout < 1 || normalizedTimeout > 120_000) {
    throw new PrivateLoginError('INVALID_INPUT', '登录参数无效。');
  }
  return { host: normalizedHost, port, timeoutMs: normalizedTimeout };
}

function copyExact(value, length, label) {
  if ((!Buffer.isBuffer(value) && !(value instanceof Uint8Array)) || value.length !== length) {
    throw new PrivateLoginError('INVALID_INPUT', `${label}参数无效。`);
  }
  return Buffer.from(value);
}

function normalizeRuntimeMaterialOptions(value) {
  if (value === undefined || value === null) return null;
  if (typeof value !== 'object' || value.required !== true ||
      value.algorithm !== 'rsa-oaep-sha256' ||
      typeof value.decrypt !== 'function' ||
      (value.randomBytes !== undefined && typeof value.randomBytes !== 'function') ||
      (value.now !== undefined && typeof value.now !== 'function')) {
    throw new PrivateLoginError('INVALID_INPUT', '运行材料参数无效。');
  }
  const bundleId = copyExact(value.bundleId, 32, 'bundleId');
  const publicKey = copyExact(value.publicKey, 259, 'publicKey');
  const fingerprint = copyExact(value.fingerprint, 32, 'fingerprint');
  if (!publicKey.subarray(0, 3).equals(Buffer.from([0x01, 0x00, 0x01]))) {
    bundleId.fill(0);
    publicKey.fill(0);
    fingerprint.fill(0);
    throw new PrivateLoginError('INVALID_INPUT', '运行材料公钥无效。');
  }
  return {
    required: true,
    algorithm: value.algorithm,
    bundleId,
    publicKey,
    fingerprint,
    decrypt: value.decrypt,
    randomBytes: value.randomBytes ?? cryptoRandomBytes,
    now: value.now ?? Date.now
  };
}

export function requestPreloadRuntimeMaterial(options) {
  let endpoint;
  let runtimeMaterial;
  try {
    endpoint = validateEndpoint(options ?? {});
    runtimeMaterial = normalizeRuntimeMaterialOptions(options?.runtimeMaterial);
    if (!runtimeMaterial) {
      throw new PrivateLoginError('INVALID_INPUT', '运行材料参数无效。');
    }
  } catch (error) {
    return Promise.reject(error);
  }

  const signal = options?.signal;
  const createConnection = options?.createConnection ?? createNetConnection;
  const randomBytes = options?.randomBytes;
  if (typeof createConnection !== 'function' || (signal !== undefined && !(signal instanceof AbortSignal))) {
    runtimeMaterial.bundleId.fill(0);
    runtimeMaterial.publicKey.fill(0);
    runtimeMaterial.fingerprint.fill(0);
    return Promise.reject(new PrivateLoginError('INVALID_INPUT', '登录参数无效。'));
  }
  if (signal?.aborted) {
    runtimeMaterial.bundleId.fill(0);
    runtimeMaterial.publicKey.fill(0);
    runtimeMaterial.fingerprint.fill(0);
    return Promise.reject(loginError('ABORTED'));
  }

  let clientNonce;
  let requestPacket;
  try {
    clientNonce = copyExact(runtimeMaterial.randomBytes(16), 16, 'clientNonce');
    const requestBody = encodePreloadRuntimeMaterialRequestBody({
      bundleId: runtimeMaterial.bundleId,
      clientNonce,
      publicKey: runtimeMaterial.publicKey
    });
    try {
      requestPacket = encodeMessagePacket({
        msgId: 3,
        body: requestBody,
        key: CONFIG_KEY_BYTES,
        encrypt: true,
        randomBytes,
        // Server-side anti-packet-spoofing starts HeadSeq at zero. Unlike the
        // login message, anonymous MsgID 3 is not a sequence-reset message, so
        // its first packet must advance the connection sequence to one.
        protocolSequence: 1
      });
    } finally {
      requestBody.fill(0);
    }
  } catch (error) {
    clientNonce?.fill(0);
    requestPacket?.fill(0);
    runtimeMaterial.bundleId.fill(0);
    runtimeMaterial.publicKey.fill(0);
    runtimeMaterial.fingerprint.fill(0);
    return Promise.reject(error instanceof PrivateLoginError
      ? error
      : new PrivateLoginError('INVALID_INPUT', '运行材料参数无效。'));
  }

  return new Promise((resolve, reject) => {
    const framer = new LoginPacketFramer();
    let socket;
    let timer;
    let settled = false;
    let requestSent = false;

    const removeListeners = () => {
      signal?.removeEventListener('abort', onAbort);
      socket?.off?.('connect', onConnect);
      socket?.off?.('data', onData);
      socket?.off?.('error', onSocketError);
      socket?.off?.('close', onClose);
    };
    const dispose = () => {
      if (timer) clearTimeout(timer);
      timer = undefined;
      removeListeners();
      framer.reset();
      requestPacket.fill(0);
      clientNonce.fill(0);
      runtimeMaterial.bundleId.fill(0);
      runtimeMaterial.publicKey.fill(0);
      runtimeMaterial.fingerprint.fill(0);
    };
    const fail = (error) => {
      if (settled) return;
      settled = true;
      dispose();
      try { socket?.destroy?.(); } catch { /* Best-effort close. */ }
      reject(error);
    };
    const succeed = (result) => {
      if (settled) return;
      settled = true;
      dispose();
      try { socket?.end?.(); } catch {
        try { socket?.destroy?.(); } catch { /* Result is already complete. */ }
      }
      resolve(result);
    };

    function onAbort() {
      fail(loginError('ABORTED'));
    }
    function onConnect() {
      requestSent = true;
      try {
        socket.write(requestPacket, (error) => {
          if (error) fail(loginError('NETWORK_ERROR'));
        });
      } catch {
        fail(loginError('NETWORK_ERROR'));
      }
    }
    async function onData(chunk) {
      if (settled) return;
      try {
        const packets = framer.push(chunk);
        try {
          for (const packet of packets) {
            let message;
            try {
              message = decodeMessagePacket(packet, { key: CONFIG_KEY_BYTES });
              if (message.msgId !== 3) {
                fail(loginError('RUNTIME_MATERIAL_INVALID'));
                return;
              }
              let material;
              try {
                material = await unwrapPreloadRuntimeMaterialResponseBody({
                  body: message.body,
                  expectedBundleId: runtimeMaterial.bundleId,
                  expectedClientNonce: clientNonce,
                  identity: runtimeMaterial,
                  decrypt: runtimeMaterial.decrypt,
                  now: runtimeMaterial.now
                });
              } catch (error) {
                fail(loginError('RUNTIME_MATERIAL_INVALID'));
                return;
              }
              succeed(material);
              return;
            } finally {
              message?.body.fill(0);
            }
          }
        } finally {
          for (const packet of packets) packet.fill(0);
        }
      } catch (error) {
        fail(error instanceof ProtocolError
          ? loginError('RUNTIME_MATERIAL_INVALID')
          : loginError('NETWORK_ERROR'));
      }
    }
    function onSocketError() {
      fail(loginError('NETWORK_ERROR'));
    }
    function onClose() {
      fail(loginError(requestSent ? 'RUNTIME_MATERIAL_UNSUPPORTED' : 'NETWORK_ERROR'));
    }

    signal?.addEventListener('abort', onAbort, { once: true });
    timer = setTimeout(
      () => fail(loginError('NETWORK_ERROR')),
      endpoint.timeoutMs
    );

    try {
      socket = createConnection({ host: endpoint.host, port: endpoint.port });
      if (!socket || typeof socket.on !== 'function' || typeof socket.write !== 'function') {
        throw new TypeError('createConnection did not return a socket-like object.');
      }
      socket.once('connect', onConnect);
      socket.on('data', onData);
      socket.once('error', onSocketError);
      socket.once('close', onClose);
    } catch {
      fail(loginError('NETWORK_ERROR'));
    }
  });
}

export function loginPrivateServer(options) {
  let validated;
  let runtimeMaterial;
  try {
    validated = validateOptions(options ?? {});
    runtimeMaterial = normalizeRuntimeMaterialOptions(options?.runtimeMaterial);
  } catch (error) {
    return Promise.reject(error);
  }

  const {
    host,
    port,
    username,
    password,
    passwordMode,
    timeoutMs
  } = validated;
  const signal = options?.signal;
  const createConnection = options?.createConnection ?? createNetConnection;
  const randomBytes = options?.randomBytes;
  if (typeof createConnection !== 'function' || (signal !== undefined && !(signal instanceof AbortSignal))) {
    return Promise.reject(new PrivateLoginError('INVALID_INPUT', '登录参数无效。'));
  }
  if (signal?.aborted) {
    return Promise.reject(loginError('ABORTED'));
  }

  let signKey;
  let requestBody;
  let requestPacket;
  let materialRequestPacket = null;
  let clientNonce = null;
  try {
    signKey = passwordMode === 'md5' ? hashLoginPassword(password) : password;
    requestBody = encodeLoginRequestBody(username, signKey);
    requestPacket = encodeMessagePacket({
      msgId: 1,
      body: requestBody,
      key: CONFIG_KEY_BYTES,
      encrypt: true,
      randomBytes,
      protocolSequence: 0
    });
    if (runtimeMaterial) {
      clientNonce = copyExact(runtimeMaterial.randomBytes(16), 16, 'clientNonce');
      const materialBody = encodeRuntimeMaterialRequestBody({
        bundleId: runtimeMaterial.bundleId,
        clientNonce,
        publicKey: runtimeMaterial.publicKey
      });
      try {
        materialRequestPacket = encodeMessagePacket({
          msgId: 3,
          body: materialBody,
          key: CONFIG_KEY_BYTES,
          encrypt: true,
          randomBytes,
          protocolSequence: 1
        });
      } finally {
        materialBody.fill(0);
      }
    }
  } catch (error) {
    requestBody?.fill(0);
    requestPacket?.fill(0);
    materialRequestPacket?.fill(0);
    clientNonce?.fill(0);
    runtimeMaterial?.bundleId.fill(0);
    runtimeMaterial?.publicKey.fill(0);
    runtimeMaterial?.fingerprint.fill(0);
    return Promise.reject(
      error instanceof PrivateLoginError
        ? error
        : new PrivateLoginError('INVALID_INPUT', '登录参数无效。')
    );
  } finally {
    requestBody?.fill(0);
  }

  return new Promise((resolve, reject) => {
    const framer = new LoginPacketFramer();
    let socket;
    let settled = false;
    let timer;
    let awaitingRuntimeMaterial = false;
    let pendingLogin = null;

    const removeListeners = () => {
      signal?.removeEventListener('abort', onAbort);
      socket?.off?.('connect', onConnect);
      socket?.off?.('data', onData);
      socket?.off?.('error', onSocketError);
      socket?.off?.('close', onClose);
    };

    const dispose = () => {
      if (timer) {
        clearTimeout(timer);
        timer = undefined;
      }
      removeListeners();
      framer.reset();
      requestPacket.fill(0);
      materialRequestPacket?.fill(0);
      clientNonce?.fill(0);
      runtimeMaterial?.bundleId.fill(0);
      runtimeMaterial?.publicKey.fill(0);
      runtimeMaterial?.fingerprint.fill(0);
    };

    const fail = (error) => {
      if (settled) return;
      settled = true;
      pendingLogin?.gameKey.fill(0);
      pendingLogin = null;
      dispose();
      try {
        socket?.destroy?.();
      } catch {
        // Best-effort close; the stable error below remains authoritative.
      }
      reject(error);
    };

    const succeed = (result) => {
      if (settled) return;
      settled = true;
      dispose();
      try {
        socket?.end?.();
      } catch {
        try {
          socket?.destroy?.();
        } catch {
          // The result is already complete.
        }
      }
      resolve(result);
    };

    function onAbort() {
      fail(loginError('ABORTED'));
    }

    function onConnect() {
      try {
        socket.write(requestPacket, (error) => {
          if (error) fail(loginError('NETWORK_ERROR'));
        });
      } catch {
        fail(loginError('NETWORK_ERROR'));
      }
    }

    async function onData(chunk) {
      if (settled) return;
      try {
        const packets = framer.push(chunk);
        try {
          for (const packet of packets) {
            let message;
            try {
              message = decodeMessagePacket(packet, { key: CONFIG_KEY_BYTES });
              if (message.msgId !== 1 && message.msgId !== 3) {
                continue;
              }
              if (message.msgId === 1) {
                if (pendingLogin) continue;
                const response = parseLoginResponseBody(message.body);
                if (response.kind === 'invalid-credentials') {
                  fail(loginError('INVALID_CREDENTIALS'));
                  return;
                }
                if (response.kind === 'account-unavailable') {
                  fail(loginError('ACCOUNT_UNAVAILABLE'));
                  return;
                }
                if (!runtimeMaterial) {
                  succeed({
                    uin: response.uin,
                    gameKey: response.gameKey,
                    signKey
                  });
                  return;
                }
                pendingLogin = {
                  uin: response.uin,
                  gameKey: response.gameKey
                };
                awaitingRuntimeMaterial = true;
                try {
                  socket.write(materialRequestPacket, (error) => {
                    if (error) fail(loginError('RUNTIME_MATERIAL_UNAVAILABLE'));
                  });
                } catch {
                  fail(loginError('RUNTIME_MATERIAL_UNAVAILABLE'));
                }
                continue;
              }

              if (!runtimeMaterial || !pendingLogin) continue;
              let material;
              try {
                material = await unwrapRuntimeMaterialResponseBody({
                  body: message.body,
                  uin: pendingLogin.uin,
                  expectedBundleId: runtimeMaterial.bundleId,
                  expectedClientNonce: clientNonce,
                  identity: runtimeMaterial,
                  decrypt: runtimeMaterial.decrypt,
                  now: runtimeMaterial.now
                });
              } catch (error) {
                if (error instanceof RuntimeMaterialError && error.code === 'RUNTIME_MATERIAL_EXPIRED') {
                  fail(loginError('RUNTIME_MATERIAL_EXPIRED'));
                } else {
                  fail(loginError('RUNTIME_MATERIAL_INVALID'));
                }
                return;
              }
              awaitingRuntimeMaterial = false;
              const completedLogin = pendingLogin;
              pendingLogin = null;
              succeed({
                uin: completedLogin.uin,
                gameKey: completedLogin.gameKey,
                signKey,
                runtimeMaterial: material
              });
              return;
            } finally {
              message?.body.fill(0);
            }
          }
        } finally {
          for (const packet of packets) packet.fill(0);
        }
      } catch (error) {
        fail(error instanceof ProtocolError
          ? loginError(awaitingRuntimeMaterial ? 'RUNTIME_MATERIAL_INVALID' : 'PROTOCOL_ERROR')
          : loginError(awaitingRuntimeMaterial ? 'RUNTIME_MATERIAL_UNAVAILABLE' : 'NETWORK_ERROR'));
      }
    }

    function onSocketError() {
      fail(loginError(awaitingRuntimeMaterial ? 'RUNTIME_MATERIAL_UNAVAILABLE' : 'NETWORK_ERROR'));
    }

    function onClose() {
      fail(loginError(awaitingRuntimeMaterial ? 'RUNTIME_MATERIAL_UNAVAILABLE' : 'NETWORK_ERROR'));
    }

    signal?.addEventListener('abort', onAbort, { once: true });
    timer = setTimeout(
      () => fail(loginError(awaitingRuntimeMaterial ? 'RUNTIME_MATERIAL_UNAVAILABLE' : 'TIMEOUT')),
      timeoutMs
    );

    try {
      socket = createConnection({ host, port });
      if (!socket || typeof socket.on !== 'function' || typeof socket.write !== 'function') {
        throw new TypeError('createConnection did not return a socket-like object.');
      }
      socket.once('connect', onConnect);
      socket.on('data', onData);
      socket.once('error', onSocketError);
      socket.once('close', onClose);
    } catch {
      fail(loginError('NETWORK_ERROR'));
    }
  });
}
