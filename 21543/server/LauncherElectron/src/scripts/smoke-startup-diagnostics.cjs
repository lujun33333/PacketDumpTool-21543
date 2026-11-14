const assert = require('node:assert/strict');
const { mkdtempSync, readFileSync, rmSync, writeFileSync, existsSync, statSync } = require('node:fs');
const { tmpdir } = require('node:os');
const { join } = require('node:path');
const { pathToFileURL } = require('node:url');

async function main() {
  const projectRoot = join(__dirname, '..', '..');
  const tempRoot = mkdtempSync(join(tmpdir(), 'qqspeed-startup-log-'));
  const originalLogDir = process.env.QQSPEED_LAUNCHER_LOG_DIR;
  const originalRunId = process.env.QQSPEED_LAUNCHER_RUN_ID;
  const originalUserProfile = process.env.USERPROFILE;

  try {
    process.env.QQSPEED_LAUNCHER_LOG_DIR = tempRoot;
    process.env.QQSPEED_LAUNCHER_RUN_ID = 'smoke-run-id';
    process.env.USERPROFILE = join(tempRoot, 'SensitiveUser');

    const moduleUrl = `${pathToFileURL(join(projectRoot, 'src', 'main', 'startup-log.js')).href}?smoke=${Date.now()}`;
    const diagnostics = await import(moduleUrl);
    const {
      getStartupLogPath,
      initializeStartupLog,
      logStartupEvent,
      redactStartupText,
      rotateStartupLog,
      serializeStartupError
    } = diagnostics;

    assert.equal(typeof initializeStartupLog, 'function');
    assert.equal(typeof logStartupEvent, 'function');
    assert.equal(typeof redactStartupText, 'function');
    assert.equal(typeof serializeStartupError, 'function');

    initializeStartupLog();
    logStartupEvent('smoke', 'SENSITIVE_SAMPLE', {
      message: `password=plain-secret token:abc123 path=${process.env.USERPROFILE}\\file.txt`,
      password: 'must-not-be-written',
      code: 7
    });

    const logPath = getStartupLogPath();
    assert.equal(existsSync(logPath), true, 'startup.log should be created');
    const lines = readFileSync(logPath, 'utf8').trim().split(/\r?\n/).map((line) => JSON.parse(line));
    const sample = lines.find((line) => line.event === 'SENSITIVE_SAMPLE');
    assert.ok(sample, 'sample event should be present');
    assert.equal(sample.run_id, 'smoke-run-id');
    assert.equal(sample.component, 'smoke');
    assert.equal(sample.code, 7);
    assert.equal('password' in sample, false, 'non-whitelisted field must be discarded');

    const serialized = JSON.stringify(sample);
    for (const forbidden of ['plain-secret', 'abc123', 'must-not-be-written', process.env.USERPROFILE]) {
      assert.equal(serialized.includes(forbidden), false, `log leaked ${forbidden}`);
    }
    assert.match(sample.message, /\[redacted\]/);
    assert.match(sample.message, /%USERPROFILE%/);

    const safeError = serializeStartupError(Object.assign(new Error(`ticket=secret-ticket ${process.env.USERPROFILE}`), { code: 'E_SMOKE' }));
    assert.equal(safeError.code, 'E_SMOKE');
    assert.equal(JSON.stringify(safeError).includes('secret-ticket'), false);
    assert.equal(JSON.stringify(safeError).includes(process.env.USERPROFILE), false);
    assert.equal(redactStartupText(process.env.USERPROFILE.toUpperCase()).includes(tempRoot.toUpperCase()), false, 'user profile redaction must be case-insensitive');

    writeFileSync(logPath, Buffer.alloc((2 * 1024 * 1024) + 32, 0x41));
    rotateStartupLog();
    assert.equal(existsSync(`${logPath}.1`), true, 'oversized log should be rotated');
    assert.ok(statSync(`${logPath}.1`).size > 2 * 1024 * 1024);
    assert.equal(existsSync(logPath), false, 'new log is created lazily after rotation');

    logStartupEvent('smoke', 'AFTER_ROTATE', { status: 'ok' });
    assert.equal(existsSync(logPath), true);
    assert.match(readFileSync(logPath, 'utf8'), /AFTER_ROTATE/);

    const packageJson = JSON.parse(readFileSync(join(projectRoot, 'package.json'), 'utf8'));
    assert.equal(packageJson.main, 'src/main/bootstrap.js', 'Electron must enter through the diagnostic bootstrap');
    assert.equal(packageJson.build?.portable, undefined, 'The retired x86 NSIS portable configuration must be removed');
    assert.doesNotMatch(packageJson.scripts?.['dist:portable'] || '', /electron-builder\s+--win\s+portable/i, 'Portable builds must not restore the x86 NSIS wrapper');
    assert.match(packageJson.scripts?.['dist:portable'] || '', /build-portable-x64\.cjs/, 'Portable builds must use the native x64 wrapper');
    assert.equal(packageJson.build?.win?.requestedExecutionLevel, 'asInvoker', 'Inner Electron must open without an elevation prompt');

    const bootstrapSource = readFileSync(join(projectRoot, 'src', 'main', 'bootstrap.js'), 'utf8');
    const mainSource = readFileSync(join(projectRoot, 'src', 'main', 'index.js'), 'utf8');
    const workerSource = readFileSync(join(projectRoot, 'src', 'main', 'native-worker.js'), 'utf8');
    const lifecycleEvents = [
      'MAIN_IMPORT_BEGIN',
      'MAIN_IMPORT_OK',
      'MAIN_IMPORT_FAILED',
      'SINGLE_INSTANCE_LOCK_ACQUIRED',
      'SINGLE_INSTANCE_LOCK_REJECTED',
      'APP_READY_BEGIN',
      'APP_READY_OK',
      'APP_READY_FAILED',
      'WINDOW_CREATE_BEGIN',
      'WINDOW_CREATED',
      'RENDERER_LOAD_BEGIN',
      'RENDERER_LOAD_OK',
      'RENDERER_LOAD_FAILED',
      'WINDOW_READY_TO_SHOW',
      'WINDOW_SHOWN',
      'RENDERER_PROCESS_GONE',
      'WINDOW_UNRESPONSIVE',
      'CHILD_PROCESS_GONE',
      'APP_BEFORE_QUIT',
      'APP_PROCESS_EXIT'
    ];
    const lifecycleSource = `${bootstrapSource}\n${mainSource}`;
    for (const eventName of lifecycleEvents) {
      assert.match(lifecycleSource, new RegExp(`['\"]${eventName}['\"]`), `missing lifecycle event ${eventName}`);
    }

    const nativeEvents = [
      'NATIVE_WORKER_PREPARE_BEGIN',
      'NATIVE_WORKER_PREPARE_FAILED',
      'NATIVE_WORKER_SPAWNED',
      'NATIVE_WORKER_STARTED',
      'NATIVE_WORKER_ERROR',
      'NATIVE_WORKER_EXIT',
      'NATIVE_SESSION_BEGIN',
      'NATIVE_MEMORY_MAPPING_OK',
      'GAMEAPP_PRELOADED',
      'NATIVE_CONTEXT_FAILED',
      'NATIVE_SESSION_END'
    ];
    const nativeSource = `${mainSource}\n${workerSource}`;
    for (const eventName of nativeEvents) {
      assert.match(nativeSource, new RegExp(`['\"]${eventName}['\"]`), `missing native event ${eventName}`);
    }

    const collectorPath = join(projectRoot, 'diagnostics', 'QQSpeedLauncher-Startup-Diagnose.ps1');
    const collectorCmdPath = join(projectRoot, 'diagnostics', '运行启动诊断.cmd');
    assert.equal(existsSync(collectorPath), true, 'PowerShell collector must exist');
    assert.equal(existsSync(collectorCmdPath), true, 'one-click diagnostic command must exist');
    const collectorSource = readFileSync(collectorPath, 'utf8');
    const collectorCmdSource = readFileSync(collectorCmdPath, 'utf8');
    for (const required of ['NoLaunch', 'Start-Process', 'Get-WinEvent', 'Compress-Archive', 'summary.txt', 'processes.jsonl']) {
      assert.match(collectorSource, new RegExp(required), `collector missing ${required}`);
    }
    for (const compatibilityEvidence of [
      'Get-PeMachine',
      'LauncherMachine',
      'OperatingSystemIs64Bit',
      'Wow64CmdExists',
      'Wow64CmdLaunch',
      'UacEnableLUA',
      'UacConsentPromptBehaviorAdmin',
      'PORTABLE_X86_WOW64_UNAVAILABLE'
    ]) {
      assert.match(
        collectorSource,
        new RegExp(compatibilityEvidence),
        `collector missing compatibility evidence ${compatibilityEvidence}`
      );
    }
    for (const elevationFallback of ['seenProcessIds', 'timestampMatches', 'processMatches']) {
      assert.match(collectorSource, new RegExp(elevationFallback), `collector missing elevated-child correlation ${elevationFallback}`);
    }
    assert.match(collectorSource, /fatalChildFailure/, 'collector must prioritize renderer/GPU failure over a prior WINDOW_SHOWN event');
    assert.match(collectorCmdSource, /powershell\.exe/i);
    assert.match(collectorCmdSource, /-ExecutionPolicy\s+Bypass/i);
    for (const forbidden of [
      /Set-MpPreference/i,
      /Add-MpPreference/i,
      /Remove-MpPreference/i,
      /Get-Credential/i,
      /Copy-Item[^\r\n]*\.dll/i,
      /\.dmp[^\r\n]*Copy-Item/i
    ]) {
      assert.equal(forbidden.test(collectorSource), false, `collector contains forbidden operation ${forbidden}`);
    }

    console.log('startup diagnostics smoke passed');
  } finally {
    if (originalLogDir === undefined) delete process.env.QQSPEED_LAUNCHER_LOG_DIR;
    else process.env.QQSPEED_LAUNCHER_LOG_DIR = originalLogDir;
    if (originalRunId === undefined) delete process.env.QQSPEED_LAUNCHER_RUN_ID;
    else process.env.QQSPEED_LAUNCHER_RUN_ID = originalRunId;
    if (originalUserProfile === undefined) delete process.env.USERPROFILE;
    else process.env.USERPROFILE = originalUserProfile;
    rmSync(tempRoot, { recursive: true, force: true });
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
