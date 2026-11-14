'use strict';

const {
  existsSync,
  mkdirSync,
  readFileSync,
  renameSync,
  statSync,
  unlinkSync,
  writeFileSync
} = require('node:fs');
const { createHash } = require('node:crypto');
const { dirname, resolve } = require('node:path');
const { pathToFileURL } = require('node:url');

const projectRoot = resolve(__dirname, '..', '..');
const defaultBridgeInput = resolve(
  projectRoot,
  'native',
  'NativeBridge',
  'build',
  'QQSpeedNativeBridge',
  'x64',
  'Release',
  'out',
  'QQSpeedNativeBridge.dll'
);
const defaultHookInput = resolve(projectRoot, 'resources', 'native', 'qqspeed_clean_hook_64.dll');
const defaultOutput = resolve(projectRoot, 'resources', 'native', 'qqspeed-runtime.bundle');
const codecUrl = pathToFileURL(resolve(projectRoot, 'src', 'shared', 'native-runtime-bundle.js')).href;

let codecPromise;
function loadCodec() {
  codecPromise ||= import(codecUrl);
  return codecPromise;
}

function resolveSecret(options) {
  if (!Buffer.isBuffer(options?.contentKey) || options.contentKey.length !== 32 ||
      !Buffer.isBuffer(options?.bundleId) || options.bundleId.length !== 32) {
    throw new Error('Runtime bundle contentKey and bundleId must be supplied as in-memory 32-byte Buffers.');
  }
  return {
    contentKey: options.contentKey,
    bundleId: options.bundleId
  };
}

function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

function resolveBuildPaths(options = {}) {
  return {
    bridgeInput: resolve(options.bridgeInput || options.input || defaultBridgeInput),
    hookInput: resolve(options.hookInput || defaultHookInput),
    output: resolve(options.output || defaultOutput)
  };
}

function resolveVerifyPaths(options = {}) {
  return {
    bridgeInput: options.bridgeInput || options.input ? resolve(options.bridgeInput || options.input) : '',
    hookInput: options.hookInput === '' ? '' : resolve(options.hookInput || defaultHookInput),
    output: resolve(options.output || defaultOutput)
  };
}

async function decodeRuntimeBundleFile(options = {}) {
  const { decodeNativeRuntimeBundle } = await loadCodec();
  const output = resolve(options.output || defaultOutput);
  const secret = resolveSecret(options);
  const bundleBytes = readFileSync(output);
  let decoded = null;
  try {
    decoded = decodeNativeRuntimeBundle(bundleBytes, { contentKey: secret.contentKey });
    if (!decoded.bundleId.equals(secret.bundleId)) {
      throw new Error('Runtime bundle bundleId does not match the supplied release identity.');
    }
    return decoded;
  } catch (error) {
    decoded?.bundleId.fill(0);
    decoded?.bridgeBytes.fill(0);
    decoded?.hookBytes.fill(0);
    throw error;
  } finally {
    bundleBytes.fill(0);
  }
}

async function buildRuntimeBundle(options = {}) {
  const { encodeNativeRuntimeBundle } = await loadCodec();
  const { bridgeInput, hookInput, output } = resolveBuildPaths(options);
  if (!existsSync(bridgeInput)) {
    throw new Error(`Transient NativeBridge build output is missing: ${bridgeInput}`);
  }
  if (!existsSync(hookInput)) {
    throw new Error(`Protected Hook build input is missing: ${hookInput}`);
  }
  const secret = resolveSecret(options);
  const bridgeBytes = readFileSync(bridgeInput);
  const hookBytes = readFileSync(hookInput);
  let bundleBytes;
  try {
    bundleBytes = encodeNativeRuntimeBundle(
      { bridgeBytes, hookBytes },
      {
        contentKey: secret.contentKey,
        bundleId: secret.bundleId,
        nonce: options.nonce
      }
    );
  } finally {
    bridgeBytes.fill(0);
    hookBytes.fill(0);
  }

  try {
    mkdirSync(dirname(output), { recursive: true });
    const temporary = `${output}.${process.pid}.tmp`;
    try {
      writeFileSync(temporary, bundleBytes, { flag: 'wx', mode: 0o600 });
      try {
        unlinkSync(output);
      } catch (error) {
        if (error?.code !== 'ENOENT') throw error;
      }
      renameSync(temporary, output);
    } finally {
      try {
        unlinkSync(temporary);
      } catch {
        // Successful rename consumed the temporary output.
      }
    }
  } finally {
    bundleBytes.fill(0);
  }
  return verifyRuntimeBundle({ ...options, bridgeInput, hookInput, output });
}

async function verifyRuntimeBundle(options = {}) {
  const { bridgeInput, hookInput, output } = resolveVerifyPaths(options);
  const hasReleaseMaterial = options.contentKey !== undefined || options.bundleId !== undefined;
  let bridgeBytes = null;
  let hookBytes = null;
  let decoded = null;
  let inspected = null;
  let bundleBytes = null;
  try {
    if (bridgeInput) bridgeBytes = readFileSync(bridgeInput);
    if (hookInput) hookBytes = readFileSync(hookInput);
    if (hasReleaseMaterial) {
      decoded = await decodeRuntimeBundleFile({ ...options, output });
      if (bridgeBytes && !decoded.bridgeBytes.equals(bridgeBytes)) {
        throw new Error('Runtime bundle Bridge payload does not match the current native build.');
      }
      if (hookBytes && !decoded.hookBytes.equals(hookBytes)) {
        throw new Error('Runtime bundle Hook payload does not match the protected build input.');
      }
      return {
        authenticated: true,
        bridgeInput: bridgeInput || null,
        hookInput: hookInput || null,
        output,
        bundleId: decoded.bundleId.toString('hex'),
        bridgeSize: decoded.bridgeBytes.length,
        hookSize: decoded.hookBytes.length,
        bundleSize: statSync(output).size,
        bridgeSha256: decoded.bridgeSha256,
        hookSha256: decoded.hookSha256,
        bundleSha256: decoded.bundleSha256
      };
    }

    const { inspectNativeRuntimeBundle } = await loadCodec();
    bundleBytes = readFileSync(output);
    inspected = inspectNativeRuntimeBundle(bundleBytes);
    if (bridgeBytes &&
        (bridgeBytes.length !== inspected.bridgeSize || sha256(bridgeBytes) !== inspected.bridgeSha256)) {
      throw new Error('Runtime bundle Bridge descriptor does not match the current native build.');
    }
    if (hookBytes &&
        (hookBytes.length !== inspected.hookSize || sha256(hookBytes) !== inspected.hookSha256)) {
      throw new Error('Runtime bundle Hook descriptor does not match the protected build input.');
    }
    return {
      authenticated: false,
      bridgeInput: bridgeInput || null,
      hookInput: hookInput || null,
      output,
      bundleId: inspected.bundleId.toString('hex'),
      bridgeSize: inspected.bridgeSize,
      hookSize: inspected.hookSize,
      bundleSize: statSync(output).size,
      bridgeSha256: inspected.bridgeSha256,
      hookSha256: inspected.hookSha256,
      bundleSha256: inspected.bundleSha256
    };
  } finally {
    bridgeBytes?.fill(0);
    hookBytes?.fill(0);
    bundleBytes?.fill(0);
    inspected?.bundleId.fill(0);
    decoded?.bundleId.fill(0);
    decoded?.bridgeBytes.fill(0);
    decoded?.hookBytes.fill(0);
  }
}

async function main(argv = process.argv.slice(2)) {
  const unknown = argv.filter((argument) => argument !== '--verify-only');
  if (unknown.length > 0) throw new Error(`Unknown argument: ${unknown.join(' ')}`);
  const verifyOnly = argv.includes('--verify-only');
  const result = verifyOnly
    ? await verifyRuntimeBundle()
    : await buildRuntimeBundle();
  process.stdout.write(
    `${verifyOnly ? 'RUNTIME_BUNDLE_VERIFY_OK' : 'RUNTIME_BUNDLE_BUILD_OK'} ` +
    `authenticated=${result.authenticated ? 'yes' : 'no'} ` +
    `bundleId=${result.bundleId} bridgeSize=${result.bridgeSize} hookSize=${result.hookSize} ` +
    `bundleSize=${result.bundleSize} bridgeSha256=${result.bridgeSha256} ` +
    `hookSha256=${result.hookSha256} bundleSha256=${result.bundleSha256}\n`
  );
  return result;
}

module.exports = {
  buildRuntimeBundle,
  decodeRuntimeBundleFile,
  defaultBridgeInput,
  defaultHookInput,
  defaultInput: defaultBridgeInput,
  defaultOutput,
  main,
  verifyRuntimeBundle
};

if (require.main === module) {
  main().catch((error) => {
    process.stderr.write(`RUNTIME_BUNDLE_ERROR ${error?.message || String(error)}\n`);
    process.exitCode = 1;
  });
}
