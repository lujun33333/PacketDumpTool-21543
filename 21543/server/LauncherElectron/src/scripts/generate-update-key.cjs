const { generateKeyPairSync } = require('node:crypto');
const { mkdir, stat, writeFile } = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index];
    const value = argv[index + 1];
    if (!key?.startsWith('--') || !value) {
      throw new Error('参数格式错误。');
    }
    args[key.slice(2)] = value;
  }
  return args;
}

async function exists(filePath) {
  try {
    return (await stat(filePath)).isFile();
  } catch (error) {
    if (error?.code === 'ENOENT') return false;
    throw error;
  }
}

async function main() {
  const projectRoot = path.resolve(__dirname, '..', '..');
  const args = parseArgs(process.argv.slice(2));
  const privateKeyPath = path.resolve(
    args['private-key'] || path.join(os.homedir(), '.qqspeed-launcher', 'update-signing-private.pem')
  );
  const publicKeyPath = path.resolve(
    args['public-key'] || path.join(projectRoot, 'resources', 'update-public-key.pem')
  );
  if (await exists(privateKeyPath) || await exists(publicKeyPath)) {
    throw new Error('更新签名密钥已存在，已拒绝覆盖或静默轮换。');
  }

  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  await mkdir(path.dirname(privateKeyPath), { recursive: true });
  await mkdir(path.dirname(publicKeyPath), { recursive: true });
  await writeFile(privateKeyPath, privateKey.export({ type: 'pkcs8', format: 'pem' }), {
    mode: 0o600,
    flag: 'wx'
  });
  await writeFile(publicKeyPath, publicKey.export({ type: 'spki', format: 'pem' }), {
    mode: 0o644,
    flag: 'wx'
  });
  process.stdout.write(`[update] private signing key created at ${privateKeyPath}\n`);
  process.stdout.write(`[update] public verification key created at ${publicKeyPath}\n`);
  process.stdout.write('[update] back up the private key offline; never copy it to the update server or installer.\n');
}

main().catch((error) => {
  process.stderr.write(`[update] ${error.message}\n`);
  process.exitCode = 1;
});
