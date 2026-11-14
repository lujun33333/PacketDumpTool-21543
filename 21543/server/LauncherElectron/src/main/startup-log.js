import {
  appendFileSync,
  existsSync,
  mkdirSync,
  renameSync,
  rmSync,
  statSync
} from 'node:fs';
import { randomUUID } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

const MAX_LOG_BYTES = 2 * 1024 * 1024;
const MAX_TEXT_LENGTH = 500;
const ALLOWED_FIELDS = new Set([
  'arch',
  'code',
  'elapsedMs',
  'errorCode',
  'exitCode',
  'isMainFrame',
  'message',
  'name',
  'platform',
  'reason',
  'serviceName',
  'signal',
  'stage',
  'status',
  'type',
  'url',
  'version'
]);

function replaceLiteralIgnoreCase(value, literal, replacement) {
  const source = String(literal || '').trim();
  if (!source) {
    return value;
  }
  const escaped = source.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return value.replace(new RegExp(escaped, 'gi'), replacement);
}

export function redactStartupText(value) {
  let text = String(value ?? '');
  const userProfile = String(process.env.USERPROFILE || '').trim();
  const home = String(process.env.HOME || '').trim();

  text = replaceLiteralIgnoreCase(text, userProfile, '%USERPROFILE%');
  if (home && home.toLowerCase() !== userProfile.toLowerCase()) {
    text = replaceLiteralIgnoreCase(text, home, '%USERPROFILE%');
  }

  text = text.replace(
    /\b(password|passwd|pwd|token|ticket|cookie|authorization|secret)\b\s*[:=]\s*(?:"[^"]*"|'[^']*'|[^\s,;]+)/gi,
    '$1=[redacted]'
  );
  return text.slice(0, MAX_TEXT_LENGTH);
}

function normalizeFieldValue(value) {
  if (typeof value === 'string') {
    return redactStartupText(value);
  }
  if (typeof value === 'number') {
    return Number.isFinite(value) ? value : String(value);
  }
  if (typeof value === 'boolean' || value === null) {
    return value;
  }
  return redactStartupText(value);
}

function getStartupLogDirectory() {
  const override = String(process.env.QQSPEED_LAUNCHER_LOG_DIR || '').trim();
  if (override) {
    return resolve(override);
  }
  const localAppData = String(process.env.LOCALAPPDATA || '').trim();
  return localAppData
    ? join(resolve(localAppData), 'QQSpeedLauncher', 'logs')
    : join(tmpdir(), 'QQSpeedLauncher', 'logs');
}

export function getStartupLogPath() {
  return join(getStartupLogDirectory(), 'startup.log');
}

function ensureRunId() {
  let runId = String(process.env.QQSPEED_LAUNCHER_RUN_ID || '').trim();
  if (!runId) {
    runId = randomUUID();
    process.env.QQSPEED_LAUNCHER_RUN_ID = runId;
  }
  return runId;
}

function ensureLogDirectory() {
  try {
    mkdirSync(getStartupLogDirectory(), { recursive: true });
    return true;
  } catch {
    return false;
  }
}

export function rotateStartupLog() {
  try {
    const logPath = getStartupLogPath();
    if (!existsSync(logPath) || statSync(logPath).size <= MAX_LOG_BYTES) {
      return false;
    }
    const backupPath = `${logPath}.1`;
    rmSync(backupPath, { force: true });
    renameSync(logPath, backupPath);
    return true;
  } catch {
    return false;
  }
}

export function initializeStartupLog() {
  ensureRunId();
  if (ensureLogDirectory()) {
    rotateStartupLog();
  }
  logStartupEvent('bootstrap', 'LOG_INITIALIZED', {
    platform: process.platform,
    arch: process.arch,
    version: process.versions.electron || process.versions.node
  });
  return {
    path: getStartupLogPath(),
    runId: process.env.QQSPEED_LAUNCHER_RUN_ID
  };
}

export function serializeStartupError(error) {
  if (!error) {
    return { name: 'Error', message: '' };
  }
  return {
    name: redactStartupText(error.name || 'Error'),
    code: redactStartupText(error.code || ''),
    message: redactStartupText(error.message || error)
  };
}

export function logStartupEvent(component, event, fields = {}) {
  try {
    if (!ensureLogDirectory()) {
      return false;
    }
    const record = {
      timestamp: new Date().toISOString(),
      run_id: ensureRunId(),
      pid: process.pid,
      ppid: process.ppid,
      component: redactStartupText(component || 'unknown'),
      event: redactStartupText(event || 'UNKNOWN')
    };
    for (const [key, value] of Object.entries(fields || {})) {
      if (!ALLOWED_FIELDS.has(key) || value === undefined) {
        continue;
      }
      record[key] = normalizeFieldValue(value);
    }
    appendFileSync(getStartupLogPath(), `${JSON.stringify(record)}\n`, { encoding: 'utf8', flag: 'a' });
    return true;
  } catch {
    return false;
  }
}
