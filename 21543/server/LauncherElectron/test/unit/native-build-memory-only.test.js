import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = new URL('../../', import.meta.url);
const require = createRequire(import.meta.url);
const buildScriptPath = fileURLToPath(new URL('src/scripts/build-native.cjs', root));
const deployedServerProjectPath = resolve(
  fileURLToPath(root),
  '..', '..', '..',
  '21446', '新建文件夹', 'server', 'Server', 'Server.vcxproj'
);

test('native build pins the VMProtect memory-mappable Hook and never expects Bridge RCDATA', async () => {
  const script = await readFile(new URL('src/scripts/build-native.cjs', root), 'utf8');
  assert.match(script, /d4a55baacdebc91a876a483b3a39dbae919855ed897989a91c56903f1e8f3436/i);
  assert.match(script, /verifyManualMapHookInput/);
  assert.match(script, /QQSpeedNativeBridge_SetHookPayload/);
  assert.doesNotMatch(script, /QQSPEED_HOOK_RESOURCE_ID|verifyEmbeddedHook|内嵌 RCDATA Hook/);
});

test('native release keeps one stable local material include and publishes the deployed server to project bin', async () => {
  const script = await readFile(new URL('src/scripts/build-native.cjs', root), 'utf8');
  const packageJson = JSON.parse(await readFile(new URL('package.json', root), 'utf8'));
  const { resolveReleaseCandidateBundlePath, resolveSpeedServerBuildPaths } = require(buildScriptPath);
  const serverProject = resolve('C:\\qqspeed-server', 'Server', 'Server.vcxproj');
  const paths = resolveSpeedServerBuildPaths(serverProject);
  const runtimeBundle = resolve('F:\\launcher', 'resources', 'native', 'qqspeed-runtime.bundle');
  const candidateBundle = resolveReleaseCandidateBundlePath(runtimeBundle, 'test-build');

  assert.match(script, /generateReleaseMaterial/);
  assert.match(script, /deriveContentKey/);
  assert.match(script, /renderEmbeddedReleaseSource/);
  assert.match(script, /parseEmbeddedReleaseSource/);
  assert.match(script, /LauncherReleaseMaterial\.generated\.inc/);
  assert.match(script, /QQSReleaseMaterialIncludeDir/);
  assert.match(script, /Server\.vcxproj/);
  assert.match(script, /QQSpeedServer\.exe/);
  assert.match(script, /--rotate-release/);
  assert.doesNotMatch(script, /21446|新建文件夹/);
  assert.match(script, /FORBIDDEN_RELEASE_DEBUG_EXTENSIONS/);
  assert.equal(paths.outputDir, join(dirname(serverProject), 'bin'));
  assert.equal(paths.releaseMaterialDir, join(dirname(serverProject), 'build', 'release-material'));
  assert.equal(paths.releaseMaterialPath, join(paths.releaseMaterialDir, 'LauncherReleaseMaterial.generated.inc'));
  assert.equal(paths.objectDir.startsWith(paths.outputDir), false);
  assert.equal(dirname(candidateBundle), dirname(runtimeBundle));
  assert.notEqual(candidateBundle, runtimeBundle);
  assert.match(candidateBundle, /\.candidate$/);
  assert.equal(packageJson.scripts['build:server'], 'node src/scripts/build-native.cjs --server-only');
  assert.equal(packageJson.scripts['rotate:runtime-release'], 'node src/scripts/build-native.cjs --rotate-release');
  assert.doesNotMatch(script, /QQS_RUNTIME_BUNDLE_SECRET_FILE|build-secret\.json/);
  assert.match(script, /仍包含 LauncherRuntimeKeyring\.bin 依赖/);
});

test('deployed QQSpeedServer direct Release x64 builds reuse local material and keep outputs under bin/build', async () => {
  const project = await readFile(deployedServerProjectPath, 'utf8');
  assert.match(project, /<OutDir>\$\(ProjectDir\)bin\\<\/OutDir>/);
  assert.match(
    project,
    /<QQSReleaseMaterialIncludeDir[^>]*>\$\(ProjectDir\)build\\release-material<\/QQSReleaseMaterialIncludeDir>/
  );
  assert.match(
    project,
    /<IntDir>\$\(ProjectDir\)build\\obj\\\$\(ProjectName\)-\$\(Platform\)\\\$\(Configuration\)\\<\/IntDir>/
  );
  assert.match(project, /\$\(QQSReleaseMaterialPreprocessor\)NDEBUG/);
  assert.match(project, /<AdditionalIncludeDirectories>\$\(QQSReleaseMaterialIncludeDir\);/);
});
