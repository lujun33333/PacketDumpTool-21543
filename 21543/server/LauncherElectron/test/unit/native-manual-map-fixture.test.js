import assert from 'node:assert/strict';
import {
  existsSync,
  mkdtempSync,
  readFileSync,
  rmSync
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const fixtureRoot = join(root, 'test', 'fixtures', 'native-manual-map');

function findMsBuild() {
  if (process.env.MSBUILD_EXE_PATH && existsSync(process.env.MSBUILD_EXE_PATH)) {
    return process.env.MSBUILD_EXE_PATH;
  }

  const programFiles = process.env.ProgramFiles || 'C:\\Program Files';
  for (const edition of ['Community', 'BuildTools', 'Professional', 'Enterprise']) {
    const candidate = join(
      programFiles,
      'Microsoft Visual Studio',
      '2022',
      edition,
      'MSBuild',
      'Current',
      'Bin',
      'MSBuild.exe'
    );
    if (existsSync(candidate)) return candidate;
  }

  const programFilesX86 = process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)';
  const vswhere = join(programFilesX86, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
  if (!existsSync(vswhere)) return null;

  const found = spawnSync(vswhere, [
    '-latest',
    '-products',
    '*',
    '-requires',
    'Microsoft.Component.MSBuild',
    '-find',
    'MSBuild\\**\\Bin\\MSBuild.exe'
  ], { encoding: 'utf8', windowsHide: true });
  if (found.status !== 0) return null;
  return found.stdout.split(/\r?\n/u).map((line) => line.trim()).find(existsSync) || null;
}

function commandEvidence(result) {
  return [result.stdout, result.stderr].filter(Boolean).join('\n');
}

test('x64 PE loader lifecycle works in an owned blank child process', (t) => {
  if (process.platform !== 'win32' || process.arch !== 'x64') {
    t.skip('requires Windows x64');
    return;
  }

  const msbuild = findMsBuild();
  if (!msbuild) {
    t.skip('MSBuild with the Visual C++ x64 toolchain is unavailable');
    return;
  }

  const project = join(fixtureRoot, 'NativeManualMapFixtures.proj');
  assert.equal(existsSync(project), true, `missing native fixture project: ${project}`);

  const buildRoot = mkdtempSync(join(tmpdir(), 'native-manual-map-fixture-'));
  try {
    const build = spawnSync(msbuild, [
      project,
      '/t:Build',
      '/m',
      '/nologo',
      '/v:minimal',
      '/p:Configuration=Release',
      '/p:Platform=x64',
      `/p:NativeFixtureRoot=${buildRoot}\\`
    ], {
      cwd: root,
      encoding: 'utf8',
      timeout: 120_000,
      windowsHide: true
    });
    assert.equal(build.status, 0, `native fixture build failed:\n${commandEvidence(build)}`);

    const out = join(buildRoot, 'out');
    const harness = join(out, 'NativeManualMapHarness.exe');
    const child = join(out, 'NativeManualMapChild.exe');
    const payload = join(out, 'NativeManualMapFixture.dll');
    for (const path of [harness, child, payload]) {
      assert.equal(existsSync(path), true, `missing native fixture output: ${path}`);
    }

    const fixtureBytes = readFileSync(payload);
    assert.ok(fixtureBytes.length > 0, 'fixture PE must be non-empty');

    const run = spawnSync(harness, [child, payload], {
      cwd: out,
      encoding: 'utf8',
      timeout: 60_000,
      windowsHide: true
    });
    assert.equal(run.status, 0, `native fixture harness failed:\n${commandEvidence(run)}`);
    assert.match(run.stdout, /PASS lifecycle=0x[0-9a-f]+ rollback=MEM_FREE child_exit=0 payload_file=absent/iu);
    assert.equal(existsSync(payload), false, 'payload file must be absent before and after mapping');
  } finally {
    rmSync(buildRoot, { recursive: true, force: true });
  }
});
