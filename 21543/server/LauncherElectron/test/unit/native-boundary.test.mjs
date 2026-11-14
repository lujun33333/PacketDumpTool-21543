import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { access, readFile } from 'node:fs/promises';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = new URL('../../', import.meta.url);
const require = createRequire(import.meta.url);

async function source(relativePath) {
  return readFile(new URL(relativePath, root), 'utf8');
}

test('NativeBridge production build excludes private login and crypto implementation', async () => {
  const project = await source('native/NativeBridge/NativeBridge.vcxproj');
  const nativeBuild = await source('src/scripts/build-native.cjs');
  const verifier = require(fileURLToPath(new URL('src/scripts/build-native.cjs', root)));
  assert.ok((project.match(/\/Brepro/g) || []).length >= 2, 'compiler and linker must emit reproducible PE bytes');
  assert.match(nativeBuild, /EXPECTED_HOOK_SHA256\s*=\s*['"][0-9a-f]{64}['"]/);
  assert.match(nativeBuild, /Hook DLL SHA256[^\n]*fixed|fixed[^\n]*Hook DLL SHA256/i);
  assert.doesNotThrow(() => verifier.verifyPinnedHookIdentity());
  for (const legacyFile of [
    'PrivateLogin.cpp',
    'PrivateProtocol.h',
    'QqTea.c',
    'QqTea.h',
    'MD5.h',
    'LockedEndpoint.h'
  ]) {
    assert.doesNotMatch(project, new RegExp(legacyFile.replace('.', '\\.')));
    await assert.rejects(access(new URL(`native/NativeBridge/${legacyFile}`, root)), { code: 'ENOENT' });
  }
});

test('NativeBridge accepts Hook bytes and remote-maps them without custom DLL materialization', async () => {
  const project = await source('native/NativeBridge/NativeBridge.vcxproj');
  const implementation = await source('native/NativeBridge/NativeBridge.cpp');
  const payloadImplementation = await source('native/NativeBridge/NativeBridgeHookPayload.cpp');
  const mapperImplementation = await source('native/NativeBridge/NativeBridgeManualMap.cpp');
  const processImplementation = await source('native/NativeBridge/NativeBridgeProcess.cpp');

  assert.match(project, /NativeBridgeHookPayload\.cpp/);
  assert.match(project, /NativeBridgeManualMap\.cpp/);
  assert.match(project, /NativeBridgeRemoteStub\.asm/);
  assert.doesNotMatch(project, /HookPayload\.rc|resource\.h|ResourceCompile/);
  await assert.rejects(access(new URL('native/NativeBridge/HookPayload.rc', root)), { code: 'ENOENT' });
  await assert.rejects(access(new URL('native/NativeBridge/resource.h', root)), { code: 'ENOENT' });
  assert.match(payloadImplementation, /QQSpeedNativeBridge_SetHookPayload/);
  assert.match(payloadImplementation, /SecureZeroMemory\(gHookPayloadBytes/);
  assert.match(implementation, /ClearHookPayload\(\)/);
  assert.ok((processImplementation.match(/ManualMapHookImage\(/g) || []).length >= 2);
  assert.match(mapperImplementation, /VirtualAllocEx/);
  assert.match(mapperImplementation, /WriteProcessMemory/);
  assert.match(mapperImplementation, /RtlAddFunctionTable/);
  assert.doesNotMatch(
    `${implementation}\n${payloadImplementation}\n${processImplementation}`,
    /CreateFileW|WriteFile|ReadFile|FindResourceExW|LoadResource|SizeofResource|LockResource/
  );

  const terminateStart = processImplementation.indexOf('void TerminateGameProcess()');
  const terminateEnd = processImplementation.indexOf('\nBOOL LogGameProcessState', terminateStart);
  const terminate = processImplementation.slice(terminateStart, terminateEnd);
  assert.ok(terminateStart >= 0 && terminateEnd > terminateStart);
  assert.match(terminate, /TerminateProcess\(hGameProcess/);
  assert.match(terminate, /WaitForSingleObject\(hGameProcess/);
  assert.ok(terminate.indexOf('WaitForSingleObject(hGameProcess') < terminate.indexOf('CloseGameProcessHandle()'));

  const beginExport = implementation.slice(
    implementation.indexOf('extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_BeginSession'),
    implementation.indexOf('\nstatic int DecodeHexNibble')
  );
  assert.match(beginExport, /__except[\s\S]*?TerminateGameProcess\(\)[\s\S]*?Cleanup\(\)/);
});

test('NativeBridge C++ boundary accepts auth material but never credentials or sockets', async () => {
  const productionFiles = [
    'native/NativeBridge/NativeBridge.cpp',
    'native/NativeBridge/NativeBridge.h',
    'native/NativeBridge/NativeBridgeIpc.cpp',
    'native/NativeBridge/NativeBridgeIpc.h',
    'native/NativeBridge/NativeBridgeProcess.cpp',
    'native/NativeBridge/NativeBridgeProcess.h',
    'native/NativeBridge/NativeBridgeState.h'
  ];
  const combined = (await Promise.all(productionFiles.map(source))).join('\n');

  for (const forbidden of [
    'SubmitCredentials',
    'LoginWithCredentials',
    'OnAsyncSocket',
    'gElectronNativePassword',
    'PrivateLogin.cpp',
    'QqTea',
    'MD5.h',
    'LockedEndpoint.h',
    'WSAStartup',
    'socket(',
    'connect('
  ]) {
    assert.equal(combined.includes(forbidden), false, `native boundary still contains ${forbidden}`);
  }
});

test('NativeBridge source has no unreachable executable-mode or debug-only residue', async () => {
  const productionFiles = [
    'native/NativeBridge/NativeBridge.cpp',
    'native/NativeBridge/NativeBridge.h',
    'native/NativeBridge/NativeBridgeState.h',
    'native/NativeBridge/NativeBridgeProcess.cpp',
    'native/NativeBridge/NativeBridgeProcess.h'
  ];
  const combined = (await Promise.all(productionFiles.map(source))).join('\n');

  for (const forbidden of [
    'IsElectronNativeMode',
    'IsQQSpeedNativeBridgeDllMode',
    'RegGetValue',
    'LauncherMessageBox',
    'MessageBoxW',
    'MessageBoxA',
    'EnsureBundledGameFiles',
    'resourceTypedb',
    'ExportRes',
    'wchar_t* Msg',
    'AToW(',
    'kDisableHookInjectionForTest',
    'GameProcessMonitorContext',
    'StartGameProcessMonitor',
    'StopGameProcessMonitor',
    '<wincrypt.h>',
    '<Psapi.h>',
    'application entry point'
  ]) {
    assert.equal(combined.includes(forbidden), false, `retired native residue remains: ${forbidden}`);
  }
});

test('NativeBridge keeps ABI offsets fixed and clears shared auth bytes during cleanup', async () => {
  const header = await source('native/NativeBridge/NativeBridge.h');
  const implementation = await source('native/NativeBridge/NativeBridge.cpp');

  for (const assertion of [
    /offsetof\(SharedData, Uin\)\s*==\s*0x08/,
    /offsetof\(SharedData, PwdMD5\)\s*==\s*0x0C/,
    /offsetof\(SharedData, Key\)\s*==\s*0x1C0/,
    /offsetof\(SharedData, SignKey\)\s*==\s*0x1D4/,
    /sizeof\(NotifyLoginResult\)\s*==\s*0x74/,
    /offsetof\(NotifyLoginResult, ErrorMessage\)\s*==\s*0x10/
  ]) {
    assert.match(header, assertion);
  }

  const cleanup = implementation.match(/void Cleanup\(\)\s*\{[\s\S]*?LauncherLog\("Cleanup finish"\);\s*\}/)?.[0] || '';
  assert.match(cleanup, /SecureZeroMemory\(pMapData,\s*kSharedDataMapSize\)/);
  assert.ok(
    cleanup.indexOf('SecureZeroMemory(pMapData') < cleanup.indexOf('UnmapViewOfFile(pMapData)'),
    'shared auth bytes must be cleared immediately before the cleanup unmap'
  );
  assert.match(implementation, /Status text update len=%zu/);
  assert.doesNotMatch(implementation, /Status text update[^\n]*%\.\*s/);
});

test('native verifier rejects forbidden networking and crypto import modules', async () => {
  const verifier = require(fileURLToPath(new URL('src/scripts/build-native.cjs', root)));
  assert.equal(typeof verifier.inspectPeImports, 'function');
  assert.equal(typeof verifier.assertNativeImportPolicy, 'function');
  assert.ok(verifier.FORBIDDEN_NATIVE_IMPORT_MODULES instanceof Set);

  assert.throws(
    () => verifier.assertNativeImportPolicy(new Map([['WS2_32.dll', new Set(['connect'])]])),
    /WS2_32\.dll/
  );
  assert.throws(
    () => verifier.assertNativeImportPolicy(new Map([['ADVAPI32.dll', new Set(['CryptHashData'])]])),
    /CryptHashData/
  );
  assert.doesNotThrow(
    () => verifier.assertNativeImportPolicy(new Map([['USER32.dll', new Set(['SendMessageA'])]]))
  );

  assert.doesNotThrow(
    () => verifier.assertNativeImportPolicy(new Map([['KERNEL32.dll', new Set(['VirtualAllocEx'])]]))
  );
});

test('native verifier pins the VMProtect Hook as a separate in-memory bundle entry', async () => {
  const verifier = require(fileURLToPath(new URL('src/scripts/build-native.cjs', root)));
  const builder = await source('src/scripts/build-runtime-bundle.cjs');
  assert.equal(typeof verifier.verifyPinnedHookIdentity, 'function');
  assert.doesNotThrow(() => verifier.verifyPinnedHookIdentity());
  assert.match(builder, /bridgeBytes/);
  assert.match(builder, /hookBytes/);
  assert.doesNotMatch(builder, /RCDATA|inspectPeResourceById|verifyEmbeddedHook/);
});

test('NativeBridge exposes the stable JS utility-process ABI', async () => {
  const header = await source('native/NativeBridge/NativeBridge.h');
  const implementation = await source('native/NativeBridge/NativeBridge.cpp');
  const payloadImplementation = await source('native/NativeBridge/NativeBridgeHookPayload.cpp');
  const implementations = `${implementation}\n${payloadImplementation}`;
  const expectedExports = [
    'QQSpeedNativeBridge_BeginSession',
    'QQSpeedNativeBridge_SetHookPayload',
    'QQSpeedNativeBridge_ApplyAuth',
    'QQSpeedNativeBridge_StartGame',
    'QQSpeedNativeBridge_Cancel',
    'QQSpeedNativeBridge_GetState',
    'QQSpeedNativeBridge_GetProgress',
    'QQSpeedNativeBridge_IsBusy',
    'QQSpeedNativeBridge_GetGameProcessId',
    'QQSpeedNativeBridge_GetLastError'
  ];

  for (const name of expectedExports) {
    assert.match(header, new RegExp(`\\b${name}\\b`));
    assert.match(implementations, new RegExp(`\\b${name}\\b`));
  }

  assert.match(
    header,
    /QQSpeedNativeBridge_ApplyAuth\s*\(\s*uint32_t\s+uin\s*,\s*const char\*\s+gameKeyHex\s*,\s*const char\*\s+signKey\s*\)/s
  );
  assert.doesNotMatch(implementation, /sign-key digest|signKeyDigest|SignKeyDigest/);
  assert.match(implementation, /signKeyLength\s*>\s*64/);
});

test('NativeBridge uses the current GameApp LoadingGameUI resources and authoritative progress only', async () => {
  const implementation = await source('native/NativeBridge/NativeBridge.cpp');
  const processImplementation = await source('native/NativeBridge/NativeBridgeProcess.cpp');
  const stateHeader = await source('native/NativeBridge/NativeBridgeState.h');
  const nativeCore = await source('src/main/native-core.js');

  for (const retired of [
    'UTF8ToW',
    'WToUTF8',
    'WToA',
    'myprintf',
    'hLoaderCloseNormal'
  ]) {
    assert.equal(
      `${implementation}\n${stateHeader}\n${nativeCore}`.includes(retired),
      false,
      `retired native loader residue remains: ${retired}`
    );
  }

  for (const requiredBitmap of ['LoaderBG.bmp', 'Progress_Back.bmp', 'Progress_Fore.bmp', 'cancel_normal.bmp']) {
    assert.match(implementation, new RegExp(requiredBitmap.replace('.', '\\.')));
    assert.match(nativeCore, new RegExp(requiredBitmap.replace('.', '\\.')));
  }
  assert.match(implementation, /LoadingGameUI/);
  assert.match(nativeCore, /LoadingGameUI/);
  assert.doesNotMatch(nativeCore, /join\('Loader'/);
  assert.doesNotMatch(processImplementation, /elapsed\s*\/\s*timeoutMs/);
  assert.doesNotMatch(processImplementation, /for\s*\(int\s+p\s*=\s*progress\s*\+\s*1/);
  assert.doesNotMatch(implementation, /SetLoaderProgress\((?:12|35|42|58|64|75)\)/);
});
