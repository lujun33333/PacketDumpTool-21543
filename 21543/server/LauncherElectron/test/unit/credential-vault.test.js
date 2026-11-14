import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import {
  CredentialVaultError,
  createCredentialOwner,
  createCredentialVault,
  protectPasswordWithDpapi,
  unprotectPasswordWithDpapi
} from '../../src/main/credential-vault.js';

async function withTempDir(run) {
  const dir = await mkdtemp(join(tmpdir(), 'qqspeed-vault-test-'));
  try {
    return await run(dir);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}

function fakeDpapi() {
  return {
    protect: (password) => Buffer.from(`dpapi:${password}`, 'utf8').toString('base64'),
    unprotect: (ciphertext) => {
      const decoded = Buffer.from(ciphertext, 'base64').toString('utf8');
      if (!decoded.startsWith('dpapi:')) throw new Error('bad cipher');
      return decoded.slice(6);
    }
  };
}

test('vault atomically stores only per-account protected bytes', async () => {
  await withTempDir(async (settingsDir) => {
    const vault = createCredentialVault(settingsDir, fakeDpapi());
    await vault.store('alice', 'secret-A');

    const file = join(settingsDir, 'launcher-credential-vault.json');
    const text = await readFile(file, 'utf8');
    const parsed = JSON.parse(text);
    assert.equal(parsed.version, 1);
    assert.equal(parsed.accounts[0].user, 'alice');
    assert.ok(parsed.accounts[0].dpapi.length > 0);
    assert.doesNotMatch(text, /secret-A/);
    assert.equal(await vault.load('alice'), 'secret-A');
    assert.deepEqual((await readdir(settingsDir)).filter((name) => name.endsWith('.tmp')), []);
  });
});

test('vault replaces and deletes only the selected account', async () => {
  await withTempDir(async (settingsDir) => {
    const vault = createCredentialVault(settingsDir, fakeDpapi());
    await vault.store('alice', 'old');
    await vault.store('bob', 'bob-secret');
    await vault.store('alice', 'new');

    assert.equal(await vault.load('alice'), 'new');
    assert.equal(await vault.load('bob'), 'bob-secret');
    assert.equal(await vault.remove('alice'), true);
    assert.equal(await vault.load('alice'), '');
    assert.equal(await vault.load('bob'), 'bob-secret');
  });
});

test('credential owner resolves blank passwords inside utility ownership only', async () => {
  await withTempDir(async (settingsDir) => {
    const owner = createCredentialOwner(settingsDir, fakeDpapi());
    await owner.vault.store('alice', 'remembered-secret');

    const credentials = await owner.resolve({
      username: 'alice', password: '', remember: true, autoLogin: true
    });
    assert.deepEqual(credentials, {
      username: 'alice', password: 'remembered-secret', remember: true, autoLogin: true
    });
    await assert.rejects(
      owner.resolve({ username: 'bob', password: '', remember: true }),
      (error) => error instanceof CredentialVaultError && error.code === 'CREDENTIAL_NOT_FOUND'
    );
  });
});

test('credential owner exposes a utility-only idempotent forget command', async () => {
  await withTempDir(async (settingsDir) => {
    const owner = createCredentialOwner(settingsDir, fakeDpapi());
    await owner.vault.store('alice', 'remembered-secret');
    assert.equal(await owner.forget('alice'), true);
    assert.equal(await owner.vault.load('alice'), '');
    assert.equal(await owner.forget('alice'), false);
  });
});

test('credential owner persists only after authentication and returns metadata state', async () => {
  await withTempDir(async (settingsDir) => {
    const owner = createCredentialOwner(settingsDir, fakeDpapi());
    const before = await owner.resolve({
      username: 'alice', password: 'new-secret', remember: true, autoLogin: true
    });
    assert.equal(await owner.vault.load('alice'), '');

    const outcome = await owner.commitAuthenticated(before, { now: () => 123 });
    assert.deepEqual(outcome, {
      state: {
        username: 'alice', rememberPassword: true, autoLogin: true, lastLoginTime: 123
      },
      warning: ''
    });
    assert.equal(await owner.vault.load('alice'), 'new-secret');
    assert.doesNotMatch(JSON.stringify(outcome), /new-secret|"password"\s*:/i);
  });
});

test('post-auth vault failure keeps authentication successful but disables remembered metadata', async () => {
  await withTempDir(async (settingsDir) => {
    const owner = createCredentialOwner(settingsDir, {
      protect: () => { throw new Error('DPAPI unavailable'); },
      unprotect: fakeDpapi().unprotect
    });
    const outcome = await owner.commitAuthenticated({
      username: 'alice', password: 'one-shot', remember: true, autoLogin: true
    }, { now: () => 456 });

    assert.deepEqual(outcome.state, {
      username: 'alice', rememberPassword: false, autoLogin: false, lastLoginTime: 456
    });
    assert.match(outcome.warning, /已登录.*密码.*未.*记住/);
  });
});

test('non-remembered successful authentication removes an older vault entry', async () => {
  await withTempDir(async (settingsDir) => {
    const owner = createCredentialOwner(settingsDir, fakeDpapi());
    await owner.vault.store('alice', 'old-secret');
    const outcome = await owner.commitAuthenticated({
      username: 'alice', password: 'one-shot', remember: false, autoLogin: true
    }, { now: () => 789 });

    assert.equal(await owner.vault.load('alice'), '');
    assert.deepEqual(outcome.state, {
      username: 'alice', rememberPassword: false, autoLogin: false, lastLoginTime: 789
    });
  });
});

test('malformed vault fails closed on load but a fresh authenticated password repairs it', async () => {
  await withTempDir(async (settingsDir) => {
    const file = join(settingsDir, 'launcher-credential-vault.json');
    await writeFile(file, '{"version":1,"accounts":[', 'utf8');
    const owner = createCredentialOwner(settingsDir, fakeDpapi());

    await assert.rejects(
      owner.vault.load('alice'),
      (error) => error instanceof CredentialVaultError && error.code === 'VAULT_INVALID'
    );

    const outcome = await owner.commitAuthenticated({
      username: 'alice', password: 'fresh-secret', remember: true, autoLogin: true
    }, { now: () => 901 });
    assert.equal(outcome.warning, '');
    assert.equal(outcome.state.rememberPassword, true);
    assert.equal(await owner.vault.load('alice'), 'fresh-secret');
    assert.doesNotMatch(await readFile(file, 'utf8'), /fresh-secret/);
  });
});

test('forgetting a password replaces a malformed vault with an empty canonical store', async () => {
  await withTempDir(async (settingsDir) => {
    const file = join(settingsDir, 'launcher-credential-vault.json');
    await writeFile(file, 'not-json', 'utf8');
    const owner = createCredentialOwner(settingsDir, fakeDpapi());

    const outcome = await owner.commitAuthenticated({
      username: 'alice', password: 'one-shot', remember: false, autoLogin: true
    }, { now: () => 902 });
    assert.equal(outcome.warning, '');
    assert.deepEqual(JSON.parse(await readFile(file, 'utf8')), { version: 1, accounts: [] });
  });
});

test('vault rejects unknown secret fields and a fresh store rewrites an exact canonical schema', async () => {
  await withTempDir(async (settingsDir) => {
    const file = join(settingsDir, 'launcher-credential-vault.json');
    const dpapi = Buffer.from('dpapi:old-secret', 'utf8').toString('base64');
    await writeFile(file, JSON.stringify({
      version: 1,
      accounts: [{ user: 'alice', dpapi, password: 'plaintext-residue' }]
    }), 'utf8');
    const vault = createCredentialVault(settingsDir, fakeDpapi());

    await assert.rejects(
      vault.load('alice'),
      (error) => error instanceof CredentialVaultError && error.code === 'VAULT_INVALID'
    );

    await vault.store('alice', 'fresh-secret');
    const repairedText = await readFile(file, 'utf8');
    const repaired = JSON.parse(repairedText);
    assert.deepEqual(Object.keys(repaired).sort(), ['accounts', 'version']);
    assert.deepEqual(Object.keys(repaired.accounts[0]).sort(), ['dpapi', 'user']);
    assert.doesNotMatch(repairedText, /plaintext-residue|fresh-secret|"password"/i);
    assert.equal(await vault.load('alice'), 'fresh-secret');
  });
});

test('remove repairs dirty, duplicate, invalid and over-limit vault schemas instead of silently retaining them', async () => {
  await withTempDir(async (settingsDir) => {
    const file = join(settingsDir, 'launcher-credential-vault.json');
    const validDpapi = Buffer.from('dpapi:value', 'utf8').toString('base64');
    const malformedStores = [
      { version: 1, accounts: [{ user: ' alice ', dpapi: validDpapi }] },
      { version: 1, accounts: [
        { user: 'alice', dpapi: validDpapi },
        { user: 'alice', dpapi: validDpapi }
      ] },
      { version: 1, accounts: [{ user: 'alice', dpapi: 'not-base64' }] },
      { version: 1, accounts: Array.from({ length: 13 }, (_, index) => ({
        user: `user-${index}`,
        dpapi: validDpapi
      })) },
      { version: 1, accounts: [], unexpected: 'secret-residue' }
    ];

    for (const malformed of malformedStores) {
      await writeFile(file, JSON.stringify(malformed), 'utf8');
      const vault = createCredentialVault(settingsDir, fakeDpapi());
      assert.equal(await vault.remove('alice'), true);
      assert.deepEqual(JSON.parse(await readFile(file, 'utf8')), { version: 1, accounts: [] });
    }
  });
});

test('real vault I/O errors are not mistaken for a repairable format error', async () => {
  await withTempDir(async (settingsDir) => {
    await mkdir(join(settingsDir, 'launcher-credential-vault.json'));
    const owner = createCredentialOwner(settingsDir, fakeDpapi());
    const outcome = await owner.commitAuthenticated({
      username: 'alice', password: 'one-shot', remember: true, autoLogin: true
    }, { now: () => 903 });

    assert.notEqual(outcome.warning, '');
    assert.equal(outcome.state.rememberPassword, false);
  });
});

test('Windows DPAPI protects and restores an exact UTF-8 password', {
  skip: process.platform !== 'win32'
}, () => {
  const password = `DPAPI-roundtrip-密码-${process.pid}-${Date.now()}`;
  const protectedValue = protectPasswordWithDpapi(password);
  assert.ok(protectedValue.length > 0);
  assert.notEqual(protectedValue, Buffer.from(password, 'utf8').toString('base64'));
  assert.equal(unprotectPasswordWithDpapi(protectedValue), password);
});
