import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..', '..');
const read = (relativePath) => readFileSync(resolve(root, relativePath), 'utf8');
const packageJson = JSON.parse(read('package.json'));
const packageLock = JSON.parse(read('package-lock.json'));

test('1.0.6 packages one video soundtrack and the GM database without independent music UI', () => {
  const html = read('src/renderer/index.html');
  const mediaController = read('src/renderer/media-controller.js');

  assert.equal(packageJson.version, '1.0.6');
  assert.equal(packageLock.version, '1.0.6');
  assert.equal(packageLock.packages?.['']?.version, '1.0.6');
  assert.ok((packageJson.build?.files || []).includes('assets/**/*'));
  assert.ok((packageJson.build?.asarUnpack || []).includes('assets/launcher-background.mp4'));
  assert.ok((packageJson.build?.extraResources || []).some(
    (entry) => entry.from === 'resources/gm-items.sqlite3' && entry.to === 'gm-items.sqlite3'
  ));
  assert.match(html, /id="launcherBackgroundVideo"[^>]*src="\.\.\/\.\.\/assets\/launcher-background\.mp4"/s);
  assert.doesNotMatch(html, /id="(?:loginMediaVideo|homeHeroVideo|launcherMusic|musicControlItem|musicMuteBtn|musicVolume)"/);
  assert.doesNotMatch(mediaController, /launcherMusic|musicControlItem|musicMuteBtn|musicVolume/);
});

test('packaged release smoke reports the audible shared background video state', () => {
  const releaseSmoke = read('src/scripts/smoke-release.cjs');

  assert.match(
    releaseSmoke,
    /const backgroundVideo = document\.getElementById\('launcherBackgroundVideo'\);/
  );
  assert.doesNotMatch(releaseSmoke, /loginMediaVideo/);
  assert.match(
    releaseSmoke,
    /backgroundVideo:\s*{[\s\S]*?readyState:\s*Number\(backgroundVideo\?\.readyState \|\| 0\),[\s\S]*?duration:\s*Number\(backgroundVideo\?\.duration \|\| 0\),[\s\S]*?paused:\s*Boolean\(backgroundVideo\?\.paused\),[\s\S]*?muted:\s*Boolean\(backgroundVideo\?\.muted\),[\s\S]*?volume:\s*Number\(backgroundVideo\?\.volume \|\| 0\),[\s\S]*?source:\s*String\(backgroundVideo\?\.currentSrc \|\| backgroundVideo\?\.src \|\| ''\),[\s\S]*?soundLabel:\s*backgroundSoundButton\?\.getAttribute\('aria-label'\) \|\| ''[\s\S]*?}/
  );
  assert.match(releaseSmoke, /assert\.equal\(state\.backgroundVideo\.muted,\s*false\);/);
  assert.match(releaseSmoke, /assert\.equal\(state\.backgroundVideo\.volume,\s*0\.7\);/);
  assert.match(
    releaseSmoke,
    /assert\.match\(state\.backgroundVideo\.soundLabel,\s*\/\^关闭背景声音\$\/\);/
  );
});

test('NSIS uses an assisted installer with an installation-directory chooser', () => {
  const nsis = packageJson.build?.nsis || {};
  assert.equal(nsis.oneClick, false);
  assert.equal(nsis.allowToChangeInstallationDirectory, true);
  assert.equal(nsis.perMachine, false);
});

test('production artifacts use normal compression without claiming unsupported differential updates', () => {
  const build = packageJson.build || {};
  const nsis = build.nsis || {};

  assert.equal(build.compression, 'normal');
  assert.equal(nsis.differentialPackage, false);
});

test('Windows release uses QQSpeedLauncher for the visible application, executable and shortcuts', () => {
  const build = packageJson.build || {};
  const win = build.win || {};
  const nsis = build.nsis || {};

  assert.equal(packageJson.productName, 'QQSpeedLauncher');
  assert.equal(packageJson.author, 'QQSpeedLauncher');
  assert.equal(build.productName, 'QQSpeedLauncher');
  assert.equal(win.executableName, 'QQSpeedLauncher');
  assert.equal(nsis.shortcutName, 'QQSpeedLauncher');
  assert.equal(nsis.uninstallDisplayName, 'QQSpeedLauncher');
  assert.equal(nsis.artifactName, '${productName}-Setup-${version}-${arch}.${ext}');
});

test('QQSpeedLauncher branding preserves existing launcher data and removes legacy shortcuts on upgrade', () => {
  const main = read('src/main/index.js');
  const installer = read('src/build/installer.nsh');

  assert.match(main, /app\.setPath\('userData',\s*join\(app\.getPath\('appData'\),\s*'QQSpeedLauncher'\)\)/);
  assert.match(installer, /Delete\s+"\$DESKTOP\\QQSpeedLauncher\.lnk"/);
  assert.match(installer, /Delete\s+"\$SMPROGRAMS\\QQSpeedLauncher\.lnk"/);
});

test('an explicit Electron user-data directory remains available for isolated release checks', () => {
  const main = read('src/main/index.js');

  assert.match(
    main,
    /if\s*\(!app\.commandLine\.hasSwitch\('user-data-dir'\)\)\s*{[\s\S]*?app\.setPath\('userData',\s*join\(app\.getPath\('appData'\),\s*'QQSpeedLauncher'\)\);?[\s\S]*?}/
  );
});

test('packaged application selects no native bundle or DLL inside ASAR', () => {
  const build = packageJson.build || {};
  const packagedFiles = build.files || [];
  const extraResources = build.extraResources || [];

  assert.deepEqual(extraResources, [
    { from: 'resources/gm-items.sqlite3', to: 'gm-items.sqlite3' }
  ]);
  assert.equal(extraResources.some((entry) => /resources[\\/]native|\.(?:dll|bundle)$/i.test(String(entry?.from || entry))), false);
  assert.equal(packagedFiles.includes('resources/native/qqspeed-runtime.bundle'), false,
    'Encrypted native bundle must be a PE resource, not an ASAR entry.');
  assert.equal(packagedFiles.some((entry) => /resources[\\/]native[\\/].*\.(?:dll|bundle)$/i.test(String(entry))), false);
  assert.equal(packagedFiles.includes('resources/native/runtime-artifacts.json'), false);
});

test('custom launcher background video is packaged and narrowly unpacked for media seeking', () => {
  assert.equal(existsSync(resolve(root, 'assets/launcher-background.mp4')), true);
  assert.ok((packageJson.build?.files || []).includes('assets/**/*'));
  assert.ok((packageJson.build?.asarUnpack || []).includes('assets/launcher-background.mp4'));
});

test('afterPack writes and byte-verifies resource 301 before flipping Electron fuses', () => {
  const afterPack = read('src/build/after-pack.cjs');
  const peResource = read('src/build/pe-runtime-resource.cjs');
  assert.match(afterPack, /writePeRuntimeResource/);
  assert.match(afterPack, /readPeRuntimeResource/);
  assert.match(peResource, /RUNTIME_RESOURCE_ID\s*=\s*301/);
  assert.ok(afterPack.indexOf('writePeRuntimeResource') < afterPack.indexOf('hardenElectronExecutable(executablePath)'));
  assert.match(afterPack, /resourceBytes\.equals\(bundleBytes\)/);
});

test('portable production checks verify the embedded PE resource instead of an ASAR bundle copy', () => {
  for (const relativePath of [
    'src/scripts/build-portable-x64.cjs',
    'src/scripts/smoke-portable-x64.cjs'
  ]) {
    const script = read(relativePath);
    assert.match(script, /readPeRuntimeResource/,
      `${relativePath} must verify runtime resource 301 in the packaged QQSpeedLauncher.exe.`);
    assert.doesNotMatch(script, /\bextractFile\b/,
      `${relativePath} must not read the runtime bundle from app.asar.`);
    assert.match(script, /asarEntries\.includes\(RUNTIME_BUNDLE_ENTRY\)[\s\S]{0,180}app\.asar 不得包含/,
      `${relativePath} must reject a duplicate runtime bundle in app.asar.`);
  }
});

test('runtime bundle builder requires Bridge, protected Hook and in-memory release material', () => {
  const builder = read('src/scripts/build-runtime-bundle.cjs');
  assert.match(builder, /defaultBridgeInput/);
  assert.match(builder, /defaultHookInput/);
  assert.match(builder, /qqspeed_clean_hook_64\.dll/);
  assert.match(builder, /in-memory 32-byte Buffers/);
  assert.doesNotMatch(builder, /QQS_RUNTIME_BUNDLE_SECRET_FILE|loadBuildSecret|secretFile/);
  assert.match(read('src/scripts/offline-release-material.cjs'), /hkdfSync/);
  assert.doesNotMatch(read('src/shared/native-runtime-bundle.js'), /createBuildKey|BUILD_KEY_LABEL/);
  assert.doesNotMatch(read('src/shared/native-runtime-bundle.js'), /QQSpeedNativeBridge\.dll|qqspeed_clean_hook_64\.dll/);
});

test('NSIS preserves the per-install identity during updates and removes it only on normal uninstall', () => {
  const includePath = packageJson.build?.nsis?.include;
  assert.equal(includePath, 'src/build/installer.nsh');
  assert.ok(existsSync(resolve(root, includePath)));
  const installer = read(includePath);
  assert.match(installer, /!macro\s+customInstall/);
  assert.match(installer, /!macro\s+customUnInstall[\s\S]*?\$\{ifNot\}\s+\$\{isUpdated\}[\s\S]*?\$LOCALAPPDATA\\QQSpeedLauncher\\install-secrets/);
  assert.doesNotMatch(installer, /\$APPDATA|credential|settings|launcher-account|launcher-credential-vault/i);
});

test('package checks the new in-memory resource/key modules and removes obsolete disk staging checks', () => {
  const check = String(packageJson.scripts?.check || '');
  for (const file of [
    'src/main/windows-dpapi.js',
    'src/main/install-key-vault.js',
    'src/main/runtime-material-envelope.js',
    'src/main/executable-runtime-resource.js',
    'src/build/pe-runtime-resource.cjs'
  ]) {
    assert.ok(check.includes(`node --check ${file}`), `check is missing ${file}`);
  }
  assert.doesNotMatch(check, /native-runtime-cleaner|smoke-runtime-artifacts/);
  assert.equal(Object.hasOwn(packageJson.scripts || {}, 'check:runtime-artifacts'), false);
});
