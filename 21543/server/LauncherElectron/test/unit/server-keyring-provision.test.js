import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = new URL('../../', import.meta.url);
const source = (relativePath) => readFileSync(new URL(relativePath, root), 'utf8');

test('offline release build has no persisted build-secret or server-keyring tooling', () => {
  const packageJson = JSON.parse(source('package.json'));
  const buildScript = source('src/scripts/build-runtime-bundle.cjs');
  const provisionUrl = new URL('src/scripts/provision-server-runtime-keyring.cjs', root);

  assert.equal(existsSync(fileURLToPath(provisionUrl)), false);
  assert.equal(Object.hasOwn(packageJson.scripts, 'provision:server-runtime-keyring'), false);
  assert.doesNotMatch(String(packageJson.scripts.check), /provision-server-runtime-keyring/);
  assert.doesNotMatch(buildScript, /QQS_RUNTIME_BUNDLE_SECRET_FILE|loadBuildSecret|secretFile/);
});
