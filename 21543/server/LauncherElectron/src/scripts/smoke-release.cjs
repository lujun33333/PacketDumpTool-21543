const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');

const root = path.resolve(__dirname, '..', '..');
const packageJson = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
const executableName = packageJson.build?.win?.executableName
  || packageJson.build?.productName
  || packageJson.productName;
const executablePath = process.env.QQSPEED_RELEASE_EXE
  ? path.resolve(process.env.QQSPEED_RELEASE_EXE)
  : path.join(root, 'dist', 'win-unpacked', `${executableName}.exe`);

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

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

function evaluateCdp(webSocketDebuggerUrl, expression) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(webSocketDebuggerUrl);
    const timeout = setTimeout(() => {
      socket.close();
      reject(new Error('Release smoke CDP evaluation timed out.'));
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
      if (message.id !== 1) {
        return;
      }
      clearTimeout(timeout);
      socket.close();
      if (message.error || message.result?.exceptionDetails) {
        reject(new Error(`Release smoke CDP evaluation failed: ${JSON.stringify(message.error || message.result.exceptionDetails)}`));
        return;
      }
      resolve(message.result?.result?.value);
    });
    socket.addEventListener('error', () => {
      clearTimeout(timeout);
      reject(new Error('Release smoke CDP WebSocket connection failed.'));
    });
  });
}

async function waitForRenderer(port) {
  const deadline = Date.now() + 20000;
  let lastState = null;
  let lastError = null;

  while (Date.now() < deadline) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      const targets = await response.json();
      const target = targets.find((entry) => entry.type === 'page' && entry.webSocketDebuggerUrl);
      if (target) {
        lastState = await evaluateCdp(target.webSocketDebuggerUrl, `(async () => {
          const api = window.qqspeedLauncher;
          const trustedPage = location.protocol === 'file:' && Boolean(document.getElementById('loginView'));
          if (trustedPage && location.hash !== '#ipc-trust-smoke') {
            location.hash = 'ipc-trust-smoke';
            await new Promise((resolve) => setTimeout(resolve, 0));
          }
           const state = trustedPage && typeof api?.getState === 'function' ? await api.getState() : null;
           const launch = trustedPage && typeof api?.getLaunchStatus === 'function' ? await api.getLaunchStatus() : null;
           const backgroundVideo = document.getElementById('launcherBackgroundVideo');
           const backgroundSoundButton = document.getElementById('videoMuteBtn');
           return {
            href: location.href,
            protocol: location.protocol,
            title: document.title,
            preloadApi: typeof api?.getState,
            loginPresent: Boolean(document.getElementById('loginView')),
            homeStatus: document.getElementById('homeStatusText')?.textContent || '',
            gameRootText: document.getElementById('gameRootText')?.textContent || '',
            startState: document.getElementById('startBtn')?.dataset.state || '',
            launchPhase: launch?.phase || '',
            hasSavedAccount: Boolean(state?.login?.user),
             gameReady: Boolean(state?.paths?.gameRoot),
             backgroundVideo: {
               readyState: Number(backgroundVideo?.readyState || 0),
               duration: Number(backgroundVideo?.duration || 0),
               paused: Boolean(backgroundVideo?.paused),
               muted: Boolean(backgroundVideo?.muted),
               volume: Number(backgroundVideo?.volume || 0),
               source: String(backgroundVideo?.currentSrc || backgroundVideo?.src || ''),
               soundLabel: backgroundSoundButton?.getAttribute('aria-label') || ''
             },
             rendererRequire: typeof window.require,
            rendererProcess: typeof window.process
          };
        })()`);

        if (
          lastState?.protocol === 'file:' &&
          lastState?.preloadApi === 'function' &&
          lastState?.loginPresent &&
          lastState?.startState === 'unsupported' &&
           lastState?.launchPhase === 'idle' &&
           lastState?.gameReady === false &&
           lastState?.backgroundVideo?.readyState >= 2 &&
           lastState?.backgroundVideo?.duration > 9 &&
           /launcher-background\.mp4(?:$|[?#])/.test(lastState?.backgroundVideo?.source || '') &&
               /^未(?:选择|定位) QQ飞车主目录/.test(lastState?.gameRootText || '')
        ) {
          return { target, state: lastState };
        }
      }
    } catch (error) {
      lastError = error;
    }
    await delay(100);
  }

  throw new Error(
    `Real packaged renderer did not become ready. Last state: ${JSON.stringify(lastState)}. ${lastError?.message || ''}`
  );
}

function runCommand(file, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(file, args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (chunk) => { stdout = `${stdout}${chunk}`; });
    child.stderr.on('data', (chunk) => { stderr = `${stderr}${chunk}`; });
    child.once('error', reject);
    child.once('exit', (code) => {
      if (code !== 0) {
        reject(new Error(`${file} exited with code ${String(code)}. ${stderr}`));
        return;
      }
      resolve(stdout);
    });
  });
}

async function startElevatedExecutable(args) {
  const psLiteral = (value) => `'${String(value).replaceAll("'", "''")}'`;
  const script = [
    "$ErrorActionPreference='Stop'",
    `$exe=${psLiteral(executablePath)}`,
    `$cwd=${psLiteral(path.dirname(executablePath))}`,
    `$launchArgs=@(${args.map(psLiteral).join(',')})`,
    "$p=Start-Process -FilePath $exe -ArgumentList $launchArgs -WorkingDirectory $cwd -Verb RunAs -WindowStyle Hidden -PassThru",
    "Write-Output ('PID=' + $p.Id)"
  ].join(';');
  const stdout = await runCommand('powershell.exe', [
    '-NoProfile',
    '-NonInteractive',
    '-Command',
    script
  ]);
  const match = stdout.match(/PID=(\d+)/);
  if (!match) {
    throw new Error(`Elevated release smoke returned no PID. ${stdout}`);
  }
  return Number(match[1]);
}

async function isProcessRunning(pid) {
  const stdout = await runCommand('tasklist.exe', ['/FI', `PID eq ${pid}`, '/FO', 'CSV', '/NH']);
  return stdout.includes(`"${pid}"`);
}

async function waitForExit(pid, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (!await isProcessRunning(pid)) {
      return true;
    }
    await delay(100);
  }
  return false;
}

async function stopElevatedProcess(pid) {
  await runCommand('powershell.exe', [
    '-NoProfile',
    '-NonInteractive',
    '-Command',
    `$p=Start-Process -FilePath 'taskkill.exe' -ArgumentList @('/PID','${pid}','/F') -Verb RunAs -WindowStyle Hidden -PassThru -Wait; exit $p.ExitCode`
  ]).catch(() => {});
}

async function main() {
  assert.equal(process.platform, 'win32', 'Release smoke currently targets Windows x64.');
  assert.ok(fs.existsSync(executablePath), `Missing packaged executable: ${executablePath}`);

  const systemTemp = path.resolve(os.tmpdir());
  const profileRoot = fs.mkdtempSync(path.join(systemTemp, 'qqspeed-release-smoke-'));
  const port = await reservePort();
  let launchedPid = 0;

  try {
    launchedPid = await startElevatedExecutable([
      `--remote-debugging-port=${port}`,
      '--remote-debugging-address=127.0.0.1',
      `--user-data-dir=${profileRoot}`
    ]);

    const { target, state } = await waitForRenderer(port);
    assert.match(state.gameRootText, /^未(?:选择|定位) QQ飞车主目录/);
    assert.equal(state.hasSavedAccount, false);
     assert.equal(state.gameReady, false);
     assert.equal(state.backgroundVideo.muted, false);
     assert.equal(state.backgroundVideo.volume, 0.7);
     assert.match(state.backgroundVideo.soundLabel, /^关闭背景声音$/);
     assert.equal(state.rendererRequire, 'undefined');
    assert.equal(state.rendererProcess, 'undefined');

    const closeRequest = await evaluateCdp(target.webSocketDebuggerUrl, 'window.qqspeedLauncher.close()');
    assert.equal(closeRequest?.requested, true);
    const closeResult = await evaluateCdp(
      target.webSocketDebuggerUrl,
      "window.qqspeedLauncher.applyCloseAction('tray')"
    );
    assert.equal(closeResult?.closed, false);
    assert.equal(closeResult?.hidden, true);
    assert.equal(
      await isProcessRunning(launchedPid),
      true,
      'Packaged launcher should remain running after hiding to the tray.'
    );
    console.log(`REAL_RELEASE_RENDERER_PRELOAD_IPC_SMOKE_OK ${JSON.stringify(state)}`);
  } catch (error) {
    throw new Error(`${error?.stack || error}`);
  } finally {
    if (launchedPid && await isProcessRunning(launchedPid).catch(() => false)) {
      await stopElevatedProcess(launchedPid);
      await waitForExit(launchedPid, 3000).catch(() => false);
    }

    const resolvedProfileRoot = path.resolve(profileRoot);
    if (!resolvedProfileRoot.startsWith(`${systemTemp}${path.sep}`)) {
      throw new Error(`Refusing to remove unexpected smoke profile: ${resolvedProfileRoot}`);
    }
    fs.rmSync(resolvedProfileRoot, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 });
  }
}

main().catch((error) => {
  console.error(error?.stack || error);
  process.exitCode = 1;
});
