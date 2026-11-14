import { existsSync } from 'node:fs';
import { isAbsolute, resolve } from 'node:path';
import koffi from 'koffi';

export const RUNTIME_RESOURCE_ID = 301;

const RT_RCDATA = 10;
const LANG_NEUTRAL = 0;
const LOAD_LIBRARY_AS_DATAFILE = 0x00000002;
const LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x00000020;
const MAX_RESOURCE_BYTES = 256 * 1024 * 1024;
let bindings;

export class ExecutableRuntimeResourceError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'ExecutableRuntimeResourceError';
    this.code = code;
  }
}

function getBindings() {
  if (bindings) return bindings;
  if (process.platform !== 'win32') {
    throw new ExecutableRuntimeResourceError('RESOURCE_UNAVAILABLE', 'Windows executable resources are unavailable.');
  }
  const kernel32 = koffi.load('kernel32.dll');
  bindings = {
    kernel32,
    loadLibraryEx: kernel32.func('__stdcall', 'LoadLibraryExW', 'void *', ['str16', 'void *', 'uint32']),
    findResourceEx: kernel32.func('__stdcall', 'FindResourceExW', 'void *', [
      'void *', 'uintptr', 'uintptr', 'uint16'
    ]),
    sizeOfResource: kernel32.func('__stdcall', 'SizeofResource', 'uint32', ['void *', 'void *']),
    loadResource: kernel32.func('__stdcall', 'LoadResource', 'void *', ['void *', 'void *']),
    lockResource: kernel32.func('__stdcall', 'LockResource', 'void *', ['void *']),
    freeLibrary: kernel32.func('__stdcall', 'FreeLibrary', 'bool', ['void *']),
    getLastError: kernel32.func('__stdcall', 'GetLastError', 'uint32', [])
  };
  return bindings;
}

function fail(api, code, action) {
  throw new ExecutableRuntimeResourceError(
    code,
    `${action} failed for executable resource ${RUNTIME_RESOURCE_ID} (Windows error ${Number(api.getLastError()) || 0}).`
  );
}

export function readEmbeddedRuntimeBundle(executablePath = process.execPath) {
  const file = resolve(String(executablePath || ''));
  if (!executablePath || !isAbsolute(String(executablePath)) || !existsSync(file)) {
    throw new ExecutableRuntimeResourceError('RESOURCE_INVALID_PATH', 'Executable resource path is invalid.');
  }
  const api = getBindings();
  const module = api.loadLibraryEx(
    file,
    null,
    LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE
  );
  if (!module) fail(api, 'RESOURCE_LOAD_FAILED', 'LoadLibraryExW');
  try {
    const resource = api.findResourceEx(module, RT_RCDATA, RUNTIME_RESOURCE_ID, LANG_NEUTRAL);
    if (!resource) fail(api, 'RESOURCE_MISSING', 'FindResourceExW');
    const size = Number(api.sizeOfResource(module, resource)) || 0;
    if (size <= 0 || size > MAX_RESOURCE_BYTES) {
      throw new ExecutableRuntimeResourceError('RESOURCE_INVALID_SIZE', `Executable resource ${RUNTIME_RESOURCE_ID} size is invalid.`);
    }
    const loaded = api.loadResource(module, resource);
    if (!loaded) fail(api, 'RESOURCE_LOAD_FAILED', 'LoadResource');
    const locked = api.lockResource(loaded);
    if (!locked) fail(api, 'RESOURCE_LOAD_FAILED', 'LockResource');
    return Buffer.from(koffi.decode(locked, 'uint8', size));
  } finally {
    api.freeLibrary(module);
  }
}
