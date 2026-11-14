import { timingSafeEqual } from 'node:crypto';

export const RUNTIME_MATERIAL_MSG_ID = 3;
export const RUNTIME_MATERIAL_VERSION = 1;
export const PRELOAD_RUNTIME_MATERIAL_VERSION = 2;
export const RUNTIME_MATERIAL_ALGORITHM = 1;
export const RUNTIME_MATERIAL_REQUEST_SIZE = 313;
export const RUNTIME_MATERIAL_RESPONSE_SIZE = 342;

const PUBLIC_KEY_SIZE = 259;
const FINGERPRINT_SIZE = 32;
const BUNDLE_ID_SIZE = 32;
const NONCE_SIZE = 16;
const TOKEN_ID_SIZE = 16;
const CONTENT_KEY_SIZE = 32;
const RSA_CIPHERTEXT_SIZE = 256;
const PLAINTEXT_MAGIC_V1 = Buffer.from('QQSRMAT1', 'ascii');
const PLAINTEXT_MAGIC_V2 = Buffer.from('QQSRMAT2', 'ascii');
const PLAINTEXT_SIZE = 160;
const MAX_LIFETIME_MS = 120_000;

export class RuntimeMaterialError extends Error {
  constructor(code, message, options = {}) {
    super(message, options);
    this.name = 'RuntimeMaterialError';
    this.code = code;
  }
}

function copyExact(value, length, label) {
  if (!Buffer.isBuffer(value) && !(value instanceof Uint8Array)) {
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', `${label} must be binary.`);
  }
  const bytes = Buffer.from(value);
  if (bytes.length !== length) {
    bytes.fill(0);
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', `${label} must be exactly ${length} bytes.`);
  }
  return bytes;
}

function validatePublicKey(value) {
  const publicKey = copyExact(value, PUBLIC_KEY_SIZE, 'Install public key');
  if (!publicKey.subarray(0, 3).equals(Buffer.from([0x01, 0x00, 0x01])) ||
      publicKey.subarray(3).every((byte) => byte === 0)) {
    publicKey.fill(0);
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Install public key is invalid.');
  }
  return publicKey;
}

function validateUin(value) {
  if (!Number.isInteger(value) || value <= 10_000 || value > 0xffff_ffff) {
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Authenticated UIN is invalid.');
  }
  return value;
}

function validateTimestamp(value, label) {
  if (!Number.isSafeInteger(value) || value <= 0) {
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', `${label} is invalid.`);
  }
  return value;
}

function writeTimestamp(buffer, offset, value, label) {
  buffer.writeBigUInt64BE(BigInt(validateTimestamp(value, label)), offset);
}

function readTimestamp(buffer, offset, label) {
  const value = Number(buffer.readBigUInt64BE(offset));
  return validateTimestamp(value, label);
}

function exactEqual(left, right) {
  return left.length === right.length && timingSafeEqual(left, right);
}

function encodeRequestBody({ bundleId, clientNonce, publicKey }, version) {
  const id = copyExact(bundleId, BUNDLE_ID_SIZE, 'Bundle id');
  const nonce = copyExact(clientNonce, NONCE_SIZE, 'Client nonce');
  const key = validatePublicKey(publicKey);
  try {
    const body = Buffer.alloc(RUNTIME_MATERIAL_REQUEST_SIZE);
    body.writeUInt8(version, 0);
    body.writeUInt8(RUNTIME_MATERIAL_ALGORITHM, 1);
    body.writeUInt16BE(0, 2);
    id.copy(body, 4);
    nonce.copy(body, 36);
    body.writeUInt16BE(PUBLIC_KEY_SIZE, 52);
    key.copy(body, 54);
    return body;
  } finally {
    id.fill(0);
    nonce.fill(0);
    key.fill(0);
  }
}

export function encodeRuntimeMaterialRequestBody(fields) {
  return encodeRequestBody(fields, RUNTIME_MATERIAL_VERSION);
}

export function encodePreloadRuntimeMaterialRequestBody(fields) {
  return encodeRequestBody(fields, PRELOAD_RUNTIME_MATERIAL_VERSION);
}

function parseRequestBody(bodyInput, version) {
  const body = copyExact(bodyInput, RUNTIME_MATERIAL_REQUEST_SIZE, 'Runtime material request length');
  try {
    if (body.readUInt8(0) !== version ||
        body.readUInt8(1) !== RUNTIME_MATERIAL_ALGORITHM ||
        body.readUInt16BE(2) !== 0 ||
        body.readUInt16BE(52) !== PUBLIC_KEY_SIZE) {
      throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Runtime material request header is invalid.');
    }
    const publicKey = validatePublicKey(body.subarray(54));
    return {
      bundleId: Buffer.from(body.subarray(4, 36)),
      clientNonce: Buffer.from(body.subarray(36, 52)),
      publicKey
    };
  } finally {
    body.fill(0);
  }
}

export function parseRuntimeMaterialRequestBody(body) {
  return parseRequestBody(body, RUNTIME_MATERIAL_VERSION);
}

export function parsePreloadRuntimeMaterialRequestBody(body) {
  return parseRequestBody(body, PRELOAD_RUNTIME_MATERIAL_VERSION);
}

function encodePlaintext({
  subject,
  bundleId,
  tokenId,
  clientNonce,
  installFingerprint,
  issuedAt,
  expiresAt,
  contentKey
}, version, magic) {
  const id = copyExact(bundleId, BUNDLE_ID_SIZE, 'Bundle id');
  const token = copyExact(tokenId, TOKEN_ID_SIZE, 'Token id');
  const nonce = copyExact(clientNonce, NONCE_SIZE, 'Client nonce');
  const fingerprint = copyExact(installFingerprint, FINGERPRINT_SIZE, 'Install fingerprint');
  const key = copyExact(contentKey, CONTENT_KEY_SIZE, 'Content key');
  if (!Number.isInteger(subject) || subject < 0 || subject > 0xffff_ffff) {
    id.fill(0); token.fill(0); nonce.fill(0); fingerprint.fill(0); key.fill(0);
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Runtime material subject is invalid.');
  }
  const issued = validateTimestamp(issuedAt, 'Issued timestamp');
  const expires = validateTimestamp(expiresAt, 'Expiry timestamp');
  if (expires <= issued || expires - issued > MAX_LIFETIME_MS) {
    id.fill(0); token.fill(0); nonce.fill(0); fingerprint.fill(0); key.fill(0);
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Runtime material lifetime is invalid.');
  }
  try {
    const plaintext = Buffer.alloc(PLAINTEXT_SIZE);
    magic.copy(plaintext, 0);
    plaintext.writeUInt8(version, 8);
    plaintext.writeUInt8(RUNTIME_MATERIAL_ALGORITHM, 9);
    plaintext.writeUInt16BE(0, 10);
    plaintext.writeUInt32BE(subject, 12);
    id.copy(plaintext, 16);
    token.copy(plaintext, 48);
    nonce.copy(plaintext, 64);
    fingerprint.copy(plaintext, 80);
    writeTimestamp(plaintext, 112, issued, 'Issued timestamp');
    writeTimestamp(plaintext, 120, expires, 'Expiry timestamp');
    key.copy(plaintext, 128);
    return plaintext;
  } finally {
    id.fill(0);
    token.fill(0);
    nonce.fill(0);
    fingerprint.fill(0);
    key.fill(0);
  }
}

export function encodeRuntimeMaterialPlaintext({ uin, ...fields }) {
  return encodePlaintext({ ...fields, subject: validateUin(uin) }, RUNTIME_MATERIAL_VERSION, PLAINTEXT_MAGIC_V1);
}

export function encodePreloadRuntimeMaterialPlaintext(fields) {
  return encodePlaintext({ ...fields, subject: 0 }, PRELOAD_RUNTIME_MATERIAL_VERSION, PLAINTEXT_MAGIC_V2);
}

function encodeResponseBody({
  bundleId,
  tokenId,
  clientNonce,
  issuedAt,
  expiresAt,
  ciphertext
}, version) {
  const id = copyExact(bundleId, BUNDLE_ID_SIZE, 'Bundle id');
  const token = copyExact(tokenId, TOKEN_ID_SIZE, 'Token id');
  const nonce = copyExact(clientNonce, NONCE_SIZE, 'Client nonce');
  const encrypted = copyExact(ciphertext, RSA_CIPHERTEXT_SIZE, 'Runtime material ciphertext');
  const issued = validateTimestamp(issuedAt, 'Issued timestamp');
  const expires = validateTimestamp(expiresAt, 'Expiry timestamp');
  if (expires <= issued || expires - issued > MAX_LIFETIME_MS) {
    id.fill(0); token.fill(0); nonce.fill(0); encrypted.fill(0);
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Runtime material lifetime is invalid.');
  }
  try {
    const body = Buffer.alloc(RUNTIME_MATERIAL_RESPONSE_SIZE);
    body.writeUInt8(version, 0);
    body.writeUInt8(RUNTIME_MATERIAL_ALGORITHM, 1);
    body.writeUInt16BE(0, 2);
    id.copy(body, 4);
    token.copy(body, 36);
    nonce.copy(body, 52);
    writeTimestamp(body, 68, issued, 'Issued timestamp');
    writeTimestamp(body, 76, expires, 'Expiry timestamp');
    body.writeUInt16BE(RSA_CIPHERTEXT_SIZE, 84);
    encrypted.copy(body, 86);
    return body;
  } finally {
    id.fill(0);
    token.fill(0);
    nonce.fill(0);
    encrypted.fill(0);
  }
}

export function encodeRuntimeMaterialResponseBody(fields) {
  return encodeResponseBody(fields, RUNTIME_MATERIAL_VERSION);
}

export function encodePreloadRuntimeMaterialResponseBody(fields) {
  return encodeResponseBody(fields, PRELOAD_RUNTIME_MATERIAL_VERSION);
}

function parseResponseBody(bodyInput, version) {
  const body = copyExact(bodyInput, RUNTIME_MATERIAL_RESPONSE_SIZE, 'Runtime material response length');
  try {
    if (body.readUInt8(0) !== version ||
        body.readUInt8(1) !== RUNTIME_MATERIAL_ALGORITHM ||
        body.readUInt16BE(2) !== 0 ||
        body.readUInt16BE(84) !== RSA_CIPHERTEXT_SIZE) {
      throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Runtime material response header is invalid.');
    }
    return {
      bundleId: Buffer.from(body.subarray(4, 36)),
      tokenId: Buffer.from(body.subarray(36, 52)),
      clientNonce: Buffer.from(body.subarray(52, 68)),
      issuedAt: readTimestamp(body, 68, 'Issued timestamp'),
      expiresAt: readTimestamp(body, 76, 'Expiry timestamp'),
      ciphertext: Buffer.from(body.subarray(86))
    };
  } finally {
    body.fill(0);
  }
}

function parsePlaintext(plaintextInput, version, magic) {
  const plaintext = copyExact(plaintextInput, PLAINTEXT_SIZE, 'Runtime material plaintext');
  try {
    if (!exactEqual(plaintext.subarray(0, 8), magic) ||
        plaintext.readUInt8(8) !== version ||
        plaintext.readUInt8(9) !== RUNTIME_MATERIAL_ALGORITHM ||
        plaintext.readUInt16BE(10) !== 0) {
      throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Runtime material plaintext header is invalid.');
    }
    return {
      subject: plaintext.readUInt32BE(12),
      bundleId: Buffer.from(plaintext.subarray(16, 48)),
      tokenId: Buffer.from(plaintext.subarray(48, 64)),
      clientNonce: Buffer.from(plaintext.subarray(64, 80)),
      installFingerprint: Buffer.from(plaintext.subarray(80, 112)),
      issuedAt: readTimestamp(plaintext, 112, 'Issued timestamp'),
      expiresAt: readTimestamp(plaintext, 120, 'Expiry timestamp'),
      contentKey: Buffer.from(plaintext.subarray(128, 160))
    };
  } finally {
    plaintext.fill(0);
  }
}

function disposeFields(value) {
  value?.bundleId?.fill(0);
  value?.tokenId?.fill(0);
  value?.clientNonce?.fill(0);
  value?.installFingerprint?.fill(0);
  value?.contentKey?.fill(0);
  value?.ciphertext?.fill(0);
}

async function unwrapResponseBody({
  body,
  expectedSubject,
  expectedBundleId,
  expectedClientNonce,
  identity,
  decrypt,
  now = Date.now
}, version, magic) {
  const requestBundleId = copyExact(expectedBundleId, BUNDLE_ID_SIZE, 'Expected bundle id');
  const requestClientNonce = copyExact(expectedClientNonce, NONCE_SIZE, 'Expected client nonce');
  if (identity?.algorithm !== 'rsa-oaep-sha256' ||
      (!Buffer.isBuffer(identity?.fingerprint) && !(identity?.fingerprint instanceof Uint8Array)) ||
      identity.fingerprint.length !== FINGERPRINT_SIZE ||
      typeof decrypt !== 'function' || typeof now !== 'function') {
    requestBundleId.fill(0);
    requestClientNonce.fill(0);
    throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Install identity or decryptor is invalid.');
  }
  const response = parseResponseBody(body, version);
  let decrypted;
  let decryptedSource;
  let plaintext;
  try {
    if (!exactEqual(response.bundleId, requestBundleId) ||
        !exactEqual(response.clientNonce, requestClientNonce)) {
      throw new RuntimeMaterialError('RUNTIME_MATERIAL_BINDING_MISMATCH', 'Runtime material request binding mismatch.');
    }
    // Freshness is enforced by the authenticated socket request timeout and the
    // per-request random nonce. Comparing server epoch time with the customer's
    // wall clock made otherwise valid responses fail on machines whose clocks
    // were a few seconds out of sync.
    if (response.expiresAt <= response.issuedAt ||
        response.expiresAt - response.issuedAt > MAX_LIFETIME_MS) {
      throw new RuntimeMaterialError('RUNTIME_MATERIAL_INVALID', 'Runtime material lifetime is invalid.');
    }
    try {
      decryptedSource = await decrypt(response.ciphertext);
      if (!Buffer.isBuffer(decryptedSource) && !(decryptedSource instanceof Uint8Array)) {
        throw new TypeError('Decryptor returned non-binary data.');
      }
      decrypted = Buffer.from(decryptedSource);
    } catch (error) {
      throw new RuntimeMaterialError('RUNTIME_MATERIAL_DECRYPT_FAILED', 'Runtime material cannot be decrypted.', { cause: error });
    } finally {
      if (Buffer.isBuffer(decryptedSource) || decryptedSource instanceof Uint8Array) {
        decryptedSource.fill(0);
      }
    }
    plaintext = parsePlaintext(decrypted, version, magic);
    const bindingMatches = plaintext.subject === expectedSubject &&
      exactEqual(plaintext.bundleId, response.bundleId) &&
      exactEqual(plaintext.tokenId, response.tokenId) &&
      exactEqual(plaintext.clientNonce, response.clientNonce) &&
      exactEqual(plaintext.installFingerprint, identity.fingerprint) &&
      plaintext.issuedAt === response.issuedAt &&
      plaintext.expiresAt === response.expiresAt;
    if (!bindingMatches) {
      throw new RuntimeMaterialError('RUNTIME_MATERIAL_BINDING_MISMATCH', 'Runtime material binding mismatch.');
    }
    const result = {
      contentKey: Buffer.from(plaintext.contentKey),
      bundleId: Buffer.from(plaintext.bundleId),
      tokenId: Buffer.from(plaintext.tokenId),
      expiresAt: plaintext.expiresAt
    };
    return result;
  } finally {
    decrypted?.fill(0);
    requestBundleId.fill(0);
    requestClientNonce.fill(0);
    disposeFields(response);
    disposeFields(plaintext);
  }
}

export function unwrapRuntimeMaterialResponseBody({ uin, ...fields }) {
  return unwrapResponseBody(
    { ...fields, expectedSubject: validateUin(uin) },
    RUNTIME_MATERIAL_VERSION,
    PLAINTEXT_MAGIC_V1
  );
}

export function unwrapPreloadRuntimeMaterialResponseBody(fields) {
  return unwrapResponseBody(
    { ...fields, expectedSubject: 0 },
    PRELOAD_RUNTIME_MATERIAL_VERSION,
    PLAINTEXT_MAGIC_V2
  );
}
