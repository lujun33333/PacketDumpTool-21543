import {
  createCipheriv,
  createDecipheriv,
  createHash,
  randomBytes as cryptoRandomBytes,
  timingSafeEqual
} from 'node:crypto';
import {
  brotliCompressSync,
  brotliDecompressSync,
  constants as zlibConstants
} from 'node:zlib';

export const RUNTIME_BUNDLE_FILE = 'qqspeed-runtime.bundle';
export const RUNTIME_BUNDLE_ARCH = 'win32-x64';
export const RUNTIME_BUNDLE_SCHEMA_VERSION = 2;
export const MAX_NATIVE_ENTRY_BYTES = 128 * 1024 * 1024;

const MAGIC = Buffer.from('QQSRUNTIMEBNDL02', 'ascii');
const HEADER_SIZE = 176;
const AUTH_TAG_SIZE = 16;
const NONCE_SIZE = 12;
const HASH_SIZE = 32;
const BUNDLE_ID_SIZE = 32;
const CONTENT_KEY_SIZE = 32;
const ARCH_CODE_WIN32_X64 = 0x00008664;
const CIPHER_AES_256_GCM = 1;
const COMPRESSION_BROTLI = 1;
const ENTRY_COUNT = 2;
const ENTRY_KIND_BRIDGE = 1;
const ENTRY_KIND_HOOK = 2;

export const RUNTIME_BUNDLE_HEADER_OFFSETS = Object.freeze({
  magic: 0,
  schemaVersion: 16,
  headerSize: 18,
  architecture: 20,
  cipher: 24,
  compression: 25,
  entryCount: 26,
  flags: 28,
  bundleId: 32,
  nonce: 64,
  reserved: 76,
  bridgeKind: 80,
  bridgeReserved: 82,
  bridgeSize: 84,
  bridgeCompressedSize: 88,
  bridgeOffset: 92,
  bridgeSha256: 96,
  hookKind: 128,
  hookReserved: 130,
  hookSize: 132,
  hookCompressedSize: 136,
  hookOffset: 140,
  hookSha256: 144
});

function sha256(bytes) {
  return createHash('sha256').update(bytes).digest();
}

function sha256Hex(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

function toInputBuffer(value, label) {
  if (!Buffer.isBuffer(value) && !(value instanceof Uint8Array)) {
    throw new TypeError(`${label} must be a Buffer or Uint8Array.`);
  }
  return Buffer.isBuffer(value)
    ? value
    : Buffer.from(value.buffer, value.byteOffset, value.byteLength);
}

function copyExactBytes(value, expectedLength, label) {
  const bytes = toInputBuffer(value, label);
  if (bytes.length !== expectedLength) {
    throw new Error(`${label} must be exactly ${expectedLength} bytes.`);
  }
  return Buffer.from(bytes);
}

function copyNativeEntry(value, label) {
  const bytes = toInputBuffer(value, label);
  if (bytes.length <= 0 || bytes.length > MAX_NATIVE_ENTRY_BYTES) {
    throw new Error(`${label} size exceeds the ${MAX_NATIVE_ENTRY_BYTES}-byte limit.`);
  }
  return Buffer.from(bytes);
}

function compressEntry(bytes) {
  const compressed = brotliCompressSync(bytes, {
    params: {
      [zlibConstants.BROTLI_PARAM_QUALITY]: 7,
      [zlibConstants.BROTLI_PARAM_MODE]: zlibConstants.BROTLI_MODE_GENERIC,
      [zlibConstants.BROTLI_PARAM_SIZE_HINT]: bytes.length
    }
  });
  if (compressed.length <= 0 || compressed.length > MAX_NATIVE_ENTRY_BYTES) {
    compressed.fill(0);
    throw new Error('Native runtime compressed entry size is invalid.');
  }
  return compressed;
}

function assertMagic(bundle) {
  const actual = bundle.subarray(0, MAGIC.length);
  if (actual.length !== MAGIC.length || !timingSafeEqual(actual, MAGIC)) {
    throw new Error('Native runtime bundle magic is invalid.');
  }
}

function readEntry(header, prefix, expectedKind) {
  const kind = header.readUInt16LE(RUNTIME_BUNDLE_HEADER_OFFSETS[`${prefix}Kind`]);
  const reserved = header.readUInt16LE(RUNTIME_BUNDLE_HEADER_OFFSETS[`${prefix}Reserved`]);
  const size = header.readUInt32LE(RUNTIME_BUNDLE_HEADER_OFFSETS[`${prefix}Size`]);
  const compressedSize = header.readUInt32LE(RUNTIME_BUNDLE_HEADER_OFFSETS[`${prefix}CompressedSize`]);
  const offset = header.readUInt32LE(RUNTIME_BUNDLE_HEADER_OFFSETS[`${prefix}Offset`]);
  if (kind !== expectedKind || reserved !== 0) {
    throw new Error(`Native runtime ${prefix} entry descriptor is invalid.`);
  }
  if (size <= 0 || size > MAX_NATIVE_ENTRY_BYTES || compressedSize <= 0 || compressedSize > MAX_NATIVE_ENTRY_BYTES) {
    throw new Error(`Native runtime ${prefix} entry size exceeds its limit.`);
  }
  const hashOffset = RUNTIME_BUNDLE_HEADER_OFFSETS[`${prefix}Sha256`];
  return {
    size,
    compressedSize,
    offset,
    hash: Buffer.from(header.subarray(hashOffset, hashOffset + HASH_SIZE))
  };
}

function parseBundle(bundleInput) {
  const bundle = toInputBuffer(bundleInput, 'bundleInput');
  if (bundle.length < HEADER_SIZE + AUTH_TAG_SIZE) {
    throw new Error('Native runtime bundle is truncated.');
  }
  assertMagic(bundle);
  const schemaVersion = bundle.readUInt16LE(RUNTIME_BUNDLE_HEADER_OFFSETS.schemaVersion);
  if (schemaVersion !== RUNTIME_BUNDLE_SCHEMA_VERSION) {
    throw new Error(`Native runtime bundle schema ${schemaVersion} is unsupported.`);
  }
  const headerSize = bundle.readUInt16LE(RUNTIME_BUNDLE_HEADER_OFFSETS.headerSize);
  const architecture = bundle.readUInt32LE(RUNTIME_BUNDLE_HEADER_OFFSETS.architecture);
  const cipher = bundle.readUInt8(RUNTIME_BUNDLE_HEADER_OFFSETS.cipher);
  const compression = bundle.readUInt8(RUNTIME_BUNDLE_HEADER_OFFSETS.compression);
  const entryCount = bundle.readUInt16LE(RUNTIME_BUNDLE_HEADER_OFFSETS.entryCount);
  const flags = bundle.readUInt32LE(RUNTIME_BUNDLE_HEADER_OFFSETS.flags);
  const reserved = bundle.readUInt32LE(RUNTIME_BUNDLE_HEADER_OFFSETS.reserved);
  if (headerSize !== HEADER_SIZE || architecture !== ARCH_CODE_WIN32_X64) {
    throw new Error('Native runtime bundle header or architecture is invalid.');
  }
  if (cipher !== CIPHER_AES_256_GCM || compression !== COMPRESSION_BROTLI || entryCount !== ENTRY_COUNT) {
    throw new Error('Native runtime bundle cipher, compression or entry count is invalid.');
  }
  if (flags !== 0 || reserved !== 0) {
    throw new Error('Native runtime bundle reserved fields are not zero.');
  }

  const bridge = readEntry(bundle, 'bridge', ENTRY_KIND_BRIDGE);
  const hook = readEntry(bundle, 'hook', ENTRY_KIND_HOOK);
  if (bridge.offset !== 0 || hook.offset !== bridge.compressedSize) {
    bridge.hash.fill(0);
    hook.hash.fill(0);
    throw new Error('Native runtime bundle entry offsets are invalid.');
  }
  const payloadSize = bridge.compressedSize + hook.compressedSize;
  const expectedLength = HEADER_SIZE + payloadSize + AUTH_TAG_SIZE;
  if (!Number.isSafeInteger(payloadSize) || bundle.length !== expectedLength) {
    bridge.hash.fill(0);
    hook.hash.fill(0);
    throw new Error('Native runtime bundle length does not match its authenticated header.');
  }

  return {
    bundle,
    schemaVersion,
    headerSize,
    payloadSize,
    bundleId: Buffer.from(bundle.subarray(
      RUNTIME_BUNDLE_HEADER_OFFSETS.bundleId,
      RUNTIME_BUNDLE_HEADER_OFFSETS.bundleId + BUNDLE_ID_SIZE
    )),
    nonce: Buffer.from(bundle.subarray(
      RUNTIME_BUNDLE_HEADER_OFFSETS.nonce,
      RUNTIME_BUNDLE_HEADER_OFFSETS.nonce + NONCE_SIZE
    )),
    bridge,
    hook
  };
}

function disposeParsed(parsed) {
  parsed?.bundleId.fill(0);
  parsed?.nonce.fill(0);
  parsed?.bridge.hash.fill(0);
  parsed?.hook.hash.fill(0);
}

export function encodeNativeRuntimeBundle(entries, options = {}) {
  const bridgeBytes = copyNativeEntry(entries?.bridgeBytes, 'bridgeBytes');
  const hookBytes = copyNativeEntry(entries?.hookBytes, 'hookBytes');
  const contentKey = copyExactBytes(options.contentKey, CONTENT_KEY_SIZE, 'content key');
  const bundleId = copyExactBytes(options.bundleId, BUNDLE_ID_SIZE, 'bundle id');
  const nonce = options.nonce === undefined
    ? cryptoRandomBytes(NONCE_SIZE)
    : copyExactBytes(options.nonce, NONCE_SIZE, 'nonce');
  let bridgeHash;
  let hookHash;
  let bridgeCompressed;
  let hookCompressed;
  let plaintext;
  let encrypted;

  try {
    bridgeHash = sha256(bridgeBytes);
    hookHash = sha256(hookBytes);
    bridgeCompressed = compressEntry(bridgeBytes);
    hookCompressed = compressEntry(hookBytes);

    const header = Buffer.alloc(HEADER_SIZE);
    MAGIC.copy(header, RUNTIME_BUNDLE_HEADER_OFFSETS.magic);
    header.writeUInt16LE(RUNTIME_BUNDLE_SCHEMA_VERSION, RUNTIME_BUNDLE_HEADER_OFFSETS.schemaVersion);
    header.writeUInt16LE(HEADER_SIZE, RUNTIME_BUNDLE_HEADER_OFFSETS.headerSize);
    header.writeUInt32LE(ARCH_CODE_WIN32_X64, RUNTIME_BUNDLE_HEADER_OFFSETS.architecture);
    header.writeUInt8(CIPHER_AES_256_GCM, RUNTIME_BUNDLE_HEADER_OFFSETS.cipher);
    header.writeUInt8(COMPRESSION_BROTLI, RUNTIME_BUNDLE_HEADER_OFFSETS.compression);
    header.writeUInt16LE(ENTRY_COUNT, RUNTIME_BUNDLE_HEADER_OFFSETS.entryCount);
    header.writeUInt32LE(0, RUNTIME_BUNDLE_HEADER_OFFSETS.flags);
    bundleId.copy(header, RUNTIME_BUNDLE_HEADER_OFFSETS.bundleId);
    nonce.copy(header, RUNTIME_BUNDLE_HEADER_OFFSETS.nonce);
    header.writeUInt32LE(0, RUNTIME_BUNDLE_HEADER_OFFSETS.reserved);

    header.writeUInt16LE(ENTRY_KIND_BRIDGE, RUNTIME_BUNDLE_HEADER_OFFSETS.bridgeKind);
    header.writeUInt16LE(0, RUNTIME_BUNDLE_HEADER_OFFSETS.bridgeReserved);
    header.writeUInt32LE(bridgeBytes.length, RUNTIME_BUNDLE_HEADER_OFFSETS.bridgeSize);
    header.writeUInt32LE(bridgeCompressed.length, RUNTIME_BUNDLE_HEADER_OFFSETS.bridgeCompressedSize);
    header.writeUInt32LE(0, RUNTIME_BUNDLE_HEADER_OFFSETS.bridgeOffset);
    bridgeHash.copy(header, RUNTIME_BUNDLE_HEADER_OFFSETS.bridgeSha256);

    header.writeUInt16LE(ENTRY_KIND_HOOK, RUNTIME_BUNDLE_HEADER_OFFSETS.hookKind);
    header.writeUInt16LE(0, RUNTIME_BUNDLE_HEADER_OFFSETS.hookReserved);
    header.writeUInt32LE(hookBytes.length, RUNTIME_BUNDLE_HEADER_OFFSETS.hookSize);
    header.writeUInt32LE(hookCompressed.length, RUNTIME_BUNDLE_HEADER_OFFSETS.hookCompressedSize);
    header.writeUInt32LE(bridgeCompressed.length, RUNTIME_BUNDLE_HEADER_OFFSETS.hookOffset);
    hookHash.copy(header, RUNTIME_BUNDLE_HEADER_OFFSETS.hookSha256);

    plaintext = Buffer.concat([bridgeCompressed, hookCompressed]);
    const cipher = createCipheriv('aes-256-gcm', contentKey, nonce, { authTagLength: AUTH_TAG_SIZE });
    cipher.setAAD(header, { plaintextLength: plaintext.length });
    encrypted = Buffer.concat([cipher.update(plaintext), cipher.final()]);
    return Buffer.concat([header, encrypted, cipher.getAuthTag()]);
  } finally {
    bridgeBytes.fill(0);
    hookBytes.fill(0);
    contentKey.fill(0);
    bundleId.fill(0);
    nonce.fill(0);
    bridgeHash?.fill(0);
    hookHash?.fill(0);
    bridgeCompressed?.fill(0);
    hookCompressed?.fill(0);
    plaintext?.fill(0);
    encrypted?.fill(0);
  }
}

export function inspectNativeRuntimeBundle(bundleInput) {
  const parsed = parseBundle(bundleInput);
  try {
    return {
      schemaVersion: parsed.schemaVersion,
      bundleId: Buffer.from(parsed.bundleId),
      bridgeSize: parsed.bridge.size,
      hookSize: parsed.hook.size,
      bridgeSha256: parsed.bridge.hash.toString('hex'),
      hookSha256: parsed.hook.hash.toString('hex'),
      bundleSha256: sha256Hex(parsed.bundle)
    };
  } finally {
    disposeParsed(parsed);
  }
}

export function decodeNativeRuntimeBundle(bundleInput, options = {}) {
  const contentKey = copyExactBytes(options.contentKey, CONTENT_KEY_SIZE, 'content key');
  const parsed = parseBundle(bundleInput);
  let compressedPlaintext;
  let bridgeBytes;
  let hookBytes;
  try {
    const header = parsed.bundle.subarray(0, parsed.headerSize);
    const encrypted = parsed.bundle.subarray(parsed.headerSize, parsed.headerSize + parsed.payloadSize);
    const authTag = parsed.bundle.subarray(parsed.bundle.length - AUTH_TAG_SIZE);
    const decipher = createDecipheriv('aes-256-gcm', contentKey, parsed.nonce, { authTagLength: AUTH_TAG_SIZE });
    decipher.setAAD(header, { plaintextLength: parsed.payloadSize });
    decipher.setAuthTag(authTag);
    try {
      compressedPlaintext = Buffer.concat([decipher.update(encrypted), decipher.final()]);
    } catch {
      throw new Error('Native runtime bundle authentication failed; the bundle is tampered or the content key is invalid.');
    }

    try {
      bridgeBytes = brotliDecompressSync(
        compressedPlaintext.subarray(parsed.bridge.offset, parsed.bridge.offset + parsed.bridge.compressedSize),
        { maxOutputLength: MAX_NATIVE_ENTRY_BYTES }
      );
      hookBytes = brotliDecompressSync(
        compressedPlaintext.subarray(parsed.hook.offset, parsed.hook.offset + parsed.hook.compressedSize),
        { maxOutputLength: MAX_NATIVE_ENTRY_BYTES }
      );
    } catch {
      throw new Error('Native runtime bundle Brotli payload is corrupt or exceeds its size limit.');
    }

    if (bridgeBytes.length !== parsed.bridge.size || hookBytes.length !== parsed.hook.size) {
      throw new Error('Native runtime bundle entry size failed integrity validation.');
    }
    const actualBridgeHash = sha256(bridgeBytes);
    const actualHookHash = sha256(hookBytes);
    const hashesMatch = timingSafeEqual(actualBridgeHash, parsed.bridge.hash) &&
      timingSafeEqual(actualHookHash, parsed.hook.hash);
    actualBridgeHash.fill(0);
    actualHookHash.fill(0);
    if (!hashesMatch) {
      throw new Error('Native runtime bundle entry hash failed integrity validation.');
    }

    const result = {
      arch: RUNTIME_BUNDLE_ARCH,
      bundleId: Buffer.from(parsed.bundleId),
      bridgeBytes,
      hookBytes,
      bridgeSha256: parsed.bridge.hash.toString('hex'),
      hookSha256: parsed.hook.hash.toString('hex'),
      bundleSha256: sha256Hex(parsed.bundle)
    };
    bridgeBytes = null;
    hookBytes = null;
    return result;
  } finally {
    contentKey.fill(0);
    compressedPlaintext?.fill(0);
    bridgeBytes?.fill(0);
    hookBytes?.fill(0);
    disposeParsed(parsed);
  }
}
