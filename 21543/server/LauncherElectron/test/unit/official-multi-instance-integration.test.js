import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const root = new URL('../../', import.meta.url);
const [main, preload, html, app, styles] = await Promise.all([
  readFile(new URL('src/main/index.js', root), 'utf8'),
  readFile(new URL('src/preload/preload.cjs', root), 'utf8'),
  readFile(new URL('src/renderer/index.html', root), 'utf8'),
  readFile(new URL('src/renderer/app.js', root), 'utf8'),
  readFile(new URL('src/renderer/styles.css', root), 'utf8')
]);

test('main and preload expose authoritative multi-instance snapshot, bind, unbind and start contracts', () => {
  for (const channel of [
    'launcher:get-instances',
    'launcher:bind-instance-account',
    'launcher:unbind-instance-account',
    'launcher:start-instance'
  ]) {
    assert.match(main, new RegExp(channel.replaceAll(':', '\\:')));
    assert.match(preload, new RegExp(channel.replaceAll(':', '\\:')));
  }
  assert.match(main, /GameInstanceManager/);
  assert.match(main, /utilityProcess\.fork/);
  assert.match(preload, /launcher:instances/);
});

test('renderer contains the official multi-open entry, viewport-bounded management modal and real account controls', () => {
  for (const id of [
    'multiOpenBtn',
    'multiOpenCount',
    'multiInstanceModal',
    'multiInstanceList',
    'bindAccountBtn',
    'bindAccountForm',
    'bindAccountUser',
    'bindAccountPassword'
  ]) {
    assert.match(html, new RegExp(`id=["']${id}["']`));
  }
  assert.match(html, />多开管理</);
  assert.match(html, />\+\s*绑定账号</);
  assert.match(app, /api\.getInstances\(/);
  assert.match(app, /api\.startInstance\(/);
  assert.match(app, /api\.bindInstanceAccount\(/);
  assert.match(styles, /\.multi-instance-panel[\s\S]*?width:\s*min\(92vw,\s*720px\)[\s\S]*?height:\s*min\(88vh,\s*750px\)/);
});

test('multi-open UI renders only main-process instance state and never fabricates a running PID', () => {
  assert.match(app, /function renderMultiInstanceSnapshot/);
  assert.match(app, /api\.onInstancesChanged/);
  assert.doesNotMatch(app, /Math\.random\(\)[\s\S]{0,120}(pid|gameProcessId)/i);
  assert.doesNotMatch(app, /setInterval\([\s\S]{0,160}(running|gameProcessId|pid)/i);
});
