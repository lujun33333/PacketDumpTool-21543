import assert from 'node:assert/strict';
import { createHash, generateKeyPairSync, sign } from 'node:crypto';
import { EventEmitter } from 'node:events';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';

import {
  canonicalUpdatePayload,
  compareLauncherVersions,
  downloadVerifiedInstaller,
  evaluateSignedUpdateManifest,
  fetchSignedUpdateManifest,
  launchUpdateInstaller,
  runMandatoryLauncherUpdate
} from '../../src/main/launcher-updater.js';

function makeSignedManifest(overrides = {}) {
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  const manifest = {
    version: '1.0.2',
    required: true,
    file: 'QQSpeedLauncher-Setup-1.0.2-x64.exe',
    sha256: 'a'.repeat(64),
    ...overrides
  };
  manifest.signature = sign(null, canonicalUpdatePayload(manifest), privateKey).toString('base64');
  return { manifest, publicKey };
}

test('launcher versions compare numeric semver components instead of string order', () => {
  assert.equal(compareLauncherVersions('1.0.9', '1.0.10'), -1);
  assert.equal(compareLauncherVersions('1.2.0', '1.2.0'), 0);
  assert.equal(compareLauncherVersions('2.0.0', '1.99.99'), 1);
});

test('signed manifest requires a newer forced version before startup is blocked', () => {
  const { manifest, publicKey } = makeSignedManifest();
  assert.deepEqual(evaluateSignedUpdateManifest(manifest, {
    currentVersion: '1.0.1',
    publicKey
  }), {
    status: 'required',
    version: '1.0.2',
    file: 'QQSpeedLauncher-Setup-1.0.2-x64.exe',
    sha256: 'a'.repeat(64)
  });

  assert.equal(evaluateSignedUpdateManifest(manifest, {
    currentVersion: '1.0.2',
    publicKey
  }).status, 'current');
});

test('tampering any signed update field is rejected', () => {
  const { manifest, publicKey } = makeSignedManifest();
  assert.throws(() => evaluateSignedUpdateManifest({
    ...manifest,
    file: 'Different.exe'
  }, {
    currentVersion: '1.0.1',
    publicKey
  }), /签名/);
});

test('unsafe installer paths and invalid hashes are rejected before download', () => {
  const unsafe = makeSignedManifest({ file: '../Other.exe' });
  assert.throws(() => evaluateSignedUpdateManifest(unsafe.manifest, {
    currentVersion: '1.0.1',
    publicKey: unsafe.publicKey
  }), /文件名/);

  const invalidHash = makeSignedManifest({ sha256: '1234' });
  assert.throws(() => evaluateSignedUpdateManifest(invalidHash.manifest, {
    currentVersion: '1.0.1',
    publicKey: invalidHash.publicKey
  }), /SHA256/);
});

test('manifest check automatically retries transient network failures', async () => {
  const expected = {
    version: '1.0.4',
    required: true,
    file: 'QQSpeedLauncher-Setup-1.0.4-x64.exe',
    sha256: 'a'.repeat(64),
    signature: 'test'
  };
  let attempts = 0;
  const actual = await fetchSignedUpdateManifest({
    manifestUrl: 'http://127.0.0.1/update.json',
    retryDelaysMs: [0, 0],
    async fetchImpl() {
      attempts += 1;
      if (attempts < 3) {
        throw new TypeError('fetch failed');
      }
      return new Response(JSON.stringify(expected), {
        status: 200,
        headers: { 'Content-Type': 'application/json' }
      });
    }
  });

  assert.equal(attempts, 3);
  assert.deepEqual(actual, expected);
});

test('mandatory updater distributes downloads independently from the manifest winner and falls back', async (t) => {
  const installer = Buffer.from('dual-source-installer');
  const sha256 = createHash('sha256').update(installer).digest('hex');
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  const manifest = {
    version: '1.0.4',
    required: true,
    file: 'QQSpeedLauncher-Setup-1.0.4-x64.exe',
    sha256
  };
  manifest.signature = sign(null, canonicalUpdatePayload(manifest), privateKey).toString('base64');

  const root = await mkdtemp(join(tmpdir(), 'qqspeed-dual-source-test-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const publicKeyPath = join(root, 'update-public-key.pem');
  await writeFile(publicKeyPath, publicKey.export({ type: 'spki', format: 'pem' }));

  const requests = [];
  const installing = await runMandatoryLauncherUpdate({
    currentVersion: '1.0.3',
    publicKeyPath,
    downloadDirectory: root,
    manifestUrls: [
      'http://primary.test:28881/update.json',
      'http://backup.test:28881/update.json'
    ],
    randomImpl: () => 0.99,
    async fetchImpl(input) {
      const url = String(input);
      requests.push(url);
      if (url === 'http://primary.test:28881/update.json') {
        return new Response(JSON.stringify(manifest), { status: 200 });
      }
      if (url === 'http://backup.test:28881/update.json') {
        await new Promise((resolve) => setTimeout(resolve, 20));
        return new Response(JSON.stringify(manifest), { status: 200 });
      }
      if (url === 'http://primary.test:28881/QQSpeedLauncher-Setup-1.0.4-x64.exe') {
        return new Response(installer, {
          status: 200,
          headers: { 'Content-Length': String(installer.length) }
        });
      }
      if (url === 'http://backup.test:28881/QQSpeedLauncher-Setup-1.0.4-x64.exe') {
        throw new TypeError('backup download failed');
      }
      throw new Error(`unexpected request: ${url}`);
    },
    spawnImpl() {
      const child = new EventEmitter();
      child.unref = () => {};
      queueMicrotask(() => child.emit('spawn'));
      return child;
    }
  });

  assert.equal(installing.status, 'installing');
  assert.ok(requests.includes('http://primary.test:28881/update.json'));
  assert.ok(requests.includes('http://backup.test:28881/update.json'));
  assert.deepEqual(
    requests.filter((url) => url.endsWith('.exe')),
    [
      'http://backup.test:28881/QQSpeedLauncher-Setup-1.0.4-x64.exe',
      'http://primary.test:28881/QQSpeedLauncher-Setup-1.0.4-x64.exe'
    ]
  );
  assert.deepEqual(await readFile(installing.installerPath), installer);
});

test('an invalid faster mirror manifest cannot bypass signature verification', async (t) => {
  const installer = Buffer.from('signed-source-installer');
  const sha256 = createHash('sha256').update(installer).digest('hex');
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  const manifest = {
    version: '1.0.4',
    required: true,
    file: 'QQSpeedLauncher-Setup-1.0.4-x64.exe',
    sha256
  };
  manifest.signature = sign(null, canonicalUpdatePayload(manifest), privateKey).toString('base64');
  const tamperedManifest = { ...manifest, sha256: 'f'.repeat(64) };

  const root = await mkdtemp(join(tmpdir(), 'qqspeed-signed-source-test-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const publicKeyPath = join(root, 'update-public-key.pem');
  await writeFile(publicKeyPath, publicKey.export({ type: 'spki', format: 'pem' }));

  const installing = await runMandatoryLauncherUpdate({
    currentVersion: '1.0.3',
    publicKeyPath,
    downloadDirectory: root,
    manifestUrls: [
      'http://primary.test:28881/update.json',
      'http://backup.test:28881/update.json'
    ],
    async fetchImpl(input) {
      const url = String(input);
      if (url === 'http://backup.test:28881/update.json') {
        return new Response(JSON.stringify(tamperedManifest), { status: 200 });
      }
      if (url === 'http://primary.test:28881/update.json') {
        await new Promise((resolve) => setTimeout(resolve, 20));
        return new Response(JSON.stringify(manifest), { status: 200 });
      }
      if (url.endsWith('/QQSpeedLauncher-Setup-1.0.4-x64.exe')) {
        return new Response(installer, {
          status: 200,
          headers: { 'Content-Length': String(installer.length) }
        });
      }
      throw new Error(`unexpected request: ${url}`);
    },
    spawnImpl() {
      const child = new EventEmitter();
      child.unref = () => {};
      queueMicrotask(() => child.emit('spawn'));
      return child;
    }
  });

  assert.equal(installing.status, 'installing');
  assert.deepEqual(await readFile(installing.installerPath), installer);
});

test('installer is streamed to disk and kept only when SHA256 matches', async (t) => {
  const payload = Buffer.from('signed-installer-fixture');
  const expectedHash = createHash('sha256').update(payload).digest('hex');
  const server = createServer((_request, response) => {
    response.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': payload.length
    });
    response.end(payload);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  t.after(() => server.close());

  const root = await mkdtemp(join(tmpdir(), 'qqspeed-update-test-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const destination = join(root, 'Launcher.exe');
  const { port } = server.address();
  const progressEvents = [];

  await downloadVerifiedInstaller({
    url: `http://127.0.0.1:${port}/Launcher.exe`,
    destination,
    sha256: expectedHash,
    onProgress(progress) {
      progressEvents.push(progress);
    }
  });
  assert.deepEqual(await readFile(destination), payload);
  assert.ok(progressEvents.some((progress) => (
    progress.phase === 'downloading'
      && progress.downloadedBytes === payload.length
      && progress.totalBytes === payload.length
      && progress.percent === 100
  )));
  assert.ok(progressEvents.some((progress) => (
    progress.phase === 'verifying'
      && progress.downloadedBytes === payload.length
      && progress.totalBytes === payload.length
  )));

  await assert.rejects(downloadVerifiedInstaller({
    url: `http://127.0.0.1:${port}/Launcher.exe`,
    destination: join(root, 'Bad.exe'),
    sha256: 'f'.repeat(64)
  }), /校验失败/);
  await assert.rejects(readFile(join(root, 'Bad.exe')), /ENOENT/);
  await assert.rejects(readFile(join(root, 'Bad.exe.part')), /ENOENT/);
});

test('NSIS update launch is silent, marked as update, and waits until process creation succeeds', async () => {
  let invocation = null;
  const child = new EventEmitter();
  child.unrefCalled = false;
  child.unref = () => { child.unrefCalled = true; };
  const started = launchUpdateInstaller('C:\\Temp\\Launcher.exe', {
    spawnImpl(command, args, options) {
      invocation = { command, args, options };
      return child;
    }
  });
  assert.equal(typeof started?.then, 'function');
  queueMicrotask(() => child.emit('spawn'));
  await started;

  assert.equal(invocation.command, 'C:\\Temp\\Launcher.exe');
  assert.deepEqual(invocation.args, ['/S', '--updated', '--force-run']);
  assert.equal(invocation.options.detached, true);
  assert.equal(invocation.options.stdio, 'ignore');
  assert.equal(child.unrefCalled, true);
});

test('installer process creation failure blocks launcher shutdown for a retry', async () => {
  const child = new EventEmitter();
  child.unref = () => {};
  const started = launchUpdateInstaller('C:\\Temp\\Launcher.exe', {
    spawnImpl: () => child
  });
  queueMicrotask(() => child.emit('error', new Error('spawn failed')));
  await assert.rejects(started, /spawn failed/);
});

test('mandatory updater stays silent when current and announces a required update before download', async (t) => {
  const installer = Buffer.from('required-update-installer');
  const sha256 = createHash('sha256').update(installer).digest('hex');
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  const manifest = {
    version: '1.0.2',
    required: true,
    file: 'QQSpeedLauncher-Setup-1.0.2-x64.exe',
    sha256
  };
  manifest.signature = sign(null, canonicalUpdatePayload(manifest), privateKey).toString('base64');

  let installerRequests = 0;
  const server = createServer((request, response) => {
    if (request.url === '/update.json') {
      const body = Buffer.from(JSON.stringify(manifest));
      response.writeHead(200, { 'Content-Type': 'application/json', 'Content-Length': body.length });
      response.end(body);
      return;
    }
    installerRequests += 1;
    response.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': installer.length });
    response.end(installer);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  t.after(() => server.close());

  const root = await mkdtemp(join(tmpdir(), 'qqspeed-mandatory-update-test-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const publicKeyPath = join(root, 'update-public-key.pem');
  await writeFile(publicKeyPath, publicKey.export({ type: 'spki', format: 'pem' }));
  const manifestUrl = `http://127.0.0.1:${server.address().port}/update.json`;

  let requiredCalls = 0;
  const progressEvents = [];
  const current = await runMandatoryLauncherUpdate({
    currentVersion: '1.0.2',
    publicKeyPath,
    downloadDirectory: root,
    manifestUrl,
    onUpdateRequired: () => { requiredCalls += 1; }
  });
  assert.equal(current.status, 'current');
  assert.equal(requiredCalls, 0);
  assert.equal(installerRequests, 0);

  const requiredDecisions = [];
  const installing = await runMandatoryLauncherUpdate({
    currentVersion: '1.0.1',
    publicKeyPath,
    downloadDirectory: root,
    manifestUrl,
    onUpdateRequired(decision) {
      assert.equal(installerRequests, 0);
      requiredDecisions.push(decision);
    },
    onProgress(progress) {
      progressEvents.push(progress);
    },
    spawnImpl() {
      const child = new EventEmitter();
      child.unref = () => {};
      queueMicrotask(() => child.emit('spawn'));
      return child;
    }
  });
  assert.equal(installing.status, 'installing');
  assert.equal(requiredDecisions.length, 1);
  assert.equal(requiredDecisions[0].version, '1.0.2');
  assert.equal(installerRequests, 1);
  assert.ok(progressEvents.some((progress) => (
    progress.phase === 'downloading' && progress.version === '1.0.2'
  )));
  assert.deepEqual(progressEvents.at(-1), {
    phase: 'installing',
    version: '1.0.2',
    downloadedBytes: installer.length,
    totalBytes: installer.length,
    percent: 100,
    bytesPerSecond: 0
  });
});
