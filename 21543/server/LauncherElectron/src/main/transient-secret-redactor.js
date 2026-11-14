import { createHash } from 'node:crypto';

import { hashLoginPassword } from './login/private-protocol.js';
import { redactStartupText } from './startup-log.js';

const MAX_TEXT_LENGTH = 500;
const REDACTED = '[redacted]';
const SHORT_SECRET_MAX_LENGTH = 3;

function normalizeSecret(value) {
  if (value === undefined || value === null) return '';
  return String(value);
}

function isAsciiAlphaNumeric(value) {
  return value !== undefined && /[0-9A-Za-z]/.test(value);
}

function replaceStandaloneLiteral(value, secret) {
  let cursor = 0;
  let output = '';
  for (;;) {
    const index = value.indexOf(secret, cursor);
    if (index < 0) return output + value.slice(cursor);

    const end = index + secret.length;
    const embedded =
      isAsciiAlphaNumeric(value[index - 1]) ||
      isAsciiAlphaNumeric(value[end]);
    output += value.slice(cursor, index);
    output += embedded ? secret : REDACTED;
    cursor = end;
  }
}

function replaceLiteral(value, secret) {
  if (!secret) return value;
  return secret.length <= SHORT_SECRET_MAX_LENGTH
    ? replaceStandaloneLiteral(value, secret)
    : value.split(secret).join(REDACTED);
}

export class TransientSecretRedactor {
  #secrets = new Set();

  track(...values) {
    for (const value of values) {
      const secret = normalizeSecret(value);
      if (!secret) continue;
      this.#secrets.add(secret);
      if (/^[0-9a-f]{8,}$/i.test(secret)) {
        this.#secrets.add(secret.toLowerCase());
        this.#secrets.add(secret.toUpperCase());
      }
    }
    return this;
  }

  trackPassword(password) {
    const value = normalizeSecret(password);
    if (!value) return this;
    this.track(value);
    const digest = createHash('md5').update(value, 'utf8').digest('hex');
    this.track(digest, digest.toUpperCase());
    try {
      const wireDigest = hashLoginPassword(value);
      this.track(wireDigest, wireDigest.toUpperCase());
    } catch {
      // Input validation remains owned by the login path; redaction is best effort.
    }
    return this;
  }

  redact(value, fallback = '') {
    const selected = value === undefined || value === null || value === '' ? fallback : value;
    let text = String(selected ?? '');
    const secrets = [...this.#secrets].sort((left, right) => right.length - left.length);
    for (const secret of secrets) {
      text = replaceLiteral(text, secret);
    }
    return redactStartupText(text)
      .replace(/[\u0000-\u001f\u007f]/g, ' ')
      .trim()
      .slice(0, MAX_TEXT_LENGTH);
  }

  serializeError(error) {
    if (!error) {
      return { name: 'Error', message: '' };
    }
    return {
      name: this.redact(error.name, 'Error'),
      code: this.redact(error.code || ''),
      message: this.redact(error.message || error)
    };
  }

  clear() {
    this.#secrets.clear();
  }
}
