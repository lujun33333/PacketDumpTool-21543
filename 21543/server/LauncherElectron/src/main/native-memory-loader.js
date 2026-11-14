import { createHash, timingSafeEqual } from 'node:crypto';

import koffi from 'koffi';

const IMAGE_DOS_SIGNATURE = 0x5a4d;
const IMAGE_NT_SIGNATURE = 0x00004550;
const IMAGE_FILE_MACHINE_AMD64 = 0x8664;
const IMAGE_FILE_DLL = 0x2000;
const IMAGE_NT_OPTIONAL_HDR64_MAGIC = 0x20b;
const IMAGE_REL_BASED_ABSOLUTE = 0;
const IMAGE_REL_BASED_DIR64 = 10;
const IMAGE_ORDINAL_FLAG64 = 0x8000000000000000n;

const DIRECTORY_EXPORT = 0;
const DIRECTORY_IMPORT = 1;
const DIRECTORY_EXCEPTION = 3;
const DIRECTORY_BASERELOC = 5;
const DIRECTORY_TLS = 9;
const DIRECTORY_DELAY_IMPORT = 13;

const MEM_COMMIT = 0x1000;
const MEM_RESERVE = 0x2000;
const MEM_RELEASE = 0x8000;
const PAGE_NOACCESS = 0x01;
const PAGE_READONLY = 0x02;
const PAGE_READWRITE = 0x04;
const PAGE_EXECUTE = 0x10;
const PAGE_EXECUTE_READ = 0x20;
const PAGE_EXECUTE_READWRITE = 0x40;
const LOAD_LIBRARY_SEARCH_SYSTEM32 = 0x00000800;
const DLL_PROCESS_DETACH = 0;
const DLL_PROCESS_ATTACH = 1;

const SECTION_MEM_EXECUTE = 0x20000000;
const SECTION_MEM_READ = 0x40000000;
const SECTION_MEM_WRITE = 0x80000000;

const MAX_FILE_SIZE = 128 * 1024 * 1024;
const MAX_IMAGE_SIZE = 160 * 1024 * 1024;
const MAX_SECTIONS = 32;
const MAX_IMPORT_DESCRIPTORS = 64;
const MAX_IMPORTS_PER_MODULE = 4096;
const MAX_EXPORTS = 4096;
const SYSTEM_IMPORT_MODULES = new Set([
  'advapi32.dll',
  'gdi32.dll',
  'kernel32.dll',
  'ntdll.dll',
  'ole32.dll',
  'shell32.dll',
  'shlwapi.dll',
  'user32.dll'
]);

export const NATIVE_BRIDGE_EXPORT_DEFINITIONS = Object.freeze({
  beginSession: Object.freeze([
    'f6b0747ef850001b56245f96e699c753f85471f8cc4ef9a9d805c70a14d984cb',
    'int',
    Object.freeze(['str16', 'int', 'str', 'uint16'])
  ]),
  setHookPayload: Object.freeze([
    '343915b834dd4956579373c9fb15a427bbc418f6e8ca270d81afc371acb144e4',
    'int',
    Object.freeze(['void *', 'uint32'])
  ]),
  applyAuth: Object.freeze([
    '3259691fa8fdabe0ed9132093420053778035688bc4dd9de4be841b47fd15543',
    'int',
    Object.freeze(['uint32', 'str', 'str'])
  ]),
  startGame: Object.freeze([
    '0568b3fe1ed924dfdb7e19f2fc5df9dfbe99634dbbe6ea468dfb332ebad47730',
    'int',
    Object.freeze([])
  ]),
  cancel: Object.freeze([
    '873b6b2da7ad0150b1bc9b696c5e87061207bfb1be8da4a2c14dc05a30fa8e9d',
    'int',
    Object.freeze([])
  ]),
  getState: Object.freeze([
    '5557663436a2368a20b1571f579c3044b6a6e2f1fc045373ad5cf678afcd1925',
    'int',
    Object.freeze([])
  ]),
  getProgress: Object.freeze([
    '7a670bc35e5861d93574867f269b40d7014833100cba63b834bbfd8a0041b3b7',
    'int',
    Object.freeze([])
  ]),
  isBusy: Object.freeze([
    'c6d8951fa53d5dab2de53dc85d898f102f50cf34786e8f122dc1aa245869cb7f',
    'int',
    Object.freeze([])
  ]),
  getGameProcessId: Object.freeze([
    '0d9ec2b44acc2bca5f140d25a1fa8019cd21f560efe6f2f4ad69c38ca9bd009c',
    'uint32',
    Object.freeze([])
  ]),
  getLastError: Object.freeze([
    'c7c2399e9a4525d3172135cf3c2adafd2bac99cc2a78a5f4f785ba122a5b2c53',
    'str16',
    Object.freeze([])
  ])
});

const NATIVE_BRIDGE_EXPORT_DIGEST_OWNERS = new Map();
for (const [property, definition] of Object.entries(NATIVE_BRIDGE_EXPORT_DEFINITIONS)) {
  const digest = definition[0];
  if (!/^[0-9a-f]{64}$/.test(digest)) {
    throw new Error(`NativeBridge export digest for ${property} is invalid.`);
  }
  if (NATIVE_BRIDGE_EXPORT_DIGEST_OWNERS.has(digest)) {
    throw new Error('NativeBridge required export digest collision detected.');
  }
  NATIVE_BRIDGE_EXPORT_DIGEST_OWNERS.set(digest, property);
}

let prototypeSequence = 0;
let windowsApi = null;

function toBuffer(value) {
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  throw new TypeError('NativeBridge image must be a Buffer or Uint8Array.');
}

function assertRange(total, offset, size, label) {
  if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(size) ||
      offset < 0 || size < 0 || offset > total || size > total - offset) {
    throw new Error(`NativeBridge PE ${label} is out of bounds.`);
  }
}

function checkedAdd(left, right, label) {
  const result = left + right;
  if (!Number.isSafeInteger(result) || result < left) {
    throw new Error(`NativeBridge PE ${label} overflowed.`);
  }
  return result;
}

function alignUp(value, alignment) {
  if (!Number.isSafeInteger(value) || !Number.isSafeInteger(alignment) ||
      value < 0 || alignment <= 0 || (alignment & (alignment - 1)) !== 0) {
    throw new Error('NativeBridge PE alignment is invalid.');
  }
  return Math.ceil(value / alignment) * alignment;
}

function hashHex(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

function hashExportName(name) {
  return createHash('sha256').update(name, 'ascii').digest('hex');
}

function verifyExpectedHash(bytes, expectedSha256, required) {
  const expected = String(expectedSha256 || '').trim().toLowerCase();
  if (!expected) {
    if (required) throw new Error('NativeBridge expected SHA-256 hash is required.');
    return hashHex(bytes);
  }
  if (!/^[0-9a-f]{64}$/.test(expected)) {
    throw new Error('NativeBridge expected SHA-256 hash is invalid.');
  }
  const actualBytes = createHash('sha256').update(bytes).digest();
  const expectedBytes = Buffer.from(expected, 'hex');
  try {
    if (!timingSafeEqual(actualBytes, expectedBytes)) {
      throw new Error('NativeBridge image hash does not match the authenticated bundle.');
    }
  } finally {
    actualBytes.fill(0);
    expectedBytes.fill(0);
  }
  return expected;
}

function readAscii(bytes, offset, maximum, label) {
  assertRange(bytes.length, offset, 1, label);
  const endLimit = Math.min(bytes.length, checkedAdd(offset, maximum, label));
  const end = bytes.indexOf(0, offset);
  if (end < offset || end >= endLimit) {
    throw new Error(`NativeBridge PE ${label} is not a bounded NUL-terminated string.`);
  }
  const value = bytes.toString('ascii', offset, end);
  if (!value || /[^\x20-\x7e]/.test(value)) {
    throw new Error(`NativeBridge PE ${label} is invalid.`);
  }
  return value;
}

function rvaToFileOffset(descriptor, rva, size, label) {
  assertRange(descriptor.sizeOfImage, rva, size, label);
  if (rva < descriptor.sizeOfHeaders && size <= descriptor.sizeOfHeaders - rva) {
    assertRange(descriptor.fileSize, rva, size, label);
    return rva;
  }
  for (const section of descriptor.sections) {
    if (rva < section.virtualAddress) continue;
    const delta = rva - section.virtualAddress;
    if (delta <= section.rawSize && size <= section.rawSize - delta) {
      const offset = checkedAdd(section.rawPointer, delta, label);
      assertRange(descriptor.fileSize, offset, size, label);
      return offset;
    }
  }
  throw new Error(`NativeBridge PE ${label} does not map to file-backed data.`);
}

function readDirectory(bytes, optionalOffset, directoryCount, index) {
  if (index >= directoryCount) return { rva: 0, size: 0 };
  const offset = optionalOffset + 112 + index * 8;
  assertRange(bytes.length, offset, 8, `directory ${index}`);
  return {
    rva: bytes.readUInt32LE(offset),
    size: bytes.readUInt32LE(offset + 4)
  };
}

function parseExports(bytes, descriptor) {
  const directory = descriptor.directories[DIRECTORY_EXPORT];
  const byName = new Map();
  const byDigest = new Map();
  if (!directory.rva || !directory.size) return { byName, byDigest };
  const offset = rvaToFileOffset(descriptor, directory.rva, 40, 'export directory');
  const functionCount = bytes.readUInt32LE(offset + 20);
  const nameCount = bytes.readUInt32LE(offset + 24);
  const functionsRva = bytes.readUInt32LE(offset + 28);
  const namesRva = bytes.readUInt32LE(offset + 32);
  const ordinalsRva = bytes.readUInt32LE(offset + 36);
  if (!functionCount || !nameCount || functionCount > MAX_EXPORTS || nameCount > MAX_EXPORTS) {
    throw new Error('NativeBridge PE export counts are invalid.');
  }
  const functionsOffset = rvaToFileOffset(descriptor, functionsRva, functionCount * 4, 'export address table');
  const namesOffset = rvaToFileOffset(descriptor, namesRva, nameCount * 4, 'export name table');
  const ordinalsOffset = rvaToFileOffset(descriptor, ordinalsRva, nameCount * 2, 'export ordinal table');
  for (let index = 0; index < nameCount; index += 1) {
    const nameRva = bytes.readUInt32LE(namesOffset + index * 4);
    const name = readAscii(bytes, rvaToFileOffset(descriptor, nameRva, 1, 'export name'), 256, 'export name');
    const ordinal = bytes.readUInt16LE(ordinalsOffset + index * 2);
    if (ordinal >= functionCount || byName.has(name)) {
      throw new Error('NativeBridge PE export ordinal or name is invalid.');
    }
    const functionRva = bytes.readUInt32LE(functionsOffset + ordinal * 4);
    if (!functionRva || functionRva >= descriptor.sizeOfImage ||
        (functionRva >= directory.rva && functionRva < directory.rva + directory.size)) {
      throw new Error(`NativeBridge PE export ${name} is null, forwarded or out of bounds.`);
    }
    const digest = hashExportName(name);
    if (byDigest.has(digest)) {
      throw new Error('NativeBridge PE export name digest collision detected.');
    }
    byName.set(name, functionRva);
    byDigest.set(digest, functionRva);
  }
  return { byName, byDigest };
}

function parseImports(bytes, descriptor) {
  const directory = descriptor.directories[DIRECTORY_IMPORT];
  const modules = [];
  if (!directory.rva || !directory.size) return modules;
  let cursor = rvaToFileOffset(descriptor, directory.rva, directory.size, 'import directory');
  const end = cursor + directory.size;
  let terminated = false;
  for (let moduleIndex = 0; moduleIndex < MAX_IMPORT_DESCRIPTORS && cursor + 20 <= end; moduleIndex += 1) {
    const originalFirstThunk = bytes.readUInt32LE(cursor);
    const timestamp = bytes.readUInt32LE(cursor + 4);
    const forwarderChain = bytes.readUInt32LE(cursor + 8);
    const nameRva = bytes.readUInt32LE(cursor + 12);
    const firstThunkRva = bytes.readUInt32LE(cursor + 16);
    cursor += 20;
    if (!(originalFirstThunk || timestamp || forwarderChain || nameRva || firstThunkRva)) {
      terminated = true;
      break;
    }
    if (!nameRva || !firstThunkRva) throw new Error('NativeBridge PE import descriptor is incomplete.');
    const moduleName = readAscii(
      bytes,
      rvaToFileOffset(descriptor, nameRva, 1, 'import module name'),
      128,
      'import module name'
    ).toLowerCase();
    if (!SYSTEM_IMPORT_MODULES.has(moduleName) || moduleName.includes('/') || moduleName.includes('\\')) {
      throw new Error(`NativeBridge PE imports disallowed module ${moduleName}.`);
    }
    const lookupRva = originalFirstThunk || firstThunkRva;
    const entries = [];
    let thunkTerminated = false;
    for (let thunkIndex = 0; thunkIndex < MAX_IMPORTS_PER_MODULE; thunkIndex += 1) {
      const lookupOffset = rvaToFileOffset(descriptor, lookupRva + thunkIndex * 8, 8, 'import lookup thunk');
      const lookup = bytes.readBigUInt64LE(lookupOffset);
      if (lookup === 0n) {
        thunkTerminated = true;
        break;
      }
      if (lookup & IMAGE_ORDINAL_FLAG64) {
        throw new Error(`NativeBridge PE ordinal import from ${moduleName} is not allowed.`);
      }
      const importByNameRva = Number(lookup & 0x7fffffffn);
      const importNameOffset = rvaToFileOffset(descriptor, importByNameRva, 3, 'import-by-name entry');
      const name = readAscii(bytes, importNameOffset + 2, 512, 'import function name');
      const iatRva = firstThunkRva + thunkIndex * 8;
      assertRange(descriptor.sizeOfImage, iatRva, 8, 'import address slot');
      entries.push({ name, iatRva });
    }
    if (!thunkTerminated || !entries.length) {
      throw new Error(`NativeBridge PE import thunk list for ${moduleName} is invalid.`);
    }
    modules.push({ name: moduleName, entries });
  }
  if (!terminated || !modules.length) throw new Error('NativeBridge PE import directory is not terminated.');
  return modules;
}

function parseRelocations(bytes, descriptor) {
  const directory = descriptor.directories[DIRECTORY_BASERELOC];
  const relocations = [];
  const typeCounts = new Map();
  if (!directory.rva || !directory.size) return { relocations, typeCounts };
  let cursor = rvaToFileOffset(descriptor, directory.rva, directory.size, 'base relocation directory');
  const end = cursor + directory.size;
  while (cursor < end) {
    if (end - cursor < 8) throw new Error('NativeBridge PE relocation block is truncated.');
    const pageRva = bytes.readUInt32LE(cursor);
    const blockSize = bytes.readUInt32LE(cursor + 4);
    if (!pageRva && !blockSize) {
      for (let index = cursor; index < end; index += 1) {
        if (bytes[index] !== 0) throw new Error('NativeBridge PE relocation padding is invalid.');
      }
      cursor = end;
      break;
    }
    if (blockSize < 8 || (blockSize & 1) !== 0 || blockSize > end - cursor) {
      throw new Error('NativeBridge PE relocation block size is invalid.');
    }
    const entryCount = (blockSize - 8) / 2;
    for (let index = 0; index < entryCount; index += 1) {
      const value = bytes.readUInt16LE(cursor + 8 + index * 2);
      const type = value >>> 12;
      const targetRva = pageRva + (value & 0x0fff);
      typeCounts.set(type, (typeCounts.get(type) || 0) + 1);
      if (type === IMAGE_REL_BASED_ABSOLUTE) continue;
      if (type !== IMAGE_REL_BASED_DIR64) {
        throw new Error(`NativeBridge PE relocation type ${type} is unsupported.`);
      }
      assertRange(descriptor.sizeOfImage, targetRva, 8, 'DIR64 relocation target');
      relocations.push(targetRva);
    }
    cursor += blockSize;
  }
  if (cursor !== end) throw new Error('NativeBridge PE relocation directory length is invalid.');
  return { relocations, typeCounts };
}

function parseExceptionDirectory(bytes, descriptor) {
  const directory = descriptor.directories[DIRECTORY_EXCEPTION];
  if (!directory.rva || !directory.size || directory.size % 12 !== 0) {
    throw new Error('NativeBridge PE x64 exception directory is missing or invalid.');
  }
  const count = directory.size / 12;
  const offset = rvaToFileOffset(descriptor, directory.rva, directory.size, 'exception directory');
  for (let index = 0; index < count; index += 1) {
    const entry = offset + index * 12;
    const beginRva = bytes.readUInt32LE(entry);
    const endRva = bytes.readUInt32LE(entry + 4);
    const unwindRva = bytes.readUInt32LE(entry + 8);
    if (!beginRva || beginRva >= endRva || endRva > descriptor.sizeOfImage ||
        !unwindRva || unwindRva >= descriptor.sizeOfImage) {
      throw new Error('NativeBridge PE x64 runtime function entry is invalid.');
    }
  }
  return count;
}

export function inspectNativeBridgePe(input, options = {}) {
  const bytes = toBuffer(input);
  if (bytes.length < 0x200 || bytes.length > MAX_FILE_SIZE) {
    throw new Error('NativeBridge PE file size is invalid.');
  }
  const sha256 = verifyExpectedHash(bytes, options.expectedSha256, false);
  if (bytes.readUInt16LE(0) !== IMAGE_DOS_SIGNATURE) throw new Error('NativeBridge DOS signature is invalid.');
  const ntOffset = bytes.readUInt32LE(0x3c);
  assertRange(bytes.length, ntOffset, 24, 'NT headers');
  if (bytes.readUInt32LE(ntOffset) !== IMAGE_NT_SIGNATURE) throw new Error('NativeBridge NT signature is invalid.');
  const fileOffset = ntOffset + 4;
  const machine = bytes.readUInt16LE(fileOffset);
  const sectionCount = bytes.readUInt16LE(fileOffset + 2);
  const optionalSize = bytes.readUInt16LE(fileOffset + 16);
  const characteristics = bytes.readUInt16LE(fileOffset + 18);
  if (machine !== IMAGE_FILE_MACHINE_AMD64 || !sectionCount || sectionCount > MAX_SECTIONS ||
      !(characteristics & IMAGE_FILE_DLL)) {
    throw new Error('NativeBridge must be an x64 DLL with a bounded section table.');
  }
  const optionalOffset = ntOffset + 24;
  assertRange(bytes.length, optionalOffset, optionalSize, 'optional header');
  if (optionalSize < 112 || bytes.readUInt16LE(optionalOffset) !== IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    throw new Error('NativeBridge must use the PE32+ optional header.');
  }
  const entrypointRva = bytes.readUInt32LE(optionalOffset + 16);
  const imageBase = bytes.readBigUInt64LE(optionalOffset + 24);
  const sectionAlignment = bytes.readUInt32LE(optionalOffset + 32);
  const sizeOfImage = bytes.readUInt32LE(optionalOffset + 56);
  const sizeOfHeaders = bytes.readUInt32LE(optionalOffset + 60);
  const directoryCount = Math.min(bytes.readUInt32LE(optionalOffset + 108), 16);
  if (!entrypointRva || entrypointRva >= sizeOfImage || imageBase === 0n ||
      sizeOfImage < 0x1000 || sizeOfImage > MAX_IMAGE_SIZE ||
      sectionAlignment < 0x1000 || (sectionAlignment & (sectionAlignment - 1)) !== 0 ||
      sizeOfHeaders <= 0 || sizeOfHeaders > bytes.length || sizeOfHeaders > sizeOfImage) {
    throw new Error('NativeBridge PE image geometry is invalid.');
  }
  const sectionTable = optionalOffset + optionalSize;
  assertRange(bytes.length, sectionTable, sectionCount * 40, 'section table');
  const sections = [];
  for (let index = 0; index < sectionCount; index += 1) {
    const offset = sectionTable + index * 40;
    const nameEnd = bytes.indexOf(0, offset);
    const boundedNameEnd = nameEnd >= offset && nameEnd < offset + 8 ? nameEnd : offset + 8;
    const name = bytes.toString('ascii', offset, boundedNameEnd);
    const virtualSize = bytes.readUInt32LE(offset + 8);
    const virtualAddress = bytes.readUInt32LE(offset + 12);
    const rawSize = bytes.readUInt32LE(offset + 16);
    const rawPointer = bytes.readUInt32LE(offset + 20);
    const sectionCharacteristics = bytes.readUInt32LE(offset + 36);
    const mappedSize = Math.max(virtualSize, rawSize);
    if (!name || /[^\x20-\x7e]/.test(name) || !mappedSize ||
        virtualAddress % sectionAlignment !== 0 ||
        checkedAdd(virtualAddress, mappedSize, 'section extent') > sizeOfImage) {
      throw new Error('NativeBridge PE section geometry is invalid.');
    }
    if (rawSize) assertRange(bytes.length, rawPointer, rawSize, `section ${name} raw data`);
    sections.push({
      name,
      virtualAddress,
      virtualSize,
      rawPointer,
      rawSize,
      characteristics: sectionCharacteristics,
      mappedSize
    });
  }
  const orderedSections = [...sections].sort((left, right) => left.virtualAddress - right.virtualAddress);
  let previousEnd = alignUp(sizeOfHeaders, sectionAlignment);
  for (const section of orderedSections) {
    if (section.virtualAddress < previousEnd) throw new Error('NativeBridge PE sections overlap.');
    previousEnd = alignUp(section.virtualAddress + section.mappedSize, sectionAlignment);
  }
  const directories = Array.from({ length: 16 }, (_, index) =>
    readDirectory(bytes, optionalOffset, directoryCount, index));
  for (let index = 0; index < directories.length; index += 1) {
    const directory = directories[index];
    if ((directory.rva === 0) !== (directory.size === 0)) {
      throw new Error(`NativeBridge PE directory ${index} is incomplete.`);
    }
    if (directory.rva) assertRange(sizeOfImage, directory.rva, directory.size, `directory ${index}`);
  }
  const descriptor = {
    fileSize: bytes.length,
    sha256,
    machine,
    pe32Plus: true,
    imageBase,
    sizeOfImage,
    sizeOfHeaders,
    sectionAlignment,
    entrypointRva,
    characteristics,
    sections,
    directories
  };
  const imports = parseImports(bytes, descriptor);
  const relocation = parseRelocations(bytes, descriptor);
  const parsedExports = parseExports(bytes, descriptor);
  const exportRvas = parsedExports.byName;
  const exportRvasByDigest = parsedExports.byDigest;
  const exceptionEntryCount = parseExceptionDirectory(bytes, descriptor);
  if (!exportRvas.size) throw new Error('NativeBridge PE has no named exports.');
  const missingExports = [];
  for (const [digest, property] of NATIVE_BRIDGE_EXPORT_DIGEST_OWNERS) {
    if (!exportRvasByDigest.has(digest)) missingExports.push(property);
  }
  if (missingExports.length) {
    throw new Error(`NativeBridge PE is missing required exports: ${missingExports.join(', ')}.`);
  }
  return {
    ...descriptor,
    imports,
    importModules: new Set(imports.map((entry) => entry.name)),
    relocations: relocation.relocations,
    relocationTypeCounts: relocation.typeCounts,
    exportRvas,
    exportRvasByDigest,
    exceptionEntryCount,
    hasTls: Boolean(directories[DIRECTORY_TLS].rva),
    hasDelayImports: Boolean(directories[DIRECTORY_DELAY_IMPORT].rva)
  };
}

function getWindowsApi() {
  if (windowsApi) return windowsApi;
  const kernel32 = koffi.load('kernel32.dll');
  windowsApi = Object.freeze({
    kernel32,
    virtualAlloc: kernel32.func('__stdcall', 'VirtualAlloc', 'void *', ['void *', 'size_t', 'uint32', 'uint32']),
    virtualFree: kernel32.func('__stdcall', 'VirtualFree', 'int', ['void *', 'size_t', 'uint32']),
    virtualProtect: kernel32.func('__stdcall', 'VirtualProtect', 'int', ['void *', 'size_t', 'uint32', 'void *']),
    loadLibraryExW: kernel32.func('__stdcall', 'LoadLibraryExW', 'void *', ['str16', 'void *', 'uint32']),
    freeLibrary: kernel32.func('__stdcall', 'FreeLibrary', 'int', ['void *']),
    getProcAddress: kernel32.func('__stdcall', 'GetProcAddress', 'void *', ['void *', 'str']),
    rtlMoveMemory: kernel32.func('__stdcall', 'RtlMoveMemory', 'void', ['void *', 'void *', 'size_t']),
    rtlAddFunctionTable: kernel32.func('__stdcall', 'RtlAddFunctionTable', 'int', ['void *', 'uint32', 'uint64_t']),
    rtlDeleteFunctionTable: kernel32.func('__stdcall', 'RtlDeleteFunctionTable', 'int', ['void *']),
    getCurrentProcess: kernel32.func('__stdcall', 'GetCurrentProcess', 'void *', []),
    flushInstructionCache: kernel32.func('__stdcall', 'FlushInstructionCache', 'int', ['void *', 'void *', 'size_t'])
  });
  return windowsApi;
}

function sectionProtection(characteristics) {
  const execute = Boolean(characteristics & SECTION_MEM_EXECUTE);
  const read = Boolean(characteristics & SECTION_MEM_READ);
  const write = Boolean(characteristics & SECTION_MEM_WRITE);
  if (execute && write) return PAGE_EXECUTE_READWRITE;
  if (execute && read) return PAGE_EXECUTE_READ;
  if (execute) return PAGE_EXECUTE;
  if (write) return PAGE_READWRITE;
  if (read) return PAGE_READONLY;
  return PAGE_NOACCESS;
}

function buildMappedImage(bytes, descriptor, baseAddress, api, loadedModules) {
  const image = Buffer.alloc(descriptor.sizeOfImage);
  try {
    bytes.copy(image, 0, 0, descriptor.sizeOfHeaders);
    for (const section of descriptor.sections) {
      if (section.rawSize) {
        bytes.copy(
          image,
          section.virtualAddress,
          section.rawPointer,
          section.rawPointer + section.rawSize
        );
      }
    }
    const delta = baseAddress - descriptor.imageBase;
    if (delta !== 0n && !descriptor.relocations.length) {
      throw new Error('NativeBridge PE cannot relocate away from its preferred image base.');
    }
    for (const targetRva of descriptor.relocations) {
      const current = image.readBigUInt64LE(targetRva);
      image.writeBigUInt64LE(BigInt.asUintN(64, current + delta), targetRva);
    }
    const handles = new Map();
    for (const module of descriptor.imports) {
      let handle = handles.get(module.name);
      if (!handle) {
        handle = api.loadLibraryExW(module.name, null, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!handle) throw new Error(`NativeBridge dependency ${module.name} could not be loaded from System32.`);
        handles.set(module.name, handle);
        loadedModules.push(handle);
      }
      for (const imported of module.entries) {
        const pointer = api.getProcAddress(handle, imported.name);
        if (!pointer) throw new Error(`NativeBridge import ${module.name}!${imported.name} could not be resolved.`);
        image.writeBigUInt64LE(koffi.address(pointer), imported.iatRva);
      }
    }
    return image;
  } catch (error) {
    image.fill(0);
    throw error;
  }
}

function protectImage(api, baseAddress, descriptor) {
  const oldProtection = Buffer.alloc(4);
  try {
    if (!api.virtualProtect(baseAddress, descriptor.sizeOfImage, PAGE_NOACCESS, oldProtection)) {
      throw new Error('NativeBridge image-wide protection reset failed.');
    }
    const headerSize = alignUp(descriptor.sizeOfHeaders, 0x1000);
    if (!api.virtualProtect(baseAddress, headerSize, PAGE_READONLY, oldProtection)) {
      throw new Error('NativeBridge header protection failed.');
    }
    for (const section of descriptor.sections) {
      const size = alignUp(section.mappedSize, 0x1000);
      const address = baseAddress + BigInt(section.virtualAddress);
      if (!api.virtualProtect(address, size, sectionProtection(section.characteristics), oldProtection)) {
        throw new Error(`NativeBridge section ${section.name} protection failed.`);
      }
    }
  } finally {
    oldProtection.fill(0);
  }
}

function createGuardedFunction(nativeFunction, isUnavailable, onAsyncBegin, onAsyncEnd) {
  const unavailable = () => {
    if (isUnavailable()) throw new Error('NativeBridge mapped image is unloaded.');
  };
  const guarded = (...args) => {
    unavailable();
    return nativeFunction(...args);
  };
  guarded.async = (...args) => {
    unavailable();
    const callback = args.at(-1);
    if (typeof callback !== 'function') throw new TypeError('Koffi async call requires a callback.');
    onAsyncBegin();
    args[args.length - 1] = (error, result) => {
      onAsyncEnd();
      callback(error, result);
    };
    try {
      return nativeFunction.async(...args);
    } catch (error) {
      onAsyncEnd();
      throw error;
    }
  };
  return guarded;
}

export function mapNativeBridgeFromBytes(input, options = {}) {
  const bytes = toBuffer(input);
  const descriptor = inspectNativeBridgePe(bytes, {
    expectedSha256: verifyExpectedHash(bytes, options.expectedSha256, true)
  });
  if (process.platform !== 'win32' || process.arch !== 'x64') {
    throw new Error('NativeBridge in-memory mapping requires Windows x64.');
  }
  if (descriptor.hasTls) throw new Error('NativeBridge PE TLS callbacks are not supported by the memory loader.');
  if (descriptor.hasDelayImports) throw new Error('NativeBridge PE delay imports are not supported by the memory loader.');
  const api = getWindowsApi();
  const loadedModules = [];
  let basePointer = null;
  let baseAddress = 0n;
  let functionTableAddress = 0n;
  let unwindRegistered = false;
  let attached = false;
  let closing = false;
  let closed = false;
  let activeAsyncCalls = 0;
  let entrypoint = null;
  const boundFunctions = new Map();

  const rollback = (detach) => {
    const failures = [];
    if (detach && attached && entrypoint) {
      try {
        entrypoint(baseAddress, DLL_PROCESS_DETACH, null);
      } catch (error) {
        failures.push(error);
      }
      attached = false;
    }
    if (unwindRegistered) {
      try {
        if (!api.rtlDeleteFunctionTable(functionTableAddress)) {
          failures.push(new Error('NativeBridge unwind table could not be unregistered.'));
        }
      } catch (error) {
        failures.push(error);
      }
      unwindRegistered = false;
    }
    for (let index = loadedModules.length - 1; index >= 0; index -= 1) {
      try {
        if (!api.freeLibrary(loadedModules[index])) {
          failures.push(new Error('NativeBridge dependency reference could not be released.'));
        }
      } catch (error) {
        failures.push(error);
      }
    }
    loadedModules.length = 0;
    if (basePointer) {
      try {
        if (!api.virtualFree(basePointer, 0, MEM_RELEASE)) {
          failures.push(new Error('NativeBridge mapped image could not be released.'));
        }
      } catch (error) {
        failures.push(error);
      }
      basePointer = null;
      baseAddress = 0n;
    }
    return failures;
  };

  try {
    basePointer = api.virtualAlloc(
      descriptor.imageBase,
      descriptor.sizeOfImage,
      MEM_RESERVE | MEM_COMMIT,
      PAGE_READWRITE
    );
    if (!basePointer) {
      basePointer = api.virtualAlloc(
        null,
        descriptor.sizeOfImage,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
      );
    }
    if (!basePointer) throw new Error('NativeBridge image memory allocation failed.');
    baseAddress = koffi.address(basePointer);

    const image = buildMappedImage(bytes, descriptor, baseAddress, api, loadedModules);
    try {
      api.rtlMoveMemory(baseAddress, image, image.length);
    } finally {
      image.fill(0);
    }
    protectImage(api, baseAddress, descriptor);
    if (!api.flushInstructionCache(api.getCurrentProcess(), baseAddress, descriptor.sizeOfImage)) {
      throw new Error('NativeBridge instruction cache flush failed.');
    }

    const exceptionDirectory = descriptor.directories[DIRECTORY_EXCEPTION];
    functionTableAddress = baseAddress + BigInt(exceptionDirectory.rva);
    if (!api.rtlAddFunctionTable(
      functionTableAddress,
      descriptor.exceptionEntryCount,
      baseAddress
    )) {
      throw new Error('NativeBridge x64 unwind table registration failed.');
    }
    unwindRegistered = true;

    const entryPrototype = koffi.proto(
      '__stdcall',
      `MappedNativeBridgeEntry_${++prototypeSequence}`,
      'int',
      ['void *', 'uint32', 'void *']
    );
    entrypoint = koffi.decode(basePointer, descriptor.entrypointRva, entryPrototype);
    if (!entrypoint(baseAddress, DLL_PROCESS_ATTACH, null)) {
      throw new Error('NativeBridge CRT entrypoint rejected DLL_PROCESS_ATTACH.');
    }
    attached = true;
  } catch (error) {
    const failures = rollback(false);
    if (failures.length) throw new AggregateError([error, ...failures], error.message);
    throw error;
  }

  const bindResolvedExport = (convention, cacheIdentity, exportRva, resultType, argumentTypes) => {
    if (closed || closing) throw new Error('NativeBridge mapped image is unloaded.');
    const args = Array.isArray(argumentTypes) ? [...argumentTypes] : [];
    const key = JSON.stringify([convention, cacheIdentity, resultType, args]);
    if (boundFunctions.has(key)) return boundFunctions.get(key);
    const prototype = koffi.proto(
      convention,
      `MappedNativeBridgeExport_${++prototypeSequence}`,
      resultType,
      args
    );
    const nativeFunction = koffi.decode(basePointer, exportRva, prototype);
    const guarded = createGuardedFunction(
      nativeFunction,
      () => closed || closing,
      () => { activeAsyncCalls += 1; },
      () => { activeAsyncCalls = Math.max(0, activeAsyncCalls - 1); }
    );
    boundFunctions.set(key, guarded);
    return guarded;
  };

  const bindExport = (convention, name, resultType, argumentTypes) => {
    const exportName = String(name || '');
    const digest = hashExportName(exportName);
    const exportRva = descriptor.exportRvasByDigest.get(digest);
    if (!exportRva || descriptor.exportRvas.get(exportName) !== exportRva) {
      throw new Error(`NativeBridge export ${exportName} is unavailable.`);
    }
    return bindResolvedExport(convention, digest, exportRva, resultType, argumentTypes);
  };

  const cleanup = () => {
    if (closed) return false;
    if (closing) throw new Error('NativeBridge mapped image cleanup is already running.');
    if (activeAsyncCalls) throw new Error('NativeBridge cannot unload while an asynchronous export is running.');
    closing = true;
    const failures = rollback(true);
    closed = true;
    closing = false;
    boundFunctions.clear();
    if (failures.length) throw new AggregateError(failures, 'NativeBridge mapped image cleanup failed.');
    return true;
  };

  const mapped = {
    baseAddress,
    sizeOfImage: descriptor.sizeOfImage,
    bridgeSha256: descriptor.sha256,
    exportRvas: new Map(descriptor.exportRvas),
    exportRvasByDigest: new Map(descriptor.exportRvasByDigest),
    exportAddresses: new Map(
      [...descriptor.exportRvas].map(([name, rva]) => [name, baseAddress + BigInt(rva)])
    ),
    bindExport,
    cleanup
  };
  const bindings = {};
  for (const [property, [digest, resultType, argumentTypes]] of Object.entries(NATIVE_BRIDGE_EXPORT_DEFINITIONS)) {
    const exportRva = descriptor.exportRvasByDigest.get(digest);
    if (!exportRva) throw new Error(`NativeBridge required binding ${property} is unavailable.`);
    bindings[property] = bindResolvedExport('__stdcall', digest, exportRva, resultType, argumentTypes);
  }
  mapped.bindings = Object.freeze(bindings);
  return Object.freeze(mapped);
}
