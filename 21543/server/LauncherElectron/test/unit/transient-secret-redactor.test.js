import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import test from 'node:test';

import { hashLoginPassword } from '../../src/main/login/private-protocol.js';
import { TransientSecretRedactor } from '../../src/main/transient-secret-redactor.js';

test('transient redactor removes raw password, derived MD5, wire sign key and game key', () => {
  const password = 'synthetic-raw-password';
  const digest = createHash('md5').update(password, 'utf8').digest('hex');
  const wireDigest = hashLoginPassword(password);
  const wireSignKey = 'synthetic-wire-sign-key';
  const gameKeyHex = '102132435465768798a9bacbdcedfe0f';
  const redactor = new TransientSecretRedactor();
  redactor.trackPassword(password);
  redactor.track(wireSignKey, gameKeyHex);

  const output = redactor.redact(
    `raw=${password}; digest=${digest.toUpperCase()}; wireDigest=${wireDigest}; sign=${wireSignKey}; game=${gameKeyHex}`
  );

  for (const secret of [password, digest, digest.toUpperCase(), wireDigest, wireSignKey, gameKeyHex]) {
    assert.equal(output.includes(secret), false);
  }
  assert.match(output, /\[redacted\]/);
});

test('transient redactor sanitizes standalone one-byte secrets and serialized errors', () => {
  const redactor = new TransientSecretRedactor();
  redactor.trackPassword('4');

  assert.equal(redactor.redact('4'), '[redacted]');
  assert.equal(
    redactor.redact('The in-memory Hook payload is not a supported x64 PE image.'),
    'The in-memory Hook payload is not a supported x64 PE image.'
  );
  const serialized = redactor.serializeError(Object.assign(new Error('remote failure echoed 4'), {
    code: 'WIRE_4'
  }));
  assert.equal(JSON.stringify(serialized).includes('4'), false);
});

test('transient redactor preserves stable user-facing login errors and can be cleared', () => {
  const password = 'synthetic-password';
  const message = '账号或密码错误。';
  const redactor = new TransientSecretRedactor();
  redactor.trackPassword(password);

  assert.equal(redactor.redact(message), message);
  assert.equal(redactor.redact(password), '[redacted]');
  redactor.clear();
  assert.equal(redactor.redact(password), password);
});
