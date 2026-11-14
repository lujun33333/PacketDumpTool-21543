import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

import { shouldUseAuthenticatedPreloadFallback } from '../../src/main/preload-fallback-policy.js';

const worker = await readFile(new URL('../../src/main/native-worker.js', import.meta.url), 'utf8');

test('only unavailable anonymous-v2 failures fall back to the authenticated launch path', () => {
  for (const code of ['NETWORK_ERROR', 'TIMEOUT', 'RUNTIME_MATERIAL_UNSUPPORTED', 'RUNTIME_MATERIAL_UNAVAILABLE']) {
    assert.equal(shouldUseAuthenticatedPreloadFallback({ code }), true, code);
  }
  for (const code of ['ABORTED', 'INVALID_INPUT', 'RUNTIME_MATERIAL_INVALID', 'RUNTIME_MATERIAL_EXPIRED']) {
    assert.equal(shouldUseAuthenticatedPreloadFallback({ code }), false, code);
  }
});

test('worker keeps startup usable and obtains v1 material after login when v2 preload is unavailable', () => {
  assert.match(worker, /shouldUseAuthenticatedPreloadFallback/);
  assert.match(worker, /deferredNativePreload\s*=\s*true/);
  assert.match(worker, /AUTHENTICATED_PRELOAD_FALLBACK/);

  const authenticate = worker.slice(
    worker.indexOf('async function authenticate'),
    worker.indexOf('\nfunction requestTerminalFailure')
  );
  const login = authenticate.indexOf('loginPrivateServer(');
  const material = authenticate.indexOf('runtimeMaterial: deferredNativePreload');
  const launch = authenticate.indexOf('startNativeGameFromMaterial(');
  const applyAuth = authenticate.indexOf('applyAuth(');
  assert.ok(login >= 0 && material > login && launch > material && applyAuth > launch,
    'fallback must authenticate, receive v1 material, create GameApp, then apply auth');
});
