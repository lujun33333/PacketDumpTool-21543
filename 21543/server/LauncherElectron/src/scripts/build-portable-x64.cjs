const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { listPackage } = require('@electron/asar');
const { readPeRuntimeResource } = require('../build/pe-runtime-resource.cjs');

const {
  FOOTER_MAGIC,
  FOOTER_SIZE,
  INDEX_ENTRY_HEADER_SIZE,
  hashFile,
  listFiles,
  readPeInfo
} = require('./smoke-portable-x64.cjs');

const ROOT = path.resolve(__dirname, '..', '..');
const PACKAGE_PATH = path.join(ROOT, 'package.json');
const DIST_DIR = path.join(ROOT, 'dist');
const UNPACKED_DIR = path.join(DIST_DIR, 'win-unpacked');
const RUNTIME_BUNDLE_BUILDER = path.join(ROOT, 'src', 'scripts', 'build-runtime-bundle.cjs');
const RUNTIME_BUNDLE_PATH = path.join(ROOT, 'resources', 'native', 'qqspeed-runtime.bundle');
const RUNTIME_BUNDLE_ENTRY = 'resources/native/qqspeed-runtime.bundle';
const FORBIDDEN_NATIVE_DLLS = new Set(['qqspeednativebridge.dll', 'qqspeed_clean_hook_64.dll']);
const CRATE_DIR = path.join(ROOT, 'native', 'portable-x64-bootstrap');
const STUB_PATH = path.join(
  CRATE_DIR,
  'target',
  'x86_64-pc-windows-msvc',
  'release',
  'portable-x64-bootstrap.exe'
);

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: options.cwd || ROOT,
    env: options.env || process.env,
    encoding: 'utf8',
    stdio: options.capture ? 'pipe' : 'inherit',
    windowsHide: true
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    const detail = options.capture
      ? `\nstdout:\n${result.stdout || ''}\nstderr:\n${result.stderr || ''}`
      : '';
    throw new Error(`命令失败 (${result.status})：${command} ${args.join(' ')}${detail}`);
  }
  return result;
}

function findCargo() {
  const candidates = [
    process.env.CARGO,
    process.env.USERPROFILE && path.join(process.env.USERPROFILE, '.cargo', 'bin', 'cargo.exe'),
    'cargo.exe'
  ].filter(Boolean);
  for (const candidate of candidates) {
    if (candidate === 'cargo.exe' || fs.existsSync(candidate)) return candidate;
  }
  throw new Error('未找到 Cargo。请安装 x86_64-pc-windows-msvc Rust 工具链。');
}

function validateX64Gui(filePath, label) {
  const info = readPeInfo(filePath);
  if (info.machine !== 0x8664 || info.optionalMagic !== 0x20b || info.subsystem !== 2) {
    throw new Error(
      `${label} 必须为 x64 PE32+ GUI：machine=0x${info.machine.toString(16)} optional=0x${info.optionalMagic.toString(16)} subsystem=${info.subsystem}`
    );
  }
  return info;
}

function validatePackagedRuntimeBundle(executablePath) {
  if (!fs.existsSync(RUNTIME_BUNDLE_PATH) || !fs.statSync(RUNTIME_BUNDLE_PATH).isFile()) {
    throw new Error(`源运行时 bundle 缺失：${RUNTIME_BUNDLE_PATH}`);
  }

  const resourcesDir = path.join(UNPACKED_DIR, 'resources');
  const looseNativeDir = path.join(resourcesDir, 'Native');
  if (fs.existsSync(looseNativeDir)) {
    throw new Error(`unpacked 不得包含裸 Native 目录：${looseNativeDir}`);
  }

  const looseDlls = listFiles(resourcesDir)
    .filter((file) => FORBIDDEN_NATIVE_DLLS.has(path.posix.basename(file.relativePath).toLowerCase()));
  if (looseDlls.length > 0) {
    throw new Error(`unpacked 暴露裸原生 DLL：${looseDlls.map((file) => file.relativePath).join(', ')}`);
  }

  const asarPath = path.join(resourcesDir, 'app.asar');
  if (!fs.existsSync(asarPath) || !fs.statSync(asarPath).isFile()) {
    throw new Error(`unpacked app.asar 缺失：${asarPath}`);
  }
  const asarEntries = listPackage(asarPath)
    .map((entry) => String(entry).replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase());
  if (asarEntries.includes(RUNTIME_BUNDLE_ENTRY)) {
    throw new Error(`app.asar 不得包含运行时 bundle：${RUNTIME_BUNDLE_ENTRY}`);
  }
  const dllEntries = asarEntries.filter((entry) => entry.endsWith('.dll'));
  if (dllEntries.length > 0) {
    throw new Error(`app.asar 暴露原生 DLL 条目：${dllEntries.join(', ')}`);
  }

  const sourceBundle = fs.readFileSync(RUNTIME_BUNDLE_PATH);
  const packagedBundle = readPeRuntimeResource(executablePath);
  try {
    if (!packagedBundle.equals(sourceBundle)) {
      throw new Error('QQSpeedLauncher.exe 内嵌运行时资源与源 bundle 不一致。');
    }
    return {
      size: sourceBundle.length,
      sha256: crypto.createHash('sha256').update(sourceBundle).digest('hex')
    };
  } finally {
    sourceBundle.fill(0);
    packagedBundle.fill(0);
  }
}

function writeAll(fd, buffer, position) {
  let offset = 0;
  while (offset < buffer.length) {
    const written = fs.writeSync(fd, buffer, offset, buffer.length - offset, position + offset);
    if (written <= 0) throw new Error(`写入 SFX 失败：position=${position + offset}`);
    offset += written;
  }
  return position + buffer.length;
}

function appendFile(fd, sourcePath, position, expectedLength) {
  const source = fs.openSync(sourcePath, 'r');
  const hash = crypto.createHash('sha256');
  const buffer = Buffer.allocUnsafe(1024 * 1024);
  let copied = 0;
  try {
    while (true) {
      const bytesRead = fs.readSync(source, buffer, 0, buffer.length, null);
      if (bytesRead === 0) break;
      position = writeAll(fd, buffer.subarray(0, bytesRead), position);
      hash.update(buffer.subarray(0, bytesRead));
      copied += bytesRead;
    }
  } finally {
    fs.closeSync(source);
  }
  if (copied !== expectedLength) {
    throw new Error(`payload 打包期间发生变化：${sourcePath} expected=${expectedLength} actual=${copied}`);
  }
  return { position, sha256: hash.digest() };
}

function makeIndexEntry(file, offset, sha256) {
  const name = Buffer.from(file.relativePath, 'utf8');
  if (name.length === 0 || name.length > 0xffff) {
    throw new Error(`SFX 相对路径长度无效：${file.relativePath}`);
  }
  const pathSegments = file.relativePath.split('/');
  if (file.relativePath.includes('\\') || /^[A-Za-z]:|^\//.test(file.relativePath) ||
      pathSegments.some((segment) => !segment || segment === '.' || segment === '..')) {
    throw new Error(`SFX 相对路径不安全：${file.relativePath}`);
  }
  const header = Buffer.alloc(INDEX_ENTRY_HEADER_SIZE);
  header.writeUInt16LE(name.length, 0);
  header.writeUInt16LE(0, 2);
  header.writeUInt32LE(0, 4);
  header.writeBigUInt64LE(BigInt(offset), 8);
  header.writeBigUInt64LE(BigInt(file.length), 16);
  sha256.copy(header, 24);
  return Buffer.concat([header, name]);
}

function validateEmbeddedResources(filePath) {
  validateX64Gui(filePath, '已嵌入资源的 Rust stub');
  const buffer = fs.readFileSync(filePath);
  const ascii = buffer.toString('latin1');
  const utf16 = buffer.toString('utf16le');
  if (!/requestedExecutionLevel[^>]+level=["']asInvoker["']/i.test(ascii) &&
      !/requestedExecutionLevel[^>]+level=["']asInvoker["']/i.test(utf16)) {
    throw new Error('Rust x64 stub 缺少构建期嵌入的 asInvoker manifest。');
  }
  if (!utf16.includes('QQSpeedLauncher-Portable-x64.exe')) {
    throw new Error('Rust x64 stub 缺少构建期嵌入的 VERSIONINFO。');
  }
}

function writeArchive(candidatePath, files) {
  const fd = fs.openSync(candidatePath, 'r+');
  let position = fs.fstatSync(fd).size;
  const payloadStart = position;
  const indexEntries = [];
  try {
    for (const file of files) {
      const offset = position;
      const appended = appendFile(fd, file.absolutePath, position, file.length);
      position = appended.position;
      indexEntries.push(makeIndexEntry(file, offset, appended.sha256));
    }
    const indexOffset = position;
    const index = Buffer.concat(indexEntries);
    position = writeAll(fd, index, position);

    const footer = Buffer.alloc(FOOTER_SIZE);
    FOOTER_MAGIC.copy(footer, 0);
    footer.writeUInt32LE(1, 16);
    footer.writeUInt32LE(files.length, 20);
    footer.writeBigUInt64LE(BigInt(indexOffset), 24);
    footer.writeBigUInt64LE(BigInt(index.length), 32);
    crypto.createHash('sha256').update(index).digest().copy(footer, 40);
    footer.writeBigUInt64LE(BigInt(payloadStart), 72);
    position = writeAll(fd, footer, position);
    fs.ftruncateSync(fd, position);
    fs.fsyncSync(fd);
  } finally {
    fs.closeSync(fd);
  }
  return { payloadStart, entries: files.length, size: position };
}

function verifyCandidate(candidatePath) {
  run(process.execPath, [path.join(ROOT, 'src', 'scripts', 'smoke-portable-x64.cjs')], {
    capture: true,
    env: {
      ...process.env,
      QQSPEED_PORTABLE_PATH: candidatePath,
      QQSPEED_UNPACKED_DIR: UNPACKED_DIR
    }
  });
}

function promoteVerifiedCandidate(candidatePath, portablePath) {
  const backupPath = `${portablePath}.${process.pid}.previous`;
  if (fs.existsSync(backupPath)) fs.rmSync(backupPath, { force: true });
  const hadPrevious = fs.existsSync(portablePath);
  if (hadPrevious) fs.renameSync(portablePath, backupPath);
  try {
    fs.renameSync(candidatePath, portablePath);
    verifyCandidate(portablePath);
  } catch (error) {
    if (fs.existsSync(portablePath)) fs.rmSync(portablePath, { force: true });
    if (hadPrevious && fs.existsSync(backupPath)) fs.renameSync(backupPath, portablePath);
    throw error;
  }
  if (fs.existsSync(backupPath)) fs.rmSync(backupPath, { force: true });
}

function main() {
  if (process.platform !== 'win32' || process.arch !== 'x64') {
    throw new Error(`原生 Portable 构建仅支持 Windows x64 host：${process.platform}/${process.arch}`);
  }
  const packageJson = JSON.parse(fs.readFileSync(PACKAGE_PATH, 'utf8'));
  const productExecutableName = packageJson.build?.win?.executableName
    || packageJson.build?.productName
    || packageJson.productName;
  const portablePath = path.join(
    DIST_DIR,
    `${packageJson.productName}-Portable-${packageJson.version}-x64.exe`
  );
  const candidatePath = path.join(
    DIST_DIR,
    `.${packageJson.productName}-Portable-${packageJson.version}-x64.${process.pid}.tmp.exe`
  );
  const oldIdentity = fs.existsSync(portablePath)
    ? { size: fs.statSync(portablePath).size, sha256: hashFile(portablePath), machine: readPeInfo(portablePath).machine }
    : null;

  run(process.execPath, [RUNTIME_BUNDLE_BUILDER, '--verify-only'], { capture: true });
  const innerExecutablePath = path.join(UNPACKED_DIR, `${productExecutableName}.exe`);
  validateX64Gui(innerExecutablePath, '内部 Electron');
  const runtimeBundle = validatePackagedRuntimeBundle(innerExecutablePath);
  const files = listFiles(UNPACKED_DIR);
  if (files.length === 0 || files.length > 4096) {
    throw new Error(`unpacked 文件数量无效：${files.length}`);
  }

  const cargo = findCargo();
  run(cargo, ['build', '--release', '--target', 'x86_64-pc-windows-msvc', '--locked'], { cwd: CRATE_DIR });
  validateEmbeddedResources(STUB_PATH);

  if (fs.existsSync(candidatePath)) fs.rmSync(candidatePath, { force: true });
  try {
    fs.copyFileSync(STUB_PATH, candidatePath);
    const archive = writeArchive(candidatePath, files);
    verifyCandidate(candidatePath);
    promoteVerifiedCandidate(candidatePath, portablePath);

    const finalIdentity = {
      size: fs.statSync(portablePath).size,
      sha256: hashFile(portablePath),
      machine: readPeInfo(portablePath).machine
    };
    console.log(`[portable-x64] previous=${oldIdentity ? JSON.stringify(oldIdentity) : 'none'}`);
    console.log(`[portable-x64] archive entries=${archive.entries} payloadStart=${archive.payloadStart} size=${archive.size}`);
    console.log(`[portable-x64] output=${portablePath}`);
    console.log(`[portable-x64] final=${JSON.stringify(finalIdentity)}`);
    console.log(`[portable-x64] runtime_bundle_size=${runtimeBundle.size} runtime_bundle_sha256=${runtimeBundle.sha256}`);
  } finally {
    if (fs.existsSync(candidatePath)) fs.rmSync(candidatePath, { force: true });
  }
}

module.exports = {
  appendFile,
  makeIndexEntry,
  promoteVerifiedCandidate,
  validateEmbeddedResources,
  writeArchive
};

if (require.main === module) {
  try {
    main();
  } catch (error) {
    console.error(error?.stack || error);
    process.exitCode = 1;
  }
}
