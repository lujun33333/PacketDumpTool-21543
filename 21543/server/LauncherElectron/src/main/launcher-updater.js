import { createHash, verify } from 'node:crypto';
import { spawn } from 'node:child_process';
import { createWriteStream } from 'node:fs';
import { mkdir, readFile, rename, rm } from 'node:fs/promises';
import { basename, dirname, join } from 'node:path';
import { Readable, Transform } from 'node:stream';
import { pipeline } from 'node:stream/promises';

export const UPDATE_MANIFEST_URL = 'http://43.226.61.100:28881/update.json';
export const UPDATE_MANIFEST_URLS = Object.freeze([
  UPDATE_MANIFEST_URL,
  'http://114.132.178.23:28881/update.json'
]);
const MANIFEST_MAX_BYTES = 64 * 1024;
const CHECK_TIMEOUT_MS = 10_000;
const CHECK_MAX_ATTEMPTS = 3;
const CHECK_RETRY_DELAYS_MS = Object.freeze([500, 1_500]);
const DOWNLOAD_TIMEOUT_MS = 15 * 60_000;
const SAFE_INSTALLER_NAME = /^[A-Za-z0-9][A-Za-z0-9._-]{0,199}\.exe$/i;
const SHA256_HEX = /^[a-f0-9]{64}$/;
const VERSION_PATTERN = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;

function normalizeVersion(value) {
  const normalized = String(value || '').trim();
  const match = VERSION_PATTERN.exec(normalized);
  if (!match) {
    throw new Error('登录器更新版本号格式不正确。');
  }
  return {
    value: normalized,
    parts: match.slice(1).map((part) => Number(part))
  };
}

export function compareLauncherVersions(left, right) {
  const leftParts = normalizeVersion(left).parts;
  const rightParts = normalizeVersion(right).parts;
  for (let index = 0; index < 3; index += 1) {
    if (leftParts[index] !== rightParts[index]) {
      return leftParts[index] < rightParts[index] ? -1 : 1;
    }
  }
  return 0;
}

export function canonicalUpdatePayload(manifest) {
  return Buffer.from(JSON.stringify({
    version: manifest?.version,
    required: manifest?.required,
    file: manifest?.file,
    sha256: manifest?.sha256
  }), 'utf8');
}

function validateManifestFields(manifest) {
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
    throw new Error('登录器更新配置格式不正确。');
  }
  const version = normalizeVersion(manifest.version).value;
  if (typeof manifest.required !== 'boolean') {
    throw new Error('登录器更新策略格式不正确。');
  }
  const file = String(manifest.file || '').trim();
  if (!SAFE_INSTALLER_NAME.test(file) || basename(file) !== file) {
    throw new Error('登录器更新文件名不安全。');
  }
  const sha256 = String(manifest.sha256 || '').trim().toLowerCase();
  if (!SHA256_HEX.test(sha256)) {
    throw new Error('登录器更新 SHA256 格式不正确。');
  }
  const signature = String(manifest.signature || '').trim();
  if (!signature || !/^[A-Za-z0-9+/]+={0,2}$/.test(signature)) {
    throw new Error('登录器更新签名格式不正确。');
  }
  return { version, required: manifest.required, file, sha256, signature };
}

export function evaluateSignedUpdateManifest(manifest, { currentVersion, publicKey }) {
  const normalized = validateManifestFields(manifest);
  let signatureBytes;
  try {
    signatureBytes = Buffer.from(normalized.signature, 'base64');
  } catch {
    throw new Error('登录器更新签名格式不正确。');
  }
  const signed = verify(
    null,
    canonicalUpdatePayload({
      version: normalized.version,
      required: normalized.required,
      file: normalized.file,
      sha256: normalized.sha256
    }),
    publicKey,
    signatureBytes
  );
  if (!signed) {
    throw new Error('登录器更新签名验证失败。');
  }
  if (!normalized.required || compareLauncherVersions(currentVersion, normalized.version) >= 0) {
    return { status: 'current', version: normalized.version };
  }
  return {
    status: 'required',
    version: normalized.version,
    file: normalized.file,
    sha256: normalized.sha256
  };
}

function timeoutSignal(timeoutMs) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  timer.unref?.();
  return {
    signal: controller.signal,
    clear: () => clearTimeout(timer)
  };
}

function readResponseLength(response) {
  const value = Number.parseInt(response.headers.get('content-length') || '', 10);
  return Number.isSafeInteger(value) && value >= 0 ? value : 0;
}

function reportProgress(onProgress, progress) {
  if (typeof onProgress !== 'function') {
    return;
  }
  try {
    onProgress(progress);
  } catch {
    // A presentation callback must never invalidate an otherwise valid update.
  }
}

async function readLimitedResponse(response, maxBytes) {
  if (!response.body) {
    throw new Error('更新服务器返回了空响应。');
  }
  const chunks = [];
  let total = 0;
  for await (const chunk of Readable.fromWeb(response.body)) {
    total += chunk.length;
    if (total > maxBytes) {
      throw new Error('更新配置大小超过限制。');
    }
    chunks.push(chunk);
  }
  return Buffer.concat(chunks, total);
}

function waitForRetry(milliseconds) {
  const delay = Math.max(0, Number(milliseconds) || 0);
  return delay > 0
    ? new Promise((resolve) => setTimeout(resolve, delay))
    : Promise.resolve();
}

function markRetryable(error) {
  error.retryable = true;
  return error;
}

export async function fetchSignedUpdateManifest({
  manifestUrl = UPDATE_MANIFEST_URL,
  fetchImpl = globalThis.fetch,
  retryDelaysMs = CHECK_RETRY_DELAYS_MS,
  maxAttempts = CHECK_MAX_ATTEMPTS
} = {}) {
  const retryDelays = Array.isArray(retryDelaysMs)
    ? retryDelaysMs
    : CHECK_RETRY_DELAYS_MS;
  const attemptLimit = Number.isSafeInteger(maxAttempts) && maxAttempts > 0
    ? Math.min(maxAttempts, CHECK_MAX_ATTEMPTS)
    : CHECK_MAX_ATTEMPTS;
  let lastError = null;

  for (let attempt = 1; attempt <= attemptLimit; attempt += 1) {
    const timeout = timeoutSignal(CHECK_TIMEOUT_MS);
    try {
      const response = await fetchImpl(manifestUrl, {
        method: 'GET',
        cache: 'no-store',
        redirect: 'error',
        signal: timeout.signal
      });
      if (!response.ok) {
        const error = new Error(`更新服务器返回状态 ${response.status}。`);
        if ([408, 425, 429].includes(response.status) || response.status >= 500) {
          markRetryable(error);
        }
        throw error;
      }
      const bytes = await readLimitedResponse(response, MANIFEST_MAX_BYTES);
      try {
        return JSON.parse(bytes.toString('utf8'));
      } catch {
        throw new Error('更新服务器返回的配置无法解析。');
      }
    } catch (error) {
      lastError = error?.name === 'AbortError'
        ? markRetryable(new Error('连接更新服务器超时。', { cause: error }))
        : error;
      const retryable = lastError?.retryable === true || lastError instanceof TypeError;
      if (!retryable || attempt >= attemptLimit) {
        if (retryable && attempt >= attemptLimit && attemptLimit > 1) {
          throw new Error(
            `${String(lastError?.message || '连接更新服务器失败。')}（已自动尝试 ${attemptLimit} 次）`,
            { cause: lastError }
          );
        }
        throw lastError;
      }
    } finally {
      timeout.clear();
    }
    await waitForRetry(retryDelays[attempt - 1]);
  }

  throw lastError || new Error('连接更新服务器失败。');
}

function normalizeManifestUrls(manifestUrl, manifestUrls) {
  const candidates = manifestUrl === undefined
    ? manifestUrls
    : [manifestUrl];
  if (!Array.isArray(candidates) || candidates.length === 0) {
    throw new Error('没有配置登录器更新服务器。');
  }

  const uniqueUrls = [];
  const seen = new Set();
  for (const candidate of candidates) {
    let parsed;
    try {
      parsed = new URL(candidate);
    } catch {
      throw new Error('登录器更新地址格式不正确。');
    }
    if (!['http:', 'https:'].includes(parsed.protocol)) {
      throw new Error('登录器更新地址不受支持。');
    }
    if (!seen.has(parsed.href)) {
      seen.add(parsed.href);
      uniqueUrls.push(parsed.href);
    }
  }
  return uniqueUrls;
}

function orderDownloadSources(manifestUrls, randomImpl) {
  if (manifestUrls.length < 2) {
    return [...manifestUrls];
  }
  let sample = 0;
  try {
    const candidate = Number(randomImpl());
    if (Number.isFinite(candidate) && candidate >= 0 && candidate < 1) {
      sample = candidate;
    }
  } catch {
    // Keep the primary source first if random selection is unavailable.
  }
  const preferredIndex = Math.floor(sample * manifestUrls.length);
  return [
    manifestUrls[preferredIndex],
    ...manifestUrls.filter((_url, index) => index !== preferredIndex)
  ];
}

async function fetchVerifiedManifestFromSources({
  manifestUrls,
  currentVersion,
  publicKey,
  fetchImpl
}) {
  if (manifestUrls.length === 1) {
    const manifestUrl = manifestUrls[0];
    const manifest = await fetchSignedUpdateManifest({ manifestUrl, fetchImpl });
    return {
      decision: evaluateSignedUpdateManifest(manifest, { currentVersion, publicKey }),
      manifestUrl
    };
  }

  let lastErrors = [];
  for (let attempt = 1; attempt <= CHECK_MAX_ATTEMPTS; attempt += 1) {
    try {
      return await Promise.any(manifestUrls.map(async (manifestUrl) => {
        const manifest = await fetchSignedUpdateManifest({
          manifestUrl,
          fetchImpl,
          maxAttempts: 1
        });
        return {
          decision: evaluateSignedUpdateManifest(manifest, { currentVersion, publicKey }),
          manifestUrl
        };
      }));
    } catch (error) {
      lastErrors = error instanceof AggregateError ? error.errors : [error];
      if (attempt < CHECK_MAX_ATTEMPTS) {
        await waitForRetry(CHECK_RETRY_DELAYS_MS[attempt - 1]);
      }
    }
  }

  const details = lastErrors
    .map((error) => String(error?.message || error || '未知错误'))
    .join('；');
  throw new Error(
    `主、备用更新服务器均不可用（已自动尝试 ${CHECK_MAX_ATTEMPTS} 次）${details ? `：${details}` : '。'}`,
    { cause: new AggregateError(lastErrors, 'All update manifest sources failed') }
  );
}

export async function downloadVerifiedInstaller({
  url,
  destination,
  sha256,
  onProgress,
  fetchImpl = globalThis.fetch
}) {
  if (!SHA256_HEX.test(String(sha256 || '').toLowerCase())) {
    throw new Error('登录器更新 SHA256 格式不正确。');
  }
  const parsedUrl = new URL(url);
  if (!['http:', 'https:'].includes(parsedUrl.protocol)) {
    throw new Error('登录器更新地址不受支持。');
  }
  await mkdir(dirname(destination), { recursive: true });
  const partialPath = `${destination}.part`;
  await rm(partialPath, { force: true });
  await rm(destination, { force: true });

  const timeout = timeoutSignal(DOWNLOAD_TIMEOUT_MS);
  try {
    const response = await fetchImpl(parsedUrl, {
      method: 'GET',
      cache: 'no-store',
      redirect: 'error',
      signal: timeout.signal
    });
    if (!response.ok || !response.body) {
      throw new Error(`更新文件下载失败，服务器状态 ${response.status}。`);
    }

    const totalBytes = readResponseLength(response);
    const startedAt = Date.now();
    let downloadedBytes = 0;
    let lastProgressAt = 0;
    const emitDownloadProgress = (force = false) => {
      const now = Date.now();
      if (!force && now - lastProgressAt < 200) {
        return;
      }
      lastProgressAt = now;
      const elapsedSeconds = Math.max((now - startedAt) / 1000, 0.001);
      reportProgress(onProgress, {
        phase: 'downloading',
        downloadedBytes,
        totalBytes,
        percent: totalBytes > 0
          ? Math.min(100, Math.round((downloadedBytes / totalBytes) * 100))
          : 0,
        bytesPerSecond: Math.round(downloadedBytes / elapsedSeconds)
      });
    };
    emitDownloadProgress(true);

    const hash = createHash('sha256');
    const digestingStream = new Transform({
      transform(chunk, _encoding, callback) {
        hash.update(chunk);
        downloadedBytes += chunk.length;
        emitDownloadProgress(totalBytes > 0 && downloadedBytes >= totalBytes);
        callback(null, chunk);
      }
    });
    await pipeline(
      Readable.fromWeb(response.body),
      digestingStream,
      createWriteStream(partialPath, { flags: 'wx' })
    );
    emitDownloadProgress(true);
    reportProgress(onProgress, {
      phase: 'verifying',
      downloadedBytes,
      totalBytes: totalBytes || downloadedBytes,
      percent: 100,
      bytesPerSecond: 0
    });

    const actualHash = hash.digest('hex');
    if (actualHash !== sha256.toLowerCase()) {
      throw new Error('登录器更新文件校验失败。');
    }
    await rename(partialPath, destination);
    return destination;
  } catch (error) {
    await rm(partialPath, { force: true }).catch(() => {});
    if (error?.name === 'AbortError') {
      throw new Error('登录器更新下载超时。');
    }
    throw error;
  } finally {
    timeout.clear();
  }
}

export function launchUpdateInstaller(installerPath, { spawnImpl = spawn } = {}) {
  const child = spawnImpl(installerPath, ['/S', '--updated', '--force-run'], {
    detached: true,
    stdio: 'ignore',
    windowsHide: false
  });
  return new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('spawn', () => {
      child.unref();
      resolve(child);
    });
  });
}

export async function runMandatoryLauncherUpdate({
  currentVersion,
  publicKeyPath,
  downloadDirectory,
  manifestUrl,
  manifestUrls = UPDATE_MANIFEST_URLS,
  fetchImpl = globalThis.fetch,
  randomImpl = Math.random,
  spawnImpl = spawn,
  onUpdateRequired,
  onProgress
}) {
  const sourceUrls = normalizeManifestUrls(manifestUrl, manifestUrls);
  const publicKey = await readFile(publicKeyPath, 'utf8');
  const { decision } = await fetchVerifiedManifestFromSources({
    manifestUrls: sourceUrls,
    currentVersion,
    publicKey,
    fetchImpl
  });
  if (decision.status === 'current') {
    return decision;
  }

  if (typeof onUpdateRequired === 'function') {
    await onUpdateRequired(decision);
  }

  const orderedManifestUrls = orderDownloadSources(sourceUrls, randomImpl);
  const destination = join(downloadDirectory, decision.file);
  let latestProgress = {
    downloadedBytes: 0,
    totalBytes: 0,
    percent: 0,
    bytesPerSecond: 0
  };
  let lastDownloadError = null;
  for (const sourceManifestUrl of orderedManifestUrls) {
    try {
      await downloadVerifiedInstaller({
        url: new URL(decision.file, sourceManifestUrl).href,
        destination,
        sha256: decision.sha256,
        fetchImpl,
        onProgress(progress) {
          latestProgress = {
            downloadedBytes: progress.downloadedBytes,
            totalBytes: progress.totalBytes,
            percent: progress.percent,
            bytesPerSecond: progress.bytesPerSecond
          };
          reportProgress(onProgress, {
            ...progress,
            version: decision.version
          });
        }
      });
      lastDownloadError = null;
      break;
    } catch (error) {
      lastDownloadError = error;
    }
  }
  if (lastDownloadError) {
    throw new Error('主、备用更新服务器的安装包下载均失败。', {
      cause: lastDownloadError
    });
  }
  reportProgress(onProgress, {
    phase: 'installing',
    version: decision.version,
    ...latestProgress,
    bytesPerSecond: 0
  });
  await launchUpdateInstaller(destination, { spawnImpl });
  return {
    status: 'installing',
    version: decision.version,
    installerPath: destination
  };
}
