const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const net = require('node:net');
const { DatabaseSync } = require('node:sqlite');
const { listPackage } = require('@electron/asar');
const { Arch, Platform, build } = require('electron-builder');

const root = path.resolve(__dirname, '..', '..');
const RUNTIME_BUNDLE_ENTRY = 'resources/native/qqspeed-runtime.bundle';
const GM_DATABASE_NAME = 'gm-items.sqlite3';
const FORBIDDEN_NATIVE_DLLS = new Set([
  'qqspeednativebridge.dll',
  'qqspeed_clean_hook_64.dll'
]);

function normalizeAsarEntry(entry) {
  return String(entry).replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase();
}

function findForbiddenLooseDlls(directoryPath, matches = []) {
  if (!fs.existsSync(directoryPath)) {
    return matches;
  }
  for (const entry of fs.readdirSync(directoryPath, { withFileTypes: true })) {
    const entryPath = path.join(directoryPath, entry.name);
    if (entry.isDirectory()) {
      findForbiddenLooseDlls(entryPath, matches);
    } else if (entry.isFile() && FORBIDDEN_NATIVE_DLLS.has(entry.name.toLowerCase())) {
      matches.push(entryPath);
    }
  }
  return matches;
}

function verifyReadOnlyGmDatabase(databasePath) {
  const database = new DatabaseSync(databasePath, { readOnly: true });
  try {
    assert.equal(database.prepare('PRAGMA integrity_check').get().integrity_check, 'ok');
    assert.equal(database.prepare('SELECT COUNT(*) AS count FROM items').get().count, 138486);
    assert.equal(database.prepare(`
      SELECT COUNT(*) AS count
      FROM items
      WHERE category = '赛车' AND subcategory = 'S级赛车'
    `).get().count, 179);
    const highestSRacer = database.prepare(`
      SELECT id, name
      FROM items
      WHERE category = '赛车' AND subcategory = 'S级赛车'
      ORDER BY id DESC
      LIMIT 1
    `).get();
    assert.ok(Number(highestSRacer?.id) > 0, 'Packaged GM database could not answer the S-racer query.');
  } finally {
    database.close();
  }
}

function reservePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      server.close((error) => {
        if (error) {
          reject(error);
        } else {
          resolve(address.port);
        }
      });
    });
  });
}

function waitForFile(filePath, timeoutMs = 20000) {
  const deadline = Date.now() + timeoutMs;
  return new Promise((resolve, reject) => {
    const poll = () => {
      if (fs.existsSync(filePath)) {
        resolve();
        return;
      }
      if (Date.now() >= deadline) {
        reject(new Error(`Timed out waiting for ${filePath}`));
        return;
      }
      setTimeout(poll, 100);
    };
    poll();
  });
}

function evaluateCdp(webSocketDebuggerUrl, expression) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(webSocketDebuggerUrl);
    const timeout = setTimeout(() => {
      socket.close();
      reject(new Error('CDP Runtime.evaluate timed out.'));
    }, 5000);
    socket.addEventListener('open', () => {
      socket.send(JSON.stringify({
        id: 1,
        method: 'Runtime.evaluate',
        params: { expression, returnByValue: true }
      }));
    });
    socket.addEventListener('message', (event) => {
      const message = JSON.parse(String(event.data || ''));
      if (message.id !== 1) {
        return;
      }
      clearTimeout(timeout);
      socket.close();
      if (message.error || message.result?.exceptionDetails) {
        reject(new Error(`CDP evaluation failed: ${JSON.stringify(message.error || message.result.exceptionDetails)}`));
        return;
      }
      resolve(message.result?.result?.value);
    });
    socket.addEventListener('error', () => {
      clearTimeout(timeout);
      reject(new Error('CDP WebSocket connection failed.'));
    });
  });
}

async function waitForCdpRenderer(port) {
  const deadline = Date.now() + 15000;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      const targets = await response.json();
      const target = targets.find((entry) => entry.type === 'page' && entry.webSocketDebuggerUrl);
      if (target) {
        const state = await evaluateCdp(target.webSocketDebuggerUrl, `({
          rendererReady: document.body.dataset.rendererReady === 'ok',
          defaultCopy: document.getElementById('homeStatusText')?.textContent || '',
          startState: document.getElementById('startBtn')?.dataset.state || '',
          protocol: location.protocol
        })`);
        if (state?.rendererReady && state.startState === 'disabled') {
          return state;
        }
      }
    } catch (error) {
      lastError = error;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Packaged CDP renderer did not become ready. ${lastError?.message || ''}`);
}

function startExecutable(executablePath, resultPath, cdpAckPath, cdpPort) {
  const child = spawn(executablePath, [], {
    cwd: path.dirname(executablePath),
    env: {
      ...process.env,
      QQSPEED_PACKAGED_SMOKE_RESULT: resultPath,
      QQSPEED_PACKAGED_SMOKE_CDP_ACK: cdpAckPath,
      QQSPEED_PACKAGED_SMOKE_CDP_PORT: String(cdpPort)
    },
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true
  });
  let stderr = '';
  child.stderr.on('data', (chunk) => {
    stderr = `${stderr}${chunk}`.slice(-4000);
  });
  const exit = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      child.kill();
      reject(new Error('Packaged renderer/utility smoke timed out.'));
    }, 30000);
    child.on('error', (error) => {
      clearTimeout(timeout);
      reject(error);
    });
    child.on('exit', (code) => {
      clearTimeout(timeout);
      if (code !== 0) {
        reject(new Error(`Packaged smoke exited with code ${String(code)}. ${stderr}`));
        return;
      }
      resolve();
    });
  });
  return { child, exit };
}

async function main() {
  assert.equal(process.platform, 'win32', 'Packaged smoke currently targets Windows x64.');
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'qqspeed-packaged-smoke-'));
  const outputDir = path.join(tempRoot, 'out');
  const resultPath = path.join(tempRoot, 'result.json');
  const cdpAckPath = path.join(tempRoot, 'cdp-ack');
  const productName = 'QQSpeedLauncherPackagedSmoke';

  try {
    await build({
      projectDir: root,
      targets: Platform.WINDOWS.createTarget('dir', Arch.x64),
      config: {
        appId: 'local.qqspeed.launcher.packaged-smoke',
        productName,
        directories: { output: outputDir },
        extraMetadata: { main: 'smoke/packaged-smoke-main.cjs' },
        npmRebuild: false,
        compression: 'store',
        win: {
          executableName: productName,
          requestedExecutionLevel: 'asInvoker'
        },
        files: [
          {
            from: 'src/scripts',
            to: 'smoke',
            filter: ['packaged-smoke-main.cjs']
          },
          {
            from: 'resources/native',
            to: 'resources/native',
            filter: ['qqspeed-runtime.bundle']
          }
        ]
      }
    });

    const unpackedDir = path.join(outputDir, 'win-unpacked');
    const resourcesDir = path.join(unpackedDir, 'resources');
    const executablePath = path.join(unpackedDir, `${productName}.exe`);
    const gmDatabasePath = path.join(resourcesDir, GM_DATABASE_NAME);
    assert.ok(fs.existsSync(executablePath), 'Packaged smoke executable was not produced.');
    assert.ok(fs.existsSync(gmDatabasePath), 'Packaged resources are missing gm-items.sqlite3.');
    verifyReadOnlyGmDatabase(gmDatabasePath);
    assert.equal(fs.existsSync(path.join(resourcesDir, 'Native')), false,
      'Packaged resources must not contain a loose Native directory.');

    const looseNativeDlls = findForbiddenLooseDlls(resourcesDir);
    assert.deepEqual(looseNativeDlls, [],
      `Packaged resources exposed loose native DLLs: ${looseNativeDlls.join(', ')}`);

    const asarPath = path.join(resourcesDir, 'app.asar');
    assert.ok(fs.existsSync(asarPath), 'Packaged app.asar was not produced.');
    const asarEntries = listPackage(asarPath).map(normalizeAsarEntry);
    assert.equal(asarEntries.includes(GM_DATABASE_NAME), false,
      'GM database must remain outside app.asar as a direct read-only resource.');
    assert.ok(asarEntries.includes(RUNTIME_BUNDLE_ENTRY),
      `app.asar is missing ${RUNTIME_BUNDLE_ENTRY}.`);
    assert.equal(asarEntries.includes('resources/native/runtime-artifacts.json'), false,
      'app.asar still contains the legacy runtime artifact manifest.');
    const asarDllEntries = asarEntries.filter((entry) => entry.endsWith('.dll'));
    assert.deepEqual(asarDllEntries, [],
      `app.asar exposed native DLL entries: ${asarDllEntries.join(', ')}`);

    const cdpPort = await reservePort();
    const launched = startExecutable(executablePath, resultPath, cdpAckPath, cdpPort);
    let cdp = null;
    try {
      [cdp] = await Promise.all([
        waitForCdpRenderer(cdpPort),
        waitForFile(resultPath)
      ]);
      fs.writeFileSync(cdpAckPath, 'ok', 'utf8');
      await launched.exit;
    } catch (error) {
      launched.child.kill();
      await launched.exit.catch(() => {});
      throw error;
    }

    const result = JSON.parse(fs.readFileSync(resultPath, 'utf8'));
    assert.equal(result.ok, true, result.error || 'Packaged smoke failed.');
    assert.equal(result.renderer?.rendererReady, true);
    assert.equal(result.renderer?.defaultCopy, '账号已就绪，可以开始游戏。');
    assert.equal(result.utility?.receivedResult, true);
    assert.equal(result.utility?.rejectedInvalidDllPath, true);
    assert.equal(cdp?.rendererReady, true);
    assert.equal(cdp?.defaultCopy, '账号已就绪，可以开始游戏。');
    assert.equal(cdp?.protocol, 'file:');
    console.log('PACKAGED_CDP_RENDERER_UTILITY_SMOKE_OK');
  } finally {
    const resolvedTempRoot = path.resolve(tempRoot);
    const resolvedSystemTemp = path.resolve(os.tmpdir());
    if (!resolvedTempRoot.startsWith(`${resolvedSystemTemp}${path.sep}`)) {
      throw new Error(`Refusing to remove unexpected smoke directory: ${resolvedTempRoot}`);
    }
    fs.rmSync(resolvedTempRoot, { recursive: true, force: true, maxRetries: 3, retryDelay: 500 });
  }
}

main().catch((error) => {
  console.error(error?.stack || error);
  process.exitCode = 1;
});
