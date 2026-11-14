import assert from 'node:assert/strict';
import test from 'node:test';

import {
  createQqAvatarUrl,
  normalizeQqUin,
  sanitizeAccountAvatarUrl
} from '../../src/shared/account-avatar.js';

test('QQ avatar URL is derived only from a plausible numeric account', () => {
  assert.equal(normalizeQqUin(' 12345678 '), '12345678');
  assert.equal(normalizeQqUin('1234'), '');
  assert.equal(normalizeQqUin('01234567'), '');
  assert.equal(normalizeQqUin('user@example.com'), '');
  assert.equal(
    createQqAvatarUrl('12345678'),
    'https://q3.qlogo.cn/g?b=qq&nk=12345678&s=100'
  );
  assert.equal(createQqAvatarUrl('not-a-qq-number'), '');
});

test('account avatar sanitizer accepts only the fixed HTTPS qlogo contract', () => {
  const expected = 'https://q3.qlogo.cn/g?b=qq&nk=12345678&s=100';
  assert.equal(sanitizeAccountAvatarUrl(expected), expected);
  for (const rejected of [
    'http://q3.qlogo.cn/g?b=qq&nk=12345678&s=100',
    'https://q1.qlogo.cn/g?b=qq&nk=12345678&s=100',
    'https://evil.example/g?b=qq&nk=12345678&s=100',
    'https://q3.qlogo.cn/other?b=qq&nk=12345678&s=100',
    'https://q3.qlogo.cn/g?b=qq&nk=not-numeric&s=100',
    'https://q3.qlogo.cn/g?b=qq&nk=12345678&s=640',
    'https://q3.qlogo.cn/g?b=qq&nk=12345678&nk=87654321&s=100',
    'https://q3.qlogo.cn/g?b=qq&nk=12345678&s=100&token=secret'
  ]) {
    assert.equal(sanitizeAccountAvatarUrl(rejected), '', rejected);
  }
});
