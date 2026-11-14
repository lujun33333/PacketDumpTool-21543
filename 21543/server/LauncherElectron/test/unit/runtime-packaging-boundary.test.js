import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..', '..');
const read = (relative) => readFileSync(resolve(root, relative), 'utf8');

test('DirSvrInfo1.ini is absent from source runtime and packaging contracts', () => {
  const searchedFiles = [
    'package.json',
    'src/main/native-core.js',
    'src/scripts/build-native.cjs',
    'src/scripts/verify-hook.cjs',
    'src/scripts/smoke-runtime-artifacts.cjs'
  ];
  for (const file of searchedFiles) {
    assert.doesNotMatch(read(file), /DirSvrInfo1\.ini/, file);
  }
  assert.equal(existsSync(resolve(root, 'resources/native/DirSvrInfo1.ini')), false);
  assert.equal(existsSync(resolve(root, 'resources/native/runtime-artifacts.json')), false);
});

test('production renderer does not reference the captured entire-page screenshot', () => {
  for (const file of ['src/renderer/index.html', 'src/renderer/styles.css', 'src/renderer/app.js']) {
    assert.doesNotMatch(read(file), /home-bg-official-qqspeed\.png/, file);
  }
  assert.equal(existsSync(resolve(root, 'assets/home-bg-official-qqspeed.png')), false);
});

test('main window uses the approved responsive content geometry', () => {
  const main = read('src/main/index.js');
  assert.match(main, /const BASE_WINDOW_WIDTH = 1280;/);
  assert.match(main, /const BASE_WINDOW_HEIGHT = 720;/);
  assert.match(main, /const MIN_WINDOW_WIDTH = 1024;/);
  assert.match(main, /const MIN_WINDOW_HEIGHT = 640;/);
  assert.match(main, /width:\s*BASE_WINDOW_WIDTH/);
  assert.match(main, /height:\s*BASE_WINDOW_HEIGHT/);
  assert.match(main, /minWidth:\s*MIN_WINDOW_WIDTH/);
  assert.match(main, /minHeight:\s*MIN_WINDOW_HEIGHT/);
  assert.match(main, /resizable:\s*true/);
  assert.match(main, /maximizable:\s*true/);
  assert.doesNotMatch(main, /setZoomFactor|getWindowLayout|applyWindowLayout/);
});

test('content IPC separates pre-login configuration from authenticated home feeds', () => {
  const main = read('src/main/index.js');
  const preload = read('src/preload/preload.cjs');
  const service = read('src/main/launcher-content-service.js');
  assert.match(main, /getLauncherContent\(scope/);
  assert.match(main, /resolveAuthorizedLauncherContentScope\(\s*scope,\s*Boolean\(launcherAuthenticatedUser\)\s*\)/);
  assert.match(main, /getCachedLauncherContentRequest\(/);
  assert.match(main, /loadLauncherContent\(getSettingsDir\(\),\s*\{ scope: normalizedScope \}\)/);
  assert.match(main, /invalidateLauncherHomeContent\(launcherContentPromises\)/);
  assert.match(service, /throw new Error\('请先登录账号，再加载主页内容。'\)/);
  assert.match(preload, /getLauncherContent:\s*\(scope, options/);
});

test('main and worker acquire exclusive login ownership before dispatch or await', () => {
  const main = read('src/main/index.js');
  const worker = read('src/main/native-worker.js');
  const submitStart = main.indexOf('async function submitNativeCredentials');
  const submitEnd = main.indexOf('\nasync function ensureAuthenticatedNativeSession', submitStart);
  const submit = main.slice(submitStart, submitEnd);
  const authenticateStart = worker.indexOf('async function authenticate');
  const authenticateEnd = worker.indexOf('\nfunction startGame', authenticateStart);
  const authenticate = worker.slice(authenticateStart, authenticateEnd);

  assert.ok(submitStart >= 0 && submitEnd > submitStart);
  assert.ok(submit.indexOf('.loginAttemptGuard.acquire()') >= 0);
  assert.ok(submit.indexOf('.loginAttemptGuard.acquire()') < submit.indexOf('record.child.postMessage({'));
  assert.match(submit, /loginAttemptGuard\.release\(attemptToken\)/);

  assert.ok(authenticateStart >= 0 && authenticateEnd > authenticateStart);
  assert.ok(authenticate.indexOf('loginAttemptGuard.acquire()') >= 0);
  assert.ok(authenticate.indexOf('loginAttemptGuard.acquire()') < authenticate.indexOf('await credentialOwner.resolve'));
  assert.match(authenticate, /loginAttemptGuard\.release\(attemptToken\)[\s\S]*?transientSecrets\.clear\(\)/);
});

test('unremembered terminal sessions expire once while remembered accounts remain resumable', () => {
  const main = read('src/main/index.js');
  const policy = read('src/main/credential-policy.js');
  assert.match(policy, /function hasRememberedCredentialForUser/);
  assert.match(main, /function expireUnrememberedLauncherSession/);
  assert.match(main, /hasRememberedCredentialForUser\(state\.login,\s*authenticatedUser\)/);
  assert.match(main, /handleGameProcessExit[\s\S]*?setLaunchStatus\([\s\S]*?phase:\s*'game-exited'[\s\S]*?expireUnrememberedLauncherSession\(\)/);
  assert.match(main, /FAILED_SESSION_PHASES\.has\(launchStatus\.phase\)[\s\S]*?expireUnrememberedLauncherSession\(\)/);
  const expiryStart = main.indexOf('function expireUnrememberedLauncherSession');
  const expiryEnd = main.indexOf('\nfunction emitLaunchStatus', expiryStart);
  const expiry = main.slice(expiryStart, expiryEnd);
  assert.match(expiry, /return clearLauncherAuthenticatedSession\(\)/);
  const clearStart = main.indexOf('function clearLauncherAuthenticatedSession');
  const clearEnd = main.indexOf('\nfunction expireUnrememberedLauncherSession', clearStart);
  const clear = main.slice(clearStart, clearEnd);
  assert.equal((clear.match(/emitLauncherUserLogout\(\)/g) || []).length, 1);
  assert.ok(clear.indexOf("launcherAuthenticatedUser = ''") < clear.indexOf('emitLauncherUserLogout()'));
});

test('mock mall IPC is not exposed by main or preload', () => {
  assert.doesNotMatch(read('src/main/index.js'), /mall:get-state|mall:exchange|mock-db-adapter/);
  assert.doesNotMatch(read('src/preload/preload.cjs'), /getMallState|exchangeMallItem/);
});

test('only the official QQ scan affordance is restored; retired WeChat assets stay absent', () => {
  for (const file of [
    'assets/Launcher.ico',
    'assets/login-icon-wechat-active-L7b1HuYs.png',
    'assets/login-icon-wechat-default-7eLKI8-T.png'
  ]) {
    assert.equal(existsSync(resolve(root, file)), false, file);
  }
  assert.equal(existsSync(resolve(root, 'assets/login-icon-qrcode-DRdIde-Z.png')), true);
  assert.match(read('src/renderer/styles.css'), /login-icon-qrcode-DRdIde-Z\.png/);
  assert.match(read('src/renderer/app.js'), /当前暂不支持扫码登录/);
  assert.doesNotMatch(read('src/preload/preload.cjs'), /onLauncherStatus/);
});

test('switch account cancels the authenticated native session before another login', () => {
  const main = read('src/main/index.js');
  const preload = read('src/preload/preload.cjs');
  const renderer = read('src/renderer/app.js');

  assert.match(main, /ipcMain\.handle\('launcher:switch-account'/);
  assert.match(main, /async function switchNativeAccount\([\s\S]*?cancelNativeSession\(\)/);
  const switchStart = main.indexOf('async function switchNativeAccount');
  const switchEnd = main.indexOf('\nfunction createMainWindow', switchStart);
  const switchAccount = main.slice(switchStart, switchEnd);
  assert.ok(switchAccount.indexOf('await cancelNativeSession()') < switchAccount.indexOf('clearLauncherAuthenticatedSession'));
  assert.match(preload, /switchAccount:\s*\(\)\s*=>\s*ipcRenderer\.invoke\('launcher:switch-account'\)/);
  assert.match(renderer, /accountBtn[\s\S]*?api\.switchAccount\(\)/);
});

test('authoritative main-process session events drive login and logout routing', () => {
  const main = read('src/main/index.js');
  const preload = read('src/preload/preload.cjs');
  const renderer = read('src/renderer/app.js');

  assert.match(main, /function getPublicSession\(\)/);
  assert.match(main, /webContents\.send\('launcher:user-login'/);
  assert.match(main, /webContents\.send\('launcher:user-logout'/);
  assert.match(preload, /onUserLogin/);
  assert.match(preload, /onUserLogout/);
  assert.match(renderer, /api\.onUserLogin/);
  assert.match(renderer, /api\.onUserLogout/);
});

test('remembered reauthentication preserves worker error codes and expires only unrecoverable sessions once', () => {
  const main = read('src/main/index.js');
  assert.match(main, /error\.code\s*=\s*normalizeWorkerCommandCode\(message\.code\)/);
  assert.match(main, /shouldExpireAuthenticatedSessionAfterReauth\(error\)/);
  const ensureStart = main.indexOf('async function ensureAuthenticatedNativeSession');
  const ensureEnd = main.indexOf('\nasync function startNativeGame', ensureStart);
  const ensure = main.slice(ensureStart, ensureEnd);
  assert.match(ensure, /catch\s*\(error\)[\s\S]*?clearLauncherAuthenticatedSession\(\)/);
  assert.match(ensure, /throw error/);
});

test('metadata commit failure waits for utility vault cleanup without turning cleanup failure into native failure', () => {
  const main = read('src/main/index.js');
  const worker = read('src/main/native-worker.js');
  assert.match(main, /reconcileAuthenticatedCredentialPersistence\(/);
  assert.match(main, /type:\s*'forget-credential'/);
  assert.match(main, /await record\.credentialOutcome/);
  const commandStart = main.indexOf("if (message.type === 'command-result')");
  const commandEnd = main.indexOf("if (message.type !== 'result'", commandStart);
  const commandHandler = main.slice(commandStart, commandEnd);
  assert.match(commandHandler, /command === 'forget-credential'/);
  assert.ok(commandHandler.indexOf("command === 'forget-credential'") < commandHandler.indexOf("phase: 'failed'"));
  assert.match(worker, /async function forgetCredential/);
  assert.match(worker, /credentialOwner\.forget\(/);
  assert.match(worker, /message\?\.type === 'forget-credential'/);
});
