import assert from 'node:assert/strict';
import test from 'node:test';

import {
  GameInstanceError,
  GameInstanceManager,
  MAX_BOUND_ACCOUNTS,
  makeGameInstanceKey
} from '../../src/main/game-instance-manager.js';

test('official multi-instance model permits many running instances but only one starting instance', () => {
  const manager = new GameInstanceManager();
  const first = manager.reserveStart({ user: '1', zone: '64', zoneLabel: '电信区', main: true });

  assert.equal(manager.startingKey, first.key);
  assert.throws(
    () => manager.reserveStart({ user: '2', zone: '64', zoneLabel: '电信区' }),
    (error) => error instanceof GameInstanceError && error.code === 'INSTANCE_START_IN_PROGRESS'
  );

  manager.markRunning(first.key, 10001);
  const second = manager.reserveStart({ user: '2', zone: '64', zoneLabel: '电信区' });
  manager.markRunning(second.key, 10002);

  assert.equal(manager.runningCount, 2);
  assert.equal(manager.startingKey, '');
});

test('same account and zone cannot be duplicated, while the same account may use another zone', () => {
  const manager = new GameInstanceManager();
  const first = manager.reserveStart({ user: '1', zone: '64', zoneLabel: '电信区' });
  manager.markRunning(first.key, 11001);

  assert.throws(
    () => manager.reserveStart({ user: '1', zone: '64', zoneLabel: '电信区' }),
    (error) => error instanceof GameInstanceError && error.code === 'ACCOUNT_ZONE_ALREADY_IN_USE'
  );

  const otherZone = manager.reserveStart({ user: '1', zone: '65', zoneLabel: '网通区' });
  assert.equal(otherZone.key, makeGameInstanceKey('1', '65'));
});

test('failed and exited reservations release the official global starting gate and can be retried', () => {
  const manager = new GameInstanceManager();
  const failed = manager.reserveStart({ user: '1', zone: '64' });
  manager.markFailed(failed.key, '启动失败');
  assert.equal(manager.startingKey, '');

  const retry = manager.reserveStart({ user: '1', zone: '64' });
  manager.markRunning(retry.key, 12001);
  manager.markExited(retry.key, 0);
  assert.equal(manager.runningCount, 0);

  assert.doesNotThrow(() => manager.reserveStart({ user: '1', zone: '64' }));
});

test('public snapshot keeps the main account first and exposes at most ten bound subaccounts', () => {
  const manager = new GameInstanceManager();
  const accounts = Array.from({ length: MAX_BOUND_ACCOUNTS + 4 }, (_, index) => ({
    user: String(index + 2),
    hasPassword: true
  }));
  const running = manager.reserveStart({ user: '2', zone: '64', zoneLabel: '电信区' });
  manager.markRunning(running.key, 13001);

  const snapshot = manager.snapshot({
    mainUser: '1',
    accounts: [{ user: '1', hasPassword: true }, ...accounts],
    servers: [{ id: '64', label: '电信区', available: true }]
  });

  assert.equal(snapshot.accounts[0].user, '1');
  assert.equal(snapshot.accounts[0].main, true);
  assert.equal(snapshot.accounts.filter((account) => !account.main).length, MAX_BOUND_ACCOUNTS);
  assert.deepEqual(snapshot.accounts.find((account) => account.user === '2').runningZones, ['电信区']);
  assert.equal(snapshot.runningCount, 1);
});

test('restored running process records participate in duplicate checks', () => {
  const manager = new GameInstanceManager();
  const restored = manager.restoreRunning({ user: '9', zone: '64', zoneLabel: '电信区', pid: 14001 });
  assert.equal(restored.restored, true);
  assert.equal(manager.runningCount, 1);
  assert.throws(
    () => manager.reserveStart({ user: '9', zone: '64' }),
    (error) => error?.code === 'ACCOUNT_ZONE_ALREADY_IN_USE'
  );
});
