import koffi from 'koffi';
import { randomUUID } from 'node:crypto';
import { mkdir, readFile, rename, rm, writeFile } from 'node:fs/promises';
import { isAbsolute, join, resolve } from 'node:path';

const VAULT_FILE = 'launcher-credential-vault.json';
const VAULT_VERSION = 1;
const MAX_ACCOUNTS = 12;
const MAX_ACCOUNT_BYTES = 64;
const MAX_PASSWORD_BYTES = 64;
const CRYPTPROTECT_UI_FORBIDDEN = 0x1;

const DATA_BLOB = koffi.struct('QQSpeedCredentialDataBlob', {
  cbData: 'uint32',
  pbData: 'void *'
});
const DATA_BLOB_PTR = koffi.pointer(DATA_BLOB);
let dpapiBindings = null;

export class CredentialVaultError extends Error {
  constructor(code, message, options = {}) {
    super(message, options);
    this.name = 'CredentialVaultError';
    this.code = code;
  }
}

function validateUsername(value) {
  const username = String(value || '').trim();
  if (!username || Buffer.byteLength(username, 'utf8') > MAX_ACCOUNT_BYTES) {
    throw new CredentialVaultError('INVALID_INPUT', '账号参数无效。');
  }
  return username;
}

function validatePassword(value) {
  const password = String(value || '');
  if (!password || Buffer.byteLength(password, 'utf8') > MAX_PASSWORD_BYTES) {
    throw new CredentialVaultError('INVALID_INPUT', '账号或密码参数无效。');
  }
  return password;
}

function isCanonicalBase64(value) {
  if (typeof value !== 'string' || !value || value.length > 64 * 1024 || value.length % 4 !== 0) {
    return false;
  }
  if (!/^[A-Za-z0-9+/]+={0,2}$/.test(value)) {
    return false;
  }
  try {
    return Buffer.from(value, 'base64').toString('base64') === value;
  } catch {
    return false;
  }
}

function hasExactKeys(value, expectedKeys) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return false;
  }
  const keys = Object.keys(value).sort();
  const expected = [...expectedKeys].sort();
  return keys.length === expected.length && keys.every((key, index) => key === expected[index]);
}

function getDpapiBindings() {
  if (dpapiBindings) {
    return dpapiBindings;
  }
  if (process.platform !== 'win32') {
    throw new CredentialVaultError('DPAPI_UNAVAILABLE', 'Windows DPAPI 不可用。');
  }

  const crypt32 = koffi.load('crypt32.dll');
  const kernel32 = koffi.load('kernel32.dll');
  dpapiBindings = {
    crypt32,
    kernel32,
    protect: crypt32.func('__stdcall', 'CryptProtectData', 'bool', [
      DATA_BLOB_PTR,
      'str16',
      DATA_BLOB_PTR,
      'void *',
      'void *',
      'uint32',
      koffi.out(DATA_BLOB_PTR)
    ]),
    unprotect: crypt32.func('__stdcall', 'CryptUnprotectData', 'bool', [
      DATA_BLOB_PTR,
      'void *',
      DATA_BLOB_PTR,
      'void *',
      'void *',
      'uint32',
      koffi.out(DATA_BLOB_PTR)
    ]),
    localFree: kernel32.func('__stdcall', 'LocalFree', 'void *', ['void *']),
    getLastError: kernel32.func('__stdcall', 'GetLastError', 'uint32', [])
  };
  return dpapiBindings;
}

function callDpapi(operation, inputBytes) {
  const bindings = getDpapiBindings();
  const input = Buffer.from(inputBytes);
  const inputBlob = { cbData: input.length, pbData: input };
  const outputBlob = {};
  try {
    const ok = operation === 'protect'
      ? bindings.protect(inputBlob, null, null, null, null, CRYPTPROTECT_UI_FORBIDDEN, outputBlob)
      : bindings.unprotect(inputBlob, null, null, null, null, CRYPTPROTECT_UI_FORBIDDEN, outputBlob);
    if (!ok) {
      const code = Number(bindings.getLastError()) || 0;
      throw new CredentialVaultError(
        operation === 'protect' ? 'DPAPI_PROTECT_FAILED' : 'DPAPI_UNPROTECT_FAILED',
        `Windows DPAPI ${operation === 'protect' ? '加密' : '解密'}失败（代码 ${code}）。`
      );
    }
    const size = Number(outputBlob.cbData) || 0;
    if (!outputBlob.pbData || size <= 0 || size > 64 * 1024) {
      throw new CredentialVaultError('DPAPI_INVALID_OUTPUT', 'Windows DPAPI 返回了无效数据。');
    }
    return Buffer.from(koffi.decode(outputBlob.pbData, 'uint8', size));
  } finally {
    input.fill(0);
    if (outputBlob.pbData) {
      try {
        bindings.localFree(outputBlob.pbData);
      } catch {
        // DPAPI output ownership is best-effort during process teardown.
      }
    }
  }
}

export function protectPasswordWithDpapi(password) {
  const normalized = validatePassword(password);
  const cleartext = Buffer.from(normalized, 'utf8');
  let protectedBytes;
  try {
    protectedBytes = callDpapi('protect', cleartext);
    return protectedBytes.toString('base64');
  } finally {
    cleartext.fill(0);
    protectedBytes?.fill(0);
  }
}

export function unprotectPasswordWithDpapi(protectedValue) {
  if (!isCanonicalBase64(protectedValue)) {
    throw new CredentialVaultError('INVALID_CIPHERTEXT', '保存的密码数据无效。');
  }
  const protectedBytes = Buffer.from(protectedValue, 'base64');
  let cleartext;
  try {
    cleartext = callDpapi('unprotect', protectedBytes);
    const password = new TextDecoder('utf-8', { fatal: true }).decode(cleartext);
    return validatePassword(password);
  } catch (error) {
    if (error instanceof CredentialVaultError) {
      throw error;
    }
    throw new CredentialVaultError('INVALID_CLEARTEXT', '保存的密码数据无法读取。', { cause: error });
  } finally {
    protectedBytes.fill(0);
    cleartext?.fill(0);
  }
}

function normalizeVaultStore(parsed) {
  if (!hasExactKeys(parsed, ['version', 'accounts']) ||
      parsed.version !== VAULT_VERSION ||
      !Array.isArray(parsed.accounts) ||
      parsed.accounts.length > MAX_ACCOUNTS) {
    throw new CredentialVaultError('VAULT_INVALID', '密码保险库格式无效。');
  }
  const accounts = [];
  const seen = new Set();
  for (const entry of parsed.accounts) {
    if (!hasExactKeys(entry, ['user', 'dpapi']) ||
        typeof entry.user !== 'string' ||
        typeof entry.dpapi !== 'string') {
      throw new CredentialVaultError('VAULT_INVALID', '密码保险库格式无效。');
    }
    const user = entry.user.trim();
    const dpapi = entry.dpapi;
    if (entry.user !== user || !user || Buffer.byteLength(user, 'utf8') > MAX_ACCOUNT_BYTES ||
        seen.has(user) || !isCanonicalBase64(dpapi)) {
      throw new CredentialVaultError('VAULT_INVALID', '密码保险库格式无效。');
    }
    seen.add(user);
    accounts.push({ user, dpapi });
  }
  return { version: VAULT_VERSION, accounts };
}

function emptyVaultStore() {
  return { version: VAULT_VERSION, accounts: [] };
}

export function createCredentialVault(settingsDir, crypto = {}) {
  const normalizedSettingsDir = resolve(String(settingsDir || ''));
  if (!settingsDir || !isAbsolute(String(settingsDir)) || !isAbsolute(normalizedSettingsDir)) {
    throw new CredentialVaultError('INVALID_SETTINGS_DIR', '密码保险库目录无效。');
  }
  const file = join(normalizedSettingsDir, VAULT_FILE);
  const protect = typeof crypto.protect === 'function' ? crypto.protect : protectPasswordWithDpapi;
  const unprotect = typeof crypto.unprotect === 'function' ? crypto.unprotect : unprotectPasswordWithDpapi;

  async function readStore() {
    let text;
    try {
      text = await readFile(file, 'utf8');
    } catch (error) {
      if (error?.code === 'ENOENT') {
        return emptyVaultStore();
      }
      throw new CredentialVaultError('VAULT_READ_FAILED', '密码保险库无法读取。', { cause: error });
    }

    try {
      return normalizeVaultStore(JSON.parse(text));
    } catch (error) {
      if (error instanceof CredentialVaultError) {
        throw error;
      }
      throw new CredentialVaultError('VAULT_INVALID', '密码保险库格式无效。', { cause: error });
    }
  }

  async function readStoreForMutation() {
    try {
      return { store: await readStore(), repairInvalidFormat: false };
    } catch (error) {
      if (error instanceof CredentialVaultError && error.code === 'VAULT_INVALID') {
        return { store: emptyVaultStore(), repairInvalidFormat: true };
      }
      throw error;
    }
  }

  async function writeStore(store) {
    await mkdir(normalizedSettingsDir, { recursive: true });
    const temporary = `${file}.${process.pid}.${randomUUID()}.tmp`;
    try {
      await writeFile(temporary, `${JSON.stringify(normalizeVaultStore(store), null, 2)}\n`, {
        encoding: 'utf8',
        flag: 'wx',
        mode: 0o600
      });
      await rename(temporary, file);
    } catch (error) {
      throw new CredentialVaultError('VAULT_WRITE_FAILED', '密码保险库无法保存。', { cause: error });
    } finally {
      await rm(temporary, { force: true }).catch(() => {});
    }
  }

  return Object.freeze({
    file,
    async load(userInput) {
      const user = validateUsername(userInput);
      const store = await readStore();
      const entry = store.accounts.find((account) => account.user === user);
      if (!entry) return '';
      try {
        return validatePassword(await unprotect(entry.dpapi));
      } catch (error) {
        throw new CredentialVaultError('CREDENTIAL_UNAVAILABLE', '已保存的密码无法读取，请重新输入密码。', { cause: error });
      }
    },
    async store(userInput, passwordInput) {
      const user = validateUsername(userInput);
      const password = validatePassword(passwordInput);
      let dpapi;
      try {
        dpapi = String(await protect(password) || '');
      } catch (error) {
        throw new CredentialVaultError('CREDENTIAL_STORE_FAILED', '密码无法由 Windows DPAPI 保护。', { cause: error });
      }
      if (!isCanonicalBase64(dpapi)) {
        throw new CredentialVaultError('CREDENTIAL_STORE_FAILED', 'Windows DPAPI 未返回有效的密码数据。');
      }
      const { store: current } = await readStoreForMutation();
      await writeStore({
        version: VAULT_VERSION,
        accounts: [{ user, dpapi }, ...current.accounts.filter((entry) => entry.user !== user)].slice(0, MAX_ACCOUNTS)
      });
      return true;
    },
    async remove(userInput) {
      const user = validateUsername(userInput);
      const { store: current, repairInvalidFormat } = await readStoreForMutation();
      const accounts = current.accounts.filter((entry) => entry.user !== user);
      if (accounts.length === current.accounts.length && !repairInvalidFormat) return false;
      await writeStore({ version: VAULT_VERSION, accounts });
      return true;
    }
  });
}

export function createCredentialOwner(settingsDir, crypto = {}) {
  const vault = createCredentialVault(settingsDir, crypto);
  return Object.freeze({
    vault,
    async forget(userInput) {
      return vault.remove(validateUsername(userInput));
    },
    async resolve(payload = {}) {
      const username = validateUsername(payload.username);
      const remember = payload.remember !== false;
      const autoLogin = Boolean(payload.autoLogin && remember);
      let password = String(payload.password || '');
      if (!password && remember) {
        password = await vault.load(username);
      }
      if (!password) {
        throw new CredentialVaultError('CREDENTIAL_NOT_FOUND', '没有可用的已保存密码，请重新输入密码。');
      }
      password = validatePassword(password);
      return { username, password, remember, autoLogin };
    },
    async commitAuthenticated(credentials, options = {}) {
      const username = validateUsername(credentials?.username);
      const password = validatePassword(credentials?.password);
      const remember = credentials?.remember !== false;
      const now = typeof options.now === 'function' ? options.now : Date.now;
      const timestamp = Number(now());
      const lastLoginTime = Number.isSafeInteger(timestamp) && timestamp > 0 ? timestamp : Date.now();
      try {
        if (remember) {
          await vault.store(username, password);
        } else {
          await vault.remove(username);
        }
        return {
          state: {
            username,
            rememberPassword: remember,
            autoLogin: Boolean(credentials?.autoLogin && remember),
            lastLoginTime
          },
          warning: ''
        };
      } catch {
        return {
          state: {
            username,
            rememberPassword: false,
            autoLogin: false,
            lastLoginTime
          },
          warning: '账号已登录，但密码保险库保存失败；本次仍可开始游戏，密码未被记住。'
        };
      }
    }
  });
}
