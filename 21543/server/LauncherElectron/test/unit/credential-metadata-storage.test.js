import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  commitAuthenticatedCredentials,
  loadNativeState,
  loadRememberedCredentials,
  resolveCredentialsForAuthentication
} from '../../src/main/native-core.js';

const appRoot = resolve(fileURLToPath(new URL('../../', import.meta.url)));

async function withTempSettings(run) {
  const settingsDir = await mkdtemp(join(tmpdir(), 'qqspeed-account-metadata-'));
  try {
    await run(settingsDir);
  } finally {
    await rm(settingsDir, { recursive: true, force: true });
  }
}

test('loading a legacy launcher-account file atomically removes old ciphertext metadata', async () => {
  await withTempSettings(async (settingsDir) => {
    const accountFile = join(settingsDir, 'launcher-account.json');
    await writeFile(accountFile, `${JSON.stringify({
      version: 4,
      lastUser: 'alice',
      autoLogin: true,
      accounts: [{
        user: 'alice',
        protectedPassword: 'old-electron-safe-storage-cipher',
        rememberPassword: true,
        lastLoginTime: 100
      }]
    }, null, 2)}\n`, 'utf8');

    const state = loadNativeState(appRoot, settingsDir);
    const migratedText = await readFile(accountFile, 'utf8');
    const migrated = JSON.parse(migratedText);

    assert.equal(state.login.user, 'alice');
    assert.equal(state.login.hasPassword, false);
    assert.equal(state.login.autoLogin, false);
    assert.equal(migrated.version, 5);
    assert.deepEqual(migrated.accounts[0], {
      user: 'alice', rememberPassword: false, lastLoginTime: 100
    });
    assert.doesNotMatch(migratedText, /protectedPassword|old-electron-safe-storage-cipher/);
  });
});

test('malformed legacy account content is replaced by an empty metadata-only store', async () => {
  await withTempSettings(async (settingsDir) => {
    const accountFile = join(settingsDir, 'launcher-account.json');
    await writeFile(accountFile, '{"protectedPassword":"truncated-old-cipher"', 'utf8');

    const state = loadNativeState(appRoot, settingsDir);
    const rewrittenText = await readFile(accountFile, 'utf8');

    assert.equal(state.login.user, '');
    assert.deepEqual(JSON.parse(rewrittenText), {
      version: 5, lastUser: '', autoLogin: false, accounts: []
    });
    assert.doesNotMatch(rewrittenText, /protectedPassword|truncated-old-cipher/);
  });
});

test('main resolves direct input but leaves remembered password resolution to utility owner', async () => {
  await withTempSettings(async (settingsDir) => {
    commitAuthenticatedCredentials(appRoot, {
      username: 'alice', rememberPassword: true, autoLogin: true, lastLoginTime: 200
    }, settingsDir);

    const direct = resolveCredentialsForAuthentication(appRoot, {
      username: 'alice', password: 'one-shot', remember: true, autoLogin: false
    }, settingsDir);
    const remembered = loadRememberedCredentials(appRoot, settingsDir, 'alice');

    assert.equal(direct.passwordForLaunch, 'one-shot');
    assert.equal(remembered.passwordForLaunch, '');
    assert.equal(remembered.remember, true);
    assert.equal(remembered.autoLogin, true);
  });
});

test('metadata commit writes no plaintext or ciphertext field', async () => {
  await withTempSettings(async (settingsDir) => {
    commitAuthenticatedCredentials(appRoot, {
      username: 'alice', rememberPassword: true, autoLogin: true, lastLoginTime: 300
    }, settingsDir);
    const text = await readFile(join(settingsDir, 'launcher-account.json'), 'utf8');

    assert.doesNotMatch(text, /protectedPassword|passwordForLaunch|cipher|secret/);
    assert.deepEqual(JSON.parse(text), {
      version: 5,
      lastUser: 'alice',
      autoLogin: true,
      accounts: [{ user: 'alice', rememberPassword: true, lastLoginTime: 300 }]
    });
  });
});

test('version 5 account stores with unknown secret fields are rewritten canonically', async () => {
  await withTempSettings(async (settingsDir) => {
    const accountFile = join(settingsDir, 'launcher-account.json');
    await writeFile(accountFile, `${JSON.stringify({
      version: 5,
      lastUser: 'alice',
      autoLogin: true,
      password: 'unexpected-cleartext',
      dpapi: 'unexpected-ciphertext',
      accounts: [{
        user: 'alice', rememberPassword: true, lastLoginTime: 500,
        secret: 'unexpected-account-secret'
      }]
    }, null, 2)}\n`, 'utf8');

    const state = loadNativeState(appRoot, settingsDir);
    assert.equal(state.login.user, 'alice');
    assert.equal(state.login.hasPassword, true);
    assert.deepEqual(JSON.parse(await readFile(accountFile, 'utf8')), {
      version: 5,
      lastUser: 'alice',
      autoLogin: true,
      accounts: [{ user: 'alice', rememberPassword: true, lastLoginTime: 500 }]
    });
  });
});
