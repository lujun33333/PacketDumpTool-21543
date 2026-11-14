const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..', '..');
const packageJson = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
const verifierPath = path.join(root, 'src', 'scripts', 'verify-hook.cjs');
const bundleBuilderPath = path.join(root, 'src', 'scripts', 'build-runtime-bundle.cjs');
const hookPath = path.join(root, 'resources', 'native', 'qqspeed_clean_hook_64.dll');
const bundlePath = path.join(root, 'resources', 'native', 'qqspeed-runtime.bundle');
const legacyBridgePath = path.join(root, 'resources', 'native', 'QQSpeedNativeBridge.dll');
const legacyManifestPath = path.join(root, 'resources', 'native', 'runtime-artifacts.json');

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function sha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function snapshot(filePath) {
  const stat = fs.statSync(filePath);
  return {
    size: stat.size,
    mtimeMs: stat.mtimeMs,
    sha256: sha256(filePath)
  };
}

function runNode(scriptPath, args = [], env = process.env) {
  return spawnSync(process.execPath, [scriptPath, ...args], {
    cwd: root,
    encoding: 'utf8',
    windowsHide: true,
    env
  });
}

assert(fs.existsSync(verifierPath), 'Hook validator must be named verify-hook.cjs.');
assert(fs.existsSync(bundleBuilderPath), 'Runtime bundle builder is missing.');
assert(fs.existsSync(hookPath), 'Build-time Hook input is missing.');
assert(fs.existsSync(bundlePath), 'Authenticated runtime bundle is missing; run npm run build:native.');
assert(!fs.existsSync(legacyBridgePath), 'Source resources must not retain a loose NativeBridge build output.');
assert(!fs.existsSync(legacyManifestPath), 'Source resources must not retain the legacy runtime manifest.');

const verifierSource = fs.readFileSync(verifierPath, 'utf8');
assert(!verifierSource.includes('copyFileSync'), 'Hook validation must never copy a DLL.');
assert(!verifierSource.includes('qqspeed_clean_hook - \u526f\u672c'), 'Hook validation still references the legacy Hook directory.');
assert(!verifierSource.includes('spawnSync'), 'Hook validation must not invoke MSBuild or another build process.');

for (const lifecycle of ['prepack', 'predist', 'predist:portable', 'predist:all']) {
  assert(packageJson.scripts[lifecycle] === 'npm run verify:runtime', `${lifecycle} must be validation-only.`);
}
assert(!packageJson.scripts['build:runtime-bundle'],
  'Runtime bundle creation must be owned atomically by build:native.');
assert(packageJson.scripts['verify:runtime-bundle'] === 'node src/scripts/build-runtime-bundle.cjs --verify-only',
  'Runtime bundle verify-only command is missing.');
assert(
  packageJson.scripts['build:native'] === 'node src/scripts/build-native.cjs',
  'NativeBridge build and bundle creation must use one cleanup-owning command.'
);
assert(!packageJson.scripts['build:hook'], 'build:hook must not exist in the Electron packaging project.');
assert(!packageJson.scripts['build:runtime'], 'The retired loose runtime build command must not exist.');

const packagedFiles = packageJson.build?.files || [];
assert(!Object.hasOwn(packageJson.build || {}, 'extraResources'),
  'Loose native DLL extraResources must be removed.');
assert(packagedFiles.includes('resources/native/qqspeed-runtime.bundle'),
  'The runtime bundle must be included inside app.asar.');
assert(!packagedFiles.includes('resources/native/runtime-artifacts.json'),
  'The legacy loose runtime manifest must not be packaged.');
assert(!packagedFiles.some((entry) => /resources[\\/]native[\\/].*\.dll$/i.test(String(entry))),
  'No native DLL may be selected by the app.asar files allowlist.');

const before = {
  hook: snapshot(hookPath),
  bundle: snapshot(bundlePath)
};

const hookResult = runNode(verifierPath);
assert(hookResult.status === 0, `Hook validation failed:\n${hookResult.stdout}\n${hookResult.stderr}`);

const bundleResult = runNode(bundleBuilderPath, ['--verify-only']);
assert(bundleResult.status === 0,
  `Runtime bundle validation failed:\n${bundleResult.stdout}\n${bundleResult.stderr}`);

const after = {
  hook: snapshot(hookPath),
  bundle: snapshot(bundlePath)
};
assert(JSON.stringify(after) === JSON.stringify(before),
  'Validation-only commands mutated native build inputs or the packaged bundle.');
assert(!fs.existsSync(legacyBridgePath) && !fs.existsSync(legacyManifestPath),
  'Validation recreated a retired loose source artifact.');

const mismatch = runNode(verifierPath, [], {
  ...process.env,
  QQSPEED_HOOK_SHA256: '0'.repeat(64)
});
assert(mismatch.status !== 0, 'Hook validation must reject an unexpected configured SHA256.');
assert(JSON.stringify(snapshot(hookPath)) === JSON.stringify(before.hook),
  'Failed Hook validation mutated the protected DLL.');

const bridgeHash = hookResult.stdout.match(/descriptor QQSpeedNativeBridge\.dll size=\d+ sha256=([0-9a-f]{64})/i)?.[1];
assert(bridgeHash, 'Hook validator did not report the Bridge descriptor from the bundle.');
console.log(
  `smoke-runtime-artifacts: PASS bundle_sha256=${before.bundle.sha256} `
  + `bridge_sha256=${bridgeHash} hook_sha256=${before.hook.sha256}`
);
