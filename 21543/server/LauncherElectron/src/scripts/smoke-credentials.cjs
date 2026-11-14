const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

const root = path.resolve(__dirname, '..', '..');

async function loadModule(relativePath) {
  return import(pathToFileURL(path.join(root, relativePath)).href);
}

async function main() {
  assert.equal(process.platform, 'win32', 'Credential DPAPI smoke currently targets Windows.');
  const [{ createCredentialOwner }, nativeCore] = await Promise.all([
    loadModule('src/main/credential-vault.js'),
    loadModule('src/main/native-core.js')
  ]);

  const systemTemp = path.resolve(os.tmpdir());
  const profileRoot = fs.mkdtempSync(path.join(systemTemp, 'qqspeed-credential-smoke-'));
  const settingsDir = path.join(profileRoot, 'settings');
  const accountFile = path.join(settingsDir, 'launcher-account.json');
  const vaultFile = path.join(settingsDir, 'launcher-credential-vault.json');
  const username = 'credential-smoke-user';
  const firstPassword = 'credential-smoke-密码-A';
  const changedPassword = 'credential-smoke-密码-B';

  try {
    fs.mkdirSync(settingsDir, { recursive: true });
    fs.writeFileSync(accountFile, `${JSON.stringify({
      version: 4,
      lastUser: username,
      autoLogin: true,
      accounts: [{
        user: username,
        protectedPassword: 'incompatible-old-ciphertext',
        rememberPassword: true,
        lastLoginTime: 1
      }]
    }, null, 2)}\n`, 'utf8');

    const migrated = nativeCore.loadNativeState(root, settingsDir);
    const migratedText = fs.readFileSync(accountFile, 'utf8');
    assert.equal(migrated.login.hasPassword, false);
    assert.equal(migrated.login.autoLogin, false);
    assert.doesNotMatch(migratedText, /protectedPassword|incompatible-old-ciphertext/);

    const owner = createCredentialOwner(settingsDir);
    const first = await owner.resolve({
      username,
      password: firstPassword,
      remember: true,
      autoLogin: true
    });
    assert.equal(fs.existsSync(vaultFile), false, 'Credentials must not persist before authentication succeeds.');
    const firstOutcome = await owner.commitAuthenticated(first);
    nativeCore.commitAuthenticatedCredentials(root, firstOutcome.state, settingsDir);
    first.password = '';

    const firstVaultText = fs.readFileSync(vaultFile, 'utf8');
    const firstDpapi = JSON.parse(firstVaultText).accounts[0].dpapi;
    const accountText = fs.readFileSync(accountFile, 'utf8');
    assert.doesNotMatch(firstVaultText, new RegExp(firstPassword));
    assert.doesNotMatch(accountText, /protectedPassword|dpapi|cipher/i);
    assert.equal(nativeCore.loadNativeState(root, settingsDir).login.autoLogin, true);

    const restartedOwner = createCredentialOwner(settingsDir);
    const remembered = await restartedOwner.resolve({
      username,
      password: '',
      remember: true,
      autoLogin: true
    });
    assert.equal(remembered.password, firstPassword);
    remembered.password = '';

    const rejected = await restartedOwner.resolve({
      username,
      password: 'rejected-password-must-not-persist',
      remember: true,
      autoLogin: true
    });
    const beforeRejected = fs.readFileSync(vaultFile, 'utf8');
    rejected.password = '';
    assert.equal(fs.readFileSync(vaultFile, 'utf8'), beforeRejected);
    assert.doesNotMatch(beforeRejected, /rejected-password-must-not-persist/);

    const changed = await restartedOwner.resolve({
      username,
      password: changedPassword,
      remember: true,
      autoLogin: true
    });
    const changedOutcome = await restartedOwner.commitAuthenticated(changed);
    nativeCore.commitAuthenticatedCredentials(root, changedOutcome.state, settingsDir);
    changed.password = '';
    const changedVaultText = fs.readFileSync(vaultFile, 'utf8');
    const changedDpapi = JSON.parse(changedVaultText).accounts[0].dpapi;
    assert.notEqual(changedDpapi, firstDpapi);
    assert.doesNotMatch(changedVaultText, new RegExp(changedPassword));

    const forgotten = await restartedOwner.resolve({
      username,
      password: changedPassword,
      remember: false,
      autoLogin: true
    });
    const forgottenOutcome = await restartedOwner.commitAuthenticated(forgotten);
    nativeCore.commitAuthenticatedCredentials(root, forgottenOutcome.state, settingsDir);
    forgotten.password = '';
    assert.equal(await restartedOwner.vault.load(username), '');
    const finalState = nativeCore.loadNativeState(root, settingsDir);
    assert.equal(finalState.login.hasPassword, false);
    assert.equal(finalState.login.autoLogin, false);

    console.log('CREDENTIAL_DPAPI_SMOKE_OK');
  } finally {
    const resolvedProfileRoot = path.resolve(profileRoot);
    if (!resolvedProfileRoot.startsWith(`${systemTemp}${path.sep}`)) {
      throw new Error(`Refusing to remove unexpected smoke profile: ${resolvedProfileRoot}`);
    }
    fs.rmSync(resolvedProfileRoot, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 });
  }
}

main().catch((error) => {
  console.error(error?.stack || error);
  process.exitCode = 1;
});
