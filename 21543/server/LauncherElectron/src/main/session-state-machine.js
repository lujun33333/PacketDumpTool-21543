const TRANSITIONS = Object.freeze({
  idle: Object.freeze({ begin: 'preloading', cancel: 'cancelled' }),
  preloading: Object.freeze({
    'native-preloaded': 'preloaded',
    'native-failed': 'failed',
    cancel: 'cancelled'
  }),
  preloaded: Object.freeze({
    login: 'authenticating',
    'native-failed': 'failed',
    cancel: 'cancelled'
  }),
  authenticating: Object.freeze({
    'auth-succeeded': 'authenticated',
    'auth-rejected': 'preloaded',
    'native-failed': 'failed',
    cancel: 'cancelled'
  }),
  authenticated: Object.freeze({
    start: 'starting',
    'native-failed': 'failed',
    cancel: 'cancelled'
  }),
  starting: Object.freeze({
    'native-ready': 'ready',
    'native-failed': 'failed',
    cancel: 'cancelled'
  }),
  ready: Object.freeze({}),
  failed: Object.freeze({}),
  cancelled: Object.freeze({})
});

const UNRECOVERABLE_REAUTH_CODES = new Set([
  'CREDENTIAL_NOT_FOUND',
  'CREDENTIAL_UNAVAILABLE',
  'VAULT_INVALID',
  'INVALID_INPUT',
  'INVALID_CREDENTIALS',
  'ACCOUNT_UNAVAILABLE'
]);

export const SESSION_PHASES = Object.freeze(Object.keys(TRANSITIONS));

export function shouldExpireAuthenticatedSessionAfterReauth(error) {
  const code = typeof error?.code === 'string' ? error.code.trim().toUpperCase() : '';
  return UNRECOVERABLE_REAUTH_CODES.has(code);
}

export class ExclusiveAttemptGuard {
  #owner = null;

  acquire() {
    if (this.#owner) {
      return null;
    }
    const token = Object.freeze({});
    this.#owner = token;
    return token;
  }

  owns(token) {
    return Boolean(token) && this.#owner === token;
  }

  release(token) {
    if (!this.owns(token)) {
      return false;
    }
    this.#owner = null;
    return true;
  }

  get active() {
    return Boolean(this.#owner);
  }
}

export class LauncherSessionStateMachine {
  #phase = 'idle';

  get phase() {
    return this.#phase;
  }

  transition(event) {
    const normalizedEvent = String(event || '');
    const next = TRANSITIONS[this.#phase]?.[normalizedEvent];
    if (!next) {
      throw new Error(`非法会话状态转换：${this.#phase} -> ${normalizedEvent || '(empty)'}`);
    }
    this.#phase = next;
    return Object.freeze({ phase: this.#phase });
  }
}
