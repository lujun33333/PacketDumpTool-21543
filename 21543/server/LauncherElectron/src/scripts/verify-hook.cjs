const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..', '..');
const nativeDir = path.join(root, 'resources', 'native');
const hookPath = path.join(nativeDir, 'qqspeed_clean_hook_64.dll');
const bundlePath = path.join(nativeDir, 'qqspeed-runtime.bundle');
const legacyBridgePath = path.join(nativeDir, 'QQSpeedNativeBridge.dll');
const legacyManifestPath = path.join(nativeDir, 'runtime-artifacts.json');
const nativeVerifier = require('./build-native.cjs');
const { verifyRuntimeBundle } = require('./build-runtime-bundle.cjs');

function inspectX64Dll(input, fallbackName = 'QQSpeedNativeBridge.dll') {
  const filePath = typeof input === 'string' ? input : '';
  const buffer = Buffer.isBuffer(input) ? input : fs.readFileSync(filePath);
  const label = filePath ? path.basename(filePath) : fallbackName;
  if (buffer.length < 0x100 || buffer.toString('ascii', 0, 2) !== 'MZ') {
    throw new Error(`${label} 不是有效的 PE 文件。`);
  }
  const peOffset = buffer.readUInt32LE(0x3c);
  if (peOffset + 24 > buffer.length || buffer.toString('ascii', peOffset, peOffset + 4) !== 'PE\0\0') {
    throw new Error(`${label} 的 PE 头无效。`);
  }
  const machine = buffer.readUInt16LE(peOffset + 4);
  const optionalHeaderOffset = peOffset + 24;
  const characteristics = buffer.readUInt16LE(peOffset + 22);
  if (machine !== 0x8664 || buffer.readUInt16LE(optionalHeaderOffset) !== 0x20b) {
    throw new Error(`${label} 必须为 x64 PE32+。`);
  }
  if ((characteristics & 0x2000) === 0) {
    throw new Error(`${label} 不是 DLL。`);
  }
  return buffer;
}

function sha256(buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

function assertNoLegacyLooseArtifacts() {
  for (const filePath of [legacyBridgePath, legacyManifestPath]) {
    if (fs.existsSync(filePath)) {
      throw new Error(`资源目录仍包含已退役的裸原生制品：${filePath}`);
    }
  }
}

async function verifyRuntimeArtifacts() {
  assertNoLegacyLooseArtifacts();
  if (!fs.existsSync(hookPath) || !fs.statSync(hookPath).isFile()) {
    throw new Error(`固定 VMProtect Hook 构建输入缺失：${hookPath}`);
  }
  if (!fs.existsSync(bundlePath) || !fs.statSync(bundlePath).isFile()) {
    throw new Error(`认证 runtime bundle 缺失：${bundlePath}`);
  }

  const hookBytes = inspectX64Dll(hookPath, 'qqspeed_clean_hook_64.dll');
  const hookIdentity = nativeVerifier.verifyPinnedHookIdentity(hookPath);
  const environmentHash = String(process.env.QQSPEED_HOOK_SHA256 || '').trim().toLowerCase();
  if (environmentHash && !/^[0-9a-f]{64}$/.test(environmentHash)) {
    throw new Error('Hook DLL 期望 SHA256 无效。');
  }
  if (environmentHash && environmentHash !== hookIdentity.sha256) {
    throw new Error('环境指定的 Hook SHA256 与固定构建输入不一致。');
  }

  const inspected = await verifyRuntimeBundle({
    output: bundlePath,
    hookInput: hookPath
  });
  const verified = [
      {
        name: 'QQSpeedNativeBridge.dll',
        path: `${bundlePath}#QQSpeedNativeBridge.dll`,
        size: inspected.bridgeSize,
        sha256: inspected.bridgeSha256,
        mode: 'descriptor'
      },
      {
        name: 'qqspeed_clean_hook_64.dll',
        path: hookPath,
        size: hookBytes.length,
        sha256: sha256(hookBytes),
        mode: 'verified'
      },
      {
        name: 'qqspeed-runtime.bundle',
        path: bundlePath,
        size: fs.statSync(bundlePath).size,
        sha256: inspected.bundleSha256,
        mode: 'structure'
      }
  ];
  for (const artifact of verified) {
    console.log(`[runtime] ${artifact.mode} ${artifact.name} size=${artifact.size} sha256=${artifact.sha256}`);
  }
  return verified;
}

async function verifyHookArtifact() {
  const verified = await verifyRuntimeArtifacts();
  const hook = verified.find((file) => file.name === 'qqspeed_clean_hook_64.dll');
  return { hookPath: hook.path, size: hook.size, sha256: hook.sha256 };
}

async function main() {
  if (process.platform !== 'win32') {
    throw new Error('Hook DLL 仅支持 Windows x64 发布校验。');
  }
  await verifyHookArtifact();
}

module.exports = { inspectX64Dll, verifyRuntimeArtifacts, verifyHookArtifact, main };

if (require.main === module) {
  main().catch((error) => {
    console.error(error?.stack || error);
    process.exitCode = 1;
  });
}
