const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const {
  deriveContentKey,
  generateReleaseMaterial,
  parseEmbeddedReleaseSource,
  renderEmbeddedReleaseSource
} = require('./offline-release-material.cjs');

const root = path.resolve(__dirname, '..', '..');
const projectPath = path.join(root, 'native', 'NativeBridge', 'NativeBridge.vcxproj');
const outputPath = path.join(
  root,
  'native',
  'NativeBridge',
  'build',
  'QQSpeedNativeBridge',
  'x64',
  'Release',
  'out',
  'QQSpeedNativeBridge.dll'
);
const hookInputPath = path.join(root, 'resources', 'native', 'qqspeed_clean_hook_64.dll');
const runtimeBundlePath = path.join(root, 'resources', 'native', 'qqspeed-runtime.bundle');
const defaultServerProjectSearchRoot = path.resolve(root, '..', '..', '..');
const SERVER_PROJECT_SEARCH_DEPTH = 5;
const SERVER_PROJECT_SEARCH_SKIP = new Set([
  '.codex-backups', '.git', 'bin', 'build', 'bulid', 'dist', 'node_modules'
]);
const GENERATED_RELEASE_INCLUDE = 'LauncherReleaseMaterial.generated.inc';
const FORBIDDEN_RELEASE_DEBUG_EXTENSIONS = new Set(['.pdb', '.map', '.ilk']);
const legacyLooseBridgePath = path.join(root, 'resources', 'native', 'QQSpeedNativeBridge.dll');
const legacyRuntimeManifestPath = path.join(root, 'resources', 'native', 'runtime-artifacts.json');
const EXPECTED_HOOK_SHA256 = 'd4a55baacdebc91a876a483b3a39dbae919855ed897989a91c56903f1e8f3436';
const MAXIMUM_MANUAL_MAP_IMAGE_BYTES = 128 * 1024 * 1024;
const requiredExports = [
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
const FORBIDDEN_NATIVE_IMPORT_MODULES = new Set([
  'bcrypt.dll',
  'crypt32.dll',
  'dnsapi.dll',
  'iphlpapi.dll',
  'mswsock.dll',
  'ncrypt.dll',
  'secur32.dll',
  'urlmon.dll',
  'winhttp.dll',
  'wininet.dll',
  'ws2_32.dll',
  'wsock32.dll'
]);
const FORBIDDEN_NATIVE_IMPORT_NAMES = new Set([
  'connect',
  'recv',
  'send',
  'socket',
  'wsastartup'
]);
const FORBIDDEN_NATIVE_IMPORT_PREFIXES = [
  'bcrypt',
  'cryptacquirecontext',
  'cryptdecrypt',
  'cryptencrypt',
  'crypthashdata',
  'cryptprotectdata',
  'cryptunprotectdata',
  'internetopen',
  'ncrypt',
  'winhttp'
];

function existingFile(candidate) {
  const value = String(candidate || '').trim().replace(/^"|"$/g, '');
  return value && fs.existsSync(value) && fs.statSync(value).isFile() ? path.resolve(value) : '';
}

function locateMsBuild() {
  const candidates = [process.env.MSBUILD_EXE_PATH];
  if (process.env.VSINSTALLDIR) {
    candidates.push(path.join(process.env.VSINSTALLDIR, 'MSBuild', 'Current', 'Bin', 'MSBuild.exe'));
  }

  const whereResult = spawnSync('where.exe', ['MSBuild.exe'], { encoding: 'utf8', windowsHide: true });
  if (whereResult.status === 0) {
    candidates.push(...whereResult.stdout.split(/\r?\n/));
  }

  const programFilesX86 = process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)';
  const vswhere = existingFile(path.join(programFilesX86, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe'));
  if (vswhere) {
    const result = spawnSync(vswhere, [
      '-latest',
      '-products', '*',
      '-requires', 'Microsoft.Component.MSBuild',
      '-find', 'MSBuild\\**\\Bin\\MSBuild.exe'
    ], { encoding: 'utf8', windowsHide: true });
    if (result.status === 0) {
      candidates.push(...result.stdout.split(/\r?\n/));
    }
  }

  const programFiles = process.env.ProgramFiles || 'C:\\Program Files';
  for (const year of ['2022', '2019']) {
    for (const edition of ['Community', 'BuildTools', 'Professional', 'Enterprise']) {
      candidates.push(path.join(programFiles, 'Microsoft Visual Studio', year, edition, 'MSBuild', 'Current', 'Bin', 'MSBuild.exe'));
      candidates.push(path.join(programFilesX86, 'Microsoft Visual Studio', year, edition, 'MSBuild', 'Current', 'Bin', 'MSBuild.exe'));
    }
  }

  for (const candidate of candidates) {
    const resolved = existingFile(candidate);
    if (resolved) {
      return resolved;
    }
  }
  throw new Error('未找到 MSBuild.exe。请安装 Visual Studio 2022 C++ 桌面开发工具，或设置 MSBUILD_EXE_PATH。');
}

function discoverServerProjects(searchRoot, maximumDepth = SERVER_PROJECT_SEARCH_DEPTH) {
  const resolvedRoot = path.resolve(searchRoot);
  if (!fs.existsSync(resolvedRoot) || !fs.statSync(resolvedRoot).isDirectory()) return [];
  const queue = [{ directory: resolvedRoot, depth: 0 }];
  const matches = [];
  while (queue.length > 0) {
    const { directory, depth } = queue.shift();
    let entries;
    try {
      entries = fs.readdirSync(directory, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const entry of entries) {
      if (entry.isFile() && entry.name.toLowerCase() === 'server.vcxproj' &&
          fs.existsSync(path.join(directory, 'LauncherRuntimeMaterial.cpp')) &&
          fs.existsSync(path.join(directory, 'UserServer.cpp'))) {
        matches.push(path.join(directory, entry.name));
        continue;
      }
      if (depth >= maximumDepth || !entry.isDirectory() || entry.isSymbolicLink() ||
          SERVER_PROJECT_SEARCH_SKIP.has(entry.name.toLowerCase())) {
        continue;
      }
      queue.push({ directory: path.join(directory, entry.name), depth: depth + 1 });
    }
  }
  return matches.sort((left, right) => left.localeCompare(right));
}

function resolveSpeedServerProject() {
  const configured = String(process.env.QQS_SPEED_SERVER_PROJECT || '').trim();
  if (configured) {
    const candidate = existingFile(configured);
    if (candidate) return candidate;
    throw new Error(`QQS_SPEED_SERVER_PROJECT 指定的项目不存在：${path.resolve(configured)}`);
  }
  const candidates = discoverServerProjects(defaultServerProjectSearchRoot);
  if (candidates.length !== 1) {
    throw new Error(
      `自动发现到 ${candidates.length} 个部署用 Server.vcxproj；` +
      '请通过 QQS_SPEED_SERVER_PROJECT 明确指定。构建不绑定盘符或客户端安装目录。'
    );
  }
  return candidates[0];
}

function resolveSpeedServerBuildPaths(serverProject) {
  const projectPath = path.resolve(serverProject);
  const projectDir = path.dirname(projectPath);
  const buildDir = path.join(projectDir, 'build');
  const releaseMaterialDir = path.join(buildDir, 'release-material');
  const outputDir = path.join(projectDir, 'bin');
  const projectName = path.basename(projectPath, path.extname(projectPath));
  const serverFileName = projectName.toLowerCase() === 'server'
    ? 'QQSpeedServer.exe'
    : 'SpeedServer.exe';
  return {
    projectPath,
    projectDir,
    buildDir,
    outputDir,
    objectDir: path.join(buildDir, 'obj', `${projectName}-x64`, 'Release'),
    releaseMaterialDir,
    releaseMaterialPath: path.join(releaseMaterialDir, GENERATED_RELEASE_INCLUDE),
    serverFileName,
    serverPath: path.join(outputDir, serverFileName)
  };
}

function resolveReleaseCandidateBundlePath(bundlePath = runtimeBundlePath, identity = crypto.randomUUID()) {
  const resolvedBundle = path.resolve(bundlePath);
  const safeIdentity = String(identity || '').trim();
  if (!/^[0-9A-Za-z-]+$/.test(safeIdentity)) {
    throw new Error('Runtime bundle candidate identity contains unsupported characters.');
  }
  return path.join(
    path.dirname(resolvedBundle),
    `.${path.basename(resolvedBundle)}.${safeIdentity}.candidate`
  );
}

function runMsBuild(msbuild, args, cwd, label) {
  const result = spawnSync(msbuild, args, {
    cwd,
    encoding: 'utf8',
    stdio: 'inherit',
    windowsHide: true
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error(`${label} MSBuild 失败，退出码 ${String(result.status)}。`);
  }
}

function verifySpeedServerOutput(serverExe, bundleId, buildStartedAt = 0) {
  if (!fs.existsSync(serverExe) || !fs.statSync(serverExe).isFile()) {
    throw new Error(`服务端构建输出缺失：${serverExe}`);
  }
  const stat = fs.statSync(serverExe);
  if (buildStartedAt && stat.mtimeMs < buildStartedAt - 5000) {
    throw new Error('服务端 EXE 时间戳早于本次构建。');
  }
  const bytes = fs.readFileSync(serverExe);
  try {
    if (bytes.length < 0x100 || bytes.toString('ascii', 0, 2) !== 'MZ') {
      throw new Error('服务端 EXE 不是有效的 PE 文件。');
    }
    const peOffset = bytes.readUInt32LE(0x3c);
    if (peOffset + 24 > bytes.length || bytes.toString('ascii', peOffset, peOffset + 4) !== 'PE\0\0' ||
        bytes.readUInt16LE(peOffset + 4) !== 0x8664 || bytes.readUInt16LE(peOffset + 24) !== 0x20b ||
        (bytes.readUInt16LE(peOffset + 22) & 0x2000) !== 0) {
      throw new Error('服务端 EXE 必须为 x64 PE32+ 可执行文件。');
    }
    if (bytes.indexOf(bundleId) < 0) {
      throw new Error('服务端 EXE 未内嵌稳定的运行材料。');
    }
    const forbiddenAscii = Buffer.from('LauncherRuntimeKeyring.bin', 'ascii');
    const forbiddenUtf16 = Buffer.from('LauncherRuntimeKeyring.bin', 'utf16le');
    if (bytes.indexOf(forbiddenAscii) >= 0 || bytes.indexOf(forbiddenUtf16) >= 0) {
      throw new Error('服务端 EXE 仍包含 LauncherRuntimeKeyring.bin 依赖。');
    }
    return {
      path: serverExe,
      size: bytes.length,
      sha256: crypto.createHash('sha256').update(bytes).digest('hex')
    };
  } finally {
    bytes.fill(0);
  }
}

function assertNoFreshReleaseDebugArtifacts(outputDir, buildStartedAt) {
  for (const entry of fs.readdirSync(outputDir, { withFileTypes: true })) {
    if (!FORBIDDEN_RELEASE_DEBUG_EXTENSIONS.has(path.extname(entry.name).toLowerCase())) continue;
    if (!entry.isFile() || entry.isSymbolicLink()) {
      throw new Error(`SpeedServer bin 包含非普通调试制品：${entry.name}`);
    }
    const artifact = path.join(outputDir, entry.name);
    if (fs.statSync(artifact).mtimeMs >= buildStartedAt - 5000) {
      throw new Error(`SpeedServer 本次构建向 bin 写入了禁止的调试制品：${artifact}`);
    }
  }
}

function buildSpeedServer(msbuild, serverProject, includeDir, outDir, bundleId, objectDir) {
  const defaults = resolveSpeedServerBuildPaths(serverProject);
  const resolvedOutputDir = path.resolve(outDir || defaults.outputDir);
  const resolvedObjectDir = path.resolve(objectDir || defaults.objectDir);
  if (resolvedObjectDir === resolvedOutputDir ||
      resolvedObjectDir.startsWith(`${resolvedOutputDir}${path.sep}`)) {
    throw new Error('SpeedServer 中间文件目录不得位于 bin 输出目录。');
  }
  fs.mkdirSync(resolvedOutputDir, { recursive: true });
  fs.mkdirSync(resolvedObjectDir, { recursive: true });
  const buildStartedAt = Date.now();
  runMsBuild(msbuild, [
    serverProject,
    '/nologo',
    '/clp:ErrorsOnly;Summary',
    '/m:1',
    '/t:Rebuild',
    '/p:Configuration=Release',
    '/p:Platform=x64',
    `/p:QQSReleaseMaterialIncludeDir=${includeDir}`,
    `/p:OutDir=${resolvedOutputDir}${path.sep}`,
    `/p:IntDir=${resolvedObjectDir}${path.sep}`
  ], path.dirname(serverProject), path.basename(serverProject));
  const identity = verifySpeedServerOutput(
    path.join(resolvedOutputDir, defaults.serverFileName),
    bundleId,
    buildStartedAt
  );
  assertNoFreshReleaseDebugArtifacts(resolvedOutputDir, buildStartedAt);
  return identity;
}

function publishRuntimeBundle(candidate) {
  const backup = `${runtimeBundlePath}.${process.pid}.previous`;
  if (fs.existsSync(backup)) {
    throw new Error(`拒绝覆盖遗留 runtime bundle 备份：${backup}`);
  }
  let previousMoved = false;
  let published = false;
  try {
    if (fs.existsSync(runtimeBundlePath)) {
      fs.renameSync(runtimeBundlePath, backup);
      previousMoved = true;
    }
    fs.renameSync(candidate, runtimeBundlePath);
    published = true;
  } finally {
    if (!published && previousMoved && !fs.existsSync(runtimeBundlePath) && fs.existsSync(backup)) {
      fs.renameSync(backup, runtimeBundlePath);
      previousMoved = false;
    }
    if (published && previousMoved) {
      try {
        fs.unlinkSync(backup);
      } catch (error) {
        console.warn(`[release] previous bundle cleanup deferred: ${error?.message || String(error)}`);
      }
    }
  }
}

function snapshotRegularFile(filePath, backupPath) {
  try {
    const stat = fs.lstatSync(filePath);
    if (!stat.isFile() || stat.isSymbolicLink()) {
      throw new Error(`拒绝备份非普通文件：${filePath}`);
    }
    fs.copyFileSync(filePath, backupPath);
    return true;
  } catch (error) {
    if (error?.code === 'ENOENT') return false;
    throw error;
  }
}

function restoreFileSnapshot(filePath, backupPath, existed) {
  if (existed) {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    fs.copyFileSync(backupPath, filePath);
    return;
  }
  try {
    const stat = fs.lstatSync(filePath);
    if (!stat.isFile() || stat.isSymbolicLink()) {
      throw new Error(`拒绝清理非普通失败制品：${filePath}`);
    }
    fs.unlinkSync(filePath);
  } catch (error) {
    if (error?.code !== 'ENOENT') throw error;
  }
}

function writeGeneratedReleaseMaterial(filePath, source) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  const temporary = `${filePath}.${process.pid}.tmp`;
  if (fs.existsSync(temporary)) {
    throw new Error(`拒绝覆盖遗留发布材料临时文件：${temporary}`);
  }
  try {
    fs.writeFileSync(temporary, source, { flag: 'wx', mode: 0o600 });
    try {
      const stat = fs.lstatSync(filePath);
      if (!stat.isFile() || stat.isSymbolicLink()) {
        throw new Error(`拒绝替换非普通发布材料：${filePath}`);
      }
      fs.unlinkSync(filePath);
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
    }
    fs.renameSync(temporary, filePath);
  } finally {
    try {
      fs.unlinkSync(temporary);
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
    }
  }
}

function removeTemporaryReleaseDirectory(directory) {
  const resolved = path.resolve(directory);
  const temporaryRoot = path.resolve(os.tmpdir());
  if (path.dirname(resolved) !== temporaryRoot ||
      !path.basename(resolved).startsWith('qqspeed-offline-release-')) {
    throw new Error(`拒绝清理未验证的临时发布目录：${resolved}`);
  }
  fs.rmSync(resolved, { recursive: true, force: true });
}

function removeReleaseCandidateBundle(candidate) {
  const resolved = path.resolve(candidate);
  const expectedDirectory = path.dirname(runtimeBundlePath);
  const expectedPrefix = `.${path.basename(runtimeBundlePath)}.`;
  if (path.dirname(resolved) !== expectedDirectory ||
      !path.basename(resolved).startsWith(expectedPrefix) ||
      !path.basename(resolved).endsWith('.candidate')) {
    throw new Error(`拒绝清理未验证的 runtime bundle 候选文件：${resolved}`);
  }
  try {
    const stat = fs.lstatSync(resolved);
    if (!stat.isFile() || stat.isSymbolicLink()) {
      throw new Error(`拒绝清理非普通 runtime bundle 候选文件：${resolved}`);
    }
    fs.unlinkSync(resolved);
  } catch (error) {
    if (error?.code !== 'ENOENT') throw error;
  }
}

function rvaToOffset(rva, sections) {
  for (const section of sections) {
    const size = Math.max(section.virtualSize, section.rawSize);
    if (rva >= section.virtualAddress && rva < section.virtualAddress + size) {
      return section.rawPointer + (rva - section.virtualAddress);
    }
  }
  throw new Error(`PE RVA 0x${rva.toString(16)} 不在任何节内。`);
}

function readAsciiZ(buffer, offset) {
  let end = offset;
  while (end < buffer.length && buffer[end] !== 0) {
    end += 1;
  }
  if (end >= buffer.length) {
    throw new Error('PE 导出名称未正确终止。');
  }
  return buffer.toString('ascii', offset, end);
}

function readPeInput(input, fallbackName = 'QQSpeedNativeBridge.dll') {
  if (Buffer.isBuffer(input)) {
    return { buffer: input, label: fallbackName };
  }
  if (input instanceof Uint8Array) {
    return {
      buffer: Buffer.from(input.buffer, input.byteOffset, input.byteLength),
      label: fallbackName
    };
  }
  const filePath = path.resolve(String(input || ''));
  return { buffer: fs.readFileSync(filePath), label: path.basename(filePath) };
}

function verifyManualMapHookInput(input = hookInputPath) {
  const { buffer, label } = readPeInput(input, 'qqspeed_clean_hook_64.dll');
  const reject = (reason) => {
    throw new Error(`${label} 不满足远程 ManualMap 契约：${reason}`);
  };
  if (buffer.length < 0x100 || buffer.length > MAXIMUM_MANUAL_MAP_IMAGE_BYTES ||
      buffer.toString('ascii', 0, 2) !== 'MZ') {
    reject('文件大小或 DOS 头无效');
  }

  const peOffset = buffer.readUInt32LE(0x3c);
  if (peOffset + 24 > buffer.length || buffer.toString('ascii', peOffset, peOffset + 4) !== 'PE\0\0') {
    reject('PE 头无效');
  }
  const sectionCount = buffer.readUInt16LE(peOffset + 6);
  const optionalSize = buffer.readUInt16LE(peOffset + 20);
  const characteristics = buffer.readUInt16LE(peOffset + 22);
  const optionalOffset = peOffset + 24;
  if (buffer.readUInt16LE(peOffset + 4) !== 0x8664 ||
      (characteristics & 0x2000) === 0 ||
      optionalSize < 0xf0 || optionalOffset + optionalSize > buffer.length ||
      buffer.readUInt16LE(optionalOffset) !== 0x20b) {
    reject('必须是 x64 PE32+ DLL');
  }

  const entryRva = buffer.readUInt32LE(optionalOffset + 16);
  const sectionAlignment = buffer.readUInt32LE(optionalOffset + 32);
  const sizeOfImage = buffer.readUInt32LE(optionalOffset + 56);
  const sizeOfHeaders = buffer.readUInt32LE(optionalOffset + 60);
  const numberOfDirectories = buffer.readUInt32LE(optionalOffset + 108);
  if (sectionAlignment < 0x1000 || sizeOfImage === 0 ||
      sizeOfImage > MAXIMUM_MANUAL_MAP_IMAGE_BYTES || sizeOfHeaders > buffer.length ||
      sectionCount === 0 || sectionCount > 96 || numberOfDirectories < 16) {
    reject('映像尺寸、节数或数据目录超出映射器限制');
  }

  const sectionTable = optionalOffset + optionalSize;
  if (sectionTable + sectionCount * 40 > buffer.length) {
    reject('节表越界');
  }
  const sections = [];
  for (let index = 0; index < sectionCount; index += 1) {
    const offset = sectionTable + index * 40;
    const section = {
      name: buffer.toString('ascii', offset, offset + 8).replace(/\0+$/, ''),
      virtualSize: buffer.readUInt32LE(offset + 8),
      virtualAddress: buffer.readUInt32LE(offset + 12),
      rawSize: buffer.readUInt32LE(offset + 16),
      rawPointer: buffer.readUInt32LE(offset + 20),
      characteristics: buffer.readUInt32LE(offset + 36)
    };
    const mappedSize = Math.max(section.virtualSize, section.rawSize);
    if (mappedSize > sizeOfImage || section.virtualAddress > sizeOfImage - mappedSize ||
        (section.rawSize > 0 &&
         (section.rawPointer > buffer.length || section.rawSize > buffer.length - section.rawPointer))) {
      reject(`节 ${section.name || index} 越界`);
    }
    sections.push(section);
  }

  const rawOffsetForRange = (rva, size, description) => {
    for (const section of sections) {
      if (rva < section.virtualAddress) continue;
      const delta = rva - section.virtualAddress;
      if (delta <= section.rawSize && size <= section.rawSize - delta) {
        return section.rawPointer + delta;
      }
    }
    reject(`${description} 未由文件原始数据覆盖`);
  };
  const directory = (index) => ({
    rva: buffer.readUInt32LE(optionalOffset + 112 + index * 8),
    size: buffer.readUInt32LE(optionalOffset + 116 + index * 8)
  });

  const delayImports = directory(13);
  if (delayImports.rva !== 0 || delayImports.size !== 0) {
    reject('延迟导入不受支持');
  }
  const tls = directory(9);
  if (tls.rva !== 0 || tls.size !== 0) {
    reject('正式 Hook 必须使用无持久静态 TLS 的 FLS 构建');
  }

  const textSection = sections.find((section) => section.name === '.text');
  if (!textSection || textSection.rawSize !== 0) {
    reject('当前文件不是已加密的 VMProtect 输出');
  }
  const entrySection = sections.find((section) => {
    const delta = entryRva - section.virtualAddress;
    return entryRva >= section.virtualAddress && delta < section.rawSize &&
      (section.characteristics & 0x20000000) !== 0;
  });
  if (!entrySection) {
    reject('VMProtect 入口点未由可执行原始数据覆盖');
  }

  const exception = directory(3);
  if (!exception.rva || exception.size < 12 || exception.size % 12 !== 0 ||
      exception.rva > sizeOfImage || exception.size > sizeOfImage - exception.rva) {
    reject('x64 unwind 目录缺失或形式错误');
  }
  const exceptionOffset = rawOffsetForRange(exception.rva, exception.size, 'x64 unwind 目录');
  for (let offset = 0; offset < exception.size; offset += 12) {
    const begin = buffer.readUInt32LE(exceptionOffset + offset);
    const end = buffer.readUInt32LE(exceptionOffset + offset + 4);
    const unwind = buffer.readUInt32LE(exceptionOffset + offset + 8);
    if (begin >= end || end > sizeOfImage || unwind >= sizeOfImage) {
      reject(`x64 unwind 条目 ${offset / 12} 越界`);
    }
  }

  const relocations = directory(5);
  if (!relocations.rva || relocations.size < 8 ||
      relocations.rva > sizeOfImage || relocations.size > sizeOfImage - relocations.rva) {
    reject('DIR64 重定位目录缺失或越界');
  }
  const relocationOffset = rawOffsetForRange(relocations.rva, relocations.size, 'DIR64 重定位目录');
  let consumed = 0;
  while (consumed < relocations.size) {
    if (relocations.size - consumed < 8) reject('重定位块头不完整');
    const pageRva = buffer.readUInt32LE(relocationOffset + consumed);
    const blockSize = buffer.readUInt32LE(relocationOffset + consumed + 4);
    if (blockSize < 8 || blockSize > relocations.size - consumed || (blockSize - 8) % 2 !== 0) {
      reject('重定位块大小无效');
    }
    for (let offset = 8; offset < blockSize; offset += 2) {
      const relocation = buffer.readUInt16LE(relocationOffset + consumed + offset);
      const type = relocation >>> 12;
      const patchRva = pageRva + (relocation & 0x0fff);
      if (type !== 0 && (type !== 10 || patchRva > sizeOfImage - 8)) {
        reject(`不支持的重定位条目 type=${type}`);
      }
    }
    consumed += blockSize;
  }

  const importsDirectory = directory(1);
  if (importsDirectory.rva || importsDirectory.size) {
    if (!importsDirectory.rva || importsDirectory.size < 20 ||
        importsDirectory.rva > sizeOfImage || importsDirectory.size > sizeOfImage - importsDirectory.rva) {
      reject('导入目录缺失或越界');
    }
    const importsOffset = rawOffsetForRange(importsDirectory.rva, importsDirectory.size, '导入目录');
    let terminated = false;
    for (let offset = 0; offset + 20 <= importsDirectory.size; offset += 20) {
      const descriptor = buffer.subarray(importsOffset + offset, importsOffset + offset + 20);
      if (descriptor.every((value) => value === 0)) {
        terminated = true;
        break;
      }
    }
    if (!terminated) reject('导入描述符未在目录范围内终止');
  }
  const imports = inspectPeImports(buffer);
  if ([...imports.keys()].some((moduleName) => moduleName.toLowerCase() === 'vmprotectsdk64.dll')) {
    reject('检测到 VMProtectSDK64.dll，这是未保护的 ReleaseVmp 中间产物');
  }

  return {
    size: buffer.length,
    imageSize: sizeOfImage,
    sectionCount,
    exceptionCount: exception.size / 12,
    entrySection: entrySection.name,
    importModules: [...imports.keys()]
  };
}

function verifyPinnedHookIdentity(sourceHookPath = hookInputPath) {
  const source = fs.readFileSync(sourceHookPath);
  const digest = crypto.createHash('sha256').update(source).digest('hex');
  if (digest !== EXPECTED_HOOK_SHA256) {
    throw new Error('Hook DLL SHA256 does not match the fixed/pinned VMProtect release identity.');
  }
  const manualMap = verifyManualMapHookInput(source);
  return { size: source.length, sha256: digest, manualMap };
}

function inspectPeExports(dllPath) {
  const { buffer } = readPeInput(dllPath);
  if (buffer.length < 0x100 || buffer.toString('ascii', 0, 2) !== 'MZ') {
    throw new Error('QQSpeedNativeBridge.dll 不是有效的 PE 文件。');
  }
  const peOffset = buffer.readUInt32LE(0x3c);
  if (peOffset + 0x108 > buffer.length || buffer.toString('ascii', peOffset, peOffset + 4) !== 'PE\0\0') {
    throw new Error('QQSpeedNativeBridge.dll 的 PE 头无效。');
  }
  const machine = buffer.readUInt16LE(peOffset + 4);
  if (machine !== 0x8664) {
    throw new Error(`QQSpeedNativeBridge.dll 必须为 x64，当前 Machine=0x${machine.toString(16)}。`);
  }

  const sectionCount = buffer.readUInt16LE(peOffset + 6);
  const optionalSize = buffer.readUInt16LE(peOffset + 20);
  const optionalOffset = peOffset + 24;
  if (buffer.readUInt16LE(optionalOffset) !== 0x20b) {
    throw new Error('QQSpeedNativeBridge.dll 必须使用 PE32+ 格式。');
  }
  const exportRva = buffer.readUInt32LE(optionalOffset + 112);
  if (!exportRva) {
    throw new Error('QQSpeedNativeBridge.dll 没有导出表。');
  }

  const sections = [];
  const sectionTable = optionalOffset + optionalSize;
  for (let index = 0; index < sectionCount; index += 1) {
    const offset = sectionTable + index * 40;
    sections.push({
      virtualSize: buffer.readUInt32LE(offset + 8),
      virtualAddress: buffer.readUInt32LE(offset + 12),
      rawSize: buffer.readUInt32LE(offset + 16),
      rawPointer: buffer.readUInt32LE(offset + 20)
    });
  }

  const exportOffset = rvaToOffset(exportRva, sections);
  const nameCount = buffer.readUInt32LE(exportOffset + 24);
  const namesRva = buffer.readUInt32LE(exportOffset + 32);
  const namesOffset = rvaToOffset(namesRva, sections);
  const exports = new Set();
  for (let index = 0; index < nameCount; index += 1) {
    const nameRva = buffer.readUInt32LE(namesOffset + index * 4);
    exports.add(readAsciiZ(buffer, rvaToOffset(nameRva, sections)));
  }
  return exports;
}

function inspectPeImports(dllPath) {
  const { buffer } = readPeInput(dllPath);
  if (buffer.length < 0x100 || buffer.toString('ascii', 0, 2) !== 'MZ') {
    throw new Error('QQSpeedNativeBridge.dll 不是有效的 PE 文件。');
  }
  const peOffset = buffer.readUInt32LE(0x3c);
  if (peOffset + 0x108 > buffer.length || buffer.toString('ascii', peOffset, peOffset + 4) !== 'PE\0\0') {
    throw new Error('QQSpeedNativeBridge.dll 的 PE 头无效。');
  }
  const machine = buffer.readUInt16LE(peOffset + 4);
  if (machine !== 0x8664) {
    throw new Error(`QQSpeedNativeBridge.dll 必须为 x64，当前 Machine=0x${machine.toString(16)}。`);
  }

  const sectionCount = buffer.readUInt16LE(peOffset + 6);
  const optionalSize = buffer.readUInt16LE(peOffset + 20);
  const optionalOffset = peOffset + 24;
  if (buffer.readUInt16LE(optionalOffset) !== 0x20b) {
    throw new Error('QQSpeedNativeBridge.dll 必须使用 PE32+ 格式。');
  }

  const sections = [];
  const sectionTable = optionalOffset + optionalSize;
  for (let index = 0; index < sectionCount; index += 1) {
    const offset = sectionTable + index * 40;
    sections.push({
      virtualSize: buffer.readUInt32LE(offset + 8),
      virtualAddress: buffer.readUInt32LE(offset + 12),
      rawSize: buffer.readUInt32LE(offset + 16),
      rawPointer: buffer.readUInt32LE(offset + 20)
    });
  }

  const importRva = buffer.readUInt32LE(optionalOffset + 120);
  const imports = new Map();
  if (!importRva) {
    return imports;
  }

  let descriptorOffset = rvaToOffset(importRva, sections);
  for (;;) {
    if (descriptorOffset + 20 > buffer.length) {
      throw new Error('QQSpeedNativeBridge.dll 的 PE 导入表越界。');
    }
    const originalFirstThunk = buffer.readUInt32LE(descriptorOffset);
    const timeDateStamp = buffer.readUInt32LE(descriptorOffset + 4);
    const forwarderChain = buffer.readUInt32LE(descriptorOffset + 8);
    const nameRva = buffer.readUInt32LE(descriptorOffset + 12);
    const firstThunk = buffer.readUInt32LE(descriptorOffset + 16);
    if (!(originalFirstThunk || timeDateStamp || forwarderChain || nameRva || firstThunk)) {
      break;
    }

    const moduleName = readAsciiZ(buffer, rvaToOffset(nameRva, sections));
    const names = new Set();
    const thunkRva = originalFirstThunk || firstThunk;
    let thunkOffset = rvaToOffset(thunkRva, sections);
    for (;;) {
      if (thunkOffset + 8 > buffer.length) {
        throw new Error(`QQSpeedNativeBridge.dll 的 ${moduleName} 导入 thunk 越界。`);
      }
      const thunk = buffer.readBigUInt64LE(thunkOffset);
      if (thunk === 0n) {
        break;
      }
      if ((thunk & 0x8000000000000000n) !== 0n) {
        names.add(`#${Number(thunk & 0xffffn)}`);
      } else {
        const importByNameRva = Number(thunk & 0x7fffffffffffffffn);
        const importByNameOffset = rvaToOffset(importByNameRva, sections);
        names.add(readAsciiZ(buffer, importByNameOffset + 2));
      }
      thunkOffset += 8;
    }
    imports.set(moduleName, names);
    descriptorOffset += 20;
  }
  return imports;
}

function assertNativeImportPolicy(imports) {
  const violations = [];
  for (const [moduleName, names] of imports) {
    const normalizedModule = moduleName.toLowerCase();
    if (FORBIDDEN_NATIVE_IMPORT_MODULES.has(normalizedModule)) {
      violations.push(moduleName);
    }
    for (const name of names) {
      const normalizedName = name.toLowerCase();
      if (FORBIDDEN_NATIVE_IMPORT_NAMES.has(normalizedName) ||
          FORBIDDEN_NATIVE_IMPORT_PREFIXES.some((prefix) => normalizedName.startsWith(prefix))) {
        violations.push(`${moduleName}!${name}`);
      }
    }
  }
  if (violations.length > 0) {
    throw new Error(`QQSpeedNativeBridge.dll 包含禁止的网络/密码学导入：${violations.join(', ')}`);
  }
  return imports;
}

function verifyNativeImports(dllPath) {
  return assertNativeImportPolicy(inspectPeImports(dllPath));
}

function verifyNativeOutput(dllInput = outputPath, buildStartedAt = 0) {
  if (!fs.existsSync(hookInputPath) || !fs.statSync(hookInputPath).isFile()) {
    throw new Error(`Hook 构建输入缺失：${hookInputPath}`);
  }
  if (!Buffer.isBuffer(dllInput) && !(dllInput instanceof Uint8Array)) {
    if (!fs.existsSync(dllInput) || !fs.statSync(dllInput).isFile()) {
      throw new Error(`NativeBridge 构建输出缺失：${dllInput}`);
    }
  }
  verifyPinnedHookIdentity(hookInputPath);
  const stat = typeof dllInput === 'string' ? fs.statSync(dllInput) : null;
  if (buildStartedAt && stat && stat.mtimeMs < buildStartedAt - 5000) {
    throw new Error('QQSpeedNativeBridge.dll 时间戳早于本次构建，拒绝继续打包陈旧 DLL。');
  }
  const exports = inspectPeExports(dllInput);
  const missing = requiredExports.filter((name) => !exports.has(name));
  if (missing.length > 0) {
    throw new Error(`QQSpeedNativeBridge.dll 缺少导出：${missing.join(', ')}`);
  }
  verifyNativeImports(dllInput);
  const { buffer } = readPeInput(dllInput);
  const sha256 = crypto.createHash('sha256').update(buffer).digest('hex');
  console.log(`[native] verified x64 QQSpeedNativeBridge.dll sha256=${sha256}`);
  return { size: buffer.length, sha256 };
}

function removeTransientNativeOutput() {
  try {
    fs.unlinkSync(outputPath);
  } catch (error) {
    if (error?.code !== 'ENOENT') throw error;
  }
  try {
    fs.rmdirSync(path.dirname(outputPath));
  } catch (error) {
    if (!['ENOENT', 'ENOTEMPTY'].includes(error?.code)) throw error;
  }
}

function removeLegacyLooseNativeArtifacts() {
  for (const filePath of [legacyLooseBridgePath, legacyRuntimeManifestPath]) {
    try {
      const stat = fs.lstatSync(filePath);
      if (!stat.isFile() || stat.isSymbolicLink()) {
        throw new Error(`拒绝删除非普通旧制品：${filePath}`);
      }
      fs.unlinkSync(filePath);
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
    }
  }
}

function assertNoLegacyLooseNativeArtifacts() {
  for (const filePath of [legacyLooseBridgePath, legacyRuntimeManifestPath]) {
    if (fs.existsSync(filePath)) {
      throw new Error(`旧的裸 NativeBridge 制品仍存在：${filePath}；请运行 npm run build:native 完成迁移。`);
    }
  }
}

async function verifyBundledNativeOutput(options = {}) {
  const { verifyRuntimeBundle } = require('./build-runtime-bundle.cjs');
  const result = await verifyRuntimeBundle({
    ...options,
    output: runtimeBundlePath,
    hookInput: hookInputPath
  });
  console.log(
    `[native] runtime bundle ${result.authenticated ? 'authenticated' : 'structure checked'} ` +
    `bundleId=${result.bundleId} sha256=${result.bundleSha256}`
  );
  return result;
}

async function buildOfflineRelease(msbuild, serverProject, options = {}) {
  const rotateRelease = options.rotateRelease === true;
  const serverPaths = resolveSpeedServerBuildPaths(serverProject);
  const temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'qqspeed-offline-release-'));
  const candidateBundle = resolveReleaseCandidateBundlePath(runtimeBundlePath);
  const previousMaterial = path.join(temporaryRoot, `${GENERATED_RELEASE_INCLUDE}.previous`);
  const previousServer = path.join(temporaryRoot, `${serverPaths.serverFileName}.previous`);
  let materialExisted = false;
  let serverExisted = false;
  let snapshotsReady = false;
  let completed = false;
  let embeddedSource = '';
  let releaseMaterial;
  let contentKey;
  let materialMode = '';
  try {
    if (!rotateRelease) {
      try {
        const stat = fs.lstatSync(serverPaths.releaseMaterialPath);
        if (!stat.isFile() || stat.isSymbolicLink()) {
          throw new Error(`稳定发布材料不是普通文件：${serverPaths.releaseMaterialPath}`);
        }
        embeddedSource = fs.readFileSync(serverPaths.releaseMaterialPath, 'utf8');
        releaseMaterial = parseEmbeddedReleaseSource(embeddedSource);
        materialMode = 'reused';
      } catch (error) {
        if (error?.code !== 'ENOENT') throw error;
      }
    }
    if (!releaseMaterial) {
      releaseMaterial = generateReleaseMaterial();
      materialMode = rotateRelease ? 'rotated' : 'created';
    }
    contentKey = deriveContentKey(releaseMaterial.releaseMaster, releaseMaterial.bundleId);
    const bundleIdHex = releaseMaterial.bundleId.toString('hex');

    materialExisted = snapshotRegularFile(serverPaths.releaseMaterialPath, previousMaterial);
    serverExisted = snapshotRegularFile(serverPaths.serverPath, previousServer);
    snapshotsReady = true;
    if (materialMode !== 'reused') {
      embeddedSource = renderEmbeddedReleaseSource(releaseMaterial);
      writeGeneratedReleaseMaterial(serverPaths.releaseMaterialPath, embeddedSource);
    }
    embeddedSource = '';

    const serverIdentity = buildSpeedServer(
      msbuild,
      serverProject,
      serverPaths.releaseMaterialDir,
      serverPaths.outputDir,
      releaseMaterial.bundleId,
      serverPaths.objectDir
    );

    removeLegacyLooseNativeArtifacts();
    removeTransientNativeOutput();
    const nativeBuildStartedAt = Date.now();
    try {
      runMsBuild(msbuild, [
        projectPath,
        '/nologo',
        '/clp:ErrorsOnly;Summary',
        '/m:1',
        '/t:Rebuild',
        '/p:Configuration=Release',
        '/p:Platform=x64'
      ], root, 'NativeBridge');
      const bridgeIdentity = verifyNativeOutput(outputPath, nativeBuildStartedAt);
      const { buildRuntimeBundle } = require('./build-runtime-bundle.cjs');
      const bundleIdentity = await buildRuntimeBundle({
        input: outputPath,
        hookInput: hookInputPath,
        output: candidateBundle,
        contentKey,
        bundleId: releaseMaterial.bundleId
      });
      if (!bundleIdentity.authenticated || bundleIdentity.bundleId !== bundleIdHex) {
        throw new Error('runtime bundle 与稳定服务端材料不一致。');
      }

      publishRuntimeBundle(candidateBundle);
      completed = true;

      console.log(`[release] stable material ${materialMode}: bundleId=${bundleIdHex}`);
      console.log(`[release] server published: ${serverIdentity.path}`);
      console.log(`[release] local material retained: ${serverPaths.releaseMaterialPath}`);
      console.log(`[release] runtime bundle published: ${runtimeBundlePath}`);
      console.log(
        `[release] serverSha256=${serverIdentity.sha256} bridgeSha256=${bridgeIdentity.sha256} ` +
        `bundleSha256=${bundleIdentity.bundleSha256}`
      );
      return {
        bundleId: bundleIdHex,
        serverPath: serverIdentity.path,
        releaseMaterialPath: serverPaths.releaseMaterialPath,
        runtimeBundlePath,
        serverSha256: serverIdentity.sha256,
        bridgeSha256: bridgeIdentity.sha256,
        bundleSha256: bundleIdentity.bundleSha256
      };
    } finally {
      removeTransientNativeOutput();
    }
  } finally {
    try {
      if (!completed && snapshotsReady) {
        try {
          restoreFileSnapshot(serverPaths.serverPath, previousServer, serverExisted);
        } finally {
          restoreFileSnapshot(serverPaths.releaseMaterialPath, previousMaterial, materialExisted);
        }
      }
    } finally {
      embeddedSource = '';
      releaseMaterial?.releaseMaster.fill(0);
      releaseMaterial?.bundleId.fill(0);
      contentKey?.fill(0);
      try {
        removeReleaseCandidateBundle(candidateBundle);
      } finally {
        removeTemporaryReleaseDirectory(temporaryRoot);
      }
    }
  }
}

async function buildServerOnly(msbuild, serverProject) {
  const serverPaths = resolveSpeedServerBuildPaths(serverProject);
  let releaseMaterialStat;
  try {
    releaseMaterialStat = fs.lstatSync(serverPaths.releaseMaterialPath);
  } catch (error) {
    if (error?.code !== 'ENOENT') throw error;
  }
  if (!releaseMaterialStat?.isFile() || releaseMaterialStat.isSymbolicLink()) {
    throw new Error(
      `缺少可复用的本地发布材料：${serverPaths.releaseMaterialPath}；` +
      '请先运行一次 npm run build:native。'
    );
  }

  let embeddedSource = fs.readFileSync(serverPaths.releaseMaterialPath, 'utf8');
  let releaseMaterial;
  let contentKey;
  let temporaryRoot = '';
  let serverExisted = false;
  let completed = false;
  try {
    releaseMaterial = parseEmbeddedReleaseSource(embeddedSource);
    contentKey = deriveContentKey(releaseMaterial.releaseMaster, releaseMaterial.bundleId);
    const { verifyRuntimeBundle } = require('./build-runtime-bundle.cjs');
    const bundleIdentity = await verifyRuntimeBundle({
      output: runtimeBundlePath,
      hookInput: hookInputPath,
      contentKey,
      bundleId: releaseMaterial.bundleId
    });
    const bundleIdHex = releaseMaterial.bundleId.toString('hex');
    if (!bundleIdentity.authenticated || bundleIdentity.bundleId !== bundleIdHex) {
      throw new Error('本地发布材料与现有 runtime bundle 不匹配。');
    }

    temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'qqspeed-offline-release-'));
    const previousServer = path.join(temporaryRoot, 'SpeedServer.previous.exe');
    serverExisted = snapshotRegularFile(serverPaths.serverPath, previousServer);
    try {
      const serverIdentity = buildSpeedServer(
        msbuild,
        serverProject,
        serverPaths.releaseMaterialDir,
        serverPaths.outputDir,
        releaseMaterial.bundleId,
        serverPaths.objectDir
      );
      completed = true;
      console.log(`[server] bundleId reused: ${bundleIdHex}`);
    console.log(`[server] deployed server rebuilt: ${serverIdentity.path}`);
      console.log(`[server] serverSha256=${serverIdentity.sha256}`);
      return {
        bundleId: bundleIdHex,
        serverPath: serverIdentity.path,
        releaseMaterialPath: serverPaths.releaseMaterialPath,
        serverSha256: serverIdentity.sha256
      };
    } finally {
      if (!completed) {
        restoreFileSnapshot(serverPaths.serverPath, previousServer, serverExisted);
      }
    }
  } finally {
    embeddedSource = '';
    releaseMaterial?.releaseMaster.fill(0);
    releaseMaterial?.bundleId.fill(0);
    contentKey?.fill(0);
    if (temporaryRoot) removeTemporaryReleaseDirectory(temporaryRoot);
  }
}

async function main(argv = process.argv.slice(2)) {
  if (process.platform !== 'win32') {
    throw new Error('QQSpeedNativeBridge.dll 只能在 Windows 上构建。');
  }
  if (!fs.existsSync(projectPath)) {
    throw new Error(`本地组件项目缺失：${projectPath}`);
  }
  verifyPinnedHookIdentity(hookInputPath);
  const unknown = argv.filter((argument) => ![
    '--verify-only',
    '--dry-run',
    '--server-only',
    '--rotate-release'
  ].includes(argument));
  if (unknown.length > 0) {
    throw new Error(`未知参数：${unknown.join(' ')}`);
  }
  if (argv.includes('--verify-only')) {
    assertNoLegacyLooseNativeArtifacts();
    await verifyBundledNativeOutput();
    return;
  }
  const msbuild = locateMsBuild();
  const serverProject = resolveSpeedServerProject();
  if (argv.includes('--server-only') && argv.includes('--rotate-release')) {
    throw new Error('--server-only 不允许轮换运行材料。');
  }
  if (argv.includes('--dry-run')) {
    const paths = resolveSpeedServerBuildPaths(serverProject);
    console.log(`[native] dry-run OK: ${msbuild}`);
    console.log(`[native] project: ${projectPath}`);
    console.log(`[native] deployed server project: ${serverProject}`);
    console.log(`[native] deployed server output: ${paths.serverPath}`);
    console.log(`[native] release material: ${paths.releaseMaterialPath}`);
    return;
  }
  if (argv.includes('--server-only')) {
    return buildServerOnly(msbuild, serverProject);
  }
  const result = await buildOfflineRelease(msbuild, serverProject, {
    rotateRelease: argv.includes('--rotate-release')
  });
  assertNoLegacyLooseNativeArtifacts();
  return result;
}

module.exports = {
  FORBIDDEN_NATIVE_IMPORT_MODULES,
  assertNativeImportPolicy,
  buildOfflineRelease,
  buildServerOnly,
  buildSpeedServer,
  locateMsBuild,
  resolveReleaseCandidateBundlePath,
  resolveSpeedServerProject,
  resolveSpeedServerBuildPaths,
  verifyNativeOutput,
  inspectPeExports,
  inspectPeImports,
  verifyManualMapHookInput,
  verifyNativeImports,
  verifyPinnedHookIdentity,
  verifySpeedServerOutput,
  verifyBundledNativeOutput,
  removeTransientNativeOutput,
  main
};

if (require.main === module) {
  main().catch((error) => {
    console.error(error?.stack || error);
    process.exitCode = 1;
  });
}
