import {
  ProtocolError,
  qqTeaDecrypt,
  qqTeaEncrypt
} from './qq-tea.js';

export { ProtocolError } from './qq-tea.js';

export const CS_HEADER_LENGTH = 18;
export const MESSAGE_HEADER_LENGTH = 16;
export const MAX_PACKET_SIZE = 1024 * 1024;

const MAX_LOGIN_FIELD_LENGTH = 64;
const MAX_MESSAGE_BODY_LENGTH = 0xffff - 2;

// Wire-compatible with the historical Launcher/Server MD5.h implementation.
// That implementation has a leading zero round constant and sizes its padded
// uint32 vector in bytes, so a standards-compliant MD5 digest is not accepted.
const LEGACY_PASSWORD_CONSTANTS = Uint32Array.from([
  0x00000000, 0xd76aa478, 0xe8c7b756, 0x242070db,
  0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
  0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
  0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e,
  0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51,
  0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681,
  0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87,
  0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9,
  0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
  0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60,
  0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085,
  0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8,
  0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7,
  0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
  0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314,
  0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb
]);

const LEGACY_PASSWORD_ROTATIONS = Uint8Array.from([
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
  5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
]);

function asBuffer(value, label) {
  if (!Buffer.isBuffer(value) && !(value instanceof Uint8Array)) {
    throw new TypeError(`${label} must be a Buffer or Uint8Array.`);
  }
  return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
}

function validateLoginText(value, label) {
  if (typeof value !== 'string' || value.length === 0 || value.includes('\0')) {
    throw new TypeError(`${label} must be a non-empty string without NUL bytes.`);
  }
  const bytes = Buffer.from(value, 'utf8');
  if (bytes.length > MAX_LOGIN_FIELD_LENGTH) {
    throw new RangeError(`${label} exceeds ${MAX_LOGIN_FIELD_LENGTH} UTF-8 bytes.`);
  }
  return bytes;
}

export function hashLoginPassword(password) {
  const bytes = validateLoginText(password, 'password');
  try {
    const originalLength = bytes.length;
    const paddedByteLength = originalLength + 1 + ((56 - ((originalLength + 1) % 64) + 64) % 64);
    // Deliberately mirrors vector<unsigned int>::resize(paddedByteLength + 8).
    const words = new Uint32Array(paddedByteLength + 8);
    for (let index = 0; index < originalLength; index += 1) {
      const shift = (index % 4) * 8;
      const signedByte = bytes[index] < 0x80 ? bytes[index] : bytes[index] - 0x100;
      words[index >>> 2] = (words[index >>> 2] | (signedByte << shift)) >>> 0;
    }
    const paddingShift = (originalLength % 4) * 8;
    words[originalLength >>> 2] = (words[originalLength >>> 2] | (0x80 << paddingShift)) >>> 0;
    words[paddedByteLength >>> 2] = (originalLength * 8) >>> 0;

    let stateA = 0x67452301;
    let stateB = 0xefcdab89;
    let stateC = 0x98badcfe;
    let stateD = 0x10325476;
    for (let block = 0; block < words.length; block += 16) {
      let a = stateA;
      let b = stateB;
      let c = stateC;
      let d = stateD;
      for (let round = 0; round < 64; round += 1) {
        let f;
        let g;
        if (round < 16) {
          f = ((b & c) | (~b & d)) >>> 0;
          g = round;
        } else if (round < 32) {
          f = ((b & d) | (c & ~d)) >>> 0;
          g = ((5 * round) + 1) % 16;
        } else if (round < 48) {
          f = (b ^ c ^ d) >>> 0;
          g = ((3 * round) + 5) % 16;
        } else {
          f = (c ^ (b | ~d)) >>> 0;
          g = (7 * round) % 16;
        }
        const sum = (a + f + LEGACY_PASSWORD_CONSTANTS[round] + words[block + g]) >>> 0;
        const rotation = LEGACY_PASSWORD_ROTATIONS[round];
        const rotated = ((sum << rotation) | (sum >>> (32 - rotation))) >>> 0;
        const previousD = d;
        d = c;
        c = b;
        b = (b + rotated) >>> 0;
        a = previousD;
      }
      stateA = (stateA + a) >>> 0;
      stateB = (stateB + b) >>> 0;
      stateC = (stateC + c) >>> 0;
      stateD = (stateD + d) >>> 0;
    }
    return [stateA, stateB, stateC, stateD]
      .map((value) => value.toString(16).padStart(8, '0'))
      .join('');
  } finally {
    bytes.fill(0);
  }
}

export function encodeLoginRequestBody(username, wirePassword) {
  const usernameBytes = validateLoginText(username, 'username');
  const passwordBytes = validateLoginText(wirePassword, 'wirePassword');
  const output = Buffer.allocUnsafe(2 + usernameBytes.length + passwordBytes.length);
  let cursor = 0;
  output[cursor] = usernameBytes.length;
  cursor += 1;
  usernameBytes.copy(output, cursor);
  cursor += usernameBytes.length;
  output[cursor] = passwordBytes.length;
  cursor += 1;
  passwordBytes.copy(output, cursor);
  usernameBytes.fill(0);
  passwordBytes.fill(0);
  return output;
}

function encodeMessageHeader({ msgId, bodyLength }) {
  if (!Number.isInteger(msgId) || msgId < 0 || msgId > 0xffff) {
    throw new RangeError('msgId must be an unsigned 16-bit integer.');
  }
  if (!Number.isSafeInteger(bodyLength) || bodyLength < 0 || bodyLength > MAX_MESSAGE_BODY_LENGTH) {
    throw new RangeError(`message body exceeds ${MAX_MESSAGE_BODY_LENGTH} bytes.`);
  }
  const header = Buffer.alloc(MESSAGE_HEADER_LENGTH);
  header.writeUInt16BE(msgId, 0);
  header.writeInt16BE(0, 2);
  header.writeInt32BE(-1, 4);
  header[8] = 1;
  header[9] = 0;
  header.writeInt16BE(0, 10);
  header.writeInt16BE(0, 12);
  header.writeUInt16BE(bodyLength + 2, 14);
  return header;
}

export function encodeMessagePacket({
  msgId,
  body,
  key,
  encrypt = true,
  randomBytes,
  protocolSequence = 0
}) {
  if (!Number.isInteger(protocolSequence) || protocolSequence < 0 || protocolSequence > 0xffff_ffff) {
    throw new RangeError('protocolSequence must be an unsigned 32-bit integer.');
  }
  const bodyBytes = asBuffer(body, 'body');
  const message = Buffer.concat([
    encodeMessageHeader({ msgId, bodyLength: bodyBytes.length }),
    bodyBytes
  ]);
  let wireBody;
  try {
    wireBody = encrypt
      ? qqTeaEncrypt(message, key, randomBytes ? { randomBytes } : undefined)
      : Buffer.from(message);
  } finally {
    message.fill(0);
  }
  const totalLength = CS_HEADER_LENGTH + wireBody.length;
  if (totalLength > MAX_PACKET_SIZE) {
    wireBody.fill(0);
    throw new RangeError(`packet exceeds ${MAX_PACKET_SIZE} bytes.`);
  }

  const packet = Buffer.allocUnsafe(totalLength);
  packet.writeUInt32BE(totalLength, 0);
  packet.writeInt16BE(116, 4);
  packet.writeInt16BE(0, 6);
  packet.writeUInt32BE(protocolSequence, 8);
  packet.writeUInt32BE(0, 12);
  packet[16] = encrypt ? 1 : 0;
  packet[17] = 0;
  wireBody.copy(packet, CS_HEADER_LENGTH);
  wireBody.fill(0);
  return packet;
}

export function decodeMessagePacket(packet, { key } = {}) {
  const bytes = asBuffer(packet, 'packet');
  if (bytes.length < CS_HEADER_LENGTH || bytes.length > MAX_PACKET_SIZE) {
    throw new ProtocolError('Login packet length is outside the allowed range.');
  }
  const declaredLength = bytes.readUInt32BE(0);
  if (declaredLength !== bytes.length) {
    throw new ProtocolError('Login packet declared length does not match received bytes.');
  }
  const optionLength = bytes[17];
  const wireBodyOffset = CS_HEADER_LENGTH + optionLength;
  if (wireBodyOffset > bytes.length) {
    throw new ProtocolError('Login packet option length exceeds the packet boundary.');
  }

  const encrypted = bytes[16] !== 0;
  const wireBody = bytes.subarray(wireBodyOffset);
  const message = encrypted ? qqTeaDecrypt(wireBody, key) : Buffer.from(wireBody);
  try {
    if (message.length < MESSAGE_HEADER_LENGTH) {
      throw new ProtocolError('Login message is shorter than MSGHEAD.');
    }
    const declaredBodyLength = message.readUInt16BE(14);
    const actualBodyLengthWithPrefix = message.length - MESSAGE_HEADER_LENGTH + 2;
    if (declaredBodyLength !== actualBodyLengthWithPrefix) {
      throw new ProtocolError('MSGHEAD BodyLen does not match the message body.');
    }
    return {
      msgId: message.readUInt16BE(0),
      body: Buffer.from(message.subarray(MESSAGE_HEADER_LENGTH))
    };
  } finally {
    message.fill(0);
  }
}

export function parseLoginResponseBody(body) {
  const bytes = asBuffer(body, 'body');
  if (bytes.length < 6) {
    throw new ProtocolError('Login response is missing its fixed fields.');
  }
  const uin = bytes.readUInt32BE(2);
  if (uin === 0) {
    return { kind: 'invalid-credentials' };
  }
  if (uin === 1) {
    return { kind: 'account-unavailable' };
  }
  if (bytes.length < 22) {
    throw new ProtocolError('Successful login response is missing the game key.');
  }
  return {
    kind: 'success',
    uin,
    gameKey: Buffer.from(bytes.subarray(6, 22))
  };
}

export class LoginPacketFramer {
  #buffer = Buffer.alloc(0);

  push(chunk) {
    const bytes = asBuffer(chunk, 'chunk');
    if (bytes.length === 0) {
      return [];
    }
    if (this.#buffer.length === 0) {
      this.#buffer = Buffer.from(bytes);
    } else {
      const previous = this.#buffer;
      this.#buffer = Buffer.concat([previous, bytes]);
      previous.fill(0);
    }

    const packets = [];
    try {
      while (this.#buffer.length >= 4) {
        const packetLength = this.#buffer.readUInt32BE(0);
        if (packetLength < CS_HEADER_LENGTH || packetLength > MAX_PACKET_SIZE) {
          throw new ProtocolError('Login stream declared an invalid packet length.');
        }
        if (this.#buffer.length < packetLength) {
          break;
        }
        const previous = this.#buffer;
        packets.push(Buffer.from(previous.subarray(0, packetLength)));
        this.#buffer = Buffer.from(previous.subarray(packetLength));
        previous.fill(0);
      }
      return packets;
    } catch (error) {
      this.reset();
      throw error;
    }
  }

  reset() {
    this.#buffer.fill(0);
    this.#buffer = Buffer.alloc(0);
  }
}
