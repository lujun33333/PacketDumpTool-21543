import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');
const main = readFileSync(resolve(root, 'src/main/index.js'), 'utf8');

function between(startMarker, endMarker) {
  const start = main.indexOf(startMarker);
  const end = main.indexOf(endMarker, start + startMarker.length);
  assert.ok(start >= 0 && end > start, `${startMarker} block is missing`);
  return main.slice(start, end);
}

test('outer preload timeout covers material request plus native GameApp preload budget', () => {
  assert.match(main, /const NATIVE_PRELOAD_TIMEOUT_MS\s*=\s*90_000/);
  assert.match(main, /waitForRecordPhase\(record, \['preloaded', 'authenticated'\], NATIVE_PRELOAD_TIMEOUT_MS\)/);
  assert.match(main, /waitForRecordPhase\(record, \['preloaded'\], NATIVE_PRELOAD_TIMEOUT_MS\)/);
});

test('cancel timeout forcibly reaps only a pending GameApp and its utility process', () => {
  const helper = between('function forceTerminatePendingNativeRecord(', 'async function cancelNativeSession(');
  assert.match(helper, /record\.runFinished|status\.phase === 'ready'/);
  assert.match(helper, /process\.kill\(gameProcessId, 'SIGTERM'\)/);
  assert.match(helper, /record\.child\.kill\(\)/);

  const primary = between('async function cancelNativeSession(', 'function normalizeInstanceLaunchRequest');
  assert.match(primary, /removeListener\('exit', onExit\)/);
  assert.match(primary, /forceTerminatePendingNativeRecord\(record/);

  const auxiliary = between('async function cancelAuxiliaryNativeSession(', 'function findPublicAccount');
  assert.match(auxiliary, /removeListener\('exit', onExit\)/);
  assert.match(auxiliary, /forceTerminatePendingNativeRecord\(record/);
});

test('preload timeout cancels the pinned single-flight record and quitting rejects new sessions', () => {
  const preload = between('async function preloadNativeGame()', 'async function authenticateNativeSession');
  assert.match(preload, /catch \(error\)[\s\S]*?cancelNativeSession\(/);

  const primaryFactory = between('async function createNativeSession()', 'async function preloadNativeGame()');
  assert.match(primaryFactory, /assertApplicationNotQuitting\(\)/);
  const auxiliaryFactory = between('async function createAuxiliaryNativeSession(', 'async function submitAuxiliaryCredentials');
  assert.match(auxiliaryFactory, /assertApplicationNotQuitting\(\)/);
});
