import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const serverRoot = resolve(import.meta.dirname, '..', '..', '..', 'Server');
const userServer = readFileSync(resolve(serverRoot, 'UserServer.cpp'), 'utf8');
const runtimeSource = readFileSync(resolve(serverRoot, 'LauncherRuntimeMaterial.cpp'), 'utf8');
const runtimeHeader = readFileSync(resolve(serverRoot, 'LauncherRuntimeMaterial.h'), 'utf8');

test('server routes anonymous MsgID3 v2 without entering the account database path', () => {
  const start = userServer.indexOf('void MyRequestRuntimeMaterial(');
  const end = userServer.indexOf('\nvoid MyResponseRegister', start);
  const handler = userServer.slice(start, end);
  assert.ok(start >= 0 && end > start);
  assert.match(handler, /Body\[0\]\s*==\s*launcher_runtime::kPreloginProtocolVersion/);
  assert.match(handler, /preloginRequest[\s\S]*?IssuePrelogin\(/);
  assert.match(handler, /authenticatedUin[\s\S]*?\.Issue\(/);
  assert.doesNotMatch(handler, /UserDB_/);
});

test('prelogin v2 is isolated from authenticated v1 and restricted to the configured bundle', () => {
  assert.match(runtimeHeader, /kProtocolVersion\s*=\s*1/);
  assert.match(runtimeHeader, /kPreloginProtocolVersion\s*=\s*2/);
  assert.match(runtimeHeader, /bool IssuePrelogin\(/);
  assert.match(runtimeSource, /kPlaintextMagic\s*\{\s*'Q',\s*'Q',\s*'S',\s*'R',\s*'M',\s*'A',\s*'T',\s*'1'/);
  assert.match(runtimeSource, /kPreloginPlaintextMagic\s*\{\s*'Q',\s*'Q',\s*'S',\s*'R',\s*'M',\s*'A',\s*'T',\s*'2'/);
  assert.match(runtimeSource, /IssuePrelogin[\s\S]*?IssueInternal\(\s*kPreloginProtocolVersion,\s*0,\s*true,/s);
});
