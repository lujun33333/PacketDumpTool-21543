import assert from 'node:assert/strict';
import { access, readFile } from 'node:fs/promises';
import test from 'node:test';

const root = new URL('../../', import.meta.url);
const source = (relativePath) => readFile(new URL(relativePath, root), 'utf8');

test('only native worker imports the credential vault in production main sources', async () => {
  const [index, core, worker] = await Promise.all([
    source('src/main/index.js'),
    source('src/main/native-core.js'),
    source('src/main/native-worker.js')
  ]);

  assert.doesNotMatch(index, /credential-vault/);
  assert.doesNotMatch(core, /credential-vault/);
  assert.match(worker, /credential-vault/);
  assert.doesNotMatch(index, /safeStorage\s*[?.]|CryptProtectData|CryptUnprotectData/);
  assert.doesNotMatch(core, /safeStorage\s*[?.]|CryptProtectData|CryptUnprotectData/);
});

test('main account metadata never writes password ciphertext fields', async () => {
  const [policy, core] = await Promise.all([
    source('src/main/credential-policy.js'),
    source('src/main/native-core.js')
  ]);

  assert.doesNotMatch(policy, /protectedPassword\s*:/);
  assert.doesNotMatch(core, /safeStorage\s*[?.]|encryptString|decryptString/);
  assert.match(core, /launcher-account\.json/);
});

test('worker session receives settings directory and login credential preferences', async () => {
  const [index, worker] = await Promise.all([
    source('src/main/index.js'),
    source('src/main/native-worker.js')
  ]);

  assert.match(index, /type:\s*'start-session'[\s\S]*?settingsDir:\s*getSettingsDir\(\)/);
  assert.match(index, /type:\s*'login'[\s\S]*?remember:[\s\S]*?autoLogin:/);
  assert.match(worker, /createCredentialOwner\(payload\.settingsDir/);
  assert.match(worker, /type:\s*'credential-state'/);
});

test('worker commits vault state only after server and native authentication, then reports authenticated', async () => {
  const worker = await source('src/main/native-worker.js');
  const checkpoints = [
    'await loginPrivateServer({',
    'bindings.applyAuth(',
    'await waitForNativeState(4',
    'await credentialOwner.commitAuthenticated(',
    "type: 'credential-state'",
    "transition('auth-succeeded')",
    "command: 'login', ok: true"
  ];
  let previous = -1;
  for (const checkpoint of checkpoints) {
    const offset = worker.indexOf(checkpoint);
    assert.ok(offset > previous, `${checkpoint} must follow the previous authentication checkpoint`);
    previous = offset;
  }
});

test('worker attempts native preload at startup before credentials are resolved', async () => {
  const worker = await source('src/main/native-worker.js');
  const initialize = worker.slice(
    worker.indexOf('async function initializeSession'),
    worker.indexOf('\nasync function waitForNativeState')
  );
  const authenticate = worker.slice(
    worker.indexOf('async function authenticate'),
    worker.indexOf('\nfunction requestTerminalFailure')
  );
  assert.match(initialize, /await preloadNativeGame\(nativeContext/);
  assert.doesNotMatch(initialize, /credentialOwner\.resolve\(/);
  assert.match(authenticate, /credentialOwner\.resolve\(/);
});

test('start game rechecks current GameApp readiness before issuing native start', async () => {
  const index = await source('src/main/index.js');
  assert.match(
    index,
    /async function startNativeGame\([^)]*\)[\s\S]*?prepareGameApp\(appRoot, getSettingsDir\(\)\)[\s\S]*?record\.child\.postMessage\(\{ type: 'start-game' \}\)/
  );
});

test('credential smoke is pure Node and cannot start Electron or GameApp', async () => {
  const smoke = await source('src/scripts/smoke-credentials.cjs');
  assert.doesNotMatch(smoke, /electron(?:\.exe)?|child_process|\bspawn\s*\(|GameApp/i);
  assert.match(smoke, /credential-vault\.js/);
  assert.match(smoke, /DPAPI/);
  await assert.rejects(access(new URL('src/scripts/credential-storage-smoke-main.js', root)), { code: 'ENOENT' });
  await assert.rejects(access(new URL('src/scripts/credential-storage-smoke-app/main.cjs', root)), { code: 'ENOENT' });
});
