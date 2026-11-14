const MAX_ACCOUNT_BYTES = 64;
const MAX_PASSWORD_BYTES = 64;
const MAX_ACCOUNTS = 12;

function normalizeUser(value) {
  return String(value || '').trim();
}

function normalizeTimestamp(value) {
  const timestamp = Number(value);
  return Number.isSafeInteger(timestamp) && timestamp > 0 ? timestamp : 0;
}

function owns(object, property) {
  return Boolean(object && typeof object === 'object' && Object.hasOwn(object, property));
}

export function hasLegacyProtectedPassword(parsed) {
  if (owns(parsed, 'protectedPassword')) {
    return true;
  }
  return Array.isArray(parsed?.accounts) && parsed.accounts.some((entry) => owns(entry, 'protectedPassword'));
}

export function hasRememberedCredentialForUser(loginState, userInput) {
  const user = normalizeUser(userInput);
  if (!user || !loginState || typeof loginState !== 'object') {
    return false;
  }

  const accounts = Array.isArray(loginState.accounts) ? loginState.accounts : [];
  const account = accounts.find((entry) => normalizeUser(entry?.user) === user);
  if (account) {
    return Boolean(account.hasPassword ?? account.rememberPassword);
  }

  return normalizeUser(loginState.user) === user &&
    Boolean(loginState.hasPassword ?? loginState.rememberPassword);
}

function normalizeAccount(entry) {
  const user = normalizeUser(entry?.user);
  // Electron safeStorage ciphertext belonged to the old main-process owner. The
  // utility process cannot decrypt it, so migration intentionally requires one
  // fresh login instead of preserving a misleading remember flag.
  const legacyCiphertext = owns(entry, 'protectedPassword');
  return {
    user,
    rememberPassword: legacyCiphertext ? false : Boolean(entry?.rememberPassword),
    lastLoginTime: normalizeTimestamp(entry?.lastLoginTime)
  };
}

export function normalizeAccountStore(parsed = {}) {
  const accounts = [];
  const seen = new Set();
  const append = (entry) => {
    const normalized = normalizeAccount(entry);
    if (!normalized.user || seen.has(normalized.user)) {
      return;
    }
    seen.add(normalized.user);
    accounts.push(normalized);
  };

  if (Array.isArray(parsed?.accounts)) {
    parsed.accounts.forEach(append);
  }
  append(parsed);

  const requestedUser = normalizeUser(parsed?.lastUser || parsed?.user || accounts[0]?.user);
  const selected = accounts.find((account) => account.user === requestedUser) || accounts[0];
  const lastUser = selected?.user || requestedUser;

  return {
    version: 5,
    lastUser,
    autoLogin: Boolean(parsed?.autoLogin && selected?.rememberPassword),
    accounts: accounts.slice(0, MAX_ACCOUNTS)
  };
}

function validateUser(user) {
  if (!user || Buffer.byteLength(user, 'utf8') > MAX_ACCOUNT_BYTES) {
    throw new Error('账号不能为空，且 UTF-8 长度不能超过 64 字节。');
  }
}

function validatePassword(password) {
  if (!password || Buffer.byteLength(password, 'utf8') > MAX_PASSWORD_BYTES) {
    throw new Error('密码不能为空。请重新输入密码。');
  }
}

export function resolveCredentialRequest(storeInput, input = {}) {
  const store = normalizeAccountStore(storeInput);
  const username = normalizeUser(input.username || store.lastUser);
  validateUser(username);

  const selected = store.accounts.find((account) => account.user === username);
  const password = String(input.password || '');
  const remember = input.remember !== false;
  if (password) {
    validatePassword(password);
  } else if (!(remember && selected?.rememberPassword)) {
    validatePassword(password);
  }
  const autoLogin = Boolean(input.autoLogin && remember);

  return { username, password, remember, autoLogin };
}

export function persistAuthenticatedCredential(options = {}) {
  const emptyLogin = () => ({
    user: '',
    hasPassword: false,
    rememberPassword: false,
    autoLogin: false,
    lastLoginTime: 0,
    accounts: []
  });
  const persist = typeof options.persist === 'function' ? options.persist : () => {
    throw new Error('账号元数据保存函数不可用。');
  };
  const fallback = typeof options.fallback === 'function' ? options.fallback : emptyLogin;
  try {
    return { login: persist(), warning: '', persisted: true };
  } catch (error) {
    try {
      options.onError?.(error);
    } catch {
      // Observability must never turn a successful authentication into a failure.
    }
    let login;
    try {
      login = fallback();
    } catch {
      login = emptyLogin();
    }
    return {
      login,
      warning: '账号已登录，但账号设置保存失败；本次仍可开始游戏，密码不会被记住。',
      persisted: false
    };
  }
}

export function maskRememberedCredentialForUser(loginInput, userInput, lastLoginTimeInput = 0) {
  const user = normalizeUser(userInput);
  validateUser(user);
  const login = loginInput && typeof loginInput === 'object' ? loginInput : {};
  const selectedLastLoginTime = normalizeTimestamp(lastLoginTimeInput) ||
    normalizeTimestamp(login.lastLoginTime);
  const accounts = [];
  const seen = new Set();
  let selectedFound = false;

  for (const entry of Array.isArray(login.accounts) ? login.accounts : []) {
    const accountUser = normalizeUser(entry?.user);
    if (!accountUser || seen.has(accountUser)) continue;
    seen.add(accountUser);
    const selected = accountUser === user;
    selectedFound ||= selected;
    accounts.push({
      user: accountUser,
      hasPassword: selected ? false : Boolean(entry?.hasPassword ?? entry?.rememberPassword),
      rememberPassword: selected ? false : Boolean(entry?.rememberPassword ?? entry?.hasPassword),
      lastLoginTime: selected
        ? selectedLastLoginTime || normalizeTimestamp(entry?.lastLoginTime)
        : normalizeTimestamp(entry?.lastLoginTime)
    });
    if (accounts.length === MAX_ACCOUNTS) break;
  }

  if (!selectedFound) {
    accounts.unshift({
      user,
      hasPassword: false,
      rememberPassword: false,
      lastLoginTime: selectedLastLoginTime
    });
    accounts.splice(MAX_ACCOUNTS);
  }

  return {
    user,
    hasPassword: false,
    rememberPassword: false,
    autoLogin: false,
    lastLoginTime: selectedLastLoginTime,
    accounts,
    ...(login.server !== undefined ? { server: String(login.server) } : {})
  };
}

function combineWarnings(...values) {
  return values.map((value) => String(value || '').trim()).filter(Boolean).join(' ');
}

export async function reconcileAuthenticatedCredentialPersistence(options = {}) {
  const username = normalizeUser(options.username || options.credentialState?.username);
  validateUser(username);
  const workerWarning = String(options.workerWarning || '').trim();
  const persistence = persistAuthenticatedCredential(options);
  if (persistence.persisted) {
    return {
      login: persistence.login,
      warning: combineWarnings(workerWarning, persistence.warning),
      remembered: hasRememberedCredentialForUser(persistence.login, username),
      metadataPersisted: true,
      vaultCleanup: 'not-required'
    };
  }

  let vaultCleanup = 'succeeded';
  try {
    if (typeof options.forget !== 'function') {
      throw new Error('密码保险库清理命令不可用。');
    }
    await options.forget(username);
  } catch (error) {
    vaultCleanup = 'failed';
    try {
      options.onCleanupError?.(error);
    } catch {
      // Observability must not alter the authenticated session result.
    }
  }

  const login = maskRememberedCredentialForUser(
    persistence.login,
    username,
    options.credentialState?.lastLoginTime
  );
  const cleanupWarning = vaultCleanup === 'succeeded'
    ? '账号设置保存失败；密码保险库中的本次密码已清除，本次不会记住密码。'
    : '账号设置保存失败，且密码保险库清理失败；Windows DPAPI 加密副本可能仍保留，本次会话已禁用自动复用，请关闭登录器后检查账号存储。';

  return {
    login,
    warning: combineWarnings(workerWarning, cleanupWarning),
    remembered: false,
    metadataPersisted: false,
    vaultCleanup
  };
}

export function commitAuthenticatedCredentialMetadata(storeInput, stateInput, options = {}) {
  const store = normalizeAccountStore(storeInput);
  const username = normalizeUser(stateInput?.username || stateInput?.user);
  validateUser(username);

  const rememberPassword = Boolean(stateInput?.rememberPassword);
  const now = typeof options.now === 'function' ? options.now : Date.now;
  const lastLoginTime = normalizeTimestamp(stateInput?.lastLoginTime) || normalizeTimestamp(now()) || Date.now();
  const account = { user: username, rememberPassword, lastLoginTime };
  const selectUser = options.selectUser !== false;

  return {
    version: 5,
    lastUser: selectUser ? username : store.lastUser || username,
    autoLogin: selectUser
      ? Boolean(stateInput?.autoLogin && rememberPassword)
      : Boolean(store.autoLogin),
    accounts: [account, ...store.accounts.filter((entry) => entry.user !== username)].slice(0, MAX_ACCOUNTS)
  };
}

export function removeAccountCredentialMetadata(storeInput, userInput) {
  const store = normalizeAccountStore(storeInput);
  const username = normalizeUser(userInput);
  validateUser(username);
  const accounts = store.accounts.filter((entry) => entry.user !== username);
  const removedSelected = store.lastUser === username;
  return {
    version: 5,
    lastUser: removedSelected ? accounts[0]?.user || '' : store.lastUser,
    autoLogin: removedSelected ? false : Boolean(store.autoLogin),
    accounts
  };
}

export function toPublicLoginState(storeInput) {
  const store = normalizeAccountStore(storeInput);
  const selected = store.accounts.find((account) => account.user === store.lastUser) || store.accounts[0];
  return {
    user: selected?.user || store.lastUser || '',
    hasPassword: Boolean(selected?.rememberPassword),
    rememberPassword: Boolean(selected?.rememberPassword),
    autoLogin: Boolean(store.autoLogin && selected?.rememberPassword),
    lastLoginTime: selected?.lastLoginTime || 0,
    accounts: store.accounts.map((account) => ({
      user: account.user,
      hasPassword: Boolean(account.rememberPassword),
      rememberPassword: Boolean(account.rememberPassword),
      lastLoginTime: account.lastLoginTime || 0
    }))
  };
}
