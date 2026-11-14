import { timingSafeEqual } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { isAbsolute, resolve } from 'node:path';

import { createCredentialOwner, CredentialVaultError } from './credential-vault.js';
import { readEmbeddedRuntimeBundle } from './executable-runtime-resource.js';
import { createInstallKeyVault } from './install-key-vault.js';
import { LOCKED_LOGIN_ENDPOINT } from './locked-endpoint.js';
import {
  loginPrivateServer,
  PrivateLoginError,
  requestPreloadRuntimeMaterial
} from './login/private-login-client.js';
import { mapNativeBridgeFromBytes } from './native-memory-loader.js';
import { shouldUseAuthenticatedPreloadFallback } from './preload-fallback-policy.js';
import {
  ExclusiveAttemptGuard,
  LauncherSessionStateMachine
} from './session-state-machine.js';
import { logStartupEvent } from './startup-log.js';
import { TransientSecretRedactor } from './transient-secret-redactor.js';
import {
  decodeNativeRuntimeBundle,
  inspectNativeRuntimeBundle
} from '../shared/native-runtime-bundle.js';
import { toCustomerErrorMessage } from '../shared/customer-error-message.js';

const parentPort = process.parentPort;
const POLL_INTERVAL_MS = 150;
const PRELOGIN_MATERIAL_TIMEOUT_MS = 5000;
const NATIVE_PRELOAD_WAIT_MS = 45_000;
const NATIVE_AUTH_WAIT_MS = 5000;

const phaseDescriptors = Object.freeze({
  idle: { progress: 0, busy: false, message: '启动会话尚未开始。' },
  preloading: { progress: 0, busy: true, message: '正在验证组件并预加载 GameApp…' },
  preloaded: { progress: 55, busy: false, message: 'GameApp 已预加载，请输入账号。' },
  authenticating: { progress: 65, busy: true, message: '正在验证账号…' },
  authenticated: { progress: 80, busy: false, message: '账号已登录，点击开始游戏后进入客户端。' },
  starting: { progress: 90, busy: true, message: '已放行 GameApp，正在进入游戏…' },
  ready: { progress: 100, busy: false, message: '游戏已就绪。' },
  failed: { progress: 0, busy: false, message: '启动游戏失败。' },
  cancelled: { progress: 0, busy: false, message: '启动会话已取消。' }
});

const machine = new LauncherSessionStateMachine();
let sessionStarted = false;
let finished = false;
let pollTimer = null;
let bindings = null;
let mappedBridge = null;
let nativeContext = null;
let deferredNativePreload = false;
let nativeSessionActive = false;
let preloadedGameProcessId = 0;
let pendingTerminalFailure = null;
let lastStatusKey = '';
let cancelRequested = false;
let preloadAbortController = null;
let loginAbortController = null;
let credentialOwner = null;
const loginAttemptGuard = new ExclusiveAttemptGuard();
const transientSecrets = new TransientSecretRedactor();

logStartupEvent('native-worker', 'NATIVE_WORKER_STARTED', {
  platform: process.platform,
  arch: process.arch
});

function post(message) {
  try {
    const safe = message && typeof message === 'object' ? { ...message } : message;
    if (safe && typeof safe === 'object') {
      if (typeof safe.message === 'string') safe.message = customerMessage(safe.message);
      if (typeof safe.error === 'string') safe.error = customerMessage(safe.error);
      if (typeof safe.reason === 'string') safe.reason = customerMessage(safe.reason);
      if (typeof safe.code === 'string') safe.code = safeMessage(safe.code);
      if (typeof safe.warning === 'string') safe.warning = customerMessage(safe.warning);
      if (safe.status && typeof safe.status === 'object') {
        safe.status = { ...safe.status };
        if (typeof safe.status.message === 'string') {
          safe.status.message = safeMessage(safe.status.message);
        }
      }
    }
    parentPort?.postMessage(safe);
  } catch {
    // The Electron parent may already be shutting down.
  }
}

function safeMessage(value, fallback = '') {
  return transientSecrets.redact(value, fallback);
}

function customerMessage(value, fallback = '') {
  return toCustomerErrorMessage(safeMessage(value, fallback), fallback);
}

function serializeSafeError(error) {
  return transientSecrets.serializeError(error);
}

function readNativeStatus() {
  try {
    return {
      state: Number(bindings?.getState()) || 0,
      progress: Math.max(0, Math.min(100, Number(bindings?.getProgress()) || 0)),
      gameProcessId: Math.max(0, Number(bindings?.getGameProcessId()) || 0)
    };
  } catch {
    return { state: 0, progress: 0, gameProcessId: 0 };
  }
}

function readNativeError() {
  try {
    return safeMessage(bindings?.getLastError());
  } catch {
    return '';
  }
}

function postCurrentStatus(options = {}) {
  const descriptor = phaseDescriptors[machine.phase] || phaseDescriptors.failed;
  const native = readNativeStatus();
  const status = {
    phase: machine.phase,
    progress: Math.max(descriptor.progress, native.progress),
    busy: descriptor.busy,
    gameProcessId: native.gameProcessId,
    message: safeMessage(options.message, descriptor.message)
  };
  const key = JSON.stringify(status);
  if (options.force || key !== lastStatusKey) {
    lastStatusKey = key;
    post({ type: 'status', status });
  }
  return native;
}

function transition(event, options = {}) {
  machine.transition(event);
  return postCurrentStatus({ ...options, force: true });
}

function validateSessionPayload(payload) {
  const gameRoot = resolve(String(payload?.gameRoot || ''));
  const settingsDirInput = String(payload?.settingsDir || '');
  const settingsDir = resolve(settingsDirInput);
  const installStateDirInput = String(payload?.installStateDir || '');
  const installStateDir = resolve(installStateDirInput);
  const executablePathInput = String(payload?.executablePath || '');
  const executablePath = resolve(executablePathInput);
  const packaged = payload?.packaged === true;
  const developmentBundlePathInput = String(payload?.developmentBundlePath || '');
  const developmentBundlePath = developmentBundlePathInput
    ? resolve(developmentBundlePathInput)
    : '';
  const server = Number(payload?.server);
  if (process.platform !== 'win32' || process.arch !== 'x64') {
    throw new Error('启动组件仅支持 Windows x64。');
  }
  if (!isAbsolute(gameRoot) || !settingsDirInput || !installStateDirInput || !executablePathInput ||
      !isAbsolute(settingsDirInput) || !isAbsolute(settingsDir) ||
      !isAbsolute(installStateDirInput) || !isAbsolute(installStateDir) ||
      !isAbsolute(executablePathInput) || !isAbsolute(executablePath) ||
      (!packaged && (!developmentBundlePathInput || !isAbsolute(developmentBundlePathInput) ||
        !isAbsolute(developmentBundlePath)))) {
    throw new Error('启动参数中的目录无效。');
  }
  if (packaged && executablePath.toLowerCase() !== resolve(process.execPath).toLowerCase()) {
    throw new Error('已打包启动组件必须来自当前可执行文件。');
  }
  if (server !== 64) {
    throw new Error('服务器参数无效。');
  }
  return {
    gameRoot,
    settingsDir,
    installStateDir,
    executablePath,
    packaged,
    developmentBundlePath,
    server
  };
}

function requireBuffer(value, length, label) {
  if (!Buffer.isBuffer(value) || value.length !== length) {
    throw new Error(`${label}无效。`);
  }
  return value;
}

function wipeBuffer(value) {
  if (Buffer.isBuffer(value)) value.fill(0);
}

function sameBytes(left, right) {
  return Buffer.isBuffer(left) && Buffer.isBuffer(right) &&
    left.length === right.length && timingSafeEqual(left, right);
}

function validateBundleMetadata(metadata) {
  requireBuffer(metadata?.bundleId, 32, '内嵌组件包标识');
  for (const name of ['bridgeSha256', 'hookSha256', 'bundleSha256']) {
    if (!/^[0-9a-f]{64}$/.test(String(metadata?.[name] || ''))) {
      throw new Error('内嵌组件包摘要无效。');
    }
  }
  if (!Number.isSafeInteger(metadata.bridgeSize) || metadata.bridgeSize <= 0 ||
      !Number.isSafeInteger(metadata.hookSize) || metadata.hookSize <= 0) {
    throw new Error('内嵌组件包尺寸无效。');
  }
  return metadata;
}

function validateInstallIdentity(identity) {
  if (identity?.algorithm !== 'rsa-oaep-sha256') {
    throw new Error('本机安装密钥算法无效。');
  }
  requireBuffer(identity.publicKey, 259, '本机安装公钥');
  requireBuffer(identity.fingerprint, 32, '本机安装指纹');
  return identity;
}

async function initializeNativeRuntimeContext(payload) {
  let bundleBytes = null;
  let metadata = null;
  let identity = null;
  try {
    bundleBytes = payload.packaged
      ? readEmbeddedRuntimeBundle(process.execPath)
      : await readFile(payload.developmentBundlePath);
    if (!Buffer.isBuffer(bundleBytes) || bundleBytes.length <= 0) {
      throw new Error('可执行文件中的内嵌组件包无效。');
    }
    metadata = validateBundleMetadata(inspectNativeRuntimeBundle(bundleBytes));
    const vault = createInstallKeyVault(payload.installStateDir);
    if (!vault || typeof vault.getPublicIdentity !== 'function' || typeof vault.decrypt !== 'function') {
      throw new Error('本机安装密钥保险库无效。');
    }
    identity = validateInstallIdentity(await vault.getPublicIdentity());
    return {
      payload,
      bundleBytes,
      metadata,
      vault,
      identity
    };
  } catch (error) {
    wipeBuffer(bundleBytes);
    wipeBuffer(metadata?.bundleId);
    wipeBuffer(identity?.publicKey);
    wipeBuffer(identity?.fingerprint);
    throw error;
  }
}

function cleanupNativeContext() {
  if (!nativeContext) return;
  wipeBuffer(nativeContext.bundleBytes);
  wipeBuffer(nativeContext.metadata?.bundleId);
  wipeBuffer(nativeContext.identity?.publicKey);
  wipeBuffer(nativeContext.identity?.fingerprint);
  nativeContext.bundleBytes = null;
  nativeContext.metadata = null;
  nativeContext.identity = null;
  nativeContext.vault = null;
  nativeContext = null;
}

function cleanupMappedBridge() {
  if (!mappedBridge) {
    bindings = null;
    return true;
  }
  const mapping = mappedBridge;
  const removed = mapping.cleanup();
  if (removed) {
    mappedBridge = null;
    bindings = null;
  }
  return removed;
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

function observeNativeState() {
  if (finished) return;
  const native = readNativeStatus();
  try {
    if (native.state === 6 && machine.phase === 'starting') {
      transition('native-ready');
      return;
    }
    if (native.state === 7 && !['failed', 'cancelled', 'ready'].includes(machine.phase)) {
      transition('native-failed', { message: readNativeError() || '原生 Hook 会话失败。' });
      return;
    }
    if (native.state === 8 && !['failed', 'cancelled', 'ready'].includes(machine.phase)) {
      transition('cancel');
      return;
    }
  } catch {
    // A concurrent terminal callback remains authoritative.
  }
  postCurrentStatus();
}

function finishSession({ ok, code, phase, message, gameProcessId = 0 }) {
  if (finished) return;
  const finalNative = readNativeStatus();
  finished = true;
  stopPolling();
  preloadAbortController?.abort();
  preloadAbortController = null;
  loginAbortController?.abort();
  loginAbortController = null;
  pendingTerminalFailure = null;
  try {
    cleanupMappedBridge();
  } catch (error) {
    logStartupEvent('native-worker', 'NATIVE_MAPPING_CLEANUP_FAILED', serializeSafeError(error));
  }
  cleanupNativeContext();
  const finalMessage = safeMessage(message, phaseDescriptors[phase]?.message);
  logStartupEvent('native-worker', 'NATIVE_SESSION_END', {
    status: ok ? 'ok' : phase,
    code: Number(code) || 0,
    message: finalMessage
  });
  post({
    type: 'status',
    status: {
      phase,
      progress: ok ? 100 : finalNative.progress,
      busy: false,
      gameProcessId: Math.max(0, Number(gameProcessId) || finalNative.gameProcessId || 0),
      message: finalMessage
    }
  });
  post({ type: 'result', ok, code, phase, message: finalMessage, gameProcessId });
  setTimeout(() => process.exit(ok || phase === 'cancelled' ? 0 : 1), 25);
}

function requirePreloadedGameProcess(native) {
  if (native?.state !== 2 || !Number.isSafeInteger(native?.gameProcessId) || native.gameProcessId <= 0) {
    throw new Error(readNativeError() || 'GameApp 预加载未返回有效进程 PID。');
  }
  return native;
}

async function startNativeGameFromMaterial(context, runtimeMaterial) {
  let decodedRuntime;
  let contentKey;
  let bridgeBytes;
  let hookBytes;
  try {
    contentKey = requireBuffer(runtimeMaterial?.contentKey, 32, '一次性组件解包密钥');
    requireBuffer(runtimeMaterial?.bundleId, 32, '服务端组件包标识');
    if (!sameBytes(runtimeMaterial.bundleId, context.metadata.bundleId)) {
      throw new Error('服务端一次性材料与本机组件包不匹配。');
    }

    decodedRuntime = decodeNativeRuntimeBundle(context.bundleBytes, { contentKey });
    bridgeBytes = decodedRuntime?.bridgeBytes;
    hookBytes = decodedRuntime?.hookBytes;
    if (!Buffer.isBuffer(bridgeBytes) || bridgeBytes.length !== context.metadata.bridgeSize ||
        !Buffer.isBuffer(hookBytes) || hookBytes.length !== context.metadata.hookSize ||
        !sameBytes(decodedRuntime?.bundleId, context.metadata.bundleId) ||
        decodedRuntime?.bridgeSha256 !== context.metadata.bridgeSha256 ||
        decodedRuntime?.hookSha256 !== context.metadata.hookSha256 ||
        decodedRuntime?.bundleSha256 !== context.metadata.bundleSha256) {
      throw new Error('解密后的原生组件包身份校验失败。');
    }
    context.bundleBytes.fill(0);
    context.bundleBytes = null;
    contentKey.fill(0);
    contentKey = null;

    mappedBridge = mapNativeBridgeFromBytes(bridgeBytes, {
      expectedSha256: decodedRuntime.bridgeSha256
    });
    bridgeBytes.fill(0);
    bridgeBytes = null;
    bindings = mappedBridge.bindings;
    for (const required of [
      'setHookPayload', 'beginSession', 'applyAuth', 'startGame', 'cancel',
      'getState', 'getProgress', 'isBusy', 'getGameProcessId', 'getLastError'
    ]) {
      if (typeof bindings?.[required] !== 'function') {
        throw new Error(`启动桥接缺少必需导出：${required}。`);
      }
    }

    const hookCode = Number(bindings.setHookPayload(hookBytes, hookBytes.length));
    hookBytes.fill(0);
    hookBytes = null;
    if (hookCode !== 0) {
      throw new Error(readNativeError() || '启动桥接拒绝了内存 Hook 载荷。');
    }

    logStartupEvent('native-worker', 'NATIVE_MEMORY_MAPPING_OK');
    nativeSessionActive = true;
    if (!pollTimer) pollTimer = setInterval(observeNativeState, POLL_INTERVAL_MS);
    try {
      bindings.beginSession.async(
        context.payload.gameRoot,
        context.payload.server,
        LOCKED_LOGIN_ENDPOINT.host,
        LOCKED_LOGIN_ENDPOINT.port,
        (error, result) => {
          nativeSessionActive = false;
          const native = readNativeStatus();
          const code = Number(result);
          if (pendingTerminalFailure) {
            const terminal = pendingTerminalFailure;
            pendingTerminalFailure = null;
            finishSession(terminal);
            return;
          }
          if (error) {
            finishSession({ ok: false, code: 3, phase: 'failed', message: '原生 Hook 会话调用异常。' });
            return;
          }
          if (code === 4 || native.state === 8 || cancelRequested) {
            finishSession({ ok: false, code: 4, phase: 'cancelled', message: '启动会话已取消。' });
            return;
          }
          if (code !== 0) {
            finishSession({
              ok: false,
              code: Number.isInteger(code) ? code : 3,
              phase: 'failed',
              message: readNativeError() || `启动游戏失败（代码 ${Number.isInteger(code) ? code : 3}）。`
            });
            return;
          }
          if (!Number.isSafeInteger(native.gameProcessId) || native.gameProcessId <= 0) {
            finishSession({ ok: false, code: 3, phase: 'failed', message: '启动组件未返回有效的游戏进程 PID。' });
            return;
          }
          if (preloadedGameProcessId <= 0 || native.gameProcessId !== preloadedGameProcessId) {
            finishSession({ ok: false, code: 3, phase: 'failed', message: 'GameApp 进程 PID 在启动期间发生变化。' });
            return;
          }
          finishSession({ ok: true, code: 0, phase: 'ready', message: '游戏已就绪。', gameProcessId: native.gameProcessId });
        }
      );
    } catch (error) {
      nativeSessionActive = false;
      throw error;
    }

    const preloaded = await waitForNativeState(2, NATIVE_PRELOAD_WAIT_MS);
    requirePreloadedGameProcess(preloaded);
    preloadedGameProcessId = preloaded.gameProcessId;
    logStartupEvent('native-worker', 'GAMEAPP_PRELOADED', {
      gameProcessId: preloaded.gameProcessId
    });
    return preloaded;
  } finally {
    if (contentKey) contentKey.fill(0);
    if (bridgeBytes) bridgeBytes.fill(0);
    if (hookBytes) hookBytes.fill(0);
    wipeBuffer(decodedRuntime?.bundleId);
    decodedRuntime = null;
  }
}

async function preloadNativeGame(context, signal) {
  let runtimeMaterial;
  try {
    runtimeMaterial = await requestPreloadRuntimeMaterial({
      host: LOCKED_LOGIN_ENDPOINT.host,
      port: LOCKED_LOGIN_ENDPOINT.port,
      timeoutMs: PRELOGIN_MATERIAL_TIMEOUT_MS,
      signal,
      runtimeMaterial: {
        required: true,
        algorithm: context.identity.algorithm,
        bundleId: context.metadata.bundleId,
        publicKey: context.identity.publicKey,
        fingerprint: context.identity.fingerprint,
        decrypt: (ciphertext) => context.vault.decrypt(ciphertext)
      }
    });
    if (cancelRequested || signal.aborted) {
      throw new PrivateLoginError('ABORTED', '预加载已取消。');
    }
    return await startNativeGameFromMaterial(context, runtimeMaterial);
  } finally {
    wipeBuffer(runtimeMaterial?.contentKey);
    wipeBuffer(runtimeMaterial?.bundleId);
    wipeBuffer(runtimeMaterial?.tokenId);
    runtimeMaterial = null;
  }
}

async function initializeSession(rawPayload) {
  if (sessionStarted) {
    post({ type: 'command-result', command: 'start-session', ok: false, fatal: true, message: '启动会话已在运行。' });
    return;
  }
  if (cancelRequested) {
    transition('cancel');
    finishSession({ ok: false, code: 4, phase: 'cancelled', message: '启动会话已取消。' });
    return;
  }
  sessionStarted = true;
  logStartupEvent('native-worker', 'NATIVE_SESSION_BEGIN');

  transition('begin', { message: '正在验证组件并预加载 GameApp…' });
  try {
    const payload = validateSessionPayload(rawPayload);
    credentialOwner = createCredentialOwner(payload.settingsDir);
    nativeContext = await initializeNativeRuntimeContext(payload);
    if (cancelRequested) {
      cleanupNativeContext();
      return;
    }
    const attemptController = new AbortController();
    preloadAbortController = attemptController;
    try {
      try {
        await preloadNativeGame(nativeContext, attemptController.signal);
      } catch (error) {
        if (!shouldUseAuthenticatedPreloadFallback(error)) throw error;
        deferredNativePreload = true;
        logStartupEvent('native-worker', 'AUTHENTICATED_PRELOAD_FALLBACK', {
          code: error.code || 'NETWORK_ERROR'
        });
      }
    } finally {
      if (preloadAbortController === attemptController) preloadAbortController = null;
    }
    if (finished || cancelRequested) return;
    if (deferredNativePreload) {
      transition('native-preloaded', { message: '登录服务器暂不支持提前加载，登录后将自动启动 GameApp。' });
    } else {
      cleanupNativeContext();
      transition('native-preloaded', { message: 'GameApp 已预加载，请输入账号。' });
    }
    post({ type: 'command-result', command: 'start-session', ok: true, code: 0, fatal: false, message: '' });
  } catch (error) {
    logStartupEvent('native-worker', 'NATIVE_CONTEXT_FAILED', serializeSafeError(error));
    if (finished || cancelRequested) return;
    const failureMessage = safeMessage(error?.message, 'GameApp 预加载失败。');
    post({
      type: 'command-result', command: 'start-session', ok: false, code: error?.code || 3,
      fatal: true, message: failureMessage
    });
    requestTerminalFailure(failureMessage);
  }
}

async function waitForNativeState(expectedState, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (!cancelRequested && Date.now() < deadline) {
    const native = readNativeStatus();
    if (native.state === expectedState) return native;
    if (native.state === 7 || native.state === 8) break;
    await new Promise((resolveWait) => setTimeout(resolveWait, 50));
  }
  throw new Error(readNativeError() || '原生 Hook 会话未接受登录结果。');
}

async function authenticate(rawPayload) {
  let credentials;
  let authResult;
  let gameKeyHex = '';
  let authApplied = false;
  let attemptAbortController = null;
  let attemptToken = null;
  let ownsLoginAttempt = false;
  let context = null;
  try {
    if (machine.phase !== 'preloaded' || loginAbortController) {
      throw new PrivateLoginError('INVALID_STATE', '当前状态不能登录，请等待预加载完成。');
    }
    attemptToken = loginAttemptGuard.acquire();
    if (!attemptToken) {
      throw new PrivateLoginError('LOGIN_BUSY', '账号登录正在处理中，请稍候。');
    }
    ownsLoginAttempt = true;
    attemptAbortController = new AbortController();
    loginAbortController = attemptAbortController;
    if (!credentialOwner) {
      throw new CredentialVaultError('INVALID_STATE', '密码保险库尚未初始化。');
    }
    if (deferredNativePreload) {
      context = nativeContext;
      if (!context?.bundleBytes || !context?.metadata || !context?.vault || !context?.identity) {
        throw new PrivateLoginError('INVALID_STATE', '登录后启动上下文尚未就绪。');
      }
    } else {
      const preloadedNative = readNativeStatus();
      if (!mappedBridge || !bindings || preloadedNative.state !== 2 ||
          !Number.isSafeInteger(preloadedNative.gameProcessId) || preloadedNative.gameProcessId <= 0) {
        throw new PrivateLoginError('INVALID_STATE', 'GameApp 预加载会话尚未就绪。');
      }
      if (preloadedNative.gameProcessId !== preloadedGameProcessId) {
        throw new PrivateLoginError('INVALID_STATE', 'GameApp 预加载进程身份已变化。');
      }
    }

    credentials = await credentialOwner.resolve(rawPayload);
    if (cancelRequested || attemptAbortController.signal.aborted) {
      throw new PrivateLoginError('ABORTED', '登录已取消。');
    }
    transientSecrets.trackPassword(credentials.password);
    transition('login');
    authResult = await loginPrivateServer({
      host: LOCKED_LOGIN_ENDPOINT.host,
      port: LOCKED_LOGIN_ENDPOINT.port,
      username: credentials.username,
      password: credentials.password,
      passwordMode: LOCKED_LOGIN_ENDPOINT.passwordMode,
      timeoutMs: 30_000,
      signal: attemptAbortController.signal,
      runtimeMaterial: deferredNativePreload ? {
        required: true,
        algorithm: context.identity.algorithm,
        bundleId: context.metadata.bundleId,
        publicKey: context.identity.publicKey,
        fingerprint: context.identity.fingerprint,
        decrypt: (ciphertext) => context.vault.decrypt(ciphertext)
      } : undefined
    });
    if (cancelRequested) throw new PrivateLoginError('ABORTED', '登录已取消。');

    if (deferredNativePreload) {
      const preloaded = await startNativeGameFromMaterial(context, authResult?.runtimeMaterial);
      requirePreloadedGameProcess(preloaded);
      preloadedGameProcessId = preloaded.gameProcessId;
      deferredNativePreload = false;
      cleanupNativeContext();
      context = null;
      logStartupEvent('native-worker', 'GAMEAPP_PRELOADED_AFTER_LOGIN', {
        gameProcessId: preloadedGameProcessId
      });
    }

    const preloadedNative = readNativeStatus();
    if (!mappedBridge || !bindings || preloadedNative.state !== 2 ||
        preloadedNative.gameProcessId !== preloadedGameProcessId) {
      throw new PrivateLoginError('INVALID_STATE', 'GameApp 预加载会话尚未就绪。');
    }

    gameKeyHex = authResult.gameKey.toString('hex');
    transientSecrets.track(authResult.signKey, gameKeyHex);
    const code = Number(bindings.applyAuth(authResult.uin, gameKeyHex, authResult.signKey));
    if (code !== 0) {
      throw new Error(readNativeError() || '原生 Hook 会话拒绝登录结果。');
    }
    authApplied = true;
    const authenticatedNative = await waitForNativeState(4, NATIVE_AUTH_WAIT_MS);
    if (authenticatedNative.gameProcessId !== preloadedGameProcessId) {
      throw new Error('登录认证后的 GameApp PID 与预加载进程不一致。');
    }
    if (rawPayload?.persistCredential !== false) {
      const credentialOutcome = await credentialOwner.commitAuthenticated(credentials);
      post({
        type: 'credential-state',
        state: credentialOutcome.state,
        warning: credentialOutcome.warning
      });
    }
    transition('auth-succeeded');
    post({ type: 'command-result', command: 'login', ok: true, code: 0, fatal: false, message: '' });
  } catch (error) {
    if (cancelRequested || error?.code === 'ABORTED' || machine.phase === 'cancelled') {
      post({
        type: 'command-result', command: 'login', ok: false, code: 'ABORTED',
        fatal: false, recoverable: false, message: '登录已取消。'
      });
      return;
    }
    const nativeStillPreloaded = readNativeStatus().state === 2;
    const deferredLoginCanRetry = deferredNativePreload && !mappedBridge &&
      Boolean(nativeContext?.bundleBytes) && Boolean(nativeContext?.metadata);
    const recoverable = (error instanceof PrivateLoginError || error instanceof CredentialVaultError) &&
      !cancelRequested && !authApplied &&
      ((nativeStillPreloaded && Boolean(mappedBridge)) || deferredLoginCanRetry);
    if (ownsLoginAttempt && recoverable && machine.phase === 'authenticating') {
      transition('auth-rejected', { message: safeMessage(error.message, '登录失败，请重试。') });
    }
    post({
      type: 'command-result',
      command: 'login',
      ok: false,
      code: error?.code || 1,
      fatal: !recoverable,
      recoverable,
      message: safeMessage(error?.message, recoverable ? '登录失败，请重试。' : '登录会话失败。')
    });
    if (!recoverable && !finished) {
      requestTerminalFailure(safeMessage(error?.message, '登录会话失败。'));
    }
  } finally {
    authResult?.gameKey?.fill(0);
    wipeBuffer(authResult?.runtimeMaterial?.contentKey);
    wipeBuffer(authResult?.runtimeMaterial?.bundleId);
    wipeBuffer(authResult?.runtimeMaterial?.tokenId);
    if (authResult) authResult.signKey = '';
    gameKeyHex = '';
    authResult = null;
    if (credentials) {
      credentials.username = '';
      credentials.password = '';
    }
    if (rawPayload && typeof rawPayload === 'object') {
      rawPayload.username = '';
      rawPayload.password = '';
      rawPayload.remember = false;
      rawPayload.autoLogin = false;
      rawPayload.persistCredential = false;
    }
    if (ownsLoginAttempt) {
      if (loginAttemptGuard.release(attemptToken)) {
        if (loginAbortController === attemptAbortController) loginAbortController = null;
        transientSecrets.clear();
      }
    }
  }
}

function requestTerminalFailure(message) {
  if (finished) return;
  const terminal = {
    ok: false,
    code: 3,
    phase: 'failed',
    message: safeMessage(message, '启动会话失败。')
  };
  pendingTerminalFailure = terminal;
  cancelRequested = true;
  preloadAbortController?.abort();
  loginAbortController?.abort();
  if (!['cancelled', 'failed', 'ready'].includes(machine.phase)) {
    try { transition('native-failed', { message: terminal.message }); } catch { /* terminal callback won */ }
  }
  if (nativeSessionActive) {
    try { bindings?.cancel(); } catch { /* callback/utility exit remains authoritative */ }
    return;
  }
  pendingTerminalFailure = null;
  finishSession(terminal);
}

async function forgetCredential(rawPayload) {
  try {
    if (!credentialOwner) {
      throw new CredentialVaultError('INVALID_STATE', '密码保险库尚未初始化。');
    }
    await credentialOwner.forget(rawPayload?.username);
    post({
      type: 'command-result',
      command: 'forget-credential',
      ok: true,
      code: 0,
      fatal: false,
      message: ''
    });
  } catch (error) {
    post({
      type: 'command-result',
      command: 'forget-credential',
      ok: false,
      code: error?.code || 'CREDENTIAL_CLEANUP_FAILED',
      fatal: false,
      recoverable: true,
      message: '密码保险库清理失败。'
    });
  } finally {
    if (rawPayload && typeof rawPayload === 'object') {
      rawPayload.username = '';
    }
  }
}

function startGame() {
  try {
    if (machine.phase !== 'authenticated') {
      throw new Error('请先完成账号登录。');
    }
    const native = readNativeStatus();
    if (native.gameProcessId !== preloadedGameProcessId || preloadedGameProcessId <= 0) {
      throw new Error('GameApp 预加载进程身份已变化。');
    }
    transition('start');
    const code = Number(bindings?.startGame());
    if (code !== 0) throw new Error(readNativeError() || '提交开始游戏命令失败。');
    post({ type: 'command-result', command: 'start-game', ok: true, code: 0, fatal: false, message: '' });
  } catch (error) {
    post({ type: 'command-result', command: 'start-game', ok: false, code: 1, fatal: true, message: safeMessage(error?.message) });
    cancelSession();
  }
}

function cancelSession() {
  if (finished) return;
  cancelRequested = true;
  preloadAbortController?.abort();
  loginAbortController?.abort();
  if (!['cancelled', 'failed', 'ready'].includes(machine.phase)) {
    try { transition('cancel'); } catch { /* terminal callback won */ }
  }
  try {
    bindings?.cancel();
  } catch {
    // Parent shutdown remains best effort; Electron reaps the utility process.
  }
  if (!nativeSessionActive) {
    finishSession({ ok: false, code: 4, phase: 'cancelled', message: '启动会话已取消。' });
  }
}

if (!parentPort) {
  logStartupEvent('native-worker', 'NATIVE_WORKER_ERROR', { message: 'Electron utility parent port is unavailable.' });
  process.exitCode = 1;
} else {
  parentPort.on('message', (event) => {
    const message = event?.data;
    if (message?.type === 'start-session') void initializeSession(message.payload);
    else if (message?.type === 'login') void authenticate(message.payload);
    else if (message?.type === 'forget-credential') void forgetCredential(message.payload);
    else if (message?.type === 'start-game') startGame();
    else if (message?.type === 'cancel') cancelSession();
  });

  const failWorker = (error) => {
    logStartupEvent('native-worker', 'NATIVE_WORKER_ERROR', serializeSafeError(error));
    requestTerminalFailure('启动工作进程异常退出。');
  };
  process.on('uncaughtException', failWorker);
  process.on('unhandledRejection', failWorker);
  process.on('exit', () => {
    try { cleanupMappedBridge(); } catch { /* process teardown reclaims the image */ }
    cleanupNativeContext();
  });
}
