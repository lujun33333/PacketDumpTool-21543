const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const electronExecutable = require('electron');
const afterPack = require('../build/after-pack.cjs');
const { getCurrentFuseWire } = require('@electron/fuses');

async function main() {
  assert.equal(process.platform, 'win32', 'Fuse smoke test currently expects the Windows Electron executable.');
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'qqspeed-fuses-'));
  const productFilename = 'QQSpeedLauncherFuseSmoke';
  const executablePath = path.join(tempDir, `${productFilename}.exe`);

  try {
    fs.copyFileSync(electronExecutable, executablePath);
    await afterPack({
      electronPlatformName: 'win32',
      appOutDir: tempDir,
      packager: {
        appInfo: { productFilename },
        projectDir: path.resolve(__dirname, '..', '..')
      }
    });
    const wire = await getCurrentFuseWire(executablePath);
    afterPack.assertFuseWire(wire);
    console.log('FUSES_SMOKE_OK');
  } finally {
    if (fs.existsSync(executablePath)) {
      fs.unlinkSync(executablePath);
    }
    fs.rmdirSync(tempDir);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
