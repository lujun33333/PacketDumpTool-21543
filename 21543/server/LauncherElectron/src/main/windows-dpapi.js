import koffi from 'koffi';

const CRYPTPROTECT_UI_FORBIDDEN = 0x1;
const MAX_DPAPI_BYTES = 1024 * 1024;
const DATA_BLOB = koffi.struct('QQSpeedRuntimeDataBlob', {
  cbData: 'uint32',
  pbData: 'void *'
});
const DATA_BLOB_PTR = koffi.pointer(DATA_BLOB);
let bindings;

export class WindowsDpapiError extends Error {
  constructor(code, message, options = {}) {
    super(message, options);
    this.name = 'WindowsDpapiError';
    this.code = code;
  }
}

function getBindings() {
  if (bindings) return bindings;
  if (process.platform !== 'win32') {
    throw new WindowsDpapiError('DPAPI_UNAVAILABLE', 'Windows DPAPI is unavailable.');
  }
  const crypt32 = koffi.load('crypt32.dll');
  const kernel32 = koffi.load('kernel32.dll');
  bindings = {
    crypt32,
    kernel32,
    protect: crypt32.func('__stdcall', 'CryptProtectData', 'bool', [
      DATA_BLOB_PTR,
      'str16',
      DATA_BLOB_PTR,
      'void *',
      'void *',
      'uint32',
      koffi.out(DATA_BLOB_PTR)
    ]),
    unprotect: crypt32.func('__stdcall', 'CryptUnprotectData', 'bool', [
      DATA_BLOB_PTR,
      'void *',
      DATA_BLOB_PTR,
      'void *',
      'void *',
      'uint32',
      koffi.out(DATA_BLOB_PTR)
    ]),
    localFree: kernel32.func('__stdcall', 'LocalFree', 'void *', ['void *']),
    getLastError: kernel32.func('__stdcall', 'GetLastError', 'uint32', [])
  };
  return bindings;
}

function copyInput(value) {
  if (!Buffer.isBuffer(value) && !(value instanceof Uint8Array)) {
    throw new WindowsDpapiError('DPAPI_INVALID_INPUT', 'DPAPI input must be binary data.');
  }
  const bytes = Buffer.from(value);
  if (bytes.length <= 0 || bytes.length > MAX_DPAPI_BYTES) {
    bytes.fill(0);
    throw new WindowsDpapiError('DPAPI_INVALID_INPUT', 'DPAPI input size is invalid.');
  }
  return bytes;
}

function callDpapi(operation, value, options = {}) {
  const api = getBindings();
  const input = copyInput(value);
  const inputBlob = { cbData: input.length, pbData: input };
  const outputBlob = {};
  const description = operation === 'protect' && typeof options.description === 'string'
    ? options.description.slice(0, 128)
    : null;
  try {
    const ok = operation === 'protect'
      ? api.protect(inputBlob, description, null, null, null, CRYPTPROTECT_UI_FORBIDDEN, outputBlob)
      : api.unprotect(inputBlob, null, null, null, null, CRYPTPROTECT_UI_FORBIDDEN, outputBlob);
    if (!ok) {
      const lastError = Number(api.getLastError()) || 0;
      throw new WindowsDpapiError(
        operation === 'protect' ? 'DPAPI_PROTECT_FAILED' : 'DPAPI_UNPROTECT_FAILED',
        `Windows DPAPI ${operation} failed (${lastError}).`
      );
    }
    const size = Number(outputBlob.cbData) || 0;
    if (!outputBlob.pbData || size <= 0 || size > MAX_DPAPI_BYTES) {
      throw new WindowsDpapiError('DPAPI_INVALID_OUTPUT', 'Windows DPAPI returned invalid output.');
    }
    return Buffer.from(koffi.decode(outputBlob.pbData, 'uint8', size));
  } finally {
    input.fill(0);
    if (outputBlob.pbData) {
      try {
        api.localFree(outputBlob.pbData);
      } catch {
        // The process is already tearing down; no usable plaintext is retained.
      }
    }
  }
}

export function protectBytesWithDpapi(value, options = {}) {
  return callDpapi('protect', value, options);
}

export function unprotectBytesWithDpapi(value) {
  return callDpapi('unprotect', value);
}
