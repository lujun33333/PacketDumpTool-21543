const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..', '..');
const read = (relativePath) => fs.readFileSync(path.join(root, relativePath), 'utf8');
const assert = (condition, message) => { if (!condition) throw new Error(message); };

const nativeDir = path.join(root, 'native', 'NativeBridge');
const project = read('native/NativeBridge/NativeBridge.vcxproj');
const bridge = read('native/NativeBridge/NativeBridge.cpp');
const bridgeHeader = read('native/NativeBridge/NativeBridge.h');
const processSource = read('native/NativeBridge/NativeBridgeProcess.cpp');
const worker = read('src/main/native-worker.js');

assert(project.includes('<ConfigurationType>DynamicLibrary</ConfigurationType>'), 'Native bridge is not DLL-only.');
assert(project.includes('<TargetName>QQSpeedNativeBridge</TargetName>'), 'Native bridge output name is not locked.');
assert((project.match(/\/Brepro/g) || []).length >= 2, 'Native compiler/linker reproducible-build flags are missing.');
assert((project.match(/<ResourceCompile\b/g) || []).length === 0 &&
       !project.includes('HookPayload.rc') && !project.includes('Launcher.rc'),
  'Native bridge must receive Hook bytes in memory and compile no embedded payload resource.');

for (const legacyFile of [
  'PrivateLogin.cpp', 'PrivateProtocol.h', 'QqTea.c', 'QqTea.h', 'MD5.h', 'LockedEndpoint.h'
]) {
  assert(!project.includes(legacyFile), `Native project still compiles ${legacyFile}.`);
  assert(!fs.existsSync(path.join(nativeDir, legacyFile)), `Retired native source still exists: ${legacyFile}.`);
}

for (const exportName of [
  'QQSpeedNativeBridge_BeginSession', 'QQSpeedNativeBridge_ApplyAuth',
  'QQSpeedNativeBridge_StartGame', 'QQSpeedNativeBridge_Cancel',
  'QQSpeedNativeBridge_GetState', 'QQSpeedNativeBridge_GetProgress',
  'QQSpeedNativeBridge_IsBusy', 'QQSpeedNativeBridge_GetGameProcessId',
  'QQSpeedNativeBridge_GetLastError'
]) {
  assert(bridge.includes(exportName), `Missing native bridge export source: ${exportName}`);
}
for (const forbidden of ['QQSpeedNativeBridge_RunSession', 'QQSpeedNativeBridge_SubmitCredentials', 'WSAStartup', 'socket(', 'connect(']) {
  assert(!bridge.includes(forbidden), `Native bridge still owns forbidden login/network behavior: ${forbidden}`);
}
for (const forbidden of [
  'IsElectronNativeMode', 'IsQQSpeedNativeBridgeDllMode', 'RegGetValue',
  'LauncherMessageBox', 'MessageBoxW', 'MessageBoxA', 'EnsureBundledGameFiles',
  'resourceTypedb', 'ExportRes', 'wchar_t* Msg', 'AToW(',
  'kDisableHookInjectionForTest', 'GameProcessMonitorContext',
  'StartGameProcessMonitor', 'StopGameProcessMonitor', 'application entry point'
]) {
  assert(!`${bridge}\n${bridgeHeader}\n${processSource}`.includes(forbidden),
    `Retired native residue remains: ${forbidden}`);
}

assert(!bridge.includes('wWinMain') && !bridge.includes('--electron-native'), 'Legacy native EXE entry remains.');
assert(!bridge.includes('HWND_MESSAGE') && bridge.includes('WS_POPUP'), 'GameApp hidden top-level protocol window is missing.');
assert(bridge.includes('Injected hook owns Network.dll compatibility'), 'Hook/Network ownership is unclear.');
assert(bridge.includes('Status text update len=%zu') && !/Status text update[^\n]*%\.\*s/.test(bridge),
  'GameApp status text is still written verbatim to the native debug log.');
assert(bridge.includes('SecureZeroMemory(pMapData, kSharedDataMapSize)') &&
       bridge.indexOf('SecureZeroMemory(pMapData, kSharedDataMapSize)') < bridge.indexOf('UnmapViewOfFile(pMapData)'),
  'KartMap shared auth material is not cleared before cleanup unmap.');
for (const requiredLayout of [
  'offsetof(SharedData, Uin) == 0x08',
  'offsetof(SharedData, PwdMD5) == 0x0C',
  'offsetof(SharedData, Key) == 0x1C0',
  'offsetof(SharedData, SignKey) == 0x1D4',
  'sizeof(NotifyLoginResult) == 0x74',
  'offsetof(NotifyLoginResult, ErrorMessage) == 0x10'
]) {
  assert(bridgeHeader.includes(requiredLayout), `Missing native ABI layout assertion: ${requiredLayout}`);
}
assert(!processSource.includes('WriteProcessMemoryInTargetProcess') && !processSource.includes('SupportedNetworkPatch'),
  'Legacy Network.dll patcher remains.');

const preloadLaunch = bridge.indexOf('LaunchGameProcess()');
const authGate = bridge.indexOf('WaitForSessionGate(gQQSpeedNativeBridgeAuthEvent');
const startGate = bridge.indexOf('WaitForSessionGate(gQQSpeedNativeBridgeStartEvent');
const release = bridge.indexOf('SetEvent(hRunSpeedGameEvent)');
assert(preloadLaunch >= 0 && authGate > preloadLaunch, 'GameApp must preload before auth handoff.');
assert(startGate > authGate && release > startGate, 'GameApp release must follow auth and explicit start.');

assert(worker.includes("'beginSession'") && worker.includes("'applyAuth'") && worker.includes("'setHookPayload'"),
  'Utility worker is not bound to the minimal native ABI.');
assert(worker.includes('loginPrivateServer') && worker.includes('LauncherSessionStateMachine'),
  'Private login/state orchestration is not owned by JS utility process.');
assert(worker.includes('passwordMode: LOCKED_LOGIN_ENDPOINT.passwordMode') &&
       /applyAuth\(authResult\.uin,\s*gameKeyHex,\s*authResult\.signKey\)/.test(worker),
  'Utility worker does not preserve the configured wire-password/sign-key contract.');
assert(worker.includes('TransientSecretRedactor') && worker.includes('serializeSafeError'),
  'Utility worker can forward transient authentication material through error text.');
assert(worker.includes('ownsLoginAttempt') && worker.includes('if (ownsLoginAttempt)') &&
       worker.includes('loginAbortController === attemptAbortController'),
  'A duplicate login command can clear another attempt\'s state or transient secrets.');

const { verifyRuntimeBundle } = require('./build-runtime-bundle.cjs');

verifyRuntimeBundle({
  output: path.join(root, 'resources', 'native', 'qqspeed-runtime.bundle'),
  hookInput: path.join(root, 'resources', 'native', 'qqspeed_clean_hook_64.dll')
}).then((inspected) => {
  assert(inspected.authenticated === false && inspected.bridgeSize > 0,
    'Native runtime bundle descriptor validation failed.');
  assert(!fs.existsSync(path.join(root, 'resources', 'native', 'QQSpeedNativeBridge.dll')),
    'Native boundary verification must consume the bundle, not a loose source Bridge.');
  console.log('smoke-native-boundary: PASS');
}).catch((error) => {
  console.error(error?.stack || error);
  process.exitCode = 1;
});
