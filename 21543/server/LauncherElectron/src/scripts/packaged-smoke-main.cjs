const fs = require('node:fs');
const path = require('node:path');

if (process.parentPort) {
  process.parentPort.on('message', (event) => {
    const payload = event?.data?.payload;
    const invalidDll = path.basename(String(payload?.dllPath || '')).toLowerCase() !== 'launchercore.dll';
    process.parentPort.postMessage({
      type: 'result',
      ok: false,
      code: invalidDll ? 3 : 4,
      message: invalidDll ? 'Invalid DLL path rejected.' : 'Smoke worker never executes native code.'
    });
    setTimeout(() => process.exit(1), 10);
  });
} else {
  const { app, BrowserWindow, utilityProcess } = require('electron');

  const resultPath = String(process.env.QQSPEED_PACKAGED_SMOKE_RESULT || '');
  const cdpAckPath = String(process.env.QQSPEED_PACKAGED_SMOKE_CDP_ACK || '');
  const cdpPort = Number(process.env.QQSPEED_PACKAGED_SMOKE_CDP_PORT);
  let nativeWorker = null;
  let fixturePath = '';

  if (Number.isSafeInteger(cdpPort) && cdpPort > 0 && cdpPort <= 65535) {
    app.commandLine.appendSwitch('remote-debugging-address', '127.0.0.1');
    app.commandLine.appendSwitch('remote-debugging-port', String(cdpPort));
  }

  function writeResult(result) {
    if (!resultPath) {
      throw new Error('QQSPEED_PACKAGED_SMOKE_RESULT is missing.');
    }
    fs.writeFileSync(resultPath, JSON.stringify(result), 'utf8');
  }

  function removeFixture() {
    if (fixturePath && fs.existsSync(fixturePath)) {
      fs.unlinkSync(fixturePath);
    }
  }

  function waitForRenderer(window) {
    const deadline = Date.now() + 10000;
    return new Promise((resolve, reject) => {
      const poll = async () => {
        try {
          const state = await window.webContents.executeJavaScript(`({
            rendererReady: document.body.dataset.rendererReady === 'ok',
            defaultCopy: document.getElementById('homeStatusText')?.textContent || '',
            startState: document.getElementById('startBtn')?.dataset.state || '',
            loginVisible: !document.getElementById('loginView')?.classList.contains('hidden')
          })`, true);
          if (state.rendererReady && state.startState === 'disabled') {
            resolve(state);
            return;
          }
        } catch {
          // The renderer may still be initializing.
        }
        if (Date.now() >= deadline) {
          reject(new Error('Packaged renderer did not finish initialization.'));
          return;
        }
        setTimeout(poll, 100);
      };
      poll();
    });
  }

  function runUtilitySmoke() {
    return new Promise((resolve, reject) => {
      let settled = false;
      const finish = (callback, value) => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timeout);
        callback(value);
      };
      const timeout = setTimeout(() => {
        nativeWorker?.kill();
        finish(reject, new Error('Packaged utilityProcess smoke timed out.'));
      }, 10000);

      nativeWorker = utilityProcess.fork(__filename, ['--utility-smoke'], {
        cwd: process.resourcesPath,
        env: { ...process.env },
        serviceName: 'QQSpeed Packaged Utility Smoke',
        stdio: 'ignore'
      });
      nativeWorker.on('spawn', () => {
        nativeWorker.postMessage({
          type: 'start',
          payload: {
            dllPath: path.join(process.resourcesPath, 'NotQQSpeedNativeBridge.dll')
          }
        });
      });
      nativeWorker.on('message', (message) => {
        if (message?.type === 'result') {
          finish(resolve, {
            receivedResult: true,
            rejectedInvalidDllPath: message.ok === false && Number(message.code) === 3
          });
        }
      });
      nativeWorker.on('error', (error) => finish(reject, error));
      nativeWorker.on('exit', (code) => {
        if (!settled) {
          finish(reject, new Error(`Packaged utilityProcess exited before result (code ${String(code)}).`));
        }
      });
    });
  }

  function waitForCdpAck() {
    if (!cdpAckPath) {
      return Promise.resolve();
    }
    const deadline = Date.now() + 20000;
    return new Promise((resolve, reject) => {
      const poll = () => {
        if (fs.existsSync(cdpAckPath)) {
          resolve();
          return;
        }
        if (Date.now() >= deadline) {
          reject(new Error('Packaged CDP smoke acknowledgement timed out.'));
          return;
        }
        setTimeout(poll, 100);
      };
      poll();
    });
  }

  app.whenReady().then(async () => {
    fixturePath = path.join(app.getPath('temp'), `qqspeed-packaged-renderer-${process.pid}.html`);
    fs.writeFileSync(fixturePath, `<!doctype html>
<html lang="zh-CN"><head><meta charset="UTF-8"><title>Packaged Smoke</title></head>
<body data-renderer-ready="ok"><main id="loginView"><div id="homeStatusText">账号已就绪，可以开始游戏。</div>
<button id="startBtn" data-state="disabled">开始游戏</button></main></body></html>`, 'utf8');

    const window = new BrowserWindow({
      show: false,
      webPreferences: {
        contextIsolation: true,
        sandbox: true,
        nodeIntegration: false,
        webSecurity: true
      }
    });
    await window.loadFile(fixturePath);
    const renderer = await waitForRenderer(window);
    const utility = await runUtilitySmoke();

    if (renderer.defaultCopy !== '账号已就绪，可以开始游戏。') {
      throw new Error(`Unexpected packaged homepage copy: ${renderer.defaultCopy}`);
    }
    if (!renderer.loginVisible || !utility.receivedResult || !utility.rejectedInvalidDllPath) {
      throw new Error('Packaged renderer/utility assertions failed.');
    }

    writeResult({ ok: true, renderer, utility });
    await waitForCdpAck();
    window.destroy();
    removeFixture();
    app.exit(0);
  }).catch((error) => {
    try {
      writeResult({ ok: false, error: String(error?.stack || error) });
    } catch {
      // The runner will report a missing result file.
    }
    nativeWorker?.kill();
    removeFixture();
    app.exit(1);
  });
}
