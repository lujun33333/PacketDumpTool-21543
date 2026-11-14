import { randomBytes as secureRandomBytes } from 'node:crypto';

const BLOCK_SIZE = 8;
const KEY_SIZE = 16;
const SALT_LENGTH = 2;
const ZERO_LENGTH = 7;
const DELTA = 0x9e3779b9;
const ROUNDS = 16;

export class ProtocolError extends Error {
  constructor(message) {
    super(message);
    this.name = 'ProtocolError';
    this.code = 'PROTOCOL_ERROR';
  }
}

function asBuffer(value, label) {
  if (!Buffer.isBuffer(value) && !(value instanceof Uint8Array)) {
    throw new TypeError(`${label} must be a Buffer or Uint8Array.`);
  }
  return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
}

function validateKey(key) {
  const bytes = asBuffer(key, 'key');
  if (bytes.length !== KEY_SIZE) {
    throw new RangeError(`key must contain exactly ${KEY_SIZE} bytes.`);
  }
  return bytes;
}

function validateBlock(block) {
  const bytes = asBuffer(block, 'block');
  if (bytes.length !== BLOCK_SIZE) {
    throw new RangeError(`block must contain exactly ${BLOCK_SIZE} bytes.`);
  }
  return bytes;
}

function mix(value, sum, keyA, keyB) {
  return (
    (((value << 4) + keyA) >>> 0) ^
    ((value + sum) >>> 0) ^
    (((value >>> 5) + keyB) >>> 0)
  ) >>> 0;
}

export function teaEncryptBlock(block, key) {
  const input = validateBlock(block);
  const keyBytes = validateKey(key);
  let y = input.readUInt32BE(0);
  let z = input.readUInt32BE(4);
  const words = [
    keyBytes.readUInt32BE(0),
    keyBytes.readUInt32BE(4),
    keyBytes.readUInt32BE(8),
    keyBytes.readUInt32BE(12)
  ];
  let sum = 0;

  for (let index = 0; index < ROUNDS; index += 1) {
    sum = (sum + DELTA) >>> 0;
    y = (y + mix(z, sum, words[0], words[1])) >>> 0;
    z = (z + mix(y, sum, words[2], words[3])) >>> 0;
  }

  const output = Buffer.allocUnsafe(BLOCK_SIZE);
  output.writeUInt32BE(y, 0);
  output.writeUInt32BE(z, 4);
  return output;
}

export function teaDecryptBlock(block, key) {
  const input = validateBlock(block);
  const keyBytes = validateKey(key);
  let y = input.readUInt32BE(0);
  let z = input.readUInt32BE(4);
  const words = [
    keyBytes.readUInt32BE(0),
    keyBytes.readUInt32BE(4),
    keyBytes.readUInt32BE(8),
    keyBytes.readUInt32BE(12)
  ];
  let sum = (DELTA << 4) >>> 0;

  for (let index = 0; index < ROUNDS; index += 1) {
    z = (z - mix(y, sum, words[2], words[3])) >>> 0;
    y = (y - mix(z, sum, words[0], words[1])) >>> 0;
    sum = (sum - DELTA) >>> 0;
  }

  const output = Buffer.allocUnsafe(BLOCK_SIZE);
  output.writeUInt32BE(y, 0);
  output.writeUInt32BE(z, 4);
  return output;
}

export function qqTeaEncryptedLength(plainLength) {
  if (!Number.isSafeInteger(plainLength) || plainLength < 0) {
    throw new RangeError('plainLength must be a non-negative safe integer.');
  }
  const baseLength = plainLength + 1 + SALT_LENGTH + ZERO_LENGTH;
  const paddingLength = (BLOCK_SIZE - (baseLength % BLOCK_SIZE)) % BLOCK_SIZE;
  return baseLength + paddingLength;
}

function xorBlocks(left, right) {
  const output = Buffer.allocUnsafe(BLOCK_SIZE);
  for (let index = 0; index < BLOCK_SIZE; index += 1) {
    output[index] = left[index] ^ right[index];
  }
  return output;
}

function getRandomBytes(randomBytes, length) {
  const value = randomBytes(length);
  const bytes = asBuffer(value, 'randomBytes result');
  if (bytes.length !== length) {
    throw new RangeError(`randomBytes must return exactly ${length} bytes.`);
  }
  return bytes;
}

export function qqTeaEncrypt(plain, key, { randomBytes = secureRandomBytes } = {}) {
  const input = asBuffer(plain, 'plain');
  const keyBytes = validateKey(key);
  if (typeof randomBytes !== 'function') {
    throw new TypeError('randomBytes must be a function.');
  }

  const outputLength = qqTeaEncryptedLength(input.length);
  const paddingLength = outputLength - input.length - 1 - SALT_LENGTH - ZERO_LENGTH;
  const random = getRandomBytes(randomBytes, 1 + paddingLength + SALT_LENGTH);
  const framed = Buffer.alloc(outputLength, 0);
  let cursor = 0;
  framed[cursor] = (random[0] & 0xf8) | paddingLength;
  cursor += 1;
  random.copy(framed, cursor, 1, 1 + paddingLength);
  cursor += paddingLength;
  random.copy(framed, cursor, 1 + paddingLength);
  cursor += SALT_LENGTH;
  input.copy(framed, cursor);

  const output = Buffer.allocUnsafe(outputLength);
  let previousCrypt = Buffer.alloc(BLOCK_SIZE);
  let previousPlainXor = Buffer.alloc(BLOCK_SIZE);
  for (let offset = 0; offset < framed.length; offset += BLOCK_SIZE) {
    const plainXor = xorBlocks(framed.subarray(offset, offset + BLOCK_SIZE), previousCrypt);
    const encrypted = xorBlocks(teaEncryptBlock(plainXor, keyBytes), previousPlainXor);
    encrypted.copy(output, offset);
    previousPlainXor = plainXor;
    previousCrypt = encrypted;
  }
  framed.fill(0);
  previousPlainXor.fill(0);
  return output;
}

export function qqTeaDecrypt(ciphertext, key) {
  const input = asBuffer(ciphertext, 'ciphertext');
  const keyBytes = validateKey(key);
  if (input.length < BLOCK_SIZE * 2 || input.length % BLOCK_SIZE !== 0) {
    throw new ProtocolError('QQ TEA ciphertext has an invalid length.');
  }

  const decoded = Buffer.allocUnsafe(input.length);
  let previousCrypt = Buffer.alloc(BLOCK_SIZE);
  let previousPlainXor = Buffer.alloc(BLOCK_SIZE);
  for (let offset = 0; offset < input.length; offset += BLOCK_SIZE) {
    const encrypted = input.subarray(offset, offset + BLOCK_SIZE);
    const plainXor = teaDecryptBlock(xorBlocks(encrypted, previousPlainXor), keyBytes);
    const plain = xorBlocks(plainXor, previousCrypt);
    plain.copy(decoded, offset);
    previousPlainXor = plainXor;
    previousCrypt = encrypted;
  }
  previousPlainXor.fill(0);

  const paddingLength = decoded[0] & 0x07;
  const bodyStart = 1 + paddingLength + SALT_LENGTH;
  const bodyEnd = decoded.length - ZERO_LENGTH;
  if (bodyStart > bodyEnd) {
    decoded.fill(0);
    throw new ProtocolError('QQ TEA plaintext framing is invalid.');
  }
  for (let index = bodyEnd; index < decoded.length; index += 1) {
    if (decoded[index] !== 0) {
      decoded.fill(0);
      throw new ProtocolError('QQ TEA zero trailer validation failed.');
    }
  }

  const body = Buffer.from(decoded.subarray(bodyStart, bodyEnd));
  decoded.fill(0);
  return body;
}
