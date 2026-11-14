import test from 'node:test';
import assert from 'node:assert/strict';

import {
  commitAuthenticatedCredentialMetadata,
  hasRememberedCredentialForUser,
  reconcileAuthenticatedCredentialPersistence,
  normalizeAccountStore,
  persistAuthenticatedCredential,
  removeAccountCredentialMetadata,
  resolveCredentialRequest,
  toPublicLoginState
} from '../../src/main/credential-policy.js';

test('binding a multi-open account can preserve the selected main account', () => {
  const committed = commitAuthenticatedCredentialMetadata({
    version: 5,
    lastUser: 'main',
    autoLogin: true,
    accounts: [{ user: 'main', rememberPassword: true, lastLoginTime: 10 }]
  }, {
    username: 'bound',
    rememberPassword: true,
    autoLogin: false,
    lastLoginTime: 20
  }, { selectUser: false });

  assert.equal(committed.lastUser, 'main');
  assert.equal(committed.autoLogin, true);
  assert.deepEqual(committed.accounts.map((entry) => entry.user), ['bound', 'main']);
});

test('unbinding account metadata preserves the main selection and clears a removed selection', () => {
  const source = {
    version: 5,
    lastUser: 'main',
    autoLogin: true,
    accounts: [
      { user: 'main', rememberPassword: true, lastLoginTime: 10 },
      { user: 'bound', rememberPassword: true, lastLoginTime: 20 }
    ]
  };
  const withoutBound = removeAccountCredentialMetadata(source, 'bound');
  assert.equal(withoutBound.lastUser, 'main');
  assert.equal(withoutBound.autoLogin, true);
  assert.deepEqual(withoutBound.accounts.map((entry) => entry.user), ['main']);

  const withoutMain = removeAccountCredentialMetadata(source, 'main');
  assert.equal(withoutMain.lastUser, 'bound');
  assert.equal(withoutMain.autoLogin, false);
});

test('remembered-session retention is scoped to the exact authenticated account', () => {
  const login = {
    user: 'alice',
    hasPassword: true,
    rememberPassword: true,
    accounts: [
      { user: 'alice', hasPassword: true, rememberPassword: true },
      { user: 'bob', hasPassword: false, rememberPassword: false }
    ]
  };

  assert.equal(hasRememberedCredentialForUser(login, 'alice'), true);
  assert.equal(hasRememberedCredentialForUser(login, 'bob'), false);
  assert.equal(hasRememberedCredentialForUser(login, 'charlie'), false);
  assert.equal(hasRememberedCredentialForUser({
    user: 'alice',
    hasPassword: true,
    rememberPassword: true
  }, 'alice'), true);
  assert.equal(hasRememberedCredentialForUser(login, ''), false);
});

test('legacy Electron ciphertext is discarded and disables remember and auto login', () => {
  const store = normalizeAccountStore({
    version: 4,
    lastUser: 'alice',
    autoLogin: true,
    accounts: [
      {
        user: 'alice',
        protectedPassword: 'legacy-safe-storage-ciphertext',
        rememberPassword: true,
        lastLoginTime: 42
      },
      { user: 'bob', rememberPassword: true, lastLoginTime: 21 }
    ]
  });

  assert.deepEqual(store, {
    version: 5,
    lastUser: 'alice',
    autoLogin: false,
    accounts: [
      { user: 'alice', rememberPassword: false, lastLoginTime: 42 },
      { user: 'bob', rememberPassword: true, lastLoginTime: 21 }
    ]
  });
  assert.doesNotMatch(JSON.stringify(store), /protectedPassword|legacy-safe-storage-ciphertext/);
});

test('metadata-only account state preserves a remembered account and auto login', () => {
  const store = normalizeAccountStore({
    version: 5,
    lastUser: 'alice',
    autoLogin: true,
    accounts: [{ user: 'alice', rememberPassword: true, lastLoginTime: 42 }]
  });

  assert.deepEqual(store, {
    version: 5,
    lastUser: 'alice',
    autoLogin: true,
    accounts: [{ user: 'alice', rememberPassword: true, lastLoginTime: 42 }]
  });
});

test('direct login request remains transient and contains no persisted ciphertext', () => {
  const stored = normalizeAccountStore({
    lastUser: 'alice',
    accounts: [{ user: 'alice', rememberPassword: false }]
  });
  const snapshot = structuredClone(stored);

  const candidate = resolveCredentialRequest(stored, {
    username: 'alice',
    password: 'new-secret',
    remember: true,
    autoLogin: false
  });

  assert.deepEqual(stored, snapshot);
  assert.deepEqual(candidate, {
    username: 'alice',
    password: 'new-secret',
    remember: true,
    autoLogin: false
  });
  assert.doesNotMatch(JSON.stringify(stored), /new-secret|protectedPassword/i);
});

test('blank password is forwarded only for a selected remembered account', () => {
  const stored = normalizeAccountStore({
    accounts: [
      { user: 'alice', rememberPassword: true },
      { user: 'bob', rememberPassword: false }
    ],
    lastUser: 'alice'
  });

  assert.deepEqual(resolveCredentialRequest(stored, {
    username: 'alice', password: '', remember: true, autoLogin: true
  }), {
    username: 'alice', password: '', remember: true, autoLogin: true
  });
  assert.throws(() => resolveCredentialRequest(stored, {
    username: 'bob', password: '', remember: true
  }), /重新输入密码|密码不能为空/);
  assert.throws(() => resolveCredentialRequest(stored, {
    username: 'alice', password: '', remember: false
  }), /重新输入密码|密码不能为空/);
});

test('successful worker credential state commits metadata only', () => {
  const stored = normalizeAccountStore({
    accounts: [{ user: 'other', rememberPassword: true, lastLoginTime: 1 }]
  });
  const committed = commitAuthenticatedCredentialMetadata(stored, {
    username: 'alice', rememberPassword: true, autoLogin: true, lastLoginTime: 123456
  });

  assert.equal(committed.version, 5);
  assert.equal(committed.lastUser, 'alice');
  assert.equal(committed.autoLogin, true);
  assert.deepEqual(committed.accounts[0], {
    user: 'alice', rememberPassword: true, lastLoginTime: 123456
  });
  assert.equal(committed.accounts[1].user, 'other');
  assert.doesNotMatch(JSON.stringify(committed), /protectedPassword|cipher|secret|"password"\s*:/i);
});

test('worker credential failure state disables remember and auto login', () => {
  const stored = normalizeAccountStore({
    lastUser: 'alice',
    autoLogin: true,
    accounts: [{ user: 'alice', rememberPassword: true, lastLoginTime: 1 }]
  });
  const committed = commitAuthenticatedCredentialMetadata(stored, {
    username: 'alice', rememberPassword: false, autoLogin: true, lastLoginTime: 88
  });

  assert.equal(committed.autoLogin, false);
  assert.equal(committed.accounts[0].rememberPassword, false);
});

test('public login DTO exposes only account metadata', () => {
  const stored = normalizeAccountStore({
    lastUser: 'alice',
    autoLogin: true,
    accounts: [{ user: 'alice', rememberPassword: true, lastLoginTime: 42 }]
  });
  const publicState = toPublicLoginState(stored);

  assert.deepEqual(publicState, {
    user: 'alice',
    hasPassword: true,
    rememberPassword: true,
    autoLogin: true,
    lastLoginTime: 42,
    accounts: [{ user: 'alice', hasPassword: true, rememberPassword: true, lastLoginTime: 42 }]
  });
  assert.doesNotMatch(JSON.stringify(publicState), /protectedPassword|cipher|secret/);
});

test('post-auth metadata persistence failure is a warning, not a failed login', () => {
  const observed = [];
  const result = persistAuthenticatedCredential({
    persist: () => { throw new Error('disk full'); },
    fallback: () => ({ user: '', accounts: [] }),
    onError: (error) => observed.push(error.message)
  });

  assert.deepEqual(result.login, { user: '', accounts: [] });
  assert.equal(result.persisted, false);
  assert.match(result.warning, /账号已登录.*保存失败/);
  assert.deepEqual(observed, ['disk full']);
});

test('post-auth persistence remains non-fatal when fallback metadata cannot be read', () => {
  const result = persistAuthenticatedCredential({
    persist: () => { throw new Error('read only'); },
    fallback: () => { throw new Error('unreadable'); }
  });

  assert.equal(result.login.user, '');
  assert.deepEqual(result.login.accounts, []);
  assert.equal(result.persisted, false);
  assert.match(result.warning, /账号已登录/);
});

test('metadata success needs no utility cleanup and preserves remembered state', async () => {
  let forgetCalls = 0;
  const result = await reconcileAuthenticatedCredentialPersistence({
    username: 'alice',
    credentialState: { username: 'alice', rememberPassword: true, lastLoginTime: 42 },
    persist: () => ({
      user: 'alice',
      hasPassword: true,
      rememberPassword: true,
      autoLogin: true,
      accounts: [{ user: 'alice', hasPassword: true, rememberPassword: true, lastLoginTime: 42 }]
    }),
    forget: async () => { forgetCalls += 1; }
  });

  assert.equal(result.metadataPersisted, true);
  assert.equal(result.remembered, true);
  assert.equal(result.vaultCleanup, 'not-required');
  assert.equal(result.warning, '');
  assert.equal(forgetCalls, 0);
});

test('metadata failure waits for utility cleanup and returns an explicit nonremembered DTO', async () => {
  let cleanupFinished = false;
  const result = await reconcileAuthenticatedCredentialPersistence({
    username: 'alice',
    credentialState: { username: 'alice', rememberPassword: true, lastLoginTime: 88 },
    persist: () => { throw new Error('account file locked'); },
    fallback: () => ({
      user: 'alice',
      hasPassword: true,
      rememberPassword: true,
      autoLogin: true,
      accounts: [{ user: 'alice', hasPassword: true, rememberPassword: true, lastLoginTime: 1 }]
    }),
    forget: async () => { cleanupFinished = true; }
  });

  assert.equal(cleanupFinished, true);
  assert.equal(result.metadataPersisted, false);
  assert.equal(result.remembered, false);
  assert.equal(result.vaultCleanup, 'succeeded');
  assert.equal(result.login.user, 'alice');
  assert.equal(result.login.hasPassword, false);
  assert.equal(result.login.rememberPassword, false);
  assert.equal(result.login.autoLogin, false);
  assert.equal(result.login.accounts[0].hasPassword, false);
  assert.match(result.warning, /保存失败/);
  assert.match(result.warning, /已清除/);
  assert.doesNotMatch(result.warning, /清理失败|仍保留/);
});

test('cleanup failure is surfaced honestly and never claims the encrypted copy was cleared', async () => {
  const cleanupError = new Error('vault locked');
  const observed = [];
  const result = await reconcileAuthenticatedCredentialPersistence({
    username: 'alice',
    credentialState: { username: 'alice', rememberPassword: true, lastLoginTime: 99 },
    persist: () => { throw new Error('metadata read-only'); },
    fallback: () => ({
      user: 'alice',
      hasPassword: true,
      rememberPassword: true,
      autoLogin: true,
      accounts: [{ user: 'alice', hasPassword: true, rememberPassword: true }]
    }),
    forget: async () => { throw cleanupError; },
    onCleanupError: (error) => observed.push(error)
  });

  assert.equal(result.metadataPersisted, false);
  assert.equal(result.remembered, false);
  assert.equal(result.vaultCleanup, 'failed');
  assert.equal(result.login.hasPassword, false);
  assert.match(result.warning, /清理失败/);
  assert.match(result.warning, /可能仍保留/);
  assert.doesNotMatch(result.warning, /已清除/);
  assert.deepEqual(observed, [cleanupError]);
});
