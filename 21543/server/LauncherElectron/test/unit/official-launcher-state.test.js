import assert from 'node:assert/strict';
import test from 'node:test';

import {
  AuthSessionRequestGate,
  AUTH_PHASES,
  CONTENT_SCOPES,
  CONTENT_STATUSES,
  GAME_PHASES,
  createOfficialLauncherState,
  isRoutineLoginPreloadStatus,
  officialLauncherReducer,
  reduceNativeLaunchStatus,
  selectContentEffects,
  selectGameButton,
  selectLauncherView
} from '../../src/renderer/official-launcher-state.js';

test('routine preloaded status clears login progress without hiding real warnings', () => {
  assert.equal(isRoutineLoginPreloadStatus({
    phase: 'preloaded',
    message: 'GameApp 已预加载，正在等待账号登录。'
  }, 'login-ready'), true);
  assert.equal(isRoutineLoginPreloadStatus({ phase: 'preloaded', message: '' }, 'login-ready'), true);
  assert.equal(isRoutineLoginPreloadStatus({
    phase: 'preloaded',
    message: '账号或密码错误',
    code: 1
  }, 'error'), false);
  assert.equal(isRoutineLoginPreloadStatus({
    phase: 'preloaded',
    message: 'GameApp 已预加载，正在等待账号登录。'
  }, 'authenticated'), false);
});

function reduce(state, ...events) {
  return events.reduce(
    (current, event) => officialLauncherReducer(current, event),
    state
  );
}

test('initial state mounts only bootstrap content and requests no home content', () => {
  const state = createOfficialLauncherState();

  assert.deepEqual(state, {
    auth: { phase: 'booting', error: null },
    game: { phase: 'unknown', error: null },
    content: {
      scope: 'bootstrap',
      status: 'loading',
      data: null,
      error: null
    }
  });
  assert.equal(selectLauncherView(state), 'login');
  assert.deepEqual(selectContentEffects(state), ['load-bootstrap']);
});

test('successful authentication is the only transition that mounts and loads home', () => {
  const initial = createOfficialLauncherState();
  const loginReady = reduce(
    initial,
    { type: 'BOOT_READY' },
    { type: 'BOOTSTRAP_CONTENT_READY', data: { background: 'login.jpg' } }
  );
  const authenticating = officialLauncherReducer(loginReady, {
    type: 'AUTH_REQUESTED'
  });

  assert.equal(selectLauncherView(loginReady), 'login');
  assert.equal(selectLauncherView(authenticating), 'login');
  assert.deepEqual(selectContentEffects(loginReady), []);
  assert.deepEqual(selectContentEffects(authenticating), []);

  const authenticated = officialLauncherReducer(authenticating, {
    type: 'AUTH_SUCCEEDED'
  });
  assert.equal(authenticated.auth.phase, 'authenticated');
  assert.deepEqual(authenticated.content, {
    scope: 'home',
    status: 'loading',
    data: null,
    error: null
  });
  assert.equal(selectLauncherView(authenticated), 'home');
  assert.deepEqual(selectContentEffects(authenticated), ['load-home']);
});

test('authoritative session restoration mounts home without a renderer login shortcut', () => {
  const restored = officialLauncherReducer(createOfficialLauncherState(), {
    type: 'SESSION_RESTORED'
  });

  assert.equal(restored.auth.phase, 'authenticated');
  assert.equal(selectLauncherView(restored), 'home');
  assert.equal(restored.content.scope, 'home');
  assert.deepEqual(selectContentEffects(restored), ['load-home']);
});

test('an authenticated main-process session restores the home branch after renderer reload', () => {
  const state = reduce(
    createOfficialLauncherState(),
    { type: 'BOOT_READY' },
    { type: 'SESSION_RESTORED' }
  );

  assert.equal(state.auth.phase, 'authenticated');
  assert.equal(state.content.scope, 'home');
  assert.equal(state.content.status, 'loading');
  assert.equal(selectLauncherView(state), 'home');
  assert.deepEqual(selectContentEffects(state), ['load-home']);
});

test('authentication failure stays on login and retry clears the error', () => {
  const authenticating = reduce(
    createOfficialLauncherState(),
    { type: 'BOOT_READY' },
    { type: 'AUTH_REQUESTED' }
  );
  const failed = officialLauncherReducer(authenticating, {
    type: 'AUTH_FAILED',
    error: '账号或密码错误'
  });

  assert.deepEqual(failed.auth, {
    phase: 'error',
    error: '账号或密码错误'
  });
  assert.equal(failed.content.scope, 'bootstrap');
  assert.equal(selectLauncherView(failed), 'login');
  assert.notDeepEqual(selectContentEffects(failed), ['load-home']);

  const retry = officialLauncherReducer(failed, { type: 'AUTH_RETRY' });
  assert.deepEqual(retry.auth, { phase: 'login-ready', error: null });
});

test('switch account unmounts home and returns through switching to login-ready', () => {
  const authenticated = reduce(
    createOfficialLauncherState(),
    { type: 'BOOT_READY' },
    { type: 'AUTH_REQUESTED' },
    { type: 'AUTH_SUCCEEDED' },
    { type: 'HOME_CONTENT_READY', data: { ads: [1] } }
  );
  const switching = officialLauncherReducer(authenticated, {
    type: 'SWITCH_ACCOUNT_REQUESTED'
  });

  assert.equal(switching.auth.phase, 'switching');
  assert.equal(switching.content.scope, 'bootstrap');
  assert.equal(selectLauncherView(switching), 'login');
  assert.deepEqual(selectContentEffects(switching), ['load-bootstrap']);

  const ready = officialLauncherReducer(switching, {
    type: 'SWITCH_ACCOUNT_COMPLETED'
  });
  assert.deepEqual(ready.auth, { phase: 'login-ready', error: null });
});

test('game button matches official ready/login/start/running labels', () => {
  let state = reduce(
    createOfficialLauncherState(),
    { type: 'BOOT_READY' },
    { type: 'GAME_DISCOVERY_STARTED' },
    { type: 'NATIVE_PRELOAD_STARTED' }
  );
  assert.deepEqual(selectGameButton(state), {
    label: '加载中',
    disabled: true,
    action: 'loading',
    unsupported: false
  });

  state = officialLauncherReducer(state, { type: 'NATIVE_PRELOADED' });
  assert.deepEqual(selectGameButton(state), {
    label: '登录后开始游戏',
    disabled: true,
    action: 'need-login',
    unsupported: false
  });

  state = reduce(
    state,
    { type: 'AUTH_REQUESTED' },
    { type: 'AUTH_SUCCEEDED' },
    { type: 'NATIVE_AUTHENTICATED' }
  );
  assert.deepEqual(selectGameButton(state), {
    label: '开始游戏',
    disabled: false,
    action: 'start',
    unsupported: false
  });

  state = officialLauncherReducer(state, { type: 'START_REQUESTED' });
  assert.deepEqual(selectGameButton(state), {
    label: '开始游戏',
    disabled: true,
    action: 'starting',
    unsupported: false
  });

  state = officialLauncherReducer(state, { type: 'GAME_RUNNING' });
  assert.deepEqual(selectGameButton(state), {
    label: '游戏中',
    disabled: true,
    action: 'running',
    unsupported: false
  });
});

test('missing game is explicitly unsupported instead of inventing install/update flows', () => {
  const state = reduce(
    createOfficialLauncherState(),
    { type: 'GAME_DISCOVERY_STARTED' },
    { type: 'GAME_MISSING' }
  );

  assert.deepEqual(selectGameButton(state), {
    label: '未找到游戏',
    disabled: true,
    action: 'unsupported',
    unsupported: true
  });
  assert.throws(
    () => officialLauncherReducer(state, { type: 'INSTALL_REQUESTED' }),
    /不支持的启动器事件/
  );
  assert.throws(
    () => officialLauncherReducer(state, { type: 'UPDATE_REQUESTED' }),
    /不支持的启动器事件/
  );
});

test('game exit can immediately start again and a failed native session can retry preload', () => {
  let state = reduce(
    createOfficialLauncherState(),
    { type: 'BOOT_READY' },
    { type: 'AUTH_REQUESTED' },
    { type: 'AUTH_SUCCEEDED' },
    { type: 'GAME_DISCOVERY_STARTED' },
    { type: 'NATIVE_PRELOAD_STARTED' },
    { type: 'NATIVE_PRELOADED' },
    { type: 'NATIVE_AUTHENTICATED' },
    { type: 'START_REQUESTED' },
    { type: 'GAME_RUNNING' },
    { type: 'GAME_EXITED' }
  );
  assert.equal(state.game.phase, 'exited');
  assert.equal(selectGameButton(state).disabled, false);
  assert.equal(selectGameButton(state).label, '开始游戏');

  state = officialLauncherReducer(state, { type: 'START_REQUESTED' });
  assert.equal(state.game.phase, 'starting');

  state = officialLauncherReducer(state, {
    type: 'GAME_FAILED',
    error: 'GameApp 启动失败'
  });
  assert.deepEqual(state.game, {
    phase: 'failed',
    error: 'GameApp 启动失败'
  });
  assert.deepEqual(selectGameButton(state), {
    label: '重新加载',
    disabled: false,
    action: 'retry',
    unsupported: false
  });

  state = officialLauncherReducer(state, { type: 'GAME_RETRY' });
  assert.deepEqual(state.game, { phase: 'preparing', error: null });
});

test('game cancellation is recoverable but cannot bypass authentication', () => {
  let state = reduce(
    createOfficialLauncherState(),
    { type: 'BOOT_READY' },
    { type: 'GAME_DISCOVERY_STARTED' },
    { type: 'NATIVE_PRELOAD_STARTED' },
    { type: 'GAME_CANCELLED' }
  );
  assert.equal(state.game.phase, 'cancelled');
  assert.equal(selectGameButton(state).action, 'retry');

  state = officialLauncherReducer(state, { type: 'GAME_RETRY' });
  assert.equal(state.game.phase, 'preparing');
  assert.throws(
    () => officialLauncherReducer(state, { type: 'START_REQUESTED' }),
    /非法游戏状态转换/
  );
});

test('authoritative native statuses map to the official game model', () => {
  const loginReady = officialLauncherReducer(createOfficialLauncherState(), {
    type: 'BOOT_READY'
  });
  const missing = reduceNativeLaunchStatus(loginReady, { phase: 'idle' }, {
    hasGameRoot: false
  });
  assert.equal(missing.game.phase, 'missing');
  assert.equal(selectGameButton(missing).action, 'unsupported');

  const preloaded = reduceNativeLaunchStatus(loginReady, { phase: 'preloaded' }, {
    hasGameRoot: true
  });
  assert.equal(preloaded.game.phase, 'preloaded');
  assert.equal(selectGameButton(preloaded).label, '登录后开始游戏');

  const restored = officialLauncherReducer(createOfficialLauncherState(), {
    type: 'SESSION_RESTORED'
  });
  const authenticated = reduceNativeLaunchStatus(restored, {
    phase: 'authenticated'
  }, { hasGameRoot: true });
  assert.equal(authenticated.game.phase, 'authenticated');
  assert.deepEqual(selectGameButton(authenticated), {
    label: '开始游戏',
    disabled: false,
    action: 'start',
    unsupported: false
  });

  const starting = reduceNativeLaunchStatus(restored, { phase: 'starting' }, {
    hasGameRoot: true
  });
  assert.deepEqual(selectGameButton(starting), {
    label: '开始游戏',
    disabled: true,
    action: 'starting',
    unsupported: false
  });

  const running = reduceNativeLaunchStatus(restored, { phase: 'ready' }, {
    hasGameRoot: true
  });
  assert.equal(running.game.phase, 'running');
  assert.equal(selectGameButton(running).label, '游戏中');

  const exited = reduceNativeLaunchStatus(restored, { phase: 'game-exited' }, {
    hasGameRoot: true
  });
  assert.equal(exited.game.phase, 'exited');
  assert.equal(selectGameButton(exited).disabled, false);
  assert.equal(
    reduceNativeLaunchStatus(exited, { phase: 'idle' }, { hasGameRoot: true }),
    exited
  );

  const failed = reduceNativeLaunchStatus(restored, {
    phase: 'failed',
    message: 'Hook 会话失败'
  }, {
    hasGameRoot: true
  });
  assert.equal(failed.game.phase, 'failed');
  assert.equal(failed.game.error, 'Hook 会话失败');
  assert.equal(selectGameButton(failed).action, 'retry');

  const cancelled = reduceNativeLaunchStatus(restored, { phase: 'cancelled' }, {
    hasGameRoot: true
  });
  assert.equal(cancelled.game.phase, 'cancelled');
  assert.equal(selectGameButton(cancelled).action, 'retry');
});

test('native authentication cannot make the game startable without an authoritative user session', () => {
  const loginReady = officialLauncherReducer(createOfficialLauncherState(), {
    type: 'BOOT_READY'
  });
  const state = reduceNativeLaunchStatus(loginReady, { phase: 'authenticated' }, {
    hasGameRoot: true
  });

  assert.equal(state.game.phase, 'preloaded');
  assert.deepEqual(selectGameButton(state), {
    label: '登录后开始游戏',
    disabled: true,
    action: 'need-login',
    unsupported: false
  });
});

test('home content supports ready, stale revalidation, error, and explicit retry', () => {
  let state = reduce(
    createOfficialLauncherState(),
    { type: 'BOOT_READY' },
    { type: 'AUTH_REQUESTED' },
    { type: 'AUTH_SUCCEEDED' },
    { type: 'HOME_CONTENT_READY', data: { ads: ['a'] } }
  );
  assert.deepEqual(state.content, {
    scope: 'home',
    status: 'ready',
    data: { ads: ['a'] },
    error: null
  });
  assert.deepEqual(selectContentEffects(state), []);

  state = officialLauncherReducer(state, { type: 'HOME_CONTENT_STALE' });
  assert.equal(state.content.status, 'stale');
  assert.deepEqual(state.content.data, { ads: ['a'] });
  assert.deepEqual(selectContentEffects(state), ['load-home']);

  state = officialLauncherReducer(state, {
    type: 'HOME_CONTENT_FAILED',
    error: 'network unavailable'
  });
  assert.equal(state.content.status, 'error');
  assert.deepEqual(state.content.data, { ads: ['a'] });
  assert.equal(state.content.error, 'network unavailable');
  assert.deepEqual(selectContentEffects(state), []);

  state = officialLauncherReducer(state, { type: 'HOME_CONTENT_RETRY' });
  assert.equal(state.content.status, 'loading');
  assert.deepEqual(selectContentEffects(state), ['load-home']);
});

test('auth session epoch detaches a pending home request and protects the fresh relogin request', async () => {
  const gate = new AuthSessionRequestGate();
  let resolveOld;
  let resolveFresh;
  const oldRequest = new Promise((resolve) => { resolveOld = resolve; });
  const oldEpoch = gate.capture('home');
  gate.set('home', oldRequest);

  gate.advanceAuthSession();
  assert.equal(gate.has('home'), false);
  assert.equal(gate.isCurrent('home', oldEpoch), false);

  const freshRequest = new Promise((resolve) => { resolveFresh = resolve; });
  const freshEpoch = gate.capture('home');
  gate.set('home', freshRequest);
  assert.equal(gate.get('home'), freshRequest);
  assert.equal(gate.isCurrent('home', freshEpoch), true);

  resolveOld('old-home');
  assert.equal(await oldRequest, 'old-home');
  assert.equal(gate.deleteIfCurrentRequest('home', oldRequest), false);
  assert.equal(gate.get('home'), freshRequest);

  resolveFresh('fresh-home');
  assert.equal(await freshRequest, 'fresh-home');
  assert.equal(gate.deleteIfCurrentRequest('home', freshRequest), true);
  assert.equal(gate.has('home'), false);
});

test('invalid transitions fail closed without mutating the previous state', () => {
  const state = createOfficialLauncherState();
  const snapshot = structuredClone(state);

  assert.throws(
    () => officialLauncherReducer(state, { type: 'AUTH_SUCCEEDED' }),
    /非法认证状态转换/
  );
  assert.deepEqual(state, snapshot);
  assert.throws(
    () => officialLauncherReducer(state, { type: '' }),
    /不支持的启动器事件/
  );
  assert.throws(
    () => officialLauncherReducer(state, null),
    /不支持的启动器事件/
  );
});

test('exported phase sets cover the complete supported model', () => {
  assert.deepEqual(AUTH_PHASES, [
    'booting',
    'login-ready',
    'authenticating',
    'authenticated',
    'switching',
    'error'
  ]);
  assert.deepEqual(GAME_PHASES, [
    'unknown',
    'missing',
    'preparing',
    'preloading',
    'preloaded',
    'authenticated',
    'starting',
    'running',
    'exited',
    'failed',
    'cancelled'
  ]);
  assert.deepEqual(CONTENT_SCOPES, ['bootstrap', 'home']);
  assert.deepEqual(CONTENT_STATUSES, ['loading', 'ready', 'stale', 'error']);
});
