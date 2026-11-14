import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const buildScript = fileURLToPath(new URL('../../src/scripts/build-runtime-bundle.cjs', import.meta.url));

function peFixture(size, multiplier) {
  const bytes = Buffer.alloc(size);
  bytes.write('MZ', 0, 'ascii');
  for (let index = 2; index < bytes.length; index += 1) bytes[index] = (index * multiplier + 3) & 0xff;
  return bytes;
}

const buildSecret = Object.freeze({
  contentKey: Buffer.from('41'.repeat(32), 'hex'),
  bundleId: Buffer.from('52'.repeat(32), 'hex')
});

test('bundle builder consumes explicit Bridge, Hook and secret inputs while verify-only never rewrites them', async (t) => {
  const { buildRuntimeBundle, verifyRuntimeBundle } = require(buildScript);
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-bundle-build-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const bridgeInput = join(root, 'bridge.bin');
  const hookInput = join(root, 'hook.bin');
  const output = join(root, 'runtime.bin');
  writeFileSync(bridgeInput, peFixture(32768, 19));
  writeFileSync(hookInput, peFixture(16384, 23));

  const built = await buildRuntimeBundle({ bridgeInput, hookInput, output, ...buildSecret });
  const firstBytes = readFileSync(output);
  const beforeBridge = statSync(bridgeInput);
  const beforeHook = statSync(hookInput);
  const beforeOutput = statSync(output);
  const verified = await verifyRuntimeBundle({ bridgeInput, hookInput, output, ...buildSecret });
  const inspected = await verifyRuntimeBundle({ bridgeInput, hookInput, output });

  assert.equal(verified.authenticated, true);
  assert.equal(inspected.authenticated, false);
  assert.equal(built.bridgeSha256, verified.bridgeSha256);
  assert.equal(built.bundleId, inspected.bundleId);
  assert.equal(built.hookSha256, verified.hookSha256);
  assert.equal(built.bundleSha256, verified.bundleSha256);
  assert.deepEqual(readFileSync(output), firstBytes);
  assert.equal(statSync(bridgeInput).mtimeMs, beforeBridge.mtimeMs);
  assert.equal(statSync(hookInput).mtimeMs, beforeHook.mtimeMs);
  assert.equal(statSync(output).mtimeMs, beforeOutput.mtimeMs);
});

test('bundle builder accepts release material only as in-memory Buffers', async (t) => {
  const builder = require(buildScript);
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-bundle-secret-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const bridgeInput = join(root, 'bridge.bin');
  const hookInput = join(root, 'hook.bin');
  const output = join(root, 'runtime.bin');
  writeFileSync(bridgeInput, peFixture(32768, 19));
  writeFileSync(hookInput, peFixture(16384, 23));

  assert.equal(Object.hasOwn(builder, 'loadBuildSecret'), false);
  assert.equal(Object.hasOwn(builder, 'BUILD_SECRET_ENV'), false);
  await assert.rejects(
    () => builder.buildRuntimeBundle({ bridgeInput, hookInput, output }),
    /in-memory|contentKey|bundleId|32-byte Buffer/i
  );
  await assert.rejects(
    () => builder.decodeRuntimeBundleFile({ output }),
    /in-memory|contentKey|bundleId|32-byte Buffer/i
  );
});

test('bundle verify-only rejects tampered output without repairing it', async (t) => {
  const { buildRuntimeBundle, verifyRuntimeBundle } = require(buildScript);
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-bundle-verify-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const bridgeInput = join(root, 'bridge.bin');
  const hookInput = join(root, 'hook.bin');
  const output = join(root, 'runtime.bin');
  writeFileSync(bridgeInput, peFixture(32768, 19));
  writeFileSync(hookInput, peFixture(16384, 23));
  await buildRuntimeBundle({ bridgeInput, hookInput, output, ...buildSecret });
  const tampered = readFileSync(output);
  tampered[tampered.length - 1] ^= 1;
  writeFileSync(output, tampered);
  const before = readFileSync(output);

  await assert.rejects(
    () => verifyRuntimeBundle({ bridgeInput, hookInput, output, ...buildSecret }),
    /authentication|tampered|verify|mismatch/i
  );
  assert.deepEqual(readFileSync(output), before);
});

test('bundle verify-only rejects a valid ciphertext whose bundleId is not the supplied release identity', async (t) => {
  const { buildRuntimeBundle, verifyRuntimeBundle } = require(buildScript);
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-bundle-identity-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const bridgeInput = join(root, 'bridge.bin');
  const hookInput = join(root, 'hook.bin');
  const output = join(root, 'runtime.bin');
  writeFileSync(bridgeInput, peFixture(32768, 19));
  writeFileSync(hookInput, peFixture(16384, 23));
  await buildRuntimeBundle({ bridgeInput, hookInput, output, ...buildSecret });

  await assert.rejects(
    () => verifyRuntimeBundle({
      bridgeInput,
      hookInput,
      output,
      contentKey: buildSecret.contentKey,
      bundleId: Buffer.from('53'.repeat(32), 'hex')
    }),
    /bundleId|release identity|mismatch/i
  );
});
