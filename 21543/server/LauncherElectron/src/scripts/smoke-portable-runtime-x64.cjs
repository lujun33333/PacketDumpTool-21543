const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..', '..');
const PACKAGE_JSON = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
const PRODUCT_NAME = PACKAGE_JSON.build?.productName || PACKAGE_JSON.productName;
const PORTABLE_PATH = process.env.QQSPEED_RUNTIME_EXE
  ? path.resolve(process.env.QQSPEED_RUNTIME_EXE)
  : path.join(ROOT, 'dist', `${PRODUCT_NAME}-Portable-${PACKAGE_JSON.version}-x64.exe`);
const EXPECT_NATIVE_OUTER = !process.env.QQSPEED_RUNTIME_EXE;
const GAME_ROOT = String(process.env.QQSPEED_GAME_ROOT || '').trim();
const ALLOW_ADMIN_DIRECT_INNER = process.env.QQSPEED_ALLOW_ADMIN_RUNTIME_SMOKE === '1';
const TEST_USERNAME = String(process.env.QQSPEED_TEST_USERNAME || '').trim();
const TEST_PASSWORD = String(process.env.QQSPEED_TEST_PASSWORD || '');
const TEST_AUTHENTICATION = Boolean(TEST_USERNAME && TEST_PASSWORD);
const TEST_START_GAME = process.env.QQSPEED_TEST_START_GAME === '1';
const OUTER_LOG_PATH = process.env.LOCALAPPDATA
  ? path.join(process.env.LOCALAPPDATA, 'QQSpeedLauncher', 'logs', 'portable-x64.log')
  : '';
const HOOK_RUNTIME_ROOT = process.env.LOCALAPPDATA
  ? path.join(process.env.LOCALAPPDATA, 'QQSpeedLauncher', 'runtime')
  : '';
const RUNTIME_SESSIONS_ROOT = HOOK_RUNTIME_ROOT
  ? path.join(HOOK_RUNTIME_ROOT, 'sessions')
  : '';
const RUNTIME_SESSION_NAME_PATTERN = /^s-[0-9a-f]{32}$/i;

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

function reservePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      server.close((error) => error ? reject(error) : resolve(address.port));
    });
  });
}

function evaluateCdp(webSocketDebuggerUrl, expression) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(webSocketDebuggerUrl);
    const timeout = setTimeout(() => {
      socket.close();
      reject(new Error('Portable x64 CDP evaluation timed out.'));
    }, 5000);
    socket.addEventListener('open', () => {
      socket.send(JSON.stringify({
        id: 1,
        method: 'Runtime.evaluate',
        params: { expression, awaitPromise: true, returnByValue: true }
      }));
    });
    socket.addEventListener('message', (event) => {
      const message = JSON.parse(String(event.data || ''));
      if (message.id !== 1) return;
      clearTimeout(timeout);
      socket.close();
      if (message.error || message.result?.exceptionDetails) {
        reject(new Error(`Portable x64 CDP evaluation failed: ${JSON.stringify(message.error || message.result.exceptionDetails)}`));
        return;
      }
      resolve(message.result?.result?.value);
    });
    socket.addEventListener('error', () => {
      clearTimeout(timeout);
      reject(new Error('Portable x64 CDP WebSocket connection failed.'));
    });
  });
}

async function waitForRenderer(port) {
  const deadline = Date.now() + 30000;
  let lastState = null;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      const targets = await response.json();
      for (const target of targets.filter((entry) => entry.type === 'page' && entry.webSocketDebuggerUrl)) {
        lastState = await evaluateCdp(target.webSocketDebuggerUrl, `(() => ({
          protocol: location.protocol,
          title: document.title,
          loginPresent: Boolean(document.getElementById('loginView')),
          preloadApi: typeof window.qqspeedLauncher?.getState,
          rendererRequire: typeof window.require,
          rendererProcess: typeof window.process
        }))()`);
        if (lastState?.protocol === 'file:' && lastState?.loginPresent && lastState?.preloadApi === 'function') {
          return { target, state: lastState };
        }
      }
    } catch (error) {
      lastError = error;
    }
    await delay(100);
  }
  throw new Error(`Portable x64 renderer did not become ready. state=${JSON.stringify(lastState)} error=${lastError?.message || ''}`);
}

async function waitForGamePreload(target) {
  const deadline = Date.now() + 60000;
  let lastStatus = null;
  while (Date.now() < deadline) {
    lastStatus = await evaluateCdp(target.webSocketDebuggerUrl, `(async () => {
      const api = window.qqspeedLauncher;
      const status = await api.getLaunchStatus();
      const state = await api.getState();
      return {
        phase: status?.phase || '',
        progress: Number(status?.progress) || 0,
        gameProcessId: Number(status?.gameProcessId) || 0,
        message: status?.message || '',
        gameRoot: state?.paths?.gameRoot || '',
        hasSavedAccount: Boolean(state?.login?.user)
      };
    })()`);
    if (lastStatus?.phase === 'preloaded') return lastStatus;
    if (['failed', 'cancelled'].includes(lastStatus?.phase)) {
      throw new Error(`Real GameApp preload failed: ${JSON.stringify(lastStatus)}`);
    }
    await delay(100);
  }
  throw new Error(`Real GameApp preload timed out: ${JSON.stringify(lastStatus)}`);
}

async function waitForGameReady(target, expectedProcessId) {
  const deadline = Date.now() + 120000;
  let lastStatus = null;
  while (Date.now() < deadline) {
    lastStatus = await evaluateCdp(target.webSocketDebuggerUrl, `(async () => {
      const status = await window.qqspeedLauncher.getLaunchStatus();
      return {
        phase: status?.phase || '',
        progress: Number(status?.progress) || 0,
        gameProcessId: Number(status?.gameProcessId) || 0,
        message: status?.message || ''
      };
    })()`);
    if (lastStatus?.phase === 'ready') {
      assert.equal(lastStatus.gameProcessId, expectedProcessId,
        'Start Game did not reuse the preloaded GameApp process.');
      return lastStatus;
    }
    if (['failed', 'cancelled'].includes(lastStatus?.phase)) {
      throw new Error(`Real GameApp start failed: ${JSON.stringify(lastStatus)}`);
    }
    await delay(200);
  }
  throw new Error(`Real GameApp did not reach ready after Start Game: ${JSON.stringify(lastStatus)}`);
}

async function waitForAuthenticatedHome(target) {
  const deadline = Date.now() + 30000;
  let lastState = null;
  while (Date.now() < deadline) {
    lastState = await evaluateCdp(target.webSocketDebuggerUrl, `(async () => {
      const api = window.qqspeedLauncher;
      const state = await api.getState();
      const status = await api.getLaunchStatus();
      return {
        authenticated: state?.session?.authenticated === true,
        user: state?.session?.user || '',
        phase: status?.phase || '',
        loginHidden: document.getElementById('loginView')?.classList.contains('hidden') === true,
        homeVisible: document.getElementById('homeView')?.classList.contains('hidden') === false
      };
    })()`);
    if (lastState?.authenticated && lastState?.phase === 'authenticated' &&
        lastState?.loginHidden && lastState?.homeVisible) return lastState;
    if (['failed', 'cancelled'].includes(lastState?.phase)) break;
    await delay(100);
  }
  throw new Error(`Private login did not reach the authenticated home state: ${JSON.stringify({
    authenticated: lastState?.authenticated,
    phase: lastState?.phase,
    loginHidden: lastState?.loginHidden,
    homeVisible: lastState?.homeVisible
  })}`);
}

function currentTokenIsAdministrator() {
  const script = [
    '$identity=[Security.Principal.WindowsIdentity]::GetCurrent()',
    '$principal=[Security.Principal.WindowsPrincipal]::new($identity)',
    '$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)'
  ].join(';');
  const result = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script], {
    encoding: 'utf8',
    windowsHide: true
  });
  assert.equal(result.status, 0, `Unable to inspect current Windows token: ${result.stderr || ''}`);
  return String(result.stdout || '').trim().toLowerCase() === 'true';
}

function listOuterTempDirectories() {
  const temp = os.tmpdir();
  return new Set(fs.readdirSync(temp, { withFileTypes: true })
    .filter((entry) => entry.isDirectory() && /^QQSpeedLauncher-\d+-[0-9a-f]+-[0-9a-f]{2}$/i.test(entry.name))
    .map((entry) => path.join(temp, entry.name).toLowerCase()));
}

function listNativeRuntimeSessions() {
  const sessions = new Map();
  if (!RUNTIME_SESSIONS_ROOT || !fs.existsSync(RUNTIME_SESSIONS_ROOT)) return sessions;
  for (const entry of fs.readdirSync(RUNTIME_SESSIONS_ROOT, { withFileTypes: true })) {
    if (!entry.isDirectory() || entry.isSymbolicLink() ||
        !RUNTIME_SESSION_NAME_PATTERN.test(entry.name)) continue;
    const absolutePath = path.resolve(RUNTIME_SESSIONS_ROOT, entry.name);
    sessions.set(absolutePath.toLowerCase(), absolutePath);
  }
  return sessions;
}

function readRuntimeSessionLease(runtimeDirectory) {
  const leasePath = path.join(runtimeDirectory, 'runtime-lease.json');
  assert(fs.existsSync(leasePath), `Runtime session lease is missing: ${leasePath}`);
  const lease = JSON.parse(fs.readFileSync(leasePath, 'utf8'));
  assert.equal(lease?.product, 'QQSpeedLauncher', 'Runtime session lease has an unexpected product.');
  assert.equal(lease?.sessionId, path.basename(runtimeDirectory), 'Runtime session lease has an unexpected session id.');
  return lease;
}

function waitForExit(child, timeoutMs) {
  return new Promise((resolve) => {
    if (child.exitCode !== null) {
      resolve(child.exitCode);
      return;
    }
    const timeout = setTimeout(() => resolve(null), timeoutMs);
    child.once('exit', (code) => {
      clearTimeout(timeout);
      resolve(code);
    });
  });
}

function stopProcessTree(pid) {
  spawnSync('taskkill.exe', ['/PID', String(pid), '/T', '/F'], {
    encoding: 'utf8',
    windowsHide: true
  });
}

function isProcessRunning(pid) {
  if (!Number.isInteger(pid) || pid <= 0) return false;
  const result = spawnSync('tasklist.exe', ['/FI', `PID eq ${pid}`, '/FO', 'CSV', '/NH'], {
    encoding: 'utf8',
    windowsHide: true
  });
  return result.status === 0 && String(result.stdout || '').includes(`"${pid}"`);
}

function findHookLogs(pid) {
  if (!HOOK_RUNTIME_ROOT || !fs.existsSync(HOOK_RUNTIME_ROOT)) return [];
  const expectedName = `qqspeed_clean_hook_${pid}.log`.toLowerCase();
  const matches = [];
  const visit = (directory, depth) => {
    if (depth > 4) return;
    let entries = [];
    try {
      entries = fs.readdirSync(directory, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const entryPath = path.join(directory, entry.name);
      if (entry.isDirectory()) visit(entryPath, depth + 1);
      else if (entry.isFile() && entry.name.toLowerCase() === expectedName) matches.push(entryPath);
    }
  };
  visit(HOOK_RUNTIME_ROOT, 0);
  return matches;
}

async function waitForHookEvidence(pid, startedAt) {
  const deadline = Date.now() + 15000;
  const requiredMarkers = [
    'clean hook loaded',
    'network_preflight accepted=1',
    'network_hooks install_result exact=1'
  ];
  let lastEvidence = 'no per-PID Hook log';
  while (Date.now() < deadline) {
    for (const logPath of findHookLogs(pid)) {
      try {
        const stat = fs.statSync(logPath);
        if (stat.mtimeMs < startedAt - 2000) continue;
        const contents = fs.readFileSync(logPath, 'utf8');
        const missing = requiredMarkers.filter((marker) => !contents.includes(marker));
        if (missing.length === 0) return logPath;
        lastEvidence = `${path.basename(logPath)} missing ${missing.join(', ')}`;
      } catch (error) {
        lastEvidence = error?.message || String(error);
      }
    }
    await delay(100);
  }
  throw new Error(`Required Hook injection/patch evidence was not observed for GameApp PID ${pid}: ${lastEvidence}`);
}

async function main() {
  assert.equal(process.platform, 'win32', 'Portable runtime smoke requires Windows.');
  assert.equal(process.arch, 'x64', 'Portable runtime smoke requires an x64 Node host.');
  const administratorToken = currentTokenIsAdministrator();
  if (administratorToken) {
    assert(
      ALLOW_ADMIN_DIRECT_INNER && !EXPECT_NATIVE_OUTER,
      'Portable runtime smoke must run from a non-admin token unless an explicit direct-inner GameApp regression opts in.'
    );
  }
  assert.equal(Boolean(TEST_USERNAME), Boolean(TEST_PASSWORD),
    'Private login regression requires both QQSPEED_TEST_USERNAME and QQSPEED_TEST_PASSWORD.');
  assert(!TEST_AUTHENTICATION || GAME_ROOT,
    'Private login regression requires QQSPEED_GAME_ROOT so the Hook session can be authenticated.');
  assert(!TEST_START_GAME || TEST_AUTHENTICATION,
    'Start Game regression requires a successful private authentication regression.');
  assert(fs.existsSync(PORTABLE_PATH), `Missing Portable x64 artifact: ${PORTABLE_PATH}`);

  const tempRoot = path.resolve(os.tmpdir());
  const profileRoot = fs.mkdtempSync(path.join(tempRoot, 'qqspeed-portable-x64-profile-'));
  if (GAME_ROOT) {
    const gameAppPath = path.join(GAME_ROOT, 'Releasephysx27_64', 'GameApp.exe');
    assert(fs.existsSync(gameAppPath), `Configured GameApp does not exist: ${gameAppPath}`);
    fs.writeFileSync(path.join(profileRoot, 'launcher-settings.json'), `${JSON.stringify({
      gameRoot: path.resolve(GAME_ROOT),
      gameRootSource: 'user'
    }, null, 2)}\n`, 'utf8');
  }
  const initialOuterLogSize = EXPECT_NATIVE_OUTER && OUTER_LOG_PATH && fs.existsSync(OUTER_LOG_PATH)
    ? fs.statSync(OUTER_LOG_PATH).size
    : 0;
  const tempBefore = listOuterTempDirectories();
  const runtimeSessionsBefore = listNativeRuntimeSessions();
  const port = await reservePort();
  const startedAt = Date.now();
  const child = spawn(PORTABLE_PATH, [
    `--remote-debugging-port=${port}`,
    '--remote-debugging-address=127.0.0.1',
    `--user-data-dir=${profileRoot}`
  ], {
    cwd: path.dirname(PORTABLE_PATH),
    env: Object.fromEntries(Object.entries(process.env).filter(([name]) => {
      const normalizedName = name.toUpperCase();
      return normalizedName !== 'QQSPEED_TEST_USERNAME'
        && normalizedName !== 'QQSPEED_TEST_PASSWORD';
    })),
    windowsHide: true,
    stdio: 'ignore'
  });
  let gameProcessId = 0;
  let hookEvidenceVerified = false;
  let authenticationVerified = false;
  let startGameVerified = false;
  let createdRuntimeSessions = [];
  let successMessage = '';

  try {
    const { target, state } = await waitForRenderer(port);
    const rendererReadyMs = Date.now() - startedAt;
    assert.equal(state.rendererRequire, 'undefined');
    assert.equal(state.rendererProcess, 'undefined');
    // CDP can observe the DOM a few milliseconds before BrowserWindow.loadFile()
    // settles. Avoid turning the smoke's immediate close into a false ERR_FAILED.
    await delay(250);
    const tempDuring = listOuterTempDirectories();
    const createdTemp = [...tempDuring].filter((directory) => !tempBefore.has(directory));
    if (EXPECT_NATIVE_OUTER) {
      assert(createdTemp.length >= 1, 'Native x64 outer did not expose a live unique extraction directory while Electron was running.');
    }

    if (GAME_ROOT) {
      const preload = await waitForGamePreload(target);
      gameProcessId = preload.gameProcessId;
      assert.equal(preload.gameRoot.toLowerCase(), path.resolve(GAME_ROOT).toLowerCase());
      assert.equal(preload.hasSavedAccount, false, 'Isolated preload smoke must not read real saved credentials.');
      assert(gameProcessId > 0 && isProcessRunning(gameProcessId), `Preloaded GameApp PID is not alive: ${gameProcessId}`);
      assert.equal(preload.phase, 'preloaded', 'GameApp must wait in preloaded phase until the user clicks Start Game.');
      await waitForHookEvidence(gameProcessId, startedAt);
      hookEvidenceVerified = true;
      const runtimeSessionsDuring = listNativeRuntimeSessions();
      createdRuntimeSessions = [...runtimeSessionsDuring]
        .filter(([key]) => !runtimeSessionsBefore.has(key))
        .map(([, directory]) => directory);
      assert.equal(createdRuntimeSessions.length, 1,
        `Expected exactly one new native runtime session, found ${createdRuntimeSessions.length}.`);
      readRuntimeSessionLease(createdRuntimeSessions[0]);
      if (TEST_AUTHENTICATION) {
        const loginPayload = JSON.stringify({
          username: TEST_USERNAME,
          password: TEST_PASSWORD,
          remember: false,
          autoLogin: false,
          server: '64'
        });
        const loginResult = await evaluateCdp(
          target.webSocketDebuggerUrl,
          `(async () => window.qqspeedLauncher.login(${loginPayload}))()`
        );
        assert(loginResult && typeof loginResult === 'object', 'Private login returned no structured result.');
        const authenticated = await waitForAuthenticatedHome(target);
        assert.equal(authenticated.user, TEST_USERNAME, 'Authenticated session belongs to an unexpected account.');
        authenticationVerified = true;
        if (TEST_START_GAME) {
          const startResult = await evaluateCdp(
            target.webSocketDebuggerUrl,
            '(async () => window.qqspeedLauncher.startGame())()'
          );
          assert(startResult && typeof startResult === 'object', 'Start Game returned no structured result.');
            await waitForGameReady(target, gameProcessId);
            startGameVerified = true;
            const readyLease = readRuntimeSessionLease(createdRuntimeSessions[0]);
            assert.equal(readyLease.gameProcessId, gameProcessId,
              'Ready runtime session lease does not own the expected GameApp process.');
          }
      }
    }

    await evaluateCdp(target.webSocketDebuggerUrl, "window.qqspeedLauncher.applyCloseAction('quit')");
    const exitCode = await waitForExit(child, 20000);
    assert.equal(exitCode, 0, 'Native x64 outer did not exit cleanly after Electron closed.');
    if (gameProcessId > 0) {
      if (startGameVerified) {
        assert.equal(isProcessRunning(gameProcessId), true,
          `Ready GameApp unexpectedly exited with the launcher: ${gameProcessId}`);
      } else {
        const gameExitDeadline = Date.now() + 10000;
        while (Date.now() < gameExitDeadline && isProcessRunning(gameProcessId)) await delay(100);
        assert.equal(isProcessRunning(gameProcessId), false,
          `Preloaded GameApp did not exit with the launcher: ${gameProcessId}`);
      }
    }

    if (EXPECT_NATIVE_OUTER) {
      const cleanupDeadline = Date.now() + 5000;
      while (Date.now() < cleanupDeadline) {
        const remaining = listOuterTempDirectories();
        if (createdTemp.every((directory) => !remaining.has(directory))) break;
        await delay(100);
      }
      const tempAfter = listOuterTempDirectories();
      assert(createdTemp.every((directory) => !tempAfter.has(directory)), 'Native x64 outer left its extraction directory behind.');

      assert(OUTER_LOG_PATH && fs.existsSync(OUTER_LOG_PATH), 'Native x64 outer did not create portable-x64.log.');
      const fd = fs.openSync(OUTER_LOG_PATH, 'r');
      try {
        const finalSize = fs.fstatSync(fd).size;
        const appendedLength = Math.max(0, finalSize - initialOuterLogSize);
        const buffer = Buffer.alloc(appendedLength);
        fs.readSync(fd, buffer, 0, appendedLength, initialOuterLogSize);
        const appendedLog = buffer.toString('utf8');
        for (const event of ['OUTER_BEGIN', 'ARCHIVE_VERIFIED', 'ARCHIVE_EXTRACTED', 'INNER_STARTED', 'TEMP_CLEANED', 'INNER_EXITED']) {
          assert.match(appendedLog, new RegExp(`event=${event}(?:\\s|$)`), `Missing outer runtime event: ${event}`);
        }
      } finally {
        fs.closeSync(fd);
      }
    }

    successMessage = `PORTABLE_X64_RUNTIME_SMOKE_OK mode=${EXPECT_NATIVE_OUTER ? 'native-outer' : 'direct-inner'} token=${administratorToken ? 'admin-explicit' : 'standard'} rendererReadyMs=${rendererReadyMs} outerPid=${child.pid} gamePreloadPid=${gameProcessId} hookEvidence=${hookEvidenceVerified ? 1 : 0} authenticated=${authenticationVerified ? 1 : 0} startInvoked=${TEST_START_GAME ? 1 : 0} ready=${startGameVerified ? 1 : 0} runtimeSessionCleaned=1`;
  } finally {
    try {
      if (child.exitCode === null) {
        stopProcessTree(child.pid);
        await waitForExit(child, 5000);
      }
      if (gameProcessId > 0) {
        if (isProcessRunning(gameProcessId)) stopProcessTree(gameProcessId);
        const gameProcessExitDeadline = Date.now() + 10000;
        while (Date.now() < gameProcessExitDeadline && isProcessRunning(gameProcessId)) await delay(100);
        assert.equal(isProcessRunning(gameProcessId), false,
          `GameApp did not exit during smoke cleanup: ${gameProcessId}`);
      }
      if (createdRuntimeSessions.length > 0) {
        const runtimeCleanupDeadline = Date.now() + 30000;
        while (Date.now() < runtimeCleanupDeadline &&
               !createdRuntimeSessions.every((directory) => !fs.existsSync(directory))) {
          await delay(100);
        }
        assert(createdRuntimeSessions.every((directory) => !fs.existsSync(directory)),
          `Native runtime session was not cleaned: ${createdRuntimeSessions.join(', ')}`);
      }
    } finally {
      const resolvedProfile = path.resolve(profileRoot);
      assert(resolvedProfile.startsWith(`${tempRoot}${path.sep}`), `Refusing to clean unexpected profile: ${resolvedProfile}`);
      fs.rmSync(resolvedProfile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
    }
  }
  console.log(successMessage);
}

main().catch((error) => {
  console.error(error?.stack || error);
  process.exitCode = 1;
});
