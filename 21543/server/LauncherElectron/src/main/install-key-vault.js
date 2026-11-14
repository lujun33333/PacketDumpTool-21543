import {
  constants,
  createHash,
  generateKeyPairSync,
  privateDecrypt,
  timingSafeEqual
} from 'node:crypto';
import { mkdir, readFile, rename, rm, writeFile } from 'node:fs/promises';
import { isAbsolute, join, resolve } from 'node:path';

import {
  protectBytesWithDpapi,
  unprotectBytesWithDpapi
} from './windows-dpapi.js';

const FILE_NAME = 'runtime-install-key.json';
const STORE_VERSION = 1;
const ALGORITHM = 'rsa-oaep-sha256';
const PUBLIC_KEY_BYTES = 259;
const RSA_CIPHERTEXT_BYTES = 256;
const MAX_PRIVATE_KEY_BYTES = 16 * 1024;

export class InstallKeyVaultError extends Error {
  constructor(code, message, options = {}) {
    super(message, options);
    this.name = 'InstallKeyVaultError';
    this.code = code;
  }
}

function hasExactKeys(value, expectedKeys) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const actual = Object.keys(value).sort();
  const expected = [...expectedKeys].sort();
  return actual.length === expected.length && actual.every((key, index) => key === expected[index]);
}

function decodeCanonicalBase64(value, maximum, label) {
  if (typeof value !== 'string' || !value || value.length % 4 !== 0 ||
      value.length > Math.ceil(maximum / 3) * 4 + 4 ||
      !/^[A-Za-z0-9+/]+={0,2}$/.test(value)) {
    throw new InstallKeyVaultError('INSTALL_KEY_INVALID', `${label} is invalid.`);
  }
  const bytes = Buffer.from(value, 'base64');
  if (bytes.length <= 0 || bytes.length > maximum || bytes.toString('base64') !== value) {
    bytes.fill(0);
    throw new InstallKeyVaultError('INSTALL_KEY_INVALID', `${label} is invalid.`);
  }
  return bytes;
}

function base64UrlBytes(value, label) {
  try {
    const bytes = Buffer.from(String(value || ''), 'base64url');
    if (!bytes.length || bytes.toString('base64url') !== value) throw new Error('non-canonical');
    return bytes;
  } catch (error) {
    throw new InstallKeyVaultError('INSTALL_KEY_GENERATION_FAILED', `${label} is invalid.`, { cause: error });
  }
}

function generateRsaIdentity() {
  const pair = generateKeyPairSync('rsa', {
    modulusLength: 2048,
    publicExponent: 0x10001
  });
  const publicJwk = pair.publicKey.export({ format: 'jwk' });
  const exponent = base64UrlBytes(publicJwk.e, 'RSA exponent');
  const modulus = base64UrlBytes(publicJwk.n, 'RSA modulus');
  if (exponent.length !== 3 || !exponent.equals(Buffer.from([0x01, 0x00, 0x01])) || modulus.length !== 256) {
    exponent.fill(0);
    modulus.fill(0);
    throw new InstallKeyVaultError('INSTALL_KEY_GENERATION_FAILED', 'Generated RSA public key has an invalid shape.');
  }
  const privateKey = pair.privateKey.export({ format: 'der', type: 'pkcs8' });
  const publicKey = Buffer.concat([exponent, modulus]);
  exponent.fill(0);
  modulus.fill(0);
  return { publicKey, privateKey };
}

function identityFromStore(store) {
  return {
    algorithm: ALGORITHM,
    publicKey: Buffer.from(store.publicKey),
    fingerprint: Buffer.from(store.fingerprint)
  };
}

export function createInstallKeyVault(rootDir, dependencies = {}) {
  const normalizedRoot = resolve(String(rootDir || ''));
  if (!rootDir || !isAbsolute(String(rootDir)) || !isAbsolute(normalizedRoot)) {
    throw new InstallKeyVaultError('INVALID_ROOT', 'Install key directory must be absolute.');
  }
  const file = join(normalizedRoot, FILE_NAME);
  const protect = typeof dependencies.protect === 'function'
    ? dependencies.protect
    : protectBytesWithDpapi;
  const unprotect = typeof dependencies.unprotect === 'function'
    ? dependencies.unprotect
    : unprotectBytesWithDpapi;
  const generateIdentity = typeof dependencies.generateIdentity === 'function'
    ? dependencies.generateIdentity
    : generateRsaIdentity;
  const now = typeof dependencies.now === 'function' ? dependencies.now : Date.now;
  let identityPromise;

  async function readStore() {
    let parsed;
    try {
      parsed = JSON.parse(await readFile(file, 'utf8'));
    } catch (error) {
      if (error?.code === 'ENOENT') return null;
      throw new InstallKeyVaultError('INSTALL_KEY_READ_FAILED', 'Install key cannot be read.', { cause: error });
    }
    if (!hasExactKeys(parsed, [
      'version',
      'algorithm',
      'createdAt',
      'publicKey',
      'fingerprint',
      'privateKeyDpapi'
    ]) || parsed.version !== STORE_VERSION || parsed.algorithm !== ALGORITHM ||
        !Number.isSafeInteger(parsed.createdAt) || parsed.createdAt <= 0) {
      throw new InstallKeyVaultError('INSTALL_KEY_INVALID', 'Install key store format is invalid.');
    }
    const publicKey = decodeCanonicalBase64(parsed.publicKey, PUBLIC_KEY_BYTES, 'Install public key');
    const fingerprint = decodeCanonicalBase64(parsed.fingerprint, 32, 'Install key fingerprint');
    const privateKeyDpapi = decodeCanonicalBase64(parsed.privateKeyDpapi, MAX_PRIVATE_KEY_BYTES, 'Protected install private key');
    try {
      if (publicKey.length !== PUBLIC_KEY_BYTES || fingerprint.length !== 32) {
        throw new InstallKeyVaultError('INSTALL_KEY_INVALID', 'Install key store length is invalid.');
      }
      const actualFingerprint = createHash('sha256').update(publicKey).digest();
      const matches = timingSafeEqual(actualFingerprint, fingerprint);
      actualFingerprint.fill(0);
      if (!matches) {
        throw new InstallKeyVaultError('INSTALL_KEY_INVALID', 'Install key fingerprint does not match.');
      }
      return { publicKey, fingerprint, privateKeyDpapi, createdAt: parsed.createdAt };
    } catch (error) {
      publicKey.fill(0);
      fingerprint.fill(0);
      privateKeyDpapi.fill(0);
      throw error;
    }
  }

  async function writeNewStore() {
    let generated;
    let protectedPrivate;
    try {
      generated = await generateIdentity();
      if (!Buffer.isBuffer(generated?.publicKey) || generated.publicKey.length !== PUBLIC_KEY_BYTES ||
          !Buffer.isBuffer(generated?.privateKey) || generated.privateKey.length <= 0 ||
          generated.privateKey.length > MAX_PRIVATE_KEY_BYTES) {
        throw new InstallKeyVaultError('INSTALL_KEY_GENERATION_FAILED', 'Generated install identity is invalid.');
      }
      protectedPrivate = Buffer.from(await protect(generated.privateKey, {
        description: 'QQSpeedLauncher per-install runtime key'
      }));
      if (!protectedPrivate.length || protectedPrivate.length > MAX_PRIVATE_KEY_BYTES) {
        throw new InstallKeyVaultError('INSTALL_KEY_PROTECT_FAILED', 'DPAPI did not protect the install key.');
      }
      const fingerprint = createHash('sha256').update(generated.publicKey).digest();
      const createdAt = Number(now());
      if (!Number.isSafeInteger(createdAt) || createdAt <= 0) {
        fingerprint.fill(0);
        throw new InstallKeyVaultError('INSTALL_KEY_GENERATION_FAILED', 'Install key timestamp is invalid.');
      }
      const serialized = `${JSON.stringify({
        version: STORE_VERSION,
        algorithm: ALGORITHM,
        createdAt,
        publicKey: generated.publicKey.toString('base64'),
        fingerprint: fingerprint.toString('base64'),
        privateKeyDpapi: protectedPrivate.toString('base64')
      }, null, 2)}\n`;
      await mkdir(normalizedRoot, { recursive: true });
      const temporary = `${file}.${process.pid}.${createdAt}.tmp`;
      try {
        await writeFile(temporary, serialized, { encoding: 'utf8', flag: 'wx', mode: 0o600 });
        await rename(temporary, file);
      } finally {
        await rm(temporary, { force: true }).catch(() => {});
      }
      const result = {
        publicKey: Buffer.from(generated.publicKey),
        fingerprint: Buffer.from(fingerprint),
        privateKeyDpapi: Buffer.from(protectedPrivate),
        createdAt
      };
      fingerprint.fill(0);
      return result;
    } catch (error) {
      if (error instanceof InstallKeyVaultError) throw error;
      throw new InstallKeyVaultError('INSTALL_KEY_WRITE_FAILED', 'Install key cannot be created.', { cause: error });
    } finally {
      generated?.publicKey?.fill(0);
      generated?.privateKey?.fill(0);
      protectedPrivate?.fill(0);
    }
  }

  async function ensureStore() {
    return (await readStore()) || writeNewStore();
  }

  async function getStore() {
    identityPromise ||= ensureStore().catch((error) => {
      identityPromise = undefined;
      throw error;
    });
    return identityPromise;
  }

  return Object.freeze({
    file,
    async getPublicIdentity() {
      return identityFromStore(await getStore());
    },
    async decrypt(ciphertextInput) {
      if (!Buffer.isBuffer(ciphertextInput) && !(ciphertextInput instanceof Uint8Array)) {
        throw new InstallKeyVaultError('INVALID_CIPHERTEXT', 'Runtime material ciphertext must be binary.');
      }
      const ciphertext = Buffer.from(ciphertextInput);
      if (ciphertext.length !== RSA_CIPHERTEXT_BYTES) {
        ciphertext.fill(0);
        throw new InstallKeyVaultError('INVALID_CIPHERTEXT', 'Runtime material ciphertext must be exactly 256 bytes.');
      }
      const store = await getStore();
      let privateKey;
      try {
        privateKey = Buffer.from(await unprotect(store.privateKeyDpapi));
        if (!privateKey.length || privateKey.length > MAX_PRIVATE_KEY_BYTES) {
          throw new InstallKeyVaultError('INSTALL_KEY_UNPROTECT_FAILED', 'DPAPI returned an invalid install key.');
        }
        return privateDecrypt({
          key: privateKey,
          format: 'der',
          type: 'pkcs8',
          padding: constants.RSA_PKCS1_OAEP_PADDING,
          oaepHash: 'sha256'
        }, ciphertext);
      } catch (error) {
        if (error instanceof InstallKeyVaultError) throw error;
        throw new InstallKeyVaultError('RUNTIME_MATERIAL_DECRYPT_FAILED', 'Runtime material cannot be decrypted.', { cause: error });
      } finally {
        ciphertext.fill(0);
        privateKey?.fill(0);
      }
    }
  });
}
