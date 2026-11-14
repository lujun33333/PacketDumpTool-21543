const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..', '..');
const read = (relativePath) => fs.readFileSync(path.join(root, relativePath), 'utf8');
const assert = (condition, message) => { if (!condition) throw new Error(message); };

const endpoint = read('src/main/locked-endpoint.js');
assert(endpoint.includes("'127.0.0.1'") && endpoint.includes('28880'),
  'Electron endpoint host/port is not locked.');
assert(endpoint.includes("passwordMode: 'md5'"),
  'Electron password mode does not match the current private-server configuration.');

const protocol = read('src/main/login/private-protocol.js');
assert(protocol.includes('LEGACY_PASSWORD_CONSTANTS') &&
       protocol.includes('vector<unsigned int>::resize(paddedByteLength + 8)'),
  'Electron no longer carries the deployed Launcher/Server password-digest compatibility contract.');

const worker = read('src/main/native-worker.js');
assert(worker.includes("from './locked-endpoint.js'"),
  'The JS utility process must own the private-login endpoint.');
assert(worker.includes('passwordMode: LOCKED_LOGIN_ENDPOINT.passwordMode'),
  'The JS utility process does not apply the locked password mode.');
assert(/applyAuth\(authResult\.uin,\s*gameKeyHex,\s*authResult\.signKey\)/.test(worker),
  'The JS utility process does not pass the selected wire sign key to the native bridge.');
assert(worker.includes('transientSecrets.trackPassword(credentials.password)') &&
       worker.includes('transientSecrets.track(authResult.signKey, gameKeyHex)') &&
       worker.includes('transientSecrets.clear()'),
  'The JS utility process does not bound authentication-secret redaction to the login attempt.');

const project = read('native/NativeBridge/NativeBridge.vcxproj');
for (const legacySource of ['PrivateLogin.cpp', 'QqTea.c', 'MD5.cpp', 'LockedEndpoint.h']) {
  assert(!project.includes(legacySource), `Native production build still includes ${legacySource}.`);
}

const bridge = read('native/NativeBridge/NativeBridge.cpp');
assert(/QQSpeedNativeBridge_BeginSession\s*\([\s\S]*?hookHost[\s\S]*?hookPort/.test(bridge),
  'Native bridge does not receive the Hook endpoint from the utility process.');
assert(!bridge.includes('QQSpeedNativeBridge_SubmitCredentials'),
  'Native bridge still exports the old credential/login entry point.');

const ipc = read('native/NativeBridge/NativeBridgeIpc.cpp');
assert(ipc.includes('ConfigureSpeedHookTarget'),
  'Native Hook adapter has no explicit runtime target configuration.');

for (const relativePath of [
  'package.json',
  'src/main/native-core.js',
  'src/scripts/build-native.cjs',
  'src/scripts/verify-hook.cjs'
]) {
  assert(!read(relativePath).includes('DirSvrInfo1.ini'), `${relativePath} still packages DirSvrInfo1.ini.`);
}
assert(!fs.existsSync(path.join(root, 'resources', 'native', 'DirSvrInfo1.ini')),
  'DirSvrInfo1.ini still exists in production resources.');
assert(!fs.existsSync(path.join(root, 'resources', 'native', 'runtime-artifacts.json')),
  'Retired runtime-artifacts.json still exists in production resources.');

console.log('smoke-locked-endpoint: PASS');
