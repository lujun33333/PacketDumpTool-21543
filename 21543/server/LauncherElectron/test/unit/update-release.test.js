import assert from 'node:assert/strict';
import { generateKeyPairSync, verify } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');

test('publish script copies the installer and atomically writes a signed update.json', async (t) => {
  const temp = await mkdtemp(join(tmpdir(), 'qqspeed-publish-test-'));
  t.after(() => rm(temp, { recursive: true, force: true }));
  const installer = join(temp, 'QQSpeedLauncher-Setup-1.0.2-x64.exe');
  const output = join(temp, 'server');
  const privateKeyPath = join(temp, 'private.pem');
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  await writeFile(installer, Buffer.from('installer-fixture'));
  await writeFile(privateKeyPath, privateKey.export({ type: 'pkcs8', format: 'pem' }));

  const result = spawnSync(process.execPath, [
    resolve(root, 'src/scripts/publish-update.cjs'),
    '--installer', installer,
    '--output', output,
    '--private-key', privateKeyPath,
    '--version', '1.0.2'
  ], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr || result.stdout);

  const manifest = JSON.parse(await readFile(join(output, 'update.json'), 'utf8'));
  assert.equal(manifest.version, '1.0.2');
  assert.equal(manifest.required, true);
  assert.equal(manifest.file, 'QQSpeedLauncher-Setup-1.0.2-x64.exe');
  assert.match(manifest.sha256, /^[a-f0-9]{64}$/);
  const payload = Buffer.from(JSON.stringify({
    version: manifest.version,
    required: manifest.required,
    file: manifest.file,
    sha256: manifest.sha256
  }), 'utf8');
  assert.equal(verify(null, payload, publicKey, Buffer.from(manifest.signature, 'base64')), true);
  assert.deepEqual(await readFile(join(output, manifest.file)), Buffer.from('installer-fixture'));
});

test('default primary setup maintains only the existing launcher update Nginx', () => {
  const maintenance = readFileSync(
    resolve(root, 'deploy/update-server/maintain-update-server.ps1'),
    'utf8'
  );
  const start = readFileSync(resolve(root, 'deploy/update-server/start-update-server.cmd'), 'utf8');
  const setup = readFileSync(resolve(root, 'deploy/update-server/setup-update-server.cmd'), 'utf8');
  const settings = readFileSync(resolve(root, 'deploy/update-server/update-server.settings.cmd'), 'utf8');
  const chineseSetup = readFileSync(
    resolve(root, 'deploy/update-server/配置主登录器更新服务.cmd'),
    'utf8'
  );
  const chineseStart = readFileSync(
    resolve(root, 'deploy/update-server/启动或重载主更新服务.cmd'),
    'utf8'
  );
  const safeEntryPoints = [maintenance, start, setup].join('\n');

  assert.match(setup, /maintain-update-server\.ps1/);
  assert.doesNotMatch(setup, /install-update-server\.ps1/);
  assert.match(maintenance, /launcher-update\.conf/);
  assert.match(maintenance, /Remove-NetQosPolicy/);
  assert.match(maintenance, /Register-ScheduledTask/);
  assert.match(maintenance, /New-NetFirewallRule[\s\S]*LocalPort\s+\$listenPort/);
  assert.match(start, /NGINX_ROOT_ENV=%~dp0nginx-root\.env\.cmd/);
  assert.match(start, /launcher-update\.conf/);
  assert.match(start, /"%NGINX_EXE%"\s+-t/);
  assert.match(start, /127\.0\.0\.1:'\s*\+\s*\$env:UPDATE_SERVER_PORT\s*\+\s*'\/update\.json/);
  assert.doesNotMatch(
    safeEntryPoints,
    /SpeedServer|Aurora|mysql\.ini|Stop-Process/i
  );
  assert.match(settings, /QQS_THROTTLE_MBPS=0/);
  assert.match(chineseSetup, /setup-update-server\.cmd/);
  assert.match(chineseStart, /start-update-server\.cmd/);
  assert.doesNotMatch(
    [chineseSetup, chineseStart].join('\n'),
    /with-aurora|SpeedServer|QQSpeedServer/i
  );
});

test('Aurora and SpeedServer deployment remains available only through explicit combined entry points', () => {
  const nginx = readFileSync(resolve(root, 'deploy/update-server/nginx-launcher-update.conf'), 'utf8');
  const install = readFileSync(resolve(root, 'deploy/update-server/install-update-server.ps1'), 'utf8');
  const start = readFileSync(
    resolve(root, 'deploy/update-server/start-update-server-with-aurora.cmd'),
    'utf8'
  );
  const setup = readFileSync(
    resolve(root, 'deploy/update-server/setup-update-server-with-aurora.cmd'),
    'utf8'
  );
  assert.match(nginx, /listen\s+__LISTEN_PORT__/);
  assert.match(nginx, /autoindex\s+off/);
  assert.match(nginx, /request_method[\s\S]*?GET\|HEAD[\s\S]*?return\s+405/);
  assert.doesNotMatch(nginx, /limit_conn|limit_req|limit_rate/);
  assert.match(install, /ThrottleMbps\s*=\s*0/);
  assert.match(install, /ThrottleMbps\s+-lt\s+0/);
  assert.match(install, /Remove-NetQosPolicy/);
  assert.match(install, /start-update-server-with-aurora\.cmd/);
  assert.match(install, /if\s*\(\$ThrottleMbps\s+-gt\s+0\)[\s\S]*?ThrottleRateActionBitsPerSecond/);
  assert.match(install, /\.Replace\('__LISTEN_PORT__',\s*\$listenPort\.ToString\(\)\)/);
  assert.match(install, /New-NetFirewallRule[\s\S]*?-LocalPort\s+\$listenPort/);
  assert.match(install, /UpdateRoot[\s\S]*?-replace\s+'\\\\',\s*'\/'[\s\S]*?Set-Content/);
  assert.match(setup, /install-update-server\.ps1/);
  assert.match(setup, /SpeedServer|Aurora/i);
  assert.match(start, /SPEED_SERVER_ENV=%~dp0speed-server\.env\.cmd/);
  assert.match(start, /SpeedServer\.exe/);
  assert.match(setup, /CONFIG_NGINX_ROOT=C:\\nginx-1\.31\.2/);
  assert.match(setup, /CONFIG_THROTTLE_MBPS=0/);
  assert.match(setup, /ThrottleMbps "%CONFIG_THROTTLE_MBPS%"/);
  assert.doesNotMatch(
    [nginx, install, start, setup].join('\n'),
    /114\.132\.178\.23|backup-server/i
  );
});

test('backup server is a standalone package and cannot configure the primary game server', () => {
  const backupRoot = resolve(root, 'deploy/update-server/backup-server');
  const mirror = readFileSync(
    resolve(backupRoot, 'nginx-update-mirror.conf'),
    'utf8'
  );
  const install = readFileSync(
    resolve(backupRoot, 'install-backup-server.ps1'),
    'utf8'
  );
  const setup = readFileSync(
    resolve(backupRoot, 'setup-backup-server.cmd'),
    'utf8'
  );
  const chineseSetup = readFileSync(
    resolve(backupRoot, '配置备用登录器下载服务.cmd'),
    'utf8'
  );
  const readme = readFileSync(
    resolve(backupRoot, 'README.md'),
    'utf8'
  );
  const runbook = readFileSync(
    resolve(root, 'docs/launcher-update-simple-runbook.md'),
    'utf8'
  );

  assert.match(mirror, /listen\s+__LISTEN_PORT__/);
  assert.match(mirror, /root\s+__UPDATE_ROOT__/);
  assert.match(mirror, /location\s+=\s+\/update\.json/);
  assert.match(mirror, /location\s+~\s+"\^\/\[A-Za-z0-9\]/);
  assert.match(mirror, /autoindex\s+off/);
  assert.doesNotMatch(mirror, /aurora|dav_methods|limit_conn|limit_req|limit_rate/i);
  assert.match(install, /NginxRoot\s*=\s*'C:\\nginx-1\.31\.3'/);
  assert.match(install, /UpdateRoot\s*=\s*'C:\\LauncherUpdate'/);
  assert.match(install, /\.Replace\('__LISTEN_PORT__',\s*\$ListenPort\.ToString\(\)\)/);
  assert.match(install, /\.Replace\('__UPDATE_ROOT__',\s*\$nginxUpdateRoot\)/);
  assert.match(install, /New-NetFirewallRule[\s\S]*LocalPort\s+\$ListenPort/);
  assert.match(install, /Register-ScheduledTask/);
  assert.doesNotMatch(install, /SpeedServer|Aurora|mysql|43\.226\.61\.100/i);
  assert.match(setup, /install-backup-server\.ps1/);
  assert.match(setup, /net session/);
  assert.match(setup, /-File\s+"%~dp0install-backup-server\.ps1"/);
  assert.match(setup, /if errorlevel 1/);
  assert.match(chineseSetup, /setup-backup-server\.cmd/);
  assert.match(readme, /114\.132\.178\.23:28881/);
  assert.match(readme, /C:\\LauncherUpdate/);
  assert.match(runbook, /114\.132\.178\.23:28881/);
  assert.match(runbook, /backup-server/);
  assert.match(runbook, /约 50%[\s\S]*优先下载/);
  assert.match(runbook, /先上传新.*\.exe.*两个服务器/);
  assert.match(runbook, /最后替换主服务器[\s\S]*update\.json/);

});

test('NSIS preserves install identity during an automatic update', () => {
  const installer = readFileSync(resolve(root, 'src/build/installer.nsh'), 'utf8');
  const customInstallStart = installer.indexOf('!macro customInstall');
  const customInstallEnd = installer.indexOf('!macroend', customInstallStart);
  assert.ok(customInstallStart >= 0 && customInstallEnd > customInstallStart);
  assert.doesNotMatch(installer.slice(customInstallStart, customInstallEnd), /RMDir/);
  assert.match(installer, /!macro\s+customUnInstall[\s\S]*?\$\{ifNot\}\s+\$\{isUpdated\}[\s\S]*?RMDir[\s\S]*?!macroend/);
});
