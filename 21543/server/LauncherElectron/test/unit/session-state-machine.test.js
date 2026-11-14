import test from 'node:test';
import assert from 'node:assert/strict';

import {
  ExclusiveAttemptGuard,
  LauncherSessionStateMachine,
  shouldExpireAuthenticatedSessionAfterReauth
} from '../../src/main/session-state-machine.js';

test('exclusive login ownership is acquired synchronously and only its owner can release it', async () => {
  const guard = new ExclusiveAttemptGuard();
  const first = guard.acquire();
  assert.ok(first);
  assert.equal(guard.active, true);

  await Promise.resolve();
  assert.equal(guard.acquire(), null);
  assert.equal(guard.release(Object.freeze({})), false);
  assert.equal(guard.owns(first), true);
  assert.equal(guard.active, true);

  assert.equal(guard.release(first), true);
  assert.equal(guard.active, false);
  const retry = guard.acquire();
  assert.ok(retry);
  assert.notEqual(retry, first);
});

test('JS state machine owns the official-equivalent happy path', () => {
  const machine = new LauncherSessionStateMachine();
  for (const [event, expected] of [
    ['begin', 'preloading'],
    ['native-preloaded', 'preloaded'],
    ['login', 'authenticating'],
    ['auth-succeeded', 'authenticated'],
    ['start', 'starting'],
    ['native-ready', 'ready']
  ]) {
    assert.equal(machine.transition(event).phase, expected);
  }
});

test('credential rejection returns to preloaded and permits a retry', () => {
  const machine = new LauncherSessionStateMachine();
  machine.transition('begin');
  machine.transition('native-preloaded');
  machine.transition('login');
  assert.equal(machine.transition('auth-rejected').phase, 'preloaded');
  assert.equal(machine.transition('login').phase, 'authenticating');
  assert.equal(machine.transition('auth-succeeded').phase, 'authenticated');
});

test('invalid transitions fail closed without mutating state', () => {
  const machine = new LauncherSessionStateMachine();
  assert.throws(() => machine.transition('start'), /非法会话状态转换/);
  assert.equal(machine.phase, 'idle');
});

test('failure and cancellation are terminal', () => {
  const failed = new LauncherSessionStateMachine();
  failed.transition('begin');
  assert.equal(failed.transition('native-failed').phase, 'failed');
  assert.throws(() => failed.transition('native-preloaded'), /非法会话状态转换/);

  const cancelled = new LauncherSessionStateMachine();
  cancelled.transition('begin');
  assert.equal(cancelled.transition('cancel').phase, 'cancelled');
  assert.throws(() => cancelled.transition('login'), /非法会话状态转换/);
});

test('only stable unrecoverable remembered-credential failures expire the authoritative session', () => {
  for (const code of [
    'CREDENTIAL_NOT_FOUND',
    'CREDENTIAL_UNAVAILABLE',
    'VAULT_INVALID',
    'INVALID_INPUT',
    'INVALID_CREDENTIALS',
    'ACCOUNT_UNAVAILABLE'
  ]) {
    assert.equal(shouldExpireAuthenticatedSessionAfterReauth({ code }), true, code);
  }
  for (const code of [
    'VAULT_READ_FAILED',
    'TIMEOUT',
    'NETWORK_ERROR',
    'PROTOCOL_ERROR',
    'LOGIN_BUSY',
    '',
    null
  ]) {
    assert.equal(shouldExpireAuthenticatedSessionAfterReauth({ code }), false, String(code));
  }
  assert.equal(shouldExpireAuthenticatedSessionAfterReauth(null), false);
});
