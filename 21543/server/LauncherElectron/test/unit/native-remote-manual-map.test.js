import assert from 'node:assert/strict';
import { readFileSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const native = join(root, 'native', 'NativeBridge');
const read = (name) => readFileSync(join(native, name), 'utf8');

test('NativeBridge accepts a copied in-memory Hook payload and securely releases it', () => {
  const source = read('NativeBridgeHookPayload.cpp');
  const header = read('NativeBridgeHookPayload.h');

  assert.match(header, /QQSpeedNativeBridge_SetHookPayload/);
  assert.match(source, /HeapAlloc\s*\(/);
  assert.match(source, /memcpy\s*\(/);
  assert.match(source, /SecureZeroMemory\s*\(/);
  assert.match(source, /HeapFree\s*\(/);
  assert.match(source, /IMAGE_FILE_MACHINE_AMD64/);
  assert.match(source, /IMAGE_NT_OPTIONAL_HDR64_MAGIC/);
  assert.match(source, /IMAGE_DIRECTORY_ENTRY_TLS/);
  assert.match(source, /tlsDirectory\.VirtualAddress\s*==\s*0\s*&&\s*tlsDirectory\.Size\s*==\s*0/);
});

test('NativeBridge accepts the staged protected Hook under the shared 128 MiB payload contract', () => {
  const source = read('NativeBridgeHookPayload.cpp');
  const limit = source.match(
    /kMaximumHookPayloadSize\s*=\s*(\d+)\s*\*\s*1024\s*\*\s*1024\s*;/
  );
  assert.ok(limit, 'NativeBridge payload limit must be explicit');
  assert.equal(Number(limit[1]), 128);

  const hookSize = statSync(
    join(root, 'resources', 'native', 'qqspeed_clean_hook_64.dll')
  ).size;
  assert.ok(hookSize <= Number(limit[1]) * 1024 * 1024);
});

test('remote Hook mapper covers the standard x64 PE load lifecycle without a Hook path', () => {
  const source = read('NativeBridgeManualMap.cpp');

  for (const token of [
    'IMAGE_DIRECTORY_ENTRY_IMPORT',
    'IMAGE_DIRECTORY_ENTRY_BASERELOC',
    'IMAGE_REL_BASED_DIR64',
    'IMAGE_DIRECTORY_ENTRY_TLS',
    'IMAGE_DIRECTORY_ENTRY_EXCEPTION',
    'RtlAddFunctionTable',
    'VirtualAllocEx',
    'WriteProcessMemory',
    'VirtualProtectEx',
    'FlushInstructionCache',
    'DLL_PROCESS_ATTACH'
  ]) {
    assert.match(source, new RegExp(token), token);
  }
  assert.doesNotMatch(source, /qqspeed_clean_hook_64\.dll/i);
  assert.doesNotMatch(source, /CreateFileW|WriteFile|PathFileExistsW/);
});

test('remote system procedures survive protected module enumeration without trusting an unverified ASLR address', () => {
  const source = read('NativeBridgeManualMap.cpp');

  assert.match(source, /RemoteImageMatchesLocalModule/);
  assert.match(source, /VirtualQueryEx\s*\(/);
  assert.match(source, /MEM_IMAGE/);
  assert.match(source, /TimeDateStamp/);
  assert.match(source, /SizeOfImage/);
  assert.match(source, /ResolveExistingRemoteProcedure\s*\(\s*HANDLE\s+process/);
  assert.match(source, /constexpr\s+DWORD\s+kRemoteLoaderReadyTimeoutMs\s*=\s*5000\s*;/);
  assert.match(source, /constexpr\s+DWORD\s+kRemoteLoaderPollIntervalMs\s*=\s*15\s*;/);
  assert.match(source, /WaitForRemoteLoaderReady\s*\(/);
  assert.match(source, /WaitForSingleObject\s*\(\s*process\s*,\s*0\s*\)/);
  assert.match(source, /ERROR_PARTIAL_COPY/);
  assert.match(source, /ERROR_BAD_LENGTH/);
  assert.match(source, /ntdll\.dll/i);
  assert.match(source, /kernel32\.dll/i);
  assert.match(source, /kernelbase\.dll/i);
  assert.doesNotMatch(source, /Sleep\s*\(\s*3000\s*\)/);

  const mapperStart = source.indexOf('BOOL ManualMapHookImage(');
  assert.ok(mapperStart >= 0, 'manual-map entry point must exist');
  const mapper = source.slice(mapperStart);
  const wait = mapper.indexOf('WaitForRemoteLoaderReady(');
  const resolve = mapper.indexOf('remoteLoadLibraryW = ResolveExistingRemoteProcedure(');
  assert.ok(wait >= 0 && resolve > wait, 'loader readiness must precede remote procedure resolution');
});

test('initial and replacement GameApp injection consume the same memory payload', () => {
  const processSource = read('NativeBridgeProcess.cpp');

  assert.doesNotMatch(processSource, /InjectDLLEx|LoadLibraryW|HookDllFile|PathFileExistsW/);
  assert.ok((processSource.match(/ManualMapHookImage\s*\(/g) || []).length >= 2);
  assert.match(processSource, /GetHookPayloadSnapshot/);
});

test('NativeBridge project no longer compiles an embedded Hook resource', () => {
  const project = read('NativeBridge.vcxproj');

  assert.doesNotMatch(project, /HookPayload\.rc|qqspeed_clean_hook_64\.dll/i);
  assert.match(project, /NativeBridgeManualMap\.cpp/);
  assert.match(project, /NativeBridgeHookPayload\.cpp/);
});
