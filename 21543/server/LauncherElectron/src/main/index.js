import { app, BrowserWindow, dialog, ipcMain, Menu, shell, Tray, utilityProcess } from 'electron';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import {
  isValidGameRoot,
  commitAuthenticatedCredentials,
  loadRememberedCredentials,
  loadNativeState,
  normalizeGameRoot,
  prepareGameApp,
  removeRememberedAccountMetadata,
  resolveCredentialsForAuthentication,
  saveLauncherSettings
} from './native-core.js';
import {
  hasRememberedCredentialForUser,
  maskRememberedCredentialForUser,
  reconcileAuthenticatedCredentialPersistence
} from './credential-policy.js';
import { watchProcessByPid } from './game-process-monitor.js';
import {
  GameInstanceError,
  GameInstanceManager,
  MAX_BOUND_ACCOUNTS,
  makeGameInstanceKey
} from './game-instance-manager.js';
import { inspectIpcSender } from './ipc-trust.js';
import { logStartupEvent, serializeStartupError } from './startup-log.js';
import {
  getCachedLauncherContentRequest,
  invalidateLauncherHomeContent,
  loadLauncherContent,
  resolveAuthorizedLauncherContentScope
} from './launcher-content-service.js';
import {
  ExclusiveAttemptGuard,
  shouldExpireAuthenticatedSessionAfterReauth
} from './session-state-machine.js';
import { createGameLoadingVisibilityController } from './game-loading-visibility.js';
import { createTrayController } from './tray-controller.js';
import { createGmItemQueryService } from './gm-item-query-service.js';
import { createQqAvatarUrl } from '../shared/account-avatar.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const appRoot = resolve(__dirname, '..', '..');
const rendererEntry = join(appRoot, 'src', 'renderer', 'index.html');
const rendererEntryUrl = pathToFileURL(rendererEntry).href;
const updateRendererEntry = join(appRoot, 'src', 'renderer', 'update.html');
const updateRendererEntryUrl = pathToFileURL(updateRendererEntry).href;
const preloadEntry = join(appRoot, 'src', 'preload', 'preload.cjs');
const updatePreloadEntry = join(appRoot, 'src', 'preload', 'update-preload.cjs');
const nativeWorkerEntry = join(__dirname, 'native-worker.js');
const updatePublicKeyPath = join(appRoot, 'resources', 'update-public-key.pem');

// Keep the existing storage identity so an
// in-place update does not lose the selected game directory or saved accounts.
// Preserve Electron's explicit override so packaged release checks stay isolated.
if (!app.commandLine.hasSwitch('user-data-dir')) {
  app.setPath('userData', join(app.getPath('appData'), 'QQSpeedLauncher'));
}

const BASE_WINDOW_WIDTH = 1280;
const BASE_WINDOW_HEIGHT = 720;
const MIN_WINDOW_WIDTH = 1024;
const MIN_WINDOW_HEIGHT = 640;
const NATIVE_PRELOAD_TIMEOUT_MS = 90_000;
const UPDATE_WINDOW_SHOW_DELAY_MS = 300;
const UPDATE_PROGRESS_CHANNEL = 'launcher:update-progress';
const TERMINAL_LAUNCH_PHASES = new Set(['idle', 'preloaded', 'authenticated', 'ready', 'failed', 'cancelled', 'game-exited']);
const ALL_LAUNCH_PHASES = new Set([
  'idle',
  'preparing',
  'preloading',
  'preloaded',
  'authenticating',
  'authenticated',
  'starting',
  'ready',
  'failed',
  'cancelled',
  'game-exited'
]);
const WORKER_LAUNCH_PHASES = new Set([
  'idle',
  'preloading',
  'preloaded',
  'authenticating',
  'authenticated',
  'starting',
  'ready',
  'failed',
  'cancelled'
]);
const FAILED_SESSION_PHASES = new Set(['failed', 'cancelled']);
const INSTANCE_SERVERS = Object.freeze([
  Object.freeze({ id: '64', label: '电信区', available: true })
]);
const mainStartupStartedAt = Date.now();

logStartupEvent('electron-main', 'MAIN_MODULE_EVALUATED', {
  platform: process.platform,
  arch: process.arch,
  version: process.versions.electron
});

let mainWindow = null;
let updateWindow = null;
let updateWindowLoaded = false;
let updateWindowReady = false;
let updateWindowMayShow = false;
let updateWindowShowTimer = null;
let latestUpdateProgress = {
  phase: 'checking',
  version: '',
  downloadedBytes: 0,
  totalBytes: 0,
  percent: 0,
  bytesPerSecond: 0
};
let mainWindowShouldBeVisible = true;
let updateGatePassed = false;
let launchStarting = false;
let activeLaunch = null;
let gameProcessMonitor = null;
const auxiliaryLaunches = new Map();
const auxiliaryGameProcessMonitors = new Map();
const gameInstanceManager = new GameInstanceManager();
let auxiliaryLaunchSequence = 0;
let accountBindingInProgress = false;
let gameExitIdleTimer = null;
let closeAfterSessionCancellation = false;
let trayController = null;
let applicationQuitRequested = false;
let startupNativePreloadStarted = false;
let launcherAuthenticatedUser = '';
let launcherSessionCredential = { user: '', remembered: false };
const unavailableRememberedUsers = new Set();
const launcherContentPromises = new Map();
let suppressTerminalSessionExpiry = false;
let gmItemQueryService = null;
let launchStatus = {
  phase: 'idle',
  progress: 0,
  gameProcessId: 0,
  message: '尚未启动游戏。'
};
const gameLoadingVisibility = createGameLoadingVisibilityController({
  onHide(window) {
    mainWindowShouldBeVisible = false;
    if (!trayController?.hideForGameLoading(window)) {
      window.hide();
    }
  },
  onRestore: restoreMainWindow,
  isQuitting: () => applicationQuitRequested
});

function getSettingsDir() {
  return app.getPath('userData');
}

function getGmItemDatabasePath() {
  return app.isPackaged
    ? join(process.resourcesPath, 'gm-items.sqlite3')
    : join(appRoot, 'resources', 'gm-items.sqlite3');
}

function getGmItemQueryService() {
  if (!gmItemQueryService) {
    gmItemQueryService = createGmItemQueryService({ databasePath: getGmItemDatabasePath() });
  }
  return gmItemQueryService;
}

function closeGmItemQueryService() {
  gmItemQueryService?.close();
  gmItemQueryService = null;
}

function assertGmItemAccess() {
  if (!launcherAuthenticatedUser) {
    throw new Error('请先登录后使用 GM 道具查询。');
  }
}

function getInstallStateDir() {
  const localAppData = String(process.env.LOCALAPPDATA || '').trim();
  return localAppData
    ? join(resolve(localAppData), 'QQSpeedLauncher', 'install-secrets')
    : join(getSettingsDir(), 'install-secrets');
}

function assertApplicationNotQuitting() {
  if (applicationQuitRequested) {
    throw new Error('登录器正在退出，不能创建新的游戏会话。');
  }
}

function applyCredentialAvailabilityOverrides(state) {
  if (!state?.login || unavailableRememberedUsers.size === 0) {
    return state;
  }
  const login = state.login;
  const selectedUnavailable = unavailableRememberedUsers.has(String(login.user || '').trim());
  return {
    ...state,
    login: {
      ...login,
      ...(selectedUnavailable ? {
        hasPassword: false,
        rememberPassword: false,
        autoLogin: false
      } : {}),
      accounts: (Array.isArray(login.accounts) ? login.accounts : []).map((account) => {
        if (!unavailableRememberedUsers.has(String(account?.user || '').trim())) {
          return account;
        }
        return {
          user: String(account.user || '').trim(),
          hasPassword: false,
          rememberPassword: false,
          lastLoginTime: Math.max(0, Number(account.lastLoginTime) || 0)
        };
      })
    }
  };
}

function loadEffectiveNativeState() {
  return applyCredentialAvailabilityOverrides(loadNativeState(appRoot, getSettingsDir()));
}

function getPublicInstanceSnapshot() {
  let accounts = [];
  try {
    accounts = loadEffectiveNativeState().login?.accounts || [];
  } catch (error) {
    logStartupEvent('electron-main', 'INSTANCE_ACCOUNT_STATE_READ_FAILED', serializeStartupError(error));
  }
  return gameInstanceManager.snapshot({
    mainUser: launcherAuthenticatedUser,
    accounts,
    servers: INSTANCE_SERVERS
  });
}

function emitInstanceSnapshot() {
  const snapshot = getPublicInstanceSnapshot();
  if (mainWindow && !mainWindow.isDestroyed() && !mainWindow.webContents.isDestroyed()) {
    mainWindow.webContents.send('launcher:instances', snapshot);
  }
  return snapshot;
}

function hasAnyActiveGame() {
  return gameInstanceManager.runningCount > 0 || hasActiveGame();
}

function applyEffectiveReadinessState(readiness) {
  return readiness?.state
    ? { ...readiness, state: applyCredentialAvailabilityOverrides(readiness.state) }
    : readiness;
}

function getLauncherContent(scope = 'bootstrap', refresh = false) {
  const normalizedScope = resolveAuthorizedLauncherContentScope(
    scope,
    Boolean(launcherAuthenticatedUser)
  );
  return getCachedLauncherContentRequest(
    launcherContentPromises,
    normalizedScope,
    refresh,
    () => loadLauncherContent(getSettingsDir(), { scope: normalizedScope }).catch(() => null)
  );
}

function normalizeOfficialExternalUrl(value) {
  try {
    const url = new URL(String(value || ''));
    const host = url.hostname.toLowerCase();
    const allowedHost = host === 'qq.com' || host.endsWith('.qq.com');
    return url.protocol === 'https:' && allowedHost ? url.href : '';
  } catch {
    return '';
  }
}

function clearGameExitIdleTimer() {
  if (gameExitIdleTimer) {
    clearTimeout(gameExitIdleTimer);
    gameExitIdleTimer = null;
  }
}

function restoreMainWindow(reason = 'internal') {
  const restoreReason = String(reason || 'internal').slice(0, 80);
  if (applicationQuitRequested) {
    logStartupEvent('electron-main', 'WINDOW_RESTORE_SKIPPED_QUIT', {
      reason: restoreReason
    });
    return false;
  }
  if (!app.isReady()) {
    logStartupEvent('electron-main', 'WINDOW_RESTORE_DEFERRED_APP_NOT_READY', {
      reason: restoreReason
    });
    return false;
  }
  if (!updateGatePassed) {
    const updateWindowCreated = !updateWindow || updateWindow.isDestroyed();
    if (updateWindowCreated) {
      createUpdateWindow();
    }
    updateWindowMayShow = true;
    showUpdateWindowWhenReady();
    logStartupEvent('electron-main', 'UPDATE_WINDOW_RESTORE_REQUESTED', {
      reason: restoreReason,
      status: updateWindowCreated ? 'created' : 'existing'
    });
    return true;
  }
  mainWindowShouldBeVisible = true;
  if (!mainWindow || mainWindow.isDestroyed()) {
    logStartupEvent('electron-main', 'MAIN_WINDOW_RECREATE_REQUESTED', {
      reason: restoreReason
    });
    startLauncherAfterUpdateGate();
    return Boolean(mainWindow && !mainWindow.isDestroyed());
  }
  if (trayController?.restore()) {
    logStartupEvent('electron-main', 'MAIN_WINDOW_RESTORED', {
      reason: restoreReason,
      stage: 'tray'
    });
    return true;
  }
  if (mainWindow.isMinimized()) {
    mainWindow.restore();
  }
  mainWindow.show();
  mainWindow.focus();
  logStartupEvent('electron-main', 'MAIN_WINDOW_RESTORED', {
    reason: restoreReason,
    stage: 'window'
  });
  return true;
}

function requestRendererCloseDecision(window = mainWindow) {
  if (
    window !== mainWindow
    || !mainWindow
    || mainWindow.isDestroyed()
    || mainWindow.webContents.isDestroyed()
  ) {
    return false;
  }
  mainWindow.webContents.send('window:close-requested');
  return true;
}

function resetClosePreferencePrompt() {
  restoreMainWindow();
  if (!mainWindow || mainWindow.isDestroyed() || mainWindow.webContents.isDestroyed()) {
    return false;
  }
  mainWindow.webContents.send('window:close-preference-reset');
  return true;
}

function hideForGameLoading(window = mainWindow) {
  if (!window || window.isDestroyed()) {
    return null;
  }
  return gameLoadingVisibility.begin(window);
}

function activateGameLoadingAttempt(attempt) {
  return gameLoadingVisibility.activate(attempt);
}

function finishGameLoadingAttempt(attempt) {
  return gameLoadingVisibility.finish(attempt);
}

function restoreAfterGameLoadingFailure(attempt) {
  return finishGameLoadingAttempt(attempt);
}

function hasActiveGame() {
  return Boolean(gameProcessMonitor && !gameProcessMonitor.stopped);
}

function stopGameProcessMonitor() {
  gameProcessMonitor?.stop();
  gameProcessMonitor = null;
}

function handleGameProcessExit(monitor, pid, instanceKey = '', loadingAttempt = null) {
  if (gameProcessMonitor !== monitor) {
    return;
  }
  stopGameProcessMonitor();
  if (instanceKey) {
    gameInstanceManager.markExited(instanceKey, 0);
    emitInstanceSnapshot();
  }
  setLaunchStatus({
    phase: 'game-exited',
    progress: 0,
    gameProcessId: pid,
    message: '游戏进程已退出，可以重新启动。'
  });
  expireUnrememberedLauncherSession();

  finishGameLoadingAttempt(loadingAttempt);

  clearGameExitIdleTimer();
  gameExitIdleTimer = setTimeout(() => {
    gameExitIdleTimer = null;
    if (!hasActiveGame() && !isLaunchBusy() && launchStatus.phase === 'game-exited') {
      setLaunchStatus({
        phase: 'idle',
        progress: 0,
        gameProcessId: 0,
        message: '尚未启动游戏。'
      });
    }
  }, 1500);
  gameExitIdleTimer.unref?.();
}

function beginGameProcessMonitor(pid, instanceKey = '', loadingAttempt = null) {
  clearGameExitIdleTimer();
  stopGameProcessMonitor();
  let monitor = null;
  monitor = watchProcessByPid(pid, (exitedPid) => handleGameProcessExit(monitor, exitedPid, instanceKey, loadingAttempt));
  gameProcessMonitor = monitor;
}

function beginAuxiliaryGameProcessMonitor(instanceKey, pid, loadingAttempt = null) {
  auxiliaryGameProcessMonitors.get(instanceKey)?.stop();
  let monitor = null;
  monitor = watchProcessByPid(pid, (exitedPid) => {
    if (auxiliaryGameProcessMonitors.get(instanceKey) !== monitor) return;
    monitor.stop();
    auxiliaryGameProcessMonitors.delete(instanceKey);
    gameInstanceManager.markExited(instanceKey, 0);
    logStartupEvent('electron-main', 'MULTI_INSTANCE_GAME_EXIT', {
      instanceKey,
      gameProcessId: exitedPid
    });
    emitInstanceSnapshot();
    finishGameLoadingAttempt(loadingAttempt);
  });
  auxiliaryGameProcessMonitors.set(instanceKey, monitor);
}

function isLaunchBusy() {
  return launchStarting || !TERMINAL_LAUNCH_PHASES.has(launchStatus.phase);
}

function getPublicLaunchStatus() {
  return {
    ...launchStatus,
    busy: isLaunchBusy()
  };
}

function getPublicSession() {
  const authenticated = Boolean(launcherAuthenticatedUser);
  return {
    authenticated,
    user: launcherAuthenticatedUser,
    avatarUrl: authenticated ? createQqAvatarUrl(launcherAuthenticatedUser) : ''
  };
}

function emitLauncherUserLogin() {
  if (mainWindow && !mainWindow.isDestroyed() && !mainWindow.webContents.isDestroyed()) {
    mainWindow.webContents.send('launcher:user-login', getPublicSession());
  }
  emitInstanceSnapshot();
}

function emitLauncherUserLogout() {
  if (mainWindow && !mainWindow.isDestroyed() && !mainWindow.webContents.isDestroyed()) {
    mainWindow.webContents.send('launcher:user-logout', getPublicSession());
  }
  emitInstanceSnapshot();
}

function clearLauncherAuthenticatedSession({ emit = true } = {}) {
  const hadAuthenticatedUser = Boolean(launcherAuthenticatedUser);
  launcherAuthenticatedUser = '';
  launcherSessionCredential = { user: '', remembered: false };
  invalidateLauncherHomeContent(launcherContentPromises);
  if (hadAuthenticatedUser && emit) {
    emitLauncherUserLogout();
  }
  return hadAuthenticatedUser;
}

function expireUnrememberedLauncherSession() {
  const authenticatedUser = launcherAuthenticatedUser;
  if (!authenticatedUser) {
    return false;
  }

  let remembered = launcherSessionCredential.user === authenticatedUser
    ? launcherSessionCredential.remembered
    : false;
  if (launcherSessionCredential.user !== authenticatedUser) {
    try {
      const state = loadEffectiveNativeState();
      remembered = hasRememberedCredentialForUser(state.login, authenticatedUser);
    } catch (error) {
      logStartupEvent(
        'electron-main',
        'SESSION_RETENTION_STATE_READ_FAILED',
        serializeStartupError(error)
      );
    }
  }
  if (remembered) {
    return false;
  }

  return clearLauncherAuthenticatedSession();
}

function emitLaunchStatus(status = getPublicLaunchStatus()) {
  if (mainWindow && !mainWindow.isDestroyed() && !mainWindow.webContents.isDestroyed()) {
    mainWindow.webContents.send('launcher:status', status);
  }
}

function setLaunchStatus(nextStatus) {
  const phase = ALL_LAUNCH_PHASES.has(nextStatus?.phase) ? nextStatus.phase : 'failed';
  const progress = Math.max(0, Math.min(100, Number(nextStatus?.progress) || 0));
  const gameProcessId = Math.max(0, Number(nextStatus?.gameProcessId) || 0);
  const message = String(nextStatus?.message || '').slice(0, 500);
  const code = Number(nextStatus?.code);

  launchStatus = {
    phase,
    progress,
    gameProcessId,
    message,
    ...(Number.isInteger(code) ? { code } : {})
  };
  const failedSession = FAILED_SESSION_PHASES.has(launchStatus.phase);
  emitLaunchStatus();
  if (failedSession && !suppressTerminalSessionExpiry) {
    expireUnrememberedLauncherSession();
  }
}

function logRejectedIpc(channel, inspection) {
  logStartupEvent('electron-main', 'IPC_REJECTED', {
    channel,
    ...inspection
  });
}

function assertTrustedSender(event, channel) {
  const inspection = inspectIpcSender(event, mainWindow, rendererEntry);
  if (!inspection.trusted) {
    logRejectedIpc(channel, inspection);
    throw new Error('拒绝未授权页面 IPC。');
  }
}

function assertWindowControlSender(event, channel) {
  const inspection = inspectIpcSender(event, mainWindow, rendererEntry);
  if (!inspection.windowControl) {
    logRejectedIpc(channel, inspection);
    throw new Error('拒绝未授权窗口控制 IPC。');
  }
}

function normalizeWorkerStatus(value) {
  if (!value || typeof value !== 'object' || !WORKER_LAUNCH_PHASES.has(value.phase)) {
    return null;
  }
  return {
    phase: value.phase,
    progress: Math.max(0, Math.min(100, Number(value.progress) || 0)),
    gameProcessId: Math.max(0, Number(value.gameProcessId) || 0),
    message: String(value.message || '').slice(0, 500)
  };
}

function getPublicRecordStatus(record) {
  const status = record?.status || launchStatus;
  return {
    phase: String(status?.phase || 'idle'),
    progress: Math.max(0, Math.min(100, Number(status?.progress) || 0)),
    gameProcessId: Math.max(0, Number(status?.gameProcessId) || 0),
    message: String(status?.message || '').slice(0, 500),
    busy: !TERMINAL_LAUNCH_PHASES.has(String(status?.phase || 'idle'))
  };
}

function isTrackedWorkerRecord(record) {
  if (!record) return false;
  if (record.kind === 'primary') return activeLaunch === record;
  return auxiliaryLaunches.get(record.id) === record;
}

function setRecordStatus(record, status) {
  record.status = {
    phase: String(status?.phase || 'failed'),
    progress: Math.max(0, Math.min(100, Number(status?.progress) || 0)),
    gameProcessId: Math.max(0, Number(status?.gameProcessId) || 0),
    message: String(status?.message || '').slice(0, 500),
    ...(Number.isInteger(Number(status?.code)) ? { code: Number(status.code) } : {})
  };
  if (FAILED_SESSION_PHASES.has(record.status.phase)) {
    restoreAfterGameLoadingFailure(record.gameLoadingAttempt);
  }
  if (record.kind === 'primary') {
    setLaunchStatus(record.status);
  } else if (record.instanceKey && FAILED_SESSION_PHASES.has(record.status.phase)) {
    gameInstanceManager.markFailed(record.instanceKey, record.status.message);
    emitInstanceSnapshot();
  }
  return getPublicRecordStatus(record);
}

function rejectRecordWaiters(record, error) {
  for (const waiter of record.waiters.splice(0)) {
    clearTimeout(waiter.timer);
    waiter.reject(error);
  }
}

function rejectCommandWaiters(record, error) {
  for (const waiter of record.commandWaiters.splice(0)) {
    clearTimeout(waiter.timer);
    waiter.reject(error);
  }
}

function waitForCommandResult(record, command, timeoutMs = 5000) {
  return new Promise((resolveCommand, rejectCommand) => {
    const waiter = {
      command,
      resolve: resolveCommand,
      reject: rejectCommand,
      timer: setTimeout(() => {
        const index = record.commandWaiters.indexOf(waiter);
        if (index >= 0) {
          record.commandWaiters.splice(index, 1);
        }
        rejectCommand(new Error(`等待原生命令确认超时：${command}。`));
      }, timeoutMs)
    };
    record.commandWaiters.push(waiter);
  });
}

function normalizeWorkerCommandCode(value) {
  if (typeof value === 'string') {
    return value.trim().slice(0, 64).toUpperCase();
  }
  const numeric = Number(value);
  return Number.isInteger(numeric) ? numeric : '';
}

async function requestWorkerCredentialForget(record, username) {
  const acknowledged = waitForCommandResult(record, 'forget-credential', 5000);
  record.child.postMessage({
    type: 'forget-credential',
    payload: { username }
  });
  await acknowledged;
}

function reconcileWorkerCredentialState(record, state, workerWarning, username) {
  return reconcileAuthenticatedCredentialPersistence({
    username,
    credentialState: state,
    workerWarning,
    persist: () => commitAuthenticatedCredentials(
      appRoot,
      state,
      getSettingsDir(),
      record.preserveSelectedAccount ? { selectUser: false } : undefined
    ),
    fallback: () => loadEffectiveNativeState().login,
    forget: () => requestWorkerCredentialForget(record, username),
    onError: (error) => logStartupEvent(
      'electron-main',
      'CREDENTIAL_METADATA_COMMIT_FAILED_AFTER_AUTH',
      serializeStartupError(error)
    ),
    onCleanupError: (error) => logStartupEvent(
      'electron-main',
      'CREDENTIAL_VAULT_CLEANUP_FAILED_AFTER_METADATA_ERROR',
      serializeStartupError(error)
    )
  });
}

function createUnverifiedCredentialOutcome(username) {
  let login;
  try {
    login = maskRememberedCredentialForUser(loadEffectiveNativeState().login, username);
  } catch {
    login = {
      user: username,
      hasPassword: false,
      rememberPassword: false,
      autoLogin: false,
      lastLoginTime: 0,
      accounts: [{
        user: username,
        hasPassword: false,
        rememberPassword: false,
        lastLoginTime: 0
      }]
    };
  }
  return {
    login,
    warning: '账号已登录，但账号元数据与密码保险库状态未能完成对账；本次会话已禁用密码自动复用，且无法确认是否存在待清理的 Windows DPAPI 加密副本。',
    remembered: false,
    metadataPersisted: false,
    vaultCleanup: 'unverified'
  };
}

function settleRecordWaiters(record, status) {
  for (const waiter of [...record.waiters]) {
    if (waiter.phases.has(status.phase)) {
      record.waiters.splice(record.waiters.indexOf(waiter), 1);
      clearTimeout(waiter.timer);
      waiter.resolve(getPublicRecordStatus(record));
    } else if (FAILED_SESSION_PHASES.has(status.phase)) {
      record.waiters.splice(record.waiters.indexOf(waiter), 1);
      clearTimeout(waiter.timer);
      waiter.reject(new Error(status.message || '原生启动会话失败。'));
    }
  }
}

function waitForRecordPhase(record, phases, timeoutMs) {
  const accepted = new Set(phases);
  const current = getPublicRecordStatus(record);
  if (accepted.has(current.phase)) {
    return Promise.resolve(current);
  }
  if (FAILED_SESSION_PHASES.has(current.phase)) {
    return Promise.reject(new Error(current.message || '原生启动会话失败。'));
  }
  return new Promise((resolveWait, rejectWait) => {
    const waiter = {
      phases: accepted,
      resolve: resolveWait,
      reject: rejectWait,
      timer: setTimeout(() => {
        const index = record.waiters.indexOf(waiter);
        if (index >= 0) {
          record.waiters.splice(index, 1);
        }
        rejectWait(new Error('等待原生启动阶段超时。'));
      }, timeoutMs)
    };
    record.waiters.push(waiter);
  });
}

function handleWorkerMessage(record, message) {
  if (!isTrackedWorkerRecord(record) || !message || typeof message !== 'object') {
    return;
  }

  if (message.type === 'credential-state') {
    const state = message.state && typeof message.state === 'object' ? message.state : {};
    const expectedUser = String(record.pendingLoginUser || '');
    const receivedUser = String(state.username || state.user || '').trim();
    if (!expectedUser || receivedUser !== expectedUser) {
      logStartupEvent('electron-main', 'CREDENTIAL_STATE_REJECTED', {
        reason: 'worker account did not match the pending login'
      });
      return;
    }
    const workerWarning = String(message.warning || '').slice(0, 500);
    record.credentialOutcome = reconcileWorkerCredentialState(
      record,
      state,
      workerWarning,
      receivedUser
    ).catch((error) => {
      logStartupEvent(
        'electron-main',
        'CREDENTIAL_RECONCILIATION_FAILED_AFTER_AUTH',
        serializeStartupError(error)
      );
      return createUnverifiedCredentialOutcome(receivedUser);
    });
    return;
  }

  if (message.type === 'status') {
    const status = normalizeWorkerStatus(message.status);
    if (status) {
      setRecordStatus(record, status);
      settleRecordWaiters(record, status);
    }
    return;
  }

  if (message.type === 'command-result') {
    const command = String(message.command || '');
    const waiterIndex = record.commandWaiters.findIndex((waiter) => waiter.command === command);
    const waiter = waiterIndex >= 0 ? record.commandWaiters.splice(waiterIndex, 1)[0] : null;
    if (waiter) {
      clearTimeout(waiter.timer);
    }
    if (command === 'forget-credential') {
      if (message.ok === true) {
        waiter?.resolve({ ok: true });
      } else {
        const error = new Error(String(message.message || '密码保险库清理失败。').slice(0, 500));
        error.code = normalizeWorkerCommandCode(message.code);
        waiter?.reject(error);
      }
      return;
    }
    if (message.ok !== true) {
      const error = new Error(String(message.message || '原生会话命令失败。').slice(0, 500));
      error.code = normalizeWorkerCommandCode(message.code);
      waiter?.reject(error);
      if (command === 'login' && message.recoverable === true && message.fatal !== true) {
        rejectRecordWaiters(record, error);
        if (record.kind === 'primary') {
          const current = getPublicRecordStatus(record);
          setRecordStatus(record, {
            phase: 'preloaded',
            progress: Math.max(55, current.progress),
            gameProcessId: current.gameProcessId,
            message: error.message,
            code: Number(message.code) || 1
          });
          return;
        }
      }
      rejectCommandWaiters(record, error);
      rejectRecordWaiters(record, error);
      const current = getPublicRecordStatus(record);
      setRecordStatus(record, {
        phase: 'failed',
        progress: current.progress,
        gameProcessId: current.gameProcessId,
        message: error.message,
        code: Number(message.code) || -1
      });
      record.child.postMessage({ type: 'cancel' });
    } else {
      waiter?.resolve(getPublicRecordStatus(record));
    }
    return;
  }

  if (message.type !== 'result' || record.runFinished) {
    return;
  }

  record.runFinished = true;
  rejectCommandWaiters(record, new Error(String(message.message || '原生启动会话已结束。').slice(0, 500)));
  const gameProcessId = Math.max(0, Number(message.gameProcessId) || 0);
  const ok = message.ok === true && Number.isSafeInteger(gameProcessId) && gameProcessId > 0;
  const cancelled = message.phase === 'cancelled' || Number(message.code) === 4;
  const code = Number(message.code);
  const current = getPublicRecordStatus(record);
  const status = {
    phase: ok ? 'ready' : cancelled ? 'cancelled' : 'failed',
    progress: ok ? 100 : current.progress,
    gameProcessId,
    message: String(
      message.ok === true && !ok
        ? '启动组件未返回有效的游戏进程 PID。'
        : message.message || (ok ? '游戏已就绪。' : cancelled ? '启动会话已取消。' : '启动游戏失败。')
    ).slice(0, 500),
    code: ok ? 0 : Number.isInteger(code) ? code : -1
  };
  setRecordStatus(record, status);
  if (ok) {
    record.gameProcessId = gameProcessId;
    activateGameLoadingAttempt(record.gameLoadingAttempt);
    if (record.instanceKey) {
      gameInstanceManager.markRunning(record.instanceKey, gameProcessId);
      if (record.kind === 'primary') {
        beginGameProcessMonitor(gameProcessId, record.instanceKey, record.gameLoadingAttempt);
      } else {
        beginAuxiliaryGameProcessMonitor(record.instanceKey, gameProcessId, record.gameLoadingAttempt);
      }
      emitInstanceSnapshot();
    } else if (record.kind === 'primary') {
      beginGameProcessMonitor(gameProcessId, '', record.gameLoadingAttempt);
    }
    settleRecordWaiters(record, status);
  } else {
    if (record.instanceKey) {
      gameInstanceManager.markFailed(record.instanceKey, status.message);
      emitInstanceSnapshot();
    }
    rejectRecordWaiters(record, new Error(status.message));
  }
}

function attachUtilityProcess(record) {
  const { child } = record;
  child.on('message', (message) => handleWorkerMessage(record, message));
  child.on('error', (error) => {
    logStartupEvent('electron-main', 'NATIVE_WORKER_ERROR', serializeStartupError(error));
    if (isTrackedWorkerRecord(record) && !record.runFinished) {
      record.runFinished = true;
      const workerError = new Error('原生启动工作进程发生严重错误。');
      rejectCommandWaiters(record, workerError);
      rejectRecordWaiters(record, workerError);
      setRecordStatus(record, { phase: 'failed', progress: getPublicRecordStatus(record).progress, gameProcessId: 0, message: workerError.message, code: -1 });
    }
  });
  child.on('exit', (code) => {
    logStartupEvent('electron-main', 'NATIVE_WORKER_EXIT', {
      exitCode: Number(code) || 0
    });
    record.workerExited = true;
    if (activeLaunch === record) {
      activeLaunch = null;
    }
    if (record.kind !== 'primary') {
      auxiliaryLaunches.delete(record.id);
    }
    if (!record.runFinished) {
      record.runFinished = true;
      const error = new Error(`原生启动工作进程异常退出（代码 ${Number(code) || 0}）。`);
      rejectCommandWaiters(record, error);
      rejectRecordWaiters(record, error);
      setRecordStatus(record, { phase: 'failed', progress: getPublicRecordStatus(record).progress, gameProcessId: 0, message: error.message, code: Number(code) || -1 });
    }
    if (record.kind !== 'primary') emitInstanceSnapshot();
  });
}

async function createNativeSession() {
  assertApplicationNotQuitting();
  if (hasActiveGame()) {
    throw new Error(`游戏正在运行（PID ${gameProcessMonitor.pid}），请勿重复启动。`);
  }
  if (activeLaunch && !activeLaunch.runFinished) {
    return activeLaunch;
  }
  if (launchStarting) {
    throw new Error('GameApp 预加载正在初始化，请稍候。');
  }

  const readiness = prepareGameApp(appRoot, getSettingsDir());
  if (!readiness.ready) {
    throw new Error(readiness.message);
  }

  clearGameExitIdleTimer();
  launchStarting = true;
  setLaunchStatus({ phase: 'preparing', progress: 0, gameProcessId: 0, message: '正在准备安全登录会话…' });

  let record = null;
  logStartupEvent('electron-main', 'NATIVE_WORKER_PREPARE_BEGIN');
  try {
    const utilityEnv = { ...process.env, QQSPEED_ELECTRON_NATIVE: '1' };
    delete utilityEnv.QQSPEED_LOGIN_USER;
    delete utilityEnv.QQSPEED_LOGIN_PASSWORD;
    delete utilityEnv.QQSPEED_RUNTIME_CONTENT_KEY;
    delete utilityEnv.QQSPEED_RUNTIME_CONTENT_KEY_HEX;
    delete utilityEnv.QQSPEED_RUNTIME_CONTENT_KEY_FILE;
    const child = utilityProcess.fork(nativeWorkerEntry, [], {
      cwd: readiness.gameWorkDir,
      env: utilityEnv,
      serviceName: 'QQSpeed Launcher Native Core',
      stdio: 'ignore'
    });
    record = {
      id: 'primary',
      kind: 'primary',
      child,
      spawned: false,
      sessionRequested: false,
      runFinished: false,
      workerExited: false,
      gameProcessId: 0,
      waiters: [],
      commandWaiters: [],
      pendingLoginUser: '',
      credentialOutcome: null,
      preserveSelectedAccount: false,
      instanceKey: '',
      status: { ...launchStatus },
      loginAttemptGuard: new ExclusiveAttemptGuard()
    };
    activeLaunch = record;
    attachUtilityProcess(record);

    await new Promise((resolveSpawn, rejectSpawn) => {
      child.once('spawn', () => {
        record.spawned = true;
        logStartupEvent('electron-main', 'NATIVE_WORKER_SPAWNED');
        resolveSpawn();
      });
      child.once('exit', (code) => {
        if (!record.spawned) {
          rejectSpawn(new Error(`原生启动工作进程未能启动（代码 ${Number(code) || 0}）。`));
        }
      });
    });

    child.postMessage({
      type: 'start-session',
      payload: {
        gameRoot: readiness.gameRoot,
        settingsDir: getSettingsDir(),
        installStateDir: getInstallStateDir(),
        executablePath: process.execPath,
        packaged: app.isPackaged,
        developmentBundlePath: app.isPackaged
          ? ''
          : join(appRoot, 'resources', 'native', 'qqspeed-runtime.bundle'),
        server: 64
      }
    });
    record.sessionRequested = true;
    setRecordStatus(record, { phase: 'preloading', progress: 0, gameProcessId: 0, message: '安全登录会话正在初始化。' });
    return record;
  } catch (error) {
    logStartupEvent('electron-main', 'NATIVE_WORKER_PREPARE_FAILED', serializeStartupError(error));
    if (record && !record.sessionRequested) {
      record.runFinished = true;
      record.child.kill();
      if (activeLaunch === record) {
        activeLaunch = null;
      }
    }
    const message = String(error?.message || error || '安全登录会话初始化失败。').slice(0, 500);
    setLaunchStatus({ phase: 'failed', progress: 0, gameProcessId: 0, message, code: -1 });
    throw new Error(message);
  } finally {
    launchStarting = false;
    emitLaunchStatus();
  }
}

async function preloadNativeGame() {
  assertApplicationNotQuitting();
  if (['preloaded', 'authenticated'].includes(launchStatus.phase) && activeLaunch && !activeLaunch.runFinished) {
    return getPublicLaunchStatus();
  }
  const record = await createNativeSession();
  try {
    return await waitForRecordPhase(record, ['preloaded', 'authenticated'], NATIVE_PRELOAD_TIMEOUT_MS);
  } catch (error) {
    if (activeLaunch === record && !record.runFinished) {
      await cancelNativeSession().catch((cancelError) => {
        logStartupEvent('electron-main', 'PRELOAD_TIMEOUT_CANCEL_FAILED', serializeStartupError(cancelError));
      });
    }
    throw error;
  }
}

async function authenticateNativeSession(input) {
  assertApplicationNotQuitting();
  await preloadNativeGame();
  const record = activeLaunch;
  if (!record || record.runFinished || launchStatus.phase !== 'preloaded') {
    throw new Error('安全登录会话不可用，请重新打开登录器。');
  }

  const saved = resolveCredentialsForAuthentication(appRoot, input || {}, getSettingsDir());
  try {
    const result = await submitNativeCredentials(record, saved);
    const persistence = record.credentialOutcome
      ? await record.credentialOutcome
      : createUnverifiedCredentialOutcome(saved.username);
    if (persistence.metadataPersisted) {
      unavailableRememberedUsers.delete(saved.username);
    } else {
      unavailableRememberedUsers.add(saved.username);
    }
    launcherAuthenticatedUser = saved.username;
    launcherSessionCredential = {
      user: saved.username,
      remembered: persistence.remembered === true
    };
    invalidateLauncherHomeContent(launcherContentPromises);
    const authenticatedResult = {
      ...result,
      login: persistence.login,
      warning: persistence.warning
    };
    // Mirror the official route gate: renderer navigation happens only
    // after the authoritative login pipeline has committed in main.
    emitLauncherUserLogin();
    return authenticatedResult;
  } finally {
    saved.username = '';
    saved.passwordForLaunch = '';
  }
}

async function submitNativeCredentials(record, credentials, { persistCredential = true } = {}) {
  const attemptToken = record.loginAttemptGuard.acquire();
  if (!attemptToken) {
    throw new Error('账号登录正在处理中，请稍候。');
  }
  try {
    const current = getPublicRecordStatus(record);
    setRecordStatus(record, { phase: 'authenticating', progress: current.progress, gameProcessId: current.gameProcessId, message: '正在验证账号并请求一次性启动材料。' });
    record.pendingLoginUser = credentials.username;
    record.credentialOutcome = null;
    record.child.postMessage({
      type: 'login',
      payload: {
        username: credentials.username,
        password: credentials.passwordForLaunch,
        remember: credentials.remember,
        autoLogin: credentials.autoLogin,
        persistCredential: persistCredential !== false
      }
    });
    // Includes account verification, one-time material exchange, local mapping,
    // and the official client preload gate. Keep this above the worker's
    // 30s network + 45s native preload budgets.
    return await waitForRecordPhase(record, ['authenticated'], 120000);
  } finally {
    if (record.loginAttemptGuard.release(attemptToken)) {
      record.pendingLoginUser = '';
    }
  }
}

async function ensureAuthenticatedNativeSession() {
  if (activeLaunch && !activeLaunch.runFinished && launchStatus.phase === 'authenticated') {
    return activeLaunch;
  }
  if (!launcherAuthenticatedUser) {
    throw new Error('登录状态已失效，请重新登录账号。');
  }

  await preloadNativeGame();
  const record = activeLaunch;
  if (!record || record.runFinished || launchStatus.phase !== 'preloaded') {
    throw new Error('安全登录会话不可用，请重新打开登录器。');
  }

  let remembered = null;
  try {
    remembered = loadRememberedCredentials(appRoot, getSettingsDir(), launcherAuthenticatedUser);
    await submitNativeCredentials(record, remembered, { persistCredential: false });
    return record;
  } catch (error) {
    if (shouldExpireAuthenticatedSessionAfterReauth(error)) {
      clearLauncherAuthenticatedSession();
    }
    throw error;
  } finally {
    if (remembered) {
      remembered.username = '';
      remembered.passwordForLaunch = '';
    }
  }
}

async function startNativeGame({ zone = '64', zoneLabel = '电信区' } = {}, loadingAttempt = null) {
  assertApplicationNotQuitting();
  const record = await ensureAuthenticatedNativeSession();
  if (!record || record.runFinished || launchStatus.phase !== 'authenticated') {
    throw new Error('请先登录账号，等待 GameApp 预加载完成后再点击开始游戏。');
  }
  const readiness = prepareGameApp(appRoot, getSettingsDir());
  if (!readiness.ready) {
    throw new Error(readiness.message || '当前 GameApp 安装状态不满足启动条件。');
  }
  const reservation = gameInstanceManager.reserveStart({
    user: launcherAuthenticatedUser,
    zone,
    zoneLabel,
    main: true
  });
  record.gameLoadingAttempt = loadingAttempt;
  record.instanceKey = reservation.key;
  emitInstanceSnapshot();
  const current = getPublicRecordStatus(record);
  setRecordStatus(record, { phase: 'starting', progress: Math.max(80, current.progress), gameProcessId: current.gameProcessId, message: '已确认开始游戏，正在放行 GameApp…' });
  try {
    const commandAccepted = waitForCommandResult(record, 'start-game');
    record.child.postMessage({ type: 'start-game' });
    await commandAccepted;
    return getPublicLaunchStatus();
  } catch (error) {
    gameInstanceManager.markFailed(record.instanceKey, error?.message || '启动失败');
    emitInstanceSnapshot();
    throw error;
  }
}

function forceTerminatePendingNativeRecord(record, reason = 'cancel-timeout') {
  if (!record || record.runFinished) return false;
  const status = getPublicRecordStatus(record);
  if (status.phase === 'ready') return false;
  const gameProcessId = Math.max(0, Number(status.gameProcessId) || 0);
  if (Number.isSafeInteger(gameProcessId) && gameProcessId > 0) {
    try {
      process.kill(gameProcessId, 'SIGTERM');
    } catch (error) {
      if (error?.code !== 'ESRCH') {
        logStartupEvent('electron-main', 'PENDING_GAMEAPP_FORCE_TERMINATE_FAILED', {
          ...serializeStartupError(error), gameProcessId, reason
        });
      }
    }
  }
  if (!record.workerExited) {
    try { record.child.kill(); } catch { /* Electron teardown remains the final fallback. */ }
  }
  logStartupEvent('electron-main', 'PENDING_NATIVE_FORCE_CLEANUP', {
    kind: record.kind,
    phase: status.phase,
    gameProcessId,
    reason
  });
  return true;
}

async function cancelNativeSession(timeoutMs = 15000) {
  const record = activeLaunch;
  if (!record || record.runFinished) {
    return;
  }
  let onExit;
  const exited = new Promise((resolveCancel, rejectCancel) => {
    const timer = setTimeout(() => {
      record.child.removeListener('exit', onExit);
      rejectCancel(new Error('等待安全启动会话关闭超时。'));
    }, timeoutMs);
    onExit = () => {
      clearTimeout(timer);
      resolveCancel();
    };
    record.child.once('exit', onExit);
  });
  try {
    record.child.postMessage({ type: 'cancel' });
    await exited;
  } catch (error) {
    void exited.catch(() => {});
    record.child.removeListener('exit', onExit);
    forceTerminatePendingNativeRecord(record, 'primary-cancel-timeout');
    throw error;
  }
}

function normalizeInstanceLaunchRequest(input = {}) {
  const user = String(input.user || '').trim();
  const zone = String(input.zone || '64').trim();
  const server = INSTANCE_SERVERS.find((entry) => entry.id === zone && entry.available);
  if (!user) throw new GameInstanceError('INVALID_INSTANCE', '请选择多开账号。');
  if (!server) throw new GameInstanceError('INVALID_INSTANCE_ZONE', '所选区服当前不可启动。');
  return { user, zone, zoneLabel: server.label };
}

function createUtilityEnvironment() {
  const utilityEnv = { ...process.env, QQSPEED_ELECTRON_NATIVE: '1' };
  delete utilityEnv.QQSPEED_LOGIN_USER;
  delete utilityEnv.QQSPEED_LOGIN_PASSWORD;
  delete utilityEnv.QQSPEED_RUNTIME_CONTENT_KEY;
  delete utilityEnv.QQSPEED_RUNTIME_CONTENT_KEY_HEX;
  delete utilityEnv.QQSPEED_RUNTIME_CONTENT_KEY_FILE;
  return utilityEnv;
}

async function createAuxiliaryNativeSession({
  kind,
  instanceKey = '',
  pendingLoginUser = '',
  preserveSelectedAccount = false,
  gameLoadingAttempt = null
}) {
  assertApplicationNotQuitting();
  const readiness = prepareGameApp(appRoot, getSettingsDir());
  if (!readiness.ready) throw new Error(readiness.message);

  const id = `aux-${++auxiliaryLaunchSequence}`;
  const child = utilityProcess.fork(nativeWorkerEntry, [], {
    cwd: readiness.gameWorkDir,
    env: createUtilityEnvironment(),
    serviceName: kind === 'multi' ? 'QQSpeed Multi Instance Core' : 'QQSpeed Account Binding Core',
    stdio: 'ignore'
  });
  const record = {
    id,
    kind,
    child,
    spawned: false,
    sessionRequested: false,
    runFinished: false,
    workerExited: false,
    expectedCancellation: false,
    gameProcessId: 0,
    waiters: [],
    commandWaiters: [],
    pendingLoginUser,
    credentialOutcome: null,
    preserveSelectedAccount,
    instanceKey,
    gameLoadingAttempt,
    status: {
      phase: 'preparing',
      progress: 0,
      gameProcessId: 0,
      message: '正在准备独立游戏实例。'
    },
    loginAttemptGuard: new ExclusiveAttemptGuard()
  };
  auxiliaryLaunches.set(id, record);
  attachUtilityProcess(record);

  try {
    await new Promise((resolveSpawn, rejectSpawn) => {
      child.once('spawn', () => {
        record.spawned = true;
        resolveSpawn();
      });
      child.once('exit', (code) => {
        if (!record.spawned) {
          rejectSpawn(new Error(`独立启动进程未能启动（代码 ${Number(code) || 0}）。`));
        }
      });
    });
    child.postMessage({
      type: 'start-session',
      payload: {
        gameRoot: readiness.gameRoot,
        settingsDir: getSettingsDir(),
        installStateDir: getInstallStateDir(),
        executablePath: process.execPath,
        packaged: app.isPackaged,
        developmentBundlePath: app.isPackaged
          ? ''
          : join(appRoot, 'resources', 'native', 'qqspeed-runtime.bundle'),
        server: 64
      }
    });
    record.sessionRequested = true;
    setRecordStatus(record, {
      phase: 'preloading',
      progress: 0,
      gameProcessId: 0,
      message: '独立登录环境正在初始化。'
    });
    await waitForRecordPhase(record, ['preloaded'], NATIVE_PRELOAD_TIMEOUT_MS);
    return record;
  } catch (error) {
    if (!record.workerExited && !record.runFinished) {
      await cancelAuxiliaryNativeSession(record).catch((cancelError) => {
        logStartupEvent('electron-main', 'AUXILIARY_PRELOAD_CANCEL_FAILED', serializeStartupError(cancelError));
      });
    }
    throw error;
  }
}

async function submitAuxiliaryCredentials(record, credentials, { persistCredential = false } = {}) {
  record.pendingLoginUser = credentials.username;
  record.credentialOutcome = null;
  const phaseResult = waitForRecordPhase(record, ['authenticated'], 120000);
  const commandResult = waitForCommandResult(record, 'login', 120000);
  record.child.postMessage({
    type: 'login',
    payload: {
      username: credentials.username,
      password: credentials.passwordForLaunch,
      remember: credentials.remember,
      autoLogin: credentials.autoLogin,
      persistCredential
    }
  });
  try {
    const [status] = await Promise.all([phaseResult, commandResult]);
    return status;
  } finally {
    record.pendingLoginUser = '';
  }
}

async function cancelAuxiliaryNativeSession(record, timeoutMs = 15000) {
  if (!record || record.workerExited) return;
  record.expectedCancellation = true;
  let onExit;
  const exited = new Promise((resolveExit, rejectExit) => {
    const timer = setTimeout(() => {
      record.child.removeListener('exit', onExit);
      rejectExit(new Error('等待独立启动会话关闭超时。'));
    }, timeoutMs);
    onExit = () => {
      clearTimeout(timer);
      resolveExit();
    };
    record.child.once('exit', onExit);
  });
  try {
    record.child.postMessage({ type: 'cancel' });
    await exited;
  } catch (error) {
    void exited.catch(() => {});
    record.child.removeListener('exit', onExit);
    forceTerminatePendingNativeRecord(record, 'auxiliary-cancel-timeout');
    throw error;
  }
}

function findPublicAccount(user) {
  const normalized = String(user || '').trim();
  return (loadEffectiveNativeState().login?.accounts || [])
    .find((account) => String(account?.user || '').trim() === normalized) || null;
}

async function startAdditionalNativeGame(input = {}, loadingAttempt = null) {
  assertApplicationNotQuitting();
  if (!launcherAuthenticatedUser) {
    throw new Error('请先登录主账号，再使用多开管理。');
  }
  if (accountBindingInProgress) {
    throw new GameInstanceError('INSTANCE_START_IN_PROGRESS', '账号绑定正在处理中，请稍候。');
  }
  const request = normalizeInstanceLaunchRequest(input);
  const key = makeGameInstanceKey(request.user, request.zone);
  const existing = gameInstanceManager.get(key);
  if (existing && ['starting', 'running'].includes(existing.phase)) {
    throw new GameInstanceError('ACCOUNT_ZONE_ALREADY_IN_USE', `账号 ${request.user} 已在${request.zoneLabel}启动，请勿重复开启。`);
  }

  if (
    request.user === launcherAuthenticatedUser &&
    activeLaunch && !activeLaunch.runFinished &&
    getPublicRecordStatus(activeLaunch).phase === 'authenticated'
  ) {
    await startNativeGame(request, loadingAttempt);
    return emitInstanceSnapshot();
  }

  const account = findPublicAccount(request.user);
  if (!account?.hasPassword) {
    throw new Error('该账号未绑定可用密码，请先在多开管理中重新绑定。');
  }

  const reservation = gameInstanceManager.reserveStart({
    ...request,
    main: request.user === launcherAuthenticatedUser
  });
  emitInstanceSnapshot();

  let record = null;
  let remembered = null;
  try {
    record = await createAuxiliaryNativeSession({
      kind: 'multi',
      instanceKey: reservation.key,
      pendingLoginUser: request.user,
      gameLoadingAttempt: loadingAttempt
    });
    remembered = loadRememberedCredentials(appRoot, getSettingsDir(), request.user);
    await submitAuxiliaryCredentials(record, remembered, { persistCredential: false });
    const commandAccepted = waitForCommandResult(record, 'start-game', 10000);
    setRecordStatus(record, {
      phase: 'starting',
      progress: Math.max(80, getPublicRecordStatus(record).progress),
      gameProcessId: 0,
      message: `账号 ${request.user} 正在启动${request.zoneLabel}。`
    });
    record.child.postMessage({ type: 'start-game' });
    await commandAccepted;
    emitInstanceSnapshot();
    return getPublicInstanceSnapshot();
  } catch (error) {
    gameInstanceManager.markFailed(reservation.key, error?.message || '多开实例启动失败。');
    emitInstanceSnapshot();
    if (record && !record.workerExited && !record.runFinished) {
      await cancelAuxiliaryNativeSession(record).catch(() => {});
    }
    throw error;
  } finally {
    if (remembered) {
      remembered.username = '';
      remembered.passwordForLaunch = '';
    }
  }
}

async function bindInstanceAccount(input = {}) {
  if (!launcherAuthenticatedUser) throw new Error('请先登录主账号。');
  if (accountBindingInProgress || gameInstanceManager.startingKey) {
    throw new GameInstanceError('INSTANCE_START_IN_PROGRESS', '有其他游戏实例或账号绑定正在处理中，请稍候。');
  }
  const username = String(input.username || '').trim();
  const password = String(input.password || '');
  if (!username || !password) throw new Error('绑定账号和密码不能为空。');
  if (username === launcherAuthenticatedUser) throw new Error('当前登录账号无需重复绑定。');

  const snapshot = getPublicInstanceSnapshot();
  const existing = snapshot.accounts.find((account) => account.user === username);
  if (!existing && snapshot.accounts.filter((account) => !account.main).length >= MAX_BOUND_ACCOUNTS) {
    throw new Error(`最多只能绑定 ${MAX_BOUND_ACCOUNTS} 个多开账号。`);
  }

  accountBindingInProgress = true;
  let record = null;
  let credentials = null;
  try {
    record = await createAuxiliaryNativeSession({
      kind: 'binding',
      pendingLoginUser: username,
      preserveSelectedAccount: true
    });
    credentials = resolveCredentialsForAuthentication(appRoot, {
      username,
      password,
      remember: true,
      autoLogin: false
    }, getSettingsDir());
    await submitAuxiliaryCredentials(record, credentials, { persistCredential: true });
    const persistence = record.credentialOutcome
      ? await record.credentialOutcome
      : createUnverifiedCredentialOutcome(username);
    if (persistence.remembered !== true || persistence.metadataPersisted !== true) {
      throw new Error(persistence.warning || '账号验证成功，但密码未能写入 Windows DPAPI 凭据仓库。');
    }
    await cancelAuxiliaryNativeSession(record);
    return emitInstanceSnapshot();
  } finally {
    accountBindingInProgress = false;
    if (record && !record.workerExited && !record.runFinished) {
      await cancelAuxiliaryNativeSession(record).catch((error) => {
        logStartupEvent('electron-main', 'ACCOUNT_BIND_CANCEL_FAILED', serializeStartupError(error));
      });
    }
    if (credentials) {
      credentials.username = '';
      credentials.passwordForLaunch = '';
    }
    if (input && typeof input === 'object') {
      input.username = '';
      input.password = '';
    }
  }
}

async function unbindInstanceAccount(input = {}) {
  if (!launcherAuthenticatedUser) throw new Error('请先登录主账号。');
  if (accountBindingInProgress || gameInstanceManager.startingKey) {
    throw new GameInstanceError('INSTANCE_START_IN_PROGRESS', '有其他操作正在处理中，请稍候。');
  }
  const username = String(input.user || input.username || '').trim();
  if (!username || username === launcherAuthenticatedUser) {
    throw new Error('当前登录账号不能在多开管理中解绑。');
  }
  const active = getPublicInstanceSnapshot().instances.some(
    (instance) => instance.user === username && ['starting', 'running'].includes(instance.phase)
  );
  if (active) throw new Error('该账号仍有游戏实例运行，不能解绑。');

  accountBindingInProgress = true;
  let record = null;
  try {
    record = await createAuxiliaryNativeSession({ kind: 'unbinding' });
    await requestWorkerCredentialForget(record, username);
    removeRememberedAccountMetadata(getSettingsDir(), username);
    await cancelAuxiliaryNativeSession(record);
    return emitInstanceSnapshot();
  } finally {
    accountBindingInProgress = false;
    if (record && !record.workerExited && !record.runFinished) {
      await cancelAuxiliaryNativeSession(record).catch((error) => {
        logStartupEvent('electron-main', 'ACCOUNT_UNBIND_CANCEL_FAILED', serializeStartupError(error));
      });
    }
  }
}

function hasPendingNativeSession() {
  return Boolean(activeLaunch && !activeLaunch.runFinished) ||
    [...auxiliaryLaunches.values()].some((record) => !record.runFinished && !record.workerExited);
}

async function cancelAllPendingNativeSessions() {
  const pending = [...auxiliaryLaunches.values()]
    .filter((record) => !record.runFinished && !record.workerExited)
    .map((record) => cancelAuxiliaryNativeSession(record).catch((error) => {
      logStartupEvent('electron-main', 'AUXILIARY_CANCEL_FAILED', serializeStartupError(error));
    }));
  if (activeLaunch && !activeLaunch.runFinished) {
    pending.push(cancelNativeSession().catch((error) => {
      logStartupEvent('electron-main', 'PRIMARY_CANCEL_FAILED', serializeStartupError(error));
    }));
  }
  await Promise.all(pending);
}

async function switchNativeAccount() {
  if (hasActiveGame()) {
    throw new Error('游戏正在运行，不能切换账号。');
  }
  suppressTerminalSessionExpiry = true;
  try {
    await cancelNativeSession();
  } finally {
    suppressTerminalSessionExpiry = false;
  }
  const hadAuthenticatedUser = clearLauncherAuthenticatedSession({ emit: false });
  setLaunchStatus({
    phase: 'idle',
    progress: 0,
    gameProcessId: 0,
    message: '账号会话已退出，正在准备新的登录。'
  });
  const state = {
    ...loadEffectiveNativeState(),
    launch: getPublicLaunchStatus(),
    session: getPublicSession(),
    instances: getPublicInstanceSnapshot()
  };
  if (hadAuthenticatedUser) {
    emitLauncherUserLogout();
  }
  return state;
}

function startStartupNativePreload() {
  const readiness = prepareGameApp(appRoot, getSettingsDir());
  if (!readiness.ready) {
    logStartupEvent('electron-main', 'STARTUP_GAMEAPP_PRELOAD_SKIPPED', {
      reason: String(readiness.message || 'GameApp installation is not ready.').slice(0, 500)
    });
    return;
  }
  void preloadNativeGame().catch((error) => {
    logStartupEvent('electron-main', 'STARTUP_GAMEAPP_PRELOAD_FAILED', serializeStartupError(error));
  });
}

function requestApplicationQuit() {
  if (applicationQuitRequested) {
    return;
  }
  applicationQuitRequested = true;
  app.quit();
}

function createApplicationTray() {
  if (!trayController) {
    trayController = createTrayController({
      iconPath: join(appRoot, 'assets', 'app.ico'),
      createTray: (iconPath) => new Tray(iconPath),
      buildMenu: (template) => Menu.buildFromTemplate(template),
      getMainWindow: () => mainWindow,
      onQuit: requestApplicationQuit,
      onResetClosePreference: resetClosePreferencePrompt
    });
  }
  return trayController.create();
}

function startLauncherAfterUpdateGate() {
  if (!updateGatePassed || applicationQuitRequested) {
    return false;
  }
  if (!mainWindow || mainWindow.isDestroyed()) {
    createMainWindow();
  }
  createApplicationTray();
  if (!startupNativePreloadStarted) {
    startStartupNativePreload();
    startupNativePreloadStarted = true;
  }
  return true;
}

function publishUpdateProgress(progress = {}) {
  latestUpdateProgress = {
    phase: String(progress.phase || 'checking'),
    version: String(progress.version || ''),
    downloadedBytes: Math.max(0, Number(progress.downloadedBytes) || 0),
    totalBytes: Math.max(0, Number(progress.totalBytes) || 0),
    percent: Math.max(0, Math.min(100, Number(progress.percent) || 0)),
    bytesPerSecond: Math.max(0, Number(progress.bytesPerSecond) || 0)
  };
  if (updateWindowLoaded && updateWindow && !updateWindow.isDestroyed()) {
    updateWindow.webContents.send(UPDATE_PROGRESS_CHANNEL, latestUpdateProgress);
  }
}

function showUpdateWindowWhenReady() {
  if (
    updateWindowMayShow
    && updateWindowReady
    && updateWindow
    && !updateWindow.isDestroyed()
  ) {
    updateWindow.show();
    updateWindow.focus();
  }
}

function createUpdateWindow() {
  if (updateWindow && !updateWindow.isDestroyed()) {
    return updateWindow;
  }
  updateWindowLoaded = false;
  updateWindowReady = false;
  updateWindowMayShow = false;
  updateWindow = new BrowserWindow({
    width: 520,
    height: 300,
    show: false,
    resizable: false,
    minimizable: true,
    maximizable: false,
    fullscreenable: false,
    closable: false,
    autoHideMenuBar: true,
    backgroundColor: '#17133f',
    webPreferences: {
      preload: updatePreloadEntry,
      contextIsolation: true,
      sandbox: true,
      nodeIntegration: false,
      webSecurity: true
    }
  });
  updateWindow.setMenu(null);
  updateWindow.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  updateWindow.webContents.on('will-navigate', (event, url) => {
    if (url !== updateRendererEntryUrl) {
      event.preventDefault();
    }
  });
  updateWindow.webContents.on('did-finish-load', () => {
    updateWindowLoaded = true;
    publishUpdateProgress(latestUpdateProgress);
  });
  updateWindow.once('ready-to-show', () => {
    updateWindowReady = true;
    showUpdateWindowWhenReady();
  });
  updateWindowShowTimer = setTimeout(() => {
    updateWindowShowTimer = null;
    updateWindowMayShow = true;
    showUpdateWindowWhenReady();
  }, UPDATE_WINDOW_SHOW_DELAY_MS);
  updateWindowShowTimer.unref?.();
  updateWindow.on('closed', () => {
    if (updateWindowShowTimer) {
      clearTimeout(updateWindowShowTimer);
      updateWindowShowTimer = null;
    }
    updateWindowLoaded = false;
    updateWindowReady = false;
    updateWindowMayShow = false;
    updateWindow = null;
  });
  updateWindow.loadFile(updateRendererEntry).catch((error) => {
    logStartupEvent('electron-main', 'UPDATE_WINDOW_LOAD_FAILED', serializeStartupError(error));
  });
  return updateWindow;
}

function destroyUpdateWindow() {
  if (updateWindowShowTimer) {
    clearTimeout(updateWindowShowTimer);
    updateWindowShowTimer = null;
  }
  if (!updateWindow || updateWindow.isDestroyed()) {
    return;
  }
  updateWindow.destroy();
  updateWindow = null;
}

async function runStartupUpdateGate() {
  updateGatePassed = true;
  logStartupEvent('electron-main', 'UPDATE_GATE_DISABLED');
  return true;
}

function createMainWindow() {
  logStartupEvent('electron-main', 'WINDOW_CREATE_BEGIN', {
    elapsedMs: Date.now() - mainStartupStartedAt
  });
  mainWindowShouldBeVisible = true;
  try {
    mainWindow = new BrowserWindow({
      width: BASE_WINDOW_WIDTH,
      height: BASE_WINDOW_HEIGHT,
      minWidth: MIN_WINDOW_WIDTH,
      minHeight: MIN_WINDOW_HEIGHT,
      useContentSize: true,
      center: true,
      frame: false,
      show: false,
      resizable: true,
      maximizable: true,
      fullscreenable: false,
      backgroundColor: '#1d1559',
      webPreferences: {
        preload: preloadEntry,
        contextIsolation: true,
        sandbox: true,
        nodeIntegration: false,
        webSecurity: true
      }
    });
  } catch (error) {
    logStartupEvent('electron-main', 'WINDOW_CREATE_FAILED', serializeStartupError(error));
    throw error;
  }

  logStartupEvent('electron-main', 'WINDOW_CREATED', {
    elapsedMs: Date.now() - mainStartupStartedAt
  });
  const rendererLoadWindow = mainWindow;

  mainWindow.setMenu(null);
  mainWindow.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  mainWindow.webContents.on('will-navigate', (event, url) => {
    if (url !== rendererEntryUrl) {
      event.preventDefault();
    }
  });
  mainWindow.webContents.on('did-start-loading', () => {
    logStartupEvent('electron-main', 'RENDERER_LOAD_BEGIN', {
      elapsedMs: Date.now() - mainStartupStartedAt
    });
  });
  mainWindow.webContents.on('did-finish-load', () => {
    logStartupEvent('electron-main', 'RENDERER_LOAD_OK', {
      elapsedMs: Date.now() - mainStartupStartedAt
    });
    emitLaunchStatus();
    emitInstanceSnapshot();
  });
  mainWindow.webContents.on('did-fail-load', (_event, errorCode, errorDescription, validatedURL, isMainFrame) => {
    const loadEvent = applicationQuitRequested
      ? 'RENDERER_LOAD_EVENT_CANCELLED_DURING_QUIT'
      : 'RENDERER_LOAD_FAILED';
    logStartupEvent('electron-main', loadEvent, {
      errorCode,
      message: errorDescription,
      url: validatedURL,
      isMainFrame
    });
  });
  mainWindow.webContents.on('render-process-gone', (_event, details) => {
    logStartupEvent('electron-main', 'RENDERER_PROCESS_GONE', {
      reason: details.reason,
      exitCode: details.exitCode
    });
  });
  mainWindow.on('unresponsive', () => {
    logStartupEvent('electron-main', 'WINDOW_UNRESPONSIVE', {
      elapsedMs: Date.now() - mainStartupStartedAt
    });
  });
  mainWindow.on('close', (event) => {
    if (!applicationQuitRequested) {
      event.preventDefault();
      requestRendererCloseDecision(mainWindow);
      return;
    }
    if (hasPendingNativeSession() && !closeAfterSessionCancellation) {
      event.preventDefault();
      closeAfterSessionCancellation = true;
      mainWindowShouldBeVisible = false;
      mainWindow.hide();
      cancelAllPendingNativeSessions().catch((error) => {
        logStartupEvent('electron-main', 'WINDOW_CLOSE_CANCEL_FAILED', {
          message: String(error?.message || error || 'Native session cancellation failed.').slice(0, 500)
        });
      }).finally(() => {
        mainWindow?.close();
      });
    }
  });
  mainWindow.on('closed', () => {
    mainWindow = null;
  });
  mainWindow.on('hide', () => {
    mainWindowShouldBeVisible = false;
  });
  mainWindow.on('show', () => {
    mainWindowShouldBeVisible = true;
  });
  mainWindow.once('ready-to-show', () => {
    logStartupEvent('electron-main', 'WINDOW_READY_TO_SHOW', {
      elapsedMs: Date.now() - mainStartupStartedAt
    });
    if (mainWindowShouldBeVisible) {
      destroyUpdateWindow();
      mainWindow?.show();
    }
  });
  mainWindow.once('show', () => {
    logStartupEvent('electron-main', 'WINDOW_SHOWN', {
      elapsedMs: Date.now() - mainStartupStartedAt
    });
  });
  logStartupEvent('electron-main', 'RENDERER_LOAD_REQUESTED');
  rendererLoadWindow.loadFile(rendererEntry).catch((error) => {
    if (applicationQuitRequested) {
      logStartupEvent('electron-main', 'RENDERER_LOAD_CANCELLED_DURING_QUIT', {
        ...serializeStartupError(error),
        stage: rendererLoadWindow.isDestroyed() ? 'window-destroyed' : 'window-active'
      });
      return;
    }
    logStartupEvent('electron-main', 'RENDERER_LOAD_FAILED', serializeStartupError(error));
    setImmediate(() => {
      throw error;
    });
  });
}

ipcMain.handle('launcher:get-gm-catalog', async (event, request = {}) => {
  assertTrustedSender(event, 'launcher:get-gm-catalog');
  assertGmItemAccess();
  return getGmItemQueryService().getCatalog(request?.primaryCategory);
});

ipcMain.handle('launcher:query-gm-items', async (event, request = {}) => {
  assertTrustedSender(event, 'launcher:query-gm-items');
  assertGmItemAccess();
  return getGmItemQueryService().queryItems(request);
});

ipcMain.handle('launcher:get-state', async (event) => {
  assertTrustedSender(event, 'launcher:get-state');
  return {
    ...loadEffectiveNativeState(),
    launch: getPublicLaunchStatus(),
    session: getPublicSession(),
    instances: getPublicInstanceSnapshot()
  };
});

ipcMain.handle('launcher:get-launch-status', async (event) => {
  assertTrustedSender(event, 'launcher:get-launch-status');
  return getPublicLaunchStatus();
});

ipcMain.handle('launcher:get-content', async (event, request) => {
  assertTrustedSender(event, 'launcher:get-content');
  const scope = typeof request === 'string' ? request : request?.scope;
  const data = await getLauncherContent(scope, request?.refresh === true);
  return data ? { ok: true, data } : { ok: false, error: '暂时无法加载启动器内容。' };
});

ipcMain.handle('launcher:open-external', async (event, value) => {
  assertTrustedSender(event, 'launcher:open-external');
  const url = normalizeOfficialExternalUrl(value);
  if (!url) {
    throw new Error('拒绝打开未授权链接。');
  }
  await shell.openExternal(url);
  return { ok: true };
});

ipcMain.handle('launcher:select-game-root', async (event) => {
  assertTrustedSender(event, 'launcher:select-game-root');
  const owner = BrowserWindow.fromWebContents(event.sender);
  const result = await dialog.showOpenDialog(owner || undefined, {
    title: '请选择 QQ飞车主目录',
    buttonLabel: '选择此目录',
    properties: ['openDirectory']
  });

  if (result.canceled || !result.filePaths[0]) {
    return loadEffectiveNativeState();
  }

  const gameRoot = result.filePaths[0];
  if (!isValidGameRoot(gameRoot)) {
    throw new Error('选择的目录不是 QQ飞车主目录：需要存在 Releasephysx27_64\\GameApp.exe。');
  }

  if (hasAnyActiveGame()) {
    throw new Error('游戏正在运行，不能切换游戏主目录。');
  }
  await cancelNativeSession();

  saveLauncherSettings(getSettingsDir(), {
    gameRoot: normalizeGameRoot(gameRoot),
    gameRootSource: 'user'
  });
  return loadEffectiveNativeState();
});

ipcMain.handle('launcher:prepare-game', async (event) => {
  assertTrustedSender(event, 'launcher:prepare-game');
  return applyEffectiveReadinessState(prepareGameApp(appRoot, getSettingsDir()));
});

ipcMain.handle('launcher:preload-game', async (event) => {
  assertTrustedSender(event, 'launcher:preload-game');
  return preloadNativeGame();
});

ipcMain.handle('launcher:login', async (event, payload) => {
  assertTrustedSender(event, 'launcher:login');
  return authenticateNativeSession(payload || {});
});

ipcMain.handle('launcher:switch-account', async (event) => {
  assertTrustedSender(event, 'launcher:switch-account');
  return switchNativeAccount();
});

ipcMain.handle('launcher:get-instances', async (event) => {
  assertTrustedSender(event, 'launcher:get-instances');
  return getPublicInstanceSnapshot();
});

ipcMain.handle('launcher:bind-instance-account', async (event, payload) => {
  assertTrustedSender(event, 'launcher:bind-instance-account');
  return bindInstanceAccount(payload || {});
});

ipcMain.handle('launcher:unbind-instance-account', async (event, payload) => {
  assertTrustedSender(event, 'launcher:unbind-instance-account');
  return unbindInstanceAccount(payload || {});
});

ipcMain.handle('launcher:start-instance', async (event, payload) => {
  assertTrustedSender(event, 'launcher:start-instance');
  const loadingAttempt = hideForGameLoading(BrowserWindow.fromWebContents(event.sender));
  try {
    return await startAdditionalNativeGame(payload || {}, loadingAttempt);
  } catch (error) {
    restoreAfterGameLoadingFailure(loadingAttempt);
    throw error;
  }
});

ipcMain.handle('launcher:start-game', async (event, payload) => {
  assertTrustedSender(event, 'launcher:start-game');
  const request = normalizeInstanceLaunchRequest({
    user: launcherAuthenticatedUser,
    zone: payload?.zone || '64'
  });
  const loadingAttempt = hideForGameLoading(BrowserWindow.fromWebContents(event.sender));
  try {
    return await startNativeGame(request, loadingAttempt);
  } catch (error) {
    restoreAfterGameLoadingFailure(loadingAttempt);
    throw error;
  }
});

ipcMain.handle('window:minimize', async (event) => {
  assertWindowControlSender(event, 'window:minimize');
  BrowserWindow.fromWebContents(event.sender)?.minimize();
});

ipcMain.handle('window:close', async (event) => {
  assertWindowControlSender(event, 'window:close');
  const window = BrowserWindow.fromWebContents(event.sender);
  return {
    requested: requestRendererCloseDecision(window),
    busy: isLaunchBusy()
  };
});

ipcMain.handle('window:apply-close-action', async (event, action) => {
  assertWindowControlSender(event, 'window:apply-close-action');
  const window = BrowserWindow.fromWebContents(event.sender);
  if (!window || window.isDestroyed()) {
    return { closed: false, hidden: false };
  }
  if (action === 'tray') {
    mainWindowShouldBeVisible = false;
    if (!trayController?.hideToTray(window)) {
      window.hide();
    }
    return { closed: false, hidden: true, busy: isLaunchBusy() };
  }
  if (action === 'quit') {
    requestApplicationQuit();
    return { closed: true, hidden: false, busy: isLaunchBusy() };
  }
  throw new Error('Unsupported close action.');
});

const hasSingleInstanceLock = app.requestSingleInstanceLock();

if (!hasSingleInstanceLock) {
  logStartupEvent('electron-main', 'SINGLE_INSTANCE_LOCK_REJECTED');
  app.quit();
} else {
  logStartupEvent('electron-main', 'SINGLE_INSTANCE_LOCK_ACQUIRED');
  app.on('child-process-gone', (_event, details) => {
    logStartupEvent('electron-main', 'CHILD_PROCESS_GONE', {
      type: details.type,
      reason: details.reason,
      exitCode: details.exitCode,
      serviceName: details.serviceName,
      name: details.name
    });
  });
  app.on('before-quit', () => {
    applicationQuitRequested = true;
    trayController?.destroy();
    closeGmItemQueryService();
    logStartupEvent('electron-main', 'APP_BEFORE_QUIT');
  });
  app.on('second-instance', () => {
    logStartupEvent('electron-main', 'SECOND_INSTANCE_RECEIVED', {
      status: [
        `gate=${updateGatePassed ? 1 : 0}`,
        `update=${updateWindow && !updateWindow.isDestroyed() ? 1 : 0}`,
        `main=${mainWindow && !mainWindow.isDestroyed() ? 1 : 0}`
      ].join(',')
    });
    restoreMainWindow('second-instance');
  });

  logStartupEvent('electron-main', 'APP_READY_BEGIN');
  app.whenReady().then(async () => {
    logStartupEvent('electron-main', 'APP_READY_OK', {
      elapsedMs: Date.now() - mainStartupStartedAt
    });
    if (await runStartupUpdateGate()) {
      startLauncherAfterUpdateGate();
    }
  }).catch((error) => {
    logStartupEvent('electron-main', 'APP_READY_FAILED', serializeStartupError(error));
    throw error;
  });

  app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') {
      app.quit();
    }
  });

  app.on('activate', () => {
    if (!updateGatePassed) {
      restoreMainWindow();
      return;
    }
    if (BrowserWindow.getAllWindows().length === 0) {
      startLauncherAfterUpdateGate();
    } else {
      restoreMainWindow();
    }
  });
}
