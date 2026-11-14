export const AUTH_PHASES = Object.freeze([
  'booting',
  'login-ready',
  'authenticating',
  'authenticated',
  'switching',
  'error'
]);

export const GAME_PHASES = Object.freeze([
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

export const CONTENT_SCOPES = Object.freeze(['bootstrap', 'home']);
export const CONTENT_STATUSES = Object.freeze([
  'loading',
  'ready',
  'stale',
  'error'
]);

export class AuthSessionRequestGate {
  #authEpoch = 0;
  #requests = new Map();

  advanceAuthSession() {
    this.#authEpoch += 1;
    this.#requests.delete('home');
    return this.#authEpoch;
  }

  capture(scope) {
    return scope === 'home' ? this.#authEpoch : 0;
  }

  isCurrent(scope, epoch) {
    return scope !== 'home' || epoch === this.#authEpoch;
  }

  has(scope) {
    return this.#requests.has(scope);
  }

  get(scope) {
    return this.#requests.get(scope);
  }

  set(scope, request) {
    this.#requests.set(scope, request);
    return request;
  }

  deleteIfCurrentRequest(scope, request) {
    if (this.#requests.get(scope) !== request) {
      return false;
    }
    return this.#requests.delete(scope);
  }
}

export function isRoutineLoginPreloadStatus(status, authPhase) {
  if (String(status?.phase || '').toLowerCase() !== 'preloaded' || authPhase === 'authenticated') {
    return false;
  }
  if (Object.hasOwn(status || {}, 'code') && Number(status.code) !== 0) {
    return false;
  }
  const message = String(status?.message || '').trim();
  return !message || (message.includes('GameApp 已预加载') && message.includes('等待账号登录'));
}

const SUPPORTED_EVENTS = new Set([
  'BOOT_READY',
  'BOOT_FAILED',
  'SESSION_RESTORED',
  'AUTH_REQUESTED',
  'AUTH_SUCCEEDED',
  'AUTH_FAILED',
  'AUTH_RETRY',
  'AUTH_EXPIRED',
  'SWITCH_ACCOUNT_REQUESTED',
  'SWITCH_ACCOUNT_COMPLETED',
  'SWITCH_ACCOUNT_FAILED',
  'GAME_DISCOVERY_STARTED',
  'GAME_MISSING',
  'NATIVE_PRELOAD_STARTED',
  'NATIVE_PRELOADED',
  'NATIVE_AUTHENTICATED',
  'START_REQUESTED',
  'GAME_RUNNING',
  'GAME_EXITED',
  'GAME_FAILED',
  'GAME_CANCELLED',
  'GAME_RETRY',
  'BOOTSTRAP_CONTENT_READY',
  'BOOTSTRAP_CONTENT_FAILED',
  'BOOTSTRAP_CONTENT_RETRY',
  'HOME_CONTENT_READY',
  'HOME_CONTENT_STALE',
  'HOME_CONTENT_FAILED',
  'HOME_CONTENT_RETRY'
]);

const BOOTSTRAP_LOADING = Object.freeze({
  scope: 'bootstrap',
  status: 'loading',
  data: null,
  error: null
});

const LOAD_BOOTSTRAP_EFFECT = Object.freeze(['load-bootstrap']);
const LOAD_HOME_EFFECT = Object.freeze(['load-home']);
const NO_EFFECTS = Object.freeze([]);

function freezeState(state) {
  Object.freeze(state.auth);
  Object.freeze(state.game);
  Object.freeze(state.content);
  return Object.freeze(state);
}

function withAuth(state, phase, error = null) {
  return freezeState({
    ...state,
    auth: { phase, error }
  });
}

function withGame(state, phase, error = null) {
  return freezeState({
    ...state,
    game: { phase, error }
  });
}

function withContent(state, content) {
  return freezeState({
    ...state,
    content
  });
}

function eventError(event, fallback) {
  const message = String(event?.error ?? '').trim();
  return message || fallback;
}

function invalidAuthTransition(state, type) {
  throw new Error(`非法认证状态转换：${state.auth.phase} -> ${type}`);
}

function invalidGameTransition(state, type) {
  throw new Error(`非法游戏状态转换：${state.game.phase} -> ${type}`);
}

function invalidContentTransition(state, type) {
  throw new Error(
    `非法内容状态转换：${state.content.scope}/${state.content.status} -> ${type}`
  );
}

function resetForLogin(state, authPhase, authError = null) {
  const gamePhase = ['unknown', 'missing'].includes(state.game.phase)
    ? state.game.phase
    : 'cancelled';
  return freezeState({
    auth: { phase: authPhase, error: authError },
    game: { phase: gamePhase, error: null },
    content: { ...BOOTSTRAP_LOADING }
  });
}

function reduceAuth(state, event) {
  const { type } = event;
  switch (type) {
    case 'BOOT_READY':
      if (state.auth.phase !== 'booting') {
        return invalidAuthTransition(state, type);
      }
      return withAuth(state, 'login-ready');
    case 'BOOT_FAILED':
      if (state.auth.phase !== 'booting') {
        return invalidAuthTransition(state, type);
      }
      return withAuth(state, 'error', eventError(event, '启动器初始化失败'));
    case 'SESSION_RESTORED':
      if (!['booting', 'login-ready', 'error'].includes(state.auth.phase)) {
        return invalidAuthTransition(state, type);
      }
      return freezeState({
        ...state,
        auth: { phase: 'authenticated', error: null },
        content: {
          scope: 'home',
          status: 'loading',
          data: null,
          error: null
        }
      });
    case 'AUTH_REQUESTED':
      if (state.auth.phase !== 'login-ready') {
        return invalidAuthTransition(state, type);
      }
      return withAuth(state, 'authenticating');
    case 'AUTH_SUCCEEDED':
      if (state.auth.phase !== 'authenticating') {
        return invalidAuthTransition(state, type);
      }
      return freezeState({
        ...state,
        auth: { phase: 'authenticated', error: null },
        content: {
          scope: 'home',
          status: 'loading',
          data: null,
          error: null
        }
      });
    case 'AUTH_FAILED':
      if (state.auth.phase !== 'authenticating') {
        return invalidAuthTransition(state, type);
      }
      return withAuth(state, 'error', eventError(event, '登录失败'));
    case 'AUTH_RETRY':
      if (state.auth.phase !== 'error') {
        return invalidAuthTransition(state, type);
      }
      return withAuth(state, 'login-ready');
    case 'AUTH_EXPIRED':
      if (state.auth.phase !== 'authenticated') {
        return invalidAuthTransition(state, type);
      }
      return resetForLogin(state, 'error', eventError(event, '登录状态已失效'));
    case 'SWITCH_ACCOUNT_REQUESTED':
      if (state.auth.phase !== 'authenticated') {
        return invalidAuthTransition(state, type);
      }
      return resetForLogin(state, 'switching');
    case 'SWITCH_ACCOUNT_COMPLETED':
      if (state.auth.phase !== 'switching') {
        return invalidAuthTransition(state, type);
      }
      return withAuth(state, 'login-ready');
    case 'SWITCH_ACCOUNT_FAILED':
      if (state.auth.phase !== 'switching') {
        return invalidAuthTransition(state, type);
      }
      return withAuth(
        state,
        'error',
        eventError(event, '切换账号失败')
      );
    default:
      return null;
  }
}

function assertGamePhase(state, eventType, allowedPhases) {
  if (!allowedPhases.includes(state.game.phase)) {
    invalidGameTransition(state, eventType);
  }
}

function reduceGame(state, event) {
  const { type } = event;
  switch (type) {
    case 'GAME_DISCOVERY_STARTED':
      assertGamePhase(state, type, ['unknown', 'missing', 'failed', 'cancelled']);
      return withGame(state, 'preparing');
    case 'GAME_MISSING':
      assertGamePhase(state, type, ['preparing']);
      return withGame(state, 'missing');
    case 'NATIVE_PRELOAD_STARTED':
      assertGamePhase(state, type, ['preparing']);
      return withGame(state, 'preloading');
    case 'NATIVE_PRELOADED':
      assertGamePhase(state, type, ['preloading']);
      return withGame(state, 'preloaded');
    case 'NATIVE_AUTHENTICATED':
      assertGamePhase(state, type, ['preloaded']);
      if (state.auth.phase !== 'authenticated') {
        invalidGameTransition(state, type);
      }
      return withGame(state, 'authenticated');
    case 'START_REQUESTED':
      assertGamePhase(state, type, ['authenticated', 'exited']);
      if (state.auth.phase !== 'authenticated') {
        invalidGameTransition(state, type);
      }
      return withGame(state, 'starting');
    case 'GAME_RUNNING':
      assertGamePhase(state, type, ['starting']);
      return withGame(state, 'running');
    case 'GAME_EXITED':
      assertGamePhase(state, type, ['running']);
      return withGame(state, 'exited');
    case 'GAME_FAILED':
      assertGamePhase(state, type, [
        'preparing',
        'preloading',
        'preloaded',
        'authenticated',
        'starting',
        'running',
        'exited'
      ]);
      return withGame(state, 'failed', eventError(event, '游戏组件失败'));
    case 'GAME_CANCELLED':
      assertGamePhase(state, type, [
        'preparing',
        'preloading',
        'preloaded',
        'authenticated',
        'starting',
        'exited',
        'failed'
      ]);
      return withGame(state, 'cancelled');
    case 'GAME_RETRY':
      assertGamePhase(state, type, ['missing', 'failed', 'cancelled']);
      return withGame(state, 'preparing');
    default:
      return null;
  }
}

function assertContentScope(state, eventType, scope) {
  if (state.content.scope !== scope) {
    invalidContentTransition(state, eventType);
  }
}

function reduceContent(state, event) {
  const { type } = event;
  switch (type) {
    case 'BOOTSTRAP_CONTENT_READY':
      assertContentScope(state, type, 'bootstrap');
      return withContent(state, {
        scope: 'bootstrap',
        status: 'ready',
        data: event.data ?? null,
        error: null
      });
    case 'BOOTSTRAP_CONTENT_FAILED':
      assertContentScope(state, type, 'bootstrap');
      return withContent(state, {
        ...state.content,
        status: 'error',
        error: eventError(event, '启动内容加载失败')
      });
    case 'BOOTSTRAP_CONTENT_RETRY':
      assertContentScope(state, type, 'bootstrap');
      if (state.content.status !== 'error') {
        return invalidContentTransition(state, type);
      }
      return withContent(state, {
        ...state.content,
        status: 'loading',
        error: null
      });
    case 'HOME_CONTENT_READY':
      assertContentScope(state, type, 'home');
      return withContent(state, {
        scope: 'home',
        status: 'ready',
        data: event.data ?? null,
        error: null
      });
    case 'HOME_CONTENT_STALE':
      assertContentScope(state, type, 'home');
      if (state.content.status !== 'ready') {
        return invalidContentTransition(state, type);
      }
      return withContent(state, {
        ...state.content,
        status: 'stale',
        error: null
      });
    case 'HOME_CONTENT_FAILED':
      assertContentScope(state, type, 'home');
      if (!['loading', 'ready', 'stale'].includes(state.content.status)) {
        return invalidContentTransition(state, type);
      }
      return withContent(state, {
        ...state.content,
        status: 'error',
        error: eventError(event, '主页内容加载失败')
      });
    case 'HOME_CONTENT_RETRY':
      assertContentScope(state, type, 'home');
      if (state.content.status !== 'error') {
        return invalidContentTransition(state, type);
      }
      return withContent(state, {
        ...state.content,
        status: 'loading',
        error: null
      });
    default:
      return null;
  }
}

export function createOfficialLauncherState() {
  return freezeState({
    auth: { phase: 'booting', error: null },
    game: { phase: 'unknown', error: null },
    content: { ...BOOTSTRAP_LOADING }
  });
}

export function officialLauncherReducer(state, event) {
  const type = String(event?.type ?? '');
  if (!SUPPORTED_EVENTS.has(type)) {
    throw new Error(`不支持的启动器事件：${type || '(empty)'}`);
  }

  const normalizedEvent = { ...event, type };
  return (
    reduceAuth(state, normalizedEvent) ??
    reduceGame(state, normalizedEvent) ??
    reduceContent(state, normalizedEvent)
  );
}

function resetGameState(state) {
  return freezeState({
    ...state,
    game: { phase: 'unknown', error: null }
  });
}

/**
 * Converts the authoritative main-process launch snapshot into the renderer
 * game model. Rebuilding the game branch from reducer events makes reconnects
 * and missed intermediate IPC notifications deterministic without allowing a
 * native status to authenticate the user session.
 */
export function reduceNativeLaunchStatus(
  state,
  status,
  { hasGameRoot = true } = {}
) {
  const phase = String(status?.phase || '').trim().toLowerCase();

  if (!hasGameRoot) {
    let next = resetGameState(state);
    next = officialLauncherReducer(next, { type: 'GAME_DISCOVERY_STARTED' });
    return officialLauncherReducer(next, { type: 'GAME_MISSING' });
  }

  if (phase === 'idle' && state.game.phase === 'exited') {
    return state;
  }
  if (!phase || phase === 'idle') {
    return resetGameState(state);
  }

  const knownPhases = new Set([
    'preparing',
    'preloading',
    'preloaded',
    'authenticating',
    'authenticated',
    'starting',
    'ready',
    'game-exited',
    'failed',
    'error',
    'cancelled'
  ]);
  if (!knownPhases.has(phase)) {
    return state;
  }

  let next = resetGameState(state);
  next = officialLauncherReducer(next, { type: 'GAME_DISCOVERY_STARTED' });

  if (['failed', 'error'].includes(phase)) {
    return officialLauncherReducer(next, {
      type: 'GAME_FAILED',
      error: eventError({ error: status?.error ?? status?.message }, '游戏组件失败')
    });
  }
  if (phase === 'cancelled') {
    return officialLauncherReducer(next, { type: 'GAME_CANCELLED' });
  }
  if (phase === 'preparing') {
    return next;
  }

  next = officialLauncherReducer(next, { type: 'NATIVE_PRELOAD_STARTED' });
  if (phase === 'preloading') {
    return next;
  }

  next = officialLauncherReducer(next, { type: 'NATIVE_PRELOADED' });
  if (['preloaded', 'authenticating'].includes(phase)) {
    return next;
  }

  // Native authentication is necessary but never sufficient to establish a
  // launcher user session. Only main's session event/snapshot may do that.
  if (next.auth.phase !== 'authenticated') {
    return next;
  }
  next = officialLauncherReducer(next, { type: 'NATIVE_AUTHENTICATED' });
  if (phase === 'authenticated') {
    return next;
  }

  next = officialLauncherReducer(next, { type: 'START_REQUESTED' });
  if (phase === 'starting') {
    return next;
  }

  next = officialLauncherReducer(next, { type: 'GAME_RUNNING' });
  if (phase === 'ready') {
    return next;
  }
  return officialLauncherReducer(next, { type: 'GAME_EXITED' });
}

export function selectLauncherView(state) {
  return state.auth.phase === 'authenticated' ? 'home' : 'login';
}

export function selectContentEffects(state) {
  if (state.content.scope === 'bootstrap') {
    return state.content.status === 'loading'
      ? LOAD_BOOTSTRAP_EFFECT
      : NO_EFFECTS;
  }
  return ['loading', 'stale'].includes(state.content.status)
    ? LOAD_HOME_EFFECT
    : NO_EFFECTS;
}

function button(label, disabled, action, unsupported = false) {
  return Object.freeze({ label, disabled, action, unsupported });
}

export function selectGameButton(state) {
  switch (state.game.phase) {
    case 'unknown':
    case 'preparing':
    case 'preloading':
      return button('加载中', true, 'loading');
    case 'missing':
      return button('未找到游戏', true, 'unsupported', true);
    case 'preloaded':
      return state.auth.phase === 'authenticated'
        ? button('加载中', true, 'loading')
        : button('登录后开始游戏', true, 'need-login');
    case 'authenticated':
      return state.auth.phase === 'authenticated'
        ? button('开始游戏', false, 'start')
        : button('登录后开始游戏', true, 'need-login');
    case 'starting':
      return button('开始游戏', true, 'starting');
    case 'running':
      return button('游戏中', true, 'running');
    case 'exited':
      return state.auth.phase === 'authenticated'
        ? button('开始游戏', false, 'start')
        : button('登录后开始游戏', true, 'need-login');
    case 'failed':
    case 'cancelled':
      return button('重新加载', false, 'retry');
    default:
      throw new Error(`未知游戏状态：${String(state.game.phase)}`);
  }
}
