import assert from 'node:assert/strict';
import { isProcessAlive, watchProcessByPid } from '../main/game-process-monitor.js';

assert.equal(isProcessAlive(process.pid), true, '当前 Node 进程应可被 PID 监测');
assert.equal(isProcessAlive(0), false, 'PID 0 不应被当作游戏进程');

const samples = [true, false, true, false, false];
const exits = [];
const watcher = watchProcessByPid(4242, (pid) => exits.push(pid), {
  pollIntervalMs: 60_000,
  missingThreshold: 2,
  isAlive: () => samples.shift() ?? false
});

assert.equal(watcher.check(), true, '存活样本不应触发退出');
assert.equal(watcher.check(), true, '单次丢失应被防抖');
assert.equal(watcher.check(), true, '再次存活应清零丢失计数');
assert.equal(watcher.check(), true, '首次连续丢失不应立即退出');
assert.equal(watcher.check(), false, '连续两次丢失应确认退出');
assert.deepEqual(exits, [4242], '退出回调必须且只能触发一次');
assert.equal(watcher.stopped, true, '退出后监测器应停止');
assert.equal(watcher.check(), false, '停止后不应重复回调');
assert.deepEqual(exits, [4242]);

console.log('LIFECYCLE_SMOKE_OK');
