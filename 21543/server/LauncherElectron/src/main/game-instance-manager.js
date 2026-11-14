export const MAX_BOUND_ACCOUNTS = 10;

const ACTIVE_PHASES = new Set(['starting', 'running']);

function normalizeText(value, label) {
  const normalized = String(value || '').trim();
  if (!normalized) {
    throw new GameInstanceError('INVALID_INSTANCE', `${label}不能为空。`);
  }
  return normalized;
}

function normalizePid(value) {
  const pid = Number(value);
  if (!Number.isSafeInteger(pid) || pid <= 0) {
    throw new GameInstanceError('INVALID_PROCESS_ID', '游戏进程 PID 无效。');
  }
  return pid;
}

function cloneRecord(record) {
  return record ? {
    key: record.key,
    user: record.user,
    zone: record.zone,
    zoneLabel: record.zoneLabel,
    main: record.main,
    phase: record.phase,
    pid: record.pid,
    message: record.message,
    startedAt: record.startedAt,
    updatedAt: record.updatedAt,
    restored: record.restored,
    exitCode: record.exitCode
  } : null;
}

export class GameInstanceError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'GameInstanceError';
    this.code = code;
  }
}

export function makeGameInstanceKey(userInput, zoneInput) {
  const user = normalizeText(userInput, '账号');
  const zone = normalizeText(zoneInput, '区服');
  return JSON.stringify([user.toLowerCase(), zone.toLowerCase()]);
}

export class GameInstanceManager {
  #records = new Map();
  #startingKey = '';
  #now;

  constructor({ now = Date.now } = {}) {
    this.#now = typeof now === 'function' ? now : Date.now;
  }

  get startingKey() {
    return this.#startingKey;
  }

  get runningCount() {
    let count = 0;
    for (const record of this.#records.values()) {
      if (record.phase === 'running') count += 1;
    }
    return count;
  }

  get(key) {
    return cloneRecord(this.#records.get(String(key || '')));
  }

  reserveStart(input = {}) {
    const user = normalizeText(input.user, '账号');
    const zone = normalizeText(input.zone, '区服');
    const key = makeGameInstanceKey(user, zone);
    const existing = this.#records.get(key);
    if (existing && ACTIVE_PHASES.has(existing.phase)) {
      throw new GameInstanceError(
        'ACCOUNT_ZONE_ALREADY_IN_USE',
        `账号 ${user} 已在${existing.zoneLabel || zone}启动，请勿重复开启。`
      );
    }
    if (this.#startingKey && this.#startingKey !== key) {
      throw new GameInstanceError(
        'INSTANCE_START_IN_PROGRESS',
        '有其他游戏实例正在启动中，请等待其启动完成后再试。'
      );
    }

    const timestamp = Math.max(0, Number(this.#now()) || Date.now());
    const record = {
      key,
      user,
      zone,
      zoneLabel: String(input.zoneLabel || zone).trim().slice(0, 80) || zone,
      main: input.main === true,
      phase: 'starting',
      pid: 0,
      message: String(input.message || '正在启动').slice(0, 500),
      startedAt: timestamp,
      updatedAt: timestamp,
      restored: false,
      exitCode: null
    };
    this.#records.set(key, record);
    this.#startingKey = key;
    return cloneRecord(record);
  }

  markRunning(keyInput, pidInput) {
    const key = String(keyInput || '');
    const record = this.#records.get(key);
    if (!record || record.phase !== 'starting') {
      throw new GameInstanceError('INVALID_INSTANCE_STATE', '游戏实例不处于启动中状态。');
    }
    record.phase = 'running';
    record.pid = normalizePid(pidInput);
    record.message = '游戏运行中';
    record.updatedAt = Math.max(0, Number(this.#now()) || Date.now());
    record.exitCode = null;
    if (this.#startingKey === key) this.#startingKey = '';
    return cloneRecord(record);
  }

  markFailed(keyInput, message = '启动失败') {
    const key = String(keyInput || '');
    const record = this.#records.get(key);
    if (!record) return null;
    record.phase = 'failed';
    record.pid = 0;
    record.message = String(message || '启动失败').slice(0, 500);
    record.updatedAt = Math.max(0, Number(this.#now()) || Date.now());
    if (this.#startingKey === key) this.#startingKey = '';
    return cloneRecord(record);
  }

  markExited(keyInput, exitCode = 0) {
    const key = String(keyInput || '');
    const record = this.#records.get(key);
    if (!record) return null;
    record.phase = 'exited';
    record.message = '游戏进程已退出';
    record.updatedAt = Math.max(0, Number(this.#now()) || Date.now());
    record.exitCode = Number.isInteger(Number(exitCode)) ? Number(exitCode) : 0;
    if (this.#startingKey === key) this.#startingKey = '';
    return cloneRecord(record);
  }

  restoreRunning(input = {}) {
    const user = normalizeText(input.user, '账号');
    const zone = normalizeText(input.zone, '区服');
    const key = makeGameInstanceKey(user, zone);
    const timestamp = Math.max(0, Number(this.#now()) || Date.now());
    const record = {
      key,
      user,
      zone,
      zoneLabel: String(input.zoneLabel || zone).trim().slice(0, 80) || zone,
      main: input.main === true,
      phase: 'running',
      pid: normalizePid(input.pid),
      message: '游戏运行中',
      startedAt: Math.max(0, Number(input.startedAt) || timestamp),
      updatedAt: timestamp,
      restored: true,
      exitCode: null
    };
    this.#records.set(key, record);
    return cloneRecord(record);
  }

  remove(keyInput) {
    const key = String(keyInput || '');
    if (this.#startingKey === key) this.#startingKey = '';
    return this.#records.delete(key);
  }

  snapshot({ mainUser = '', accounts = [], servers = [] } = {}) {
    const normalizedMain = String(mainUser || '').trim();
    const accountMap = new Map();
    const addAccount = (entry, main = false) => {
      const user = String(entry?.user || '').trim();
      if (!user || accountMap.has(user.toLowerCase())) return;
      accountMap.set(user.toLowerCase(), {
        user,
        hasPassword: Boolean(entry?.hasPassword ?? entry?.rememberPassword),
        main
      });
    };

    if (normalizedMain) {
      const mainEntry = Array.isArray(accounts)
        ? accounts.find((entry) => String(entry?.user || '').trim() === normalizedMain)
        : null;
      addAccount(mainEntry || { user: normalizedMain }, true);
    }
    let boundCount = 0;
    for (const entry of Array.isArray(accounts) ? accounts : []) {
      const user = String(entry?.user || '').trim();
      if (!user || user === normalizedMain || boundCount >= MAX_BOUND_ACCOUNTS) continue;
      const before = accountMap.size;
      addAccount(entry, false);
      if (accountMap.size > before) boundCount += 1;
    }

    const publicRecords = [...this.#records.values()]
      .filter((record) => ACTIVE_PHASES.has(record.phase))
      .map(cloneRecord)
      .sort((left, right) => left.startedAt - right.startedAt);
    for (const account of accountMap.values()) {
      const owned = publicRecords.filter((record) => record.user === account.user);
      account.runningZones = owned
        .filter((record) => record.phase === 'running')
        .map((record) => record.zoneLabel);
      account.instances = owned;
    }

    const publicServers = (Array.isArray(servers) ? servers : [])
      .map((server) => ({
        id: String(server?.id ?? server?.value ?? '').trim(),
        label: String(server?.label || server?.name || '').trim().slice(0, 80),
        available: server?.available !== false
      }))
      .filter((server) => server.id && server.available);
    if (!publicServers.length) {
      publicServers.push({ id: '64', label: '电信区', available: true });
    }

    return {
      starting: Boolean(this.#startingKey),
      startingKey: this.#startingKey,
      runningCount: publicRecords.filter((record) => record.phase === 'running').length,
      instances: publicRecords,
      accounts: [...accountMap.values()],
      servers: publicServers,
      maxBoundAccounts: MAX_BOUND_ACCOUNTS
    };
  }
}
