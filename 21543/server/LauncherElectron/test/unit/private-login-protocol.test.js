import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import {
  CS_HEADER_LENGTH,
  MAX_PACKET_SIZE,
  LoginPacketFramer,
  ProtocolError,
  decodeMessagePacket,
  encodeLoginRequestBody,
  encodeMessagePacket,
  hashLoginPassword,
  parseLoginResponseBody
} from '../../src/main/login/private-protocol.js';
import {
  qqTeaDecrypt,
  qqTeaEncrypt,
  qqTeaEncryptedLength,
  teaDecryptBlock,
  teaEncryptBlock
} from '../../src/main/login/qq-tea.js';

const CONFIG_KEY = Buffer.from([
  0x66, 0x5f, 0xae, 0x33, 0x44, 0xdf, 0x99, 0x11,
  0x88, 0x99, 0x00, 0xaa, 0x6d, 0x78, 0xdd, 0xee
]);

const deterministicBytes = (length) => Buffer.from(
  Array.from({ length }, (_, index) => (0x40 + index) & 0xff)
);

describe('QQ TEA wire compatibility', () => {
  it('matches the 16-round C ECB zero vector and decrypts it', () => {
    const key = Buffer.alloc(16);
    const plain = Buffer.alloc(8);
    const encrypted = teaEncryptBlock(plain, key);

    assert.equal(encrypted.toString('hex'), 'a889f798182d8083');
    assert.deepEqual(teaDecryptBlock(encrypted, key), plain);
  });

  it('calculates the same padded length contract as oi_symmetry_encrypt2_len', () => {
    assert.equal(qqTeaEncryptedLength(0), 16);
    assert.equal(qqTeaEncryptedLength(1), 16);
    assert.equal(qqTeaEncryptedLength(6), 16);
    assert.equal(qqTeaEncryptedLength(7), 24);
    assert.equal(qqTeaEncryptedLength(32), 48);
  });

  it('round-trips arbitrary binary data with deterministic padding', () => {
    const plain = Buffer.from('00010203ff807f1020304050', 'hex');
    const encrypted = qqTeaEncrypt(plain, CONFIG_KEY, { randomBytes: deterministicBytes });

    assert.equal(encrypted.length, qqTeaEncryptedLength(plain.length));
    assert.deepEqual(qqTeaDecrypt(encrypted, CONFIG_KEY), plain);
  });

  it('matches an oi_symmetry_encrypt2 fixture compiled from QqTea.c', () => {
    const plain = Buffer.from('00010203ff807f1020304050', 'hex');
    const cRandBytes = Buffer.from('e4d5d93610', 'hex');
    const encrypted = qqTeaEncrypt(plain, CONFIG_KEY, {
      randomBytes(length) {
        assert.equal(length, cRandBytes.length);
        return cRandBytes;
      }
    });

    assert.equal(
      encrypted.toString('hex'),
      '20df5ff2e493c4b47a42ada746e86debb0b400582802799b'
    );
    assert.deepEqual(qqTeaDecrypt(encrypted, CONFIG_KEY), plain);
  });

  it('rejects malformed ciphertext and a corrupted zero trailer', () => {
    assert.throws(() => qqTeaDecrypt(Buffer.alloc(15), CONFIG_KEY), ProtocolError);

    const encrypted = qqTeaEncrypt(Buffer.from('payload'), CONFIG_KEY, {
      randomBytes: deterministicBytes
    });
    encrypted[encrypted.length - 1] ^= 0x01;
    assert.throws(() => qqTeaDecrypt(encrypted, CONFIG_KEY), ProtocolError);
  });
});

describe('private login packet codec', () => {
  it('matches the deployed C++ Launcher/Server password-digest fixtures', () => {
    const fixtures = [
      ['a', '0adba19820f1647abefa6fb10f72e0f0'],
      ['synthetic-password', 'b091db4ed887836f5f92e05a2947cc58'],
      ['p'.repeat(55), 'e73cc836a9d98197e5d2845c418701da'],
      ['p'.repeat(56), '14625c2572b987eb68da93e26940f3bd'],
      ['p'.repeat(64), 'aed800638d44ab449a5a84cc71dc58e6']
    ];
    for (const [password, expected] of fixtures) {
      assert.equal(hashLoginPassword(password), expected);
    }
  });

  it('hashes a server-compatible md5-mode password and length-prefixes request fields', () => {
    const digest = hashLoginPassword('synthetic-password');
    const body = encodeLoginRequestBody('test-user', digest);

    assert.match(digest, /^[0-9a-f]{32}$/);
    assert.equal(body[0], Buffer.byteLength('test-user'));
    assert.equal(body.subarray(1, 10).toString('utf8'), 'test-user');
    assert.equal(body[10], 32);
    assert.equal(body.subarray(11).toString('ascii'), digest);
    assert.equal(body.includes(Buffer.from('synthetic-password')), false);
  });

  it('length-prefixes a plain-mode wire password without changing it', () => {
    const body = encodeLoginRequestBody('test-user', 'plain-password');
    assert.equal(body[0], Buffer.byteLength('test-user'));
    assert.equal(body[10], Buffer.byteLength('plain-password'));
    assert.equal(body.subarray(11).toString(), 'plain-password');
  });

  it('accepts 1/64-byte wire passwords and rejects 65-byte or NUL values', () => {
    const oneByte = encodeLoginRequestBody('u', '4');
    assert.equal(oneByte[2], 1);
    assert.equal(oneByte.subarray(3).toString(), '4');

    const maxPassword = 'p'.repeat(64);
    const maxBody = encodeLoginRequestBody('u', maxPassword);
    assert.equal(maxBody[2], 64);
    assert.equal(maxBody.subarray(3).toString(), maxPassword);

    assert.throws(() => encodeLoginRequestBody('u', 'p'.repeat(65)), /64 UTF-8 bytes/);
    assert.throws(() => encodeLoginRequestBody('u', 'before\0after'), /without NUL bytes/);
  });

  it('matches the packed CSHEAD/MSGHEAD byte order without encryption', () => {
    const packet = encodeMessagePacket({
      msgId: 1,
      body: Buffer.from([0xaa, 0xbb]),
      key: CONFIG_KEY,
      encrypt: false
    });

    assert.equal(packet.toString('hex'), [
      '00000024', // CSHEAD.TotalLength
      '0074',     // CSHEAD.Ver = 116
      '0000',     // CSHEAD.DialogID
      '00000000', // CSHEAD.Seq
      '00000000', // CSHEAD.Uin
      '00',       // CSHEAD.BodyFlag
      '00',       // CSHEAD.OptLength
      '0001',     // MSGHEAD.MsgID
      '0000',     // MSGHEAD.MsgType
      'ffffffff', // MSGHEAD.MsgSeq
      '01',       // MSGHEAD.SrcFE
      '00',       // MSGHEAD.DstFE
      '0000',     // MSGHEAD.SrcID
      '0000',     // MSGHEAD.DstID
      '0004',     // MSGHEAD.BodyLen includes its uint16 length field
      'aabb'
    ].join(''));

    const decoded = decodeMessagePacket(packet, { key: CONFIG_KEY });
    assert.equal(decoded.msgId, 1);
    assert.deepEqual(decoded.body, Buffer.from([0xaa, 0xbb]));
  });

  it('writes an explicit monotonic CSHEAD sequence for follow-up messages', () => {
    const packet = encodeMessagePacket({
      msgId: 3,
      body: Buffer.from([0x01]),
      key: CONFIG_KEY,
      encrypt: false,
      protocolSequence: 1
    });

    assert.equal(packet.readUInt32BE(8), 1);
    assert.throws(
      () => encodeMessagePacket({
        msgId: 3,
        body: Buffer.alloc(0),
        key: CONFIG_KEY,
        encrypt: false,
        protocolSequence: -1
      }),
      /protocolSequence/
    );
  });

  it('encodes and decodes encrypted messages using CSHEAD option offsets', () => {
    const packet = encodeMessagePacket({
      msgId: 1,
      body: Buffer.from('private-body'),
      key: CONFIG_KEY,
      encrypt: true,
      randomBytes: deterministicBytes
    });

    assert.equal(packet.readUInt32BE(0), packet.length);
    assert.equal(packet[16], 1);
    const decoded = decodeMessagePacket(packet, { key: CONFIG_KEY });
    assert.equal(decoded.msgId, 1);
    assert.equal(decoded.body.toString('utf8'), 'private-body');
  });

  it('validates declared lengths and MSGHEAD BodyLen', () => {
    const packet = encodeMessagePacket({
      msgId: 1,
      body: Buffer.from([1, 2, 3]),
      key: CONFIG_KEY,
      encrypt: false
    });

    const wrongTotal = Buffer.from(packet);
    wrongTotal.writeUInt32BE(packet.length + 1, 0);
    assert.throws(() => decodeMessagePacket(wrongTotal, { key: CONFIG_KEY }), ProtocolError);

    const wrongBodyLength = Buffer.from(packet);
    wrongBodyLength.writeUInt16BE(0xffff, CS_HEADER_LENGTH + 14);
    assert.throws(() => decodeMessagePacket(wrongBodyLength, { key: CONFIG_KEY }), ProtocolError);
  });

  it('parses success, invalid-password and unavailable-account responses', () => {
    const gameKey = Buffer.from('00112233445566778899aabbccddeeff', 'hex');
    const success = Buffer.alloc(22);
    success.writeUInt16BE(0, 0);
    success.writeUInt32BE(123456, 2);
    gameKey.copy(success, 6);

    assert.deepEqual(parseLoginResponseBody(success), {
      kind: 'success',
      uin: 123456,
      gameKey
    });

    const invalid = Buffer.alloc(6);
    invalid.writeUInt32BE(0, 2);
    assert.deepEqual(parseLoginResponseBody(invalid), { kind: 'invalid-credentials' });

    const unavailable = Buffer.alloc(6);
    unavailable.writeUInt32BE(1, 2);
    assert.deepEqual(parseLoginResponseBody(unavailable), { kind: 'account-unavailable' });
  });

  it('frames split headers, split bodies and coalesced packets', () => {
    const first = encodeMessagePacket({ msgId: 1, body: Buffer.from('one'), key: CONFIG_KEY, encrypt: false });
    const second = encodeMessagePacket({ msgId: 2, body: Buffer.from('two'), key: CONFIG_KEY, encrypt: false });
    const framer = new LoginPacketFramer();

    assert.deepEqual(framer.push(first.subarray(0, 3)), []);
    assert.deepEqual(framer.push(first.subarray(3, 11)), []);
    const packets = framer.push(Buffer.concat([first.subarray(11), second]));

    assert.equal(packets.length, 2);
    assert.deepEqual(packets[0], first);
    assert.deepEqual(packets[1], second);
  });

  it('rejects undersized and oversized frame declarations before buffering bodies', () => {
    const framer = new LoginPacketFramer();
    const undersized = Buffer.alloc(4);
    undersized.writeUInt32BE(CS_HEADER_LENGTH - 1);
    assert.throws(() => framer.push(undersized), ProtocolError);

    const oversized = Buffer.alloc(4);
    oversized.writeUInt32BE(MAX_PACKET_SIZE + 1);
    assert.throws(() => new LoginPacketFramer().push(oversized), ProtocolError);
  });
});
