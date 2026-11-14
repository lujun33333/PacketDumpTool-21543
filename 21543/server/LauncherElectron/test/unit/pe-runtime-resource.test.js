import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { copyFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { readEmbeddedRuntimeBundle } from '../../src/main/executable-runtime-resource.js';

const require = createRequire(import.meta.url);
const resourceModule = fileURLToPath(new URL('../../src/build/pe-runtime-resource.cjs', import.meta.url));

test('PE runtime resource writer embeds and replaces exact RT_RCDATA bytes without a sidecar', {
  skip: process.platform !== 'win32'
}, (t) => {
  const {
    RUNTIME_RESOURCE_ID,
    readPeRuntimeResource,
    writePeRuntimeResource
  } = require(resourceModule);
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-pe-resource-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const executable = join(root, 'fixture.exe');
  copyFileSync(process.execPath, executable);
  const first = Buffer.from('first-encrypted-runtime-bundle');
  const second = Buffer.from('second-encrypted-runtime-bundle-with-more-bytes');

  assert.equal(RUNTIME_RESOURCE_ID, 301);
  writePeRuntimeResource(executable, first);
  assert.deepEqual(readPeRuntimeResource(executable), first);
  assert.deepEqual(readEmbeddedRuntimeBundle(executable), first);

  writePeRuntimeResource(executable, second);
  assert.deepEqual(readPeRuntimeResource(executable), second);
  assert.deepEqual(readEmbeddedRuntimeBundle(executable), second);
});

test('PE runtime resource reader rejects executables without resource 301', {
  skip: process.platform !== 'win32'
}, (t) => {
  const root = mkdtempSync(join(tmpdir(), 'qqspeed-pe-resource-missing-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const executable = join(root, 'fixture.exe');
  copyFileSync(process.execPath, executable);

  assert.throws(() => readEmbeddedRuntimeBundle(executable), /resource|301|missing/i);
});
