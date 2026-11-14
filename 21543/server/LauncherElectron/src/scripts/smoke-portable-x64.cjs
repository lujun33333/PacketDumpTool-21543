const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { listPackage } = require('@electron/asar');
const { readPeRuntimeResource } = require('../build/pe-runtime-resource.cjs');

const ROOT = path.resolve(__dirname, '..', '..');
const PACKAGE_PATH = path.join(ROOT, 'package.json');
const PACKAGE_JSON = JSON.parse(fs.readFileSync(PACKAGE_PATH, 'utf8'));
const PRODUCT_NAME = PACKAGE_JSON.build?.productName || PACKAGE_JSON.productName;
const PRODUCT_EXECUTABLE_NAME = PACKAGE_JSON.build?.win?.executableName || PRODUCT_NAME;
const RUNTIME_BUNDLE_PATH = path.join(ROOT, 'resources', 'native', 'qqspeed-runtime.bundle');
const RUNTIME_BUNDLE_ENTRY = 'resources/native/qqspeed-runtime.bundle';
const GM_DATABASE_PATH = path.join(ROOT, 'resources', 'gm-items.sqlite3');
const GM_DATABASE_ENTRY = 'resources/gm-items.sqlite3';
const FORBIDDEN_NATIVE_DLLS = new Set(['qqspeednativebridge.dll', 'qqspeed_clean_hook_64.dll']);
const UNPACKED_DIR = process.env.QQSPEED_UNPACKED_DIR
  ? path.resolve(process.env.QQSPEED_UNPACKED_DIR)
  : path.join(ROOT, 'dist', 'win-unpacked');
const PORTABLE_PATH = process.env.QQSPEED_PORTABLE_PATH
  ? path.resolve(process.env.QQSPEED_PORTABLE_PATH)
  : path.join(ROOT, 'dist', `${PRODUCT_NAME}-Portable-${PACKAGE_JSON.version}-x64.exe`);
const BOOTSTRAP_ROOT = path.join(ROOT, 'native', 'portable-x64-bootstrap');
const FOOTER_MAGIC = Buffer.from('QQSPEED-X64-SFX\0', 'ascii');
const FOOTER_SIZE = 80;
const INDEX_ENTRY_HEADER_SIZE = 56;
const MAX_INDEX_SIZE = 16 * 1024 * 1024;

function readExact(fd, length, position) {
  const buffer = Buffer.alloc(length);
  let offset = 0;
  while (offset < length) {
    const bytesRead = fs.readSync(fd, buffer, offset, length - offset, position + offset);
    if (bytesRead === 0) {
      throw new Error(`文件意外结束：position=${position + offset} expected=${length}`);
    }
    offset += bytesRead;
  }
  return buffer;
}

function toSafeNumber(value, field) {
  assert(value <= BigInt(Number.MAX_SAFE_INTEGER), `${field} 超出 Node 安全整数范围`);
  return Number(value);
}

function readPeInfo(filePath) {
  const fd = fs.openSync(filePath, 'r');
  try {
    const dos = readExact(fd, 64, 0);
    assert.equal(dos.toString('ascii', 0, 2), 'MZ', `${filePath} 不是 PE 文件`);
    const peOffset = dos.readUInt32LE(0x3c);
    const pe = readExact(fd, 24 + 96, peOffset);
    assert.equal(pe.toString('ascii', 0, 4), 'PE\0\0', `${filePath} PE signature 无效`);
    const machine = pe.readUInt16LE(4);
    const optionalMagic = pe.readUInt16LE(24);
    const subsystem = pe.readUInt16LE(24 + 68);
    return { machine, optionalMagic, subsystem, peOffset };
  } finally {
    fs.closeSync(fd);
  }
}

function readSfxIndex(filePath) {
  const stat = fs.statSync(filePath);
  assert(stat.isFile() && stat.size > FOOTER_SIZE, `SFX 文件无效：${filePath}`);
  const fd = fs.openSync(filePath, 'r');
  try {
    const footerOffset = stat.size - FOOTER_SIZE;
    const footer = readExact(fd, FOOTER_SIZE, footerOffset);
    assert(footer.subarray(0, FOOTER_MAGIC.length).equals(FOOTER_MAGIC), '缺少原生 x64 SFX footer magic');
    assert.equal(footer.readUInt32LE(16), 1, 'SFX 格式版本不受支持');
    const entryCount = footer.readUInt32LE(20);
    const indexOffset = toSafeNumber(footer.readBigUInt64LE(24), 'indexOffset');
    const indexSize = toSafeNumber(footer.readBigUInt64LE(32), 'indexSize');
    const expectedIndexHash = footer.subarray(40, 72);
    const payloadStart = toSafeNumber(footer.readBigUInt64LE(72), 'payloadStart');

    assert(entryCount > 0 && entryCount <= 4096, `SFX entryCount 无效：${entryCount}`);
    assert(indexSize > 0 && indexSize <= MAX_INDEX_SIZE, `SFX indexSize 无效：${indexSize}`);
    assert(payloadStart > 0 && payloadStart < indexOffset, 'SFX payloadStart 无效');
    assert.equal(indexOffset + indexSize, footerOffset, 'SFX index/footer 边界不连续');

    const index = readExact(fd, indexSize, indexOffset);
    const actualIndexHash = crypto.createHash('sha256').update(index).digest();
    assert(actualIndexHash.equals(expectedIndexHash), 'SFX index SHA-256 不匹配');

    const entries = [];
    const names = new Set();
    let cursor = 0;
    let previousEnd = payloadStart;
    for (let i = 0; i < entryCount; i += 1) {
      assert(cursor + INDEX_ENTRY_HEADER_SIZE <= index.length, `SFX index entry ${i} 头部越界`);
      const pathLength = index.readUInt16LE(cursor);
      const flags = index.readUInt16LE(cursor + 2);
      const reserved = index.readUInt32LE(cursor + 4);
      const offset = toSafeNumber(index.readBigUInt64LE(cursor + 8), `entry[${i}].offset`);
      const length = toSafeNumber(index.readBigUInt64LE(cursor + 16), `entry[${i}].length`);
      const sha256 = index.subarray(cursor + 24, cursor + 56).toString('hex');
      cursor += INDEX_ENTRY_HEADER_SIZE;
      assert(pathLength > 0 && cursor + pathLength <= index.length, `SFX index entry ${i} 路径越界`);
      const relativePath = index.subarray(cursor, cursor + pathLength).toString('utf8');
      cursor += pathLength;

      assert.equal(flags, 0, `SFX entry ${relativePath} flags 非零`);
      assert.equal(reserved, 0, `SFX entry ${relativePath} reserved 非零`);
      assert(!relativePath.includes('\\'), `SFX 路径必须使用正斜杠：${relativePath}`);
      assert(!relativePath.startsWith('/') && !/^[A-Za-z]:/.test(relativePath), `SFX 路径不得为绝对路径：${relativePath}`);
      assert(!relativePath.split('/').some((part) => !part || part === '.' || part === '..'), `SFX 路径不安全：${relativePath}`);
      assert(!names.has(relativePath.toLowerCase()), `SFX 路径重复：${relativePath}`);
      assert(offset >= previousEnd && offset + length <= indexOffset, `SFX entry ${relativePath} 数据范围越界或重叠`);
      names.add(relativePath.toLowerCase());
      previousEnd = offset + length;
      entries.push({ relativePath, offset, length, sha256 });
    }
    assert.equal(cursor, index.length, 'SFX index 存在多余字节');
    assert.equal(previousEnd, indexOffset, 'SFX payload 与 index 之间存在未登记字节');
    return { entries, indexOffset, indexSize, payloadStart, fileSize: stat.size };
  } finally {
    fs.closeSync(fd);
  }
}

function listFiles(root) {
  const output = [];
  function walk(directory) {
    for (const item of fs.readdirSync(directory, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name, 'en'))) {
      const absolutePath = path.join(directory, item.name);
      if (item.isDirectory()) {
        walk(absolutePath);
      } else if (item.isFile()) {
        output.push({
          absolutePath,
          relativePath: path.relative(root, absolutePath).split(path.sep).join('/'),
          length: fs.statSync(absolutePath).size
        });
      } else {
        throw new Error(`unpacked payload 不允许符号链接或特殊文件：${absolutePath}`);
      }
    }
  }
  walk(root);
  return output.sort((a, b) => a.relativePath.localeCompare(b.relativePath, 'en'));
}

function hashFile(filePath) {
  const hash = crypto.createHash('sha256');
  const fd = fs.openSync(filePath, 'r');
  const buffer = Buffer.allocUnsafe(1024 * 1024);
  try {
    while (true) {
      const bytesRead = fs.readSync(fd, buffer, 0, buffer.length, null);
      if (bytesRead === 0) break;
      hash.update(buffer.subarray(0, bytesRead));
    }
  } finally {
    fs.closeSync(fd);
  }
  return hash.digest('hex');
}

function hashRange(filePath, start, length) {
  const hash = crypto.createHash('sha256');
  const fd = fs.openSync(filePath, 'r');
  const buffer = Buffer.allocUnsafe(1024 * 1024);
  let position = start;
  let remaining = length;
  try {
    while (remaining > 0) {
      const wanted = Math.min(buffer.length, remaining);
      const bytesRead = fs.readSync(fd, buffer, 0, wanted, position);
      if (bytesRead === 0) throw new Error(`SFX payload 意外结束：position=${position}`);
      hash.update(buffer.subarray(0, bytesRead));
      position += bytesRead;
      remaining -= bytesRead;
    }
  } finally {
    fs.closeSync(fd);
  }
  return hash.digest('hex');
}

function assertAsInvokerManifest(filePath, searchLength) {
  const fd = fs.openSync(filePath, 'r');
  try {
    const buffer = readExact(fd, Math.min(searchLength, 8 * 1024 * 1024), 0);
    const ascii = buffer.toString('latin1');
    const utf16 = buffer.toString('utf16le');
    assert(
      /requestedExecutionLevel[^>]+level=["']asInvoker["']/i.test(ascii) ||
      /requestedExecutionLevel[^>]+level=["']asInvoker["']/i.test(utf16),
      '原生 x64 SFX 未嵌入 asInvoker manifest'
    );
  } finally {
    fs.closeSync(fd);
  }
}

function assertBootstrapBoundary() {
  const sourcePaths = [
    path.join(BOOTSTRAP_ROOT, 'build.rs'),
    ...listFiles(path.join(BOOTSTRAP_ROOT, 'src'))
      .filter((file) => file.relativePath.endsWith('.rs'))
      .map((file) => file.absolutePath)
  ];
  assert(sourcePaths.length >= 3, `Rust x64 bootstrap 源码不完整：${BOOTSTRAP_ROOT}`);
  const source = sourcePaths.map((sourcePath) => fs.readFileSync(sourcePath, 'utf8')).join('\n');
  for (const forbidden of [
    /GameApp/i,
    /qqspeed_clean_hook/i,
    /LauncherCore/i,
    /LockedEndpoint/i,
    /120\.232\.19\.139/,
    /inject/i,
    /shared.?memory/i,
    /login/i
  ]) {
    assert(!forbidden.test(source), `Rust x64 bootstrap 越界包含业务逻辑：${forbidden}`);
  }
}

function verifyPortableX64() {
  assert(fs.existsSync(PORTABLE_PATH), `正式 Portable 不存在：${PORTABLE_PATH}`);
  assert(fs.existsSync(UNPACKED_DIR), `unpacked payload 不存在：${UNPACKED_DIR}`);

  const outer = readPeInfo(PORTABLE_PATH);
  assert.equal(
    outer.machine,
    0x8664,
    `Portable 外层必须为 x64：expected=0x8664 actual=0x${outer.machine.toString(16).padStart(4, '0')}`
  );
  assert.equal(outer.optionalMagic, 0x20b, 'Portable 外层必须为 PE32+');
  assert.equal(outer.subsystem, 2, 'Portable 外层必须为 Windows GUI subsystem');

  const innerPath = path.join(UNPACKED_DIR, `${PRODUCT_EXECUTABLE_NAME}.exe`);
  const inner = readPeInfo(innerPath);
  assert.equal(inner.machine, 0x8664, `内部 ${PRODUCT_EXECUTABLE_NAME}.exe 必须为 x64`);
  assert.equal(inner.optionalMagic, 0x20b, `内部 ${PRODUCT_EXECUTABLE_NAME}.exe 必须为 PE32+`);

  const sfx = readSfxIndex(PORTABLE_PATH);
  assertAsInvokerManifest(PORTABLE_PATH, sfx.payloadStart);
  const unpacked = listFiles(UNPACKED_DIR);
  assert.deepEqual(
    sfx.entries.map(({ relativePath, length }) => ({ relativePath, length })),
    unpacked.map(({ relativePath, length }) => ({ relativePath, length })),
    'SFX 文件清单必须与 dist/win-unpacked 完全一致'
  );

  for (let i = 0; i < unpacked.length; i += 1) {
    const sourceHash = hashFile(unpacked[i].absolutePath);
    assert.equal(sfx.entries[i].sha256, sourceHash, `SFX index hash 不匹配：${unpacked[i].relativePath}`);
    assert.equal(
      hashRange(PORTABLE_PATH, sfx.entries[i].offset, sfx.entries[i].length),
      sourceHash,
      `SFX payload hash 不匹配：${unpacked[i].relativePath}`
    );
  }

  assert.equal(fs.existsSync(path.join(UNPACKED_DIR, 'resources', 'Native')), false,
    'unpacked payload 不得包含裸 Native 目录。');
  const forbiddenPayloadDlls = unpacked
    .filter((file) => FORBIDDEN_NATIVE_DLLS.has(path.posix.basename(file.relativePath).toLowerCase()));
  assert.deepEqual(forbiddenPayloadDlls, [], 'SFX payload 不得包含裸原生 DLL。');

  const asarRelative = 'resources/app.asar';
  const asarEntry = sfx.entries.find((entry) => entry.relativePath.toLowerCase() === asarRelative);
  assert(asarEntry, `SFX 缺少 ${asarRelative}`);
  const gmDatabaseEntry = sfx.entries.find((entry) => entry.relativePath.toLowerCase() === GM_DATABASE_ENTRY);
  assert(gmDatabaseEntry, `SFX 缺少 ${GM_DATABASE_ENTRY}`);
  assert.equal(gmDatabaseEntry.sha256, hashFile(GM_DATABASE_PATH), 'SFX GM 数据库 SHA-256 不匹配');
  const asarPath = path.join(UNPACKED_DIR, 'resources', 'app.asar');
  const asarEntries = listPackage(asarPath)
    .map((entry) => String(entry).replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase());
  assert(!asarEntries.some((entry) => entry.endsWith('gm-items.sqlite3')), 'GM 数据库必须位于 app.asar 外部');
  assert(!asarEntries.includes(RUNTIME_BUNDLE_ENTRY), `app.asar 不得包含 ${RUNTIME_BUNDLE_ENTRY}`);
  const forbiddenAsarDlls = asarEntries.filter((entry) => entry.endsWith('.dll'));
  assert.deepEqual(forbiddenAsarDlls, [], 'app.asar 不得包含原生 DLL 条目。');

  const sourceBundle = fs.readFileSync(RUNTIME_BUNDLE_PATH);
  const packagedBundle = readPeRuntimeResource(innerPath);
  let bundleHash;
  try {
    assert(packagedBundle.equals(sourceBundle), 'QQSpeedLauncher.exe 内嵌运行时资源与生成制品不一致。');
    bundleHash = crypto.createHash('sha256').update(sourceBundle).digest('hex');
    assert(sourceBundle.length > 0 && /^[0-9a-f]{64}$/.test(bundleHash), '运行时 bundle 身份无效。');
  } finally {
    sourceBundle.fill(0);
    packagedBundle.fill(0);
  }

  const packageJson = JSON.parse(fs.readFileSync(PACKAGE_PATH, 'utf8'));
  assert(!/electron-builder\s+--win\s+portable/i.test(packageJson.scripts?.['dist:portable'] || ''), 'dist:portable 不得继续调用 x86 NSIS portable target');
  assert(/build-portable-x64\.cjs/.test(packageJson.scripts?.['dist:portable'] || ''), 'dist:portable 必须调用原生 x64 SFX 构建器');
  assertBootstrapBoundary();

  console.log(
    `PORTABLE_X64_SMOKE_OK entries=${sfx.entries.length} outer=0x${outer.machine.toString(16)} `
    + `inner=0x${inner.machine.toString(16)} runtime_bundle_sha256=${bundleHash}`
  );
}

module.exports = {
  FOOTER_MAGIC,
  FOOTER_SIZE,
  INDEX_ENTRY_HEADER_SIZE,
  hashFile,
  listFiles,
  readPeInfo,
  readSfxIndex,
  verifyPortableX64
};

if (require.main === module) {
  try {
    verifyPortableX64();
  } catch (error) {
    console.error(error?.stack || error);
    process.exitCode = 1;
  }
}
