const { copyFile, mkdir, readFile, rename, rm, stat, writeFile } = require('node:fs/promises');
const { constants, createReadStream } = require('node:fs');
const { createHash, sign } = require('node:crypto');
const path = require('node:path');

const VERSION_PATTERN = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const SAFE_INSTALLER_NAME = /^[A-Za-z0-9][A-Za-z0-9._-]{0,199}\.exe$/i;

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index];
    const value = argv[index + 1];
    if (!key?.startsWith('--') || !value) {
      throw new Error('参数格式错误。请使用 --installer、--output、--private-key 和 --version。');
    }
    args[key.slice(2)] = value;
  }
  return args;
}

async function sha256File(filePath) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(filePath)) {
    hash.update(chunk);
  }
  return hash.digest('hex');
}

function canonicalPayload(manifest) {
  return Buffer.from(JSON.stringify({
    version: manifest.version,
    required: manifest.required,
    file: manifest.file,
    sha256: manifest.sha256
  }), 'utf8');
}

async function publishUpdate({ installer, output, privateKeyPath, version }) {
  if (!VERSION_PATTERN.test(String(version || ''))) {
    throw new Error('版本号必须是 x.y.z 格式。');
  }
  const installerPath = path.resolve(installer);
  const outputPath = path.resolve(output);
  const file = path.basename(installerPath);
  if (!SAFE_INSTALLER_NAME.test(file)) {
    throw new Error('安装包文件名只能包含字母、数字、点、下划线和连字符，并且必须是 .exe。');
  }
  const info = await stat(installerPath);
  if (!info.isFile() || info.size <= 0) {
    throw new Error('安装包不存在或为空。');
  }

  const privateKey = await readFile(path.resolve(privateKeyPath), 'utf8');
  const sha256 = await sha256File(installerPath);
  const manifest = { version, required: true, file, sha256 };
  manifest.signature = sign(null, canonicalPayload(manifest), privateKey).toString('base64');

  await mkdir(outputPath, { recursive: true });
  const finalInstaller = path.join(outputPath, file);
  const partialInstaller = `${finalInstaller}.uploading`;
  const manifestPath = path.join(outputPath, 'update.json');
  const partialManifest = `${manifestPath}.tmp`;
  await rm(partialInstaller, { force: true });
  await rm(partialManifest, { force: true });
  try {
    await copyFile(installerPath, partialInstaller, constants.COPYFILE_EXCL);
    await rm(finalInstaller, { force: true });
    await rename(partialInstaller, finalInstaller);
    await writeFile(partialManifest, `${JSON.stringify(manifest, null, 2)}\n`, {
      encoding: 'utf8',
      flag: 'wx'
    });
    await rm(manifestPath, { force: true });
    await rename(partialManifest, manifestPath);
  } catch (error) {
    await rm(partialInstaller, { force: true }).catch(() => {});
    await rm(partialManifest, { force: true }).catch(() => {});
    throw error;
  }
  return { manifest, outputPath };
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const result = await publishUpdate({
    installer: args.installer,
    output: args.output,
    privateKeyPath: args['private-key'],
    version: args.version
  });
  process.stdout.write(
    `[update] published ${result.manifest.file} version ${result.manifest.version} SHA256 ${result.manifest.sha256}\n`
  );
}

if (require.main === module) {
  main().catch((error) => {
    process.stderr.write(`[update] ${error.message}\n`);
    process.exitCode = 1;
  });
}

module.exports = { canonicalPayload, parseArgs, publishUpdate, sha256File };
